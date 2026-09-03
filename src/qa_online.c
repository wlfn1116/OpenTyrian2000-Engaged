/* Tests for online-mode boundaries: Arcade rules, Campaign wallets, and co-op
 * records must not leak into one another or solo play. */
#include "qa.h"

#include "config.h"
#include "endless.h"
#include "endless_internal.h"  // endlessPerkEffective, for reading a seat's row that is not ours
#include "episodes.h"
#include "font.h"      // small_font, for measuring the picker's names against its columns
#include "fonthand.h"
#include "game_menu.h"  // the Endless zone jump's staging, carried by the departure handshake
#include "helptext.h"  // superShips[]
#include "lvlmast.h"
#include "mainint.h"
#include "net_rollback.h"  // session netcode/recovery flags
#include "network.h"
#include "params.h"   // constantPlay, one of the co-op campaign board's conditions
#include "player.h"
#include "rollback.h"
#include "tyrian2.h"  // difficulty bump, Super Arcade equip, picker and boss-bar layout
#include "varz.h"
#include "video.h"  // PLAYFIELD_LEFT / PLAYFIELD_RIGHT, for the HUD block geometry check

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* ---- harness ------------------------------------------------------------------------ */

typedef struct
{
	JE_boolean twoPlayer, onePlayer, coopCampaign, coopEndless, endless, superTyrian, linked;
	JE_boolean arcadeSeparate;
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
	e->linked = twoPlayerLinked;        e->arcadeSeparate = arcadeSeparateMode;
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
	twoPlayerLinked = e->linked;        arcadeSeparateMode = e->arcadeSeparate;
	superTyrian = e->superTyrian;       endlessMode = e->endless;
	coopEndlessMode = e->coopEndless;   coopCampaignMode = e->coopCampaign;
	onePlayerAction = e->onePlayer;     twoPlayerMode = e->twoPlayer;
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
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
	arcadeSeparateMode = false;
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

/* Each session belongs to exactly one ruleset, independent of the number of visible ships. */
static void qa_mode_split_matrix(void)
{
	char label[224];

	for (int two = 0; two <= 1; ++two)
	for (int onePlayerAct = 0; onePlayerAct <= 1; ++onePlayerAct)
	for (int camp = 0; camp <= 1; ++camp)
	for (int endl = 0; endl <= 1; ++endl)
	{
		const bool coop = (camp != 0) || (endl != 0);

		/* Co-op cannot reach the one-ship or one-player arcade states. */
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

	/* Separate arcade is the sixth shape: the arcade ruleset, but two independent ships instead
	 * of the linked pair, so it takes the single-player HUD and every per-ship path. */
	qa_modes_clear();
	twoPlayerMode = true;
	arcadeSeparateMode = true;
	qa_check(arcade_rules_active() && arcade_separate_mode() && !split_arcade_mode()
	         && dual_ship_mode() && !coop_mode_active(),
	         "Separate arcade: arcade rules, two own ships, and no split HUD");

	/* The separate-ships flag matters only to two-ship arcade sessions. */
	coopCampaignMode = true;
	qa_check(!arcade_separate_mode() && !arcade_rules_active() && coop_mode_active(),
	         "a Separate flag left set does not touch an online campaign");
	coopCampaignMode = false;
	twoPlayerMode = false;
	qa_check(!arcade_separate_mode() && !dual_ship_mode(),
	         "...nor a one-ship game, which has no second ship to separate");

	qa_modes_clear();
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

		/* Separate arcade: each machine owns its own ship, so the HUD sidebar, the specials and
		 * the wallet all follow the local slot the way co-op's do. */
		qa_modes_clear();
		twoPlayerMode = true;
		arcadeSeparateMode = true;
		snprintf(label, sizeof(label),
		         "Separate arcade from machine %d owns ship %d", slot, slot);
		qa_check(gameplay_local_player_index() == (uint)(slot - 1), label);
		snprintf(label, sizeof(label),
		         "Separate arcade from machine %d shows its own sidekicks", slot);
		qa_check(hud_sidekick_player_index() == (uint)(slot - 1), label);

		/* The linked pair hangs the pods off ship two on both machines. */
		qa_modes_clear();
		twoPlayerMode = true;
		snprintf(label, sizeof(label),
		         "linked arcade from machine %d still shows ship two's sidekicks", slot);
		qa_check(hud_sidekick_player_index() == 1, label);

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

/* The Credit and Double Earnings settings belong to the two co-op modes. Arcade has one wallet,
 * so both must stand down there even when the host left them switched on. */
static void qa_arcade_economy_matrix(void)
{
	char label[224];

	for (int separate = 0; separate <= 1; ++separate)
	for (int shared = 0; shared <= 1; ++shared)
	for (int dbl = 0; dbl <= 1; ++dbl)
	for (int slot = 1; slot <= 2; ++slot)
	{
		const char *const ships = separate ? "Separate" : "Linked";

		qa_modes_clear();
		twoPlayerMode = true;
		arcadeSeparateMode = (separate != 0);
		isNetworkGame = true;
		thisPlayerNum = (uint)slot;
		coop_set_session_shared_credit(shared != 0);
		coop_set_session_double_earnings(dbl != 0);

		snprintf(label, sizeof(label),
		         "%s online arcade from machine %d ignores Credit=%s", ships, slot,
		         shared ? "Shared" : "Individual");
		qa_check(!coop_credit_is_shared(), label);
		snprintf(label, sizeof(label),
		         "%s online arcade from machine %d ignores Double Earnings=%s", ships, slot,
		         dbl ? "on" : "off");
		qa_check(!coop_earnings_are_doubled(), label);

		/* So a pickup pays its collector once, and nobody else. Separate arcade is where that
		 * matters most: two scores are being kept, and neither may be fed by the other's run. */
		qa_wallets_clear();
		player_award_pickup_cash(&player[0], 400);
		snprintf(label, sizeof(label),
		         "%s online arcade pickup pays its collector once (Credit=%s, 2x=%s)",
		         ships, shared ? "Shared" : "Individual", dbl ? "on" : "off");
		qa_check(player[0].cash == 400 && player[1].cash == 0, label);

		qa_wallets_clear();
		player_award_kill_cash(&player[1], 400);
		snprintf(label, sizeof(label),
		         "%s online arcade kill cash pays its shooter once (Credit=%s, 2x=%s)",
		         ships, shared ? "Shared" : "Individual", dbl ? "on" : "off");
		qa_check(player[1].cash == 400 && player[0].cash == 0, label);
	}

	/* Local arcade behaves the same as online arcade here: no session, same one economy. */
	qa_modes_clear();
	twoPlayerMode = true;
	isNetworkGame = false;
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(true);
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
	twoPlayerMode = true;
	arcadeSeparateMode = true;
	qa_check(arcade_life_scaling_active(),
	         "Separate arcade scales each hull to its own lives, like every other arcade game");
	qa_modes_clear();
	onePlayerAction = true;
	superTyrian = true;
	qa_check(!arcade_life_scaling_active(), "SuperTyrian is excluded from arcade hull scaling");

	qa_modes_clear();
	isNetworkGame = false;
	arcadeLifeBoost = false;
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
}

/* ---- 3b. Separate arcade gives each ship its own life counter ------------------------ */

/* Lives alias the linked pair's rear gun or each separate ship's front gun. */
static void qa_separate_arcade_lives(void)
{
	qa_modes_clear();
	twoPlayerMode = true;
	qa_check(player_lives_port(0) == FRONT_WEAPON && player_lives_port(1) == REAR_WEAPON,
	         "the linked pair counts player two's lives on the Dragonwing's rear bay");

	arcadeSeparateMode = true;
	qa_check(player_lives_port(0) == FRONT_WEAPON && player_lives_port(1) == FRONT_WEAPON,
	         "Separate arcade counts each ship's lives on its own front gun");

	/* Spending a life must update only the weapon byte aliased to that ship. */
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].items.weapon[FRONT_WEAPON].power = 4;
		player[p].items.weapon[REAR_WEAPON].power = 7;
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;
	}
	--(*player[0].lives);
	qa_check(player[0].items.weapon[FRONT_WEAPON].power == 3
	         && player[0].items.weapon[REAR_WEAPON].power == 7
	         && player[1].items.weapon[FRONT_WEAPON].power == 4,
	         "a Separate arcade death spends only that ship's own front-gun life counter");

	--(*player[1].lives);
	qa_check(player[1].items.weapon[FRONT_WEAPON].power == 3
	         && player[1].items.weapon[REAR_WEAPON].power == 7,
	         "...and the second ship's counter is its front gun too, not the rear bay");

	/* Snapshot restore must rebuild the same lives aliases; claim the lazy ring before writing. */
	rollback_state_hash();
	rollback_snapshot(0x5EAAu);
	player[0].lives = player[1].lives = NULL;
	qa_check(rollback_restore(0x5EAAu)
	         && player[0].lives == &player[0].items.weapon[FRONT_WEAPON].power
	         && player[1].lives == &player[1].items.weapon[FRONT_WEAPON].power,
	         "a rollback restore leaves each Separate arcade ship on its own life counter");

	arcadeSeparateMode = false;
	rollback_snapshot(0x5EABu);
	player[0].lives = player[1].lives = NULL;
	qa_check(rollback_restore(0x5EABu)
	         && player[0].lives == &player[0].items.weapon[FRONT_WEAPON].power
	         && player[1].lives == &player[1].items.weapon[REAR_WEAPON].power,
	         "...and the linked pair is restored onto the bays it flies with");

	/* Rear-gun scaling adds lives - 1 to the rear bay's power. That is only safe where the rear
	 * bay is not itself the life counter, which rules out the linked pair and nothing else. */
	const bool savedRearScale = arcadeRearGunScale;
	arcadeRearGunScale = true;

	qa_modes_clear();
	onePlayerAction = true;
	qa_check(arcade_rear_scale_active(), "one-player arcade scales the rear gun with lives");
	qa_modes_clear();
	twoPlayerMode = true;
	qa_check(!arcade_rear_scale_active(),
	         "the linked pair does not: player two's rear bay IS its life counter");
	arcadeSeparateMode = true;
	qa_check(arcade_rear_scale_active(),
	         "Separate arcade does: each ship counts lives on its own front gun");
	qa_modes_clear();
	coopCampaignMode = true;
	qa_check(!arcade_rear_scale_active(), "an online campaign has no lives to scale a gun with");
	qa_modes_clear();
	onePlayerAction = true;
	superTyrian = true;
	qa_check(!arcade_rear_scale_active(), "SuperTyrian is excluded from rear-gun scaling");

	arcadeRearGunScale = savedRearScale;
	qa_modes_clear();
}

/* ---- 3c. the special-weapon block at the top of the playfield ------------------------ */

// Do two axis-aligned rectangles share a pixel? Half-open on the right and bottom edges.
static bool qa_rects_overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
	return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* The special-weapon icon and its ready light sit above the owning ship's name and lives, on
 * that ship's own side. */
static void qa_special_block_geometry(void)
{
	char label[224];

	for (int slot = 1; slot <= 2; ++slot)
	{
		const uint local = (uint)(slot - 1);

		qa_modes_clear();
		twoPlayerMode = true;
		arcadeSeparateMode = true;
		isNetworkGame = true;
		thisPlayerNum = (uint)slot;
		for (uint p = 0; p < COUNTOF(player); ++p)
			player[p].items.special = 1;

		snprintf(label, sizeof(label),
		         "machine %d draws the special block for its own ship and no other", slot);
		qa_check(hud_special_block_shown(local) && !hud_special_block_shown(1 - local), label);

		const int iconX = hud_special_icon_x(local);
		const int lightX = hud_special_light_x(local);
		const int rowY = hud_lives_row_y(local);
		const int nameY = rowY - HUD_LIVES_NAME_RISE;

		snprintf(label, sizeof(label),
		         "machine %d: the ready light sits beside the special icon, not on it", slot);
		qa_check(!qa_rects_overlap(iconX, HUD_SPECIAL_ICON_Y, HUD_SPECIAL_ICON_W, HUD_SPECIAL_ICON_H,
		                           lightX, HUD_SPECIAL_LIGHT_Y, HUD_SPECIAL_LIGHT_W, HUD_SPECIAL_LIGHT_H),
		         label);

		/* Clear the full name label, including the tiny font's shade and separator rows. */
		const int blockBottom = MAX(HUD_SPECIAL_ICON_Y + HUD_SPECIAL_ICON_H,
		                            HUD_SPECIAL_LIGHT_Y + HUD_SPECIAL_LIGHT_H);
		snprintf(label, sizeof(label),
		         "machine %d: blank rows separate the block, name and lives (block ends %d, "
		         "name at %d, lives at %d)",
		         slot, blockBottom - 1, nameY, rowY);
		qa_check(rowY == HUD_LIVES_Y_SPECIAL && nameY - 1 > blockBottom && rowY > nameY + 9, label);

		snprintf(label, sizeof(label),
		         "machine %d: the other ship's row does not move for a special it does not hold",
		         slot);
		qa_check(hud_lives_row_y(1 - local) == HUD_LIVES_Y, label);

		snprintf(label, sizeof(label),
		         "machine %d: the whole block stays inside the playfield", slot);
		qa_check(iconX >= PLAYFIELD_LEFT && lightX >= PLAYFIELD_LEFT
		         && iconX + HUD_SPECIAL_ICON_W <= PLAYFIELD_RIGHT + 1
		         && lightX + HUD_SPECIAL_LIGHT_W <= PLAYFIELD_RIGHT + 1, label);

		/* A centred TOP boss bar stops at these edges, so both have to cover the block or the
		 * bar draws straight through it. */
		const int blockLeft = MIN(iconX, lightX);
		const int blockRight = MAX(iconX + HUD_SPECIAL_ICON_W, lightX + HUD_SPECIAL_LIGHT_W);
		snprintf(label, sizeof(label),
		         "machine %d: a TOP boss bar is told about the block on ship %d's side",
		         slot, slot);
		qa_check(hud_special_on_right(local) ? hud_top_right_left_edge() <= blockLeft
		                                     : hud_top_left_right_edge() >= blockRight, label);
	}

	/* Co-op has no name or lives row under the block, so there is no second corner to mirror
	 * into and both machines keep the historical left one. */
	qa_modes_clear();
	twoPlayerMode = true;
	coopCampaignMode = true;
	isNetworkGame = true;
	thisPlayerNum = 2;
	player[1].items.special = 1;
	qa_check(hud_special_block_shown(1) && !hud_special_on_right(1)
	         && hud_special_icon_x(1) == 25
	         && hud_top_left_right_edge() >= hud_special_light_x(1) + HUD_SPECIAL_LIGHT_W,
	         "an online campaign joiner draws its block in the same corner the host does");

	/* No special held, no block, and the row sits back at its stock height. */
	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].items.special = 0;
	qa_check(!hud_special_block_shown(0) && !hud_special_block_shown(1)
	         && hud_lives_row_y(0) == HUD_LIVES_Y && hud_lives_row_y(1) == HUD_LIVES_Y,
	         "with no special held there is no block and no row shift");

	/* The linked pair shares one HUD sidebar, so both machines draw ship one's block. */
	qa_modes_clear();
	twoPlayerMode = true;
	isNetworkGame = true;
	thisPlayerNum = 2;
	player[1].items.special = 1;
	qa_check(!hud_special_block_shown(0) && !hud_special_block_shown(1),
	         "the linked pair draws no block for a special player two somehow holds");
	player[0].items.special = 1;
	qa_check(hud_special_block_shown(0) && hud_special_icon_x(0) == 25,
	         "...and ship one's block stays in its historical corner");

	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].items.special = 0;
	qa_modes_clear();
	isNetworkGame = false;
	thisPlayerNum = 1;
}

/* ---- 3d. boss bar clearance ---------------------------------------------------------- */

/* Check boss-bar clearance against the surrounding HUD ink. */
static void qa_boss_bar_side(bool onLeft, int inkBottom, int inkTop, const char *label)
{
	int top, bot;
	boss_bar_vertical_span(onLeft, &top, &bot);

	// One row to step off the neighbour's own ink, then the gap. The clamps are the WARNING
	// strip above (rows 0..3) and its text below (row 178).
	const int wantTop = MAX(inkBottom + 1 + 3, 3 + 1 + 3);
	const int wantBot = MIN(inkTop - 1 - 1, 178 - 1 - 1);
	char message[224];
	snprintf(message, sizeof(message), "%s (span %d..%d, expected %d..%d)",
	         label, top, bot, wantTop, wantBot);
	qa_check(top == wantTop && bot == wantBot, message);
}

static void qa_boss_bar_clearance(void)
{
	const int scoreTop = vga_height - 26;   // the score row's outline top
	const int bombTop  = 160;               // the superbomb icon row

	/* Separate arcade, both ships holding a special: the machine flying ship two draws the
	 * block in the right corner over that ship's name and lives rows. */
	qa_modes_clear();
	twoPlayerMode = true;
	arcadeSeparateMode = true;
	isNetworkGame = true;
	thisPlayerNum = 2;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].items.special = 1;
		player[p].superbombs = 0;
		player[p].is_alive = true;
	}

	qa_boss_bar_side(false, HUD_LIVES_Y_SPECIAL + 11, scoreTop,
	                 "right bar clears the special block's lives row and the score");
	qa_boss_bar_side(true, HUD_LIVES_Y + 11, scoreTop,
	                 "left bar clears the plain lives row and the score");

	/* The same session seen from ship one's machine: the block swaps corners. */
	thisPlayerNum = 1;
	qa_boss_bar_side(false, HUD_LIVES_Y + 11, scoreTop,
	                 "right bar tracks the block moving to the other corner");
	qa_boss_bar_side(true, HUD_LIVES_Y_SPECIAL + 11, scoreTop,
	                 "left bar clears its own ship's shifted rows");

	/* Linked two-player runs the arcade ruleset, so both corners carry name and lives rows
	 * at their stock height with no special held. */
	qa_modes_clear();
	twoPlayerMode = true;
	isNetworkGame = true;
	thisPlayerNum = 1;
	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].items.special = 0;
	qa_boss_bar_side(false, HUD_LIVES_Y + 11, scoreTop,
	                 "the linked pair's right bar sits under player two's lives row");
	qa_boss_bar_side(true, HUD_LIVES_Y + 11, scoreTop,
	                 "...and its left bar under player one's");

	/* The linked pair shows both superbomb stocks, player two's row in the right corner. */
	player[1].superbombs = 2;
	qa_boss_bar_side(false, HUD_LIVES_Y + 11, bombTop,
	                 "player two's superbomb row bounds the right bar");
	qa_boss_bar_side(true, HUD_LIVES_Y + 11, scoreTop,
	                 "...and leaves the left bar on the score");
	player[1].superbombs = 0;
	player[0].superbombs = 1;
	qa_boss_bar_side(true, HUD_LIVES_Y + 11, bombTop,
	                 "player one's superbomb row bounds the left bar");
	player[0].superbombs = 0;

	/* Co-op campaign draws no name or lives rows, so only the scores bound the bars. */
	qa_modes_clear();
	twoPlayerMode = true;
	coopCampaignMode = true;
	isNetworkGame = true;
	qa_boss_bar_side(false, -1, scoreTop, "a clear right corner frees the whole column");
	qa_boss_bar_side(true, -1, scoreTop, "the left column runs from the strip to the score");

	/* One player: the right corner is empty top and bottom. */
	qa_modes_clear();
	qa_boss_bar_side(false, -1, vga_height, "a one-player right bar runs the full height");
	qa_boss_bar_side(true, -1, scoreTop, "the one-player left bar still clears the score");
}

/* ---- 3e. the rear-gun mode toggle is each ship's own --------------------------------- */

/* weapon_mode is per ship and its toggle rides the networked button tuple, so both machines
 * simulate both ships' toggles. */
static void qa_rear_gun_mode_matrix(void)
{
	char label[224];
	const PlayerItems savedItems[2] = { player[0].items, player[1].items };

	/* Two rear bays whose pattern counts differ, so reading the wrong ship's is visible. */
	int wide = 0, narrow = 0;
	for (int id = 1; id <= PORT_NUM; ++id)
	{
		if (weaponPort[id].opnum > weaponPort[wide].opnum)
			wide = id;
		if (narrow == 0 || weaponPort[id].opnum < weaponPort[narrow].opnum)
			narrow = id;
	}
	qa_check(wide > 0 && narrow > 0 && weaponPort[wide].opnum > weaponPort[narrow].opnum,
	         "the weapon ports offer differing rear-gun pattern counts to test against");

	for (int slot = 1; slot <= 2; ++slot)
	for (int mode = 0; mode <= 2; ++mode)
	{
		qa_modes_clear();
		twoPlayerMode = true;
		isNetworkGame = true;
		thisPlayerNum = (uint)slot;
		const char *shape = "Separate arcade";
		if (mode == 1)      { coopCampaignMode = true; shape = "online campaign"; }
		else if (mode == 2) { coopEndlessMode = true; endlessMode = true; shape = "online Endless"; }
		else                { arcadeSeparateMode = true; }

		player[0].items.weapon[REAR_WEAPON].id = (Uint8)narrow;
		player[1].items.weapon[REAR_WEAPON].id = (Uint8)wide;

		snprintf(label, sizeof(label),
		         "%s from machine %d: each ship's rear bay answers with its own pattern count",
		         shape, slot);
		qa_check(JE_portConfigs(&player[0]) == weaponPort[narrow].opnum
		         && JE_portConfigs(&player[1]) == weaponPort[wide].opnum, label);

		/* The toggle's own wrap, run the way JE_playerMovement runs it, on the ship the
		 * partner is flying. Both machines must land on the same mode. */
		player[1].weapon_mode = weaponPort[wide].opnum;   // one short of wrapping
		if (++player[1].weapon_mode > JE_portConfigs(&player[1]))
			player[1].weapon_mode = 1;
		snprintf(label, sizeof(label),
		         "%s from machine %d: ship two's toggle wraps on ship two's own bay",
		         shape, slot);
		qa_check(player[1].weapon_mode == 1, label);

		player[1].weapon_mode = 1;
		if (++player[1].weapon_mode > JE_portConfigs(&player[1]))
			player[1].weapon_mode = 1;
		snprintf(label, sizeof(label),
		         "%s from machine %d: ...and advances instead of wrapping while it has room",
		         shape, slot);
		qa_check(player[1].weapon_mode == 2, label);
	}

	player[0].items = savedItems[0];
	player[1].items = savedItems[1];
	player[0].weapon_mode = player[1].weapon_mode = 1;
	qa_modes_clear();
	isNetworkGame = false;
	thisPlayerNum = 1;
}

/* ---- 3f. a ship that is out leaves nothing behind on the HUD ------------------------- */

/* HUD availability follows player_is_out because the final death leaves the life counter at one. */
static void qa_downed_ship_hud(void)
{
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].items.weapon[FRONT_WEAPON].power = 1;
		player[p].items.weapon[REAR_WEAPON].power = 1;
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;
		player[p].is_alive = true;
		player[p].exploding_ticks = 0;
	}

	qa_modes_clear();
	twoPlayerMode = true;
	qa_check(!player_is_out(1) && hud_lives_count(1) == 1,
	         "a linked-pair ship on its last life still flies it");

	player[1].is_alive = false;
	player[1].exploding_ticks = 60;
	qa_check(!player_is_out(1) && hud_lives_count(1) == 1,
	         "...and holds its icon while the wreck explodes, the way a respawning ship does");

	player[1].exploding_ticks = 0;
	qa_check(player_is_out(1) && hud_lives_count(1) == 0,
	         "...but shows nothing once the explosion ends with no life left to spend");
	qa_check(!player_is_out(0) && hud_lives_count(0) == 1,
	         "the surviving ship's own row is untouched by its partner going down");

	/* A death with a life in hand ends in a respawn, so the row keeps its icons through it. */
	*player[1].lives = 3;
	qa_check(!player_is_out(1) && hud_lives_count(1) == 3,
	         "a ship with lives left keeps its icons through the death that spends one");

	/* The superbomb row follows the same rule. The bottom-band layout measures it through this
	 * same count, so the row a boss bar has to clear goes with the icons. */
	player[1].superbombs = 2;
	qa_check(hud_superbomb_count(1) == 2,
	         "a ship still flying shows the superbombs it can still fire");
	*player[1].lives = 1;
	qa_check(player_is_out(1) && hud_superbomb_count(1) == 0,
	         "...and a ship that is out shows none");
	player[1].superbombs = 0;

	/* Separate arcade counts lives on the front gun, so the same states have to read the same
	 * way through the other binding. */
	*player[1].lives = 1;
	arcadeSeparateMode = true;
	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;
	qa_check(player_is_out(1) && hud_lives_count(1) == 0,
	         "a Separate arcade ship reads out the same way off its own front-gun counter");

	/* Co-op hands out no lives, and the byte the pointer aliases there is a weapon's power.
	 * Reading it as a life count would strand a flying ship at "out" for a level-one front gun. */
	qa_modes_clear();
	twoPlayerMode = true;
	coopCampaignMode = true;
	player[1].is_alive = true;
	qa_check(!player_is_out(1), "a flying co-op ship is never out, whatever its front gun holds");
	player[1].is_alive = false;
	qa_check(player_is_out(1), "...and a downed one is out as soon as its wreck is gone");

	qa_modes_clear();
	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].is_alive = true;
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
		coop_set_session_double_earnings(dbl != 0);

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
		         "campaign %s/%s: P%d's kill cash pays them %ld from machine %d",
		         shared ? "Shared" : "Individual", dbl ? "2x" : "1x", payee + 1, wantPayee, slot);
		qa_check((long)player[payee].cash == wantPayee, label);
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
	coop_set_session_double_earnings(false);
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
	coop_set_session_double_earnings(false);
}

/* ---- 5. the co-op campaign record board --------------------------------------------- */

// A run that started in this episode and is finishing it, which is the only shape that records.
static void qa_campaign_run_at_episode(JE_byte episode)
{
	initial_episode_num = episode;
	episodeNum = episode;
	gameHasRepeated = false;
	constantPlay = false;
}

/* One best run per episode, scored on the two players' combined cash. It is a co-op-only
 * board, so nothing else may write to it. */
static void qa_campaign_score_matrix(void)
{
	char label[224];
	char nameHold[24];

	const JE_byte savedEpisode = initial_episode_num;
	const JE_byte savedEpisodeNum = episodeNum;
	const JE_boolean savedRepeated = gameHasRepeated;
	const JE_boolean savedConstantPlay = constantPlay;
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
		qa_campaign_run_at_episode(1);
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
	qa_campaign_run_at_episode(0);
	coopCampaignScoreNote();
	qa_campaign_run_at_episode(COOP_CAMPAIGN_SCORE_EPISODES + 1);
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
		qa_campaign_run_at_episode((JE_byte)e);
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
	qa_campaign_run_at_episode(1);
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

	/* Campaign records count only the first playthrough of the starting episode. */
	static const char *const carried[3] = {
		"a run now in a later episode", "a repeated game", "demo playback",
	};
	for (int shape = 0; shape < 3; ++shape)
	{
		qa_modes_clear();
		coopCampaignMode = true;
		memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
		qa_campaign_run_at_episode(1);
		if (shape == 0)
			episodeNum = 2;
		else if (shape == 1)
			gameHasRepeated = true;
		else
			constantPlay = true;

		qa_wallets_clear();
		player[0].cash = 400000;
		player[1].cash = 400000;
		coopCampaignScoreNote();

		snprintf(label, sizeof(label), "%s writes no record", carried[shape]);
		qa_check(coopCampaignScores[0].score == 0, label);
	}

	/* The credit rule decides what a figure is worth, so the record carries the one it was earned
	 * on. Double Earnings stands down under Shared, which leaves three states. */
	static const struct { bool shared, doubled; Uint8 want; } creditShapes[3] = {
		{ true,  false, COOP_CREDIT_SHARED },
		{ false, false, COOP_CREDIT_INDIVIDUAL },
		{ false, true,  COOP_CREDIT_INDIVIDUAL_DOUBLED },
	};
	for (int i = 0; i < 3; ++i)
	{
		qa_modes_clear();
		coopCampaignMode = true;
		memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
		qa_campaign_run_at_episode(1);
		initialDifficulty = 2;
		coop_set_session_shared_credit(creditShapes[i].shared);
		coop_set_session_double_earnings(creditShapes[i].doubled);

		qa_wallets_clear();
		player[0].cash = 3000;
		player[1].cash = 3000;
		coopCampaignScoreNote();

		snprintf(label, sizeof(label), "a record earned on %s says so",
		         coopCampaignCreditName(creditShapes[i].want));
		qa_check(coopCampaignScores[0].credit == creditShapes[i].want, label);
	}

	qa_check(coopCampaignCreditName(COOP_CREDIT_UNKNOWN) == NULL,
	         "a record kept before the board carried the rule prints without one");

	memset(coopCampaignScores, 0, sizeof(coopCampaignScores));
	network_opponent_name = savedOpponent;
	initialDifficulty = savedDifficulty;
	constantPlay = savedConstantPlay;
	gameHasRepeated = savedRepeated;
	episodeNum = savedEpisodeNum;
	initial_episode_num = savedEpisode;
	qa_modes_clear();
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
}

/* ---- 5b. the strings the lobby and outpost put on screen ---------------------------- */

/* Check that online menu values exist, render in the game font, and fit their columns. */
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

	/* Two maximum-width lobby names and the run terms must fit the Campaign record column. */
	{
		char widest = 'W';
		int widestPx = 0;
		for (int c = '!'; c < 127; ++c)
		{
			const char one[2] = { (char)c, '\0' };
			if (font_ascii[c] >= 0 && JE_textWidth(one, TINY_FONT) > widestPx)
			{
				widestPx = JE_textWidth(one, TINY_FONT);
				widest = (char)c;
			}
		}

		const int columnWidthPx = coopCampaignRecordLineWidthPx();

		CoopCampaignScore record = { 100000, "", 0, 0 };
		for (size_t i = 0; i < 2 * NET_NAME_MAX + sizeof(" and ") - 1; ++i)
			record.name[i] = widest;
		memcpy(record.name + NET_NAME_MAX, " and ", sizeof(" and ") - 1);

		for (int d = 0; d <= DIFFICULTY_10; ++d)
		for (Uint8 credit = 0; credit < COOP_CREDIT_COUNT; ++credit)
		{
			record.difficulty = (Uint8)d;
			record.credit = credit;

			char line[80];
			coopCampaignRecordLine(line, sizeof(line), &record, columnWidthPx);

			const char *const rule = coopCampaignCreditName(credit);
			snprintf(label, sizeof(label), "the board's widest line on %s, %s fits its column",
			         difficultyNameB[d], rule != NULL ? rule : "no recorded rule");
			qa_check(JE_textWidth(line, TINY_FONT) <= columnWidthPx, label);

			snprintf(label, sizeof(label), "the board's line on %s, %s keeps its terms",
			         difficultyNameB[d], rule != NULL ? rule : "no recorded rule");
			qa_check(strstr(line, difficultyNameB[d]) != NULL
			         && (rule == NULL || strstr(line, rule) != NULL), label);
		}
	}
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

/* ---- hostile inbound packets --------------------------------------------------------- */

#ifdef WITH_NETWORK
// Place a crafted packet at the head of the reliable queue, as network_check would have.
static void qa_inject_packet(const Uint8 *data, int len)
{
	if (packet_in[0] == NULL)
		packet_in[0] = SDLNet_AllocPacket(NET_PACKET_SIZE);
	memcpy(packet_in[0]->data, data, (size_t)len);
	packet_in[0]->len = len;
}

/* The reliable pumps must consume or reject malformed packets without adopting invalid state. */
static void qa_hostile_packets(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedTwo = twoPlayerMode, savedCampaign = coopCampaignMode;
	const bool savedEndless = coopEndlessMode;
	const uint savedThis = thisPlayerNum;
	Player savedPeer = player[1];

	isNetworkGame = true;
	twoPlayerMode = true;
	coopCampaignMode = false;
	coopEndlessMode = true;
	thisPlayerNum = 1;

	Uint8 raw[NET_PACKET_SIZE];

	/* Truncated shop state: too short to parse, still consumed so the queue keeps moving. */
	memset(raw, 0xFF, sizeof(raw));
	SDLNet_Write16(PACKET_SHOP_SYNC, &raw[0]);
	const Sint64 cashBefore = player[1].cash;
	qa_inject_packet(raw, 10);
	qa_check(network_shop_pump() && packet_in[0] == NULL && player[1].cash == cashBefore,
	         "a truncated shop packet is consumed without adopting anything");

	/* Hostile full shop state: every payload byte lit. The out-of-range course index has to
	 * clamp to "nothing committed"; flags are zero so the pump owes no reply. */
	memset(raw, 0xFF, sizeof(raw));
	SDLNet_Write16(PACKET_SHOP_SYNC, &raw[0]);
	SDLNet_Write16(2, &raw[4]);        // sender: the peer
	SDLNet_Write16(1000, &raw[6]);     // sequence, past anything seen
	SDLNet_Write16(0, &raw[8]);        // flags
	qa_inject_packet(raw, 44);
	qa_check(network_shop_pump() && network_shop_peer_course() == -1,
	         "an out-of-range charted course from a hostile packet reads as none");

	/* Truncated debug block: refused (the pump only adopts whole blocks), left for its caller. */
	memset(raw, 0xEE, sizeof(raw));
	SDLNet_Write16(PACKET_DEBUG_SYNC, &raw[0]);
	qa_inject_packet(raw, 8);
	qa_check(!network_debug_sync_pump(false) && packet_in[0] != NULL,
	         "a truncated debug block is refused rather than adopted");
	network_update();

	/* Custom weapon chunk with absurd counts: the clamps have to reject it before the memcpy
	 * and allocation it would otherwise size. */
	memset(raw, 0xFF, sizeof(raw));
	SDLNet_Write16(PACKET_CUSTOM_WEAPON, &raw[0]);
	raw[4] = 2;                        // owner: the peer
	SDLNet_Write16(1, &raw[6]);        // generation
	SDLNet_Write16(0xFFFF, &raw[8]);   // chunk index
	SDLNet_Write16(0xFFFF, &raw[10]);  // chunk count
	SDLNet_Write16(0xFFFF, &raw[12]);  // payload length
	qa_inject_packet(raw, 20);
	qa_check(network_shop_pump() && packet_in[0] == NULL,
	         "an absurd custom-weapon chunk is consumed and refused");

	/* The shop pump must leave a death choice queued for its owner across barrier skew. */
	memset(raw, 0, sizeof(raw));
	SDLNet_Write16(PACKET_ENDLESS_RUN, &raw[0]);
	SDLNet_Write16(2, &raw[8]);        // host chose End Run
	SDLNet_Write16(0xFFFF, &raw[10]);  // death-choice sentinel
	qa_inject_packet(raw, 14);
	qa_check(!network_shop_pump() && packet_in[0] != NULL,
	         "an Endless death choice is left queued for the death wait");
	network_update();

	/* A stray Endless run chunk outside the resume wait is a late duplicate; dropped. */
	memset(raw, 0xDD, sizeof(raw));
	SDLNet_Write16(PACKET_ENDLESS_RUN, &raw[0]);
	qa_inject_packet(raw, 24);
	qa_check(network_shop_pump() && packet_in[0] == NULL,
	         "a stray Endless run chunk is consumed as a late duplicate");

	player[1] = savedPeer;
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedEndless;
	thisPlayerNum = savedThis;
}

/* A co-op quit reopens the outpost on both machines with the quitter's notice still at the head
 * of the other one's reliable queue, where the level leaves it for its handler. */
static void qa_quit_notice_retire(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedTwo = twoPlayerMode, savedCampaign = coopCampaignMode;
	const bool savedEndless = coopEndlessMode;
	const uint savedThis = thisPlayerNum;
	const Player savedPeer = player[1];

	isNetworkGame = true;
	twoPlayerMode = true;
	coopCampaignMode = false;
	coopEndlessMode = true;
	thisPlayerNum = 1;

	Uint8 raw[NET_PACKET_SIZE];

	// The peer's quit at the head, its first outpost packet queued behind it.
	memset(raw, 0, sizeof(raw));
	SDLNet_Write16(PACKET_GAME_QUIT, &raw[0]);
	qa_inject_packet(raw, 4);

	memset(raw, 0, sizeof(raw));
	SDLNet_Write16(PACKET_SHOP_SYNC, &raw[0]);
	SDLNet_Write16(2, &raw[4]);        // sender: the peer
	SDLNet_Write16(5000, &raw[6]);     // sequence, past anything seen
	SDLNet_Write16(0, &raw[8]);        // flags: plain state, so the pump owes no reply
	net_bytes_write64(4321, &raw[14]); // cash, the adopted field the check reads back
	if (packet_in[1] == NULL)
		packet_in[1] = SDLNet_AllocPacket(NET_PACKET_SIZE);
	memcpy(packet_in[1]->data, raw, 44);
	packet_in[1]->len = 44;

	qa_check(!network_shop_pump() && network_shop_departure_pending(),
	         "a queued quit blocks the outpost pump and reads as a departure");
	qa_check(network_quit_notice_retire() && network_inbound_head() == PACKET_SHOP_SYNC,
	         "opening the outpost retires the peer's quit notice");
	qa_check(network_shop_pump() && packet_in[0] == NULL && player[1].cash == 4321,
	         "...and the shop packet behind it is read");
	qa_check(!network_quit_notice_retire(), "there is nothing to retire off an empty queue");

	// A rollback menu release a level-end timeout stranded goes the same way as the quit.
	memset(raw, 0, sizeof(raw));
	SDLNet_Write16(PACKET_GAME_MENU, &raw[0]);
	qa_inject_packet(raw, 4);
	qa_check(!network_shop_pump() && network_quit_notice_retire() && packet_in[0] == NULL,
	         "opening the outpost retires a stranded in-game menu release");

	player[1] = savedPeer;
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedEndless;
	thisPlayerNum = savedThis;
}
#endif

/* ---- the debug-menu wire block ------------------------------------------------------- */

#ifdef WITH_NETWORK
/* Everything the Endless debug panel can move, so a test can rewrite it all and put it back. */
typedef struct {
	JE_boolean coop;
	int        depth;
	Uint64     mods;
	Uint64     live[2];
	unsigned   purchased[2];
	JE_byte    perks[2][64];
} QaEndlessDebugState;

static void qa_endless_debug_save(QaEndlessDebugState *s)
{
	s->coop = coopEndlessMode;
	s->depth = endlessRunDepth;
	s->mods = endlessActiveMods;
	for (uint p = 0; p < 2; ++p)
	{
		s->live[p] = endlessPlayerMods[p];
		s->purchased[p] = endlessPurchasedMods[p];
		for (int i = 0; i < endlessPerkCount() && i < (int)COUNTOF(s->perks[p]); ++i)
			s->perks[p][i] = (JE_byte)endlessPerkGetOwnedFor(p, i);
	}
}

static void qa_endless_debug_restore(const QaEndlessDebugState *s)
{
	coopEndlessMode = s->coop;
	endlessRunDepth = s->depth;
	endlessActiveMods = s->mods;
	for (uint p = 0; p < 2; ++p)
	{
		endlessPlayerMods[p] = s->live[p];
		endlessPurchasedMods[p] = s->purchased[p];
		for (int i = 0; i < endlessPerkCount() && i < (int)COUNTOF(s->perks[p]); ++i)
			endlessPerkSetOwnedFor(p, i, s->perks[p][i]);
	}
}

/* Round-trip every debug setting through the wire block, then compare the complete re-packed
 * block. A menu field omitted from the protocol leaves a detectable mutation behind. */
static void qa_debug_block_roundtrip(void)
{
	const int size = network_debug_state_size();
	Uint8 published[320], readback[320];

	if (size <= 0 || (size_t)size > sizeof(published))
	{
		qa_check(false, "the debug wire block fits the packet buffer");
		return;
	}

	const JE_boolean savedNet = isNetworkGame;
	Player savedShips[2];
	memcpy(savedShips, player, sizeof(savedShips));
	isNetworkGame = true;

	// JE_getShipInfo runs inside the adopt and reads both hull ceilings through this.
	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;

	difficultyLevel        = DIFFICULTY_HARD;
	cheatInfiniteShields   = true;
	cheatInfiniteArmor     = false;
	cheatInfiniteGenerator = true;
	cheatNoEnemyFire       = true;
	cheatInstantCharge     = false;
	cheatInfiniteSidekickAmmo = true;
	autoFireSpecial        = true;
	debugAutofireTwiddle   = false;
	debugToggleFire        = true;
	expertMode             = true;
	difficultyAdjust       = false;
	noclipMode             = 1;
	chargeSidekickAutofire = 2;
	debugTwiddleSpecial    = 3;
	expertBossHpMult       = 7;
	expertEnemyArmorPct    = 150;
	network_debug_state_pack(published);

	// Opposite of every one of them, so a field the block does not carry stays visibly wrong.
	difficultyLevel        = DIFFICULTY_WIMP;
	cheatInfiniteShields   = false;
	cheatInfiniteArmor     = true;
	cheatInfiniteGenerator = false;
	cheatNoEnemyFire       = false;
	cheatInstantCharge     = true;
	cheatInfiniteSidekickAmmo = false;
	autoFireSpecial        = false;
	debugAutofireTwiddle   = true;
	debugToggleFire        = false;
	expertMode             = false;
	difficultyAdjust       = true;
	noclipMode             = 2;
	chargeSidekickAutofire = 0;
	debugTwiddleSpecial    = 9;
	expertBossHpMult       = 2;
	expertEnemyArmorPct    = 250;

	network_debug_state_adopt(published, false);

	qa_check(difficultyLevel == DIFFICULTY_HARD, "the debug block carries the difficulty");
	qa_check(cheatInfiniteShields && !cheatInfiniteArmor && cheatInfiniteGenerator
	         && cheatNoEnemyFire && !cheatInstantCharge && cheatInfiniteSidekickAmmo,
	         "the debug block carries every cheat flag");
	qa_check(autoFireSpecial && !debugAutofireTwiddle && debugToggleFire
	         && expertMode && !difficultyAdjust,
	         "the debug block carries the firing and expert-mode flags");
	qa_check(noclipMode == 1, "the debug block carries noclip");
	qa_check(chargeSidekickAutofire == 2, "the debug block carries the sidekick autofire mode");
	qa_check(debugTwiddleSpecial == 3, "the debug block carries the armed twiddle");
	qa_check(expertBossHpMult == 7 && expertEnemyArmorPct == 150,
	         "the debug block carries the expert tunables");

	/* Compare the complete block so newly added settings cannot be omitted silently. */
	network_debug_state_pack(readback);
	qa_check(memcmp(published, readback, (size_t)size) == 0,
	         "a debug block re-packed after adopting it is the same block");

	/* Hostile bytes. These arrive from a peer and index tables directly, so the adopt clamps them
	 * rather than trusting the sender. */
	Uint8 hostile[320];
	memcpy(hostile, published, (size_t)size);
	hostile[9]  = 0x7f;   // difficulty, well past DIFFICULTY_10
	hostile[12] = 0xff;   // noclip
	hostile[13] = 0xff;   // charge autofire
	hostile[14] = 0xff;   // armed twiddle
	network_debug_state_adopt(hostile, false);

	qa_check(difficultyLevel >= DIFFICULTY_WIMP && difficultyLevel <= DIFFICULTY_10,
	         "an out-of-range difficulty from a peer is clamped, not indexed with");
	qa_check(noclipMode < NOCLIP_NUM, "an out-of-range noclip mode from a peer is clamped");
	qa_check(chargeSidekickAutofire < CHARGE_AUTOFIRE_NUM,
	         "an out-of-range sidekick autofire mode from a peer is clamped");
	qa_check(debugTwiddleSpecial <= SPECIAL_NUM,
	         "an out-of-range armed twiddle from a peer is clamped");

	/* The block carries Endless depth, modifiers, perks, and personal buffs. */
	QaEndlessDebugState savedEndlessState;
	qa_endless_debug_save(&savedEndlessState);

	coopEndlessMode = true;
	endlessRunDepth = 41;
	endlessActiveMods = 0x8000000400000002ull;   // both halves of the 64-bit mask
	for (uint p = 0; p < 2; ++p)
		for (int i = 0; i < endlessPerkCount(); ++i)
			endlessPerkSetOwnedFor(p, i, 0);
	endlessPerkSetOwnedFor(0, 1, 3);
	endlessPerkSetOwnedFor(1, 1, 2);
	endlessSetPersonalBuffMods(0, ENDLESS_MOD_TURBODRIVE);
	endlessSetPersonalBuffMods(1, ENDLESS_MOD_OVERDRIVE);
	network_debug_state_pack(published);

	// Opposite of every one of them, so a field the block does not carry stays visibly wrong.
	endlessRunDepth = 0;
	endlessActiveMods = 0;
	endlessPerkSetOwnedFor(0, 1, 0);
	endlessPerkSetOwnedFor(1, 1, 0);
	endlessSetPersonalBuffMods(0, 0);
	endlessSetPersonalBuffMods(1, 0);
	network_debug_state_adopt(published, false);

	qa_check(endlessRunDepth == 41 && endlessActiveMods == 0x8000000400000002ull,
	         "the debug block carries the Endless depth and its whole 64-bit modifier mask");
	qa_check(endlessPerkGetOwnedFor(0, 1) == 3 && endlessPerkGetOwnedFor(1, 1) == 2,
	         "...and both ships' perk rows, each on its own ship");
	qa_check(endlessPersonalBuffMods(0) == ENDLESS_MOD_TURBODRIVE
	         && endlessPersonalBuffMods(1) == ENDLESS_MOD_OVERDRIVE,
	         "...and each ship's personal buffs");
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_OVERDRIVE) == 0
	         && (endlessPlayerMods[1] & ENDLESS_MOD_TURBODRIVE) == 0,
	         "...without either ship picking up the other's");

	/* Mid-zone sync preserves both consumed purchases and their live buff mask. */
	endlessPlayerMods[0] = ENDLESS_MOD_TURBODRIVE;
	endlessPlayerMods[1] = 0;
	endlessPurchasedMods[0] = 0;
	endlessPurchasedMods[1] = ENDLESS_MOD_OVERDRIVE;
	network_debug_state_pack(published);
	endlessPlayerMods[0] = endlessPlayerMods[1] = 0;
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;
	network_debug_state_adopt(published, false);
	qa_check(endlessPlayerMods[0] == ENDLESS_MOD_TURBODRIVE && endlessPlayerMods[1] == 0,
	         "a buff a sector already consumed survives the block on the ship still flying it");
	qa_check(endlessPurchasedMods[0] == 0 && endlessPurchasedMods[1] == ENDLESS_MOD_OVERDRIVE,
	         "...and a buff still waiting at the outpost stays waiting, on its own ship");

	qa_endless_debug_restore(&savedEndlessState);
	memcpy(player, savedShips, sizeof(savedShips));
	isNetworkGame = savedNet;
}
#endif

/* ---- 9. the two one-player rulesets, flown online ------------------------------------ */

/* Online SuperTyrian and Super Arcade give each ship independent state. These
 * cases catch code that still reads the old session-wide ruleset. */
static void qa_super_online_matrix(void)
{
	char label[224];
	const PlayerItems savedItems[2] = { player[0].items, player[1].items };

	/* --- the loadouts each mode issues --- */

	/* Super Arcade: each player picked their own, and both machines equip both ships from the
	 * settled pair. Every ship in the table, against every other. */
	for (int mine = 1; mine <= SA; ++mine)
	for (int theirs = 1; theirs <= SA; ++theirs)
	{
		networkSuperArcadeEquip(&player[0], mine);
		networkSuperArcadeEquip(&player[1], theirs);

		snprintf(label, sizeof(label),
		         "Super Arcade ships %d and %d each fly their own hull, gun and special",
		         mine, theirs);
		qa_check(player[0].items.ship == SAShip[mine - 1]
		         && player[1].items.ship == SAShip[theirs - 1]
		         && player[0].items.weapon[FRONT_WEAPON].id == SAWeapon[mine - 1][0]
		         && player[1].items.weapon[FRONT_WEAPON].id == SAWeapon[theirs - 1][0]
		         && player[0].items.special == SASpecialWeapon[mine - 1]
		         && player[1].items.special == SASpecialWeapon[theirs - 1], label);

		snprintf(label, sizeof(label),
		         "...and carries its own ruleset on the ship, not in a session global (%d/%d)",
		         mine, theirs);
		qa_check(player_sa_ship(&player[0]) == (uint)mine
		         && player_sa_ship(&player[1]) == (uint)theirs, label);

		/* Super Arcade issues no rear gun, so the rear bay must not be inherited from whatever
		 * the previous game left in the slot. */
		snprintf(label, sizeof(label), "Super Arcade ship %d takes no rear gun", mine);
		qa_check(player[0].items.weapon[REAR_WEAPON].id == 0, label);

		/* The ball table is what the colour slots resolve against: every slot, both ships. */
		for (uint slot = 0; slot < COUNTOF(SAWeapon[0]); ++slot)
		{
			snprintf(label, sizeof(label),
			         "colour slot %u pays ship %d and ship %d out of their own arsenals",
			         slot, mine, theirs);
			qa_check(player_sa_ball_weapon(&player[0], slot) == SAWeapon[mine - 1][slot]
			         && player_sa_ball_weapon(&player[1], slot) == SAWeapon[theirs - 1][slot],
			         label);
		}
	}

	/* SuperTyrian replaces the linked pair's inherited loadout on both ships. */
	player[0].items.weapon[REAR_WEAPON].id = 5;    // whatever the previous game left in the bay
	player[1].items.weapon[REAR_WEAPON].id = 15;   // the Dragonwing's own Vulcan Cannon
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		networkSuperTyrianEquip(&player[i]);

		snprintf(label, sizeof(label), "online SuperTyrian ship %u flies the Stalker and RailGun", i + 1);
		qa_check(player[i].items.ship == 13
		         && player[i].items.weapon[FRONT_WEAPON].id == 39
		         && player[i].items.super_arcade_mode == SA_SUPERTYRIAN, label);

		snprintf(label, sizeof(label), "...and takes no rear gun (ship %u)", i + 1);
		qa_check(player[i].items.weapon[REAR_WEAPON].id == 0, label);
	}

	/* The Nort Ship is the one hull that arrives with sidekicks attached. */
	networkSuperArcadeEquip(&player[0], SA_NORTSHIPZ);
	qa_check(player[0].items.sidekick[LEFT_SIDEKICK] == 24
	         && player[0].items.sidekick[RIGHT_SIDEKICK] == 24,
	         "the Nort Ship still brings its pair of Companion Ship Quicksilvers online");

	/* One color maps to a different gun for each selected ship. */
	networkSuperArcadeEquip(&player[0], 1);
	networkSuperArcadeEquip(&player[1], 2);
	bool anyDiffer = false;
	for (uint slot = 0; slot < COUNTOF(SAWeapon[0]); ++slot)
		if (player_sa_ball_weapon(&player[0], slot) != player_sa_ball_weapon(&player[1], slot))
			anyDiffer = true;
	qa_check(anyDiffer, "one colour ball hands two different Super Arcade ships different guns");

	/* Identical picks are allowed, and then the same colour does pay the same gun. */
	networkSuperArcadeEquip(&player[1], 1);
	qa_check(player_sa_ball_weapon(&player[0], 2) == player_sa_ball_weapon(&player[1], 2),
	         "two players who picked the same ship get the same gun off the same colour");

	/* Hostile inputs: the slot indexes a fixed-width row, and the ruleset byte rides the save
	 * record and the wire. Neither may read past its table. */
	qa_check(player_sa_ball_weapon(&player[0], 99) == SAWeapon[0][COUNTOF(SAWeapon[0]) - 1],
	         "a colour slot past the table clamps to the last one instead of reading past it");
	player[1].items.super_arcade_mode = 200;   // neither a hull nor SuperTyrian
	qa_check(player_sa_ship(&player[1]) == (uint)SA_NONE
	         && player_sa_ball_weapon(&player[1], 0) == SAWeapon[0][0],
	         "an out-of-range ruleset byte reads as no Super Arcade ship, on the first table");
	player[1].items.super_arcade_mode = SA_SUPERTYRIAN;
	qa_check(player_sa_ship(&player[1]) == (uint)SA_NONE,
	         "SuperTyrian is not a Super Arcade hull, so it indexes no ball table");

	/* --- what the difficulty field means, per game type --- */

	/* Host and joiner must derive the same initial difficulty for every game type. */
	const NetworkGameType savedType = network_game_type;
	static const struct { NetworkGameType type; bool separate; int bump; const char *why; } bumps[] =
	{
		{ NETWORK_GAME_ARCADE,      false, 1, "the linked pair concentrates two players' fire on one hull" },
		{ NETWORK_GAME_ARCADE,      true,  0, "two Separate personal arcades keep the solo curve" },
		{ NETWORK_GAME_CAMPAIGN,    false, 0, "an online campaign flies two full ships, like Endless" },
		{ NETWORK_GAME_ENDLESS,     false, 0, "online Endless flies two full ships" },
		{ NETWORK_GAME_SUPERTYRIAN, true,  0, "SuperTyrian keeps its own curve; the field is its variant" },
		{ NETWORK_GAME_SUPERARCADE, true,  0, "Super Arcade plays the rung the host picked, not a step above it" },
	};
	for (uint i = 0; i < COUNTOF(bumps); ++i)
	{
		qa_modes_clear();
		twoPlayerMode = true;
		isNetworkGame = true;
		network_game_type = bumps[i].type;
		if (bumps[i].type == NETWORK_GAME_CAMPAIGN)  coopCampaignMode = true;
		if (bumps[i].type == NETWORK_GAME_ENDLESS) { coopEndlessMode = true; endlessMode = true; }
		arcadeSeparateMode = bumps[i].separate;

		snprintf(label, sizeof(label), "difficulty bump for game type %d (%s ships): %s",
		         (int)bumps[i].type, bumps[i].separate ? "Separate" : "Linked", bumps[i].why);
		qa_check(networkDifficultyBump() == bumps[i].bump, label);

		/* And the round trip the two machines actually perform. */
		const int lobbyPick = DIFFICULTY_NORMAL;
		const int sent = lobbyPick + networkDifficultyBump();
		snprintf(label, sizeof(label),
		         "...and the joiner recovers the host's own initialDifficulty from it (type %d)",
		         (int)bumps[i].type);
		qa_check(sent - networkDifficultyBump() == lobbyPick, label);
	}
	network_game_type = savedType;

	/* SuperTyrian's two variants ride the difficulty field, because that is what they are: the
	 * solo mode reads Scroll Lock and picks between the same two rungs. */
	qa_check(DIFFICULTY_LORD_OF_GAME != DIFFICULTY_SUICIDE,
	         "SuperTyrian's Standard and Scrollock variants are two distinct rungs");

	/* --- SuperTyrian's own exclusions, now that a second ship exists --- */

	qa_modes_clear();
	twoPlayerMode = true;
	arcadeSeparateMode = true;
	superTyrian = true;
	qa_check(dual_ship_mode() && arcade_separate_mode() && !coop_mode_active(),
	         "online SuperTyrian runs as two personal ships, not as the linked pair");
	const bool savedBoost = arcadeLifeBoost, savedRear = arcadeRearGunScale;
	arcadeLifeBoost = arcadeRearGunScale = true;
	qa_check(!arcade_life_scaling_active() && !arcade_rear_scale_active(),
	         "...and is still excluded from hull scaling and rear-gun scaling, as it is solo");
	arcadeLifeBoost = savedBoost;
	arcadeRearGunScale = savedRear;

	player[0].items = savedItems[0];
	player[1].items = savedItems[1];
	qa_modes_clear();
	isNetworkGame = false;
}

/* ---- 10. the Super Arcade ship picker ------------------------------------------------ */

/* Measure the real ship names against the two-column mouse and keyboard layout. */
static void qa_sa_picker_layout(void)
{
	char label[224];

	// The hull sprite is blitted 2x2 under the list, and the two status lines sit under that.
	const int listBottom = sa_pick_name_y(SA_PICK_ROWS - 1) + SA_PICK_ROW_H;
	qa_check(listBottom <= SA_PICK_SHIP_Y,
	         "the ship list ends before the hull picture starts");
	qa_check(SA_PICK_SHIP_Y + 28 <= SA_PICK_STATUS_Y && SA_PICK_STATUS_Y < SA_PICK_PEER_Y
	         && SA_PICK_PEER_Y + 8 <= 200,
	         "the hull, the status line and the partner's pick stack without overlapping");
	qa_check(sa_pick_name_y(0) >= SA_PICK_HEADER_Y + 15,
	         "the first row clears the header");

	for (int i = 0; i < SA; ++i)
	{
		const int x = sa_pick_name_x(i);
		const int w = JE_textWidth(superShips[i + 1], small_font);

		snprintf(label, sizeof(label), "ship name %d (\"%s\") fits its column",
		         i + 1, superShips[i + 1]);
		// The right column has to stop inside the 320px field; the left, before the right starts.
		qa_check(w > 0 && x + w <= (i < SA_PICK_ROWS ? SA_PICK_COL_X + SA_PICK_COL_DX : 320) - 4,
		         label);

		// Two names on the same row in different columns must not share a pixel.
		if (i >= SA_PICK_ROWS)
		{
			const int left = i - SA_PICK_ROWS;
			snprintf(label, sizeof(label), "ship names %d and %d share a row without colliding",
			         left + 1, i + 1);
			qa_check(sa_pick_name_y(i) == sa_pick_name_y(left)
			         && sa_pick_name_x(left) + JE_textWidth(superShips[left + 1], small_font) < x,
			         label);
		}
	}

	// Every index has a distinct cell, so a click can only ever mean one ship.
	for (int i = 0; i < SA; ++i)
	for (int j = i + 1; j < SA; ++j)
	{
		snprintf(label, sizeof(label), "picker cells %d and %d are distinct", i + 1, j + 1);
		qa_check(sa_pick_name_x(i) != sa_pick_name_x(j) || sa_pick_name_y(i) != sa_pick_name_y(j),
		         label);
	}

	// The status lines the screen can show, against the same field the names use.
	static const char *const status[] =
	{
		"Choose your ship.", "Waiting for the other player...", "Both ready.",
		SA_PICK_UNPICK_HINT,
	};
	for (uint i = 0; i < COUNTOF(status); ++i)
	{
		snprintf(label, sizeof(label), "picker status line \"%s\" fits the screen", status[i]);
		qa_check(JE_textWidth(status[i], small_font) <= 300, label);
	}
	for (int i = 0; i < SA; ++i)
	{
		char line[64];
		snprintf(line, sizeof(line), "Player 2 flies %s", superShips[i + 1]);
		snprintf(label, sizeof(label), "the partner's pick line fits for ship %d", i + 1);
		qa_check(JE_textWidth(line, small_font) <= 300, label);
	}
}

/* ---- 11. saving a session that flies two complete arcade ships ----------------------- */

/* Two-complete-ship saves need all four weapon powers and both rear-fire modes.
 * Arcade also derives ship two's life count from its front-gun power. */
static void qa_dual_arcade_save_roundtrip(void)
{
	// The two-player LAST LEVEL slot, which is the one an online arcade session writes and the
	// one its resume reads. Saved and put back at the end, so a real record here survives.
	enum { QA_SAVE_SLOT = 22 };
	char label[224];
	const PlayerItems savedItems[2] = { player[0].items, player[1].items };
	const JE_SaveFileType savedSlot = saveFiles[QA_SAVE_SLOT - 1];
	const int savedLevel = saveLevel;

	for (int mode = 0; mode < 3; ++mode)
	{
		const char *const shape = mode == 0 ? "Separate arcade"
		                        : mode == 1 ? "online Super Arcade" : "online SuperTyrian";
		qa_modes_clear();
		twoPlayerMode = true;
		arcadeSeparateMode = true;
		isNetworkGame = true;
		saveLevel = 3;

		if (mode == 1)
		{
			networkSuperArcadeEquip(&player[0], 3);
			networkSuperArcadeEquip(&player[1], 6);
			// The session global carries ship one's pick (networkStartScreen), and JE_saveGame
			// writes it into ship one's block; without it the record would say SA_NONE there.
			superArcadeMode = (JE_byte)player[0].items.super_arcade_mode;
		}
		else
		{
			for (uint p = 0; p < COUNTOF(player); ++p)
			{
				player[p].items.ship = mode == 2 ? 13 : 8;
				player[p].items.weapon[FRONT_WEAPON].id = mode == 2 ? 39 : 1;
				player[p].items.weapon[REAR_WEAPON].id = 15;
				player[p].items.super_arcade_mode = mode == 2 ? SA_SUPERTYRIAN : SA_NONE;
			}
			superTyrian = (mode == 2);
		}

		/* Distinct values in all four bays and both modes, so a crossed field is visible. */
		player[0].items.weapon[FRONT_WEAPON].power = 5;   // ship one's lives
		player[0].items.weapon[REAR_WEAPON].power = 9;
		player[1].items.weapon[FRONT_WEAPON].power = 2;   // ship two's lives
		player[1].items.weapon[REAR_WEAPON].power = 7;
		player[0].weapon_mode = 3;
		player[1].weapon_mode = 4;
		for (uint p = 0; p < COUNTOF(player); ++p)
			player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;
		const PlayerItems wrote[2] = { player[0].items, player[1].items };

		JE_saveGame(QA_SAVE_SLOT, "QA DUAL SHIPS ");
		const JE_SaveFileType *const rec = &saveFiles[QA_SAVE_SLOT - 1];

		snprintf(label, sizeof(label), "a %s record is marked dual-ship, and NOT as co-op", shape);
		qa_check(save_record_is_dual_arcade(rec) && !save_record_is_coop(rec), label);

		/* Wipe the live loadouts, then read the record back the way a resume does. */
		memset(&player[0].items, 0, sizeof(player[0].items));
		memset(&player[1].items, 0, sizeof(player[1].items));
		player[0].weapon_mode = player[1].weapon_mode = 0;
		JE_loadGameRecord(rec, true);

		snprintf(label, sizeof(label), "%s: both hulls and both front guns come back", shape);
		qa_check(player[0].items.ship == wrote[0].ship && player[1].items.ship == wrote[1].ship
		         && player[0].items.weapon[FRONT_WEAPON].id == wrote[0].weapon[FRONT_WEAPON].id
		         && player[1].items.weapon[FRONT_WEAPON].id == wrote[1].weapon[FRONT_WEAPON].id,
		         label);

		snprintf(label, sizeof(label),
		         "%s: all four bay powers survive, so ship two keeps its own life count", shape);
		qa_check(player[0].items.weapon[FRONT_WEAPON].power == 5
		         && player[0].items.weapon[REAR_WEAPON].power == 9
		         && player[1].items.weapon[FRONT_WEAPON].power == 2
		         && player[1].items.weapon[REAR_WEAPON].power == 7, label);

		snprintf(label, sizeof(label), "%s: each ship keeps its own rear-gun mode", shape);
		qa_check(player[0].weapon_mode == 3 && player[1].weapon_mode == 4, label);

		snprintf(label, sizeof(label),
		         "%s: the load rebinds each ship's life counter to the bay this shape uses", shape);
		qa_check(player[0].lives == &player[0].items.weapon[FRONT_WEAPON].power
		         && player[1].lives == &player[1].items.weapon[FRONT_WEAPON].power
		         && *player[1].lives == 2, label);

		snprintf(label, sizeof(label), "%s: each ship's own ruleset rides the record", shape);
		qa_check(save_record_sa_ship(rec, 0) == (uint)wrote[0].super_arcade_mode
		         && save_record_sa_ship(rec, 1) == (uint)wrote[1].super_arcade_mode
		         && player[0].items.super_arcade_mode == wrote[0].super_arcade_mode
		         && player[1].items.super_arcade_mode == wrote[1].super_arcade_mode, label);

		/* And which lobby may pick that record up. The three arcade types share one slot page,
		 * so the record is the only thing that says which of them wrote it. */
		const NetworkGameType savedType = network_game_type;
		// In the same order as the modes above: Separate, Super Arcade, SuperTyrian.
		static const NetworkGameType arcadeTypes[] =
		{
			NETWORK_GAME_ARCADE, NETWORK_GAME_SUPERARCADE, NETWORK_GAME_SUPERTYRIAN,
		};
		for (uint t = 0; t < COUNTOF(arcadeTypes); ++t)
		{
			network_game_type = arcadeTypes[t];
			const bool want = ((int)t == mode);
			snprintf(label, sizeof(label), "a %s record %s offered to a game-type-%d lobby",
			         shape, want ? "is" : "is not", (int)arcadeTypes[t]);
			qa_check(save_type_compatible(rec, QA_SAVE_SLOT, true) == want, label);
		}

		/* ...and never to the linked pair, which would put ship two's lives on the wrong bay. */
		network_game_type = NETWORK_GAME_ARCADE;
		arcadeSeparateMode = false;
		snprintf(label, sizeof(label), "a %s record is not offered to a Linked arcade lobby", shape);
		qa_check(!save_type_compatible(rec, QA_SAVE_SLOT, true), label);
		snprintf(label, sizeof(label), "...nor to a local two-player game (%s)", shape);
		isNetworkGame = false;
		qa_check(!save_type_compatible(rec, QA_SAVE_SLOT, true)
		         && !save_type_compatible(rec, QA_SAVE_SLOT, false), label);
		isNetworkGame = true;
		network_game_type = savedType;
	}

	/* The linked pair still writes a plain record, and still gets it back. */
	qa_modes_clear();
	twoPlayerMode = true;
	isNetworkGame = true;
	saveLevel = 3;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].items.ship = p == 0 ? 11 : 12;
		player[p].items.super_arcade_mode = SA_NONE;
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;
	}
	player[1].items.weapon[REAR_WEAPON].power = 6;   // the Dragonwing's lives
	JE_saveGame(QA_SAVE_SLOT, "QA LINKED PAIR");
	qa_check(!save_record_is_dual_arcade(&saveFiles[QA_SAVE_SLOT - 1])
	         && !save_record_is_coop(&saveFiles[QA_SAVE_SLOT - 1]),
	         "a linked arcade pair still writes a plain record, marked neither way");
	network_game_type = NETWORK_GAME_ARCADE;
	qa_check(save_type_compatible(&saveFiles[QA_SAVE_SLOT - 1], QA_SAVE_SLOT, true),
	         "...which a Linked arcade lobby still loads");
	JE_loadGameRecord(&saveFiles[QA_SAVE_SLOT - 1], true);
	qa_check(player[1].lives == &player[1].items.weapon[REAR_WEAPON].power
	         && *player[1].lives == 6,
	         "...and the Dragonwing's life counter is still its rear bay");

	saveFiles[QA_SAVE_SLOT - 1] = savedSlot;
	saveLevel = savedLevel;
	player[0].items = savedItems[0];
	player[1].items = savedItems[1];
	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;
	superArcadeMode = SA_NONE;   // the loads above adopted the records' rulesets
	qa_modes_clear();
	isNetworkGame = false;
}

#ifdef WITH_NETWORK
/* ---- 12. the Super Arcade ship announcement ------------------------------------------ */

/* Each player publishes a Super Arcade ship and acknowledges the peer's pick.
 * Zero withdraws a pick; no player may leave while the peer can still change. */
static void qa_sa_ship_packet(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const uint savedThis = thisPlayerNum;

	isNetworkGame = true;
	thisPlayerNum = 1;

	Uint8 raw[NET_PACKET_SIZE];
	memset(raw, 0, sizeof(raw));
	SDLNet_Write16(PACKET_SA_SHIP, &raw[0]);

	network_sa_ship_reset();
	qa_check(network_sa_ship_peer() == 0, "no pick has arrived until one does");

	/* A well-formed announcement from the peer, carrying their acknowledgement of ours. */
	raw[4] = 2;  raw[5] = 5;  raw[6] = 1;
	qa_inject_packet(raw, 7);
	qa_check(network_sa_ship_peer() == 5 && packet_in[0] == NULL,
	         "the peer's pick is adopted and its packet retired from the queue");
	qa_check(network_sa_ship_peer_saw_us(), "...and their acknowledgement of our own comes with it");

	/* Zero is the pick taken back: the peer is choosing again, so they no longer hold ours. */
	raw[5] = 0;  raw[6] = 0;
	qa_inject_packet(raw, 7);
	qa_check(network_sa_ship_peer() == 0 && !network_sa_ship_peer_saw_us(),
	         "ship 0 takes the peer's pick back, and their acknowledgement with it");

	/* Out of range: refused, and the pick already held stands. */
	raw[5] = 6;  raw[6] = 0;
	qa_inject_packet(raw, 7);
	qa_check(network_sa_ship_peer() == 6, "a fresh pick replaces the one that was taken back");
	raw[5] = (Uint8)(SA + 1);
	qa_inject_packet(raw, 7);
	qa_check(network_sa_ship_peer() == 6, "a ship past the table does not replace the pick");
	raw[5] = 255;
	qa_inject_packet(raw, 7);
	qa_check(network_sa_ship_peer() == 6, "...nor does a byte that is not a ship at all");

	/* An announcement short of the acknowledgement byte reads as a pick nobody has answered. */
	raw[5] = 4;  raw[6] = 1;
	qa_inject_packet(raw, 6);
	qa_check(network_sa_ship_peer() == 4 && !network_sa_ship_peer_saw_us(),
	         "an announcement with no acknowledgement byte is a pick and nothing more");

	/* Ignore this machine's reflected announcement. */
	network_sa_ship_reset();
	raw[4] = 1;  raw[5] = 4;  raw[6] = 1;
	qa_inject_packet(raw, 7);
	qa_check(network_sa_ship_peer() == 0 && !network_sa_ship_peer_saw_us(),
	         "a machine's own announcement coming back is not read as its partner's");

	/* Truncated: adopted from nobody, but still retired, or it blocks the queue for good. */
	network_sa_ship_reset();
	raw[4] = 2;  raw[5] = 7;
	qa_inject_packet(raw, 5);
	qa_check(network_sa_ship_peer() == 0 && packet_in[0] == NULL,
	         "a truncated announcement is not adopted, and does not jam the queue");

	/* Publishing clamps at the source too, so a corrupted local value never leaves. Zero is a
	 * legal announcement now, so the refused values are the ones outside 0..SA. */
	network_sa_ship_reset();
	network_sa_ship_publish(-1, false);
	network_sa_ship_publish(SA + 1, false);
	qa_check(network_sa_ship_peer() == 0, "publishing a non-ship announces nothing");

	network_sa_ship_reset();
	isNetworkGame = savedNet;
	thisPlayerNum = savedThis;
}

/* ---- the departure gate -------------------------------------------------------------- */

/* Exercise every gate/commit ordering for menus without a shared outpost. */
static void qa_depart_gate(void)
{
	const JE_boolean savedNet = isNetworkGame;

	isNetworkGame = true;

	Uint8 raw[NET_PACKET_SIZE];
	memset(raw, 0, sizeof(raw));
	SDLNet_Write16(PACKET_DEPART_GATE, &raw[0]);

	qa_check(network_depart_gate_peer() == -1, "no gate announcement has arrived until one does");

	raw[4] = 1;
	qa_inject_packet(raw, 5);
	qa_check(network_depart_gate_peer() == 1 && packet_in[0] == NULL,
	         "the peer's gate is read and its packet retired from the queue");

	raw[4] = 0;
	qa_inject_packet(raw, 5);
	qa_check(network_depart_gate_peer() == 0, "a zero is the peer withdrawing to its menu");

	/* Truncated: taken as standing at the gate rather than jamming the queue, which matches
	 * the bare four-byte form every other rendezvous sends. */
	qa_inject_packet(raw, 4);
	qa_check(network_depart_gate_peer() == 1 && packet_in[0] == NULL,
	         "a gate packet with no answer byte reads as arrival, and does not jam the queue");

	/* A packet of another type is left alone: this poll only ever consumes its own. */
	SDLNet_Write16(PACKET_WAITING, &raw[0]);
	qa_inject_packet(raw, 5);
	qa_check(network_depart_gate_peer() == -1 && packet_in[0] != NULL,
	         "the gate poll leaves a commit at the head for the phase that reads it");
	network_update();

	/* The gate wait. Esc outranks everything inbound, so the answer cannot depend on which of
	 * the two landed first within a frame. */
	qa_check(network_depart_gate_step(true, -1, 0) == DEPART_GATE_WITHDRAW,
	         "Esc at the gate reopens the menu");
	qa_check(network_depart_gate_step(true, 1, 0) == DEPART_GATE_WITHDRAW
	         && network_depart_gate_step(true, -1, PACKET_WAITING) == DEPART_GATE_WITHDRAW,
	         "...whatever arrived on the same frame");
	qa_check(network_depart_gate_step(false, 1, 0) == DEPART_GATE_GO,
	         "the peer reaching the gate releases the wait");
	qa_check(network_depart_gate_step(false, -1, PACKET_WAITING) == DEPART_GATE_GO,
	         "so does a peer that is already past it");
	qa_check(network_depart_gate_step(false, 0, 0) == DEPART_GATE_WAIT
	         && network_depart_gate_step(false, -1, 0) == DEPART_GATE_WAIT
	         && network_depart_gate_step(false, -1, PACKET_KEEP_ALIVE) == DEPART_GATE_WAIT,
	         "a withdrawn peer, an empty queue and other traffic all keep waiting");

	/* A peer withdrawal returns a committed waiter to the gate. */
	qa_check(network_depart_wait_step(0, 0) == DEPART_WAIT_REOPENED
	         && network_depart_wait_step(0, PACKET_WAITING) == DEPART_WAIT_REOPENED,
	         "a peer that withdraws reopens the gate, even behind their own commit");
	qa_check(network_depart_wait_step(-1, PACKET_WAITING) == DEPART_WAIT_DONE,
	         "the peer's commit pairs with ours and both leave");
	qa_check(network_depart_wait_step(1, 0) == DEPART_WAIT_MORE
	         && network_depart_wait_step(-1, 0) == DEPART_WAIT_MORE
	         && network_depart_wait_step(-1, PACKET_DEPART_GATE) == DEPART_WAIT_MORE,
	         "a re-announced gate leaves the commit wait waiting");

	/* The pair of decisions the two phases exist for: the machine that pressed Esc reopens its
	 * menu, and the machine that committed against it falls back to the gate. */
	qa_check(network_depart_gate_step(true, 1, PACKET_WAITING) == DEPART_GATE_WITHDRAW
	         && network_depart_wait_step(0, PACKET_WAITING) == DEPART_WAIT_REOPENED,
	         "one side withdrawing and the other committing resolves to gate and reopen");

	isNetworkGame = savedNet;
}
#endif

/* ---- 13. the Endless debug zone jump crosses the wire -------------------------------- */

/* Endless jumps round-trip the level and complete debug block, and reject truncation. */
static void qa_endless_jump_pick(void)
{
	const JE_boolean savedEndless = endlessMode;
	QaEndlessDebugState saved;
	qa_endless_debug_save(&saved);
	endlessMode = true;
	coopEndlessMode = true;

	Uint8 block[ENDLESS_DEBUG_BLOCK_SIZE], readback[ENDLESS_DEBUG_BLOCK_SIZE];

	endlessJumpPickReset();
	qa_check(!endlessJumpPickGet(block), "no zone jump is staged until one is made");

	/* Use both halves of the 64-bit modifier mask and distinct state for each ship. */
	const Uint64 wide = 0x8000000400000002ull;
	endlessRunDepth = 37;
	endlessActiveMods = wide;
	for (uint p = 0; p < 2; ++p)
		for (int i = 0; i < endlessPerkCount(); ++i)
			endlessPerkSetOwnedFor(p, i, 0);
	endlessPerkSetOwnedFor(0, 0, 2);
	endlessPerkSetOwnedFor(1, 1, 3);
	endlessSetPersonalBuffMods(0, ENDLESS_MOD_TURBODRIVE);
	endlessSetPersonalBuffMods(1, ENDLESS_MOD_BACKFIRE);
	endlessJumpPickStage();
	qa_check(endlessJumpPickGet(block), "staging a jump arms it");

	// Wipe it all, then adopt the staged block as the partner's machine would.
	endlessRunDepth = 0;
	endlessActiveMods = 0;
	for (uint p = 0; p < 2; ++p)
	{
		endlessSetPersonalBuffMods(p, 0);
		for (int i = 0; i < endlessPerkCount(); ++i)
			endlessPerkSetOwnedFor(p, i, 0);
	}
	endlessJumpPickApply(block, sizeof(block));

	qa_check(endlessRunDepth == 37 && endlessActiveMods == wide,
	         "an adopted zone jump lands the depth and the whole 64-bit modifier mask");
	qa_check(endlessPerkGetOwnedFor(0, 0) == 2 && endlessPerkGetOwnedFor(1, 0) == 0
	         && endlessPerkGetOwnedFor(1, 1) == 3 && endlessPerkGetOwnedFor(0, 1) == 0,
	         "...and each ship's perk stacks on that ship, whichever machine adopts it");
	qa_check(endlessPersonalBuffMods(0) == ENDLESS_MOD_TURBODRIVE
	         && endlessPersonalBuffMods(1) == ENDLESS_MOD_BACKFIRE,
	         "...and each ship's personal buffs the same way");
	qa_check(endlessJumpPickGet(readback)
	         && memcmp(block, readback, sizeof(block)) == 0,
	         "...and re-stages it byte for byte, so a republish cannot move anything");

	/* A truncated block leaves the run alone. The packet's own length byte is not trusted, so a
	 * short read has to be refused outright rather than applied to whatever arrived. */
	endlessRunDepth = 9;
	endlessActiveMods = 0;
	endlessPerkSetOwnedFor(0, 0, 1);
	endlessJumpPickApply(block, sizeof(block) - 1);
	qa_check(endlessRunDepth == 9 && endlessActiveMods == 0 && endlessPerkGetOwnedFor(0, 0) == 1,
	         "a truncated jump block is refused, not applied as far as it goes");
	endlessJumpPickApply(block, 0);
	qa_check(endlessRunDepth == 9 && endlessPerkGetOwnedFor(0, 0) == 1,
	         "...and so is an empty one");

	/* Hostile stack counts. These index the perk table on arrival, so the adopt clamps them. */
	Uint8 hostile[ENDLESS_DEBUG_BLOCK_SIZE];
	memcpy(hostile, block, sizeof(hostile));
	memset(&hostile[10], 0xff, 2 * ENDLESS_DEBUG_BLOCK_PERKS);
	endlessJumpPickApply(hostile, sizeof(hostile));
	bool clamped = true;
	for (uint p = 0; p < 2; ++p)
		for (int i = 0; i < endlessPerkCount(); ++i)
			clamped = clamped && endlessPerkGetOwnedFor(p, i) <= endlessPerkMaxStack(i);
	qa_check(clamped, "a peer's out-of-range perk stacks clamp to this build's own maximums");

	/* A depth at the field's ceiling survives the round trip; one past it is what the pack clamps,
	 * so both machines still agree on the number they fly. */
	endlessRunDepth = 0xFFFF;
	endlessJumpPickStage();
	endlessRunDepth = 0;
	qa_check(endlessJumpPickGet(block), "a jump at the depth field's ceiling stages");
	endlessJumpPickApply(block, sizeof(block));
	qa_check(endlessRunDepth == 0xFFFF, "...and arrives at that ceiling rather than wrapping");

	endlessRunDepth = 0x1FFFF;
	endlessJumpPickStage();
	endlessJumpPickGet(block);
	endlessJumpPickApply(block, sizeof(block));
	qa_check(endlessRunDepth == 0xFFFF, "a depth past the field clamps instead of wrapping to zero");

	endlessJumpPickReset();
	qa_check(!endlessJumpPickGet(block),
	         "leaving the outpost clears the staged jump, so it fires once and not every level");

	/* The two per-ship accessors the panel and the block are both built on. Their guards are what
	 * keep a hostile block, or a row the panel has no ship for, out of the arrays behind them. */
	for (uint p = 0; p < 2; ++p)
		for (int i = 0; i < endlessPerkCount(); ++i)
			endlessPerkSetOwnedFor(p, i, 0);
	endlessPerkSetOwnedFor(2, 0, 3);
	endlessPerkSetOwnedFor(0, -1, 3);
	endlessPerkSetOwnedFor(0, endlessPerkCount(), 3);
	qa_check(endlessPerkGetOwnedFor(0, 0) == 0 && endlessPerkGetOwnedFor(1, 0) == 0,
	         "a perk write for a ship or an id this build has no room for lands nowhere");
	qa_check(endlessPerkGetOwnedFor(2, 0) == 0 && endlessPerkGetOwnedFor(0, -1) == 0,
	         "...and reading one back answers zero rather than the memory beside the array");

	endlessPerkSetOwnedFor(0, 0, -5);
	qa_check(endlessPerkGetOwnedFor(0, 0) == 0, "a negative stack count clamps to none owned");
	endlessPerkSetOwnedFor(0, 0, 99);
	qa_check(endlessPerkGetOwnedFor(0, 0) == endlessPerkMaxStack(0),
	         "...and one past the maximum clamps to the maximum");

	endlessActiveMods = 0;
	endlessPurchasedMods[0] = (unsigned)(ENDLESS_MOD_FRENZY | ENDLESS_MOD_TURBODRIVE);
	endlessSetPersonalBuffMods(0, ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_FRENZY);
	qa_check((endlessPurchasedMods[0] & ENDLESS_MOD_FRENZY) != 0,
	         "setting a ship's buffs leaves the sector effects it bought alone");
	qa_check(endlessPersonalBuffMods(0) == ENDLESS_MOD_OVERDRIVE,
	         "...and takes only the personal bits out of what it was handed");
	qa_check(endlessPersonalBuffMods(2) == 0, "a buff read for a ship that does not exist is none");
	endlessSetPersonalBuffMods(2, ENDLESS_MOD_TURBODRIVE);   // must not write past the array

	endlessMode = savedEndless;
	qa_endless_debug_restore(&saved);
}

#ifdef WITH_NETWORK

/* Joiner wait screen */

/* Checks one row layout against the screen budget. */
static int qa_guest_wait_check(const char *shape)
{
	const char *label[GUEST_WAIT_ROWS_CAP], *value[GUEST_WAIT_ROWS_CAP];
	const int rows = networkGuestWaitRows(label, value);
	char msg[224];

	snprintf(msg, sizeof(msg), "%s: %d rows stay within the screen's cap", shape, rows);
	qa_check(rows > 0 && rows <= GUEST_WAIT_ROWS_CAP, msg);

	for (int i = 0; i < rows && i < GUEST_WAIT_ROWS_CAP; ++i)
	{
		snprintf(msg, sizeof(msg), "%s: row '%s' fits its value '%s'", shape,
		         label[i] ? label[i] : "(null)", value[i] ? value[i] : "(null)");
		qa_check(label[i] != NULL && value[i] != NULL && value[i][0] != '\0'
		         && JE_textWidth(label[i], small_font) + 20
		            + JE_textWidth(value[i], small_font) <= 300,
		         msg);
	}

	snprintf(msg, sizeof(msg), "%s: %d rows, the waiting line and the hint fit the screen",
	         shape, rows);
	qa_check(rows * guest_wait_row_h(rows) + guest_wait_gap(rows) + GUEST_WAIT_LINE_H
	         + GUEST_WAIT_HINT_H <= GUEST_WAIT_BOTTOM - GUEST_WAIT_TOP, msg);

	return rows;
}

// Widest accepted host name or seed.
static void qa_guest_wait_worst(char *out, size_t len)
{
	int widest = -1;
	char pick = 'W';
	for (unsigned char c = 33; c < 127; ++c)
	{
		if (font_ascii[c] < 0 || !isalnum(c))
			continue;

		const char one[2] = { (char)c, '\0' };
		const int w = JE_textWidth(one, small_font);
		if (w > widest)
		{
			widest = w;
			pick = (char)c;
		}
	}
	memset(out, pick, len - 1);
	out[len - 1] = '\0';
}

// Check every session layout with the widest host name.
static void qa_guest_wait_layout(void)
{
	char *const savedName = network_opponent_name;
	const NetworkGameType savedType = network_game_type;
	const int savedEpisode = network_host_episode;
	const int savedDifficulty = network_host_difficulty;
	const int savedRunMode = network_host_endless_run_mode;
	const int savedBaseRule = network_host_endless_base_rule;
	const int savedChooser = network_host_endless_chooser;
	const bool savedCombo = network_host_endless_combo_shared;
	const JE_boolean savedTimed = timedBattleMode;
	const JE_boolean savedSeparate = arcadeSeparateMode;
	const uint savedSeat = thisPlayerNum;
	const JE_byte savedBattle = timeBattleSelection;
	const JE_byte savedSpeed = gameSpeed;
	const bool savedRollback = nrb_session_mode();
	const bool savedRecovery = nrb_session_recovery();
	char savedSeed[NET_ENDLESS_SEED_MAX];
	memcpy(savedSeed, network_endless_session_seed, sizeof(savedSeed));

	char nameWorst[NET_NAME_MAX + 1];
	qa_guest_wait_worst(nameWorst, sizeof(nameWorst));
	network_opponent_name = nameWorst;

	network_host_episode = 1;
	network_host_difficulty = DIFFICULTY_NORMAL;
	gameSpeed = 3;
	network_endless_session_seed[0] = '\0';
	nrb_set_session_mode(true);
	nrb_set_session_recovery(true);
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
	timedBattleMode = false;
	arcadeSeparateMode = false;
	network_game_type = NETWORK_GAME_ARCADE;

	thisPlayerNum = 1;
	qa_guest_wait_check("linked arcade, seat one");
	thisPlayerNum = 2;
	qa_guest_wait_check("linked arcade, seat two");
	nrb_set_session_mode(false);
	qa_guest_wait_check("linked arcade, delay-based");
	nrb_set_session_mode(true);

	for (int d = 1; d <= DIFFICULTY_10; ++d)
	{
		network_host_difficulty = d;
		qa_guest_wait_check("arcade difficulty sweep");
	}
	network_host_difficulty = DIFFICULTY_NORMAL;

	arcadeSeparateMode = true;
	qa_guest_wait_check("separate arcade");

	timedBattleMode = true;
	for (int b = 1; b <= NET_TIMED_BATTLE_LEVELS; ++b)
	{
		timeBattleSelection = (JE_byte)b;
		qa_guest_wait_check("timed battle");
	}
	timedBattleMode = false;

	network_game_type = NETWORK_GAME_SUPERTYRIAN;
	qa_guest_wait_check("supertyrian, standard");
	network_host_difficulty = DIFFICULTY_SUICIDE;
	qa_guest_wait_check("supertyrian, scrollock");
	network_host_difficulty = DIFFICULTY_NORMAL;

	network_game_type = NETWORK_GAME_SUPERARCADE;
	qa_guest_wait_check("super arcade");

	arcadeSeparateMode = false;
	network_game_type = NETWORK_GAME_CAMPAIGN;
	qa_guest_wait_check("campaign, shared credit");
	coop_set_session_shared_credit(false);
	coop_set_session_double_earnings(true);
	qa_guest_wait_check("campaign, individual doubled");
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);

	network_game_type = NETWORK_GAME_ENDLESS;
	network_host_endless_run_mode = 0;
	network_host_endless_base_rule = 0;
	network_host_endless_chooser = 0;
	network_host_endless_combo_shared = true;
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		network_host_endless_run_mode = m;
		qa_guest_wait_check("endless run-mode sweep");
	}
	network_host_endless_run_mode = 0;
	for (int r = 0; r < ENDLESS_BASE_RULE_COUNT; ++r)
	{
		network_host_endless_base_rule = r;
		qa_guest_wait_check("endless base-rule sweep");
	}
	network_host_endless_base_rule = 0;
	for (int c = 0; c < ENDLESS_PICK_COUNT; ++c)
	{
		network_host_endless_chooser = c;
		qa_guest_wait_check("endless chooser sweep");
	}
	network_host_endless_chooser = 0;

	{
		char unnamed[1] = { '\0' };
		network_opponent_name = unnamed;
		qa_guest_wait_check("endless, unnamed host");
		network_opponent_name = nameWorst;
	}

	// This combination produces the most rows.
	qa_guest_wait_worst(network_endless_session_seed, sizeof(network_endless_session_seed));
	network_host_endless_combo_shared = false;
	coop_set_session_shared_credit(false);
	coop_set_session_double_earnings(true);
	const int deepest = qa_guest_wait_check("endless, deepest shape");
	qa_check(deepest == 13,
	         "the deepest wait-screen shape is 13 rows; a new row must re-earn the fit above");

	qa_check(JE_textWidth(GUEST_WAIT_HINT, small_font) <= 300,
	         "the wait screen's Esc hint fits the field");

	memcpy(network_endless_session_seed, savedSeed, sizeof(savedSeed));
	nrb_set_session_recovery(savedRecovery);
	nrb_set_session_mode(savedRollback);
	gameSpeed = savedSpeed;
	timeBattleSelection = savedBattle;
	thisPlayerNum = savedSeat;
	arcadeSeparateMode = savedSeparate;
	timedBattleMode = savedTimed;
	network_host_endless_combo_shared = savedCombo;
	network_host_endless_chooser = savedChooser;
	network_host_endless_base_rule = savedBaseRule;
	network_host_endless_run_mode = savedRunMode;
	network_host_difficulty = savedDifficulty;
	network_host_episode = savedEpisode;
	network_game_type = savedType;
	network_opponent_name = savedName;
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
}

#endif  /* WITH_NETWORK */

/* ---- entry point -------------------------------------------------------------------- */

void qa_test_online_suite(void)
{
	QaOnlineEnv saved;
	qa_online_save(&saved);

	qa_mode_split_matrix();
	qa_local_index_matrix();
	qa_arcade_economy_matrix();
	qa_separate_arcade_lives();
	qa_special_block_geometry();
	qa_boss_bar_clearance();
	qa_rear_gun_mode_matrix();
	qa_downed_ship_hud();
	qa_campaign_economy_matrix();
	qa_campaign_score_matrix();
	qa_online_strings_matrix();
	qa_online_pause_matrix();
	qa_super_online_matrix();
	qa_sa_picker_layout();
	qa_dual_arcade_save_roundtrip();
#ifdef WITH_NETWORK
	qa_debug_block_roundtrip();
	qa_test_net_lobby_strings();
	qa_hostile_packets();
	qa_quit_notice_retire();
	qa_sa_ship_packet();
	qa_depart_gate();
	qa_endless_jump_pick();
	qa_guest_wait_layout();
#endif

	qa_online_restore(&saved);
}
