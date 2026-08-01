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

// 320 (was 256): the rollback input packet carries a 48-byte header plus up to
// 16 x 14-byte redundant input records = 272 bytes.  In the header rather than
// network.c so bulk senders (the resync stream) can size and throttle to it.
#define NET_PACKET_SIZE   320
#define NET_PACKET_QUEUE  16

// Longest player name, enforced at network_set_player_name (so every entry path -- lobby,
// config file, command line -- gets the same clamp) and on receive.  10 keeps the in-game
// "<name> got <item>" line inside the text bar even against the longest item names.
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

// The port the lobby offers when hosting.  Kept apart from network_player_port, which is the
// port actually bound and which a joiner deliberately sets to 0 (any free port) -- persisting
// that would wipe out the host port the player chose.
extern Uint16 network_listen_port;

// The player slot the lobby claims when hosting: 1, or 2 to fly the Dragonwing.  A preference,
// unlike networkHostPlayerNum, which is what the live session settled on.
extern int network_host_player;

// Set by the in-game lobby before network_connect(). The host listens, picks which player it
// flies, and dictates every simulation-affecting setting for the session (see
// network_settings_*). The joiner takes the other slot. Command-line netplay (params.c) still
// sets these directly.
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
// This machine's own addresses, network byte order, for showing a host what to read out and
// for aiming directed broadcasts. Wraps SDLNet_GetLocalAddresses and falls back to the
// console's own "what is my IP" service where SDL_net cannot enumerate interfaces. Zero is a
// normal answer (network down, or an interface list nobody can produce).
int network_local_addresses(IPaddress *out, int max);

// Broadcast a probe on every local interface and collect replies for `timeout_ms`.  Uses its
// own short-lived socket, so it must NOT be called while a game socket is open.  Returns how
// many distinct hosts were found (at most `max`).  Zero is a normal answer: broadcast may be
// blocked by a firewall, or nobody is hosting.  `poll` (may be NULL) is called every few
// milliseconds so the caller's screen and cursor stay alive through the blocking wait.
int network_discover(NetworkHostInfo *out, int max, Uint32 timeout_ms, void (*poll)(void));
#endif

#ifdef WITH_NETWORK
extern UDPpacket *packet_out_temp;
extern UDPpacket *packet_in[], *packet_out[],
                 *packet_state_in[], *packet_state_out[];
#endif

extern uint thisPlayerNum;

// The slot the host is flying (1 or 2).  Everything the host decides for both machines --
// episode, difficulty, a debug-menu edit made by both at once -- keys off this rather than
// spelling the host as "player 1", which it no longer has to be.  Command-line netplay has no
// host and leaves it at 1, so whoever is player 1 decides there, exactly as before.
extern uint networkHostPlayerNum;

// Append the live netcode diagnostics to a crash report; crashlog_state.c calls this for
// the game-state dump.  Safe from a fault handler: reads only statics, no SDL_net calls.
void network_write_diagnostics(FILE *f);

extern JE_boolean haltGame;
extern JE_boolean moveOk;
extern JE_boolean pauseRequest, skipLevelRequest, helpRequest, nortShipRequest;
extern JE_boolean yourInGameMenuRequest, inGameMenuRequest;

#ifdef WITH_NETWORK
#include <setjmp.h>

// Landing pad for a mid-game network teardown: once armed (opentyr.c's main
// loop, just before the title screen), network_tyrian_halt cleans the session
// up and longjmps back there instead of exiting the process.
extern jmp_buf network_bailout_env;
extern bool network_bailout_armed;

void network_prepare(Uint16 type);
bool network_send(int len);

// Send packet_out_temp without entering the acknowledgement queue.  Rollback
// input packets are redundant by construction and must never be retransmitted
// by the reliability layer.
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

// Outbound acknowledged packets still awaiting their ACK.  The resync stream
// throttles on this: network_send halts the game on a full queue, so a bulk
// sender has to know how much room is left before each send.
int network_ack_backlog(void);

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

// Debug Mode across the wire.  The debug menu rewrites simulation state directly -- either
// player's loadout, cash, armor and shield, the cheat flags, difficulty, the expert tunables --
// so an edit made on one machine would leave the two sims playing different games.  The machine
// that edited publishes the whole block; the peer adopts it verbatim.
//   ..._mark     take the live state as the baseline (call when the debug menu opens)
//   ..._changed  true if the live state differs from the last published/adopted block
//   ..._send     publish the live state (bumps the generation; no-op if nothing changed)
//   ..._pump     adopt a block waiting at the head of the inbound queue, if any
// `in_level` tells the adopt path whether a gameplay HUD is on screen to repaint.
void network_debug_sync_mark(void);
bool network_debug_sync_changed(void);
void network_debug_sync_send(void);
bool network_debug_sync_pump(bool in_level);
// Expert tunables the block has room for.  Exposed so varz.c, which owns the table, can assert
// it still fits -- the send/adopt loops stop at this many and would drop a later one in silence.
#define NETWORK_DEBUG_EXPERT_SLOTS 8

// Summary of the parts of the simulation that must match between the two machines, split into
// three independent pieces so a mismatch says WHICH part diverged.  That distinction matters:
// a differing RNG draw count means the two sims really are running different games, whereas
// matching draws with differing state points at the check itself being wrong.
void network_sim_state(Uint32 *rand_draws, Uint32 *player_hash, Uint32 *enemy_hash);

// Session-long desync memo for the crash log: call once per desynced level (the lockstep
// once-per-level report and the rollback canary's first report both do).
void network_diag_note_desync(int level);

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
#define network_ping_ms() (-1)
#endif

#endif /* NETWORK_H */
