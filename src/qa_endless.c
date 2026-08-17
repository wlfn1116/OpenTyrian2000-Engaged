/* Online Endless tests use real two-ship entry points for outposts, perks,
 * drives, death states, and restart behavior. */
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
#include <stdlib.h>
#include <string.h>

/* ---- session harness ---------------------------------------------------------------- */

/* Everything a case here is allowed to disturb. Saved once around the whole suite so a failure
 * mid-matrix cannot leak a half-built session into the tests that run after it. */
typedef struct
{
	JE_boolean endless, coopEndless, coopCampaign, twoPlayer, onePlayer;
	bool netGame, host, comboShared, hostCharts;
	uint playerNum;
	EndlessRunMode runMode;
	EndlessBaseRule baseRule;
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
	Sint64 rerollCost[2], entryCash[2], hullCost[2];
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
	e->baseRule = endlessRunBaseRule;
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
	endlessRunBaseRule = e->baseRule;
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
		endlessExtraPerksBought[p] = 0;
		endlessExtraPerksVisit[p] = 0;
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

/* Drive one Endless ram kill through the real destruction walk and report what it paid, counted and
 * left behind. Shaped like qa_chain_kill_row: `tiles` bodies worth `evalue` each, linked together
 * when `linknum` is nonzero, at `eliteState` when that is a tier. */
static void qa_ram_kill_row(int killer, int evalue, int tiles, JE_byte linknum, int eliteState,
                            long *out_paid0, long *out_paid1, int *out_killed, bool *out_dropped)
{
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;   // an empty field, so anything live afterwards came out of a death
	}

	for (int i = 0; i < tiles; ++i)
	{
		enemyAvail[i] = 0;
		enemy[i].ex = (JE_integer)(100 + i * 8);
		enemy[i].ey = 100;
		enemy[i].armorleft = 1;
		enemy[i].enemytype = 1;
		enemy[i].evalue = (Sint16)evalue;
		enemy[i].enemydie = 1;   // any spawnable body; the test only asks whether one appeared
		enemy[i].linknum = linknum;
		enemy[i].eliteState = (JE_byte)((eliteState >= 2) ? eliteState : 0);
	}

	const Sint64 before0 = player[0].cash;
	const Sint64 before1 = player[1].cash;
	const JE_word killedBefore = enemyKilled;

	enemy_kill_group(0, killer, killer);

	*out_paid0 = (long)(player[0].cash - before0);
	*out_paid1 = (long)(player[1].cash - before1);
	*out_killed = (int)(enemyKilled - killedBefore);

	*out_dropped = false;
	for (uint i = 0; i < COUNTOF(enemy); ++i)
		if (enemyAvail[i] != 1)
			*out_dropped = true;   // the row is all dead by now, so anything live is a drop
}

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

	/* Chain Reaction: every stack widens the blast, and the queued pulse is measured against the
	 * stacks of the ship that made the kill, not whichever ship the effect context last named. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_CHAINRXN, 1);
	endlessPerkGrant(1, PERK_CHAINRXN, 3);
	for (int stacks = 0; stacks <= 3; ++stacks)
	{
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		endlessPerkGrant(0, PERK_CHAINRXN, (JE_byte)stacks);
		endlessSetFxPlayer(0);
		const int want = (stacks == 0)
		               ? 0
		               : ENDLESS_PERK_CHAIN_RADIUS + (stacks - 1) * ENDLESS_PERK_CHAIN_REACH;
		char reachLabel[96];
		snprintf(reachLabel, sizeof(reachLabel), "Chain Reaction at %d stacks blasts %d px",
		         stacks, want);
		qa_check(endlessPerkChainRadius() == want, reachLabel);

		/* Damage is the stacks against the ship's own damage scale, which is 100% here. */
		const int wantDmg = stacks * ENDLESS_PERK_CHAIN_DMG;
		snprintf(reachLabel, sizeof(reachLabel), "...and hits for %d, %d per stack unscaled",
		         wantDmg, ENDLESS_PERK_CHAIN_DMG);
		qa_check(endlessPerkChainDamage(false) == (stacks == 0 ? 0 : wantDmg), reachLabel);
	}

	/* The pulse rides the owner's damage scale, so a damage build deepens it and the partner's
	 * build never does. Heavy Rounds on one ship only, both holding the same chain stacks, and no
	 * drives on either: the figures below are derived from the two constants, not from the function
	 * under test re-run. */
	const unsigned savedScaleMods[2] = { endlessPlayerMods[0], endlessPlayerMods[1] };
	endlessPlayerMods[0] = 0;
	endlessPlayerMods[1] = 0;

	const int chainStacks = 2;
	const int heavyRounds = 3;

	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_CHAINRXN, (JE_byte)chainStacks);
	endlessPerkGrant(1, PERK_CHAINRXN, (JE_byte)chainStacks);
	endlessSetFxPlayer(0);
	const int chainPlain = endlessPerkChainDamage(false);

	endlessPerkGrant(0, PERK_DAMAGE, (JE_byte)heavyRounds);
	endlessSetFxPlayer(0);
	const int chainArmed = endlessPerkChainDamage(false);
	const int chainSalvo = endlessPerkChainDamage(true);
	endlessSetFxPlayer(1);
	const int chainPartner = endlessPerkChainDamage(false);
	endlessSetFxPlayer(0);

	const int wantPlain = chainStacks * ENDLESS_PERK_CHAIN_DMG;
	const int wantArmed = wantPlain * (100 + heavyRounds * ENDLESS_PERK_DAMAGE_PCT) / 100;

	char scaleLabel[128];
	snprintf(scaleLabel, sizeof(scaleLabel), "an unbuilt pulse hits for %d, %d a stack",
	         chainPlain, ENDLESS_PERK_CHAIN_DMG);
	qa_check(chainPlain == wantPlain, scaleLabel);

	snprintf(scaleLabel, sizeof(scaleLabel), "%d Heavy Rounds deepen it to %d, wanted %d",
	         heavyRounds, chainArmed, wantArmed);
	qa_check(chainArmed == wantArmed && chainArmed > chainPlain, scaleLabel);

	snprintf(scaleLabel, sizeof(scaleLabel), "...and leaves the partner's at %d, its own scale",
	         chainPartner);
	qa_check(chainPartner == chainPlain, scaleLabel);

	/* Opening Salvo bumps a pulse struck inside its window, the same points it adds to a tagged
	 * round, and only for the ship whose window it is. */
	const int wantSalvo = wantPlain * (100 + heavyRounds * ENDLESS_PERK_DAMAGE_PCT
	                                   + ENDLESS_PERK_SALVO_DMG_PCT) / 100;
	snprintf(scaleLabel, sizeof(scaleLabel), "a salvo pulse hits for %d against the same build's %d",
	         chainSalvo, chainArmed);
	qa_check(chainSalvo == wantSalvo && chainSalvo > chainArmed, scaleLabel);

	/* End to end through the real drain, which is where the owner's scale actually has to be read:
	 * the same two ships, the same pulse, against a hull tough enough to survive and report it. */
	const int dealtArmed = qa_chain_pulse_damage(0, false);
	const int dealtPartner = qa_chain_pulse_damage(1, false);
	snprintf(scaleLabel, sizeof(scaleLabel),
	         "a pulse P1 owns lands its own %d, P2's lands %d", dealtArmed, dealtPartner);
	qa_check(dealtArmed == chainArmed && dealtPartner == chainPartner, scaleLabel);

	const int dealtSalvo = qa_chain_pulse_damage(0, true);
	const int dealtSalvoP2 = qa_chain_pulse_damage(1, true);
	snprintf(scaleLabel, sizeof(scaleLabel),
	         "the tag reaches the drain: P1's salvo pulse lands %d, P2's %d", dealtSalvo, dealtSalvoP2);
	qa_check(dealtSalvo == chainSalvo && dealtSalvoP2 > dealtPartner, scaleLabel);

	for (int owner = 0; owner <= 1; ++owner)
	{
		snprintf(scaleLabel, sizeof(scaleLabel),
		         "P%d's wave keeps its salvo bump for every hop, after the window would have lapsed",
		         owner + 1);
		qa_check(qa_chain_salvo_latch_holds(owner), scaleLabel);
	}

	endlessPlayerMods[0] = savedScaleMods[0];
	endlessPlayerMods[1] = savedScaleMods[1];

	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_CHAINRXN, 1);
	endlessPerkGrant(1, PERK_CHAINRXN, 3);
	qa_check(endlessPerkChainOwner(0) == 0 && endlessPerkChainOwner(1) == 1,
	         "a pulse belongs to the ship that fired the killing shot");
	qa_check(endlessPerkChainOwner(ENDLESS_KILLER_NONE) == 1,
	         "a kill neither ship can be credited with pulses at the wider holding");
	endlessSetFxPlayer(1);   // the partner acting last must not lend P1 its reach
	qa_check(endlessPerkChainOwner(0) == 0, "...and the effect context does not decide ownership");
	endlessSetFxPlayer(endlessPerkChainOwner(0));
	qa_check(endlessPerkChainRadius() == ENDLESS_PERK_CHAIN_RADIUS,
	         "P1's kill blasts at P1's one-stack radius");
	endlessSetFxPlayer(endlessPerkChainOwner(1));
	qa_check(endlessPerkChainRadius() == ENDLESS_PERK_CHAIN_RADIUS + 2 * ENDLESS_PERK_CHAIN_REACH,
	         "...and P2's kill at P2's three-stack radius");
	endlessSetFxPlayer(0);

	coopEndlessMode = false;
	qa_check(endlessPerkChainOwner(1) == 0 && endlessPerkChainOwner(ENDLESS_KILLER_NONE) == 0,
	         "one ship flying alone owns every pulse");
	qa_test_chain_cascade();   // the queue and the drain, with P1's one stack in the effect context
	qa_test_chain_wave_latch();
	coopEndlessMode = true;

	/* A wave's kills are worth what the ship that made them would have earned by shooting. The
	 * lobby's Credit rule decides which wallets that reaches, Combo Feed decides whose streak it
	 * feeds, and the drop is owed whoever made it. Both ships hold the perk, so a pulse owned by
	 * either has a blast of its own to be measured against. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_CHAINRXN, 1);
	endlessPerkGrant(1, PERK_CHAINRXN, 1);

	const bool savedCombo = endlessCoopComboShared;
	const Sint64 savedCash[2] = { player[0].cash, player[1].cash };
	coop_set_session_double_earnings(false);   // it would scale every figure below

	const int worth = 500;
	char label[160];

	for (int shared = 0; shared <= 1; ++shared)
	{
		coop_set_session_shared_credit(shared != 0);

		/* Both shapes a wave can destroy: loose fodder it walks through hop by hop, and one linked
		 * hull it takes down whole, which is what a boss is. Their payouts follow the same rule. */
		static const struct { const char *shape; JE_byte linknum; } shapes[] = {
			{ "wave",        0 },
			{ "linked hull", 9 },
		};

		for (uint s = 0; s < COUNTOF(shapes); ++s)
		for (int owner = 0; owner <= 1; ++owner)
		{
			/* Four of them, so the answer covers everything the blast destroys and not just the
			 * enemy the first pulse reached. */
			long paid0 = 0, paid1 = 0;
			int killed = 0;
			bool dropped = false;
			qa_chain_kill_row(owner, worth, 4, shapes[s].linknum, 0,
			                  &paid0, &paid1, &killed, &dropped);

			const long mine = (owner == 0) ? paid0 : paid1;
			const long theirs = (owner == 0) ? paid1 : paid0;

			/* A drop can be worth something of its own, so the take is only bounded below by the
			 * four it cleared. What it may not do is leave any of that take unpaid. */
			snprintf(label, sizeof(label),
			         "%s credit pays P%d its whole %s: %d kills, %ld for four worth %d",
			         shared ? "Shared" : "Individual", owner + 1, shapes[s].shape, killed, mine,
			         4 * worth);
			qa_check(killed >= 4 && mine >= 4 * worth, label);

			snprintf(label, sizeof(label), "...and the partner takes %ld of P%d's %ld",
			         theirs, owner + 1, mine);
			qa_check(theirs == (shared ? mine : 0), label);

			snprintf(label, sizeof(label), "...and P%d's %s still leaves the drops behind",
			         owner + 1, shapes[s].shape);
			qa_check(dropped, label);
		}
	}

	/* An elite or a champion the wave destroys owes its bounty on top of its value, and it owes it
	 * to the ship whose blast killed it. The banner beside the figure names that ship online, from
	 * the same `killer` this checks the payment against. */
	static const struct { const char *tier; int state; } tiers[] = {
		{ "elite",    2 },
		{ "champion", 3 },
	};

	for (uint t = 0; t < COUNTOF(tiers); ++t)
	for (int owner = 0; owner <= 1; ++owner)
	{
		coop_set_session_shared_credit(false);   // Individual, so the wallets answer separately

		long paid0 = 0, paid1 = 0;
		int killed = 0;
		bool dropped = false;
		qa_chain_kill_row(owner, worth, 1, 0, tiers[t].state, &paid0, &paid1, &killed, &dropped);

		const long mine = (owner == 0) ? paid0 : paid1;
		const long theirs = (owner == 0) ? paid1 : paid0;

		snprintf(label, sizeof(label),
		         "a wave that kills an %s pays P%d %ld, its value and its bounty",
		         tiers[t].tier, owner + 1, mine);
		qa_check(killed >= 1 && mine > worth, label);

		snprintf(label, sizeof(label), "...and none of that %s bounty reaches the partner, who took %ld",
		         tiers[t].tier, theirs);
		qa_check(theirs == 0, label);
	}

	/* A champion is the dearer tier, so the two figures have to differ. Equal ones would mean the
	 * blast pays a flat bounty and the tier premium is going unread. */
	{
		long elitePaid = 0, elitePartner = 0;
		long champPaid = 0, champPartner = 0;
		int killed = 0;
		bool dropped = false;
		coop_set_session_shared_credit(false);
		qa_chain_kill_row(0, worth, 1, 0, 2, &elitePaid, &elitePartner, &killed, &dropped);
		qa_chain_kill_row(0, worth, 1, 0, 3, &champPaid, &champPartner, &killed, &dropped);
		snprintf(label, sizeof(label), "a champion pays more than an elite, %ld against %ld",
		         champPaid, elitePaid);
		qa_check(champPaid > elitePaid, label);
	}

	/* A streak only advances for a ship flying a kill-fire drive, so both need one before Combo
	 * Feed has anything to divide. */
	const unsigned savedMods[2] = { endlessPlayerMods[0], endlessPlayerMods[1] };
	endlessPlayerMods[0] = endlessPlayerMods[1] = (unsigned)ENDLESS_MOD_TURBODRIVE;

	coop_set_session_shared_credit(false);   // one wallet at a time, so a streak is the only variable
	for (int sharedCombo = 0; sharedCombo <= 1; ++sharedCombo)
	{
		endlessCoopComboShared = (sharedCombo != 0);

		for (int owner = 0; owner <= 1; ++owner)
		{
			long paid0;
			long paid1;
			int killed = 0;
			bool dropped;
			memset(endlessComboKills, 0, sizeof(endlessComboKills));
			qa_chain_kill_row(owner, worth, 4, 0, 0, &paid0, &paid1, &killed, &dropped);

			const int mine = endlessComboKills[owner], theirs = endlessComboKills[1 - owner];
			snprintf(label, sizeof(label),
			         "%s Combo Feed gives P%d's wave of %d a streak of %d, the partner %d",
			         sharedCombo ? "Shared" : "Individual", owner + 1, killed, mine, theirs);
			qa_check(killed >= 4 && mine == killed && theirs == (sharedCombo ? killed : 0), label);
		}
	}

	endlessPlayerMods[0] = savedMods[0];
	endlessPlayerMods[1] = savedMods[1];
	endlessCoopComboShared = savedCombo;
	coop_set_session_shared_credit(true);
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	player[0].cash = savedCash[0];
	player[1].cash = savedCash[1];
	qa_clear_ships();

	/* A ram kill is a kill, so it owes the same wallets a shot does: the lobby's Credit rule decides
	 * which of them, and the ram site names the rammer as both payee and killer. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));

	for (int shared = 0; shared <= 1; ++shared)
	{
		coop_set_session_shared_credit(shared != 0);

		static const struct { const char *shape; JE_byte linknum; int tiles; } rams[] = {
			{ "lone enemy", 0, 1 },
			{ "linked hull", 9, 3 },
		};

		for (uint s = 0; s < COUNTOF(rams); ++s)
		for (int killer = 0; killer <= 1; ++killer)
		{
			long paid0 = 0, paid1 = 0;
			int killed = 0;
			bool dropped = false;
			qa_ram_kill_row(killer, worth, rams[s].tiles, rams[s].linknum, 0,
			                &paid0, &paid1, &killed, &dropped);

			const long mine = (killer == 0) ? paid0 : paid1;
			const long theirs = (killer == 0) ? paid1 : paid0;

			snprintf(label, sizeof(label),
			         "%s credit pays P%d for ramming a %s: %d kills, %ld for %d worth %d",
			         shared ? "Shared" : "Individual", killer + 1, rams[s].shape, killed, mine,
			         rams[s].tiles, worth);
			qa_check(killed >= 1 && mine >= (long)rams[s].tiles * worth, label);

			snprintf(label, sizeof(label), "...and the partner takes %ld of P%d's %ld",
			         theirs, killer + 1, mine);
			qa_check(theirs == (shared ? mine : 0), label);

			snprintf(label, sizeof(label), "...and P%d's ram still leaves the drop behind",
			         killer + 1);
			qa_check(dropped, label);
		}
	}

	/* An elite a ram destroys owes its bounty, and owes it to the ship that rammed it. This is what
	 * separates an Endless ram from the vanilla one, which removed the enemy and paid nothing. */
	for (int killer = 0; killer <= 1; ++killer)
	{
		coop_set_session_shared_credit(false);

		long plain0 = 0, plain1 = 0, elite0 = 0, elite1 = 0;
		int killed = 0;
		bool dropped = false;
		qa_ram_kill_row(killer, worth, 1, 0, 0, &plain0, &plain1, &killed, &dropped);
		qa_ram_kill_row(killer, worth, 1, 0, 2, &elite0, &elite1, &killed, &dropped);

		const long plain = (killer == 0) ? plain0 : plain1;
		const long elite = (killer == 0) ? elite0 : elite1;
		const long partner = (killer == 0) ? elite1 : elite0;

		snprintf(label, sizeof(label), "P%d ramming an elite is paid %ld over the %ld a plain kill pays",
		         killer + 1, elite, plain);
		qa_check(elite > plain, label);

		snprintf(label, sizeof(label), "...and none of that bounty reaches the partner, who took %ld",
		         partner);
		qa_check(partner == 0, label);
	}

	coop_set_session_shared_credit(true);
	player[0].cash = savedCash[0];
	player[1].cash = savedCash[1];
	qa_clear_ships();

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
		const Sint64 priceBefore = endlessRerollPrice();
		const Sint64 partnerBefore = endlessRerollCost[them];
		player[me].cash = priceBefore * 4;
		player[them].cash = priceBefore * 4;
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

		/* The queue is the pair's, because the strips come off the sector both ships fly. A partner
		 * who has bought none is still locked out once the cap is met, so neither pays for a strip
		 * the launch pass would clamp away. */
		endlessCleanseChargeCount[me] = ENDLESS_CLEANSE_MAX_CHARGES - 1;
		endlessCleanseChargeCount[them] = 1;
		snprintf(label, sizeof(label),
		         "machine %d: sabotage counts the partner's charges towards the cap", local + 1);
		qa_check(endlessCleanseCharges() == ENDLESS_CLEANSE_MAX_CHARGES
		         && endlessCleanseMaxed() && !endlessTryBuyCleanse(), label);

		endlessCleanseChargeCount[me] = 0;
		endlessCleanseChargeCount[them] = 1;
		snprintf(label, sizeof(label),
		         "machine %d: a partner's charge still leaves room under the cap", local + 1);
		qa_check(endlessCleanseCharges() == 1 && !endlessCleanseMaxed() && endlessTryBuyCleanse()
		         && endlessCleanseChargeCount[me] == 1, label);
		endlessCleanseChargeCount[me] = 0;
		endlessCleanseChargeCount[them] = 0;

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

		/* --- extra perk --- */
		qa_session(me);
		qa_clear_ships();
		qa_clear_ledger();
		endlessRunDepth = 20;

		/* The ladder is depth alone until an outpost has sold one: free picks and a fat wallet
		 * leave it where it is. */
		const Sint64 ladder = 70000 + 2500 * 20;
		player[me].cash = 99999999;
		endlessCashResync();
		endlessResetShopPrices();
		snprintf(label, sizeof(label), "machine %d: a first perk is the depth price", local + 1);
		qa_check(endlessExtraPerkPrice() == ladder, label);

		endlessPerkGrant(me, PERK_DAMAGE, 5);
		endlessPerkGrant(me, PERK_ARMOR, 5);
		snprintf(label, sizeof(label),
		         "machine %d: perks taken from free picks do not raise the price", local + 1);
		qa_check(endlessExtraPerkPrice() == ladder, label);

		/* The surcharge is quadratic in perks bought: 1.00, 1.20, 1.45, 1.75, 2.10, 2.50. */
		static const int wantPct[6] = { 100, 120, 145, 175, 210, 250 };
		bool curveHolds = true;
		for (int n = 0; n < 6; ++n)
		{
			endlessExtraPerksBought[me] = n;
			if (endlessExtraPerkPrice() != ladder * wantPct[n] / 100)
				curveHolds = false;
		}
		snprintf(label, sizeof(label),
		         "machine %d: each bought perk widens the surcharge step", local + 1);
		qa_check(curveHolds, label);

		/* A partner's spending is their own: the count is personal. */
		endlessExtraPerksBought[me] = 0;
		endlessExtraPerksBought[them] = 5;
		snprintf(label, sizeof(label),
		         "machine %d: the partner's purchases do not price this pick", local + 1);
		qa_check(endlessExtraPerkPrice() == ladder, label);
		endlessExtraPerksBought[them] = 0;

		/* One outpost sells two: the second at the repeat multiple of the price it has just
		 * risen to, and the third is not offered at any wallet. */
		qa_clear_ships();
		player[me].cash = 99999999;
		endlessCashResync();
		endlessResetShopPrices();
		snprintf(label, sizeof(label), "machine %d: the first perk of a visit sells", local + 1);
		qa_check(endlessTryBuyExtraPerk() && endlessExtraPerksBought[me] == 1, label);

		const Sint64 second = ladder * wantPct[1] / 100 * ENDLESS_PERK_VISIT_REPEAT_PCT / 100;
		snprintf(label, sizeof(label),
		         "machine %d: the second is the repeat multiple of the risen price", local + 1);
		qa_check(!endlessExtraPerkMaxed() && endlessExtraPerkPrice() == second, label);
		snprintf(label, sizeof(label), "machine %d: the second perk of a visit sells", local + 1);
		qa_check(endlessTryBuyExtraPerk() && endlessExtraPerksBought[me] == 2, label);

		snprintf(label, sizeof(label),
		         "machine %d: no outpost sells a third perk, however rich", local + 1);
		qa_check(endlessExtraPerkMaxed() && !endlessTryBuyExtraPerk()
		         && endlessExtraPerksBought[me] == 2, label);
		snprintf(label, sizeof(label),
		         "machine %d: the perks were charged to the buyer alone", local + 1);
		qa_check(player[me].cash < 99999999 && player[them].cash == 0
		         && endlessExtraPerksBought[them] == 0, label);

		/* The visit counter clears at the next outpost; the run total does not. */
		endlessResetShopPrices();
		snprintf(label, sizeof(label),
		         "machine %d: the next outpost sells again at the run's own price", local + 1);
		qa_check(!endlessExtraPerkMaxed()
		         && endlessExtraPerkPrice() == ladder * wantPct[2] / 100, label);

		qa_clear_ledger();
		qa_clear_ships();
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

/* ---- 6c. piercing rounds answer the damage levers ----------------------------------- */

/* What one bullet lands over a run of ticks, carrying its remainder the way the hit site does. */
static int qa_pierce_run(int rawDamage, int dmgPct, int ticks)
{
	JE_byte carry = 0;
	int total = 0;
	for (int tick = 0; tick < ticks; ++tick)
		total += endlessPierceHitDamage(rawDamage, dmgPct, &carry);
	return total;
}

/* Piercing damage is 1 a hit in the weapon table, the one quantity a percentage lever cannot reach
 * in whole points. The rule holds the run's percentage over a bullet's life instead, so every Heavy
 * Rounds stack has to move what the bullet lands. */
static void qa_pierce_damage_matrix(void)
{
	char label[192];
	const JE_boolean savedEndless = endlessMode;
	const JE_boolean savedMods = endlessCampaignMods;
	const int savedDepth = endlessRunDepth;
	const JE_shortint savedDifficulty = difficultyLevel;

	endlessMode = false;
	endlessCampaignMods = false;
	qa_check(endlessPiercePotencyPercent() == 100 && qa_pierce_run(1, 100, 100) == 100,
	         "outside the Endless effect layer a piercing round keeps its weapon-table damage");

	endlessMode = true;
	difficultyLevel = DIFFICULTY_NORMAL;

	int lastPotency = 0;
	for (int zone = 1; zone <= 200; ++zone)
	{
		endlessRunDepth = zone - 1;
		const int potency = endlessPiercePotencyPercent();

		snprintf(label, sizeof(label), "zone %d: pierce potency rises and stays bounded", zone);
		qa_check(potency >= 100 && potency <= endlessScalingOverrideMax(ESO_PIERCEDMG)
		         && potency >= lastPotency, label);
		lastPotency = potency;

		/* A round with no damage of its own stays that way however the levers are set. */
		snprintf(label, sizeof(label), "zone %d: a 0-damage round is never scaled up", zone);
		qa_check(qa_pierce_run(0, 400, 50) == 0, label);

		/* The carry is what makes the percentage exact over a bullet's life. */
		snprintf(label, sizeof(label), "zone %d: 100 ticks land the full percentage owed", zone);
		qa_check(qa_pierce_run(1, 137, 100) == 137 * potency / 100, label);

		/* A stack the player paid for that lands the same damage as the one before it is a dead
		 * purchase, so the whole ladder has to be strictly increasing. */
		bool climbs = true;
		int previous = qa_pierce_run(1, 100, 100);
		for (int stack = 1; stack <= endlessPerkMaxStack(PERK_DAMAGE); ++stack)
		{
			const int landed = qa_pierce_run(1, 100 + stack * ENDLESS_PERK_DAMAGE_PCT, 100);
			climbs = climbs && landed > previous;
			previous = landed;
		}
		snprintf(label, sizeof(label), "zone %d: every Heavy Rounds stack moves the damage", zone);
		qa_check(climbs, label);
	}

	endlessMode = savedEndless;
	endlessCampaignMods = savedMods;
	endlessRunDepth = savedDepth;
	difficultyLevel = savedDifficulty;
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

	/* The turn counts sectors flown. A sector the pair bails out of is re-charted by the same
	 * player, so the launch snapshot has to reopen the outpost on the turn that charted it. */
	{
		const bool savedHave = endlessSortieHave, savedCustom = endlessRunUsedCustom;
		const JE_byte savedOutpostEp = endlessSortieOutpostEp;
		const Uint64 savedOutpostMods = endlessSortieOutpostMods;

		qa_session(0);
		qa_clear_ships();
		endlessCourseChooser = ENDLESS_PICK_ALTERNATE;
		endlessCoopHostCharts = true;
		endlessRunMode = ENDLESS_RUNMODE_RELAXED;   // a run mode whose bail reopens the outpost
		endlessRunUsedCustom = false;               // keep the clear below off the record tables
		endlessSortieOutpostEp = 0;                 // no episode reload inside the restore
		endlessSortieOutpostMods = 0;

		endlessCaptureSortie();        // the host charted this sector and launched into it
		endlessCoopHostCharts = false; // whatever the level leaves behind must not survive the bail

		endlessRestoreSortie();   // died, or gave the sector up: back to the same outpost
		qa_check(endlessCoopHostCharts && endlessChartSeat == networkHostPlayerNum - 1u,
		         "a bailed sector reopens the outpost on the turn that charted it");

		endlessOnSectorCleared();
		qa_check(!endlessCoopHostCharts,
		         "...and flying a sector to its end is what passes the turn");

		endlessSortieOutpostMods = savedOutpostMods;
		endlessSortieOutpostEp = savedOutpostEp;
		endlessRunUsedCustom = savedCustom;
		endlessSortieHave = savedHave;
		endlessLockedSortie = false;
		endlessResumeVisit = false;
	}

	endlessRunDepth = 0;
	endlessCourseChooser = ENDLESS_PICK_HOST;
	endlessCoopHostCharts = true;
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
		endlessExtraPerksBought[0] = variant * 3;
		endlessExtraPerksVisit[0]  = variant % (ENDLESS_PERK_VISIT_MAX + 1);
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
		         && endlessExtraPerksBought[1] == endlessExtraPerksBought[0]
		         && endlessExtraPerksVisit[1] == endlessExtraPerksVisit[0]
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

	/* The disconnect chain, end to end on one machine: the outpost checkpoint writes slot 22, the
	 * dropped session reverts to that backup and saves it into a slot of its own, and the host that
	 * later resumes it finds an Endless slot whose run and record are the ones it checkpointed.
	 * Wire scenarios 7 and 21 fly the two-process half. */
	{
		JE_SaveFileType savedSlots[2] = { saveFiles[22 - 1], saveFiles[15 - 1] };
		const NetworkGameType savedType = network_game_type;

		qa_session(0);
		qa_clear_ships();
		endlessRunMode = ENDLESS_RUNMODE_STANDARD;
		endlessRunDepth = 61;
		endlessRunKills = 4242;
		endlessArmorBonus[0] = 18;
		player[0].cash = 5000000000LL;   // past a 32-bit wallet, so a narrowing on the way out shows
		endlessCashResync();
		JE_saveGame(22, "LAST LEVEL    ");   // the outpost auto-checkpoint

		/* The disconnect prompt: revert to the pre-level backup, then save it where the player
		 * chose. JE_loadGameRecord clears endlessMode, so the run has to be read back first. */
		const bool wasEndless = endlessMode;
		JE_loadGameRecord(&saveFiles[22 - 1], true);
		qa_check(wasEndless && !endlessMode,
		         "loading the backup record clears the mode flag the re-save depends on");
		qa_check(endlessLoadSlot(22) && endlessMode && endlessRunDepth == 61
		         && endlessRunKills == 4242 && endlessArmorBonus[0] == 18,
		         "the checkpointed run comes back off the backup slot");
		JE_saveGame(15, "DISCONNECTED  ");

		qa_check(endlessSlotHasRun(15) && saveFiles[15 - 1].level != 0
		         && saveFiles[15 - 1].score == 5000000000LL,
		         "the disconnect save carries the run and its 64-bit wallet into the chosen slot");

		// What the host's Load Game shows: an Endless lobby takes it, a Campaign lobby does not.
		network_game_type = NETWORK_GAME_ENDLESS;
		const bool endlessTakes = save_type_compatible(&saveFiles[15 - 1], 15, true);
		network_game_type = NETWORK_GAME_CAMPAIGN;
		const bool campaignRefuses = !save_type_compatible(&saveFiles[15 - 1], 15, true);
		qa_check(endlessTakes && campaignRefuses,
		         "an Endless disconnect save is offered to an Endless lobby and withheld from Campaign");

		// ...and hosting it: the record packs for PACKET_DETAILS and the run serializes for transfer.
		Uint8 packed[SAVE_RECORD_PACKED_SIZE];
		JE_SaveFileType wired;
		save_record_pack(packed, &saveFiles[15 - 1]);
		save_record_unpack(&wired, packed);
		qa_check(wired.score == 5000000000LL && save_record_is_coop(&wired),
		         "the resume record reaches the joiner with its wallet and co-op tag");

		qa_check(endlessLoadSlot(15) && endlessRunDepth == 61 && endlessRunKills == 4242,
		         "the host loads that slot back into the run it saved");
		Uint8 *const stream = malloc(ENDLESS_RUN_WIRE_MAX);
		const size_t streamLen = stream ? endlessRunSerialize(stream, ENDLESS_RUN_WIRE_MAX) : 0;
		endlessRunDepth = 0;
		endlessRunKills = 0;
		qa_check(streamLen > 0 && endlessRunAdopt(stream, streamLen)
		         && endlessRunDepth == 61 && endlessRunKills == 4242,
		         "...and the joiner adopts the same run off the wire");
		free(stream);

		network_game_type = savedType;
		saveFiles[22 - 1] = savedSlots[0];
		saveFiles[15 - 1] = savedSlots[1];
	}

	/* The two extra-perk counters ride the save. The run total is what prices the next pick, and
	 * the visit count has to come back too, or reloading an outpost would sell its limit again. */
	{
		const JE_SaveFileType original = saveFiles[22 - 1];

		qa_session(0);
		qa_clear_ships();
		endlessRunMode = ENDLESS_RUNMODE_STANDARD;
		endlessRunDepth = 30;
		player[0].cash = 99999999;
		endlessCashResync();
		endlessResetShopPrices();

		qa_check(endlessTryBuyExtraPerk() && endlessTryBuyExtraPerk() && endlessExtraPerkMaxed(),
		         "the outpost sells its two perks before the save");
		endlessExtraPerksBought[1] = 5;   // the partner is further along a ladder of their own
		const Sint64 pricedAt = endlessExtraPerkPrice();
		JE_saveGame(22, "PERK COUNTS   ");

		endlessExtraPerksBought[0] = endlessExtraPerksBought[1] = 0;
		endlessExtraPerksVisit[0] = 0;
		qa_check(endlessLoadSlot(22) && endlessExtraPerksBought[0] == 2
		         && endlessExtraPerksBought[1] == 5,
		         "both players' bought-perk totals come back off the slot");
		qa_check(endlessExtraPerksVisit[0] == ENDLESS_PERK_VISIT_MAX && endlessExtraPerkMaxed()
		         && !endlessTryBuyExtraPerk() && endlessExtraPerksBought[0] == 2,
		         "a reloaded outpost is still sold out, so saving cannot buy a third perk");
		qa_check(endlessExtraPerkPrice() == pricedAt,
		         "the restored total quotes the price the run was saved at");

		/* A corrupt count must not come back and overflow the quadratic surcharge. */
		endlessExtraPerksBought[0] = 999999999;
		endlessExtraPerksBought[1] = -7;
		JE_saveGame(22, "PERK COUNTS   ");
		qa_check(endlessLoadSlot(22) && endlessExtraPerksBought[0] == ENDLESS_PERK_PAID_MAX
		         && endlessExtraPerksBought[1] == 0 && endlessExtraPerkPrice() > 0,
		         "a corrupt bought-perk count clamps on load and still quotes a price");

		endlessExtraPerksBought[0] = endlessExtraPerksBought[1] = 0;
		endlessExtraPerksVisit[0] = endlessExtraPerksVisit[1] = 0;
		saveFiles[22 - 1] = original;
		qa_clear_ships();
	}

	/* Verify both save APIs reject mid-run Hardcore without changing the slot. The
	 * positive path is covered by migration fixtures and wire scenario 7. */
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

/* A save stores both halves of the outpost: the partner's rows and stream ride the save
 * acknowledgement into the saver's own record, and a wire adopt hands the other seat its
 * half back. */
static void qa_resume_partner_matrix(void)
{
	char seedSaved[COUNTOF(endlessRunSeed)];
	memcpy(seedSaved, endlessRunSeed, sizeof(seedSaved));
	JE_byte availSaved[9][10], availMaxSaved[9];
	memcpy(availSaved, itemAvail, sizeof(availSaved));
	memcpy(availMaxSaved, itemAvailMax, sizeof(availMaxSaved));

	// The saver's machine: seat one flies it, and seat two's half arrived on the save
	// acknowledgement, packed the way its machine packs it.
	qa_session(0);
	qa_clear_ships();
	endlessRunMode = ENDLESS_RUNMODE_RELAXED;
	endlessSetSeed("QA HALVES");
	endlessRunDepth = 7;

	memset(itemAvail, 0, sizeof(itemAvail));
	memset(itemAvailMax, 0, sizeof(itemAvailMax));
	itemAvail[0][0] = 5;   // the saver's own row, which must never reach the other seat
	itemAvailMax[0] = 1;

	Uint8 block[ENDLESS_OUTPOST_BLOCK_SIZE];
	memset(block, 0, sizeof(block));
	block[0] = 2;                    // availMax[0]
	block[9] = 9;                    // avail[0][0]
	block[10] = 12;                  // avail[0][1]
	block[9 + 90 + 6] = 0x12;        // stream, big end first
	block[9 + 90 + 7] = 0x34;
	endlessPartnerOutpostStash(1, block);

	Uint8 *const wire = malloc(ENDLESS_RUN_WIRE_MAX);   // off the stack: the text record is kilobytes
	const size_t wireLen = wire ? endlessRunSerialize(wire, ENDLESS_RUN_WIRE_MAX) : 0;
	qa_check(wireLen > 0, "a run with a stashed partner half serializes");

	// The other machine: seat two adopts and gets its own half back, rows and stream.
	qa_session(1);
	memset(itemAvail, 0x77, sizeof(itemAvail));
	endlessPlayerRngState[1] = 1;
	qa_check(wire != NULL && endlessRunAdopt(wire, wireLen)
	         && itemAvail[0][0] == 9 && itemAvail[0][1] == 12 && itemAvailMax[0] == 2
	         && endlessPlayerRngState[1] == 0x1234u,
	         "the adopted record hands seat two its own half of the outpost");

	// A record without a checkpointed half redeals; the saver's rows never bleed over.
	qa_session(0);
	endlessPartnerOutpostClear();
	itemAvail[0][0] = 5;
	itemAvailMax[0] = 1;
	const size_t bareLen = wire ? endlessRunSerialize(wire, ENDLESS_RUN_WIRE_MAX) : 0;
	qa_session(1);
	qa_check(bareLen > 0 && endlessRunAdopt(wire, bareLen)
	         && itemAvail[0][0] == player[1].items.ship,
	         "without a half the rows are redealt around this seat's own gear");
	free(wire);

	endlessPartnerOutpostClear();
	endlessSetSeed(seedSaved);
	memcpy(itemAvail, availSaved, sizeof(availSaved));
	memcpy(itemAvailMax, availMaxSaved, sizeof(availMaxSaved));
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

/* Restart Zone re-arms the committed level without reopening the outpost, so nothing downstream
 * re-runs the course pick that folded the drives into the per-ship masks. Everything the launch
 * was paid for has to survive the revert; the wallet it reverts to has already spent that cash. */
static void qa_sortie_restart_matrix(void)
{
	const bool savedHave = endlessSortieHave;
	const JE_boolean savedJump = jumpSection;
	const JE_byte savedMain = mainLevel, savedNext = nextLevel;
	const JE_byte savedFile = lvlFileNum, savedForced = forcedLvlFileNum;

	qa_session(0);
	qa_clear_ships();

	/* An outpost that sold a drive to each ship and a sabotage charge to player 1, then the course
	 * pick: it snapshots the pending buys, folds them, and clears them as consumed. */
	endlessPurchasedMods[0] = (unsigned)ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[1] = (unsigned)ENDLESS_MOD_OVERBLAST;
	endlessCleanseChargeCount[0] = 2;
	memcpy(endlessSortiePrePurchased, endlessPurchasedMods, sizeof(endlessSortiePrePurchased));
	memcpy(endlessSortiePreCleanse, endlessCleanseChargeCount, sizeof(endlessSortiePreCleanse));
	endlessApplyPurchasedMods();
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;

	endlessCaptureSortie();
	endlessRestartSortie();

	const Uint64 drive0 = endlessPlayerMods[0] & (Uint64)ENDLESS_MOD_KILLFIRE_ANY;
	const Uint64 drive1 = endlessPlayerMods[1] & (Uint64)ENDLESS_MOD_KILLFIRE_ANY;
	qa_check(drive0 == (Uint64)ENDLESS_MOD_TURBODRIVE,
	         "a restarted zone hands P1 back the drive it paid for");
	qa_check(drive1 == (Uint64)ENDLESS_MOD_OVERBLAST,
	         "...and P2 the one it bought for itself");

	// What a drive is worth is the window it opens, so drive one kill through the restored masks.
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	endlessCountKill(0, ENDLESS_KILLER_NONE);
	qa_check(endlessTurbodriveTimer[0] > 0 && endlessTurbodriveTimer[1] > 0,
	         "...and both still open their window on a kill after the retry");

	// A later bail out of the restarted zone restores the pre-pick one-shots from these.
	qa_check(endlessSortiePrePurchased[0] == (unsigned)ENDLESS_MOD_TURBODRIVE
	         && endlessSortiePrePurchased[1] == (unsigned)ENDLESS_MOD_OVERBLAST,
	         "a restarted zone still owes the outpost the purchases it launched with");
	qa_check(endlessSortiePreCleanse[0] == 2, "...and the sabotage charges bought beside them");

	memset(endlessSortiePrePurchased, 0, sizeof(endlessSortiePrePurchased));
	memset(endlessSortiePreCleanse, 0, sizeof(endlessSortiePreCleanse));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	forcedLvlFileNum = savedForced;
	lvlFileNum = savedFile;
	nextLevel = savedNext;
	mainLevel = savedMain;
	jumpSection = savedJump;
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
	qa_sortie_restart_matrix();
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
	qa_pierce_damage_matrix();
	qa_chooser_matrix();
	qa_wire_matrix();
	qa_resume_partner_matrix();
	qa_scenario_suite();

	qa_env_restore(&saved);
}
