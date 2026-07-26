/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Endless mode: depth-scaled enemy difficulty, elites, and the player-side modifiers.
 *
 * One of the endless_*.c files that make up endless mode: endless.h is the public
 * interface, endless_internal.h the state and helpers the group shares.
 */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"        // difficultyLevel, DIFFICULTY_*, player-independent globals
#include "custom_weapon.h" // customWeaponPort / customSidekickSlot (reserved shop slots)
#include "episodes.h"      // item arrays + SHIP_NUM/PORT_NUM/... counts, episodeAvail, JE_initEpisode
#include "joystick.h"      // push_joysticks_as_keyboard
#include "lvlmast.h"       // shapeFile[]
#include "mainint.h"       // JE_getCost
#include "mtrand.h"        // mt_rand
#include "player.h"        // player[]
#include "sprite.h"        // JE_loadCompShapes, enemySpriteSheets, shopSpriteSheet
#include "tyrian2.h"       // itemAvail, itemAvailMax
#include "varz.h"          // eventRec, maxEvent, map* globals

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Combo kill counter: +1 per kill while a kill-fire window is up, reset the instant it lapses.
// Not itself capped (the HUD's plain "xN"); only the derived fire-rate multiplier below is.
int endlessComboKills = 0;
#define ENDLESS_COMBO_KILLS_PER_STEP 25  // every this many combo kills adds +1x to the fire-rate multiplier
// Linear ramp: 2x at combo 0, +1x per step, capped at 8 steps (x10 at a 200 combo).
// Turbodrive, Overdrive and the evil jams share the schedule; only Overblast skips the fire boost.
#define ENDLESS_COMBO_MAX_STEPS       8

// The evil kill-fire mirrors. Fire-jam curses (Backfire, Burnout): while the window is up they add
// cooldown to every shot (slower fire) instead of removing it, ramping with the same combo steps;
// Burnout stacks even more jam. Damage-cut curses (Burnout, Misfire): each kill stacks a shot-damage
// reduction, peaking at the stack cap and floored so you can still fight.
#define ENDLESS_EVIL_JAM_BASE        3   // base extra shotRepeat ticks per shot while a fire-jam curse is up
#define ENDLESS_EVIL_JAM_PER_STEP    3   // + this many more per combo step (up to ENDLESS_COMBO_MAX_STEPS)
#define ENDLESS_EVIL_JAM_STACK_MAX   10  // Burnout: up to this many MORE jam ticks at full stacks
#define ENDLESS_EVIL_DMG_MAX         75  // Burnout / Misfire: up to -this% shot damage at full stacks (mirror of the +150% boon)
#define ENDLESS_EVIL_DMG_FLOOR       25  // ...but never cut the player's shot damage below this %

// --- Depth- and mutator-scaled enemy difficulty --------------------------------
// Endless never changes WHICH enemies appear -- it only scales the stats of what the level
// already spawns, via levers the engine applies at use (notes.md §Difficulty ramp).

// Difficulty factor (percent) tilting the depth ramp: NORMAL = 100 keeps the tuned baseline,
// and the spread is wide so the modes feel distinct (notes.md §Difficulty ramp).
static int endlessDifficultyRampPercent(void)
{
	switch (difficultyLevel)
	{
	case DIFFICULTY_WIMP:       return 50;
	case DIFFICULTY_EASY:       return 75;
	case DIFFICULTY_NORMAL:     return 100;
	case DIFFICULTY_HARD:       return 120;   // 80% elite cap at ~zone 100
	case DIFFICULTY_IMPOSSIBLE: return 134;   // 80% elite cap at ~zone 90
	default:                    return 160;   // Insanity and beyond: 80% elite cap at ~zone 75
	}
}

// Depth driving the enemy-difficulty levers: real run depth x1.25, tilted by base difficulty
// (endlessDifficultyRampPercent). Each lever below has its own slope so the caps mature one
// at a time across the run instead of piling into a wall (notes.md §Endless / Difficulty
// ramp); HUD, score, milestones and the economy still use the real endlessRunDepth.
// The lever clock for an ARBITRARY (run depth, difficulty tilt) pair. Split out from the accessor
// below so a calibration point can be evaluated off a fixed zone and difficulty without touching
// the live globals -- see the pierce lockout's reference zone.
static int endlessEffectiveDepthOf(int runDepth, int rampPercent)
{
	return runDepth * rampPercent * 5 / 400;
}

static int endlessEffectiveDepth(void)
{
	return endlessEffectiveDepthOf(endlessRunDepth, endlessDifficultyRampPercent());
}

// The current zone as the difficulty ramp sees it: the real zone (endlessRunDepth + 1) on NORMAL,
// advanced on harder difficulties and held back on easier ones (same rampPercent as every other
// enemy lever). The player-facing "zone N" thresholds -- the extra-shot tide onset, the contact-
// damage ramp, and the course-danger ramp -- are all expressed against THIS, so harder modes reach
// each one sooner and easier modes later (notes.md §Difficulty ramp).
int endlessDifficultyZone(void)
{
	return 1 + endlessRunDepth * endlessDifficultyRampPercent() / 100;
}

// --- Enemy intensity tuning ------------------------------------------------------
// The four intensity levers all have the same shape: a stock value of 100 (or 1x), a slope per
// EFFECTIVE depth, a set of per-mutator deltas, and a clamp. Every number one of them uses is
// named here, so retuning the ramp means editing this block rather than hunting literals in the
// bodies below. The clamps matter as much as the slopes: each lever tops out at a different
// zone, which is what makes the run's difficulty arrive in waves rather than as one wall
// (notes.md §Difficulty ramp).

// Ordinary-enemy HP, percent of stock.
#define ENDLESS_HP_PER_DEPTH       4    // +% per effective depth
#define ENDLESS_HP_FORTIFIED     120    // FORTIFIED: +% (2.2x HP, clearly felt)
#define ENDLESS_HP_FRAGILE        50    // FRAGILE: -%
#define ENDLESS_HP_MIN            25    // floor: a FRAGILE zone-0 enemy still takes a hit
#define ENDLESS_HP_MAX           600    // reached at effective depth 125

// Boss HP, as a whole multiplier (1 = stock).
#define ENDLESS_BOSS_DEPTH_PER_X   8    // +1x per this many effective depths
#define ENDLESS_BOSS_FORTIFIED     3    // FORTIFIED: +this many x (a 4x boss at depth 0)
#define ENDLESS_BOSS_MARKED        2    // gamble "Marked": the boss you paid to forget comes back bulked up
#define ENDLESS_BOSS_MAX          16    // reached at run depth ~96 on Normal

// Elite/champion HP, as a whole multiplier (1 = stock) -- a damage divisor like the boss one, and
// it stacks on top of endlessArmorPercent, which has already scaled their raw armour. A divisor of
// N means N hits per armour point, which is why the ceiling is this low against the 1-damage
// piercing weapons (notes.md §Difficulty ramp has the retune history).
#define ENDLESS_ELITE_HP_BASE      2    // multiplier at depth 0
#define ENDLESS_ELITE_HP_PER_X    40    // +1x per this many effective depths
#define ENDLESS_ELITE_HP_MAX       4    // ceiling, reached at effective depth 80

// Boss pierce lockout, in sim ticks. A piercing shot (weapon attack >= 250) is never consumed on
// impact, so the SAME shot re-damages the SAME hull on every tick it overlaps -- five to ten free
// hits per pass, multiplied again by every linked segment it happens to cover. Ordinary enemies
// live with that fine; a BOSS does not, because the boss HP multiplier above buys hull that pierce
// DPS simply ignores: pierce damage scales with overlap time, not with the armour it is pointed at,
// so a 16x boss dies to a Mega Cannon in about the same time a 1x one does. The lockout therefore
// rides the target's OWN multiplier -- zero ticks at stock HP (exactly the behaviour every
// previous build had), a little more repeat-hit immunity for each further multiple. Reading it off
// the multiplier is also what dilutes it for the special tiers for free: an elite or champion
// carries 2..4x, not 1..16x, so it earns a small fraction of what a deep boss does. Ordinary
// enemies carry 1x and are therefore never locked out at all.
//
// The figures are deliberately small. Pierce DPS is roughly proportional to 1/(lock+1) -- a bullet
// re-hits the same hull once a tick otherwise -- so at the reference zone:
//
//   boss      0.10 tick  (~9% tax)
//   champion  0.05 tick  (~5%)
//   elite     0.02 tick  (~2%)
//
// A plain boss only reaches 0.19 (~16%) at the very top of its HP ramp, the 16x cap at zone ~97;
// an elite/champion boss riding the 24x cap tops out at 0.30 (~23%). It is a safeguard, not a wall,
// and it can never make a boss feel immune. Tuned by play-testing rather than by theory: the
// weapons this touches (Mega Cannon, Sonic Impulse) deal 1 damage a hit, so anything heavier reads
// in play as "my gun does nothing" -- which is exactly how the first few attempts at these numbers
// landed.
//
// Carried in HUNDREDTHS of a tick rather than whole ticks. The boss multiplier itself is an
// integer -- the damage accumulator can only divide by one -- so keying the lockout off it made
// the lockout jump a whole tick at a time, once every 16 effective depth. The lockout has no such
// constraint, so it reads the UNROUNDED ramp and creeps every zone instead. The fraction is spent
// at the hit site through a per-bullet carry (tyrian2.c).
//
// ---- THE TUNING KNOBS ----------------------------------------------------------------------
// One number per tier, and each one IS the play-tested figure: what that tier's lockout reads, in
// hundredths of a tick, at the reference zone. Everything else is derived, so changing a tier here
// moves that tier and nothing else, and the shape (ride the target's own HP ramp) is preserved
// automatically. Earlier revisions expressed this as a slope plus per-tier percentages that had to
// be hand-fitted every time an HP ramp moved; these do not -- the reference span is recomputed
// from the ramps themselves, so retuning elite HP can no longer silently drag the lockout with it.
#define ENDLESS_PIERCE_LOCK_REF_ZONE      50  // the zone the three figures below were tuned at
#define ENDLESS_PIERCE_LOCK_BOSS          10  // boss:     0.10 tick at the reference zone
#define ENDLESS_PIERCE_LOCK_CHAMP		   5  // champion: 0.05
#define ENDLESS_PIERCE_LOCK_ELITE          2  // elite:    0.02
#define ENDLESS_PIERCE_LOCK_MAX            1  // hard backstop in whole ticks; the ramps never reach it
// --------------------------------------------------------------------------------------------
// ENDLESS_PIERCE_LOCK_SCALE (the 1/100-tick fixed-point unit) lives in endless.h: it is the unit
// of the value endlessPierceLock100 hands back, so the hit site has to agree on it.

// Enemy shot cooldown, percent of stock. This one counts DOWN: the deltas are subtracted, so a
// bigger number means faster enemy fire.
#define ENDLESS_FIRE_PER_DEPTH_NUM 3    // -% per effective depth, as NUM/DEN (0.75%)
#define ENDLESS_FIRE_PER_DEPTH_DEN 4
#define ENDLESS_FIRE_FRENZY       50    // FRENZY: -% (about 2x fire on its own)
#define ENDLESS_FIRE_ENRAGE_TICKS 25    // ENRAGE: -1% per this many ticks spent in the zone...
#define ENDLESS_FIRE_ENRAGE_MAX   55    // ...up to -this%
#define ENDLESS_FIRE_OVERCLOCK    30    // OVERCLOCK: -% (everything runs hot)
#define ENDLESS_FIRE_OVERLOAD     55    // OVERLOAD: -% (Overclock cranked way up)
#define ENDLESS_FIRE_MAX_REDUCE   75    // floor of 25% cooldown, i.e. 4x fire rate
#define ENDLESS_RETALIATION_FIRE_PCT 80 // RETALIATION: while the kill-storm window is up, enemy cooldown x this% (~25% quicker fire), applied MULTIPLICATIVELY after the reduce cap so it still bites deep in a run

// Enemy projectile speed, percent of stock.
#define ENDLESS_SPEED_PER_DEPTH_NUM 5   // +% per effective depth, as NUM/DEN (1.67%)
#define ENDLESS_SPEED_PER_DEPTH_DEN 3
#define ENDLESS_SPEED_SWIFT       70    // SWIFT: +% (1.7x shots)
#define ENDLESS_SPEED_DILATION    45    // DILATION: -% (time dilation, shots crawl)
#define ENDLESS_SPEED_OVERCLOCK   40    // OVERCLOCK: +%
#define ENDLESS_SPEED_OVERLOAD    90    // OVERLOAD: +%
#define ENDLESS_SPEED_MIN         40    // floor: even a dilated shot still travels
#define ENDLESS_SPEED_MAX        240    // 2.4x, reached at run depth ~67 on Normal

// Enemy shot damage, percent of stock. Capped lower than the other levers because the tide
// resumes the climb past the cap (see endlessShotDamagePercent).
#define ENDLESS_DMG_PER_DEPTH_NUM  7    // +% per effective depth, as NUM/DEN (1.75%)
#define ENDLESS_DMG_PER_DEPTH_DEN  4
#define ENDLESS_DMG_DEVASTATING   75    // DEVASTATING: +%
#define ENDLESS_DMG_MAX          220    // intensity cap; the tide climbs on from here

// --- Debug per-lever overrides ----------------------------------------------------
// A pinned lever short-circuits its accessor entirely -- depth AND mutators are bypassed, which is
// the whole point: it lets one lever be held still (or forced) while the rest ramp normally, so a
// difficulty wall can be attributed to a single axis. All-zero by default, so an unpinned build
// behaves exactly as before. See the ESO_* enum in endless.h.
EndlessScalingOverride endlessScalingOverride[ESO_COUNT];

// Every override check is this one line at the top of an accessor. A macro rather than a helper
// call so the early return reads at the call site -- an accessor that can be short-circuited should
// say so on its first line. The lookup TABLES and the snapshot builder live at the END of this
// file, because their editing bounds cite tunables declared further down with the levers they
// belong to; only the array above and this macro have to precede the accessors.
#define ENDLESS_OVERRIDE(id) \
	do { if (endlessScalingOverride[id].active) return endlessScalingOverride[id].value; } while (0)

// Ordinary-enemy HP multiplier (100 = normal): +4% per (effective) level; FORTIFIED +120%
// (2.2x HP, clearly felt); FRAGILE -50%.
int endlessArmorPercent(void)
{
	ENDLESS_OVERRIDE(ESO_ARMOR);
	int pct = 100 + endlessEffectiveDepth() * ENDLESS_HP_PER_DEPTH;
	if (endlessActiveMods & ENDLESS_MOD_FORTIFIED)
		pct += ENDLESS_HP_FORTIFIED;
	if (endlessActiveMods & ENDLESS_MOD_FRAGILE)
		pct -= ENDLESS_HP_FRAGILE;
	return endlessClamp(pct, ENDLESS_HP_MIN, ENDLESS_HP_MAX);
}

// Boss HP multiplier (1 = normal): +1x every 8 (effective) levels, reaching the 16x cap at run
// depth ~96 on Normal; FORTIFIED +3x (a 4x boss at depth 0); FRAGILE ~halves it.
int endlessBossHpMult(void)
{
	ENDLESS_OVERRIDE(ESO_BOSSHP);
	int mult = 1 + endlessEffectiveDepth() / ENDLESS_BOSS_DEPTH_PER_X;
	if (endlessActiveMods & ENDLESS_MOD_FORTIFIED)
		mult += ENDLESS_BOSS_FORTIFIED;
	if (endlessActiveMods & ENDLESS_MOD_MARKED)
		mult += ENDLESS_BOSS_MARKED;
	if (endlessActiveMods & ENDLESS_MOD_FRAGILE)
		mult = (mult + 1) / 2;   // halve, rounding up: FRAGILE softens a boss, never erases it
	return endlessClamp(mult, 1, ENDLESS_BOSS_MAX);
}

// The same boss multiplier WITHOUT the integer truncation, in hundredths. endlessBossHpMult has to
// round down to a whole number because the damage accumulator divides by it; the pierce lockout has
// no such constraint and wants the ramp itself, so it reads this. Identical slope, identical
// mutator deltas, identical cap -- just continuous, so it passes exactly through the stepped
// version at every point where that one steps. Keep the two bodies in step.
// The BARE boss depth curve at an arbitrary effective depth, hundredths, no mutators -- what the
// pierce lockout's reference point is measured against, so a calibration taken at a fixed zone
// can never drift from the curve the run actually follows. Deliberately NOT folded into the live
// accessor below: that one has to clamp LAST, after the mutator deltas, or FRAGILE would halve an
// already-clamped figure instead of the true one.
static int endlessBossRamp100(int effDepth)
{
	return endlessClamp(100 + effDepth * 100 / ENDLESS_BOSS_DEPTH_PER_X, 100, ENDLESS_BOSS_MAX * 100);
}

static int endlessBossHpMult100(void)
{
	int mult = 100 + endlessEffectiveDepth() * 100 / ENDLESS_BOSS_DEPTH_PER_X;
	if (endlessActiveMods & ENDLESS_MOD_FORTIFIED)
		mult += ENDLESS_BOSS_FORTIFIED * 100;
	if (endlessActiveMods & ENDLESS_MOD_MARKED)
		mult += ENDLESS_BOSS_MARKED * 100;
	if (endlessActiveMods & ENDLESS_MOD_FRAGILE)
		mult = (mult + 1) / 2;
	return endlessClamp(mult, 100, ENDLESS_BOSS_MAX * 100);
}

// Enemy shot-cooldown multiplier (100 = normal; LOWER = fires faster): -0.75% per (effective)
// level, bottoming at the 4x-fire floor at run depth ~80 on Normal; FRENZY an extra -50% (~2x
// fire), floored at 25% so deep FRENZY runs reach ~4x.
int endlessFireDelayPercent(void)
{
	ENDLESS_OVERRIDE(ESO_FIREDELAY);
	int reduce = endlessEffectiveDepth() * ENDLESS_FIRE_PER_DEPTH_NUM / ENDLESS_FIRE_PER_DEPTH_DEN;
	if (endlessActiveMods & ENDLESS_MOD_FRENZY)
		reduce += ENDLESS_FIRE_FRENZY;
	if (endlessActiveMods & ENDLESS_MOD_ENRAGE)  // ramps up the longer you linger in the zone
		reduce += endlessClamp(endlessZoneTicks / ENDLESS_FIRE_ENRAGE_TICKS, 0, ENDLESS_FIRE_ENRAGE_MAX);
	if (endlessActiveMods & ENDLESS_MOD_OVERCLOCK)
		reduce += ENDLESS_FIRE_OVERCLOCK;
	if (endlessActiveMods & ENDLESS_MOD_OVERLOAD)
		reduce += ENDLESS_FIRE_OVERLOAD;
	if (reduce > ENDLESS_FIRE_MAX_REDUCE)
		reduce = ENDLESS_FIRE_MAX_REDUCE;
	int pct = 100 - reduce;
	// RETALIATION: each enemy kill refreshes a short window (endlessRetaliationTimer, driven from
	// endlessCountKill / endlessGameplayTick) during which ALL enemy fire is ~25% quicker. Applied as a
	// final MULTIPLY, not another additive reduce, so it still speeds fire even once the depth/mod
	// reduce has hit its floor -- and so a kill-tempo storm reads differently from the time-based Enrage.
	if ((endlessActiveMods & ENDLESS_MOD_RETALIATION) && endlessRetaliationTimer > 0)
		pct = pct * ENDLESS_RETALIATION_FIRE_PCT / 100;
	return pct;
}

// Enemy projectile-speed multiplier (100 = normal): +1.67% per (effective) level, reaching the
// 2.4x cap at run depth ~67 on Normal; SWIFT +70% (1.7x shots).
int endlessShotSpeedPercent(void)
{
	ENDLESS_OVERRIDE(ESO_SHOTSPEED);
	int pct = 100 + endlessEffectiveDepth() * ENDLESS_SPEED_PER_DEPTH_NUM / ENDLESS_SPEED_PER_DEPTH_DEN;
	if (endlessActiveMods & ENDLESS_MOD_SWIFT)
		pct += ENDLESS_SPEED_SWIFT;
	if (endlessActiveMods & ENDLESS_MOD_DILATION)
		pct -= ENDLESS_SPEED_DILATION;
	if (endlessActiveMods & ENDLESS_MOD_OVERCLOCK)
		pct += ENDLESS_SPEED_OVERCLOCK;
	if (endlessActiveMods & ENDLESS_MOD_OVERLOAD)
		pct += ENDLESS_SPEED_OVERLOAD;
	return endlessClamp(pct, ENDLESS_SPEED_MIN, ENDLESS_SPEED_MAX);
}

// The tide resumes the shot-DAMAGE climb past its intensity cap (notes.md §Difficulty ramp).
// Defined here (not in the tide block below) because a macro must precede its user.
#define ENDLESS_TIDE_DMG_STEP  3    // tide levels per +1% enemy shot damage past the 220 intensity cap
#define ENDLESS_TIDE_DMG_CAP   400  // absolute ceiling on the tide-boosted shot-damage percent (sanity backstop)

// Enemy shot-DAMAGE multiplier (100 = normal): +1.75% per (effective) level; DEVASTATING +75%.
// Capped lower than the others, then the tide resumes a SLOW climb (notes.md §Difficulty ramp).
int endlessShotDamagePercent(void)
{
	ENDLESS_OVERRIDE(ESO_SHOTDMG);
	int pct = 100 + endlessEffectiveDepth() * ENDLESS_DMG_PER_DEPTH_NUM / ENDLESS_DMG_PER_DEPTH_DEN;
	if (endlessActiveMods & ENDLESS_MOD_DEVASTATING)
		pct += ENDLESS_DMG_DEVASTATING;
	if (pct > ENDLESS_DMG_MAX)
		pct = ENDLESS_DMG_MAX;
	// The tide (0 until effective DEPTH 35, then +1 per effective depth, uncapped) adds a gentle
	// +1% per ENDLESS_TIDE_DMG_STEP ON TOP of the intensity cap: ~+30% by zone 100, ~+70% by
	// zone 200 on NORMAL. The high ENDLESS_TIDE_DMG_CAP is only a backstop; the consumer
	// (tyrian2.c) also clamps the final per-shot byte to 255, so a big multiplier can't wrap.
	pct += endlessTideLevel() / ENDLESS_TIDE_DMG_STEP;
	if (pct > ENDLESS_TIDE_DMG_CAP)
		pct = ENDLESS_TIDE_DMG_CAP;
	return pct;
}

// --- Rising tide: quantity scaling past the intensity caps ------------------------
// The intensity levers above saturate by ~effective depth 100-125; the tide adds the one axis
// with NO engine ceiling -- extra shots per volley and a rising elite/champion share -- off
// this single coefficient, staying 0 through the early hump (notes.md §Endless).
//
// NOTE the clock: TIDE_START is on the EFFECTIVE-DEPTH clock (endlessTideLevel subtracts it from
// endlessEffectiveDepth), while the TIDE_SHOT_* thresholds below are real ZONES on NORMAL
// (endlessDifficultyZone). Effective depth is real depth x1.25 on NORMAL, so the two are not
// interchangeable -- 35 effective depth is about real zone 28.
#define ENDLESS_TIDE_START      35   // effective DEPTH the tide begins (intensity is ~capped by here)
// Enemy "rising tide" of EXTRA shots per volley (see endlessExtraEnemyShots). NORMAL-difficulty
// baseline: the FIRST extra shot at ENDLESS_TIDE_SHOT_ONSET (zone 25), rising evenly to
// ENDLESS_TIDE_SHOT_ANCHOR_ADD (3) by the anchor zone (100), then +1 shot every ENDLESS_TIDE_SHOT_STEP
// zones with NO hard cap -- so 5 by zone 150, then climbing indefinitely (only the MAX sanity backstop
// and the enemy-shot pool bound it). Harder/easier difficulties travel this same curve sooner/later
// (scaled by the same rampPercent as the other enemy levers).
#define ENDLESS_TIDE_SHOT_ONSET      25   // ZONE (on NORMAL) the FIRST extra shot appears -- start of the early ramp
#define ENDLESS_TIDE_SHOT_ANCHOR     100  // ZONE (on NORMAL) the early ramp reaches ENDLESS_TIDE_SHOT_ANCHOR_ADD
#define ENDLESS_TIDE_SHOT_ANCHOR_ADD 3    // extra shots/volley at the anchor zone (on NORMAL)
#define ENDLESS_TIDE_SHOT_STEP       25   // past the anchor: +1 extra shot every this-many zones (so 5 by zone 150, then more)
#define ENDLESS_TIDE_SHOT_MAX        50   // sanity ceiling on added shots/volley (the enemy-shot pool caps total too)

// The single tide coefficient (the "knob"): 0 through the early game, then +1 per effective depth,
// uncapped. Everything the tide drives is derived from this.
int endlessTideLevel(void)
{
	ENDLESS_OVERRIDE(ESO_TIDE);
	if (!endlessFxActive())
		return 0;
	const int t = endlessEffectiveDepth() - ENDLESS_TIDE_START;
	return (t > 0) ? t : 0;
}

// Extra enemy shots per firing volley, difficulty-scaled like every other lever (endlessDifficultyZone).
// Two segments meeting at the anchor (zone 100 on NORMAL): an EARLY ramp -- first extra shot at the
// ONSET zone (25), rising evenly to ANCHOR_ADD (3) by the anchor -- then a steady +1 shot every STEP
// zones with NO hard cap (5 by zone 150 on NORMAL, then climbing). tyrian2.c fans these out around the
// weapon's own shots; the enemy-shot pool (ENEMY_SHOT_MAX) still hard-caps what reaches the screen
// (notes.md §Difficulty ramp).
// The tide's RAW extra-shot count, before any modifier touches it. Split out because
// endlessTideBoonsUnlocked must ask "is the tide adding shots at all" -- and it is called during course
// generation, when endlessActiveMods still holds the PREVIOUS sector's bits. Reading the public
// (Flak-Screen-adjusted) figure there would let one Flak Screen sector's own effect decide whether the
// next one may be charted.
static int endlessTideExtraShotsRaw(void)
{
	if (!endlessFxActive())
		return 0;
	const int zone = endlessDifficultyZone();

	int extra;
	if (zone >= ENDLESS_TIDE_SHOT_ANCHOR)
	{
		// Anchor onward: ANCHOR_ADD at the anchor, then +1 every STEP zones -- no hard cap beyond the
		// MAX sanity backstop (3 by zone 100, 5 by zone 150 on NORMAL, then climbing without bound).
		extra = ENDLESS_TIDE_SHOT_ANCHOR_ADD + (zone - ENDLESS_TIDE_SHOT_ANCHOR) / ENDLESS_TIDE_SHOT_STEP;
	}
	else if (zone >= ENDLESS_TIDE_SHOT_ONSET)
	{
		// Early ramp: 1 shot at the onset zone, rising evenly to ANCHOR_ADD by the anchor.
		extra = 1 + (ENDLESS_TIDE_SHOT_ANCHOR_ADD - 1) * (zone - ENDLESS_TIDE_SHOT_ONSET)
		              / (ENDLESS_TIDE_SHOT_ANCHOR - ENDLESS_TIDE_SHOT_ONSET);
	}
	else
	{
		extra = 0;
	}
	if (extra > ENDLESS_TIDE_SHOT_MAX)
		extra = ENDLESS_TIDE_SHOT_MAX;
	return extra;
}

int endlessExtraEnemyShots(void)
{
	ENDLESS_OVERRIDE(ESO_EXTRASHOTS);
	int extra = endlessTideExtraShotsRaw();
	// FLAK SCREEN boon: half the tide's ADDED shots never leave the barrel. Only this endless-specific
	// multiplication is thinned -- the level's authored volley is the `endlessBaseMulti` the caller adds
	// this to (tyrian2.c), so every shipped firing pattern still plays exactly as designed. Rounds the
	// kept half UP, so a lone extra shot stays one: the boon thins the tide, it never cancels it.
	if (endlessActiveMods & ENDLESS_MOD_FLAKSCREEN)
		extra = (extra + 1) / 2;
	return extra;
}

// Is FLAK SCREEN worth charting yet? It only removes shots the TIDE added, so before the tide starts
// (zone 25 on NORMAL) it would be an empty boon on the monitor. Gates every path that can emit the bit,
// exactly like endlessEliteBoonsUnlocked does for the no-elite-tier boons. Reads the RAW tide, never
// the Flak-Screen-adjusted figure -- see endlessTideExtraShotsRaw.
bool endlessTideBoonsUnlocked(void)
{
	return endlessTideExtraShotsRaw() > 0;
}

// --- Contact (ramming) damage ramp -------------------------------------------------
// The damage the PLAYER takes from colliding with an enemy climbs deep in a run, so trading hull for
// a ram stops being cheap: no bonus until the START zone (35), then linear to +ANCHOR_PCT (150%) by
// the anchor zone (100), the SAME slope onward, capped at +MAX_PCT (500%). Only the player's RECEIVED
// contact damage scales -- the damage the collision deals to the enemy is left untouched (mainint.c).
// Difficulty-scaled like every other lever (notes.md §Difficulty ramp).
#define ENDLESS_CONTACT_START      35   // zone the contact-damage climb begins (no bonus at/below)
#define ENDLESS_CONTACT_ANCHOR     100  // zone at which the bonus reaches ENDLESS_CONTACT_ANCHOR_PCT
#define ENDLESS_CONTACT_ANCHOR_PCT 150  // +this% player contact damage at the anchor zone
#define ENDLESS_CONTACT_MAX_PCT    500  // ceiling on the added player contact-damage percent
// SOFT LANDING boon: contact damage the player receives is cut to this % -- applied LAST, so it bites
// into the depth ramp and the elite/champion ram bonuses alike (a deep champion ram is exactly what the
// boon is for). Projectiles are untouched, which is what keeps it distinct from a general damage cut.
#define ENDLESS_CONTACT_SOFTLANDING 30

int endlessContactDamagePercent(void)
{
	ENDLESS_OVERRIDE(ESO_CONTACT);
	if (!endlessFxActive())
		return 100;
	const int zone = endlessDifficultyZone();
	int pct = 100;
	if (zone > ENDLESS_CONTACT_START)
	{
		int bonus = ENDLESS_CONTACT_ANCHOR_PCT * (zone - ENDLESS_CONTACT_START)
		              / (ENDLESS_CONTACT_ANCHOR - ENDLESS_CONTACT_START);
		if (bonus > ENDLESS_CONTACT_MAX_PCT)
			bonus = ENDLESS_CONTACT_MAX_PCT;
		pct = 100 + bonus;
	}
	if (endlessActiveMods & ENDLESS_MOD_SOFTLANDING)
	{
		pct = pct * ENDLESS_CONTACT_SOFTLANDING / 100;
		if (pct < 1)
			pct = 1;   // a scrape still costs something -- the boon softens ramming, it doesn't licence it
	}
	return pct;
}

// --- Elite enemies --------------------------------------------------------------
// A depth-scaled trickle of tougher, tinted, bounty-paying enemies. The roll is cached per
// linkgroup so a multi-tile enemy is one tier as a whole (notes.md §Difficulty ramp).

static signed char endlessEliteLink[256];  // per-linknum tier this level: -1 undecided, else 1/2/3

// MARTYRDOM per-level state: the dedup link, so a multi-tile enemy bursts once, mirroring
// endlessCountKill's "once per linked enemy". Reset at each level start.
static int endlessMartyrLastLink = 0;

// SHOCKWAVE's dedup link, the same idea one boon over (see endlessShockwaveRadius, further down).
// Declared here so endlessResetElites -- which runs before it -- can clear it with the martyr pair.
static int endlessShockwaveLastLink = 0;

// ...and the elite/champion BOUNTY dedup link (see endlessAwardEliteKill), so a multi-tile elite
// pays its bounty once instead of once per destroyed tile.
static int endlessBountyLastLink = 0;

void endlessResetElites(void)
{
	for (unsigned i = 0; i < COUNTOF(endlessEliteLink); ++i)
		endlessEliteLink[i] = -1;

	endlessMartyrLastLink = 0;     // fresh MARTYRDOM dedup each level
	endlessShockwaveLastLink = 0;  // ...and a fresh SHOCKWAVE dedup
	endlessBountyLastLink = 0;     // ...and a fresh bounty dedup, so the zone's first kill always pays
	endlessAegisReset();           // ...and a ready AEGIS GATE: a block never carries into the next zone

	// Seed this zone's elite/champion tier stream from the run seed + depth, so the rolls are
	// reproducible for a given seed. Own salt phase: a large offset that can't collide with the
	// outpost (depth*2), level/music (depth*2+1), light-cone (depth*2+0x40000000) or gravity-heading
	// (depth*2+0x60000000) streams. Every phase must be UNIQUE even across separate state variables:
	// the same salt derives the same SplitMix state, so a shared phase correlates the two sequences.
	endlessEliteRngState = endlessSplitMixSeed((Uint64)endlessRunDepth * 2 + 0x50000000);
}

// The depth-driven SPECIAL-enemy share BEFORE any mutator override: a 2% trickle rising to an 80%
// cap. endlessEliteChancePercent applies the Elite Pack / Apex / Legion overrides on top; the course
// generator also reads this directly, to retire a now-pointless "half enemies elite" once the natural
// share has already passed 50% (see endlessFixRedundantElitePack).
int endlessNaturalEliteChancePercent(void)
{
	ENDLESS_OVERRIDE(ESO_ELITECHANCE);
	int pct = 2 + endlessEffectiveDepth() / 2;
	if (pct > 25)
		// Past the 25% shoulder (effective depth 46), climb the last 55 points to the cap at
		// ~0.54/level, so the share reaches 80% at effective depth 148 (~zone 120 on Normal).
		pct = 25 + (endlessEffectiveDepth() - 46) * 27 / 50;
	if (pct > 80)
		pct = 80;                           // leave a true 100% to the Apex / Legion sectors
	return pct;
}

// Whether the no-elite-tier boons (NOCHAMP / NOELITE) are eligible to be charted yet. They only start
// appearing once the natural special-enemy share climbs PAST 25% -- below that, elites/champions are a
// rare trickle and "no champions / no elites" would be a near-empty boon. The 25% shoulder lands around
// effective depth 47, i.e. ~zone 38 on Normal (effective depth is real depth x1.25 there, so the two
// are NOT interchangeable), sooner on harder modes. Gates every generation path that can
// emit either bit; a leaked bit below the threshold is also scrubbed in endlessGenerateCourses.
bool endlessEliteBoonsUnlocked(void)
{
	return endlessNaturalEliteChancePercent() > 25;
}

// Chance (percent) that an eligible enemy becomes SPECIAL: the natural depth share above, except
// Elite Pack forces half and Apex/Legion force all (notes.md §Difficulty ramp). Elite Pack is only
// ever meant to RAISE the share, so the generator stops charting it once the natural share tops 50%
// -- otherwise it would CAP elites BELOW the natural rate (a stealth boon on a danger course).
static int endlessEliteChancePercent(void)
{
	if (endlessActiveMods & ENDLESS_MOD_NOELITE)
		return 0;                               // "no elites or champions" boon: nothing spawns special (wins over Elite Pack / Apex / Legion)
	if (endlessActiveMods & (ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION))
		return 100;
	if (endlessActiveMods & ENDLESS_MOD_ELITEPACK)
		return 50;
	return endlessNaturalEliteChancePercent();
}

// Roll one enemy's tier: 1 normal, 2 elite, 3 champion. Champions are ~half as common as
// elites (1 in 3 of the specials) -- except a LEGION sector makes every special a champion.
static int endlessPickTier(void)
{
	if ((int)(endlessEliteRand() % 100) >= endlessEliteChancePercent())  // seeded elite stream, per (seed, zone)
		return 1;  // normal
	if (endlessActiveMods & ENDLESS_MOD_NOCHAMP)
		return 2;  // "no champions" boon: this special stays an ELITE, never a champion (even under Legion)
	if (endlessActiveMods & ENDLESS_MOD_LEGION)
		return 3;  // every special is a champion
	// Among specials, the champion share climbs with the tide (tougher shooters deeper),
	// from the base ~1/3 toward a majority.
	int champPct = 33 + endlessTideLevel() / 3;
	if (champPct > 70)
		champPct = 70;
	return ((int)(endlessEliteRand() % 100) < champPct) ? 3 : 2;  // seeded elite stream, per (seed, zone)
}

int endlessRollEliteTier(JE_byte linknum)
{
	if (linknum == 0)  // lone single-tile enemy: independent per-enemy roll
		return endlessPickTier();
	// Multi-tile enemy: decide once for the whole linkgroup so every tile is the same tier.
	if (endlessEliteLink[linknum] < 0)
		endlessEliteLink[linknum] = (signed char)endlessPickTier();
	return endlessEliteLink[linknum];
}

// Elite/champion HP multiplier -- a damage divisor applied like the boss one: the enemy
// spends N damage per 1 armor, so it effectively has N times its HP. ~2x, up with depth.
//
// GIANT KILLER boon flattens it to 1: elites and champions still spawn, still wear their tint, still
// fire like elites and champions, and still pay their full bounty -- they just have ordinary hulls. That
// is what separates it from NOELITE, which deletes the tier (and its income) outright: Giant Killer
// leaves a sector full of profitable, killable specials rather than an empty one.
int endlessEliteHpMult(void)
{
	ENDLESS_OVERRIDE(ESO_ELITEHP);
	if (endlessActiveMods & ENDLESS_MOD_GIANTKILLER)
		return 1;
	return endlessClamp(ENDLESS_ELITE_HP_BASE + endlessEffectiveDepth() / ENDLESS_ELITE_HP_PER_X,
	                    ENDLESS_ELITE_HP_BASE, ENDLESS_ELITE_HP_MAX);
}

// The BARE elite depth curve at an arbitrary effective depth -- the elite counterpart of
// endlessBossRamp100, same purpose. Keep in step with endlessEliteHpMult above.
static int endlessEliteRamp100(int effDepth)
{
	return endlessClamp(ENDLESS_ELITE_HP_BASE * 100 + effDepth * 100 / ENDLESS_ELITE_HP_PER_X,
	                    ENDLESS_ELITE_HP_BASE * 100, ENDLESS_ELITE_HP_MAX * 100);
}

// The elite/champion multiplier unrounded, in hundredths -- the counterpart of
// endlessBossHpMult100 and for the same reason (the pierce lockout wants the ramp, not the
// integer the damage accumulator has to divide by).
static int endlessEliteHpMult100(void)
{
	if (endlessActiveMods & ENDLESS_MOD_GIANTKILLER)
		return 100;
	return endlessEliteRamp100(endlessEffectiveDepth());
}

// Combined per-hit HP divisor for an endless enemy. Non-boss elites/champions use the full
// elite multiplier; an elite/champion BOSS (already depth-scaled) gets a gentler x2 bump on
// top, capped so no enemy becomes an unkillable sponge. Ordinary enemies -> 1.
int endlessEnemyHpMult(bool hasBossBar, int bossHpMult, int eliteState)
{
	if (!hasBossBar)
		return (eliteState >= 2) ? endlessEliteHpMult() : 1;
	if (eliteState < 2 || (endlessActiveMods & ENDLESS_MOD_GIANTKILLER))
		return bossHpMult;                             // normal boss -- or GIANT KILLER, which drops the elite bump here too
	int mult = bossHpMult * 2;                         // elite/champion boss: gentle bump
	int cap  = (bossHpMult > ENDLESS_HP_MULT_MAX) ? bossHpMult : ENDLESS_HP_MULT_MAX;
	return (mult > cap) ? cap : mult;                  // capped, but never below the base
}

// How long a target ignores REPEAT piercing hits, in HUNDREDTHS of a sim tick. Deliberately
// mirrors endlessEnemyHpMult's shape and takes the same three arguments: the lockout exists to
// give back what an HP multiplier was supposed to buy, so it reads the SAME multiplier that
// target is carrying, and the tiering falls out of that rather than being bolted on.
//
//   boss (an enemy that explicitly has a boss health bar)  full, off the boss ramp
//   elite / champion                                       diluted -- their ramp is 2..4x
//   everything else                                        none at all
//
// Both ramps are read UNROUNDED so the figure creeps every zone rather than jumping a whole tick.
int endlessPierceLock100(bool hasBossBar, int hpMult, int eliteState)
{
	// Ordinary enemies answer first, and answer before the debug pin: "no lockout on ordinary
	// hulls" is structural, not a magnitude, so a pinned lever must not be able to introduce one.
	// It is also the cheap path -- the hit site asks this question for every hull a piercing bullet
	// touches, and most of them are ordinary.
	if (!hasBossBar && eliteState < 2)
		return 0;

	ENDLESS_OVERRIDE(ESO_PIERCELOCK);

	// The reference zone in the levers' own clock, at NORMAL. Fixed on purpose: the calibration
	// must not shift with the player's difficulty or with the sector's mutators, or the tuned
	// figures would mean a different thing in every run.
	const int refDepth = endlessEffectiveDepthOf(ENDLESS_PIERCE_LOCK_REF_ZONE - 1, 100);

	int span;      // how far above stock THIS target's multiplier sits, in hundredths
	int refSpan;   // the same span at the reference zone, for this tier
	int atRef;     // the tuned figure this tier reads at the reference zone

	if (hasBossBar)
	{
		int mult100 = endlessBossHpMult100();
		// Whatever hpMult holds BEYOND the plain depth ramp -- the elite-boss bump, expert mode's
		// own boss factor, endlessEnemyHpMult's cap -- is carried across as a ratio, so the lockout
		// tracks the hull this boss actually got rather than the one depth alone implies.
		const int stepped = endlessBossHpMult();
		if (stepped > 0 && hpMult > 0 && hpMult != stepped)
			mult100 = mult100 * hpMult / stepped;
		span    = mult100 - 100;
		refSpan = endlessBossRamp100(refDepth) - 100;
		atRef   = ENDLESS_PIERCE_LOCK_BOSS;
	}
	else
	{
		// Elite or champion (the ordinary tier already returned above). Their 2..4x ramp is what
		// dilutes the special tiers below the boss figure; the two tuned constants then separate a
		// champion from a plain elite (eliteState 3 vs 2 -- see varz.h).
		span    = endlessEliteHpMult100() - 100;
		refSpan = endlessEliteRamp100(refDepth) - 100;
		atRef   = (eliteState >= 3) ? ENDLESS_PIERCE_LOCK_CHAMP : ENDLESS_PIERCE_LOCK_ELITE;
	}

	if (span <= 0 || refSpan <= 0)
		return 0;   // stock HP (or GIANT KILLER flattening the elite ramp): nothing to give back
	return endlessClamp(span * atRef / refSpan,
	                    0, ENDLESS_PIERCE_LOCK_MAX * ENDLESS_PIERCE_LOCK_SCALE);
}

// Extra cash for destroying an elite / a champion (on top of the normal score value).
// Elite/champion bounties: base scales with depth, doubled by the Bounty Hunter perk, then
// scaled by the Scavenger cash multiplier. (endlessPerkCashPercent is defined above.)
long endlessEliteBounty(void)
{
	long b = 150 + (long)endlessRunDepth * 40;
	if (b > 2500)  // per-elite cap: the tide multiplies elite COUNT, so keep per-kill value bounded
		b = 2500;  // (else deep zones mint enough cash to trivially buy Overdrive -- the tide's own counter)
	if (endlessPerkOwned[PERK_BOUNTY])
		b *= 2;
	return b * endlessPerkCashPercent() / 100;
}
long endlessChampionBounty(void)
{
	long b = 350 + (long)endlessRunDepth * 90;
	if (b > 6000)  // per-champion cap (same reasoning as the elite cap above)
		b = 6000;
	if (endlessPerkOwned[PERK_BOUNTY])
		b *= 2;
	return b * endlessPerkCashPercent() / 100;
}

// Champion aggression, applied per-champion on top of the sector's global scaling: they
// fire noticeably faster and their shots hit harder.
//
// CLEAN SIGNALS boon returns both to neutral: the special tier keeps its HP, its tint and its bounty --
// so the sector still LOOKS and PAYS like an elite one -- but its guns behave like everyone else's. The
// exact complement of Giant Killer, which takes the hulls and leaves the guns.
int endlessChampionFireDelayPercent(void)
{
	if (endlessActiveMods & ENDLESS_MOD_CLEANSIGNALS)
		return 100;
	return 60;    // 0.6x cooldown (~1.7x fire rate)
}
int endlessChampionShotDamagePercent(void)
{
	if (endlessActiveMods & ENDLESS_MOD_CLEANSIGNALS)
		return 100;
	return 150;   // +50% shot damage
}

// The elite/champion RAM premium (elites +25%, champions +50%), on top of the depth contact ramp.
// This is the ONLY offensive bonus a plain ELITE carries -- the fire-rate and shot-damage bonuses above
// are champion-only -- so Clean Signals has to neutralise it too, or the boon would be champions-only
// while its monitor row promises the whole special tier. Distinct from Soft Landing, which scales ALL
// contact damage: this removes only the premium special enemies add, so the two stack without overlap.
int endlessEliteContactPercent(int eliteState)
{
	if (!endlessFxActive() || eliteState < 2 || (endlessActiveMods & ENDLESS_MOD_CLEANSIGNALS))
		return 100;
	return (eliteState == 3) ? 150 : 125;
}

// Award an elite/champion kill: pay the bounty and post a kill message to the in-game text bar.
//
// Called from enemy_logical_death (tyrian2.c) for EVERY killed enemy, elite or not -- exactly
// like endlessCountKill, and for the same reason. A multi-tile enemy is several enemy[] slots
// sharing one nonzero linknum, all removed consecutively in a single kill loop, so the bounty is
// deduped on that linknum: paying per tile handed a multipart elite two, three or more bounties for
// one kill. Feeding the latch every kill (rather than only elite ones) is what makes it safe -- an
// ordinary enemy dying in between breaks the run, so two same-linknum elites still both pay.
void endlessAwardEliteKill(int linknum, int eliteState)
{
	if (!endlessFxActive())
		return;

	const bool sameEnemy = (linknum != 0 && linknum == endlessBountyLastLink);
	endlessBountyLastLink = linknum;
	if (sameEnemy || eliteState < 2)
		return;

	const bool champion = (eliteState == 3);
	const long bounty = champion ? endlessChampionBounty() : endlessEliteBounty();
	player[0].cash += bounty;

	// Message reads the same for an elite/champion regular or boss.
	// Label stays left-aligned in the normal message-bar slot; only the cash bonus is right-aligned,
	// its rightmost pixel sitting on x=244 (before the HUD at x=299).
	char label[48], cash[24];
	snprintf(label, sizeof(label), "%s Enemy destroyed!", champion ? "Champion" : "Elite");
	snprintf(cash, sizeof(cash), "+%ld", bounty);
	JE_drawTextWindowSplit(label, cash, 244);
}

// --- Special-weapon pickups -----------------------------------------------------
// Endless has no data-cube archive and no secret-level warps, so the datacubes and secret
// orbs a shipped level drops would otherwise be dead pickups. Instead each grants a random
// SPECIAL weapon (Repulsor, Flare, ...), equipped instantly with a text-bar announcement.

// Number of sprites in the HUD "power-up" sheet (spriteSheet10). A Sprite2_array begins with
// a Uint16 offset table, one entry per sprite; entry[0] is the byte offset to sprite 1's
// pixels -- i.e. the table's own size -- so entry[0] / 2 is the sprite count. Used to reject
// specials whose icon index would blit past the table (see endlessGrantSpecial).
static unsigned endlessHudIconCount(void)
{
	if (spriteSheet10.data == NULL || spriteSheet10.size < sizeof(Uint16))
		return 0;
	return SDL_SwapLE16(((Uint16 *)spriteSheet10.data)[0]) / (unsigned)sizeof(Uint16);
}

// Name of the last special weapon endlessGrantSpecial granted this shop visit, shown in the
// E-Shop "Special Weapon" help line. Reset at shop entry (endlessResetShopPrices).
char endlessLastSpecialName[31] = "";

const char *endlessLastGrantedSpecial(void) { return endlessLastSpecialName; }

void endlessGrantSpecial(void)
{
	if (!endlessFxActive())
		return;

	// Gather the real, SAFE specials: non-empty name, a dispatcher-handled effect type
	// (stype 1..18), and an in-range itemgraphic -- the HUD redraws the equipped icon every
	// frame, so the bad icon several unfinished specials carry crashes instantly.
	const unsigned iconMax = endlessHudIconCount();

	// Invulnerability (stype 12 in JE_specialComplete -- the invulnerable_ticks effect) is
	// deliberately kept OUT of the endless pool: a Buy Special that could roll full
	// invulnerability would trivialize the run. Excluding by stype covers every
	// invulnerability entry in the data ("Invulnerability" and "Invulnerability [easier]").
	JE_byte pool[SPECIAL_NUM] = { 0 };
	int n = 0;
	for (int id = 1; id <= SPECIAL_NUM; ++id)
		if (special[id].name[0] != '\0' &&
		    special[id].stype >= 1 && special[id].stype <= 18 &&
		    special[id].stype != 12 &&  // never Invulnerability (see note above)
		    special[id].itemgraphic >= 1 && special[id].itemgraphic <= iconMax)
			pool[n++] = (JE_byte)id;
	if (n == 0)
		return;

	// Never hand back the special the player already has equipped -- a pickup/grant should feel like
	// a change, not a dud. Drop the current one from the pool, but only when something else remains
	// (if it's the sole valid special, keep it rather than grant nothing).
	if (n > 1)
	{
		const JE_byte current = player[0].items.special;
		for (int i = 0; i < n; ++i)
			if (pool[i] == current)
			{
				pool[i] = pool[--n];  // swap-remove; order is irrelevant for a uniform pick
				break;
			}
	}

	const JE_byte id = pool[mt_rand() % n];  // pickup/shop grant: gameplay RNG, not the seed
	player[0].items.special = id;
	shotMultiPos[SHOT_SPECIAL]  = 0;
	shotRepeat[SHOT_SPECIAL]    = 0;
	shotMultiPos[SHOT_SPECIAL2] = 0;
	shotRepeat[SHOT_SPECIAL2]   = 0;

	// Copy the granted special's name, trimming the padding whitespace some data names carry
	// (else the E-Shop help reads "Got NAME !" with a gap before the "!").
	const char *s = special[id].name;
	while (*s == ' ' || *s == '\t')
		++s;
	SDL_strlcpy(endlessLastSpecialName, s, sizeof(endlessLastSpecialName));
	for (size_t len = strlen(endlessLastSpecialName);
	     len > 0 && (endlessLastSpecialName[len - 1] == ' ' || endlessLastSpecialName[len - 1] == '\t'); )
		endlessLastSpecialName[--len] = '\0';

	char msg[64];
	snprintf(msg, sizeof(msg), "Special weapon:  %s", endlessLastSpecialName);
	JE_drawTextWindow(msg);
}

// --- Weapon-powerup drops -------------------------------------------------------------
// Pickup enemy ids, verified against tyrian.hdt: 533 has value -1 (front powerup), 534 value -2
// (rear powerup), 399 value 5000 (the top gem of the 390..399 ladder). All three sit in shapebank
// 21, so swapping one for another keeps the same sprite sheet.
#define ENEMY_FRONT_POWERUP 533
#define ENEMY_REAR_POWERUP  534
#define ENEMY_GEM_5000      399

// Can this port still take a powerup? Same test power_up_weapon uses for can_power_up, so an
// "open" port never means a pickup that silently converts to the +1000 cash consolation prize.
static bool endlessPortCanPowerUp(uint port)
{
	return player[0].items.weapon[port].id != 0 && player[0].items.weapon[port].power < 11;
}

// What an endless enemy drops in place of the vanilla "random special weapon" pickup.
//
// Events 33/45 turn a front-powerup dropper (enemy 533) into one of the six special-weapon
// droppers (829..834) on a roll weighted by front-gun power -- guaranteed at power 11, where a
// front powerup would be wasted. Endless already hands out a guaranteed random special for every
// datacube and secret orb it converts, so a third source only re-rolls what the player was just
// given; the displaced powerup is handed back instead, front or rear at even odds. No port checks
// here on purpose: the event fires long before the kill, so a full gun is caught at spawn time by
// endlessResolvePowerupDrop instead.
JE_word endlessPowerupDropEnemy(void)
{
	return (mt_rand() % 2) ? ENEMY_REAR_POWERUP : ENEMY_FRONT_POWERUP;  // drop: gameplay RNG, not the seed
}

// Spawn-time redirect for a powerup pickup whose gun is already full, hooked into JE_makeEnemy so
// it covers EVERY way one reaches the playfield -- the event-33/45 droppers above, the rear-powerup
// (534) drops a level scripts directly (which the 533-only substitution never touches), and pickups
// a level places by hand. Doing it at spawn instead of at the event also kills the staleness window:
// a gun that fills up between the event firing and the enemy dying would otherwise still drop the
// pickup the event chose. A full port falls through to the other gun, and with both full the drop
// pays out as the 5000-point gem rather than the cash consolation. Anything else passes through.
JE_word endlessResolvePowerupDrop(JE_word eDatI)
{
	if (eDatI != ENEMY_FRONT_POWERUP && eDatI != ENEMY_REAR_POWERUP)
		return eDatI;

	const uint want  = (eDatI == ENEMY_REAR_POWERUP) ? REAR_WEAPON : FRONT_WEAPON;
	const uint other = (want == REAR_WEAPON) ? FRONT_WEAPON : REAR_WEAPON;

	if (endlessPortCanPowerUp(want))
		return eDatI;
	if (endlessPortCanPowerUp(other))
		return (other == REAR_WEAPON) ? ENEMY_REAR_POWERUP : ENEMY_FRONT_POWERUP;
	return ENEMY_GEM_5000;
}

// --- Kill-fire buff HUD readout -------------------------------------------------------
// Live combo/timer/fire/damage numbers for JE_inGameDisplays -- the BUFF's own contribution
// only, and no buff NAME (just the numbers) by design.
int endlessKillBuffTicksLeft(void) { return endlessTurbodriveTimer; }
int endlessKillBuffTicksMax(void)  { return endlessBuffWindowTicks(); }

// The combo kill count driving the escalation (see endlessKillBuffFireDecrements) -- shown on
// the HUD as a plain "xN". Universal: climbs for Turbodrive and Overdrive alike (both refresh
// endlessComboKills the same way in endlessCountKill).
int endlessKillBuffComboCount(void)
{
	if (!endlessFxActive())
		return 0;
	return endlessComboKills;
}

// Themed HUD colour bank (matches the ship tints): red Turbodrive (bank 12 / 0xC0), yellow
// Overdrive (bank 7 / 0x70), blue Overblast (bank 9), bank 4 for an evil curse.
int endlessKillBuffColorBank(void)
{
	if (endlessActiveMods & ENDLESS_MOD_KILLFIRE_EVIL)
		return 4;
	if (endlessActiveMods & ENDLESS_MOD_OVERBLAST)
		return 9;   // blue -- the damage-only buff
	return (endlessActiveMods & ENDLESS_MOD_OVERDRIVE) ? 7 : 12;
}

// The buff's current fire-rate MULTIPLIER (1 = none; 2x..10x from the combo ramp -- same for
// Turbodrive and Overdrive). Derived from the same decrement count the fire block actually applies
// (endlessKillBuffFireDecrements) plus 1 for the weapon's own per-tick decrement, so the HUD
// multiplier can never drift from the real, combo-scaled rate.
int endlessKillBuffFireMultiplier(void)
{
	if (!endlessTurbodriveActive())
		return 1;
	return endlessKillBuffFireDecrements() + 1;
}

int endlessKillBuffDamagePercent(void)
{
	if (!endlessTurbodriveActive() || !(endlessActiveMods & ENDLESS_MOD_DMGUP))
		return 0;  // only the DAMAGE buffs (Overdrive/Overblast) grant a bonus -- Turbodrive is fire-only
	int pct = endlessBuffCharge * 2;  // cash-paid charge adds flat damage on top of the per-kill stacks
	pct += endlessOverdriveStacks * ENDLESS_OVERDRIVE_DMG_MAX / ENDLESS_OVERDRIVE_MAX_STACKS;  // Overdrive OR Overblast: +150% at full stacks (combo 200)
	return pct;
}

// Extra shotRepeat decrements this tick while a kill-fire BOON is up (multiplier = dec+1; the combo
// ramp up top). Returns 0 during an evil curse -- those SLOW fire (see endlessKillFireJamTicks).
int endlessKillBuffFireDecrements(void)
{
	if (!(endlessActiveMods & ENDLESS_MOD_FIREBOOST))
		return 0;  // only Turbodrive/Overdrive quicken fire (not Overblast); the evil mirrors slow it
	int steps = endlessComboKills / ENDLESS_COMBO_KILLS_PER_STEP;
	if (steps > ENDLESS_COMBO_MAX_STEPS)
		steps = ENDLESS_COMBO_MAX_STEPS;
	return 1 + steps;
}

// --- Evil kill-fire curses (Backfire / Burnout / Misfire): the hostile mirrors ------------------
// Is the currently-active kill-fire window an evil curse (slows fire / cuts damage) not a boon?
bool endlessKillFireIsEvil(void)
{
	return endlessFxActive() && endlessTurbodriveActive()
	    && (endlessActiveMods & ENDLESS_MOD_KILLFIRE_EVIL);
}

// Extra shotRepeat cooldown added to every shot while an evil curse is up (0 otherwise), so the guns
// fire SLOWER. Mirrors endlessKillBuffFireDecrements: ramps with the same combo steps, and Evil
// Overdrive piles on more from its per-kill stacks. Applied at shot-reset in shots.c (clamped there).
int endlessKillFireJamTicks(void)
{
	if (!endlessTurbodriveActive() || !(endlessActiveMods & ENDLESS_MOD_FIREJAM))
		return 0;  // only Backfire/Burnout jam fire (not Misfire, which only cuts damage)
	int steps = endlessComboKills / ENDLESS_COMBO_KILLS_PER_STEP;
	if (steps > ENDLESS_COMBO_MAX_STEPS)
		steps = ENDLESS_COMBO_MAX_STEPS;
	int add = ENDLESS_EVIL_JAM_BASE + steps * ENDLESS_EVIL_JAM_PER_STEP;
	if (endlessActiveMods & ENDLESS_MOD_BURNOUT)
		add += endlessOverdriveStacks * ENDLESS_EVIL_JAM_STACK_MAX / ENDLESS_OVERDRIVE_MAX_STACKS;
	return add;
}

// Evil Overdrive: the shot-damage REDUCTION % currently applied (0 otherwise). Peaks at
// ENDLESS_EVIL_DMG_MAX at full stacks; endlessPlayerDamagePercent subtracts it (with a floor).
int endlessKillBuffEvilDamagePenalty(void)
{
	if (!endlessKillFireIsEvil() || !(endlessActiveMods & ENDLESS_MOD_DMGDOWN))
		return 0;  // Burnout OR Misfire cut shot damage
	return endlessOverdriveStacks * ENDLESS_EVIL_DMG_MAX / ENDLESS_OVERDRIVE_MAX_STACKS;
}

// The one-word HUD label naming the active evil curse: JAMMED (Backfire) / BURNOUT / MISFIRE.
// Empty string when no evil kill-fire window is up. (A sector carries at most one evil mod.)
const char *endlessKillFireEvilName(void)
{
	if (!endlessKillFireIsEvil())
		return "";
	if (endlessActiveMods & ENDLESS_MOD_BURNOUT)
		return "BURNOUT";
	if (endlessActiveMods & ENDLESS_MOD_MISFIRE)
		return "MISFIRE";
	return "JAMMED";  // Backfire
}

// GRAVITY: a steady drag growing with zone AND difficulty; the absolute cap stays clear of the
// ship's top speed so full throttle can always climb (notes.md §Difficulty ramp). A plain well
// pulls straight down; an OMNIDIRECTIONAL well (ENDLESS_MOD_GRAVITY_OMNI) pulls along a fixed
// random heading chosen per sector -- same magnitude, so any single axis is still out-climbable.
#define ENDLESS_GRAVITY_BASE     1.6f   // px/tick at zone 0 on NORMAL (difficulty then scales this)
#define ENDLESS_GRAVITY_PER_ZONE 0.04f  // +px/tick per zone cleared, before the difficulty tilt
#define ENDLESS_GRAVITY_MAX      3.6f   // hard cap (72% of VT_VMAX): full throttle always climbs ~1.4 px/tick

// This sector's gravity heading (unit vector). Rolled per sector in endlessRollGravityDir; both ship
// paths read it via the X/Y drift helpers. Defaults to straight down so a well with no roll yet, or a
// plain (non-omni) well, behaves exactly as before.
static float endlessGravityDirX = 0.0f;
static float endlessGravityDirY = 1.0f;

// 16 evenly-spaced unit headings for an omnidirectional well: enough to feel "any direction" while
// avoiding a <math.h> dependency (removed from this file long ago) and staying bit-identical across
// the PC/Switch/Vita builds. A fixed heading per sector keeps it learnable rather than chaotic.
static const float endlessGravityHeadings[16][2] = {
	{  1.000f,  0.000f }, {  0.924f,  0.383f }, {  0.707f,  0.707f }, {  0.383f,  0.924f },
	{  0.000f,  1.000f }, { -0.383f,  0.924f }, { -0.707f,  0.707f }, { -0.924f,  0.383f },
	{ -1.000f,  0.000f }, { -0.924f, -0.383f }, { -0.707f, -0.707f }, { -0.383f, -0.924f },
	{  0.000f, -1.000f }, {  0.383f, -0.924f }, {  0.707f, -0.707f }, {  0.924f, -0.383f },
};

// Pick this sector's gravity heading: a fixed random one for an omni well, else straight down.
// Called once per level from endlessRegenerateLevel (in its own seeded reseed phase).
void endlessRollGravityDir(void)
{
	if (endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_GRAVITY_OMNI))
	{
		const unsigned h = endlessRand() % COUNTOF(endlessGravityHeadings);
		endlessGravityDirX = endlessGravityHeadings[h][0];
		endlessGravityDirY = endlessGravityHeadings[h][1];
	}
	else
	{
		endlessGravityDirX = 0.0f;
		endlessGravityDirY = 1.0f;  // classic Gravity Well: straight down
	}
}

float endlessGravityDrift(void)  // pull magnitude (px/tick), direction-agnostic
{
	// Responds to either bit so a debug-toggled bare OMNI (no GRAVITY) still pulls.
	if (!endlessFxActive() || !(endlessActiveMods & (ENDLESS_MOD_GRAVITY | ENDLESS_MOD_GRAVITY_OMNI)))
		return 0.0f;
	float g = (ENDLESS_GRAVITY_BASE + ENDLESS_GRAVITY_PER_ZONE * (float)endlessRunDepth)
	        * (float)endlessDifficultyRampPercent() / 100.0f;
	if (g > ENDLESS_GRAVITY_MAX)
		g = ENDLESS_GRAVITY_MAX;
	// SLUGGISH slows gravity in lock-step with the ship (endlessMoveScale is 1.0 when not sluggish, so
	// gravity-only sectors are unchanged). A sluggish+gravity sector then stays flyable: full throttle
	// out-climbs the pull by the same ratio it always did, just in the slowed frame -- no strand.
	return g * endlessMoveScale();
}

float endlessGravityDriftX(void) { return endlessGravityDrift() * endlessGravityDirX; }
float endlessGravityDriftY(void) { return endlessGravityDrift() * endlessGravityDirY; }

// Classic (non-VT) path: integer px/tick per axis that tracks the (fractional) drift. Each axis carries
// its own sub-pixel remainder between ticks so the integer nudge averages
// out to exactly the scaled drift, whatever the zone -- not a fixed 1/2 wobble. (int) truncates toward
// zero, so a negative component -- an omni well pulling up/left -- carries correctly too.
int endlessGravityPullX(void)
{
	static float accum = 0.0f;
	accum += endlessGravityDriftX();
	const int step = (int)accum;
	accum -= (float)step;
	return step;
}
int endlessGravityPullY(void)
{
	static float accum = 0.0f;
	accum += endlessGravityDriftY();
	const int step = (int)accum;
	accum -= (float)step;
	return step;
}

// SLUGGISH: the ship crawls. Like GRAVITY, the bite RAMPS with depth and difficulty -- barely slowed
// early, heavy deep in a run -- but floored so you can ALWAYS move. Returns the traverse-speed scale
// (1.0 = normal) applied to the committed per-tick displacement in BOTH ship paths (VT tyrian2.c +
// classic mainint.c), so keyboard, stick, mouse AND touch slow together. endlessGravityDrift multiplies
// gravity by this same factor, so a sluggish+gravity sector stays climbable (the pull slows to match).
#define ENDLESS_SLUGGISH_BASE     0.18f  // fraction slowed at zone 0 on NORMAL (=> 0.82x), before the difficulty tilt
#define ENDLESS_SLUGGISH_PER_ZONE 0.010f // +slowed per zone cleared
#define ENDLESS_SLUGGISH_MAX      0.55f  // hardest slow (=> 0.45x): still clearly movable, never a standstill
float endlessMoveScale(void)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_SLUGGISH))
		return 1.0f;
	float slow = (ENDLESS_SLUGGISH_BASE + ENDLESS_SLUGGISH_PER_ZONE * (float)endlessRunDepth)
	           * (float)endlessDifficultyRampPercent() / 100.0f;
	if (slow > ENDLESS_SLUGGISH_MAX)
		slow = ENDLESS_SLUGGISH_MAX;
	return 1.0f - slow;
}

// SHIELDLESS / DEADGEN: the shield stops recharging. SHIELDLESS just freezes regen (you keep the shield
// you have, then fight on armor once it's spent); DEADGEN is the dead-generator nightmare, which implies
// no regen too. tyrian2.c gates the shield-regen step on this.
bool endlessShieldRegenOff(void)
{
	return endlessFxActive() && (endlessActiveMods & (ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_DEADGEN));
}

// DEADGEN: the generator is dead, so it barely trickles charge -- the main gun (which draws generator
// power per shot, shots.c) sputters and shields never refill, while rear guns / sidekicks / specials
// (power-free) keep you in the fight. Never zero, so every weapon still EVENTUALLY fires -- brutal, not
// unwinnable. Returns the normal rate untouched when the modifier is off (byte-identical normal play).
#define ENDLESS_DEADGEN_POWER_ADD 2u   // generator charge per tick while dead (a normal generator adds ~5-23)
unsigned endlessGeneratorPowerAdd(unsigned normalAdd)
{
	if (endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_DEADGEN))
		return ENDLESS_DEADGEN_POWER_ADD;
	// STATIC DISCHARGE: a hit shorts the generator out for a moment -- no recharge at all while the
	// lockout runs, which is what lets the drained power actually stay drained (see the block below).
	if (endlessStaticLockoutActive())
		return 0;
	return normalAdd;
}

// --- AEGIS GATE / AUXILIARY REACTOR / LOW PROFILE / SHOCKWAVE boons ---------------------------
// Like the reactive dangers above, these four ride existing engine systems (the shield/armor split in
// JE_playerDamage, the shield-regen step, the two player hit-area tests, the enemy-death sites), so
// endless_combat.c only owns the decision and the numbers -- the hooks live where those systems do.

// AUXILIARY REACTOR: the shield still recharges on its normal interval, it just stops billing the
// generator for it. Distinct from Shield Matrix (which shortens the interval) and Efficient Coils
// (which discounts FIRING), so the three stack cleanly instead of overlapping.
bool endlessShieldRegenFree(void)
{
	return endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_AUXREACTOR);
}

// LOW PROFILE: scale a player hit-area half-extent. 75% of the stock box, applied at the collision
// TESTS rather than to player[].shot_hit_area_x/y, so (a) the ship sprite and the pickup reach are
// untouched -- shrinking the source fields would also shrink the item-collect box and the
// Countermeasure sweep -- and (b) the boon can't leak into a non-endless game. Every damaging
// collision test the player has (enemy projectiles in tyrian2.c, enemy contact in mainint.c) runs
// through this one helper, which is what keeps the reduced box consistent between them.
#define ENDLESS_LOWPROFILE_PCT 75
int endlessHitboxScale(int area)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_LOWPROFILE))
		return area;
	int a = area * ENDLESS_LOWPROFILE_PCT / 100;
	return (a < 1) ? 1 : a;   // never zero: a hitbox that can't be hit is a different (broken) boon
}

// AEGIS GATE: while the shield holds, a hit cannot spill through into armor -- the gate dumps whatever
// shield is left and stops there. THE COOLDOWN IS THE WHOLE BALANCE: without it, one regenerated shield
// point would block an entire champion volley forever, and Shield Matrix would make that trivial. So a
// block costs the gate ~2s of recharge, during which hits punch through normally.
//
// ...and THE MINIMUM SPILL is what makes it felt. A shield only ever overflows on the hit that finishes
// it, so the spill is whatever the shield couldn't cover -- frequently 1 point. Gating those spent the
// whole 2s window to save a single hull point, and left the gate on cooldown for the champion railgun
// that landed a moment later: the boon fired constantly and was worth almost nothing, which is exactly
// how it read in play. Skipping the trivial spills keeps the gate LOADED for the hits that matter.
#define ENDLESS_AEGIS_COOLDOWN  70  // ticks (~2s at the 35Hz sim) before the gate can block again
#define ENDLESS_AEGIS_MIN_SPILL  2  // ...and a spill smaller than this isn't worth spending it on

static int endlessAegisCooldown = 0;  // ticks until the gate is ready (per level; drained in endlessGameplayTick)

void endlessAegisTick(void)
{
	if (endlessAegisCooldown > 0)
		--endlessAegisCooldown;
}

void endlessAegisReset(void) { endlessAegisCooldown = 0; }

// May THIS hit be stopped at the shield? Returns true at most once per cooldown, and ARMS the cooldown
// when it does -- so the caller must act on a true (JE_playerDamage does, immediately). `shieldBefore`
// is the shield the hit landed on (a gate with nothing to spend blocks nothing) and `spill` is the
// damage that is about to reach armor -- i.e. what a block is actually worth.
bool endlessAegisGateConsume(int shieldBefore, int spill)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_AEGIS))
		return false;
	if (shieldBefore <= 0 || spill < ENDLESS_AEGIS_MIN_SPILL || endlessAegisCooldown > 0)
		return false;
	endlessAegisCooldown = ENDLESS_AEGIS_COOLDOWN;
	return true;
}

// SHOCKWAVE: an elite/champion death vaporises enemy projectiles around it, turning the sector's
// scariest targets into tactical objectives -- hold one alive through a bad patch, then pop it for room.
// Ordinary fodder does nothing, so the boon rewards picking the right target rather than just shooting.
#define ENDLESS_SHOCKWAVE_ELITE_RADIUS    40
#define ENDLESS_SHOCKWAVE_CHAMPION_RADIUS 60

bool endlessShockwaveActive(void)
{
	return endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_SHOCKWAVE);
}

int endlessShockwaveRadius(int linknum, int eliteState)
{
	if (!endlessShockwaveActive())
		return 0;

	// Latch BEFORE the elite test, exactly like endlessAwardEliteKill: a dedup link only holds if
	// EVERY kill feeds it, which is why enemy_logical_death calls this unconditionally. Testing
	// eliteState first left the last elite's linknum latched indefinitely -- ordinary kills never
	// cleared it -- so the next enemy to reuse that linknum read as another tile of it and its sweep
	// was silently skipped.
	const bool sameEnemy = (linknum != 0 && linknum == endlessShockwaveLastLink);
	endlessShockwaveLastLink = linknum;
	if (sameEnemy || eliteState < 2)
		return 0;                       // same multi-tile enemy as the last removed tile, or not elite

	return (eliteState == 3) ? ENDLESS_SHOCKWAVE_CHAMPION_RADIUS : ENDLESS_SHOCKWAVE_ELITE_RADIUS;
}

// --- MARTYRDOM / SEEKER / STATIC sector dangers ------------------------------------------------
// These three reuse the existing enemy-death, enemy-projectile and player-damage systems, so the
// bulk of each lives at its engine hook (tyrian2.c / varz.c). endless_combat.c owns the small
// per-modifier decisions -- whether the danger is active, and the numbers it feeds those hooks.

// MARTYRDOM: how many bullets a just-killed enemy's death burst fires -- 0 when the modifier is off,
// else 4 (normal) / 6 (elite) / 8 (champion). Dedups per linked enemy exactly like endlessCountKill
// (consecutive same-linknum removals are one enemy, so a multi-tile enemy bursts once, not per tile);
// linknum 0 is a lone enemy and always fires. Called unconditionally from enemy_logical_death
// (tyrian2.c), which does the spawning -- that is where the enemy-shot pool lives, and it also
// honours the "suppress when the pool is nearly full" rule, so this only decides the count.
int endlessMartyrdomBurstShots(int linknum, int eliteState)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_MARTYRDOM))
		return 0;
	if (linknum != 0 && linknum == endlessMartyrLastLink)
		return 0;                       // same multi-tile enemy as the last removed tile -- already burst
	endlessMartyrLastLink = linknum;
	return (eliteState == 3) ? 8 : (eliteState == 2) ? 6 : 4;
}

// The martyr burst's own bullet sprite: ONE fixed graphic, never the level's. It used to mirror the
// last enemy bullet fired this level, which made the burst change appearance mid-level as different
// shooters came on screen -- the burst has to be recognisable on sight, so it gets its own sprite.
// 100 is a fat radially-symmetric orb in spriteSheet8 (loaded once from tyrian.shp, so it is valid in
// every level and episode); symmetry matters because the burst fires in 4/6/8 directions at once.
#define ENDLESS_MARTYR_SHOT_SGR 100
JE_word endlessMartyrShotSprite(void) { return ENDLESS_MARTYR_SHOT_SGR; }

// SEEKER ROUNDS: true while newly-fired enemy shots should arm for their single mid-flight course
// correction (the arming + the one-time turn itself live at the enemy-shot sites in tyrian2.c).
bool endlessSeekerActive(void)
{
	return endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_SEEKER);
}

// STATIC DISCHARGE: the generator power a hit of `actualDamage` (shield+armor lost) should bleed --
// proportional to the damage, uncapped here (JE_playerDamage caps it at the current reserve). 0 when
// the modifier is off, or under a dead generator: DEADGEN already starves the generator, so stacking
// Static on it would add nothing (the spec's incompatibility), and generation never pairs the two.
//
// The per-damage multiplier is scaled for the RAW power pool (0..900, shown on the gauge as power/10):
// the spec's "damage x5" is on that displayed 0..90 scale, so it's ~x50 on the raw pool. Kept a bit
// below that (x30) so a common Static sector bites -- a small hit visibly dents the gauge, a heavier
// one can drop power under a shot's cost and briefly stall the front gun -- without a single moderate
// hit always emptying the reserve. Tune here if it wants to be nastier / gentler.
//
// THE DRAIN ALONE IS NOT ENOUGH, and this is the whole reason the first two attempts read as "doesn't
// work": the generator refills the pool EVERY TICK (tyrian2.c power += powerAdd, ~5-23/tick), so even
// draining all 900 to zero is repaid in about a second and the gauge just twitches. So a hit also
// LOCKS OUT regen for a short, damage-scaled window -- that's what makes the loss persist long enough
// to actually starve the guns (and shield regen, which spends power too). The lockout is applied at
// the one existing regen seam, endlessGeneratorPowerAdd, the same hook DEADGEN uses.
#define ENDLESS_STATIC_POWER_PER_DMG   30
#define ENDLESS_STATIC_POWER_MIN      150   // ...but ANY hit costs at least this much (1/6 of the bar), so a 1-2 point graze still reads
#define ENDLESS_STATIC_LOCKOUT_PER_DMG  6   // regen-lockout ticks per point of damage taken...
#define ENDLESS_STATIC_LOCKOUT_MIN     25   // ...with a floor of ~0.7s, so even a graze visibly stalls the generator...
#define ENDLESS_STATIC_LOCKOUT_MAX     70   // ...capped at ~2s, so even a huge hit can't strand you forever

static int endlessStaticLockout = 0;  // ticks of suppressed generator regen left (per level; drained in endlessGameplayTick)

// Is the generator currently locked out by a Static Discharge hit?
bool endlessStaticLockoutActive(void) { return endlessFxActive() && endlessStaticLockout > 0; }

// Tick down the lockout; called once per tick from endlessGameplayTick.
void endlessStaticLockoutTick(void)
{
	if (endlessStaticLockout > 0)
		--endlessStaticLockout;
}

// Clear it at level start, so a sector's discharge can't bleed into the next zone.
void endlessStaticLockoutReset(void) { endlessStaticLockout = 0; }

unsigned endlessStaticDischargeDrain(unsigned actualDamage)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_STATIC) || (endlessActiveMods & ENDLESS_MOD_DEADGEN))
		return 0;
	// Arm the regen lockout alongside the drain (longest window wins, so a fresh hit never shortens
	// one already running), then report the power to bleed. Both have a floor, so even a 1-point
	// graze is felt rather than lost in the generator's normal churn.
	int lock = (int)actualDamage * ENDLESS_STATIC_LOCKOUT_PER_DMG;
	if (lock < ENDLESS_STATIC_LOCKOUT_MIN)
		lock = ENDLESS_STATIC_LOCKOUT_MIN;
	if (lock > ENDLESS_STATIC_LOCKOUT_MAX)
		lock = ENDLESS_STATIC_LOCKOUT_MAX;
	if (lock > endlessStaticLockout)
		endlessStaticLockout = lock;

	unsigned drain = actualDamage * ENDLESS_STATIC_POWER_PER_DMG;
	if (drain < ENDLESS_STATIC_POWER_MIN)
		drain = ENDLESS_STATIC_POWER_MIN;
	return drain;
}

// Player shot-damage scale (100 = normal): OVERCHARGE is a flat +50%; Overdrive adds +2.5%
// per active kill-stack on top. Applied to the player's shots (tyrian2.c).
int endlessPlayerDamagePercent(void)
{
	ENDLESS_OVERRIDE(ESO_PLAYERDMG);
	if (!endlessFxActive())
		return 100;
	int pct = (endlessActiveMods & ENDLESS_MOD_OVERCHARGE) ? 150 : 100;
	if ((endlessActiveMods & ENDLESS_MOD_DMGUP) && endlessTurbodriveActive())
		pct += endlessOverdriveStacks * ENDLESS_OVERDRIVE_DMG_MAX / ENDLESS_OVERDRIVE_MAX_STACKS;  // Overdrive OR Overblast: +150% at full stacks (combo 200)
	if (endlessTurbodriveActive() && (endlessActiveMods & ENDLESS_MOD_DMGUP))
		pct += endlessBuffCharge * 2;  // cash-paid charge adds damage only to the DAMAGE buffs (Overdrive/Overblast), not fire-only Turbodrive
	pct += endlessPerkOwned[PERK_DAMAGE] * ENDLESS_PERK_DAMAGE_PCT;  // Heavy Rounds perk (run-persistent)
	if (endlessPerkOwned[PERK_GLASSCANNON])
		pct += ENDLESS_PERK_GLASS_DMG;                              // Glass Cannon relic (paired with -armor)
	// Burnout / Misfire: each kill stacks a shot-damage CUT (mirror of Overdrive/Overblast's bonus),
	// floored so you can still fight. Applied last so it bites into every other bonus.
	if ((endlessActiveMods & ENDLESS_MOD_DMGDOWN) && endlessTurbodriveActive())
	{
		pct -= endlessKillBuffEvilDamagePenalty();
		if (pct < ENDLESS_EVIL_DMG_FLOOR)
			pct = ENDLESS_EVIL_DMG_FLOOR;
	}
	return pct;
}

// Flat reduction applied to each hit the player takes (Bulwark relic), applied in JE_playerDamage.
// Always leaves at least 1 damage so it can't make the player invulnerable.
int endlessPlayerDamageReduce(void)
{
	if (!endlessFxActive())
		return 0;
	return endlessPerkOwned[PERK_BULWARK] * ENDLESS_PERK_BULWARK;
}

// Single source of truth for the scroll multiplier. Bound fixed-motion scripts use this too,
// while sky/local scripts deliberately do not. notes.md §Endless scroll boost.
int endlessScrollBoostPercent(void)
{
	if (!endlessFxActive())
		return 0;
	if (endlessActiveMods & (ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_WARP))
		return 220;
	if (endlessActiveMods & (ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_SLIPSTREAM))
		return 70;
	return 0;
}

// True while a scroll-speed modifier is active -- STABLE across ticks, unlike the fractional
// step count, so the bg bottom-margin gate can't flicker (notes.md §Endless scroll boost).
bool endlessScrollBoostActive(void)
{
	return endlessScrollBoostPercent() != 0;
}

// Smooth vertical scroll for ONE layer: outputs the constant display rate + sub-pixel fraction, and
// (only under a scroll modifier) returns extra px so base + extra tracks a boosted target. Call once
// per channel per tick; channel 0/1/2 = bg layer 1/2/3. notes.md §Slow-scroll smoothing.
int endlessScrollExtraPx(int channel, int fireStep, int delayMax, int baseThisTick,
                         float *rateOut, float *fracOut)
{
	static int carry[3] = { 0, 0, 0 };  // signed pending extra scroll, px*100, per channel
	static int trem[3]  = { 0, 0, 0 };  // remainder of the target's /delayMax division, per channel
	if (rateOut != NULL)
		*rateOut = 0.0f;
	if (fracOut != NULL)
		*fracOut = 0.0f;
	if (channel < 0 || channel > 2)
		return 0;
	const int boost = endlessScrollBoostPercent();
	if (fireStep <= 0)  // the layer isn't scrolling this section
	{
		carry[channel] = 0;
		trem[channel]  = 0;
		return 0;
	}
	if (delayMax < 1)
		delayMax = 1;
	// Target per-tick scroll (px*100): average base rate fireStep/delayMax scaled by the modifier,
	// remainder carried so the long-run average is exact. boost 0 (no modifier) still runs -- the
	// base rate alone is smoothed for the display. notes.md §Slow-scroll smoothing.
	int tnum = fireStep * (100 + boost) + trem[channel];
	int target = tnum / delayMax;
	trem[channel] = tnum - target * delayMax;
	carry[channel] += target - baseThisTick * 100;
	// Only a real modifier drains whole px to ADD scroll (base + extra tracks the target); the base
	// rate alone emits none, so the sim scroll stays byte-identical to the stock game.
	int px = 0;
	if (boost > 0 && carry[channel] >= 100)
	{
		px = carry[channel] / 100;
		carry[channel] -= px * 100;
	}
	if (carry[channel] > 5000 || carry[channel] < -5000)  // guard runaway if a layer is never drawn
		carry[channel] = 0;
	if (rateOut != NULL)
		*rateOut = (float)target / 100.0f;
	if (fracOut != NULL)
		*fracOut = (float)carry[channel] / 100.0f;
	return px;
}

// Player-ship blit filter (0 = none). Only PLAYER-SIDE buffs tint the hull, while the kill-fire
// boost is active: Overdrive burns red, plain Turbodrive electric yellow. (The
// E-Shop "Turbodrive"/"Overdrive" buys and a Turbodrive sector all directly buff the player;
// Overclock/Overload change ENEMY behaviour, so they no longer tint the ship.)
int endlessShipTintFilter(void)
{
	if (!endlessFxActive())
		return 0;
	if (endlessTurbodriveActive())  // active kill-fire window (boost OR evil curse)
	{
		if (endlessActiveMods & ENDLESS_MOD_KILLFIRE_EVIL)
			return ENDLESS_EVIL_SHIP_FILTER;         // ominous curse tint
		if (endlessActiveMods & ENDLESS_MOD_OVERBLAST)
			return ENDLESS_OVERBLAST_SHIP_FILTER;    // blue -- damage-only buff
		return (endlessActiveMods & ENDLESS_MOD_OVERDRIVE)
		       ? ENDLESS_OVERDRIVE_SHIP_FILTER   // electric yellow
		       : ENDLESS_TURBODRIVE_SHIP_FILTER;   // red
	}
	return 0;
}

// --- Debug scaling readout & override tables --------------------------------------
// Placed last so the editing bounds can cite every lever's own tunables (see the ENDLESS_OVERRIDE
// macro up top, which is all the accessors themselves need).

// `key` is the config-file name a pinned lever is stored under -- it is ON DISK, so rename a display
// name freely but never one of these.
static const struct { const char *name; const char *key; int lo, hi; } endlessOverrideInfo[ESO_COUNT] = {
	[ESO_ARMOR]       = { "Enemy HP %",      "enemy_hp",      ENDLESS_HP_MIN,                ENDLESS_HP_MAX },
	[ESO_BOSSHP]      = { "Boss HP x",       "boss_hp",       1,                             ENDLESS_BOSS_MAX },
	[ESO_FIREDELAY]   = { "Fire Cooldown %", "fire_cooldown", 100 - ENDLESS_FIRE_MAX_REDUCE, 400 },
	[ESO_SHOTSPEED]   = { "Shot Speed %",    "shot_speed",    ENDLESS_SPEED_MIN,             ENDLESS_SPEED_MAX },
	[ESO_SHOTDMG]     = { "Shot Damage %",   "shot_damage",   10,                            ENDLESS_TIDE_DMG_CAP },
	[ESO_CONTACT]     = { "Ram Damage %",    "ram_damage",    1,                             100 + ENDLESS_CONTACT_MAX_PCT },
	[ESO_TIDE]        = { "Tide Level",      "tide",          0,                             400 },
	[ESO_EXTRASHOTS]  = { "Extra Shots",     "extra_shots",   0,                             ENDLESS_TIDE_SHOT_MAX },
	[ESO_ELITECHANCE] = { "Elite Share %",   "elite_share",   0,                             100 },
	[ESO_ELITEHP]     = { "Elite HP x",      "elite_hp",      1,                             ENDLESS_HP_MULT_MAX },
	[ESO_PLAYERDMG]   = { "Your Damage %",   "your_damage",   10,                            1000 },
	[ESO_PIERCELOCK]  = { "Boss Pierce Lock","boss_pierce",   0,                             ENDLESS_PIERCE_LOCK_MAX * ENDLESS_PIERCE_LOCK_SCALE },
};

const char *endlessScalingOverrideName(int id)
{
	return (id >= 0 && id < ESO_COUNT) ? endlessOverrideInfo[id].name : "";
}
const char *endlessScalingOverrideKey(int id)
{
	return (id >= 0 && id < ESO_COUNT) ? endlessOverrideInfo[id].key : "";
}
int endlessScalingOverrideMin(int id)
{
	return (id >= 0 && id < ESO_COUNT) ? endlessOverrideInfo[id].lo : 0;
}
int endlessScalingOverrideMax(int id)
{
	return (id >= 0 && id < ESO_COUNT) ? endlessOverrideInfo[id].hi : 0;
}

void endlessScalingOverridesClear(void)
{
	memset(endlessScalingOverride, 0, sizeof(endlessScalingOverride));
}

int endlessScalingOverrideCount(void)
{
	int n = 0;
	for (int i = 0; i < ESO_COUNT; ++i)
		if (endlessScalingOverride[i].active)
			++n;
	return n;
}

// What a lever would read RIGHT NOW if it weren't pinned -- for the debug page's "stock vs pinned"
// column, and to seed a freshly-armed override with the live value so arming one never jumps the
// difficulty by itself. Momentarily disarms that one override and calls the real accessor, so there
// is no second copy of any formula to drift out of step.
int endlessScalingOverrideStock(int id)
{
	if (id < 0 || id >= ESO_COUNT)
		return 0;
	// Indexed once, right after the bounds check: the accessor calls below are opaque enough that
	// static analysis loses `id`'s proven range and reports the restore as a buffer overrun (C6386).
	EndlessScalingOverride *const ov = &endlessScalingOverride[id];
	const bool was = ov->active;
	ov->active = false;
	int v = 0;
	switch (id)
	{
	case ESO_ARMOR:       v = endlessArmorPercent();              break;
	case ESO_BOSSHP:      v = endlessBossHpMult();                break;
	case ESO_FIREDELAY:   v = endlessFireDelayPercent();          break;
	case ESO_SHOTSPEED:   v = endlessShotSpeedPercent();          break;
	case ESO_SHOTDMG:     v = endlessShotDamagePercent();         break;
	case ESO_CONTACT:     v = endlessContactDamagePercent();      break;
	case ESO_TIDE:        v = endlessTideLevel();                 break;
	case ESO_EXTRASHOTS:  v = endlessExtraEnemyShots();           break;
	case ESO_ELITECHANCE: v = endlessNaturalEliteChancePercent(); break;
	case ESO_ELITEHP:     v = endlessEliteHpMult();               break;
	case ESO_PLAYERDMG:   v = endlessPlayerDamagePercent();       break;
	// The only lever that is a function of another one: read it at the boss multiplier this zone
	// actually produces, which is what the run's bosses will be carrying.
	// Reported at the BOSS tier -- the tier the lever is there to tune; the special tiers follow it down.
	case ESO_PIERCELOCK:  v = endlessPierceLock100(true, endlessBossHpMult(), 1); break;
	default: break;
	}
	ov->active = was;
	return v;
}

// Compute the whole ramp for an arbitrary (zone, difficulty, mods) triple. Swaps the three globals
// every lever reads, calls the REAL accessors, then puts them back -- so the readout can never
// drift from the formulas, and a snapshot taken mid-level leaves the live run untouched.
//
// Three things the caller has to know. (1) The effect layer is FORCED ON for the duration, so the
// page describes the ramp itself rather than whether it currently applies -- otherwise the handful
// of endlessFxActive-gated levers would read as flat zeroes whenever the layer is switched off,
// which is exactly when someone is most likely to be looking the curve up. Whether it applies is
// the master toggle's job to state, and it does. (2) Overrides are deliberately left ARMED: a
// pinned lever reads pinned rather than showing a stock curve the run is not following. (3)
// fireDelayPct folds in whatever ENRAGE and RETALIATION contribute at this instant, since both key
// off live per-level timers rather than depth -- outside a level both are idle and the figure is
// the pure depth ramp.
void endlessScalingSnapshot(int zone, int difficulty, Uint64 mods, EndlessScaling *out)
{
	if (out == NULL)
		return;

	const int         saveDepth = endlessRunDepth;
	const JE_shortint saveDiff  = difficultyLevel;
	const Uint64      saveMods  = endlessActiveMods;
	const JE_boolean  saveCamp  = endlessCampaignMods;

	endlessRunDepth     = (zone > 0) ? zone - 1 : 0;
	endlessActiveMods   = mods;
	endlessCampaignMods = true;   // see (1) above
	if (difficulty >= 0)
		difficultyLevel = (JE_shortint)difficulty;

	out->effDepth     = endlessEffectiveDepth();
	out->diffZone     = endlessDifficultyZone();
	out->rampPercent  = endlessDifficultyRampPercent();
	out->armorPct     = endlessArmorPercent();
	out->bossMult     = endlessBossHpMult();
	out->fireDelayPct = endlessFireDelayPercent();
	out->shotSpeedPct = endlessShotSpeedPercent();
	out->shotDmgPct   = endlessShotDamagePercent();
	out->tide         = endlessTideLevel();
	out->extraShots   = endlessExtraEnemyShots();
	out->contactPct   = endlessContactDamagePercent();
	out->elitePct     = endlessNaturalEliteChancePercent();
	out->eliteHpMult  = endlessEliteHpMult();
	out->playerDmgPct = endlessPlayerDamagePercent();
	out->pierceLock100 = endlessPierceLock100(true, out->bossMult, 1);  // hundredths of a tick, at the boss tier
	out->eliteBounty  = endlessEliteBounty();
	out->champBounty  = endlessChampionBounty();

	endlessRunDepth     = saveDepth;
	difficultyLevel     = saveDiff;
	endlessActiveMods   = saveMods;
	endlessCampaignMods = saveCamp;
}
