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
#endif

#include "network.h"

#include "config.h"
#include "console_platform.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "endless.h"
#include "game_menu.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "mainint.h"
#include "menus.h"
#include "mouse.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "nortvars.h"
#include "opentyr.h"
#include "picload.h"
#include "player.h"
#include "rollback.h"
#include "shots.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"
#include "video.h"

#include <assert.h>
#include <stdlib.h>

/* UDP session transport, handshake, discovery, and deterministic state exchange. */

#define NET_VERSION       16           // v16 adds the Endless lobby type and its settings
#define NET_PORT          1333         // UDP

// PACKET_CONNECT layout past the 4-byte header: version, delay, episode mask, player number,
// game type, episode, difficulty, the host's simulation settings, its Endless lobby block, and
// finally the player name.
#define NET_CONNECT_GAME_TYPE 12
#define NET_CONNECT_EPISODE   14
#define NET_CONNECT_DIFFICULTY 16
#define NET_CONNECT_SETTINGS  18
#define NET_CONNECT_ENDLESS   (NET_CONNECT_SETTINGS + NETWORK_SETTINGS_SIZE)
#define NET_CONNECT_ENDLESS_SIZE (2 + NET_ENDLESS_SEED_MAX)   // run mode, course chooser, seed
#define NET_CONNECT_NAME      (NET_CONNECT_ENDLESS + NET_CONNECT_ENDLESS_SIZE)

#define NET_RETRY         640          // ticks to wait for packet acknowledgment before resending
#define NET_RESEND        320          // ticks to wait before requesting unreceived game packet
#define NET_KEEP_ALIVE    1600         // ticks to wait between keep-alive packets
#define NET_TIME_OUT      16000        // ticks to wait before considering connection dead
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

// Endless lobby block. Blank seed means "roll one when the run starts".
char network_host_endless_seed[NET_ENDLESS_SEED_MAX] = "";
int  network_host_endless_run_mode = 1;   // ENDLESS_RUNMODE_STANDARD
int  network_host_endless_chooser = 0;    // ENDLESS_PICK_HOST

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

UDPpacket *packet_out_temp;
static UDPpacket *packet_temp;

UDPpacket *packet_in[NET_PACKET_QUEUE] = { NULL },
          *packet_out[NET_PACKET_QUEUE] = { NULL };

static Uint16 last_out_sync = 0, queue_in_sync = 0, queue_out_sync = 0, last_ack_sync = 0;
static Uint32 last_in_tick = 0, last_out_tick = 0;

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
	Uint32 acked_dropped;                       // reliable packets acked into a full queue, then lost
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
JE_boolean pauseRequest, skipLevelRequest, helpRequest, nortShipRequest;
JE_boolean yourInGameMenuRequest, inGameMenuRequest;

#ifdef WITH_NETWORK
static void packet_copy(UDPpacket *dst, UDPpacket *src)
{
	void *temp = dst->data;
	memcpy(dst, src, sizeof(*dst));
	dst->data = temp;
	memcpy(dst->data, src->data, src->len);
}

static void packets_shift_up(UDPpacket **packet, int max_packets)
{
		if (packet[0])
		{
			SDLNet_FreePacket(packet[0]);
		}
		for (int i = 0; i < max_packets - 1; i++)
		{
			packet[i] = packet[i + 1];
		}
		packet[max_packets - 1] = NULL;
}

static void packets_shift_down(UDPpacket **packet, int max_packets)
{
	if (packet[max_packets - 1])
	{
		SDLNet_FreePacket(packet[max_packets - 1]);
	}
	for (int i = max_packets - 1; i > 0; i--)
	{
		packet[i] = packet[i - 1];
	}
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
		fprintf(stderr, "error: outbound packet queue overflow\n");

		crashlog_note_net("NETWORK QUEUE OVERFLOW",
		              "no acknowledgement for a full outbound queue; treating the link as lost");

		if (!quit)
			network_tyrian_halt(2, false);

		return false;
	}

	bool temp = network_send_no_ack(len);

	packet_out[i] = SDLNet_AllocPacket(NET_PACKET_SIZE);
	packet_copy(packet_out[i], packet_out_temp);

	last_out_sync++;

	if (network_is_sync())
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
				{
					// network_set_player_name already clamps the stored name; the re-clamp
					// keeps the fixed-size packet fill safe on its own terms.
					size_t name_len = strlen(network_player_name);
					if (name_len > NET_NAME_MAX)
						name_len = NET_NAME_MAX;

					SDLNet_Write16(PACKET_DISCOVER_REPLY, &packet_out_temp->data[0]);
					SDLNet_Write16(NET_VERSION,           &packet_out_temp->data[2]);
					SDLNet_Write16(network_player_port,   &packet_out_temp->data[4]);
					memcpy(&packet_out_temp->data[6], network_player_name, name_len);
					packet_out_temp->data[6 + name_len] = '\0';

					packet_out_temp->len = (int)(6 + name_len + 1);
					packet_out_temp->address = packet_temp->address;

					// Channel -1 sends to the packet's own address, which is what we want:
					// replying must not disturb the channel binding the game protocol uses.
					SDLNet_UDP_Send(net_socket, -1, packet_out_temp);
				}

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
			}

			// Accept the known peer by host address even if NAT rewrites its outbound source port.
			const bool from_peer = packet_temp->channel == 0 ||
			                       (peer_addr_known && packet_temp->address.host == ip.host);

			if (from_peer && packet_temp->len >= 4)
			{
				switch (SDLNet_Read16(&packet_temp->data[0]))
				{
					case PACKET_ACKNOWLEDGE:
						if ((Uint16)(SDLNet_Read16(&packet_temp->data[2]) - last_ack_sync) < NET_PACKET_QUEUE)
						{
							last_ack_sync = SDLNet_Read16(&packet_temp->data[2]);
						}

						{
							Uint16 i = SDLNet_Read16(&packet_temp->data[2]) - queue_out_sync;
							if (i < NET_PACKET_QUEUE)
							{
								if (packet_out[i])
								{
									SDLNet_FreePacket(packet_out[i]);
									packet_out[i] = NULL;
								}
							}
						}

						// remove acknowledged packets from queue
						while (packet_out[0] == NULL && (Uint16)(last_ack_sync - queue_out_sync) < NET_PACKET_QUEUE)
						{
							packets_shift_up(packet_out, NET_PACKET_QUEUE);

							queue_out_sync++;
						}

						last_in_tick = SDL_GetTicks();
						break;

					case PACKET_CONNECT:
						/*
						 * A delayed/duplicated handshake may arrive after gameplay packets.
						 * Resetting queue_in_sync here would discard the live receive window.
						 */
						if (connected)
						{
							const Uint16 connect_sync = SDLNet_Read16(&packet_temp->data[2]);
							if (connect_sync == queue_in_sync)
							{
								packets_shift_up(packet_in, NET_PACKET_QUEUE);
								++queue_in_sync;
							}
							network_acknowledge(connect_sync);
							last_in_tick = SDL_GetTicks();
							break;
						}
						queue_in_sync = SDLNet_Read16(&packet_temp->data[2]);

						for (int i = 0; i < NET_PACKET_QUEUE; i++)
						{
							if (packet_in[i])
							{
								SDLNet_FreePacket(packet_in[i]);
								packet_in[i] = NULL;
							}
						}
						// fall through

					case PACKET_DETAILS:
					case PACKET_WAITING:
					case PACKET_BUSY:
					case PACKET_GAME_QUIT:
					case PACKET_GAME_PAUSE:
					case PACKET_GAME_MENU:
					case PACKET_DEBUG_SYNC:
					case PACKET_SHOP_SYNC:
					case PACKET_CUSTOM_WEAPON:
					case PACKET_RESYNC:
						{
							Uint16 i = SDLNet_Read16(&packet_temp->data[2]) - queue_in_sync;
							if (i < NET_PACKET_QUEUE)
							{
								if (packet_in[i] == NULL)
									packet_in[i] = SDLNet_AllocPacket(NET_PACKET_SIZE);
								packet_copy(packet_in[i], packet_temp);
							}
							else
							{
								// Count near-window overflow; larger gaps are stale duplicates.
								if ((Uint16)(i - NET_PACKET_QUEUE) < NET_PACKET_QUEUE)
									++net_diag.acked_dropped;
							}
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
									packet_state_in[i] = SDLNet_AllocPacket(NET_PACKET_SIZE);
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
									packet_state_in_xor[i] = SDLNet_AllocPacket(NET_PACKET_SIZE);
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

	if (connected)
	{
		// timeout
		if (!network_is_alive())
		{
			if (!quit)
				network_tyrian_halt(2, false);
		}

		// keep-alive, which doubles as the ping probe: it is the one thing still flowing while
		// a player sits in the outpost, so the round trip stays measurable off the menus.  The
		// four extra bytes cost an old peer nothing; it reads the header and ignores the rest,
		// and simply never sends the reply that would produce a reading.
		static Uint32 keep_alive_tick = 0;
		if (SDL_GetTicks() - keep_alive_tick > NET_KEEP_ALIVE)
		{
			network_prepare(PACKET_KEEP_ALIVE);
			SDLNet_Write32(SDL_GetTicks(), &packet_out_temp->data[4]);
			network_send_no_ack(8);

			keep_alive_tick = SDL_GetTicks();
		}
	}

	// retry
	if (packet_out[0] && SDL_GetTicks() - last_out_tick > NET_RETRY)
	{
		if (!SDLNet_UDP_Send(net_socket, 0, packet_out[0]))
		{
			printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
			++net_diag.send_errors;
			return -1;
		}

		++net_diag.dg_out;
		++net_diag.retries;
		last_out_tick = SDL_GetTicks();
	}

	// DRAIN, don't take one.  Every caller polls this at most once per frame while the peer
	// sends one datagram per frame, so one-per-call sits exactly at break-even: any burst (a
	// retransmit, a keep-alive, a stray discovery probe) parks a backlog that never clears
	// again and reads as permanently added latency.  Bounded so a flood cannot hold the frame
	// open indefinitely.
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

// has opponent gotten all the packets we've sent?
bool network_is_sync(void)
{
	return (queue_out_sync - last_ack_sync == 1);
}

int network_ack_backlog(void)
{
	return (Uint16)(last_out_sync - queue_out_sync);
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
		packet_state_out[0] = SDLNet_AllocPacket(NET_PACKET_SIZE);
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
					packet_state_in[0] = SDLNet_AllocPacket(NET_PACKET_SIZE);
					packet_copy(packet_state_in[0], packet_state_in_xor[x]);
					for (int i = 1; i <= x; i++)
						for (int j = 4; j < packet_state_in[0]->len; j++)
							packet_state_in[0]->data[j] ^= packet_state_in[i]->data[j];
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

		if (network_delay > 1)
		{
			// process the current in packet against the xor queue
			if (packet_state_in_xor[x] == NULL)
			{
				packet_state_in_xor[x] = SDLNet_AllocPacket(NET_PACKET_SIZE);
				packet_copy(packet_state_in_xor[x], packet_state_in[0]);
				packet_state_in_xor[x]->status = 0;
			}
			else
			{
				for (int j = 4; j < packet_state_in_xor[x]->len; j++)
					packet_state_in_xor[x]->data[j] ^= packet_state_in[0]->data[j];
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
			SDLNet_FreePacket(packet_state_in[i]);
			packet_state_in[i] = NULL;
		}
	}
	for (int i = 0; i < NET_PACKET_QUEUE; i++)
	{
		if (packet_state_in_xor[i])
		{
			SDLNet_FreePacket(packet_state_in_xor[i]);
			packet_state_in_xor[i] = NULL;
		}
	}
	for (int i = 0; i < NET_PACKET_QUEUE; i++)
	{
		if (packet_state_out[i])
		{
			SDLNet_FreePacket(packet_state_out[i]);
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
	SDLNet_Write16(NET_VERSION,    &packet_out_temp->data[4]);
	SDLNet_Write16(network_delay,  &packet_out_temp->data[6]);
	SDLNet_Write16(episodes_local, &packet_out_temp->data[8]);
	SDLNet_Write16(thisPlayerNum,  &packet_out_temp->data[10]);
	SDLNet_Write16(network_game_type, &packet_out_temp->data[NET_CONNECT_GAME_TYPE]);
	SDLNet_Write16(network_host_episode, &packet_out_temp->data[NET_CONNECT_EPISODE]);
	SDLNet_Write16(network_host_difficulty, &packet_out_temp->data[NET_CONNECT_DIFFICULTY]);
	network_settings_pack(&packet_out_temp->data[NET_CONNECT_SETTINGS]);
	packet_out_temp->data[NET_CONNECT_ENDLESS] = (Uint8)network_host_endless_run_mode;
	packet_out_temp->data[NET_CONNECT_ENDLESS + 1] = (Uint8)network_host_endless_chooser;
	memcpy(&packet_out_temp->data[NET_CONNECT_ENDLESS + 2], network_host_endless_seed,
	       NET_ENDLESS_SEED_MAX);
	memcpy(&packet_out_temp->data[NET_CONNECT_NAME], network_player_name, name_len);
	packet_out_temp->data[NET_CONNECT_NAME + name_len] = '\0';
	network_send(NET_CONNECT_NAME + name_len + 1);
}

// attempt to punch through firewall by firing off UDP packets at the opponent
// exchange game information
int network_connect(void)
{
	network_settings_apply_session_speed();

	const bool listening = network_from_lobby && network_is_host;

	if (listening)
	{
		// Nothing to resolve: whoever sends the first connect packet becomes the peer, and
		// network_check() binds channel 0 to them at that point.
		host_awaiting_peer = true;
		peer_addr_known = false;
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

	// Netcode mode: start from our own config.  A lobby joiner overwrites this
	// when it adopts the host's settings block; command-line games have no host,
	// so both sides must simply be configured alike (as with network_delay).
	nrb_set_session_mode(net_rollback);
	nrb_set_session_vt(vt_ship && smoothMotion && smoothScroll != 0);
	nrb_set_session_recovery(net_desync_recovery);
	coop_set_session_shared_credit(coopSharedCredit);

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

		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			// From the lobby, backing out of "waiting for player" has to return to the menu.
			// Only a command-line game (which has no menu to return to) still exits here.
			if (network_from_lobby)
				return -1;
			network_tyrian_halt(0, false);
		}

		// never timeout
		last_in_tick = SDL_GetTicks();

		if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CONNECT)
			break;

		network_update();
		network_check();

		SDL_Delay(16);
	}

	// The joiner is known now, so a listening host can finally introduce itself.  last_out_sync
	// only advances on an acknowledged send, so zero means the connect packet is still owed.
	if (listening && last_out_sync == 0)
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
	if (SDLNet_Read16(&packet_in[0]->data[4]) != NET_VERSION)
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
		// Command-line netplay has no host: both sides were configured by hand, so a
		// disagreement is still a hard error rather than something to resolve.
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

		// got a duplicate packet; process it again (but why?)
		if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CONNECT)
			goto connect_again;

		network_check();

		// maybe opponent didn't get our packet
		if (SDL_GetTicks() - last_out_tick > NET_RETRY)
			goto connect_reset;

		SDL_Delay(16);
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
void network_tyrian_halt(unsigned int err, bool attempt_sync)
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

	if (err >= COUNTOF(err_msg))
		err = 0;

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
			JE_loadGameRecord(&saveFiles[22 - 1], true);
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

				network_check();
				SDL_Delay(16);
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

				SDL_Delay(16);
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
		haltGame = false;
		JE_clearSpecialRequests();

		longjmp(network_bailout_env, 1);
	}

	// Not armed (very early startup failure): the original hard exit.  JE_tyrianHalt saves
	// the config, so put the stashed local settings (forced-Normal gameSpeed) back first.
	network_settings_restore();
	SDLNet_Quit();

	JE_tyrianHalt(5);
}

/* Host-authoritative simulation settings.
 * Synchronize settings that change RNG use, weapon/enemy data, object spawning, survivability,
 * shared pickups, data sets, or tick rate. Rendering, audio, and local input settings stay local. */
static bool settings_stashed = false;
static struct
{
	int  superSparkMode[SSW_COUNT];
	int  epDiffMode[EDW_COUNT];
	int  zicaLaserBase, zicaLaserLength;
	bool zicaLaserLock, zicaLaserBuff;
	int  wallopSecondBolt;
	bool chargeLaserCannon, restoreBaseDispensers, arcadeLifeBoost, arcadeRandomBalls;
	int  xmasMode;
	JE_byte gameSpeed;
}
settings_local;

int network_settings_pack(Uint8 *buf)
{
	Uint16 spark = 0;  // SSW_COUNT(4) x 2 bits
	for (int i = SSW_COUNT - 1; i >= 0; --i)
		spark = (spark << 2) | (superSparkMode[i] & 3);

	Uint16 epdiff = 0;  // EDW_COUNT(8) x 2 bits
	for (int i = EDW_COUNT - 1; i >= 0; --i)
		epdiff = (epdiff << 2) | (epDiffMode[i] & 3);

	Uint16 flags = 0;
	flags |= zicaLaserLock         ? 1 << 0 : 0;
	flags |= zicaLaserBuff         ? 1 << 1 : 0;
	flags |= chargeLaserCannon     ? 1 << 2 : 0;
	flags |= restoreBaseDispensers ? 1 << 3 : 0;
	flags |= net_rollback          ? 1 << 4 : 0;  // rollback vs lockstep; host decides
	// The ship-physics tail is sim code (see JE_playerMovement's vt_sim gate),
	// so the host's smooth-motion choice binds the session.
	flags |= (vt_ship && smoothMotion && smoothScroll != 0) ? 1 << 5 : 0;
	flags |= net_desync_recovery   ? 1 << 6 : 0;  // desync recovery; host decides
	flags |= arcadeLifeBoost       ? 1 << 7 : 0;
	flags |= arcadeRandomBalls     ? 1 << 8 : 0;
	flags |= coopSharedCredit      ? 1 << 9 : 0;  // Campaign credit sharing; host decides

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
	settings_local.xmasMode             = xmasMode;
	settings_local.gameSpeed            = gameSpeed;
	settings_stashed = true;
}

// Session game speed: the host applies its lobby choice here and the joiner adopts it from
// the settings block in the connect packet.  Command-line netplay has no host, so both sides
// pin Normal.  network_settings_restore puts the player's own speed back afterward.
void network_settings_apply_session_speed(void)
{
	network_settings_stash();
	gameSpeed = (network_from_lobby && network_is_host) ? (JE_byte)network_host_game_speed : 4;
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
	for (int i = 0; i < EDW_COUNT; ++i, epdiff >>= 2)
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

	return NETWORK_SETTINGS_SIZE;
}

// The Endless lobby block travels beside the settings block and binds the run the same way.
COMPILE_TIME_ASSERT(net_endless_seed_fits, NET_ENDLESS_SEED_MAX == ENDLESS_SEED_MAXLEN);

void network_endless_adopt(const Uint8 *buf)
{
	network_host_endless_run_mode = buf[0];
	network_host_endless_chooser = buf[1];
	memcpy(network_host_endless_seed, &buf[2], NET_ENDLESS_SEED_MAX);
	network_host_endless_seed[NET_ENDLESS_SEED_MAX - 1] = '\0';

	if (network_host_endless_run_mode < 0 || network_host_endless_run_mode >= ENDLESS_RUNMODE_COUNT)
		network_host_endless_run_mode = ENDLESS_RUNMODE_STANDARD;
	if (network_host_endless_chooser < 0 || network_host_endless_chooser >= ENDLESS_PICK_COUNT)
		network_host_endless_chooser = ENDLESS_PICK_HOST;

	// Only printable ASCII reaches the seed hash, so a hostile packet cannot smuggle control
	// characters onto the seed-name line the run-over screen prints.
	for (int i = 0; i < NET_ENDLESS_SEED_MAX; ++i)
	{
		const unsigned char c = (unsigned char)network_host_endless_seed[i];
		if (c != '\0' && (c < 32 || c >= 127))
			network_host_endless_seed[i] = '?';
	}
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
	SDLNet_Write32(this_player->cash, &packet_out_temp->data[14]);
	SDLNet_Write16((Uint16)this_player->weapon_mode, &packet_out_temp->data[18]);
	SDLNet_Write16(acknowledge, &packet_out_temp->data[20]);
	int len = 22 + network_shop_pack_items(&packet_out_temp->data[22], &this_player->items);
	if (coopEndlessMode)
		len += endlessPackPlayerBlock(&packet_out_temp->data[len], thisPlayerNum - 1);
	network_send(len);
	return sequence;
}

void network_shop_begin(void)
{
	network_shop_save_request = 0;
	network_shop_peer_ready = false;
	network_shop_peer_lock = false;
	network_shop_local_ready = false;
	network_shop_local_locked = false;
	network_shop_save_ready = false;
	network_shop_host_committed = false;
	network_shop_peer_pick = -1;
	network_shop_active = isNetworkGame && coop_mode_active();
	if (isNetworkGame && coop_mode_active())
		network_shop_send_state(false);
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

/* Publish from a point where the peer is known to be draining its inbound queue: the outpost
 * rendezvous is the one place both machines are guaranteed to be in a pump loop. */
void network_custom_weapon_publish(void)
{
	// Nothing to publish while the feature is off: the port stays a placeholder and no ship can
	// be carrying the weapon. Turning it on and equipping goes through the next rendezvous.
	if (!isNetworkGame || !coop_mode_active() || !customWeaponEnabled ||
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

	for (int attempt = 0; attempt < NCW_ATTEMPTS && !network_custom_acked; ++attempt)
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

	for (int attempt = 0; attempt < NCW_ATTEMPTS && !network_endless_acked; ++attempt)
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

			if (packet_in[0] != NULL
			    && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_ENDLESS_RUN
			    && packet_in[0]->len >= NCW_HDR
			    && SDLNet_Read16(&packet_in[0]->data[NCW_COUNT]) == 0)
			{
				if (SDLNet_Read16(&packet_in[0]->data[NCW_GEN]) == gen)
					network_endless_acked = true;
				network_update();
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
			done = endlessRunAdopt(buf, total);
			network_endless_send_ack(gen);   // answer either way: a resend produces the same bytes
			break;
		}
	}

	free(buf);
	free(seen);
	return done;
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

	if (packet_in[0]->len >= 35)
	{
		const uint sender = SDLNet_Read16(&packet_in[0]->data[4]);
		const Uint16 sequence = SDLNet_Read16(&packet_in[0]->data[6]);
		if (sender >= 1 && sender <= 2 && sender != thisPlayerNum && sequence > network_shop_peer_sequence)
		{
			Player *const peer = &player[sender - 1];
			const Uint16 flags = SDLNet_Read16(&packet_in[0]->data[8]);
			network_shop_peer_sequence = sequence;
			peer->cash = SDLNet_Read32(&packet_in[0]->data[14]);
			peer->weapon_mode = SDLNet_Read16(&packet_in[0]->data[18]);
			const int item_size = network_shop_unpack_items(&peer->items, &packet_in[0]->data[22]);
			peer->last_items = peer->items;
			if (coopEndlessMode && packet_in[0]->len >= 22 + item_size + ENDLESS_PLAYER_BLOCK_SIZE)
				endlessUnpackPlayerBlock(&packet_in[0]->data[22 + item_size], sender - 1);

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

			if (flags & SHOP_SYNC_SAVE_REQUEST)
				network_shop_send_packet(SHOP_SYNC_SAVE_ACK, sequence);
			if ((flags & SHOP_SYNC_SAVE_ACK) &&
			    SDLNet_Read16(&packet_in[0]->data[20]) == network_shop_save_request)
				network_shop_save_ready = true;
		}
	}

	network_update();
	return true;
}

bool network_shop_peer_done(void)
{
	return network_shop_peer_ready;
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

void network_shop_sync_for_save(void)
{
	if (!isNetworkGame || !coop_mode_active() || !network_shop_active || !network_peer_alive())
		return;

	network_shop_save_ready = false;
	network_shop_save_request = network_shop_send_packet(SHOP_SYNC_SAVE_REQUEST, 0);
	while (!network_shop_save_ready)
	{
		watchdog_heartbeat();
		service_SDL_events(false);
		if (network_shop_pump())
			continue;
		network_update();
		network_check();
		SDL_Delay(16);
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
	xmasMode              = settings_local.xmasMode;
	gameSpeed             = settings_local.gameSpeed;

	settings_stashed = false;
}

/* Debug Mode wire state.
 * Publish debug-menu state as one reliable block while both peers are in a menu rendezvous.
 * Armor and shield are transmitted because the sender has already applied any hull change. */
#define NDS_GEN        4    /* Uint32: generation of the block            */
#define NDS_SENDER     8    /* Uint8:  publishing player number, 1 or 2   */
#define NDS_DIFFICULTY 9    /* Uint8                                      */
#define NDS_FLAGS     10    /* Uint16: the boolean cheats                 */
#define NDS_NOCLIP    12    /* Uint8                                      */
#define NDS_CHARGEAF  13    /* Uint8:  chargeSidekickAutofire             */
#define NDS_TWIDDLE   14    /* Uint8:  debugTwiddleSpecial                */
#define NDS_ITEMS     16    /* 2 x PlayerItems                            */
#define NDS_CASH      42    /* 2 x Uint32                                 */
#define NDS_ARMOR     50    /* Uint16 armor, shield, per player           */
#define NDS_EXPERT    58    /* NDS_EXPERT_SLOTS x Uint16                  */
#define NDS_EXPERT_SLOTS NETWORK_DEBUG_EXPERT_SLOTS
#define NDS_SIZE      (NDS_EXPERT + NDS_EXPERT_SLOTS * 2)

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

/* Everything from NDS_DIFFICULTY on; the packet header, generation and sender are stamped by
 * the send path, and left zero here so two packings of the same state compare equal. */
static void network_debug_state_pack(Uint8 *buf)
{
	memset(buf, 0, NDS_SIZE);

	Uint16 flags = 0;
	flags |= cheatInfiniteShields     ? 1 << 0 : 0;
	flags |= cheatInfiniteArmor       ? 1 << 1 : 0;
	flags |= cheatInfiniteGenerator   ? 1 << 2 : 0;
	flags |= cheatNoEnemyFire         ? 1 << 3 : 0;
	flags |= cheatInstantCharge       ? 1 << 4 : 0;
	flags |= cheatInfiniteSidekickAmmo? 1 << 5 : 0;
	flags |= autoFireSpecial          ? 1 << 6 : 0;
	flags |= debugAutofireTwiddle     ? 1 << 7 : 0;
	flags |= debugToggleFire          ? 1 << 8 : 0;
	flags |= expertMode               ? 1 << 9 : 0;
	flags |= difficultyAdjust         ? 1 << 10 : 0;
	flags |= debugTwiddleTrigger      ? 1 << 11 : 0;

	buf[NDS_DIFFICULTY] = (Uint8)difficultyLevel;
	SDLNet_Write16(flags, &buf[NDS_FLAGS]);
	buf[NDS_NOCLIP]   = noclipMode;
	buf[NDS_CHARGEAF] = chargeSidekickAutofire;
	buf[NDS_TWIDDLE]  = debugTwiddleSpecial;

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		memcpy(&buf[NDS_ITEMS + i * sizeof(PlayerItems)], &player[i].items, sizeof(PlayerItems));
		SDLNet_Write32((Uint32)player[i].cash, &buf[NDS_CASH + i * 4]);
		SDLNet_Write16((Uint16)player[i].armor,  &buf[NDS_ARMOR + i * 4]);
		SDLNet_Write16((Uint16)player[i].shield, &buf[NDS_ARMOR + i * 4 + 2]);
	}

	for (int i = 0; i < expertSettingsCount && i < NDS_EXPERT_SLOTS; ++i)
		SDLNet_Write16((Uint16)*expertSettings[i].value, &buf[NDS_EXPERT + i * 2]);
}

static void network_debug_state_adopt(const Uint8 *buf, bool in_level)
{
	const Uint16 flags = SDLNet_Read16(&buf[NDS_FLAGS]);

	cheatInfiniteShields      = (flags & (1 << 0)) != 0;
	cheatInfiniteArmor        = (flags & (1 << 1)) != 0;
	cheatInfiniteGenerator    = (flags & (1 << 2)) != 0;
	cheatNoEnemyFire          = (flags & (1 << 3)) != 0;
	cheatInstantCharge        = (flags & (1 << 4)) != 0;
	cheatInfiniteSidekickAmmo = (flags & (1 << 5)) != 0;
	autoFireSpecial           = (flags & (1 << 6)) != 0;
	debugAutofireTwiddle      = (flags & (1 << 7)) != 0;
	debugToggleFire           = (flags & (1 << 8)) != 0;
	expertMode                = (flags & (1 << 9)) != 0;
	difficultyAdjust          = (flags & (1 << 10)) != 0;
	// One-shot: both machines resume from the same confirmed frame, so both fire it on the
	// same tick.  Never cleared here; a trigger already pending locally must still happen.
	if (flags & (1 << 11))
		debugTwiddleTrigger = true;

	difficultyLevel = (JE_shortint)buf[NDS_DIFFICULTY];
	if (difficultyLevel < DIFFICULTY_WIMP || difficultyLevel > DIFFICULTY_10)
		difficultyLevel = DIFFICULTY_NORMAL;

	noclipMode             = buf[NDS_NOCLIP] % NOCLIP_NUM;
	chargeSidekickAutofire = buf[NDS_CHARGEAF] % CHARGE_AUTOFIRE_NUM;
	debugTwiddleSpecial    = (buf[NDS_TWIDDLE] <= SPECIAL_NUM) ? buf[NDS_TWIDDLE] : 0;

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		memcpy(&player[i].items, &buf[NDS_ITEMS + i * sizeof(PlayerItems)], sizeof(PlayerItems));
		player[i].cash   = SDLNet_Read32(&buf[NDS_CASH + i * 4]);
		player[i].armor  = SDLNet_Read16(&buf[NDS_ARMOR + i * 4]);
		player[i].shield = SDLNet_Read16(&buf[NDS_ARMOR + i * 4 + 2]);
	}

	for (int i = 0; i < expertSettingsCount && i < NDS_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = (int)SDLNet_Read16(&buf[NDS_EXPERT + i * 2]);
	clamp_expert_settings();

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
		HASH_WORD(player[i].cash);
	}
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
		out->p[i].cash   = (Sint32)player[i].cash;
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
		        (unsigned long)net_diag.acked_dropped);
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

	if (nrb_session_mode())
		nrb_write_diagnostics(f);
}

// Tear down far enough that another network_init() can succeed.  Called when the lobby
// backs out, when a connection attempt fails, and when a network game ends.
void network_shutdown(void)
{
	if (!net_initialized)
		return;

	for (int i = 0; i < NET_PACKET_QUEUE; i++)
	{
		if (packet_in[i])          { SDLNet_FreePacket(packet_in[i]);          packet_in[i] = NULL; }
		if (packet_out[i])         { SDLNet_FreePacket(packet_out[i]);         packet_out[i] = NULL; }
		if (packet_state_in[i])    { SDLNet_FreePacket(packet_state_in[i]);    packet_state_in[i] = NULL; }
		if (packet_state_in_xor[i]){ SDLNet_FreePacket(packet_state_in_xor[i]);packet_state_in_xor[i] = NULL; }
		if (packet_state_out[i])   { SDLNet_FreePacket(packet_state_out[i]);   packet_state_out[i] = NULL; }
	}

	if (packet_temp)     { SDLNet_FreePacket(packet_temp);     packet_temp = NULL; }
	if (packet_out_temp) { SDLNet_FreePacket(packet_out_temp); packet_out_temp = NULL; }

	if (net_socket) { SDLNet_UDP_Close(net_socket); net_socket = NULL; }

	SDLNet_Quit();

	// Reset every sync counter, or a second session would start mid-sequence.
	debug_sync_gen = 0;
	memset(debug_sync_last, 0, sizeof(debug_sync_last));
	network_custom_weapon_reset();
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

	// Ports worth asking: the well-known default, plus whatever this machine last used to
	// host (players who change it tend to agree on the same number).  A host on some other
	// port simply will not be found, and has to be reached by typing its address.
	Uint16 ports[2] = { NET_PORT, network_listen_port };
	const int port_count = (ports[1] == ports[0]) ? 1 : 2;

	IPaddress local[8];
	const int local_count = network_local_addresses(local, (int)COUNTOF(local));

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

	int found = 0;
	const Uint32 start = SDL_GetTicks();

	while (SDL_GetTicks() - start < timeout_ms && found < max)
	{
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

	// A joiner deliberately opens port 0, which SDL_net leaves unbound and reports as 0; there is
	// nothing to compare against then, and the socket type check above already stands alone.
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
		return -2;
	}

	network_allow_conn_reset();

	packet_temp = SDLNet_AllocPacket(NET_PACKET_SIZE);
	packet_out_temp = SDLNet_AllocPacket(NET_PACKET_SIZE);

	if (!packet_temp || !packet_out_temp)
	{
		printf("SDLNet_AllocPacket: %s\n", SDLNet_GetError());
		return -3;
	}

	net_initialized = true;

	return 0;
}

int network_test_peer(int rounds)
{
	if (rounds < 1 || rounds > 1000)
		return 2;
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

	for (int round = 0; round < rounds; ++round)
	{
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

		/*
		 * Gameplay exchanges stay within a bounded lead over the peer.  Keep the test
		 * inside that same reliable window: a deliberately lost ACK
		 * must be recovered by the retry path before the next round is queued.
		 */
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

	const Uint32 sync_start = SDL_GetTicks();
	while (!network_is_sync() && SDL_GetTicks() - sync_start < 12000)
	{
		watchdog_heartbeat();
		network_check();
		SDL_Delay(1);
	}
	if (!network_is_sync())
		return 1;

	/* An ACK is intentionally not reliable itself. If the peer's ACK for our final payload was
	 * dropped, it needs us to keep the socket alive long enough to resend that payload and receive
	 * another ACK. Earlier rounds naturally get this grace from the next round; the last one needs
	 * an explicit bounded drain before either test process exits. */
	const Uint32 drain_start = SDL_GetTicks();
	while (SDL_GetTicks() - drain_start < NET_RETRY * 3 + 200)
	{
		watchdog_heartbeat();
		network_check();
		SDL_Delay(1);
	}

	printf("NETWORK TEST PASS player=%u rounds=%d ping=%dms\n",
	       thisPlayerNum, rounds, network_ping_ms());
	return 0;
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
	pauseRequest = false;
	inGameMenuRequest = false;
	skipLevelRequest = false;
	helpRequest = false;
	nortShipRequest = false;
}
