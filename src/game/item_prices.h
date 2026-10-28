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
#ifndef ITEM_PRICES_H
#define ITEM_PRICES_H

#include "opentyr.h"
#include "player.h"

extern Sint64 upgradeCost;
extern Sint64 downgradeCost;

enum
{
	ITEM_PART_HULL, ITEM_PART_FRONT, ITEM_PART_REAR, ITEM_PART_SHIELD, ITEM_PART_GENERATOR,
	ITEM_PART_LEFT, ITEM_PART_RIGHT, ITEM_PARTS
};

Sint64 weapon_upgrade_cost(Sint64 base_cost, unsigned int power);
Sint64 weapon_level_cost(JE_word port, unsigned int level);
Uint8 *playeritem_map(PlayerItems *items, uint i);
Sint64 JE_getCost(JE_byte itemType, JE_word itemNum);
Sint64 JE_totalScore(const Player *);

#endif /* ITEM_PRICES_H */
