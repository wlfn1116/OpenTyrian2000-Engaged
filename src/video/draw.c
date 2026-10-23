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
#include "draw.h"

#include "config.h"
#include "opentyr.h"

#include "SDL.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

void JE_pix(SDL_Surface *surface, int x, int y, JE_byte c)
{
	/* Bad things happen if we don't clip. Negative coords write BEFORE the surface
	   buffer, corrupting whatever is allocated there (e.g. another surface's pixels). */
	if (x >= 0 && y >= 0 && x <  surface->pitch && y <  surface->h)
	{
		Uint8 *vga = surface->pixels;
		vga[y * surface->pitch + x] = c;
	}
}

void JE_pix3(SDL_Surface *surface, int x, int y, JE_byte c)
{
	/* Originally implemented as several direct accesses */
	JE_pix(surface, x, y, c);
	JE_pix(surface, x - 1, y, c);
	JE_pix(surface, x + 1, y, c);
	JE_pix(surface, x, y - 1, c);
	JE_pix(surface, x, y + 1, c);
}

void JE_rectangle(SDL_Surface *surface, int a, int b, int c, int d, int e) /* x1, y1, x2, y2, color */
{
	/* Both bounds: a negative coordinate passed the old upper-bound-only check and sent the
	 * memsets below a huge negative offset (a poisoned Destruct terrain column crashed here). */
	if (a >= 0 && b >= 0 && c >= 0 && d >= 0 &&
	    a < surface->pitch && b < surface->h &&
	    c < surface->pitch && d < surface->h)
	{
		Uint8 *vga = surface->pixels;
		int i;

		/* Top line */
		memset(&vga[b * surface->pitch + a], e, c - a + 1);

		/* Bottom line */
		memset(&vga[d * surface->pitch + a], e, c - a + 1);

		/* Left line */
		for (i = (b + 1) * surface->pitch + a; i < (d * surface->pitch + a); i += surface->pitch)
		{
			vga[i] = e;
		}

		/* Right line */
		for (i = (b + 1) * surface->pitch + c; i < (d * surface->pitch + c); i += surface->pitch)
		{
			vga[i] = e;
		}
	}
	else
	{
		printf("!!! WARNING: Rectangle clipped: %d %d %d %d %d\n", a, b, c, d, e);
	}
}

void fill_rectangle_xy(SDL_Surface *surface, int x, int y, int x2, int y2, Uint8 color)
{
	SDL_Rect rect = { x, y, x2 - x + 1, y2 - y + 1 };
	SDL_FillRect(surface, &rect, color);
}

void JE_barShade(SDL_Surface *surface, int a, int b, int c, int d) /* x1, y1, x2, y2 */
{
	if (a < surface->pitch && b < surface->h &&
	    c < surface->pitch && d < surface->h)
	{
		Uint8 *vga = surface->pixels;
		int i, j, width;

		width = c - a + 1;

		for (i = b * surface->pitch + a; i <= d * surface->pitch + a; i += surface->pitch)
		{
			for (j = 0; j < width; j++)
			{
				vga[i + j] = ((vga[i + j] & 0x0F) >> 1) | (vga[i + j] & 0xF0);
			}
		}
	}
	else
	{
		printf("!!! WARNING: Darker Rectangle clipped: %d %d %d %d\n", a,b,c,d);
	}
}

void JE_barBright(SDL_Surface *surface, int a, int b, int c, int d) /* x1, y1, x2, y2 */
{
	if (a < surface->pitch && b < surface->h &&
	    c < surface->pitch && d < surface->h)
	{
		Uint8 *vga = surface->pixels;
		int i, j, width;

		width = c-a+1;

		for (i = b * surface->pitch + a; i <= d * surface->pitch + a; i += surface->pitch)
		{
			for (j = 0; j < width; j++)
			{
				JE_byte al, ah;
				al = ah = vga[i + j];

				ah &= 0xF0;
				al = (al & 0x0F) + 2;

				if (al > 0x0F)
				{
					al = 0x0F;
				}

				vga[i + j] = al + ah;
			}
		}
	}
	else
	{
		printf("!!! WARNING: Brighter Rectangle clipped: %d %d %d %d\n", a,b,c,d);
	}
}

void draw_segmented_gauge(SDL_Surface *surface, int x, int y, Uint8 color, uint segment_width, uint segment_height, uint segment_value, uint value)
{
	assert(segment_width > 0 && segment_height > 0);

	const uint segments = value / segment_value,
	           partial_segment = value % segment_value;

	for (uint i = 0; i < segments; ++i)
	{
		fill_rectangle_wh(surface, x, y, segment_width, segment_height, color + 12);
		x += segment_width + 1;
	}
	if (partial_segment > 0)
		fill_rectangle_wh(surface, x, y, segment_width, segment_height, color + (12 * partial_segment / segment_value));
}

// Original shade sequence for the vertical 9x2 bands; downward bars mirror it.
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

// Draw a 9px gradient gauge at `scale`. Only its height is fractional; the base stays on an exact
// row. `topPad` extends the two-player gauge into the extra row its wipe already clears.
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
		// Horizontal gradient: nine 1px-wide, full-height stripes whose shade steps across the width.
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
