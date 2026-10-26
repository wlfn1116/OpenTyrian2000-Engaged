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
#ifndef DRAW_H
#define DRAW_H

#include "opentyr.h"

#include "SDL.h"

void JE_pix(SDL_Surface *surface, int x, int y, JE_byte c);
void JE_pix3(SDL_Surface *surface, int x, int y, JE_byte c);
void JE_rectangle(SDL_Surface *surface, int a, int b, int c, int d, int e);

void fill_rectangle_xy(SDL_Surface *, int x, int y, int x2, int y2, Uint8 color);
void draw_corner_clamps(SDL_Surface *, int x0, int y0, int x1, int y1, int arm, Uint8 color);
void draw_rect_outline(SDL_Surface *, int x0, int y0, int x1, int y1, Uint8 color);

void JE_barShade(SDL_Surface *surface, int a, int b, int c, int d);
void JE_barBright(SDL_Surface *surface, int a, int b, int c, int d);

static inline void fill_rectangle_wh(SDL_Surface *surface, int x, int y, uint w, uint h, Uint8 color)
{
	SDL_Rect rect = { x, y, w, h };
	SDL_FillRect(surface, &rect, color);
}

void draw_segmented_gauge(SDL_Surface *surface, int x, int y, Uint8 color, uint segment_width, uint segment_height, uint segment_value, uint value);

void JE_dBar3(SDL_Surface *surface, JE_integer x,  JE_integer y,  JE_integer num,  JE_integer col,  JE_integer dir,  JE_integer flash,  JE_integer topPad);
void JE_dBar3_scaled(SDL_Surface *surface, JE_integer x, JE_integer y, float num, JE_integer col, JE_integer dir, JE_integer flash, JE_integer topPad, int scale);
void JE_barDrawShadow(SDL_Surface *surface, JE_word x, JE_word y, JE_word res, JE_word col, JE_word amt, JE_word xsize, JE_word ysize);
void JE_barDrawMark(SDL_Surface *surface, JE_word x, JE_word y, JE_word col, JE_word mark, JE_word xsize, JE_word ysize);

#endif /* DRAW_H */
