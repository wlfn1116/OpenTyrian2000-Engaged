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
#include "nortvars.h"

#include "config.h"
#include "file.h"
#include "joystick.h"
#include "keyboard.h"
#include "opentyr.h"
#include "vga256d.h"
#include "video.h"

#include <assert.h>
#include <ctype.h>
#include <math.h>

JE_boolean inputDetected;

JE_boolean JE_anyButton(void)
{
	poll_joysticks();
	service_SDL_events(true);
	return newkey || mousedown || joydown;
}

// Classic vertical shade progression for the 9x2 bands, indexed by band z (0 = bottom):
// 0,0,0,1,1,2,2,3,3,... i.e. the darkest shade for the first three bands then +1 every two.
// Matches the original zWait counter exactly; the Down direction mirrors it within the bar.
static int dbar_voffset(int z)
{
	return z < 1 ? 0 : (z - 1) / 2;
}

#define GAUGE_FLASH_WHITE 5

// The bar's leading row, when its top edge falls between rows: the top shade blended toward the
// bank floor by how much of that row the edge covers, so the edge reads as moving at sub-pixel
// resolution. Supersampling already puts the edge on a sub-row; this softens whatever is left.
static void dbar_edge_row(SDL_Surface *surface, int x0, int x1, int top, float cover, int shade)
{
	if (cover <= 0.04f || top < 1)
		return;

	const int dark = shade & ~0x0F;
	int edgeCol = dark + (int)(cover * (shade - dark) + 0.5f);
	if (edgeCol > shade)
		edgeCol = shade;
	fill_rectangle_xy(surface, x0, top - 1, x1, top - 1, (Uint8)edgeCol);
}

// Draw a 9px gauge with an up, down, left, or right brightness gradient.
// num is the unit count and may be fractional between ticks; scale renders the whole bar at that
// supersample factor, so its top edge lands on a sub-pixel row rather than a whole one. Only the
// height carries the fraction -- the base is the exact scaled row -- so a still bar never jitters.
// topPad grows the bar by that many rows at the top without touching the bottom row or the
// 2px band pitch; the two-player strip uses 1 so its four gauges reach the full height the
// wipe already clears (the one-player bars fill their slot exactly and pass 0).
void JE_dBar3_scaled(SDL_Surface *surface, JE_integer x, JE_integer y, float num, JE_integer col, JE_integer dir, JE_integer flash, JE_integer topPad, int scale)
{
	col += 2;

	if (num < 0.0f)
		return;

	const int x0 = x * scale, x1 = (x + 9) * scale - 1;
	const int bot = (y + 1) * scale - 1;
	const float topf = (float)((y - topPad) * scale) - (2.0f * num + 1.0f) * (float)scale;
	int top = (int)ceilf(topf);
	const float cover = (float)top - topf;  // share of the row above that the edge reaches into
	if (top < 0)
		top = 0;

	if (flash >= GAUGE_FLASH_WHITE)
	{
		fill_rectangle_xy(surface, x0, top, x1, bot, 15);
		dbar_edge_row(surface, x0, x1, top, cover, 15);
		return;
	}

	const int bright = (flash > 0) ? flash * 3 : 0;
	const int bankTop = (col & 0xF0) | 0x0F;

	if (dir == GAUGE_GRAD_LEFT || dir == GAUGE_GRAD_RIGHT)
	{
		// Horizontal gradient: nine 1px-wide, full-height stripes whose shade steps across
		// the width. Same vertical extent as the stacked bands (bottom row y, top row y-2*num-1).
		// Lifted +2 shades so the horizontal ramp reads a touch brighter (still in-family; the
		// vertical bar's upper bands reach higher still).
		for (int j = 0; j <= 8; j++)
		{
			const int off = (dir == GAUGE_GRAD_RIGHT) ? j : (8 - j);
			int shade = col + 2 + off;
			if (bright)
			{
				shade += bright;
				if (shade > bankTop)
					shade = bankTop;
			}
			const int sx0 = (x + j) * scale, sx1 = (x + j + 1) * scale - 1;
			fill_rectangle_xy(surface, sx0, top, sx1, bot, (Uint8)shade);
			dbar_edge_row(surface, sx0, sx1, top, cover, shade);
		}
		return;
	}

	// Vertical gradient, bottom-up in 2px bands: Up = classic; Down = the same shading mirrored
	// top-to-bottom. The topmost band is clipped to the bar's (possibly fractional) top, and past
	// the last whole unit the shade holds -- which is what carries topPad's extra rows.
	const int numi = (int)num;
	int shadeTop = col;
	for (int z = 0; ; z++)
	{
		const int bandBot = (y - 2 * z + 1) * scale - 1;
		int bandTop = (y - 2 * z - 1) * scale;
		const bool last = bandTop <= top;
		if (last)
			bandTop = top;
		if (bandBot < bandTop)
			break;

		const int zs = (z > numi) ? numi : z;
		const int off = (dir == GAUGE_GRAD_DOWN) ? dbar_voffset(numi - zs) : dbar_voffset(zs);
		int shade = col + off;
		if (bright)
		{
			shade += bright;
			if (shade > bankTop)
				shade = bankTop;
		}
		fill_rectangle_xy(surface, x0, bandTop, x1, bandBot, (Uint8)shade); /* <MXD> SEGa000 */
		shadeTop = shade;
		if (last)
			break;
	}
	dbar_edge_row(surface, x0, x1, top, cover, shadeTop);
}

void JE_dBar3(SDL_Surface *surface, JE_integer x,  JE_integer y,  JE_integer num,  JE_integer col,  JE_integer dir,  JE_integer flash,  JE_integer topPad)
{
	JE_dBar3_scaled(surface, x, y, (float)num, col, dir, flash, topPad, 1);
}

void JE_barDrawShadow(SDL_Surface *surface, JE_word x, JE_word y, JE_word res, JE_word col, JE_word amt, JE_word xsize, JE_word ysize)
{
	xsize--;
	ysize--;

	for (int z = 1; z <= amt / res; z++)
	{
		JE_barShade(surface, x+2, y+2, x+xsize+2, y+ysize+2);
		fill_rectangle_xy(surface, x, y, x+xsize, y+ysize, col+12);
		fill_rectangle_xy(surface, x, y, x+xsize, y, col+13);
		JE_pix(surface, x, y, col+15);
		fill_rectangle_xy(surface, x, y+ysize, x+xsize, y+ysize, col+11);
		x += xsize + 2;
	}

	amt %= res;
	if (amt > 0)
	{
		JE_barShade(surface, x+2, y+2, x+xsize+2, y+ysize+2);
		fill_rectangle_xy(surface, x,y, x+xsize, y+ysize, col+(12 / res * amt));
	}
}

// Recolor one 1-based slider slot without redrawing its shadow.
void JE_barDrawMark(SDL_Surface *surface, JE_word x, JE_word y, JE_word col, JE_word mark, JE_word xsize, JE_word ysize)
{
	if (mark == 0)
		return;

	xsize--;
	ysize--;

	x += (mark - 1) * (xsize + 2);  // JE_barDrawShadow advances x by xsize+2 per bar

	fill_rectangle_xy(surface, x, y, x+xsize, y+ysize, col+12);
	fill_rectangle_xy(surface, x, y, x+xsize, y, col+13);
	JE_pix(surface, x, y, col+15);
	fill_rectangle_xy(surface, x, y+ysize, x+xsize, y+ysize, col+11);
}

void JE_wipeKey(void)
{
	// /!\ Doesn't seems to affect anything.
}
