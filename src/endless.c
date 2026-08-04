/* Endless run state, lifecycle, milestones, and run-over screen. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "fonthand.h"
#include "keyboard.h"
#include "loudness.h"
#include "mainint.h"
#include "mtrand.h"
#include "nortsong.h"
#include "nortvars.h"
#include "palette.h"
#include "pcxload.h"
#include "player.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

// Run state.

int      endlessRunDepth  = 0;
Uint64   endlessActiveMods = 0;
int      endlessArmorBonus = 0;
int      endlessRunKills = 0;
int      endlessRunBossKills = 0;

// The run's cash ledger. Income is DECLARED at the source (endlessAddCash), so the run-over tally can
// break down where a run's money came from. What can't be declared is the outgoing side: the outpost
// assigns a recomputed balance (player[0].cash = JE_cashLeft()) instead of subtracting, so there is no
// debit to hook. endlessCashSample reconciles instead -- it compares the wallet against a high-water
// mark and books the difference, a fall as spending and a rise as ENDLESS_CASH_OTHER.
//
// The rule that keeps the reconciler honest: it must run between every FALL and the next rise, or the
// stale mark swallows that rise. endlessGameplayTick runs it every tick, so the first tick of a zone
// re-marks after all the shopping; the gamble is the only place a debit and a credit land with no tick
// between them, so it reconciles around its own wager. The upgrade shop's full-refund trade-ins get
// their own bracket (endlessShopTradeSettle) so gear churn cannot inflate either run-over total.
Uint64 endlessRunCashEarned = 0;
Uint64 endlessRunCashSpent  = 0;
Uint64 endlessCashBySource[ENDLESS_CASH_SOURCES] = { 0 };
Uint64 endlessCashGearSpent = 0;
static ulong endlessCashMark = 0;

// 12 digits: high enough that no run reaches it, low enough that the run-over column can print it.
#define ENDLESS_CASH_TALLY_MAX  999999999999ULL

// Lower case: these read as a list under a heading, not as titles. Order tracks EndlessCashSource,
// which is append-only, so "starting stake" sits at the tail rather than in chronological order.
static const char *const endlessCashSourceNames[ENDLESS_CASH_SOURCES] = {
	"kills", "pickups", "bounties", "zone clears", "interest", "gambling", "declined perks", "untagged",
	"starting stake", "gear sold",
};

const char *endlessCashSourceName(EndlessCashSource src)
{
	return ((unsigned)src < ENDLESS_CASH_SOURCES) ? endlessCashSourceNames[src] : "";
}

static void endlessCashAddSat(Uint64 *tally, Uint64 amount)
{
	*tally = (amount > ENDLESS_CASH_TALLY_MAX - *tally) ? ENDLESS_CASH_TALLY_MAX : *tally + amount;
}

static void endlessCashCredit(Uint64 amount, EndlessCashSource src)
{
	if ((unsigned)src >= ENDLESS_CASH_SOURCES)
		src = ENDLESS_CASH_OTHER;
	// Clamp against the TOTAL first, then apply that same figure to the source, so the breakdown sums
	// to the total even in the (unreachable) saturating case -- clamping the two independently would
	// let them disagree.
	if (amount > ENDLESS_CASH_TALLY_MAX - endlessRunCashEarned)
		amount = ENDLESS_CASH_TALLY_MAX - endlessRunCashEarned;
	endlessRunCashEarned += amount;
	endlessCashBySource[src] += amount;
}

void endlessAddCash(long amount, EndlessCashSource src)
{
	if (amount <= 0)
		return;
	if (!endlessMode)
	{
		player[0].cash += (ulong)amount;   // campaign with the effect layer on: pay out, nothing to tally
		return;
	}

	// Settle any drift FIRST. Re-marking over an unreconciled wallet would erase whatever moved it:
	// an undeclared rise would vanish silently instead of surfacing as ENDLESS_CASH_OTHER, and a
	// purchase not yet re-marked (a shop visit with no gameplay tick since) would never reach the
	// spent total. Both would break earned - spent == wallet.
	endlessCashSample();

	player[0].cash += (ulong)amount;
	endlessCashCredit((Uint64)amount, src);
	endlessCashMark = player[0].cash;   // declared, so the reconciler must not book it a second time
}

void endlessCashSample(void)
{
	if (!endlessMode)
		return;
	const ulong now = player[0].cash;
	if (now > endlessCashMark)
		endlessCashCredit((Uint64)(now - endlessCashMark), ENDLESS_CASH_OTHER);
	else if (now < endlessCashMark)
		endlessCashAddSat(&endlessRunCashSpent, (Uint64)(endlessCashMark - now));
	endlessCashMark = now;
}

// Cash that was placed rather than earned -- the starting stake, a save being loaded, a sortie
// snapshot being reverted -- moves the mark without booking anything either way.
void endlessCashResync(void)
{
	endlessCashMark = player[0].cash;
}

// The upgrade shop refunds gear at full price, so a sell-back must not read as income the way an
// undeclared rise does. Gear falls are tracked in their own slice of the spent total; a rise cancels
// against that slice (churn nets to zero on both totals) and only the excess -- selling gear the run
// GRANTED, which booked nothing when acquired -- is credited, labelled rather than "untagged".
void endlessShopTradeSettle(void)
{
	if (!endlessMode)
		return;
	const ulong now = player[0].cash;
	if (now > endlessCashMark)
	{
		const Uint64 refund = (Uint64)(now - endlessCashMark);
		Uint64 cancel = (refund < endlessCashGearSpent) ? refund : endlessCashGearSpent;
		if (cancel > endlessRunCashSpent)   // unreachable (gear slice never exceeds the total), kept for the unsigned math
			cancel = endlessRunCashSpent;
		endlessCashGearSpent -= cancel;
		endlessRunCashSpent  -= cancel;
		if (refund > cancel)
			endlessCashCredit(refund - cancel, ENDLESS_CASH_TRADEIN);
	}
	else if (now < endlessCashMark)
	{
		const Uint64 fall = (Uint64)(endlessCashMark - now);
		endlessCashAddSat(&endlessRunCashSpent, fall);
		endlessCashAddSat(&endlessCashGearSpent, fall);
	}
	endlessCashMark = now;
}

// Per-zone timers, advanced by endlessGameplayTick.
int endlessZoneTicks      = 0;
int endlessTurbodriveTimer = 0;
int endlessRetaliationTimer = 0;

// Rewards banked on sector clear and paid at the next outpost.
bool endlessStarChartsOwed  = false;
int  endlessBreakthroughOwed = 0;
static bool endlessArmorHudDirty = false;

// Milestones use the real upcoming zone so their labels match the HUD.
#define ENDLESS_MILESTONE_EVERY 50
#define ENDLESS_MILESTONE_GRAND 100

// Kinds are tags: 0 ordinary, 1 plain, 2 grand, 3 minor.
int endlessMilestoneKindOfZone(int zone)
{
	if (zone <= 0)
		return 0;
	if (zone % ENDLESS_MILESTONE_GRAND == 0)
		return 2;
	if (zone % ENDLESS_MILESTONE_EVERY == 0)
		return 1;
	if (zone % ENDLESS_MILESTONE_EVERY == ENDLESS_MILESTONE_EVERY / 2)
		return 3;
	return 0;
}

// Run depth counts cleared zones, so charting looks one zone ahead.
int endlessMilestoneKind(void)
{
	return endlessMilestoneKindOfZone(endlessRunDepth + 1);
}

// All milestone classes grant a perk after clearing the zone.
static bool endlessPerkMilestoneAt(int depth)
{
	return endlessMilestoneKindOfZone(depth) != 0;
}

// Scheduled perk picks occur at depths 1, 5, 9, ...
#define ENDLESS_PERK_EVERY 4

// Song IDs are 1-based, like levelSong.
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

// A cadence/milestone collision defers the second pick by one zone.
bool endlessPerkDueAtDepth(int depth)
{
	if (depth <= 0)
		return false;
	if (depth % ENDLESS_PERK_EVERY == 1 || endlessPerkMilestoneAt(depth))
		return true;
	const int prev = depth - 1;
	return endlessPerkMilestoneAt(prev) && prev % ENDLESS_PERK_EVERY == 1;
}

// Milestones use the larger offer count; deferred picks use the normal count.
int endlessPerkOffersAtDepth(int depth)
{
	return endlessMilestoneKindOfZone(depth) ? ENDLESS_PERK_OFFERS_MILESTONE : ENDLESS_PERK_OFFERS;
}

// Run mode from the seed screen. Relaxed adds the death menu; Hardcore disables saving and locks
// the outpost after a mid-zone bail. Standard sits between them: the run ends when the ship does.
// (It is "Standard" rather than "Normal" so nothing here reads as the Normal difficulty.)
EndlessRunMode endlessRunMode = ENDLESS_RUNMODE_RELAXED;

const char *endlessRunModeName(EndlessRunMode mode)
{
	switch (mode)
	{
	case ENDLESS_RUNMODE_STANDARD: return "Standard";
	case ENDLESS_RUNMODE_HARDCORE: return "Hardcore";
	default:                       return "Relaxed";
	}
}

// The all-time records are stored in opentyrian.cfg, including for Hardcore runs. One per mode:
// reaching Zone 40 on Relaxed says nothing about how deep you can fly Hardcore, so a mode's record
// only ever moves under that mode.
int endlessBestZone[ENDLESS_RUNMODE_COUNT] = { 0 };
static int endlessBestZoneAtRunStart = 0;

#define ENDLESS_BEST_ZONE_MAX 99999  // a sanity ceiling on what a hand-edited config can claim

// Record a zone when it starts, not after it is cleared.
void endlessNoteZoneReached(int zone)
{
	if (!endlessMode || zone > ENDLESS_BEST_ZONE_MAX)
		return;
	int *const best = &endlessBestZone[endlessRunMode];
	if (zone <= *best)
		return;
	*best = zone;
	save_opentyrian_config();
}

// Snapshot the record for the run-over screen's gain display. Callers set the run's mode first.
void endlessRecordRunStart(void) { endlessBestZoneAtRunStart = endlessBestZone[endlessRunMode]; }
int  endlessBestZoneAtStart(void) { return endlessBestZoneAtRunStart; }

void endlessResetRun(void)
{
	endlessRunDepth   = 0;
	endlessActiveMods = 0;
	endlessArmorBonus = 0;
	endlessRunKills   = 0;
	endlessRunBossKills = 0;
	endlessRunCashEarned = 0;
	endlessRunCashSpent  = 0;
	endlessCashGearSpent = 0;
	memset(endlessCashBySource, 0, sizeof(endlessCashBySource));
	endlessCashResync();   // whatever is in the wallet right now was not earned by the run starting here
	endlessPurchasedMods = 0;
	endlessBuffKind = 0;
	endlessBuffCooldownUntil = 0;
	endlessOverdriveStacks = 0;
	endlessComboKills = 0;
	endlessPerkPending = false;
	endlessStarChartsOwed = false;
	endlessBreakthroughOwed = 0;
	endlessPerkChoiceN = 0;
	endlessPerkDepthDone = -1;
	endlessResumeVisit = false;
	endlessCreditsShown = false;
	endlessLastSong = 0;
	endlessLastSongDepth = -1;
	endlessRegenTick = 0;
	endlessSalvoIdle = ENDLESS_PERK_SALVO_IDLE;
	endlessSalvoWindow = 0;
	endlessCmCooldown = 0;
	endlessBuffCharge = 0;
	endlessReviveHeld = false;
	endlessRevivesUsed = 0;
	endlessCleanseChargeCount = 0;
	endlessGamblePerkWon = false;
	endlessShopTax = 0;
	endlessGambleRigged = false;
	endlessLongCon = 0;
	endlessLockedSortie = false;
	endlessQuitToOutpost = false;
	endlessSortieHave = false;
	endlessSortiePrePurchased = 0;
	endlessSortiePreCleanse = 0;
	endlessSortiePreLongCon = 0;
	endlessSortieOutpostMods = 0;
	// New runs override this after reset, and a loaded/reverted one restores the saved mode.
	endlessRunMode = ENDLESS_RUNMODE_RELAXED;
	endlessBaseName[0] = endlessPrevBaseName[0] = '\0';
	endlessBaseEp = endlessBaseLvl = endlessPrevBaseEp = endlessPrevBaseLvl = 0;
	endlessRecentCount = 0;
	player[0].superbombs = 0;
	memset(endlessPerkOwned, 0, sizeof(endlessPerkOwned));
	endlessSetSeed("");
}

// Clear outpost-only state before enabling campaign debug effects.
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
	endlessPerkPending = false;
}

void endlessCountKill(int linknum)
{
	if (!endlessFxActive())
		return;

	// Multi-part enemies share a nonzero link number and count once.
	static int lastCountedLink = 0;
	if (linknum != 0 && linknum == lastCountedLink)
		return;
	lastCountedLink = linknum;

	++endlessRunKills;
	// Boss kills are counted when their health bar empties.
	if (endlessActiveMods & ENDLESS_MOD_KILLFIRE_ANY)
	{
		endlessTurbodriveTimer = endlessBuffWindowTicks();
		++endlessComboKills;
	}
	if ((endlessActiveMods & ENDLESS_MOD_STACKED) && endlessOverdriveStacks < ENDLESS_OVERDRIVE_MAX_STACKS)
		++endlessOverdriveStacks;

	// Retaliation refreshes its window but does not stack.
	if (endlessActiveMods & ENDLESS_MOD_RETALIATION)
		endlessRetaliationTimer = ENDLESS_RETALIATION_TICKS;

	// Siphon perk: a per-kill chance to restore 1 armor (up to the ship's max).
	if (endlessPerkOwned[PERK_SIPHON] > 0
	    && (int)(mt_rand() % 100) < endlessPerkOwned[PERK_SIPHON] * ENDLESS_PERK_SIPHON_PCT
	    && player[0].armor < player[0].initial_armor)
		++player[0].armor;
}

// Bank post-clear boons before the next sector changes endlessActiveMods.
void endlessOnSectorCleared(void)
{
	if (!endlessMode)
		return;
	if (endlessActiveMods & ENDLESS_MOD_STARCHARTS)
		endlessStarChartsOwed = true;
	if (endlessActiveMods & ENDLESS_MOD_BREAKTHROUGH)
		++endlessBreakthroughOwed;
}

// Time-based and player-side modifiers.

void endlessGameplayTick(void)
{
	if (!endlessFxActive())
		return;
	++endlessZoneTicks;

	// Bank in-level income (enemy value, pickups, bounties) and, on the first tick of a zone,
	// re-mark after everything the outpost and the E-Shop just spent.
	endlessCashSample();

	// Overheat drains hull but cannot land the killing blow.
	if ((endlessActiveMods & ENDLESS_MOD_OVERHEAT) && player[0].armor > 1 && (endlessZoneTicks % 80) == 0)
	{
		--player[0].armor;
		endlessArmorHudDirty = true;
	}

	if (endlessTurbodriveTimer > 0)
	{
		--endlessTurbodriveTimer;
		if (endlessTurbodriveTimer == 0)
		{
			endlessOverdriveStacks = 0;
			endlessComboKills = 0;
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

// Consume the event-driven armor-bar repaint flag.
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

// Run-over flavor text, one line per five-zone band.
static const char *endlessMilestoneLine(int d)
{
	static const char* const lines[] = {
		"The gate seals shut behind you.",         //   0-4
		"The last friendly beacon fades.",         //   5
		"Something is following your signal.",     //  10
		"The wreckage ahead is still warm.",       //  15
		"Command has stopped answering.",          //  20
		"Enemy signals fill every channel.",       //  25
		"The stars no longer match the charts.",   //  30
		"Every route leads farther in.",            //  35
		"The navigation computer refuses course.", //  40
		"The wrecks are starting to look familiar.", // 45
		"Something has learned how you fight.",     //  50
		"The guns have not cooled in hours.",       //  55
		"A dreadful hush falls between volleys.",  //  60
		"The hull remembers every impact.",         //  65
		"No human signal reaches this far.",        //  70
		"Even the warning lights fall silent.",     //  75
		"The charts end here.",                     //  80
		"Nothing living knows these coordinates.", //  85
		"Reality bends around the wreckage.",       //  90
		"Your engines run on borrowed time.",       //  95
		"Legends come this far to die.",            // 100
		"The enemy no longer sees you as prey.",    // 105
		"Their fleets gather beyond the static.",   // 110
		"The stars flicker when you fire.",         // 115
		"The distress calls are no longer yours.",  // 120
		"The swarm goes on without end.",           // 125
		"They tell stories about your ship.",       // 130
		"Your signal has become a warning.",        // 135
		"Time loses count between the gunfire.",    // 140
		"The last known beacon has gone dark.",     // 145
		"The end of the map was the beginning.",    // 150
		"These zones should not exist.",            // 155
		"The next sector is waiting for you.",      // 160
		"Still it grows. Still you press on.",      // 165
		"No rescue was ever coming.",               // 170
		"There are no maps for what comes next.",   // 175
		"Enemy fleets turn before you arrive.",     // 180
		"They scatter when your signal appears.",   // 185
		"The hunters have become the hunted.",      // 190
		"Only the guns remember you now.",          // 195
		"Two hundred zones burn behind you.",       // 200
		"The guns glow white with wrath.",          // 205
		"Entire fleets vanish in your wake.",       // 210
		"Your name is now an evacuation order.",    // 215
		"Even their warships flee your signal.",    // 220
		"You are the anomaly on their charts.",     // 225
		"The universe is running out of hiding places.", // 230
		"Creation grows thin around your ship.",    // 235
		"There are no more stars ahead.",           // 240
		"There is nothing left to chart.",          // 245
	};

	int i = d / 5;
	if (i < 0)
		i = 0;
	if (i >= (int)COUNTOF(lines))
		i = (int)COUNTOF(lines) - 1;
	return lines[i];
}

// The sign-off, printed under the last milestone line once the flavor has nowhere further to go.
static const char *endlessMilestoneEpilogue(int d)
{
	return (d >= 250) ? "Thank you for playing." : NULL;
}

// Draw centered text on the full widescreen surface.
static void endlessGlowCentered(int y, unsigned int font, const char *s)
{
	textGlowFont = font;
	JE_outTextGlow(VGAScreen, (vga_width - JE_textWidth(s, font)) / 2, y, s);
}

// Draw one stat row: label from the left edge of the block, value flush against its right edge.
// Both glow in together, as one line -- two JE_outTextGlow calls would play the effect twice.
static void endlessGlowRow(int left, int right, int y, unsigned int font, const char *label, const char *value)
{
	textGlowFont = font;
	const int xs[2] = { left, right - JE_textWidth(value, font) };
	const char *const ss[2] = { label, value };
	JE_outTextGlowMulti(VGAScreen, xs, y, ss, 2);
}

// Dim the campaign-ending ship art behind the run summary.
#define ENDLESS_RUNEND_PIC   "tshp2.pcx"
#define ENDLESS_RUNEND_DIM   32   // percent brightness kept: the ship still reads, the tally still wins

static void endlessDrawRunEndBackdrop(void)
{
	JE_loadPCX(ENDLESS_RUNEND_PIC);

	// Center the 320px image and extend its edge columns into the side strips.
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

	// Dim the image palette.
	for (int i = 0; i < 224; ++i)
	{
		colors[i].r = (Uint8)(colors[i].r * ENDLESS_RUNEND_DIM / 100);
		colors[i].g = (Uint8)(colors[i].g * ENDLESS_RUNEND_DIM / 100);
		colors[i].b = (Uint8)(colors[i].b * ENDLESS_RUNEND_DIM / 100);
	}

	// Restore the text glow ramp in bank 15.
	memcpy(&colors[240], &palettes[0][240], 16 * sizeof(colors[0]));
}

// A destroyed ship gets the death menu (JE_endlessDeathMenu) rather than GAME OVER and the run
// summary. The level loop skips the GAME OVER wait for it, and JE_main puts the
// menu up in its place. Relaxed only: Hardcore has no second chance to offer, and in Standard a
// fatal hit is the end of the run.
// The death menu exists because any player can just press esc during the death explosion animation
// and get to the pause menu and effectively have the same choices -- which is why the modes that do
// NOT offer it close that route off (endlessDeathLocksMenu).
bool endlessDeathMenuDue(void)
{
	return endlessMode && endlessRunMode == ENDLESS_RUNMODE_RELAXED && endlessSortieValid();
}

// The other half of that bargain: with no death menu on offer, the pause menu's Quit Level row would
// be a free trip back to the outpost mid-explosion, so Standard and Hardcore shut the menu itself.
bool endlessDeathLocksMenu(void)
{
	return endlessMode && endlessRunMode != ENDLESS_RUNMODE_RELAXED;
}

void endlessOnRunEnd(void)
{
	endlessCashSample();  // catch whatever the killing tick paid out before the tally is printed

	// Draw the run summary over the dimmed ship illustration.
	VGAScreen = VGAScreenSeg;
	JE_clr256(VGAScreen);
	endlessDrawRunEndBackdrop();
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	JE_wipeKey();
	frameCountMax = 4;
	SDL_Color white = { 255, 255, 255 };
	set_colors(white, 254, 254);

	// The tally is a two-column block: labels left, values right. Only the headline, the epitaph
	// and the closing line are centered.
	// SMALL_FONT_SHAPES lacks several punctuation glyphs, so these lines use words.
	char fellLine[48];
	snprintf(fellLine, sizeof(fellLine), "You fell in Zone %d", endlessRunDepth + 1);

	// Zero-initialized for the analyzer (C6001): it cannot correlate the n guard with which
	// entries the width loop below reads.
	struct { char label[28], value[40]; } rows[10] = { 0 };
	int n = 0;
	#define RUNEND_ROW(lbl, ...) \
		do { \
			if (n < (int)COUNTOF(rows)) \
			{ \
				SDL_strlcpy(rows[n].label, (lbl), sizeof(rows[0].label)); \
				snprintf(rows[n].value, sizeof(rows[0].value), __VA_ARGS__); \
				++n; \
			} \
		} while (0)

	RUNEND_ROW("Mode:", "%s", endlessRunModeName(endlessRunMode));
	RUNEND_ROW("Zones cleared:", "%d", endlessRunDepth);
	RUNEND_ROW("Enemies destroyed:", "%d", endlessRunKills);
	RUNEND_ROW("Bosses slain:", "%d", endlessRunBossKills);
	// Everything the run took in and everything it burned, rather than the wallet at the moment of
	// death -- that mostly measures how recently you last spent. The two differ by the balance left,
	// so a hoarder and a spender with the same earnings read very differently here.
	RUNEND_ROW("Cash earned:", "$%llu", (unsigned long long)endlessRunCashEarned);
	RUNEND_ROW("Cash spent:", "$%llu", (unsigned long long)endlessRunCashSpent);

	if (endlessArmorBonus > 0)
		RUNEND_ROW("Hull reinforced:", "%d", endlessArmorBonus);

	RUNEND_ROW("Seed:", "%s", endlessSeedString());
	#undef RUNEND_ROW

	// The all-time record is not part of the run tally, so it closes the screen centered under the
	// milestone line instead. Each mode keeps its own record, so the number carries the mode's
	// initial (25 H) -- otherwise a Relaxed best and a Hardcore best would read as the same zone.
	// Show the gain when positive.
	char recordLine[64];
	const int best = endlessBestZone[endlessRunMode];
	const int recordGain = best - endlessBestZoneAtStart();
	const char modeInitial = endlessRunModeName(endlessRunMode)[0];
	if (recordGain > 0)
		snprintf(recordLine, sizeof(recordLine), "New furthest zone: %d %c   up %d", best, modeInitial, recordGain);
	else
		snprintf(recordLine, sizeof(recordLine), "Furthest zone: %d %c", best, modeInitial);

	// Size the block to its widest label and widest value, then center it as a unit so both columns
	// line up whatever the run produced.
	int labelW = 0, valueW = 0;
	for (int i = 0; i < n; ++i)
	{
		const int lw = JE_textWidth(rows[i].label, SMALL_FONT_SHAPES);
		const int vw = JE_textWidth(rows[i].value, SMALL_FONT_SHAPES);
		if (lw > labelW) labelW = lw;
		if (vw > valueW) valueW = vw;
	}

	const int colGap = 30;                    // enough that the two columns read as columns
	const int blockMax = vga_width - 40;      // ...but never past the margins
	int blockW = labelW + colGap + valueW;
	if (blockW > blockMax)
		blockW = blockMax;                    // squeeze the gap first: the columns meet before anything clips
	const int blockLeft = (vga_width - blockW) / 2;
	const int blockRight = blockLeft + blockW;

	// Fit the title, stats, and closing lines within the screen.
	const char *const closingLine = endlessMilestoneLine(endlessRunDepth + 1);
	const char *const closingTail = endlessMilestoneEpilogue(endlessRunDepth + 1);

	const int titleH  = 20;   // FONT_SHAPES
	const int lineH   = 13;   // SMALL_FONT_SHAPES
	const int recordGap = 3;  // the record hangs off the closing line as its own beat, not a new block
	int titleGap = 12;        // breathing room under the title
	int tailGap  = 10;        // ...and above the closing milestone line

	const int bodyLines = n + 1;                            // the epitaph, then one line per row
	const int closeLines = (closingTail != NULL) ? 2 : 1;   // the milestone line, plus the sign-off at 250
	int step = 18;
	int total;
	// Squeeze the row pitch down to the glyph height first, then the two gaps: the deepest runs carry
	// the most stat rows AND the extra sign-off line, and there are only 200 scanlines to spend.
	while ((total = titleH + titleGap + (bodyLines - 1) * step + lineH
	                + tailGap + lineH
	                + (closeLines - 1) * (recordGap + lineH)
	                + recordGap + lineH) > 182)
	{
		if (step > lineH)
			--step;
		else if (titleGap > 6)
			--titleGap;
		else if (tailGap > 6)
			--tailGap;
		else
			break;
	}

	int y = (vga_height - total) / 2;

	endlessGlowCentered(y, FONT_SHAPES, "RUN OVER");
	y += titleH + titleGap;

	endlessGlowCentered(y, SMALL_FONT_SHAPES, fellLine);
	y += step;

	for (int i = 0; i < n; ++i, y += step)
		endlessGlowRow(blockLeft, blockRight, y, SMALL_FONT_SHAPES, rows[i].label, rows[i].value);

	int closingY = y - step + lineH + tailGap;
	endlessGlowCentered(closingY, SMALL_FONT_SHAPES, closingLine);
	if (closingTail != NULL)
	{
		closingY += lineH + recordGap;   // the sign-off stays with the line it follows from
		endlessGlowCentered(closingY, SMALL_FONT_SHAPES, closingTail);
	}
	endlessGlowCentered(closingY + lineH + recordGap, SMALL_FONT_SHAPES, recordLine);

	// Ignore held controls, then wait for fresh input.
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
	// A Hardcore quit is final; a saveable run may still be resumed.
	if (endlessHardcore())
	{
		fade_song();
		endlessOnRunEnd();
	}
	endlessMode = false;
}
