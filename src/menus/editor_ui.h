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
#ifndef EDITOR_UI_H
#define EDITOR_UI_H

#include "opentyr.h"

#include "SDL.h"

enum
{
	C_PANEL = 0xF1, C_WELL = 0xF0, C_DIV = 0xF6, C_GUIDE = 0xF8, C_HI = 0xFB, C_SEL = 0xF5,
};

enum { EDITOR_TICK_DELAY = 3 };

double editorTickMs(void);
void editorDrawHint(const char *text);
void editorDrawPanel(int x0, int y0, int x1, int y1);
void editorDrawWell(int x0, int y0, int x1, int y1, Uint8 fill, Uint8 rim, Uint8 clamp);

typedef void (*EditorStarCell)(SDL_Surface *dst, int scale, int x_offset, int x, float fy,
                               JE_byte c);
void editorDrawStars(SDL_Surface *dst, int scale, int x_offset, Uint32 clock_ms, int x0, int y0,
                     int x1, int y1, unsigned int count, EditorStarCell cell);

typedef struct
{
	int x0, y0, x1, y1;
	Uint32 start_ms;
}
EditorFlash;
void editorFlashStart(EditorFlash *flash, int x0, int y0, int x1, int y1);
void editorDrawFlash(EditorFlash *flash, SDL_Surface *dst, int scale, int x_offset);

#endif // EDITOR_UI_H
