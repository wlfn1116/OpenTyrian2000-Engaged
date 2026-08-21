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
#include "net_style.h"

#include "config.h"
#include "endless.h"
#include "network.h"
#include "player.h"

// One dye per player seat.
static int shipColor[2] = { NET_SHIP_COLOR_NONE, NET_SHIP_COLOR_NONE };

// Each seat owns the view chosen on that player's machine.
#define NET_SHIP_VIEW_DEFAULT { .opacity = NET_OPACITY_FULL, .shipOpacity = true, .hpBars = NET_HP_BARS_OFF }

static NetShipView shipView[2] = { NET_SHIP_VIEW_DEFAULT, NET_SHIP_VIEW_DEFAULT };

static const NetShipView defaultView = NET_SHIP_VIEW_DEFAULT;

static NetShipView clampView(NetShipView view)
{
	int opacity = view.opacity - view.opacity % NET_OPACITY_STEP;
	if (opacity < NET_OPACITY_MIN)
		opacity = NET_OPACITY_MIN;
	else if (opacity > NET_OPACITY_FULL)
		opacity = NET_OPACITY_FULL;
	view.opacity = (Uint8)opacity;

	if (view.hpBars >= NET_HP_BARS_COUNT)
		view.hpBars = NET_HP_BARS_OFF;

	return view;
}

NetShipView netStyleView(uint seat)
{
	return (seat < COUNTOF(shipView)) ? shipView[seat] : defaultView;
}

void netStyleSetView(uint seat, NetShipView view)
{
	if (seat < COUNTOF(shipView))
		shipView[seat] = clampView(view);
}

NetShipView netStyleLocalView(void)
{
	return netStyleView(netStyleLocalSeat());
}

void netStyleSetLocalView(NetShipView view)
{
	netStyleSetView(netStyleLocalSeat(), view);
}

static bool previewActive = false;
static int  previewColor = NET_SHIP_COLOR_NONE;
static int  previewOpacity = NET_OPACITY_FULL;

static const NetShipStyle plainStyle = { .bank = -1, .opacity = NET_STYLE_SOLID };

static int clampColor(int color)
{
	return (color >= NET_SHIP_COLOR_NONE && color <= NET_SHIP_COLORS) ? color : NET_SHIP_COLOR_NONE;
}

void netStyleSessionReset(void)
{
	for (unsigned int i = 0; i < COUNTOF(shipColor); ++i)
	{
		shipColor[i] = NET_SHIP_COLOR_NONE;
		shipView[i] = defaultView;
	}
}

void netStyleSetSeatColor(uint seat, int color)
{
	if (seat < COUNTOF(shipColor))
		shipColor[seat] = clampColor(color);
}

int netStyleSeatColor(uint seat)
{
	return (seat < COUNTOF(shipColor)) ? clampColor(shipColor[seat]) : NET_SHIP_COLOR_NONE;
}

int netStylePeerColor(void)
{
	return netStyleSeatColor(1u - netStyleLocalSeat());
}

bool netStyleColorReserved(int color)
{
	if (color == NET_SHIP_COLOR_NONE || !endlessFxActive())
		return false;

	// The three drive banks and their evil variant.
	static const int driveBank[] = {
		ENDLESS_TURBODRIVE_SHIP_FILTER >> 4,
		ENDLESS_OVERDRIVE_SHIP_FILTER >> 4,
		ENDLESS_OVERBLAST_SHIP_FILTER >> 4,
		ENDLESS_EVIL_SHIP_FILTER >> 4,
	};
	for (unsigned int i = 0; i < COUNTOF(driveBank); ++i)
		if (color - 1 == driveBank[i])
			return true;

	return false;
}

void netStylePreviewSet(int color, int opacity)
{
	previewColor = clampColor(color);
	previewOpacity = opacity;
	previewActive = true;
}

void netStylePreviewClear(void)
{
	previewActive = false;
}

// Linked Arcade still assigns one seat to each machine through thisPlayerNum.
uint netStyleLocalSeat(void)
{
	return (thisPlayerNum >= 1 && thisPlayerNum <= 2) ? thisPlayerNum - 1u : 0u;
}

// Preview state overrides the session. Only body styles carry dyes.
static NetShipStyle seatStyle(uint seat, bool body)
{
	int color = NET_SHIP_COLOR_NONE, opacity = NET_OPACITY_FULL;
	// Only the local seat's view affects rendering.
	const NetShipView local = netStyleLocalView();

	if (previewActive)
	{
		color = previewColor;
		opacity = previewOpacity;
	}
	else if (isNetworkGame && twoPlayerMode && seat < COUNTOF(player))
	{
		color = netStyleSeatColor(seat);
		if (seat != netStyleLocalSeat())
			opacity = local.opacity;
	}
	else
	{
		return plainStyle;
	}

	NetShipStyle style = plainStyle;

	if (body && color != NET_SHIP_COLOR_NONE && !netStyleColorReserved(color))
		style.bank = (Sint8)(color - 1);

	if (opacity < NET_OPACITY_FULL && (!body || local.shipOpacity))
	{
		const int pct = (opacity < NET_OPACITY_MIN) ? NET_OPACITY_MIN : opacity;
		style.opacity = (Uint8)((pct * NET_STYLE_SOLID + 50) / NET_OPACITY_FULL);
	}

	return style;
}

NetShipStyle netStyleForSeat(uint seat)
{
	return seatStyle(seat, true);
}

NetShipStyle netStyleForShot(uint seat)
{
	return seatStyle(seat, false);
}
