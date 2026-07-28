/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Endless mode — see endless.h.
 *
 * The run plays real, UNMODIFIED shipped levels in a random cross-episode order.
 * Between levels the player visits an OUTPOST (bank interest, reroll the shop, buy
 * hull upgrades, then the standard item shop) and CHARTS A COURSE: a branching
 * choice of the next sector, each option carrying its own risk/reward MUTATORS.
 * Difficulty ramps through depth-scaled enemy stats (HP / boss HP / fire rate /
 * projectile speed) plus whatever mutators the chosen sector adds. The run ends on
 * death; only the depth reached (a high score) persists.
 *
 * This file holds the run state, the run lifecycle (reset / kill counting / the
 * per-tick modifiers) and the zone milestones. The rest of endless mode lives in
 * the sibling endless_*.c files -- endless_internal.h maps out which owns what.
 */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"        // difficultyLevel, DIFFICULTY_*, player-independent globals
#include "custom_weapon.h" // customWeaponPort / customSidekickSlot (reserved shop slots)
#include "episodes.h"      // item arrays + SHIP_NUM/PORT_NUM/... counts, episodeAvail, JE_initEpisode
#include "fonthand.h"      // JE_outText
#include "keyboard.h"      // newkey/lastkey_scan/keysactive, service_SDL_events
#include "loudness.h"      // fade_song
#include "mainint.h"       // JE_getCost
#include "mtrand.h"        // mt_rand
#include "nortsong.h"      // JE_playSampleNum, setDelay, wait_delayorinput, limit_render_fps
#include "nortvars.h"      // JE_anyButton
#include "palette.h"       // colors, palettes, fade_palette, fade_black
#include "pcxload.h"       // JE_loadPCX (the run-end ship illustration)
#include "player.h"        // player[]
#include "sprite.h"        // JE_loadCompShapes, enemySpriteSheets, shopSpriteSheet
#include "tyrian2.h"       // itemAvail, itemAvailMax
#include "varz.h"          // eventRec, maxEvent, map* globals
#include "video.h"         // VGAScreen/VGAScreen2, JE_showVGA, output_vsync

#include <stdio.h>
#include <string.h>

// --- Run state ------------------------------------------------------------------

int      endlessRunDepth  = 0;   // levels cleared this run (0 on the first level)
Uint64   endlessActiveMods = 0;  // ENDLESS_MOD_* bits for the current level (64-bit: TOPSY/SLUGGISH use bits 32-33)
int      endlessArmorBonus = 0;  // run-persistent +max armor bought at the outpost
int      endlessRunKills = 0;    // total enemies destroyed this run (shown on the end screen)
int      endlessRunBossKills = 0;// boss-tier enemies destroyed this run

// Per-zone timers (reset each zone): elapsed ticks drive ENRAGE; the turbodrive timer counts
// down the quickened-fire window after each kill. Advanced by endlessGameplayTick.
int endlessZoneTicks      = 0;
int endlessTurbodriveTimer = 0;
int endlessRetaliationTimer = 0;   // RETALIATION: ticks left in the quickened-enemy-fire window (refreshed each kill; read by endlessFireDelayPercent)

// Boons whose reward is banked on CLEAR and paid out at the NEXT outpost (see endlessOnSectorCleared).
bool endlessStarChartsOwed  = false;  // STAR CHARTS: the next ordinary chart deals its full route slate
int  endlessBreakthroughOwed = 0;     // BREAKTHROUGH: bonus perk picks owed (a counter, so two can queue up)
static bool endlessArmorHudDirty = false;  // set when the Overheat DoT shaves hull; the game loop repaints the (event-driven) armor bar

// --- Milestone zones -------------------------------------------------------------------------
// Set-pieces: Chart-a-Course offers a full slate of five high-tier sectors and nothing else, and
// the outpost before it plays the "Parlance" warning theme. Three cadences -- minor (25, 75, ...)
// charts S/S+ on a pinned track, plain (50, 150, ...) charts S+/S++, GRAND (every 100th) charts
// S++/S+++ and always includes "The End". Every class grants a guaranteed perk pick on clear, since
// the slate is unavoidable. Keyed off the REAL zone, so the numbers match the HUD.
#define ENDLESS_MILESTONE_EVERY 50
#define ENDLESS_MILESTONE_GRAND 100

// Milestone class of an arbitrary ZONE number: 0 = ordinary, 1 = the S+/S++ milestone (50, 150, ...),
// 2 = the S++/S+++ one (100, 200, ...), 3 = the minor "Tunneling Trolls" S/S+ milestone that falls
// halfway between the others (25, 75, 125, 175, ...). The kinds are tags, not an ordinal -- kind 3 is
// the MILDEST despite its number. Taking the zone as a parameter (rather than reading the run depth)
// is what lets the music picker look at a zone's NEIGHBOURS without any RNG.
int endlessMilestoneKindOfZone(int zone)
{
	if (zone <= 0)
		return 0;
	if (zone % ENDLESS_MILESTONE_GRAND == 0)
		return 2;
	if (zone % ENDLESS_MILESTONE_EVERY == 0)
		return 1;
	if (zone % ENDLESS_MILESTONE_EVERY == ENDLESS_MILESTONE_EVERY / 2)
		return 3;   // 25, 75, 125, ... -- one 50-cycle offset from the S+/S++ milestone
	return 0;
}

// The zone about to be charted / played (the run depth counts zones CLEARED).
int endlessMilestoneKind(void)
{
	return endlessMilestoneKindOfZone(endlessRunDepth + 1);
}

// Was run depth `depth` a PERK-GRANTING milestone? Every class is (25, 50, 75, 100, ...): each one
// forces its slate on the player, so each one pays. (A run depth IS the zone just cleared, so this
// tests "the outpost I'm standing in follows a milestone".)
static bool endlessPerkMilestoneAt(int depth)
{
	return endlessMilestoneKindOfZone(depth) != 0;
}

// Forced perk picks come on a fixed cadence: after the first cleared zone, then every 4th zone
// (depths 1, 5, 9, ...). Perks are strong, so they stay sparing.
#define ENDLESS_PERK_EVERY 4

// Each milestone class flies to its OWN pinned track, so the two set-pieces stay distinct. Both are
// 1-based into musicTitle[] (musmast.c), matching levelSong -- the level start plays levelSong - 1.
#define ENDLESS_MILESTONE_SONG_GRAND 35  // "One Mustn't Fall"  -- every 100th zone
#define ENDLESS_MILESTONE_SONG_PLAIN 37  // "A Field for Mag"   -- the other 50th zones (50, 150, 250, ...)
#define ENDLESS_MILESTONE_SONG_MINOR 17  // "Tunneling Trolls"  -- the minor milestone (25, 75, 125, ...)

// The pinned track for a milestone class, or 0 for an ordinary zone.
JE_byte endlessMilestoneSong(int kind)
{
	return (kind == 2) ? ENDLESS_MILESTONE_SONG_GRAND
	     : (kind == 1) ? ENDLESS_MILESTONE_SONG_PLAIN
	     : (kind == 3) ? ENDLESS_MILESTONE_SONG_MINOR
	     : 0;
}

// Is a forced perk pick due at the outpost for run depth `depth` (the zone just cleared)? Three
// reasons: the cadence above; a cleared MILESTONE zone; or the zone right after a depth where those
// two COLLIDED -- the second perk is deferred by a zone rather than swallowed, and the cadence
// carries on from its own schedule (notes.md §Economy & perk plumbing). Derived purely from the
// depth, so it needs no persisted state and survives a save/reload or a mid-zone bail unchanged.
bool endlessPerkDueAtDepth(int depth)
{
	if (depth <= 0)
		return false;
	if (depth % ENDLESS_PERK_EVERY == 1 || endlessPerkMilestoneAt(depth))
		return true;
	const int prev = depth - 1;  // deferred half of a collision on the previous zone
	return endlessPerkMilestoneAt(prev) && prev % ENDLESS_PERK_EVERY == 1;
}

// How wide the scheduled pick at run depth `depth` is: a cleared milestone deals the bigger slate.
// Off the depth like the predicate above, so it survives a save/reload. A collision's deferred half
// lands on the zone AFTER the milestone -- an ordinary depth -- so it deals the ordinary three.
int endlessPerkOffersAtDepth(int depth)
{
	return endlessMilestoneKindOfZone(depth) ? ENDLESS_PERK_OFFERS_MILESTONE : ENDLESS_PERK_OFFERS;
}

// Hardcore mode for the current run (see endless.h): no saving at all + a locked outpost on a
// mid-zone bail. Chosen on the seed screen, applied in newEndlessGame, cleared by endlessResetRun.
bool endlessHardcore = false;

// --- All-time record: the furthest zone ever reached -----------------------------------------
// Deliberately NOT reset by endlessResetRun -- it is the one thing that survives a run. It lives
// in opentyrian.cfg (see endlessRecordConfigSave): endless.sav is per-slot, and a hardcore run
// never writes one, so the record would be lost exactly where it matters most.
int endlessBestZone = 0;                   // furthest zone ever reached, over every run (0 = none yet)
static int endlessBestZoneAtRunStart = 0;  // where the record stood when this run began

#define ENDLESS_BEST_ZONE_MAX 99999  // a sanity ceiling on what a hand-edited config can claim

// Called at every endless level start: the zone being launched is a zone REACHED. Written straight
// through to the config, so quitting the process at zone 60 doesn't cost the record.
void endlessNoteZoneReached(int zone)
{
	if (!endlessMode || zone <= endlessBestZone || zone > ENDLESS_BEST_ZONE_MAX)
		return;
	endlessBestZone = zone;
	save_opentyrian_config();
}

// Snapshot the record for the run-over screen's "(+n)". Called when a run begins and when one is
// resumed from a save -- NOT from endlessResetRun, so a Quit Level bail (which re-applies a run
// snapshot) doesn't quietly zero out the gain the run has already earned.
void endlessRecordRunStart(void) { endlessBestZoneAtRunStart = endlessBestZone; }
int  endlessBestZoneAtStart(void) { return endlessBestZoneAtRunStart; }

void endlessResetRun(void)
{
	endlessRunDepth   = 0;
	endlessActiveMods = 0;
	endlessArmorBonus = 0;
	endlessRunKills   = 0;
	endlessRunBossKills = 0;
	endlessPurchasedMods = 0;
	endlessBuffKind = 0;
	endlessBuffCooldownUntil = 0;  // kill-fire recharge: fresh run, no lock
	endlessOverdriveStacks = 0;
	endlessComboKills = 0;
	endlessPerkPending = false;
	endlessStarChartsOwed = false;   // fresh run: nothing owed from a cleared Star Charts / Breakthrough sector
	endlessBreakthroughOwed = 0;
	endlessPerkChoiceN = 0;
	endlessPerkDepthDone = -1;
	endlessResumeVisit = false;
	endlessCreditsShown = false;   // fresh run: the zone-100 credits are still ahead (a resume reloads this in endlessApplyCurrent)
	endlessLastSong = 0;           // no zone has played yet, so nothing for the music anti-repeat to avoid
	endlessLastSongDepth = -1;
	endlessRegenTick = 0;
	endlessSalvoIdle = ENDLESS_PERK_SALVO_IDLE;  // Opening Salvo: fresh run opens with the volley charged (see endlessResetZonePerkTimers)
	endlessSalvoWindow = 0;                      // ...and with no salvo already being spent
	endlessCmCooldown = 0;  // Countermeasure Suite: fresh run, first burst ready
	endlessBuffCharge = 0;
	endlessReviveHeld = false;
	endlessRevivesUsed = 0;
	endlessCleanseChargeCount = 0;
	endlessGamblePerkWon = false;
	endlessShopTax = 0;         // gamble state: clear any lingering debt tax / rigged flag / long con
	endlessGambleRigged = false;
	endlessLongCon = 0;
	endlessLockedSortie = false;   // no locked "gave up" outpost on a fresh run
	endlessQuitToOutpost = false;
	endlessSortieHave = false;     // no launch-time snapshot yet
	endlessSortiePrePurchased = 0; // no pre-pick one-shots stashed yet
	endlessSortiePreCleanse = 0;
	endlessSortiePreLongCon = 0;
	// Default to non-hardcore. newEndlessGame overrides this from the seed screen right after the
	// reset; a save/resume (endlessApplyCurrent runs this reset) correctly leaves it cleared, since
	// hardcore runs never save and so are never resumed.
	endlessHardcore = false;
	endlessBaseName[0] = endlessPrevBaseName[0] = '\0';  // crash-log base-level history: fresh run
	endlessBaseEp = endlessBaseLvl = endlessPrevBaseEp = endlessPrevBaseLvl = 0;
	endlessRecentCount = 0;  // anti-repeat ring: fresh run (a resume reloads it in endlessApplyCurrent)
	player[0].superbombs = 0;  // fresh run: no bombs (they persist across levels within a run)
	memset(endlessPerkOwned, 0, sizeof(endlessPerkOwned));
	endlessSetSeed("");  // safe default; newEndlessGame / a resume load sets the real run seed next
}

// Arm the debug campaign-mods effect layer from a clean slate. Perks and mod bits are LEFT ALONE
// -- they are what the debug screen exists to set. What gets cleared is state that can only be
// BOUGHT at an outpost (hull upgrade, buff charge, revive token), which a campaign game has no shop
// for and would otherwise inherit from a previous run. No-op during a real run.
void endlessCampaignModsArm(void)
{
	if (endlessMode)
		return;
	endlessArmorBonus = 0;
	endlessBuffCharge = 0;
	endlessBuffKind = 0;
	endlessPurchasedMods = 0;
	endlessOverdriveStacks = 0;
	endlessComboKills = 0;
	endlessReviveHeld = false;
	endlessCleanseChargeCount = 0;
	endlessShopTax = 0;
	endlessStarChartsOwed = false;
	endlessBreakthroughOwed = 0;
	endlessPerkPending = false;   // no outpost to spend a pick at
}

void endlessCountKill(int linknum)
{
	if (!endlessFxActive())
		return;

	// A multi-part enemy is several enemy[] slots sharing one nonzero linknum, all removed
	// consecutively in a single kill loop, so count the whole enemy once, not once per tile
	// (else the run tally and the Overdrive stack balloon). Lone enemies are linknum 0 and always
	// counted. Two live enemies never share a linknum, so deduping consecutive same-linknum calls
	// is safe.
	static int lastCountedLink = 0;
	if (linknum != 0 && linknum == lastCountedLink)
		return;
	lastCountedLink = linknum;

	++endlessRunKills;
	// Boss kills are tallied in draw_boss_bar (when a boss's health bar empties), so the "Bosses
	// slain" stat counts only real bar-spawning bosses. Do NOT count them here off an armor
	// threshold -- that sweeps in the high-armor regulars too.
	if (endlessActiveMods & ENDLESS_MOD_KILLFIRE_ANY)
	{
		endlessTurbodriveTimer = endlessBuffWindowTicks();  // refresh the window (boost OR evil jam; charge lengthens it)
		++endlessComboKills;                              // combo kill -> compounds the fire boost / evil jam
	}
	if ((endlessActiveMods & ENDLESS_MOD_STACKED) && endlessOverdriveStacks < ENDLESS_OVERDRIVE_MAX_STACKS)
		++endlessOverdriveStacks;                        // per-kill damage stack (Overdrive/Overblast) or damage-cut stack (Burnout/Misfire)

	// RETALIATION: every kill (re)opens the quickened-enemy-fire window -- refresh the timer, never
	// stack its strength, so a fast kill tempo keeps the storm up for as long as you keep clearing.
	if (endlessActiveMods & ENDLESS_MOD_RETALIATION)
		endlessRetaliationTimer = ENDLESS_RETALIATION_TICKS;

	// Siphon perk: a per-kill chance to restore 1 armor (up to the ship's max).
	if (endlessPerkOwned[PERK_SIPHON] > 0
	    && (int)(mt_rand() % 100) < endlessPerkOwned[PERK_SIPHON] * ENDLESS_PERK_SIPHON_PCT  // per-kill: gameplay RNG, not the seed
	    && player[0].armor < player[0].initial_armor)
		++player[0].armor;
}

// Two boons pay out AFTER the sector, so they can't be read off endlessActiveMods at the outpost --
// by then the player is charting the next one. Bank them the moment the zone is cleared (tyrian2.c,
// right where endlessRunDepth is bumped) into run state that rides the save.
void endlessOnSectorCleared(void)
{
	if (!endlessMode)
		return;
	if (endlessActiveMods & ENDLESS_MOD_STARCHARTS)
		endlessStarChartsOwed = true;   // a flag, not a count: two in a row still means "one full slate"
	if (endlessActiveMods & ENDLESS_MOD_BREAKTHROUGH)
		++endlessBreakthroughOwed;      // a COUNT: every Breakthrough cleared owes its own pick, even if
		                                // an outpost can only hand out one at a time
}

// --- Time-based & player-side modifiers -----------------------------------------
// endlessZoneTicks / endlessTurbodriveTimer live up top; advanced by endlessGameplayTick.

void endlessGameplayTick(void)
{
	if (!endlessFxActive())
		return;
	++endlessZoneTicks;

	// Overheat (gamble deal): the reactor runs hot -- shed 1 hull every ~80 ticks (a hair faster than
	// a single Nanorepair stack can mend). Floored at 1 so the DoT itself never lands the killing blow
	// (that keeps it clear of the death/revive path in varz.c); it just steadily bleeds you toward
	// one-hit-from-death while your guns run wild. Cleared next sector when the OVERHEAT bit lapses.
	if ((endlessActiveMods & ENDLESS_MOD_OVERHEAT) && player[0].armor > 1 && (endlessZoneTicks % 80) == 0)
	{
		--player[0].armor;
		endlessArmorHudDirty = true;  // JE_drawArmor is event-driven -- ask the game loop to repaint the bar (mainint.c)
	}

	if (endlessTurbodriveTimer > 0)
	{
		--endlessTurbodriveTimer;
		if (endlessTurbodriveTimer == 0)
		{
			endlessOverdriveStacks = 0;  // the kill-fire window lapsed -> lose the Overdrive stacks
			endlessComboKills = 0;        // and the combo resets -- back to the base 2x on the next kill
		}
	}

	// RETALIATION: drain the quickened-enemy-fire window opened by the last kill.
	if (endlessRetaliationTimer > 0)
		--endlessRetaliationTimer;

	// STATIC DISCHARGE: drain the generator-regen lockout opened by the last hit taken.
	endlessStaticLockoutTick();

	// AEGIS GATE: recharge the shield gate after a block.
	endlessAegisTick();

	// REVIVE GRACE: drain the enemy-fire stun a spent revive token bought.
	endlessReviveGraceTick();

	// Nanorepair perk: regenerate 1 armor every so often (interval shortens with more stacks).
	if (endlessPerkOwned[PERK_REGEN] > 0)
	{
		if (++endlessRegenTick >= ENDLESS_PERK_REGEN_TICKS / endlessPerkOwned[PERK_REGEN])
		{
			endlessRegenTick = 0;
			if (player[0].armor < player[0].initial_armor)
				++player[0].armor;
		}
	}

	endlessOpeningSalvoTick();    // Opening Salvo perk: advance the main-gun idle timer
	endlessCountermeasureTick();  // Countermeasure Suite perk: advance the burst cooldown
}

// True (once) if the Overheat DoT just shaved a point of hull this tick; the game loop uses it to
// repaint the armor bar, which is otherwise only redrawn on a damage/special/setup event.
bool endlessConsumeArmorHudDirty(void)
{
	const bool dirty = endlessArmorHudDirty;
	endlessArmorHudDirty = false;
	return dirty;
}

bool endlessTurbodriveActive(void)
{
	return endlessFxActive() && endlessTurbodriveTimer > 0;
}

// --- Zone milestones ------------------------------------------------------------

// One flavour line per 5-zone band (index = zone / 5), shown on the run-end summary screen.
// The tone escalates from ominous to apocalyptic as the zone climbs. Clamps to the last line
// past the end of the list.
static const char *endlessMilestoneLine(int d)
{
	static const char *const lines[] = {
		"The gate seals shut behind you.",   //   0-4
		"The zones grow aware of you.",       //   5
		"The enemy has marked your name.",    //  10
		"A dark tide rises to meet you.",     //  15
		"No signal reaches home from here.",  //  20
		"The swarm hungers, and multiplies.", //  25
		"Hostile stars wheel overhead.",      //  30
		"Beyond every chart ever drawn.",     //  35
		"The enemy tide has no ebb.",         //  40
		"Every zone bleeds a little more.",   //  45
		"Few living souls have come this far.", //  50
		"The stars themselves grow cold.",    //  55
		"A dreadful hush between the guns.",  //  60
		"The void hums with ancient malice.", //  65
		"The abyss has turned to stare back.", //  70
		"Your name is forgotten out here.",   //  75
		"Only the guns remember you now.",    //  80
		"The dark has grown teeth.",          //  85
		"Past where reason dares to follow.", //  90
		"The hull screams, and still holds.", //  95
		"Legends come this far to die.",      // 100
		"The enemy pours out of nowhere.",    // 105
		"Reality frays along the seams.",     // 110
		"The stars burn wrong out here.",     // 115
		"No light was meant to reach here.",  // 120
		"The swarm goes on without end.",     // 125
		"You are their ghost story now.",     // 130
		"The abyss forgets its own floor.",   // 135
		"Time itself loses the thread.",      // 140
		"The last known star winks out.",     // 145
		"Beyond the beyond, and climbing.",   // 150
		"These zones should not exist.",      // 155
		"The void has learned your name.",    // 160
		"Still it grows. Still you press on.", // 165
		"No rescue was ever coming.",         // 170
		"The end is a place. You near it.",   // 175
		"Further now than death itself.",     // 180
		"The dark is all that remains.",      // 185
		"You were never meant to reach here.", // 190
		"And still the zones unfold.",        // 195
		"Two hundred zones of ruin behind.",  // 200
		"The nightmare has no far edge.",     // 205
		"No sane soul flies these zones.",    // 210
		"The guns glow white with wrath.",    // 215
		"The void itself recoils from you.",  // 220
		"You are the terror they flee.",      // 225
		"Beyond every legend ever told.",     // 230
		"Even the dark runs out of dark.",    // 235
		"The final zones of creation.",       // 240
		"One breath from the end of all.",    // 245
		"Thank you for playing.",		      // 250+ (final change)
	};
	int i = d / 5;
	if (i < 0)
		i = 0;
	if (i >= (int)COUNTOF(lines))
		i = (int)COUNTOF(lines) - 1;
	return lines[i];
}

// Draw a glowing line horizontally centred on the widescreen surface (vga_width), so the
// run-end screen respects the widescreen edit instead of hugging the left.
static void endlessGlowCentered(int y, unsigned int font, const char *s)
{
	textGlowFont = font;
	JE_outTextGlow(VGAScreen, (vga_width - JE_textWidth(s, font)) / 2, y, s);
}

// The Run Over backdrop: the painted ship illustration from the campaign ending ("NOT ZINGLON!"),
// dimmed to an underlay so the glowing tally still reads over it. Simple because the picture is a
// 320x200 PCX carrying its OWN palette and using only indices 0..223 -- banks 14-15 are unused
// placeholder green, so the text can't collide with it, and dimming is just a scale of its entries.
#define ENDLESS_RUNEND_PIC   "tshp2.pcx"
#define ENDLESS_RUNEND_DIM   32   // percent brightness kept: the ship still reads, the tally still wins

static void endlessDrawRunEndBackdrop(void)
{
	JE_loadPCX(ENDLESS_RUNEND_PIC);  // fills x 0..319 of each row; also replaces colors[] wholesale

	// Centre the 320px picture on the widescreen surface and smear its edge columns into the two
	// side strips. The picture's left and right edges are soft sky/haze gradients, so a repeated
	// column reads as more of the same rather than as a seam. Sample the two edge columns BEFORE
	// the move, from the picture where it still sits: reading them back out of the moved copy is
	// the same byte but leaves the bounds resting on the memmove, which analysis can't follow.
	const int pad = (vga_width - 320) / 2;   // left strip
	const int tail = vga_width - pad - 320;  // right strip
	if (pad > 0 && tail >= 0 && vga_width <= VGAScreen->pitch)
	{
		for (int row = 0; row < vga_height; ++row)
		{
			Uint8 *const p = (Uint8 *)VGAScreen->pixels + row * VGAScreen->pitch;
			const Uint8 left = p[0], right = p[319];
			memmove(p + pad, p, 320);
			memset(p, left, pad);
			memset(p + pad + 320, right, tail);
		}
	}

	// Dim the picture through its own palette entries...
	for (int i = 0; i < 224; ++i)
	{
		colors[i].r = (Uint8)(colors[i].r * ENDLESS_RUNEND_DIM / 100);
		colors[i].g = (Uint8)(colors[i].g * ENDLESS_RUNEND_DIM / 100);
		colors[i].b = (Uint8)(colors[i].b * ENDLESS_RUNEND_DIM / 100);
	}

	// ...and give bank 15 the standard dark-to-white glow ramp the text is drawn from. The file's
	// own bank 15 is placeholder green, which would render the whole tally unreadable.
	memcpy(&colors[240], &palettes[0][240], 16 * sizeof(colors[0]));
}

void endlessOnRunEnd(void)
{
	// Run-over summary, styled like the level-end tally: glowing stat lines (the same
	// JE_outTextGlow effect JE_endLevelAni uses), centred on the widescreen, over the ending's
	// ship illustration. The caller has already faded to black, so we draw the backdrop, fade its
	// palette in, glow the lines in, wait for a key, then fade out. Returns to the title screen.
	VGAScreen = VGAScreenSeg;
	JE_clr256(VGAScreen);
	endlessDrawRunEndBackdrop();  // also swaps colors[] to the picture's palette
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	JE_wipeKey();
	frameCountMax = 4;
	SDL_Color white = { 255, 255, 255 };
	set_colors(white, 254, 254);

	// Build the stat block first, then lay it out: only the run itself knows how tall it is (the
	// hull line is conditional), and the whole screen is centred vertically around that height.
	//
	// SMALL_FONT_SHAPES has BLANK 2x2 stubs where '(', ')', '+', '*' and '=' should be (verified
	// against data/tyrian.shp), so those characters silently draw nothing here. Words only.
	// Sized for every line that can be pushed below (5 fixed + hull + seed + record) with nothing to
	// spare, so pushes go through RUNEND_LINE: adding a stat line without growing the array drops it
	// rather than running off the end of the stack. Only slots [0, n) are ever read.
	char lines[8][64] = { { 0 } };
	int n = 0;
	#define RUNEND_LINE(...) \
		do { if (n < (int)COUNTOF(lines)) snprintf(lines[n++], sizeof(lines[0]), __VA_ARGS__); } while (0)

	RUNEND_LINE("You fell in Zone %d", endlessRunDepth + 1);
	RUNEND_LINE("Zones cleared:   %d", endlessRunDepth);
	RUNEND_LINE("Enemies destroyed:   %d", endlessRunKills);
	RUNEND_LINE("Bosses slain:   %d", endlessRunBossKills);
	RUNEND_LINE("Cash amassed:   $%lu", (unsigned long)player[0].cash);

	if (endlessArmorBonus > 0)
		RUNEND_LINE("Hull reinforced:   %d", endlessArmorBonus);

	RUNEND_LINE("Seed:   %s", endlessSeedString());

	// The all-time record. endlessNoteZoneReached has already folded this run into it, so a run that
	// pushed the record says by how much -- the gap to where the record stood when the run began.
	const int recordGain = endlessBestZone - endlessBestZoneAtStart();
	if (recordGain > 0)
		RUNEND_LINE("New furthest zone:   %d   up %d", endlessBestZone, recordGain);
	else
		RUNEND_LINE("Furthest zone:   %d", endlessBestZone);
	#undef RUNEND_LINE

	// Vertical layout: title, stat block, then the milestone line held a little further off. Glyph
	// heights are the tallest in each bank (tyrian.shp), so this measures the real drawn extent.
	const int titleH  = 20;   // FONT_SHAPES
	const int lineH   = 13;   // SMALL_FONT_SHAPES
	const int titleGap = 12;  // breathing room under the title
	const int tailGap  = 10;  // ...and above the closing milestone line

	// Tighten the stat pitch until the whole thing leaves a reasonable margin top and bottom. A run
	// that bought hull has one extra line and lands a notch tighter; nothing else changes.
	int step = 18;
	int total;
	while ((total = titleH + titleGap + (n - 1) * step + lineH + tailGap + lineH) > 176 && step > 14)
		--step;

	int y = (vga_height - total) / 2;

	endlessGlowCentered(y, FONT_SHAPES, "RUN OVER");
	y += titleH + titleGap;

	for (int i = 0; i < n; ++i, y += step)
		endlessGlowCentered(y, SMALL_FONT_SHAPES, lines[i]);

	// y has advanced one full step past the last line; back that out so the tail gap is measured
	// from the text itself.
	endlessGlowCentered(y - step + lineH + tailGap, SMALL_FONT_SHAPES, endlessMilestoneLine(endlessRunDepth + 1));

	// Require inputs released first (the player may have died mid-fire), then wait for a
	// fresh key/button so the summary can't flash past.
	wait_noinput(true, true, true);
	do
	{
		setDelay(1);
		wait_delay();
	} while (!JE_anyButton());

	wait_noinput(false, false, true);
	fade_black(15);
	JE_clr256(VGAScreen);
}

void endlessEndRunToTitle(void)
{
	// Voluntarily quitting an in-progress run back to the title. In HARDCORE this is as final as
	// dying -- there's no save to resume -- so it gets the same Run Over summary the death path shows
	// (the shop has already faded to black on Quit, so endlessOnRunEnd fades its summary in cleanly).
	// In non-hardcore a quit stays silent: the run may have a save to come back to, so it isn't over.
	if (endlessHardcore)
	{
		fade_song();       // silence the shop track so the summary plays clean, like the death path
		endlessOnRunEnd();
	}
	endlessMode = false;
}
