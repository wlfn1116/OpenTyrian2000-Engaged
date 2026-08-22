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

// Ahead of network.h, which pulls in SDL_net.h: SDL_net only defines INADDR_* when the platform
// headers have not, so winsock2.h has to win the race or every one of them warns C4005.
#if defined(WITH_NETWORK) && defined(_WIN32)
#include <winsock2.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
// Working-set probe for the wire tests' soak check (net_test_rss_kb).
#include <psapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "psapi.lib")
#endif
#endif

#include "network.h"

#include "config.h"
#include "console_platform.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "destruct_rollback.h"
#include "endless.h"
#include "game_menu.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "loudness.h"
#include "mainint.h"
#include "menus.h"
#include "mouse.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "net_style.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyr.h"
#include "params.h"
#include "picload.h"
#include "player.h"
#include "qa.h"
#include "rollback.h"
#include "shots.h"
#include "sprite.h"
#include "touch_ui.h"
#include "tyrian2.h"
#include "varz.h"
#include "video.h"

#include <assert.h>
#include <stdlib.h>

/* UDP session transport, handshake, discovery, and deterministic state exchange. */

#define NET_VERSION       83           /* See doc/notes.md#wire-compatibility. */
#define NET_PORT          1333         // UDP

// PACKET_CONNECT layout past the 4-byte header: version, delay, episode mask, player number,
// game type, episode, difficulty, the host's simulation settings, its Endless lobby block, its
// Destruct lobby block, and finally the player name.
#define NET_CONNECT_GAME_TYPE 12
#define NET_CONNECT_EPISODE   14
#define NET_CONNECT_DIFFICULTY 16
#define NET_CONNECT_SETTINGS  18
#define NET_CONNECT_ENDLESS   (NET_CONNECT_SETTINGS + NETWORK_SETTINGS_SIZE)
// run mode, chooser, combo feed, base level, seed
#define NET_CONNECT_ENDLESS_SIZE (4 + NET_ENDLESS_SEED_MAX)
#define NET_CONNECT_DESTRUCT  (NET_CONNECT_ENDLESS + NET_CONNECT_ENDLESS_SIZE)
#define NET_CONNECT_DESTRUCT_SIZE 5   // battle mode, Uint32 terrain seed
#define NET_CONNECT_NAME      (NET_CONNECT_DESTRUCT + NET_CONNECT_DESTRUCT_SIZE)

#define NET_RETRY         640          // ticks to wait for packet acknowledgment before resending
#define NET_RESEND        320          // ticks to wait before requesting unreceived game packet
#define NET_KEEP_ALIVE    1600         // ticks to wait between keep-alive packets
#define NET_TIME_OUT      16000        // ticks to wait before considering connection dead
#define NET_DEPART_GRACE  4000         // ticks of end-screen silence that read as the peer having left
#define NET_PING_MAX      5000         // round trips longer than this are treated as garbage, not latency
#define NET_DRAIN_MAX     32           // datagrams network_check() reads per call at most

bool isNetworkGame = false;

// Input-delay ticks also bound how far either peer may run ahead. Values above 1 enable
// single-packet XOR recovery. Default 3 supports roughly 85ms RTT at 35Hz.
int network_delay = 3;

char *network_opponent_host = NULL;

Uint16 network_player_port = NET_PORT,
       network_opponent_port = NET_PORT;

Uint16 network_listen_port = NET_PORT;

int network_host_player = 1;

// Session game speed, a host option (1..5, 4 = Normal); the joiner adopts it from the
// settings block like every other sim-binding choice.
int network_host_game_speed = 4;
NetworkGameType network_game_type = NETWORK_GAME_ARCADE;
int network_host_episode = 1;
int network_host_difficulty = DIFFICULTY_NORMAL;

// Arcade's Timed Battle shape and which of the three levels it races (see network.h).
bool network_host_timed_battle = false;
int  network_host_battle_level = 1;

// Endless lobby block. Blank seed means "roll one when the run starts".
char network_host_endless_seed[NET_ENDLESS_SEED_MAX] = "";
char network_endless_session_seed[NET_ENDLESS_SEED_MAX] = "";
int  network_host_endless_run_mode = 1;   // ENDLESS_RUNMODE_STANDARD
int  network_host_endless_chooser = 0;    // ENDLESS_PICK_HOST
bool network_host_endless_combo_shared = false;
int  network_host_endless_base_rule = 0;  // ENDLESS_BASE_VARIED: a level per charted route

// Destruct lobby block. The seed is per session; every round hashes it with the round number.
int    network_host_destruct_mode = 0;    // MODE_5CARDWAR
Uint32 network_destruct_session_seed = 0;

static char empty_string[] = "";
char *network_player_name = empty_string,
     *network_opponent_name = empty_string;

bool network_is_host = false;

bool network_session_saveable = false;
bool network_from_lobby = false;

// network_player_name starts out pointing at a static empty string and is otherwise owned
// heap memory, so changing it has to know which it currently is.  Clamps to NET_NAME_MAX:
// the stored name feeds fixed-size HUD buffers directly, so an over-long name from a
// hand-edited config file must never get past this point.
void network_set_player_name(const char *name)
{
	if (network_player_name != empty_string)
		free(network_player_name);

	if (name == NULL || name[0] == '\0')
	{
		network_player_name = empty_string;
		return;
	}

	size_t len = strlen(name);
	if (len > NET_NAME_MAX)
		len = NET_NAME_MAX;

	network_player_name = malloc_die(len + 1);
	memcpy(network_player_name, name, len);
	network_player_name[len] = '\0';
}

#ifdef WITH_NETWORK
// Not `socket`: <winsock2.h> (included below for the WSAECONNRESET fix) declares a function of
// that name, and a file-scope object would collide with it.
static UDPsocket net_socket;
static IPaddress ip;

/* Second socket a listening host keeps on the well-known port, so Find LAN Games reaches it
 * whatever port the game itself is on. The reply names the real port. Closed the moment a
 * player joins, and best-effort to open: with the port taken, address entry still works. */
static UDPsocket discover_socket;

UDPpacket *packet_out_temp;
static UDPpacket *packet_temp;

UDPpacket *packet_in[NET_PACKET_QUEUE] = { NULL },
          *packet_out[NET_PACKET_QUEUE] = { NULL };

static Uint16 last_out_sync = 0, queue_in_sync = 0, queue_out_sync = 0, last_ack_sync = 0;
static Uint32 last_in_tick = 0, last_out_tick = 0;

// Wire tests start the sequence space here instead of zero, so every scenario crosses the
// Uint16 wraparound as part of its normal run. Zero outside the tests.
static Uint16 qa_seq_base = 0;

UDPpacket *packet_state_in[NET_PACKET_QUEUE] = { NULL };
static UDPpacket *packet_state_in_xor[NET_PACKET_QUEUE] = { NULL };
UDPpacket *packet_state_out[NET_PACKET_QUEUE] = { NULL };

static Uint16 last_state_in_sync = 0, last_state_out_sync = 0;
static Uint32 last_state_in_tick = 0;

static bool net_initialized = false;
static bool connected = false, quit = false;

// A lobby host listens without knowing who will join, so it cannot bind channel 0 up front
// the way the original command-line netplay did (both sides were given each other's address).
// While this is set, the first inbound connect packet binds the channel to its sender.
static bool host_awaiting_peer = false;

// Whether `ip` names a peer yet.  It is only assigned, never cleared, so a session that never
// reached a peer would otherwise still be comparing against the one before it.
static bool peer_addr_known = false;

// Round trip to the peer.  Every keep-alive carries the sender's own tick count and the peer
// echoes it straight back, so the sample never leaves the machine that started it and the two
// clocks never have to agree.  Smoothed: a single UDP round trip is noisy enough that a raw
// figure jitters by tens of ticks between readings.
static float ping_ema = 0.0f;
static bool ping_valid = false;

// Session traffic/health counters for the crash log's Network section (network_write_diagnostics).
// Cheap enough to maintain on every packet; reset when the session tears down.
static struct
{
	Uint32 dg_in, dg_out;                       // datagrams consumed / put on the wire
	Uint32 recv_errors, send_errors;            // socket-level failures (ICMP resets etc.)
	Uint32 bad_packets;                         // unknown type on the game channel
	Uint32 retries;                             // reliable-channel retransmits
	Uint32 window_overflow;                     // reliable packets that arrived past the receive window
	Uint32 state_in, state_out, state_late;     // state stream; late = outside the queue window
	Uint32 xor_rebuilds;                        // lost state packets reconstructed from parity
	Uint32 resend_req_sent, resend_req_served;  // state resend requests
	Uint32 stalls;                              // state waits that earned a >3 s crashlog note
	Uint32 desync_levels;                       // levels that reported a desync (either mode)
	Uint32 connect_tick, first_desync_tick;
	int    first_desync_level;
} net_diag;
#endif

#ifdef WITH_NETWORK
jmp_buf network_bailout_env;
bool network_bailout_armed = false;
#endif

uint thisPlayerNum = 0;  /* Player number on this PC (1 or 2) */
uint networkHostPlayerNum = 1;  /* Player number the host is flying */

JE_boolean haltGame = false;

JE_boolean moveOk;

/* Special Requests */
JE_boolean skipLevelRequest, helpRequest, nortShipRequest;
JE_boolean yourInGameMenuRequest, inGameMenuRequest;

#ifdef WITH_NETWORK
static void packet_copy(UDPpacket *dst, UDPpacket *src)
{
	void *temp = dst->data;
	memcpy(dst, src, sizeof(*dst));
	dst->data = temp;
	memcpy(dst->data, src->data, src->len);
}

/* Reuse queue packets to avoid per-tick SDL_net allocations. */
#define NET_PACKET_POOL_MAX (NET_PACKET_QUEUE * 5)
static UDPpacket *packet_pool[NET_PACKET_POOL_MAX];
static int packet_pool_count;

static UDPpacket *packet_acquire(void)
{
	if (packet_pool_count > 0)
		return packet_pool[--packet_pool_count];
	return SDLNet_AllocPacket(NET_PACKET_SIZE);
}

static UDPpacket *packet_acquire_or_halt(void)
{
	UDPpacket *packet = packet_acquire();
	if (packet == NULL)
	{
		fprintf(stderr, "error: SDLNet_AllocPacket: %s\n", SDLNet_GetError());
		network_tyrian_halt(2, false);
	}
	return packet;
}

static void packet_release(UDPpacket *packet)
{
	if (packet == NULL)
		return;
	if (packet_pool_count < NET_PACKET_POOL_MAX)
		packet_pool[packet_pool_count++] = packet;
	else
		SDLNet_FreePacket(packet);
}

static void packet_destroy(UDPpacket **packet)
{
	if (*packet == NULL)
		return;
	SDLNet_FreePacket(*packet);
	*packet = NULL;
}

static void packets_shift_up(UDPpacket **packet, int max_packets)
{
	packet_release(packet[0]);
	memmove(packet, packet + 1, (size_t)(max_packets - 1) * sizeof(*packet));
	packet[max_packets - 1] = NULL;
}

static void packets_shift_down(UDPpacket **packet, int max_packets)
{
	packet_release(packet[max_packets - 1]);
	memmove(packet + 1, packet, (size_t)(max_packets - 1) * sizeof(*packet));
	packet[0] = NULL;
}

// prepare new packet for sending
void network_prepare(Uint16 type)
{
	SDLNet_Write16(type,          &packet_out_temp->data[0]);
	SDLNet_Write16(last_out_sync, &packet_out_temp->data[2]);
}

// send packet but don't expect acknowledgment of delivery
static bool network_send_no_ack(int len)
{
	packet_out_temp->len = len;

	if (!SDLNet_UDP_Send(net_socket, 0, packet_out_temp))
	{
		printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
		++net_diag.send_errors;
		return false;
	}

	++net_diag.dg_out;
	return true;
}

bool network_send_unacked(int len)
{
	return network_send_no_ack(len);
}

// send packet and place it in queue to be acknowledged
bool network_send(int len)
{
	// Reserve queue space before sending so every datagram receives a unique sequence number.
	Uint16 i = last_out_sync - queue_out_sync;
	if (i >= NET_PACKET_QUEUE)
	{
		/* A full window is backpressure. Service the socket until a slot opens,
		 * preserving this packet because service calls reuse packet_out_temp. */
		Uint8 pending[NET_PACKET_SIZE];
		memcpy(pending, packet_out_temp->data, (size_t)len);

		const Uint32 wait_start = SDL_GetTicks();
		while ((i = (Uint16)(last_out_sync - queue_out_sync)) >= NET_PACKET_QUEUE
		       && SDL_GetTicks() - wait_start < NET_TIME_OUT && !quit)
		{
			watchdog_heartbeat();
			network_check();
			SDL_Delay(1);
		}

		memcpy(packet_out_temp->data, pending, (size_t)len);

		if (i >= NET_PACKET_QUEUE)
		{
			fprintf(stderr, "error: outbound packet queue overflow\n");

			crashlog_note_net("NETWORK QUEUE OVERFLOW",
			              "no acknowledgement for a full outbound queue; treating the link as lost");

			if (!quit)
				network_tyrian_halt(2, false);

			return false;
		}
	}

	bool temp = network_send_no_ack(len);

	packet_out[i] = packet_acquire_or_halt();
	packet_copy(packet_out[i], packet_out_temp);

	last_out_sync++;

	// The retry timer belongs to the queue head. Starting it on any later send instead pushes
	// the head's retransmission into the future for as long as new sends keep coming.
	if (i == 0)
		last_out_tick = SDL_GetTicks();

	return temp;
}

// send acknowledgment packet
static int network_acknowledge(Uint16 sync)
{
	SDLNet_Write16(PACKET_ACKNOWLEDGE, &packet_out_temp->data[0]);
	SDLNet_Write16(sync,               &packet_out_temp->data[2]);
	network_send_no_ack(4);

	return 0;
}

// activity lately?
static bool network_is_alive(void)
{
	return (SDL_GetTicks() - last_in_tick < NET_TIME_OUT || SDL_GetTicks() - last_state_in_tick < NET_TIME_OUT);
}

/* Retransmit interval, adapted to the measured round trip. NET_RETRY is sized for a link with
 * nothing known about it; holding a 40ms link to it turns every lost packet into a two-thirds
 * of a second stall. Floor well above jitter so a slow reply is not answered with a duplicate. */
static Uint32 net_retry_interval(void)
{
	if (!ping_valid)
		return NET_RETRY;

	Uint32 interval = (Uint32)(ping_ema + 0.5f) * 2 + 60;
	if (interval < 120)
		interval = 120;
	if (interval > NET_RETRY)
		interval = NET_RETRY;
	return interval;
}

// Exported liveness for the rollback stall logic: a peer sitting in menus for
// minutes keeps sending keep-alives, and that must read as "slow, not dead".
bool network_peer_alive(void)
{
	return network_is_alive();
}

// -1 rather than 0 while unknown: no reply has come back yet, or the peer is old enough not to
// echo the stamp at all.  Callers show that as "--", which is honest; a zero would read as a
// perfect link.
int network_ping_ms(void)
{
	if (!connected || !ping_valid)
		return -1;

	return (int)(ping_ema + 0.5f);
}

// Answer a discovery probe with the version, the real game port, and this player's name.
static void network_discover_answer(UDPsocket sock, const IPaddress *to)
{
	// network_set_player_name already clamps the stored name; the re-clamp keeps the
	// fixed-size packet fill safe on its own terms.
	size_t name_len = strlen(network_player_name);
	if (name_len > NET_NAME_MAX)
		name_len = NET_NAME_MAX;

	SDLNet_Write16(PACKET_DISCOVER_REPLY, &packet_out_temp->data[0]);
	SDLNet_Write16(NET_VERSION,           &packet_out_temp->data[2]);
	SDLNet_Write16(network_player_port,   &packet_out_temp->data[4]);
	memcpy(&packet_out_temp->data[6], network_player_name, name_len);
	packet_out_temp->data[6 + name_len] = '\0';

	packet_out_temp->len = (int)(6 + name_len + 1);
	packet_out_temp->address = *to;

	// Channel -1 sends to the packet's own address, which is what we want: replying must
	// not disturb the channel binding the game protocol uses.
	SDLNet_UDP_Send(sock, -1, packet_out_temp);
}

// Consume at most one datagram. A receive error is transient: Windows reports ICMP unreachable as
// WSAECONNRESET on the next receive without disturbing queued datagrams.
static int network_recv_one(void)
{
	switch (SDLNet_UDP_Recv(net_socket, packet_temp))
	{
		case -1:
			++net_diag.recv_errors;
			return -1;
		case 0:
			return 0;
		default:
			++net_diag.dg_in;
			// LAN discovery probes come from machines we have never spoken to, so they arrive
			// on no channel (-1) and have to be answered before the channel check below.  They
			// are never queued and never bind anything; a host that already has a player stays
			// silent so it does not advertise a full game.
			if (packet_temp->len >= 4 &&
			    SDLNet_Read16(&packet_temp->data[0]) == PACKET_DISCOVER)
			{
				if (host_awaiting_peer)
					network_discover_answer(net_socket, &packet_temp->address);

				return 1;
			}

			// An unbound socket reports channel -1, so a listening host has to adopt the
			// sender before the normal channel check below can pass.  Only a connect packet
			// may do this, and only until someone has actually joined.
			if (host_awaiting_peer && packet_temp->len >= 4 &&
			    SDLNet_Read16(&packet_temp->data[0]) == PACKET_CONNECT)
			{
				if (SDLNet_UDP_Bind(net_socket, 0, &packet_temp->address) == -1)
				{
					fprintf(stderr, "error: SDLNet_UDP_Bind: %s\n", SDLNet_GetError());
					return -1;
				}
				ip = packet_temp->address;
				peer_addr_known = true;
				host_awaiting_peer = false;
				packet_temp->channel = 0;

				// The lobby is full; stop advertising on the well-known port.
				if (discover_socket)
				{
					SDLNet_UDP_Close(discover_socket);
					discover_socket = NULL;
				}
			}

			// Accept the known peer by host address even if NAT rewrites its outbound source port.
			const bool from_peer = packet_temp->channel == 0 ||
			                       (peer_addr_known && packet_temp->address.host == ip.host);

			if (from_peer && packet_temp->len >= 4)
			{
				switch (SDLNet_Read16(&packet_temp->data[0]))
				{
					case PACKET_ACKNOWLEDGE:
					{
						const Uint16 ack = SDLNet_Read16(&packet_temp->data[2]);
						const Uint16 i = ack - queue_out_sync;

						// Only an acknowledgement for a packet still outstanding may advance the
						// bookkeeping. Anything else is a stale duplicate or forged; trusting it
						// drifts queue_out_sync off last_out_sync and the send path reads that
						// as an overflow.
						if (i >= (Uint16)(last_out_sync - queue_out_sync))
						{
							last_in_tick = SDL_GetTicks();
							break;
						}

						if ((Uint16)(ack - last_ack_sync) < NET_PACKET_QUEUE)
							last_ack_sync = ack;

						if (packet_out[i])
						{
							packet_release(packet_out[i]);
							packet_out[i] = NULL;
						}

						// remove acknowledged packets from queue
						while (packet_out[0] == NULL && (Uint16)(last_ack_sync - queue_out_sync) < NET_PACKET_QUEUE)
						{
							packets_shift_up(packet_out, NET_PACKET_QUEUE);

							queue_out_sync++;
						}

						last_in_tick = SDL_GetTicks();
						break;
					}

					case PACKET_CONNECT:
						/*
						 * A delayed/duplicated handshake may arrive after gameplay packets.
						 * Resetting queue_in_sync here would discard the live receive window.
						 */
						if (connected)
						{
							const Uint16 connect_sync = SDLNet_Read16(&packet_temp->data[2]);
							const Sint16 slot = (Sint16)(connect_sync - queue_in_sync);
							if (slot == 0)
							{
								// At the head: discard it by advancing the window past it.
								packets_shift_up(packet_in, NET_PACKET_QUEUE);
								++queue_in_sync;
							}
							else if (slot > 0 && slot < NET_PACKET_QUEUE)
							{
								/* Queue reordered handshake packets in their sequence slot. An
								 * acknowledged gap would stall the receive window permanently. */
							if (packet_in[slot] == NULL)
								packet_in[slot] = packet_acquire_or_halt();
							packet_copy(packet_in[slot], packet_temp);
							}
							else if (slot >= NET_PACKET_QUEUE)
							{
								// No room to place it; withhold the acknowledgement so the
								// retransmit arrives once the window has drained (same rule as
								// the general placement path below).
								++net_diag.window_overflow;
								last_in_tick = SDL_GetTicks();
								break;
							}
							// Behind the window: consumed already; just re-acknowledge.
							network_acknowledge(connect_sync);
							last_in_tick = SDL_GetTicks();
							break;
						}
						queue_in_sync = SDLNet_Read16(&packet_temp->data[2]);

						for (int i = 0; i < NET_PACKET_QUEUE; i++)
						{
							if (packet_in[i])
							{
								packet_release(packet_in[i]);
								packet_in[i] = NULL;
							}
						}
						// fall through

					case PACKET_DETAILS:
					case PACKET_WAITING:
					case PACKET_BUSY:
					case PACKET_LEVEL_READY:
					// The departure gate. A reliable type missing from this list is queued
					// nowhere and never acknowledged, so both ends would sit at the gate.
					case PACKET_DEPART_GATE:
					case PACKET_GAME_QUIT:
					case PACKET_GAME_PAUSE:
					case PACKET_GAME_MENU:
					case PACKET_DEBUG_SYNC:
					case PACKET_SHOP_SYNC:
					case PACKET_CUSTOM_WEAPON:
					// Every packet the Endless co-op channel carries: the run transfer on resume,
					// the death-prompt choice, and the "I have left the level" notice. Missing from
					// this list, all three were dropped here unread and unacknowledged, so the whole
					// channel was write-only however carefully both ends waited on it.
					case PACKET_ENDLESS_RUN:
					// Online Super Arcade's ship announcement. Same reason as the Endless block
					// above: a reliable type missing from this list is queued nowhere and never
					// acknowledged, so the sender retransmits forever and the pair deadlocks.
					case PACKET_SA_SHIP:
					// The Endless zone jump, settled before the course is folded. Same rule again.
					case PACKET_ENDLESS_JUMP:
					case PACKET_RESYNC:
					// Destruct's own recovery stream, on the same terms as PACKET_RESYNC.
					case PACKET_DESTRUCT_RESYNC:
						{
							const Uint16 sync = SDLNet_Read16(&packet_temp->data[2]);
							/* Signed so the comparison is wraparound-safe: behind the window is a
							 * packet already consumed, ahead of it is one there is no room for. */
							const Sint16 slot = (Sint16)(sync - queue_in_sync);
							if (slot >= 0 && slot < NET_PACKET_QUEUE)
							{
								if (packet_in[slot] == NULL)
									packet_in[slot] = packet_acquire_or_halt();
								packet_copy(packet_in[slot], packet_temp);
							}
							else if (slot >= NET_PACKET_QUEUE)
							{
								/* Do not acknowledge packets beyond the receive window. The sender
								 * must retain and retry them after this side drains enough space. */
								++net_diag.window_overflow;
								last_in_tick = SDL_GetTicks();
								break;
							}
							// Behind the window: consumed already, and the sender is repeating it
							// because our acknowledgement was lost. Acknowledge it again below.
						}

						network_acknowledge(SDLNet_Read16(&packet_temp->data[2]));
						// fall through

					case PACKET_KEEP_ALIVE:
						// Echo ping stamps immediately. Re-check the type because reliable packets fall through.
						if (SDLNet_Read16(&packet_temp->data[0]) == PACKET_KEEP_ALIVE &&
						    packet_temp->len >= 8)
						{
							SDLNet_Write16(PACKET_PING_REPLY, &packet_out_temp->data[0]);
							SDLNet_Write16(last_out_sync,     &packet_out_temp->data[2]);
							memcpy(&packet_out_temp->data[4], &packet_temp->data[4], 4);
							network_send_no_ack(8);
						}

						last_in_tick = SDL_GetTicks();
						break;

					case PACKET_PLAYER_LOOK:
						// Repeated keep-alive announcements repair packet loss.
						if (packet_temp->len >= 9 && packet_temp->data[4] >= 1 && packet_temp->data[4] <= 2
						    && packet_temp->data[4] != (Uint8)thisPlayerNum)
						{
							const uint seat = packet_temp->data[4] - 1u;
							netStyleSetSeatColor(seat, packet_temp->data[5]);

							// Cache the sender's view for future saves; it does not affect this machine.
							NetShipView view;
							view.opacity = packet_temp->data[6];
							view.shipOpacity = packet_temp->data[7] != 0;
							view.hpBars = packet_temp->data[8];
							netStyleSetView(seat, view);
						}

						last_in_tick = SDL_GetTicks();
						break;

					case PACKET_PING_REPLY:
						if (packet_temp->len >= 8)
						{
							// Our own stamp coming home.  An absurd figure means a corrupt or
							// stale packet rather than a slow link, and averaging it in would
							// leave the readout wrong for many seconds afterwards.
							const Uint32 rtt = SDL_GetTicks() - SDLNet_Read32(&packet_temp->data[4]);
							if (rtt <= NET_PING_MAX)
							{
								ping_ema = ping_valid ? ping_ema * 0.7f + rtt * 0.3f
								                      : (float)rtt;
								ping_valid = true;
							}
						}

						last_in_tick = SDL_GetTicks();
						break;

					case PACKET_QUIT:
						if (!quit)
						{
							network_prepare(PACKET_QUIT);
							network_send(4);  // PACKET_QUIT
						}

						network_acknowledge(SDLNet_Read16(&packet_temp->data[2]));

						if (!quit)
							network_tyrian_halt(1, true);
						break;

					case PACKET_STATE:
						// place packet in queue if within limits
						{
							Uint16 i = SDLNet_Read16(&packet_temp->data[2]) - last_state_in_sync + 1;
							if (i < NET_PACKET_QUEUE)
							{
								++net_diag.state_in;
								if (packet_state_in[i] == NULL)
									packet_state_in[i] = packet_acquire_or_halt();
								packet_copy(packet_state_in[i], packet_temp);
							}
							else
							{
								++net_diag.state_late;
							}
						}
						break;

					case PACKET_STATE_XOR:
						// place packet in queue if within limits
						{
							Uint16 i = SDLNet_Read16(&packet_temp->data[2]) - last_state_in_sync + 1;
							if (i < NET_PACKET_QUEUE)
							{
								if (packet_state_in_xor[i] == NULL)
								{
									packet_state_in_xor[i] = packet_acquire_or_halt();
									packet_copy(packet_state_in_xor[i], packet_temp);
								}
								else if (SDLNet_Read16(&packet_state_in_xor[i]->data[0]) != PACKET_STATE_XOR)
								{
									for (int j = 4; j < packet_state_in_xor[i]->len; j++)
										packet_state_in_xor[i]->data[j] ^= packet_temp->data[j];
									SDLNet_Write16(PACKET_STATE_XOR, &packet_state_in_xor[i]->data[0]);
								}
							}
						}
						break;

					case PACKET_INPUT:
						// Rollback input stream: redundant, unacknowledged, consumed
						// directly into the input history.
						nrb_handle_packet(packet_temp->data, packet_temp->len);
						last_in_tick = SDL_GetTicks();
						break;

					case PACKET_DESTRUCT_INPUT:
						// The minigame's own rollback stream, on the same terms.
						drb_handle_packet(packet_temp->data, packet_temp->len);
						last_in_tick = SDL_GetTicks();
						break;

					case PACKET_STATE_RESEND:
						// resend requested state packet if still available
						{
							Uint16 i = last_state_out_sync - SDLNet_Read16(&packet_temp->data[2]);
							if (i > 0 && i < NET_PACKET_QUEUE)
							{
								if (packet_state_out[i])
								{
									// A failed resend is not a receive error: log it and carry on, or
									// the drain would stop on a datagram it had already consumed.
									if (!SDLNet_UDP_Send(net_socket, 0, packet_state_out[i]))
									{
										printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
										++net_diag.send_errors;
									}
									else
									{
										++net_diag.dg_out;
										++net_diag.resend_req_served;
									}
								}
							}
						}
						break;

					default:
						++net_diag.bad_packets;
						fprintf(stderr, "warning: bad packet %d received\n", SDLNet_Read16(&packet_temp->data[0]));
						break;
				}

				return 1;
			}
			break;
	}

	// A datagram we had no use for is still a datagram consumed, so the drain below keeps going.
	return 1;
}

// poll for new packets received, check that connection is alive, resend queued packets if necessary
int network_check(void)
{
	if (!net_initialized)
		return -1;

	// Probes land on the well-known port while the game itself listens elsewhere. The socket
	// exists only while this machine hosts an empty lobby, and answers nothing else.
	while (discover_socket && SDLNet_UDP_Recv(discover_socket, packet_temp) > 0)
	{
		if (host_awaiting_peer && packet_temp->len >= 4 &&
		    SDLNet_Read16(&packet_temp->data[0]) == PACKET_DISCOVER)
			network_discover_answer(discover_socket, &packet_temp->address);
	}

	if (connected)
	{
		// timeout.  Every wait loop services the socket through here, so this halt reaches a dead
		// link before their own deadlines do and has to name the silence itself.
		if (!network_is_alive())
		{
			if (!quit)
			{
				const Uint32 quiet = SDL_GetTicks() - last_in_tick;
				const Uint32 quiet_state = SDL_GetTicks() - last_state_in_tick;

				fprintf(stderr, "error: nothing received from the other player for %u ms\n",
				        (unsigned)(quiet < quiet_state ? quiet : quiet_state));
				network_tyrian_halt(2, false);
			}
		}

		// keep-alive, which doubles as the ping probe: it is the one thing still flowing while
		// a player sits in the outpost, so the round trip stays measurable off the menus. Older
		// peers ignore the four-byte tail and send no timing reply.
		static Uint32 keep_alive_tick = 0;
		if (SDL_GetTicks() - keep_alive_tick > NET_KEEP_ALIVE)
		{
			network_prepare(PACKET_KEEP_ALIVE);
			SDLNet_Write32(SDL_GetTicks(), &packet_out_temp->data[4]);
			network_send_no_ack(8);

			network_player_look_publish();  // repeat cosmetic state on the keep-alive beat

			keep_alive_tick = SDL_GetTicks();
		}

		nrb_menu_keepalive();
	}

	// Retry everything still unacknowledged, oldest first. Resending only the head would drain
	// a backlog at one packet per retry interval when the acks for the rest were lost with it.
	if (packet_out[0] && SDL_GetTicks() - last_out_tick > net_retry_interval())
	{
		for (int i = 0; i < NET_PACKET_QUEUE; i++)
		{
			if (packet_out[i] == NULL)
				continue;

			if (!SDLNet_UDP_Send(net_socket, 0, packet_out[i]))
			{
				printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
				++net_diag.send_errors;
				return -1;
			}

			++net_diag.dg_out;
			++net_diag.retries;
		}

		last_out_tick = SDL_GetTicks();
	}

	// Drain a bounded batch. Taking one packet per frame cannot clear bursts and
	// turns their backlog into permanent latency.
	int handled = 0;
	for (int i = 0; i < NET_DRAIN_MAX; ++i)
	{
		const int got = network_recv_one();
		if (got <= 0)
			return handled > 0 ? handled : got;

		++handled;
	}

	return handled;
}

// discard working packet, now processing next packet in queue
bool network_update(void)
{
	if (packet_in[0])
	{
		packets_shift_up(packet_in, NET_PACKET_QUEUE);

		queue_in_sync++;

		return true;
	}

	return false;
}

/* True when every reliable packet sent has been acknowledged. Measured off the queue itself
 * rather than the acknowledgement high-water mark, which reads ahead of an unacknowledged head
 * whenever the ack for it was lost. See doc/notes.md on the reliable-channel retry rules. */
bool network_is_sync(void)
{
	return network_ack_backlog() == 0;
}

int network_ack_backlog(void)
{
	return (Uint16)(last_out_sync - queue_out_sync);
}

// Reliable packets waiting to be consumed, head included.  Diagnostic: a wait that stalls with a
// non-empty queue is stalled on a head nobody claims, which names the bug on sight.
int network_inbound_depth(void)
{
	int depth = 0;
	for (int i = 0; i < NET_PACKET_QUEUE; ++i)
	{
		if (packet_in[i] != NULL)
			++depth;
	}
	return depth;
}

// Type of the packet at the head of the reliable queue, or 0 when it is empty.
Uint16 network_inbound_head(void)
{
	return packet_in[0] != NULL ? SDLNet_Read16(&packet_in[0]->data[0]) : 0;
}

/* Reliable packets that arrived past the end of the receive window. They were acknowledged on the
 * way in, so the sender considers them delivered and will never send them again: each one is a
 * reliable packet permanently lost. Nonzero means the queue was allowed to fill, which is a bug
 * in whoever was supposed to be draining it, not a transport problem. */
Uint32 network_window_overflow(void)
{
	return net_diag.window_overflow;
}

// prepare new state for sending
void network_state_prepare(void)
{
	if (packet_state_out[0])
	{
		fprintf(stderr, "warning: state packet overwritten (previous packet remains unsent)\n");
	}
	else
	{
		packet_state_out[0] = packet_acquire_or_halt();
	}
	// Set unconditionally: a reused packet would otherwise keep whatever length it had.
	packet_state_out[0]->len = NET_STATE_SIZE;

	SDLNet_Write16(PACKET_STATE, &packet_state_out[0]->data[0]);
	SDLNet_Write16(last_state_out_sync, &packet_state_out[0]->data[2]);
	memset(&packet_state_out[0]->data[4], 0, NET_STATE_SIZE - 4);
}

// send state packet, xor packet if applicable
int network_state_send(void)
{
	if (!SDLNet_UDP_Send(net_socket, 0, packet_state_out[0]))
	{
		printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
		++net_diag.send_errors;
		return -1;
	}

	++net_diag.dg_out;
	++net_diag.state_out;

	// send xor of last network_delay packets
	if (network_delay > 1 && (last_state_out_sync + 1) % network_delay == 0 && packet_state_out[network_delay - 1] != NULL)
	{
		packet_copy(packet_temp, packet_state_out[0]);
		SDLNet_Write16(PACKET_STATE_XOR, &packet_temp->data[0]);
		for (int i = 1; i < network_delay; i++)
			for (int j = 4; j < packet_temp->len; j++)
				packet_temp->data[j] ^= packet_state_out[i]->data[j];

		if (!SDLNet_UDP_Send(net_socket, 0, packet_temp))
		{
			printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
			++net_diag.send_errors;
			return -1;
		}

		++net_diag.dg_out;
	}

	packets_shift_down(packet_state_out, NET_PACKET_QUEUE);

	last_state_out_sync++;

	return 0;
}

// receive state packet, wait until received
bool network_state_update(void)
{
	if (network_state_is_reset())
	{
		return 0;
	}
	else
	{
		packets_shift_up(packet_state_in, NET_PACKET_QUEUE);

		packets_shift_up(packet_state_in_xor, NET_PACKET_QUEUE);

		last_state_in_sync++;

		// current xor packet index
		int x = network_delay - (last_state_in_sync - 1) % network_delay - 1;

		// loop until needed packet is available
		const Uint32 wait_start = SDL_GetTicks();
		bool stall_reported = false;

		while (!packet_state_in[0])
		{
			// xor the packet from thin air, if possible
			if (packet_state_in_xor[x] && SDLNet_Read16(&packet_state_in_xor[x]->data[0]) == PACKET_STATE_XOR)
			{
				// check for all other required packets
				bool okay = true;
				for (int i = 1; i <= x; i++)
				{
					if (packet_state_in[i] == NULL)
					{
						okay = false;
						break;
					}
				}
				if (okay)
				{
					++net_diag.xor_rebuilds;
					packet_state_in[0] = packet_acquire_or_halt();
					packet_copy(packet_state_in[0], packet_state_in_xor[x]);
					for (int i = 1; i <= x; i++)
						for (int j = 4; j < packet_state_in[0]->len; j++)
							packet_state_in[0]->data[j] ^= packet_state_in[i]->data[j];
					/* XOR parity covers payload bytes only. Restore the reconstructed packet's
					 * type and tick before consumers key local replay history from its header. */
					SDLNet_Write16(PACKET_STATE, &packet_state_in[0]->data[0]);
					SDLNet_Write16((Uint16)(last_state_in_sync - 1),
					               &packet_state_in[0]->data[2]);
					break;
				}
			}

			static Uint32 resend_tick = 0;
			if (SDL_GetTicks() - last_state_in_tick > NET_RESEND && SDL_GetTicks() - resend_tick > NET_RESEND)
			{
				SDLNet_Write16(PACKET_STATE_RESEND,    &packet_out_temp->data[0]);
				SDLNet_Write16(last_state_in_sync - 1, &packet_out_temp->data[2]);
				network_send_no_ack(4);  // PACKET_RESEND
				++net_diag.resend_req_sent;

				resend_tick = SDL_GetTicks();
			}

			// Pump SDL while we wait.  Without this the window stops answering the OS (Windows
			// greys it out as "not responding") and the hang watchdog reports a crash-like
			// stall, when all that is really happening is waiting on the other player.  Every
			// other network wait loop in the game already pumps this way.
			service_SDL_events(false);

			const Uint32 waited = SDL_GetTicks() - wait_start;

			// Keep-alives refresh liveness even when state packets stop. Log that stall once
			// with enough context to distinguish a slow peer from a dead one.
			if (!stall_reported && waited > 3000)
			{
				stall_reported = true;
				++net_diag.stalls;

				char detail[256];
				snprintf(detail, sizeof(detail),
				         "waiting for peer state packet\n"
				         "  player      : %u\n"
				         "  in_sync     : %u   out_sync: %u   delay: %d\n"
				         "  since state : %u ms   since any packet: %u ms",
				         thisPlayerNum,
				         (unsigned)last_state_in_sync, (unsigned)last_state_out_sync, network_delay,
				         (unsigned)(SDL_GetTicks() - last_state_in_tick),
				         (unsigned)(SDL_GetTicks() - last_in_tick));
				crashlog_note_net("NETWORK STALL", detail);
			}

			// Bound it.  network_is_alive() only asks whether ANY packet arrived recently, and
			// a peer stuck outside the level keeps sending keep-alives, so that check alone
			// would let this spin forever.  Give up on the state stream specifically.
			if (waited > NET_TIME_OUT)
			{
				fprintf(stderr, "error: no state packets from the other player for %u ms\n",
				        (unsigned)waited);
				network_tyrian_halt(2, false);
			}

			// <= 0, not == 0: a receive error reads nothing, so skipping the sleep on it
			// would spin this loop on a core for as long as the peer takes.
			if (network_check() <= 0)
				SDL_Delay(1);
		}

		/* The wait completes only after the requested state is installed. */
		UDPpacket *const current_state = packet_state_in[0];
		if (current_state == NULL)
			return false;

		if (network_delay > 1)
		{
			// process the current in packet against the xor queue
			if (packet_state_in_xor[x] == NULL)
			{
				packet_state_in_xor[x] = packet_acquire_or_halt();
				packet_copy(packet_state_in_xor[x], current_state);
				packet_state_in_xor[x]->status = 0;
			}
			else
			{
				for (int j = 4; j < packet_state_in_xor[x]->len; j++)
					packet_state_in_xor[x]->data[j] ^= current_state->data[j];
			}
		}

		last_state_in_tick = SDL_GetTicks();
	}

	return 1;
}

// ignore first network_delay states of level
bool network_state_is_reset(void)
{
	return (last_state_out_sync < network_delay);
}

// reset queues for new level
void network_state_reset(void)
{
	last_state_in_sync = last_state_out_sync = 0;

	for (int i = 0; i < NET_PACKET_QUEUE; i++)
	{
		if (packet_state_in[i])
		{
			packet_release(packet_state_in[i]);
			packet_state_in[i] = NULL;
		}
	}
	for (int i = 0; i < NET_PACKET_QUEUE; i++)
	{
		if (packet_state_in_xor[i])
		{
			packet_release(packet_state_in_xor[i]);
			packet_state_in_xor[i] = NULL;
		}
	}
	for (int i = 0; i < NET_PACKET_QUEUE; i++)
	{
		if (packet_state_out[i])
		{
			packet_release(packet_state_out[i]);
			packet_state_out[i] = NULL;
		}
	}

	last_state_in_tick = SDL_GetTicks();
}

// Build and send PACKET_CONNECT.  Sent more than once during the handshake (retry, and again
// after sync), so it lives here rather than being spelled out at each site.
static void send_connect_packet(Uint16 episodes_local)
{
	size_t name_len = strlen(network_player_name);
	if (name_len > NET_NAME_MAX)
		name_len = NET_NAME_MAX;

	network_prepare(PACKET_CONNECT);
	// qa_net_version_skew is zero outside the wire tests; the mismatch scenario offsets it.
	SDLNet_Write16((Uint16)(NET_VERSION + qa_net_version_skew), &packet_out_temp->data[4]);
	SDLNet_Write16(network_delay,  &packet_out_temp->data[6]);
	SDLNet_Write16(episodes_local, &packet_out_temp->data[8]);
	SDLNet_Write16(thisPlayerNum,  &packet_out_temp->data[10]);
	SDLNet_Write16(network_game_type, &packet_out_temp->data[NET_CONNECT_GAME_TYPE]);
	SDLNet_Write16(network_host_episode, &packet_out_temp->data[NET_CONNECT_EPISODE]);
	SDLNet_Write16(network_host_difficulty, &packet_out_temp->data[NET_CONNECT_DIFFICULTY]);
	network_settings_pack(&packet_out_temp->data[NET_CONNECT_SETTINGS]);
	packet_out_temp->data[NET_CONNECT_ENDLESS] = (Uint8)network_host_endless_run_mode;
	packet_out_temp->data[NET_CONNECT_ENDLESS + 1] = (Uint8)network_host_endless_chooser;
	packet_out_temp->data[NET_CONNECT_ENDLESS + 2] = network_host_endless_combo_shared ? 1 : 0;
	packet_out_temp->data[NET_CONNECT_ENDLESS + 3] = (Uint8)network_host_endless_base_rule;
	memcpy(&packet_out_temp->data[NET_CONNECT_ENDLESS + 4], network_endless_session_seed,
	       NET_ENDLESS_SEED_MAX);
	packet_out_temp->data[NET_CONNECT_DESTRUCT] = (Uint8)network_host_destruct_mode;
	SDLNet_Write32(network_destruct_session_seed, &packet_out_temp->data[NET_CONNECT_DESTRUCT + 1]);
	memcpy(&packet_out_temp->data[NET_CONNECT_NAME], network_player_name, name_len);
	packet_out_temp->data[NET_CONNECT_NAME + name_len] = '\0';
	network_send(NET_CONNECT_NAME + name_len + 1);
}

// attempt to punch through firewall by firing off UDP packets at the opponent
// exchange game information
int network_connect(void)
{
	network_settings_apply_session_speed();
	netStyleSessionReset();

	const bool listening = network_from_lobby && network_is_host;

	if (listening)
	{
		// Nothing to resolve: whoever sends the first connect packet becomes the peer, and
		// network_check() binds channel 0 to them at that point.
		host_awaiting_peer = true;
		peer_addr_known = false;

		// Find LAN Games probes the well-known port, so a host on any other port keeps an
		// ear there too; the reply names the real one. Best effort: with the port taken,
		// joining by address still works, which is all a missing ear costs.
		if (network_listen_port != NET_PORT && discover_socket == NULL)
			discover_socket = SDLNet_UDP_Open(NET_PORT);
	}
	else
	{
		if (SDLNet_ResolveHost(&ip, network_opponent_host, network_opponent_port) == -1)
		{
			fprintf(stderr, "error: could not resolve '%s'\n",
			        network_opponent_host ? network_opponent_host : "(null)");
			return -2;
		}

		if (SDLNet_UDP_Bind(net_socket, 0, &ip) == -1)
		{
			fprintf(stderr, "error: SDLNet_UDP_Bind: %s\n", SDLNet_GetError());
			return -2;
		}

		peer_addr_known = true;
	}

	Uint16 episodes = 0, episodes_local = 0;
	assert(EPISODE_MAX <= 16);
	for (int i = EPISODE_MAX - 1; i >= 0; i--)
	{
		episodes <<= 1;
		episodes |= (episodeAvail[i] != 0);
	}
	episodes_local = episodes;

	assert(NET_PACKET_SIZE - NET_CONNECT_NAME >= NET_NAME_MAX + 1);

	// The lobby decides the roles, so derive the player number from them; a command-line
	// game keeps whatever --net-player-number set and is checked for conflicts below.
	// The joiner's slot is provisional until the host's connect packet names the host's
	// own; it has to send before it can know which one that leaves it.
	if (network_from_lobby)
		thisPlayerNum = network_is_host ? networkHostPlayerNum : 3 - networkHostPlayerNum;

	// A lobby joiner adopts the host's session flags. Command-line peers receive no settings
	// block and must start with matching configuration.
	network_arm_local_session();

connect_reset:
	// A listening host has no address to send to yet, so it stays quiet until the joiner
	// turns up; the send happens once the channel is bound, just below the wait loop.
	if (!host_awaiting_peer)
		send_connect_packet(episodes_local);

	// until opponent sends connect packet
	while (true)
	{
		push_joysticks_as_keyboard();
		service_SDL_events(false);

		// The lobby's "Connecting..." / "Waiting for a player" frame is still on
		// screen; re-present it with the cursor composited so the pointer stays
		// alive (and visibly responsive) through the whole wait.
		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();

		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			// From the lobby, backing out of "waiting for player" has to return to the menu.
			// Only a command-line game (which has no menu to return to) still exits here.
			if (network_from_lobby)
			{
				/* Best effort, repeated because nothing will retry it: whoever we may already
				 * have been talking to gets told, or they sit out the dead-link timeout (and a
				 * joiner still mid-connect has no timeout at all, only its own Esc). */
				if (peer_addr_known)
				{
					network_prepare(PACKET_QUIT);
					for (int i = 0; i < 3; ++i)
						network_send_no_ack(4);
				}
				return -1;
			}
			network_tyrian_halt(0, false);
		}

		// never timeout
		last_in_tick = SDL_GetTicks();

		if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CONNECT)
			break;

		network_update();
		network_check();
	}

	// The joiner is known now, so a listening host can finally introduce itself.  last_out_sync
	// only advances on a send, so sitting at its starting value means the connect is still owed.
	if (listening && last_out_sync == qa_seq_base)
		send_connect_packet(episodes_local);

connect_again:
	// packet_copy only fills the first `len` bytes of a reused NET_PACKET_SIZE buffer, so reading
	// past the length gets whatever the PREVIOUS packet left there; a short connect packet would
	// take the version, delay and whole settings block from stale bytes.  Nothing that speaks this
	// protocol version sends fewer, so treat it as the version mismatch it is.
	if (packet_in[0]->len < NET_CONNECT_NAME)
	{
		fprintf(stderr, "error: malformed connect packet from opponent (%d bytes)\n", packet_in[0]->len);
		network_tyrian_halt(4, true);
	}
	// Compared against the same skewed value the connect packet advertises, so the mismatch
	// wire scenario models a peer whose build constant genuinely differs.
	if (SDLNet_Read16(&packet_in[0]->data[4]) != (Uint16)(NET_VERSION + qa_net_version_skew))
	{
		fprintf(stderr, "error: network version did not match opponent's\n");
		network_tyrian_halt(4, true);
	}
	if (network_from_lobby)
	{
		// Host dictates: the joiner takes the host's delay and every simulation-affecting
		// setting, so the two sims are configured identically before the first tick.  Its own
		// values are stashed and restored when the session ends.
		if (!network_is_host)
		{
			const int host_delay = SDLNet_Read16(&packet_in[0]->data[6]);

			// Must be taken exactly or not at all: quietly clamping to something else would
			// leave the two sides reading each other's packets at different offsets, which
			// looks like a working connection and plays like nonsense.
			if (host_delay < 1 || host_delay * 2 > NET_PACKET_QUEUE - 2)
			{
				fprintf(stderr, "error: host asked for an unusable network delay (%d)\n", host_delay);
				network_tyrian_halt(5, true);
			}
			network_delay = host_delay;

			const int host_game_type = SDLNet_Read16(&packet_in[0]->data[NET_CONNECT_GAME_TYPE]);
			const int host_episode = SDLNet_Read16(&packet_in[0]->data[NET_CONNECT_EPISODE]);
			const int host_difficulty = SDLNet_Read16(&packet_in[0]->data[NET_CONNECT_DIFFICULTY]);
			if (host_game_type < 0 || host_game_type >= NETWORK_GAME_TYPE_COUNT ||
			    host_episode < 1 || host_episode > EPISODE_MAX ||
			    host_difficulty < DIFFICULTY_EASY || host_difficulty > DIFFICULTY_LORD_OF_GAME)
			{
				fprintf(stderr, "error: host sent unusable lobby settings (%d/%d/%d)\n",
				        host_game_type, host_episode, host_difficulty);
				network_tyrian_halt(5, true);
			}
			network_game_type = (NetworkGameType)host_game_type;
			network_host_episode = host_episode;
			network_host_difficulty = host_difficulty;

			network_settings_adopt(&packet_in[0]->data[NET_CONNECT_SETTINGS]);
			network_endless_adopt(&packet_in[0]->data[NET_CONNECT_ENDLESS]);

			// The Destruct block. The mode indexes battle-mode tables on both machines, so a
			// corrupt byte is clamped rather than trusted; any seed value is a playable seed.
			network_host_destruct_mode = packet_in[0]->data[NET_CONNECT_DESTRUCT];
			if (network_host_destruct_mode < 0 || network_host_destruct_mode >= DESTRUCT_MODES)
				network_host_destruct_mode = 0;
			network_destruct_session_seed = SDLNet_Read32(&packet_in[0]->data[NET_CONNECT_DESTRUCT + 1]);

			// The adopted gameSpeed only sets the global; push it through to the
			// tick-rate machinery so the joiner runs at the host's chosen speed.
			JE_initProcessorType();
			JE_setNewGameSpeed();

			// The host names its own slot too, and the joiner takes the other one: a host
			// that picked player 2 to fly the Dragonwing leaves player 1 here.
			const int host_slot = SDLNet_Read16(&packet_in[0]->data[10]);
			if (host_slot != 1 && host_slot != 2)
			{
				fprintf(stderr, "error: host asked for an unusable player number (%d)\n", host_slot);
				network_tyrian_halt(6, true);
			}
			networkHostPlayerNum = (uint)host_slot;
			thisPlayerNum = 3 - networkHostPlayerNum;
		}
	}
	else
	{
		// Command-line netplay only fills in the host ROLE (player 1, for recovery and
		// arbitration); both sides were configured by hand, so a disagreement is still a
		// hard error rather than something to resolve.
		if (SDLNet_Read16(&packet_in[0]->data[6]) != network_delay)
		{
			fprintf(stderr, "error: network delay did not match opponent's\n");
			network_tyrian_halt(5, true);
		}
	}
	// Layout compatibility is mutual and is checked by BOTH sides, host included: the
	// settings block above is host-dictated, but whether the peer could ever adopt our
	// snapshot bytes is a property of the pair.  Runs after the adopt, so it has the last
	// word over the recovery flag the host asked for.
	network_settings_check_layout(&packet_in[0]->data[NET_CONNECT_SETTINGS]);

	// Only command-line netplay can conflict: both sides were numbered by hand, and nothing
	// else stops them flying the same ship.  A lobby game is settled above instead; the
	// joiner's declared number is stale by construction (sent before the host's arrived), so
	// comparing the two would reject the very case that assignment resolves.
	if (!network_from_lobby && SDLNet_Read16(&packet_in[0]->data[10]) == thisPlayerNum)
	{
		fprintf(stderr, "error: player number conflicts with opponent's\n");
		network_tyrian_halt(6, true);
	}

	episodes = SDLNet_Read16(&packet_in[0]->data[8]);
	for (int i = 0; i < EPISODE_MAX; i++) {
		episodeAvail[i] &= (episodes & 1);
		episodes >>= 1;
	}

	// The name is whatever trails the settings block.  Take the length from the packet rather
	// than trusting it to be terminated, tolerate a packet too short to hold one at all, and
	// hold the sender to the same limit we send under; the retry path below re-enters here,
	// so anything already allocated is ours to release first.
	{
		int name_len = packet_in[0]->len - NET_CONNECT_NAME;
		if (name_len > NET_NAME_MAX)
			name_len = NET_NAME_MAX;

		if (network_opponent_name != empty_string)
			free(network_opponent_name);

		if (name_len > 0)
		{
			network_opponent_name = malloc_die(name_len + 1);
			memcpy(network_opponent_name, &packet_in[0]->data[NET_CONNECT_NAME], name_len);
			network_opponent_name[name_len] = '\0';
		}
		else
		{
			network_opponent_name = empty_string;
		}
	}

	network_update();

	// until opponent has acknowledged
	while (!network_is_sync())
	{
		service_SDL_events(false);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();

		// got a duplicate packet; process it again (but why?)
		if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CONNECT)
			goto connect_again;

		network_check();

		// maybe opponent didn't get our packet
		if (SDL_GetTicks() - last_out_tick > NET_RETRY)
			goto connect_reset;
	}

	// send another packet since sometimes the network syncs without both connect packets exchanged
	// there should be a better way to handle this
	send_connect_packet(episodes_local);

	connected = true;
	net_diag.connect_tick = SDL_GetTicks();

	// Session banner: every online session leaves a mark in the net log, so an entry-free
	// log means "no trouble detected" instead of "logging never ran".
	{
		// The state layout goes in the banner rather than only in the mismatch entry:
		// two logs from a working session then PROVE the snapshots are interchangeable,
		// which is the whole precondition for recovery and is otherwise invisible.
		char detail[192];
		snprintf(detail, sizeof(detail),
		         "player %u (%s), netcode %s, desync recovery %s, delay %d\n"
		         "state layout: %lu bytes, fingerprint %08x",
		         (unsigned)thisPlayerNum, network_is_host ? "host" : "joiner",
		         nrb_session_mode() ? "rollback" : "delay-based",
		         nrb_session_recovery() ? "on" : "off",
		         network_delay,
		         (unsigned long)rollback_state_size(),
		         (unsigned)rollback_layout_fingerprint());
		crashlog_netlog_line("NETWORK SESSION START", detail);
	}

	return 0;
}

// Terminate the network session after a local or peer error.
OT_NORETURN void network_tyrian_halt(unsigned int err, bool attempt_sync)
{
	const char *const err_msg[] = {
		"Quitting...",
		"Other player quit the game.",
		"Network connection was lost.",
		"Network connection failed.",
		"Network version mismatch.",
		"Network delay mismatch.",
		"Network player number conflict.",
		"Players desynced - game stopped.",
	};

	quit = true;

	// The desync halt can arrive from inside a re-simulation pass, whose silent flag
	// suppresses every sprite draw; including this screen's text and menus.
	rollback_resim = false;
	rollback_resim_silent = false;

	/* The peer is gone with the session, but it still reads as alive until the activity timeout
	 * runs out. Disarm the outpost rendezvous now, or a halt raised while the shop was open holds
	 * the disconnect save on "Waiting for other player." for an answer that cannot come. */
	network_shop_end();

	if (err >= COUNTOF(err_msg))
		err = 0;

	// Test peers report the halt and exit nonzero because no player can dismiss this screen.
	if (qa_net_rounds > 0 || qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "network test: session halt: %s\n", err_msg[err]);
		fflush(stderr);
		exit(1);
	}

	// The session is over; take the music with it rather than leaving the level's track playing
	// under a dead-link screen. Half a second, the ramp the death screen already uses; it runs
	// before the fade to black so the last frame holds while the music goes, and finishes with
	// the song stopped and the master volume back where the next screen expects it.
	{
		MusicFadeOut songFade;
		music_fade_out_init(&songFade);
		while (!songFade.done)
		{
			music_fade_out_tick(&songFade);
			service_SDL_events(false);
			JE_showVGA();
			SDL_Delay(4);
		}
	}

	fade_black(10);

	VGAScreen = VGAScreenSeg;

	// This screen can be reached mid-game, where the widescreen pillarbox is
	// off; it is a legacy 320px picture, so centre it with the side gradients
	// like every other menu screen.
	set_menu_centered(true);

	// Reached mid-game the mouse is still captured for ship control; release it,
	// or the cursor below can never move off wherever the ship left it.
	mouseSetRelative(false);

	// Offer a coherent LAST LEVEL backup after an involuntary mid-game disconnect.
	if (err != 0 && network_session_saveable && network_bailout_armed)
	{
		if (networkDisconnectSavePrompt(err_msg[err]))
		{
			// Save the pre-level outpost state, not partial progress from the interrupted level.
			// The load clears the mode flag before the slot's Endless half is read back, so read it
			// first; without this the re-save keeps no run at all.
			const bool was_endless = endlessMode;
			JE_loadGameRecord(&saveFiles[22 - 1], true);
			if (was_endless)
				endlessLoadSlot(22);
			JE_loadScreen(true, true);
		}
	}
	else
	{
		JE_loadPic(VGAScreen, 2, false);
		draw_font_hv_shadow(VGAScreen, 320 / 2, 20, "Online Multiplayer", large_font, centered, 15, -3, false, 2);
		JE_dString(VGAScreen, JE_fontCenter(err_msg[err], SMALL_FONT_SHAPES), 140, err_msg[err], SMALL_FONT_SHAPES);

		JE_showVGA();
		fade_palette(colors, 10, 0, 255);

		if (attempt_sync)
		{
			while (!network_is_sync() && network_is_alive())
			{
				service_SDL_events(false);

				mouseCursor = MOUSE_POINTER_NORMAL;
				JE_mouseStart();
				JE_showVGA();
				JE_mouseReplace();
				if (!output_vsync)
					limit_render_fps();

				network_check();
			}
		}

		if (err)
		{
			// Re-present each frame so the cursor stays alive on the "press any
			// button" screen, like every other menu wait.
			while (!JE_anyButton())
			{
				mouseCursor = MOUSE_POINTER_NORMAL;
				JE_mouseStart();
				JE_showVGA();
				JE_mouseReplace();
				if (!output_vsync)
					limit_render_fps();
			}
		}
	}

	fade_black(10);

	// A dead session should put the player back on the title screen, not on the
	// desktop.  Tear the whole session down (socket, queues, adopted settings,
	// rollback mode) and unwind to the main loop's landing pad; the next JE_main
	// entry reloads sprite banks and state like any fresh game.
	if (network_bailout_armed)
	{
		network_shutdown();

		isNetworkGame = false;
		network_from_lobby = false;
		network_is_host = false;
		twoPlayerMode = false;
		coopCampaignMode = false;
		coopEndlessMode = false;
		arcadeSeparateMode = false;
		timedBattleMode = false;
		superTyrian = false;
		superArcadeMode = SA_NONE;
		network_sa_ship_reset();
		haltGame = false;
		/* The main loop dispatches into Destruct on this flag and clears it when the minigame
		 * returns; a teardown unwinds past that, so a Destruct session abandoned at its title card
		 * left it set and the NEXT game started -- whatever it was -- was diverted into Destruct. */
		loadDestruct = false;
		JE_clearSpecialRequests();

		longjmp(network_bailout_env, 1);
	}

	// Not armed (very early startup failure): the original hard exit.  JE_tyrianHalt saves
	// the config, so put the stashed local settings (forced-Normal gameSpeed) back first.
	network_settings_restore();
	SDLNet_Quit();

	JE_tyrianHalt(5);
}

/* The settings block's tail, added when the flags word at byte 4 filled up. Bytes 0..23 keep the
 * layout they always had, so only what is below moved onto new ground. */
#define NET_SET_FLAGS2   24   /* Uint16: bit 0 expertMode, bits 1-2 epDiffMode[8], bit 3 centered
                                 shot hitboxes, bits 4+ the later epDiffMode entries, bit 15 Guided
                                 Aim; seven spare between the two                                  */
#define NET_SET_EXPERT   26   /* NETWORK_EXPERT_SLOTS x Uint16                                    */
#define NET_SET_DEBUG_FLAGS 42 /* Uint16: simulation-affecting Debug Mode toggles                  */
#define NET_SET_NOCLIP      44 /* Uint8: noclipMode                                                 */
#define NET_SET_CHARGE_AF   45 /* Uint8: chargeSidekickAutofire                                    */
#define NET_SET_TWIDDLE     46 /* Uint8: debugTwiddleSpecial; byte 47 reserved                      */
COMPILE_TIME_ASSERT(net_settings_block_fits,
                    NET_SET_TWIDDLE + 2 == NETWORK_SETTINGS_SIZE);

// The epdiff word holds eight two-bit entries. Later entries use flags2; keep
// the split in sync when extending the table.
#define NET_SET_EPDIFF_PACKED   8  /* entries 0..7 pack into the byte-2 word, two bits each */
#define NET_SET_EPDIFF_TAIL_BIT 4  /* entries 9 up run from this flags2 bit, two bits each  */
#define NET_SET_GUIDED_AIM_BIT 15  /* guidedShotScreenAim; the epdiff tail grows up to it   */
COMPILE_TIME_ASSERT(net_settings_epdiff_fits,
                    NET_SET_EPDIFF_TAIL_BIT + 2 * (EDW_COUNT - NET_SET_EPDIFF_PACKED - 1)
                    <= NET_SET_GUIDED_AIM_BIT);

/* Where epdiff entry i (i >= NET_SET_EPDIFF_PACKED) sits in flags2. Entry 8 keeps its original
 * bits 1-2, below the shot-hitbox flag; the rest run from NET_SET_EPDIFF_TAIL_BIT up. */
static int network_epdiff_tail_shift(int i)
{
	return (i == NET_SET_EPDIFF_PACKED)
	       ? 1
	       : NET_SET_EPDIFF_TAIL_BIT + 2 * (i - NET_SET_EPDIFF_PACKED - 1);
}

static Uint16 network_debug_flags_pack(void)
{
	Uint16 flags = 0;
	flags |= cheatInfiniteShields      ? 1 << 0 : 0;
	flags |= cheatInfiniteArmor        ? 1 << 1 : 0;
	flags |= cheatInfiniteGenerator    ? 1 << 2 : 0;
	flags |= cheatNoEnemyFire          ? 1 << 3 : 0;
	flags |= cheatInstantCharge        ? 1 << 4 : 0;
	flags |= cheatInfiniteSidekickAmmo ? 1 << 5 : 0;
	flags |= autoFireSpecial           ? 1 << 6 : 0;
	flags |= debugAutofireTwiddle      ? 1 << 7 : 0;
	flags |= debugToggleFire           ? 1 << 8 : 0;
	flags |= expertMode                ? 1 << 9 : 0;
	flags |= difficultyAdjust          ? 1 << 10 : 0;
	flags |= debugTwiddleTrigger       ? 1 << 11 : 0;
	flags |= constantPlay              ? 1 << 12 : 0;
	flags |= constantDie               ? 1 << 13 : 0;
	flags |= endlessCampaignMods       ? 1 << 14 : 0;
	return flags;
}

/* A menu update preserves a one-shot already pending locally. Initial session adoption replaces
 * the joiner's stale state completely. */
static void network_debug_flags_adopt(Uint16 flags, bool preserve_pending_trigger)
{
	cheatInfiniteShields      = (flags & (1 << 0)) != 0;
	cheatInfiniteArmor        = (flags & (1 << 1)) != 0;
	cheatInfiniteGenerator    = (flags & (1 << 2)) != 0;
	cheatNoEnemyFire          = (flags & (1 << 3)) != 0;
	cheatInstantCharge        = (flags & (1 << 4)) != 0;
	cheatInfiniteSidekickAmmo = (flags & (1 << 5)) != 0;
	autoFireSpecial           = (flags & (1 << 6)) != 0;
	debugAutofireTwiddle      = (flags & (1 << 7)) != 0;
	debugToggleFire           = (flags & (1 << 8)) != 0;
	expertMode               = (flags & (1 << 9)) != 0;
	difficultyAdjust          = (flags & (1 << 10)) != 0;
	debugTwiddleTrigger       = (flags & (1 << 11)) != 0
	                         || (preserve_pending_trigger && debugTwiddleTrigger);
	constantPlay              = (flags & (1 << 12)) != 0;
	constantDie               = (flags & (1 << 13)) != 0;

	/* The other half of endlessFxActive: with the two machines disagreeing on it, one flies the
	 * endless perks and modifiers in a campaign level and the other does not. Armed through the
	 * shared path, exactly as the debug row toggles it (mainint.c). */
	if (!endlessMode)
	{
		const bool campaignMods = (flags & (1 << 14)) != 0;
		if (campaignMods && !endlessCampaignMods)
			endlessCampaignModsArm();
		endlessCampaignMods = campaignMods;
	}
}

/* Host-authoritative settings that affect simulation or deterministic input. Rendering and
 * audio settings remain local. */
static bool settings_stashed = false;
static struct
{
	int  superSparkMode[SSW_COUNT];
	int  epDiffMode[EDW_COUNT];
	int  zicaLaserBase, zicaLaserLength;
	bool zicaLaserLock, zicaLaserBuff;
	int  wallopSecondBolt;
	bool chargeLaserCannon, restoreBaseDispensers, arcadeLifeBoost, arcadeRandomBalls;
	bool arcadeRearGunScale, centeredShotHitboxes, guidedShotScreenAim;
	int  xmasMode;
	JE_byte gameSpeed;
	JE_boolean arcadeSeparateMode;
	bool timedBattle;
	int  battleLevel;
	JE_boolean expertMode;
	int  expert[NETWORK_EXPERT_SLOTS];
	bool cheatInfiniteShields;
	bool cheatInfiniteArmor;
	bool cheatInfiniteGenerator;
	bool cheatNoEnemyFire;
	bool cheatInstantCharge;
	bool cheatInfiniteSidekickAmmo;
	bool autoFireSpecial;
	bool debugAutofireTwiddle;
	bool debugToggleFire;
	bool difficultyAdjust;
	bool debugTwiddleTrigger;
	bool constantPlay;
	bool constantDie;
	JE_byte noclipMode;
	JE_byte chargeSidekickAutofire;
	JE_byte debugTwiddleSpecial;
}
settings_local;

int network_settings_pack(Uint8 *buf)
{
	Uint16 spark = 0;  // SSW_COUNT(4) x 2 bits
	for (int i = SSW_COUNT - 1; i >= 0; --i)
		spark = (spark << 2) | (superSparkMode[i] & 3);

	Uint16 epdiff = 0;  // the first NET_SET_EPDIFF_PACKED(8) entries x 2 bits; the rest ride flags2
	for (int i = NET_SET_EPDIFF_PACKED - 1; i >= 0; --i)
		epdiff = (epdiff << 2) | (epDiffMode[i] & 3);

	// Destruct brings its own rollback and its own recovery (destruct_rollback.c), so both rows
	// mean the same thing for every game type.
	const bool rollback_applies = net_rollback;
	const bool recovery_applies = net_desync_recovery && rollback_applies;

	Uint16 flags = 0;
	flags |= zicaLaserLock         ? 1 << 0 : 0;
	flags |= zicaLaserBuff         ? 1 << 1 : 0;
	flags |= chargeLaserCannon     ? 1 << 2 : 0;
	flags |= restoreBaseDispensers ? 1 << 3 : 0;
	flags |= rollback_applies      ? 1 << 4 : 0;  // rollback vs lockstep; host decides
	// The ship-physics tail is sim code (see JE_playerMovement's vt_sim gate),
	// so the host's smooth-motion choice binds the session.
	flags |= (vt_ship && smoothMotion && smoothScroll != 0) ? 1 << 5 : 0;
	flags |= recovery_applies      ? 1 << 6 : 0;  // desync recovery; host decides
	flags |= arcadeLifeBoost       ? 1 << 7 : 0;
	flags |= arcadeRandomBalls     ? 1 << 8 : 0;
	flags |= coopSharedCredit      ? 1 << 9 : 0;  // co-op credit sharing; host decides
	flags |= coopDoubleEarnings     ? 1 << 10 : 0; // ...and whether Individual pays combat cash twice
	flags |= arcadeSeparateShips    ? 1 << 11 : 0; // Arcade: two Separate personal ships; host decides
	flags |= arcadeRearGunScale     ? 1 << 12 : 0; // ...and whether lives raise the rear gun
	// Arcade's Timed Battle: the shape, and which of the three levels, in the last three bits.
	flags |= network_timed_battle() ? 1 << 13 : 0;
	flags |= (Uint16)((network_host_battle_level - 1) & 3) << 14;

	SDLNet_Write16(spark,                    &buf[0]);
	SDLNet_Write16(epdiff,                   &buf[2]);
	SDLNet_Write16(flags,                    &buf[4]);
	SDLNet_Write16(zicaLaserBase,            &buf[6]);
	SDLNet_Write16(zicaLaserLength,          &buf[8]);
	SDLNet_Write16(wallopSecondBolt,         &buf[10]);
	SDLNet_Write16((Uint16)(xmasMode + 1),   &buf[12]);  // xmasMode is -1..1; bias to 0..2
	SDLNet_Write16(gameSpeed,                &buf[14]);

	// Not a setting: our snapshot layout, for the peer to compare against its own.
	SDLNet_Write32(rollback_layout_fingerprint(),   &buf[16]);
	SDLNet_Write32((Uint32)rollback_state_size(),   &buf[20]);

	/* Expert Mode and its tunables. The flag rides the save record and the multipliers ride each
	 * machine's own config, and until this went in nothing published either at connect time: only
	 * the debug block did, and only when somebody opened that menu. Two players who had once set
	 * a different Boss HP therefore started a campaign fighting bosses with different health. */
	Uint16 flags2 = expertMode ? 1 << 0 : 0;
	for (int i = NET_SET_EPDIFF_PACKED; i < EDW_COUNT; ++i)
		flags2 |= (Uint16)(epDiffMode[i] & 3) << network_epdiff_tail_shift(i);
	flags2 |= centeredShotHitboxes ? 1 << 3 : 0;  // where both shot loops take a hit from
	flags2 |= guidedShotScreenAim ? 1 << NET_SET_GUIDED_AIM_BIT : 0;  // which x a guided shot chases
	SDLNet_Write16(flags2, &buf[NET_SET_FLAGS2]);
	for (int i = 0; i < NETWORK_EXPERT_SLOTS; ++i)
		SDLNet_Write16((Uint16)(i < expertSettingsCount ? *expertSettings[i].value : 0),
		               &buf[NET_SET_EXPERT + i * 2]);

	/* Debug Mode and the player-facing Sidekick Autofire option are all simulation state. The
	 * ordinary debug packet only publishes edits made after its menu opens, so this initial copy
	 * closes the gap for two machines arriving with different saved or previous-game values. */
	SDLNet_Write16(network_debug_flags_pack(), &buf[NET_SET_DEBUG_FLAGS]);
	buf[NET_SET_NOCLIP]    = noclipMode;
	buf[NET_SET_CHARGE_AF] = chargeSidekickAutofire;
	buf[NET_SET_TWIDDLE]   = debugTwiddleSpecial;
	buf[NET_SET_TWIDDLE + 1] = 0;

	return NETWORK_SETTINGS_SIZE;
}

/* Both peers compare layouts because the host streams state and the joiner adopts it. A mismatch
 * disables recovery but retains desync detection. */
void network_settings_check_layout(const Uint8 *buf)
{
	const Uint32 their_fp   = SDLNet_Read32(&buf[16]);
	const Uint32 their_size = SDLNet_Read32(&buf[20]);
	const Uint32 our_fp     = rollback_layout_fingerprint();
	const Uint32 our_size   = (Uint32)rollback_state_size();

	if (their_fp == our_fp && their_size == our_size)
		return;

	nrb_set_session_recovery(false);

	char line[256];
	snprintf(line, sizeof(line),
	         "desync recovery unavailable this session: peer state %lu bytes / layout %08x, "
	         "ours %lu / %08x (different build, or PC<->console).  A desync will be reported "
	         "and played through rather than repaired.",
	         (unsigned long)their_size, (unsigned)their_fp,
	         (unsigned long)our_size, (unsigned)our_fp);
	crashlog_netlog_line("NETWORK LAYOUT MISMATCH", line);
}

static void network_settings_stash(void)
{
	if (settings_stashed)
		return;

	memcpy(settings_local.superSparkMode, superSparkMode, sizeof(superSparkMode));
	memcpy(settings_local.epDiffMode, epDiffMode, sizeof(epDiffMode));
	settings_local.zicaLaserBase        = zicaLaserBase;
	settings_local.zicaLaserLength      = zicaLaserLength;
	settings_local.zicaLaserLock        = zicaLaserLock;
	settings_local.zicaLaserBuff        = zicaLaserBuff;
	settings_local.wallopSecondBolt     = wallopSecondBolt;
	settings_local.chargeLaserCannon    = chargeLaserCannon;
	settings_local.restoreBaseDispensers = restoreBaseDispensers;
	settings_local.arcadeLifeBoost      = arcadeLifeBoost;
	settings_local.arcadeRandomBalls    = arcadeRandomBalls;
	settings_local.arcadeRearGunScale   = arcadeRearGunScale;
	settings_local.centeredShotHitboxes = centeredShotHitboxes;
	settings_local.guidedShotScreenAim  = guidedShotScreenAim;
	settings_local.xmasMode             = xmasMode;
	settings_local.gameSpeed            = gameSpeed;
	// Session-scoped, so leaving a session has to put it back: a leftover Separate flag would
	// otherwise reshape the next LOCAL two-player arcade, which never armed it.
	settings_local.arcadeSeparateMode   = arcadeSeparateMode;
	// The joiner adopts the host's Timed Battle pick into these, so its own lobby choice -- which
	// is what the config file keeps -- has to survive a session spent racing somebody else's.
	settings_local.timedBattle          = network_host_timed_battle;
	settings_local.battleLevel          = network_host_battle_level;
	// The joiner adopts the host's Expert Mode and every multiplier behind it; those are this
	// machine's own config (and its own save's flag), so they go back when the session does.
	settings_local.expertMode           = expertMode;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		settings_local.expert[i] = *expertSettings[i].value;
	settings_local.cheatInfiniteShields      = cheatInfiniteShields;
	settings_local.cheatInfiniteArmor        = cheatInfiniteArmor;
	settings_local.cheatInfiniteGenerator    = cheatInfiniteGenerator;
	settings_local.cheatNoEnemyFire          = cheatNoEnemyFire;
	settings_local.cheatInstantCharge        = cheatInstantCharge;
	settings_local.cheatInfiniteSidekickAmmo = cheatInfiniteSidekickAmmo;
	settings_local.autoFireSpecial           = autoFireSpecial;
	settings_local.debugAutofireTwiddle      = debugAutofireTwiddle;
	settings_local.debugToggleFire           = debugToggleFire;
	settings_local.difficultyAdjust          = difficultyAdjust;
	settings_local.debugTwiddleTrigger       = debugTwiddleTrigger;
	settings_local.constantPlay              = constantPlay;
	settings_local.constantDie               = constantDie;
	settings_local.noclipMode                = noclipMode;
	settings_local.chargeSidekickAutofire    = chargeSidekickAutofire;
	settings_local.debugTwiddleSpecial       = debugTwiddleSpecial;
	settings_stashed = true;
}

/* Every session flag the settings block carries, armed from this machine's own config. The
 * host's own arming and the joiner's adoption must cover the same set: a flag the block
 * carries but the host never arms locally splits the two simulations at the first place it
 * pays out. Double Earnings was exactly that, and every pickup desynced by its own value. */
void network_arm_local_session(void)
{
	// Arming writes session state the local preferences have to survive, so take the same
	// snapshot the joiner's adopt path takes. Both are undone by network_settings_restore.
	network_settings_stash();

	// Same rule the settings block packs: every game type honours both netcode rows.
	const bool rollback_applies = net_rollback;

	nrb_set_session_mode(rollback_applies);
	nrb_set_session_vt(vt_ship && smoothMotion && smoothScroll != 0);
	nrb_set_session_recovery(net_desync_recovery && rollback_applies);
	coop_set_session_shared_credit(coopSharedCredit);
	coop_set_session_double_earnings(coopDoubleEarnings);
	arcadeSeparateMode = arcadeSeparateShips;
}

// Apply the host's lobby speed for ordinary sessions and restore the local preference afterward.
// Command-line and Destruct sessions use Normal so both peers advance at the same rate.
void network_settings_apply_session_speed(void)
{
	const bool host_picks_speed = network_from_lobby && network_is_host
	                           && network_game_type != NETWORK_GAME_DESTRUCT;

	network_settings_stash();
	gameSpeed = host_picks_speed ? (JE_byte)network_host_game_speed : 4;
	JE_initProcessorType();
	JE_setNewGameSpeed();
}

int network_settings_adopt(const Uint8 *buf)
{
	network_settings_stash();

	Uint16 spark  = SDLNet_Read16(&buf[0]);
	Uint16 epdiff = SDLNet_Read16(&buf[2]);
	Uint16 flags  = SDLNet_Read16(&buf[4]);

	for (int i = 0; i < SSW_COUNT; ++i, spark >>= 2)
	{
		superSparkMode[i] = spark & 3;
		if (superSparkMode[i] >= SUPER_SPARKS_COUNT)
			superSparkMode[i] = SUPER_SPARKS_AUTO;
	}
	for (int i = 0; i < NET_SET_EPDIFF_PACKED; ++i, epdiff >>= 2)
	{
		epDiffMode[i] = epdiff & 3;
		if (epDiffMode[i] >= EPDIFF_MODE_COUNT)
			epDiffMode[i] = EPDIFF_AUTO;
	}

	zicaLaserLock         = (flags & (1 << 0)) != 0;
	zicaLaserBuff         = (flags & (1 << 1)) != 0;
	chargeLaserCannon     = (flags & (1 << 2)) != 0;
	restoreBaseDispensers = (flags & (1 << 3)) != 0;
	arcadeLifeBoost       = (flags & (1 << 7)) != 0;
	arcadeRandomBalls     = (flags & (1 << 8)) != 0;

	// Netcode mode is a session property, not a config setting: the joiner's own
	// net_rollback preference is left untouched and restored semantics don't apply.
	nrb_set_session_mode((flags & (1 << 4)) != 0);
	nrb_set_session_vt((flags & (1 << 5)) != 0);
	nrb_set_session_recovery((flags & (1 << 6)) != 0);
	coop_set_session_shared_credit((flags & (1 << 9)) != 0);
	coop_set_session_double_earnings((flags & (1 << 10)) != 0);
	arcadeSeparateMode = (flags & (1 << 11)) != 0;
	arcadeRearGunScale = (flags & (1 << 12)) != 0;

	// A hostile packet could name a battle that does not exist, and the level number indexes
	// both the name table and the episode script's jump list, so clamp it into range.
	network_host_timed_battle = (flags & (1 << 13)) != 0;
	network_host_battle_level = (int)((flags >> 14) & 3) + 1;
	if (network_host_battle_level > NET_TIMED_BATTLE_LEVELS)
		network_host_battle_level = 1;

	zicaLaserBase    = SDLNet_Read16(&buf[6]);
	zicaLaserLength  = SDLNet_Read16(&buf[8]);
	wallopSecondBolt = SDLNet_Read16(&buf[10]);
	xmasMode         = (int)SDLNet_Read16(&buf[12]) - 1;
	gameSpeed        = (JE_byte)SDLNet_Read16(&buf[14]);

	// Clamp everything: a malformed or hostile packet must not index past an array.
	if (zicaLaserBase < 0 || zicaLaserBase >= ZICA_BASE_COUNT)
		zicaLaserBase = ZICA_BASE_AUTO;
	if (zicaLaserLength < 0 || zicaLaserLength >= ZICA_LEN_COUNT)
		zicaLaserLength = ZICA_LEN_SHORT;
	if (wallopSecondBolt < 0 || wallopSecondBolt >= SUPER_SPARKS_COUNT)
		wallopSecondBolt = SUPER_SPARKS_AUTO;
	if (xmasMode < -1 || xmasMode > 1)
		xmasMode = -1;
	if (gameSpeed < 1 || gameSpeed > 5)
		gameSpeed = 4;

	// Expert Mode and its tunables, clamped by the same table-driven pass the debug block uses:
	// every one of these multiplies enemy health, weapon energy or a price, so a hostile packet
	// must not be able to name 65535 of anything.
	const Uint16 flags2 = SDLNet_Read16(&buf[NET_SET_FLAGS2]);
	expertMode = (flags2 & 1) != 0;
	for (int i = NET_SET_EPDIFF_PACKED; i < EDW_COUNT; ++i)
	{
		epDiffMode[i] = (flags2 >> network_epdiff_tail_shift(i)) & 3;
		if (epDiffMode[i] >= EPDIFF_MODE_COUNT)
			epDiffMode[i] = EPDIFF_AUTO;
	}
	centeredShotHitboxes = (flags2 & (1 << 3)) != 0;
	guidedShotScreenAim = (flags2 & (1 << NET_SET_GUIDED_AIM_BIT)) != 0;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = (int)SDLNet_Read16(&buf[NET_SET_EXPERT + i * 2]);
	clamp_expert_settings();

	network_debug_flags_adopt(SDLNet_Read16(&buf[NET_SET_DEBUG_FLAGS]), false);
	noclipMode = buf[NET_SET_NOCLIP] % NOCLIP_NUM;
	chargeSidekickAutofire = buf[NET_SET_CHARGE_AF] % CHARGE_AUTOFIRE_NUM;
	debugTwiddleSpecial = (buf[NET_SET_TWIDDLE] <= SPECIAL_NUM) ? buf[NET_SET_TWIDDLE] : 0;

	return NETWORK_SETTINGS_SIZE;
}

// The Endless lobby block travels beside the settings block and binds the run the same way.
COMPILE_TIME_ASSERT(net_endless_seed_fits, NET_ENDLESS_SEED_MAX == ENDLESS_SEED_MAXLEN);

void network_endless_adopt(const Uint8 *buf)
{
	network_host_endless_run_mode = buf[0];
	network_host_endless_chooser = buf[1];
	network_host_endless_combo_shared = buf[2] != 0;
	network_host_endless_base_rule = buf[3];
	memcpy(network_endless_session_seed, &buf[4], NET_ENDLESS_SEED_MAX);
	network_endless_session_seed[NET_ENDLESS_SEED_MAX - 1] = '\0';

	if (network_host_endless_run_mode < 0 || network_host_endless_run_mode >= ENDLESS_RUNMODE_COUNT)
		network_host_endless_run_mode = ENDLESS_RUNMODE_STANDARD;
	if (network_host_endless_chooser < 0 || network_host_endless_chooser >= ENDLESS_PICK_COUNT)
		network_host_endless_chooser = ENDLESS_PICK_HOST;
	if (network_host_endless_base_rule < 0
	    || network_host_endless_base_rule >= ENDLESS_BASE_RULE_COUNT)
	{
		network_host_endless_base_rule = ENDLESS_BASE_VARIED;
	}

	// Only printable ASCII reaches the seed hash, so a hostile packet cannot smuggle control
	// characters onto the seed-name line the run-over screen prints.
	for (int i = 0; i < NET_ENDLESS_SEED_MAX; ++i)
	{
		const unsigned char c = (unsigned char)network_endless_session_seed[i];
		if (c != '\0' && (c < 32 || c >= 127))
			network_endless_session_seed[i] = '?';
	}
}

/* Resolve a random Endless seed before the host sends it. Keep the lobby field
 * blank so the next session rolls again. */
void network_endless_session_begin(void)
{
	SDL_strlcpy(network_endless_session_seed, network_host_endless_seed,
	            sizeof(network_endless_session_seed));

	if (network_endless_session_seed[0] == '\0')
		snprintf(network_endless_session_seed, sizeof(network_endless_session_seed), "%lu",
		         (unsigned long)(1u + mt_rand() % 999999999u));
}

/* Roll the Destruct terrain seed, host side, before the connect packet carries it. Per session,
 * so two matches hosted back to back fight different maps. */
void network_destruct_session_begin(void)
{
	network_destruct_session_seed = (Uint32)mt_rand();
}

static Uint16 network_shop_sequence;
static Uint16 network_shop_peer_sequence;
static Uint16 network_shop_save_request;
static bool network_shop_peer_ready;
static bool network_shop_peer_lock;
static bool network_shop_local_ready;
static bool network_shop_local_locked;
static bool network_shop_save_ready;
static bool network_shop_active;
static Uint32 network_shop_beat_at;
#define NET_SHOP_BEAT 400   // ms between re-announcements while waiting on the peer

// The level the host committed to when it left the outpost.  Held rather than applied: writing
// jumpSection straight out of the packet ended the joiner's outpost visit the moment the host
// picked a planet, mid-purchase.  network_shop_adopt_host_level() applies it once the joiner is
// done too, which is also where the host's pick wins a disagreement.
static bool network_shop_host_committed;
static JE_byte network_shop_host_level;

// Endless: the sector index the peer published, or -1 while it has committed to none.
static int network_shop_peer_pick = -1;

/* DONE and LOCK describe the sender's state at send time rather than an event, so every shop
 * packet carries both and the receiver assigns them. That is what lets a commit be withdrawn. */
enum
{
	SHOP_SYNC_DONE = 1 << 0,
	SHOP_SYNC_SAVE_REQUEST = 1 << 1,
	SHOP_SYNC_SAVE_ACK = 1 << 2,
	SHOP_SYNC_TRANSACTION = 1 << 3,
	SHOP_SYNC_LOCK = 1 << 4,
	SHOP_SYNC_HELLO = 1 << 5,   // "I just opened an outpost; tell me where you are"
};

static int network_shop_pack_items(Uint8 *buf, const PlayerItems *items)
{
	int n = 0;
	buf[n++] = items->ship;
	buf[n++] = items->generator;
	buf[n++] = items->shield;
	for (uint i = 0; i < COUNTOF(items->weapon); ++i)
	{
		buf[n++] = items->weapon[i].id;
		buf[n++] = items->weapon[i].power;
	}
	buf[n++] = items->sidekick[0];
	buf[n++] = items->sidekick[1];
	buf[n++] = items->special;
	buf[n++] = items->sidekick_series;
	buf[n++] = items->sidekick_level;
	buf[n++] = items->super_arcade_mode;
	return n;
}

static int network_shop_unpack_items(PlayerItems *items, const Uint8 *buf)
{
	int n = 0;
	items->ship = buf[n++];
	items->generator = buf[n++];
	items->shield = buf[n++];
	for (uint i = 0; i < COUNTOF(items->weapon); ++i)
	{
		items->weapon[i].id = buf[n++];
		items->weapon[i].power = buf[n++];
	}
	items->sidekick[0] = buf[n++];
	items->sidekick[1] = buf[n++];
	items->special = buf[n++];
	items->sidekick_series = buf[n++];
	items->sidekick_level = buf[n++];
	items->super_arcade_mode = buf[n++];
	return n;
}

static Uint16 network_shop_send_packet(Uint16 flags, Uint16 acknowledge)
{
	if (!isNetworkGame || !coop_mode_active() || thisPlayerNum < 1 || thisPlayerNum > 2)
		return 0;

	const Player *const this_player = &player[thisPlayerNum - 1];
	const Uint16 sequence = ++network_shop_sequence;

	// Ride our current rendezvous state on every packet, whatever it was sent for.
	if (network_shop_local_ready)
		flags |= SHOP_SYNC_DONE;
	if (network_shop_local_locked)
		flags |= SHOP_SYNC_LOCK;

	network_prepare(PACKET_SHOP_SYNC);
	SDLNet_Write16(thisPlayerNum, &packet_out_temp->data[4]);
	SDLNet_Write16(sequence, &packet_out_temp->data[6]);
	SDLNet_Write16(flags, &packet_out_temp->data[8]);
	// Endless publishes the chosen sector index here; Campaign publishes the level it jumped to.
	SDLNet_Write16(coopEndlessMode ? (Uint16)(endlessCoopCourse + 1) : mainLevel,
	               &packet_out_temp->data[10]);
	SDLNet_Write16(jumpSection ? 1 : 0, &packet_out_temp->data[12]);
	net_bytes_write64((Uint64)this_player->cash, &packet_out_temp->data[14]);
	SDLNet_Write16((Uint16)this_player->weapon_mode, &packet_out_temp->data[22]);
	SDLNet_Write16(acknowledge, &packet_out_temp->data[24]);
	int len = 26 + network_shop_pack_items(&packet_out_temp->data[26], &this_player->items);
	if (coopEndlessMode)
		len += endlessPackPlayerBlock(&packet_out_temp->data[len], thisPlayerNum - 1);
	// The save acknowledgement carries this machine's own outpost, its stock rows and the
	// stream they came off, so the saver stores both halves in its own file; see "Online
	// saves" in doc/notes.md.
	if ((flags & SHOP_SYNC_SAVE_ACK) && coopEndlessMode)
		len += endlessPackOwnOutpost(&packet_out_temp->data[len]);
	network_send(len);
	return sequence;
}

void network_shop_begin(void)
{
	/* A level that ended on the peer's quit leaves the notice at the reliable head for its
	 * handler (nrb_peer_left_level). Both co-op modes reopen the outpost after a quit, so this
	 * is that handler; the pumps below read only shop traffic and would leave it in front of
	 * everything the peer sends from now on. */
	network_quit_notice_retire();

	network_shop_save_request = 0;
	network_shop_peer_ready = false;
	network_shop_peer_lock = false;
	network_shop_local_ready = false;
	network_shop_local_locked = false;
	network_shop_save_ready = false;
	network_shop_host_committed = false;
	network_shop_peer_pick = -1;
	network_shop_beat_at = SDL_GetTicks();
	network_shop_active = isNetworkGame && coop_mode_active();
	if (isNetworkGame && coop_mode_active())
	{
		// Everything above forgets what the peer had told us, which is right for a new visit and
		// wrong for a peer that committed while we were still on the way here. HELLO asks them to
		// say it again, so the reset cannot swallow a commit that was announced exactly once.
		network_shop_local_ready = false;
		network_shop_local_locked = false;
		network_shop_send_packet(SHOP_SYNC_HELLO, 0);
	}
}

/* Re-announce DONE, LOCK, and the route while waiting. These are state fields, and a peer clears
 * its view when opening an outpost; rate limiting keeps only one restatement in flight. */
void network_shop_keepalive(void)
{
	if (!isNetworkGame || !coop_mode_active() || thisPlayerNum < 1 || thisPlayerNum > 2)
		return;

	// Never more than one of these in flight. The reliable queue is 16 deep and overflowing it
	// takes the session down, so a partner parked in a screen that does not drain the queue (the
	// weapon editor, ship specs, a save prompt) must not be beaten at until they come back.
	if (!network_is_sync())
		return;

	const Uint32 now = SDL_GetTicks();
	if (now - network_shop_beat_at < NET_SHOP_BEAT)
		return;

	network_shop_beat_at = now;
	network_shop_send_packet(0, 0);
}

void network_shop_send_state(bool done)
{
	if (!isNetworkGame || !coop_mode_active() || thisPlayerNum < 1 || thisPlayerNum > 2)
		return;

	network_shop_local_ready = done;
	if (!done)
		network_shop_local_locked = false;
	network_shop_send_packet(0, 0);
}

// Step two of the outpost rendezvous, which is what keeps a withdrawal from racing a departure.
// See "Leaving the outpost" in doc/notes.md.
void network_shop_set_locked(bool locked)
{
	if (!isNetworkGame || !coop_mode_active() || thisPlayerNum < 1 || thisPlayerNum > 2)
		return;

	network_shop_local_locked = locked;
	network_shop_send_packet(0, 0);
}

bool network_shop_peer_locked(void)
{
	return network_shop_peer_lock;
}

void network_shop_send_transaction(void)
{
	if (!isNetworkGame || !coop_mode_active() || thisPlayerNum < 1 || thisPlayerNum > 2)
		return;

	network_shop_send_packet(SHOP_SYNC_TRANSACTION, 0);
}

/* Custom weapon design exchange. A design is chunked over the reliable channel and the receiver
 * answers a completed generation with a chunk count of zero; see "Custom weapons online" in
 * doc/notes.md for why transport delivery alone is not enough. */
#define NCW_OWNER      4    /* Uint8:  publishing player number, 1 or 2      */
#define NCW_GEN        6    /* Uint16: publication, so a stale stream is dropped */
#define NCW_CHUNK      8    /* Uint16: chunk index                           */
#define NCW_COUNT     10    /* Uint16: chunk total, or 0 for an acknowledgement */
#define NCW_LEN       12    /* Uint16: payload bytes in this chunk            */
#define NCW_HDR       14
#define NCW_PAYLOAD   (NET_PACKET_SIZE - NCW_HDR)
#define NCW_ATTEMPTS   3    /* whole-stream retries before giving up          */
#define NCW_ATTEMPT_MS 6000 /* ms to get one attempt acknowledged             */

static Uint16 network_custom_gen;
static Uint32 network_custom_out_hash;  // last design the peer took; 0 = nothing published yet
static bool   network_custom_acked;
static Uint16 network_custom_in_gen;
static Uint32 network_custom_in_have;
static Uint32 network_custom_in_count;
static int    network_custom_in_owner = -1;
static size_t network_custom_in_len;
static Uint8 *network_custom_in_buf;
static Uint8 *network_custom_in_seen;

static void network_custom_in_reset(void)
{
	network_custom_in_owner = -1;
	network_custom_in_count = 0;
	network_custom_in_have = 0;
	network_custom_in_len = 0;
	free(network_custom_in_buf);
	free(network_custom_in_seen);
	network_custom_in_buf = NULL;
	network_custom_in_seen = NULL;
}

void network_custom_weapon_reset(void)
{
	network_custom_in_reset();
	network_custom_out_hash = 0;
	network_custom_acked = false;
}

static void network_custom_send_ack(Uint16 gen)
{
	network_prepare(PACKET_CUSTOM_WEAPON);
	packet_out_temp->data[NCW_OWNER] = (Uint8)thisPlayerNum;
	packet_out_temp->data[NCW_OWNER + 1] = 0;
	SDLNet_Write16(gen, &packet_out_temp->data[NCW_GEN]);
	SDLNet_Write16(0, &packet_out_temp->data[NCW_CHUNK]);
	SDLNet_Write16(0, &packet_out_temp->data[NCW_COUNT]);
	SDLNet_Write16(0, &packet_out_temp->data[NCW_LEN]);
	network_send(NCW_HDR);
}

static bool network_custom_weapon_receive(void)
{
	const int len = packet_in[0]->len;
	if (len < NCW_HDR)
	{
		network_update();
		return true;
	}

	const uint   sender = packet_in[0]->data[NCW_OWNER];
	const Uint16 gen    = SDLNet_Read16(&packet_in[0]->data[NCW_GEN]);
	const Uint32 chunk  = SDLNet_Read16(&packet_in[0]->data[NCW_CHUNK]);
	const Uint32 count  = SDLNet_Read16(&packet_in[0]->data[NCW_COUNT]);
	const Uint32 plen   = SDLNet_Read16(&packet_in[0]->data[NCW_LEN]);

	if (sender < 1 || sender > 2 || sender == thisPlayerNum)
	{
		network_update();
		return true;
	}

	if (count == 0)  // the peer took the generation we are publishing
	{
		if (gen == network_custom_gen)
			network_custom_acked = true;
		network_update();
		return true;
	}

	if (chunk >= count || plen > NCW_PAYLOAD || (Uint32)len < NCW_HDR + plen ||
	    (size_t)count * NCW_PAYLOAD > CUSTOM_WEAPON_WIRE_MAX)
	{
		network_update();
		return true;
	}

	if (network_custom_in_buf == NULL || network_custom_in_gen != gen ||
	    network_custom_in_count != count || network_custom_in_owner != (int)sender - 1)
	{
		network_custom_in_reset();
		network_custom_in_buf = calloc((size_t)count, NCW_PAYLOAD);
		network_custom_in_seen = calloc((size_t)count, 1);
		if (network_custom_in_buf == NULL || network_custom_in_seen == NULL)
		{
			network_custom_in_reset();
			network_update();
			return true;
		}
		network_custom_in_gen = gen;
		network_custom_in_count = count;
		network_custom_in_owner = (int)sender - 1;
	}

	memcpy(network_custom_in_buf + (size_t)chunk * NCW_PAYLOAD, &packet_in[0]->data[NCW_HDR], plen);
	if (chunk == count - 1)
		network_custom_in_len = (size_t)chunk * NCW_PAYLOAD + plen;
	if (!network_custom_in_seen[chunk])
	{
		network_custom_in_seen[chunk] = 1;
		++network_custom_in_have;
	}

	if (network_custom_in_have >= count && network_custom_in_len > 0)
	{
		const int owner = network_custom_in_owner;
		const size_t total = network_custom_in_len;
		Uint8 *const stream = network_custom_in_buf;

		network_custom_in_buf = NULL;  // the adopt call owns the bytes
		if (!customWeaponAdoptDesign(owner, stream, total))
		{
			crashlog_netlog_line("CUSTOM WEAPON REFUSED",
			                     "the peer's design did not decode; its ship keeps the "
			                     "placeholder weapon and the two machines would disagree.");
		}
		free(stream);
		network_custom_in_reset();

		// Answer whether or not the design decoded: resending would produce the same bytes.
		network_custom_send_ack(gen);
	}

	network_update();
	return true;
}

/* Publish while the peer drains its inbound queue. Both machines pump during the outpost
 * rendezvous and Campaign resume. force=true covers a loaded custom slot when its editor toggle is
 * currently off. */
static void network_custom_weapon_publish_internal(bool force)
{
	// Outside resume, nothing can newly carry the weapon while the feature is off. Turning it on
	// and equipping goes through the next outpost rendezvous.
	if (!isNetworkGame || !coop_mode_active() || (!customWeaponEnabled && !force) ||
	    thisPlayerNum < 1 || thisPlayerNum > 2 || !network_peer_alive())
		return;

	Uint8 *const stream = malloc(CUSTOM_WEAPON_WIRE_MAX);
	if (stream == NULL)
		return;

	const size_t total = customWeaponSerializeDesign(stream, CUSTOM_WEAPON_WIRE_MAX);
	if (total == 0)
	{
		free(stream);
		return;
	}

	// FNV-1a over the encoded design: this runs on the way out of every outpost visit, and
	// most of those fly the same weapon as the last one.
	Uint32 hash = 2166136261u;
	for (size_t i = 0; i < total; ++i)
	{
		hash ^= stream[i];
		hash *= 16777619u;
	}
	if (hash == 0)
		hash = 1;  // 0 means "nothing published yet"
	if (hash == network_custom_out_hash)
	{
		free(stream);
		return;
	}

	const Uint32 chunks = (Uint32)((total + NCW_PAYLOAD - 1) / NCW_PAYLOAD);
	const Uint16 gen = ++network_custom_gen;
	network_custom_acked = false;
	bool peer_left = false;

	for (int attempt = 0; attempt < NCW_ATTEMPTS && !network_custom_acked && !peer_left; ++attempt)
	{
		const Uint32 started = SDL_GetTicks();
		Uint32 sent = 0;

		while (!network_custom_acked && SDL_GetTicks() - started < NCW_ATTEMPT_MS)
		{
			// Keep half the reliable queue free: transport acknowledgements arrive ahead of
			// consumption, and the shop's own traffic still has to fit alongside this.
			while (sent < chunks && network_ack_backlog() < NET_PACKET_QUEUE / 2)
			{
				const size_t from = (size_t)sent * NCW_PAYLOAD;
				const size_t plen = MIN(total - from, (size_t)NCW_PAYLOAD);

				network_prepare(PACKET_CUSTOM_WEAPON);
				packet_out_temp->data[NCW_OWNER] = (Uint8)thisPlayerNum;
				packet_out_temp->data[NCW_OWNER + 1] = 0;
				SDLNet_Write16(gen, &packet_out_temp->data[NCW_GEN]);
				SDLNet_Write16((Uint16)sent, &packet_out_temp->data[NCW_CHUNK]);
				SDLNet_Write16((Uint16)chunks, &packet_out_temp->data[NCW_COUNT]);
				SDLNet_Write16((Uint16)plen, &packet_out_temp->data[NCW_LEN]);
				memcpy(&packet_out_temp->data[NCW_HDR], stream + from, plen);
				network_send(NCW_HDR + (int)plen);
				++sent;
			}

			watchdog_heartbeat();
			service_SDL_events(false);
			network_check();

			// Drain our own inbound: the peer publishes from the same rendezvous, and its
			// acknowledgement is behind whatever it has already sent us.
			while (network_shop_pump() || network_debug_sync_pump(false))
				;

			/* Whatever the pumps left is stale or final: a trailing handshake duplicate would
			 * block the acknowledgement for the whole window, and a quit means it never comes
			 * (the packet stays queued for the quit handler, as everywhere else). */
			if (network_inbound_head() == PACKET_CONNECT)
				network_update();
			else if (network_inbound_head() == PACKET_GAME_QUIT)
			{
				peer_left = true;
				break;
			}

			if (!network_peer_alive())
				break;

			SDL_Delay(4);
		}

		if (!network_peer_alive())
			break;
	}

	if (!network_custom_acked)
	{
		crashlog_netlog_line("CUSTOM WEAPON NOT DELIVERED",
		                     "the peer never acknowledged the design; its copy of this ship "
		                     "keeps the placeholder weapon, which the canary reports as a desync.");
		network_custom_out_hash = 0;  // start over at the next outpost
	}
	else
	{
		network_custom_out_hash = hash;
	}

	free(stream);
}

void network_custom_weapon_publish(void)
{
	network_custom_weapon_publish_internal(false);
}

void network_custom_weapon_publish_resume(void)
{
	network_custom_weapon_publish_internal(true);
}

/* Endless run transfer. Same chunked shape as the custom weapon exchange above, and for the same
 * reason: transport delivery alone does not mean the peer has consumed the bytes. */
static Uint16 network_endless_gen;
static bool   network_endless_acked;

static void network_endless_send_ack(Uint16 gen)
{
	network_prepare(PACKET_ENDLESS_RUN);
	packet_out_temp->data[NCW_OWNER] = (Uint8)thisPlayerNum;
	packet_out_temp->data[NCW_OWNER + 1] = 0;
	SDLNet_Write16(gen, &packet_out_temp->data[NCW_GEN]);
	SDLNet_Write16(0, &packet_out_temp->data[NCW_CHUNK]);
	SDLNet_Write16(0, &packet_out_temp->data[NCW_COUNT]);
	SDLNet_Write16(0, &packet_out_temp->data[NCW_LEN]);
	network_send(NCW_HDR);
}

void network_endless_run_publish(void)
{
	if (!isNetworkGame || !coopEndlessMode || !network_peer_alive())
		return;

	Uint8 *const stream = malloc(ENDLESS_RUN_WIRE_MAX);
	if (stream == NULL)
		return;

	const size_t total = endlessRunSerialize(stream, ENDLESS_RUN_WIRE_MAX);
	if (total == 0)
	{
		free(stream);
		return;
	}

	const Uint32 chunks = (Uint32)((total + NCW_PAYLOAD - 1) / NCW_PAYLOAD);
	const Uint16 gen = ++network_endless_gen;
	network_endless_acked = false;

	// This wait spans the joiner's whole save apply, and the host arrives with the load
	// menu's fade-out still on the palette; without a redraw it reads as a hang.
	const Uint32 publish_start = SDL_GetTicks();
	bool overlay_drawn = false;
	bool peer_left = false;

	for (int attempt = 0; attempt < NCW_ATTEMPTS && !network_endless_acked && !peer_left; ++attempt)
	{
		const Uint32 started = SDL_GetTicks();
		Uint32 sent = 0;

		while (!network_endless_acked && SDL_GetTicks() - started < NCW_ATTEMPT_MS)
		{
			while (sent < chunks && network_ack_backlog() < NET_PACKET_QUEUE / 2)
			{
				const size_t from = (size_t)sent * NCW_PAYLOAD;
				const size_t plen = MIN(total - from, (size_t)NCW_PAYLOAD);

				network_prepare(PACKET_ENDLESS_RUN);
				packet_out_temp->data[NCW_OWNER] = (Uint8)thisPlayerNum;
				packet_out_temp->data[NCW_OWNER + 1] = 0;
				SDLNet_Write16(gen, &packet_out_temp->data[NCW_GEN]);
				SDLNet_Write16((Uint16)sent, &packet_out_temp->data[NCW_CHUNK]);
				SDLNet_Write16((Uint16)chunks, &packet_out_temp->data[NCW_COUNT]);
				SDLNet_Write16((Uint16)plen, &packet_out_temp->data[NCW_LEN]);
				memcpy(&packet_out_temp->data[NCW_HDR], stream + from, plen);
				network_send(NCW_HDR + (int)plen);
				++sent;
			}

			watchdog_heartbeat();
			service_SDL_events(false);
			network_check();

			/* The acknowledgement can only surface at the head of the ordered queue, so stale
			 * traffic ahead of it has to be retired or this spins out the whole window on a
			 * packet nobody claims. Endless packets are taken before the shop pump gets a look:
			 * the pump drops that type as a late resume duplicate, the acknowledgement included. */
			const Uint16 head = network_inbound_head();
			if (head == PACKET_ENDLESS_RUN)
			{
				if (packet_in[0]->len >= NCW_HDR
				    && SDLNet_Read16(&packet_in[0]->data[NCW_COUNT]) == 0
				    && SDLNet_Read16(&packet_in[0]->data[NCW_GEN]) == gen)
					network_endless_acked = true;
				network_update();  // the acknowledgement, or a stale generation's leftovers
			}
			else if (head == PACKET_GAME_QUIT)
			{
				/* The peer has left; nothing is coming. Leave the notice queued: it was
				 * acknowledged on arrival, and the quit handler still has to see it. */
				peer_left = true;
				break;
			}
			else if (head == PACKET_CONNECT)
			{
				network_update();  // trailing handshake duplicate (see network_sa_ship_peer)
			}
			else if (head != 0 && !network_debug_sync_pump(false))
			{
				(void)network_shop_pump();  // an early outpost hello ahead of the acknowledgement
			}

			if (!overlay_drawn && SDL_GetTicks() - publish_start > 700)
			{
				overlay_drawn = true;
				JE_clr256(VGAScreen);
				JE_drawNetworkNotice("Waiting for other player.");
				JE_showVGA();
				fade_palette(colors, 10, 0, 255);  // the load menu faded to black on its way out
			}

			if (!network_peer_alive())
				break;

			SDL_Delay(4);
		}

		if (!network_peer_alive())
			break;
	}

	if (!network_endless_acked)
	{
		crashlog_netlog_line("ENDLESS RUN NOT DELIVERED",
		                     "the joiner never acknowledged the resumed run; the two machines would "
		                     "start the session from different zones.");
	}

	free(stream);
}

/* Both ships going down at once ends the zone on both machines, but only one of them may decide
 * what happens next. The host picks and publishes; the joiner waits for the answer. Carried on the
 * run packet under a sentinel chunk count, so no separate message type is needed. */
#define NET_ENDLESS_DEATH_SENTINEL 0xffff
#define NET_ENDLESS_LEFT_LEVEL     0xfffe   /* "I am out of the level"; frees a peer still in it */

int network_endless_death_sync(int hostChoice)
{
	if (!isNetworkGame || !coopEndlessMode)
		return hostChoice;

	if (hostChoice >= 0)
	{
		network_prepare(PACKET_ENDLESS_RUN);
		packet_out_temp->data[NCW_OWNER] = (Uint8)thisPlayerNum;
		packet_out_temp->data[NCW_OWNER + 1] = 0;
		SDLNet_Write16(0, &packet_out_temp->data[NCW_GEN]);
		SDLNet_Write16((Uint16)hostChoice, &packet_out_temp->data[NCW_CHUNK]);
		SDLNet_Write16(NET_ENDLESS_DEATH_SENTINEL, &packet_out_temp->data[NCW_COUNT]);
		SDLNet_Write16(0, &packet_out_temp->data[NCW_LEN]);
		network_send(NCW_HDR);
		return hostChoice;
	}

	const Uint32 started = SDL_GetTicks();
	Uint32 announced = 0;
	while (SDL_GetTicks() - started < 60000)
	{
		watchdog_heartbeat();
		service_SDL_events(false);
		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();
		network_check();

		// Tell a host still in the level that this peer left. Keep at most one reliable
		// notice in flight so an unattended prompt cannot overflow the send queue.
		if (announced == 0
		    || (network_is_sync() && SDL_GetTicks() - announced > 5000))
		{
			announced = SDL_GetTicks();
			network_prepare(PACKET_ENDLESS_RUN);
			packet_out_temp->data[NCW_OWNER] = (Uint8)thisPlayerNum;
			packet_out_temp->data[NCW_OWNER + 1] = 0;
			SDLNet_Write16(0, &packet_out_temp->data[NCW_GEN]);
			SDLNet_Write16(0, &packet_out_temp->data[NCW_CHUNK]);
			SDLNet_Write16(NET_ENDLESS_LEFT_LEVEL, &packet_out_temp->data[NCW_COUNT]);
			SDLNet_Write16(0, &packet_out_temp->data[NCW_LEN]);
			network_send(NCW_HDR);
		}

		if (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_ENDLESS_RUN
		    && packet_in[0]->len >= NCW_HDR)
		{
			const Uint16 count = SDLNet_Read16(&packet_in[0]->data[NCW_COUNT]);
			if (count == NET_ENDLESS_DEATH_SENTINEL)
			{
				const int choice = SDLNet_Read16(&packet_in[0]->data[NCW_CHUNK]);
				network_update();
				return choice;
			}
			network_update();   // our own announcement echoed back, or a stale chunk
			continue;
		}

		network_update();
		if (!network_peer_alive())
			break;
	}

	crashlog_netlog_line("ENDLESS DEATH CHOICE TIMEOUT",
	                     "the host never published what to do after a shared death; this machine "
	                     "ended the run rather than guessing.");
	return -1;
}

/* Non-blocking Super Arcade pick state. The picker services the connection, and the reliable
 * channel retransmits each player's independent choice. */
static int  net_sa_ship_peer_pick = 0;
static bool net_sa_ship_peer_saw_us = false;

void network_sa_ship_reset(void)
{
	net_sa_ship_peer_pick = 0;
	net_sa_ship_peer_saw_us = false;
}

void network_player_look_publish(void)
{
	if (!isNetworkGame || !connected)
		return;

	const NetShipView view = netStyleLocalView();

	network_prepare(PACKET_PLAYER_LOOK);
	packet_out_temp->data[4] = (Uint8)thisPlayerNum;
	packet_out_temp->data[5] = (Uint8)netStyleSeatColor(netStyleLocalSeat());
	packet_out_temp->data[6] = view.opacity;
	packet_out_temp->data[7] = view.shipOpacity ? 1 : 0;
	packet_out_temp->data[8] = view.hpBars;
	network_send_no_ack(9);
}

void network_sa_ship_publish(int ship, bool seen_peer)
{
	if (!isNetworkGame || ship < 0 || ship > SA)
		return;

	network_prepare(PACKET_SA_SHIP);
	packet_out_temp->data[4] = (Uint8)thisPlayerNum;
	packet_out_temp->data[5] = (Uint8)ship;          // 0 = taken back
	packet_out_temp->data[6] = seen_peer ? 1 : 0;
	network_send(7);
}

bool network_sa_ship_peer_saw_us(void)
{
	return isNetworkGame && net_sa_ship_peer_saw_us;
}

/* Settle an Endless debug jump before folding the course, which would overwrite it. Both peers
 * skip the fold when a jump exists; the host wins simultaneous jumps. */
void network_endless_jump_publish(void)
{
	Uint8 block[ENDLESS_DEBUG_BLOCK_SIZE];
	JE_byte ep = 0, sec = 0, file = 0;

	const bool mine = endlessJumpPickGet(block);
	const bool myLevel = debugLevelPickGet(&ep, &sec, &file);

	// Publish immediately so a partner waiting for this player's course can leave that wait.
	if (!isNetworkGame || !coopEndlessMode || !mine || !myLevel)
		return;

	network_prepare(PACKET_ENDLESS_JUMP);
	packet_out_temp->data[4] = (Uint8)thisPlayerNum;
	packet_out_temp->data[5] = 1;   // a jump is only usable with its level, and both are here
	packet_out_temp->data[6] = ep;
	packet_out_temp->data[7] = sec;
	packet_out_temp->data[8] = file;
	packet_out_temp->data[9] = (Uint8)ENDLESS_DEBUG_BLOCK_SIZE;
	memcpy(&packet_out_temp->data[10], block, ENDLESS_DEBUG_BLOCK_SIZE);
	network_send(10 + ENDLESS_DEBUG_BLOCK_SIZE);
}

/* Adopt a queued jump without waiting. A true result tells both peers to skip course folding. */
bool network_endless_jump_poll(void)
{
	Uint8 block[ENDLESS_DEBUG_BLOCK_SIZE];
	const bool mine = endlessJumpPickGet(block);

	if (!isNetworkGame || !coopEndlessMode)
		return mine;

	network_check();

	// Only ever retire a jump announcement. Draining whatever else heads the queue is how the
	// outpost eats the packet a later wait is blocking on (see network_sa_ship_peer).
	if (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_ENDLESS_JUMP)
	{
		const bool ours = packet_in[0]->data[4] == (Uint8)thisPlayerNum;
		bool theirs = false;

		if (!ours && packet_in[0]->len > 10 && packet_in[0]->data[5] != 0)
		{
			// The host's jump wins if both jumped, so the two can never resolve it opposite ways.
			if (!mine || !network_is_host)
			{
				// Trust the length over the block's own length byte, so a truncated packet cannot
				// walk us past the end of what actually arrived.
				const size_t have = (size_t)packet_in[0]->len - 10;
				const size_t said = packet_in[0]->data[9];
				debugLevelPickApply(packet_in[0]->data[6], packet_in[0]->data[7],
				                    packet_in[0]->data[8]);
				endlessJumpPickApply(&packet_in[0]->data[10], said < have ? said : have);
			}
			theirs = true;
		}

		network_update();
		return mine || theirs;
	}

	return mine;
}

int network_sa_ship_peer(void)
{
	if (!isNetworkGame)
		return 0;

	// The handshake's trailing connect can be placed in the window to keep it gap-free
	// (see PACKET_CONNECT's connected path).  Stale by definition this deep in the session,
	// and this screen's wait is the head's only consumer, so throw it away or the pick
	// behind it never reaches the head.
	while (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CONNECT)
		network_update();

	// Only ever retire a ship announcement: draining whatever else heads the queue is how the
	// outpost used to eat the packet a later wait was blocking on. A truncated one is retired
	// too, adopted from nobody; left at the head it would block the queue for good.
	if (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_SA_SHIP)
	{
		if (packet_in[0]->len >= 6)
		{
			const uint sender = packet_in[0]->data[4];
			const int ship = packet_in[0]->data[5];
			// This indexes SAShip[] and SAWeapon. Zero retracts a previous pick.
			if (sender != thisPlayerNum && ship >= 0 && ship <= SA)
			{
				net_sa_ship_peer_pick = ship;
				net_sa_ship_peer_saw_us = packet_in[0]->len >= 7 && packet_in[0]->data[6] != 0;
			}
		}
		network_update();
	}

	return net_sa_ship_peer_pick;
}

bool network_endless_run_receive(Uint32 timeout_ms)
{
	if (!isNetworkGame || !coopEndlessMode)
		return false;

	Uint8 *buf = NULL;
	Uint8 *seen = NULL;
	Uint32 have = 0, count = 0;
	Uint16 gen = 0;
	size_t total = 0;
	bool done = false;

	const Uint32 started = SDL_GetTicks();
	while (!done && SDL_GetTicks() - started < timeout_ms)
	{
		watchdog_heartbeat();
		service_SDL_events(false);
		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		network_check();

		if (packet_in[0] == NULL || SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_ENDLESS_RUN
		    || packet_in[0]->len < NCW_HDR)
		{
			network_update();
			SDL_Delay(8);
			continue;
		}

		const Uint16 pgen  = SDLNet_Read16(&packet_in[0]->data[NCW_GEN]);
		const Uint32 chunk = SDLNet_Read16(&packet_in[0]->data[NCW_CHUNK]);
		const Uint32 total_chunks = SDLNet_Read16(&packet_in[0]->data[NCW_COUNT]);
		const Uint32 plen  = SDLNet_Read16(&packet_in[0]->data[NCW_LEN]);

		if (total_chunks == 0 || chunk >= total_chunks || plen > NCW_PAYLOAD
		    || (Uint32)packet_in[0]->len < NCW_HDR + plen
		    || (size_t)total_chunks * NCW_PAYLOAD > ENDLESS_RUN_WIRE_MAX)
		{
			network_update();
			continue;
		}

		if (buf == NULL || gen != pgen || count != total_chunks)
		{
			free(buf);
			free(seen);
			buf = calloc((size_t)total_chunks, NCW_PAYLOAD);
			seen = calloc((size_t)total_chunks, 1);
			if (buf == NULL || seen == NULL)
			{
				free(buf);
				free(seen);
				return false;
			}
			gen = pgen;
			count = total_chunks;
			have = 0;
			total = 0;
		}

		memcpy(buf + (size_t)chunk * NCW_PAYLOAD, &packet_in[0]->data[NCW_HDR], plen);
		if (chunk == count - 1)
			total = (size_t)chunk * NCW_PAYLOAD + plen;
		if (!seen[chunk])
		{
			seen[chunk] = 1;
			++have;
		}
		network_update();

		if (have >= count && total > 0)
		{
			// The adopt settles this seat's own rows itself: the record's partner half when
			// the save checkpointed one, a redeal from the restored stream otherwise.
			done = endlessRunAdopt(buf, total);
			network_endless_send_ack(gen);   // answer either way: a resend produces the same bytes
			break;
		}
	}

	free(buf);
	free(seen);
	return done;
}

/* Exchange one level-boundary marker and retire preceding control traffic. */
static void network_level_barrier(Uint16 packet_type, bool settle_outbound)
{
	network_prepare(packet_type);
	network_send(4);

	const Uint32 started = SDL_GetTicks();
	bool peer_ready = false;
	bool overlay_drawn = false;
	while (SDL_GetTicks() - started < NET_TIME_OUT && network_peer_alive())
	{
		watchdog_heartbeat();
		service_SDL_events(false);

		network_check();

		// A debug-menu edit can arrive ahead of the level marker on the ordered channel.
		// Apply it before advancing the queue to the marker behind it.
		if (network_debug_sync_pump(false))
			continue;

		/* So can the peer's last outpost transaction. The retire below would destroy it, and an
		 * acknowledged packet is never repeated, so their final purchase (a perk, a hull, a drive)
		 * would be missing from our mirror of their ship for the whole level. */
		if (network_shop_pump())
			continue;

		if (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == packet_type)
		{
			peer_ready = true;
			network_update();
		}
		else
		{
			// Every earlier control phase is complete at this boundary. Retire any delayed
			// packet at the head so it cannot hide the level marker behind it.
			network_update();
		}

		if (peer_ready && (!settle_outbound || network_is_sync()))
			break;

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		if (!overlay_drawn && SDL_GetTicks() - started > 700)
		{
			overlay_drawn = true;
			JE_drawNetworkNotice("Waiting for other player.");
			JE_showVGA();
		}

		if (!output_vsync)
			limit_render_fps();
		else
			SDL_Delay(1);
	}

	if (!peer_ready || (settle_outbound && !network_is_sync()))
	{
		fprintf(stderr, "error: level rendezvous timed out\n");
		network_tyrian_halt(2, false);
	}
}

/* Run the pre-load ready barrier and reset state queues. Paths that bypass
 * JE_itemScreen, such as Restart Zone, must call this themselves. */
void network_level_rendezvous(void)
{
	if (!isNetworkGame)
		return;

	network_level_barrier(PACKET_WAITING, true);
	network_state_reset();
}

/* Synchronize after both machines have loaded the map. This marker has its own type so a delayed
 * outpost or ready-card packet cannot release one player into the level. */
void network_level_loaded_rendezvous(void)
{
	if (!isNetworkGame)
		return;

	/* Receiving the peer's marker proves that machine has loaded. Our reliable marker may remain
	 * unacknowledged; gameplay network service keeps retrying it until the peer can leave too. */
	network_level_barrier(PACKET_LEVEL_READY, false);
}

/* The both-ready barrier the Destruct title and the Timed Battle card hold on, split into an
 * announcement and a poll rather than reusing network_level_rendezvous above: that one owns the
 * wait, and these screens keep drawing (and keep reporting which side is still to confirm). */
void network_ready_publish(bool ready)
{
	if (!isNetworkGame)
		return;

	network_prepare(PACKET_WAITING);
	packet_out_temp->data[4] = ready ? 1 : 0;
	network_send(5);  // PACKET_WAITING + the answer it carries
}

/* The departure gate. Announced by the machine that picked Start Level and withdrawn when it
 * presses Esc; the commit only follows once both are standing here. See "Outpost protocol" in
 * doc/notes.md. */
void network_depart_gate_publish(bool at_gate)
{
	if (!isNetworkGame)
		return;

	network_prepare(PACKET_DEPART_GATE);
	packet_out_temp->data[4] = at_gate ? 1 : 0;
	network_send(5);  // PACKET_DEPART_GATE + the answer it carries
}

int network_depart_gate_peer(void)
{
	if (!isNetworkGame || packet_in[0] == NULL
	    || SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_DEPART_GATE)
		return -1;

	const int at_gate = (packet_in[0]->len >= 5) ? (packet_in[0]->data[4] != 0 ? 1 : 0) : 1;
	network_update();   // consume it, or it heads the queue for the rest of the session
	return at_gate;
}

DepartGateStep network_depart_gate_step(bool esc_pressed, int peer_gate, Uint16 head)
{
	// Esc is read first: while it is still offered it outranks anything inbound, so the answer
	// does not depend on which arrived within the frame.
	if (esc_pressed)
		return DEPART_GATE_WITHDRAW;

	if (peer_gate > 0)
		return DEPART_GATE_GO;

	// A commit at the head proves the peer is at the gate and already past it. Left queued for
	// the commit wait, which is the phase that reads its payload.
	if (peer_gate < 0 && head == PACKET_WAITING)
		return DEPART_GATE_GO;

	return DEPART_GATE_WAIT;
}

DepartWaitStep network_depart_wait_step(int peer_gate, Uint16 head)
{
	// A withdrawal outranks a commit queued behind it, so the gate reopens.
	if (peer_gate == 0)
		return DEPART_WAIT_REOPENED;

	if (head == PACKET_WAITING)
		return DEPART_WAIT_DONE;

	return DEPART_WAIT_MORE;
}

int network_ready_peer(void)
{
	if (!isNetworkGame)
		return -1;

	network_check();

	// The handshake's trailing connect can be placed in the window to keep it gap-free (see
	// PACKET_CONNECT's connected path). Stale by the time the minigame starts, and this wait is
	// the head's only consumer, so throw it away or the announcement behind it never surfaces.
	while (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CONNECT)
		network_update();

	if (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_WAITING)
	{
		// Every other rendezvous sends a bare four-byte one, which can only mean "ready" here.
		const int ready = (packet_in[0]->len >= 5) ? (packet_in[0]->data[4] != 0 ? 1 : 0) : 1;
		network_update();   // consume it, or it heads the queue for the rest of the session
		return ready;
	}

	return -1;
}

void network_end_screen_rendezvous(bool local_dismissed)
{
	if (!isNetworkGame)
		return;

	/* Echoing the first dismissal makes either player's input authoritative while retaining a
	 * two-way reliable handshake. Each machine holds the screen until both announcements are in
	 * and its own is acknowledged, or until a silent peer reads as having finished and left. */
	bool local_initiated = local_dismissed;
	if (local_dismissed)
		network_ready_publish(true);
	bool peer_ready = false;
	bool complete = false;

	while (network_peer_alive())
	{
		// Renew Select and synthesize its key inside this draw-once wait loop.
		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);

		watchdog_heartbeat();
		push_joysticks_as_keyboard();
		service_SDL_events(true);
		poll_joysticks();

		if (!local_dismissed && (newkey || newmouse || joydown))
		{
			local_dismissed = true;
			local_initiated = true;
			network_ready_publish(true);
		}

		const int peer = network_ready_peer();
		if (peer >= 0)
		{
			peer_ready = peer != 0;
			if (peer_ready && !local_dismissed)
			{
				local_dismissed = true;
				network_ready_publish(true);
			}
		}

		if (local_dismissed && peer_ready)
		{
			if (network_is_sync())
			{
				complete = true;
				break;
			}

			/* Both announcements are in, so only the final acknowledgement of ours is owed. A
			 * live peer answers retransmits and keep-alives well inside this window; one that
			 * finished and closed its socket never will, and NET_TIME_OUT would hold the
			 * screen for it. */
			if (SDL_GetTicks() - last_in_tick > NET_DEPART_GRACE)
			{
				complete = true;
				break;
			}
		}

		if (!output_vsync)
			limit_render_fps();
		else
			SDL_Delay(1);
	}

	if (qa_net_gameplay_ticks > 0 && complete)
	{
		fprintf(stderr, "net gameplay: terminal rendezvous complete, dismissal=%s\n",
		        local_initiated ? "local" : "peer");
		fflush(stderr);
	}
}

bool network_shop_pump(void)
{
	if (!isNetworkGame || !coop_mode_active() || packet_in[0] == NULL)
		return false;

	if (SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CUSTOM_WEAPON)
		return network_custom_weapon_receive();

	// A late duplicate of the resume transfer: the run is already adopted, so drop it.
	if (SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_ENDLESS_RUN)
	{
		network_update();
		return true;
	}

	if (SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_SHOP_SYNC)
		return false;

	if (packet_in[0]->len >= 39)
	{
		const uint sender = SDLNet_Read16(&packet_in[0]->data[4]);
		const Uint16 sequence = SDLNet_Read16(&packet_in[0]->data[6]);
		if (sender >= 1 && sender <= 2 && sender != thisPlayerNum && sequence > network_shop_peer_sequence)
		{
			Player *const peer = &player[sender - 1];
			const Uint16 flags = SDLNet_Read16(&packet_in[0]->data[8]);
			network_shop_peer_sequence = sequence;
			player_set_cash(peer, (Sint64)net_bytes_read64(&packet_in[0]->data[14]));
			peer->weapon_mode = SDLNet_Read16(&packet_in[0]->data[22]);
			const int item_size = network_shop_unpack_items(&peer->items, &packet_in[0]->data[26]);
			peer->last_items = peer->items;
			if (coopEndlessMode && packet_in[0]->len >= 26 + item_size + ENDLESS_PLAYER_BLOCK_SIZE)
				endlessUnpackPlayerBlock(&packet_in[0]->data[26 + item_size], sender - 1);

			network_shop_peer_ready = (flags & SHOP_SYNC_DONE) != 0;
			network_shop_peer_lock  = (flags & SHOP_SYNC_LOCK) != 0;

			// A withdrawn commit takes the route with it, or the host's abandoned planet would
			// still drag the session there once both sides finally leave.
			network_shop_host_committed = false;
			if (network_shop_peer_ready && sender == networkHostPlayerNum &&
			    SDLNet_Read16(&packet_in[0]->data[12]) != 0)
			{
				network_shop_host_committed = true;
				network_shop_host_level = (JE_byte)SDLNet_Read16(&packet_in[0]->data[10]);
			}

			// Endless: any peer may be the one charting, and the index is good the moment it
			// arrives (biased by one so zero still reads as "nothing committed").
			if (coopEndlessMode)
			{
				const int course = (int)SDLNet_Read16(&packet_in[0]->data[10]) - 1;
				network_shop_peer_pick = (course >= 0 && course < ENDLESS_MAX_COURSE_SLOTS)
				                       ? course : -1;
			}

			// Answer a peer that has just opened its outpost with where we stand. Our reply carries
			// no HELLO of its own, so this is one exchange and never a volley.
			if (flags & SHOP_SYNC_HELLO)
				network_shop_send_packet(0, 0);
			if (flags & SHOP_SYNC_SAVE_REQUEST)
				network_shop_send_packet(SHOP_SYNC_SAVE_ACK, sequence);
			if ((flags & SHOP_SYNC_SAVE_ACK) &&
			    SDLNet_Read16(&packet_in[0]->data[24]) == network_shop_save_request)
			{
				/* The peer's own outpost trails the acknowledgement; stash it so the save
				 * being written captures both halves (see "Online saves" in doc/notes.md). */
				const int tail = 26 + item_size + (coopEndlessMode ? ENDLESS_PLAYER_BLOCK_SIZE : 0);
				if (coopEndlessMode && packet_in[0]->len >= tail + ENDLESS_OUTPOST_BLOCK_SIZE)
					endlessPartnerOutpostStash(sender - 1, &packet_in[0]->data[tail]);

				network_shop_save_ready = true;
			}
		}
	}

	network_update();
	return true;
}

/* True when the packet at the head of the reliable queue is one the departure handshake that
 * follows an outpost wait is the one meant to read. A wait loop that calls network_update on it
 * throws it away, and that handshake then waits forever for something already gone. */
bool network_shop_departure_pending(void)
{
	if (packet_in[0] == NULL)
		return false;

	const Uint16 head = SDLNet_Read16(&packet_in[0]->data[0]);
	return head == PACKET_WAITING || head == PACKET_DETAILS || head == PACKET_GAME_QUIT;
}

bool network_shop_peer_done(void)
{
	return network_shop_peer_ready;
}

// Wire-test diagnostic: this machine's own rendezvous announcement and the sequence guard.
void network_shop_debug_state(int *localDone, int *localLock, int *mySeq, int *peerSeq)
{
	*localDone = network_shop_local_ready ? 1 : 0;
	*localLock = network_shop_local_locked ? 1 : 0;
	*mySeq = (int)network_shop_sequence;
	*peerSeq = (int)network_shop_peer_sequence;
}

int network_shop_peer_course(void)
{
	return network_shop_peer_pick;
}

void network_shop_adopt_host_level(void)
{
	// Both players choose freely, so the two picks can differ; the host's is the one the
	// session loads.  A no-op on the host, and on a joiner the host never sent a level to.
	if (!network_shop_host_committed)
		return;

	mainLevel = network_shop_host_level;
	jumpSection = true;
}

void network_shop_end(void)
{
	network_shop_active = false;
}

/* Everything the peer sends after its quit queues behind that notice, and the outpost's departure
 * test reads a queued quit as "the peer already left". A menu release a level-end timeout left
 * behind goes the same way. See "Outpost protocol" in doc/notes.md. */
bool network_quit_notice_retire(void)
{
	bool retired = false;
	while (isNetworkGame && (network_inbound_head() == PACKET_GAME_QUIT
	                         || network_inbound_head() == PACKET_GAME_MENU))
	{
		network_update();
		retired = true;
	}
	return retired;
}

/* Both machines write the same two loadouts, so the save waits on the peer confirming what it
 * holds. Bounded: the save is worth having with one stale ship in it, and is not worth hanging
 * the game over. */
#define NET_SHOP_SAVE_WAIT 6000

void network_shop_sync_for_save(void)
{
	if (!isNetworkGame || !coop_mode_active() || !network_shop_active || !network_peer_alive())
		return;

	network_shop_save_ready = false;
	network_shop_save_request = network_shop_send_packet(SHOP_SYNC_SAVE_REQUEST, 0);

	/* Held back briefly: a peer already outfitting answers within a frame or two, and a notice
	 * that short only flickers. See "Outpost protocol" in doc/notes.md for who can answer. */
	bool notice_drawn = false;

	const Uint32 started = SDL_GetTicks();
	while (!network_shop_save_ready)
	{
		watchdog_heartbeat();

		if (!notice_drawn && SDL_GetTicks() - started > 400)
		{
			notice_drawn = true;
			shopWaitNotice("Waiting for other player.", "They have not reached the outpost yet.",
			               "Press Esc to save now.");
		}

		shopWaitFrame();

		// Esc takes the same exit as the timeout below, on the same terms.
		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			newkey = false;
			JE_playSampleNum(S_SPRING);
			break;
		}
		newkey = false;

		if (network_shop_pump())
			continue;

		// The acknowledgement rides the shop channel and a debug block can be queued ahead of it.
		// Without this the block is dropped rather than adopted and the peer's debug edit is lost
		// on this machine alone.
		if (network_debug_sync_pump(false))
			continue;

		/* The peer has left. No acknowledgement is coming, and consuming the notice to reach the
		 * queue behind it destroys it: it was acknowledged on arrival, so the peer counts it
		 * delivered and never repeats it, and the quit handler then never sees it. */
		if (network_inbound_head() == PACKET_GAME_QUIT)
			break;

		// Bound the wait. On timeout, write the save with stale peer state.
		if (!network_peer_alive() || SDL_GetTicks() - started > NET_SHOP_SAVE_WAIT)
			break;

		/* Everything else at the head is transient rendezvous traffic, and consuming it is what
		 * keeps the acknowledgement behind it reachable -- this wait is a real synchronization
		 * point and the two machines serialize the same transaction boundary through it. */
		network_update();
		network_check();
	}
}

void network_settings_restore(void)
{
	if (!settings_stashed)
		return;

	memcpy(superSparkMode, settings_local.superSparkMode, sizeof(superSparkMode));
	memcpy(epDiffMode, settings_local.epDiffMode, sizeof(epDiffMode));
	zicaLaserBase         = settings_local.zicaLaserBase;
	zicaLaserLength       = settings_local.zicaLaserLength;
	zicaLaserLock         = settings_local.zicaLaserLock;
	zicaLaserBuff         = settings_local.zicaLaserBuff;
	wallopSecondBolt      = settings_local.wallopSecondBolt;
	chargeLaserCannon     = settings_local.chargeLaserCannon;
	restoreBaseDispensers = settings_local.restoreBaseDispensers;
	arcadeLifeBoost       = settings_local.arcadeLifeBoost;
	arcadeRandomBalls     = settings_local.arcadeRandomBalls;
	arcadeRearGunScale    = settings_local.arcadeRearGunScale;
	centeredShotHitboxes  = settings_local.centeredShotHitboxes;
	guidedShotScreenAim   = settings_local.guidedShotScreenAim;
	xmasMode              = settings_local.xmasMode;
	gameSpeed             = settings_local.gameSpeed;
	arcadeSeparateMode    = settings_local.arcadeSeparateMode;
	network_host_timed_battle = settings_local.timedBattle;
	network_host_battle_level = settings_local.battleLevel;
	expertMode                = settings_local.expertMode;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = settings_local.expert[i];
	cheatInfiniteShields      = settings_local.cheatInfiniteShields;
	cheatInfiniteArmor        = settings_local.cheatInfiniteArmor;
	cheatInfiniteGenerator    = settings_local.cheatInfiniteGenerator;
	cheatNoEnemyFire          = settings_local.cheatNoEnemyFire;
	cheatInstantCharge        = settings_local.cheatInstantCharge;
	cheatInfiniteSidekickAmmo = settings_local.cheatInfiniteSidekickAmmo;
	autoFireSpecial           = settings_local.autoFireSpecial;
	debugAutofireTwiddle      = settings_local.debugAutofireTwiddle;
	debugToggleFire           = settings_local.debugToggleFire;
	difficultyAdjust          = settings_local.difficultyAdjust;
	debugTwiddleTrigger       = settings_local.debugTwiddleTrigger;
	constantPlay              = settings_local.constantPlay;
	constantDie               = settings_local.constantDie;
	noclipMode                = settings_local.noclipMode;
	chargeSidekickAutofire    = settings_local.chargeSidekickAutofire;
	debugTwiddleSpecial       = settings_local.debugTwiddleSpecial;

	settings_stashed = false;
}

/* Debug Mode wire state.
 * Publish debug-menu state as one reliable block while both peers are in a menu rendezvous.
 * Armor and shield are transmitted because the sender has already applied any hull change. */
#define NDS_GEN        4    /* Uint32: generation of the block            */
#define NDS_SENDER     8    /* Uint8:  publishing player number, 1 or 2   */
#define NDS_DIFFICULTY 9    /* Uint8                                      */
#define NDS_FLAGS     10    /* Uint16: the boolean cheats, + endlessCampaignMods at bit 14 */
#define NDS_NOCLIP    12    /* Uint8                                      */
#define NDS_CHARGEAF  13    /* Uint8:  chargeSidekickAutofire             */
#define NDS_TWIDDLE   14    /* Uint8:  debugTwiddleSpecial                */
#define NDS_ITEMS     16    /* 2 x PlayerItems                            */
#define NDS_CASH      42    /* 2 x Sint64                                 */
#define NDS_ARMOR     58    /* Uint16 armor, shield, per player           */
#define NDS_EXPERT    66    /* NDS_EXPERT_SLOTS x Uint16                  */
#define NDS_EXPERT_SLOTS NETWORK_EXPERT_SLOTS
#define NDS_ENDLESS   (NDS_EXPERT + NDS_EXPERT_SLOTS * 2)  /* ENDLESS_DEBUG_BLOCK_SIZE bytes */
#define NDS_SIZE      (NDS_ENDLESS + ENDLESS_DEBUG_BLOCK_SIZE)

// PlayerItems goes on the wire as flat bytes, enforced by this check. Growing it also
// requires moving NDS_CASH and later offsets, then bumping NET_VERSION.
COMPILE_TIME_ASSERT(nds_items_are_flat_bytes, sizeof(PlayerItems) == 13);
COMPILE_TIME_ASSERT(nds_items_fit_the_slot, 2 * sizeof(PlayerItems) <= NDS_CASH - NDS_ITEMS);
COMPILE_TIME_ASSERT(nds_block_fits_a_packet, NDS_SIZE <= NET_PACKET_SIZE);

// Generation of the block this machine currently holds, whether it published it or adopted
// it.  Both players editing during the same rendezvous is the only case that needs a rule:
// equal generations are broken in the host's favour, so the two can never end up having
// swapped each other's edits.
static Uint32 debug_sync_gen = 0;
static Uint8 debug_sync_last[NDS_SIZE];   // what we last published or adopted, for the change test

int network_debug_state_size(void)
{
	return NDS_SIZE;
}

/* Everything from NDS_DIFFICULTY on; the packet header, generation and sender are stamped by
 * the send path, and left zero here so two packings of the same state compare equal. */
void network_debug_state_pack(Uint8 *buf)
{
	memset(buf, 0, NDS_SIZE);

	buf[NDS_DIFFICULTY] = (Uint8)difficultyLevel;
	SDLNet_Write16(network_debug_flags_pack(), &buf[NDS_FLAGS]);
	buf[NDS_NOCLIP]   = noclipMode;
	buf[NDS_CHARGEAF] = chargeSidekickAutofire;
	buf[NDS_TWIDDLE]  = debugTwiddleSpecial;

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		memcpy(&buf[NDS_ITEMS + i * sizeof(PlayerItems)], &player[i].items, sizeof(PlayerItems));
		net_bytes_write64((Uint64)player[i].cash, &buf[NDS_CASH + i * 8]);
		SDLNet_Write16((Uint16)player[i].armor,  &buf[NDS_ARMOR + i * 4]);
		SDLNet_Write16((Uint16)player[i].shield, &buf[NDS_ARMOR + i * 4 + 2]);
	}

	for (int i = 0; i < expertSettingsCount && i < NDS_EXPERT_SLOTS; ++i)
		SDLNet_Write16((Uint16)*expertSettings[i].value, &buf[NDS_EXPERT + i * 2]);

	// The Endless panel's whole slate: depth, modifiers, both ships' perks and personal buffs.
	// Carried here as well as on the zone jump, so any drift in it heals on the next debug edit.
	endlessPackDebugBlock(&buf[NDS_ENDLESS]);
}

void network_debug_state_adopt(const Uint8 *buf, bool in_level)
{
	const Uint16 flags = SDLNet_Read16(&buf[NDS_FLAGS]);

	// One-shot: both machines resume from the same confirmed frame, so both fire it on the same
	// tick. A trigger already pending locally must still happen when the incoming block has none.
	network_debug_flags_adopt(flags, true);

	difficultyLevel = (JE_shortint)buf[NDS_DIFFICULTY];
	if (difficultyLevel < DIFFICULTY_WIMP || difficultyLevel > DIFFICULTY_10)
		difficultyLevel = DIFFICULTY_NORMAL;

	noclipMode             = buf[NDS_NOCLIP] % NOCLIP_NUM;
	chargeSidekickAutofire = buf[NDS_CHARGEAF] % CHARGE_AUTOFIRE_NUM;
	debugTwiddleSpecial    = (buf[NDS_TWIDDLE] <= SPECIAL_NUM) ? buf[NDS_TWIDDLE] : 0;

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		memcpy(&player[i].items, &buf[NDS_ITEMS + i * sizeof(PlayerItems)], sizeof(PlayerItems));
		player_set_cash(&player[i], (Sint64)net_bytes_read64(&buf[NDS_CASH + i * 8]));
		player[i].armor  = SDLNet_Read16(&buf[NDS_ARMOR + i * 4]);
		player[i].shield = SDLNet_Read16(&buf[NDS_ARMOR + i * 4 + 2]);
	}

	for (int i = 0; i < expertSettingsCount && i < NDS_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = (int)SDLNet_Read16(&buf[NDS_EXPERT + i * 2]);
	clamp_expert_settings();

	endlessUnpackDebugBlock(&buf[NDS_ENDLESS]);

	// Ships, hit boxes, shield ceilings and the sidekick pods are all cached off items[];
	// the same rebuild the editing machine ran, minus the hull re-armor it already applied.
	debugLoadoutRefresh(in_level);

	// Re-pack rather than keeping the received bytes: the clamps above may have landed
	// somewhere else, and the baseline has to be the state we actually hold.
	network_debug_state_pack(debug_sync_last);
}

void network_debug_sync_mark(void)
{
	network_debug_state_pack(debug_sync_last);
}

bool network_debug_sync_changed(void)
{
	Uint8 now[NDS_SIZE];
	network_debug_state_pack(now);
	return memcmp(now, debug_sync_last, NDS_SIZE) != 0;
}

void network_debug_sync_send(void)
{
	if (!isNetworkGame || !network_debug_sync_changed())
		return;

	network_debug_state_pack(debug_sync_last);
	++debug_sync_gen;

	network_prepare(PACKET_DEBUG_SYNC);
	memcpy(&packet_out_temp->data[NDS_GEN], &debug_sync_last[NDS_GEN], NDS_SIZE - NDS_GEN);
	SDLNet_Write32(debug_sync_gen, &packet_out_temp->data[NDS_GEN]);
	packet_out_temp->data[NDS_SENDER] = (Uint8)thisPlayerNum;

	network_send(NDS_SIZE);
}

bool network_debug_sync_pump(bool in_level)
{
	if (!packet_in[0] || packet_in[0]->len < NDS_SIZE ||
	    SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_DEBUG_SYNC)
	{
		return false;
	}

	const Uint32 gen = SDLNet_Read32(&packet_in[0]->data[NDS_GEN]);
	const bool from_host = packet_in[0]->data[NDS_SENDER] == networkHostPlayerNum;

	// Older than what we hold means our own edit superseded it; a tie means both players
	// edited during the same rendezvous, and the host's block is the one both sides take.
	if (gen > debug_sync_gen || (gen == debug_sync_gen && from_host && thisPlayerNum != networkHostPlayerNum))
	{
		network_debug_state_adopt(packet_in[0]->data, in_level);
		debug_sync_gen = gen;
	}

	network_update();
	return true;
}

/* Desync detection.
 * Compare RNG draw count plus player and enemy state. Network levels share a fixed RNG seed. */
bool networkDesyncHalt = false;

void network_sim_state(Uint32 *rand_draws, Uint32 *player_hash, Uint32 *enemy_hash)
{
	// FNV-1a, chosen for being short enough to read and good enough to spot a single bit.
	Uint32 h = 2166136261u;
	#define HASH_WORD(v) do { \
		Uint32 w_ = (Uint32)(v); \
		for (int b_ = 0; b_ < 4; ++b_) { \
			h ^= (w_ >> (b_ * 8)) & 0xffu; \
			h *= 16777619u; \
		} \
	} while (0)

	// Reported raw rather than hashed: seeing "1320 vs 1324" names the size of the divergence,
	// which a hash would throw away.
	*rand_draws = (Uint32)mt_rand_count;

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		HASH_WORD(player[i].x);
		HASH_WORD(player[i].y);
		HASH_WORD(player[i].armor);
		HASH_WORD(player[i].shield);
		HASH_WORD(player[i].is_alive ? 1 : 0);
		HASH_WORD((Uint32)player[i].cash);
		HASH_WORD((Uint32)((Uint64)player[i].cash >> 32));
	}
	/* The linked turret direction is shared simulation state but is not stored on either Player.
	 * Cover it explicitly so a missing input field is reported before differently aimed shots hit
	 * an enemy. Hash its IEEE bytes exactly, matching the bit-identical wire quantization. */
	Uint32 link_direction_bits = 0;
	memcpy(&link_direction_bits, &linkGunDirec,
	       MIN(sizeof(link_direction_bits), sizeof(linkGunDirec)));
	HASH_WORD(twoPlayerLinked ? 1 : 0);
	HASH_WORD(twoPlayerLinked ? link_direction_bits : 0);
	*player_hash = h;

	// Enemies dominate the simulation, so sample the whole table; position and remaining
	// armor are enough to catch a divergence without hashing every field.  enemyAvail is 1
	// for a FREE slot (tyrian2.c), so anything else is a live enemy worth hashing; the slot
	// state itself goes in too, since 0 and 2 are distinct live states.
	h = 2166136261u;
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		if (enemyAvail[i] == 1)
			continue;
		HASH_WORD(i);
		HASH_WORD(enemyAvail[i]);
		HASH_WORD(enemy[i].ex);
		HASH_WORD(enemy[i].ey);
		HASH_WORD(enemy[i].armorleft);
	}
	*enemy_hash = h;

	#undef HASH_WORD
}

Uint32 network_sim_pools(NetSimPools *detail)
{
	Uint32 h = 2166136261u;
	#define HASH_WORD(v) do { \
		Uint32 w_ = (Uint32)(v); \
		for (int b_ = 0; b_ < 4; ++b_) { \
			h ^= (w_ >> (b_ * 8)) & 0xffu; \
			h *= 16777619u; \
		} \
	} while (0)
	#define POOL_DONE(field, count) do { \
		if (detail) { detail->field = h; detail->count = (Uint16)live; } \
		combined ^= h; combined *= 16777619u; \
		h = 2166136261u; live = 0; \
	} while (0)

	Uint32 combined = 2166136261u;
	Uint16 live = 0;

	// Slot index goes in with every row: two pools holding the same rows in different
	// slots are NOT the same state; the next spawn picks a different slot and the
	// timelines part for good.
	for (uint i = 0; i < COUNTOF(explosions); ++i)
	{
		if (explosions[i].ttl == 0)
			continue;
		++live;
		HASH_WORD(i);
		HASH_WORD(explosions[i].ttl);
		HASH_WORD(explosions[i].x);
		HASH_WORD(explosions[i].y);
		HASH_WORD(explosions[i].sprite);
		HASH_WORD(explosions[i].deltaY);
		HASH_WORD((explosions[i].followPlayer ? 1u : 0u) | (explosions[i].fixedPosition ? 2u : 0u));
	}
	POOL_DONE(explosions, n_expl);

	for (uint i = 0; i < COUNTOF(rep_explosions); ++i)
	{
		if (rep_explosions[i].ttl == 0)
			continue;
		++live;
		HASH_WORD(i);
		HASH_WORD(rep_explosions[i].delay);
		HASH_WORD(rep_explosions[i].ttl);
		HASH_WORD(rep_explosions[i].x);
		HASH_WORD(rep_explosions[i].y);
		HASH_WORD(rep_explosions[i].big ? 1u : 0u);
	}
	POOL_DONE(rep_explosions, n_rep);

	// enemyShotAvail is 1 for a FREE slot, like enemyAvail.
	for (uint i = 0; i < COUNTOF(enemyShot); ++i)
	{
		if (enemyShotAvail[i] == 1)
			continue;
		++live;
		HASH_WORD(i);
		HASH_WORD(enemyShot[i].sx);
		HASH_WORD(enemyShot[i].sy);
		HASH_WORD(enemyShot[i].sxm);
		HASH_WORD(enemyShot[i].sym);
		HASH_WORD(enemyShot[i].sxc);
		HASH_WORD(enemyShot[i].syc);
		HASH_WORD(enemyShot[i].sdmg);
		HASH_WORD(enemyShot[i].duration);
		HASH_WORD(enemyShot[i].animate);
	}
	POOL_DONE(enemy_shots, n_eshot);

	// shotAvail is a countdown, not a flag: nonzero means the slot is LIVE.
	for (uint i = 0; i < COUNTOF(shotAvail); ++i)
	{
		if (shotAvail[i] == 0)
			continue;
		++live;
		// Same fields, same order, same moment as the hash below: the rows must name
		// exactly what the mismatching hash covered or a log diff would mislead.
		if (detail && live <= NET_SIM_DETAIL_SHOTS)
		{
			NetSimShotRow *r = &detail->pshot[live - 1];
			r->idx       = (Uint16)i;
			r->avail     = shotAvail[i];
			r->dmg       = playerShotData[i].shotDmg;
			r->playernum = playerShotData[i].playerNumber;
			r->pierce    = playerShotData[i].pierceLock;
			r->x         = (Sint32)playerShotData[i].shotX;
			r->y         = (Sint32)playerShotData[i].shotY;
			r->xm        = (Sint32)playerShotData[i].shotXM;
			r->ym        = (Sint32)playerShotData[i].shotYM;
			r->xc        = (Sint32)playerShotData[i].shotXC;
			r->yc        = (Sint32)playerShotData[i].shotYC;
		}
		HASH_WORD(i);
		HASH_WORD(shotAvail[i]);
		HASH_WORD(playerShotData[i].shotX);
		HASH_WORD(playerShotData[i].shotY);
		HASH_WORD(playerShotData[i].shotXM);
		HASH_WORD(playerShotData[i].shotYM);
		HASH_WORD(playerShotData[i].shotXC);
		HASH_WORD(playerShotData[i].shotYC);
		HASH_WORD(playerShotData[i].shotDmg);
		HASH_WORD(playerShotData[i].playerNumber);
		HASH_WORD(playerShotData[i].pierceLock);
		HASH_WORD(playerShotData[i].pierceDmgCarry);
	}
	POOL_DONE(player_shots, n_pshot);

	// The sound queue is simulation state, not presentation: its slot is drawn from the
	// shared RNG, so a divergence here is an early tell that the streams have parted.
	for (uint i = 0; i < COUNTOF(soundQueue); ++i)
		HASH_WORD(soundQueue[i]);
	if (detail)
		detail->sound = h;
	combined ^= h;
	combined *= 16777619u;

	#undef POOL_DONE
	#undef HASH_WORD
	return combined;
}

// Must sample exactly what network_sim_state() hashes, at the same call site, or the
// dumped rows and the mismatching hash could disagree about what diverged.
void network_sim_detail(NetSimDetail *out)
{
	SDL_COMPILE_TIME_ASSERT(sim_detail_enemies, NET_SIM_DETAIL_ENEMIES == COUNTOF(enemy));

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		out->p[i].x      = (Sint32)player[i].x;
		out->p[i].y      = (Sint32)player[i].y;
		out->p[i].armor  = (Sint32)player[i].armor;
		out->p[i].shield = (Sint32)player[i].shield;
		out->p[i].alive  = player[i].is_alive ? 1 : 0;
		out->p[i].cash   = player[i].cash;
	}

	out->enemy_count = 0;
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		if (enemyAvail[i] == 1)
			continue;
		NetSimEnemyRow *r = &out->e[out->enemy_count++];
		r->idx       = (Uint8)i;
		r->avail     = enemyAvail[i];
		r->type      = (Uint16)enemy[i].enemytype;
		r->ex        = (Sint32)enemy[i].ex;
		r->ey        = (Sint32)enemy[i].ey;
		r->armorleft = (Sint32)enemy[i].armorleft;
	}
}

// Session-long desync memo for the crash log.  Both detection modes call this once per
// desynced level, so a crash or hang long after the fact still names when trouble started.
void network_diag_note_desync(int level)
{
	if (net_diag.desync_levels++ == 0)
	{
		net_diag.first_desync_tick = SDL_GetTicks();
		net_diag.first_desync_level = level;
	}
}

Uint32 network_desync_count(void)
{
	return net_diag.desync_levels;
}

/* Crash-log network section.
 * Reads only static diagnostics and is safe from fault handlers. */
void network_write_diagnostics(FILE *f)
{
	if (f == NULL)
		return;

	fprintf(f, "\nNetwork:\n");

	if (!net_initialized)
	{
		fprintf(f, "  (not initialized)\n");
		return;
	}

	const Uint32 now = SDL_GetTicks();

	fprintf(f, "  Role:         %s, we fly P%u (host flies P%u)%s%s\n",
	        network_is_host ? "host" : network_from_lobby ? "joiner" : "command-line peer",
	        thisPlayerNum, networkHostPlayerNum,
	        connected ? "" : "  NOT CONNECTED",
	        quit ? "  [halting]" : "");
	fprintf(f, "  Names:        '%.20s' vs '%.20s'\n", network_player_name, network_opponent_name);
	fprintf(f, "  Session:      %s  vt=%s  recovery=%s  delay=%d  wire v%d",
	        nrb_session_mode() ? "rollback" : "lockstep",
	        nrb_session_vt() ? "on" : "off",
	        nrb_session_recovery() ? "on" : "off",
	        network_delay, NET_VERSION);
	if (net_diag.connect_tick != 0)
		fprintf(f, "   connected %lu s ago", (unsigned long)((now - net_diag.connect_tick) / 1000));
	fprintf(f, "\n");

	if (ping_valid)
		fprintf(f, "  Ping:         %d ms (smoothed)\n", (int)(ping_ema + 0.5f));
	else
		fprintf(f, "  Ping:         --\n");

	fprintf(f, "  Last traffic: any in %lu ms ago   state in %lu ms ago   reliable out %lu ms ago\n",
	        (unsigned long)(now - last_in_tick), (unsigned long)(now - last_state_in_tick),
	        (unsigned long)(now - last_out_tick));

	{
		int in_held = 0, out_held = 0;
		for (int i = 0; i < NET_PACKET_QUEUE; i++)
		{
			if (packet_in[i] != NULL)  ++in_held;
			if (packet_out[i] != NULL) ++out_held;
		}
		fprintf(f, "  Reliable ch:  out=%u acked=%u backlog=%d (%d held)   in=%u (%d held)   retries=%lu   acked-dropped=%lu\n",
		        last_out_sync, last_ack_sync, network_ack_backlog(), out_held,
		        queue_in_sync, in_held, (unsigned long)net_diag.retries,
		        (unsigned long)net_diag.window_overflow);
	}

	fprintf(f, "  State ch:     in_sync=%u out_sync=%u   sent=%lu recv=%lu late-dropped=%lu\n",
	        last_state_in_sync, last_state_out_sync,
	        (unsigned long)net_diag.state_out, (unsigned long)net_diag.state_in,
	        (unsigned long)net_diag.state_late);
	fprintf(f, "  Loss repair:  xor rebuilds=%lu   resends asked=%lu answered=%lu\n",
	        (unsigned long)net_diag.xor_rebuilds,
	        (unsigned long)net_diag.resend_req_sent, (unsigned long)net_diag.resend_req_served);
	fprintf(f, "  Datagrams:    in=%lu out=%lu   recv errors=%lu send errors=%lu bad type=%lu   state stalls=%lu\n",
	        (unsigned long)net_diag.dg_in, (unsigned long)net_diag.dg_out,
	        (unsigned long)net_diag.recv_errors, (unsigned long)net_diag.send_errors,
	        (unsigned long)net_diag.bad_packets, (unsigned long)net_diag.stalls);

	if (net_diag.desync_levels != 0)
		fprintf(f, "  Desyncs:      %lu level(s); first on level %d, %lu s after connect\n",
		        (unsigned long)net_diag.desync_levels, net_diag.first_desync_level,
		        (unsigned long)((net_diag.first_desync_tick - net_diag.connect_tick) / 1000));
	else
		fprintf(f, "  Desyncs:      none detected\n");

	// The two rollbacks are separate modules with separate timelines. Destruct sets the session
	// netcode like any other type, but its frames run through destruct_rollback.c, so printing the
	// main game's block for one would put a screenful of zeroes beside the numbers that mean
	// something.
	if (nrb_session_mode() && network_game_type != NETWORK_GAME_DESTRUCT)
		nrb_write_diagnostics(f);
	drb_write_diagnostics(f);   // only writes while a Destruct rollback session is armed
}

// Tear down far enough that another network_init() can succeed.  Called when the lobby
// backs out, when a connection attempt fails, and when a network game ends.
void network_shutdown(void)
{
	if (!net_initialized)
		return;

	for (int i = 0; i < NET_PACKET_QUEUE; i++)
	{
		packet_destroy(&packet_in[i]);
		packet_destroy(&packet_out[i]);
		packet_destroy(&packet_state_in[i]);
		packet_destroy(&packet_state_in_xor[i]);
		packet_destroy(&packet_state_out[i]);
	}

	packet_destroy(&packet_temp);
	packet_destroy(&packet_out_temp);
	while (packet_pool_count > 0)
		packet_destroy(&packet_pool[--packet_pool_count]);

	if (net_socket)
	{
		SDLNet_UDP_Close(net_socket);
		net_socket = NULL;
	}

	if (discover_socket)
	{
		SDLNet_UDP_Close(discover_socket);
		discover_socket = NULL;
	}

	SDLNet_Quit();

	// Reset every sync counter, or a second session would start mid-sequence.
	debug_sync_gen = 0;
	memset(debug_sync_last, 0, sizeof(debug_sync_last));
	network_custom_weapon_reset();
	netStyleSessionReset();
	network_shop_sequence = network_shop_peer_sequence = 0;
	last_out_sync = queue_in_sync = queue_out_sync = last_ack_sync = 0;
	last_in_tick = last_out_tick = 0;
	last_state_in_sync = last_state_out_sync = 0;
	last_state_in_tick = 0;

	ping_ema = 0.0f;
	ping_valid = false;

	// Close the session's net-log bracket while the counters still exist.
	if (net_diag.connect_tick != 0)
	{
		char detail[128];
		snprintf(detail, sizeof(detail),
		         "connected %lu s, desynced levels %lu, stalls %lu",
		         (unsigned long)((SDL_GetTicks() - net_diag.connect_tick) / 1000),
		         (unsigned long)net_diag.desync_levels,
		         (unsigned long)net_diag.stalls);
		crashlog_netlog_line("NETWORK SESSION END", detail);
	}

	memset(&net_diag, 0, sizeof(net_diag));

	net_initialized = false;
	connected = false;
	quit = false;
	host_awaiting_peer = false;
	peer_addr_known = false;
	network_session_saveable = false;
	network_shop_active = false;
	network_shop_sequence = 0;
	network_shop_peer_sequence = 0;
	network_shop_save_request = 0;
	network_shop_peer_ready = false;
	network_shop_save_ready = false;

	nrb_set_session_mode(false);
	nrb_set_session_recovery(false);

	network_settings_restore();
}

void network_deinit(void)
{
	network_shutdown();

	network_set_player_name(NULL);
	if (network_opponent_name != empty_string)
		free(network_opponent_name);
	network_opponent_name = empty_string;
	free(network_opponent_host);
	network_opponent_host = NULL;
}

/* LAN discovery.
 * Use a short-lived socket and probe both interface and global broadcast addresses. */
int network_local_addresses(IPaddress *out, int max)
{
	if (out == NULL || max < 1)
		return 0;

	const int count = SDLNet_GetLocalAddresses(out, max);
	if (count > 0)
		return count;

#if defined(__SWITCH__) || defined(__vita__)
	// SDL_net enumerates interfaces with a SIOCGIFCONF ioctl that libnx does not service, so
	// on the consoles it finds nothing and the platform has to be asked for its own address.
	uint32_t host = 0;
	if (console_get_local_ip(&host))
	{
		out[0].host = host;
		out[0].port = 0;
		return 1;
	}
#endif

	return 0;
}

static void discover_send_probe(UDPsocket sock, UDPpacket *probe, Uint32 host_be, Uint16 port)
{
	// IPaddress keeps both fields in network byte order.
	probe->address.host = host_be;
	probe->address.port = SDL_SwapBE16(port);

	// Failure is expected and ignored: SO_BROADCAST may be refused, an interface may be down,
	// or the address may be unroutable.  Discovery is best-effort by nature.
	SDLNet_UDP_Send(sock, -1, probe);
}

// One round of probes: global broadcast plus each interface's directed /24, per port.
static void discover_send_volley(UDPsocket sock, UDPpacket *probe, const Uint16 *ports,
                                 int port_count, const IPaddress *local, int local_count)
{
	for (int p = 0; p < port_count; ++p)
	{
		discover_send_probe(sock, probe, 0xffffffffu, ports[p]);  // 255.255.255.255

		for (int i = 0; i < local_count; ++i)
		{
			if (local[i].host == 0)
				continue;

			// Directed broadcast for this interface's /24.  Addresses are network byte order,
			// so the host part is the top byte as stored.
			const Uint32 subnet_bcast = local[i].host | SDL_SwapBE32(0x000000ffu);
			discover_send_probe(sock, probe, subnet_bcast, ports[p]);
		}
	}
}

int network_discover(NetworkHostInfo *out, int max, Uint32 timeout_ms, void (*poll)(void))
{
	if (max <= 0)
		return 0;

	if (SDLNet_Init() == -1)
		return 0;

	UDPsocket sock = SDLNet_UDP_Open(0);  // any free port; we only need replies back to us
	if (!sock)
	{
		SDLNet_Quit();
		return 0;
	}

	UDPpacket *probe = SDLNet_AllocPacket(NET_PACKET_SIZE);
	UDPpacket *reply = SDLNet_AllocPacket(NET_PACKET_SIZE);
	if (!probe || !reply)
	{
		if (probe) SDLNet_FreePacket(probe);
		if (reply) SDLNet_FreePacket(reply);
		SDLNet_UDP_Close(sock);
		SDLNet_Quit();
		return 0;
	}

	SDLNet_Write16(PACKET_DISCOVER, &probe->data[0]);
	SDLNet_Write16(NET_VERSION,     &probe->data[2]);
	probe->len = 4;

	/* Ports worth asking: the well-known default, which a host on any other port also keeps an
	 * ear on (see discover_socket), plus whatever this machine last used to host, covering an
	 * old-build host that changed its port and has no second ear. */
	Uint16 ports[2] = { NET_PORT, network_listen_port };
	const int port_count = (ports[1] == ports[0]) ? 1 : 2;

	IPaddress local[8];
	const int local_count = network_local_addresses(local, (int)COUNTOF(local));

	discover_send_volley(sock, probe, ports, port_count, local, local_count);

	int found = 0;
	const Uint32 start = SDL_GetTicks();
	Uint32 volley_at = start;

	while (SDL_GetTicks() - start < timeout_ms && found < max)
	{
		// A probe is one datagram; repeat the round so a single loss cannot empty the window.
		if (SDL_GetTicks() - volley_at >= 400)
		{
			volley_at = SDL_GetTicks();
			discover_send_volley(sock, probe, ports, port_count, local, local_count);
		}

		if (SDLNet_UDP_Recv(sock, reply) > 0)
		{
			if (reply->len >= 6 &&
			    SDLNet_Read16(&reply->data[0]) == PACKET_DISCOVER_REPLY &&
			    SDLNet_Read16(&reply->data[2]) == NET_VERSION)
			{
				const Uint32 host = SDL_SwapBE32(reply->address.host);

				char addr[48];
				snprintf(addr, sizeof(addr), "%u.%u.%u.%u",
				         (host >> 24) & 0xff, (host >> 16) & 0xff, (host >> 8) & 0xff, host & 0xff);

				const Uint16 port = SDLNet_Read16(&reply->data[4]);

				// The same host answers once per probe we sent it, so drop repeats.
				bool duplicate = false;
				for (int i = 0; i < found; ++i)
				{
					if (out[i].port == port && strcmp(out[i].address, addr) == 0)
					{
						duplicate = true;
						break;
					}
				}

				if (!duplicate)
				{
					SDL_strlcpy(out[found].address, addr, sizeof(out[found].address));
					out[found].port = port;

					// Name is whatever trails the header; take the length from the packet
					// rather than trusting it to be terminated.
					const int name_len = reply->len - 6;
					if (name_len > 0)
					{
						int n = name_len;
						if (n > (int)sizeof(out[found].name) - 1)
							n = (int)sizeof(out[found].name) - 1;
						memcpy(out[found].name, &reply->data[6], n);
						out[found].name[n] = '\0';
					}
					else
					{
						out[found].name[0] = '\0';
					}

					++found;
				}
			}
			continue;  // drain anything else already queued before sleeping again
		}

		// Let the caller keep its screen alive (the lobby re-presents the
		// "searching" frame with the mouse cursor) while we sit on the socket.
		if (poll)
			poll();

		SDL_Delay(5);
	}

	SDLNet_FreePacket(probe);
	SDLNet_FreePacket(reply);
	SDLNet_UDP_Close(sock);
	SDLNet_Quit();

	return found;
}

// Prevent WSAECONNRESET after an ICMP port-unreachable. SDL_net keeps the raw handle
// private, so verify its opening members against the reported datagram port before the
// ioctl; a layout mismatch skips the operation.
#ifdef _WIN32
static void network_allow_conn_reset(void)
{
	// SDL2_net's struct _UDPsocket opens with these two members.
	struct sdlnet_udpsocket_head
	{
		int    ready;
		SOCKET channel;
	};

	const SOCKET fd = ((const struct sdlnet_udpsocket_head *)net_socket)->channel;

	int type = 0, type_len = (int)sizeof(type);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, (char *)&type, &type_len) != 0 || type != SOCK_DGRAM)
	{
		fprintf(stderr, "warning: could not reach the UDP socket; leaving WSAECONNRESET on\n");
		return;
	}

	// Joiners open port 0, which SDL_net may report as unbound. The socket type
	// check is sufficient in that case.
	const IPaddress *const bound = SDLNet_UDP_GetPeerAddress(net_socket, -1);
	if (bound != NULL && bound->port != 0)
	{
		struct sockaddr_in addr;
		int addr_len = (int)sizeof(addr);

		if (getsockname(fd, (struct sockaddr *)&addr, &addr_len) != 0 ||
		    addr.sin_family != AF_INET || addr.sin_port != bound->port)
		{
			fprintf(stderr, "warning: UDP socket did not check out; leaving WSAECONNRESET on\n");
			return;
		}
	}

	DWORD off = 0, returned = 0;
	if (WSAIoctl(fd, SIO_UDP_CONNRESET, &off, sizeof(off), NULL, 0, &returned, NULL, NULL) != 0)
		fprintf(stderr, "warning: WSAIoctl(SIO_UDP_CONNRESET): %d\n", WSAGetLastError());
}
#else
static void network_allow_conn_reset(void) { }
#endif

int network_init(void)
{
	printf("Initializing network...\n");

	if (network_delay * 2 > NET_PACKET_QUEUE - 2)
	{
		fprintf(stderr, "error: network delay would overflow packet queue\n");
		return -4;
	}

	if (SDLNet_Init() == -1)
	{
		fprintf(stderr, "error: SDLNet_Init: %s\n", SDLNet_GetError());
		return -1;
	}

	net_socket = SDLNet_UDP_Open(network_player_port);
	if (!net_socket)
	{
		fprintf(stderr, "error: SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		SDLNet_Quit();
		return -2;
	}

	network_allow_conn_reset();

	packet_temp = SDLNet_AllocPacket(NET_PACKET_SIZE);
	packet_out_temp = SDLNet_AllocPacket(NET_PACKET_SIZE);

	if (!packet_temp || !packet_out_temp)
	{
		printf("SDLNet_AllocPacket: %s\n", SDLNet_GetError());
		packet_destroy(&packet_temp);
		packet_destroy(&packet_out_temp);
		SDLNet_UDP_Close(net_socket);
		net_socket = NULL;
		SDLNet_Quit();
		return -3;
	}

	net_initialized = true;

	return 0;
}

/* Working-set probe for the soak check: the harness compares the figure printed after the
 * handshake against the one at the finish, so growth across a session's traffic is visible.
 * Windows only; other platforms print zero and the harness skips the comparison. */
static unsigned long net_test_rss_kb(void)
{
#ifdef _WIN32
	PROCESS_MEMORY_COUNTERS pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
		return (unsigned long)(pmc.WorkingSetSize / 1024);
#endif
	return 0;
}

/* Test-peer deadline, kept below the harness deadline so a wedged peer can print
 * its own diagnostic before the harness kills it. */
#define NET_TEST_CEILING 70000

static Uint32 net_test_started;
static unsigned long net_test_rss_start_kb;

bool network_test_expired(void)
{
	return SDL_GetTicks() - net_test_started > NET_TEST_CEILING;
}

/* The gameplay wire runs print the same working-set pair as the base scenario, so the harness
 * can apply its soak check to a long flight too. Mark once when the session is up. */
void network_test_mem_mark(void)
{
	net_test_rss_start_kb = net_test_rss_kb();
}

unsigned long network_test_mem_start_kb(void)
{
	return net_test_rss_start_kb;
}

unsigned long network_test_mem_now_kb(void)
{
	return net_test_rss_kb();
}

/* Shared close for every scenario: settle the reliable channel, then hold the socket open long
 * enough that a dropped ACK for the last payload can still be re-earned before we exit. */
static int net_test_finish(int rounds)
{
	const Uint32 sync_start = SDL_GetTicks();
	while (!network_is_sync() && SDL_GetTicks() - sync_start < 12000)
	{
		watchdog_heartbeat();
		network_check();
		SDL_Delay(1);
	}
	if (!network_is_sync())
	{
		fprintf(stderr, "network test: the reliable channel never settled\n");
		return 1;
	}

	/* Leave only once the peer has gone quiet: a fixed window can close while a delayed link
	 * still owes the peer an acknowledgement, and whoever exits first strands the other in a
	 * retry loop against a closed socket. Bounded, in case the peer never settles. */
	const Uint32 drain_start = SDL_GetTicks();
	Uint32 last_traffic = drain_start;
	while (SDL_GetTicks() - drain_start < 8000
	       && (SDL_GetTicks() - last_traffic < 1500 || SDL_GetTicks() - drain_start < NET_RETRY))
	{
		watchdog_heartbeat();
		if (network_check() > 0)
			last_traffic = SDL_GetTicks();
		SDL_Delay(1);
	}

	printf("NETWORK TEST PASS player=%u rounds=%d scenario=%d ping=%dms\n",
	       thisPlayerNum, rounds, qa_net_scenario, network_ping_ms());
	printf("NETWORK TEST MEM player=%u start=%lu end=%lu kb\n",
	       thisPlayerNum, net_test_rss_start_kb, net_test_rss_kb());
	return 0;
}

int network_test_peer(int rounds, int scenario)
{
	net_test_started = SDL_GetTicks();

	if (rounds < 1 || rounds > 1000)
		return 2;

	/* Start the reliable sequence space just short of the Uint16 wrap, so every scenario
	 * crosses it in its normal course. The receive side adopts the sender's base from the
	 * connect packet, the way it adopts any starting sequence. */
	qa_seq_base = 0xFFD0;
	last_out_sync = queue_out_sync = last_ack_sync = qa_seq_base;

	if (network_connect() != 0)
		return 1;

	/* Retried connect packets may still be queued behind the handshake. */
	for (int guard = 0; guard < 100 && packet_in[0] != NULL; ++guard)
	{
		if (SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_CONNECT)
			break;
		network_update();
		network_check();
	}

	net_test_rss_start_kb = net_test_rss_kb();

	for (int round = 0; round < rounds; ++round)
	{
		if (network_test_expired())
		{
			fprintf(stderr, "network test: wall-clock ceiling reached in round %d of %d "
			                "(queue depth %d, head type %04x, outbound %s)\n",
			        round, rounds, network_inbound_depth(), (unsigned)network_inbound_head(),
			        network_is_sync() ? "acknowledged" : "UNACKNOWLEDGED");
			return 1;
		}

		const Uint32 payload = 0x51410000u ^ ((Uint32)thisPlayerNum << 12) ^ (Uint32)round;
		network_prepare(PACKET_WAITING);
		SDLNet_Write32(payload, &packet_out_temp->data[4]);
		if (!network_send(8))
			return 1;

		const Uint32 started = SDL_GetTicks();
		bool received = false;
		while (SDL_GetTicks() - started < 12000)
		{
			watchdog_heartbeat();
			network_check();
			if (packet_in[0] != NULL)
			{
				const Uint16 type = SDLNet_Read16(&packet_in[0]->data[0]);
				if (type == PACKET_WAITING && packet_in[0]->len >= 8)
				{
					const Uint32 expect = 0x51410000u ^ ((Uint32)(3 - thisPlayerNum) << 12) ^ (Uint32)round;
					const Uint32 got = SDLNet_Read32(&packet_in[0]->data[4]);
					network_update();
					if (got != expect)
					{
						fprintf(stderr, "network test: round %d payload %08x != %08x\n",
						        round, (unsigned)got, (unsigned)expect);
						return 1;
					}
					received = true;
					break;
				}
				network_update();
			}
			SDL_Delay(1);
		}
		if (!received)
		{
			fprintf(stderr, "network test: timed out in round %d (ack backlog %d, next type %04x)\n",
			        round, network_ack_backlog(),
			        packet_in[0] != NULL ? SDLNet_Read16(&packet_in[0]->data[0]) : 0);
			return 1;
		}

		/* Keep test traffic within the gameplay lead window so a lost ACK is
		 * recovered before the next round is queued. */
		const Uint32 ack_start = SDL_GetTicks();
		while (!network_is_sync() && SDL_GetTicks() - ack_start < 12000)
		{
			watchdog_heartbeat();
			network_check();
			SDL_Delay(1);
		}
		if (!network_is_sync())
		{
			fprintf(stderr, "network test: acknowledgement timed out in round %d\n", round);
			return 1;
		}
	}

	/* The rounds above establish a live, synchronized session; each scenario then drives one
	 * mode's own protocol over it. */
	if (scenario != 0)
	{
		int rc;
		switch (scenario)
		{
		case 1:
			rc = qa_net_campaign_phases();
			break;
		case 2:
			rc = qa_net_endless_phases();
			break;
		default:
			rc = qa_net_barrier_phases();
			break;
		}
		if (rc != 0)
			return rc;
		return net_test_finish(rounds);
	}

	/* The Relaxed death prompt. One player reads it for as long as they like while the other waits
	 * on their answer, and that answer still has to arrive: it travels on the Endless co-op channel,
	 * which nothing else in this test exercises. The reader services the socket exactly the way
	 * JE_endlessDeathMenu's frame does. */
	twoPlayerMode = true;
	coopEndlessMode = true;
	if (thisPlayerNum == networkHostPlayerNum)
	{
		const Uint32 reading = SDL_GetTicks();
		while (SDL_GetTicks() - reading < 5000)
		{
			watchdog_heartbeat();
			network_check();               /* exactly what JE_endlessDeathMenu's frame does */
			while (network_shop_pump())
				;
			SDL_Delay(16);
		}
		network_endless_death_sync((int)ENDLESS_DEATH_OUTPOST);
	}
	else
	{
		const int adopted = network_endless_death_sync(-1);
		if (adopted != (int)ENDLESS_DEATH_OUTPOST)
		{
			fprintf(stderr, "network test: death choice came back as %d, not Return to Outpost\n",
			        adopted);
			return 1;
		}
	}
	coopEndlessMode = false;

	/* Exercise the campaign shop protocol through the same hostile proxy. Both peers publish
	 * independent loadouts, then request a simultaneous save checkpoint and rendezvous. */
	twoPlayerMode = true;
	coopCampaignMode = true;
	Player *const local = &player[thisPlayerNum - 1];
	Player *const peer = &player[2 - thisPlayerNum];
	memset(&local->items, 0, sizeof(local->items));
	local->items.ship = (Uint8)thisPlayerNum;
	local->items.generator = 2;
	local->items.shield = 4;
	local->items.weapon[FRONT_WEAPON].id = (Uint8)(1 + thisPlayerNum);
	local->items.weapon[FRONT_WEAPON].power = (Uint8)(2 + thisPlayerNum);
	local->items.weapon[REAR_WEAPON].id = (Uint8)(10 + thisPlayerNum);
	local->items.weapon[REAR_WEAPON].power = (Uint8)(3 + thisPlayerNum);
	local->cash = 50000u + thisPlayerNum * 100u;
	local->weapon_mode = thisPlayerNum;
	network_shop_begin();

	const Uint32 peer_cash = 50000u + (3u - thisPlayerNum) * 100u;
	const Uint32 shop_start = SDL_GetTicks();
	while (peer->cash != peer_cash && SDL_GetTicks() - shop_start < 12000)
	{
		watchdog_heartbeat();
		network_check();
		while (network_shop_pump())
			;
		SDL_Delay(1);
	}
	if (peer->cash != peer_cash || peer->items.ship != 3u - thisPlayerNum ||
	    peer->items.weapon[REAR_WEAPON].power != 3u + (3u - thisPlayerNum))
	{
		fprintf(stderr, "network test: campaign shop baseline did not converge\n");
		return 1;
	}

	network_prepare(PACKET_WAITING);
	SDLNet_Write32(0x53484f50u, &packet_out_temp->data[4]);
	if (!network_send(8))
		return 1;
	const Uint32 shop_ready_start = SDL_GetTicks();
	bool shop_ready = false;
	while (!shop_ready && SDL_GetTicks() - shop_ready_start < 12000)
	{
		watchdog_heartbeat();
		network_check();
		if (packet_in[0] != NULL)
		{
			const Uint16 type = SDLNet_Read16(&packet_in[0]->data[0]);
			if (type == PACKET_WAITING && packet_in[0]->len >= 8 &&
			    SDLNet_Read32(&packet_in[0]->data[4]) == 0x53484f50u)
				shop_ready = true;
			network_update();
		}
		SDL_Delay(1);
	}
	if (!shop_ready)
	{
		fprintf(stderr, "network test: campaign shop checkpoint setup timed out\n");
		return 1;
	}

	local->cash += 77;
	local->items.weapon[FRONT_WEAPON].power++;
	network_shop_send_transaction();
	const Uint32 transaction_start = SDL_GetTicks();
	while ((peer->cash != peer_cash + 77 ||
	        peer->items.weapon[FRONT_WEAPON].power != 3u + (3u - thisPlayerNum)) &&
	       SDL_GetTicks() - transaction_start < 12000)
	{
		watchdog_heartbeat();
		network_check();
		while (network_shop_pump())
			;
		SDL_Delay(1);
	}
	if (peer->cash != peer_cash + 77 ||
	    peer->items.weapon[FRONT_WEAPON].power != 3u + (3u - thisPlayerNum))
	{
		fprintf(stderr, "network test: campaign shop transaction did not converge\n");
		return 1;
	}

	network_shop_sync_for_save();
	if (peer->cash != peer_cash + 77 ||
	    peer->items.weapon[FRONT_WEAPON].power != 3u + (3u - thisPlayerNum))
	{
		fprintf(stderr, "network test: campaign save checkpoint did not converge\n");
		return 1;
	}

	network_shop_send_state(true);
	const Uint32 done_start = SDL_GetTicks();
	while (!network_shop_peer_done() && SDL_GetTicks() - done_start < 12000)
	{
		watchdog_heartbeat();
		network_check();
		while (network_shop_pump())
			;
		SDL_Delay(1);
	}
	if (!network_shop_peer_done())
	{
		fprintf(stderr, "network test: campaign shop rendezvous timed out\n");
		return 1;
	}
	network_shop_end();

	/* Endless outpost: one machine charts the sector and the other has to come out of the same
	 * rendezvous already holding that index, because nothing publishes it afterwards. Ordered the
	 * way shopLeaveOutpost orders it, including the waiter committing first and the charting
	 * player sending uncommitted packets (course -1) before it picks. */
	coopCampaignMode = false;
	coopEndlessMode = true;
	network_shop_begin();

	const bool charting = (thisPlayerNum == networkHostPlayerNum);
	const int test_course = 3;
	endlessCoopCourse = -1;
	jumpSection = true;

	if (!charting)
		network_shop_send_state(true);

	if (charting)
	{
		/* Still shopping: these carry no course, and the waiter must not be left holding one. */
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
		endlessCoopCourse = test_course;
		network_shop_send_state(true);
	}

	const Uint32 endless_done_start = SDL_GetTicks();
	while (!network_shop_peer_done() && SDL_GetTicks() - endless_done_start < 12000)
	{
		watchdog_heartbeat();
		network_check();
		while (network_shop_pump())
			;
		SDL_Delay(1);
	}
	if (!network_shop_peer_done())
	{
		fprintf(stderr, "network test: endless outpost rendezvous timed out\n");
		return 1;
	}

	network_shop_set_locked(true);
	const Uint32 endless_lock_start = SDL_GetTicks();
	while (!network_shop_peer_locked() && SDL_GetTicks() - endless_lock_start < 12000)
	{
		watchdog_heartbeat();
		network_check();
		while (network_shop_pump())
			;
		SDL_Delay(1);
	}
	if (!network_shop_peer_locked())
	{
		fprintf(stderr, "network test: endless outpost lock timed out\n");
		return 1;
	}

	if (!charting && network_shop_peer_course() != test_course)
	{
		fprintf(stderr, "network test: endless course index did not survive the rendezvous (%d)\n",
		        network_shop_peer_course());
		return 1;
	}

	/* The waiter leaves later than the charting player: it still has a sector to look up and
	 * mutators to fold. Whatever it does with the queue in that window, the level handshake below
	 * has to still find the packet the peer sent, so linger here the way that lookup does. */
	if (!charting)
	{
		const Uint32 linger_start = SDL_GetTicks();
		while (SDL_GetTicks() - linger_start < 700)
		{
			watchdog_heartbeat();
			if (network_shop_pump())
				continue;
			if (network_shop_departure_pending())
				break;
			network_update();
			network_check();
			SDL_Delay(1);
		}
	}

	network_shop_end();
	endlessCoopCourse = -1;
	jumpSection = false;

	/* The level-start handshake exactly as shopLeaveOutpost runs it. */
	network_prepare(PACKET_WAITING);
	network_send(4);
	const Uint32 depart_start = SDL_GetTicks();
	bool departed = false;
	while (!departed && SDL_GetTicks() - depart_start < 12000)
	{
		watchdog_heartbeat();
		if (network_shop_pump())
			continue;
		if (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_WAITING)
		{
			network_update();
			departed = true;
			break;
		}
		network_update();
		network_check();
		SDL_Delay(1);
	}
	if (!departed)
	{
		fprintf(stderr, "network test: endless outpost departure handshake timed out\n");
		return 1;
	}
	network_state_reset();

	/* Barrier before resetting outpost state. Without it, a fast peer's commit can
	 * be drained and discarded before the slower peer opens the outpost. */
	network_shop_begin();
	if (charting)
	{
		const Uint32 slow_start = SDL_GetTicks();
		while (SDL_GetTicks() - slow_start < 800)
		{
			watchdog_heartbeat();
			network_update();
			network_check();
			SDL_Delay(1);
		}
		network_shop_begin();     /* ...and only now opens its own outpost */
	}
	network_shop_send_state(true);

	/* Both peers keep pumping until the outpost is left, exactly as the shop loop does, so the
	 * later arrival's HELLO is answered whether or not the other is still inside a wait. */
	const Uint32 late_start = SDL_GetTicks();
	while (SDL_GetTicks() - late_start < 4000)
	{
		watchdog_heartbeat();
		network_check();
		network_shop_keepalive();
		while (network_shop_pump())
			;
		SDL_Delay(1);
	}
	if (!network_shop_peer_done())
	{
		fprintf(stderr, "network test: a commit made before the peer opened its outpost was lost\n");
		return 1;
	}
	network_shop_end();

	coopEndlessMode = false;
	coopCampaignMode = true;

	return net_test_finish(rounds);
}

#else

// The crash log calls this whenever a network game is flagged; without networking
// compiled in there is nothing to report.
void network_write_diagnostics(FILE *f)
{
	(void)f;
}

#endif

void JE_clearSpecialRequests(void)
{
	inGameMenuRequest = false;
	skipLevelRequest = false;
	helpRequest = false;
	nortShipRequest = false;
}
