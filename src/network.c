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
#include "network.h"

#include "config.h"
#include "crashlog.h"
#include "episodes.h"
#include "file.h"
#include "fonthand.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "mainint.h"
#include "mouse.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "nortvars.h"
#include "opentyr.h"
#include "picload.h"
#include "player.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"
#include "video.h"

#include <assert.h>

/*                              HERE BE DRAGONS!
 *
 * When I wrote this code I thought it was wonderful... that thought was very
 * wrong.  It works, but good luck understanding how... I don't anymore.
 *
 * Hopefully it'll be rewritten some day.
 */

#define NET_VERSION       5            // increment whenever networking changes might create incompatibility
#define NET_PORT          1333         // UDP

// 320 (was 256): the rollback input packet carries a 48-byte header plus up to
// 16 x 14-byte redundant input records = 272 bytes.
#define NET_PACKET_SIZE   320
#define NET_PACKET_QUEUE  16

// PACKET_CONNECT layout past the 4-byte header: version, delay, episode mask, player number,
// then the host's simulation settings, then the null-terminated player name.
#define NET_CONNECT_SETTINGS  12
#define NET_CONNECT_NAME      (NET_CONNECT_SETTINGS + NETWORK_SETTINGS_SIZE)

#define NET_RETRY         640          // ticks to wait for packet acknowledgment before resending
#define NET_RESEND        320          // ticks to wait before requesting unreceived game packet
#define NET_KEEP_ALIVE    1600         // ticks to wait between keep-alive packets
#define NET_TIME_OUT      16000        // ticks to wait before considering connection dead

bool isNetworkGame = false;

// Ticks of input delay, and the single most important number for how netplay feels.
//
// It is NOT just input lag -- it is how far ahead of the other machine we are allowed to run,
// so it caps the tick rate at roughly network_delay / round-trip-time. At 35Hz that means a
// 30ms round trip needs at least 2 to sustain full speed, and anything less makes the whole
// game run in slow motion and stutter on every jitter spike. Values above 1 additionally
// enable the XOR parity packets that can rebuild a single lost state packet.
//
// 3 is the safe default: it sustains full speed up to roughly an 85ms round trip. Drop it to
// 2 on a fast LAN for less input lag; raise it if the game stutters.
int network_delay = 3;

char *network_opponent_host = NULL;

Uint16 network_player_port = NET_PORT,
       network_opponent_port = NET_PORT;

Uint16 network_listen_port = NET_PORT;

static char empty_string[] = "";
char *network_player_name = empty_string,
     *network_opponent_name = empty_string;

bool network_is_host = false;
bool network_from_lobby = false;

// network_player_name starts out pointing at a static empty string and is otherwise owned
// heap memory, so changing it has to know which it currently is.
void network_set_player_name(const char *name)
{
	if (network_player_name != empty_string)
		free(network_player_name);

	if (name == NULL || name[0] == '\0')
	{
		network_player_name = empty_string;
		return;
	}

	network_player_name = malloc_die(strlen(name) + 1);
	strcpy(network_player_name, name);
}

#ifdef WITH_NETWORK
static UDPsocket socket;
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
#endif

#ifdef WITH_NETWORK
jmp_buf network_bailout_env;
bool network_bailout_armed = false;
#endif

uint thisPlayerNum = 0;  /* Player number on this PC (1 or 2) */

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

	if (!SDLNet_UDP_Send(socket, 0, packet_out_temp))
	{
		printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
		return false;
	}

	return true;
}

bool network_send_unacked(int len)
{
	return network_send_no_ack(len);
}

// send packet and place it in queue to be acknowledged
bool network_send(int len)
{
	bool temp = network_send_no_ack(len);

	Uint16 i = last_out_sync - queue_out_sync;
	if (i < NET_PACKET_QUEUE)
	{
		packet_out[i] = SDLNet_AllocPacket(NET_PACKET_SIZE);
		packet_copy(packet_out[i], packet_out_temp);
	}
	else
	{
		// connection is probably bad now
		fprintf(stderr, "warning: outbound packet queue overflow\n");
		return false;
	}

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

		// keep-alive
		static Uint32 keep_alive_tick = 0;
		if (SDL_GetTicks() - keep_alive_tick > NET_KEEP_ALIVE)
		{
			network_prepare(PACKET_KEEP_ALIVE);
			network_send_no_ack(4);

			keep_alive_tick = SDL_GetTicks();
		}
	}

	// retry
	if (packet_out[0] && SDL_GetTicks() - last_out_tick > NET_RETRY)
	{
		if (!SDLNet_UDP_Send(socket, 0, packet_out[0]))
		{
			printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
			return -1;
		}

		last_out_tick = SDL_GetTicks();
	}

	switch (SDLNet_UDP_Recv(socket, packet_temp))
	{
		case -1:
			printf("SDLNet_UDP_Recv: %s\n", SDL_GetError());
			return -1;
			break;
		case 0:
			break;
		default:
			// LAN discovery probes come from machines we have never spoken to, so they arrive
			// on no channel (-1) and have to be answered before the channel check below.  They
			// are never queued and never bind anything; a host that already has a player stays
			// silent so it does not advertise a full game.
			if (packet_temp->len >= 4 &&
			    SDLNet_Read16(&packet_temp->data[0]) == PACKET_DISCOVER)
			{
				if (host_awaiting_peer)
				{
					const size_t name_len = strlen(network_player_name);

					SDLNet_Write16(PACKET_DISCOVER_REPLY, &packet_out_temp->data[0]);
					SDLNet_Write16(NET_VERSION,           &packet_out_temp->data[2]);
					SDLNet_Write16(network_player_port,   &packet_out_temp->data[4]);
					memcpy(&packet_out_temp->data[6], network_player_name, name_len + 1);

					packet_out_temp->len = (int)(6 + name_len + 1);
					packet_out_temp->address = packet_temp->address;

					// Channel -1 sends to the packet's own address, which is what we want:
					// replying must not disturb the channel binding the game protocol uses.
					SDLNet_UDP_Send(socket, -1, packet_out_temp);
				}

				return 1;
			}

			// An unbound socket reports channel -1, so a listening host has to adopt the
			// sender before the normal channel check below can pass.  Only a connect packet
			// may do this, and only until someone has actually joined.
			if (host_awaiting_peer && packet_temp->len >= 4 &&
			    SDLNet_Read16(&packet_temp->data[0]) == PACKET_CONNECT)
			{
				if (SDLNet_UDP_Bind(socket, 0, &packet_temp->address) == -1)
				{
					fprintf(stderr, "error: SDLNet_UDP_Bind: %s\n", SDLNet_GetError());
					return -1;
				}
				ip = packet_temp->address;
				host_awaiting_peer = false;
				packet_temp->channel = 0;
			}

			if (packet_temp->channel == 0 && packet_temp->len >= 4)
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
								// inbound packet queue overflow/underflow
								// under normal circumstances, this is okay
							}
						}

						network_acknowledge(SDLNet_Read16(&packet_temp->data[2]));
						// fall through

					case PACKET_KEEP_ALIVE:
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
								if (packet_state_in[i] == NULL)
									packet_state_in[i] = SDLNet_AllocPacket(NET_PACKET_SIZE);
								packet_copy(packet_state_in[i], packet_temp);
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
									if (!SDLNet_UDP_Send(socket, 0, packet_state_out[i]))
									{
										printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
										return -1;
									}
								}
							}
						}
						break;

					default:
						fprintf(stderr, "warning: bad packet %d received\n", SDLNet_Read16(&packet_temp->data[0]));
						return 0;
						break;
				}

				return 1;
			}
			break;
	}

	return 0;
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
	if (!SDLNet_UDP_Send(socket, 0, packet_state_out[0]))
	{
		printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
		return -1;
	}

	// send xor of last network_delay packets
	if (network_delay > 1 && (last_state_out_sync + 1) % network_delay == 0 && packet_state_out[network_delay - 1] != NULL)
	{
		packet_copy(packet_temp, packet_state_out[0]);
		SDLNet_Write16(PACKET_STATE_XOR, &packet_temp->data[0]);
		for (int i = 1; i < network_delay; i++)
			for (int j = 4; j < packet_temp->len; j++)
				packet_temp->data[j] ^= packet_state_out[i]->data[j];

		if (!SDLNet_UDP_Send(socket, 0, packet_temp))
		{
			printf("SDLNet_UDP_Send: %s\n", SDL_GetError());
			return -1;
		}
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

				resend_tick = SDL_GetTicks();
			}

			// Pump SDL while we wait.  Without this the window stops answering the OS (Windows
			// greys it out as "not responding") and the hang watchdog reports a crash-like
			// stall, when all that is really happening is waiting on the other player.  Every
			// other network wait loop in the game already pumps this way.
			service_SDL_events(false);

			const Uint32 waited = SDL_GetTicks() - wait_start;

			// Log the stall once, with enough context to tell a slow peer from a dead one.
			// keep-alives refresh the liveness timer, so a peer that is running but not
			// sending state packets would otherwise hang us here silently and forever.
			if (!stall_reported && waited > 3000)
			{
				stall_reported = true;

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
				crashlog_note("NETWORK STALL", detail);
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

			if (network_check() == 0)
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
	network_prepare(PACKET_CONNECT);
	SDLNet_Write16(NET_VERSION,    &packet_out_temp->data[4]);
	SDLNet_Write16(network_delay,  &packet_out_temp->data[6]);
	SDLNet_Write16(episodes_local, &packet_out_temp->data[8]);
	SDLNet_Write16(thisPlayerNum,  &packet_out_temp->data[10]);
	network_settings_pack(&packet_out_temp->data[NET_CONNECT_SETTINGS]);
	strcpy((char *)&packet_out_temp->data[NET_CONNECT_NAME], network_player_name);
	network_send(NET_CONNECT_NAME + strlen(network_player_name) + 1);
}

// attempt to punch through firewall by firing off UDP packets at the opponent
// exchange game information
int network_connect(void)
{
	const bool listening = network_from_lobby && network_is_host;

	if (listening)
	{
		// Nothing to resolve: whoever sends the first connect packet becomes the peer, and
		// network_check() binds channel 0 to them at that point.
		host_awaiting_peer = true;
	}
	else
	{
		if (SDLNet_ResolveHost(&ip, network_opponent_host, network_opponent_port) == -1)
		{
			fprintf(stderr, "error: could not resolve '%s'\n",
			        network_opponent_host ? network_opponent_host : "(null)");
			return -2;
		}

		if (SDLNet_UDP_Bind(socket, 0, &ip) == -1)
		{
			fprintf(stderr, "error: SDLNet_UDP_Bind: %s\n", SDLNet_GetError());
			return -2;
		}
	}

	Uint16 episodes = 0, episodes_local = 0;
	assert(EPISODE_MAX <= 16);
	for (int i = EPISODE_MAX - 1; i >= 0; i--)
	{
		episodes <<= 1;
		episodes |= (episodeAvail[i] != 0);
	}
	episodes_local = episodes;

	assert(NET_PACKET_SIZE - NET_CONNECT_NAME >= 20 + 1);
	if (strlen(network_player_name) > 20)
		network_player_name[20] = '\0';

	// The lobby decides the roles, so derive the player number from them; a command-line
	// game keeps whatever --net-player-number set and is checked for conflicts below.
	if (network_from_lobby)
		thisPlayerNum = network_is_host ? 1 : 2;

	// Netcode mode: start from our own config.  A lobby joiner overwrites this
	// when it adopts the host's settings block; command-line games have no host,
	// so both sides must simply be configured alike (as with network_delay).
	nrb_set_session_mode(net_rollback);
	nrb_set_session_vt(vt_ship && smoothMotion && smoothScroll != 0);

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

			network_settings_adopt(&packet_in[0]->data[NET_CONNECT_SETTINGS]);
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
	if (SDLNet_Read16(&packet_in[0]->data[10]) == thisPlayerNum)
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
	// than trusting it to be terminated, and tolerate a packet too short to hold one at all.
	{
		const int name_len = packet_in[0]->len - NET_CONNECT_NAME;
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

	return 0;
}

// something has gone wrong :(
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

	if (err >= COUNTOF(err_msg))
		err = 0;

	fade_black(10);

	VGAScreen = VGAScreenSeg;

	// This screen can be reached mid-game, where the widescreen pillarbox is
	// off; it is a legacy 320px picture, so centre it with the side gradients
	// like every other menu screen.
	set_menu_centered(true);

	JE_loadPic(VGAScreen, 2, false);
	JE_dString(VGAScreen, JE_fontCenter(err_msg[err], SMALL_FONT_SHAPES), 140, err_msg[err], SMALL_FONT_SHAPES);

	JE_showVGA();
	fade_palette(colors, 10, 0, 255);

	if (attempt_sync)
	{
		while (!network_is_sync() && network_is_alive())
		{
			service_SDL_events(false);

			network_check();
			SDL_Delay(16);
		}
	}

	if (err)
	{
		while (!JE_anyButton())
			SDL_Delay(16);
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
		haltGame = false;
		JE_clearSpecialRequests();

		longjmp(network_bailout_env, 1);
	}

	// Not armed (very early startup failure): the original hard exit.
	SDLNet_Quit();

	JE_tyrianHalt(5);
}

/* --- Host-authoritative simulation settings ---------------------------------------------
 *
 * Lockstep means both machines simulate the whole world from the same inputs, so every
 * setting that changes the simulation has to agree.  The ones below were verified to reach
 * the sim rather than just the screen:
 *
 *   superSparkMode[]  gates whether a weapon leaves a trail, and each trail spark costs two
 *                     mt_rand draws (JE_doSP, varz.c) -- a mismatch desyncs the RNG stream
 *                     immediately.  extraSparks and superSparkClassicCap only resize the
 *                     spark ring buffer and draw no extra randomness, so they stay local.
 *   epDiffMode[]      per-weapon episode data; two entries change shot patterns outright.
 *   zicaLaser*        Lv11 shot pattern, length and the extra Lv10 beam.
 *   wallopSecondBolt  adds a second bolt per volley (episodes.c).
 *   chargeLaserCannon changes what the shared shop stocks.
 *   restoreBaseDispensers wakes enemies 80-83.
 *   xmasMode          selects a different shape/data set.
 *   gameSpeed         scales the tick rate the whole sim runs at.
 *
 * Purely presentational settings (gauge gradients, boss/enemy bars, parallax, smooth motion,
 * fps cap, gamma, input device) are deliberately absent: they should stay per-player.
 */
static bool settings_stashed = false;
static struct
{
	int  superSparkMode[SSW_COUNT];
	int  epDiffMode[EDW_COUNT];
	int  zicaLaserBase, zicaLaserLength;
	bool zicaLaserLock, zicaLaserBuff;
	int  wallopSecondBolt;
	bool chargeLaserCannon, restoreBaseDispensers;
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
	flags |= net_rollback          ? 1 << 4 : 0;  // rollback vs lockstep -- host decides
	// The ship-physics tail is sim code (see JE_playerMovement's vt_sim gate),
	// so the host's smooth-motion choice binds the session.
	flags |= (vt_ship && smoothMotion && smoothScroll != 0) ? 1 << 5 : 0;

	SDLNet_Write16(spark,                    &buf[0]);
	SDLNet_Write16(epdiff,                   &buf[2]);
	SDLNet_Write16(flags,                    &buf[4]);
	SDLNet_Write16(zicaLaserBase,            &buf[6]);
	SDLNet_Write16(zicaLaserLength,          &buf[8]);
	SDLNet_Write16(wallopSecondBolt,         &buf[10]);
	SDLNet_Write16((Uint16)(xmasMode + 1),   &buf[12]);  // xmasMode is -1..1; bias to 0..2
	SDLNet_Write16(gameSpeed,                &buf[14]);

	return NETWORK_SETTINGS_SIZE;
}

int network_settings_adopt(const Uint8 *buf)
{
	if (!settings_stashed)
	{
		memcpy(settings_local.superSparkMode, superSparkMode, sizeof(superSparkMode));
		memcpy(settings_local.epDiffMode, epDiffMode, sizeof(epDiffMode));
		settings_local.zicaLaserBase        = zicaLaserBase;
		settings_local.zicaLaserLength      = zicaLaserLength;
		settings_local.zicaLaserLock        = zicaLaserLock;
		settings_local.zicaLaserBuff        = zicaLaserBuff;
		settings_local.wallopSecondBolt     = wallopSecondBolt;
		settings_local.chargeLaserCannon    = chargeLaserCannon;
		settings_local.restoreBaseDispensers = restoreBaseDispensers;
		settings_local.xmasMode             = xmasMode;
		settings_local.gameSpeed            = gameSpeed;
		settings_stashed = true;
	}

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

	// Netcode mode is a session property, not a config setting: the joiner's own
	// net_rollback preference is left untouched and restored semantics don't apply.
	nrb_set_session_mode((flags & (1 << 4)) != 0);
	nrb_set_session_vt((flags & (1 << 5)) != 0);

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
	xmasMode              = settings_local.xmasMode;
	gameSpeed             = settings_local.gameSpeed;

	settings_stashed = false;
}

/* --- Desync detection -------------------------------------------------------------------
 *
 * Nothing in the original netcode ever checked that the two simulations still agreed: a
 * single diverging RNG draw left both players happily playing different games with no
 * indication anything was wrong.  This folds the state that must match into one word.
 *
 * mt_rand_count leads deliberately.  Network levels reseed to a fixed constant, so it is
 * directly comparable, and it catches divergence a tick or two before it shows up in
 * anything visible.  Player and enemy state is sampled after it as a backstop for
 * divergence that somehow consumes the same amount of randomness.
 */
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

	// Enemies dominate the simulation, so sample the whole table -- position and remaining
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

	if (socket) { SDLNet_UDP_Close(socket); socket = NULL; }

	SDLNet_Quit();

	// Reset every sync counter, or a second session would start mid-sequence.
	last_out_sync = queue_in_sync = queue_out_sync = last_ack_sync = 0;
	last_in_tick = last_out_tick = 0;
	last_state_in_sync = last_state_out_sync = 0;
	last_state_in_tick = 0;

	net_initialized = false;
	connected = false;
	quit = false;
	host_awaiting_peer = false;

	nrb_set_session_mode(false);

	network_settings_restore();
}

/* --- LAN discovery ----------------------------------------------------------------------
 *
 * Runs on its own short-lived socket so it cannot disturb a game in progress, and so it can
 * be used from the lobby before any game socket exists.  Probes go to each interface's
 * subnet broadcast address as well as the global one: some stacks and firewalls permit one
 * and not the other, and a duplicate probe costs nothing.
 */
static void discover_send_probe(UDPsocket sock, UDPpacket *probe, Uint32 host_be, Uint16 port)
{
	// IPaddress keeps both fields in network byte order.
	probe->address.host = host_be;
	probe->address.port = SDL_SwapBE16(port);

	// Failure is expected and ignored: SO_BROADCAST may be refused, an interface may be down,
	// or the address may be unroutable.  Discovery is best-effort by nature.
	SDLNet_UDP_Send(sock, -1, probe);
}

int network_discover(NetworkHostInfo *out, int max, Uint32 timeout_ms)
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
	const int local_count = SDLNet_GetLocalAddresses(local, (int)COUNTOF(local));

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

		SDL_Delay(5);
	}

	SDLNet_FreePacket(probe);
	SDLNet_FreePacket(reply);
	SDLNet_UDP_Close(sock);
	SDLNet_Quit();

	return found;
}

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

	socket = SDLNet_UDP_Open(network_player_port);
	if (!socket)
	{
		fprintf(stderr, "error: SDLNet_UDP_Open: %s\n", SDLNet_GetError());
		return -2;
	}

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

#endif

void JE_clearSpecialRequests(void)
{
	pauseRequest = false;
	inGameMenuRequest = false;
	skipLevelRequest = false;
	helpRequest = false;
	nortShipRequest = false;
}
