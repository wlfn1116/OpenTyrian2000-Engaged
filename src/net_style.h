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
#ifndef NET_STYLE_H
#define NET_STYLE_H

#include "opentyr.h"

#include "SDL.h"

#include <stdbool.h>

/* Cosmetic styles for distinguishing online players. See "Online ship styles" in doc/notes.md. */

// Dye 0 keeps the original colors; 1..NET_SHIP_COLORS map to palette bank value - 1.
#define NET_SHIP_COLORS     16
#define NET_SHIP_COLOR_NONE  0

// Partner opacity in percent. Keep the minimum visible because the ship still has collision.
#define NET_OPACITY_STEP  10
#define NET_OPACITY_MIN   20
#define NET_OPACITY_FULL 100

// When to show shield and armor bars on the other ship.
enum
{
	NET_HP_BARS_OFF = 0,
	NET_HP_BARS_ON_HIT,
	NET_HP_BARS_ALWAYS,
	NET_HP_BARS_COUNT,
};

/* A seat's local view of the other ship. Only netStyleLocalView affects rendering; both seats are
 * retained for online resumes. */
typedef struct
{
	Uint8 opacity;      // percent, NET_OPACITY_MIN..NET_OPACITY_FULL
	bool  shipOpacity;  // false fades only what the ship fires
	Uint8 hpBars;       // one of NET_HP_BARS_*
}
NetShipView;

NetShipView netStyleView(uint seat);
void netStyleSetView(uint seat, NetShipView view);
NetShipView netStyleLocalView(void);
void netStyleSetLocalView(NetShipView view);

// A sprite bank and opacity in sixteenths. A negative bank keeps the source bank.
#define NET_STYLE_SOLID 16

typedef struct
{
	Sint8 bank;
	Uint8 opacity;
}
NetShipStyle;

static inline bool netStyleIsPlain(NetShipStyle style)
{
	return style.bank < 0 && style.opacity >= NET_STYLE_SOLID;
}

void netStyleSessionReset(void);
void netStyleSetSeatColor(uint seat, int color);
int  netStyleSeatColor(uint seat);
int  netStylePeerColor(void);

// True for kill-fire palette banks reserved by active Endless effects.
bool netStyleColorReserved(int color);

// Style for a player's body or its shots. Both are plain outside online play.
NetShipStyle netStyleForSeat(uint seat);
NetShipStyle netStyleForShot(uint seat);

// Player seat flown by this machine.
uint netStyleLocalSeat(void);

// Override styles drawn by the weapon simulator until cleared.
void netStylePreviewSet(int color, int opacity);
void netStylePreviewClear(void);

#endif /* NET_STYLE_H */
