/*
 * OpenTyrian: A modern cross-platform port of Tyrian
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
#ifndef NET_SAVEXFER_H
#define NET_SAVEXFER_H

#include "config.h"
#include "opentyr.h"

/* Copying a save slot between two machines on the same network. The sending machine offers one
 * slot; the receiving machine finds it, pulls it, and writes it into a slot of its own choosing
 * under a name of its own choosing. Everything else in the record crosses unchanged.
 *
 * Both screens are blocking and own their own socket, so neither may be entered from a live
 * session; the load screen offers them only on the title screen's own page. */

// Whether this build can transfer saves at all. False without network support compiled in.
bool saveXferAvailable(void);

// Offer `slot` until another machine takes it or the player backs out.
void saveXferUpload(JE_byte slot);

/* Pull a save from a machine offering one. True arms the pending record: the caller then runs the
 * save-slot picker and JE_operation writes it through saveXferPendingApply. */
bool saveXferDownload(void);

// The record waiting for a slot, or NULL when no download is pending.
const JE_SaveFileType *saveXferPending(void);

// Which load-screen page the pending record belongs to, taken from the slot it was sent from.
bool saveXferPendingTwoPlayer(void);

/* Write the pending record into `slot` under `name` and persist. Only the slot number and the
 * name differ from the machine that sent it. False when nothing is pending, which is every
 * ordinary save. */
bool saveXferPendingApply(JE_byte slot, const char *name);

void saveXferPendingClear(void);

#endif /* NET_SAVEXFER_H */
