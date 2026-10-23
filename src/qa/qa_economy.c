#define ENDLESS_INTERNAL_ACCESS
#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "endless.h"
#include "endless_internal.h"
#include "hud.h"
#include "network.h"
#include "player.h"

#include "SDL.h"

#include <stdio.h>
#include <string.h>

void qa_test_cash_ledger(void)
{
	const bool savedMode = endlessMode;
	const Sint64 savedCash = player[0].cash;
	const Uint64 savedEarned = endlessRunCashEarned, savedSpent = endlessRunCashSpent;
	Uint64 savedSources[ENDLESS_CASH_SOURCES], savedSinks[ENDLESS_CASH_SINKS];
	memcpy(savedSources, endlessCashBySource, sizeof(savedSources));
	memcpy(savedSinks, endlessCashBySink, sizeof(savedSinks));

	endlessMode = true;
	player[0].cash = 0;
	endlessRunCashEarned = endlessRunCashSpent = 0;
	memset(endlessCashBySource, 0, sizeof(endlessCashBySource));
	memset(endlessCashBySink, 0, sizeof(endlessCashBySink));
	endlessCashResync();

	endlessCashCredit(1000, ENDLESS_CASH_START);
	endlessCashDebit(250, ENDLESS_SINK_SUPPLIES);
	qa_check(player[0].cash == 750 && endlessRunCashEarned == 1000
	         && endlessRunCashSpent == 250
	         && endlessCashBySource[ENDLESS_CASH_START] == 1000
	         && endlessCashBySink[ENDLESS_SINK_SUPPLIES] == 250,
	         "cash ledger books ordinary credits and debits by category");

	endlessShopTradeBegin();
	player[0].cash = 600;
	endlessShopTradeCommit();
	qa_check(endlessRunCashSpent == 400 && endlessCashBySink[ENDLESS_SINK_GEAR] == 150,
	         "upgrade trade commits book temporary-wallet spending once");
	endlessShopTradeBegin();
	player[0].cash = 650;
	endlessShopTradeCommit();
	qa_check(endlessRunCashSpent == 350 && endlessCashBySink[ENDLESS_SINK_GEAR] == 100,
	         "full-refund trades cancel prior gear spending instead of creating income");
	endlessShopTradeBegin();
	player[0].cash = 800;
	endlessShopTradeCommit();
	qa_check(endlessRunCashSpent == 250 && endlessRunCashEarned == 1050
	         && endlessCashBySink[ENDLESS_SINK_GEAR] == 0
	         && endlessCashBySource[ENDLESS_CASH_TRADEIN] == 50,
	         "trade refunds beyond booked gear become trade-in income only for the excess");
	endlessShopTradeCommit();
	qa_check(endlessRunCashSpent == 250 && endlessRunCashEarned == 1050,
	         "duplicate trade commit is idempotent");
	endlessCashDebit(2000, ENDLESS_SINK_SUPPLIES);
	qa_check(player[0].cash == 0 && endlessRunCashSpent == 1050
	         && endlessCashBySink[ENDLESS_SINK_SUPPLIES] == 1050
	         && endlessRunCashEarned - endlessRunCashSpent == (Uint64)player[0].cash,
	         "oversized cash debit stops at the wallet and preserves ledger conservation");

	endlessMode = savedMode;
	player[0].cash = savedCash;
	endlessRunCashEarned = savedEarned;
	endlessRunCashSpent = savedSpent;
	memcpy(endlessCashBySource, savedSources, sizeof(savedSources));
	memcpy(endlessCashBySink, savedSinks, sizeof(savedSinks));
	endlessCashResync();
}

/* Check bounty payouts on both peers, in both credit modes, with all relevant modifiers. */
void qa_test_bounty_matrix(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedTwo = twoPlayerMode, savedCampaign = coopCampaignMode;
	const bool savedCoop = coopEndlessMode, savedEndless = endlessMode;
	const uint savedThis = thisPlayerNum;
	const Uint64 savedMods = endlessActiveMods;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	isNetworkGame = true;
	twoPlayerMode = true;
	coopCampaignMode = false;
	coopEndlessMode = true;
	endlessMode = true;
	endlessActiveMods = 0;

	char label[160];
	int link = 50;
	for (uint machine = 1; machine <= 2; ++machine)
	for (int shared = 0; shared <= 1; ++shared)
	for (int doubled = 0; doubled <= 1; ++doubled)
	for (int perk = 0; perk <= 1; ++perk)
	for (int champ = 0; champ <= 1; ++champ)
	{
		thisPlayerNum = machine;
		coop_set_session_shared_credit(shared != 0);
		coop_set_session_double_earnings(doubled != 0);
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		if (perk)
			endlessPerkGrant(1, PERK_BOUNTY, 1);   // personal: the KILLER's own perk sizes it
		endlessPerkRederive();

		player[0].cash = player[1].cash = 0;
		endlessCashResync();
		endlessAwardEliteKill(++link, champ ? 3 : 2, 1);   // player 2's kill

		endlessSetFxPlayer(1);   // read the figure the killer's row produced
		const Sint64 base = champ ? endlessChampionBounty() : endlessEliteBounty();
		endlessSetFxPlayer(0);
		const Sint64 want = (doubled && !shared) ? base * 2 : base;  // Double Earnings covers bounties
		const bool okay = shared
		                ? (player[0].cash == want && player[1].cash == want)
		                : (player[1].cash == want && player[0].cash == 0);
		snprintf(label, sizeof(label),
		         "%s bounty (machine %u, %s credit, double %d, perk %d) pays the right wallets",
		         champ ? "champion" : "elite", machine,
		         shared ? "Shared" : "Individual", doubled, perk);
		qa_check(okay && want > 0, label);
	}

	player[0].cash = player[1].cash = 0;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessCashResync();
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	thisPlayerNum = savedThis;
	endlessActiveMods = savedMods;
}

/* Score-pickup cash uses the collecting ship's Bounty Hunter stacks on both peers. */
void qa_test_score_pickup_multiplier(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedTwo = twoPlayerMode, savedCampaign = coopCampaignMode;
	const bool savedCoop = coopEndlessMode, savedEndless = endlessMode;
	const JE_boolean savedMods = endlessCampaignMods;
	const uint savedThis = thisPlayerNum;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	isNetworkGame = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	endlessMode = false;
	endlessCampaignMods = false;   // the perk only reaches a campaign through the effect layer
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_BOUNTY, 1);

	const Sint64 face = 250, boosted = face * ENDLESS_PERK_BOUNTY_PICKUP_MULT;
	qa_check(endlessScorePickupValue(0, face) == face,
	         "a campaign score pickup pays its authored value whatever perks are stored");

	endlessMode = true;
	qa_check(endlessScorePickupValue(0, face) == boosted
	         && endlessScorePickupValue(1, face) == face,
	         "Bounty Hunter multiplies its holder's score pickups and nobody else's");
	qa_check(endlessScorePickupValue(0, 0) == 0 && endlessScorePickupValue(0, -1) == -1
	         && endlessScorePickupValue((uint)COUNTOF(player), face) == face,
	         "the score-pickup multiplier leaves non-cash values and out-of-range ships alone");

	isNetworkGame = true;
	twoPlayerMode = true;
	coopEndlessMode = true;

	char label[160];
	for (uint machine = 1; machine <= 2; ++machine)
	for (int shared = 0; shared <= 1; ++shared)
	for (int doubled = 0; doubled <= 1; ++doubled)
	{
		thisPlayerNum = machine;
		coop_set_session_shared_credit(shared != 0);
		coop_set_session_double_earnings(doubled != 0);
		player[0].cash = player[1].cash = 0;
		endlessCashResync();

		// Player 1 holds the perk; each ship then collects one pickup of the same authored value.
		for (uint p = 0; p < COUNTOF(player); ++p)
			player_award_pickup_cash(&player[p], endlessScorePickupValue(p, face));

		const Sint64 scale = (doubled && !shared) ? 2 : 1;  // Double Earnings covers pickups
		const bool okay = shared
		                ? (player[0].cash == boosted + face && player[1].cash == boosted + face)
		                : (player[0].cash == boosted * scale && player[1].cash == face * scale);
		snprintf(label, sizeof(label),
		         "score pickups (machine %u, %s credit, double %d) pay the right wallets",
		         machine, shared ? "Shared" : "Individual", doubled);
		qa_check(okay, label);
	}

	player[0].cash = player[1].cash = 0;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessCashResync();
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	endlessCampaignMods = savedMods;
	thisPlayerNum = savedThis;
}

/* Both peers derive both ships' interest and clear bonuses. */
void qa_test_zone_payout(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedTwo = twoPlayerMode, savedCampaign = coopCampaignMode;
	const bool savedCoop = coopEndlessMode, savedEndless = endlessMode;
	const uint savedThis = thisPlayerNum;
	const int savedDepth = endlessRunDepth;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	isNetworkGame = true;
	twoPlayerMode = true;
	coopCampaignMode = false;
	coopEndlessMode = true;
	endlessMode = true;
	endlessRunDepth = 5;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();

	Sint64 out[2][2];
	for (uint machine = 1; machine <= 2; ++machine)
	{
		thisPlayerNum = machine;
		coop_set_session_shared_credit(false);
		coop_set_session_double_earnings(false);
		player[0].cash = 10000;
		player[1].cash = 40000;
		endlessCashResync();
		Sint64 interest = 0, bonus = 0;
		endlessApplyLevelPayout(&interest, &bonus);
		out[machine - 1][0] = player[0].cash;
		out[machine - 1][1] = player[1].cash;
		qa_check(interest > 0 && bonus > 0,
		         "the zone tally reports the local ship's own interest and bonus");
	}
	qa_check(out[0][0] == out[1][0] && out[0][1] == out[1][1],
	         "both machines derive the same wallets from the zone payout");
	qa_check(out[0][1] - 40000 > out[0][0] - 10000,
	         "zone interest follows each ship's own bank");

	thisPlayerNum = 1;
	coop_set_session_shared_credit(true);
	player[0].cash = player[1].cash = 20000;
	endlessCashResync();
	endlessApplyLevelPayout(NULL, NULL);
	qa_check(player[0].cash == player[1].cash && player[0].cash > 20000,
	         "Shared credit's equal wallets stay equal through the zone payout");

	isNetworkGame = false;
	twoPlayerMode = false;
	coopEndlessMode = false;
	player[0].cash = 10000;
	player[1].cash = 777;
	endlessCashResync();
	endlessApplyLevelPayout(NULL, NULL);
	qa_check(player[1].cash == 777 && player[0].cash > 10000,
	         "a solo zone payout touches one wallet");

	player[0].cash = player[1].cash = 0;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessCashResync();
	coop_set_session_shared_credit(true);
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	thisPlayerNum = savedThis;
	endlessRunDepth = savedDepth;
}

void qa_test_arcade_scaling(void)
{
	const bool savedBoost = arcadeLifeBoost, savedRear = arcadeRearGunScale;
	const JE_boolean savedOne = onePlayerAction, savedTwo = twoPlayerMode, savedSuper = superTyrian;
	const bool savedHudDirty = hud_bars_dirty;
	Player p = player[0];
	Uint8 lives = 1;
	bool gaugeSafe = AMMO_GAUGE_STEP(0) == 1 && AMMO_GAUGE_STEP(26) == 3;
	for (int ammo = 1; ammo <= 65535; ++ammo)
	{
		const uint step = AMMO_GAUGE_STEP(ammo);
		gaugeSafe &= step >= 1 && ((uint)ammo + step - 1) / step <= 10;
	}
	qa_check(gaugeSafe,
	         "sidekick ammo gauge rounding never draws more than ten segments");
	p.lives = &lives;
	p.hull_armor = 10;

	arcadeLifeBoost = true;
	onePlayerAction = true;
	twoPlayerMode = false;
	superTyrian = false;
	qa_check(arcade_life_scaling_active(), "arcade life scaling activates only in an eligible mode");
	lives = 0;
	qa_check(arcade_armor_max(&p) == 10, "zero arcade lives clamps to the one-life hull");
	uint previous = arcade_armor_max(&p);
	for (lives = 1; lives <= ARCADE_LIVES_MAX; ++lives)
	{
		const uint scaled = arcade_armor_max(&p);
		qa_check(scaled >= previous && scaled >= 10 && scaled <= ARCADE_FULL_BAR,
		         "arcade hull scaling is monotonic and bounded");
		previous = scaled;
	}
	qa_check(previous == ARCADE_FULL_BAR, "maximum arcade lives reach one full armor bar");
	lives = ARCADE_LIVES_MAX + 1;
	qa_check(arcade_armor_max(&p) == ARCADE_FULL_BAR,
	         "arcade hull scaling clamps lives above the gameplay cap");
	p.hull_armor = 0;
	qa_check(arcade_armor_max(&p) == 0, "an absent arcade hull remains absent");
	p.hull_armor = ARCADE_FULL_BAR;
	qa_check(arcade_armor_max(&p) == ARCADE_FULL_BAR, "a full stock hull is never inflated");

	p.hull_armor = 10; p.initial_armor = 10; p.armor = 5;
	p.shield_max = p.shield = 0; lives = ARCADE_LIVES_MAX;
	arcade_rescale_to_lives(&p);
	qa_check(p.initial_armor == 28 && p.armor == 14,
	         "arcade rescaling preserves the live hull damage ratio while growing");
	lives = 1;
	arcade_rescale_to_lives(&p);
	qa_check(p.initial_armor == 10 && p.armor == 5,
	         "arcade rescaling preserves the live hull damage ratio while shrinking");

	arcadeRearGunScale = true;
	p.items.weapon[REAR_WEAPON].power = 3; lives = 5; p.lives = &lives;
	qa_check(arcade_weapon_power(&p, REAR_WEAPON) == 7,
	         "one-player arcade rear gun combines pickups with the life counter");
	p.lives = &p.items.weapon[REAR_WEAPON].power;
	qa_check(arcade_weapon_power(&p, REAR_WEAPON) == 3,
	         "arcade rear scaling avoids the two-player life-counter alias");
	p.items.weapon[REAR_WEAPON].power = 10; lives = 11; p.lives = &lives;
	qa_check(arcade_weapon_power(&p, REAR_WEAPON) == 11,
	         "arcade rear scaling respects the engine power ceiling");

	arcadeLifeBoost = false;
	qa_check(!arcade_life_scaling_active() && arcade_armor_max(&p) == p.hull_armor,
	         "disabling arcade life scaling restores the stock hull ceiling");

	arcadeLifeBoost = savedBoost; arcadeRearGunScale = savedRear;
	onePlayerAction = savedOne; twoPlayerMode = savedTwo; superTyrian = savedSuper;
	hud_bars_dirty = savedHudDirty;
}

void qa_test_effect_gates(void)
{
	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;
	endlessMode = false;
	endlessCampaignMods = false;
	endlessActiveMods = ~(Uint64)0;
	endlessStaticLockoutReset();
	qa_check(endlessMoveScale() == 1.0f && !endlessShieldRegenOff()
	         && !endlessShieldRegenFree() && endlessGeneratorPowerAdd(7) == 7
	         && endlessHitboxScale(13) == 13,
	         "Endless movement, shield, generator, and hitbox effects cannot leak into normal play");
	qa_check(!endlessAegisGateConsume(10, 10) && !endlessShockwaveActive()
	         && endlessMartyrdomBurstShots(1, 3) == 0 && !endlessSeekerActive()
	         && endlessStaticDischargeDrain(10) == 0,
	         "Endless reactive combat effects cannot leak into normal play");
	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
	endlessActiveMods = savedMods;
}
