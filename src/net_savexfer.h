/*
 * OpenTyrian: A modern cross-platform port of Tyrian
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
#ifndef NET_SAVEXFER_H
#define NET_SAVEXFER_H

#include "config.h"
#include "opentyr.h"

/* Blocking LAN save-transfer screens. They own their sockets and must not run during a live
 * network session. */

bool saveXferAvailable(void);

void saveXferUpload(JE_byte slot);

// A successful download remains pending until the caller opens the destination-slot picker.
bool saveXferDownload(void);

// Replace all save slots in place, including online ownership and Endless records.
void savesXferUpload(void);
bool savesXferDownload(void);

const JE_SaveFileType *saveXferPending(void);

bool saveXferPendingTwoPlayer(void);

// Apply and persist the pending record. Return false when no download is pending.
bool saveXferPendingApply(JE_byte slot, const char *name);

void saveXferPendingClear(void);

// Each transfer replaces only the selected content; none imports unrelated settings.
void shipsXferUpload(void);
bool shipsXferDownload(void);
void weaponsXferUpload(void);
bool weaponsXferDownload(void);
void customXferUpload(void);
bool customXferDownload(void);
void allXferUpload(void);
bool allXferDownload(void);

#endif /* NET_SAVEXFER_H */
