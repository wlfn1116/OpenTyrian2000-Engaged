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
#ifdef WITH_NETWORK
#include "SDL_net.h"
#endif

#define PACKET_ACKNOWLEDGE   0x00    // 
#define PACKET_KEEP_ALIVE    0x01    // 

#define PACKET_CONNECT       0x10    // version, delay, episodes, player_number, name
#define PACKET_DETAILS       0x11    // episode, difficulty

#define PACKET_QUIT          0x20    // 
#define PACKET_WAITING       0x21    // 
#define PACKET_BUSY          0x22    // 

#define PACKET_GAME_QUIT     0x30    // 
#define PACKET_GAME_PAUSE    0x31    // 
#define PACKET_GAME_MENU     0x32    // 

#define PACKET_STATE_RESEND  0x40    // state_id
#define PACKET_STATE         0x41    // <state>  (not acknowledged)
#define PACKET_STATE_XOR     0x42    // <xor state>  (not acknowledged)

#define PACKET_DISCOVER      0x50    // <>            broadcast: "any games out there?"
#define PACKET_DISCOVER_REPLY 0x51   // version, port, name

#define PACKET_INPUT         0x60    // rollback input stream (never acknowledged; see net_rollback.c)

extern bool isNetworkGame;
extern int network_delay;

extern char *network_opponent_host;
extern Uint16 network_player_port, network_opponent_port;
extern char *network_player_name, *network_opponent_name;

// The port the lobby offers when hosting.  Kept apart from network_player_port, which is the
// port actually bound and which a joiner deliberately sets to 0 (any free port) -- persisting
// that would wipe out the host port the player chose.
extern Uint16 network_listen_port;

// Set by the in-game lobby before network_connect(). The host listens, is always player 1,
// and dictates every simulation-affecting setting for the session (see network_settings_*).
// The joiner is player 2. Command-line netplay (params.c) still sets these directly.
extern bool network_is_host;

// True once the lobby has taken over setup, so the startup path in opentyr.c knows not to
// treat isNetworkGame as "connect immediately from argv".
extern bool network_from_lobby;

// Replace the player name, handling the fact that it starts as a static empty string and is
// heap-owned thereafter.  Passing NULL or "" puts it back to the static empty string.
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
// Broadcast a probe on every local interface and collect replies for `timeout_ms`.  Uses its
// own short-lived socket, so it must NOT be called while a game socket is open.  Returns how
// many distinct hosts were found (at most `max`).  Zero is a normal answer: broadcast may be
// blocked by a firewall, or nobody is hosting.
int network_discover(NetworkHostInfo *out, int max, Uint32 timeout_ms);
#endif

#ifdef WITH_NETWORK
extern UDPpacket *packet_out_temp;
extern UDPpacket *packet_in[], *packet_out[],
                 *packet_state_in[], *packet_state_out[];
#endif

extern uint thisPlayerNum;
extern JE_boolean haltGame;
extern JE_boolean moveOk;
extern JE_boolean pauseRequest, skipLevelRequest, helpRequest, nortShipRequest;
extern JE_boolean yourInGameMenuRequest, inGameMenuRequest;

#ifdef WITH_NETWORK
void network_prepare(Uint16 type);
bool network_send(int len);

// Send packet_out_temp without entering the acknowledgement queue.  Rollback
// input packets are redundant by construction and must never be retransmitted
// by the reliability layer.
bool network_send_unacked(int len);

int network_check(void);
bool network_update(void);

bool network_is_sync(void);

void network_state_prepare(void);
int network_state_send(void);
bool network_state_update(void);
bool network_state_is_reset(void);
void network_state_reset(void);

int network_connect(void);
void network_tyrian_halt(unsigned int err, bool attempt_sync);

int network_init(void);

// Close the socket and free every queue, leaving the module ready for another network_init().
// The lobby needs this: a refused or cancelled connection has to unwind back to the menu
// instead of taking the process down with it, which is all the original code could do.
void network_shutdown(void);

// Simulation-affecting settings (weapon tweaks, spark trails, episode weapon data, game
// speed, xmas data set) are host-authoritative: the joiner adopts the host's values for the
// duration of the session and gets its own back on disconnect. Settings that only change
// what a machine draws or hears stay local.
//   ..._pack   writes the local settings into a connect packet at `offset`
//   ..._adopt  applies a received block, stashing the local values first
//   ..._restore puts the stashed local values back (no-op if never adopted)
// Returns the number of bytes written/read so the caller can place the player name after it.
int  network_settings_pack(Uint8 *buf);
int  network_settings_adopt(const Uint8 *buf);
void network_settings_restore(void);
#define NETWORK_SETTINGS_SIZE 16

// Summary of the parts of the simulation that must match between the two machines, split into
// three independent pieces so a mismatch says WHICH part diverged.  That distinction matters:
// a differing RNG draw count means the two sims really are running different games, whereas
// matching draws with differing state points at the check itself being wrong.
void network_sim_state(Uint32 *rand_draws, Uint32 *player_hash, Uint32 *enemy_hash);

// While false, a mismatch is reported once per level and play continues.  The check is new and
// unproven; halting a working game on a false positive would be worse than the divergence it is
// meant to catch, so it earns the right to stop the game only once it has been shown correct.
extern bool networkDesyncHalt;

// State packet layout.  Bytes 4..27 were already full (deltas, buttons, requests, difficulty,
// both ships' positions, curLoc), so this extends the packet rather than reusing a field.
#define NET_STATE_RAND   28  // Uint32: mt_rand draws since the level's fixed reseed
#define NET_STATE_PHASH  32  // Uint32: player state
#define NET_STATE_EHASH  36  // Uint32: live enemy state
#define NET_STATE_SIZE   40

void JE_clearSpecialRequests(void);

/* No keep-alives during a rollback re-simulation: the replay must not process
 * inbound packets mid-pass (rollback_resim is declared in rollback.h; declared
 * here loosely to keep this widely-included header light). */
extern bool rollback_resim;

#define NETWORK_KEEP_ALIVE() \
		if (isNetworkGame && !rollback_resim) \
			network_check();
#else
#define NETWORK_KEEP_ALIVE()
#endif

#endif /* NETWORK_H */
