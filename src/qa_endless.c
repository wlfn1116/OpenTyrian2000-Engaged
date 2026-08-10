/* Online Endless co-op: outpost, E-shop, perks, drives, downed/revive, restart.
 *
 * Every case drives the real engine entry points under a two-ship online session, because the
 * combinations that break are the ones a solo run never reaches: two wallets, two shelves, two
 * drives and two death states crossed against the session settings the host picked. */
#include "qa.h"

#include "config.h"
#include "endless.h"
#include "endless_internal.h"
#include "mainint.h"
#include "network.h"
#include "player.h"
#include "tyrian2.h"
#include "varz.h"

#include <stdio.h>
#include <string.h>

/* ---- session harness ---------------------------------------------------------------- */

/* Everything a case here is allowed to disturb. Saved once around the whole suite so a failure
 * mid-matrix cannot leak a half-built session into the tests that run after it. */
typedef struct
{
	JE_boolean endless, coopEndless, coopCampaign, twoPlayer, onePlayer;
	bool netGame, host, comboShared, hostCharts, baseLevelSame;
	uint playerNum;
	EndlessRunMode runMode;
	EndlessCourseChooser chooser;
	int depth, kills;
	Uint64 activeMods;
	Player ships[2];
	Uint64 playerMods[2];
	unsigned purchased[2];
	JE_byte perkOwned[PERK_COUNT];
	JE_byte perkTaken[2][PERK_COUNT];
	int buffKind[2], buffCharge[2], buffCooldown[2], turbo[2], combo[2], stacks[2];
	int armorBonus[2], revivesUsed[2], cleanse[2], shopTax[2], longCon[2];
	bool downed[2], reviveHeld[2], rigged[2];
	long rerollCost[2], entryCash[2];
	int hullCost[2];
	Uint64 rngState[2];
}
QaEndlessEnv;

static void qa_env_save(QaEndlessEnv *e)
{
	e->endless = endlessMode;         e->coopEndless = coopEndlessMode;
	e->coopCampaign = coopCampaignMode; e->twoPlayer = twoPlayerMode;
	e->onePlayer = onePlayerAction;
	e->netGame = isNetworkGame;       e->host = network_is_host;
	e->playerNum = thisPlayerNum;
	e->comboShared = endlessCoopComboShared;
	e->hostCharts = endlessCoopHostCharts;
	e->runMode = endlessRunMode;      e->chooser = endlessCourseChooser;
	e->baseLevelSame = endlessRunBaseLevelSame;
	e->depth = endlessRunDepth;       e->kills = endlessRunKills;
	e->activeMods = endlessActiveMods;
	memcpy(e->ships, player, sizeof(e->ships));
	memcpy(e->playerMods, endlessPlayerMods, sizeof(e->playerMods));
	memcpy(e->purchased, endlessPurchasedMods, sizeof(e->purchased));
	memcpy(e->perkOwned, endlessPerkOwned, sizeof(e->perkOwned));
	memcpy(e->perkTaken, endlessPerkTakenBy, sizeof(e->perkTaken));
	memcpy(e->buffKind, endlessBuffKind, sizeof(e->buffKind));
	memcpy(e->buffCharge, endlessBuffCharge, sizeof(e->buffCharge));
	memcpy(e->buffCooldown, endlessBuffCooldownUntil, sizeof(e->buffCooldown));
	memcpy(e->turbo, endlessTurbodriveTimer, sizeof(e->turbo));
	memcpy(e->combo, endlessComboKills, sizeof(e->combo));
	memcpy(e->stacks, endlessOverdriveStacks, sizeof(e->stacks));
	memcpy(e->armorBonus, endlessArmorBonus, sizeof(e->armorBonus));
	memcpy(e->revivesUsed, endlessRevivesUsed, sizeof(e->revivesUsed));
	memcpy(e->cleanse, endlessCleanseChargeCount, sizeof(e->cleanse));
	memcpy(e->shopTax, endlessShopTax, sizeof(e->shopTax));
	memcpy(e->longCon, endlessLongCon, sizeof(e->longCon));
	memcpy(e->downed, endlessPlayerDowned, sizeof(e->downed));
	memcpy(e->reviveHeld, endlessReviveHeld, sizeof(e->reviveHeld));
	memcpy(e->rigged, endlessGambleRigged, sizeof(e->rigged));
	memcpy(e->rerollCost, endlessRerollCost, sizeof(e->rerollCost));
	memcpy(e->entryCash, endlessShopEntryCash, sizeof(e->entryCash));
	memcpy(e->hullCost, endlessHullCost, sizeof(e->hullCost));
	memcpy(e->rngState, endlessPlayerRngState, sizeof(e->rngState));
}

static void qa_env_restore(const QaEndlessEnv *e)
{
	memcpy(endlessPlayerRngState, e->rngState, sizeof(e->rngState));
	memcpy(endlessHullCost, e->hullCost, sizeof(e->hullCost));
	memcpy(endlessShopEntryCash, e->entryCash, sizeof(e->entryCash));
	memcpy(endlessRerollCost, e->rerollCost, sizeof(e->rerollCost));
	memcpy(endlessGambleRigged, e->rigged, sizeof(e->rigged));
	memcpy(endlessReviveHeld, e->reviveHeld, sizeof(e->reviveHeld));
	memcpy(endlessPlayerDowned, e->downed, sizeof(e->downed));
	memcpy(endlessLongCon, e->longCon, sizeof(e->longCon));
	memcpy(endlessShopTax, e->shopTax, sizeof(e->shopTax));
	memcpy(endlessCleanseChargeCount, e->cleanse, sizeof(e->cleanse));
	memcpy(endlessRevivesUsed, e->revivesUsed, sizeof(e->revivesUsed));
	memcpy(endlessArmorBonus, e->armorBonus, sizeof(e->armorBonus));
	memcpy(endlessOverdriveStacks, e->stacks, sizeof(e->stacks));
	memcpy(endlessComboKills, e->combo, sizeof(e->combo));
	memcpy(endlessTurbodriveTimer, e->turbo, sizeof(e->turbo));
	memcpy(endlessBuffCooldownUntil, e->buffCooldown, sizeof(e->buffCooldown));
	memcpy(endlessBuffCharge, e->buffCharge, sizeof(e->buffCharge));
	memcpy(endlessBuffKind, e->buffKind, sizeof(e->buffKind));
	memcpy(endlessPerkTakenBy, e->perkTaken, sizeof(e->perkTaken));
	memcpy(endlessPerkOwned, e->perkOwned, sizeof(e->perkOwned));
	memcpy(endlessPurchasedMods, e->purchased, sizeof(e->purchased));
	memcpy(endlessPlayerMods, e->playerMods, sizeof(e->playerMods));
	memcpy(player, e->ships, sizeof(e->ships));
	endlessActiveMods = e->activeMods;
	endlessRunKills = e->kills;         endlessRunDepth = e->depth;
	endlessCourseChooser = e->chooser;  endlessRunMode = e->runMode;
	endlessRunBaseLevelSame = e->baseLevelSame;
	endlessCoopHostCharts = e->hostCharts;
	endlessCoopComboShared = e->comboShared;
	thisPlayerNum = e->playerNum;
	network_is_host = e->host;          isNetworkGame = e->netGame;
	onePlayerAction = e->onePlayer;     twoPlayerMode = e->twoPlayer;
	coopCampaignMode = e->coopCampaign; coopEndlessMode = e->coopEndless;
	endlessMode = e->endless;
	coop_set_session_shared_credit(true);   // player.c defaults
	coop_set_session_double_earnings(false);
	endlessSetFxPlayer(0);
	endlessCashResync();
}

/* An online Endless session seen from one machine. `slot` is which ship is sitting at this
 * keyboard, which is what decides whose wallet the outpost spends and whose shelves it stocks. */
static void qa_session(uint slot)
{
	endlessMode = true;
	coopEndlessMode = true;
	coopCampaignMode = false;
	twoPlayerMode = true;      // an Endless lobby starts two ships, the same as the real one
	onePlayerAction = false;
	isNetworkGame = true;
	thisPlayerNum = slot + 1;
	endlessSetFxPlayer(0);
}

// Both ships back to a known, solvent, undamaged state with no drives, perks or latches held.
static void qa_clear_ships(void)
{
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].cash = 0;
		player[p].is_alive = true;
		player[p].exploding_ticks = 0;
		player[p].armor = 20;
		player[p].initial_armor = 20;
		player[p].shield = 10;
		player[p].superbombs = 0;
		endlessPlayerDowned[p] = false;
		endlessReviveHeld[p] = false;
		endlessRevivesUsed[p] = 0;
		endlessPurchasedMods[p] = 0;
		endlessPlayerMods[p] = 0;
		endlessBuffKind[p] = ENDLESS_BUFF_KIND_NONE;
		endlessBuffCharge[p] = 0;
		endlessBuffCooldownUntil[p] = 0;
		endlessTurbodriveTimer[p] = 0;
		endlessComboKills[p] = 0;
		endlessOverdriveStacks[p] = 0;
		endlessArmorBonus[p] = 0;
		endlessShopTax[p] = 0;
		endlessLongCon[p] = 0;
		endlessGambleRigged[p] = false;
		endlessCleanseChargeCount[p] = 0;
	}
	endlessActiveMods = 0;
	endlessRunKills = 0;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessCashResync();
}

// Zero the run ledger so one case's income cannot be read as the next case's.
static void qa_clear_ledger(void)
{
	endlessRunCashEarned = 0;
	endlessRunCashSpent = 0;
	memset(endlessCashBySource, 0, sizeof(endlessCashBySource));
	memset(endlessCashBySink, 0, sizeof(endlessCashBySink));
	endlessCashResync();
}

static const char *qa_drive_name(unsigned bit)
{
	if (bit == (unsigned)ENDLESS_MOD_TURBODRIVE) return "Turbodrive";
	if (bit == (unsigned)ENDLESS_MOD_OVERDRIVE)  return "Overdrive";
	if (bit == (unsigned)ENDLESS_MOD_OVERBLAST)  return "Overblast";
	return "no drive";
}

/* ---- 1. the two-wallet economy ------------------------------------------------------ */

/* Credit mode x Double Earnings x Scavenger stacks x which machine, against the real award
 * calls. The wallet arithmetic is the part a player sees; the ledger attribution underneath
 * is what the run summary prints, so both are checked. */
static void qa_economy_matrix(void)
{
	char label[192];

	for (int shared = 0; shared <= 1; ++shared)
	for (int dbl = 0; dbl <= 1; ++dbl)
	for (int local = 0; local <= 1; ++local)
	for (int payee = 0; payee <= 1; ++payee)
	{
		qa_session((uint)local);
		qa_clear_ships();
		coop_set_session_shared_credit(shared != 0);
		coop_set_session_double_earnings(dbl != 0);

		const bool doubling = (dbl != 0) && (shared == 0);
		snprintf(label, sizeof(label),
		         "%s credit + Double Earnings %s: doubling is %s",
		         shared ? "Shared" : "Individual", dbl ? "on" : "off",
		         doubling ? "live" : "stood down");
		qa_check(coop_earnings_are_doubled() == doubling, label);

		/* A pickup. Shared pays both in full; Individual pays only its collector, doubled
		 * when the host turned Double Earnings on to compensate the split. */
		qa_clear_ledger();
		player[0].cash = player[1].cash = 0;
		player_award_pickup_cash(&player[payee], 100);
		const long wantPayee = shared ? 100 : (doubling ? 200 : 100);
		const long wantOther = shared ? 100 : 0;
		snprintf(label, sizeof(label),
		         "%s/%s pickup to P%d from machine %d pays %ld to the collector",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x",
		         payee + 1, local + 1, wantPayee);
		qa_check((long)player[payee].cash == wantPayee, label);
		snprintf(label, sizeof(label),
		         "%s/%s pickup to P%d from machine %d pays %ld to the partner",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x",
		         payee + 1, local + 1, wantOther);
		qa_check((long)player[1 - payee].cash == wantOther, label);

		/* Kill cash and bounty cash follow the same rule: Double Earnings covers combat
		 * income whole, so a split take is compensated whatever earned it. */
		qa_clear_ledger();
		player[0].cash = player[1].cash = 0;
		player_award_kill_cash(&player[payee], 100);
		snprintf(label, sizeof(label),
		         "%s/%s kill cash to P%d from machine %d pays %ld to the killer",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x", payee + 1, local + 1,
		         wantPayee);
		qa_check((long)player[payee].cash == wantPayee, label);
		snprintf(label, sizeof(label),
		         "%s/%s kill cash to P%d from machine %d reaches the partner only when Shared",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x", payee + 1, local + 1);
		qa_check((long)player[1 - payee].cash == wantOther, label);

		qa_clear_ledger();
		player[0].cash = player[1].cash = 0;
		player_award_bounty_cash(&player[payee], 150);
		snprintf(label, sizeof(label),
		         "%s/%s bounty to P%d from machine %d follows the kill rule and books as BOUNTY",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x", payee + 1, local + 1);
		qa_check((long)player[payee].cash == (shared ? 150 : (doubling ? 300 : 150))
		         && (payee != local
		             || endlessCashBySource[ENDLESS_CASH_BOUNTY]
		                == (Uint64)(shared ? 150 : (doubling ? 300 : 150))),
		         label);

		/* The run ledger follows the wallet of whoever is at this keyboard. Income into that
		 * wallet has to land against the source that earned it, or the run summary reports
		 * the wrong column. */
		if (payee == local)
		{
			qa_clear_ledger();
			player[0].cash = player[1].cash = 0;
			player_award_pickup_cash(&player[payee], 100);
			snprintf(label, sizeof(label),
			         "%s credit books this machine's own pickup against PICKUP in the run ledger",
			         shared ? "Shared" : "Individual");
			qa_check(endlessCashBySource[ENDLESS_CASH_PICKUP] == (Uint64)(doubling ? 200 : 100),
			         label);
		}
	}

	/* Scavenger is personal: each ship earns at the rate its own stacks set, whatever the
	 * partner picked. The fx context names whose rate is being read. */
	qa_session(0);
	qa_clear_ships();
	for (int a = 0; a <= 2; ++a)
	for (int b = 0; b <= 2; ++b)
	{
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		endlessPerkGrant(0, PERK_CASH, a);
		endlessPerkGrant(1, PERK_CASH, b);
		endlessSetFxPlayer(0);
		snprintf(label, sizeof(label),
		         "Scavenger %d on P1 + %d on P2 pays P1 at %d%%",
		         a, b, 100 + a * ENDLESS_PERK_CASH_PCT);
		qa_check(endlessPerkCashPercent() == 100 + a * ENDLESS_PERK_CASH_PCT, label);
		endlessSetFxPlayer(1);
		snprintf(label, sizeof(label),
		         "Scavenger %d on P1 + %d on P2 pays P2 at %d%%",
		         a, b, 100 + b * ENDLESS_PERK_CASH_PCT);
		qa_check(endlessPerkCashPercent() == 100 + b * ENDLESS_PERK_CASH_PCT, label);
		endlessSetFxPlayer(0);
	}
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
}

/* ---- 2. drives ---------------------------------------------------------------------- */

/* Every pairing of what the two ships are flying, crossed with the Combo Feed setting and who
 * fired the killing shot. A drive is the one purchase whose whole value is that it belongs to
 * the ship that paid for it, so every cell checks both ships. */
static void qa_drive_matrix(void)
{
	static const unsigned drives[4] = {
		0u,
		(unsigned)ENDLESS_MOD_TURBODRIVE,
		(unsigned)ENDLESS_MOD_OVERDRIVE,
		(unsigned)ENDLESS_MOD_OVERBLAST,
	};
	char label[224];

	qa_session(0);

	for (int i = 0; i < 4; ++i)
	for (int j = 0; j < 4; ++j)
	for (int comboShared = 0; comboShared <= 1; ++comboShared)
	for (int k = 0; k < 3; ++k)
	{
		const int killer = (k == 2) ? ENDLESS_KILLER_NONE : k;
		qa_clear_ships();
		endlessCoopComboShared = (comboShared != 0);

		endlessActiveMods = 0;
		endlessPurchasedMods[0] = drives[i];
		endlessPurchasedMods[1] = drives[j];
		endlessApplyPurchasedMods();

		/* A bought drive is personal: it must not appear on the partner's mask, and must not
		 * leak into the sector mask every ship reads. */
		for (uint p = 0; p < 2; ++p)
		{
			const unsigned mine = drives[p == 0 ? i : j];
			const Uint64 got = endlessPlayerMods[p] & (Uint64)ENDLESS_MOD_KILLFIRE_ANY;
			snprintf(label, sizeof(label),
			         "P1 %s / P2 %s: P%d flies exactly its own drive",
			         qa_drive_name(drives[i]), qa_drive_name(drives[j]), p + 1);
			qa_check(got == (Uint64)mine, label);
		}
		snprintf(label, sizeof(label),
		         "P1 %s / P2 %s: neither purchase reaches the sector mask",
		         qa_drive_name(drives[i]), qa_drive_name(drives[j]));
		qa_check((endlessActiveMods & (Uint64)ENDLESS_MOD_KILLFIRE_ANY) == 0, label);

		/* One kill. Under Individual only the shooter's window opens; under Shared both do;
		 * a kill nobody can claim feeds both either way, so no streak is punished for it. */
		memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
		memset(endlessComboKills, 0, sizeof(endlessComboKills));
		memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
		endlessCountKill(0, killer);

		for (uint p = 0; p < 2; ++p)
		{
			const unsigned mine = drives[p == 0 ? i : j];
			const bool fed = comboShared || killer == ENDLESS_KILLER_NONE || (uint)killer == p;
			const bool wantWindow = fed && (mine & (unsigned)ENDLESS_MOD_KILLFIRE_ANY) != 0;
			snprintf(label, sizeof(label),
			         "P1 %s / P2 %s, %s feed, killer %s: P%d window %s",
			         qa_drive_name(drives[i]), qa_drive_name(drives[j]),
			         comboShared ? "Shared" : "Individual",
			         killer == ENDLESS_KILLER_NONE ? "unclaimable" : (killer == 0 ? "P1" : "P2"),
			         p + 1, wantWindow ? "opens" : "stays shut");
			qa_check((endlessTurbodriveTimer[p] > 0) == wantWindow, label);
			snprintf(label, sizeof(label),
			         "P1 %s / P2 %s, %s feed, killer %s: P%d combo %s",
			         qa_drive_name(drives[i]), qa_drive_name(drives[j]),
			         comboShared ? "Shared" : "Individual",
			         killer == ENDLESS_KILLER_NONE ? "unclaimable" : (killer == 0 ? "P1" : "P2"),
			         p + 1, wantWindow ? "counts the kill" : "is untouched");
			qa_check(endlessComboKills[p] == (wantWindow ? 1 : 0), label);

			/* Overdrive and Overblast are the two that stack damage; Turbodrive does not. */
			const bool wantStack = fed && (mine & (unsigned)ENDLESS_MOD_STACKED) != 0;
			snprintf(label, sizeof(label),
			         "P1 %s / P2 %s, %s feed, killer %s: P%d damage stack %s",
			         qa_drive_name(drives[i]), qa_drive_name(drives[j]),
			         comboShared ? "Shared" : "Individual",
			         killer == ENDLESS_KILLER_NONE ? "unclaimable" : (killer == 0 ? "P1" : "P2"),
			         p + 1, wantStack ? "climbs" : "holds");
			qa_check(endlessOverdriveStacks[p] == (wantStack ? 1 : 0), label);
		}
	}

	/* A drive the SECTOR deals is not a purchase: both ships fly it. A ship that then buys its
	 * own replaces it for itself alone, and the partner keeps flying what was dealt. */
	for (int i = 1; i < 4; ++i)
	for (int j = 1; j < 4; ++j)
	{
		qa_clear_ships();
		endlessActiveMods = drives[i];
		endlessPurchasedMods[0] = 0;
		endlessPurchasedMods[1] = drives[j];
		endlessApplyPurchasedMods();
		snprintf(label, sizeof(label),
		         "sector deals %s, P2 buys %s: P1 keeps the dealt drive",
		         qa_drive_name(drives[i]), qa_drive_name(drives[j]));
		qa_check((endlessPlayerMods[0] & (Uint64)ENDLESS_MOD_KILLFIRE_ANY) == (Uint64)drives[i],
		         label);
		snprintf(label, sizeof(label),
		         "sector deals %s, P2 buys %s: the buy replaces it for P2 alone",
		         qa_drive_name(drives[i]), qa_drive_name(drives[j]));
		qa_check((endlessPlayerMods[1] & (Uint64)ENDLESS_MOD_KILLFIRE_ANY) == (Uint64)drives[j],
		         label);
	}

	/* The paid charge tier scales the window, and each ship's window is computed from its own
	 * tier: one player buying a deeper charge must not lengthen the other's. */
	qa_clear_ships();
	endlessBuffCharge[0] = 2;
	endlessBuffCharge[1] = 17;
	endlessSetFxPlayer(0);
	qa_check(endlessBuffChargePaid() == 2, "the charge readout follows the ship being computed");
	endlessSetFxPlayer(1);
	qa_check(endlessBuffChargePaid() == 17, "...and the partner reads its own");
	endlessSetFxPlayer(0);
	qa_check(endlessBuffWindowTicksFor(1) > endlessBuffWindowTicksFor(0),
	         "a deeper paid charge buys the longer kill-fire window, per ship");
	endlessBuffCharge[0] = endlessBuffCharge[1] = 0;
	endlessCoopComboShared = false;
}

/* ---- 3. perks ----------------------------------------------------------------------- */

/* Perks are personal, picked from a per-player slate: each ship flies its own row, each row
 * caps at the perk's normal maximum, and neither row leaks into the other's effects. */
static void qa_perk_matrix(void)
{
	char label[192];
	qa_session(0);
	qa_clear_ships();

	for (int id = 0; id < PERK_COUNT; ++id)
	{
		const int cap = endlessPerkMaxStack(id);
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		endlessPerkRederive();

		/* Each ship stacks to its own cap; each row reads back its own count and no more. */
		endlessPerkGrant(0, id, cap);
		endlessPerkGrant(1, id, cap);
		snprintf(label, sizeof(label),
		         "perk %d: each ship holds its own full stack of %d", id, cap);
		qa_check(endlessPerkEffective(0, id) == (JE_byte)cap
		         && endlessPerkEffective(1, id) == (JE_byte)cap, label);

		/* One ship giving a stack back leaves the other's holding intact. */
		endlessPerkGrant(0, id, -cap);
		snprintf(label, sizeof(label),
		         "perk %d: P1 losing its stacks leaves P2's %d standing", id, cap);
		qa_check(endlessPerkTakenBy[0][id] == 0 && endlessPerkTakenBy[1][id] == (JE_byte)cap,
		         label);
		snprintf(label, sizeof(label),
		         "perk %d: ...and only P2's row still carries it", id);
		qa_check(endlessPerkEffective(0, id) == 0 && endlessPerkEffective(1, id) == (JE_byte)cap,
		         label);

		/* A grant past the cap clamps rather than wrapping the byte. */
		endlessPerkGrant(0, id, cap + 5);
		snprintf(label, sizeof(label), "perk %d: an oversized grant clamps to the cap", id);
		qa_check(endlessPerkTakenBy[0][id] == (JE_byte)cap, label);

		/* And below zero it floors rather than underflowing. */
		endlessPerkGrant(0, id, -(cap + 9));
		snprintf(label, sizeof(label), "perk %d: taking back more than held floors at none", id);
		qa_check(endlessPerkTakenBy[0][id] == 0, label);
	}

	/* Out-of-range ids and player slots are ignored rather than writing past the arrays. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(2, PERK_DAMAGE, 3);
	endlessPerkGrant(0, PERK_COUNT, 3);
	endlessPerkGrant(0, -1, 3);
	qa_check(endlessPerkTakenBy[0][PERK_DAMAGE] == 0 && endlessPerkTakenBy[1][PERK_DAMAGE] == 0,
	         "a perk grant outside the player or perk range is dropped");

	/* Total-owned drives the extra-perk surcharge, priced off the buyer's own collection. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_DAMAGE, 1);
	endlessPerkGrant(1, PERK_ARMOR, 1);
	endlessPerkRederive();
	qa_check(endlessPerkTotalOwned() == 1,
	         "the perk surcharge counts only the buyer's own picks");

	/* Adrenaline is personal on both halves: the ship's own stacks, and the ship's own hull that
	 * arms them. A partner's damage arming it would have one ship's trouble firing the other's
	 * perk, which is the shared model this run no longer uses. */
	qa_clear_ships();
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_ADRENALINE, 1);
	endlessPerkRederive();
	player[0].initial_armor = player[1].initial_armor = 30;
	player[0].armor = 30;
	player[1].armor = 1;                 // only the partner is in trouble
	endlessSetFxPlayer(0);
	qa_check(!endlessAdrenalineActive(),
	         "Adrenaline stays down while its owner is whole and only the partner is hurt");
	player[0].armor = 1;
	qa_check(endlessAdrenalineActive(), "...and arms once its owner's own hull is the hurt one");
	endlessSetFxPlayer(1);
	qa_check(!endlessAdrenalineActive(),
	         "a ship that never picked Adrenaline gets nothing from being hurt");
	endlessSetFxPlayer(0);
	qa_clear_ships();

	/* The registry pair the debug screen and the campaign-mods config both drive has to name one
	 * row, not two: a get that read the combined holding would let the debug menu write its
	 * partner's stacks into its own row the moment it was opened from either machine. */
	for (int local = 0; local <= 1; ++local)
	{
		qa_session((uint)local);
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		endlessPerkGrant(1 - (uint)local, PERK_DAMAGE, 2);   // the partner's picks, not ours
		endlessPerkRederive();
		snprintf(label, sizeof(label),
		         "machine %d: the perk registry reads its own row, not the pair's", local + 1);
		qa_check(endlessPerkGetOwned(PERK_DAMAGE) == 0, label);

		endlessPerkSetOwned(PERK_DAMAGE, 3);
		snprintf(label, sizeof(label),
		         "machine %d: setting a perk writes the row the get reads back", local + 1);
		qa_check(endlessPerkGetOwned(PERK_DAMAGE) == 3
		         && endlessPerkTakenBy[local][PERK_DAMAGE] == 3
		         && endlessPerkTakenBy[1 - local][PERK_DAMAGE] == 2, label);
	}
	qa_session(0);

	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
}

/* ---- 3b. per-ship reactive timers ---------------------------------------------------- */

/* The reactive danger and perk timers belong to one hull each: the Aegis gate, the Static
 * Discharge recharge lockout, the Countermeasure Suite cooldown and the Shield Matrix interval.
 * A shared timer had one ship's event disarming or slowing the partner. */
static void qa_reactive_state_matrix(void)
{
	qa_session(0);
	qa_clear_ships();

	/* Aegis: a block arms only the blocking ship's cooldown, and each recovers on its own. */
	endlessActiveMods = ENDLESS_MOD_AEGIS;
	endlessAegisReset();
	endlessSetFxPlayer(0);
	qa_check(endlessAegisGateConsume(10, 5), "P1's Aegis gate blocks its first overflow hit");
	qa_check(!endlessAegisGateConsume(10, 5), "...and P1's own gate is then on cooldown");
	endlessSetFxPlayer(1);
	qa_check(endlessAegisGateConsume(10, 5), "P1's block leaves P2's gate armed for P2's hit");
	endlessSetFxPlayer(0);
	for (int t = 0; t < 200; ++t)   // past any cooldown length
		endlessAegisTick();
	qa_check(endlessAegisGateConsume(10, 5), "the gate recharges per ship, not per pair");
	endlessAegisReset();

	/* Static Discharge: the hit ship's recharge stalls, the partner's keeps charging. */
	endlessActiveMods = ENDLESS_MOD_STATIC;
	endlessStaticLockoutReset();
	endlessSetFxPlayer(0);
	qa_check(endlessStaticDischargeDrain(5) > 0, "a hit on P1 bleeds P1's generator");
	qa_check(endlessGeneratorPowerAdd(7) == 0, "...and stalls P1's own recharge");
	endlessSetFxPlayer(1);
	qa_check(endlessGeneratorPowerAdd(7) == 7,
	         "P2's generator keeps charging through P1's lockout");
	endlessSetFxPlayer(0);
	endlessStaticLockoutReset();
	qa_check(endlessGeneratorPowerAdd(7) == 7, "the zone-start reset clears every ship's lockout");
	endlessActiveMods = 0;

	/* Countermeasures: each ship's burst re-arms its own cooldown at its own stack radius. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_COUNTERMEASURE, 1);
	endlessPerkGrant(1, PERK_COUNTERMEASURE, 2);
	endlessResetZonePerkTimers();
	endlessSetFxPlayer(0);
	qa_check(endlessPerkCountermeasureRadius() == ENDLESS_PERK_CM_RADIUS1,
	         "P1's countermeasures are ready at its own one-stack radius");
	endlessCountermeasureFired();
	qa_check(endlessPerkCountermeasureRadius() == 0, "...and firing puts P1 on cooldown");
	endlessSetFxPlayer(1);
	qa_check(endlessPerkCountermeasureRadius() == ENDLESS_PERK_CM_RADIUS2,
	         "P1's burst leaves P2's wider suite armed");
	endlessCountermeasureFired();
	for (int t = 0; t < ENDLESS_PERK_CM_COOLDOWN; ++t)
		endlessCountermeasureTick();
	qa_check(endlessPerkCountermeasureRadius() == ENDLESS_PERK_CM_RADIUS2,
	         "the tick re-arms P2");
	endlessSetFxPlayer(0);
	qa_check(endlessPerkCountermeasureRadius() == ENDLESS_PERK_CM_RADIUS1,
	         "...and P1 alongside it");

	/* Shield Matrix reads the ship being computed; the regen loop names each ship in turn. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(1, PERK_SHIELDREGEN, 2);
	endlessSetFxPlayer(0);
	qa_check(endlessPerkShieldWait(15) == 15,
	         "P2's Shield Matrix leaves P1's regen interval stock");
	endlessSetFxPlayer(1);
	qa_check(endlessPerkShieldWait(15) == 15 - 2 * ENDLESS_PERK_SHIELDRGN_STEP,
	         "...while P2 regenerates at its own quickened interval");
	endlessSetFxPlayer(0);

	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessResetZonePerkTimers();
}

/* ---- 4. the outpost, two shelves at once -------------------------------------------- */

/* Both players shop the same outpost at the same time with their own wallet, their own prices
 * and their own shelves. Anything that escalates a price or moves an RNG stream has to move
 * only the acting player's. */
static void qa_outpost_matrix(void)
{
	char label[192];

	for (int local = 0; local <= 1; ++local)
	{
		const uint me = (uint)local, them = (uint)(1 - local);
		qa_session(me);
		qa_clear_ships();
		endlessRunDepth = 7;

		snprintf(label, sizeof(label),
		         "machine %d spends its own wallet at the outpost", local + 1);
		qa_check(endlessEconomyIndex() == me && endlessPartnerIndex() == them, label);

		/* Prices are per player. Reset from one machine must not restock the other's. */
		endlessRerollCost[0] = endlessRerollCost[1] = 0;
		endlessHullCost[0] = endlessHullCost[1] = 0;
		endlessResetShopPrices();
		snprintf(label, sizeof(label),
		         "machine %d resetting shop prices sets only its own reroll price", local + 1);
		qa_check(endlessRerollCost[me] > 0 && endlessRerollCost[them] == 0, label);
		snprintf(label, sizeof(label),
		         "machine %d resetting shop prices sets only its own hull price", local + 1);
		qa_check(endlessHullCost[me] > 0 && endlessHullCost[them] == 0, label);

		/* A reroll is the acting player's alone: their cash, their escalating price, their
		 * shelves. The partner's price must not climb because someone else rerolled. */
		endlessResetShopPrices();
		const long priceBefore = endlessRerollPrice();
		const long partnerBefore = endlessRerollCost[them];
		player[me].cash = (ulong)priceBefore * 4;
		player[them].cash = (ulong)priceBefore * 4;
		endlessCashResync();
		const bool bought = endlessTryReroll();
		snprintf(label, sizeof(label), "machine %d can afford its reroll", local + 1);
		qa_check(bought, label);
		snprintf(label, sizeof(label),
		         "machine %d paid for its own reroll", local + 1);
		qa_check((long)player[me].cash == priceBefore * 4 - priceBefore, label);
		snprintf(label, sizeof(label),
		         "machine %d rerolling leaves the partner's wallet alone", local + 1);
		qa_check((long)player[them].cash == priceBefore * 4, label);
		snprintf(label, sizeof(label),
		         "machine %d rerolling escalates only its own next price", local + 1);
		qa_check(endlessRerollCost[me] > priceBefore && endlessRerollCost[them] == partnerBefore,
		         label);

		/* An unaffordable reroll is refused outright and charges nothing. */
		player[me].cash = 0;
		endlessCashResync();
		const long partnerCash = (long)player[them].cash;
		snprintf(label, sizeof(label),
		         "machine %d cannot reroll with an empty wallet", local + 1);
		qa_check(!endlessTryReroll(), label);
		snprintf(label, sizeof(label),
		         "machine %d being broke does not reach into the partner's wallet", local + 1);
		qa_check((long)player[them].cash == partnerCash, label);

		/* Reinforce is a personal hull tier. */
		endlessResetShopPrices();
		endlessArmorBonus[0] = endlessArmorBonus[1] = 0;
		player[me].cash = 9999999;
		endlessCashResync();
		const bool reinforced = endlessTryReinforce();
		snprintf(label, sizeof(label), "machine %d buys a hull tier", local + 1);
		qa_check(reinforced, label);
		snprintf(label, sizeof(label),
		         "machine %d's hull tier is its own, not the partner's", local + 1);
		qa_check(endlessArmorBonus[me] > 0 && endlessArmorBonus[them] == 0, label);
	}

	/* Per-player RNG streams. One player's rerolls must never perturb what the other is
	 * dealt, so the streams are forked and drawn from independently. */
	qa_session(0);
	qa_clear_ships();
	endlessSetSeed("qa-coop-streams");
	endlessReseedPlayers(0);
	const Uint64 seeded0 = endlessPlayerRngState[0], seeded1 = endlessPlayerRngState[1];
	qa_check(seeded0 != seeded1,
	         "the two players' shop RNG streams fork to different states");

	endlessReseedPlayers(0);
	Uint32 solo[6];
	for (int i = 0; i < 6; ++i)
		solo[i] = endlessRandFor(1);

	endlessReseedPlayers(0);
	for (int i = 0; i < 40; ++i)
		(void)endlessRandFor(0);          // one player rerolls hard
	bool same = true;
	for (int i = 0; i < 6; ++i)
		if (endlessRandFor(1) != solo[i])
			same = false;
	qa_check(same, "one player's rerolls do not move what the other is dealt next");

	/* The same seed reproduces both streams, so both machines agree on the stock. */
	endlessReseedPlayers(0);
	qa_check(endlessPlayerRngState[0] == seeded0 && endlessPlayerRngState[1] == seeded1,
	         "reseeding the run restores both players' streams identically");

	/* Shop-entry cash is per player: the cash-fraction prices must be quoted off the wallet
	 * of the player looking at them. */
	qa_clear_ships();
	endlessShopEntryCash[0] = 100000;
	endlessShopEntryCash[1] = 20000;
	qa_session(0);
	const long turboAt0 = endlessTurbodrivePrice();
	qa_session(1);
	const long turboAt1 = endlessTurbodrivePrice();
	qa_check(turboAt0 > turboAt1,
	         "a cash-fraction price is quoted off the looking player's own entry cash");

	/* Loan Shark is a personal, permanent surcharge. It must not tax the partner. */
	qa_clear_ships();
	endlessShopTax[0] = 50;
	endlessShopTax[1] = 0;
	qa_session(0);
	const int taxedMe = endlessShopTaxPercent();
	qa_session(1);
	const int taxedThem = endlessShopTaxPercent();
	qa_check(taxedMe == 50 && taxedThem == 0,
	         "Loan Shark surcharges only the player who took the loan");

	endlessShopTax[0] = endlessShopTax[1] = 0;
	endlessRunDepth = 0;
}

/* ---- 4b. every E-Shop button, from both machines ------------------------------------ */

/* Each buy is checked three ways: it is refused when the buyer cannot afford it, it charges
 * and delivers to the buyer alone when they can, and its refusal gate (already held, maxed
 * out, still recharging) holds. A purchase leaking to the partner is the failure that matters,
 * so every case reads both slots. */
static void qa_eshop_matrix(void)
{
	char label[224];

	for (int local = 0; local <= 1; ++local)
	{
		const uint me = (uint)local, them = (uint)(1 - local);

		/* --- superbombs --- */
		qa_session(me);
		qa_clear_ships();
		endlessRunDepth = 4;
		endlessResetShopPrices();
		player[me].cash = 0;
		endlessCashResync();
		snprintf(label, sizeof(label), "machine %d: a broke buyer cannot take a bomb", local + 1);
		qa_check(!endlessTryBuyBomb(), label);
		snprintf(label, sizeof(label),
		         "machine %d: the refused bomb left both bomb counts alone", local + 1);
		qa_check(player[me].superbombs == 0 && player[them].superbombs == 0, label);

		player[me].cash = 99999999;
		endlessCashResync();
		snprintf(label, sizeof(label), "machine %d: a solvent buyer takes a bomb", local + 1);
		qa_check(endlessTryBuyBomb(), label);
		snprintf(label, sizeof(label),
		         "machine %d: the bomb went to the buyer and not the partner", local + 1);
		qa_check(player[me].superbombs == 1 && player[them].superbombs == 0, label);

		/* The magazine has a ceiling, and hitting it refuses rather than overflowing. */
		player[me].superbombs = 10;
		snprintf(label, sizeof(label),
		         "machine %d: a full bomb magazine refuses another", local + 1);
		qa_check(!endlessTryBuyBomb(), label);
		qa_check(player[me].superbombs == 10, "...and stays at its ceiling");

		/* --- revive token --- */
		qa_session(me);
		qa_clear_ships();
		endlessRunDepth = 4;
		endlessResetShopPrices();
		player[me].cash = 0;
		endlessCashResync();
		snprintf(label, sizeof(label),
		         "machine %d: a broke buyer cannot take a revive token", local + 1);
		qa_check(!endlessTryBuyRevive(), label);

		player[me].cash = 99999999;
		endlessCashResync();
		const long revivePrice = endlessRevivePrice();
		snprintf(label, sizeof(label), "machine %d: a solvent buyer takes a revive", local + 1);
		qa_check(endlessTryBuyRevive(), label);
		snprintf(label, sizeof(label),
		         "machine %d: the token is the buyer's alone", local + 1);
		qa_check(endlessReviveHeld[me] && !endlessReviveHeld[them], label);
		snprintf(label, sizeof(label),
		         "machine %d: the revive charged its own wallet", local + 1);
		qa_check((long)player[me].cash == 99999999L - revivePrice, label);
		snprintf(label, sizeof(label),
		         "machine %d: a second token is refused while one is held", local + 1);
		qa_check(!endlessTryBuyRevive(), label);
		snprintf(label, sizeof(label),
		         "machine %d: the buyer can still see the partner has no token", local + 1);
		qa_check(!endlessReviveHeld[them], label);

		/* The price doubles per revive already spent, per player. */
		endlessReviveHeld[me] = false;
		endlessRevivesUsed[me] = 1;
		const long afterOne = endlessRevivePrice();
		endlessRevivesUsed[me] = 2;
		const long afterTwo = endlessRevivePrice();
		snprintf(label, sizeof(label),
		         "machine %d: each spent revive doubles the next token's price", local + 1);
		qa_check(afterOne == revivePrice * 2 && afterTwo == revivePrice * 4, label);
		endlessRevivesUsed[me] = 0;

		/* --- sabotage charges --- */
		qa_session(me);
		qa_clear_ships();
		endlessRunDepth = 4;
		endlessResetShopPrices();
		player[me].cash = 99999999;
		endlessCashResync();
		for (int c = 1; c <= ENDLESS_CLEANSE_MAX_CHARGES; ++c)
		{
			snprintf(label, sizeof(label),
			         "machine %d: sabotage charge %d is bought", local + 1, c);
			qa_check(endlessTryBuyCleanse(), label);
			snprintf(label, sizeof(label),
			         "machine %d: sabotage charge %d is the buyer's own", local + 1, c);
			qa_check(endlessCleanseChargeCount[me] == c
			         && endlessCleanseChargeCount[them] == 0, label);
		}
		snprintf(label, sizeof(label),
		         "machine %d: sabotage refuses past its per-visit cap", local + 1);
		qa_check(endlessCleanseMaxed() && !endlessTryBuyCleanse(), label);

		/* --- kill-fire drives --- */
		for (int which = 0; which < 3; ++which)
		{
			qa_session(me);
			qa_clear_ships();
			endlessRunDepth = 4;
			endlessResetShopPrices();
			endlessShopEntryCash[me] = 400000;
			endlessShopEntryCash[them] = 400000;
			player[me].cash = 0;
			endlessCashResync();

			const char *const driveName = (which == 0) ? "Turbodrive"
			                            : (which == 1) ? "Overblast" : "Overdrive";
			snprintf(label, sizeof(label),
			         "machine %d: a broke buyer cannot take %s", local + 1, driveName);
			const bool brokeBuy = (which == 0) ? endlessTryBuyTurbodrive()
			                    : (which == 1) ? endlessTryBuyOverblast() : endlessTryBuyOverdrive();
			qa_check(!brokeBuy, label);

			player[me].cash = 99999999;
			endlessCashResync();
			const bool bought = (which == 0) ? endlessTryBuyTurbodrive()
			                  : (which == 1) ? endlessTryBuyOverblast() : endlessTryBuyOverdrive();
			snprintf(label, sizeof(label),
			         "machine %d: a solvent buyer takes %s", local + 1, driveName);
			qa_check(bought, label);
			snprintf(label, sizeof(label),
			         "machine %d: %s is held by the buyer alone", local + 1, driveName);
			qa_check(endlessPurchasedMods[me] != 0 && endlessPurchasedMods[them] == 0, label);
			snprintf(label, sizeof(label),
			         "machine %d: %s records its kind against the buyer alone", local + 1, driveName);
			qa_check(endlessBuffKind[me] != ENDLESS_BUFF_KIND_NONE
			         && endlessBuffKind[them] == ENDLESS_BUFF_KIND_NONE, label);
			snprintf(label, sizeof(label),
			         "machine %d: %s sets a paid charge tier for the buyer", local + 1, driveName);
			qa_check(endlessBuffCharge[me] > 0 && endlessBuffCharge[them] == 0, label);

			/* One drive per visit: the other two are refused once one is held. */
			snprintf(label, sizeof(label),
			         "machine %d: holding %s refuses a second drive this visit", local + 1, driveName);
			qa_check(!endlessTryBuyTurbodrive() && !endlessTryBuyOverblast()
			         && !endlessTryBuyOverdrive(), label);

			/* The recharge lock is per player: it must not stop the partner buying. */
			endlessBuffCooldownUntil[me] = endlessRunDepth + 3;
			snprintf(label, sizeof(label),
			         "machine %d: the recharge lock is on the buyer after %s", local + 1, driveName);
			qa_check(endlessBuffOnCooldown() && endlessBuffCooldownLeft() == 3, label);
			qa_session(them);
			snprintf(label, sizeof(label),
			         "machine %d: the partner is not locked out by that purchase", local + 1);
			qa_check(!endlessBuffOnCooldown(), label);
		}
	}

	/* Hostile values must not tip a price or a counter over. A wallet at the top of its range
	 * still buys, and a debit larger than the wallet cannot take more than is there. */
	qa_session(0);
	qa_clear_ships();
	endlessRunDepth = 999;
	endlessResetShopPrices();
	player[0].cash = 0xFFFFFFFFu;
	endlessCashResync();
	qa_check(endlessRerollPrice() > 0, "a very deep run still quotes a positive reroll price");
	qa_check(endlessRevivePrice() > 0, "a very deep run still quotes a positive revive price");
	endlessCashDebit((Sint64)0x7FFFFFFFFFLL, ENDLESS_SINK_REROLL);
	qa_check(player[0].cash == 0,
	         "a debit larger than the wallet empties it rather than wrapping");

	endlessRunDepth = 0;
	qa_clear_ships();
}

/* ---- 5. going down, and coming back ------------------------------------------------- */

/* Order on a lethal hit is: a held revive token fires first, and only then does the ship enter
 * the DOWNED state. A downed ship spectates until the zone ends; if the partner finishes it,
 * the downed one is back at the outpost with full hull and no shield. */
static void qa_death_revive_matrix(void)
{
	char label[224];
	static const EndlessRunMode modes[3] = {
		ENDLESS_RUNMODE_RELAXED, ENDLESS_RUNMODE_STANDARD, ENDLESS_RUNMODE_HARDCORE };
	static const char *const modeName[3] = { "Relaxed", "Standard", "Hardcore" };

	qa_session(0);

	for (int m = 0; m < 3; ++m)
	for (int d0 = 0; d0 <= 1; ++d0)
	for (int d1 = 0; d1 <= 1; ++d1)
	for (int t0 = 0; t0 <= 1; ++t0)
	for (int t1 = 0; t1 <= 1; ++t1)
	{
		qa_clear_ships();
		endlessRunMode = modes[m];
		endlessPlayerDowned[0] = (d0 != 0);
		endlessPlayerDowned[1] = (d1 != 0);
		endlessReviveHeld[0] = (t0 != 0);
		endlessReviveHeld[1] = (t1 != 0);

		/* Somebody is still flying unless both are down. A downed ship neither flies nor
		 * counts, even though its struct is still alive for the spectate camera. */
		const bool wantFlying = !(d0 && d1);
		snprintf(label, sizeof(label),
		         "%s, P1 %s / P2 %s: the zone %s",
		         modeName[m], d0 ? "down" : "flying", d1 ? "down" : "flying",
		         wantFlying ? "continues" : "has nobody left");
		qa_check(endlessAnyPlayerFlying() == wantFlying, label);

		/* The pause menu locks from the fatal hit in Standard and Hardcore only. Relaxed
		 * offers the retry openly through the death menu instead. */
		snprintf(label, sizeof(label), "%s %s the menu once a ship is down",
		         modeName[m], modes[m] == ENDLESS_RUNMODE_RELAXED ? "leaves open" : "locks");
		qa_check(endlessDeathLocksMenu() == (modes[m] != ENDLESS_RUNMODE_RELAXED), label);

		/* The survivor finishing the zone brings the partner back at the outpost: full
		 * hull, no shield, and no longer downed. Whoever was already flying is untouched. */
		player[0].armor = 3; player[1].armor = 3;
		player[0].shield = 7; player[1].shield = 7;
		endlessReviveDownedAtOutpost();
		for (uint p = 0; p < 2; ++p)
		{
			const bool wasDown = (p == 0) ? (d0 != 0) : (d1 != 0);
			snprintf(label, sizeof(label),
			         "%s, P%d %s: reaches the outpost off the downed list",
			         modeName[m], p + 1, wasDown ? "was down" : "was flying");
			qa_check(!endlessPlayerDowned[p], label);
			if (wasDown)
			{
				snprintf(label, sizeof(label),
				         "%s, P%d revives at the outpost with a full hull", modeName[m], p + 1);
				qa_check(player[p].armor == player[p].initial_armor, label);
				snprintf(label, sizeof(label),
				         "%s, P%d revives at the outpost with no shield", modeName[m], p + 1);
				qa_check(player[p].shield == 0, label);
			}
		}
	}

	/* A held token survives one lethal hit and is then spent: it restores the hull, counts
	 * against the run, arms the escape window, and cannot fire twice. */
	for (int local = 0; local <= 1; ++local)
	for (uint victim = 0; victim < 2; ++victim)
	{
		qa_session((uint)local);
		qa_clear_ships();
		endlessReviveHeld[victim] = true;
		player[victim].armor = 1;

		const bool saved = endlessConsumeRevive(victim);
		snprintf(label, sizeof(label),
		         "machine %d: P%d's own revive token fires on the lethal hit",
		         local + 1, victim + 1);
		qa_check(saved, label);
		snprintf(label, sizeof(label),
		         "machine %d: P%d's revive restores the hull", local + 1, victim + 1);
		qa_check(player[victim].armor == player[victim].initial_armor, label);
		snprintf(label, sizeof(label),
		         "machine %d: P%d's revive is spent, not still held", local + 1, victim + 1);
		qa_check(!endlessReviveHeld[victim], label);
		snprintf(label, sizeof(label),
		         "machine %d: P%d's revive counts against the run", local + 1, victim + 1);
		qa_check(endlessRevivesUsed[victim] == 1, label);
		snprintf(label, sizeof(label),
		         "machine %d: P%d's revive arms the grace window", local + 1, victim + 1);
		qa_check(endlessReviveGraceActive(), label);
		snprintf(label, sizeof(label),
		         "machine %d: P%d cannot spend the same token twice", local + 1, victim + 1);
		qa_check(!endlessConsumeRevive(victim), label);
		snprintf(label, sizeof(label),
		         "machine %d: P%d's revive left the partner's token alone", local + 1, victim + 1);
		qa_check(!endlessReviveHeld[1 - victim] && endlessRevivesUsed[1 - victim] == 0, label);
		endlessReviveGraceReset();
	}

	/* A token the partner holds cannot save this ship. */
	qa_session(0);
	qa_clear_ships();
	endlessReviveHeld[1] = true;
	qa_check(!endlessConsumeRevive(0),
	         "a revive token held by the partner does not save this ship");
	qa_check(endlessReviveHeld[1], "...and is still theirs afterwards");

	/* Out-of-range slots are refused rather than read past the arrays. */
	qa_check(!endlessConsumeRevive(2), "a revive for a slot that does not exist is refused");

	endlessReviveGraceReset();
	endlessRunMode = ENDLESS_RUNMODE_RELAXED;
}

/* ---- 6. reactive dangers pick a target --------------------------------------------- */

/* Homing and course-correcting hostiles go for the nearer ship still flying. A downed ship
 * neither attracts them nor counts as a candidate. */
static void qa_danger_target_matrix(void)
{
	char label[192];
	qa_session(0);

	for (int d0 = 0; d0 <= 1; ++d0)
	for (int d1 = 0; d1 <= 1; ++d1)
	{
		qa_clear_ships();
		endlessPlayerDowned[0] = (d0 != 0);
		endlessPlayerDowned[1] = (d1 != 0);
		player[0].x = 50;  player[0].y = 100;
		player[1].x = 250; player[1].y = 100;

		const uint nearP0 = endlessDangerTargetPlayer(55, 100);
		const uint nearP1 = endlessDangerTargetPlayer(245, 100);

		if (!d0 && !d1)
		{
			qa_check(nearP0 == 0, "a danger beside P1 goes for P1 while both fly");
			qa_check(nearP1 == 1, "...and one beside P2 goes for P2");
		}
		else if (d0 && !d1)
		{
			snprintf(label, sizeof(label),
			         "a danger beside a downed P1 goes for the flying P2 instead");
			qa_check(nearP0 == 1, label);
		}
		else if (!d0 && d1)
		{
			snprintf(label, sizeof(label),
			         "a danger beside a downed P2 goes for the flying P1 instead");
			qa_check(nearP1 == 0, label);
		}
		else
		{
			qa_check(nearP0 < 2 && nearP1 < 2,
			         "with both ships down a danger still names a slot inside the array");
		}
	}

	/* Outside co-op there is only ever one ship to aim at. */
	qa_clear_ships();
	coopEndlessMode = false;
	qa_check(endlessDangerTargetPlayer(250, 100) == 0,
	         "outside co-op every danger aims at the only ship");
	coopEndlessMode = true;

	/* A homing enemy commits to one ship for life instead of re-rolling, and gives up on a ship
	 * that goes down. The roll itself is the coin toss; both sides have to be reachable. */
	qa_clear_ships();
	qa_check(endlessHomingTargetPlayer(0) == 0 && endlessHomingTargetPlayer(1) == 1,
	         "a homing enemy chases the ship it rolled, not the nearer one");
	endlessPlayerDowned[1] = true;
	qa_check(endlessHomingTargetPlayer(1) == 0,
	         "...and falls to the survivor once that ship is down");
	endlessPlayerDowned[1] = false;
	qa_check(endlessHomingTargetPlayer(200) < COUNTOF(player),
	         "a stored side outside the ship array still names a slot inside it");

	bool sawShip[2] = { false, false };
	for (int i = 0; i < 200; ++i)
		sawShip[endlessRollHomingTarget() & 1u] = true;
	qa_check(sawShip[0] && sawShip[1], "the homing coin toss reaches both ships");

	coopEndlessMode = false;
	qa_check(endlessRollHomingTarget() == 0 && endlessHomingTargetPlayer(1) == 0,
	         "outside co-op a homing enemy takes ship one and spends no roll on it");
	coopEndlessMode = true;
}

/* ---- 6b. the run keeps the difficulty it launched with ------------------------------ */

/* Endless pins its rung: depth scaling is that mode's difficulty curve, and the vanilla
 * score-based drift moving underneath it would re-price every sector's danger and payout
 * mid-run. Solo and online alike, and whatever the player's own difficultyAdjust setting says. */
static void qa_endless_difficulty_pinned(void)
{
	const JE_boolean savedAdjust = difficultyAdjust;
	const JE_boolean savedEndless = endlessMode;
	const JE_boolean savedMods = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;

	for (int adjust = 0; adjust <= 1; ++adjust)
	{
		difficultyAdjust = (adjust != 0);

		endlessMode = false;
		endlessCampaignMods = false;
		coopEndlessMode = false;
		qa_check(difficulty_adjust_active() == (adjust != 0),
		         "outside Endless the score-based drift follows the player's own setting");

		endlessMode = true;
		qa_check(!difficulty_adjust_active(),
		         "a solo Endless run keeps the rung it launched with");

		coopEndlessMode = true;
		qa_check(!difficulty_adjust_active(),
		         "...and so does an online one, however the setting is left");
		coopEndlessMode = false;

		endlessMode = false;
		endlessCampaignMods = true;
		qa_check(!difficulty_adjust_active(),
		         "...and a campaign running the Endless effect layer, which scales the same way");
	}

	difficultyAdjust = savedAdjust;
	endlessMode = savedEndless;
	endlessCampaignMods = savedMods;
	coopEndlessMode = savedCoop;
}

/* ---- 7. who charts the next course -------------------------------------------------- */

/* Exactly one machine charts each zone, and both machines have to reach that answer from the
 * same state: a coin flip read from a local rand would split the session. */
static void qa_chooser_matrix(void)
{
	char label[192];
	static const EndlessCourseChooser modes[4] = {
		ENDLESS_PICK_HOST, ENDLESS_PICK_GUEST, ENDLESS_PICK_ALTERNATE, ENDLESS_PICK_COINFLIP };

	qa_session(0);
	qa_clear_ships();
	endlessSetSeed("qa-chooser");

	for (int m = 0; m < 4; ++m)
	{
		endlessCourseChooser = modes[m];
		endlessCoopHostCharts = true;

		for (int zone = 0; zone < 20; ++zone)
		{
			endlessRunDepth = zone;
			network_is_host = true;
			const bool hostSays = endlessLocalPlayerCharts();
			network_is_host = false;
			const bool guestSays = endlessLocalPlayerCharts();

			snprintf(label, sizeof(label),
			         "%s picking, zone %d: exactly one machine charts",
			         endlessCourseChooserName(modes[m]), zone);
			qa_check(hostSays != guestSays, label);

			if (modes[m] == ENDLESS_PICK_HOST)
			{
				snprintf(label, sizeof(label), "Host picking, zone %d: the host charts", zone);
				qa_check(hostSays, label);
			}
			else if (modes[m] == ENDLESS_PICK_GUEST)
			{
				snprintf(label, sizeof(label), "Guest picking, zone %d: the joiner charts", zone);
				qa_check(guestSays, label);
			}
		}
	}

	/* Alternating takes turns, and the turn flag reads the same on both machines. */
	endlessCourseChooser = ENDLESS_PICK_ALTERNATE;
	endlessCoopHostCharts = true;
	for (int turn = 0; turn < 8; ++turn)
	{
		const bool hostTurn = ((turn % 2) == 0);
		network_is_host = true;
		snprintf(label, sizeof(label), "Alternating turn %d: host %s", turn,
		         hostTurn ? "charts" : "waits");
		qa_check(endlessLocalPlayerCharts() == hostTurn, label);
		network_is_host = false;
		snprintf(label, sizeof(label), "Alternating turn %d: the joiner sees the same turn", turn);
		qa_check(endlessLocalPlayerCharts() == !hostTurn, label);
		endlessAdvanceCourseTurn();
	}

	/* The coin is seeded from the run, so it is the same coin on both machines and it does
	 * actually land on both sides across a run. */
	endlessCourseChooser = ENDLESS_PICK_COINFLIP;
	endlessSetSeed("qa-coin-spread");
	int hostCount = 0;
	for (int zone = 0; zone < 40; ++zone)
	{
		endlessRunDepth = zone;
		network_is_host = true;
		if (endlessLocalPlayerCharts())
			++hostCount;
	}
	qa_check(hostCount > 0 && hostCount < 40,
	         "the 50-50 coin lands on both players across a 40-zone run");

	/* The same seed replays the same sequence of chart turns. */
	endlessSetSeed("qa-coin-spread");
	int repeat = 0;
	for (int zone = 0; zone < 40; ++zone)
	{
		endlessRunDepth = zone;
		network_is_host = true;
		if (endlessLocalPlayerCharts())
			++repeat;
	}
	qa_check(repeat == hostCount, "...and the same seed deals the same turns again");

	endlessRunDepth = 0;
	endlessCourseChooser = ENDLESS_PICK_HOST;
	network_is_host = true;
}

/* ---- 8. the wire ------------------------------------------------------------------- */

/* Whatever a case above can set has to survive the trip to the other machine, because the
 * partner's screen is drawn from the unpacked block. */
static void qa_wire_matrix(void)
{
	char label[192];
	qa_session(0);

	for (int variant = 0; variant < 4; ++variant)
	{
		qa_clear_ships();

		/* Four different shapes of held state, so a field that is only ever zero in one
		 * shape is still covered by another. */
		endlessArmorBonus[0]      = 8 * (variant + 1);
		endlessPurchasedMods[0]   = (variant & 1) ? (unsigned)ENDLESS_MOD_OVERBLAST
		                                          : (unsigned)ENDLESS_MOD_TURBODRIVE;
		endlessBuffKind[0]        = (variant & 1) ? ENDLESS_BUFF_KIND_OVERBLAST
		                                          : ENDLESS_BUFF_KIND_TURBODRIVE;
		endlessBuffCharge[0]      = variant * 5;
		endlessBuffCooldownUntil[0] = variant * 3;
		endlessCleanseChargeCount[0] = variant % (ENDLESS_CLEANSE_MAX_CHARGES + 1);
		endlessLongCon[0]         = variant;
		endlessShopTax[0]         = variant * 25;
		endlessRevivesUsed[0]     = variant;
		endlessRerollCost[0]      = 1000L * (variant + 1);
		endlessHullCost[0]        = 500 * (variant + 1);
		endlessShopEntryCash[0]   = 250000L * (variant + 1);
		endlessReviveHeld[0]      = (variant % 2) == 0;
		endlessGambleRigged[0]    = (variant % 3) == 0;
		endlessPlayerDowned[0]    = (variant % 2) == 1;
		player[0].superbombs      = (uint)variant;

		union {
			Uint32 align;
			Uint8 bytes[ENDLESS_PLAYER_BLOCK_SIZE + 8];
		} guarded;
		memset(guarded.bytes, 0x5a, sizeof(guarded.bytes));
		const int packed = endlessPackPlayerBlock(guarded.bytes + 4, 0);

		snprintf(label, sizeof(label),
		         "co-op block variant %d packs its declared width", variant);
		qa_check(packed == ENDLESS_PLAYER_BLOCK_SIZE, label);
		snprintf(label, sizeof(label),
		         "co-op block variant %d stays inside its buffer", variant);
		qa_check(guarded.bytes[3] == 0x5a
		         && guarded.bytes[4 + ENDLESS_PLAYER_BLOCK_SIZE] == 0x5a, label);

		endlessUnpackPlayerBlock(guarded.bytes + 4, 1);
		snprintf(label, sizeof(label),
		         "co-op block variant %d restores every numeric field into the peer's slot",
		         variant);
		qa_check(endlessArmorBonus[1] == endlessArmorBonus[0]
		         && endlessPurchasedMods[1] == endlessPurchasedMods[0]
		         && endlessBuffKind[1] == endlessBuffKind[0]
		         && endlessBuffCharge[1] == endlessBuffCharge[0]
		         && endlessBuffCooldownUntil[1] == endlessBuffCooldownUntil[0]
		         && endlessCleanseChargeCount[1] == endlessCleanseChargeCount[0]
		         && endlessLongCon[1] == endlessLongCon[0]
		         && endlessShopTax[1] == endlessShopTax[0]
		         && endlessRevivesUsed[1] == endlessRevivesUsed[0]
		         && endlessRerollCost[1] == endlessRerollCost[0]
		         && endlessHullCost[1] == endlessHullCost[0]
		         && endlessShopEntryCash[1] == endlessShopEntryCash[0], label);
		snprintf(label, sizeof(label),
		         "co-op block variant %d restores the one-shot latches", variant);
		qa_check(endlessReviveHeld[1] == endlessReviveHeld[0]
		         && endlessGambleRigged[1] == endlessGambleRigged[0]
		         && endlessPlayerDowned[1] == endlessPlayerDowned[0]
		         && player[1].superbombs == player[0].superbombs, label);
	}
	qa_clear_ships();
}

/* ---- 9. whole-session scenarios ----------------------------------------------------- */

/* The combinations above are each one axis at a time. These are the sessions people actually
 * report: several settings at once, in the order a run reaches them. */
static void qa_scenario_suite(void)
{
	/* Two different drives, Relaxed, Individual cash with Double Earnings on, a Scavenger
	 * stack on one ship only, then one ship goes down and the survivor restarts the zone. */
	for (int local = 0; local <= 1; ++local)
	{
		char label[224];
		qa_session((uint)local);
		qa_clear_ships();
		qa_clear_ledger();

		endlessRunMode = ENDLESS_RUNMODE_RELAXED;
		coop_set_session_shared_credit(false);
		coop_set_session_double_earnings(true);
		endlessCoopComboShared = false;
		endlessRunDepth = 5;

		/* Each ship bought a different drive. */
		endlessActiveMods = 0;
		endlessPurchasedMods[0] = (unsigned)ENDLESS_MOD_TURBODRIVE;
		endlessPurchasedMods[1] = (unsigned)ENDLESS_MOD_OVERBLAST;
		endlessApplyPurchasedMods();
		endlessBuffCharge[0] = 6;
		endlessBuffCharge[1] = 11;

		/* Only P2 took Scavenger. Perks are personal, so only P2 earns at the raised rate. */
		endlessPerkGrant(1, PERK_CASH, 1);
		endlessSetFxPlayer(1);
		snprintf(label, sizeof(label),
		         "machine %d: P2's Scavenger raises only P2's earning rate", local + 1);
		qa_check(endlessPerkCashPercent() == 100 + ENDLESS_PERK_CASH_PCT, label);
		endlessSetFxPlayer(0);
		snprintf(label, sizeof(label),
		         "machine %d: P1 still earns at the stock rate", local + 1);
		qa_check(endlessPerkCashPercent() == 100, label);

		/* Each ship flies only its own drive. */
		snprintf(label, sizeof(label),
		         "machine %d: P1 flies Turbodrive and not P2's Overblast", local + 1);
		qa_check((endlessPlayerMods[0] & (Uint64)ENDLESS_MOD_TURBODRIVE)
		         && !(endlessPlayerMods[0] & (Uint64)ENDLESS_MOD_OVERBLAST), label);
		snprintf(label, sizeof(label),
		         "machine %d: P2 flies Overblast and not P1's Turbodrive", local + 1);
		qa_check((endlessPlayerMods[1] & (Uint64)ENDLESS_MOD_OVERBLAST)
		         && !(endlessPlayerMods[1] & (Uint64)ENDLESS_MOD_TURBODRIVE), label);

		/* P1 does the shooting. Under Individual feed only P1's streak climbs. */
		for (int k = 0; k < 4; ++k)
			endlessCountKill(0, 0);
		snprintf(label, sizeof(label),
		         "machine %d: P1's kills feed only P1's streak", local + 1);
		qa_check(endlessComboKills[0] == 4 && endlessComboKills[1] == 0, label);

		/* P2 collects a pickup. Individual + Double Earnings pays double, to P2 alone. */
		player[0].cash = player[1].cash = 0;
		endlessCashResync();
		player_award_pickup_cash(&player[1], 500);
		snprintf(label, sizeof(label),
		         "machine %d: P2's pickup pays P2 double and P1 nothing", local + 1);
		qa_check((long)player[1].cash == 1000 && player[0].cash == 0, label);

		/* P1 has a revive token and takes a lethal hit: the token fires first, so P1 does
		 * not go down at all. */
		endlessReviveHeld[0] = true;
		player[0].armor = 1;
		const bool tokenSaved = endlessConsumeRevive(0);
		snprintf(label, sizeof(label),
		         "machine %d: P1's token fires before the DOWNED state, so P1 keeps flying",
		         local + 1);
		qa_check(tokenSaved && !endlessPlayerDowned[0], label);
		endlessReviveGraceReset();

		/* Now P1 goes down for real, with no token left. P2 is still flying, so the zone
		 * continues and the run is not over. */
		endlessPlayerDowned[0] = true;
		snprintf(label, sizeof(label),
		         "machine %d: P1 down with P2 alive keeps the zone running", local + 1);
		qa_check(endlessAnyPlayerFlying(), label);
		snprintf(label, sizeof(label),
		         "machine %d: Relaxed leaves the menu open for the survivor", local + 1);
		qa_check(!endlessDeathLocksMenu(), label);

		/* P2 finishes the zone. P1 revives at the outpost keeping everything it owned:
		 * hull tier, drive, spent-revive count. */
		endlessArmorBonus[0] = 16;
		endlessReviveDownedAtOutpost();
		snprintf(label, sizeof(label),
		         "machine %d: the survivor finishing the zone revives P1 at the outpost",
		         local + 1);
		qa_check(!endlessPlayerDowned[0] && player[0].is_alive, label);
		snprintf(label, sizeof(label),
		         "machine %d: P1 comes back with a full hull and no shield", local + 1);
		qa_check(player[0].armor == player[0].initial_armor && player[0].shield == 0, label);
		snprintf(label, sizeof(label),
		         "machine %d: P1 keeps the hull tier it paid for through the revive", local + 1);
		qa_check(endlessArmorBonus[0] == 16, label);
		snprintf(label, sizeof(label),
		         "machine %d: P1 keeps its own drive through the revive", local + 1);
		qa_check(endlessPurchasedMods[0] == (unsigned)ENDLESS_MOD_TURBODRIVE
		         && endlessPurchasedMods[1] == (unsigned)ENDLESS_MOD_OVERBLAST, label);
		snprintf(label, sizeof(label),
		         "machine %d: the spent revive still counts against P1", local + 1);
		qa_check(endlessRevivesUsed[0] == 1 && endlessRevivesUsed[1] == 0, label);
	}

	/* Both ships down in the same zone is the solo death rule, whatever the run mode: no new
	 * behaviour, just the one the mode already had. */
	for (int m = 0; m < 3; ++m)
	{
		static const EndlessRunMode modes[3] = {
			ENDLESS_RUNMODE_RELAXED, ENDLESS_RUNMODE_STANDARD, ENDLESS_RUNMODE_HARDCORE };
		static const char *const modeName[3] = { "Relaxed", "Standard", "Hardcore" };
		char label[192];

		qa_session(0);
		qa_clear_ships();
		endlessRunMode = modes[m];
		endlessPlayerDowned[0] = endlessPlayerDowned[1] = true;

		snprintf(label, sizeof(label),
		         "%s: both ships down leaves nobody flying", modeName[m]);
		qa_check(!endlessAnyPlayerFlying(), label);
		snprintf(label, sizeof(label),
		         "%s: both ships down %s the menu, exactly as a solo death would",
		         modeName[m], modes[m] == ENDLESS_RUNMODE_RELAXED ? "leaves open" : "locks");
		qa_check(endlessDeathLocksMenu() == (modes[m] != ENDLESS_RUNMODE_RELAXED), label);
		snprintf(label, sizeof(label),
		         "%s: hardcore is the only mode that refuses to save", modeName[m]);
		qa_check(endlessHardcore() == (modes[m] == ENDLESS_RUNMODE_HARDCORE), label);
	}

	/* The no-save rule is pinned in the data layer, not only in the menus: JE_saveGame and
	 * endlessSaveSlot both refuse mid-Hardcore-run and leave the slot exactly as it was.
	 * Driven for Hardcore only; the refusal returns before the config write, so the case is
	 * safe in-process, while a Standard-mode positive call would write the runner's real
	 * save file (the migration fixtures and wire scenario 7 cover that side). */
	{
		const JE_SaveFileType original = saveFiles[22 - 1];
		JE_SaveFileType marked;
		qa_session(0);
		qa_clear_ships();
		endlessRunMode = ENDLESS_RUNMODE_HARDCORE;
		endlessRunDepth = 9;
		player[0].cash = 123456;
		memset(&saveFiles[22 - 1], 0x5a, sizeof(saveFiles[22 - 1]));
		marked = saveFiles[22 - 1];
		JE_saveGame(22, "HARDCORE QA");
		endlessSaveSlot(22);
		qa_check(memcmp(&marked, &saveFiles[22 - 1], sizeof(marked)) == 0,
		         "Hardcore: a mid-run save attempt leaves the record untouched at the data level");
		saveFiles[22 - 1] = original;
	}

	/* A session where the host turned Shared credit on: both ships bank every kill and every
	 * pickup in full, so neither has to hang back, and Double Earnings stands down. */
	for (int local = 0; local <= 1; ++local)
	{
		char label[192];
		qa_session((uint)local);
		qa_clear_ships();
		qa_clear_ledger();
		coop_set_session_shared_credit(true);
		coop_set_session_double_earnings(true);

		player[0].cash = player[1].cash = 0;
		player_award_kill_cash(&player[local], 300);
		player_award_pickup_cash(&player[1 - local], 200);
		snprintf(label, sizeof(label),
		         "machine %d: Shared credit banks both the kill and the pickup for both ships",
		         local + 1);
		qa_check(player[0].cash == 500 && player[1].cash == 500, label);
		snprintf(label, sizeof(label),
		         "machine %d: Double Earnings does not double under Shared", local + 1);
		qa_check(!coop_earnings_are_doubled(), label);
	}

	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
	endlessRunMode = ENDLESS_RUNMODE_RELAXED;
	endlessRunDepth = 0;
}

/* ---- entry point -------------------------------------------------------------------- */

/* The flip/spotlight derivation, online and offline alike. Network games used to clear the
 * smoothie flags wholesale, which silently disabled Topsy Turvy, the scripted inverted-control
 * levels, and the light cone for every online session. */
static void qa_modifier_display_matrix(void)
{
	const JE_boolean savedInvert = smoothies[9 - 1];
	const JE_boolean savedCone = smoothies[6 - 1];
	const JE_byte savedCode = starShowVGASpecialCode;
	const Uint64 savedMods = endlessActiveMods;
	const JE_boolean savedNet = isNetworkGame;

	qa_session(0);
	for (int net = 0; net <= 1; ++net)
	{
		isNetworkGame = net != 0;
		const char *const where = net ? "online" : "solo";
		char label[128];

		endlessActiveMods = ENDLESS_MOD_TOPSY;
		smoothies[9 - 1] = false;
		smoothies[6 - 1] = false;
		JE_deriveStarShowSpecial();
		snprintf(label, sizeof(label), "Topsy Turvy flips the screen and controls %s", where);
		qa_check(starShowVGASpecialCode == 1 && smoothies[9 - 1], label);

		endlessActiveMods = 0;
		smoothies[9 - 1] = false;
		smoothies[6 - 1] = false;
		JE_deriveStarShowSpecial();
		snprintf(label, sizeof(label), "no flip without the modifier %s", where);
		qa_check(starShowVGASpecialCode == 0 && !smoothies[9 - 1], label);

		// A level whose own script inverted the controls keeps its flip.
		smoothies[9 - 1] = true;
		JE_deriveStarShowSpecial();
		snprintf(label, sizeof(label), "a scripted inverted-control level keeps its flip %s", where);
		qa_check(starShowVGASpecialCode == 1 && smoothies[9 - 1], label);
	}

	smoothies[9 - 1] = savedInvert;
	smoothies[6 - 1] = savedCone;
	starShowVGASpecialCode = savedCode;
	endlessActiveMods = savedMods;
	isNetworkGame = savedNet;
}

/* Which run modes offer the death prompt at all, driven through its real gates. Relaxed offers
 * the three-choice menu when a launch snapshot exists; Standard and Hardcore skip it and lock
 * the pause menu instead, so a fatal hit has no quiet exit there. */
static void qa_death_prompt_matrix(void)
{
	char label[128];
	const bool savedHave = endlessSortieHave;

	qa_session(0);
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		endlessRunMode = (EndlessRunMode)m;
		const bool relaxed = m == ENDLESS_RUNMODE_RELAXED;

		endlessSortieHave = true;
		snprintf(label, sizeof(label), "run mode %d %s the death prompt", m,
		         relaxed ? "offers" : "skips");
		qa_check(endlessDeathMenuDue() == relaxed, label);

		snprintf(label, sizeof(label), "run mode %d %s the pause menu after death", m,
		         relaxed ? "keeps" : "locks");
		qa_check(endlessDeathLocksMenu() == !relaxed, label);

		// Without a launch snapshot there is nothing to restart, whatever the mode.
		endlessSortieHave = false;
		snprintf(label, sizeof(label), "run mode %d skips the prompt with no snapshot", m);
		qa_check(!endlessDeathMenuDue(), label);
	}

	endlessSortieHave = savedHave;
}

/* Every gamble outcome, fired through the debug trigger from both machines. The assertions are
 * the invariants an outcome must never break whichever effect it rolls: wallets stay inside 32
 * bits, the combined perk holding stays within its caps, and nothing runs away. A broken clamp
 * in any outcome's effect shows up here as a wrapped or runaway value. */
static void qa_gamble_matrix(void)
{
	char label[160];

	for (uint slot = 0; slot < 2; ++slot)
	{
		qa_session(slot);
		qa_clear_ships();

		for (int id = 0; id < endlessGambleOutcomeCount(); ++id)
		{
			player[0].cash = player[1].cash = 100000;
			endlessCashResync();
			endlessForceGambleOutcome(id);

			bool sane = player[0].cash < 0x40000000u && player[1].cash < 0x40000000u
			         && player[slot].superbombs <= 10;
			endlessPerkRederive();
			for (int perk = 0; perk < endlessPerkCount() && sane; ++perk)
				sane = endlessPerkOwned[perk] <= endlessPerkMaxStack(perk);

			snprintf(label, sizeof(label),
			         "gamble outcome %d (%s) from machine %u keeps the run's invariants",
			         id, endlessGambleOutcomeName(id), slot + 1);
			qa_check(sane, label);
		}

		endlessPerkPending = false;
	}
}

void qa_test_endless_suite(void)
{
	QaEndlessEnv saved;
	qa_env_save(&saved);

	qa_economy_matrix();
	qa_gamble_matrix();
	qa_death_prompt_matrix();
	qa_test_endless_death_menu();
	qa_modifier_display_matrix();
	qa_drive_matrix();
	qa_perk_matrix();
	qa_reactive_state_matrix();
	qa_outpost_matrix();
	qa_eshop_matrix();
	qa_death_revive_matrix();
	qa_danger_target_matrix();
	qa_endless_difficulty_pinned();
	qa_chooser_matrix();
	qa_wire_matrix();
	qa_scenario_suite();

	qa_env_restore(&saved);
}
