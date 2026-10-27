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
#ifndef SHIP_SKIN_H
#define SHIP_SKIN_H

#include "lvlmast.h"
#include "opentyr.h"
#include "sprite.h"

#include <stddef.h>

enum { SHIP_SKIN_NONE = 0 };
enum { SHIP_SKIN_ORDER_MAX = 16 + SHIP_DRAGONWING };

void shipSkinReset(void);
JE_byte shipSkinGet(uint seat);
void shipSkinSet(uint seat, JE_byte graphic);

bool shipSkinOffered(uint seat, int graphic);
JE_byte shipSkinEffective(uint seat);
bool shipSkinFitsHull(JE_word gr);
bool shipSkinWearable(uint seat);
JE_byte shipSkinWorn(uint seat, const Sprite2_array *sheet, JE_word gr);
int shipSkinOrder(uint seat, const Sprite2_array *sheet, JE_word gr, int *order, int capacity);
bool shipSkinHasChoices(uint seat, const Sprite2_array *sheet, JE_word gr);
void shipSkinCycle(uint seat, const Sprite2_array *sheet, JE_word gr, int dir);
const char *shipSkinName(int graphic, char *buf, size_t bufSize);

JE_byte shipSkinApply(uint seat, Sprite2_array **sheet, JE_word *gr);

#endif /* SHIP_SKIN_H */
