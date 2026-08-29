/* Endless run-persistent perks. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "mainint.h"
#include "palette.h"
#include "player.h"
#include "rollback.h"
#include "tyrian2.h"
#include "varz.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// PERK_* order is serialized. Append entries here and in endless_internal.h; never renumber them.
const EndlessPerk endlessPerkTable[PERK_COUNT] = {
	{ "Heavy Rounds",     "Your shots deal more damage.",         5 },
	{ "Rapid Cyclers",    "Your guns fire noticeably faster.",    4 },
	{ "Ablative Plating", "Raises your maximum armor.",           6 },
	{ "Scavenger",        "More cash from clears, bounties, buyouts.", 4 },
	{ "Nanorepair",       "Slowly regenerate armor in flight.",   3 },
	{ "Siphon",           "Chance to restore armor on a kill.",   3 },
	{ "Bounty Hunter",    "Bounties doubled, score pickups worth 4x.", 1 },
	{ "Bulwark",          "Take less damage from every hit.",     5 },
	{ "Adrenaline",       "Fire faster and hit harder when badly hurt.", 3 },
	{ "Glass Cannon",     "Big damage, but a weaker hull.",       1 },
	{ "Rapid Recharge",   "Specials, ammo and charges refill faster.", 4 },
	{ "Autofire Special", "Auto-fires your special as you shoot.", 1 },
	{ "Efficient Coils",  "Your weapons draw less generator power.", 5 },
	{ "Shield Matrix",    "Your shield recharges faster.",        4 },
	{ "High-Velocity Shots", "Your shots travel faster.",        3 },
	{ "Radar",            "Reveals sector levels; one reroll a visit.", 1 },
	{ "Surveyor",         "An extra route; kills can drop pickups.",   2 },
	{ "Executioner",      "Hits deal more to badly wounded enemies.",  3 },
	{ "Opening Salvo",    "A pause supercharges a second of fire.", 1 },
	{ "Kinetic Converter","Hits refuel gear; twiddles cost less.", 3 },
	{ "Countermeasures",  "Taking hull damage clears nearby shots.",   2 },
	{ "Chain Reaction",   "Kills blast nearby enemies.",               3 },
	{ "Financier",        "Better interest and cheaper shop prices.",  4 },
	{ "Ordnance Reserves","More sidekick ammo; specials last longer.", 4 },
	{ "Failsafe",         "A hull hit leaves you briefly untouchable.", 4 },
	{ "Guidance Package", "Main guns home in; 2 sidekicks, 3 specials.", 4 },
	{ "Twin Pods",        "Sidekicks fire twice; double ammo and power.", 1 },
	{ "Reinforced Prow",  "Ram much harder and take much less for it.", 4 },
	{ "Knife Fight",      "More damage the closer you fly to a foe.", 4 },
	{ "Deflector",        "Shots hurt your shield less and bounce back.", 2 },
};

bool endlessPerkPending = false;             // a perk pick is queued for the next shop
JE_byte endlessPerkOwned[PERK_COUNT];        // combined view of the rows below, for diagnostics
JE_byte endlessPerkTakenBy[2][PERK_COUNT];   // who picked what; each machine owns its own row

/* Perks are personal: a stack affects only the ship that picked it, and every effect reads the
 * owning ship's row (solo play is row 0). endlessPerkOwned keeps the capped combined view for
 * diagnostics; nothing gameplay-facing reads it. */
JE_byte endlessPerkEffective(uint p, int id)
{
	if (p >= COUNTOF(endlessPerkTakenBy) || id < 0 || id >= PERK_COUNT)
		return 0;
	return endlessPerkTakenBy[p][id];
}

// The stacks in effect for the ship the current effect context names.
static JE_byte perkFx(int id)
{
	return endlessPerkEffective(endlessFxPlayer(), id);
}

// The stacks of whoever is sitting at this keyboard: shop pricing, offers, and menu text.
static JE_byte perkMine(int id)
{
	return endlessPerkEffective(endlessEconomyIndex(), id);
}

// The stacks in effect for a named ship, for effects that know which ship they belong to instead of
// leaning on the effect context. Only co-op deals a second row, so everywhere else ship 0 holds the
// only picks; endlessFxPlayer normalizes the same way.
static JE_byte perkShip(uint p, int id)
{
	return endlessPerkEffective(coopEndlessMode ? p : 0, id);
}

void endlessPerkRederive(void)
{
	for (int i = 0; i < PERK_COUNT; ++i)
	{
		int total = endlessPerkTakenBy[0][i] + endlessPerkTakenBy[1][i];
		if (total > endlessPerkTable[i].maxStack)
			total = endlessPerkTable[i].maxStack;
		endlessPerkOwned[i] = (JE_byte)total;
	}
}

// Move player p's holding of one perk. Negative takes a stack back (the Amnesia gamble).
void endlessPerkGrant(uint p, int id, int delta)
{
	if (p >= COUNTOF(endlessPerkTakenBy) || id < 0 || id >= PERK_COUNT)
		return;
	int n = endlessPerkTakenBy[p][id] + delta;
	if (n < 0)
		n = 0;
	if (n > endlessPerkTable[id].maxStack)
		n = endlessPerkTable[id].maxStack;
	endlessPerkTakenBy[p][id] = (JE_byte)n;
	endlessPerkRederive();
}
int endlessPerkChoice[ENDLESS_PERK_OFFERS_MILESTONE];  // this visit's offered perk ids
int endlessPerkChoiceN = 0;           // how many are offered (0..ENDLESS_PERK_OFFERS_MILESTONE)
int endlessRegenTick = 0;             // Nanorepair countdown (reset each run)
/* Opening Salvo charges on a gun sitting idle and is spent by that gun firing, so both are per
 * ship: one shared charge had the second ship's fire spending the first ship's salvo. */
int endlessSalvoIdle[2] = { 0, 0 };   // ticks the main gun has sat idle (reset each run)
int endlessSalvoWindow[2] = { 0, 0 }; // ticks left in a consumed salvo (reset each run)
// Last depth whose post-zone perk was resolved, or -1. This prevents duplicate picks on reload.
int endlessPerkDepthDone = -1;

// Cash multiplier (100 = unchanged) from the Scavenger perk, applied to the clear bonus, the
// elite/champion bounties and the "Take the Cash" perk buyout.
int endlessPerkCashPercent(void)
{
	return 100 + perkFx(PERK_CASH) * ENDLESS_PERK_CASH_PCT;
}

// Total stacks held by the local player. Used by Take the Cash and Extra Perk pricing.
int endlessPerkTotalOwned(void)
{
	int total = 0;
	for (int i = 0; i < PERK_COUNT; ++i)
		total += perkMine(i);
	return total;
}

// Financier perk, first half: the level-clear bank-interest rate, as a % of unspent cash
// (ENDLESS_INTEREST_BASE_PCT = stock).
int endlessPerkInterestPercent(void)
{
	if (!endlessFxActive())
		return ENDLESS_INTEREST_BASE_PCT;
	return ENDLESS_INTEREST_BASE_PCT + perkFx(PERK_FINANCIER) * ENDLESS_PERK_INTEREST_PCT;
}

// Financier's shop-price multiplier in basis points; it composes with other modifiers. A shop
// price belongs to whoever is buying, the same player endlessShopTaxPercent charges.
int endlessPerkShopCostBp(void)
{
	if (!endlessFxActive())
		return 10000;
	return 10000 - perkMine(PERK_FINANCIER) * ENDLESS_PERK_DISCOUNT_BP;
}

// +max armor from the Ablative Plating perk; added to the ship's armor each level start (varz.c),
// alongside the outpost hull upgrade (endlessArmorBonus).
int endlessPerkArmorBonus(void)
{
	int bonus = perkFx(PERK_ARMOR) * ENDLESS_PERK_ARMOR_STEP;
	if (perkFx(PERK_GLASSCANNON))
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

/* Personal on both halves: the stacks are the ship's own, and so is the hull that arms them. A
 * partner in trouble used to arm it too, which under personal perks would have let one ship's
 * damage drive the other ship's perk. */
bool endlessAdrenalineActive(void)
{
	const uint p = endlessFxPlayer();
	return perkFx(PERK_ADRENALINE) > 0 && player[p].initial_armor > 0
	    && player[p].armor * ENDLESS_PERK_ADRENALINE_HP < player[p].initial_armor;
}

// Fire-rate decrement percentage, including Adrenaline when `hurtBonus` is true.
static int endlessPerkFireRate(bool hurtBonus)
{
	int rate = perkFx(PERK_FIRERATE) * ENDLESS_PERK_FIRE_PCT;
	if (hurtBonus && endlessAdrenalineActive())
		rate += perkFx(PERK_ADRENALINE) * ENDLESS_PERK_ADRENALINE_PCT;
	return rate;
}

/* Per-ship fractional carry for Rapid Cyclers and Adrenaline. Register it for
 * rollback because the crossing tick determines when each gun fires. */
int endlessPerkFireAccum[2];
int endlessPerkSpecialCdAccum[2];
int endlessPerkKineticAmmoAccum[2];

int endlessPerkFireDecrements(void)
{
	if (!endlessFxActive())
	{
		endlessPerkFireAccum[0] = endlessPerkFireAccum[1] = 0;
		return 0;
	}
	return endlessAccumSteps(&endlessPerkFireAccum[endlessFxPlayer()], endlessPerkFireRate(true));
}

// The preview includes Rapid Cyclers but not Adrenaline because zones start at full hull.
// Its accumulator is isolated from gameplay.
int endlessPerkPreviewFireDecrements(void)
{
	static int accum = 0;
	if (!endlessFxActive())
	{
		accum = 0;
		return 0;
	}
	return endlessAccumSteps(&accum, endlessPerkFireRate(false));
}

// Rapid Recharge applies to the special-fire gate and sidekick refill, not the main guns.
// Charge sidekicks have no magazine to refill; endlessPerkChargeTicks covers them instead.
int endlessPerkSpecialCooldownDecrements(void)
{
	if (!endlessFxActive())
	{
		endlessPerkSpecialCdAccum[0] = endlessPerkSpecialCdAccum[1] = 0;
		return 0;
	}
	return endlessAccumSteps(&endlessPerkSpecialCdAccum[endlessFxPlayer()],
	                         perkFx(PERK_SPECIALCD) * ENDLESS_PERK_SPECIALCD_PCT);
}

// The special-fire path combines this run perk with the debug autoFireSpecial flag.
bool endlessPerkAutoFireSpecial(void)
{
	return endlessFxActive() && perkFx(PERK_AUTOSPECIAL) > 0;
}

// Efficient Coils perk: power-use scale per main-weapon shot (100 = normal, lower = cheaper);
// applied in shots.c player_shot_create. Floored so firing is never entirely free.
int endlessPerkPowerUsePercent(void)
{
	if (!endlessFxActive())
		return 100;
	const int pct = 100 - perkFx(PERK_POWERUSE) * ENDLESS_PERK_POWERUSE_PCT;
	return pct < ENDLESS_PERK_POWERUSE_MIN ? ENDLESS_PERK_POWERUSE_MIN : pct;
}

// Shorten an interval per perk stack, without dropping below `minimum`.
static int endlessPerkShorten(int base, int perk, int step, int minimum)
{
	if (!endlessFxActive() || perkFx(perk) == 0)
		return base;
	const int v = base - perkFx(perk) * step;
	return v < minimum ? minimum : v;
}

// Shield Matrix perk: shortens the shield-regen interval from `base` (tyrian2.c), floored; a
// no-op outside endless / with no stacks. A quicker shield still drains the generator quicker.
int endlessPerkShieldWait(int base)
{
	return endlessPerkShorten(base, PERK_SHIELDREGEN,
	                          ENDLESS_PERK_SHIELDRGN_STEP, ENDLESS_PERK_SHIELDRGN_MIN);
}

// Rapid Recharge perk, third effect: shortens the charge-sidekick charge interval from `base`
// (mainint.c), floored; a no-op outside endless / with no stacks. Magazine sidekicks refill quicker
// via the decrements above, so this is what the perk does for the charge-type ones instead.
int endlessPerkChargeTicks(int base)
{
	return endlessPerkShorten(base, PERK_SPECIALCD,
	                          ENDLESS_PERK_CHARGE_STEP, ENDLESS_PERK_CHARGE_MIN);
}

// High-Velocity Rounds perk: shot travel-speed scale (100 = normal), applied in shots.c
// player_shot_create to the genuine shot velocities. A no-op outside endless / with no stacks.
int endlessPerkShotSpeedPercent(void)
{
	if (!endlessFxActive())
		return 100;
	return 100 + perkFx(PERK_SHOTSPEED) * ENDLESS_PERK_SHOTSPEED_PCT;
}

// Radar adds the shipped level name to each Chart-a-Course help line, for the player who owns it.
bool endlessPerkRadarActive(void)
{
	return endlessFxActive() && perkMine(PERK_RADAR) > 0;
}

/* Add Surveyor routes after the RNG roll so the seed stream remains unchanged. The slate is
 * shared, so the stacks that widen it are the charting seat's own; both machines derive that
 * seat identically, and the slates stay in step. */
int endlessPerkSurveyorRoutes(void)
{
	if (!endlessFxActive())
		return 0;
	return endlessPerkEffective(endlessChartingPlayerIndex(), PERK_SURVEYOR)
	     * ENDLESS_PERK_SURVEYOR_ROUTES;
}

// Calculate Executioner from raw shot damage before boss or elite scaling.
// `fullHp` is the latched healthbar maximum; zero means the target has not been hit.
int endlessPerkExecutionerBonus(int damage, int armorleft, int fullHp, bool boss)
{
	const int stacks = endlessFxActive() ? perkFx(PERK_EXECUTIONER) : 0;
	if (stacks == 0 || damage <= 0 || fullHp <= 0 || armorleft <= 0 || armorleft >= 255)
		return 0;
	const int threshold = boss ? ENDLESS_PERK_EXEC_BOSS_PCT : ENDLESS_PERK_EXEC_HP_PCT;
	if (armorleft * 100 >= fullHp * threshold)  // not wounded enough
		return 0;
	// Round to nearest so low-damage and piercing shots can receive the bonus.
	return (damage * stacks * ENDLESS_PERK_EXEC_DMG_PCT + 50) / 100;
}

// Salvo charge and its active window are per ship. The gun that waits earns
// the charge, and the gun that fires spends it.

// One ship's start-of-tick housekeeping.
static void endlessOpeningSalvoTickOne(void)
{
	if (endlessSalvoWindow[endlessFxPlayer()] > 0)
	{
		--endlessSalvoWindow[endlessFxPlayer()];          // real time, not held-fire time: the gauge counts it down
		return;                        // and no second charge banks while one runs
	}
	if (perkFx(PERK_SALVO) > 0 && endlessSalvoIdle[endlessFxPlayer()] < 1000000)
		++endlessSalvoIdle[endlessFxPlayer()];            // capped so a very long idle can't overflow
}

// Consume a charged salvo without extending an active window.
bool endlessOpeningSalvoConsume(void)
{
	if (endlessSalvoWindow[endlessFxPlayer()] > 0)
		return true;

	const bool charged = perkFx(PERK_SALVO) > 0 && endlessSalvoIdle[endlessFxPlayer()] >= ENDLESS_PERK_SALVO_IDLE;
	endlessSalvoIdle[endlessFxPlayer()] = 0;
	if (charged)
		endlessSalvoWindow[endlessFxPlayer()] = ENDLESS_PERK_SALVO_WINDOW;
	return charged;
}

// Start-of-tick housekeeping, from endlessGameplayTick, before any weapon fires. Run-wide, so it
// walks the ships rather than trusting the effect context, which is player 1 at that point.
void endlessOpeningSalvoTick(void)
{
	const uint saved = endlessFxPlayer();
	for (uint p = 0; p < endlessEffectPlayers(); ++p)
	{
		endlessSetFxPlayer(p);
		endlessOpeningSalvoTickOne();
	}
	endlessSetFxPlayer(saved);
}

bool endlessOpeningSalvoVolleyActive(void) { return endlessFxActive() && endlessSalvoWindow[endlessFxPlayer()] > 0; }

// How much of the generator gauge reads green, 0..100: a full bar while a charge is banked, then
// the share of the spent window still to run, so the green recedes as the salvo burns down.
int endlessOpeningSalvoGaugePercent(void)
{
	if (!endlessFxActive() || perkFx(PERK_SALVO) == 0)
		return 0;
	if (endlessSalvoWindow[endlessFxPlayer()] > 0)
		return endlessSalvoWindow[endlessFxPlayer()] * 100 / ENDLESS_PERK_SALVO_WINDOW;
	return (endlessSalvoIdle[endlessFxPlayer()] >= ENDLESS_PERK_SALVO_IDLE) ? 100 : 0;
}

// x2.5 a magnitude no per-shot salvo tag can reach while a window is up: a special that fires
// nothing (repulsor push, heal, invuln duration), the Zinglon pillar, and a ram.
// The floor matters: the repulsor hands this a 1, which would otherwise scale back to itself.
int endlessOpeningSalvoScale(int value)
{
	if (!endlessOpeningSalvoVolleyActive() || value <= 0)
		return value;
	const int scaled = (value * (100 + ENDLESS_PERK_SALVO_DMG_PCT) + 50) / 100;
	return (scaled > value) ? scaled : value + 1;
}

int  endlessOpeningSalvoDamagePercent(void) { return ENDLESS_PERK_SALVO_DMG_PCT; }

// Kinetic Converter. A hit that lands is fed back into what shooting costs: the generator, the
// special recharge, a sidekick magazine and a charge ramp. Every share scales with the stacks.

// Refund a percentage of absorbed shield cost per stack. The caller clamps generator power.
int endlessPerkKineticPower(int shieldAbsorbed, int tpwr)
{
	const int stacks = endlessFxActive() ? perkFx(PERK_KINETIC) : 0;
	if (stacks == 0 || shieldAbsorbed <= 0 || tpwr <= 0)
		return 0;
	return shieldAbsorbed * tpwr * ENDLESS_PERK_KINETIC_PCT * stacks / 100;
}

/* Ticks a hit takes off a special recharge clock. A share of the remaining time rather than a flat
 * count, because specials range from a 15-tick link recharge to a 250-tick one; the floor of a tick
 * per stack still clears an almost-expired clock. */
int endlessPerkKineticCooldownCut(int remaining)
{
	const int stacks = endlessFxActive() ? perkFx(PERK_KINETIC) : 0;
	if (stacks == 0 || remaining <= 0)
		return 0;
	int cut = remaining * ENDLESS_PERK_KINETIC_CD_PCT * stacks / 100;
	if (cut < stacks)
		cut = stacks;
	return (cut > remaining) ? remaining : cut;
}

/* Whole sidekick rounds a hit gives back. A hit is worth a fraction of a round per stack and the
 * remainder carries, so one stack still pays out over several hits. Stateful: call once per hit
 * and honour the answer. */
int endlessPerkKineticAmmoRounds(void)
{
	if (!endlessFxActive())
	{
		endlessPerkKineticAmmoAccum[0] = endlessPerkKineticAmmoAccum[1] = 0;
		return 0;
	}
	return endlessAccumSteps(&endlessPerkKineticAmmoAccum[endlessFxPlayer()],
	                         perkFx(PERK_KINETIC) * ENDLESS_PERK_KINETIC_AMMO_PCT);
}

/* Charge stages a hit walks a charge sidekick up. Whole stages, since a stage is already the unit
 * that ramp counts in; the caller clamps at the pod's top stage. */
int endlessPerkKineticChargeStages(void)
{
	return endlessFxActive() ? perkFx(PERK_KINETIC) * ENDLESS_PERK_KINETIC_STAGES : 0;
}

/* What a twiddle's shield or armor charge deducts after the discount, rounded to the nearest point
 * but never down to free. `listCost` is the charge the twiddle would have taken unaided, the
 * proportional ones included, and the caller keeps that list price as the effect's magnitude. */
int endlessPerkKineticTwiddleCost(int listCost)
{
	const int stacks = endlessFxActive() ? perkFx(PERK_KINETIC) : 0;
	if (stacks == 0 || listCost <= 0)
		return listCost;
	int pct = 100 - ENDLESS_PERK_KINETIC_TWIDDLE_PCT * stacks;
	if (pct < 0)  // deeper stacks can reach free, never a refund
		pct = 0;
	const int paid = (listCost * pct + 50) / 100;
	return (paid > 0) ? paid : 1;
}

// Return the Countermeasures radius for the ship selected by the effect context.
int endlessPerkCountermeasureRadius(void)
{
	if (!endlessFxActive() || perkFx(PERK_COUNTERMEASURE) == 0)
		return 0;
	return (perkFx(PERK_COUNTERMEASURE) >= 2) ? ENDLESS_PERK_CM_RADIUS2 : ENDLESS_PERK_CM_RADIUS1;
}

// A hull hit grants invulnerability. The active window prevents it from chaining.
int endlessPerkFailsafeTicks(void)
{
	if (!endlessFxActive())
		return 0;
	return perkFx(PERK_FAILSAFE) * ENDLESS_PERK_FAILSAFE_TICKS;
}

/* Guidance Package steering interval for a shot leaving `bay`, or 0 when it does not apply.
 * Existing homing uses `ownDelay`; stack thresholds are documented in GUIDE.md. */
int endlessPerkGuidanceDelay(uint bay, int ownDelay)
{
	const int stacks = endlessFxActive() ? perkFx(PERK_GUIDANCE) : 0;
	if (stacks == 0)
		return 0;

	const bool mainGun = bay == SHOT_FRONT || bay == SHOT_REAR || bay == SHOT_P2_CHARGE;
	const bool sidekick = bay == SHOT_LEFT_SIDEKICK || bay == SHOT_RIGHT_SIDEKICK;
	const bool specialGun = bay == SHOT_SPECIAL || bay == SHOT_SPECIAL2;
	if (!mainGun && !(sidekick && stacks >= ENDLESS_PERK_GUIDANCE_SIDEKICK_STACKS)
	    && !(specialGun && stacks >= ENDLESS_PERK_GUIDANCE_SPECIAL_STACKS))
		return 0;

	const int delay = (ownDelay > 0)
	                ? ownDelay - stacks * ENDLESS_PERK_GUIDANCE_TIGHTEN
	                : ENDLESS_PERK_GUIDANCE_DELAY - (stacks - 1) * ENDLESS_PERK_GUIDANCE_STEP;
	return delay < 1 ? 1 : delay;
}

/* Twin Pods offset from the named ship's pod, or 0 when inactive. The fire site moves the
 * original volley inboard by the same distance. */
int endlessPerkTwinPodOffset(uint p, uint sidekick)
{
	if (!endlessFxActive() || perkShip(p, PERK_TWINPODS) == 0)
		return 0;
	const int half = ENDLESS_PERK_TWINPODS_SPREAD_PX / 2;
	return (sidekick == LEFT_SIDEKICK) ? -half : half;
}

// Reinforced Prow: what a tick of contact deals to the enemy, from the stock `damage`.
int endlessPerkProwRamDamage(int damage)
{
	if (!endlessFxActive() || damage <= 0)
		return damage;
	return damage * (100 + perkFx(PERK_PROW) * ENDLESS_PERK_PROW_DMG_PCT) / 100;
}

// Reinforced Prow: the share of contact damage the ship still takes (100 = all of it). The ram
// site applies it after every other contact scale and keeps at least one point of a real hit.
int endlessPerkProwContactPercent(void)
{
	if (!endlessFxActive())
		return 100;
	return 100 - perkFx(PERK_PROW) * ENDLESS_PERK_PROW_TAKEN_PCT;
}

/* Knife Fight measures from hull to hull. The ship is its 24x28 sprite, blitted 5 left and 7 up
 * of its position; a tile is the 12x14 cell at its position, or the four cells around it when
 * size is 1. */
#define KNIFE_SHIP_HALF_W 12
#define KNIFE_SHIP_HALF_H 14
static int knife_box_gap(int cx0, int cy0, int hw0, int hh0, int cx1, int cy1, int hw1, int hh1)
{
	const int gapX = abs(cx0 - cx1) - (hw0 + hw1);
	const int gapY = abs(cy0 - cy1) - (hh0 + hh1);
	const int gap = (gapX > gapY) ? gapX : gapY;
	return (gap < 0) ? 0 : gap;
}

// One tile's gap from a ship centred at (shipX, shipY). A 2x2 body is two cells wide and high.
static int knife_tile_gap(int shipX, int shipY, unsigned g)
{
	const int half = (enemy[g].size == 1) ? 2 : 1;
	return knife_box_gap(shipX, shipY, KNIFE_SHIP_HALF_W, KNIFE_SHIP_HALF_H,
	                     enemy[g].ex + enemy[g].mapoffset + 6, enemy[g].ey + 7, 6 * half, 7 * half);
}

/* The gap in px between ship p's hull and the nearest live tile of the hull `slot` belongs to: every
 * tile sharing a nonzero link is one hull, so a boss is measured to whichever part is closest, and a
 * lone enemy is measured to itself. */
int endlessShipHullGapPx(uint p, unsigned slot)
{
	if (p >= COUNTOF(player) || slot >= COUNTOF(enemy))
		return INT_MAX;

	const int shipX = player[p].x + 7, shipY = player[p].y + 7;
	int nearest = knife_tile_gap(shipX, shipY, slot);

	const JE_byte link = enemy[slot].linknum;
	if (link == 0)
		return nearest;

	for (unsigned g = 0; g < COUNTOF(enemy); ++g)
	{
		if (g == slot || enemy[g].linknum != link || enemyAvail[g] == 1)
			continue;
		const int gap = knife_tile_gap(shipX, shipY, g);
		if (gap < nearest)
			nearest = gap;
	}
	return nearest;
}

// Knife Fight: the bonus percentage a hit on `slot` by the fx ship earns, whole inside
// ENDLESS_PERK_KNIFE_FULL_PX and fading linearly to nothing over the fade distance beyond it.
int endlessPerkKnifeFightPercent(unsigned slot)
{
	const int stacks = endlessFxActive() ? perkFx(PERK_KNIFE) : 0;
	if (stacks == 0)
		return 0;
	const int gap = endlessShipHullGapPx(endlessFxPlayer(), slot);
	const int full = stacks * ENDLESS_PERK_KNIFE_PCT;
	if (gap <= ENDLESS_PERK_KNIFE_FULL_PX)
		return full;
	const int left = ENDLESS_PERK_KNIFE_FULL_PX + ENDLESS_PERK_KNIFE_FADE_PX - gap;
	if (left <= 0)
		return 0;
	return (full * left + ENDLESS_PERK_KNIFE_FADE_PX / 2) / ENDLESS_PERK_KNIFE_FADE_PX;
}

// Knife Fight in armor points, from the raw damage of a hit before any enemy-health scaling, so
// it is measured across the same accumulator as Executioner is (tyrian2.c) and holds against a
// boss.
int endlessPerkKnifeFightBonus(int damage, int pct)
{
	if (damage <= 0 || pct <= 0)
		return 0;
	return (damage * pct + 50) / 100;
}

/* Knife Fight hit effect. It uses presentation-only seeded sparks and the common Endless shade
 * lift; see doc/notes.md#perks. */
#define KNIFE_BLOOD_DROPS_MAX     4  // at the deepest bonus; enough to mark the hit and stay out of the way
#define KNIFE_BLOOD_PER_FRAME    10  // ...and one presented frame spawns no more than this, over every hit in it
#define KNIFE_BLOOD_SPREAD_PX     4  // how wide a shower starts around the hit
#define KNIFE_BLOOD_LIFE_TICKS    9  // a drop runs for about a quarter second, then fades
#define KNIFE_BLOOD_SHADE_LO      9  // the shades a bank is judged red on: the bright half a drop plots in
#define KNIFE_BLOOD_SHADE_HI     14

/* The reddest ramp of the palette the level is flying, since no fixed bank is red in all 24 shipped
 * ones. Recomputed once per presented frame, because a level script can change the palette. */
static Uint8 knife_blood_bank(void)
{
	static Uint8 bank = 0;
	static Uint32 bankFrame = (Uint32)-1;
	if (bankFrame == rl_presented_frames())
		return bank;
	bankFrame = rl_presented_frames();

	int best = INT_MIN;
	for (unsigned b = 0; b < 16; ++b)
	{
		int score = 0;
		for (unsigned s = KNIFE_BLOOD_SHADE_LO; s <= KNIFE_BLOOD_SHADE_HI; ++s)
		{
			const SDL_Color *const c = &colors[b * 16 + s];
			score += (int)c->r - (c->g > c->b ? c->g : c->b);
		}
		if (score > best)
		{
			best = score;
			bank = (Uint8)(b << 4);
		}
	}
	return bank;
}

// Bleed the hull `slot` for a hit that Knife Fight raised by `pct`, deeper bonus for more blood.
void endlessPerkKnifeFightBlood(unsigned slot, int pct)
{
	static Uint32 budgetFrame = (Uint32)-1;
	static int budgetLeft = 0;

	if (rollback_resim_silent || pct <= 0 || slot >= COUNTOF(enemy))
		return;

	if (budgetFrame != rl_presented_frames())
	{
		budgetFrame = rl_presented_frames();
		budgetLeft = KNIFE_BLOOD_PER_FRAME;
	}
	if (budgetLeft <= 0)
		return;

	// The deepest bonus the perk can reach is the full shower; a fading one bleeds proportionally.
	const int full = endlessPerkTable[PERK_KNIFE].maxStack * ENDLESS_PERK_KNIFE_PCT;
	int drops = (pct * KNIFE_BLOOD_DROPS_MAX + full - 1) / full;
	if (drops > budgetLeft)
		drops = budgetLeft;
	budgetLeft -= drops;

	// The hit's own hull, at the middle of its sprite, on the screen x the collision measured.
	const int cx = enemy[slot].ex + enemy[slot].mapoffset + 6;
	const int cy = enemy[slot].ey + 7;
	if (cx < 0 || cy < 0)
		return;

	JE_doSPDripSeeded((JE_word)cx, (JE_word)cy, (JE_word)drops, KNIFE_BLOOD_SPREAD_PX,
	                  knife_blood_bank(), KNIFE_BLOOD_LIFE_TICKS, ENDLESS_SPARK_BRIGHT,
	                  rl_presented_frames() * 251u + slot);
}

/* Returned-shot damage from a shield absorption. Keep it clear of the 250+ piercing and 99 ice
 * markers; collision code applies the firing ship's scaling. */
int endlessPerkDeflectDamage(int absorbed)
{
	const int stacks = endlessFxActive() ? perkFx(PERK_DEFLECTOR) : 0;
	if (stacks == 0 || absorbed <= 0)
		return 0;
	int damage = (stacks >= 2) ? absorbed * ENDLESS_PERK_DEFLECT_MULT2 / 100 : absorbed;
	if (damage > 249)
		damage = 249;
	else if (damage == 99)
		damage = 100;
	return damage;
}

// Shield points refunded after damage resolution. Too-small discounts round to zero.
int endlessPerkDeflectShieldSpared(int absorbed)
{
	const int stacks = endlessFxActive() ? perkFx(PERK_DEFLECTOR) : 0;
	if (stacks == 0 || absorbed <= 0)
		return 0;
	return (absorbed * stacks * ENDLESS_PERK_DEFLECT_SHIELD_PCT + 50) / 100;
}

// Start each zone with Opening Salvo charged.
void endlessResetZonePerkTimers(void)
{
	for (unsigned p = 0; p < COUNTOF(endlessSalvoIdle); ++p)
	{
		endlessSalvoIdle[p]   = ENDLESS_PERK_SALVO_IDLE;  // charged: the 2s wait is dead time here
		endlessSalvoWindow[p] = 0;                        // no half-spent salvo carries over
	}
}

// Pulse application lives at the player-shot kill sites in tyrian2.c.
bool endlessPerkChainReactionActive(void) { return endlessFxActive() && perkFx(PERK_CHAINRXN) > 0; }

// The ship whose stacks decide a pulse: the one that made the kill, or, when nothing can be
// credited with it, whichever ship holds more (endlessCountKill credits an unclaimed kill to
// both).
uint endlessPerkChainOwner(int killer)
{
	if (!endlessCoop())
		return 0;
	if (killer >= 0 && (uint)killer < COUNTOF(endlessPerkTakenBy))
		return (uint)killer;
	return (endlessPerkTakenBy[1][PERK_CHAINRXN] > endlessPerkTakenBy[0][PERK_CHAINRXN]) ? 1 : 0;
}

// Stacks widen the blast as well as deepening it. Reach compounds with the cascade, since it decides
// whether the next enemy is close enough to carry the wave on.
int endlessPerkChainRadius(void)
{
	if (!endlessFxActive() || perkFx(PERK_CHAINRXN) == 0)
		return 0;
	return ENDLESS_PERK_CHAIN_RADIUS + (perkFx(PERK_CHAINRXN) - 1) * ENDLESS_PERK_CHAIN_REACH;
}

// A pulse is the owning ship's damage, so it rides that ship's damage scale exactly as its guns do,
// drives and hostile cuts included, off a base its own stacks set.
int endlessPerkChainDamage(bool salvoBoosted)
{
	if (!endlessFxActive() || perkFx(PERK_CHAINRXN) == 0)
		return 0;
	return endlessScaleOwnDamage(perkFx(PERK_CHAINRXN) * ENDLESS_PERK_CHAIN_DMG, salvoBoosted);
}

// Ordnance Reserves expands sidekick magazines and timed special effects.

// The magazine bonus as a percentage, 0 when it isn't applying. The shop name label (episodes.c)
// and the in-flight magazine (varz.c) both come off this, so the number you buy is the number you fly.
int endlessPerkAmmoPercent(void)
{
	if (!endlessFxActive())
		return 0;
	return perkFx(PERK_ORDNANCE) * ENDLESS_PERK_AMMO_PCT;
}

// Leave charge and infinite sidekicks at zero. Round a magazine bonus up and cap it for the HUD.
int endlessPerkSidekickAmmo(int base)
{
	const int pct = endlessPerkAmmoPercent();
	if (base <= 0 || pct == 0)
		return base;
	int bonus = base * pct / 100;
	if (bonus < 1)
		bonus = 1;
	const int total = base + bonus;
	return total > ENDLESS_PERK_AMMO_CAP ? ENDLESS_PERK_AMMO_CAP : total;
}

// Scale round refill time so a larger magazine keeps the stock full-refill duration.
int endlessPerkSidekickRefillTicks(int baseTicks, int stockAmmo)
{
	const int mag = endlessPerkSidekickAmmo(stockAmmo);
	if (baseTicks <= 0 || stockAmmo <= 0 || mag <= stockAmmo)
		return baseTicks;
	const int ticks = baseTicks * stockAmmo / mag;
	return ticks < 1 ? 1 : ticks;  // never a round per tick, however deep the reserve gets
}

// Extend a special duration; `cap` protects byte-wide fields, and zero disables the cap.
int endlessPerkSpecialDuration(int base, int cap)
{
	if (!endlessFxActive() || perkFx(PERK_ORDNANCE) == 0 || base <= 0)
		return base;
	const int v = base * (100 + perkFx(PERK_ORDNANCE) * ENDLESS_PERK_SPECDUR_PCT) / 100;
	return (cap > 0 && v > cap) ? cap : v;
}

// Roll this shop visit's perk offers: up to `offers` distinct perks that aren't already maxed out.
// Generate distinct, non-maxed perk offers for the requested menu size.
void endlessGeneratePerkChoices(int offers)
{
	offers = endlessClamp(offers, 0, ENDLESS_PERK_OFFERS_MILESTONE);  // never past the array width

	int pool[PERK_COUNT] = { 0 }, n = 0;
	for (int i = 0; i < PERK_COUNT; ++i)
		if (perkMine(i) < endlessPerkTable[i].maxStack)   // room in the picker's own row
			pool[n++] = i;

	// Partial Fisher-Yates: shuffle the first min(offers, n) slots and take them.
	endlessPerkChoiceN = n < offers ? n : offers;
	for (int i = 0; i < endlessPerkChoiceN; ++i)
	{
		int j = i + (int)(endlessRandFor(endlessEconomyIndex()) % (unsigned)(n - i));
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

// Help-line text for an offered perk: what it does, and how much room is left in it. Two strings
// rather than one, because the menu draws the count flush right of the description, not after it.
const char *endlessPerkChoiceDesc(int i)
{
	if (i < 0 || i >= endlessPerkChoiceN)
		return "";
	return endlessPerkTable[endlessPerkChoice[i]].desc;
}

const char *endlessPerkChoiceOwnedText(int i)
{
	static char buf[24];
	if (i < 0 || i >= endlessPerkChoiceN)
		return "";
	const int id = endlessPerkChoice[i];
	snprintf(buf, sizeof(buf), "Owned %d/%d", perkMine(id), endlessPerkTable[id].maxStack);
	return buf;
}

// The paid path charges before this function; post-zone picks are free.
void endlessTakePerk(int i)
{
	if (i < 0 || i >= endlessPerkChoiceN)
		return;
	const int id = endlessPerkChoice[i];
	if (perkMine(id) < endlessPerkTable[id].maxStack)
		endlessPerkGrant(endlessEconomyIndex(), id, 1);
	endlessPerkDepthDone = endlessRunDepth;  // this zone's perk is resolved (survives a save/reload)
}

// Cash paid by Take the Cash. Its tuning constants are in endless_internal.h.
Sint64 endlessPerkDeclineBonus(void)
{
	// A thinned pool never pays less than a standard slate; milestone slates pay proportionally more.
	const int offers = endlessClamp(endlessPerkChoiceN, ENDLESS_PERK_OFFERS, ENDLESS_PERK_OFFERS_MILESTONE);
	int surcharge = endlessPerkTotalOwned() * ENDLESS_PERK_DECLINE_OWNED_PCT;
	if (surcharge > ENDLESS_PERK_DECLINE_OWNED_CAP)
		surcharge = ENDLESS_PERK_DECLINE_OWNED_CAP;

	Sint64 cash = endlessClearBase() * ENDLESS_PERK_DECLINE_MULT / 10;
	cash = cash * offers / ENDLESS_PERK_OFFERS;
	cash = cash * (100 + surcharge) / 100;

	// Scavenger, as on every other endless cash source. The buyout is taken by the player at
	// this keyboard, so name them while their rate is read.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(endlessEconomyIndex());
	cash = cash * endlessPerkCashPercent() / 100;
	endlessSetFxPlayer(fxSaved);
	return cash;
}

void endlessDeclinePerk(void)
{
	endlessCashCredit(endlessPerkDeclineBonus(), ENDLESS_CASH_PERK);
	endlessPerkDepthDone = endlessRunDepth;  // this zone's perk is resolved (survives a save/reload)
}

// Perk registry accessors, used by the endless debug screen to list / toggle / stack perks.
int         endlessPerkCount(void)          { return PERK_COUNT; }
const char *endlessPerkName(int id)         { return (id >= 0 && id < PERK_COUNT) ? endlessPerkTable[id].name : ""; }
const char *endlessPerkDesc(int id)         { return (id >= 0 && id < PERK_COUNT) ? endlessPerkTable[id].desc : ""; }
int         endlessPerkMaxStack(int id)     { return (id >= 0 && id < PERK_COUNT) ? endlessPerkTable[id].maxStack : 0; }
// Perks are personal, so this reads the row endlessPerkSetOwned writes: this machine's own.
int         endlessPerkGetOwned(int id)     { return endlessPerkEffective(endlessEconomyIndex(), id); }

void endlessPerkSetOwned(int id, int n)
{
	endlessPerkSetOwnedFor(endlessEconomyIndex(), id, n);
}

int endlessPerkGetOwnedFor(uint p, int id)
{
	return endlessPerkEffective(p, id);
}

void endlessPerkSetOwnedFor(uint p, int id, int n)
{
	if (p >= COUNTOF(endlessPerkTakenBy) || id < 0 || id >= PERK_COUNT)
		return;
	if (n < 0)
		n = 0;
	if (n > endlessPerkTable[id].maxStack)
		n = endlessPerkTable[id].maxStack;
	endlessPerkTakenBy[p][id] = (JE_byte)n;
	endlessPerkRederive();
}
