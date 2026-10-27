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
#include "ship_skin.h"

#include "extra_ships.h"
#include "ingame_menu.h"
#include "network.h"
#include "player.h"
#include "sprite_picker.h"

#include "SDL.h"

#include <string.h>

static JE_byte shipSkin[2];

void shipSkinReset(void)
{
	memset(shipSkin, SHIP_SKIN_NONE, sizeof(shipSkin));
}

JE_byte shipSkinGet(uint seat)
{
	return (seat < COUNTOF(shipSkin)) ? shipSkin[seat] : SHIP_SKIN_NONE;
}

void shipSkinSet(uint seat, JE_byte graphic)
{
	if (seat < COUNTOF(shipSkin))
		shipSkin[seat] = graphic;
}

static bool shipSkinSingleHull(JE_word gr)
{
	return gr > 1;
}

bool shipSkinFitsHull(JE_word gr)
{
	return shipSkinSingleHull(gr) && extraShipsAllowedInGame();
}

static bool shipSkinHasInk(const Sprite2_array *sheet, JE_word gr)
{
	Uint8 pixels[2 * SPRITE2_CELL_H][2 * SPRITE2_CELL_W];
	sprite2x2_decode(*sheet, gr, pixels);
	for (int y = 0; y < 2 * SPRITE2_CELL_H; ++y)
		for (int x = 0; x < 2 * SPRITE2_CELL_W; ++x)
			if (pixels[y][x] != 0)
				return true;
	return false;
}

bool shipSkinOffered(uint seat, int graphic)
{
	if (graphic < 1 || graphic > extraShipGraphicMax() || extraShipGraphicIsWide(graphic))
		return false;
	if (!extraShipGraphicIsCustom(graphic))
		return true;
	if (!extraAvailFor(seat))
		return false;

	Sprite2_array *sheet;
	const JE_word gr = extraShipGraphicSprite(seat, graphic, &sheet);
	return shipSkinHasInk(sheet, gr);
}

JE_byte shipSkinEffective(uint seat)
{
	const JE_byte skin = shipSkinGet(seat);
	return shipSkinOffered(seat, skin) ? skin : SHIP_SKIN_NONE;
}

bool shipSkinWearable(uint seat)
{
	Sprite2_array *sheet;
	return seat < COUNTOF(shipSkin) && shipSkinFitsHull(player_seat_hull(seat, &sheet));
}

static bool shipSkinRestyles(uint seat, int graphic, const Sprite2_array *sheet, JE_word gr)
{
	Sprite2_array *skinSheet;
	return extraShipGraphicSprite(seat, graphic, &skinSheet) != gr || skinSheet != sheet;
}

JE_byte shipSkinWorn(uint seat, const Sprite2_array *sheet, JE_word gr)
{
	if (!shipSkinFitsHull(gr))
		return SHIP_SKIN_NONE;

	const JE_byte skin = shipSkinEffective(seat);
	if (skin == SHIP_SKIN_NONE || !shipSkinRestyles(seat, skin, sheet, gr))
		return SHIP_SKIN_NONE;
	return skin;
}

int shipSkinOrder(uint seat, const Sprite2_array *sheet, JE_word gr, int *order, int capacity)
{
	int count = 0;
	if (count < capacity)
		order[count++] = SHIP_SKIN_NONE;

	const int max = extraShipGraphicMax();
	for (int custom = 0; custom <= 1; ++custom)
		for (int graphic = 1; graphic <= max && count < capacity; ++graphic)
			if (extraShipGraphicIsCustom(graphic) == (custom != 0) &&
			    shipSkinOffered(seat, graphic) && shipSkinRestyles(seat, graphic, sheet, gr))
				order[count++] = graphic;
	return count;
}

bool shipSkinHasChoices(uint seat, const Sprite2_array *sheet, JE_word gr)
{
	int order[SHIP_SKIN_ORDER_MAX];
	return shipSkinFitsHull(gr) && shipSkinOrder(seat, sheet, gr, order, COUNTOF(order)) > 1;
}

void shipSkinCycle(uint seat, const Sprite2_array *sheet, JE_word gr, int dir)
{
	int order[SHIP_SKIN_ORDER_MAX];
	const int count = shipSkinOrder(seat, sheet, gr, order, COUNTOF(order));
	shipSkinSet(seat, (JE_byte)spritePickerCycle(order, count, shipSkinWorn(seat, sheet, gr), dir));
	network_player_look_publish();
}

const char *shipSkinName(int graphic, char *buf, size_t bufSize)
{
	if (graphic != SHIP_SKIN_NONE)
		return extraShipGraphicLabel(graphic, buf, bufSize);

	SDL_strlcpy(buf, "Default", bufSize);
	return buf;
}

JE_byte shipSkinApply(uint seat, Sprite2_array **sheet, JE_word *gr)
{
	const JE_byte skin = shipSkinWorn(seat, *sheet, *gr);
	if (skin != SHIP_SKIN_NONE)
		*gr = extraShipGraphicSprite(seat, skin, sheet);
	return skin;
}
