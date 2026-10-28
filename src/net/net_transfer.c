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
#define NET_INTERNAL_ACCESS

#include "network.h"
#include "net_internal.h"

#include "config.h"
#include "crashlog.h"
#include "custom_episode.h"
#include "custom_weapon.h"
#include "destruct_rollback.h"
#include "extra_ships.h"
#include "fnv1a.h"
#include "keyboard.h"
#include "opentyr.h"

#include <stdlib.h>

/* Custom weapon design exchange. The receiver acknowledges a completed generation separately
 * from transport delivery; see doc/notes.md#custom-weapons. */
void network_chunk_prepare(Uint16 type, Uint8 kind, Uint16 gen, Uint16 chunk, Uint16 count,
                           Uint16 len)
{
	network_prepare(type);
	packet_out_temp->data[NCW_OWNER] = (Uint8)thisPlayerNum;
	packet_out_temp->data[NCW_OWNER + 1] = kind;
	SDLNet_Write16(gen, &packet_out_temp->data[NCW_GEN]);
	SDLNet_Write16(chunk, &packet_out_temp->data[NCW_CHUNK]);
	SDLNet_Write16(count, &packet_out_temp->data[NCW_COUNT]);
	SDLNet_Write16(len, &packet_out_temp->data[NCW_LEN]);
}

// Consume an early PACKET_WAITING so transfer acknowledgements behind it remain reachable.
bool network_waiting_pending;

static bool network_waiting_pump(void)
{
	if (network_inbound_head() != PACKET_WAITING)
		return false;

	network_update();
	network_waiting_pending = true;
	return true;
}

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
	network_waiting_pending = false;
	customWeaponWithholdLocal(false);
}

static void network_custom_send_ack(Uint16 gen)
{
	network_chunk_prepare(PACKET_CUSTOM_WEAPON, 0, gen, 0, 0, 0);
	network_send(NCW_HDR);
}

bool network_custom_weapon_receive(void)
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

/* Publish while both peers pump incoming packets. */
void network_custom_weapon_publish(void)
{
	if (!isNetworkGame || !custom_ships_multiplayer_mode() ||
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
	Uint32 hash = fnv1a32(stream, total);
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

				network_chunk_prepare(PACKET_CUSTOM_WEAPON, 0, gen, (Uint16)sent, (Uint16)chunks,
				                      (Uint16)plen);
				memcpy(&packet_out_temp->data[NCW_HDR], stream + from, plen);
				network_send(NCW_HDR + (int)plen);
				++sent;
			}

			watchdog_heartbeat();
			service_SDL_events(false);
			network_check();

			// Drain our own inbound: the peer publishes from the same rendezvous, and its
			// acknowledgement is behind whatever it has already sent us.
			while (network_shop_pump() || network_debug_sync_pump(false) || network_waiting_pump())
				;

			/* Retire stale handshakes, but leave quit packets for the quit handler. */
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
		                     "the peer never acknowledged the design; both machines fly this "
		                     "ship with the placeholder weapon until the next outpost.");
		network_custom_out_hash = 0;  // start over at the next publish window
	}
	else
	{
		network_custom_out_hash = hash;
	}
	customWeaponWithholdLocal(!network_custom_acked);

	free(stream);
}

/* Chunked per-seat extra-ship file exchange. See doc/notes.md#extra-ships. */

static Uint16 network_ships_gen;
static Uint32 network_ships_out_hash;
static bool   network_ships_acked;

static int    network_ships_in_owner = -1;
static Uint16 network_ships_in_gen;
static Uint32 network_ships_in_count;
static Uint32 network_ships_in_have;
static size_t network_ships_in_len;
static Uint8 *network_ships_in_buf;
static Uint8 *network_ships_in_seen;

static void network_ships_in_reset(void)
{
	network_ships_in_owner = -1;
	network_ships_in_count = 0;
	network_ships_in_have = 0;
	network_ships_in_len = 0;
	free(network_ships_in_buf);
	free(network_ships_in_seen);
	network_ships_in_buf = NULL;
	network_ships_in_seen = NULL;
}

void network_extra_ships_reset(void)
{
	network_ships_in_reset();
	network_ships_out_hash = 0;
	network_ships_acked = false;
	extraShipsNetReset();
}

static void network_ships_send_ack(Uint16 gen)
{
	network_chunk_prepare(PACKET_EXTRA_SHIPS, 0, gen, 0, 0, 0);
	network_send(NCW_HDR);
}

bool network_extra_ships_receive(void)
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
		if (gen == network_ships_gen)
			network_ships_acked = true;
		network_update();
		return true;
	}

	if (chunk >= count || plen > NCW_PAYLOAD || (Uint32)len < NCW_HDR + plen ||
	    (size_t)count * NCW_PAYLOAD > EXTRA_SHIPS_WIRE_MAX)
	{
		network_update();
		return true;
	}

	if (network_ships_in_buf == NULL || network_ships_in_gen != gen ||
	    network_ships_in_count != count || network_ships_in_owner != (int)sender - 1)
	{
		network_ships_in_reset();
		network_ships_in_buf = calloc((size_t)count, NCW_PAYLOAD);
		network_ships_in_seen = calloc((size_t)count, 1);
		if (network_ships_in_buf == NULL || network_ships_in_seen == NULL)
		{
			network_ships_in_reset();
			network_update();
			return true;
		}
		network_ships_in_gen = gen;
		network_ships_in_count = count;
		network_ships_in_owner = (int)sender - 1;
	}

	memcpy(network_ships_in_buf + (size_t)chunk * NCW_PAYLOAD, &packet_in[0]->data[NCW_HDR], plen);
	if (chunk == count - 1)
		network_ships_in_len = (size_t)chunk * NCW_PAYLOAD + plen;
	if (!network_ships_in_seen[chunk])
	{
		network_ships_in_seen[chunk] = 1;
		++network_ships_in_have;
	}

	if (network_ships_in_have >= count && network_ships_in_len > 0)
	{
		if (!extraShipsAdopt((uint)network_ships_in_owner, network_ships_in_buf, network_ships_in_len))
		{
			crashlog_netlog_line("EXTRA SHIPS REFUSED",
			                     "the peer's ship file did not decode; its extra ships stay "
			                     "unavailable and neither Edit Player list offers them.");
		}
		network_ships_in_reset();

		// A decode failure will not improve by resending identical bytes.
		network_ships_send_ack(gen);
	}

	network_update();
	return true;
}

// The session hash gate suppresses unchanged files.
void network_extra_ships_publish(void)
{
	if (!isNetworkGame || !custom_ships_multiplayer_mode() ||
	    thisPlayerNum < 1 || thisPlayerNum > 2 || !network_peer_alive())
		return;

	extraShipsNetInstallLocal((uint)(thisPlayerNum - 1));

	Uint8 *const stream = malloc(EXTRA_SHIPS_WIRE_MAX);
	if (stream == NULL)
		return;

	const size_t total = extraShipsSerialize(stream, EXTRA_SHIPS_WIRE_MAX);
	if (total == 0)
	{
		free(stream);
		return;
	}

	Uint32 hash = fnv1a32(stream, total);
	if (hash == 0)
		hash = 1;  // 0 means "nothing published yet"
	if (hash == network_ships_out_hash)
	{
		free(stream);
		return;
	}

	const Uint32 chunks = (Uint32)((total + NCW_PAYLOAD - 1) / NCW_PAYLOAD);
	const Uint16 gen = ++network_ships_gen;
	network_ships_acked = false;
	bool peer_left = false;

	for (int attempt = 0; attempt < NCW_ATTEMPTS && !network_ships_acked && !peer_left; ++attempt)
	{
		const Uint32 started = SDL_GetTicks();
		Uint32 sent = 0;

		while (!network_ships_acked && SDL_GetTicks() - started < NCW_ATTEMPT_MS)
		{
			// Reserve half the reliable queue for other traffic.
			while (sent < chunks && network_ack_backlog() < NET_PACKET_QUEUE / 2)
			{
				const size_t from = (size_t)sent * NCW_PAYLOAD;
				const size_t plen = MIN(total - from, (size_t)NCW_PAYLOAD);

				network_chunk_prepare(PACKET_EXTRA_SHIPS, 0, gen, (Uint16)sent, (Uint16)chunks,
				                      (Uint16)plen);
				memcpy(&packet_out_temp->data[NCW_HDR], stream + from, plen);
				network_send(NCW_HDR + (int)plen);
				++sent;
			}

			watchdog_heartbeat();
			service_SDL_events(false);
			network_check();

			while (network_shop_pump() || network_debug_sync_pump(false) || network_waiting_pump())
				;

			if (network_inbound_head() == PACKET_CONNECT)
				network_update();
			else if (network_inbound_head() == PACKET_GAME_QUIT)
			{
				peer_left = true;
				break;
			}

			if (!network_peer_alive())
			{
				peer_left = true;
				break;
			}

			SDL_Delay(1);
		}
	}

	if (network_ships_acked)
	{
		network_ships_out_hash = hash;
		extraShipsNetAcknowledged((uint)(thisPlayerNum - 1));
	}
	else
	{
		extraShipsNetRevert((uint)(thisPlayerNum - 1));
	}

	free(stream);
}

/* Container and manifest sync; see doc/notes.md#custom-episode-containers. */

char   network_host_custom_file[64] = "";   /* CUSTOM_EPISODE_FILE_LEN; empty = stock */
Uint32 network_host_custom_size = 0;
Uint32 network_host_custom_hash = 0;
int    network_host_custom_endless = 0;     /* CUSTOM_ENDLESS_*, host-dictated */

#define NCL_REQUEST       0xFFFF
#define NCL_OFFER         0xFFFE
#define NCL_GEN_FAILED    0xFFFE
#define NCL_CONTAINER_MAX CUSTOM_EPISODE_BYTES_MAX
#define NCL_GEN_ALL_DONE  0xFFFF
#define NCL_TRANSFER_MS   90000   /* per-stream deadline; a slow link, not a hang */
#define NCL_RESEND_MS     15000   /* restream when a full pass went unacknowledged */

/* Stream counts must remain below NCL_OFFER. */
SDL_COMPILE_TIME_ASSERT(ncl_container_fits,
                        NCL_CONTAINER_MAX <= (Uint32)(NCL_OFFER - 1) * NCW_PAYLOAD);

#define NCL_KIND_CONTAINER 0
#define NCL_KIND_MANIFEST  1

#define NCL_MANIFEST_RECORD (CUSTOM_EPISODE_FILE_LEN + 8)
#define NCL_MANIFEST_MAX    (2 + CUSTOM_EPISODE_MAX * NCL_MANIFEST_RECORD)

static Uint16 network_clv_gen;
bool   network_clv_syncing;
static bool   network_clv_offer_seen;
static char   network_clv_offer_name[CUSTOM_EPISODE_FILE_LEN];
static Uint32 network_clv_offer_size;
static Uint32 network_clv_offer_hash;
static bool   network_clv_failed;
static bool   network_clv_request_seen;        // A peer requested a container.
static char   network_clv_wanted[CUSTOM_EPISODE_FILE_LEN];  // Empty means the advertised file.
static bool   network_clv_manifest_requested;
static bool   network_clv_peer_done;           // The peer confirmed the running stream.
static bool   network_clv_all_done;             // The peer settled the required set.
static bool   network_clv_seat_noted;

// Expected identity for the current download.
static char   network_clv_expect_name[CUSTOM_EPISODE_FILE_LEN];
static Uint32 network_clv_expect_size;
static Uint32 network_clv_expect_hash;

static Uint8  network_clv_in_kind;
static Uint16 network_clv_in_gen;
static Uint32 network_clv_in_count;
static Uint32 network_clv_in_have;
static Uint32 network_clv_in_arrived;   // Last chunk time; retry only after silence.
static size_t network_clv_in_len;
static Uint8 *network_clv_in_buf;
static Uint8 *network_clv_in_seen;
static bool   network_clv_in_done;             // The expected container is stored.

static Uint8  network_clv_manifest[NCL_MANIFEST_MAX];
static size_t network_clv_manifest_len;        // 0 = none received yet

static void network_clv_in_reset(void)
{
	network_clv_in_count = 0;
	network_clv_in_have = 0;
	network_clv_in_len = 0;
	free(network_clv_in_buf);
	free(network_clv_in_seen);
	network_clv_in_buf = NULL;
	network_clv_in_seen = NULL;
}

void network_custom_level_session_reset(void)
{
	network_clv_request_seen = false;
	network_clv_manifest_requested = false;
	network_clv_peer_done = false;
	network_clv_all_done = false;
	network_clv_in_done = false;
	network_clv_wanted[0] = '\0';
	network_clv_expect_name[0] = '\0';
	network_clv_expect_size = 0;
	network_clv_expect_hash = 0;
	network_clv_manifest_len = 0;
	network_clv_offer_seen = false;
	network_clv_offer_name[0] = '\0';
	network_clv_offer_size = 0;
	network_clv_offer_hash = 0;
	network_clv_failed = false;
	network_clv_seat_noted = false;
	network_clv_in_reset();
	customEpisodeSessionEnd();
}

static void network_clv_send_ack(Uint16 gen)
{
	network_chunk_prepare(PACKET_CUSTOM_LEVEL, NCL_KIND_CONTAINER, gen, 0, 0, 0);
	network_send(NCW_HDR);
}

/* Requests a manifest or a named container; NULL means the advertised one. */
static void network_clv_send_request(Uint8 kind, const char *name)
{
	size_t plen = (name != NULL) ? strlen(name) : 0;
	if (plen >= CUSTOM_EPISODE_FILE_LEN)
		plen = CUSTOM_EPISODE_FILE_LEN - 1;

	network_chunk_prepare(PACKET_CUSTOM_LEVEL, kind, 0, 0, NCL_REQUEST, (Uint16)plen);
	if (plen > 0)
		memcpy(&packet_out_temp->data[NCW_HDR], name, plen);
	network_send(NCW_HDR + (int)plen);
}

static void network_clv_send_offer(const char *name, Uint32 size, Uint32 hash)
{
	network_chunk_prepare(PACKET_CUSTOM_LEVEL, NCL_KIND_CONTAINER, 0, 0, NCL_OFFER,
	                      (Uint16)(CUSTOM_EPISODE_FILE_LEN + 8));
	memset(&packet_out_temp->data[NCW_HDR], 0, CUSTOM_EPISODE_FILE_LEN);
	memcpy(&packet_out_temp->data[NCW_HDR], name, strlen(name));
	SDLNet_Write32(size, &packet_out_temp->data[NCW_HDR + CUSTOM_EPISODE_FILE_LEN]);
	SDLNet_Write32(hash, &packet_out_temp->data[NCW_HDR + CUSTOM_EPISODE_FILE_LEN + 4]);
	network_send(NCW_HDR + CUSTOM_EPISODE_FILE_LEN + 8);
}

static Uint32 network_clv_hash(const Uint8 *data, size_t len)
{
	const Uint32 hash = fnv1a32(data, len);
	return hash != 0 ? hash : 1;
}

static void network_clv_consume(void)
{
	const int len = packet_in[0]->len;
	if (len < NCW_HDR)
	{
		network_update();
		return;
	}

	const uint   sender = packet_in[0]->data[NCW_OWNER];
	const Uint8  kind   = packet_in[0]->data[NCW_OWNER + 1];
	const Uint16 gen    = SDLNet_Read16(&packet_in[0]->data[NCW_GEN]);
	const Uint32 chunk  = SDLNet_Read16(&packet_in[0]->data[NCW_CHUNK]);
	const Uint32 count  = SDLNet_Read16(&packet_in[0]->data[NCW_COUNT]);
	const Uint32 plen   = SDLNet_Read16(&packet_in[0]->data[NCW_LEN]);

	if (sender < 1 || sender > 2 ||
	    (kind != NCL_KIND_CONTAINER && kind != NCL_KIND_MANIFEST))
	{
		network_update();
		return;
	}

	if (sender == thisPlayerNum && !network_clv_seat_noted)
	{
		network_clv_seat_noted = true;
		crashlog_netlog_line("CUSTOM LEVEL",
		                     "the peer's transfer arrived under this machine's seat");
	}

	if (count == NCL_REQUEST)
	{
		if (kind == NCL_KIND_MANIFEST)
			network_clv_manifest_requested = true;
		else
		{
			char name[CUSTOM_EPISODE_FILE_LEN];
			const size_t n = MIN((size_t)plen, sizeof(name) - 1);
			if ((Uint32)len >= NCW_HDR + n)
			{
				memcpy(name, &packet_in[0]->data[NCW_HDR], n);
				name[n] = '\0';
				if (name[0] == '\0' || customEpisodeFileNameValid(name))
				{
					SDL_strlcpy(network_clv_wanted, name, sizeof(network_clv_wanted));
					network_clv_request_seen = true;
				}
			}
		}
		network_update();
		return;
	}

	if (count == NCL_OFFER)
	{
		if (kind == NCL_KIND_CONTAINER && plen == CUSTOM_EPISODE_FILE_LEN + 8 &&
		    (Uint32)len >= NCW_HDR + plen)
		{
			char name[CUSTOM_EPISODE_FILE_LEN];
			memcpy(name, &packet_in[0]->data[NCW_HDR], CUSTOM_EPISODE_FILE_LEN);
			name[CUSTOM_EPISODE_FILE_LEN - 1] = '\0';
			if (customEpisodeFileNameValid(name))
			{
				SDL_strlcpy(network_clv_offer_name, name, sizeof(network_clv_offer_name));
				network_clv_offer_size = SDLNet_Read32(
					&packet_in[0]->data[NCW_HDR + CUSTOM_EPISODE_FILE_LEN]);
				network_clv_offer_hash = SDLNet_Read32(
					&packet_in[0]->data[NCW_HDR + CUSTOM_EPISODE_FILE_LEN + 4]);
				network_clv_offer_seen = true;
			}
		}
		network_update();
		return;
	}

	if (count == 0)
	{
		if (gen == NCL_GEN_ALL_DONE)
			network_clv_all_done = true;
		else if (gen == NCL_GEN_FAILED)
			network_clv_failed = true;
		else
			network_clv_peer_done = true;
		network_update();
		return;
	}

	// Bound streams by the advertised identity or manifest limit.
	const Uint64 bound = kind == NCL_KIND_MANIFEST
	                   ? (Uint64)NCL_MANIFEST_MAX
	                   : (Uint64)network_clv_expect_size;
	if (bound == 0 ||
	    chunk >= count || plen > NCW_PAYLOAD || (Uint32)len < NCW_HDR + plen ||
	    (Uint64)count * NCW_PAYLOAD > bound + NCW_PAYLOAD)
	{
		network_update();
		return;
	}

	if (network_clv_in_buf == NULL || network_clv_in_gen != gen ||
	    network_clv_in_count != count || network_clv_in_kind != kind)
	{
		network_clv_in_reset();
		network_clv_in_buf = calloc((size_t)count, NCW_PAYLOAD);
		network_clv_in_seen = calloc((size_t)count, 1);
		if (network_clv_in_buf == NULL || network_clv_in_seen == NULL)
		{
			network_clv_in_reset();
			network_update();
			return;
		}
		network_clv_in_kind = kind;
		network_clv_in_gen = gen;
		network_clv_in_count = count;
	}

	memcpy(network_clv_in_buf + (size_t)chunk * NCW_PAYLOAD, &packet_in[0]->data[NCW_HDR], plen);
	network_clv_in_arrived = SDL_GetTicks();
	if (chunk == count - 1)
		network_clv_in_len = (size_t)chunk * NCW_PAYLOAD + plen;
	if (!network_clv_in_seen[chunk])
	{
		network_clv_in_seen[chunk] = 1;
		++network_clv_in_have;
	}

	if (network_clv_in_have >= count && network_clv_in_len > 0)
	{
		if (kind == NCL_KIND_MANIFEST)
		{
			const size_t mlen = network_clv_in_len;
			const Uint32 mcount = mlen >= 2 ? SDLNet_Read16(&network_clv_in_buf[0]) : 0xffffu;
			if (mcount <= CUSTOM_EPISODE_MAX && mlen == 2 + (size_t)mcount * NCL_MANIFEST_RECORD)
			{
				memcpy(network_clv_manifest, network_clv_in_buf, mlen);
				network_clv_manifest_len = mlen;
				network_clv_send_ack(gen);
			}
			else
				crashlog_netlog_line("CUSTOM LEVEL REFUSED", "the collection manifest did not parse");
		}
		else if (network_clv_in_len == network_clv_expect_size &&
		         network_clv_hash(network_clv_in_buf, network_clv_in_len) == network_clv_expect_hash &&
		         customEpisodeSaveDownloaded(network_clv_expect_name,
		                                     network_clv_in_buf, (Uint32)network_clv_in_len) >= 0)
		{
			network_clv_in_done = true;
			network_clv_send_ack(gen);
		}
		else
		{
			// Wait for the host's next generation after rejecting this one.
			crashlog_netlog_line("CUSTOM LEVEL REFUSED",
			                     "the downloaded container did not match the host's advertisement");
		}
		network_clv_in_reset();
	}

	network_update();
}

static bool network_clv_pump(void)
{
	while (packet_in[0] != NULL && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_CUSTOM_LEVEL)
		network_clv_consume();
	while (network_shop_pump() || network_debug_sync_pump(false) || network_waiting_pump())
		;
	const int head = network_inbound_head();
	if (head == PACKET_CONNECT)
		network_update();
	else if (head == PACKET_QUIT || head == PACKET_GAME_QUIT)
		return false;
	return true;
}

/* Streams one generation until acknowledged. */
static bool network_clv_stream_until_acked(Uint8 kind, const Uint8 *stream, Uint32 total)
{
	const Uint32 chunks = (total + NCW_PAYLOAD - 1) / NCW_PAYLOAD;
	Uint16 gen = ++network_clv_gen;
	while (gen == NCL_GEN_ALL_DONE || gen == NCL_GEN_FAILED)
		gen = ++network_clv_gen;
	Uint32 sent = 0;
	Uint32 progressAt = SDL_GetTicks();
	Uint32 passDoneAt = 0;
	int passes = 0;

	network_clv_peer_done = false;

	while (!network_clv_peer_done)
	{
		// Completion supersedes a late stream request.
		if (network_clv_all_done)
			return true;
		if (network_clv_failed || SDL_GetTicks() - progressAt >= NCL_TRANSFER_MS ||
		    !network_peer_alive())
			return false;

		watchdog_heartbeat();
		service_SDL_events(false);
		network_check();

		if (!network_clv_pump())
			return false;

		if (sent < chunks)
		{
			// Leave half the reliable queue available while streaming.
			while (sent < chunks && network_ack_backlog() < NET_PACKET_QUEUE / 2)
			{
				const size_t from = (size_t)sent * NCW_PAYLOAD;
				const size_t plen = MIN((size_t)total - from, (size_t)NCW_PAYLOAD);

				network_chunk_prepare(PACKET_CUSTOM_LEVEL, kind, gen, (Uint16)sent, (Uint16)chunks,
				                      (Uint16)plen);
				memcpy(&packet_out_temp->data[NCW_HDR], stream + from, plen);
				network_send(NCW_HDR + (int)plen);
				progressAt = SDL_GetTicks();
				if (++sent >= chunks)
					passDoneAt = progressAt;
			}
		}
		else if (passDoneAt != 0 && SDL_GetTicks() - passDoneAt >= NCL_RESEND_MS)
		{
			// Retry an unconfirmed stream with a new generation.
			if (++passes >= NCW_ATTEMPTS)
				return false;
			gen = ++network_clv_gen;
			while (gen == NCL_GEN_ALL_DONE || gen == NCL_GEN_FAILED)
				gen = ++network_clv_gen;
			sent = 0;
			passDoneAt = 0;
			progressAt = SDL_GetTicks();
		}

		SDL_Delay(1);
	}

	return true;
}

/* Serves the advertised container until the joiner confirms it. */
static bool network_custom_level_serve_locked(void)
{
	if (network_host_custom_file[0] == '\0')
		return true;

	customEpisodeScan();
	const int idx = customEpisodeFindByFile(network_host_custom_file);
	Uint32 total = 0;
	Uint8 *const stream = idx >= 0 ? customEpisodeReadWhole(idx, &total) : NULL;
	if (stream == NULL || total == 0)
	{
		free(stream);
		return false;
	}

	const Uint32 started = SDL_GetTicks();
	bool ok = true;

	// Wait for the joiner to confirm or request the container.
	while (!network_clv_peer_done)
	{
		if (SDL_GetTicks() - started >= NCL_TRANSFER_MS || !network_peer_alive())
		{
			ok = false;
			break;
		}

		watchdog_heartbeat();
		service_SDL_events(false);
		network_check();

		if (!network_clv_pump())
		{
			ok = false;
			break;
		}

		if (network_clv_request_seen)
		{
			network_clv_request_seen = false;
			ok = network_clv_stream_until_acked(NCL_KIND_CONTAINER, stream, total);
			break;
		}

		SDL_Delay(1);
	}

	free(stream);
	return ok;
}

bool network_custom_level_serve(void)
{
	network_clv_syncing = true;
	const bool ok = network_custom_level_serve_locked();
	network_clv_syncing = false;
	return ok;
}

/* Fetches the advertised container unless a matching local copy exists. */
static bool network_custom_level_fetch_locked(void)
{
	if (network_host_custom_file[0] == '\0')
		return true;

	customEpisodeScan();
	const int idx = customEpisodeFindByFile(network_host_custom_file);
	if (idx >= 0)
	{
		Uint32 size, hash;
		if (customEpisodeIdentity(idx, &size, &hash) &&
		    size == network_host_custom_size && hash == network_host_custom_hash)
		{
			network_clv_send_ack(0);
			return true;
		}
	}

	SDL_strlcpy(network_clv_expect_name, network_host_custom_file,
	            sizeof(network_clv_expect_name));
	network_clv_expect_size = network_host_custom_size;
	network_clv_expect_hash = network_host_custom_hash;
	network_clv_in_done = false;

	network_clv_in_arrived = 0;
	Uint32 notedArrival = 0;
	Uint32 progressAt = SDL_GetTicks();
	Uint32 askedAt = 0;
	bool asked = false;

	while (!network_clv_in_done)
	{
		if (network_clv_in_arrived != notedArrival)
		{
			notedArrival = network_clv_in_arrived;
			progressAt = notedArrival;
			askedAt = notedArrival;
		}

		if (SDL_GetTicks() - progressAt >= NCL_TRANSFER_MS || !network_peer_alive())
			return false;

		watchdog_heartbeat();
		service_SDL_events(false);
		network_check();

		// Repeat in case the host reset its transfer state.
		if (!asked || SDL_GetTicks() - askedAt >= 3000)
		{
			network_clv_send_request(NCL_KIND_CONTAINER, NULL);
			askedAt = SDL_GetTicks();
			asked = true;
		}

		if (!network_clv_pump())
			return false;

		SDL_Delay(1);
	}

	return true;
}

bool network_custom_level_fetch(void)
{
	network_clv_syncing = true;
	const bool ok = network_custom_level_fetch_locked();
	network_clv_syncing = false;
	return ok;
}

/* Reconciles an ordered dependency manifest with the peer. */
static bool network_clv_serve_required(char names[][CUSTOM_EPISODE_FILE_LEN], int count)
{
	Uint8 manifest[NCL_MANIFEST_MAX];
	SDLNet_Write16((Uint16)count, &manifest[0]);
	size_t mlen = 2;
	for (int i = 0; i < count; ++i)
	{
		Uint32 size = 0, hash = 0;
		const int local = customEpisodeFindByFile(names[i]);
		if (local >= 0 && !customEpisodeIdentity(local, &size, &hash))
		{
			size = 0;
			hash = 0;
		}
		memset(&manifest[mlen], 0, CUSTOM_EPISODE_FILE_LEN);
		memcpy(&manifest[mlen], names[i], strlen(names[i]));
		SDLNet_Write32(size, &manifest[mlen + CUSTOM_EPISODE_FILE_LEN]);
		SDLNet_Write32(hash, &manifest[mlen + CUSTOM_EPISODE_FILE_LEN + 4]);
		mlen += NCL_MANIFEST_RECORD;
	}

	const Uint32 started = SDL_GetTicks();

	while (!network_clv_all_done)
	{
		if (network_clv_failed || SDL_GetTicks() - started >= NCL_TRANSFER_MS * 8u ||
		    !network_peer_alive())
			return false;

		watchdog_heartbeat();
		service_SDL_events(false);
		network_check();

		if (!network_clv_pump())
			return false;

		// Ignore requests queued before completion.
		if (network_clv_all_done)
			break;

		if (network_clv_manifest_requested)
		{
			network_clv_manifest_requested = false;
			if (!network_clv_stream_until_acked(NCL_KIND_MANIFEST, manifest, (Uint32)mlen))
				return false;
		}

		if (network_clv_all_done)
			break;

		if (network_clv_request_seen)
		{
			network_clv_request_seen = false;
			const char *const want = network_clv_wanted[0] != '\0'
			                       ? network_clv_wanted : network_host_custom_file;
			const int idx = customEpisodeFindByFile(want);
			Uint32 total = 0;
			Uint8 *const bytes = idx >= 0 ? customEpisodeReadWhole(idx, &total) : NULL;
			if (bytes == NULL || total == 0)
			{
				free(bytes);
				return false;
			}
			const bool sentOk = network_clv_stream_until_acked(NCL_KIND_CONTAINER, bytes, total);
			free(bytes);
			if (!sentOk)
				return false;
		}

		if (network_clv_offer_seen)
		{
			network_clv_offer_seen = false;
			bool wantedHere = false;
			for (int i = 0; i < count && !wantedHere; ++i)
				wantedHere = SDL_strcasecmp(names[i], network_clv_offer_name) == 0;
			if (wantedHere && customEpisodeFindByFile(network_clv_offer_name) < 0 &&
			    network_clv_offer_size > 0 && network_clv_offer_size <= NCL_CONTAINER_MAX)
			{
				SDL_strlcpy(network_clv_expect_name, network_clv_offer_name,
				            sizeof(network_clv_expect_name));
				network_clv_expect_size = network_clv_offer_size;
				network_clv_expect_hash = network_clv_offer_hash;
				network_clv_in_done = false;
				network_clv_in_reset();
				network_clv_in_arrived = 0;
				network_clv_send_request(NCL_KIND_CONTAINER, network_clv_offer_name);
				Uint32 pullNoted = 0;
				Uint32 pullProgressAt = SDL_GetTicks();
				Uint32 pullAskedAt = SDL_GetTicks();
				while (!network_clv_in_done)
				{
					if (network_clv_in_arrived != pullNoted)
					{
						pullNoted = network_clv_in_arrived;
						pullProgressAt = pullNoted;
						pullAskedAt = pullNoted;
					}
					if (network_clv_failed ||
					    SDL_GetTicks() - pullProgressAt >= NCL_TRANSFER_MS ||
					    !network_peer_alive())
						return false;
					watchdog_heartbeat();
					service_SDL_events(false);
					network_check();
					if (!network_clv_pump())
						return false;
					if (SDL_GetTicks() - pullAskedAt >= 3000)
					{
						network_clv_send_request(NCL_KIND_CONTAINER, network_clv_offer_name);
						pullAskedAt = SDL_GetTicks();
					}
					SDL_Delay(1);
				}
			}
		}

		SDL_Delay(1);
	}

	return true;
}

static bool network_clv_fetch_required(int sessionMode)
{
	customEpisodeScan();

	network_clv_manifest_len = 0;
	Uint32 started = SDL_GetTicks();
	Uint32 askedAt = 0;
	bool asked = false;
	while (network_clv_manifest_len == 0)
	{
		if (network_clv_failed || SDL_GetTicks() - started >= NCL_TRANSFER_MS ||
		    !network_peer_alive())
			return false;

		watchdog_heartbeat();
		service_SDL_events(false);
		network_check();

		if (!asked || SDL_GetTicks() - askedAt >= 3000)
		{
			network_clv_send_request(NCL_KIND_MANIFEST, NULL);
			askedAt = SDL_GetTicks();
			asked = true;
		}

		if (!network_clv_pump())
			return false;

		SDL_Delay(1);
	}

	const int count = (int)SDLNet_Read16(&network_clv_manifest[0]);
	static char names[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
	for (int i = 0; i < count; ++i)
	{
		const Uint8 *const rec = &network_clv_manifest[2 + (size_t)i * NCL_MANIFEST_RECORD];
		memcpy(names[i], rec, CUSTOM_EPISODE_FILE_LEN);
		names[i][CUSTOM_EPISODE_FILE_LEN - 1] = '\0';
		if (!customEpisodeFileNameValid(names[i]))
			return false;
		const Uint32 wantSize = SDLNet_Read32(&rec[CUSTOM_EPISODE_FILE_LEN]);
		const Uint32 wantHash = SDLNet_Read32(&rec[CUSTOM_EPISODE_FILE_LEN + 4]);
		const int local = customEpisodeFindByFile(names[i]);

		if (wantSize == 0)
		{
			Uint32 mySize = 0, myHash = 0;
			if (local < 0 || !customEpisodeIdentity(local, &mySize, &myHash) || mySize == 0)
			{
				network_clv_send_ack(NCL_GEN_FAILED);
				return false;
			}
			Uint32 total = 0;
			Uint8 *const bytes = customEpisodeReadWhole(local, &total);
			if (bytes == NULL || total == 0)
			{
				free(bytes);
				network_clv_send_ack(NCL_GEN_FAILED);
				return false;
			}
			network_clv_request_seen = false;
			network_clv_send_offer(names[i], mySize, myHash);
			started = SDL_GetTicks();
			askedAt = SDL_GetTicks();
			bool sentOk = false;
			while (!sentOk)
			{
				if (network_clv_failed || SDL_GetTicks() - started >= NCL_TRANSFER_MS ||
				    !network_peer_alive())
				{
					free(bytes);
					return false;
				}
				watchdog_heartbeat();
				service_SDL_events(false);
				network_check();
				if (!network_clv_pump())
				{
					free(bytes);
					return false;
				}
				if (network_clv_request_seen &&
				    SDL_strcasecmp(network_clv_wanted, names[i]) == 0)
				{
					network_clv_request_seen = false;
					if (!network_clv_stream_until_acked(NCL_KIND_CONTAINER, bytes, total))
					{
						free(bytes);
						return false;
					}
					sentOk = true;
				}
				else if (SDL_GetTicks() - askedAt >= 3000)
				{
					network_clv_send_offer(names[i], mySize, myHash);
					askedAt = SDL_GetTicks();
				}
				SDL_Delay(1);
			}
			free(bytes);
			continue;
		}

		{
			Uint32 mySize = 0, myHash = 0;
			if (local >= 0 && customEpisodeIdentity(local, &mySize, &myHash) &&
			    mySize == wantSize && myHash == wantHash)
				continue;
		}

		SDL_strlcpy(network_clv_expect_name, names[i], sizeof(network_clv_expect_name));
		network_clv_expect_size = wantSize;
		network_clv_expect_hash = wantHash;
		network_clv_in_done = false;
		network_clv_in_reset();
		network_clv_in_arrived = 0;
		Uint32 notedArrival = 0;
		Uint32 progressAt = SDL_GetTicks();
		askedAt = 0;
		asked = false;
		while (!network_clv_in_done)
		{
			if (network_clv_in_arrived != notedArrival)
			{
				notedArrival = network_clv_in_arrived;
				progressAt = notedArrival;
				askedAt = notedArrival;
			}

			if (network_clv_failed || SDL_GetTicks() - progressAt >= NCL_TRANSFER_MS ||
			    !network_peer_alive())
				return false;

			watchdog_heartbeat();
			service_SDL_events(false);
			network_check();

			// Retry only after the stream goes quiet.
			if (!asked || SDL_GetTicks() - askedAt >= 3000)
			{
				network_clv_send_request(NCL_KIND_CONTAINER, names[i]);
				askedAt = SDL_GetTicks();
				asked = true;
			}

			if (!network_clv_pump())
				return false;

			SDL_Delay(1);
		}
	}

	if (sessionMode >= 0)
		customEpisodeSessionBegin(names, count, sessionMode);
	network_clv_send_ack(NCL_GEN_ALL_DONE);
	return true;
}

static bool network_custom_endless_serve_locked(void)
{
	customEpisodeScan();
	const int count = customEpisodeCount();
	static char names[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
	for (int i = 0; i < count; ++i)
		SDL_strlcpy(names[i], customEpisodeFile(i), sizeof(names[i]));

	if (!network_clv_serve_required(names, count))
		return false;

	customEpisodeSessionBegin(names, count, network_host_custom_endless);
	return true;
}

bool network_custom_endless_serve(void)
{
	network_clv_syncing = true;
	const bool ok = network_custom_endless_serve_locked();
	network_clv_syncing = false;
	return ok;
}

bool network_custom_endless_fetch(void)
{
	network_clv_syncing = true;
	const bool ok = network_clv_fetch_required(
		(network_host_custom_endless == CUSTOM_ENDLESS_MIXED ||
		 network_host_custom_endless == CUSTOM_ENDLESS_ONLY)
		? network_host_custom_endless : CUSTOM_ENDLESS_OFF);
	network_clv_syncing = false;
	return ok;
}

bool network_custom_required_serve(char names[][CUSTOM_EPISODE_FILE_LEN], int count, int sessionMode)
{
	network_clv_syncing = true;
	const bool ok = network_clv_serve_required(names, count);
	if (ok && sessionMode >= 0)
		customEpisodeSessionBegin(names, count, sessionMode);
	else if (!ok && !network_clv_failed)
		network_clv_send_ack(NCL_GEN_FAILED);
	network_clv_syncing = false;
	return ok;
}

bool network_custom_required_fetch(int sessionMode)
{
	network_clv_syncing = true;
	const bool ok = network_clv_fetch_required(sessionMode);
	if (!ok && !network_clv_failed)
		network_clv_send_ack(NCL_GEN_FAILED);
	network_clv_syncing = false;
	return ok;
}

/* Activates the advertised container. */
bool networkCustomEpisodeActivate(void)
{
	customEpisodeScan();
	const int idx = customEpisodeFindByFile(network_host_custom_file);
	return idx >= 0 && JE_initEpisodeCustom(idx);
}
