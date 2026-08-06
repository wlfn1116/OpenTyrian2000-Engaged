/* Online Arcade and Online Campaign: the mode-flag split, the two-wallet campaign economy,
 * and the co-op campaign record board.
 *
 * The three online modes share one set of flags, so most of what can go wrong here is a rule
 * meant for one of them reaching another: arcade orb rules in a campaign, a campaign's split
 * wallet in arcade, a co-op branch taken by a solo game. Every case states the rule it is
 * pinning rather than restating the expression that implements it. */
#include "qa.h"

#include "config.h"
#include "endless.h"
#include "episodes.h"
#include "fonthand.h"
#include "mainint.h"
#include "network.h"
#include "player.h"
#include "varz.h"

#include <stdio.h>
#include <string.h>

/* ---- harness ------------------------------------------------------------------------ */

typedef struct
{
	JE_boolean twoPlayer, onePlayer, coopCampaign, coopEndless, endless, superTyrian, linked;
	bool netGame, host, lifeBoost;
	uint playerNum;
	Player ships[2];
	CoopCampaignScore scores[COOP_CAMPAIGN_SCORE_EPISODES];
	JE_byte episode;
	JE_shortint difficulty;
	char *opponentName;
}
QaOnlineEnv;

static void qa_online_save(QaOnlineEnv *e)
{
	e->twoPlayer = twoPlayerMode;       e->onePlayer = onePlayerAction;
	e->coopCampaign = coopCampaignMode; e->coopEndless = coopEndlessMode;
	e->endless = endlessMode;           e->superTyrian = superTyrian;
	e->linked = twoPlayerLinked;
	e->netGame = isNetworkGame;         e->host = network_is_host;
	e->lifeBoost = arcadeLifeBoost;     e->playerNum = thisPlayerNum;
	e->episode = initial_episode_num;   e->difficulty = initialDifficulty;
	e->opponentName = network_opponent_name;
	memcpy(e->ships, player, sizeof(e->ships));
	memcpy(e->scores, coopCampaignScores, sizeof(e->scores));
}

static void qa_online_restore(const QaOnlineEnv *e)
{
	memcpy(coopCampaignScores, e->scores, sizeof(e->scores));
	memcpy(player, e->ships, sizeof(e->ships));
	network_opponent_name = e->opponentName;
	initialDifficulty = e->difficulty;  initial_episode_num = e->episode;
	thisPlayerNum = e->playerNum;       arcadeLifeBoost = e->lifeBoost;
	network_is_host = e->host;          isNetworkGame = e->netGame;
	twoPlayerLinked = e->linked;
	superTyrian = e->superTyrian;       endlessMode = e->endless;
	coopEndlessMode = e->coopEndless;   coopCampaignMode = e->coopCampaign;
	onePlayerAction = e->onePlayer;     twoPlayerMode = e->twoPlayer;
	coop_set_session_shared_credit(true);
	coop_set_session_double_pickups(false);
}

// Clear every mode flag, so each case names exactly the ones it means to switch on.
static void qa_modes_clear(void)
{
	twoPlayerMode = false;
	onePlayerAction = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	endlessMode = false;
	superTyrian = false;
	twoPlayerLinked = false;
}

static void qa_wallets_clear(void)
{
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].cash = 0;
		player[p].is_alive = true;
	}
}

/* ---- 1. the mode-flag split --------------------------------------------------------- */

/* One game is exactly one of: solo, local arcade, online arcade, online campaign, online
 * Endless. The rules that used to key off "two ships on screen" now have to key off the
 * ruleset instead, and this is the table that says which is which. */
static void qa_mode_split_matrix(void)
{
	char label[224];

	for (int two = 0; two <= 1; ++two)
	for (int onePlayerAct = 0; onePlayerAct <= 1; ++onePlayerAct)
	for (int camp = 0; camp <= 1; ++camp)
	for (int endl = 0; endl <= 1; ++endl)
	{
		const bool coop = (camp != 0) || (endl != 0);

		/* A co-op lobby always starts two ships, and JE_loadGameRecord clears onePlayerAction
		 * whenever two-player is set, so a co-op session is never one-ship or one-player
		 * arcade. Those shapes are unreachable, and asserting anything of them says nothing. */
		if (coop && (two == 0 || onePlayerAct != 0))
			continue;

		qa_modes_clear();
		twoPlayerMode = (two != 0);
		onePlayerAction = (onePlayerAct != 0);
		coopCampaignMode = (camp != 0);
		coopEndlessMode = (endl != 0);

		snprintf(label, sizeof(label),
		         "2P=%d 1PAction=%d campaign=%d endless=%d: co-op is %s",
		         two, onePlayerAct, camp, endl, coop ? "on" : "off");
		qa_check(coop_mode_active() == coop, label);

		/* Orb drops, the '2' section jumps and the rest of the arcade ruleset must never
		 * reach a co-op session, whatever the ship count says. */
		if (coop)
		{
			snprintf(label, sizeof(label),
			         "campaign=%d endless=%d: a co-op session never takes arcade rules",
			         camp, endl);
			qa_check(!arcade_rules_active(), label);
			snprintf(label, sizeof(label),
			         "campaign=%d endless=%d: a co-op session never draws the split arcade HUD",
			         camp, endl);
			qa_check(!split_arcade_mode(), label);
		}
		else
		{
			const bool wantArcade = (onePlayerAct != 0) || (two != 0);
			snprintf(label, sizeof(label),
			         "2P=%d 1PAction=%d outside co-op: arcade rules are %s",
			         two, onePlayerAct, wantArcade ? "on" : "off");
			qa_check(arcade_rules_active() == wantArcade, label);
			snprintf(label, sizeof(label),
			         "2P=%d outside co-op: the split arcade HUD is %s",
			         two, two ? "drawn" : "not drawn");
			qa_check(split_arcade_mode() == (two != 0), label);
		}
	}

	/* A plain one-player campaign is none of the above. */
	qa_modes_clear();
	qa_check(!coop_mode_active() && !arcade_rules_active() && !split_arcade_mode(),
	         "a solo campaign is neither co-op nor arcade");

	/* Super Arcade is a one-player arcade ruleset with a single ship. */
	qa_modes_clear();
	onePlayerAction = true;
	qa_check(arcade_rules_active() && !split_arcade_mode(),
	         "one-player arcade takes arcade rules without the split HUD");

	/* The five shapes a session can actually be in, named. */
	qa_modes_clear();
	qa_check(!arcade_rules_active() && !split_arcade_mode() && !coop_mode_active(),
	         "solo campaign: cash economy, one ship, no co-op branches");
	qa_modes_clear();
	twoPlayerMode = true;
	qa_check(arcade_rules_active() && split_arcade_mode() && !coop_mode_active(),
	         "local arcade: arcade rules and the split HUD");
	qa_modes_clear();
	twoPlayerMode = true;
	isNetworkGame = true;
	qa_check(arcade_rules_active() && split_arcade_mode() && !coop_mode_active(),
	         "online arcade: the same arcade rules as local, over the wire");
	qa_modes_clear();
	twoPlayerMode = true;
	coopCampaignMode = true;
	qa_check(!arcade_rules_active() && !split_arcade_mode() && coop_mode_active(),
	         "online campaign: two ships on the cash economy, no arcade rules");
	qa_modes_clear();
	twoPlayerMode = true;
	coopEndlessMode = true;
	endlessMode = true;
	qa_check(!arcade_rules_active() && !split_arcade_mode() && coop_mode_active(),
	         "online Endless: two ships on the roguelite, no arcade rules");
	isNetworkGame = false;
}

/* ---- 2. whose wallet a machine spends ----------------------------------------------- */

/* Arcade shares one wallet and one HUD sidebar; co-op gives each machine its own. The index
 * that decides this is read all over the outpost, so it is pinned from both machines. */
static void qa_local_index_matrix(void)
{
	char label[224];

	for (int slot = 1; slot <= 2; ++slot)
	{
		isNetworkGame = true;
		thisPlayerNum = (uint)slot;

		/* Online arcade: both machines drive ship one's wallet, because arcade has only the
		 * one shared economy behind its split HUD. */
		qa_modes_clear();
		twoPlayerMode = true;
		snprintf(label, sizeof(label),
		         "online arcade from machine %d still works ship one's shared wallet", slot);
		qa_check(gameplay_local_player_index() == 0, label);

		/* Online campaign and Endless: each machine owns its own ship's wallet. */
		qa_modes_clear();
		coopCampaignMode = true;
		snprintf(label, sizeof(label),
		         "online campaign from machine %d owns ship %d's wallet", slot, slot);
		qa_check(gameplay_local_player_index() == (uint)(slot - 1), label);

		qa_modes_clear();
		coopEndlessMode = true;
		endlessMode = true;
		snprintf(label, sizeof(label),
		         "online Endless from machine %d owns ship %d's wallet", slot, slot);
		qa_check(gameplay_local_player_index() == (uint)(slot - 1)
		         && endlessEconomyIndex() == (uint)(slot - 1), label);
		snprintf(label, sizeof(label),
		         "online Endless from machine %d names the other ship as the partner", slot);
		qa_check(endlessPartnerIndex() == (uint)(2 - slot), label);
	}

	/* Offline, there is no second machine: the index is ship one whatever the slot says. */
	isNetworkGame = false;
	thisPlayerNum = 2;
	qa_modes_clear();
	coopCampaignMode = true;
	qa_check(gameplay_local_player_index() == 0,
	         "an offline game reads ship one however the player slot is left set");

	/* A slot outside 1..2 must not index past the array. */
	isNetworkGame = true;
	thisPlayerNum = 7;
	qa_check(gameplay_local_player_index() == 0,
	         "a player slot outside the session falls back to ship one");
	thisPlayerNum = 0;
	qa_check(gameplay_local_player_index() == 0,
	         "an unset player slot falls back to ship one");

	thisPlayerNum = 1;
	isNetworkGame = false;
}

/* ---- 3. arcade keeps its own economy ------------------------------------------------ */

/* The Credit and Double Pickups settings belong to the two co-op modes. Arcade has one wallet,
 * so both must stand down there even when the host left them switched on. */
static void qa_arcade_economy_matrix(void)
{
	char label[224];

	for (int shared = 0; shared <= 1; ++shared)
	for (int dbl = 0; dbl <= 1; ++dbl)
	for (int slot = 1; slot <= 2; ++slot)
	{
		qa_modes_clear();
		twoPlayerMode = true;
		isNetworkGame = true;
		thisPlayerNum = (uint)slot;
		coop_set_session_shared_credit(shared != 0);
		coop_set_session_double_pickups(dbl != 0);

		snprintf(label, sizeof(label),
		         "online arcade from machine %d ignores Credit=%s", slot,
		         shared ? "Shared" : "Individual");
		qa_check(!coop_credit_is_shared(), label);
		snprintf(label, sizeof(label),
		         "online arcade from machine %d ignores Double Pickups=%s", slot,
		         dbl ? "on" : "off");
		qa_check(!coop_pickups_are_doubled(), label);

		/* So a pickup pays its collector once, and nobody else. */
		qa_wallets_clear();
		player_award_pickup_cash(&player[0], 400);
		snprintf(label, sizeof(label),
		         "online arcade pickup pays its collector once (Credit=%s, 2x=%s)",
		         shared ? "Shared" : "Individual", dbl ? "on" : "off");
		qa_check(player[0].cash == 400 && player[1].cash == 0, label);

		qa_wallets_clear();
		player_award_kill_cash(&player[1], 400);
		snprintf(label, sizeof(label),
		         "online arcade kill cash pays its shooter once (Credit=%s, 2x=%s)",
		         shared ? "Shared" : "Individual", dbl ? "on" : "off");
		qa_check(player[1].cash == 400 && player[0].cash == 0, label);
	}

	/* Local arcade behaves the same as online arcade here: no session, same one economy. */
	qa_modes_clear();
	twoPlayerMode = true;
	isNetworkGame = false;
	coop_set_session_shared_credit(true);
	coop_set_session_double_pickups(true);
	qa_wallets_clear();
	player_award_pickup_cash(&player[0], 400);
	qa_check(player[0].cash == 400 && player[1].cash == 0,
	         "local arcade is untouched by the co-op credit settings");

	/* Arcade life scaling follows the arcade ruleset, not the ship count, so it must not
	 * switch itself on inside a co-op campaign that happens to have two ships. */
	arcadeLifeBoost = true;
	qa_modes_clear();
	twoPlayerMode = true;
	qa_check(arcade_life_scaling_active(), "local arcade scales hulls to lives");
	qa_modes_clear();
	onePlayerAction = true;
	qa_check(arcade_life_scaling_active(), "one-player arcade scales hulls to lives");
	qa_modes_clear();
	coopCampaignMode = true;
	qa_check(!arcade_life_scaling_active(),
	         "an online campaign has no lives to scale hulls to");
	qa_modes_clear();
	coopEndlessMode = true;
	qa_check(!arcade_life_scaling_active(),
	         "an online Endless run has no lives to scale hulls to");
	qa_modes_clear();
	onePlayerAction = true;
	superTyrian = true;
	qa_check(!arcade_life_scaling_active(), "SuperTyrian is excluded from arcade hull scaling");

	qa_modes_clear();
	isNetworkGame = false;
	coop_set_session_shared_credit(true);
	coop_set_session_double_pickups(false);
}

/* ---- 4. the campaign's two wallets -------------------------------------------------- */

/* Online Campaign runs the ordinary cash economy twice over. It shares the Credit rules with
 * Endless but not the run ledger, so the award path is exercised again with endlessMode off. */
static void qa_campaign_economy_matrix(void)
{
	char label[224];

	for (int shared = 0; shared <= 1; ++shared)
	for (int dbl = 0; dbl <= 1; ++dbl)
	for (int slot = 1; slot <= 2; ++slot)
	for (int payee = 0; payee <= 1; ++payee)
	{
		qa_modes_clear();
		coopCampaignMode = true;
		isNetworkGame = true;
		thisPlayerNum = (uint)slot;
		coop_set_session_shared_credit(shared != 0);
		coop_set_session_double_pickups(dbl != 0);

		const bool doubling = (dbl != 0) && (shared == 0);

		qa_wallets_clear();
		player_award_pickup_cash(&player[payee], 300);
		const long wantPayee = shared ? 300 : (doubling ? 600 : 300);
		const long wantOther = shared ? 300 : 0;
		snprintf(label, sizeof(label),
		         "campaign %s/%s: P%d's pickup pays them %ld from machine %d",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x",
		         payee + 1, wantPayee, slot);
		qa_check((long)player[payee].cash == wantPayee, label);
		snprintf(label, sizeof(label),
		         "campaign %s/%s: P%d's pickup pays the partner %ld from machine %d",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x",
		         payee + 1, wantOther, slot);
		qa_check((long)player[1 - payee].cash == wantOther, label);

		qa_wallets_clear();
		player_award_kill_cash(&player[payee], 300);
		snprintf(label, sizeof(label),
		         "campaign %s/%s: P%d's kill cash is not doubled, from machine %d",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x", payee + 1, slot);
		qa_check((long)player[payee].cash == 300, label);
		snprintf(label, sizeof(label),
		         "campaign %s/%s: P%d's kill cash reaches the partner only when Shared (machine %d)",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x", payee + 1, slot);
		qa_check((long)player[1 - payee].cash == (shared ? 300 : 0), label);
	}

	/* A campaign is not an Endless run: nothing here may touch the Endless ledger. */
	qa_modes_clear();
	coopCampaignMode = true;
	isNetworkGame = true;
	thisPlayerNum = 1;
	coop_set_session_shared_credit(false);
	coop_set_session_double_pickups(false);
	endlessRunCashEarned = 0;
	memset(endlessCashBySource, 0, sizeof(endlessCashBySource));
	qa_wallets_clear();
	player_award_pickup_cash(&player[0], 1000);
	qa_check(endlessRunCashEarned == 0,
	         "campaign income stays out of the Endless run ledger");

	qa_modes_clear();
	isNetworkGame = false;
	thisPlayerNum = 1;
	coop_set_session_shared_credit(true);
	coop_set_session_double_pickups(false);
}

/* ---- 5. the co-op campaign record board --------------------------------------------- */

/* One best run per episode, scored on the two players' combined cash. It is a co-op-only
 * board, so nothing else may write to it. */
static void qa_campaign_score_matrix(void)
{
	char label[224];
	char nameHold[24];

	const JE_byte savedEpisode = initial_episode_num;
	const JE_shortint savedDifficulty = initialDifficulty;
	char *const savedOpponent = network_opponent_name;

	snprintf(nameHold, sizeof(nameHold), "Partner");
	network_opponent_name = nameHold;

	/* Only a co-op campaign writes the board: not a solo run, not arcade, not Endless. */
	static const char *const nonCampaign[3] = { "a solo campaign", "arcade", "online Endless" };
	for (int mode = 0; mode < 3; ++mode)
	{
		qa_modes_clear();
		if (mode == 1)
			twoPlayerMode = true;
		else if (mode == 2)
		{
			coopEndlessMode = true;
			endlessMode = true;
		}

		memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
		initial_episode_num = 1;
		initialDifficulty = 2;
		qa_wallets_clear();
		player[0].cash = 50000;
		player[1].cash = 50000;
		coopCampaignScoreNote();
		snprintf(label, sizeof(label),
		         "%s does not write the co-op campaign board", nonCampaign[mode]);
		qa_check(coopCampaignScores[0].score == 0, label);
	}

	/* An episode outside the board's range is dropped rather than written past. */
	qa_modes_clear();
	coopCampaignMode = true;
	memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
	qa_wallets_clear();
	player[0].cash = 1000;
	player[1].cash = 1000;
	initial_episode_num = 0;
	coopCampaignScoreNote();
	initial_episode_num = COOP_CAMPAIGN_SCORE_EPISODES + 1;
	coopCampaignScoreNote();
	bool untouched = true;
	for (int e = 0; e < COOP_CAMPAIGN_SCORE_EPISODES; ++e)
		if (coopCampaignScores[e].score != 0)
			untouched = false;
	qa_check(untouched, "an episode outside the board's range writes nothing");

	/* A run records against its own episode, scored on both wallets together. */
	for (int e = 1; e <= COOP_CAMPAIGN_SCORE_EPISODES; ++e)
	{
		qa_modes_clear();
		coopCampaignMode = true;
		memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
		initial_episode_num = (JE_byte)e;
		initialDifficulty = (JE_shortint)(e % 4 + 1);
		qa_wallets_clear();
		player[0].cash = 1200;
		player[1].cash = 800;
		coopCampaignScoreNote();

		snprintf(label, sizeof(label),
		         "episode %d records the two wallets combined", e);
		qa_check(coopCampaignScores[e - 1].score == 2000, label);
		snprintf(label, sizeof(label),
		         "episode %d records the difficulty it was flown on", e);
		qa_check(coopCampaignScores[e - 1].difficulty == (Uint8)(e % 4 + 1), label);
		snprintf(label, sizeof(label),
		         "episode %d records both player names", e);
		qa_check(strstr(coopCampaignScores[e - 1].name, "Partner") != NULL, label);
		snprintf(label, sizeof(label),
		         "episode %d leaves the other episodes' records alone", e);
		bool others = true;
		for (int o = 0; o < COOP_CAMPAIGN_SCORE_EPISODES; ++o)
			if (o != e - 1 && coopCampaignScores[o].score != 0)
				others = false;
		qa_check(others, label);
	}

	/* Only a better run replaces the standing best; an equal one does not. */
	qa_modes_clear();
	coopCampaignMode = true;
	memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
	initial_episode_num = 1;
	initialDifficulty = 2;
	qa_wallets_clear();
	player[0].cash = 5000;
	player[1].cash = 5000;
	coopCampaignScoreNote();
	qa_check(coopCampaignScores[0].score == 10000, "a first run takes the empty record");

	player[0].cash = 4000;
	player[1].cash = 4000;
	coopCampaignScoreNote();
	qa_check(coopCampaignScores[0].score == 10000, "a worse run leaves the record standing");

	player[0].cash = 5000;
	player[1].cash = 5000;
	coopCampaignScoreNote();
	qa_check(coopCampaignScores[0].score == 10000, "an equal run leaves the record standing");

	player[0].cash = 9000;
	player[1].cash = 2000;
	coopCampaignScoreNote();
	qa_check(coopCampaignScores[0].score == 11000, "a better run takes the record");

	memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
	network_opponent_name = savedOpponent;
	initialDifficulty = savedDifficulty;
	initial_episode_num = savedEpisode;
	qa_modes_clear();
}

/* ---- 5b. the strings the lobby and outpost put on screen ---------------------------- */

/* Every value the online menus print has to be there, be drawable in the game font, and fit
 * the row it sits on. A name that runs past its column is only visible on the machine that
 * happens to open that screen, which is why it is checked here instead. */
static bool qa_string_drawable(const char *s)
{
	if (s == NULL || s[0] == '\0' || strlen(s) >= 24)
		return false;
	for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p)
		if (*p != ' ' && font_ascii[*p] < 0)
			return false;
	return true;
}

static void qa_online_strings_matrix(void)
{
	char label[192];
	// The narrowest column any of these land in: a settings row's value, right of its label.
	const int valueWidthMax = 120;

	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		const char *const name = endlessRunModeName((EndlessRunMode)m);
		snprintf(label, sizeof(label), "run mode %d has a drawable name", m);
		qa_check(qa_string_drawable(name), label);
		snprintf(label, sizeof(label), "run mode %d's name fits its settings row", m);
		qa_check(JE_textWidth(name, TINY_FONT) <= valueWidthMax, label);
	}

	for (int c = 0; c < ENDLESS_PICK_COUNT; ++c)
	{
		const char *const name = endlessCourseChooserName((EndlessCourseChooser)c);
		snprintf(label, sizeof(label), "course chooser %d has a drawable name", c);
		qa_check(qa_string_drawable(name), label);
		snprintf(label, sizeof(label), "course chooser %d's name fits its settings row", c);
		qa_check(JE_textWidth(name, TINY_FONT) <= valueWidthMax, label);
	}

	/* The run summary prints one row per cash source and sink, so each needs a name. */
	for (int s = 0; s < ENDLESS_CASH_SOURCES; ++s)
	{
		const char *const name = endlessCashSourceName((EndlessCashSource)s);
		snprintf(label, sizeof(label), "cash source %d has a drawable name", s);
		qa_check(qa_string_drawable(name), label);
	}
	for (int s = 0; s < ENDLESS_CASH_SINKS; ++s)
	{
		const char *const name = endlessCashSinkName((EndlessCashSink)s);
		snprintf(label, sizeof(label), "cash sink %d has a drawable name", s);
		qa_check(qa_string_drawable(name), label);
	}

	/* Chooser names have to be distinct, or the lobby row cannot say which is selected. */
	for (int a = 0; a < ENDLESS_PICK_COUNT; ++a)
	for (int b = a + 1; b < ENDLESS_PICK_COUNT; ++b)
	{
		snprintf(label, sizeof(label), "course choosers %d and %d read differently", a, b);
		qa_check(strcmp(endlessCourseChooserName((EndlessCourseChooser)a),
		                endlessCourseChooserName((EndlessCourseChooser)b)) != 0, label);
	}
	for (int a = 0; a < ENDLESS_RUNMODE_COUNT; ++a)
	for (int b = a + 1; b < ENDLESS_RUNMODE_COUNT; ++b)
	{
		snprintf(label, sizeof(label), "run modes %d and %d read differently", a, b);
		qa_check(strcmp(endlessRunModeName((EndlessRunMode)a),
		                endlessRunModeName((EndlessRunMode)b)) != 0, label);
	}

	/* A value outside the enum still has to return something printable rather than NULL:
	 * a resumed save or a peer on another build can hand these an out-of-range byte. */
	qa_check(qa_string_drawable(endlessRunModeName((EndlessRunMode)99)),
	         "an out-of-range run mode still prints something");
	qa_check(qa_string_drawable(endlessCourseChooserName((EndlessCourseChooser)99)),
	         "an out-of-range course chooser still prints something");
}

/* ---- 6. online never pauses --------------------------------------------------------- */

/* Pause halts one machine and not the other, so all three online modes refuse it however the
 * call arrives. Offline it still works. */
static void qa_online_pause_matrix(void)
{
	char label[192];
	const JE_boolean savedNet = isNetworkGame;

	static const char *const modeName[3] = { "online arcade", "online campaign", "online Endless" };
	for (int mode = 0; mode < 3; ++mode)
	{
		qa_modes_clear();
		if (mode == 0) twoPlayerMode = true;
		else if (mode == 1) coopCampaignMode = true;
		else { coopEndlessMode = true; endlessMode = true; }
		isNetworkGame = true;

		/* JE_pauseGame returns without drawing or waiting; reaching the next statement at
		 * all is the check, since a live pause would sit in its own input loop. */
		JE_pauseGame();
		snprintf(label, sizeof(label), "%s refuses to pause", modeName[mode]);
		qa_check(isNetworkGame, label);
	}

	isNetworkGame = savedNet;
	qa_modes_clear();
}

/* ---- entry point -------------------------------------------------------------------- */

void qa_test_online_suite(void)
{
	QaOnlineEnv saved;
	qa_online_save(&saved);

	qa_mode_split_matrix();
	qa_local_index_matrix();
	qa_arcade_economy_matrix();
	qa_campaign_economy_matrix();
	qa_campaign_score_matrix();
	qa_online_strings_matrix();
	qa_online_pause_matrix();

	qa_online_restore(&saved);
}
