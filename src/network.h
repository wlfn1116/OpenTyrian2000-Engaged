/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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
#endif
#endif

// Covers the 48-byte rollback header plus sixteen 14-byte redundant input records.
#define NET_PACKET_SIZE   320
#define NET_PACKET_QUEUE  16

// Keeps "<name> got <item>" inside the in-game text bar.
#define NET_NAME_MAX      10

#define PACKET_ACKNOWLEDGE   0x00    //
#define PACKET_KEEP_ALIVE    0x01    // send stamp (echoed back as PACKET_PING_REPLY)
#define PACKET_PING_REPLY    0x02    // the keep-alive's stamp, verbatim  (not acknowledged)

#define PACKET_CONNECT       0x10    // version, delay, episodes, player_number, name
#define PACKET_DETAILS       0x11    // episode, difficulty

#define PACKET_QUIT          0x20    // 
#define PACKET_WAITING       0x21    // 
#define PACKET_BUSY          0x22    // 

#define PACKET_GAME_QUIT     0x30    //
#define PACKET_GAME_PAUSE    0x31    //
#define PACKET_GAME_MENU     0x32    //
#define PACKET_DEBUG_SYNC    0x33    // generation, sender, <debug state block>  (see network_debug_sync_send)

#define PACKET_STATE_RESEND  0x40    // state_id
#define PACKET_STATE         0x41    // <state>  (not acknowledged)
#define PACKET_STATE_XOR     0x42    // <xor state>  (not acknowledged)

#define PACKET_DISCOVER      0x50    // <>            broadcast: "any games out there?"
#define PACKET_DISCOVER_REPLY 0x51   // version, port, name

#define PACKET_INPUT         0x60    // rollback input stream (never acknowledged; see net_rollback.c)
#define PACKET_RESYNC        0x61    // gen, chunk idx/count, len, <state chunk>  (acknowledged; see nrb_resync_*)

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
extern JE_boolean pauseRequest, skipLevelRequest, helpRequest, nortShipRequest;
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

void network_state_prepare(void);
int network_state_send(void);
bool network_state_update(void);
bool network_state_is_reset(void);
void network_state_reset(void);

int network_connect(void);
void network_tyrian_halt(unsigned int err, bool attempt_sync);

int network_init(void);

// Close the socket and queues, leaving the module ready for another network_init().
void network_shutdown(void);

/* Automated two-process reliable-channel exercise used by the fault proxy. */
int network_test_peer(int rounds);

// Pack, adopt, and restore host-authoritative simulation settings. Presentation settings remain
// local. The return value is the encoded byte count.
int  network_settings_pack(Uint8 *buf);
int  network_settings_adopt(const Uint8 *buf);
void network_settings_check_layout(const Uint8 *buf);
void network_settings_apply_session_speed(void);
void network_settings_restore(void);
// Bytes 0..15 are settings; 16..23 identify the rollback layout and snapshot size.
#define NETWORK_SETTINGS_SIZE 24

// Synchronize debug-menu simulation state. mark snapshots the baseline, changed compares it,
// send publishes updates, and pump adopts queued updates. in_level enables HUD repainting.
void network_debug_sync_mark(void);
bool network_debug_sync_changed(void);
void network_debug_sync_send(void);
bool network_debug_sync_pump(bool in_level);
// Capacity of the wire block; varz.c asserts that its expert-tunable table fits.
#define NETWORK_DEBUG_EXPERT_SLOTS 8

// Independent simulation hashes used to identify the first divergent subsystem.
void network_sim_state(Uint32 *rand_draws, Uint32 *player_hash, Uint32 *enemy_hash);

// Raw fields behind network_sim_state(), captured for line-by-line desync reports.
typedef struct
{
	Sint32 x, y, armor, shield, alive, cash;
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

// When false, report at most one mismatch per level and continue play.
extern bool networkDesyncHalt;

// State packet extension; bytes 4..27 belong to the original state fields.
#define NET_STATE_RAND   28  // Uint32: mt_rand draws since the level's fixed reseed
#define NET_STATE_PHASH  32  // Uint32: player state
#define NET_STATE_EHASH  36  // Uint32: live enemy state
#define NET_STATE_SIZE   40

void JE_clearSpecialRequests(void);

/* Re-simulation must not process keep-alives or inbound packets mid-pass. */
extern bool rollback_resim;

#define NETWORK_KEEP_ALIVE() \
		if (isNetworkGame && !rollback_resim) \
			network_check();
#else
#define NETWORK_KEEP_ALIVE()
#define network_ping_ms() (-1)
#endif

#endif /* NETWORK_H */
