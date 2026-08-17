/* Endless outpost stock, pricing, E-Shop purchases, and gambles. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "game_menu.h"
#include "keyboard.h"
#include "lvlmast.h"
#include "mainint.h"
#include "musmast.h"
#include "network.h"
#include "palette.h"
#include "player.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Escalating outpost prices, reset each visit.
Sint64 endlessRerollCost[2] = { 0, 0 };
Sint64 endlessHullCost[2]   = { 0, 0 };

// Store the visit's kill-fire purchase until course selection folds it into the next sector.
unsigned endlessPurchasedMods[2] = { 0, 0 };
// Which kill-fire buff was bought this visit (mutually exclusive): 0 none, 1 Turbodrive,
// 2 Overdrive.
int endlessBuffKind[2] = { 0, 0 };
// Overblast damage stacks rise per kill and reset when the kill-fire window closes.
int endlessOverdriveStacks[2] = { 0, 0 };

// Absolute run depth at which kill-fire purchases unlock, or zero when available.
int endlessBuffCooldownUntil[2] = { 0, 0 };

// E-Shop state.
// Buff "charge" scales the kill-fire window/damage with the cash paid (normalized by depth,
// cap 20). Reset each run.
int endlessBuffCharge[2] = { 0, 0 };
// Base kill-fire window for one ship, extended by what that ship paid for its drive:
// charge 0 -> 1.0x (~2s), charge 10 -> 1.75x (~3.5s), charge 20 -> 2.5x (~5s).
int endlessBuffWindowTicksFor(uint p)
{
	const int charge = endlessBuffCharge[(p < COUNTOF(endlessBuffCharge)) ? p : 0];
	return ENDLESS_TURBODRIVE_TICKS * (40 + 3 * charge) / 40;
}

int endlessBuffWindowTicks(void)
{
	return endlessBuffWindowTicksFor(endlessFxPlayer());
}

// The player the outpost is spending for, and their wallet.
static uint me(void)          { return endlessEconomyIndex(); }
static Player *shopper(void)  { return &player[endlessEconomyIndex()]; }
static Sint64 shopperCash(void) { return player[endlessEconomyIndex()].cash; }
static int endlessBuffChargeFromPaid(Sint64 paid)  // cash paid -> charge tier (normalized by depth), cap 20
{
	const Sint64 unit = 2000 + (Sint64)endlessRunDepth * 150;
	const int c = (int)(paid / unit);
	return (c > 20) ? 20 : (c < 0 ? 0 : c);
}

// A held revive persists for the run and its price ladder rides endlessRevivesUsed; sabotage
// charges and their price ladder are per visit, both reset in endlessResetShopPrices.
bool endlessReviveHeld[2] = { false, false };
int  endlessRevivesUsed[2] = { 0, 0 };
int  endlessCleanseChargeCount[2] = { 0, 0 };
Sint64 endlessBombCost[2] = { 0, 0 }, endlessCleanseCost[2] = { 0, 0 };
int  endlessExtraPerksBought[2] = { 0, 0 };
int  endlessExtraPerksVisit[2]  = { 0, 0 };
char endlessGambleMsg[2][48] = { "", "" };
bool endlessGamblePerkWon[2] = { false, false };  // a gamble handed out a free perk pick; the E-Shop dispatch opens MENU_PERKS
int  endlessShopTax[2] = { 0, 0 };            // Loan Shark: permanent +% on every shop price for the rest of the run
bool endlessGambleRigged[2] = { false, false };   // Rigged rolls twice and keeps the worse result
int  endlessLongCon[2] = { 0, 0 };            // The Long Con: sectors until a paid-and-forgotten APEX ambush comes due (0 = none)
bool endlessResumeVisit   = false;  // a save was just loaded: the next outpost restores its snapshot instead of rerolling (consumed by endlessBetweenLevels)
bool endlessCreditsShown  = false;  // the zone-100 credits roll has already played this run; rides the save so reloading the zone-101 outpost doesn't replay it

// Cash-fraction purchases use the entry balance so their prices remain fixed during the visit.
Sint64 endlessShopEntryCash[2] = { 0, 0 };

#define ENDLESS_HULL_STEP 6      // +armor per hull upgrade
#define ENDLESS_HULL_BASE 60     // starting cap on the run-persistent armor bonus (zone 0)
#define ENDLESS_HULL_MAX  150    // absolute ceiling (ship base + this + perks stays < 255, byte-safe)

// The hull-reinforcement cap rises with depth, so the outpost always has another tier to sell
// on a deep run. Every 6 zones unlocks one more +6 step, up to ENDLESS_HULL_MAX.
static int endlessHullMax(void)
{
	int m = ENDLESS_HULL_BASE + (endlessRunDepth / 6) * ENDLESS_HULL_STEP;
	return (m > ENDLESS_HULL_MAX) ? ENDLESS_HULL_MAX : m;
}

// Starting cash for a fresh run, by chosen difficulty (easier = more to spend).
Sint64 endlessStartingCash(void)
{
	switch (difficultyLevel)
	{
	case DIFFICULTY_WIMP:       return 45000;  // debug-only: not on the Difficulty Level select screen
	case DIFFICULTY_EASY:       return 34000;
	case DIFFICULTY_NORMAL:     return 25000;
	case DIFFICULTY_HARD:       return 18000;
	case DIFFICULTY_IMPOSSIBLE: return 14000;
	default:                    return  9000;  // Suicide, Lord of Game and above
	}
}

// The run's starting front gun: endless launches with the Atomic RailGun, not the campaign's Pulse
// Cannon. Applied both when the run is created (newEndlessGame) and at the depth-0 outpost, so it
// holds however the first shop was reached; both points run before the player can buy anything.
#define ENDLESS_START_FRONT_WEAPON 39   // Atomic RailGun

void endlessApplyStartingLoadout(void)
{
	// Both ships launch with the same gun; a co-op run outfits them apart from here on.
	for (uint p = 0; p < (endlessCoop() ? COUNTOF(player) : 1u); ++p)
	{
		player[p].items.weapon[FRONT_WEAPON].id = ENDLESS_START_FRONT_WEAPON;
		player[p].items.weapon[FRONT_WEAPON].power = 1;
		player[p].last_items = player[p].items;  // keep the shop's "already owned" list in sync
	}
}

// Move the starting gun to the first row after inventory sorting at the initial outpost.
void endlessHoistStartWeapon(void)
{
	if (!endlessMode || endlessRunDepth != 0)
		return;

	JE_byte *const row = itemAvail[1];  // front weapons (see itemAvailMap in game_menu.c)
	for (int i = 1; i < itemAvailMax[1]; ++i)
	{
		if (row[i] != ENDLESS_START_FRONT_WEAPON)
			continue;
		memmove(&row[1], &row[0], (size_t)i * sizeof *row);  // shift the rows above it down one
		row[0] = ENDLESS_START_FRONT_WEAPON;
		return;
	}
}

// Base clear reward before modifier bonuses, shared with the perk buyout.
// It scales with depth but is capped.
Sint64 endlessClearBase(void)
{
	Sint64 base = 900 + (Sint64)endlessRunDepth * 220;
	return (base > 60000) ? 60000 : base;
}

// Calculate a payout from base reward, modifier tenths, and the level-profile adjustment.
Sint64 endlessClearBonusForEx(Uint64 mods, int payoutMille)
{
	const Sint64 base = endlessClearBase();
	Sint64 tenths = 0;
	for (unsigned i = 0; i < COUNTOF(endlessModTable); ++i)
		if (mods & endlessModTable[i].bit)
			tenths += endlessModTable[i].reward;
	tenths += endlessSynergyBonus(mods);   // combos worse than the sum of their parts pay extra too
	// Modifiers use tenths of base reward; level profiles use thousandths.
	const Sint64 total = base + base * tenths / 10 + base * payoutMille / 1000;
	const Sint64 floor = base / 4;
	return (total > floor) ? total : floor;  // always a minimum reward
}

Sint64 endlessClearBonusFor(Uint64 mods)
{
	return endlessClearBonusForEx(mods, 0);
}

// Pay the committed level and modifier set using the calculation shown on the course card.
static Sint64 endlessClearBonus(void)
{
	return endlessClearBonusForEx(endlessActiveMods, endlessSortiePayoutMille());
}

// Shop stock.

// Reject blank placeholders and reserved custom-weapon ports.
static bool endlessItemBuyable(JE_byte costType, int id)
{
	const char *name = NULL;
	switch (costType)
	{
	case 2: name = ships[id].name;    break;  // ship
	case 3:                                    // front weapon
	case 4:                                    // rear weapon
		if (customWeaponPortIsCustom((JE_word)id))
			return false;
		name = weaponPort[id].name;
		if (SDL_strncasecmp(name, "Test", 4) == 0)
			return false;
		break;
	case 5: name = shields[id].name;  break;  // shield
	case 6: name = powerSys[id].name; break;  // generator
	case 7:                                    // left sidekick
	case 8:                                    // right sidekick
		if (customSidekickSlotIsCustom(id))
			return false;
		name = options[id].name;
		if (strncmp(name, "None", 4) == 0)
			return false;
		break;
	}
	// Exclude data-section divider placeholders.
	if (name != NULL && SDL_strncasecmp(name, "Miscellaneous", 13) == 0)
		return false;
	return name != NULL && name[0] != '\0';
}

// Check a paired-menu exclusion list.
static bool endlessIdExcluded(const JE_byte *exclude, int excludeCount, int id)
{
	for (int k = 0; k < excludeCount; ++k)
		if (exclude[k] == id)
			return true;
	return false;
}

// Fill an itemAvail row with a uniform shuffled selection, respecting paired-menu exclusions.
static void endlessFillCategory(int availIdx, JE_byte costType, int idMax, bool allowNone, int curItem, const JE_byte *exclude, int excludeCount)
{
	const int want = 5;  // items shown per category: None + 4 where None is allowed, else 5
	int n = 0;

	if (allowNone)
		itemAvail[availIdx][n++] = 0;

	// Include equipped gear so JE_itemScreen does not append a sixth row.
	if (curItem > 0)
	{
		bool present = false;
		for (int k = 0; k < n; ++k)
			if (itemAvail[availIdx][k] == curItem)
				present = true;
		if (!present)
			itemAvail[availIdx][n++] = curItem;
	}

	// Build the remaining buyable pool. Callers keep idMax within PORT_NUM.
	JE_byte pool[PORT_NUM + 1];
	int poolN = 0;
	for (int id = 1; id <= idMax && poolN < (int)COUNTOF(pool); ++id)
	{
		if (!endlessItemBuyable(costType, id))
			continue;
		if (endlessIdExcluded(exclude, excludeCount, id))
			continue;
		bool dup = false;
		for (int k = 0; k < n; ++k)
			if (itemAvail[availIdx][k] == id)
				dup = true;
		if (!dup)
			pool[poolN++] = (JE_byte)id;
	}

	// Shuffle uniformly on the structural stream and take the requested count.
	for (int i = poolN - 1; i > 0; --i)
	{
		const int j = (int)(endlessRandFor(me()) % (Uint32)(i + 1));
		const JE_byte t = pool[i]; pool[i] = pool[j]; pool[j] = t;
	}
	for (int i = 0; i < poolN && n < want; ++i)
		itemAvail[availIdx][n++] = pool[i];

	itemAvailMax[availIdx] = n;
}

// Randomize the whole shop inventory (uniformly random per visit). Called on entering the
// outpost and on each reroll.
static void endlessFillShop(void)
{
	memset(itemAvail, 0, sizeof(itemAvail));
	memset(itemAvailMax, 0, sizeof(itemAvailMax));

	// Cursed Bounty leaves every category empty.
	if (endlessActiveMods & ENDLESS_MOD_CURSED)
		return;

	// Seed each category with the player's live equipped item, never the stale
	// last_items, so a reroll keeps whatever is on the ship.
	const PlayerItems *it = &shopper()->items;

	// Front and rear share one id pool: fill front first (holding back the equipped rear), then
	// rear excluding the front row, so no weapon id lands in both menus at once.
	const JE_byte rearEquip = it->weapon[REAR_WEAPON].id;

	// itemAvail rows per category (see itemAvailMap in game_menu.c): 0 ships, 1 front,
	// 2 rear, 3 generator, 5 left sidekick, 6 right sidekick, 8 shield.
	endlessFillCategory(0, 2, SHIP_DRAGONWING, false, it->ship,                   NULL, 0);  // ships
	endlessFillCategory(1, 3, SHOP_REAL_WEAPON_PORTS, false, it->weapon[FRONT_WEAPON].id, &rearEquip, rearEquip > 0 ? 1 : 0);  // front weapons (skip equipped rear)
	endlessFillCategory(2, 4, SHOP_REAL_WEAPON_PORTS, true,  it->weapon[REAR_WEAPON].id,  itemAvail[1], itemAvailMax[1]);  // rear (+None), no dupes vs front
	endlessFillCategory(3, 6, POWER_NUM,  false, it->generator,                   NULL, 0);  // generators
	endlessFillCategory(5, 7, OPTION_NUM, true,  it->sidekick[LEFT_SIDEKICK],     NULL, 0);  // left sidekick (+None)
	endlessFillCategory(6, 8, OPTION_NUM, true,  it->sidekick[RIGHT_SIDEKICK],    NULL, 0);  // right sidekick (+None)
	endlessFillCategory(8, 5, SHIELD_NUM, false, it->shield,                      NULL, 0);  // shields
}

/* Redeal this seat's shop rows from its own stream, seeded with its own equipped gear: the
 * fallback when an adopted record carries no partner half (see endlessRunAdopt). */
void endlessShopRedrawStock(void)
{
	endlessFillShop();
}

// Outpost economy.
// Endless replaces Data Cubes and Ship Specs with these actions in the existing item screen.

// Price tuning.
// Visit prices scale with depth, then apply the corresponding rebuy escalation.
#define ENDLESS_PRICE_REROLL_BASE        6000
#define ENDLESS_PRICE_REROLL_PER_ZONE    1000
#define ENDLESS_PRICE_HULL_BASE         15000
#define ENDLESS_PRICE_HULL_PER_ZONE      2000
#define ENDLESS_PRICE_BOMB_BASE          2500
#define ENDLESS_PRICE_BOMB_PER_ZONE       400
#define ENDLESS_PRICE_EXTRAPERK_BASE    70000  // before the surcharge on perks already bought
#define ENDLESS_PRICE_EXTRAPERK_PER_ZONE 2500  // extra perks are a luxury on top of the free picks
#define ENDLESS_PRICE_CLEANSE_BASE      25000
#define ENDLESS_PRICE_CLEANSE_PER_ZONE   2500

// Repeated purchases in one visit use cost = cost * NUM/DEN + ADD.
// Steeper numbers mean "one per visit, really"; gentler ones allow a restock.
#define ENDLESS_REBUY_REROLL_NUM      8      // reroll: x1.6 and a flat bump on top
#define ENDLESS_REBUY_REROLL_DEN      5
#define ENDLESS_REBUY_REROLL_ADD   3000
#define ENDLESS_REBUY_HULL_NUM        3      // hull tier: x1.5 and a flat bump
#define ENDLESS_REBUY_HULL_DEN        2
#define ENDLESS_REBUY_HULL_ADD     5000
#define ENDLESS_REBUY_BOMB_NUM        3      // bombs: x1.5, so a full restock costs more each time
#define ENDLESS_REBUY_BOMB_DEN        2
#define ENDLESS_REBUY_CLEANSE_NUM     2      // sabotage charge: doubles
#define ENDLESS_REBUY_CLEANSE_DEN     1

// Apply one of the escalations above.
static Sint64 endlessRebuy(Sint64 cost, Sint64 num, Sint64 den, Sint64 add)
{
	return cost * num / den + add;
}

void endlessResetShopPrices(void)
{
	endlessRerollCost[me()] = ENDLESS_PRICE_REROLL_BASE + (Sint64)endlessRunDepth * ENDLESS_PRICE_REROLL_PER_ZONE;
	endlessHullCost[me()]   = ENDLESS_PRICE_HULL_BASE + endlessRunDepth * ENDLESS_PRICE_HULL_PER_ZONE;
	endlessBombCost[me()]   = ENDLESS_PRICE_BOMB_BASE + (Sint64)endlessRunDepth * ENDLESS_PRICE_BOMB_PER_ZONE;
	endlessCleanseCost[me()] = ENDLESS_PRICE_CLEANSE_BASE + (Sint64)endlessRunDepth * ENDLESS_PRICE_CLEANSE_PER_ZONE;
	endlessCleanseChargeCount[me()] = 0;  // fresh visit: no pending sabotage strips carried in
	endlessExtraPerksVisit[me()] = 0;     // ...and the perk counter is per visit; the run total is not
	endlessGambleMsg[me()][0] = '\0';
	endlessPurchasedMods[me()] = 0;   // fresh visit: no pending buff
	endlessBuffKind[me()] = 0;        // a kill-fire buff (Turbodrive or Overdrive) can be bought again
	endlessLastSpecialName[me()][0] = '\0';       // no special bought yet this visit
	endlessShopEntryCash[me()] = shopperCash();  // freeze the cash-fraction buy prices for this visit
}

Sint64 endlessRerollPrice(void) { return endlessRerollCost[me()]; }
Sint64 endlessHullPrice(void)   { return endlessHullCost[me()]; }
bool endlessHullMaxed(void)   { return endlessArmorBonus[me()] >= endlessHullMax(); }

// Cash-fraction purchases use the entry balance and apply to the next sector.
// Only one of the three kill-fire buffs per visit; Buy Special is a single premium buy.
static Sint64 endlessCashFraction(Sint64 num, Sint64 den)
{
	return endlessShopEntryCash[me()] * num / den;
}

Sint64 endlessTurbodrivePrice(void) { return endlessCashFraction(2, 3); }    // 66 percent
Sint64 endlessOverblastPrice(void)  { return endlessCashFraction(3, 4); }    // 75 percent
Sint64 endlessOverdrivePrice(void)  { return endlessCashFraction(19, 20); }  // 95 percent
Sint64 endlessSpecialPrice(void)    { return endlessCashFraction(4, 5); }    // 80 percent
int  endlessBuffKindBought(void)  { return endlessBuffKind[me()]; }

// Kill-fire purchases share a depth-scaled cooldown stored as an absolute unlock depth.
#define ENDLESS_BUFF_COOLDOWN_BASE        2
#define ENDLESS_BUFF_COOLDOWN_RAMP_START 50
#define ENDLESS_BUFF_COOLDOWN_RAMP_STEP  20
static int endlessBuffCooldownLength(void)
{
	int n = ENDLESS_BUFF_COOLDOWN_BASE;
	if (endlessRunDepth > ENDLESS_BUFF_COOLDOWN_RAMP_START)
		n += (endlessRunDepth - ENDLESS_BUFF_COOLDOWN_RAMP_START) / ENDLESS_BUFF_COOLDOWN_RAMP_STEP;
	return n;
}
static void endlessArmBuffCooldown(void)
{
	endlessBuffCooldownUntil[me()] = endlessRunDepth + endlessBuffCooldownLength();
}

bool endlessBuffOnCooldown(void)   { return endlessRunDepth < endlessBuffCooldownUntil[me()]; }
int  endlessBuffCooldownLeft(void) { int d = endlessBuffCooldownUntil[me()] - endlessRunDepth; return (d > 0) ? d : 0; }

// Kill-fire purchases share affordability, visit, effect-exclusivity, and recharge gates.
static bool endlessTryBuyKillFire(Sint64 cost, unsigned bit, int kind)
{
	if (endlessBuffKind[me()] != 0 || endlessBuffOnCooldown() || cost < 1 || shopperCash() < cost)
		return false;
	endlessCashDebit(cost, ENDLESS_SINK_BUFF);
	endlessBuffCharge[me()] = endlessBuffChargeFromPaid(cost);  // larger spend extends the window and damage
	// OR'd into the next sector in endlessSelectCourse. Replacing the whole kill-fire field keeps it
	// to one effect at a time, which also clears any gambled curse.
	endlessPurchasedMods[me()] = (endlessPurchasedMods[me()] & ~ENDLESS_MOD_KILLFIRE_ANY) | bit;
	endlessBuffKind[me()] = kind;
	endlessArmBuffCooldown();  // lock all three kill-fire buys for the scaled recharge
	return true;
}

bool endlessTryBuyTurbodrive(void)  // quickened fire for a window after each kill
{
	return endlessTryBuyKillFire(endlessTurbodrivePrice(), ENDLESS_MOD_TURBODRIVE, ENDLESS_BUFF_KIND_TURBODRIVE);
}

bool endlessTryBuyOverdrive(void)   // Turbodrive + Overblast together (one bit granting both halves)
{
	return endlessTryBuyKillFire(endlessOverdrivePrice(), ENDLESS_MOD_OVERDRIVE, ENDLESS_BUFF_KIND_OVERDRIVE);
}

bool endlessTryBuyOverblast(void)   // Overdrive damage stacks without the fire boost
{
	return endlessTryBuyKillFire(endlessOverblastPrice(), ENDLESS_MOD_OVERBLAST, ENDLESS_BUFF_KIND_OVERBLAST);
}

// The kill-fire buff bits bought this shop visit but not yet applied (endlessSelectCourse ORs
// them into the next sector). Exposed so the debug level-select can fold them in too.
unsigned endlessPendingMods(void) { return endlessPurchasedMods[me()]; }

// What the ship whose effects are being computed paid for its own drive.
int endlessBuffChargePaid(void)
{
	return endlessBuffCharge[endlessFxPlayer()];
}

// Purchased kill-fire effects replace charted effects rather than combining with them.
// No-Elites also supersedes No-Champions.
Uint64 endlessFoldPurchasedMods(Uint64 sectorMods, Uint64 purchased)
{
	if (purchased & ENDLESS_MOD_KILLFIRE_ANY)
		sectorMods &= ~(Uint64)ENDLESS_MOD_KILLFIRE_ANY;
	return endlessCanonicalMods(sectorMods | purchased);
}

bool endlessTryBuySpecial(void)
{
	const Sint64 cost = endlessSpecialPrice();
	if (cost < 1 || shopperCash() < cost)
		return false;
	endlessCashDebit(cost, ENDLESS_SINK_SUPPLIES);
	endlessGrantSpecial(me());  // equips a random valid special (+ HUD message in-level)
	return true;
}

// Buy one superbomb.
Sint64 endlessBombPrice(void) { return endlessBombCost[me()]; }
bool endlessBombFull(void)  { return shopper()->superbombs >= 10; }
bool endlessTryBuyBomb(void)
{
	if (shopper()->superbombs >= 10 || shopperCash() < endlessBombCost[me()])
		return false;
	endlessCashDebit(endlessBombCost[me()], ENDLESS_SINK_SUPPLIES);
	++shopper()->superbombs;
	endlessBombCost[me()] = endlessRebuy(endlessBombCost[me()], ENDLESS_REBUY_BOMB_NUM, ENDLESS_REBUY_BOMB_DEN, 0);
	return true;
}

// A revive survives one lethal hit and restores full hull. Its price doubles after each use.
Sint64 endlessRevivePrice(void)
{
	const int steps = endlessRevivesUsed[me()] > 5 ? 5 : endlessRevivesUsed[me()];
	return (150000 + (Sint64)endlessRunDepth * 10000) * ((Sint64)1 << steps);
}
bool endlessReviveArmed(void) { return endlessReviveHeld[me()]; }
bool endlessTryBuyRevive(void)
{
	const Sint64 cost = endlessRevivePrice();
	if (endlessReviveHeld[me()] || shopperCash() < cost)
		return false;
	endlessCashDebit(cost, ENDLESS_SINK_REVIVE);
	endlessReviveHeld[me()] = true;
	return true;
}
// Consume player p's revive and arm its grace period. The caller clears the screen on success.
// Runs inside the simulation, so it must not read the local machine's economy index.
bool endlessConsumeRevive(uint p)
{
	if (!endlessMode || p >= COUNTOF(player) || !endlessReviveHeld[p])
		return false;
	endlessReviveHeld[p] = false;
	++endlessRevivesUsed[p];
	player[p].armor = player[p].initial_armor;  // full hull restore
	endlessReviveGraceArm();
	return true;
}

/* Extra perk. What it costs answers how many have been bought and nothing else: perks taken from
 * the free picks, and the wealth a run happens to be carrying, both leave the price alone. The
 * run total is personal, so in co-op one player's spending never prices the other's pick.
 * See doc/notes.md, "Extra-perk pricing". */
bool endlessExtraPerkMaxed(void) { return endlessExtraPerksVisit[me()] >= ENDLESS_PERK_VISIT_MAX; }

Sint64 endlessExtraPerkPrice(void)
{
	// Each perk already bought adds STEP, and every step is GROWTH wider than the one before, so
	// the surcharge is quadratic in the count. The clamp keeps a hostile count from overflowing it.
	const Sint64 bought = endlessClamp(endlessExtraPerksBought[me()], 0, ENDLESS_PERK_PAID_MAX);
	const Sint64 surcharge = ENDLESS_PERK_PAID_STEP_PCT * bought
	                       + ENDLESS_PERK_PAID_GROWTH_PCT * bought * (bought - 1) / 2;
	const Sint64 base = ENDLESS_PRICE_EXTRAPERK_BASE
	                  + (Sint64)endlessRunDepth * ENDLESS_PRICE_EXTRAPERK_PER_ZONE;
	Sint64 price = base * (100 + surcharge) / 100;

	// The second pick at one outpost costs a multiple of that. There is no third.
	if (endlessExtraPerksVisit[me()] > 0)
		price = price * ENDLESS_PERK_VISIT_REPEAT_PCT / 100;
	return price;
}

bool endlessTryBuyExtraPerk(void)
{
	if (endlessExtraPerkMaxed())
		return false;
	const Sint64 price = endlessExtraPerkPrice();  // single source of truth: the same value shown in the E-Shop help line
	if (shopperCash() < price)
		return false;
	endlessCashDebit(price, ENDLESS_SINK_PERK);
	++endlessExtraPerksBought[me()];
	++endlessExtraPerksVisit[me()];
	endlessGeneratePerkChoices(ENDLESS_PERK_OFFERS_BOUGHT);  // dispatch opens MENU_PERKS to pick one of four
	return true;
}

// Sabotage charges strip the worst modifier from the next chosen course.
Sint64 endlessCleansePrice(void)   { return endlessCleanseCost[me()]; }

/* Shared Sabotage charges for the next course, capped across both players. Shop,
 * course card, and launch all read this settled total. */
int endlessCleanseCharges(void)
{
	int charges = 0;
	for (uint p = 0; p < endlessEffectPlayers(); ++p)
		charges += endlessCleanseChargeCount[p];
	return (charges > ENDLESS_CLEANSE_MAX_CHARGES) ? ENDLESS_CLEANSE_MAX_CHARGES : charges;
}

bool endlessCleanseMaxed(void)   { return endlessCleanseCharges() >= ENDLESS_CLEANSE_MAX_CHARGES; }
bool endlessTryBuyCleanse(void)
{
	if (endlessCleanseMaxed() || shopperCash() < endlessCleanseCost[me()])
		return false;
	endlessCashDebit(endlessCleanseCost[me()], ENDLESS_SINK_SUPPLIES);
	++endlessCleanseChargeCount[me()];
	endlessCleanseCost[me()] = endlessRebuy(endlessCleanseCost[me()], ENDLESS_REBUY_CLEANSE_NUM, ENDLESS_REBUY_CLEANSE_DEN, 0);
	return true;
}
// Strip the single most-dangerous hostile bit from a modifier set (one per cleanse charge).
Uint64 endlessStripWorstMod(Uint64 mods)
{
	// Include every priced hostile bit. Exclude label-only and gamble-only bits.
	// Ordering is curated by how much a bit hurts to fly with, not strictly by reward tenths.
	static const Uint64 order[] = {  // nastiest first
		ENDLESS_MOD_LEGION, ENDLESS_MOD_APEX, ENDLESS_MOD_DEADGEN, ENDLESS_MOD_RAMPAGE, ENDLESS_MOD_OVERLOAD,
		ENDLESS_MOD_WARP,  // ranked immediately below Overload
		ENDLESS_MOD_ELITEPACK,
		ENDLESS_MOD_BURNOUT, ENDLESS_MOD_MARTYRDOM,  // highest-weight kill-triggered dangers
		ENDLESS_MOD_DEVASTATING, ENDLESS_MOD_SHIELDLESS, ENDLESS_MOD_FORTIFIED, ENDLESS_MOD_FRENZY,
		ENDLESS_MOD_SLUGGISH, ENDLESS_MOD_RETALIATION,
		ENDLESS_MOD_MISFIRE, ENDLESS_MOD_SEEKER, ENDLESS_MOD_OVERHEAT,
		ENDLESS_MOD_BACKFIRE, ENDLESS_MOD_STATIC,
		ENDLESS_MOD_SWIFT, ENDLESS_MOD_OVERCLOCK, ENDLESS_MOD_ENRAGE, ENDLESS_MOD_SLIPSTREAM,
		ENDLESS_MOD_GRAVITY | ENDLESS_MOD_GRAVITY_OMNI, ENDLESS_MOD_TOPSY,  // gravity + its omni flag strip together, so a sabotaged well is fully cleared (not left as an orphaned omni pull)
		ENDLESS_MOD_KAMIKAZE, ENDLESS_MOD_HOMING,  // mild homing tiers are stripped last
	};
	for (unsigned i = 0; i < COUNTOF(order); ++i)
		if (mods & order[i])
			return mods & ~order[i];
	return mods;
}

#define GAMBLE_MSG_LEN sizeof endlessGambleMsg[0]

// Gamble outcomes use a depth-scaled fee and existing run-state levers.
Sint64 endlessGamblePrice(void) { return 25000 + (Sint64)endlessRunDepth * 2000; }  // limits repeated jackpot fishing
const char *endlessGambleResult(void) { return endlessGambleMsg[me()]; }
bool endlessGambleWonPerk(void) { return endlessGamblePerkWon[me()]; }
// Clear the one-dispatch perk flag after opening its menu.
void endlessClearGamblePerk(void) { endlessGamblePerkWon[me()] = false; }
int  endlessShopTaxPercent(void) { return endlessShopTax[me()]; }
// Outcome IDs are stable and aligned with endlessGambleOutcomeNames.
enum {
	EGO_JACKPOT, EGO_WIN, EGO_REVIVE, EGO_PERK, EGO_HULL, EGO_OVERCLOCK, EGO_SPECIAL,
	EGO_ARSENAL, EGO_SECONDWIND, EGO_BLOODMONEY, EGO_OVERBLAST, EGO_OVERCHARGE, EGO_FAVOR, EGO_GOLDEN,
	EGO_DOUBLENOTHING, EGO_REFUND, EGO_NOTHING,
	EGO_LOANSHARK, EGO_NITRO, EGO_OVERHEAT, EGO_GLASSCANNON,
	EGO_MELTDOWN, EGO_STICKY, EGO_RUSTBUCKET, EGO_AMNESIA, EGO_DUD,
	EGO_SWINDLED, EGO_CURSE_JAM, EGO_CURSE_FAIL, EGO_CURSE_MISFIRE, EGO_CURSE_FRENZY,
	EGO_MARKED, EGO_LONGCON,
	EGO_ROBBED, EGO_DISARMED, EGO_PSYCH, EGO_RIGGED, EGO_CLEANED,
	EGO_RAMPAGE,  // about 1 in 5000; rammers in the next sector
	EGO_MEGAJACKPOT,  // about 1 in 5000; flat one-million payout
	EGO_COUNT
};
static const char *const endlessGambleOutcomeNames[EGO_COUNT] = {
	"Jackpot (+5x)", "Win (+2x)", "Revive token", "Free perk pick", "Hull tier +", "Overclock gun +1", "Special weapon",
	"Arsenal (max bombs)", "Second wind (heal)", "Blood money", "Overblast next", "Overcharged next", "Merchant Favor", "Golden Touch",
	"Double or Nothing", "Refund fee", "Nothing (house wins)",
	"Loan Shark (+tax)", "Nitro deal", "Overheat deal", "Glass Cannon",
	"Meltdown (gun -1)", "Sticky Fingers", "Rustbucket", "Amnesia (-perk)", "Dud Arsenal",
	"Swindled (-20%)", "Curse: gun jam", "Curse: gun fail", "Curse: misfire", "Curse: frenzy",
	"Marked (boss+)", "The Long Con",
	"Robbed (-bomb)", "Disarmed (-revive)", "Jackpot Psych", "Rigged next pull", "Cleaned out (-50%)",
	"Kamikaze Rush", "Mega Jackpot (+$1M)",
};

// Apply one outcome without charging or rolling. `cost` scales cash outcomes.
static void endlessApplyGambleOutcome(int id, Sint64 cost)
{
	char *const msg = endlessGambleMsg[me()];

	switch (id)
	{
	case EGO_JACKPOT: { const Sint64 win = cost * 5; endlessCashCredit(win, ENDLESS_CASH_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "JACKPOT!  +$%lld", (long long)win); break; }
	case EGO_MEGAJACKPOT: { const Sint64 win = 1000000; endlessCashCredit(win, ENDLESS_CASH_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "MEGA JACKPOT!  +$%lld", (long long)win); break; }
	case EGO_WIN:     { const Sint64 win = cost * 2; endlessCashCredit(win, ENDLESS_CASH_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Win!  +$%lld", (long long)win); break; }
	case EGO_REVIVE:
		if (!endlessReviveHeld[me()])
		{ endlessReviveHeld[me()] = true; SDL_strlcpy(msg, "Won a REVIVE token!", GAMBLE_MSG_LEN); }
		else
		{ const Sint64 win = cost * 4; endlessCashCredit(win, ENDLESS_CASH_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Revive held --  +$%lld", (long long)win); }
		break;
	case EGO_PERK:
		endlessGeneratePerkChoices(ENDLESS_PERK_OFFERS);  // the E-Shop dispatch opens MENU_PERKS when endlessGambleWonPerk() is set
		if (endlessPerkChoiceCount() > 0)
		{ endlessGamblePerkWon[me()] = true; SDL_strlcpy(msg, "Won a free perk pick!", GAMBLE_MSG_LEN); }
		else
		{ const Sint64 win = cost * 3; endlessCashCredit(win, ENDLESS_CASH_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Perks maxed --  +$%lld", (long long)win); }
		break;
	case EGO_HULL:
		if (endlessArmorBonus[me()] < endlessHullMax())
		{ endlessArmorBonus[me()] += ENDLESS_HULL_STEP; snprintf(msg, GAMBLE_MSG_LEN, "Hull tier!  +%d armor", ENDLESS_HULL_STEP); }
		else
		{ const Sint64 win = cost * 3; endlessCashCredit(win, ENDLESS_CASH_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Hull maxed --  +$%lld", (long long)win); }
		break;
	case EGO_OVERCLOCK:  // +1 permanent front-gun power (the bait that makes Meltdown sting)
		if ((int)shopper()->items.weapon[FRONT_WEAPON].power < 11)
		{ ++shopper()->items.weapon[FRONT_WEAPON].power; SDL_strlcpy(msg, "Overclocked!  +1 gun power", GAMBLE_MSG_LEN); }
		else
		{ const Sint64 win = cost * 3; endlessCashCredit(win, ENDLESS_CASH_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Guns maxed --  +$%lld", (long long)win); }
		break;
	case EGO_SPECIAL: endlessGrantSpecial(me()); snprintf(msg, GAMBLE_MSG_LEN, "Won a special weapon! (%s)", endlessLastSpecialName[me()]); break;
	case EGO_ARSENAL:
	{
		int got = 0;
		while (shopper()->superbombs < 10) { ++shopper()->superbombs; ++got; }
		if (got > 0)
			snprintf(msg, GAMBLE_MSG_LEN, "Arsenal!  +%d bombs", got);
		else
		{ const Sint64 win = cost * 2; endlessCashCredit(win, ENDLESS_CASH_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Bombs full --  +$%lld", (long long)win); }
		break;
	}
	case EGO_SECONDWIND:
		shopper()->armor = shopper()->initial_armor;  // full hull restore (bonuses stack on top, as with Revive)
		SDL_strlcpy(msg, "Second wind! Hull restored.", GAMBLE_MSG_LEN);
		break;
	case EGO_BLOODMONEY:  // floor of a normal Win, plus a fat bonus the more wrecked your hull is
	{
		const int maxA = shopper()->initial_armor > 0 ? shopper()->initial_armor : 1;
		const int miss = maxA - (int)shopper()->armor;
		const Sint64 win = cost * 2 + cost * 3 * (miss > 0 ? miss : 0) / maxA;
		endlessCashCredit(win, ENDLESS_CASH_GAMBLE);
		snprintf(msg, GAMBLE_MSG_LEN, "Blood money!  +$%lld", (long long)win);
		break;
	}
	case EGO_OVERBLAST:
		endlessPurchasedMods[me()] = (endlessPurchasedMods[me()] & ~ENDLESS_MOD_KILLFIRE_ANY) | ENDLESS_MOD_OVERBLAST;
		SDL_strlcpy(msg, "Overblast next sector!", GAMBLE_MSG_LEN);
		break;
	case EGO_OVERCHARGE:
		endlessPurchasedMods[me()] |= ENDLESS_MOD_OVERCHARGE;
		SDL_strlcpy(msg, "Overcharged next sector!", GAMBLE_MSG_LEN);
		break;
	case EGO_FAVOR:
		endlessPurchasedMods[me()] |= ENDLESS_MOD_FAVOR;
		SDL_strlcpy(msg, "Favor: cheap shop next!", GAMBLE_MSG_LEN);
		break;
	case EGO_GOLDEN:
		endlessPurchasedMods[me()] |= ENDLESS_MOD_BOUNTY;
		SDL_strlcpy(msg, "Golden touch: big clear next!", GAMBLE_MSG_LEN);
		break;
	case EGO_DOUBLENOTHING:  // a straight coin-flip on your entire cash pile
		if (endlessRandFor(me()) % 2)
		{
			// Clamp at two billion and book the signed wallet delta.
			const Sint64 pile = shopperCash();
			const Sint64 doubled = (pile > 1000000000LL) ? 2000000000LL : pile * 2;
			if (doubled > pile)
				endlessCashCredit(doubled - pile, ENDLESS_CASH_GAMBLE);
			else
				endlessCashDebit(pile - doubled, ENDLESS_SINK_GAMBLE);
			SDL_strlcpy(msg, "DOUBLED! The pile is yours.", GAMBLE_MSG_LEN);
		}
		else
		{ endlessCashDebit((Sint64)shopperCash(), ENDLESS_SINK_GAMBLE); SDL_strlcpy(msg, "NOTHING. Wiped clean.", GAMBLE_MSG_LEN); }
		break;
	case EGO_REFUND: endlessCashCredit(cost, ENDLESS_CASH_GAMBLE); SDL_strlcpy(msg, "Machine jammed -- fee back.", GAMBLE_MSG_LEN); break;
	case EGO_NOTHING: SDL_strlcpy(msg, "Nothing. The house wins.", GAMBLE_MSG_LEN); break;
	case EGO_LOANSHARK:  // a fortune now, a permanent tax on every price for the rest of the run
	{
		const Sint64 win = cost * 3;  // scales with the live fee
		endlessCashCredit(win, ENDLESS_CASH_GAMBLE);
		endlessShopTax[me()] += 25;  // repeated outcomes compound
		snprintf(msg, GAMBLE_MSG_LEN, "Loan shark: +$%lld, +25%% tax", (long long)win);
		break;
	}
	case EGO_NITRO:  // +damage next sector, but any hit is fatal (see varz.c damage path)
		endlessPurchasedMods[me()] |= (ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_NITRO);
		SDL_strlcpy(msg, "Nitro: +power, one hit kills!", GAMBLE_MSG_LEN);
		break;
	case EGO_OVERHEAT:  // kills quicken your guns, but the hull cooks (see endlessGameplayTick DoT)
		endlessPurchasedMods[me()] = (endlessPurchasedMods[me()] & ~ENDLESS_MOD_KILLFIRE_ANY) | ENDLESS_MOD_TURBODRIVE;
		endlessPurchasedMods[me()] |= ENDLESS_MOD_OVERHEAT;
		SDL_strlcpy(msg, "Overheat: fast guns, hull cooks!", GAMBLE_MSG_LEN);
		break;
	case EGO_GLASSCANNON:  // +2 permanent gun power, but shave permanent max hull
	{
		const int pw = (int)shopper()->items.weapon[FRONT_WEAPON].power;
		shopper()->items.weapon[FRONT_WEAPON].power = (pw + 2 > 11) ? 11 : (pw + 2);
		const int floorBonus = 8 - (int)shopper()->initial_armor;  // keep effective max hull >= ~8
		endlessArmorBonus[me()] -= 2 * ENDLESS_HULL_STEP;
		if (endlessArmorBonus[me()] < floorBonus)
			endlessArmorBonus[me()] = floorBonus;
		if ((int)shopper()->armor + endlessArmorBonus[me()] < 1)  // never leave the hull sitting at 0 in the shop
			shopper()->armor = (JE_byte)(1 - endlessArmorBonus[me()]);
		SDL_strlcpy(msg, "Glass cannon: +2 power, -hull!", GAMBLE_MSG_LEN);
		break;
	}
	case EGO_MELTDOWN:  // -1 permanent gun power
		if ((int)shopper()->items.weapon[FRONT_WEAPON].power > 1)
		{ --shopper()->items.weapon[FRONT_WEAPON].power; SDL_strlcpy(msg, "Meltdown!  -1 gun power.", GAMBLE_MSG_LEN); }
		else
			SDL_strlcpy(msg, "Guns already stripped bare.", GAMBLE_MSG_LEN);
		break;
	case EGO_STICKY:  // steal the equipped special
		if (shopper()->items.special > 0)
		{
			shopper()->items.special = 0;
			shotMultiPos[SHOT_SPECIAL]  = 0; shotRepeat[SHOT_SPECIAL]  = 0;
			shotMultiPos[SHOT_SPECIAL2] = 0; shotRepeat[SHOT_SPECIAL2] = 0;
			SDL_strlcpy(msg, "Sticky fingers: special gone!", GAMBLE_MSG_LEN);
		}
		else
		{ const Sint64 loss = shopperCash() / 10; endlessCashDebit(loss, ENDLESS_SINK_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Frisked!  -$%lld", (long long)loss); }
		break;
	case EGO_RUSTBUCKET:  // knock a shield or generator down a tier
		if (shopper()->items.shield > 1 && (endlessRandFor(me()) % 2))
		{ --shopper()->items.shield; SDL_strlcpy(msg, "Rustbucket: shield sags!", GAMBLE_MSG_LEN); }
		else if (shopper()->items.generator > 1)
		{ --shopper()->items.generator; SDL_strlcpy(msg, "Rustbucket: reactor sags!", GAMBLE_MSG_LEN); }
		else if (shopper()->items.shield > 1)
		{ --shopper()->items.shield; SDL_strlcpy(msg, "Rustbucket: shield sags!", GAMBLE_MSG_LEN); }
		else
		{ const Sint64 loss = shopperCash() / 10; endlessCashDebit(loss, ENDLESS_SINK_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Gear too cheap -- $%lld gone", (long long)loss); }
		break;
	case EGO_AMNESIA:  // erase a random owned perk
	{
		int owned[PERK_COUNT], n = 0;
		for (int i = 0; i < PERK_COUNT; ++i)
			if (endlessPerkTakenBy[me()][i] > 0)   // only your own picks are yours to forget
				owned[n++] = i;
		if (n > 0)
		{
			endlessPerkGrant(me(), owned[endlessRandFor(me()) % (unsigned)n], -1);
			SDL_strlcpy(msg, "Amnesia: a perk erased!", GAMBLE_MSG_LEN);
		}
		else
		{ const Sint64 loss = shopperCash() / 10; endlessCashDebit(loss, ENDLESS_SINK_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Mind blank -- robbed  -$%lld", (long long)loss); }
		break;
	}
	case EGO_DUD:  // fill bombs, then jam them for the next sector
		while (shopper()->superbombs < 10)
			++shopper()->superbombs;
		endlessPurchasedMods[me()] |= ENDLESS_MOD_DUD;
		SDL_strlcpy(msg, "Loaded up... duds next sector!", GAMBLE_MSG_LEN);
		break;
	case EGO_SWINDLED: { const Sint64 loss = shopperCash() / 5; endlessCashDebit(loss, ENDLESS_SINK_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Swindled!  -$%lld", (long long)loss); break; }
	case EGO_CURSE_JAM:
		endlessPurchasedMods[me()] = (endlessPurchasedMods[me()] & ~ENDLESS_MOD_KILLFIRE_ANY) | ENDLESS_MOD_BACKFIRE;
		SDL_strlcpy(msg, "Cursed: guns jam next!", GAMBLE_MSG_LEN);
		break;
	case EGO_CURSE_FAIL:
		endlessPurchasedMods[me()] = (endlessPurchasedMods[me()] & ~ENDLESS_MOD_KILLFIRE_ANY) | ENDLESS_MOD_BURNOUT;
		SDL_strlcpy(msg, "Cursed: guns fail next!", GAMBLE_MSG_LEN);
		break;
	case EGO_CURSE_MISFIRE:
		endlessPurchasedMods[me()] = (endlessPurchasedMods[me()] & ~ENDLESS_MOD_KILLFIRE_ANY) | ENDLESS_MOD_MISFIRE;
		SDL_strlcpy(msg, "Cursed: guns misfire next!", GAMBLE_MSG_LEN);
		break;
	case EGO_CURSE_FRENZY:
		endlessPurchasedMods[me()] |= ENDLESS_MOD_FRENZY;
		SDL_strlcpy(msg, "Cursed: fast fire next!", GAMBLE_MSG_LEN);
		break;
	case EGO_MARKED:
		endlessPurchasedMods[me()] |= ENDLESS_MOD_MARKED;
		SDL_strlcpy(msg, "Marked: the next boss bulks up!", GAMBLE_MSG_LEN);
		break;
	case EGO_LONGCON:
		endlessLongCon[me()] = 3;  // an APEX ambush comes due three sectors from now (endlessSelectCourse)
		SDL_strlcpy(msg, "The long con... something coming.", GAMBLE_MSG_LEN);
		break;
	case EGO_ROBBED:
		if (shopper()->superbombs > 0)
			--shopper()->superbombs;
		SDL_strlcpy(msg, "Robbed: lost a bomb.", GAMBLE_MSG_LEN);
		break;
	case EGO_DISARMED:
		if (endlessReviveHeld[me()])
		{ endlessReviveHeld[me()] = false; SDL_strlcpy(msg, "Disarmed! Revive gone.", GAMBLE_MSG_LEN); }
		else
		{ const Sint64 loss = shopperCash() / 5; endlessCashDebit(loss, ENDLESS_SINK_GAMBLE); snprintf(msg, GAMBLE_MSG_LEN, "Pickpocketed!  -$%lld", (long long)loss); }
		break;
	case EGO_PSYCH:  // the cruel fake-out: flashes a jackpot, then snatches a little extra
	{
		const Sint64 loss = (shopperCash() < cost) ? shopperCash() : cost;
		endlessCashDebit(loss, ENDLESS_SINK_GAMBLE);
		snprintf(msg, GAMBLE_MSG_LEN, "'JACKPOT!' ...psych. -$%lld", (long long)loss);
		break;
	}
	case EGO_RIGGED:
		endlessGambleRigged[me()] = true;  // the next pull rolls twice and keeps the worse
		SDL_strlcpy(msg, "The house is watching you...", GAMBLE_MSG_LEN);
		break;
	case EGO_RAMPAGE:  // the original brutal Kamikaze as a jackpot-of-doom: rammers next sector
		endlessPurchasedMods[me()] |= ENDLESS_MOD_RAMPAGE;
		SDL_strlcpy(msg, "KAMIKAZE RUSH! Rammers next!", GAMBLE_MSG_LEN);
		break;
	default:  // EGO_CLEANED removes half the current cash
	{
		const Sint64 loss = shopperCash() / 2;
		endlessCashDebit(loss, ENDLESS_SINK_GAMBLE);
		snprintf(msg, GAMBLE_MSG_LEN, "Cleaned out!  -$%lld", (long long)loss);
		break;
	}
	}
}

// Map a percentile roll to an outcome while preserving sub-roll consumption.
static int endlessRollToOutcome(int roll)
{
	if (roll < 5)  return EGO_JACKPOT;
	if (roll < 12) { switch (endlessRandFor(me()) % 3) { case 0: return EGO_REVIVE; case 1: return EGO_HULL; default: return EGO_OVERCLOCK; } }  // EGO_PERK pulled out -> now a 1/2500 ultra-rare draw (endlessTryGamble)
	if (roll < 24) return EGO_WIN;
	if (roll < 31) return EGO_SPECIAL;
	if (roll < 37) { switch (endlessRandFor(me()) % 3) { case 0: return EGO_ARSENAL; case 1: return EGO_SECONDWIND; default: return EGO_BLOODMONEY; } }
	if (roll < 43) return (endlessRandFor(me()) % 2) ? EGO_OVERBLAST : EGO_OVERCHARGE;
	if (roll < 48) return EGO_FAVOR;
	if (roll < 52) return EGO_GOLDEN;
	if (roll < 56) return EGO_DOUBLENOTHING;
	if (roll < 59) return EGO_REFUND;
	if (roll < 62) return EGO_NOTHING;
	if (roll < 66) return EGO_LOANSHARK;
	if (roll < 71) { switch (endlessRandFor(me()) % 3) { case 0: return EGO_NITRO; case 1: return EGO_OVERHEAT; default: return EGO_GLASSCANNON; } }
	if (roll < 78) { switch (endlessRandFor(me()) % 5) { case 0: return EGO_MELTDOWN; case 1: return EGO_STICKY; case 2: return EGO_RUSTBUCKET; case 3: return EGO_AMNESIA; default: return EGO_DUD; } }
	if (roll < 84) return EGO_SWINDLED;
	if (roll < 90) { switch (endlessRandFor(me()) % 5) { case 0: return EGO_CURSE_JAM; case 1: return EGO_CURSE_FAIL; case 2: return EGO_CURSE_MISFIRE; default: return EGO_CURSE_FRENZY; } }
	if (roll < 94) return (endlessRandFor(me()) % 2) ? EGO_MARKED : EGO_LONGCON;
	if (roll < 97) { switch (endlessRandFor(me()) % 4) { case 0: return EGO_ROBBED; case 1: return EGO_DISARMED; case 2: return EGO_PSYCH; default: return EGO_RIGGED; } }
	return EGO_CLEANED;
}

bool endlessTryGamble(void)
{
	endlessGamblePerkWon[me()] = false;  // cleared each pull; set only by the free-perk outcome

	const Sint64 cost = endlessGamblePrice();
	if (shopperCash() < cost)
		return false;
	endlessCashDebit(cost, ENDLESS_SINK_GAMBLE);

	// A shared 1-in-5000 draw selects Mega Jackpot, Rampage, or either free-perk slot.
	const Uint32 ultraRare = endlessRandFor(me()) % 5000;
	if (ultraRare == 0)
	{
		endlessApplyGambleOutcome(EGO_MEGAJACKPOT, cost);
		return true;
	}
	if (ultraRare == 1)
	{
		endlessApplyGambleOutcome(EGO_RAMPAGE, cost);
		return true;
	}
	if (ultraRare == 2 || ultraRare == 3)  // 2/5000 = 1/2500: the free perk pick
	{
		endlessApplyGambleOutcome(EGO_PERK, cost);
		return true;
	}

	int roll = (int)(endlessRandFor(me()) % 100);
	if (endlessGambleRigged[me()])  // roll twice and keep the higher outcome ID
	{
		const int second = (int)(endlessRandFor(me()) % 100);
		if (second > roll)
			roll = second;
		endlessGambleRigged[me()] = false;
	}

	endlessApplyGambleOutcome(endlessRollToOutcome(roll), cost);
	return true;
}

// Debug hooks for the E-Shop's Gamble Outcomes page.
int endlessGambleOutcomeCount(void) { return EGO_COUNT; }
const char *endlessGambleOutcomeName(int id) { return (id >= 0 && id < EGO_COUNT) ? endlessGambleOutcomeNames[id] : ""; }
void endlessForceGambleOutcome(int id)
{
	if (!endlessMode || id < 0 || id >= EGO_COUNT)
		return;
	endlessGamblePerkWon[me()] = false;
	endlessApplyGambleOutcome(id, endlessGamblePrice());  // no fee charged: this is a test trigger
	if (endlessGamblePerkWon[me()])      // choices are already generated
		endlessPerkPending = true; // open now or at the next shop gate
	endlessGamblePerkWon[me()] = false;  // the debug screen has no E-Shop dispatch to consume the inline-perk flag
}

// Reroll the shop stock (rarity-by-depth) for the current price. Returns true if bought.
bool endlessTryReroll(void)
{
	if (shopperCash() < endlessRerollCost[me()])
		return false;
	endlessCashDebit(endlessRerollCost[me()], ENDLESS_SINK_REROLL);
	endlessRerollCost[me()] = endlessRebuy(endlessRerollCost[me()], ENDLESS_REBUY_REROLL_NUM, ENDLESS_REBUY_REROLL_DEN, ENDLESS_REBUY_REROLL_ADD);
	endlessFillShop();
	return true;
}

// Buy a run-persistent +armor hull upgrade for the current price. Returns true if bought.
bool endlessTryReinforce(void)
{
	if (endlessArmorBonus[me()] >= endlessHullMax() || shopperCash() < endlessHullCost[me()])
		return false;
	endlessCashDebit(endlessHullCost[me()], ENDLESS_SINK_HULL);
	endlessArmorBonus[me()] += ENDLESS_HULL_STEP;
	endlessHullCost[me()] = endlessRebuy(endlessHullCost[me()], ENDLESS_REBUY_HULL_NUM,
	                                     ENDLESS_REBUY_HULL_DEN, ENDLESS_REBUY_HULL_ADD);
	return true;
}

// One player's bank interest for this zone: 10% base, raised by the Financier perk, depth-capped.
static Sint64 endlessInterestOn(Sint64 bank)
{
	const int rate = endlessPerkInterestPercent();
	Sint64 interest = bank * rate / 100;
	Sint64 icap = (3000 + (Sint64)endlessRunDepth * 80) * rate / ENDLESS_INTEREST_BASE_PCT;
	if (interest > icap)
		interest = icap;
	return interest;
}

/* Pay every participating ship on every machine so online wallets stay mirrored. Return and book
 * only the local player's payout; personal perks are evaluated for each payee. */
void endlessApplyLevelPayout(Sint64 *interestOut, Sint64 *bonusOut)
{
	Sint64 interest = 0, bonus = 0;
	if (endlessMode && endlessRunDepth > 0)
	{
		const uint fxSaved = endlessFxPlayer();
		const uint mine = endlessEconomyIndex();
		const uint count = (isNetworkGame && coop_mode_active()) ? 2 : 1;
		for (uint n = 0; n < count; ++n)
		{
			const uint p = (count == 2) ? n : mine;
			endlessSetFxPlayer(p);
			const Sint64 pay = endlessInterestOn(player[p].cash);
			const Sint64 clear = endlessClearBonus() * endlessPerkCashPercent() / 100;
			if (p == mine)
			{
				interest = pay;
				bonus = clear;
				endlessCashCredit(interest, ENDLESS_CASH_INTEREST);
				endlessCashCredit(bonus, ENDLESS_CASH_CLEAR);
			}
			else
			{
				player_add_cash(&player[p], pay + clear);
			}
		}
		endlessSetFxPlayer(fxSaved);
	}
	if (interestOut)
		*interestOut = interest;
	if (bonusOut)
		*bonusOut = bonus;
}

void endlessBetweenLevels(void)
{
	// Reaching the outpost ends whatever zone came before it, cleared or bailed out of, and closes
	// it so the weapon editor and shop previews reachable from here cannot count as combat use.
	endlessCustomWeaponZoneEnd();

	// A partner who went down mid-zone is back on their feet here: full hull, no shield.
	endlessReviveDownedAtOutpost();

	// Pin the planet map before the first shop and after random level jumps.
	mapOrigin = 1;
	mapPNum = 1;
	mapPlanet[0] = 1;
	mapSection[0] = 1;

	// Endless has no data cubes. Clear any left over from a prior campaign game so they don't
	// linger as icons in the buy/sell menu; the first endless shop opens before any level (and
	// thus before endlessRegenerateLevel, which also zeroes these) has loaded.
	cubeMax = 0;
	lastCubeMax = 0;

	mouseSetRelative(false);  // menus use absolute mouse; start_level_first re-enables relative for gameplay

	// Show credits exactly at zone 100 and set the flag before playback so reload cannot repeat them.
	if (endlessRunDepth == ENDLESS_CREDITS_ZONE && !endlessCreditsShown)
	{
		endlessCreditsShown = true;
		VGAScreen = VGAScreenSeg;  // the level loop may have left it on VGAScreen2 (as JE_itemScreen does)
		fade_black(10);            // the level-complete screen is still up; JE_playCredits fades in from black
		JE_playCredits();
	}

	// Label anything saved from this outpost as the zone the player resumes into, and make sure
	// the slot reads as occupied. (The first outpost runs before any level has set these.)
	snprintf(levelName, sizeof(levelName), "ZONE %d", endlessRunDepth + 1);
	strcpy(lastLevelName, levelName);
	if (saveLevel < 1)
		saveLevel = FIRST_LEVEL;

	// Reapply starting gear only at a fresh depth-0 outpost.
	if (endlessRunDepth == 0 && !endlessResumeVisit && !endlessLockedSortie && !endlessSortieValid())
		endlessApplyStartingLoadout();

	// The level-clear payout (bank interest + clear bonus) is applied earlier, on the
	// level-end screen (endlessApplyLevelPayout, called from JE_endLevelAni), so the shop
	// opens with the reward already banked.

	// Generate a fresh visit only when no saved or locked outpost snapshot was restored.
	if (endlessResumeVisit || endlessLockedSortie)
	{
		endlessResumeVisit = false;
	}
	else
	{
		// A new visit deals; whatever partner half a save stashed belonged to the old one.
		endlessPartnerOutpostClear();

		// Seed structural generation by depth. Player-timed draws cannot shift later zone layouts,
		// and each player's own stream is forked from the same point so a shop reroll or a gamble
		// on one machine never moves what the other is dealt.
		endlessReseedPlayers((Uint64)endlessRunDepth * 2);

		endlessChartVisit();   // seeds the structural phase, then deals this visit's slate
		endlessResetShopPrices();
		endlessFillShop();

		// Queue one unresolved scheduled or milestone perk pick for this depth.
		if (endlessPerkDueAtDepth(endlessRunDepth) && endlessPerkDepthDone != endlessRunDepth)
		{
			endlessGeneratePerkChoices(endlessPerkOffersAtDepth(endlessRunDepth));
			endlessPerkPending = true;
		}
		// Breakthrough debt waits when this visit already has a scheduled pick.
		if (!endlessPerkPending && endlessBreakthroughOwed > 0)
		{
			--endlessBreakthroughOwed;
			endlessGeneratePerkChoices(ENDLESS_PERK_OFFERS);  // the boon's own pick, not the milestone's, so it stays three wide
			endlessPerkPending = true;
		}
	}

	// Auto-checkpoint at the completed outpost setup. Hardcore does not save.
	if (!endlessHardcore())
	{
		const Uint32 saveStart = SDL_GetTicks();
		const JE_byte autoSlot = twoPlayerMode ? 22 : 11;
		JE_saveGame(autoSlot, "LAST LEVEL    ");   // captures the run's half of the slot as well

#ifdef WITH_NETWORK
		// The checkpoint just written is what the disconnect dialog offers to keep (network.c).
		// Without this an online Endless session never armed the offer at all.
		if (isNetworkGame)
		{
			network_session_saveable = true;

			// This write sits between the resume handshake and the shop's first frame, where a
			// stall reads as a network hang. Logging a slow one attributes it to the install's
			// disk (a cloud-synced folder, say) rather than the link.
			const Uint32 saveMs = SDL_GetTicks() - saveStart;
			if (saveMs > 2000)
			{
				char detail[96];
				snprintf(detail, sizeof(detail),
				         "the outpost checkpoint took %lu ms to write.", (unsigned long)saveMs);
				crashlog_netlog_line("OUTPOST AUTOSAVE SLOW", detail);
			}
		}
#else
		(void)saveStart;
#endif
	}

	// Preserve the previous sector's modifiers so a later bail reopens this outpost unchanged.
	if (!endlessLockedSortie)
		endlessSortieOutpostMods = endlessActiveMods;
	// The episode needs no such guard: every path here leaves episodeNum on the outpost's own,
	// a locked reopen included, since the bail restores it first.
	endlessSortieOutpostEp = episodeNum;

	// Set the outpost track every visit; milestone charts use the warning track.
	songBuy = endlessMilestoneKind() ? ENDLESS_MILESTONE_SHOP_SONG : DEFAULT_SONG_BUY;
	// The run's first approach to the credits zone trades the warning track for a send-off; once
	// the credits have rolled, later century outposts (200, 300, ...) warn like any milestone.
	if (endlessRunDepth + 1 == ENDLESS_CREDITS_ZONE && !endlessCreditsShown)
		songBuy = ENDLESS_FINALE_SHOP_SONG;
	JE_itemScreen();
}
