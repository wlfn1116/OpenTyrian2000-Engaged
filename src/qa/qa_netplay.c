#define ENDLESS_INTERNAL_ACCESS
#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "background.h"
#include "config.h"
#include "crashlog.h"
#include "debug_flags.h"
#include "endless.h"
#include "endless_internal.h"
#include "font.h"
#include "game_loop.h"
#include "game_state.h"
#include "health_bars.h"
#include "hud.h"
#include "keyboard.h"
#include "lvlmast.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "net_style.h"
#include "network.h"
#include "params.h"
#include "player.h"
#include "player_ship.h"
#include "rollback.h"
#include "sprite.h"
#include "video.h"
#include "vt_ship.h"
#include "xmas.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Online Endless: the block each machine publishes for its own player, the way two players'
 * purchases fold into one sector, and who charts the next course. */
void qa_test_endless_coop(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const bool savedHost = network_is_host;
	const EndlessCourseChooser savedChooser = endlessCourseChooser;
	const bool savedHostCharts = endlessCoopHostCharts;
	const int savedDepth = endlessRunDepth;

	endlessMode = true;
	coopEndlessMode = true;

	/* The wire block round trips through the peer's slot without touching the sender's. */
	endlessArmorBonus[0] = 24;
	endlessPurchasedMods[0] = ENDLESS_MOD_OVERDRIVE;
	endlessBuffKind[0] = ENDLESS_BUFF_KIND_OVERDRIVE;
	endlessBuffCharge[0] = 7;
	endlessBuffCooldownUntil[0] = 12;
	endlessCleanseChargeCount[0] = 2;
	endlessLongCon[0] = 3;
	endlessShopTax[0] = 50;
	endlessRevivesUsed[0] = 4;
	endlessRerollCost[0] = 91000;
	endlessHullCost[0] = 45000;
	endlessShopEntryCash[0] = 1234567;
	endlessReviveHeld[0] = true;
	endlessGambleRigged[0] = true;
	endlessPlayerDowned[0] = true;
	player[0].superbombs = 6;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkTakenBy[0][PERK_DAMAGE] = 2;
	endlessPerkRederive();

	union {
		Uint32 align;
		Uint8 bytes[ENDLESS_PLAYER_BLOCK_SIZE + 8];
	} guarded;
	memset(guarded.bytes, 0x5a, sizeof(guarded.bytes));
	const int packed = endlessPackPlayerBlock(guarded.bytes + 4, 0);
	qa_check(packed == ENDLESS_PLAYER_BLOCK_SIZE,
	         "endless co-op player block packs its declared width");
	qa_check(guarded.bytes[3] == 0x5a && guarded.bytes[4 + ENDLESS_PLAYER_BLOCK_SIZE] == 0x5a,
	         "endless co-op player block stays inside its buffer");

	endlessUnpackPlayerBlock(guarded.bytes + 4, 1);
	qa_check(endlessArmorBonus[1] == 24 && endlessPurchasedMods[1] == ENDLESS_MOD_OVERDRIVE
	         && endlessBuffKind[1] == ENDLESS_BUFF_KIND_OVERDRIVE && endlessBuffCharge[1] == 7
	         && endlessBuffCooldownUntil[1] == 12 && endlessCleanseChargeCount[1] == 2
	         && endlessLongCon[1] == 3 && endlessShopTax[1] == 50 && endlessRevivesUsed[1] == 4
	         && endlessRerollCost[1] == 91000 && endlessHullCost[1] == 45000
	         && endlessShopEntryCash[1] == 1234567,
	         "endless co-op player block restores every numeric field");
	qa_check(endlessReviveHeld[1] && endlessGambleRigged[1] && endlessPlayerDowned[1]
	         && player[1].superbombs == 6,
	         "endless co-op player block restores the one-shot latches and bombs");
	qa_check(endlessPerkTakenBy[1][PERK_DAMAGE] == 2
	         && endlessPerkEffective(1, PERK_DAMAGE) == 2
	         && endlessPerkEffective(0, PERK_DAMAGE) == endlessPerkTakenBy[0][PERK_DAMAGE],
	         "endless perks stay their owner's row through the wire block");

	/* A drive belongs to the ship that bought it; the sector-changing half of a purchase does not. */
	endlessActiveMods = ENDLESS_MOD_FORTIFIED;
	endlessPurchasedMods[0] = ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FAVOR;
	endlessPurchasedMods[1] = ENDLESS_MOD_OVERBLAST;
	endlessApplyPurchasedMods();
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_TURBODRIVE)
	         && !(endlessPlayerMods[0] & ENDLESS_MOD_OVERBLAST),
	         "a bought drive reaches only the ship that bought it");
	qa_check((endlessPlayerMods[1] & ENDLESS_MOD_OVERBLAST)
	         && !(endlessPlayerMods[1] & ENDLESS_MOD_TURBODRIVE),
	         "...and the other ship keeps its own");
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_FORTIFIED)
	         && (endlessPlayerMods[1] & ENDLESS_MOD_FORTIFIED)
	         && (endlessActiveMods & ENDLESS_MOD_FAVOR),
	         "the sector's own modifiers and the shop-side buys stay shared");

	/* A drive the SECTOR deals is not a purchase: it reaches both ships. One that a player then
	 * buys for themselves replaces it on their own mask alone. */
	endlessActiveMods = ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[0] = 0;
	endlessPurchasedMods[1] = 0;
	endlessApplyPurchasedMods();
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_TURBODRIVE)
	         && (endlessPlayerMods[1] & ENDLESS_MOD_TURBODRIVE),
	         "a charted drive reaches both ships");
	endlessPurchasedMods[1] = ENDLESS_MOD_OVERBLAST;
	endlessApplyPurchasedMods();
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_TURBODRIVE)
	         && !(endlessPlayerMods[0] & ENDLESS_MOD_OVERBLAST),
	         "...and a partner who bought nothing keeps flying it");
	qa_check((endlessPlayerMods[1] & ENDLESS_MOD_OVERBLAST)
	         && !(endlessPlayerMods[1] & ENDLESS_MOD_TURBODRIVE),
	         "...while the buyer's own drive replaces it for them alone");
	endlessActiveMods = 0;
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;

	endlessBuffCharge[0] = 4;
	endlessBuffCharge[1] = 15;
	endlessSetFxPlayer(1);
	qa_check(endlessBuffChargePaid() == 15, "the kill-fire window follows the ship being computed");
	endlessSetFxPlayer(0);
	qa_check(endlessBuffChargePaid() == 4, "...and the other ship reads its own charge");
	endlessActiveMods = 0;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	endlessBuffCharge[0] = endlessBuffCharge[1] = 0;

	/* Course picking. Every mode has to answer the same way on both machines. */
	endlessCourseChooser = ENDLESS_PICK_HOST;
	network_is_host = true;
	qa_check(endlessLocalPlayerCharts(), "Host picking charts on the host");
	network_is_host = false;
	qa_check(!endlessLocalPlayerCharts(), "Host picking does not chart on the joiner");

	endlessCourseChooser = ENDLESS_PICK_GUEST;
	qa_check(endlessLocalPlayerCharts(), "Guest picking charts on the joiner");
	network_is_host = true;
	qa_check(!endlessLocalPlayerCharts(), "Guest picking does not chart on the host");

	endlessCourseChooser = ENDLESS_PICK_ALTERNATE;
	endlessCoopHostCharts = true;
	qa_check(endlessLocalPlayerCharts(), "Alternating starts on the host");
	endlessAdvanceCourseTurn();
	qa_check(!endlessLocalPlayerCharts(), "Alternating hands the next course to the joiner");
	network_is_host = false;
	qa_check(endlessLocalPlayerCharts(), "...and the joiner sees the same turn");

	/* The coin flip is derived from the seed, so it never depends on how much either player
	 * shopped, and exactly one machine charts each zone. */
	endlessCourseChooser = ENDLESS_PICK_COINFLIP;
	endlessSetSeed("qa-coinflip");
	bool split = false, alwaysAgree = true;
	for (int zone = 0; zone < 24; ++zone)
	{
		endlessRunDepth = zone;
		network_is_host = true;
		const bool hostCharts = endlessLocalPlayerCharts();
		network_is_host = false;
		const bool guestCharts = endlessLocalPlayerCharts();
		if (hostCharts == guestCharts)
			alwaysAgree = false;
		if (hostCharts)
			split = true;
	}
	qa_check(alwaysAgree, "the 50-50 coin gives exactly one machine each course");
	qa_check(split, "the 50-50 coin lands on the host at least once in 24 zones");

	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		endlessArmorBonus[p] = 0; endlessPurchasedMods[p] = 0; endlessBuffKind[p] = 0;
		endlessBuffCharge[p] = 0; endlessBuffCooldownUntil[p] = 0; endlessCleanseChargeCount[p] = 0;
		endlessLongCon[p] = 0; endlessShopTax[p] = 0; endlessRevivesUsed[p] = 0;
		endlessReviveHeld[p] = false; endlessGambleRigged[p] = false; endlessPlayerDowned[p] = false;
		player[p].superbombs = 0;
	}
	endlessRunDepth = savedDepth;
	endlessCoopHostCharts = savedHostCharts;
	endlessCourseChooser = savedChooser;
	network_is_host = savedHost;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

/* Whose combo a kill feeds, and what Individual credit does to a pickup. */
void qa_test_coop_combo_and_pickups(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const JE_boolean savedCampaign = coopCampaignMode;
	const Uint64 savedActive = endlessActiveMods;
	const int savedKills = endlessRunKills;
	const bool savedShared = endlessCoopComboShared;

	endlessMode = true;
	coopEndlessMode = true;
	memset(endlessPerkOwned, 0, sizeof(endlessPerkOwned));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));

	// Both ships fly a drive, so either one's streak could climb.
	endlessActiveMods = ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;
	endlessApplyPurchasedMods();

	endlessCoopComboShared = false;
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	for (int k = 0; k < 12; ++k)
		endlessCountKill(0, 0);          // player 1 does all the shooting
	qa_check(endlessComboKills[0] == 12 && endlessComboKills[1] == 0,
	         "Individual combo feed keeps one ship's kills out of the other's streak");
	qa_check(endlessTurbodriveTimer[0] > 0 && endlessTurbodriveTimer[1] == 0,
	         "...and only the shooter's drive window opens");

	for (int k = 0; k < 5; ++k)
		endlessCountKill(0, 1);          // now player 2 takes some
	qa_check(endlessComboKills[0] == 12 && endlessComboKills[1] == 5,
	         "...each ship counting its own");

	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	endlessCountKill(0, ENDLESS_KILLER_NONE);
	qa_check(endlessComboKills[0] == 1 && endlessComboKills[1] == 1,
	         "a kill nothing can claim feeds both, so neither streak is punished for it");

	endlessCoopComboShared = true;
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	for (int k = 0; k < 7; ++k)
		endlessCountKill(0, 0);
	qa_check(endlessComboKills[0] == 7 && endlessComboKills[1] == 7,
	         "Shared combo feed has every kill feed both streaks");

	/* Double Earnings compensates a split take, and only a split take. */
	coopEndlessMode = false;
	coopCampaignMode = true;
	endlessMode = false;

	coop_set_session_shared_credit(false);
	coop_set_session_double_earnings(true);
	qa_check(coop_earnings_are_doubled(), "Double Earnings applies under Individual credit");

	player[0].cash = 0;
	player[1].cash = 0;
	player_award_pickup_cash(&player[0], 250);
	qa_check(player[0].cash == 500 && player[1].cash == 0,
	         "a doubled pickup pays its collector twice and nobody else");

	player[0].cash = 0;
	player_award_kill_cash(&player[0], 250);
	qa_check(player[0].cash == 500, "...and covers kill cash the same way");

	coop_set_session_shared_credit(true);
	qa_check(!coop_earnings_are_doubled(),
	         "Double Earnings stands down under Shared credit, where both already collect in full");
	player[0].cash = player[1].cash = 0;
	player_award_pickup_cash(&player[0], 250);
	qa_check(player[0].cash == 250 && player[1].cash == 250,
	         "...and Shared pays the plain amount to both");

	/* On the joiner, an elite bounty still follows the shooter rather than the local seat. */
	const JE_boolean savedNetGame = isNetworkGame;
	const JE_byte savedPlayerNum = thisPlayerNum;
	isNetworkGame = true;
	thisPlayerNum = 2;
	coopCampaignMode = false;
	coopEndlessMode = true;
	endlessMode = true;
	endlessActiveMods = 0;
	coop_set_session_shared_credit(false);
	coop_set_session_double_earnings(false);
	qa_check(endlessEconomyIndex() == 1, "the joiner's own wallet is player 2's");

	player[0].cash = player[1].cash = 0;
	endlessCashResync();  // the run ledger books player 1's income; re-anchor after every reset
	endlessAwardEliteKill(41, 2, 1);
	qa_check(player[1].cash > 0 && player[0].cash == 0, "an elite bounty pays the ship that killed it");

	const Sint64 shooterBounty = player[1].cash;
	player[0].cash = player[1].cash = 0;
	endlessCashResync();
	endlessAwardEliteKill(42, 2, ENDLESS_KILLER_NONE);
	qa_check(player[0].cash == shooterBounty && player[1].cash == 0,
	         "...and an unclaimable one pays player 1, the same ship on both machines");

	player[0].cash = player[1].cash = 0;
	endlessCashResync();
	thisPlayerNum = savedPlayerNum;
	isNetworkGame = savedNetGame;
	coop_set_session_double_earnings(false);
	coop_set_session_shared_credit(true);
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	endlessCoopComboShared = savedShared;
	endlessActiveMods = savedActive;
	endlessRunKills = savedKills;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
}

/* The peer leaving a level under us. */
void qa_test_peer_left_level(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const JE_boolean savedEnd = reallyEndLevel, savedPlayerEnd = playerEndLevel;
	const bool savedQuit = endlessQuitToOutpost;

	endlessMode = true;
	coopEndlessMode = true;
	endlessCaptureSortie();  // a peer quit only reopens an outpost there is a launch snapshot for
	qa_check(endlessSortieValid(), "the quit cases below have a sortie to fall back to");

	reallyEndLevel = false;
	playerEndLevel = false;
	endlessQuitToOutpost = false;
	qa_check(!nrb_peer_left_level(0), "an empty queue does not end the level");
	qa_check(!reallyEndLevel && !playerEndLevel, "...and touches nothing");
	qa_check(!nrb_peer_left_level(PACKET_GAME_MENU) && !reallyEndLevel,
	         "a rollback in-game menu release is not a departure: the peer is still in the level");

	qa_check(nrb_peer_left_level(PACKET_WAITING), "a peer at the level handshake ends the level");
	qa_check(reallyEndLevel && !playerEndLevel && !endlessQuitToOutpost,
	         "...as a cleared zone, which is what reaching that handshake means");

	reallyEndLevel = false;
	qa_check(nrb_peer_left_level(PACKET_GAME_QUIT), "a peer quit ends the level too");
	qa_check(reallyEndLevel && playerEndLevel,
	         "...but not as a clear: the zone was given up, not finished");
	qa_check(endlessQuitToOutpost,
	         "...and Endless reopens the same outpost, so neither player deepens alone");

	// Campaign has no outpost to fall back to, so a quit there stays a quit.
	coopEndlessMode = false;
	endlessQuitToOutpost = false;
	reallyEndLevel = playerEndLevel = false;
	qa_check(nrb_peer_left_level(PACKET_GAME_QUIT) && playerEndLevel && !endlessQuitToOutpost,
	         "outside Endless a peer quit leaves the run-reopening flag alone");

	endlessQuitToOutpost = savedQuit;
	playerEndLevel = savedPlayerEnd;
	reallyEndLevel = savedEnd;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

/* Each peer advance renews the confirmation timeout; an idle peer must eventually release it. */
void qa_test_peer_idle_rule(void)
{
	const Uint32 limit = NRB_PEER_IDLE_TIME_OUT;
	Uint32 seen = 540, tick = 1000;

	qa_check(!nrb_peer_idle(1000, 540, &seen, &tick), "a wait that just began is not idle");
	qa_check(!nrb_peer_idle(1000 + limit, 540, &seen, &tick), "...nor at exactly the limit");
	qa_check(nrb_peer_idle(1001 + limit, 540, &seen, &tick),
	         "a peer whose newest frame never moved is idle once the limit passes");

	seen = 537;
	tick = 1000;
	qa_check(!nrb_peer_idle(1300, 539, &seen, &tick) && seen == 539 && tick == 1300,
	         "an advance is recorded and re-arms the clock");
	qa_check(!nrb_peer_idle(1600, 540, &seen, &tick) && tick == 1600,
	         "...as does every later advance");
	qa_check(!nrb_peer_idle(1000 + limit, 540, &seen, &tick),
	         "the limit counts from the last advance, so the original start no longer matters");
	qa_check(nrb_peer_idle(1601 + limit, 540, &seen, &tick),
	         "a peer that advanced during the wait and then went quiet is idle from that advance");
}

/* Both peers must agree on the one seat that opened a menu, or that none did. */
void qa_test_menu_claim(void)
{
	const Uint16 none = RB_BTN_FIRE;               // traffic on the frame, no request
	const Uint16 menu = RB_BTN_FIRE | RB_REQ_MENU;

	for (int hostPressed = 0; hostPressed <= 1; ++hostPressed)
	{
		for (int joinerPressed = 0; joinerPressed <= 1; ++joinerPressed)
		{
			const Uint16 hostBits = hostPressed ? menu : none;
			const Uint16 joinerBits = joinerPressed ? menu : none;

			// The same frame read from each seat: local and remote swap, is_host does not.
			const NrbMenuClaim onHost = nrb_menu_claim(hostBits, joinerBits, true);
			const NrbMenuClaim onJoiner = nrb_menu_claim(joinerBits, hostBits, false);

			qa_check(onHost.open == onJoiner.open
			         && onHost.open == (hostPressed || joinerPressed),
			         "both seats agree on whether a frame opens the in-game menu");
			if (!onHost.open)
			{
				qa_check(!onHost.local && !onJoiner.local,
				         "...and a frame that opens none leaves neither seat holding one");
				continue;
			}
			qa_check(onHost.local != onJoiner.local,
			         "an opened menu has exactly one presser, so the other seat has someone to wait for");
			qa_check(onHost.local == (hostPressed != 0),
			         "the host presses for itself, and takes a tie from the joiner");
		}
	}

	/* Presses one frame apart are two frames, and each is claimed on its own: the earlier one is
	 * the frame the menu opens at, so a joiner that pressed first keeps it. */
	const NrbMenuClaim earlyOnJoiner = nrb_menu_claim(menu, none, false);
	const NrbMenuClaim earlyOnHost = nrb_menu_claim(none, menu, true);
	qa_check(earlyOnJoiner.open && earlyOnJoiner.local && earlyOnHost.open && !earlyOnHost.local,
	         "a joiner press on a frame the host did not press is the joiner's menu");

	/* Only RB_REQ_MENU opens one; the other request bits ride the same records. */
	qa_check(!nrb_menu_claim(RB_REQ_SKIPLEVEL | RB_REQ_NORTSHIP | RB_REQ_PAUSE,
	                         RB_REQ_SKIPLEVEL | RB_REQ_PAUSE, true).open,
	         "skip-level, Nort ship and the dead pause bit open no menu");
}

// Change one local-view field while preserving the others.
static void qa_set_local_opacity(int pct)
{
	NetShipView view = netStyleLocalView();
	view.opacity = (Uint8)pct;
	netStyleSetLocalView(view);
}

static void qa_set_local_ship_opacity(bool on)
{
	NetShipView view = netStyleLocalView();
	view.shipOpacity = on;
	netStyleSetLocalView(view);
}

static void qa_set_local_hp_bars(int mode)
{
	NetShipView view = netStyleLocalView();
	view.hpBars = (Uint8)mode;
	netStyleSetLocalView(view);
}

// Partner HP-bar bounds, placement, painting, and mode eligibility.
void qa_test_partner_hp_bars(void)
{
	const Player saved0 = player[0], saved1 = player[1];
	const JE_word savedGr = shipGr, savedGr2 = shipGr2;
	const int savedLayout = enemyBarLayout, savedPos = enemyBarPosition, savedOp = enemyBarOpacity;

	player[0].x = 100; player[0].y = 100;
	player[1].x = 100; player[1].y = 100;

	int l, r, t, b;

	shipGr = 5;  // ordinary single hull
	hud_ship_hp_bar_box(0, &l, &r, &t, &b);
	qa_check(l == 95 && r == 118 && t == 93 && b == 121,
	         "an ordinary hull measures the 24x28 sprite it draws");

	shipGr2 = 0;  // Dragonwing sentinel
	hud_ship_hp_bar_box(1, &l, &r, &t, &b);
	qa_check(l == 83 && r == 130 && t == 93 && b == 121,
	         "a Dragonwing measures both halves it straddles its anchor with");

	int nl, nr, nt, nb;
	shipGr2 = 1;  // Nort Ship sentinel
	hud_ship_hp_bar_box(1, &nl, &nr, &nt, &nb);
	qa_check(nl == l && nr == r && nt == t && nb == b,
	         "...and so does the Nort Ship, the other two-piece hull");

	// A two-bar block must clear the hull while spanning it.
	enemyBarLayout = ENEMY_BAR_HORIZONTAL;
	enemyBarPosition = ENEMY_BAR_POS_BOTTOM;

	int x, y, along, y1;
	qa_check(enemy_bar_place(l, r, t, b, 2 * ENEMY_BAR_THICK, false, &x, &y, &along)
	         && x == 84 && along == 46 && y == 122,
	         "a two-bar block under a wide hull spans it and clears the sprite");
	qa_check(along <= ENEMY_BAR_MAX_LEN, "...within the length every small bar is capped at");

	enemyBarPosition = ENEMY_BAR_POS_TOP;
	qa_check(enemy_bar_place(l, r, t, b, 2 * ENEMY_BAR_THICK, false, &x, &y, &along)
	         && enemy_bar_place(l, r, t, b, ENEMY_BAR_THICK, false, &x, &y1, &along)
	         && y + 2 * ENEMY_BAR_THICK == t - 1 && y1 + ENEMY_BAR_THICK == t - 1,
	         "a block above the box hangs from its top edge whatever its thickness");

	// Paint at full opacity in a box away from the ship.
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8 && VGAScreen->h > 40)
	{
		enemyBarPosition = ENEMY_BAR_POS_BOTTOM;
		enemyBarOpacity = 100;

		const int bx = 4, by = 4, bw = 24, bh = 20;

		// Full shield, half armor.
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20,
		                         ARMOR_GAUGE_LAYER_UNITS / 2,
		                         ARMOR_GAUGE_LAYER_UNITS);

		const Uint8 *const pixels = (const Uint8 *)VGAScreen->pixels;
		const int pitch = VGAScreen->pitch;
		const int barY = by + bh + 1, barX = bx + 1;
		const int barLen = bw - 2;

		qa_check((pixels[barY * pitch + barX] & 0xf0) == (SHIELD_GAUGE_BASE & 0xf0),
		         "the partner's shield bar is drawn in the shield gauge's own bank");
		qa_check((pixels[(barY + ENEMY_BAR_THICK) * pitch + barX] & 0xf0)
		         == (armorGaugeLayerCol[0] & 0xf0),
		         "the armor bar sits right under it, in the armor gauge's bank");

		// Half a layer of armour leaves the far end of that bar empty.
		qa_check((pixels[(barY + ENEMY_BAR_THICK) * pitch + barX + barLen - 1] & 0xf0)
		         == (armorGaugeLayerCol[0] & 0xf0)
		         && pixels[(barY + ENEMY_BAR_THICK) * pitch + barX + barLen - 1]
		            < pixels[(barY + ENEMY_BAR_THICK) * pitch + barX],
		         "...half full, so its far end is track rather than fill");

		// Rollover uses the current layer for fill and the previous layer for track.
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20,
		                         ARMOR_GAUGE_LAYER_UNITS + ARMOR_GAUGE_LAYER_UNITS / 2,
		                         2 * ARMOR_GAUGE_LAYER_UNITS);

		qa_check((pixels[(barY + ENEMY_BAR_THICK) * pitch + barX] & 0xf0)
		         == (armorGaugeLayerCol[1] & 0xf0),
		         "a rolled-over armor bar fills in the layer it has reached");
		qa_check((pixels[(barY + ENEMY_BAR_THICK) * pitch + barX + barLen - 1] & 0xf0)
		         == (armorGaugeLayerCol[0] & 0xf0),
		         "...over a track showing the full layer underneath");

		// A Life Boost hull is full at its current 12-point ceiling.
		const int farEnd = (barY + ENEMY_BAR_THICK) * pitch + barX + barLen - 1;
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20, 12, 12);
		const Uint8 weakHull = pixels[farEnd];
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20,
		                         12, ARMOR_GAUGE_LAYER_UNITS);
		qa_check(weakHull > pixels[farEnd],
		         "a mini armor bar divides by the hull's own ceiling, not the strongest hull's");

		// Armor above one gauge layer still reads full at its current ceiling.
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20,
		                         2 * ARMOR_GAUGE_LAYER_UNITS, 2 * ARMOR_GAUGE_LAYER_UNITS);
		qa_check((pixels[farEnd] & 0xf0) == (armorGaugeLayerCol[1] & 0xf0),
		         "a fully repaired upgraded hull fills its top layer end to end");

		// Shield bars also use the ship's current ceiling.
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 8, 8, 12, 12);
		const Uint8 weakShield = pixels[barY * pitch + barX + barLen - 1];
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 8, 20, 12, 12);
		qa_check(weakShield > pixels[barY * pitch + barX + barLen - 1],
		         "...and the shield bar does the same with shield_max");
	}

	// Linked Arcade skips bars because its shared HUD already shows both ships.
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8 && VGAScreen->h > 70)
	{
		const bool savedNet = isNetworkGame;
		const JE_boolean savedTwo = twoPlayerMode, savedSep = arcadeSeparateMode;
		const JE_boolean savedCoop = coopCampaignMode, savedCoopE = coopEndlessMode;
		const uint savedPlayerNum = thisPlayerNum;
		const NetShipView savedView = netStyleView(0);

		isNetworkGame = true;
		twoPlayerMode = true;
		coopCampaignMode = false;
		coopEndlessMode = false;
		thisPlayerNum = 1;                 // this machine flies seat 0
		qa_set_local_hp_bars(NET_HP_BARS_ALWAYS);
		enemyBarPosition = ENEMY_BAR_POS_BOTTOM;
		enemyBarOpacity = 100;
		shipGr2 = 5;
		player[1].is_alive = true;
		player[1].x = 40;
		player[1].y = 40;
		player[1].shield_max = 20;
		player[1].shield = 20;
		player[1].armor = ARMOR_GAUGE_LAYER_UNITS;

		// The rows the bars would land on, under a 24x28 hull whose box ends at y = 61.
		const int probeY = 62, probeRows = 2 * ENEMY_BAR_THICK;
		Uint8 *const pixels = (Uint8 *)VGAScreen->pixels;
		const int pitch = VGAScreen->pitch;

		arcadeSeparateMode = true;   // independent HUDs
		for (int row = 0; row < probeRows; ++row)
			memset(pixels + (probeY + row) * pitch + 30, 0, 40);
		hud_draw_ship_hp_bars();
		int painted = 0;
		for (int row = 0; row < probeRows; ++row)
			for (int col = 30; col < 70; ++col)
				painted += (pixels[(probeY + row) * pitch + col] != 0);
		qa_check(painted > 0, "a session with two HUDs paints the partner's bars");

		arcadeSeparateMode = false;  // shared HUD
		for (int row = 0; row < probeRows; ++row)
			memset(pixels + (probeY + row) * pitch + 30, 0, 40);
		hud_draw_ship_hp_bars();
		painted = 0;
		for (int row = 0; row < probeRows; ++row)
			for (int col = 30; col < 70; ++col)
				painted += (pixels[(probeY + row) * pitch + col] != 0);
		qa_check(painted == 0, "Linked Arcade paints none, whatever the setting says");

		// 70% bar opacity scaled by 30% ship opacity is 21%.
		enemyBarOpacity = 70;
		NetShipView fade = netStyleView(0);
		fade.opacity = 30;
		fade.shipOpacity = true;
		netStyleSetView(0, fade);
		qa_check(hud_ship_hp_bar_opacity() == 53,
		         "the partner's opacity scales the enemy-bar setting instead of replacing it");
		fade.shipOpacity = false;
		netStyleSetView(0, fade);
		qa_check(hud_ship_hp_bar_opacity() == 178,
		         "...and sparing their ship leaves the bars at the enemy-bar setting alone");

		netStyleSetView(0, savedView);
		thisPlayerNum = savedPlayerNum;
		coopEndlessMode = savedCoopE;
		coopCampaignMode = savedCoop;
		arcadeSeparateMode = savedSep;
		twoPlayerMode = savedTwo;
		isNetworkGame = savedNet;
	}

	enemyBarOpacity = savedOp;
	enemyBarPosition = savedPos;
	enemyBarLayout = savedLayout;
	shipGr = savedGr;
	shipGr2 = savedGr2;
	player[0] = saved0;
	player[1] = saved1;
}

// Per-seat dyes, per-seat views, previews, and sprite blending.
void qa_test_online_ship_style(void)
{
	const bool savedNet = isNetworkGame;
	const JE_boolean savedTwo = twoPlayerMode;
	const JE_boolean savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	const uint savedPlayerNum = thisPlayerNum;
	const int savedColor[2] = { netStyleSeatColor(0), netStyleSeatColor(1) };
	const NetShipView savedView[2] = { netStyleView(0), netStyleView(1) };

	netStylePreviewClear();

	// Start outside Endless so all banks are available.
	endlessMode = false;
	endlessCampaignMods = false;

	// Offline, the one ship wears its own dye; partner styles and stale seats are ignored.
	isNetworkGame = false;
	twoPlayerMode = false;
	thisPlayerNum = 2;
	netStyleSetSeatColor(0, NET_SHIP_COLOR_NONE);
	netStyleSetSeatColor(1, 5);
	qa_set_local_opacity(50);
	qa_check(netStyleLocalSeat() == 0 && netStyleIsPlain(netStyleForSeat(0))
	         && netStyleIsPlain(netStyleForSeat(1)),
	         "offline play ignores the online ship colours, whatever seat it last flew");
	netStyleSetSeatColor(0, 6);
	qa_check(netStyleForSeat(0).bank == 5 && netStyleForSeat(0).opacity == NET_STYLE_SOLID
	         && netStyleIsPlain(netStyleForSeat(1)),
	         "offline the one ship wears its own dye, solid");
	netStyleSetSidekickDye(0, false);
	qa_check(netStyleForSidekick(0).bank < 0 && netStyleForSidekick(0).opacity == NET_STYLE_SOLID
	         && netStyleForSeat(0).bank == 5,
	         "...and with Color Sidekicks off its sidekicks keep their own colors");
	netStyleSetSidekickDye(0, true);
	qa_check(netStyleForSidekick(0).bank == 5, "...and with it on they wear the dye");
	twoPlayerMode = true;
	qa_check(netStyleIsPlain(netStyleForSeat(0)), "...but a local two-player game stays undyed");
	netStyleSetSeatColor(0, NET_SHIP_COLOR_NONE);
	qa_set_local_opacity(NET_OPACITY_FULL);

	isNetworkGame = true;
	twoPlayerMode = true;
	qa_set_local_opacity(50);
	netStyleSetSeatColor(0, 3);

	qa_check(netStyleLocalSeat() == 1, "seat two is this machine's ship");
	qa_check(netStylePeerColor() == 3, "the partner's dye is the one the other seat holds");
	qa_check(netStyleForSeat(1).bank == 4 && netStyleForSeat(1).opacity == NET_STYLE_SOLID,
	         "this ship wears the dye its own seat holds, at full strength");
	qa_check(netStyleForSeat(0).bank == 2 && netStyleForSeat(0).opacity == 8,
	         "the partner wears the dye they announced, at the opacity this machine set");

	thisPlayerNum = 1;
	qa_set_local_opacity(50);
	qa_check(netStyleForSeat(0).opacity == NET_STYLE_SOLID && netStyleForSeat(1).opacity == 8,
	         "the opacity lands on the partner whichever seat this machine flies");

	// Each seat retains its own view; only the local seat affects this screen.
	qa_check(netStyleView(1).opacity == 50 && netStyleView(0).opacity == 50
	         && netStyleLocalView().opacity == 50,
	         "each seat holds its own view and this machine reads the seat it flies");
	thisPlayerNum = 2;
	qa_set_local_opacity(NET_OPACITY_FULL);
	thisPlayerNum = 1;
	qa_check(netStyleView(1).opacity == NET_OPACITY_FULL && netStyleLocalView().opacity == 50,
	         "...so the peer's own view never overwrites this one");

	// Picker steps map to a nonzero, strictly decreasing mix.
	bool laddered = true;
	int previous = NET_STYLE_SOLID + 1;
	for (int pct = NET_OPACITY_FULL; pct >= NET_OPACITY_MIN; pct -= NET_OPACITY_STEP)
	{
		qa_set_local_opacity(pct);
		const int opacity = netStyleForSeat(1).opacity;
		laddered = laddered && opacity < previous && opacity >= 1 && opacity <= NET_STYLE_SOLID;
		previous = opacity;
	}
	qa_check(laddered, "each opacity step is fainter than the last and none of them reaches nothing");

	netStyleSetSeatColor(0, NET_SHIP_COLORS + 7);
	qa_check(netStyleSeatColor(0) == NET_SHIP_COLOR_NONE,
	         "a dye off the end of the palette is read as no dye");
	netStyleSetSeatColor(0, 3);

	// Endless reserves the four kill-fire palette banks.
	{
		static const int driveFilter[] = {
			ENDLESS_TURBODRIVE_SHIP_FILTER, ENDLESS_OVERDRIVE_SHIP_FILTER,
			ENDLESS_OVERBLAST_SHIP_FILTER, ENDLESS_EVIL_SHIP_FILTER,
		};

		endlessMode = false;
		endlessCampaignMods = false;
		bool freeOutside = true;
		for (unsigned int i = 0; i < COUNTOF(driveFilter); ++i)
			freeOutside = freeOutside && !netStyleColorReserved((driveFilter[i] >> 4) + 1);
		qa_check(freeOutside, "outside Endless every bank is offered");

		endlessMode = true;
		bool heldBack = true;
		int reserved = 0;
		for (int color = 1; color <= NET_SHIP_COLORS; ++color)
			if (netStyleColorReserved(color))
				++reserved;
		for (unsigned int i = 0; i < COUNTOF(driveFilter); ++i)
			heldBack = heldBack && netStyleColorReserved((driveFilter[i] >> 4) + 1);
		qa_check(heldBack && reserved == (int)COUNTOF(driveFilter),
		         "Endless withholds the three drive banks and the evil one, and no others");

		netStyleSetSeatColor(0, (ENDLESS_TURBODRIVE_SHIP_FILTER >> 4) + 1);
		thisPlayerNum = 1;
		qa_check(netStyleForSeat(0).bank < 0, "a withheld dye is not worn in Endless either");
		netStyleSetSeatColor(0, 3);

		endlessMode = false;
	}

	// Apply to Ship affects bodies only; shots always use partner opacity.
	thisPlayerNum = 1;
	qa_set_local_opacity(50);
	qa_set_local_ship_opacity(true);
	qa_check(netStyleForSeat(1).opacity == 8 && netStyleForShot(1).opacity == 8,
	         "the partner's ship and its shots share one opacity by default");
	qa_set_local_ship_opacity(false);
	qa_check(netStyleForSeat(1).opacity == NET_STYLE_SOLID && netStyleForSeat(1).bank == 4,
	         "sparing the ship draws their hull solid and keeps its dye");
	qa_check(netStyleForShot(1).opacity == 8 && netStyleForShot(1).bank < 0,
	         "...and leaves their shots faded, and undyed either way");
	qa_check(netStyleForSeat(0).opacity == NET_STYLE_SOLID
	         && netStyleForShot(0).opacity == NET_STYLE_SOLID,
	         "neither reaches the ship this machine flies");
	qa_set_local_ship_opacity(true);

	// Fresh sessions start plain; saved sessions restore their styles later.
	netStyleSetSeatColor(0, 9);
	netStyleSetSeatColor(1, 4);
	qa_set_local_opacity(NET_OPACITY_MIN);
	qa_set_local_ship_opacity(false);
	qa_set_local_hp_bars(NET_HP_BARS_ALWAYS);
	netStyleSessionReset();
	bool forgotten = true;
	for (uint seat = 0; seat < 2; ++seat)
		forgotten = forgotten && netStyleSeatColor(seat) == NET_SHIP_COLOR_NONE
		            && netStyleView(seat).opacity == NET_OPACITY_FULL
		            && netStyleView(seat).shipOpacity
		            && netStyleView(seat).hpBars == NET_HP_BARS_OFF;
	qa_check(forgotten, "a session starting forgets the look the last one was flying");
	netStyleSessionReset();
	qa_check(netStylePeerColor() == NET_SHIP_COLOR_NONE, "a session ending forgets the peer's dye");

	// Menu previews override session styles.
	netStylePreviewSet(10, 50);
	qa_check(netStyleForSeat(0).bank == 9 && netStyleForSeat(1).opacity == 8,
	         "a menu preview dresses every ship it draws");
	netStylePreviewClear();

	// One-pixel Sprite2 frames for direct blend checks.
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8)
	{
		union {
			Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
			Uint8 bytes[10];
		} frames = { .bytes = { 4, 0, 7, 0,        // two frames, one pixel each, bank 7 shade 12
		                        0x10, 0x7c, 0x0f,
		                        0x10, 0x7c, 0x0f } };
		const Sprite2_array sheet = { sizeof(frames.bytes), frames.bytes };

		Uint8 *const row = (Uint8 *)VGAScreen->pixels;
		const Uint8 savedPixels[2] = { row[0], row[1] };
		const int mixed = (12 * 4 + 4 * (NET_STYLE_SOLID - 4) + 8) / NET_STYLE_SOLID;

		row[0] = 0x24;  // bank 2, shade 4
		blit_sprite2_alpha_clip(VGAScreen, 0, 0, sheet, 1, -1, 4);
		row[1] = 0x24;
		blit_sprite2_alpha_clip(VGAScreen, 1, 0, sheet, 2, 9, 4);
		qa_check(row[0] == (Uint8)(0x70 | mixed),
		         "a faded sprite keeps its own bank and mixes only brightness");
		qa_check(row[1] == (Uint8)(0x90 | mixed),
		         "...and a dyed one fades inside the bank it was dyed with");

		row[0] = 0x24;
		blit_sprite2_alpha_clip(VGAScreen, 0, 0, sheet, 1, -1, NET_STYLE_SOLID);
		row[1] = 0x24;
		blit_sprite2_alpha_clip(VGAScreen, 1, 0, sheet, 2, 9, NET_STYLE_SOLID);
		qa_check(row[0] == 0x7c && row[1] == 0x9c,
		         "nothing left to mix hands the sprite back to the plain and filtered blits");

		row[0] = savedPixels[0];
		row[1] = savedPixels[1];
	}

	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
	thisPlayerNum = savedPlayerNum;
	for (uint seat = 0; seat < 2; ++seat)
	{
		netStyleSetSeatColor(seat, savedColor[seat]);
		netStyleSetView(seat, savedView[seat]);
	}
}

void qa_test_network_settings(void)
{
	int savedSpark[SSW_COUNT], savedEpDiff[EDW_COUNT];
	memcpy(savedSpark, superSparkMode, sizeof(savedSpark));
	memcpy(savedEpDiff, epDiffMode, sizeof(savedEpDiff));
	const int savedZicaBase = zicaLaserBase;
	bool savedReworks[REWORK_COUNT];
	memcpy(savedReworks, reworkEnabled, sizeof(savedReworks));
	const int savedWallop = wallopSecondBolt;
	const bool savedCharge = chargeLaserCannon, savedDispensers = restoreBaseDispensers;
	const bool savedLifeBoost = arcadeLifeBoost, savedRandomBalls = arcadeRandomBalls;
	const bool savedRearScale = arcadeRearGunScale;
	const bool savedCenteredHitboxes = centeredShotHitboxes, savedGuidedAim = guidedShotScreenAim;
	const bool savedParallax = extraParallax;
	EndlessScalingOverride savedPins[ESO_COUNT];
	memcpy(savedPins, endlessScalingOverride, sizeof(savedPins));
	const int savedXmas = xmasMode;
	const JE_byte savedSpeed = gameSpeed;
	const bool savedRollbackConfig = net_rollback, savedRecoveryConfig = net_desync_recovery;
	const bool savedVt = vt_ship, savedMotion = smoothMotion;
	const JE_boolean savedScroll = smoothScroll;
	const bool savedSessionMode = nrb_session_mode(), savedSessionVt = nrb_session_vt();
	const bool savedSessionRecovery = nrb_session_recovery();
	const bool savedSharedCredit = coopSharedCredit;
	const bool savedDoublePickups = coopDoubleEarnings;
	const JE_boolean savedCoopCampaign = coopCampaignMode;
	const JE_boolean savedExpertMode = expertMode;
	const bool savedInfShields = cheatInfiniteShields;
	const bool savedInfArmor = cheatInfiniteArmor;
	const bool savedInfGenerator = cheatInfiniteGenerator;
	const bool savedNoEnemyFire = cheatNoEnemyFire;
	const bool savedInstantCharge = cheatInstantCharge;
	const bool savedInfSidekick = cheatInfiniteSidekickAmmo;
	const bool savedAutoSpecial = autoFireSpecial;
	const bool savedAutoTwiddle = debugAutofireTwiddle;
	const bool savedToggleFire = debugToggleFire;
	const bool savedDifficultyAdjust = difficultyAdjust;
	const bool savedTwiddleTrigger = debugTwiddleTrigger;
	const bool savedConstantPlay = constantPlay;
	const bool savedConstantDie = constantDie;
	const JE_byte savedNoclip = noclipMode;
	const JE_byte savedChargeAF = chargeSidekickAutofire;
	const JE_byte savedTwiddle = debugTwiddleSpecial;
	int savedExpert[NETWORK_EXPERT_SLOTS];
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		savedExpert[i] = *expertSettings[i].value;
	{
		int margin = 320, codes = 0;
		for (const char *msg; (msg = network_halt_message((unsigned int)codes)) != NULL; ++codes)
		{
			const int center = JE_fontCenter(msg, SMALL_FONT_SHAPES);
			if (center < margin)
				margin = center;
		}
		qa_check(margin >= 8 && codes > NET_HALT_CUSTOM_SYNC,
		         "every network halt reason fits the disconnect screen");
		printf("# halt screen: %d reasons, tightest side margin %dpx\n", codes, margin);
	}

	/* SDLNet_Read/Write16/32 require naturally aligned storage. Keep guard bytes around an
	 * aligned payload instead of making the alignment itself part of this bounds test. */
	union {
		Uint32 align;
		Uint8 bytes[NETWORK_SETTINGS_SIZE + 8];
	} guarded;
	Uint8 *const packet = guarded.bytes + 4;

	for (int i = 0; i < SSW_COUNT; ++i)
		superSparkMode[i] = i % SUPER_SPARKS_COUNT;
	for (int i = 0; i < EDW_COUNT; ++i)
		epDiffMode[i] = i % EPDIFF_MODE_COUNT;
	zicaLaserBase = ZICA_BASE_EP4; wallopSecondBolt = SUPER_SPARKS_ON;
	for (int i = 0; i < REWORK_COUNT; ++i)
		reworkEnabled[i] = i % 3 == 0;
	chargeLaserCannon = true; restoreBaseDispensers = false;
	arcadeLifeBoost = true; arcadeRandomBalls = false; arcadeRearGunScale = true;
	centeredShotHitboxes = true; guidedShotScreenAim = true; extraParallax = true;
	xmas = true; gameSpeed = 2;
	net_rollback = true; net_desync_recovery = true;
	coopSharedCredit = true;
	coopDoubleEarnings = true;
	vt_ship = true; smoothMotion = true; smoothScroll = true;
	// Distinct Expert values expose swapped settings while peers check the same rules.
	expertMode = true;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = expertSettings[i].lo + expertSettings[i].step * (i + 1);
	cheatInfiniteShields = true;
	cheatInfiniteArmor = false;
	cheatInfiniteGenerator = true;
	cheatNoEnemyFire = false;
	cheatInstantCharge = true;
	cheatInfiniteSidekickAmmo = false;
	autoFireSpecial = true;
	debugAutofireTwiddle = false;
	debugToggleFire = true;
	difficultyAdjust = false;
	debugTwiddleTrigger = true;
	noclipMode = NOCLIP_TRANSPARENT;
	constantPlay = true;
	constantDie = false;
	chargeSidekickAutofire = CHARGE_AUTOFIRE_FULL;
	debugTwiddleSpecial = SPECIAL_NUM > 0 ? 1 : 0;
	memset(guarded.bytes, 0x5a, sizeof(guarded.bytes));
	const int packed = network_settings_pack(packet);
	qa_check(packed == NETWORK_SETTINGS_SIZE && guarded.bytes[3] == 0x5a
	         && guarded.bytes[4 + NETWORK_SETTINGS_SIZE] == 0x5a,
	         "network settings packing writes exactly its fixed block and no byte more");

	/* A joiner has different local preferences before it adopts the host block. */
	for (int i = 0; i < SSW_COUNT; ++i) superSparkMode[i] = SUPER_SPARKS_OFF;
	for (int i = 0; i < EDW_COUNT; ++i) epDiffMode[i] = EPDIFF_EP13;
	zicaLaserBase = ZICA_BASE_AUTO; wallopSecondBolt = SUPER_SPARKS_OFF;
	for (int i = 0; i < REWORK_COUNT; ++i)
		reworkEnabled[i] = i % 3 != 0;
	chargeLaserCannon = false; restoreBaseDispensers = true;
	arcadeLifeBoost = false; arcadeRandomBalls = true; arcadeRearGunScale = false;
	centeredShotHitboxes = false; guidedShotScreenAim = false; extraParallax = false;
	xmas = false; xmasMode = -1; gameSpeed = 5;
	endlessScalingOverride[ESO_ARMOR].active = true;
	endlessScalingOverride[ESO_ARMOR].value = 150;
	expertMode = false;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = expertSettings[i].def;
	cheatInfiniteShields = false;
	cheatInfiniteArmor = true;
	cheatInfiniteGenerator = false;
	cheatNoEnemyFire = true;
	cheatInstantCharge = false;
	cheatInfiniteSidekickAmmo = true;
	autoFireSpecial = false;
	debugAutofireTwiddle = true;
	debugToggleFire = false;
	difficultyAdjust = true;
	debugTwiddleTrigger = false;
	noclipMode = NOCLIP_OFF;
	constantPlay = false;
	constantDie = true;
	chargeSidekickAutofire = CHARGE_AUTOFIRE_OFF;
	debugTwiddleSpecial = 0;
	coop_set_session_shared_credit(false);
	coopCampaignMode = true;
	qa_check(network_settings_adopt(packet) == NETWORK_SETTINGS_SIZE,
	         "network settings decoder consumes its exact fixed block");
	bool arraysMatch = true;
	for (int i = 0; i < SSW_COUNT; ++i) arraysMatch &= superSparkMode[i] == i % SUPER_SPARKS_COUNT;
	for (int i = 0; i < EDW_COUNT; ++i) arraysMatch &= epDiffMode[i] == i % EPDIFF_MODE_COUNT;
	for (int i = 0; i < REWORK_COUNT; ++i) arraysMatch &= reworkEnabled[i] == (i % 3 == 0);
	qa_check(arraysMatch && zicaLaserBase == ZICA_BASE_EP4 && wallopSecondBolt == SUPER_SPARKS_ON
	         && chargeLaserCannon && !restoreBaseDispensers && arcadeLifeBoost
	         && !arcadeRandomBalls && arcadeRearGunScale && centeredShotHitboxes && guidedShotScreenAim
	         && extraParallax && xmasMode == 1 && xmas && gameSpeed == 2
	         && endlessScalingOverrideCount() == 0
	         && nrb_session_mode() && nrb_session_vt() && nrb_session_recovery()
	         && coop_credit_is_shared(),
	         "joiner adopts every host-authoritative simulation setting");
	bool expertMatch = expertMode;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		expertMatch &= *expertSettings[i].value == expertSettings[i].lo + expertSettings[i].step * (i + 1);
	qa_check(expertMatch, "...including Expert Mode and every tunable behind it");
	qa_check(cheatInfiniteShields && !cheatInfiniteArmor && cheatInfiniteGenerator
	         && !cheatNoEnemyFire && cheatInstantCharge && !cheatInfiniteSidekickAmmo
	         && autoFireSpecial && !debugAutofireTwiddle && debugToggleFire
	         && !difficultyAdjust && debugTwiddleTrigger && noclipMode == NOCLIP_TRANSPARENT
	         && constantPlay && !constantDie
	         && chargeSidekickAutofire == CHARGE_AUTOFIRE_FULL
	         && debugTwiddleSpecial == (SPECIAL_NUM > 0 ? 1 : 0),
	         "...including initial autofire and every simulation-affecting debug control");
	// Doubling is carried in the same word but is inert under Shared, so check the flag itself
	// by flipping the credit mode the adopted value sits behind.
	coop_set_session_shared_credit(false);
	qa_check(coop_earnings_are_doubled(), "...including whether Individual pays pickups twice");
	coop_set_session_shared_credit(true);
	network_settings_restore();
	arraysMatch = true;
	for (int i = 0; i < SSW_COUNT; ++i) arraysMatch &= superSparkMode[i] == SUPER_SPARKS_OFF;
	for (int i = 0; i < EDW_COUNT; ++i) arraysMatch &= epDiffMode[i] == EPDIFF_EP13;
	for (int i = 0; i < REWORK_COUNT; ++i) arraysMatch &= reworkEnabled[i] == (i % 3 != 0);
	qa_check(arraysMatch && zicaLaserBase == ZICA_BASE_AUTO && wallopSecondBolt == SUPER_SPARKS_OFF
	         && !chargeLaserCannon && restoreBaseDispensers && !arcadeLifeBoost
	         && arcadeRandomBalls && !arcadeRearGunScale && !centeredShotHitboxes && !guidedShotScreenAim
	         && !extraParallax && xmasMode == -1 && !xmas && gameSpeed == 5
	         && endlessScalingOverride[ESO_ARMOR].active
	         && endlessScalingOverride[ESO_ARMOR].value == 150,
	         "leaving a network session restores every local simulation preference");
	expertMatch = !expertMode;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		expertMatch &= *expertSettings[i].value == expertSettings[i].def;
	qa_check(expertMatch, "...Expert Mode and its tunables included, so the next solo game is ours");
	qa_check(!cheatInfiniteShields && cheatInfiniteArmor && !cheatInfiniteGenerator
	         && cheatNoEnemyFire && !cheatInstantCharge && cheatInfiniteSidekickAmmo
	         && !autoFireSpecial && debugAutofireTwiddle && !debugToggleFire
	         && difficultyAdjust && !debugTwiddleTrigger && noclipMode == NOCLIP_OFF
	         && !constantPlay && constantDie
	         && chargeSidekickAutofire == CHARGE_AUTOFIRE_OFF && debugTwiddleSpecial == 0,
	         "...and restores the joiner's local autofire/debug controls afterward");

	/* The host runs on flags armed from its own config; the joiner adopts the block packed from
	 * that same config. */
	coopSharedCredit = false;
	coopDoubleEarnings = true;
	net_rollback = true;
	net_desync_recovery = true;
	vt_ship = true; smoothMotion = true; smoothScroll = true;
	coopCampaignMode = true;
	coop_set_session_shared_credit(true);    // stale session values the arm must replace,
	coop_set_session_double_earnings(false);  // or a missed flag hides behind leftovers
	network_arm_local_session();
	const bool hostDoubled = coop_earnings_are_doubled();
	const bool hostShared = coop_credit_is_shared();
	network_settings_pack(packet);
	coop_set_session_shared_credit(true);     // a joiner arrives holding other values
	coop_set_session_double_earnings(false);
	network_settings_adopt(packet);
	qa_check(hostDoubled && !hostShared
	         && coop_earnings_are_doubled() == hostDoubled
	         && coop_credit_is_shared() == hostShared,
	         "host arming and joiner adoption produce the same credit session");
	network_settings_restore();

	memset(packet, 0xff, NETWORK_SETTINGS_SIZE);
	network_settings_adopt(packet);
	bool malformedClamped = true;
	for (int i = 0; i < SSW_COUNT; ++i) malformedClamped &= superSparkMode[i] == SUPER_SPARKS_AUTO;
	for (int i = 0; i < EDW_COUNT; ++i) malformedClamped &= epDiffMode[i] == EPDIFF_AUTO;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		malformedClamped &= *expertSettings[i].value == expertSettings[i].hi;
	qa_check(malformedClamped && zicaLaserBase == ZICA_BASE_AUTO
	         && wallopSecondBolt == SUPER_SPARKS_AUTO
	         && xmasMode == 0 && !xmas && gameSpeed == 4 && noclipMode < NOCLIP_NUM
	         && chargeSidekickAutofire < CHARGE_AUTOFIRE_NUM
	         && debugTwiddleSpecial <= SPECIAL_NUM,
	         "hostile network settings are clamped before any array can be indexed"
	         " or any multiplier applied");
	network_settings_restore();

	memcpy(superSparkMode, savedSpark, sizeof(savedSpark));
	memcpy(epDiffMode, savedEpDiff, sizeof(savedEpDiff));
	zicaLaserBase = savedZicaBase; wallopSecondBolt = savedWallop;
	memcpy(reworkEnabled, savedReworks, sizeof(savedReworks));
	chargeLaserCannon = savedCharge; restoreBaseDispensers = savedDispensers;
	arcadeLifeBoost = savedLifeBoost; arcadeRandomBalls = savedRandomBalls;
	arcadeRearGunScale = savedRearScale; centeredShotHitboxes = savedCenteredHitboxes;
	guidedShotScreenAim = savedGuidedAim; extraParallax = savedParallax;
	memcpy(endlessScalingOverride, savedPins, sizeof(savedPins));
	xmasMode = savedXmas; gameSpeed = savedSpeed;
	net_rollback = savedRollbackConfig; net_desync_recovery = savedRecoveryConfig;
	vt_ship = savedVt; smoothMotion = savedMotion; smoothScroll = savedScroll;
	nrb_set_session_mode(savedSessionMode);
	nrb_set_session_vt(savedSessionVt);
	nrb_set_session_recovery(savedSessionRecovery);
	coopSharedCredit = savedSharedCredit;
	coopDoubleEarnings = savedDoublePickups;
	coop_set_session_shared_credit(savedSharedCredit);
	coop_set_session_double_earnings(savedDoublePickups);
	coopCampaignMode = savedCoopCampaign;
	expertMode = savedExpertMode;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = savedExpert[i];
	cheatInfiniteShields = savedInfShields;
	cheatInfiniteArmor = savedInfArmor;
	cheatInfiniteGenerator = savedInfGenerator;
	cheatNoEnemyFire = savedNoEnemyFire;
	cheatInstantCharge = savedInstantCharge;
	cheatInfiniteSidekickAmmo = savedInfSidekick;
	autoFireSpecial = savedAutoSpecial;
	debugAutofireTwiddle = savedAutoTwiddle;
	debugToggleFire = savedToggleFire;
	difficultyAdjust = savedDifficultyAdjust;
	debugTwiddleTrigger = savedTwiddleTrigger;
	noclipMode = savedNoclip;
	constantPlay = savedConstantPlay;
	constantDie = savedConstantDie;
	chargeSidekickAutofire = savedChargeAF;
	debugTwiddleSpecial = savedTwiddle;
}

/* The Endless lobby block rides the connect packet beside the settings block. Every field the
 * joiner adopts has to arrive, and every out-of-range byte has to be clamped, not indexed with. */
void qa_test_network_endless_lobby(void)
{
	const int savedMode = network_host_endless_run_mode;
	const int savedChooser = network_host_endless_chooser;
	const bool savedCombo = network_host_endless_combo_shared;
	const int savedBaseRule = network_host_endless_base_rule;
	char savedSeed[NET_ENDLESS_SEED_MAX], savedHostSeed[NET_ENDLESS_SEED_MAX];
	memcpy(savedSeed, network_endless_session_seed, sizeof(savedSeed));
	memcpy(savedHostSeed, network_host_endless_seed, sizeof(savedHostSeed));

	Uint8 block[4 + NET_ENDLESS_SEED_MAX];
	memset(block, 0, sizeof(block));
	block[0] = (Uint8)ENDLESS_RUNMODE_HARDCORE;
	block[1] = (Uint8)(ENDLESS_PICK_COUNT - 1);
	block[2] = 1;
	block[3] = (Uint8)(ENDLESS_BASE_RULE_COUNT - 1);
	memcpy(&block[4], "qa-seed-123", sizeof("qa-seed-123"));
	network_endless_adopt(block);
	qa_check(network_host_endless_run_mode == ENDLESS_RUNMODE_HARDCORE
	         && network_host_endless_chooser == ENDLESS_PICK_COUNT - 1
	         && network_host_endless_combo_shared
	         && network_host_endless_base_rule == ENDLESS_BASE_RULE_COUNT - 1
	         && strcmp(network_endless_session_seed, "qa-seed-123") == 0,
	         "joiner adopts every field of the host's Endless lobby block");

	memset(block, 0xEE, sizeof(block));
	network_endless_adopt(block);
	qa_check(network_host_endless_run_mode == ENDLESS_RUNMODE_STANDARD
	         && network_host_endless_chooser == ENDLESS_PICK_HOST
	         && network_host_endless_base_rule == ENDLESS_BASE_VARIED,
	         "out-of-range Endless lobby bytes are clamped to their defaults");

	bool seedScrubbed = network_endless_session_seed[NET_ENDLESS_SEED_MAX - 1] == '\0'
	                 && network_endless_session_seed[0] != '\0';
	for (int i = 0; network_endless_session_seed[i] != '\0'; ++i)
		seedScrubbed = seedScrubbed && network_endless_session_seed[i] == '?';
	qa_check(seedScrubbed,
	         "a hostile Endless seed is scrubbed to printable characters and terminated");

	network_host_endless_seed[0] = '\0';
	network_endless_session_begin();
	qa_check(network_endless_session_seed[0] != '\0',
	         "a blank lobby seed rolls a session seed instead of hashing the empty string");

	SDL_strlcpy(network_host_endless_seed, "fixed", sizeof(network_host_endless_seed));
	network_endless_session_begin();
	qa_check(strcmp(network_endless_session_seed, "fixed") == 0
	         && strcmp(network_host_endless_seed, "fixed") == 0,
	         "a named lobby seed is carried into the session verbatim");

	network_host_endless_run_mode = savedMode;
	network_host_endless_chooser = savedChooser;
	network_host_endless_combo_shared = savedCombo;
	network_host_endless_base_rule = savedBaseRule;
	memcpy(network_endless_session_seed, savedSeed, sizeof(savedSeed));
	memcpy(network_host_endless_seed, savedHostSeed, sizeof(savedHostSeed));
}

void qa_test_resync_serialization(void)
{
	static Uint8 raw[4096], packed[8192], expanded[4096];
	Uint32 rng = 0x51a7e123u;

	for (unsigned pass = 0; pass < 256; ++pass)
	{
		watchdog_heartbeat();
		const size_t n = 1 + qa_prng(&rng) % sizeof(raw);
		for (size_t i = 0; i < n; ++i)
		{
			const Uint32 r = qa_prng(&rng);
			raw[i] = (pass % 3 == 0 && (r & 7) != 0) ? 0 : (Uint8)r;
		}

		const size_t compressed = nrb_resync_compress(raw, n, packed, sizeof(packed));
		const size_t restored = compressed == 0 ? 0
		                      : nrb_resync_expand(packed, compressed, expanded, sizeof(expanded));
		qa_check(compressed != 0 && restored == n && memcmp(raw, expanded, n) == 0,
		         "resync compression round-trips arbitrary state");

		if (compressed > 1)
			qa_check(nrb_resync_expand(packed, compressed - 1, expanded, n) != n,
			         "truncated resync stream is rejected");
		qa_check(nrb_resync_expand(packed, compressed, expanded, n - 1) == 0,
		         "oversized resync expansion is rejected");
	}

	{
		const Uint8 zero_run[] = { 0, 0, 0 };
		const Uint8 huge_run[] = { 0, 0xff, 0xff };
		qa_check(nrb_resync_expand(zero_run, sizeof(zero_run), expanded, sizeof(expanded)) == 0,
		         "zero-length resync run is rejected");
		qa_check(nrb_resync_expand(huge_run, sizeof(huge_run), expanded, sizeof(expanded)) == 0,
		         "oversized resync run is rejected");
	}

	/* The packet parser must safely refuse every short header and randomized malformed body. */
	for (int len = 0; len < 48; ++len)
	{
		for (int i = 0; i < NET_PACKET_SIZE; ++i)
			packed[i] = (Uint8)qa_prng(&rng);
		nrb_handle_packet(packed, len);
	}
	for (unsigned pass = 0; pass < 512; ++pass)
	{
		const int len = (int)(qa_prng(&rng) % (NET_PACKET_SIZE + 1));
		for (int i = 0; i < NET_PACKET_SIZE; ++i)
			packed[i] = (Uint8)qa_prng(&rng);
		nrb_handle_packet(packed, len);
	}
	qa_check(true, "malformed rollback input packets are safely consumed");
}

/* One sample of every modifier-derived combat parameter, memcmp-comparable (zeroed first, so
 * padding cannot differ). Floats are compared by bit pattern: parity means bit-identical. */
typedef struct
{
	EndlessScaling s;
	Uint32 gravity, gravityX, gravityY, moveScale;
	int scrollPct, genAdd, hitbox, contactElite, contactChamp;
	int shockElite, shockChamp, martyrElite, martyrChamp;
	int champFire, champDmg;
	unsigned staticDrain;
	Uint8 regenOff, regenFree, seeker, seekTier, seekPasses, scrollActive;
} QaModParity;

static Uint32 qa_float_bits(float v)
{
	Uint32 bits;
	memcpy(&bits, &v, sizeof(bits));
	return bits;
}

static void qa_mod_parity_sample(int zone, Uint64 mods, uint fx, QaModParity *out)
{
	memset(out, 0, sizeof(*out));

	endlessActiveMods = mods;
	endlessPlayerMods[0] = endlessPlayerMods[1] = mods;
	endlessRunDepth = zone;
	difficultyLevel = DIFFICULTY_HARD;

	// Identical structural draws on every machine: the omni-gravity heading and the per-level
	// special-tier latches are seeded state, so reseed and reset before each sample.
	endlessReseed((Uint64)zone * 2);
	endlessResetZoneEffects();
	endlessResetElites();
	endlessSetFxPlayer(fx);

	endlessScalingSnapshot(zone, DIFFICULTY_HARD, mods, &out->s);
	out->gravity = qa_float_bits(endlessGravityDrift());
	out->gravityX = qa_float_bits(endlessGravityDriftX());
	out->gravityY = qa_float_bits(endlessGravityDriftY());
	out->moveScale = qa_float_bits(endlessMoveScale());
	out->scrollPct = endlessScrollBoostPercent();
	out->genAdd = (int)endlessGeneratorPowerAdd(4);
	out->hitbox = endlessHitboxScale(12);
	out->contactElite = endlessEliteContactPercent(2);
	out->contactChamp = endlessEliteContactPercent(3);
	out->shockElite = endlessShockwaveRadius(5, 2);
	out->shockChamp = endlessShockwaveRadius(6, 3);
	out->martyrElite = endlessMartyrdomBurstShots(7, 2);
	out->martyrChamp = endlessMartyrdomBurstShots(8, 3);
	out->champFire = endlessChampionFireDelayPercent();
	out->champDmg = endlessChampionShotDamagePercent();
	out->staticDrain = endlessStaticDischargeDrain(10);
	out->regenOff = endlessShieldRegenOff() ? 1 : 0;
	out->regenFree = endlessShieldRegenFree() ? 1 : 0;
	out->seeker = endlessSeekerActive() ? 1 : 0;
	out->seekTier = (Uint8)endlessSeekerTier();
	out->seekPasses = endlessSeekerPasses();
	out->scrollActive = endlessScrollBoostActive() ? 1 : 0;
}

/* Every sector modifier's derived combat parameters, identical whichever machine computes them:
 * both thisPlayerNum values, both isNetworkGame values, both host roles, and both fx players. */
void qa_test_modifier_online_parity(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedHost = network_is_host;
	const uint savedThis = thisPlayerNum;
	const bool savedEndless = endlessMode, savedCoop = coopEndlessMode, savedTwo = twoPlayerMode;
	const Uint64 savedActive = endlessActiveMods;
	Uint64 savedPlayerMods[2];
	const int savedDepth = endlessRunDepth;
	const JE_shortint savedDifficulty = difficultyLevel;
	memcpy(savedPlayerMods, endlessPlayerMods, sizeof(savedPlayerMods));

	endlessMode = true;
	unsigned rows = 0;

	for (unsigned m = 0; m < COUNTOF(endlessModTable); ++m)
	{
		Uint64 mods = endlessModTable[m].bit;
		if (mods == ENDLESS_MOD_GRAVITY_OMNI)
			mods |= ENDLESS_MOD_GRAVITY;   // omni is a heading for GRAVITY, never alone

		static const int zones[] = { 6, 64 };
		for (unsigned z = 0; z < COUNTOF(zones); ++z)
		for (uint fx = 0; fx < 2; ++fx)
		{
			QaModParity want;
			bool haveWant = false;
			bool same = true;

			for (int net = 0; net <= 1; ++net)
			for (uint machine = 1; machine <= 2; ++machine)
			{
				isNetworkGame = net != 0;
				coopEndlessMode = net != 0;
				twoPlayerMode = net != 0;
				thisPlayerNum = machine;
				network_is_host = machine == 1;

				QaModParity got;
				qa_mod_parity_sample(zones[z], mods, fx, &got);
				if (!haveWant)
				{
					want = got;
					haveWant = true;
				}
				else
				{
					same &= memcmp(&want, &got, sizeof(got)) == 0;
				}
			}

			if (!same || !haveWant)
			{
				char label[160];
				snprintf(label, sizeof(label),
				         "modifier %016llx (zone %d, ship %u) derives identically on "
				         "every machine and mode",
				         (unsigned long long)mods, zones[z], fx);
				qa_check(false, label);
			}
			++rows;
		}
	}
	qa_check(rows > 0, "modifier online-parity matrix covered the registry");
	printf("# modifier online parity: %u modifier/zone/ship rows\n", rows);

	memcpy(endlessPlayerMods, savedPlayerMods, sizeof(savedPlayerMods));
	endlessActiveMods = savedActive;
	endlessRunDepth = savedDepth;
	difficultyLevel = savedDifficulty;
	twoPlayerMode = savedTwo;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	thisPlayerNum = savedThis;
	network_is_host = savedHost;
	isNetworkGame = savedNet;
	endlessSetFxPlayer(0);
}

/* Every sidekick simulation counter must survive a rollback snapshot exactly. */
void qa_test_sidekick_rollback_state(void)
{
	const Player savedPlayer = player[0];
	const JE_boolean savedLinked = twoPlayerLinked;
	const JE_real savedDirec = linkGunDirec;

	player[0].sidekick[0].ammo = 7;
	player[0].sidekick[0].ammo_refill_ticks = 13;
	player[0].sidekick[1].charge = 4;
	player[0].sidekick[1].charge_ticks = 9;
	player[0].option_satellite_rotate = 1.25f;
	player[0].option_attachment_move[0] = -3;
	player[0].option_attachment_linked[0] = true;
	player[0].option_attachment_return[1] = true;
	twoPlayerLinked = true;
	linkGunDirec = 2.5;

	rollback_snapshot(0x51DEu);
	player[0].sidekick[0].ammo = 1;
	player[0].sidekick[0].ammo_refill_ticks = 0;
	player[0].sidekick[1].charge = 0;
	player[0].sidekick[1].charge_ticks = 0;
	player[0].option_satellite_rotate = 0.0f;
	player[0].option_attachment_move[0] = 5;
	player[0].option_attachment_linked[0] = false;
	player[0].option_attachment_return[1] = false;
	twoPlayerLinked = false;
	linkGunDirec = 0.0;

	qa_check(rollback_restore(0x51DEu)
	         && player[0].sidekick[0].ammo == 7
	         && player[0].sidekick[0].ammo_refill_ticks == 13
	         && player[0].sidekick[1].charge == 4
	         && player[0].sidekick[1].charge_ticks == 9
	         && player[0].option_satellite_rotate == 1.25f
	         && player[0].option_attachment_move[0] == -3
	         && player[0].option_attachment_linked[0]
	         && player[0].option_attachment_return[1]
	         && twoPlayerLinked
	         && linkGunDirec == 2.5,
	         "sidekick ammo, charge, satellite, attachment, and link state survive a rollback");

	player[0] = savedPlayer;
	twoPlayerLinked = savedLinked;
	linkGunDirec = savedDirec;
}

/* LAN discovery replies, which the Switch local wireless advertisement also carries. */
void qa_test_discover_reply(void)
{
	char savedName[NET_NAME_MAX + 1];
	SDL_strlcpy(savedName, network_player_name, sizeof(savedName));
	network_set_player_name("Pilot");

	Uint8 body[64];
	NetworkHostInfo info;
	memset(&info, 0, sizeof(info));
	const int len = network_discover_reply_write(body, (int)sizeof(body), 1444);
	qa_check(len > 0 && network_discover_reply_read(body, len, &info)
	         && info.port == 1444 && strcmp(info.name, "Pilot") == 0,
	         "a discovery reply carries the host's port and name");
	qa_check(network_discover_reply_write(body, len - 1, 1444) == 0,
	         "a discovery reply refuses a buffer too small for it");
	qa_check(!network_discover_reply_read(body, 5, &info),
	         "a truncated discovery reply is ignored");

	Uint8 other[64];
	memcpy(other, body, sizeof(other));
	other[3] ^= 1;
	qa_check(!network_discover_reply_read(other, len, &info),
	         "a discovery reply from another NET_VERSION is ignored");
	memcpy(other, body, sizeof(other));
	other[1] ^= 1;
	qa_check(!network_discover_reply_read(other, len, &info),
	         "a packet of another type is ignored as a discovery reply");

	// A crafted name longer than the field, with no terminator, still lands terminated.
	memcpy(other, body, 6);
	memset(&other[6], 'A', sizeof(other) - 6);
	qa_check(network_discover_reply_read(other, (int)sizeof(other), &info)
	         && strlen(info.name) == sizeof(info.name) - 1,
	         "an overlong discovery reply name is cut to fit");

	network_set_player_name(savedName);
}

void qa_test_desync_guards(void)
{
	{
		const Player savedPlayer = player[1];
		const struct JE_SingleEnemyType savedEnemy = enemy[0];
		const JE_byte savedAvail = enemyAvail[0];
		Uint32 rand0, ph0, eh0, rand1, ph1, eh1;

		enemyAvail[0] = 0;
		network_sim_state(&rand0, &ph0, &eh0);
		player[1].items.special ^= 1;
		network_sim_state(&rand1, &ph1, &eh1);
		const bool specialSeen = ph1 != ph0;
		player[1].items.special ^= 1;
		player[1].items.weapon[FRONT_WEAPON].power ^= 1;
		network_sim_state(&rand1, &ph1, &eh1);
		const bool powerSeen = ph1 != ph0;
		player[1] = savedPlayer;
		enemy[0].enemydie ^= 1;
		network_sim_state(&rand1, &ph1, &eh1);
		const bool dropSeen = eh1 != eh0;
		enemy[0].enemydie ^= 1;
		enemy[0].eliteState ^= 1;
		network_sim_state(&rand1, &ph1, &eh1);
		const bool tierSeen = eh1 != eh0;
		enemy[0] = savedEnemy;
		enemyAvail[0] = savedAvail;
		qa_check(specialSeen && powerSeen && dropSeen && tierSeen,
		         "the desync canary sees a ship's loadout and an enemy's drop and tier");
	}

	{
		const boss_bar_t savedBars[2] = { boss_bar[0], boss_bar[1] };
		JE_byte savedAvail[COUNTOF(enemyAvail)];
		memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
		memset(enemyAvail, 1, sizeof(enemyAvail));
		SDL_Surface *const savedScreen = VGAScreen;
		VGAScreen = game_screen;

		boss_bar[0].link_num = 77;
		boss_bar[0].fill = 100;
		boss_bar[0].color = 5;
		boss_bar[1].link_num = 0;
		draw_boss_bar();
		const bool drawInert = boss_bar[0].link_num == 77 && boss_bar[0].color == 5;
		boss_bar_fade_flash();
		const bool fades = boss_bar[0].color == 4;
		boss_bar_tick();
		qa_check(drawInert && fades && boss_bar[0].link_num == 0,
		         "a dead boss's bar retires in the simulated tick, never in the draw");

		VGAScreen = savedScreen;
		memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
		boss_bar[0] = savedBars[0];
		boss_bar[1] = savedBars[1];
	}

	{
		const bool savedAdvance = background_advance, savedLayer = background2;
		const JE_byte savedDelay = map2YDelay, savedDelayMax = map2YDelayMax;
		const JE_word savedPos = backPos2, savedMove = backMove2, savedY = mapY2;
		JE_byte **const savedYPos = mapY2Pos;
		const int savedExtra = endlessScrollExtraPx2;

		bool same = true;
		for (int tick = 0; tick < 12; ++tick)
		{
			background_advance = true;
			background2 = false;
			map2YDelayMax = 2;
			map2YDelay = (JE_byte)(1 + tick % 2);
			backMove2 = (JE_word)(tick % 3);
			backPos2 = (JE_word)(20 + tick);
			mapY2 = 200;
			mapY2Pos = &megaData2.mainmap[mapY2][0] - 1;
			endlessScrollExtraPx2 = tick % 2;
			draw_background_2(game_screen);
			const JE_word drawnPos = backPos2, drawnMove = backMove2, drawnY = mapY2;
			const JE_byte drawnDelay = map2YDelay;
			JE_byte **const drawnYPos = mapY2Pos;

			map2YDelay = (JE_byte)(1 + tick % 2);
			backMove2 = (JE_word)(tick % 3);
			backPos2 = (JE_word)(20 + tick);
			mapY2 = 200;
			mapY2Pos = &megaData2.mainmap[mapY2][0] - 1;
			skip_background_2();
			same = same && backPos2 == drawnPos && backMove2 == drawnMove && mapY2 == drawnY
			    && map2YDelay == drawnDelay && mapY2Pos == drawnYPos;
		}
		qa_check(same, "a hidden background layer 2 advances exactly as a drawn one");

		background_advance = savedAdvance;
		background2 = savedLayer;
		map2YDelay = savedDelay;
		map2YDelayMax = savedDelayMax;
		backPos2 = savedPos;
		backMove2 = savedMove;
		mapY2 = savedY;
		mapY2Pos = savedYPos;
		endlessScrollExtraPx2 = savedExtra;
	}

	{
		const uint savedArmor = player[0].armor;
		const JE_boolean savedEndless = endlessMode, savedMods = endlessCampaignMods;
		endlessMode = false;
		endlessCampaignMods = false;
		player[0].armor = 30;
		JE_repaintShieldArmorBars();
		const bool repaintInert = player[0].armor == 30;
		JE_drawArmor();
		qa_check(repaintInert && player[0].armor == 28,
		         "a gauge repaint leaves armor alone while the simulation's draw still caps it");
		player[0].armor = savedArmor;
		endlessMode = savedEndless;
		endlessCampaignMods = savedMods;
	}

	{
		const unsigned int slot = 3;
		const Uint8 savedId = enemySpriteSheetIds[slot];
		Sprite2_array expect = { 0 };
		JE_loadCompShapes(&expect, shapeFile[1]);

		rollback_register_all();
		enemy_sprite_sheet_load(slot, 2);
		rollback_snapshot(0x5142u);
		enemy_sprite_sheet_load(slot, 1);
		const bool restored = rollback_restore(0x5142u);
		qa_check(restored && enemySpriteSheetIds[slot] == 2 && expect.data != NULL
		         && enemySpriteSheets[slot].size == expect.size
		         && memcmp(enemySpriteSheets[slot].data, expect.data, expect.size) == 0,
		         "a rollback across an enemy bank swap reloads the bank the restored id names");

		free_sprite2s(&expect);
		enemy_sprite_sheet_load(slot, savedId);
	}

	{
		rollback_register_all();
		const size_t size = rollback_state_size();
		Uint8 *wire = malloc(size);
		const bool savedLayer = background2;
		const int savedX = player[0].x;
		if (wire != NULL)
		{
			background2 = true;
			player[0].x = 123;
			const bool exported = rollback_wire_export(wire);
			background2 = false;
			player[0].x = 45;
			const bool adopted = exported && rollback_wire_adopt(wire);
			qa_check(adopted && player[0].x == 123 && !background2,
			         "desync recovery adopts shared state and leaves local presentation alone");
			free(wire);
		}
		background2 = savedLayer;
		player[0].x = savedX;
	}

	{
		const bool savedNet = isNetworkGame;
		JE_boolean savedSmoothies[COUNTOF(smoothies)];
		memcpy(savedSmoothies, smoothies, sizeof(savedSmoothies));
		const JE_byte savedFast = fastPlay;
		const JE_shortint savedFilter = levelFilter;
		static const SDL_Scancode combo[] =
		{
			SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_F12, SDL_SCANCODE_SCROLLLOCK, SDL_SCANCODE_2,
			SDL_SCANCODE_MINUS, SDL_SCANCODE_1,
		};

		bool inert = true;
		for (int online = 1; online >= 0; --online)
		{
			isNetworkGame = online != 0;
			smoothies[0] = 0;
			for (size_t k = 0; k < COUNTOF(combo); ++k)
				keysactive[combo[k]] = true;
			const uint32_t draws = mt_rand_count;
			JE_mainKeyboardInput();
			for (size_t k = 0; k < COUNTOF(combo); ++k)
				keysactive[combo[k]] = false;
			inert = inert && mt_rand_count == draws;
			if (online)
				inert = inert && smoothies[0] == 0 && fastPlay == savedFast
				     && levelFilter == savedFilter;
			else
				inert = inert && smoothies[0] != 0;
		}
		qa_check(inert,
		         "online debug key codes leave the simulation alone; offline they still work");

		isNetworkGame = savedNet;
		memcpy(smoothies, savedSmoothies, sizeof(savedSmoothies));
		fastPlay = savedFast;
		JE_setNewGameSpeed();
		levelFilter = savedFilter;
	}

	{
		const bool savedNet = isNetworkGame;
		const JE_boolean savedMods = endlessCampaignMods, savedEndless = endlessMode;
		const Uint64 savedActive = endlessActiveMods;
		const bool savedVt = vt_ship;
		const int savedY = player[0].y;

		endlessMode = false;
		endlessCampaignMods = true;
		endlessActiveMods = ENDLESS_MOD_GRAVITY;
		endlessRollGravityDir();
		vt_ship = false;

		int moved[2] = { 0, 0 };
		for (int online = 0; online < 2; ++online)
		{
			isNetworkGame = online != 0;
			player[0].y = 100;
			for (int tick = 0; tick < 40; ++tick)
				endlessPerShipTick(&player[0]);
			moved[online] = player[0].y - 100;
		}
		qa_check(moved[0] > 0 && moved[1] == 0,
		         "online, the simulation leaves gravity to the owning machine's input");

		isNetworkGame = savedNet;
		endlessCampaignMods = savedMods;
		endlessMode = savedEndless;
		endlessActiveMods = savedActive;
		endlessRollGravityDir();
		vt_ship = savedVt;
		player[0].y = savedY;
	}
}
