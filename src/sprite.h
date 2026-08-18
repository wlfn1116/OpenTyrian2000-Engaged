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
#ifndef SPRITE_H
#define SPRITE_H

#include "opentyr.h"

#include "SDL.h"

#include <assert.h>
#include <stdio.h>

#define FONT_SHAPES       0
#define SMALL_FONT_SHAPES 1
#define TINY_FONT         2
#define PLANET_SHAPES     3
#define FACE_SHAPES       4
#define OPTION_SHAPES     5 /*Also contains help shapes*/
#define WEAPON_SHAPES     6
#define EXTRA_SHAPES      7 /*Used for Ending pics*/

#define SPRITE_TABLES_MAX        8
#define SPRITES_PER_TABLE_MAX  152

typedef struct
{
	Uint16 width, height;
	Uint16 size;
	Uint8 *data;
}
Sprite;

typedef struct
{
	unsigned int count;
	Sprite sprite[SPRITES_PER_TABLE_MAX];
}
Sprite_array;

extern Sprite_array sprite_table[SPRITE_TABLES_MAX];  // fka shapearray, shapex, shapey, shapesize, shapexist, maxshape

static inline Sprite *sprite(unsigned int table, unsigned int index)
{
	assert(table < COUNTOF(sprite_table));
	assert(index < COUNTOF(sprite_table->sprite));
	return &sprite_table[table].sprite[index];
}

static inline bool sprite_exists(unsigned int table, unsigned int index)
{
	return (sprite(table, index)->data != NULL);
}
static inline Uint16 get_sprite_width(unsigned int table, unsigned int index)
{
	return (sprite_exists(table, index) ? sprite(table, index)->width : 0);
}
static inline Uint16 get_sprite_height(unsigned int table, unsigned int index)
{
	return (sprite_exists(table, index) ? sprite(table, index)->height : 0);
}

void load_sprites_file(unsigned int table, const char *filename);
void load_sprites(unsigned int table, FILE *f);
void free_sprites(unsigned int table);

// Palette bank in which most of the sprite is drawn.
Uint8 sprite_dominant_bank(unsigned int table, unsigned int index);

void blit_sprite(SDL_Surface *, int x, int y, unsigned int table, unsigned int index); // JE_newDrawCShapeNum
void blit_sprite_blend(SDL_Surface *, int x, int y, unsigned int table, unsigned int index); // JE_newDrawCShapeTrick
void blit_sprite_hv_unsafe(SDL_Surface *, int x, int y, unsigned int table, unsigned int index, Uint8 hue, Sint8 value); // JE_newDrawCShapeBright
void blit_sprite_hv(SDL_Surface *, int x, int y, unsigned int table, unsigned int index, Uint8 hue, Sint8 value); // JE_newDrawCShapeAdjust
void blit_sprite_hv_blend(SDL_Surface *, int x, int y, unsigned int table, unsigned int index, Uint8 hue, Sint8 value); // JE_newDrawCShapeModify
void blit_sprite_dark(SDL_Surface *, int x, int y, unsigned int table, unsigned int index, bool black); // JE_newDrawCShapeDarken, JE_newDrawCShapeShadow

typedef struct
{
	size_t size;
	Uint8 *data;
}
Sprite2_array;

// Shop icons and arrows sprite sheet.
extern Sprite2_array shopSpriteSheet;  // fka shapes6

// Explosions sprite sheet.
extern Sprite2_array explosionSpriteSheet;  // fka shapes6

// Enemy sprite sheet banks.
extern Sprite2_array enemySpriteSheets[4];  // fka eShapes1, eShapes2, eShapes3, eShapes4
extern Uint8 enemySpriteSheetIds[4];  // fka enemyShapeTables

// Destruct sprite sheet.
extern Sprite2_array destructSpriteSheet;  // fka shapes6

// Static sprite sheets.  Player shots, player ships, power-ups, coins, etc.
extern Sprite2_array spriteSheet8;  // fka shapesC1
extern Sprite2_array spriteSheet9;  // fka shapes9
extern Sprite2_array spriteSheet10;  // fka eShapes6
extern Sprite2_array spriteSheet11;  // fka eShapes5
extern Sprite2_array spriteSheet12;  // fka shapesW2
extern Sprite2_array spriteSheetT2000; // fka shapesT2k

void JE_loadCompShapes(Sprite2_array *, char s);
void JE_loadCompShapesB(Sprite2_array *, FILE *f);
void free_sprite2s(Sprite2_array *);

Uint8 sprite2_dominant_bank(Sprite2_array, unsigned int index); // palette bank (0..15) the sprite is mostly drawn in

// Extent of the sprite's painted pixels within its 12px cell, for centring it on something else.
bool sprite2_ink_bounds(Sprite2_array, unsigned int index, int *x0, int *y0, int *x1, int *y1);

void blit_sprite2(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index);
void blit_sprite2_clip(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index);
bool sprite2_has_pixel_in_window(int x, int y, Sprite2_array, unsigned int index, int wx0, int wx1, int wy0, int wy1);
bool sprite2_is_blank(Sprite2_array, unsigned int index);  // frame draws nothing (map-drawn / invisible pieces)
// Middle of the frame's opaque pixels, relative to where it is blitted; (0, 0) if it draws nothing.
void sprite2_center_offset(Sprite2_array, unsigned int index, int *out_dx, int *out_dy);
void blit_sprite2_blend(SDL_Surface *,  int x, int y, Sprite2_array, unsigned int index);
void blit_sprite2_blend_clip(SDL_Surface *,  int x, int y, Sprite2_array, unsigned int index);
void blit_sprite2_darken(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index);
void blit_sprite2_darken_clip(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index);
// Silhouette in one flat palette entry, for outline passes. blit_sprite2_filter can't do this:
// it keeps the sprite's own shade, so a rim drawn with it comes out shaded.
void blit_sprite2_solid(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 color);
void blit_sprite2_solid_clip(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 color);
void blit_sprite2_filter(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 filter);
void blit_sprite2_filter_clip(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 filter);
// Recolour, then lift the shade, for a sprite whose dark end would vanish in the destination bank
// (endless elite bullets). `filter` packs the bank and the lift the way blit_sprite2_blend_filter
// does; the plain filter blit cannot take one, since it ORs its argument over the sprite's shade.
void blit_sprite2_filter_bright(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 filter);
void blit_sprite2_filter_bright_clip(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 filter);
// Recolor and blend in one pass without reading tinted output back. `filter` packs the destination
// bank in its high nibble and a post-blend shade lift in its low nibble.
void blit_sprite2_blend_filter(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 filter);
void blit_sprite2_blend_filter_clip(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 filter);
// A sub-row window of the sprite only, brightened `bright` steps toward its own palette bank top,
// drawn at `scale` (x,y stay 1x). The window is in sub-rows so its edge can sit between two sprite
// rows. Records nothing -- HUD overlays only, carried by the residual (see sprite.c).
void blit_sprite2_rows_bright_scaled(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, int sub_first, int sub_last, int bright, int scale);

// Supersampled (render-list replay) variants: x,y are HI-buffer coordinates; every
// source pixel is drawn as a scale x scale block, fully clipped on all edges. These
// never record into the render list. The op selects the pixel math, matching the
// corresponding 1x blitter exactly.
typedef enum
{
	BLIT2_COPY = 0,   // blit_sprite2
	BLIT2_BLEND,      // blit_sprite2_blend
	BLIT2_DARKEN,     // blit_sprite2_darken
	BLIT2_FILTER,     // blit_sprite2_filter (uses the filter arg)
	BLIT2_SOLID,      // blit_sprite2_solid (the filter arg is the flat colour)
	BLIT2_BLEND_FILTER,  // blit_sprite2_blend_filter (filter arg = bank | shade lift)
	BLIT2_FILTER_BRIGHT, // blit_sprite2_filter_bright (filter arg = bank | shade lift)
} Blit2Op;

typedef enum
{
	BLITT_COPY = 0,   // blit_sprite
	BLITT_BLEND,      // blit_sprite_blend
	BLITT_HV_UNSAFE,  // blit_sprite_hv_unsafe
	BLITT_HV,         // blit_sprite_hv
	BLITT_HV_BLEND,   // blit_sprite_hv_blend
	BLITT_DARK,       // blit_sprite_dark
} BlitTableOp;

void blit_sprite2_scaled(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, int scale, Blit2Op op, Uint8 filter);
void blit_sprite_table_scaled(SDL_Surface *, int x, int y, unsigned int table, unsigned int index, int scale, BlitTableOp op, Uint8 hue, Sint8 value, bool black);

void blit_sprite2x2(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index);
void blit_sprite2x2_clip(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index);
void blit_sprite2x2_blend(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index);
void blit_sprite2x2_darken(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index);
void blit_sprite2x2_filter(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 filter);
void blit_sprite2x2_filter_clip(SDL_Surface *, int x, int y, Sprite2_array, unsigned int index, Uint8 filter);

void JE_loadMainShapeTables(const char *shpfile);
void free_main_shape_tables(void);

#endif // SPRITE_H
