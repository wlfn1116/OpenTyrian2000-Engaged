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
#ifndef EDITSHIP_H
#define EDITSHIP_H

#include "opentyr.h"
#include "sprite.h"

/* Serialized table width; preserve the byte layout. */
typedef JE_byte JE_ShipsType[154]; /* [1..154] */

extern JE_boolean extraAvail;
extern JE_ShipsType extraShips;
extern Sprite2_array extraShapes;

JE_boolean JE_decryptShips(void);
void JE_encryptShips(JE_ShipsType dst);
void JE_loadExtraShapes(void);
void JE_freeExtraShapes(void);
void JE_shipEditor(void);
bool JE_shapeCodecSelfTest(void);

/* Online, each seat flies the ships of its own compiled file; the files are exchanged
 * at the session rendezvous so both machines hold identical seat slots. Offline both
 * seats read the local file. playerIdx is the player array index, 0 or 1. */
JE_byte *extraShipsFor(uint playerIdx);
Sprite2_array *extraShapesFor(uint playerIdx);
bool extraAvailFor(uint playerIdx);

/* A weapon byte of 255 means "the custom weapon of whichever seat flies this ship". The
 * reserved port differs per seat and per machine, so a record stores this sentinel and the
 * live port is resolved when the ship is equipped. Resolves to None when that seat has no
 * custom weapon (the feature is off, or no port was free). */
#define EXTRA_SHIP_CUSTOM_PORT 255
JE_byte extraShipResolvePort(uint seat, JE_byte port);

// Whether any local record equips the custom weapon, and so needs the design on the wire
// even when this machine's own Weapon Creator toggle is off.
bool extraShipsUseCustomWeapon(void);

/* Wire form: version, availability, the plaintext table, then the sprite blob. */
#define EXTRA_SHIPS_WIRE_MAX (6 + sizeof(JE_ShipsType) + UINT16_MAX)
size_t extraShipsSerialize(Uint8 *buf, size_t max);
bool extraShipsAdopt(uint seat, const Uint8 *buf, size_t len);
void extraShipsNetInstallLocal(uint seat);
void extraShipsNetReset(void);

#endif /* EDITSHIP_H */
