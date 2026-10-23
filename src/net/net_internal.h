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
#ifndef NET_INTERNAL_H
#define NET_INTERNAL_H

#ifndef NET_INTERNAL_ACCESS
#error "net_internal.h is private to src/net; include network.h"
#endif

#include "network.h"
#include "opentyr.h"

#define NCW_OWNER      4    /* Uint8:  publishing player number, 1 or 2      */
#define NCW_GEN        6    /* Uint16: publication, so a stale stream is dropped */
#define NCW_CHUNK      8    /* Uint16: chunk index                           */
#define NCW_COUNT     10    /* Uint16: chunk total, or 0 for an acknowledgement */
#define NCW_LEN       12    /* Uint16: payload bytes in this chunk            */
#define NCW_HDR       14
#define NCW_PAYLOAD   (NET_PACKET_SIZE - NCW_HDR)
#define NCW_ATTEMPTS   3    /* whole-stream retries before giving up          */
#define NCW_ATTEMPT_MS 6000 /* ms to get one attempt acknowledged             */

extern bool network_waiting_pending;
extern bool network_clv_syncing;

void network_chunk_prepare(Uint16 type, Uint8 kind, Uint16 gen, Uint16 chunk, Uint16 count,
                           Uint16 len);
bool network_custom_weapon_receive(void);
void network_custom_weapon_reset(void);
bool network_extra_ships_receive(void);
void network_extra_ships_reset(void);
void network_custom_level_session_reset(void);

#endif /* NET_INTERNAL_H */
