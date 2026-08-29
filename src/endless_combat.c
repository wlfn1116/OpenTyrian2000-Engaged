/* Endless combat scaling, special tiers, and player-side modifiers. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "helptext.h"
#include "joystick.h"
#include "lvlmast.h"
#include "mainint.h"
#include "mtrand.h"
#include "network.h"
#include "player.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Combo count remains uncapped; only derived bonuses cap.
int endlessComboKills[2] = { 0, 0 };
#define ENDLESS_COMBO_KILLS_PER_STEP 25
#define ENDLESS_COMBO_MAX_STEPS       8

#define ENDLESS_EVIL_JAM_BASE         3
#define ENDLESS_EVIL_JAM_PER_STEP     3
#define ENDLESS_EVIL_JAM_STACK_MAX   10
#define ENDLESS_EVIL_DMG_MAX         75
#define ENDLESS_EVIL_DMG_FLOOR       25

// Difficulty multiplier for effective depth.
static int endlessDifficultyRampPercent(void)
{
	switch (difficultyLevel)
	{
	case DIFFICULTY_WIMP:       return 50;
	case DIFFICULTY_EASY:       return 75;
	case DIFFICULTY_NORMAL:     return 100;
	case DIFFICULTY_HARD:       return 120;
	case DIFFICULTY_IMPOSSIBLE: return 134;
	default:                    return 160;
	}
}

// Combat depth is real depth x1.25 with the difficulty tilt applied.
static int endlessEffectiveDepthOf(int runDepth, int rampPercent)
{
	return runDepth * rampPercent * 5 / 400;
}

static int endlessEffectiveDepth(void)
{
	return endlessEffectiveDepthOf(endlessRunDepth, endlessDifficultyRampPercent());
}

// Player-facing thresholds use this difficulty-adjusted zone.
int endlessDifficultyZone(void)
{
	return 1 + endlessRunDepth * endlessDifficultyRampPercent() / 100;
}

// Enemy intensity tuning.

// Ordinary-enemy HP, percent of stock. The armor byte stops at 254, so growth past
// ENDLESS_HP_MAX is spent through the damage divisor instead (see endlessArmorOverflow100).
#define ENDLESS_HP_PER_DEPTH       4    // +% per effective depth, which is +5%/zone on Normal
#define ENDLESS_HP_FORTIFIED     120    // FORTIFIED: +% (2.2x HP)
#define ENDLESS_HP_FRAGILE        50    // FRAGILE: -%
#define ENDLESS_HP_MIN            25    // floor: a FRAGILE zone-0 enemy still takes a hit
#define ENDLESS_HP_MAX           600    // spawn-armor ceiling, reached at effective depth 125
#define ENDLESS_HP_OVERFLOW_MAX 1200    // total ceiling, at depth 275 (zone 221 on Normal)

// Boss and special-tier HP are whole multipliers (1 = stock), each read off a ramp of anchors.
// The clamps below also bound their debug overrides. See doc/notes.md#health-and-tiers.
#define ENDLESS_BOSS_FORTIFIED     3    // FORTIFIED: +this many x (a 4x boss at depth 0)
#define ENDLESS_BOSS_MARKED        2    // gamble "Marked": the boss you paid to forget comes back bulked up
#define ENDLESS_BOSS_MAX          32    // ceiling, at depth 247 (zone 199 on Normal)

#define ENDLESS_ELITE_HP_BASE      2    // multiplier at depth 0
#define ENDLESS_ELITE_HP_MAX       6    // ceiling, at depth 122 (zone 99 on Normal)

// Repeat-pierce delay at the reference zone, in hundredths of a tick.
#define ENDLESS_PIERCE_LOCK_REF_ZONE      50  // the zone the three figures below were tuned at
#define ENDLESS_PIERCE_LOCK_BOSS          10  // boss:     0.10 tick at the reference zone
#define ENDLESS_PIERCE_LOCK_CHAMP		   5  // champion: 0.05
#define ENDLESS_PIERCE_LOCK_ELITE          2  // elite:    0.02
#define ENDLESS_PIERCE_LOCK_MAX            1

// Piercing damage as a percentage of the weapon-table value. Fractional carry preserves changes
// below one point; see doc/notes.md#combat.
#define ENDLESS_PIERCE_POTENCY_PER_DEPTH   5  // +% per effective depth (ENDLESS_HP_PER_DEPTH is 4)
#define ENDLESS_PIERCE_POTENCY_MAX       500  // ceiling, reached at effective depth 80

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
#define ENDLESS_RETALIATION_FIRE_PCT 80 // applied after the additive cap

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

// Debug overrides bypass both depth and modifiers.
EndlessScalingOverride endlessScalingOverride[ESO_COUNT];

#define ENDLESS_OVERRIDE(id) \
	do { if (endlessScalingOverride[id].active) return endlessScalingOverride[id].value; } while (0)

// Ordinary enemy HP percentage of stock, before the spawn-armor ceiling. Modifiers apply here so
// they keep their weight in the overflow above ENDLESS_HP_MAX as well.
static int endlessArmorPercentTotal(void)
{
	if (endlessScalingOverride[ESO_ARMOR].active)
		return endlessScalingOverride[ESO_ARMOR].value;

	int pct = 100 + endlessEffectiveDepth() * ENDLESS_HP_PER_DEPTH;
	if (endlessActiveMods & ENDLESS_MOD_FORTIFIED)
		pct += ENDLESS_HP_FORTIFIED;
	if (endlessActiveMods & ENDLESS_MOD_FRAGILE)
		pct -= ENDLESS_HP_FRAGILE;
	return endlessClamp(pct, ENDLESS_HP_MIN, ENDLESS_HP_OVERFLOW_MAX);
}

// What JE_makeEnemy scales spawn armor by. The armor byte cannot hold more than 254, so this stops
// at ENDLESS_HP_MAX and endlessArmorOverflow100 carries the rest.
int endlessArmorPercent(void)
{
	return endlessClamp(endlessArmorPercentTotal(), ENDLESS_HP_MIN, ENDLESS_HP_MAX);
}

// The remainder of the ordinary HP curve, in hundredths, spent through the damage divisor rather
// than the armor byte. 100 until the curve passes the spawn-armor ceiling.
int endlessArmorOverflow100(void)
{
	if (!endlessFxActive())
		return 100;
	const int total = endlessArmorPercentTotal();
	return (total > ENDLESS_HP_MAX) ? total * 100 / ENDLESS_HP_MAX : 100;
}

// An HP ramp is a piecewise-linear walk through anchors, read in hundredths of a multiplier.
typedef struct { int depth, mult100; } EndlessRampAnchor;

static int endlessRampAt100(const EndlessRampAnchor *pts, unsigned count, int effDepth)
{
	if (effDepth <= pts[0].depth)
		return pts[0].mult100;

	for (unsigned i = 1; i < count; ++i)
	{
		if (effDepth >= pts[i].depth)
			continue;
		const int span = pts[i].depth - pts[i - 1].depth;
		const int rise = pts[i].mult100 - pts[i - 1].mult100;
		return pts[i - 1].mult100 + (effDepth - pts[i - 1].depth) * rise / span;
	}
	return pts[count - 1].mult100;
}

// Boss HP. The first segment is the curve the mode shipped with, so every zone below depth 64
// is untouched; the two after it carry the raised ceiling, landing 20x and 32x on the zones
// before a GRAND milestone.
static const EndlessRampAnchor endlessBossAnchors[] = {
	{   0,  100 },   // 1x at the surface
	{  64,  900 },   // 9x, zone 53 on Normal
	{ 122, 2000 },   // 20x, zone 99
	{ 247, 3200 },   // 32x, zone 199
};

static int endlessBossRamp100(int effDepth)
{
	return endlessRampAt100(endlessBossAnchors, COUNTOF(endlessBossAnchors), effDepth);
}

// Boss HP divisor in hundredths, which is what damage is spent through. The debug override is
// pinned in whole x and scaled here. See doc/notes.md#health-and-tiers.
int endlessBossHpMult100(void)
{
	if (endlessScalingOverride[ESO_BOSSHP].active)
		return endlessScalingOverride[ESO_BOSSHP].value * ENDLESS_HP_MULT_SCALE;

	int mult = endlessBossRamp100(endlessEffectiveDepth());
	if (endlessActiveMods & ENDLESS_MOD_FORTIFIED)
		mult += ENDLESS_BOSS_FORTIFIED * ENDLESS_HP_MULT_SCALE;
	if (endlessActiveMods & ENDLESS_MOD_MARKED)
		mult += ENDLESS_BOSS_MARKED * ENDLESS_HP_MULT_SCALE;
	if (endlessActiveMods & ENDLESS_MOD_FRAGILE)
		mult = (mult + 1) / 2;
	return endlessClamp(mult, ENDLESS_HP_MULT_SCALE, ENDLESS_BOSS_MAX * ENDLESS_HP_MULT_SCALE);
}

// Whole-x reading of the same curve, which is what the pierce delay is calibrated against.
int endlessBossHpMult(void)
{
	return endlessBossHpMult100() / ENDLESS_HP_MULT_SCALE;
}

// Enemy cooldown percentage; lower is faster.
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
	// Multiplication keeps Retaliation effective after the additive reduction caps.
	if ((endlessActiveMods & ENDLESS_MOD_RETALIATION) && endlessRetaliationTimer > 0)
		pct = pct * ENDLESS_RETALIATION_FIRE_PCT / 100;
	return pct;
}

// Enemy shot-speed percentage.
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

// The tide continues shot-damage growth after the intensity cap.
#define ENDLESS_TIDE_DMG_STEP  3    // tide levels per +1% enemy shot damage past the 220 intensity cap
#define ENDLESS_TIDE_DMG_CAP   400  // absolute ceiling on the tide-boosted shot-damage percent (sanity backstop)

// Enemy shot-damage percentage.
int endlessShotDamagePercent(void)
{
	ENDLESS_OVERRIDE(ESO_SHOTDMG);
	int pct = 100 + endlessEffectiveDepth() * ENDLESS_DMG_PER_DEPTH_NUM / ENDLESS_DMG_PER_DEPTH_DEN;
	if (endlessActiveMods & ENDLESS_MOD_DEVASTATING)
		pct += ENDLESS_DMG_DEVASTATING;
	if (pct > ENDLESS_DMG_MAX)
		pct = ENDLESS_DMG_MAX;
	// Final byte damage is also clamped at the use site.
	pct += endlessTideLevel() / ENDLESS_TIDE_DMG_STEP;
	if (pct > ENDLESS_TIDE_DMG_CAP)
		pct = ENDLESS_TIDE_DMG_CAP;
	return pct;
}

// Rising tide. START uses effective depth; shot thresholds use difficulty zone. The anchor sits on
// the zone before a GRAND milestone, like every other landmark on this curve family.
#define ENDLESS_TIDE_START           35
#define ENDLESS_TIDE_SHOT_ONSET      25
#define ENDLESS_TIDE_SHOT_ANCHOR     99
#define ENDLESS_TIDE_SHOT_ANCHOR_ADD 3
#define ENDLESS_TIDE_SHOT_STEP       25
#define ENDLESS_TIDE_SHOT_MAX        50

int endlessTideLevel(void)
{
	ENDLESS_OVERRIDE(ESO_TIDE);
	if (!endlessFxActive())
		return 0;
	const int t = endlessEffectiveDepth() - ENDLESS_TIDE_START;
	return (t > 0) ? t : 0;
}

// Raw value avoids reading the previous sector's Flak Screen during course generation.
static int endlessTideExtraShotsRaw(void)
{
	if (!endlessFxActive())
		return 0;
	const int zone = endlessDifficultyZone();

	int extra;
	if (zone >= ENDLESS_TIDE_SHOT_ANCHOR)
	{
		extra = ENDLESS_TIDE_SHOT_ANCHOR_ADD + (zone - ENDLESS_TIDE_SHOT_ANCHOR) / ENDLESS_TIDE_SHOT_STEP;
	}
	else if (zone >= ENDLESS_TIDE_SHOT_ONSET)
	{
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

// Additional shots per enemy volley.
int endlessExtraEnemyShots(void)
{
	ENDLESS_OVERRIDE(ESO_EXTRASHOTS);
	int extra = endlessTideExtraShotsRaw();
	// Preserve authored shots and round the retained extra half up.
	if (endlessActiveMods & ENDLESS_MOD_FLAKSCREEN)
		extra = (extra + 1) / 2;
	return extra;
}

// Which side the tide fan leans toward right now: held for one second of game
// time (35 sim ticks), then flipped, so every volley in that second sweeps the
// same way.
int endlessFanPhaseNow(void)
{
	return (endlessZoneTicks / 35) & 1;
}

bool endlessTideBoonsUnlocked(void)
{
	return endlessTideExtraShotsRaw() > 0;
}

// Player-side contact damage. Enemy collision damage is unchanged.
#define ENDLESS_CONTACT_START      35
#define ENDLESS_CONTACT_ANCHOR      99   // the zone before a GRAND milestone
#define ENDLESS_CONTACT_ANCHOR_PCT 150
#define ENDLESS_CONTACT_MAX_PCT    500
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
			pct = 1;
	}
	return pct;
}

// Special-enemy tiers are cached per link group.

static signed char endlessEliteLink[256];  // per-linknum tier this level: -1 undecided, else 1/2/3

// Link groups this level's script can still make damageable, scanned from the event list at level
// start and constant from there, so an invulnerable body can be judged on its first frame.
static bool endlessArmorOpens[256];
static bool endlessArmorOpensAll;  // a level-wide armor event reaches every body, linked or not

static int endlessMartyrLastLink = 0;

static int endlessShockwaveLastLink = 0;

static int endlessBountyLastLink = 0;

static int endlessSurveyorLastLink = 0;

// Level-script events that write enemy armor, and the one that renumbers a link group.
#define ENDLESS_EVENT_ARMOR_SET     25
#define ENDLESS_EVENT_ARMOR_SET_ALT 47
#define ENDLESS_EVENT_RELINK        39

static void endlessScanArmorOpenings(void)
{
	memset(endlessArmorOpens, 0, sizeof(endlessArmorOpens));
	endlessArmorOpensAll = false;

	for (int i = 0; i < maxEvent; ++i)
	{
		const int type = eventRec[i].eventtype;
		if (type != ENDLESS_EVENT_ARMOR_SET && type != ENDLESS_EVENT_ARMOR_SET_ALT)
			continue;
		if (eventRec[i].eventdat < 1 || eventRec[i].eventdat > 254)
			continue;  // 0 kills the body and 255 seals it; neither one opens anything up

		if (eventRec[i].eventdat4 == 0)
			endlessArmorOpensAll = true;
		else
			endlessArmorOpens[eventRec[i].eventdat4] = true;
	}

	if (endlessArmorOpensAll)
		return;  // every body is already reachable, so renumbering cannot widen the set

	// Renumbering hands a group a new link number, so a group opened under the new number is
	// openable under the old one. Repeat until a pass adds nothing, since renumbers chain.
	bool grew;
	do
	{
		grew = false;
		for (int i = 0; i < maxEvent; ++i)
		{
			if (eventRec[i].eventtype != ENDLESS_EVENT_RELINK)
				continue;

			const int from = eventRec[i].eventdat, to = eventRec[i].eventdat2;
			if (from < 1 || from > 255 || to < 1 || to > 255)
				continue;
			if (endlessArmorOpens[to] && !endlessArmorOpens[from])
			{
				endlessArmorOpens[from] = true;
				grew = true;
			}
		}
	} while (grew);
}

void endlessResetElites(void)
{
	for (unsigned i = 0; i < COUNTOF(endlessEliteLink); ++i)
		endlessEliteLink[i] = -1;

	endlessScanArmorOpenings();

	endlessMartyrLastLink = 0;
	endlessShockwaveLastLink = 0;
	endlessBountyLastLink = 0;
	endlessSurveyorLastLink = 0;
	endlessAegisReset();

	// Phase salts must be unique across all structural streams.
	endlessEliteRngState = endlessSplitMixSeed((Uint64)endlessRunDepth * 2 + 0x50000000);
}

// Both special-enemy curves pivot at this effective depth, zone 99 on Normal, and both reach their
// ceiling at zone 199. Each spreads its rise up to the pivot over this constant, so moving the
// pivot moves the whole early ramp and both ceilings with it.
#define ENDLESS_SPECIAL_PIVOT_DEPTH 122

// Natural special-enemy share before modifier overrides. 2% at the surface, 60% at the pivot.
int endlessNaturalEliteChancePercent(void)
{
	ENDLESS_OVERRIDE(ESO_ELITECHANCE);
	const int eff = endlessEffectiveDepth();
	const int pct = (eff <= ENDLESS_SPECIAL_PIVOT_DEPTH)
	              ? 2 + eff * 58 / ENDLESS_SPECIAL_PIVOT_DEPTH          // 58 points to the pivot
	              : 60 + (eff - ENDLESS_SPECIAL_PIVOT_DEPTH) * 4 / 25;  // 0.16% per depth past it
	return endlessClamp(pct, 2, 80);
}

// Tier-removal boons unlock once they have a meaningful effect.
bool endlessEliteBoonsUnlocked(void)
{
	return endlessNaturalEliteChancePercent() > 25;
}

// Modifier-adjusted special-enemy share.
static int endlessEliteChancePercent(void)
{
	if (endlessActiveMods & ENDLESS_MOD_NOELITE)
		return 0;
	if (endlessActiveMods & (ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION))
		return 100;
	if (endlessActiveMods & ENDLESS_MOD_ELITEPACK)
		return 50;
	return endlessNaturalEliteChancePercent();
}

// Tier IDs: 1 normal, 2 elite, 3 champion.
static int endlessPickTier(void)
{
	if ((int)(endlessEliteRand() % 100) >= endlessEliteChancePercent())
		return 1;
	if (endlessActiveMods & ENDLESS_MOD_NOCHAMP)
		return 2;
	if (endlessActiveMods & ENDLESS_MOD_LEGION)
		return 3;
	// Champion share of those specials: 10% at the surface, 30% at the pivot, 70% at the ceiling.
	const int eff = endlessEffectiveDepth();
	const int share = (eff <= ENDLESS_SPECIAL_PIVOT_DEPTH)
	                ? 10 + eff * 20 / ENDLESS_SPECIAL_PIVOT_DEPTH         // 20 points to the pivot
	                : 30 + (eff - ENDLESS_SPECIAL_PIVOT_DEPTH) * 8 / 25;  // 0.32% per depth past it
	return ((int)(endlessEliteRand() % 100) < endlessClamp(share, 10, 70)) ? 3 : 2;
}

static int endlessRollEliteTier(JE_byte linknum)
{
	if (linknum == 0)
		return endlessPickTier();
	if (endlessEliteLink[linknum] < 0)
		endlessEliteLink[linknum] = (signed char)endlessPickTier();
	return endlessEliteLink[linknum];
}

// Match player-shot eligibility, including armor events that open a link later.
bool endlessEnemyDestructible(JE_byte avail, JE_byte linknum, JE_byte armorleft)
{
	if (avail != 0)
		return false;
	if (armorleft > 0 && armorleft < 255)
		return true;
	return endlessArmorOpensAll || (linknum != 0 && endlessArmorOpens[linknum]);
}

// The tier this enemy wears for the rest of its life, settled on its first processed frame so
// nothing recolours or rearms in front of the player. See doc/notes.md#health-and-tiers.
int endlessEliteTierNow(JE_byte linknum, JE_byte armorleft, bool scoreitem)
{
	if (scoreitem)
		return 1;  // nothing promotes a pickup
	if (armorleft > 0 && armorleft < 255)
		return endlessRollEliteTier(linknum);
	if (linknum != 0 && endlessEliteLink[linknum] > 0)
		return endlessEliteLink[linknum];  // a part of a group that has already rolled
	if (endlessArmorOpensAll || (linknum != 0 && endlessArmorOpens[linknum]))
		return endlessRollEliteTier(linknum);  // a later armor event opens this one up
	return 1;  // invulnerable for its whole life, so never worth a bounty
}

// Elite and champion HP, on the same anchor ramp. Its first segment is likewise the shipped curve.
static const EndlessRampAnchor endlessEliteAnchors[] = {
	{   0, 200 },   // 2x at the surface
	{  80, 400 },   // 4x, zone 65 on Normal
	{ 122, 600 },   // 6x, zone 99
};

static int endlessEliteRamp100(int effDepth)
{
	return endlessRampAt100(endlessEliteAnchors, COUNTOF(endlessEliteAnchors), effDepth);
}

// Special-tier HP divisor in hundredths, on the same footing as the boss one.
int endlessEliteHpMult100(void)
{
	if (endlessScalingOverride[ESO_ELITEHP].active)
		return endlessScalingOverride[ESO_ELITEHP].value * ENDLESS_HP_MULT_SCALE;
	if (endlessActiveMods & ENDLESS_MOD_GIANTKILLER)
		return ENDLESS_HP_MULT_SCALE;
	return endlessEliteRamp100(endlessEffectiveDepth());
}

// Whole-x reading of the same curve, which is what the pierce delay is calibrated against.
int endlessEliteHpMult(void)
{
	return endlessEliteHpMult100() / ENDLESS_HP_MULT_SCALE;
}

// Combined boss and special-tier HP divisor, in hundredths.
int endlessEnemyHpMult100(bool hasBossBar, int bossHpMult100, int eliteState)
{
	if (!hasBossBar)
		return (eliteState >= 2) ? endlessEliteHpMult100() : ENDLESS_HP_MULT_SCALE;
	if (eliteState < 2 || (endlessActiveMods & ENDLESS_MOD_GIANTKILLER))
		return bossHpMult100;

	const int mult = bossHpMult100 * 2;
	const int ceiling = ENDLESS_HP_MULT_MAX * ENDLESS_HP_MULT_SCALE;
	const int cap  = (bossHpMult100 > ceiling) ? bossHpMult100 : ceiling;
	return (mult > cap) ? cap : mult;
}

// Repeat-pierce delay in hundredths of a tick.
int endlessPierceLock100(bool hasBossBar, int hpMult, int eliteState)
{
	// Ordinary enemies never inherit a boss lock, including under a debug pin.
	if (!hasBossBar && eliteState < 2)
		return 0;

	ENDLESS_OVERRIDE(ESO_PIERCELOCK);

	// Calibrate against Normal so difficulty cannot move the reference.
	const int refDepth = endlessEffectiveDepthOf(ENDLESS_PIERCE_LOCK_REF_ZONE - 1, 100);

	int span;
	int refSpan;
	int atRef;

	if (hasBossBar)
	{
		int mult100 = endlessBossHpMult100();
		// Preserve factors added outside the plain depth curve.
		const int stepped = endlessBossHpMult();
		if (stepped > 0 && hpMult > 0 && hpMult != stepped)
			mult100 = mult100 * hpMult / stepped;
		span    = mult100 - 100;
		refSpan = endlessBossRamp100(refDepth) - 100;
		atRef   = ENDLESS_PIERCE_LOCK_BOSS;
	}
	else
	{
		span    = endlessEliteHpMult100() - 100;
		refSpan = endlessEliteRamp100(refDepth) - 100;
		atRef   = (eliteState >= 3) ? ENDLESS_PIERCE_LOCK_CHAMP : ENDLESS_PIERCE_LOCK_ELITE;
	}

	if (span <= 0 || refSpan <= 0)
		return 0;
	return endlessClamp(span * atRef / refSpan,
	                    0, ENDLESS_PIERCE_LOCK_MAX * ENDLESS_PIERCE_LOCK_SCALE);
}

// Piercing damage percentage; see the constants for why the class needs a term of its own.
int endlessPiercePotencyPercent(void)
{
	ENDLESS_OVERRIDE(ESO_PIERCEDMG);
	if (!endlessFxActive())
		return 100;
	return endlessClamp(100 + endlessEffectiveDepth() * ENDLESS_PIERCE_POTENCY_PER_DEPTH,
	                    100, ENDLESS_PIERCE_POTENCY_MAX);
}

// One piercing bullet's damage for one tick, spent in ENDLESS_PIERCE_DMG_SCALE units with the
// remainder banked on the bullet, so a lever holds over the bullet's life instead of rounding away.
int endlessPierceHitDamage(int rawDamage, int dmgPct, JE_byte *carry100)
{
	if (rawDamage <= 0)
		return 0;   // attack 250 is a piercing round with no damage of its own
	const int total = rawDamage * (dmgPct * endlessPiercePotencyPercent() / 100)
	                + (carry100 != NULL ? *carry100 : 0);
	if (carry100 != NULL)
		*carry100 = (JE_byte)(total % ENDLESS_PIERCE_DMG_SCALE);
	return total / ENDLESS_PIERCE_DMG_SCALE;
}

// Special-tier bounty, including Bounty Hunter and Scavenger: personal perks, so both read the
// fx ship's row, which the award site points at the killer.
long endlessEliteBounty(void)
{
	long b = 150 + (long)endlessRunDepth * 40;
	if (b > 2500)
		b = 2500;
	if (endlessPerkEffective(endlessFxPlayer(), PERK_BOUNTY))
		b *= 2;
	return b * endlessPerkCashPercent() / 100;
}
long endlessChampionBounty(void)
{
	long b = 600 + (long)endlessRunDepth * 170;
	if (b > 11000)
		b = 11000;
	if (endlessPerkEffective(endlessFxPlayer(), PERK_BOUNTY))
		b *= 2;
	return b * endlessPerkCashPercent() / 100;
}

/* Bounty Hunter's second effect. The cash goes to whoever flew over the pickup, so the multiplier
 * reads that ship's own row rather than the fx context. Both machines run every pickup for both
 * ships, so the two wallets agree. */
long endlessScorePickupValue(uint p, long value)
{
	if (!endlessFxActive() || value <= 0 || !endlessPerkEffective(p, PERK_BOUNTY))
		return value;
	return value * ENDLESS_PERK_BOUNTY_PICKUP_MULT;
}

// Champion weapon multipliers; Clean Signals returns them to neutral.
int endlessChampionFireDelayPercent(void)
{
	if (endlessActiveMods & ENDLESS_MOD_CLEANSIGNALS)
		return 100;
	return 60;
}
int endlessChampionShotDamagePercent(void)
{
	if (endlessActiveMods & ENDLESS_MOD_CLEANSIGNALS)
		return 100;
	return 150;
}

// One source for the tier tint worn by the body, its aura, its health bar and its explosion.
Uint8 endlessEliteTint(int eliteState)
{
	if (!endlessFxActive() || eliteState < 2)
		return 0;
	return (eliteState == 3) ? ENDLESS_CHAMPION_FILTER : ENDLESS_ELITE_FILTER;
}

// A body the level holds invulnerable takes no tier of its own, yet it is usually one piece of a
// structure whose damageable parts did. Lend it the group's bank so the whole hull reads as one
// elite; colour only, and no tier, bounty or health follows it.
Uint8 endlessEliteShellTint(JE_byte linknum, JE_byte armorleft)
{
	if (linknum == 0 || armorleft < 255)
		return 0;
	return endlessEliteTint(endlessEliteLink[linknum]);
}

// Elite/champion contact premium; Clean Signals returns it to neutral.
int endlessEliteContactPercent(int eliteState)
{
	if (!endlessFxActive() || eliteState < 2 || (endlessActiveMods & ENDLESS_MOD_CLEANSIGNALS))
		return 100;
	return (eliteState == 3) ? 150 : 125;
}

/* An invulnerable ship rams in Endless, so Failsafe and the invulnerability specials feed a ram
 * build, but it lands only every so many ticks of its window: the cadence is read off the ticks
 * left, so it needs no state of its own. See doc/notes.md#perks. */
#define ENDLESS_RAM_INVULN_CADENCE 10
bool endlessRamWhileInvulnerable(uint invulnerableTicks)
{
	return endlessFxActive() && invulnerableTicks % ENDLESS_RAM_INVULN_CADENCE == 0;
}

// The link latch pays one bounty per logical enemy.
void endlessAwardEliteKill(int linknum, int eliteState, int killer)
{
	if (!endlessFxActive())
		return;

	const bool sameEnemy = (linknum != 0 && linknum == endlessBountyLastLink);
	endlessBountyLastLink = linknum;
	if (sameEnemy || eliteState < 2)
		return;

	const bool champion = (eliteState == 3);
	// The killer's own Bounty Hunter and Scavenger stacks size the payment (personal perks),
	// so the fx context names the killer while the figure is derived.
	const uint fxSaved = endlessFxPlayer();
	const uint payee = (killer == ENDLESS_KILLER_NONE) ? 0 : (uint)killer;
	endlessSetFxPlayer(payee);
	const long bounty = champion ? endlessChampionBounty() : endlessEliteBounty();
	endlessSetFxPlayer(fxSaved);
	// A bounty is kill cash under its own ledger row: it follows the same Shared / Individual
	// credit and Double Earnings rules as every other kill.
	player_award_bounty_cash(&player[payee], bounty);

	// Keep the cash clear of the HUD, showing what was actually paid. Online there are two
	// wallets, so the figure is worth nothing without whose it is: name the killer beside it.
	const long paid = coop_earnings_are_doubled() ? bounty * 2 : bounty;
	char tier[24], cash[48];
	snprintf(tier, sizeof(tier), "%s Enemy", champion ? "Champion" : "Elite");
	if (isNetworkGame && dual_ship_mode() && killer != ENDLESS_KILLER_NONE)
		snprintf(cash, sizeof(cash), "%s +%ld", JE_getName((JE_byte)(payee + 1)), paid);
	else
		snprintf(cash, sizeof(cash), "+%ld", paid);
	// The tier name takes the tier's own palette bank, the one its bodies, bars and bullets wear.
	JE_drawTextWindowSplit(tier, endlessEliteTint(eliteState) >> 4, " destroyed!", cash, 244);
}

// Special-weapon pickups.

// Sprite2 offset zero encodes the offset-table size.
static unsigned endlessHudIconCount(void)
{
	if (spriteSheet10.data == NULL || spriteSheet10.size < sizeof(Uint16))
		return 0;
	return SDL_SwapLE16(((Uint16 *)spriteSheet10.data)[0]) / (unsigned)sizeof(Uint16);
}

char endlessLastSpecialName[2][31] = { "", "" };

const char *endlessLastGrantedSpecial(void) { return endlessLastSpecialName[endlessEconomyIndex()]; }

void endlessGrantSpecial(uint p)
{
	if (!endlessFxActive())
		return;

	// Invalid icons would be read every HUD frame.
	const unsigned iconMax = endlessHudIconCount();

	// Exclude both Invulnerability records by effect type.
	JE_byte pool[SPECIAL_NUM] = { 0 };
	int n = 0;
	for (int id = 1; id <= SPECIAL_NUM; ++id)
		if (special[id].name[0] != '\0' &&
		    special[id].stype >= 1 && special[id].stype <= 18 &&
		    special[id].stype != 12 &&
		    special[id].itemgraphic >= 1 && special[id].itemgraphic <= iconMax)
			pool[n++] = (JE_byte)id;
	if (n == 0)
		return;

	// Avoid returning the equipped special when another valid choice exists.
	if (n > 1)
	{
		const JE_byte current = player[p].items.special;
		for (int i = 0; i < n; ++i)
			if (pool[i] == current)
			{
				pool[i] = pool[--n];
				break;
			}
	}

	const JE_byte id = pool[mt_rand() % n];
	player[p].items.special = id;
	shotMultiPos[SHOT_SPECIAL]  = 0;
	shotRepeat[SHOT_SPECIAL]    = 0;
	shotMultiPos[SHOT_SPECIAL2] = 0;
	shotRepeat[SHOT_SPECIAL2]   = 0;
	if (coop_mode_active())
	{
		// Co-op keeps each ship's own firing cursors; the globals above are only the scratch pair.
		player[p].shot_multi_pos[SHOT_SPECIAL]  = 0;
		player[p].shot_repeat[SHOT_SPECIAL]     = 0;
		player[p].shot_multi_pos[SHOT_SPECIAL2] = 0;
		player[p].shot_repeat[SHOT_SPECIAL2]    = 0;
	}
	hud_special_light_rearm(p);

	// Item names may be space-padded in the data.
	char *const name = endlessLastSpecialName[p];
	const size_t nameSize = sizeof(endlessLastSpecialName[0]);
	const char *s = JE_specialName(id);
	while (*s == ' ' || *s == '\t')
		++s;
	SDL_strlcpy(name, s, nameSize);
	for (size_t len = strlen(name); len > 0 && (name[len - 1] == ' ' || name[len - 1] == '\t'); )
		name[--len] = '\0';

	// Two ships share one message bar, so name whoever the grant went to, in the same
	// "<who> got <what>" phrasing the weapon-ball pickups use. Solo keeps the label,
	// which is what tells you a datacube/orb handed out a special at all.
	char msg[64];
	if (dual_ship_mode())
		snprintf(msg, sizeof(msg), "%s %s %s", JE_getName((JE_byte)(p + 1)), miscTextB[4-1], name);
	else
		snprintf(msg, sizeof(msg), "Special weapon:  %s", name);
	JE_drawTextWindow(msg);
}

// Pickups endlessGrantSpecial answers. The conditions mirror JE_playerCollide's two pickup
// branches, so the "?" art can only appear where a special is handed out. See
// doc/notes.md#special-pickups.
bool endlessSpecialPickup(int slot)
{
	if (!endlessMode || slot < 0 || slot >= (int)COUNTOF(enemy) || enemyAvail[slot] == 1)
		return false;

	const int value = enemy[slot].evalue;
	if (value == 1)
		return enemy[slot].scoreitem;  // data cube

	// Secret orb. enemyAvail 2 excludes an armored one until it is shot open; balls sit above 20000.
	return value > 10000 && value <= 20000 && enemyAvail[slot] == 2;
}

// Weapon power-up substitutions. All three IDs use sprite bank 21.
#define ENEMY_FRONT_POWERUP 533
#define ENEMY_REAR_POWERUP  534
#define ENEMY_GEM_5000      399

// Surveyor's finds: the AST. CITY superbomb (value -4) and the ASTEROID1 orbiting orb (value -3).
#define ENEMY_SUPERBOMB     800
#define ENEMY_ORBIT_ORB     535

static bool endlessPortCanPowerUp(uint port)
{
	// Either ship having room keeps the pickup on the field for whoever can still take it.
	for (uint p = 0; p < (coop_mode_active() ? COUNTOF(player) : 1u); ++p)
		if (player[p].items.weapon[port].id != 0 && player[p].items.weapon[port].power < 11)
			return true;
	return false;
}

// Pick a port now; validate capacity when the pickup actually spawns.
JE_word endlessPowerupDropEnemy(void)
{
	return (mt_rand() % 2) ? ENEMY_REAR_POWERUP : ENEMY_FRONT_POWERUP;
}

// Redirect at spawn time so late loadout changes are respected.
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

// Spawn one loot enemy in the dead enemy's bank.
static void endlessSpawnLootXY(int slot, JE_word eDatI, JE_integer x, JE_integer y)
{
	const Sint16 g = JE_newEnemy(slot - (slot % 25), eDatI, 0);
	if (g == 0)
		return;

	enemy[g-1].ex = x;
	enemy[g-1].ey = y;
}

// Embedded cubes become visible gems. Campaign-effect mode keeps the normal archive.
void endlessDropCubeGem(int slot)
{
	endlessSpawnLootXY(slot, ENEMY_GEM_5000, enemy[slot].ex, enemy[slot].ey);
}

// Surveyor's kill drops. Rules and pickup identities in doc/notes.md#perks.
void endlessPerkSurveyorDrops(unsigned int slot, int linknum, int killer)
{
	if (!endlessFxActive())
		return;

	JE_byte stacks = 0;
	if (killer >= 0 && killer < 2)
	{
		stacks = endlessPerkEffective(coopEndlessMode ? (uint)killer : 0, PERK_SURVEYOR);
	}
	else
	{
		for (uint p = 0; p < endlessEffectPlayers(); ++p)
			if (endlessPerkEffective(p, PERK_SURVEYOR) > stacks)
				stacks = endlessPerkEffective(p, PERK_SURVEYOR);
	}
	if (stacks == 0)
		return;

	if (linknum != 0 && linknum == endlessSurveyorLastLink)
		return;
	endlessSurveyorLastLink = linknum;

	// Two draws as statements: an unsequenced pair diverges across compilers.
	const int odds = ENDLESS_PERK_SURVEYOR_DROP_BASE / stacks;
	const bool bomb = (mt_rand() % odds) == 0;
	const bool orb  = (mt_rand() % odds) == 0;
	if (!bomb && !orb)
		return;

	JE_integer ax, ay;
	enemy_loot_anchor(slot, &ax, &ay);

	/* The kill site freed this slot just before the call but still reads and writes its fields
	 * for the explosion, so the loot must not land there: hold the slot across the spawns. */
	const JE_byte avail = enemyAvail[slot];
	enemyAvail[slot] = 0;
	if (bomb)
		endlessSpawnLootXY((int)slot, ENEMY_SUPERBOMB, ax, ay);
	if (orb)
		endlessSpawnLootXY((int)slot, ENEMY_ORBIT_ORB, ax, ay);
	enemyAvail[slot] = avail;
}

// Kill-fire HUD values.
/* Everything from here to the ship tint reads the CURRENT ship's mask and its own window: a
 * drive belongs to whoever bought it. */
int endlessKillBuffTicksLeft(void) { return endlessTurbodriveTimer[endlessFxPlayer()]; }
int endlessKillBuffTicksMax(void)  { return endlessBuffWindowTicks(); }

int endlessKillBuffComboCount(void)
{
	if (!endlessFxActive())
		return 0;
	return endlessComboKills[endlessFxPlayer()];
}

// Matches the player tint.
int endlessKillBuffColorBank(void)
{
	if (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_KILLFIRE_EVIL)
		return 4;
	if (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_OVERBLAST)
		return 9;   // blue; the damage-only buff
	return (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_OVERDRIVE) ? 7 : 12;
}

// The buff's current fire-rate MULTIPLIER (1 = none; 2x..10x from the combo ramp). Derived from
// the decrement count the fire block actually applies, plus 1 for the weapon's own per-tick
// decrement, so the HUD figure can't drift from the real rate.
int endlessKillBuffFireMultiplier(void)
{
	if (!endlessTurbodriveActive())
		return 1;
	return endlessKillBuffFireDecrements() + 1;
}

int endlessKillBuffDamagePercent(void)
{
	if (!endlessTurbodriveActive() || !(endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_DMGUP))
		return 0;  // Overdrive and Overblast grant damage; Turbodrive affects fire rate only.
	int pct = endlessBuffChargePaid() * 2;  // cash-paid charge adds flat damage on top of the per-kill stacks
	// Overdrive and Overblast reach +150% at a full combo.
	pct += endlessOverdriveStacks[endlessFxPlayer()] * ENDLESS_OVERDRIVE_DMG_MAX / ENDLESS_OVERDRIVE_MAX_STACKS;
	return pct;
}

// Extra shotRepeat decrements for a kill-fire boon. Hostile variants use endlessKillFireJamTicks.
int endlessKillBuffFireDecrements(void)
{
	if (!(endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_FIREBOOST))
		return 0;  // Turbodrive and Overdrive quicken fire; Overblast does not.
	int steps = endlessComboKills[endlessFxPlayer()] / ENDLESS_COMBO_KILLS_PER_STEP;
	if (steps > ENDLESS_COMBO_MAX_STEPS)
		steps = ENDLESS_COMBO_MAX_STEPS;
	return 1 + steps;
}

// Hostile kill-fire effects.
bool endlessKillFireIsEvil(void)
{
	return endlessFxActive() && endlessTurbodriveActive()
	    && (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_KILLFIRE_EVIL);
}

// Additional shotRepeat cooldown while Backfire or Burnout is active.
int endlessKillFireJamTicks(void)
{
	if (!endlessTurbodriveActive() || !(endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_FIREJAM))
		return 0;
	int steps = endlessComboKills[endlessFxPlayer()] / ENDLESS_COMBO_KILLS_PER_STEP;
	if (steps > ENDLESS_COMBO_MAX_STEPS)
		steps = ENDLESS_COMBO_MAX_STEPS;
	int add = ENDLESS_EVIL_JAM_BASE + steps * ENDLESS_EVIL_JAM_PER_STEP;
	if (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_BURNOUT)
		add += endlessOverdriveStacks[endlessFxPlayer()] * ENDLESS_EVIL_JAM_STACK_MAX / ENDLESS_OVERDRIVE_MAX_STACKS;
	return add;
}

// Damage penalty from Burnout or Misfire.
int endlessKillBuffEvilDamagePenalty(void)
{
	if (!endlessKillFireIsEvil() || !(endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_DMGDOWN))
		return 0;
	return endlessOverdriveStacks[endlessFxPlayer()] * ENDLESS_EVIL_DMG_MAX / ENDLESS_OVERDRIVE_MAX_STACKS;
}

// HUD label for the active hostile kill-fire effect.
const char *endlessKillFireEvilName(void)
{
	if (!endlessKillFireIsEvil())
		return "";
	if (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_BURNOUT)
		return "BURNOUT";
	if (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_MISFIRE)
		return "MISFIRE";
	return "JAMMED";
}

// Gravity remains weaker than full upward thrust at its cap.
#define ENDLESS_GRAVITY_BASE     1.6f
#define ENDLESS_GRAVITY_PER_ZONE 0.04f
#define ENDLESS_GRAVITY_MAX      3.6f

static float endlessGravityDirX = 0.0f;
static float endlessGravityDirY = 1.0f;

// Precomputed headings keep results identical across targets.
static const float endlessGravityHeadings[16][2] = {
	{  1.000f,  0.000f }, {  0.924f,  0.383f }, {  0.707f,  0.707f }, {  0.383f,  0.924f },
	{  0.000f,  1.000f }, { -0.383f,  0.924f }, { -0.707f,  0.707f }, { -0.924f,  0.383f },
	{ -1.000f,  0.000f }, { -0.924f, -0.383f }, { -0.707f, -0.707f }, { -0.383f, -0.924f },
	{  0.000f, -1.000f }, {  0.383f, -0.924f }, {  0.707f, -0.707f }, {  0.924f, -0.383f },
};

/* Per-ship fractional carry for classic gravity. It is rollback state because
 * shared or unsnapshotted carry makes the two ships drift. */
static float endlessGravityCarryX[2];
static float endlessGravityCarryY[2];

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
		endlessGravityDirY = 1.0f;
	}

	// Zone start: neither ship owes or is owed a fraction from the previous zone.
	memset(endlessGravityCarryX, 0, sizeof(endlessGravityCarryX));
	memset(endlessGravityCarryY, 0, sizeof(endlessGravityCarryY));
}

float endlessGravityDrift(void)
{
	// A bare OMNI bit can be set by the debug editor.
	if (!endlessFxActive() || !(endlessActiveMods & (ENDLESS_MOD_GRAVITY | ENDLESS_MOD_GRAVITY_OMNI)))
		return 0.0f;
	float g = (ENDLESS_GRAVITY_BASE + ENDLESS_GRAVITY_PER_ZONE * (float)endlessRunDepth)
	        * (float)endlessDifficultyRampPercent() / 100.0f;
	if (g > ENDLESS_GRAVITY_MAX)
		g = ENDLESS_GRAVITY_MAX;
	// Scale gravity with Sluggish so the combination remains flyable.
	return g * endlessMoveScale();
}

float endlessGravityDriftX(void) { return endlessGravityDrift() * endlessGravityDirX; }
float endlessGravityDriftY(void) { return endlessGravityDrift() * endlessGravityDirY; }

// Classic movement carries the fractional drift independently per ship and per axis.
int endlessGravityPullX(uint p)
{
	if (p >= COUNTOF(endlessGravityCarryX))
		return 0;
	endlessGravityCarryX[p] += endlessGravityDriftX();
	const int step = (int)endlessGravityCarryX[p];
	endlessGravityCarryX[p] -= (float)step;
	return step;
}
int endlessGravityPullY(uint p)
{
	if (p >= COUNTOF(endlessGravityCarryY))
		return 0;
	endlessGravityCarryY[p] += endlessGravityDriftY();
	const int step = (int)endlessGravityCarryY[p];
	endlessGravityCarryY[p] -= (float)step;
	return step;
}

// Sluggish scale shared by both ship movement paths.
#define ENDLESS_SLUGGISH_BASE     0.18f
#define ENDLESS_SLUGGISH_PER_ZONE 0.010f
#define ENDLESS_SLUGGISH_MAX      0.55f
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

bool endlessShieldRegenOff(void)
{
	return endlessFxActive() && (endlessActiveMods & (ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_DEADGEN));
}

// Keep Dead Generator non-zero so every main gun eventually fires.
#define ENDLESS_DEADGEN_POWER_ADD 2u
unsigned endlessGeneratorPowerAdd(unsigned normalAdd)
{
	if (endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_DEADGEN))
		return ENDLESS_DEADGEN_POWER_ADD;
	// Static uses the same recharge seam as Dead Generator.
	if (endlessStaticLockoutActive())
		return 0;
	return normalAdd;
}

// Reactive boons.
bool endlessShieldRegenFree(void)
{
	return endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_AUXREACTOR);
}

// Apply Low Profile at damage tests so pickup reach remains unchanged.
#define ENDLESS_LOWPROFILE_PCT 75
int endlessHitboxScale(int area)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_LOWPROFILE))
		return area;
	int a = area * ENDLESS_LOWPROFILE_PCT / 100;
	return (a < 1) ? 1 : a;
}

// Ignore trivial shield overflow so Aegis remains ready for a real hit.
#define ENDLESS_AEGIS_COOLDOWN  70
#define ENDLESS_AEGIS_MIN_SPILL  2

// One gate per ship: a block on one hull must not spend the partner's.
static int endlessAegisCooldown[2] = { 0, 0 };

void endlessAegisTick(void)
{
	for (unsigned p = 0; p < COUNTOF(endlessAegisCooldown); ++p)
		if (endlessAegisCooldown[p] > 0)
			--endlessAegisCooldown[p];
}

void endlessAegisReset(void)
{
	memset(endlessAegisCooldown, 0, sizeof(endlessAegisCooldown));
}

// Revive grace window.
#define ENDLESS_REVIVE_GRACE_TICKS 105

static int endlessReviveGrace = 0;

void endlessReviveGraceArm(void)   { endlessReviveGrace = ENDLESS_REVIVE_GRACE_TICKS; }
void endlessReviveGraceReset(void) { endlessReviveGrace = 0; }
bool endlessReviveGraceActive(void) { return endlessReviveGrace > 0; }

void endlessReviveGraceTick(void)
{
	if (endlessReviveGrace > 0)
		--endlessReviveGrace;
}

// A true result spends the cooldown and must be honored by the caller. JE_playerDamage names the
// hit ship as the fx player, so the gate spent here is that ship's own.
bool endlessAegisGateConsume(int shieldBefore, int spill)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_AEGIS))
		return false;
	int *const cd = &endlessAegisCooldown[endlessFxPlayer()];
	if (shieldBefore <= 0 || spill < ENDLESS_AEGIS_MIN_SPILL || *cd > 0)
		return false;
	*cd = ENDLESS_AEGIS_COOLDOWN;
	return true;
}

// Shockwave radii by tier.
#define ENDLESS_SHOCKWAVE_ELITE_RADIUS     80
#define ENDLESS_SHOCKWAVE_CHAMPION_RADIUS 120

bool endlessShockwaveActive(void)
{
	return endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_SHOCKWAVE);
}

int endlessShockwaveRadius(int linknum, int eliteState)
{
	if (!endlessShockwaveActive())
		return 0;

	// Update before the tier test so ordinary kills break a stale link latch.
	const bool sameEnemy = (linknum != 0 && linknum == endlessShockwaveLastLink);
	endlessShockwaveLastLink = linknum;
	if (sameEnemy || eliteState < 2)
		return 0;

	return (eliteState == 3) ? ENDLESS_SHOCKWAVE_CHAMPION_RADIUS : ENDLESS_SHOCKWAVE_ELITE_RADIUS;
}

/* Which ship a homing or course-correcting shot goes for: the nearer one still flying. A downed
 * co-op partner neither triggers a reactive danger nor attracts one, so the survivor is the only
 * target while they spectate. Integer distance keeps the choice identical on both machines. */
uint endlessDangerTargetPlayer(int fromX, int fromY)
{
	if (!coopEndlessMode)
		return 0;

	uint best = 0;
	long bestDist = -1;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		if (!player[p].is_alive || endlessPlayerDowned[p])
			continue;
		const long dx = (long)player[p].x - fromX;
		const long dy = (long)player[p].y - fromY;
		const long dist = dx * dx + dy * dy;
		if (bestDist < 0 || dist < bestDist)
		{
			bestDist = dist;
			best = p;
		}
	}
	return best;
}

/* Which ship a homing enemy chases, rolled once when it is created. Vanilla tracking always
 * went for ship one, so in co-op the homing modifiers left the second player alone entirely; a
 * coin toss per enemy splits the pressure. */
uint endlessRollHomingTarget(void)
{
	if (!coopEndlessMode)
		return 0;
	return mt_rand() & 1u;
}

/* ...and read back at the moment it matters, because the ship it picked may have gone down since.
 * A downed partner is not chased, the same rule the curving shots follow. */
uint endlessHomingTargetPlayer(uint stored)
{
	if (!coopEndlessMode)
		return 0;
	if (stored < COUNTOF(player) && player[stored].is_alive && !endlessPlayerDowned[stored])
		return stored;
	for (uint p = 0; p < COUNTOF(player); ++p)
		if (player[p].is_alive && !endlessPlayerDowned[p])
			return p;
	return 0;
}

// Modifier decisions used by engine-owned object pools.

// True while the sector floors enemy tracking, which is what makes a body chase the ship.
bool endlessHomingTierActive(void)
{
	return endlessFxActive() && (endlessActiveMods &
	       (ENDLESS_MOD_HOMING | ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_RAMPAGE)) != 0;
}

int endlessMartyrdomBurstShots(int linknum, int eliteState)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_MARTYRDOM))
		return 0;
	if (linknum != 0 && linknum == endlessMartyrLastLink)
		return 0;
	endlessMartyrLastLink = linknum;
	return (eliteState == 3) ? 8 : (eliteState == 2) ? 6 : 4;
}

// Fixed symmetric sprite: radial bursts must not inherit directional level art.
#define ENDLESS_MARTYR_SHOT_SGR 100
JE_word endlessMartyrShotSprite(void) { return ENDLESS_MARTYR_SHOT_SGR; }

#define ENDLESS_SEEK_COS_23DEG  0.9205f
#define ENDLESS_SEEK_SIN_23DEG  0.3907f
#define ENDLESS_SEEK_COS_55DEG  0.5736f
#define ENDLESS_SEEK_SIN_55DEG  0.8192f
#define ENDLESS_SEEK_COS_ANYDEG (-1.0f)
#define ENDLESS_SEEK_COS_NODEG  1.0f

static const struct { float turnCos, turnSin; JE_byte passes; } endlessSeekerBend[ENDLESS_SEEK_TIERS] = {
	{ ENDLESS_SEEK_COS_NODEG,  0.0f,                   0 },
	{ ENDLESS_SEEK_COS_23DEG,  ENDLESS_SEEK_SIN_23DEG, 1 },
	{ ENDLESS_SEEK_COS_23DEG,  ENDLESS_SEEK_SIN_23DEG, 2 },
	{ ENDLESS_SEEK_COS_55DEG,  ENDLESS_SEEK_SIN_55DEG, 1 },
	{ ENDLESS_SEEK_COS_ANYDEG, 0.0f,                   1 },
	{ ENDLESS_SEEK_COS_ANYDEG, 0.0f,                   2 },
};

EndlessSeekerTier endlessSeekerTier(void)
{
	if (!endlessFxActive())
		return ENDLESS_SEEK_NONE;
	if (endlessActiveMods & ENDLESS_MOD_KILLSHOT)
		return ENDLESS_SEEK_KILL;
	if (endlessActiveMods & ENDLESS_MOD_TRUEAIM)
		return ENDLESS_SEEK_TRUE;
	if (endlessActiveMods & ENDLESS_MOD_HUNTER)
		return ENDLESS_SEEK_WIDE;
	if (endlessActiveMods & ENDLESS_MOD_TWINSEEK)
		return ENDLESS_SEEK_TWIN;
	if (endlessActiveMods & ENDLESS_MOD_SEEKER)
		return ENDLESS_SEEK_CURVE;
	return ENDLESS_SEEK_NONE;
}

bool endlessSeekerActive(void)
{
	return endlessSeekerTier() != ENDLESS_SEEK_NONE;
}

JE_byte endlessSeekerPasses(void)
{
	return endlessSeekerBend[endlessSeekerTier()].passes;
}

void endlessSeekerTurn(float *turnCos, float *turnSin)
{
	*turnCos = endlessSeekerBend[endlessSeekerTier()].turnCos;
	*turnSin = endlessSeekerBend[endlessSeekerTier()].turnSin;
}

// Static combines a raw power drain with a recharge lockout.
#define ENDLESS_STATIC_POWER_PER_DMG   30
#define ENDLESS_STATIC_POWER_MIN      150
#define ENDLESS_STATIC_LOCKOUT_PER_DMG  6
#define ENDLESS_STATIC_LOCKOUT_MIN     25
#define ENDLESS_STATIC_LOCKOUT_MAX     70

// One lockout per ship: the hit ship's generator stalls, the partner's keeps charging.
static int endlessStaticLockout[2] = { 0, 0 };

bool endlessStaticLockoutActive(void)
{
	return endlessFxActive() && endlessStaticLockout[endlessFxPlayer()] > 0;
}

void endlessStaticLockoutTick(void)
{
	for (unsigned p = 0; p < COUNTOF(endlessStaticLockout); ++p)
		if (endlessStaticLockout[p] > 0)
			--endlessStaticLockout[p];
}

void endlessStaticLockoutReset(void)
{
	memset(endlessStaticLockout, 0, sizeof(endlessStaticLockout));
}

unsigned endlessStaticDischargeDrain(unsigned actualDamage)
{
	if (!endlessFxActive() || !(endlessActiveMods & ENDLESS_MOD_STATIC) || (endlessActiveMods & ENDLESS_MOD_DEADGEN))
		return 0;
	// A new hit may extend but never shorten the active lockout. The fx context is the hit ship
	// (JE_playerDamage), so only that ship's recharge stalls.
	int lock = (int)actualDamage * ENDLESS_STATIC_LOCKOUT_PER_DMG;
	if (lock < ENDLESS_STATIC_LOCKOUT_MIN)
		lock = ENDLESS_STATIC_LOCKOUT_MIN;
	if (lock > ENDLESS_STATIC_LOCKOUT_MAX)
		lock = ENDLESS_STATIC_LOCKOUT_MAX;
	if (lock > endlessStaticLockout[endlessFxPlayer()])
		endlessStaticLockout[endlessFxPlayer()] = lock;

	unsigned drain = actualDamage * ENDLESS_STATIC_POWER_PER_DMG;
	if (drain < ENDLESS_STATIC_POWER_MIN)
		drain = ENDLESS_STATIC_POWER_MIN;
	return drain;
}

// Combined player shot-damage percentage.
int endlessPlayerDamagePercent(void)
{
	ENDLESS_OVERRIDE(ESO_PLAYERDMG);
	if (!endlessFxActive())
		return 100;
	int pct = (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_OVERCHARGE) ? 150 : 100;
	if ((endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_DMGUP) && endlessTurbodriveActive())
		pct += endlessOverdriveStacks[endlessFxPlayer()] * ENDLESS_OVERDRIVE_DMG_MAX / ENDLESS_OVERDRIVE_MAX_STACKS;
	if (endlessTurbodriveActive() && (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_DMGUP))
		pct += endlessBuffChargePaid() * 2;
	pct += endlessPerkEffective(endlessFxPlayer(), PERK_DAMAGE) * ENDLESS_PERK_DAMAGE_PCT;
	if (endlessPerkEffective(endlessFxPlayer(), PERK_GLASSCANNON))
		pct += ENDLESS_PERK_GLASS_DMG;
	if (endlessAdrenalineActive())
		pct += endlessPerkEffective(endlessFxPlayer(), PERK_ADRENALINE) * ENDLESS_PERK_ADRENALINE_DMG;
	// Apply hostile damage cuts after every bonus.
	if ((endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_DMGDOWN) && endlessTurbodriveActive())
	{
		pct -= endlessKillBuffEvilDamagePenalty();
		if (pct < ENDLESS_EVIL_DMG_FLOOR)
			pct = ENDLESS_EVIL_DMG_FLOOR;
	}
	return pct;
}

// Apply player-shot scaling to non-projectile damage. Opening Salvo uses the same additive bonus.
int endlessScaleOwnDamage(int base, bool salvoBoosted)
{
	if (base <= 0)
		return base;
	const int pct = endlessPlayerDamagePercent()
	              + (salvoBoosted ? endlessOpeningSalvoDamagePercent() : 0);
	const int scaled = base * pct / 100;
	return (scaled < 1) ? 1 : scaled;
}

// Flat Bulwark reduction; JE_playerDamage retains a minimum of one.
int endlessPlayerDamageReduce(void)
{
	if (!endlessFxActive())
		return 0;
	return endlessPerkEffective(endlessFxPlayer(), PERK_BULWARK) * ENDLESS_PERK_BULWARK;
}

// Shared scroll multiplier for layers and layer-bound fixed motion.
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

// Stable modifier predicate; the fractional step count is not stable.
bool endlessScrollBoostActive(void)
{
	return endlessScrollBoostPercent() != 0;
}

// File-scope carries let rollback restore campaign and Endless scroll fractions.
// Enemy spawn anchors depend on these values resuming at the same tick.
static int scrollExtraCarry[3] = { 0, 0, 0 };
static int scrollExtraTrem[3]  = { 0, 0, 0 };

// Publish the smooth rate and distribute extra whole pixels for one layer.
int endlessScrollExtraPx(int channel, int fireStep, int delayMax, int baseThisTick,
                         float *rateOut, float *fracOut)
{
	int *const carry = scrollExtraCarry;
	int *const trem  = scrollExtraTrem;
	if (rateOut != NULL)
		*rateOut = 0.0f;
	if (fracOut != NULL)
		*fracOut = 0.0f;
	if (channel < 0 || channel > 2)
		return 0;
	const int boost = endlessScrollBoostPercent();
	if (fireStep <= 0)
	{
		carry[channel] = 0;
		trem[channel]  = 0;
		return 0;
	}
	if (delayMax < 1)
		delayMax = 1;
	// Carry the division remainder so the long-run average is exact.
	int tnum = fireStep * (100 + boost) + trem[channel];
	int target = tnum / delayMax;
	trem[channel] = tnum - target * delayMax;
	carry[channel] += target - baseThisTick * 100;
	// At boost zero, publish the rate without changing simulation scroll.
	int px = 0;
	if (boost > 0 && carry[channel] >= 100)
	{
		px = carry[channel] / 100;
		carry[channel] -= px * 100;
	}
	if (carry[channel] > 5000 || carry[channel] < -5000)
		carry[channel] = 0;
	if (rateOut != NULL)
		*rateOut = (float)target / 100.0f;
	if (fracOut != NULL)
		*fracOut = (float)carry[channel] / 100.0f;
	return px;
}

// Player tint for active kill-fire effects.
int endlessShipTintFilter(void)
{
	if (!endlessFxActive())
		return 0;
	if (endlessTurbodriveActive())
	{
		if (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_KILLFIRE_EVIL)
			return ENDLESS_EVIL_SHIP_FILTER;
		if (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_OVERBLAST)
			return ENDLESS_OVERBLAST_SHIP_FILTER;
		return (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_OVERDRIVE)
		       ? ENDLESS_OVERDRIVE_SHIP_FILTER
		       : ENDLESS_TURBODRIVE_SHIP_FILTER;
	}
	return 0;
}

// Persisted override keys must not be renamed.
static const struct { const char *name; const char *key; int lo, hi; } endlessOverrideInfo[ESO_COUNT] = {
	[ESO_ARMOR]       = { "Enemy HP %",      "enemy_hp",      ENDLESS_HP_MIN,                ENDLESS_HP_OVERFLOW_MAX },
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
	[ESO_PIERCEDMG]   = { "Pierce Damage %", "pierce_damage", 10,                            ENDLESS_PIERCE_POTENCY_MAX },
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

// Read the live formula with one override temporarily disabled.
int endlessScalingOverrideStock(int id)
{
	if (id < 0 || id >= ESO_COUNT)
		return 0;
	// Preserve the checked pointer across opaque accessor calls for MSVC analysis.
	EndlessScalingOverride *const ov = &endlessScalingOverride[id];
	const bool was = ov->active;
	ov->active = false;
	int v = 0;
	switch (id)
	{
	case ESO_ARMOR:       v = endlessArmorPercentTotal();         break;
	case ESO_BOSSHP:      v = endlessBossHpMult100();             break;
	case ESO_FIREDELAY:   v = endlessFireDelayPercent();          break;
	case ESO_SHOTSPEED:   v = endlessShotSpeedPercent();          break;
	case ESO_SHOTDMG:     v = endlessShotDamagePercent();         break;
	case ESO_CONTACT:     v = endlessContactDamagePercent();      break;
	case ESO_TIDE:        v = endlessTideLevel();                 break;
	case ESO_EXTRASHOTS:  v = endlessExtraEnemyShots();           break;
	case ESO_ELITECHANCE: v = endlessNaturalEliteChancePercent(); break;
	case ESO_ELITEHP:     v = endlessEliteHpMult100();            break;
	case ESO_PLAYERDMG:   v = endlessPlayerDamagePercent();       break;
	case ESO_PIERCEDMG:   v = endlessPiercePotencyPercent();      break;
	// Pierce delay depends on the live boss multiplier.
	case ESO_PIERCELOCK:  v = endlessPierceLock100(true, endlessBossHpMult(), 1); break;
	default: break;
	}
	ov->active = was;
	return v;
}

// Evaluate the real scaling accessors against temporary zone, difficulty, and modifier state.
// Live state is restored afterward; debug overrides and live fire-rate timers still apply.
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
	out->armorPct     = endlessArmorPercentTotal();
	out->bossMult100  = endlessBossHpMult100();
	out->fireDelayPct = endlessFireDelayPercent();
	out->shotSpeedPct = endlessShotSpeedPercent();
	out->shotDmgPct   = endlessShotDamagePercent();
	out->tide         = endlessTideLevel();
	out->extraShots   = endlessExtraEnemyShots();
	out->contactPct   = endlessContactDamagePercent();
	out->elitePct     = endlessNaturalEliteChancePercent();
	out->eliteHpMult100 = endlessEliteHpMult100();
	out->playerDmgPct = endlessPlayerDamagePercent();
	out->piercePct    = endlessPiercePotencyPercent();
	// Hundredths of a tick, at the boss tier; the delay is calibrated against the whole-x reading.
	out->pierceLock100 = endlessPierceLock100(true, out->bossMult100 / ENDLESS_HP_MULT_SCALE, 1);
	out->eliteBounty  = endlessEliteBounty();
	out->champBounty  = endlessChampionBounty();

	endlessRunDepth     = saveDepth;
	difficultyLevel     = saveDiff;
	endlessActiveMods   = saveMods;
	endlessCampaignMods = saveCamp;
}

/* Register only scroll state shared by campaign and Endless gameplay. */
#include "rollback.h"

void endless_combat_register_rollback(void)
{
	rollback_register("ec.scrollCarry", scrollExtraCarry, sizeof(scrollExtraCarry));
	rollback_register("ec.scrollTrem",  scrollExtraTrem, sizeof(scrollExtraTrem));

	/* Everything below is decided inside a tick, so a re-simulation has to replay it from the same
	 * value. The one-shot latches matter most: an unregistered dedup guard makes its event
	 * unrepeatable, and an unregistered revive latch resurrects or kills the wrong ship. */
	rollback_register("endless.eliteLink", endlessEliteLink, sizeof(endlessEliteLink));
	rollback_register("endless.martyrLink", &endlessMartyrLastLink, sizeof(endlessMartyrLastLink));
	rollback_register("endless.shockLink", &endlessShockwaveLastLink, sizeof(endlessShockwaveLastLink));
	rollback_register("endless.bountyLink", &endlessBountyLastLink, sizeof(endlessBountyLastLink));
	rollback_register("endless.surveyLink", &endlessSurveyorLastLink, sizeof(endlessSurveyorLastLink));
	rollback_register("endless.aegisCd", endlessAegisCooldown, sizeof(endlessAegisCooldown));
	rollback_register("endless.reviveGrace", &endlessReviveGrace, sizeof(endlessReviveGrace));
	rollback_register("endless.staticLock", endlessStaticLockout, sizeof(endlessStaticLockout));
	rollback_register("endless.gravityDir", &endlessGravityDirX, sizeof(endlessGravityDirX));
	rollback_register("endless.gravityDirY", &endlessGravityDirY, sizeof(endlessGravityDirY));
	rollback_register("endless.gravityCarryX", endlessGravityCarryX, sizeof(endlessGravityCarryX));
	rollback_register("endless.gravityCarryY", endlessGravityCarryY, sizeof(endlessGravityCarryY));
}
