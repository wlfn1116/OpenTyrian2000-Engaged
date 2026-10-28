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
#ifndef SPRITE_PICKER_H
#define SPRITE_PICKER_H

#include "opentyr.h"

#include "sprite.h"

#include "SDL.h"

#include <stddef.h>

typedef enum
{
	SPRITE_PICKER_SHEET,
	SPRITE_PICKER_BLOCKS,
	SPRITE_PICKER_TABLE,
	SPRITE_PICKER_LIST,
} SpritePickerKind;

typedef struct
{
	const char *name;
	SpritePickerKind kind;
	const Sprite2_array *sheet;
	bool big;
	unsigned int table;
	int first, count, valueBase;
	int cellW, cellH;
	const int *values;
	void (*listDraw)(int value, int x, int y, int w, int h);
	const char *(*listName)(int value, char *buf, size_t n);
	const char *(*listTag)(int value, char *buf, size_t n);
} SpritePickerPage;

typedef struct
{
	const char *title;
	const SpritePickerPage *pages;
	int pageCount;
	void (*idle)(void);
	void (*tick)(void);
} SpritePickerSpec;

bool spritePickerRun(const SpritePickerSpec *spec, int *value);
void spritePickerInkBox(Sprite2_array sheet, unsigned int index, bool big, int *x0, int *y0,
                        int *x1, int *y1);
void spritePickerRelease(void);
void spritePickerDrawFit(SDL_Surface *dst, unsigned int table, unsigned int index, int x, int y,
                         int w, int h, int maxScale);
void spritePickerDrawHull(const Sprite2_array *sheet, JE_word gr, int x, int y, int w, int h);
void spritePickerDrawGraphic(uint seat, int graphic, int x, int y, int w, int h);
SpritePickerPage spritePickerHullPage(const char *name, const int *values, int count,
                                      void (*draw)(int value, int x, int y, int w, int h),
                                      const char *(*label)(int value, char *buf, size_t n));
int spritePickerIndexOf(const int *values, int count, int value);
int spritePickerCycle(const int *values, int count, int current, int dir);
bool spritePickerBlocksSelfTest(const SpritePickerPage *page, char *detail, size_t detailSize);
bool spritePickerTagSelfTest(char *detail, size_t detailSize);
bool spritePickerTagFits(const char *tag);

#endif // SPRITE_PICKER_H
