/* Two real peers on a wire: the Online Campaign and Online Endless co-op protocols.
 *
 * qa_online.c and qa_endless.c pin the rules inside one process. These run the same systems
 * across two processes through the hostile proxy in testing/network_fault_test.py, where the
 * failures are different in kind: a field that never made it into a packet, a state each
 * machine derives differently, a rendezvous that only completes when the two arrive in one
 * particular order.
 *
 * Both peers run this same code and each asserts what it should be seeing of the other, so a
 * field that crosses in only one direction fails on the side that did not receive it. */
#include "qa.h"

#include "config.h"
#include "crashlog.h"
#include "endless.h"
#include "endless_internal.h"
#include "episodes.h"
#include "game_menu.h"
#include "mainint.h"
#include "network.h"
#include "player.h"
#include "varz.h"

#include <stdio.h>
#include <string.h>

#ifdef WITH_NETWORK

#define QA_NET_TIMEOUT 12000   // matches the reliable channel's own patience

// This machine's ship and the peer's, as the session numbers them.
#define QA_ME   (thisPlayerNum - 1u)
#define QA_THEM (2u - thisPlayerNum)

static Uint32 qa_net_peer_phase;   // highest phase the peer has announced reaching
static const char *qa_net_here = "(none)";   // the phase this machine last entered

/* Everything needed to tell a starved wait from a wedged one, printed at the failure rather than
 * left for a rerun: an empty queue means the packet never came, and a non-empty one means it is
 * stuck behind a head nobody in this loop consumes. The peer's phase says which of the two
 * machines stopped first. A stall that only reports "peers did not finish" costs an hour. */
static void qa_net_fail(const char *what)
{
	fprintf(stderr,
	        "network test: %s (player %u)\n"
	        "  at phase: %s | peer reached phase %u\n"
	        "  reliable queue: depth %d, head type %04x | outbound %s (backlog %d)\n"
	        "  acked-and-dropped %u | peer %s, ping %dms\n",
	        what, thisPlayerNum,
	        qa_net_here, (unsigned)qa_net_peer_phase,
	        network_inbound_depth(), (unsigned)network_inbound_head(),
	        network_is_sync() ? "acknowledged" : "UNACKNOWLEDGED", network_ack_backlog(),
	        (unsigned)network_window_overflow(),
	        network_peer_alive() ? "alive" : "SILENT", network_ping_ms());
	fflush(stderr);
}

/* Announce each phase as it is entered. A wire test that stalls says nothing about where, and
 * the runner only reports that the peers never finished; this is what names the phase. */
static void qa_net_phase(const char *name)
{
	qa_net_here = name;
	fprintf(stderr, "net phase: player %u entering %s\n", thisPlayerNum, name);
	fflush(stderr);
}

/* Phase announcements.
 *
 * Phases are numbered and the peer's number only ever climbs, so a barrier completes on "they
 * have reached at least here" rather than on catching one particular announcement. That is what
 * makes it safe against a lost copy: an exact match strands whoever missed it, because the peer
 * that got through moves on and starts announcing the phase after, and nothing says the old one
 * again. It is the same reason the purchase waits below are >= rather than ==.
 *
 * Tagged into PACKET_WAITING under a prefix of its own so these cannot be read as the base
 * scenario's round payloads. */
#define QA_PHASE_MARK 0x50480000u
#define QA_PHASE_MASK 0xFFFF0000u

// True if the queue head is a phase announcement; records it and consumes it.
static bool qa_net_take_phase(void)
{
	if (packet_in[0] == NULL || packet_in[0]->len < 8
	    || SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_WAITING)
	{
		return false;
	}

	const Uint32 payload = SDLNet_Read32(&packet_in[0]->data[4]);
	if ((payload & QA_PHASE_MASK) != QA_PHASE_MARK)
		return false;

	const Uint32 phase = payload & ~QA_PHASE_MASK;
	if (phase > qa_net_peer_phase)
		qa_net_peer_phase = phase;
	network_update();
	return true;
}

/* Keep the reliable queue moving. Only one kind of packet is consumed by any given wait, and a
 * head nobody consumes stalls every phase after it: the shop pump returns false without
 * advancing, so a single stray arrival is enough to wedge the session. Bounded, so a busy peer
 * cannot hold us in here. */
static void qa_net_drain(void)
{
	for (int guard = 0; guard < 32 && packet_in[0] != NULL; ++guard)
	{
		if (network_shop_pump())
			continue;
		// Debug blocks travel on the same reliable queue as everything else, so a wait that does
		// not consume them wedges every phase behind one.
		if (network_debug_sync_pump(false))
			continue;

		if (qa_net_take_phase())
			continue;
		// PACKET_CONNECT: the handshake's trailing connect, placed in the window to keep it
		// gap-free (see network.c); always stale once the session is running.
		if (SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_WAITING ||
		    SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CONNECT)
			network_update();   // some other rendezvous packet; keep the queue moving
		else
			break;
	}
}

/* Service the socket the way an outpost frame does, until `ready` or the patience runs out. */
#define QA_NET_WAIT(ready, what)                                                     \
	do {                                                                             \
		const Uint32 qa_started_ = SDL_GetTicks();                                   \
		while (!(ready) && SDL_GetTicks() - qa_started_ < QA_NET_TIMEOUT              \
		       && !network_test_expired())                                            \
		{                                                                            \
			watchdog_heartbeat();                                                    \
			network_check();                                                         \
			network_shop_keepalive();                                                \
			qa_net_phase_keepalive();                                                 \
			qa_net_drain();                                                           \
			SDL_Delay(1);                                                            \
		}                                                                            \
		if (!(ready))                                                                \
		{                                                                            \
			qa_net_fail(what);                                                       \
			return 1;                                                                \
		}                                                                            \
	} while (0)

/* A phase barrier: neither machine goes on until both have reached this point.
 *
 * Barriers matter here because every shop packet carries the sender's whole current state
 * rather than a delta. The moment one machine moves on and mutates, an exact value the other is
 * still waiting to observe may never be sent again.
 *
 * The outpost's own done/lock rendezvous is not the thing to build this out of: it is designed
 * for one visit per level, so driving it repeatedly leaves the previous phase's ready and lock
 * flags standing, and the next barrier reads them as an arrival that has not happened. */
static Uint32 qa_net_my_phase;      // highest phase this machine has announced
static Uint32 qa_net_announced_at;  // when it last said so

static void qa_net_announce(Uint32 phase)
{
	if (phase > qa_net_my_phase)
		qa_net_my_phase = phase;
	qa_net_announced_at = SDL_GetTicks();

	network_prepare(PACKET_WAITING);
	SDLNet_Write32(QA_PHASE_MARK | phase, &packet_out_temp->data[4]);
	network_send(8);
}

/* Announcements are state, not events: a product wait that legitimately consumes transient
 * rendezvous traffic (the save checkpoint does) can eat one unrecorded, and the announcer has
 * moved on and never says it again. Every wait re-broadcasts the highest phase reached, rate
 * limited and never more than one in flight, the way the outpost keepalive re-announces. */
static void qa_net_phase_keepalive(void)
{
	if (qa_net_my_phase == 0 || SDL_GetTicks() - qa_net_announced_at < 250 || !network_is_sync())
		return;
	qa_net_announce(qa_net_my_phase);
}

static int qa_net_sync(Uint32 phase, const char *what)
{
	qa_net_phase(what);
	qa_net_announce(phase);

	const Uint32 started = SDL_GetTicks();
	while (qa_net_peer_phase < phase && SDL_GetTicks() - started < QA_NET_TIMEOUT
	       && !network_test_expired())
	{
		watchdog_heartbeat();
		network_check();
		qa_net_drain();
		qa_net_phase_keepalive();
		network_shop_keepalive();
		SDL_Delay(1);
	}

	if (qa_net_peer_phase < phase)
	{
		qa_net_fail(what);
		return 1;
	}
	return 0;
}

#define QA_NET_SYNC(phase, what)                     \
	do {                                             \
		const int qa_rc_ = qa_net_sync(phase, what); \
		if (qa_rc_ != 0)                             \
			return qa_rc_;                           \
	} while (0)

/* Drain until the link is quiet: everything acknowledged, nothing at the head, and a beat of
 * silence. A blocking chunked transfer consumes only its own packets, so a straggling or
 * proxy-duplicated announcement still in flight when one starts wedges it for good. Bounded,
 * like every wait here. */
static void qa_net_settle(void)
{
	const Uint32 started = SDL_GetTicks();
	Uint32 quiet = SDL_GetTicks();

	while (SDL_GetTicks() - started < 5000 && !network_test_expired())
	{
		watchdog_heartbeat();
		if (network_check() > 0 || packet_in[0] != NULL || !network_is_sync())
			quiet = SDL_GetTicks();
		qa_net_drain();
		if (SDL_GetTicks() - quiet > 600)
			break;
		SDL_Delay(1);
	}
}

/* ---- online campaign ---------------------------------------------------------------- */

// A loadout no other slot would produce, so a field arriving from the wrong ship is visible.
static void qa_net_campaign_loadout(Player *p, uint slot)
{
	memset(&p->items, 0, sizeof(p->items));
	p->items.ship       = (Uint8)(1 + slot);
	p->items.generator  = (Uint8)(2 + slot);
	p->items.shield     = (Uint8)(4 + slot * 2);
	p->items.weapon[FRONT_WEAPON].id    = (Uint8)(5 + slot);
	p->items.weapon[FRONT_WEAPON].power = (Uint8)(3 + slot);
	p->items.weapon[REAR_WEAPON].id     = (Uint8)(14 + slot);
	p->items.weapon[REAR_WEAPON].power  = (Uint8)(6 + slot);
	p->items.sidekick[0] = (Uint8)(7 + slot);
	p->items.sidekick[1] = (Uint8)(9 + slot);
	p->items.special     = (Uint8)(11 + slot);
	p->cash        = 60000u + slot * 1234u;
	p->weapon_mode = (uint)(1 + slot);
}

static bool qa_net_campaign_matches(const Player *p, uint slot)
{
	Player want;
	memset(&want, 0, sizeof(want));
	qa_net_campaign_loadout(&want, slot);
	return memcmp(&p->items, &want.items, sizeof(want.items)) == 0
	    && p->cash == want.cash
	    && p->weapon_mode == want.weapon_mode;
}

int qa_net_campaign_phases(void)
{
	twoPlayerMode = true;
	coopEndlessMode = false;
	coopCampaignMode = true;

	Player *const local = &player[QA_ME];
	Player *const peer  = &player[QA_THEM];

	/* Arcade lives live in a weapon-power slot, and everything that re-derives a hull ceiling
	 * reads through this pointer. A real session sets it up while starting the game; a peer that
	 * starts straight into a wire scenario never runs that, and the first refresh of either ship
	 * faults on a null. */
	for (uint p = 0; p < COUNTOF(player); ++p)
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;

	/* Two complete, different loadouts. Every field of the peer's has to arrive, not just the
	 * cash the older baseline checked: a ship kitted out on one machine and half-empty on the
	 * other is two different simulations. */
	qa_net_phase("campaign loadout publish");
	qa_net_campaign_loadout(local, QA_ME);
	memset(&peer->items, 0, sizeof(peer->items));
	peer->cash = 0;
	network_shop_begin();
	network_shop_send_state(false);

	QA_NET_WAIT(qa_net_campaign_matches(peer, QA_THEM),
	            "the peer's full campaign loadout did not arrive intact");

	/* ...and receiving it must not have disturbed our own. */
	if (!qa_net_campaign_matches(local, QA_ME))
	{
		qa_net_fail("adopting the peer's loadout overwrote this machine's own");
		return 1;
	}

	/* Neither machine may start spending until both have read the other's opening loadout. */
	QA_NET_SYNC(1, "campaign loadout");

	/* Interleaved purchases from both machines at once, which is what two players shopping at
	 * the same outpost actually do.
	 *
	 * Each packet carries the sender's whole current state rather than a delta, so an exact
	 * intermediate total is not something a peer is guaranteed to ever observe: drop the one
	 * packet that carried it and the next already holds a later total. Spending only ever
	 * raises the total, so each round waits for "at least this much" and the exact figures are
	 * asserted after the barrier below. */
	const int rounds = 6;
	for (int round = 1; round <= rounds; ++round)
	{
		local->cash += 100u + (Uint32)round;
		local->items.weapon[FRONT_WEAPON].power = (Uint8)(3 + QA_ME + round);
		local->items.sidekick[round % 2] = (Uint8)(20 + round + QA_ME);
		network_shop_send_transaction();

		const Uint32 wantCash = 60000u + QA_THEM * 1234u
		                      + (Uint32)(round * 100 + (round * (round + 1)) / 2);
		QA_NET_WAIT(peer->cash >= wantCash,
		            "a campaign purchase did not reach the peer");
	}

	/* Both machines have now spent everything they mean to. Barrier, so what each reads of the
	 * other is its finished state and not a snapshot from part-way through. */
	QA_NET_SYNC(2, "campaign purchases");

	const Uint32 finalCash = 60000u + QA_THEM * 1234u
	                       + (Uint32)(rounds * 100 + (rounds * (rounds + 1)) / 2);
	if (peer->cash != finalCash
	    || peer->items.weapon[FRONT_WEAPON].power != (Uint8)(3 + QA_THEM + rounds)
	    || peer->items.sidekick[0] != (Uint8)(20 + 6 + QA_THEM)
	    || peer->items.sidekick[1] != (Uint8)(20 + 5 + QA_THEM))
	{
		qa_net_fail("the peer's finished loadout did not match what it spent");
		return 1;
	}

	/* A save checkpoint has to leave both views exactly as they were: the online save carries
	 * two full loadouts and is written from whichever machine asked. Compare against what the
	 * two ships are holding now, which is what the purchases above left them with. */
	const Uint32 peerCashBefore = peer->cash;
	const Uint32 localCashBefore = local->cash;
	PlayerItems peerItemsBefore = peer->items;
	PlayerItems localItemsBefore = local->items;

	qa_net_phase("campaign save checkpoint");
	network_shop_sync_for_save();

	if (peer->cash != peerCashBefore || local->cash != localCashBefore
	    || memcmp(&peer->items, &peerItemsBefore, sizeof(peerItemsBefore)) != 0
	    || memcmp(&local->items, &localItemsBefore, sizeof(localItemsBefore)) != 0)
	{
		qa_net_fail("the campaign save checkpoint moved a loadout");
		return 1;
	}

	/* ---- the debug menu, across the wire ---- */

	/* One machine edits and both must end up holding the same values, in either direction. The
	 * block's contents are pinned in qa_online.c; this drives the delivery itself, on the same
	 * reliable queue as the outpost traffic. The adopt also rewrites both ships from the
	 * sender's view, which has to be a no-op here: both machines converged above. */
	const bool hosting = (thisPlayerNum == networkHostPlayerNum);
	const PlayerItems debugItemsBefore = local->items;

	qa_net_phase("campaign debug host edit");
	network_debug_sync_mark();
	if (hosting)
	{
		difficultyLevel = DIFFICULTY_HARD;
		noclipMode = 1;
		cheatNoEnemyFire = true;
		network_debug_sync_send();
	}
	QA_NET_WAIT(difficultyLevel == DIFFICULTY_HARD && noclipMode == 1 && cheatNoEnemyFire,
	            "the host's debug edit did not reach both machines");
	QA_NET_SYNC(3, "campaign debug host edit done");

	qa_net_phase("campaign debug joiner edit");
	if (!hosting)
	{
		difficultyLevel = DIFFICULTY_EASY;
		noclipMode = 0;
		cheatNoEnemyFire = false;
		chargeSidekickAutofire = 1;
		network_debug_sync_send();
	}
	QA_NET_WAIT(difficultyLevel == DIFFICULTY_EASY && noclipMode == 0 && !cheatNoEnemyFire
	            && chargeSidekickAutofire == 1,
	            "the joiner's debug edit did not reach both machines");
	QA_NET_SYNC(4, "campaign debug joiner edit done");

	if (memcmp(&local->items, &debugItemsBefore, sizeof(debugItemsBefore)) != 0)
	{
		qa_net_fail("adopting a debug block moved this machine's own loadout");
		return 1;
	}

	qa_net_phase("campaign slow rendezvous");
	/* One machine finishes outfitting long before the other. The early one must sit at the
	 * rendezvous rather than dragging the slow one out of its outpost. The host also leaves
	 * for a level of its own choosing, which the joiner must adopt: both players can point at
	 * different planets, and the host's pick is the one the session flies. */
	const JE_byte savedMainLevel = mainLevel;
	const JE_boolean savedJump = jumpSection;
	if (thisPlayerNum == networkHostPlayerNum)
	{
		const Uint32 slow = SDL_GetTicks();
		while (SDL_GetTicks() - slow < 900)
		{
			watchdog_heartbeat();
			network_check();
			network_shop_keepalive();
			while (network_shop_pump())
				;
			SDL_Delay(1);
		}
		mainLevel = 7;
		jumpSection = true;
	}
	else
	{
		mainLevel = 3;   // the joiner's own pick, which must lose
		jumpSection = true;
	}
	network_shop_send_state(true);
	QA_NET_WAIT(network_shop_peer_done(),
	            "the campaign outpost rendezvous did not complete");

	if (thisPlayerNum != networkHostPlayerNum)
	{
		network_shop_adopt_host_level();
		if (mainLevel != 7)
		{
			qa_net_fail("the joiner did not adopt the host's level pick");
			return 1;
		}
	}
	mainLevel = savedMainLevel;
	jumpSection = savedJump;

	network_shop_end();
	coopCampaignMode = false;
	return 0;
}

/* ---- online endless ----------------------------------------------------------------- */

/* One ship's whole Endless holding, distinct per slot: a different drive, a different paid
 * charge, its own hull tier, tokens, debts and perks. This is what has to cross. */
static void qa_net_endless_holding(uint slot)
{
	static const unsigned drive[2] = {
		(unsigned)ENDLESS_MOD_TURBODRIVE, (unsigned)ENDLESS_MOD_OVERBLAST };
	static const int kind[2] = { ENDLESS_BUFF_KIND_TURBODRIVE, ENDLESS_BUFF_KIND_OVERBLAST };

	if (slot >= COUNTOF(drive))
		return;

	endlessPurchasedMods[slot]      = drive[slot];
	endlessBuffKind[slot]           = kind[slot];
	endlessBuffCharge[slot]         = (int)(6 + slot * 5);
	endlessBuffCooldownUntil[slot]  = (int)(9 + slot);
	endlessArmorBonus[slot]         = (int)(16 + slot * 8);
	endlessRevivesUsed[slot]        = (int)(1 + slot);
	endlessCleanseChargeCount[slot] = (int)(slot % (ENDLESS_CLEANSE_MAX_CHARGES + 1));
	endlessShopTax[slot]            = (int)(25 * (slot + 1));
	endlessLongCon[slot]            = (int)(2 + slot);
	endlessRerollCost[slot]         = (long)(3000 + slot * 1500);
	endlessHullCost[slot]           = (int)(2500 + slot * 700);
	endlessShopEntryCash[slot]      = (long)(180000 + slot * 45000);
	endlessReviveHeld[slot]         = (slot == 0);
	endlessGambleRigged[slot]       = (slot == 1);
	player[slot].superbombs         = (uint)(3 + slot);

	/* Each ship picks its own perks; the run flies the combined holding. */
	memset(endlessPerkTakenBy[slot], 0, sizeof(endlessPerkTakenBy[slot]));
	endlessPerkGrant(slot, (slot == 0) ? PERK_CASH : PERK_DAMAGE, 1);
	endlessPerkGrant(slot, PERK_ARMOR, (int)(1 + slot));
}

static bool qa_net_endless_holding_matches(uint slot)
{
	static const unsigned drive[2] = {
		(unsigned)ENDLESS_MOD_TURBODRIVE, (unsigned)ENDLESS_MOD_OVERBLAST };
	static const int kind[2] = { ENDLESS_BUFF_KIND_TURBODRIVE, ENDLESS_BUFF_KIND_OVERBLAST };

	if (slot >= COUNTOF(drive))
		return false;

	return endlessPurchasedMods[slot]      == drive[slot]
	    && endlessBuffKind[slot]           == kind[slot]
	    && endlessBuffCharge[slot]         == (int)(6 + slot * 5)
	    && endlessBuffCooldownUntil[slot]  == (int)(9 + slot)
	    && endlessArmorBonus[slot]         == (int)(16 + slot * 8)
	    && endlessRevivesUsed[slot]        == (int)(1 + slot)
	    && endlessCleanseChargeCount[slot] == (int)(slot % (ENDLESS_CLEANSE_MAX_CHARGES + 1))
	    && endlessShopTax[slot]            == (int)(25 * (slot + 1))
	    && endlessLongCon[slot]            == (int)(2 + slot)
	    && endlessRerollCost[slot]         == (long)(3000 + slot * 1500)
	    && endlessHullCost[slot]           == (int)(2500 + slot * 700)
	    && endlessShopEntryCash[slot]      == (long)(180000 + slot * 45000)
	    && endlessReviveHeld[slot]         == (slot == 0)
	    && endlessGambleRigged[slot]       == (slot == 1)
	    && player[slot].superbombs         == (uint)(3 + slot)
	    && endlessPerkTakenBy[slot][PERK_ARMOR] == (JE_byte)(1 + slot)
	    && endlessPerkTakenBy[slot][(slot == 0) ? PERK_CASH : PERK_DAMAGE] == 1;
}

int qa_net_endless_phases(void)
{
	twoPlayerMode = true;
	coopCampaignMode = false;
	coopEndlessMode = true;
	endlessMode = true;
	endlessRunMode = ENDLESS_RUNMODE_RELAXED;
	endlessRunDepth = 12;
	endlessSetSeed("qa-wire-endless");

	/* Both ships flying and undamaged before anything below reads a death state, and the arcade
	 * lives pointer set the way starting a game would: the drain adopts debug blocks, and the
	 * refresh inside that adopt reads both hull ceilings through it. */
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;
		player[p].is_alive = true;
		player[p].exploding_ticks = 0;
		player[p].initial_armor = 20;
		player[p].armor = 20;
		player[p].shield = 10;
		endlessPlayerDowned[p] = false;
	}

	/* ---- both ships holding different things, over the wire ---- */

	/* Fill our own slot and blank the peer's, so anything we later read there arrived. */
	qa_net_endless_holding(QA_ME);
	endlessPurchasedMods[QA_THEM] = 0;
	endlessBuffKind[QA_THEM] = ENDLESS_BUFF_KIND_NONE;
	endlessBuffCharge[QA_THEM] = 0;
	endlessArmorBonus[QA_THEM] = 0;
	endlessReviveHeld[QA_THEM] = false;
	endlessGambleRigged[QA_THEM] = false;
	player[QA_THEM].superbombs = 0;
	memset(endlessPerkTakenBy[QA_THEM], 0, sizeof(endlessPerkTakenBy[QA_THEM]));
	endlessPerkRederive();

	network_shop_begin();
	network_shop_send_state(false);
	QA_NET_WAIT(qa_net_endless_holding_matches(QA_THEM),
	            "the partner's Endless holding did not arrive intact");

	if (!qa_net_endless_holding_matches(QA_ME))
	{
		qa_net_fail("adopting the partner's Endless holding overwrote this machine's own");
		return 1;
	}

	/* Nothing below may change published state until both machines have read the holding. */
	QA_NET_SYNC(1, "Endless holding");

	/* Each ship flies its own drive and not the other's, on both machines alike. */
	endlessActiveMods = 0;
	endlessApplyPurchasedMods();
	if ((endlessPlayerMods[0] & (Uint64)ENDLESS_MOD_KILLFIRE_ANY)
	        != (Uint64)ENDLESS_MOD_TURBODRIVE
	    || (endlessPlayerMods[1] & (Uint64)ENDLESS_MOD_KILLFIRE_ANY)
	        != (Uint64)ENDLESS_MOD_OVERBLAST)
	{
		qa_net_fail("the two ships' drives did not stay their own after the exchange");
		return 1;
	}

	/* Perks are personal, picked from two private slates, so each row has to arrive intact
	 * and stay its owner's alone on both machines. */
	endlessPerkRederive();
	if (endlessPerkEffective(0, PERK_CASH) != 1 || endlessPerkEffective(0, PERK_ARMOR) != 1
	    || endlessPerkEffective(1, PERK_DAMAGE) != 1 || endlessPerkEffective(1, PERK_ARMOR) != 2
	    || endlessPerkEffective(0, PERK_DAMAGE) != 0 || endlessPerkEffective(1, PERK_CASH) != 0)
	{
		qa_net_fail("the two ships' perk rows did not stay their own on both machines");
		return 1;
	}

	/* ---- the session settings both machines have to agree on ---- */

	/* Individual credit with Double Earnings on: each ship banks double what it collects and
	 * nothing of what the other does. Both machines simulate both ships, so the cash each
	 * derives has to match, which is what the exchange below actually proves. */
	coop_set_session_shared_credit(false);
	coop_set_session_double_earnings(true);
	if (!coop_earnings_are_doubled())
	{
		qa_net_fail("Double Earnings did not take under Individual credit");
		return 1;
	}

	player[0].cash = player[1].cash = 0;
	endlessCashResync();
	player_award_pickup_cash(&player[QA_ME], 500);
	if (player[QA_ME].cash != 1000u || player[QA_THEM].cash != 0u)
	{
		qa_net_fail("a doubled pickup did not pay its collector alone");
		return 1;
	}
	player_award_kill_cash(&player[QA_ME], 100);
	player_award_bounty_cash(&player[QA_ME], 50);
	if (player[QA_ME].cash != 1300u)
	{
		qa_net_fail("Double Earnings did not cover kill and bounty cash");
		return 1;
	}
	network_shop_send_transaction();
	QA_NET_WAIT(player[QA_THEM].cash == 1300u,
	            "the partner's doubled combat earnings did not reach this machine");
	QA_NET_SYNC(2, "Endless combat credit");

	/* ---- one ship goes down, the survivor carries the zone ---- */

	endlessPlayerDowned[QA_ME] = (thisPlayerNum == networkHostPlayerNum);
	endlessPlayerDowned[QA_THEM] = false;
	network_shop_send_transaction();

	if (thisPlayerNum != networkHostPlayerNum)
	{
		QA_NET_WAIT(endlessPlayerDowned[QA_THEM],
		            "the partner going down was never seen on this machine");
		if (!endlessAnyPlayerFlying())
		{
			qa_net_fail("a downed partner ended the zone while this ship was still flying");
			return 1;
		}
	}

	/* Both machines have now seen the downed state before anybody clears it. */
	QA_NET_SYNC(3, "Endless downed state");

	/* Reaching the outpost brings the downed ship back on both machines: full hull, no shield,
	 * and everything it owned still its own. */
	endlessReviveDownedAtOutpost();
	if (endlessPlayerDowned[0] || endlessPlayerDowned[1])
	{
		qa_net_fail("the outpost left a ship on the downed list");
		return 1;
	}
	if (endlessPurchasedMods[QA_ME] == 0 || endlessArmorBonus[QA_ME] == 0)
	{
		qa_net_fail("the revive took the ship's drive or hull tier with it");
		return 1;
	}

	/* ---- charting the next sector ---- */

	/* A fresh outpost visit on both machines, so the rendezvous below is the one being tested
	 * and not the remains of the visit the phases above were published through. */
	QA_NET_SYNC(4, "Endless outpost reopen");
	network_shop_end();
	network_shop_begin();

	/* One machine picks and the other has to leave the rendezvous already holding that index.
	 * The waiter commits first and the charting player sends uncommitted packets before it
	 * picks, which is the order that broke this once. */
	const bool charting = (thisPlayerNum == networkHostPlayerNum);
	const int wireCourse = 2;
	endlessCoopCourse = -1;
	jumpSection = true;

	if (!charting)
		network_shop_send_state(true);
	else
	{
		for (int i = 0; i < 3; ++i)
		{
			network_shop_send_transaction();
			const Uint32 tick = SDL_GetTicks();
			while (SDL_GetTicks() - tick < 60)
			{
				watchdog_heartbeat();
				network_check();
				while (network_shop_pump())
					;
				SDL_Delay(1);
			}
		}
		endlessCoopCourse = wireCourse;
		network_shop_send_state(true);
	}

	QA_NET_WAIT(network_shop_peer_done(), "the Endless outpost rendezvous did not complete");

	network_shop_set_locked(true);
	QA_NET_WAIT(network_shop_peer_locked(), "the Endless outpost lock did not complete");

	if (!charting && network_shop_peer_course() != wireCourse)
	{
		qa_net_fail("the charted sector index did not survive the rendezvous");
		return 1;
	}

	network_shop_end();
	endlessCoopCourse = -1;
	jumpSection = false;

	/* ---- both ships down: the Relaxed prompt, all three answers ---- */

	/* The host reads the death menu for as long as it likes and the joiner waits on the
	 * answer, which travels on the Endless co-op channel. Each of the menu's three choices
	 * is its own exchange, the way three separate deaths would be, and each has to arrive
	 * as itself: the joiner's whole next act (relaunch, outpost, run summary) hangs on it. */
	static const int deathChoice[] = {
		(int)ENDLESS_DEATH_RESTART, (int)ENDLESS_DEATH_OUTPOST, (int)ENDLESS_DEATH_END_RUN };
	static char deathPhase[32];
	for (uint c = 0; c < COUNTOF(deathChoice); ++c)
	{
		endlessPlayerDowned[0] = endlessPlayerDowned[1] = true;
		if (endlessAnyPlayerFlying())
		{
			qa_net_fail("both ships down still reported somebody flying");
			return 1;
		}

		if (charting)
		{
			/* Read the menu for a while; the joiner's wait announces itself and sits. */
			const Uint32 reading = SDL_GetTicks();
			while (SDL_GetTicks() - reading < 500)
			{
				watchdog_heartbeat();
				network_check();
				while (network_shop_pump())
					;
				SDL_Delay(16);
			}
			network_endless_death_sync(deathChoice[c]);
		}
		else
		{
			const int adopted = network_endless_death_sync(-1);
			if (adopted != deathChoice[c])
			{
				qa_net_fail("a death-menu choice did not reach the joiner as itself");
				return 1;
			}
		}
		endlessPlayerDowned[0] = endlessPlayerDowned[1] = false;

		/* Barrier, so the next exchange cannot read this one's leftovers. */
		snprintf(deathPhase, sizeof(deathPhase), "Endless death choice %u", c);
		QA_NET_SYNC(5 + c, deathPhase);
	}

	/* ---- the whole run record, host to joiner ---- */

	/* The resume path: the host's run is the run and the joiner adopts it wholesale.
	 *
	 * Last, deliberately. The transfer is a blocking chunked push, so for as long as it runs
	 * the host is reading for its own acknowledgements and nothing else; a phase tag sent at
	 * it in that window is gone. Nothing follows it here, and the settle below clears any
	 * straggling announcement before either machine commits to it. */
	qa_net_phase("Endless run transfer");
	qa_net_settle();
	if (thisPlayerNum == networkHostPlayerNum)
	{
		endlessRunKills = 4321;
		endlessRunDepth = 27;
		network_endless_run_publish();
	}
	else
	{
		/* Wipe first, so an adopt that quietly does nothing cannot pass. */
		endlessRunKills = 0;
		endlessRunDepth = 0;
		endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;
		endlessArmorBonus[0] = endlessArmorBonus[1] = 0;
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		endlessPerkRederive();

		if (!network_endless_run_receive(QA_NET_TIMEOUT))
		{
			qa_net_fail("the host's Endless run record never arrived");
			return 1;
		}
		if (endlessRunKills != 4321 || endlessRunDepth != 27)
		{
			qa_net_fail("the adopted run record lost the run's own progress");
			return 1;
		}
		if (!qa_net_endless_holding_matches(0) || !qa_net_endless_holding_matches(1))
		{
			qa_net_fail("the adopted run record lost a ship's holding");
			return 1;
		}
		if (endlessPerkEffective(0, PERK_ARMOR) != 1 || endlessPerkEffective(1, PERK_ARMOR) != 2)
		{
			qa_net_fail("the adopted run record lost a ship's perk row");
			return 1;
		}
	}

	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
	coopEndlessMode = false;
	endlessMode = false;
	return 0;
}

/* ---- barrier storm ------------------------------------------------------------------- */

/* Nothing but barriers, back to back. Any scenario deep enough to need many rendezvous
 * inherits whatever the barrier mechanism does under loss, so it is pinned here on its own,
 * away from any mode's protocol. See doc/notes.md on wire-scenario barriers. */
int qa_net_barrier_phases(void)
{
	static char name[24];

	for (Uint32 phase = 1; phase <= 40; ++phase)
	{
		snprintf(name, sizeof(name), "barrier %u", (unsigned)phase);
		QA_NET_SYNC(phase, name);
	}
	return 0;
}

#endif /* WITH_NETWORK */
