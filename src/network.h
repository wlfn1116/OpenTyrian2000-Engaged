/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#ifndef NETWORK_H
#define NETWORK_H

#include "opentyr.h"

#include "SDL.h"
#include <stdio.h>
#ifdef WITH_NETWORK
// VitaSDK has no SDL2_net package, so that build substitutes its own SceNet-backed
// implementation of the subset used here. Every other platform links the real library.
#ifdef __vita__
#include "vita_net.h"
#else
#include "SDL_net.h"

/* Byte-wise big-endian access for unaligned packed fields. */
static inline void net_bytes_write16(Uint16 value, void *areap)
{
	Uint8 *area = (Uint8 *)areap;
	area[0] = (Uint8)(value >> 8);
	area[1] = (Uint8)value;
}

static inline void net_bytes_write32(Uint32 value, void *areap)
{
	Uint8 *area = (Uint8 *)areap;
	area[0] = (Uint8)(value >> 24);
	area[1] = (Uint8)(value >> 16);
	area[2] = (Uint8)(value >> 8);
	area[3] = (Uint8)value;
}

static inline Uint16 net_bytes_read16(const void *areap)
{
	const Uint8 *area = (const Uint8 *)areap;
	return (Uint16)(((Uint16)area[0] << 8) | area[1]);
}

static inline Uint32 net_bytes_read32(const void *areap)
{
	const Uint8 *area = (const Uint8 *)areap;
	return ((Uint32)area[0] << 24) | ((Uint32)area[1] << 16) | ((Uint32)area[2] << 8) | area[3];
}

#undef SDLNet_Write16
#undef SDLNet_Write32
#undef SDLNet_Read16
#undef SDLNet_Read32
#define SDLNet_Write16 net_bytes_write16
#define SDLNet_Write32 net_bytes_write32
#define SDLNet_Read16  net_bytes_read16
#define SDLNet_Read32  net_bytes_read32
#endif
#endif

// The 64-bit pair, big-endian and byte-wise like the ones above; wallets travel with these.
static inline void net_bytes_write64(Uint64 value, void *areap)
{
	Uint8 *area = (Uint8 *)areap;
	for (int i = 0; i < 8; ++i)
		area[i] = (Uint8)(value >> (56 - 8 * i));
}

static inline Uint64 net_bytes_read64(const void *areap)
{
	const Uint8 *area = (const Uint8 *)areap;
	Uint64 value = 0;
	for (int i = 0; i < 8; ++i)
		value = (value << 8) | area[i];
	return value;
}

// Covers the 48-byte rollback header plus sixteen 14-byte redundant input records.
#define NET_PACKET_SIZE   320
#define NET_PACKET_QUEUE  16

// Keeps "<name> got <item>" inside the in-game text bar.
#define NET_NAME_MAX      10

#define PACKET_ACKNOWLEDGE   0x00    //
#define PACKET_KEEP_ALIVE    0x01    // send stamp (echoed back as PACKET_PING_REPLY)
#define PACKET_PING_REPLY    0x02    // the keep-alive's stamp, verbatim  (not acknowledged)

#define PACKET_CONNECT       0x10    // version, delay, episodes, player, game type/settings, name
#define PACKET_DETAILS       0x11    // game type, episode, difficulty, optional save record

#define PACKET_QUIT          0x20    // 
#define PACKET_WAITING       0x21    // 
#define PACKET_BUSY          0x22    // 
#define PACKET_LEVEL_READY   0x23    // level-start barrier
#define PACKET_DEPART_GATE   0x24    // at the departure gate (1) or withdrawn back to the menu (0)

#define PACKET_GAME_QUIT     0x30    //
#define PACKET_GAME_PAUSE    0x31    //
#define PACKET_GAME_MENU     0x32    //
#define PACKET_DEBUG_SYNC    0x33    // generation, sender, <debug state block>  (see network_debug_sync_send)
#define PACKET_SHOP_SYNC     0x34    // sender, sequence, flags, route, cash, mode, ack, items
#define PACKET_CUSTOM_WEAPON 0x35    // owner, generation, chunk idx/count, len, <design chunk>
#define PACKET_ENDLESS_RUN   0x36    // sender, generation, chunk idx/count, len, <run-record chunk>
#define PACKET_SA_SHIP       0x37    // sender, chosen Super Arcade ship (1..SA)
#define PACKET_ENDLESS_JUMP  0x38    // sender, armed, level pick, len, <Endless debug block>
#define PACKET_PLAYER_LOOK   0x39    // sender, dye, view; repeated and unacknowledged
#define PACKET_EXTRA_SHIPS   0x3A    // owner, generation, chunk idx/count, len, <ship file chunk>
#define PACKET_CUSTOM_LEVEL  0x3B    // Chunked *.clv transfer; see doc/notes.md.

#define PACKET_STATE_RESEND  0x40    // state_id
#define PACKET_STATE         0x41    // <state>  (not acknowledged)
#define PACKET_STATE_XOR     0x42    // <xor state>  (not acknowledged)

#define PACKET_DISCOVER      0x50    // <>            broadcast: "any games out there?"
#define PACKET_DISCOVER_REPLY 0x51   // version, port, name

// LAN save transfer uses its own socket and does not require a game session.
#define PACKET_SAVE_OFFER    0x52    // version       broadcast: "anyone sharing a save?"
#define PACKET_SAVE_REPLY    0x53    // version, port, slot summary
#define PACKET_SAVE_PULL     0x54    // version       "send it"
#define PACKET_SAVE_CHUNK    0x55    // version, generation, chunk idx/count, len, <payload chunk>
#define PACKET_SAVE_ACK      0x56    // version, generation   whole payload consumed

// Bulk transfers use a separate packet family; their versions distinguish each menu choice.
#define PACKET_CUSTOM_OFFER  0x57
#define PACKET_CUSTOM_REPLY  0x58
#define PACKET_CUSTOM_PULL   0x59
#define PACKET_CUSTOM_CHUNK  0x5A
#define PACKET_CUSTOM_ACK    0x5B

#define PACKET_INPUT         0x60    // rollback input stream (never acknowledged; see net_rollback.c)
#define PACKET_RESYNC        0x61    // gen, chunk idx/count, len, <state chunk>  (acknowledged; see nrb_resync_*)
#define PACKET_DESTRUCT_INPUT 0x62   // Destruct rollback input stream (never acknowledged; see destruct_rollback.c)
#define PACKET_DESTRUCT_RESYNC 0x63  // gen, chunk idx/count, len, <battle chunk>  (acknowledged; see drb_resync_*)

extern bool isNetworkGame;
extern int network_delay;

extern char *network_opponent_host;
extern Uint16 network_player_port, network_opponent_port;
extern char *network_player_name, *network_opponent_name;

// Persisted host port. Joiners bind network_player_port to any free port instead.
extern Uint16 network_listen_port;

// Persisted host slot preference: 1, or 2 for the Dragonwing.
extern int network_host_player;
extern int network_host_game_speed;

typedef enum
{
	NETWORK_GAME_ARCADE = 0,
	NETWORK_GAME_CAMPAIGN = 1,
	NETWORK_GAME_ENDLESS = 2,
	NETWORK_GAME_SUPERTYRIAN = 3,
	NETWORK_GAME_SUPERARCADE = 4,
	NETWORK_GAME_DESTRUCT = 5,
	NETWORK_GAME_TYPE_COUNT
}
NetworkGameType;

/* The two one-player rulesets flown online. Both give each player a complete ship of their own,
 * so they run in the Separate arcade shape (config.h) rather than as the linked pair. */
static inline bool network_game_type_is_super(NetworkGameType t)
{
	return t == NETWORK_GAME_SUPERTYRIAN || t == NETWORK_GAME_SUPERARCADE;
}

extern NetworkGameType network_game_type;
extern int network_host_episode;
extern int network_host_difficulty;

/* Custom container identity advertised in PACKET_CONNECT; empty means stock. */
extern char network_host_custom_file[64];   /* CUSTOM_EPISODE_FILE_LEN */
extern Uint32 network_host_custom_size;
extern Uint32 network_host_custom_hash;
/* Host-authored Custom Endless mode from PACKET_CONNECT. */
extern int network_host_custom_endless;

/* Session-start custom-container transfer and activation. */
void network_custom_level_session_reset(void);
bool network_custom_level_serve(void);
bool network_custom_level_fetch(void);
/* Synchronizes the host's Custom Endless collection and order. */
bool network_custom_endless_serve(void);
bool network_custom_endless_fetch(void);
/* Reconciles save dependencies both ways. A negative mode skips session setup. */
bool network_custom_required_serve(char names[][64], int count, int sessionMode);
bool network_custom_required_fetch(int sessionMode);
bool networkCustomEpisodeActivate(void);

/* Arcade's third shape, beside the Linked pair and Separate ships: both players race one of the
 * three Timed Battle levels for cash. */
#define NET_TIMED_BATTLE_LEVELS 3
extern bool network_host_timed_battle;
extern int network_host_battle_level;   // 1..NET_TIMED_BATTLE_LEVELS

static inline bool network_timed_battle(void)
{
	return network_game_type == NETWORK_GAME_ARCADE && network_host_timed_battle;
}

// The episode a battle level lives in; JE_initEpisode needs it before the level script runs.
static inline int network_timed_battle_episode(int level)
{
	return level <= 1 ? 1 : 5;
}

// Plain ints keep this header independent of the Endless enums they carry.
#define NET_ENDLESS_SEED_MAX 24
extern char network_host_endless_seed[NET_ENDLESS_SEED_MAX];
/* The resolved host seed used by both peers. */
extern char network_endless_session_seed[NET_ENDLESS_SEED_MAX];
void network_endless_session_begin(void);
extern int  network_host_endless_run_mode;
extern int  network_host_endless_chooser;
extern bool network_host_endless_combo_shared;
// Base Level: which EndlessBaseRule the run's charts pick their levels by.
extern int  network_host_endless_base_rule;

// Adopt the host's Endless block from the connect packet, clamping every field.
void network_endless_adopt(const Uint8 *buf);

/* The Destruct lobby block: which of the five data-backed battle modes the session plays (the
 * per-machine Custom mode never goes online), and the terrain seed every round derives from. */
extern int network_host_destruct_mode;
extern Uint32 network_destruct_session_seed;
void network_destruct_session_begin(void);

// Lobby connection role. The host also supplies the session settings and player slots.
extern bool network_is_host;

// True after gameplay writes a coherent LAST LEVEL backup. Cleared by network_shutdown.
extern bool network_session_saveable;

// True once the lobby has taken over setup, so the startup path in opentyr.c knows not to
// treat isNetworkGame as "connect immediately from argv".
extern bool network_from_lobby;

// Replace the heap-owned player name; NULL or "" restores the static empty string.
void network_set_player_name(const char *name);

// A host found by LAN discovery.
typedef struct
{
	char   name[24];     // whatever the host set as its player name ("" if unset)
	char   address[48];  // dotted quad, ready to hand back to network_opponent_host
	Uint16 port;
}
NetworkHostInfo;

#ifdef WITH_NETWORK
// Local addresses in network byte order. Returns zero if none are available.
int network_local_addresses(IPaddress *out, int max);

// Exposed for tests on platforms without an interface list.
bool network_interface_carries_lan(unsigned int flags);

// Discover up to max LAN hosts with a short-lived socket. Do not call while a game socket is
// open. poll may be NULL and keeps the UI responsive during the blocking wait.
int network_discover(NetworkHostInfo *out, int max, Uint32 timeout_ms, void (*poll)(void));
#endif

#ifdef WITH_NETWORK
extern UDPpacket *packet_out_temp;
extern UDPpacket *packet_in[], *packet_out[],
                 *packet_state_in[], *packet_state_out[];
#endif

extern uint thisPlayerNum;

// Live host slot. Host-authoritative decisions key off this instead of assuming player 1.
extern uint networkHostPlayerNum;

// Append static netcode diagnostics to a crash report without calling SDL_net.
void network_write_diagnostics(FILE *f);

extern JE_boolean haltGame;
extern JE_boolean moveOk;
extern JE_boolean skipLevelRequest, helpRequest, nortShipRequest;
extern JE_boolean yourInGameMenuRequest, inGameMenuRequest;

#ifdef WITH_NETWORK
#include <setjmp.h>

// Recovery point for tearing down a failed session without exiting the process.
extern jmp_buf network_bailout_env;
extern bool network_bailout_armed;

void network_prepare(Uint16 type);
bool network_send(int len);

// Send redundant rollback input without entering the acknowledgement queue.
bool network_send_unacked(int len);

// Any packet (including keep-alives) received recently?  Distinguishes a slow
// peer (in menus, loading) from a dead connection.
bool network_peer_alive(void);

// Smoothed round-trip time to the peer in ticks, or -1 while unknown.  Sampled off the
// keep-alive, so it keeps updating on menu screens where no gameplay traffic flows.
int network_ping_ms(void);

int network_check(void);
bool network_update(void);

bool network_is_sync(void);

// Unacknowledged outbound packets, used to throttle bulk resync transfers.
int network_ack_backlog(void);

// Reliable packets still queued for consumption, and the type at the head (0 when empty).
// Diagnostic: they say whether a stalled wait is starved or wedged behind a head nobody claims.
int network_inbound_depth(void);
Uint16 network_inbound_head(void);

// Reliable packets acknowledged into a full receive window and therefore lost for good. Nonzero
// means something stopped draining the queue; the transport cannot recover these.
Uint32 network_window_overflow(void);

void network_state_prepare(void);
int network_state_send(void);
bool network_state_update(void);
bool network_state_is_reset(void);
void network_state_reset(void);

int network_connect(void);
OT_NORETURN void network_tyrian_halt(unsigned int err, bool attempt_sync);
#define NET_HALT_CUSTOM_SYNC 8
const char *network_halt_message(unsigned int err);

int network_init(void);

// Close the socket and queues, leaving the module ready for another network_init().
void network_shutdown(void);
// Final process teardown, including persistent player/peer strings.
void network_deinit(void);

/* Automated two-process reliable-channel exercise used by the fault proxy. */
int network_test_peer(int rounds, int scenario);

// True once a test peer has outlived its wall-clock ceiling. Every wire-scenario wait checks it,
// so a wedged run reports where it stopped instead of being killed by the harness.
bool network_test_expired(void);

// Working-set probe shared by the base scenario and the gameplay verdicts (zero off Windows).
void network_test_mem_mark(void);
unsigned long network_test_mem_start_kb(void);
unsigned long network_test_mem_now_kb(void);

// Arm every session flag from this machine's own config, the same set the settings block
// carries. The host runs on these; the joiner's adoption then overwrites them.
void network_arm_local_session(void);

// Pack, adopt, and restore host-authoritative simulation settings. Presentation settings remain
// local. The return value is the encoded byte count.
int  network_settings_pack(Uint8 *buf);
int  network_settings_adopt(const Uint8 *buf);
void network_settings_check_layout(const Uint8 *buf);
void network_settings_apply_session_speed(void);
void network_settings_restore(void);
/* Bytes 0..15 are the original settings; 16..23 identify the rollback layout and snapshot size;
 * 24 onward is the extensible tail (see network.c for field offsets). */
#define NETWORK_SETTINGS_SIZE 48

/* Transfer a resumed Endless run over the reliable channel. */
void network_endless_run_publish(void);
bool network_endless_run_receive(Uint32 timeout_ms);

/* Both ships down at once: the host publishes its death-menu choice and the joiner adopts it.
 * Pass the choice on the host, -1 on the joiner; -1 comes back if nothing arrived. */
int network_endless_death_sync(int hostChoice);

// Wire-test diagnostic: this machine's own rendezvous announcement and the sequence guard.
void network_shop_debug_state(int *localDone, int *localLock, int *mySeq, int *peerSeq);

/* Publish this player's retractable Super Arcade pick. The acknowledgement bit closes retraction
 * only after both peers hold the same final pair. */
void network_sa_ship_publish(int ship, bool seen_peer);

// Send the local dye and view now; the keep-alive beat repeats them without acknowledgement.
void network_player_look_publish(void);
int  network_sa_ship_peer(void);         // the peer's pick, 0 if they have none right now
bool network_sa_ship_peer_saw_us(void);  // the peer's latest word says they hold our pick
void network_sa_ship_reset(void);

/* Publish an Endless debug jump immediately and poll before folding the course. A true poll skips
 * the fold; the host wins simultaneous jumps. */
void network_endless_jump_publish(void);
bool network_endless_jump_poll(void);

/* Both machines announce they are ready for a level, then resynchronize the state queues. Only
 * needed on a path that starts a level without passing through the outpost. */
void network_level_rendezvous(void);
/* Every network level calls this after loading. Its dedicated marker cannot be confused with an
 * earlier announcement; receiving the peer's marker is sufficient to enter the level. */
void network_level_loaded_rendezvous(void);

/* Non-blocking, retractable ready barrier for Destruct and Timed Battle cards. Poll returns -1 for
 * no update, 0 for withdrawal, and 1 for ready; release also waits for network_is_sync(). */
void network_ready_publish(bool ready);
int network_ready_peer(void);

/* Departure gate for modes without a shared outpost. See doc/notes.md#session-and-outpost. */
void network_depart_gate_publish(bool at_gate);
int network_depart_gate_peer(void);

/* The two gate waits, as pure decision functions so the unit suite can drive every ordering.
 * `peer_gate` is a network_depart_gate_peer() result and `head` the inbound queue's head type. */
typedef enum
{
	DEPART_GATE_WAIT,      // nobody has moved; keep waiting
	DEPART_GATE_GO,        // both machines are at the gate; commit
	DEPART_GATE_WITHDRAW,  // this player pressed Esc; reopen the menu
}
DepartGateStep;
DepartGateStep network_depart_gate_step(bool esc_pressed, int peer_gate, Uint16 head);

typedef enum
{
	DEPART_WAIT_MORE,      // no answer yet
	DEPART_WAIT_DONE,      // the peer committed too; both leave
	DEPART_WAIT_REOPENED,  // the peer withdrew; fall back to the gate
}
DepartWaitStep;
DepartWaitStep network_depart_wait_step(int peer_gate, Uint16 head);

/* Terminal cards are not retractable. A caller that has already accepted local input passes true;
 * otherwise either local input or the peer's announcement dismisses both copies. */
void network_end_screen_rendezvous(bool local_dismissed);

void network_shop_begin(void);
void network_shop_send_state(bool done);
void network_shop_send_transaction(void);
bool network_shop_pump(void);
bool network_shop_peer_done(void);
// True while the reliable queue holds a packet the level-start handshake is the one to read;
// an outpost wait must leave it alone rather than advance the queue past it.
bool network_shop_departure_pending(void);
// Re-announce our rendezvous state; call from any loop that waits on the peer at the outpost.
void network_shop_keepalive(void);
// Endless: the sector index the peer committed to, or -1 while it has committed to none.
int  network_shop_peer_course(void);
// Course slates never grow past this; the receiver rejects anything outside it.
#define ENDLESS_MAX_COURSE_SLOTS 5
// Second step of the outpost rendezvous. Lock once the peer is done too, then wait for its lock;
// a peer that withdrew instead clears network_shop_peer_done and the wait starts over.
void network_shop_set_locked(bool locked);
bool network_shop_peer_locked(void);

// Publish the local custom weapon. Both machines need both designs for deterministic simulation.
void network_custom_weapon_publish(void);
/* A Campaign resume can enter gameplay without an outpost. Publish even when the editor feature is
 * locally disabled, because the loaded record may already have either custom slot equipped. */
void network_custom_weapon_publish_resume(void);
void network_custom_weapon_reset(void);

// Publish this machine's extra-ship file through the reliable session channel.
void network_extra_ships_publish(void);
void network_extra_ships_reset(void);
// Settle custom-content exchange before a mode without an outpost starts gameplay.
void network_custom_content_rendezvous(void);
// Take the level the host left the outpost for. Call once both players are done, never before:
// the joiner has to be allowed to finish shopping first.
void network_shop_adopt_host_level(void);
void network_shop_end(void);
/* Retire the peer's quit notice a level left at the reliable head. network_shop_begin calls it,
 * as the outpost is where a co-op quit lands; public so the suite can drive it. */
bool network_quit_notice_retire(void);
/* Checkpoint both loadouts before a save. The acknowledgement returns the peer's own outpost
 * half, which the save stores next to this machine's. */
void network_shop_sync_for_save(void);

// Synchronize debug-menu simulation state. mark snapshots the baseline, changed compares it,
// send publishes updates, and pump adopts queued updates. in_level enables HUD repainting.
void network_debug_sync_mark(void);
bool network_debug_sync_changed(void);
void network_debug_sync_send(void);
/* pump only inspects the head, because the reliable queue is ordered and nothing can be lifted
 * out of the middle of it. */
bool network_debug_sync_pump(bool in_level);
/* Pack and adopt the complete debug state; adoption applies the normal clamps. */
int  network_debug_state_size(void);
void network_debug_state_pack(Uint8 *buf);
void network_debug_state_adopt(const Uint8 *buf, bool in_level);

/* Expert-tunable capacity, shared by the two blocks that carry them: this one and the settings
 * block. varz.c asserts that its table fits, so a seventh tunable needs no wire change. */
#define NETWORK_EXPERT_SLOTS 8

// Independent simulation hashes used to identify the first divergent subsystem.
void network_sim_state(Uint32 *rand_draws, Uint32 *player_hash, Uint32 *enemy_hash);

// Raw fields behind network_sim_state(), captured for line-by-line desync reports.
typedef struct
{
	Sint32 x, y, armor, shield, alive;
	Sint64 cash;
}
NetSimPlayerRow;

typedef struct
{
	Uint8  idx, avail;
	Uint16 type;
	Sint32 ex, ey, armorleft;
}
NetSimEnemyRow;

#define NET_SIM_DETAIL_ENEMIES 100  // == COUNTOF(enemy); asserted at the capture site

typedef struct
{
	NetSimPlayerRow p[2];
	Uint16          enemy_count;
	NetSimEnemyRow  e[NET_SIM_DETAIL_ENEMIES];
}
NetSimDetail;

void network_sim_detail(NetSimDetail *out);

// Hash object pools omitted by network_sim_state(). detail may be NULL; when provided it records
// per-pool values and a bounded sample of player shots for desync reports.
#define NET_SIM_DETAIL_SHOTS 24

typedef struct
{
	Uint16 idx;
	Uint8  avail, dmg, playernum, pierce;
	Sint32 x, y, xm, ym, xc, yc;
}
NetSimShotRow;

typedef struct
{
	Uint32 explosions, rep_explosions, enemy_shots, player_shots, sound;
	Uint16 n_expl, n_rep, n_eshot, n_pshot;
	NetSimShotRow pshot[NET_SIM_DETAIL_SHOTS];  // first n_pshot (capped) live slots
}
NetSimPools;

Uint32 network_sim_pools(NetSimPools *detail);

// Session-long desync memo for the crash log: call once per desynced level (the lockstep
// once-per-level report and the rollback canary's first report both do).
void network_diag_note_desync(int level);
Uint32 network_desync_count(void);

// When false, report at most one mismatch per level and continue play.
extern bool networkDesyncHalt;

// State packet extension; bytes 4..27 belong to the original state fields.
#define NET_STATE_LINK_FLAGS 28  // Uint16: bit 0 = linked Dragonwing analog turret aim
#define NET_STATE_LINK_ANGLE 30  // Uint16: 0..65535 = 0..2pi
#define NET_STATE_RAND       32  // Uint32: mt_rand draws since the level's fixed reseed
#define NET_STATE_PHASH      36  // Uint32: player state
#define NET_STATE_EHASH      40  // Uint32: live enemy state
#define NET_STATE_SIZE       44

void JE_clearSpecialRequests(void);

/* Re-simulation must not process keep-alives or inbound packets mid-pass. */
extern bool rollback_resim;

#define NETWORK_KEEP_ALIVE() \
		if (isNetworkGame && !rollback_resim) \
			network_check();
#else
#define NETWORK_KEEP_ALIVE()
#define network_ping_ms() (-1)
static inline void network_level_rendezvous(void) { }
static inline void network_ready_publish(bool ready) { (void)ready; }
static inline int network_ready_peer(void) { return -1; }
static inline void network_depart_gate_publish(bool at_gate) { (void)at_gate; }
static inline int network_depart_gate_peer(void) { return -1; }
static inline void network_end_screen_rendezvous(bool local_dismissed) { (void)local_dismissed; }
static inline void network_sa_ship_publish(int ship, bool seen_peer) { (void)ship; (void)seen_peer; }
static inline void network_player_look_publish(void) { }
static inline int network_sa_ship_peer(void) { return 0; }
static inline bool network_sa_ship_peer_saw_us(void) { return false; }
static inline void network_sa_ship_reset(void) { }
static inline void network_shop_begin(void) { }
static inline void network_shop_send_state(bool done) { (void)done; }
static inline void network_shop_send_transaction(void) { }
static inline bool network_shop_pump(void) { return false; }
static inline bool network_shop_peer_done(void) { return true; }
static inline bool network_shop_departure_pending(void) { return false; }
static inline void network_shop_keepalive(void) { }
static inline int network_shop_peer_course(void) { return -1; }
static inline void network_shop_set_locked(bool locked) { (void)locked; }
static inline bool network_shop_peer_locked(void) { return true; }
static inline void network_endless_run_publish(void) { }
static inline bool network_endless_run_receive(Uint32 timeout_ms) { (void)timeout_ms; return false; }
static inline int network_endless_death_sync(int hostChoice) { return hostChoice; }
static inline void network_custom_weapon_publish(void) { }
static inline void network_custom_weapon_publish_resume(void) { }
static inline void network_custom_weapon_reset(void) { }
static inline void network_extra_ships_publish(void) { }
static inline void network_extra_ships_reset(void) { }
static inline void network_custom_content_rendezvous(void) { }
static inline void network_shop_adopt_host_level(void) { }
static inline void network_shop_end(void) { }
static inline bool network_quit_notice_retire(void) { return false; }
static inline void network_shop_sync_for_save(void) { }
#endif

#endif /* NETWORK_H */
