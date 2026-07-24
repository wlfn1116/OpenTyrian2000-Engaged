/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Endless mode: perks -- the run-persistent, stacking upgrades.
 *
 * One of the endless_*.c files that make up endless mode: endless.h is the public
 * interface, endless_internal.h the state and helpers the group shares.
 */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"        // difficultyLevel, DIFFICULTY_*, player-independent globals
#include "custom_weapon.h" // customWeaponPort / customSidekickSlot (reserved shop slots)
#include "episodes.h"      // item arrays + SHIP_NUM/PORT_NUM/... counts, episodeAvail, JE_initEpisode
#include "mainint.h"       // JE_getCost
#include "player.h"        // player[]
#include "tyrian2.h"       // itemAvail, itemAvailMax
#include "varz.h"          // eventRec, maxEvent, map* globals

#include <stdio.h>
#include <stdlib.h>

// The perk registry, indexed by the PERK_* ids. The order is the on-disk save slot order,
// so append new perks at the end of the enum (endless_internal.h) and here -- never renumber.
const EndlessPerk endlessPerkTable[PERK_COUNT] = {
	{ "Heavy Rounds",     "Your shots deal more damage.",         5 },
	{ "Rapid Cyclers",    "Your guns fire noticeably faster.",    4 },
	{ "Ablative Plating", "Raises your maximum armor.",           6 },
	{ "Scavenger",        "More cash from clears and bounties.",  4 },
	{ "Nanorepair",       "Slowly regenerate armor in flight.",   3 },
	{ "Siphon",           "Chance to restore armor on a kill.",   3 },
	{ "Bounty Hunter",    "Elite and champion bounties doubled.", 1 },
	{ "Bulwark",          "Take less damage from every hit.",     5 },
	{ "Adrenaline",       "Fire much faster when badly hurt.",    3 },
	{ "Glass Cannon",     "Big damage, but a weaker hull.",       1 },
	{ "Rapid Recharge",   "Specials and ammo recharge faster.",   4 },
	{ "Autofire Special", "Auto-fires your special as you shoot.", 1 },
	{ "Efficient Coils",  "Your weapons draw less generator power.", 5 },
	{ "Shield Matrix",    "Your shield recharges faster.",        4 },
	{ "Rapid Charger",    "Charge sidekicks power up faster.",    4 },
	{ "High-Velocity Shots", "Your shots travel faster.",        3 },
	{ "Radar",            "Chart-a-Course shows each sector's level.", 1 },
	{ "Surveyor",         "Chart-a-Course offers an extra route.",     2 },
	{ "Executioner",      "Hits deal more to badly wounded enemies.",  3 },
	{ "Opening Salvo",    "A long pause supercharges your next shot.", 1 },
	{ "Kinetic Converter","Absorbed shield hits refuel the generator.",3 },
	{ "Countermeasures",  "Taking hull damage clears nearby shots.",   2 },
	{ "Chain Reaction",   "Kills blast nearby enemies.",               3 },
};

bool endlessPerkPending = false;             // a perk pick is queued for the next shop
JE_byte endlessPerkOwned[PERK_COUNT]; // stack counts, reset each run
int endlessPerkChoice[3];             // this visit's offered perk ids
int endlessPerkChoiceN = 0;           // how many are offered (0..3)
int endlessRegenTick = 0;             // Nanorepair countdown (reset each run)
int endlessSalvoIdle = 0;             // Opening Salvo: ticks the main gun has sat idle (reset each run)
int endlessCmCooldown = 0;            // Countermeasure Suite: ticks until the next burst is ready (reset each run)
// The run depth whose post-zone perk pick has already been resolved (taken or declined); -1 =
// none yet. endlessBetweenLevels offers the forced pick only when this lags the current depth,
// so re-entering the same outpost (e.g. after a save/reload) can't hand out a second perk.
int endlessPerkDepthDone = -1;

// Cash multiplier (100 = unchanged) from the Scavenger perk, applied to the clear bonus and
// elite/champion bounties.
int endlessPerkCashPercent(void)
{
	return 100 + endlessPerkOwned[PERK_CASH] * ENDLESS_PERK_CASH_PCT;
}

// +max armor from the Ablative Plating perk; added to the ship's armor each level start (varz.c),
// alongside the outpost hull upgrade (endlessArmorBonus).
int endlessPerkArmorBonus(void)
{
	int bonus = endlessPerkOwned[PERK_ARMOR] * ENDLESS_PERK_ARMOR_STEP;
	if (endlessPerkOwned[PERK_GLASSCANNON])
		bonus -= ENDLESS_PERK_GLASS_ARMOR;  // Glass Cannon relic drawback (varz.c clamps armor >= 1)
	return bonus;
}

// Turn a PERCENT-PER-TICK rate into whole steps, carrying the remainder in *accum so a
// fractional rate (say 20%/tick = one step every fifth tick) comes out smooth instead of
// lumpy. A rate of 0 clears the carry, so a perk that stops applying leaves no drip behind.
static int endlessAccumSteps(int *accum, int rate)
{
	if (rate == 0)
	{
		*accum = 0;
		return 0;
	}
	*accum += rate;
	const int steps = *accum / 100;
	*accum -= steps * 100;
	return steps;
}

// Rapid Cyclers perk: extra shotRepeat decrements this tick, as a smooth fractional rate via an
// accumulator (like the scroll-step boost). Applied every tick from the player fire block.
int endlessPerkFireDecrements(void)
{
	static int accum = 0;
	if (!endlessMode)
	{
		accum = 0;
		return 0;
	}
	int rate = endlessPerkOwned[PERK_FIRERATE] * ENDLESS_PERK_FIRE_PCT;
	// Adrenaline relic: a big extra boost while armor is below 1/N of the ship's max.
	if (endlessPerkOwned[PERK_ADRENALINE] > 0 && player[0].initial_armor > 0
	    && player[0].armor * ENDLESS_PERK_ADRENALINE_HP < player[0].initial_armor)
		rate += endlessPerkOwned[PERK_ADRENALINE] * ENDLESS_PERK_ADRENALINE_PCT;
	return endlessAccumSteps(&accum, rate);
}

// Rapid Recharge perk: extra cooldown decrements/tick (fractional accumulator). The caller
// applies them to the special-fire gate AND the sidekick ammo refill -- not the main guns.
int endlessPerkSpecialCooldownDecrements(void)
{
	static int accum = 0;
	if (!endlessMode)
	{
		accum = 0;
		return 0;
	}
	return endlessAccumSteps(&accum, endlessPerkOwned[PERK_SPECIALCD] * ENDLESS_PERK_SPECIALCD_PCT);
}

// Autofire Special perk: while owned, the equipped special weapon fires on its own as long as the
// main fire button is held -- the run-persistent equivalent of the debug "Autofire Special" toggle.
// Read in varz.c's special-fire path, OR'd with the debug autoFireSpecial global.
bool endlessPerkAutoFireSpecial(void)
{
	return endlessMode && endlessPerkOwned[PERK_AUTOSPECIAL] > 0;
}

// Efficient Coils perk: power-use scale per main-weapon shot (100 = normal, lower = cheaper);
// applied in shots.c player_shot_create. Floored so firing is never entirely free.
int endlessPerkPowerUsePercent(void)
{
	if (!endlessMode)
		return 100;
	const int pct = 100 - endlessPerkOwned[PERK_POWERUSE] * ENDLESS_PERK_POWERUSE_PCT;
	return pct < ENDLESS_PERK_POWERUSE_MIN ? ENDLESS_PERK_POWERUSE_MIN : pct;
}

// Shorten an interval by `step` ticks per stack of `perk`, never below `minimum`. Outside
// endless, or with no stacks, `base` comes back untouched -- so callers can hand their stock
// interval straight through with no endless-specific branch of their own.
static int endlessPerkShorten(int base, int perk, int step, int minimum)
{
	if (!endlessMode || endlessPerkOwned[perk] == 0)
		return base;
	const int v = base - endlessPerkOwned[perk] * step;
	return v < minimum ? minimum : v;
}

// Shield Matrix perk: shortens the shield-regen interval from `base` (tyrian2.c), floored; a
// no-op outside endless / with no stacks. A quicker shield still drains the generator quicker.
int endlessPerkShieldWait(int base)
{
	return endlessPerkShorten(base, PERK_SHIELDREGEN,
	                          ENDLESS_PERK_SHIELDRGN_STEP, ENDLESS_PERK_SHIELDRGN_MIN);
}

// Rapid Charger perk: shortens the charge-sidekick charge interval from `base` (mainint.c),
// floored; a no-op outside endless / with no stacks.
int endlessPerkChargeTicks(int base)
{
	return endlessPerkShorten(base, PERK_CHARGERATE,
	                          ENDLESS_PERK_CHARGE_STEP, ENDLESS_PERK_CHARGE_MIN);
}

// High-Velocity Rounds perk: shot travel-speed scale (100 = normal), applied in shots.c
// player_shot_create to the genuine shot velocities. A no-op outside endless / with no stacks.
int endlessPerkShotSpeedPercent(void)
{
	if (!endlessMode)
		return 100;
	return 100 + endlessPerkOwned[PERK_SHOTSPEED] * ENDLESS_PERK_SHOTSPEED_PCT;
}

// Radar perk: while owned, Chart-a-Course's help line names the shipped level behind each offered
// sector (endlessCourseHelp appends it after the danger tier). A pure information reveal -- no
// combat effect -- so it hangs off the perk-owned flag alone.
bool endlessPerkRadarActive(void)
{
	return endlessMode && endlessPerkOwned[PERK_RADAR] > 0;
}

// --- New perks: Surveyor / Executioner / Opening Salvo / Kinetic Converter / Countermeasures /
//     Chain Reaction. Like the perks above, each folds into an existing player-side lever. ----------

// Surveyor perk: extra Chart-a-Course routes this visit (one per stack). The caller adds these to the
// rolled course count AFTER the RNG roll and clamps to ENDLESS_MAX_COURSES, so the seed stream is untouched.
int endlessPerkSurveyorRoutes(void)
{
	return endlessMode ? endlessPerkOwned[PERK_SURVEYOR] * ENDLESS_PERK_SURVEYOR_ROUTES : 0;
}

// Executioner perk: bonus damage a player shot deals to a badly wounded target. `armorleft` is its
// CURRENT armor, `fullHp` its latched full armor (enemy healthbar_max; 0 before the first hit, when it
// is by definition at full HP so no bonus), `boss` selects the tighter boss threshold. Returns 0 for a
// healthy / not-yet-hit / invulnerable target, else +ENDLESS_PERK_EXEC_DMG_PCT% of `damage` per stack.
int endlessPerkExecutionerBonus(int damage, int armorleft, int fullHp, bool boss)
{
	const int stacks = endlessMode ? endlessPerkOwned[PERK_EXECUTIONER] : 0;
	if (stacks == 0 || damage <= 0 || fullHp <= 0 || armorleft <= 0 || armorleft >= 255)
		return 0;
	const int threshold = boss ? ENDLESS_PERK_EXEC_BOSS_PCT : ENDLESS_PERK_EXEC_HP_PCT;
	if (armorleft * 100 >= fullHp * threshold)  // not wounded enough
		return 0;
	return damage * stacks * ENDLESS_PERK_EXEC_DMG_PCT / 100;
}

// --- Opening Salvo perk ---------------------------------------------------------------------------
// endlessSalvoIdle counts ticks since the main gun last fired (advanced by endlessGameplayTick).
// endlessSalvoVolley is armed by the fire path when the main gun opens a charged volley, and stays
// set for the REST of that tick so every gun firing alongside it -- rear weapon and both sidekicks
// -- is boosted + power-free too. endlessOpeningSalvoTick clears it at the start of the next tick.
static bool endlessSalvoVolley = false;

// Start-of-tick housekeeping: drop last tick's volley flag (the fire path re-arms it if this tick's
// main shot is charged), then advance the idle timer while the perk is owned (capped so a long idle
// can't overflow). Called once per tick from endlessGameplayTick, before any weapon fires.
void endlessOpeningSalvoTick(void)
{
	endlessSalvoVolley = false;
	if (endlessPerkOwned[PERK_SALVO] > 0 && endlessSalvoIdle < 1000000)
		++endlessSalvoIdle;
}

// The main gun just fired: reset the idle timer and, if the pause charged this volley, arm
// endlessSalvoVolley so player_shot_create boosts every shot created for the rest of this tick.
bool endlessOpeningSalvoConsume(void)
{
	const bool charged = endlessPerkOwned[PERK_SALVO] > 0 && endlessSalvoIdle >= ENDLESS_PERK_SALVO_IDLE;
	endlessSalvoIdle = 0;
	if (charged)
		endlessSalvoVolley = true;  // rides the rest of the tick (rear + sidekicks); cleared next endlessOpeningSalvoTick
	return charged;
}

bool endlessOpeningSalvoVolleyActive(void) { return endlessMode && endlessSalvoVolley; }
int  endlessOpeningSalvoDamagePercent(void) { return ENDLESS_PERK_SALVO_DMG_PCT; }

// --- Kinetic Converter perk -----------------------------------------------------------------------
// Generator power refunded when the shield soaks a hit. `shieldAbsorbed` is the shield points lost,
// `tpwr` the shield's per-point charge cost (shields[].tpwr); refunds ENDLESS_PERK_KINETIC_PCT% of that
// per stack. The caller clamps the resulting power to the generator ceiling.
int endlessPerkKineticPower(int shieldAbsorbed, int tpwr)
{
	const int stacks = endlessMode ? endlessPerkOwned[PERK_KINETIC] : 0;
	if (stacks == 0 || shieldAbsorbed <= 0 || tpwr <= 0)
		return 0;
	return shieldAbsorbed * tpwr * ENDLESS_PERK_KINETIC_PCT * stacks / 100;
}

// --- Countermeasure Suite perk --------------------------------------------------------------------
// endlessCmCooldown counts down to the next ready burst (advanced by endlessGameplayTick).
void endlessCountermeasureTick(void)
{
	if (endlessCmCooldown > 0)
		--endlessCmCooldown;
}

// The projectile-clear radius to use RIGHT NOW: 0 if the perk isn't owned or a burst is still on
// cooldown, else the 1- or 2-stack radius. When it returns nonzero the caller fires the burst and
// must call endlessCountermeasureFired() to re-arm the cooldown.
int endlessPerkCountermeasureRadius(void)
{
	if (!endlessMode || endlessPerkOwned[PERK_COUNTERMEASURE] == 0 || endlessCmCooldown > 0)
		return 0;
	return (endlessPerkOwned[PERK_COUNTERMEASURE] >= 2) ? ENDLESS_PERK_CM_RADIUS2 : ENDLESS_PERK_CM_RADIUS1;
}

void endlessCountermeasureFired(void) { endlessCmCooldown = ENDLESS_PERK_CM_COOLDOWN; }

// --- Chain Reaction perk --------------------------------------------------------------------------
// The pulse itself (finding nearby enemies, dealing armor damage, vaporising fodder) lives at the
// player-shot kill sites in tyrian2.c, where the enemy tables and explosions are; these just report
// whether it is active and how far / hard it reaches.
bool endlessPerkChainReactionActive(void) { return endlessMode && endlessPerkOwned[PERK_CHAINRXN] > 0; }
int  endlessPerkChainRadius(void)         { return ENDLESS_PERK_CHAIN_RADIUS; }

// Armor damage the pulse deals to nearby fodder. Ordinary enemy HP is scaled up with depth
// (endlessArmorPercent, applied at spawn), so scale the pulse the same way -- otherwise a flat value
// that clears fodder early would barely scratch it deep. Kept >= the base so it never rounds to nothing.
int endlessPerkChainDamage(void)
{
	if (!endlessMode || endlessPerkOwned[PERK_CHAINRXN] == 0)
		return 0;
	const int base = endlessPerkOwned[PERK_CHAINRXN] * ENDLESS_PERK_CHAIN_DMG;
	const int scaled = base * endlessArmorPercent() / 100;
	return (scaled < base) ? base : scaled;
}

// Roll this shop visit's perk offers: up to 3 distinct perks that aren't already maxed out.
// Called once per post-zone shop (endlessBetweenLevels), before the perk menu is shown.
void endlessGeneratePerkChoices(void)
{
	int pool[PERK_COUNT], n = 0;
	for (int i = 0; i < PERK_COUNT; ++i)
		if (endlessPerkOwned[i] < endlessPerkTable[i].maxStack)
			pool[n++] = i;

	// Partial Fisher-Yates: shuffle the first min(3, n) slots and take them.
	endlessPerkChoiceN = n < 3 ? n : 3;
	for (int i = 0; i < endlessPerkChoiceN; ++i)
	{
		int j = i + (int)(endlessRand() % (unsigned)(n - i));
		int t = pool[i]; pool[i] = pool[j]; pool[j] = t;
		endlessPerkChoice[i] = pool[i];
	}
}

int endlessPerkChoiceCount(void)
{
	return endlessPerkChoiceN;
}

const char *endlessPerkChoiceName(int i)
{
	if (i < 0 || i >= endlessPerkChoiceN)
		return "";
	return endlessPerkTable[endlessPerkChoice[i]].name;
}

// Help-line text for an offered perk: its description plus how many the player already owns.
const char *endlessPerkChoiceDesc(int i)
{
	static char buf[80];
	if (i < 0 || i >= endlessPerkChoiceN)
		return "";
	const int id = endlessPerkChoice[i];
	snprintf(buf, sizeof(buf), "%s  (Owned: %d)", endlessPerkTable[id].desc, endlessPerkOwned[id]);
	return buf;
}

// Acquire offered perk i. The forced post-zone pick is FREE (perks come sparingly -- see the cadence
// gate in endlessBetweenLevels); the paid path is the E-Shop "Buy Extra Perk", which charges up front
// in endlessTryBuyExtraPerk before opening this menu.
void endlessTakePerk(int i)
{
	if (i < 0 || i >= endlessPerkChoiceN)
		return;
	const int id = endlessPerkChoice[i];
	if (endlessPerkOwned[id] < endlessPerkTable[id].maxStack)
		++endlessPerkOwned[id];
	endlessPerkDepthDone = endlessRunDepth;  // this zone's perk is resolved (survives a save/reload)
}

// Cash paid for declining the perk ("take the cash"), scaling with depth so it stays tempting.
long endlessPerkDeclineBonus(void)
{
	return 1000 + (long)endlessRunDepth * 200;
}

void endlessDeclinePerk(void)
{
	player[0].cash += endlessPerkDeclineBonus();
	endlessPerkDepthDone = endlessRunDepth;  // this zone's perk is resolved (survives a save/reload)
}

// Perk registry accessors, used by the endless debug screen to list / toggle / stack perks.
int         endlessPerkCount(void)          { return PERK_COUNT; }
const char *endlessPerkName(int id)         { return (id >= 0 && id < PERK_COUNT) ? endlessPerkTable[id].name : ""; }
const char *endlessPerkDesc(int id)         { return (id >= 0 && id < PERK_COUNT) ? endlessPerkTable[id].desc : ""; }
int         endlessPerkMaxStack(int id)     { return (id >= 0 && id < PERK_COUNT) ? endlessPerkTable[id].maxStack : 0; }
int         endlessPerkGetOwned(int id)     { return (id >= 0 && id < PERK_COUNT) ? endlessPerkOwned[id] : 0; }

void endlessPerkSetOwned(int id, int n)
{
	if (id < 0 || id >= PERK_COUNT)
		return;
	if (n < 0)
		n = 0;
	if (n > endlessPerkTable[id].maxStack)
		n = endlessPerkTable[id].maxStack;
	endlessPerkOwned[id] = (JE_byte)n;
}
