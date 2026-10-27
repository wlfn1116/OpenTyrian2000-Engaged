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
#include "sprite_viewer.h"

#include "debug_menu.h"
#include "draw.h"
#include "file.h"
#include "font.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "network.h"
#include "palette.h"
#include "sprite.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Draw a main-table sprite magnified to scale*scale blocks, writing only inside
 * the inclusive clip box (blit_sprite() can neither clip horizontally nor magnify). */
static void draw_sprite_obj_scaled_clip(SDL_Surface *s, int x, int y, const Sprite *sp, int scale,
                                        int cx0, int cy0, int cx1, int cy1)
{
	if (sp == NULL || sp->data == NULL || scale < 1)
		return;

	const Uint8 *data = sp->data;
	const Uint8 *const data_ul = data + sp->size;
	const int width = (int)sp->width;

	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 > s->w - 1) cx1 = s->w - 1;
	if (cy1 > s->h - 1) cy1 = s->h - 1;

	Uint8 *const pixels = (Uint8 *)s->pixels;
	int col = 0, row = 0;
	for (; data < data_ul; ++data)
	{
		switch (*data)
		{
		case 255:  // run of transparent pixels
			++data;
			col += *data;
			break;
		case 254:  // next row
			col = width;
			break;
		case 253:  // one transparent pixel
			++col;
			break;
		default:   // opaque pixel -> scale*scale block
		{
			const int bx = x + col * scale, by = y + row * scale;
			for (int dy = 0; dy < scale; ++dy)
			{
				const int py = by + dy;
				if (py < cy0 || py > cy1)
					continue;
				Uint8 *const prow = pixels + py * s->pitch;
				for (int dx = 0; dx < scale; ++dx)
				{
					const int px = bx + dx;
					if (px >= cx0 && px <= cx1)
						prow[px] = *data;
				}
			}
			++col;
			break;
		}
		}
		if (col >= width)
		{
			col = 0;
			++row;
		}
	}
}

/* Convenience wrapper for a sprite addressed by main-table index. */
static void draw_sprite_scaled_clip(SDL_Surface *s, int x, int y, unsigned int table,
                                    unsigned int index, int scale,
                                    int cx0, int cy0, int cx1, int cy1)
{
	if (table >= SPRITE_TABLES_MAX || !sprite_exists(table, index))
		return;
	draw_sprite_obj_scaled_clip(s, x, y, sprite(table, index), scale, cx0, cy0, cx1, cy1);
}

/* Load a Sprite_array .shp; free *out with free_local_sprite_array. */
static void free_local_sprite_array(Sprite_array *a)
{
	for (unsigned int i = 0; i < a->count && i < SPRITES_PER_TABLE_MAX; ++i)
	{
		free(a->sprite[i].data);
		a->sprite[i].data = NULL;
	}
	a->count = 0;
}
static bool load_sprite_array_file(const char *filename, Sprite_array *out)
{
	memset(out, 0, sizeof(*out));

	FILE *f = dir_fopen(data_dir(), filename, "rb");
	if (f == NULL)
		return false;

	Uint16 count = 0;
	if (fread(&count, sizeof(count), 1, f) != 1)
	{
		fclose(f);
		return false;
	}
	count = SDL_SwapLE16(count);
	if (count == 0 || count > SPRITES_PER_TABLE_MAX)  // not this format / empty
	{
		fclose(f);
		return false;
	}

	out->count = count;
	bool any = false;
	for (unsigned int i = 0; i < count; ++i)
	{
		Sprite *const sp = &out->sprite[i];
		Uint8 populated = 0;
		if (fread(&populated, 1, 1, f) != 1)
			break;
		if (!populated)
			continue;

		Uint16 whs[3];
		if (fread(whs, sizeof(whs[0]), 3, f) != 3)
			break;
		sp->width  = SDL_SwapLE16(whs[0]);
		sp->height = SDL_SwapLE16(whs[1]);
		sp->size   = SDL_SwapLE16(whs[2]);
		if (sp->size == 0 || sp->size > 64u * 1024u)  // sanity guard
			break;

		sp->data = malloc(sp->size);
		if (sp->data == NULL || fread(sp->data, 1, sp->size, f) != sp->size)
		{
			free(sp->data);
			sp->data = NULL;
			break;
		}
		any = true;
	}
	fclose(f);

	if (!any)
	{
		free_local_sprite_array(out);
		return false;
	}
	return true;
}

static const char *const spriteTableNames[SPRITE_TABLES_MAX] = {
	"Font", "Small Font", "Tiny Font", "Planets",
	"Faces", "Options / Help", "Weapons", "Extra / Endings"
};

/* Helpers for 1-based, 12-pixel compiled Sprite2 sheets. */
static int sprite2_height(const Sprite2_array *a, int index1)
{
	if (index1 < 1 || index1 > sprite2_count(a))
		return 0;
	const size_t off = SDL_SwapLE16(((const Uint16 *)a->data)[index1 - 1]);
	if (off >= a->size)
		return 0;
	const Uint8 *data = a->data + off;
	const Uint8 *const end = a->data + a->size;  // never read past the buffer
	int rows = 1;
	for (; data < end && *data != 0x0f; ++data)
	{
		const int count = (*data & 0xf0) >> 4;
		if (count == 0)
			++rows;
		else
			data += count;  // step past the opaque pixel bytes
	}
	return rows;
}
static void draw_sprite2_scaled_clip(SDL_Surface *s, int x, int y, const Sprite2_array *a,
                                     int index1, int scale, int cx0, int cy0, int cx1, int cy1)
{
	if (a->data == NULL || index1 < 1 || index1 > sprite2_count(a) || scale < 1)
		return;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 > s->w - 1) cx1 = s->w - 1;
	if (cy1 > s->h - 1) cy1 = s->h - 1;

	const size_t off = SDL_SwapLE16(((const Uint16 *)a->data)[index1 - 1]);
	if (off >= a->size)
		return;
	Uint8 *const pixels = (Uint8 *)s->pixels;
	const Uint8 *data = a->data + off;
	const Uint8 *const end = a->data + a->size;  // never read past the buffer
	int col = 0, row = 0;
	for (; data < end && *data != 0x0f; ++data)
	{
		col += *data & 0x0f;  // transparent skip
		int count = (*data & 0xf0) >> 4;
		if (count == 0)
		{
			++row;
			col = 0;
		}
		else
		{
			while (count-- && data + 1 < end)
			{
				++data;
				const int bx = x + col * scale, by = y + row * scale;
				for (int dy = 0; dy < scale; ++dy)
				{
					const int py = by + dy;
					if (py < cy0 || py > cy1) continue;
					Uint8 *const prow = pixels + py * s->pitch;
					for (int dx = 0; dx < scale; ++dx)
					{
						const int px = bx + dx;
						if (px >= cx0 && px <= cx1)
							prow[px] = *data;
					}
				}
				++col;
			}
		}
	}
}

/* Background tiles are raw 24x28 one-byte pixels. */
#define TILE_W 24
#define TILE_H 28
static void draw_tile_scaled_clip(SDL_Surface *s, int x, int y, const Uint8 *tile,
                                  int scale, int cx0, int cy0, int cx1, int cy1)
{
	if (tile == NULL || scale < 1)
		return;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 > s->w - 1) cx1 = s->w - 1;
	if (cy1 > s->h - 1) cy1 = s->h - 1;

	Uint8 *const pixels = (Uint8 *)s->pixels;
	for (int r = 0; r < TILE_H; ++r)
		for (int c = 0; c < TILE_W; ++c)
		{
			const Uint8 v = tile[r * TILE_W + c];  // tiles are opaque; draw every pixel
			const int bx = x + c * scale, by = y + r * scale;
			for (int dy = 0; dy < scale; ++dy)
			{
				const int py = by + dy;
				if (py < cy0 || py > cy1) continue;
				Uint8 *const prow = pixels + py * s->pitch;
				for (int dx = 0; dx < scale; ++dx)
				{
					const int px = bx + dx;
					if (px >= cx0 && px <= cx1)
						prow[px] = v;
				}
			}
		}
}

/* Common browser interface for shape tables, Sprite2 sheets, and tile banks. */
typedef enum { VS_MAIN, VS_SPRITE2, VS_TILE, VS_SPRITE_ARRAY } VSKind;
typedef struct
{
	const char *name;
	VSKind kind;
	int count;
	unsigned int table;          // VS_MAIN
	const Sprite2_array *sheet;  // VS_SPRITE2
	const Uint8 *tileBase;       // VS_TILE: pointer to first tile's pixels
	size_t tileStride;           // VS_TILE: bytes between successive tiles
	const Sprite_array *localArr;// VS_SPRITE_ARRAY: a freshly-loaded shape table
} VSource;

static int vsrc_add(VSource *list, int n, const char *name, VSKind kind, int count,
                    unsigned int table, const Sprite2_array *sheet,
                    const Uint8 *tileBase, size_t tileStride)
{
	list[n].name = name;
	list[n].kind = kind;
	list[n].count = count;
	list[n].table = table;
	list[n].sheet = sheet;
	list[n].tileBase = tileBase;
	list[n].tileStride = tileStride;
	list[n].localArr = NULL;
	return n + 1;
}
static bool vsrc_exists(const VSource *s, int i)
{
	if (i < 0 || i >= s->count)
		return false;
	if (s->kind == VS_MAIN)
		return sprite_exists(s->table, i);
	if (s->kind == VS_SPRITE_ARRAY)
		return i < (int)s->localArr->count && s->localArr->sprite[i].data != NULL;
	return true;  // sprite2 / tile banks have no gaps
}
static void vsrc_dims(const VSource *s, int i, int *w, int *h)
{
	switch (s->kind)
	{
	case VS_MAIN:    *w = get_sprite_width(s->table, i); *h = get_sprite_height(s->table, i); break;
	case VS_SPRITE2: *w = 12; *h = sprite2_height(s->sheet, i + 1); break;
	case VS_TILE:    *w = TILE_W; *h = TILE_H; break;
	case VS_SPRITE_ARRAY: *w = s->localArr->sprite[i].width; *h = s->localArr->sprite[i].height; break;
	}
}
static void vsrc_draw(SDL_Surface *surf, const VSource *s, int i, int x, int y, int scale,
                      int cx0, int cy0, int cx1, int cy1)
{
	switch (s->kind)
	{
	case VS_MAIN:    draw_sprite_scaled_clip(surf, x, y, s->table, i, scale, cx0, cy0, cx1, cy1); break;
	case VS_SPRITE2: draw_sprite2_scaled_clip(surf, x, y, s->sheet, i + 1, scale, cx0, cy0, cx1, cy1); break;
	case VS_TILE:    draw_tile_scaled_clip(surf, x, y, s->tileBase + (size_t)i * s->tileStride, scale, cx0, cy0, cx1, cy1); break;
	case VS_SPRITE_ARRAY: draw_sprite_obj_scaled_clip(surf, x, y, &s->localArr->sprite[i], scale, cx0, cy0, cx1, cy1); break;
	}
}
static int vsrc_first(const VSource *s)
{
	for (int i = 0; i < s->count; ++i)
		if (vsrc_exists(s, i))
			return i;
	return 0;
}
static int vsrc_last(const VSource *s)
{
	for (int i = s->count - 1; i >= 0; --i)
		if (vsrc_exists(s, i))
			return i;
	return 0;
}
static int vsrc_step(const VSource *s, int cur, int dir)
{
	for (int i = cur + dir; i >= 0 && i < s->count; i += dir)
		if (vsrc_exists(s, i))
			return i;
	return cur;  // already at the first/last existing entry
}

/* Load up to 600 nonblank 24x28 tiles; the caller frees *outBuf. */
static int load_tileset_file(char c, Uint8 **outBuf)
{
	*outBuf = NULL;

	char name[16];
	snprintf(name, sizeof(name), "shapes%c.dat", c);
	FILE *f = dir_fopen(data_dir(), name, "rb");
	if (f == NULL)
		return 0;

	Uint8 *buf = malloc((size_t)600 * TILE_W * TILE_H);
	int count = 0;
	if (buf != NULL)
	{
		for (int z = 0; z < 600; ++z)
		{
			Uint8 blank;
			if (fread(&blank, 1, 1, f) != 1)
				break;
			if (blank)
				continue;  // blank tile carries no pixel data
			if (fread(buf + (size_t)count * TILE_W * TILE_H, 1, TILE_W * TILE_H, f) != (size_t)(TILE_W * TILE_H))
				break;
			++count;
		}
	}
	fclose(f);

	if (count == 0)
	{
		free(buf);
		return 0;
	}
	*outBuf = buf;
	return count;
}

/* Load a compiled newsh<c>.shp directly for the sprite browser. */
static bool try_load_newsh(char c, Sprite2_array *out)
{
	out->data = NULL;
	out->size = 0;

	char fname[16];
	snprintf(fname, sizeof(fname), "newsh%c.shp", c);
	FILE *f = dir_fopen(data_dir(), fname, "rb");
	if (f == NULL)
		return false;

	out->size = ftell_eof(f);
	if (out->size > 2)
		JE_loadCompShapesB(out, f);  // mallocs out->data and reads the file
	fclose(f);

	if (out->data == NULL || sprite2_count(out) <= 0)
	{
		free_sprite2s(out);
		return false;
	}
	return true;
}

/* Debug sprite browser with palette switching and neighbouring frames. */
void JE_spriteViewer(int off_x, int off_y)
{
	wait_noinput_consume(false, false, true);  // don't let the click/key that opened us leak in

	/* Build the source list from whatever graphics are currently loaded. It only
	 * borrows source pointers, so reusable storage keeps this debug UI's stack small. */
	static VSource src[128];
	int nsrc = 0;
	for (unsigned int t = 0; t < SPRITE_TABLES_MAX; ++t)
		if (sprite_table[t].count > 0)
			nsrc = vsrc_add(src, nsrc, spriteTableNames[t], VS_MAIN, (int)sprite_table[t].count, t, NULL, NULL, 0);

	/* The always-in-memory compiled sheets that live inside tyrian.shp (not
	 * standalone files), so they can't be loaded by name like the newsh*.shp. */
	static const struct { const char *name; const Sprite2_array *a; } sheets[] = {
		{ "Player Shots",   &spriteSheet8 },
		{ "Player Ships",   &spriteSheet9 },
		{ "Power-ups",      &spriteSheet10 },
		{ "Coins / Cubes",  &spriteSheet11 },
		{ "Player Shots 2", &spriteSheet12 },
		{ "T2000 Ships",    &spriteSheetT2000 },
	};
	for (size_t i = 0; i < COUNTOF(sheets); ++i)
	{
		const int c = sprite2_count(sheets[i].a);
		if (c > 0)
			nsrc = vsrc_add(src, nsrc, sheets[i].name, VS_SPRITE2, c, 0, sheets[i].a, NULL, 0);
	}

	/* Load every compiled object sheet (newsh*.shp) from disk, iterating only
	 * the on-disk casing so a case-folding filesystem can't load one twice. */
	static const char newshChars[] = "0123456789abcdefghijklmnopqrstuvwxyz#$%'(@^~";
	Sprite2_array loadedSheets[64];
	char loadedNames[64][16];
	int nLoaded = 0;
	for (const char *pc = newshChars; *pc != '\0' && nLoaded < (int)COUNTOF(loadedSheets)
	                                 && nsrc < (int)COUNTOF(src); ++pc)
	{
		if (try_load_newsh(*pc, &loadedSheets[nLoaded]))
		{
			snprintf(loadedNames[nLoaded], sizeof(loadedNames[0]), "newsh %c", *pc);
			nsrc = vsrc_add(src, nsrc, loadedNames[nLoaded], VS_SPRITE2,
			                sprite2_count(&loadedSheets[nLoaded]), 0, &loadedSheets[nLoaded], NULL, 0);
			++nLoaded;
		}
	}

	/* Stand-alone shape tables outside tyrian.shp: the ending/credits sprites
	 * (estsc.shp) plus the unused-but-present estpa / user ship files. */
	static const struct { const char *name; const char *file; } arrFiles[] = {
		{ "Ending: estsc", "estsc.shp" },
		{ "Ending: estpa", "estpa.shp" },
		{ "User ship 1",   "user1.shp" },
		{ "User ship 2",   "user2.shp" },
	};
	Sprite_array localArrs[COUNTOF(arrFiles)];
	int nArr = 0;
	for (size_t i = 0; i < COUNTOF(arrFiles) && nsrc < (int)COUNTOF(src); ++i)
	{
		if (load_sprite_array_file(arrFiles[i].file, &localArrs[nArr]))
		{
			const int at = nsrc;
			nsrc = vsrc_add(src, nsrc, arrFiles[i].name, VS_SPRITE_ARRAY,
			                (int)localArrs[nArr].count, 0, NULL, NULL, 0);
			src[at].localArr = &localArrs[nArr];
			++nArr;
		}
	}

	/* Every level's tiles come from one of these five shapes<c>.dat files; load
	 * them all so the whole game's tilesets are browsable from anywhere. */
	static const char tileFileChars[] = { ')', 'w', 'x', 'y', 'z' };
	static const char *const tileSetNames[] = {
		"Tiles: Set )", "Tiles: Set W", "Tiles: Set X", "Tiles: Set Y", "Tiles: Set Z"
	};
	Uint8 *tileBufs[COUNTOF(tileFileChars)] = { NULL };
	for (size_t i = 0; i < COUNTOF(tileFileChars); ++i)
	{
		const int c = load_tileset_file(tileFileChars[i], &tileBufs[i]);
		if (c > 0)
			nsrc = vsrc_add(src, nsrc, tileSetNames[i], VS_TILE, c, 0, NULL,
			                tileBufs[i], (size_t)TILE_W * TILE_H);
	}

	if (nsrc == 0)  // nothing loaded at all
	{
		for (size_t i = 0; i < COUNTOF(tileBufs); ++i)
			free(tileBufs[i]);
		for (int i = 0; i < nLoaded; ++i)
			free_sprite2s(&loadedSheets[i]);
		for (int i = 0; i < nArr; ++i)
			free_local_sprite_array(&localArrs[i]);
		return;
	}

	const int px0 = DEBUG_MENU_X + off_x, py0 = DEBUG_MENU_Y + off_y;
	const int px1 = DEBUG_MENU_X + DEBUG_MENU_WIDTH - 1 + off_x, py1 = vga_height - 5 + off_y;
	const int title_h = 15;
	const int mid_x = (px0 + px1) / 2;

	enum { C_CHECK_A = 0xF2, C_CHECK_B = 0xF4, C_CELL_SEL = 0xFB };

	/* Remember the live palette so we can recolour for previews and restore it
	 * on the way out (palSel == -1 means "use the palette already on screen"). */
	Palette savedPal;
	memcpy(savedPal, colors, sizeof(savedPal));
	int palSel = -1;

	int s = 0;            // current source
	int idx = vsrc_first(&src[s]);

	bool done = false;
	while (!done)
	{
		debug_panel_frame(px0, py0, px1, py1, title_h, mid_x, "SPRITE  VIEWER");

		int sw = 0, sh = 0;
		vsrc_dims(&src[s], idx, &sw, &sh);

		/* info lines */
		const int line_h = 11;
		const int ct = py0 + title_h + 4;
		char buf[48];

		snprintf(buf, sizeof(buf), "Source:  %s", src[s].name);
		draw_font_hv_shadow(VGAScreen, px0 + 10, ct, buf, small_font, left_aligned, 15, 0, true, 1);
		snprintf(buf, sizeof(buf), "%d / %d", s + 1, nsrc);
		draw_font_hv_shadow(VGAScreen, px1 - 10, ct, buf, small_font, right_aligned, 15, 0, true, 1);

		snprintf(buf, sizeof(buf), "Sprite %d / %d    %dx%d", idx, src[s].count - 1, sw, sh);
		draw_font_hv_shadow(VGAScreen, px0 + 10, ct + line_h, buf, small_font, left_aligned, 15, 0, true, 1);
		if (palSel < 0)
			snprintf(buf, sizeof(buf), "Pal: native");
		else
			snprintf(buf, sizeof(buf), "Pal: %d / %d", palSel + 1, palette_count);
		draw_font_hv_shadow(VGAScreen, px1 - 10, ct + line_h, buf, small_font, right_aligned, 15, 0, true, 1);

		/* preview box */
		const int bx0 = px0 + 10, bx1 = px1 - 10;
		const int by0 = ct + 2 * line_h + 6, by1 = py1 - 50;

		/* inset frame */
		fill_rectangle_xy(VGAScreen, bx0, by0, bx1, by0, PANEL_EDGE_LO);
		fill_rectangle_xy(VGAScreen, bx0, by0, bx0, by1, PANEL_EDGE_LO);
		fill_rectangle_xy(VGAScreen, bx0, by1, bx1, by1, PANEL_EDGE_HI);
		fill_rectangle_xy(VGAScreen, bx1, by0, bx1, by1, PANEL_EDGE_HI);

		/* checkerboard so transparent / black pixels are visible */
		for (int yy = by0 + 1; yy < by1; yy += 8)
			for (int xx = bx0 + 1; xx < bx1; xx += 8)
			{
				const int c = (((xx - bx0) / 8 + (yy - by0) / 8) & 1) ? C_CHECK_A : C_CHECK_B;
				fill_rectangle_xy(VGAScreen, xx, yy, MIN(xx + 7, bx1 - 1), MIN(yy + 7, by1 - 1), c);
			}

		/* magnify to fill the box (integer scale, capped) */
		int scale = 1;
		if (sw > 0 && sh > 0)
		{
			const int boxw = bx1 - bx0 - 6, boxh = by1 - by0 - 6;
			while (scale < 8 && sw * (scale + 1) <= boxw && sh * (scale + 1) <= boxh)
				++scale;
			const int dw = sw * scale, dh = sh * scale;
			const int dx = (bx0 + bx1) / 2 - dw / 2, dy = (by0 + by1) / 2 - dh / 2;
			vsrc_draw(VGAScreen, &src[s], idx, dx, dy, scale, bx0 + 1, by0 + 1, bx1 - 1, by1 - 1);
		}

		/* zoom indicator */
		snprintf(buf, sizeof(buf), "x%d", scale);
		draw_font_hv_shadow(VGAScreen, bx1 - 3, by0 + 2, buf, small_font, right_aligned, 15, -2, true, 1);

		/* filmstrip of neighbouring sprites */
		const int strip_y = by1 + 4;
		const int strip_bot = py1 - 24;
		const int cell_h = strip_bot - strip_y;
		const int cells = 7;
		const int cell_w = (bx1 - bx0) / cells;
		for (int k = 0; k < cells; ++k)
		{
			const int si = idx - cells / 2 + k;
			const int cxL = bx0 + k * cell_w;
			const bool cur = (k == cells / 2);

			fill_rectangle_xy(VGAScreen, cxL, strip_y, cxL + cell_w - 2, strip_y + cell_h,
			                  cur ? C_CHECK_A : PANEL_BG);
			if (cur)
			{
				fill_rectangle_xy(VGAScreen, cxL, strip_y, cxL + cell_w - 2, strip_y, C_CELL_SEL);
				fill_rectangle_xy(VGAScreen, cxL, strip_y + cell_h, cxL + cell_w - 2, strip_y + cell_h, C_CELL_SEL);
				fill_rectangle_xy(VGAScreen, cxL, strip_y, cxL, strip_y + cell_h, C_CELL_SEL);
				fill_rectangle_xy(VGAScreen, cxL + cell_w - 2, strip_y, cxL + cell_w - 2, strip_y + cell_h, C_CELL_SEL);
			}

			if (vsrc_exists(&src[s], si))
			{
				int tw = 0, th = 0;
				vsrc_dims(&src[s], si, &tw, &th);
				const int tx = cxL + (cell_w - 2) / 2 - tw / 2;
				const int ty = strip_y + cell_h / 2 - th / 2;
				vsrc_draw(VGAScreen, &src[s], si, tx, ty, 1,
				          cxL + 1, strip_y + 1, cxL + cell_w - 3, strip_y + cell_h - 1);
			}
		}

		/* two-line footer, kept inside the panel */
		draw_font_hv(VGAScreen, mid_x, py1 - 20,
		             "L/R sprite    U/D source    [ ] palette",
		             small_font, centered, 15, -3);
		draw_font_hv(VGAScreen, mid_x, py1 - 11,
		             "Home/End   PgUp/PgDn   Esc back",
		             small_font, centered, 15, -3);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		NETWORK_KEEP_ALIVE();  // nothing else pumps packets while this panel is up

		/* wheel walks sprites; left-click = next, right-click = exit */
		if (mouse_scroll != 0)
		{
			idx = vsrc_step(&src[s], idx, mouse_scroll > 0 ? -1 : 1);
			mouse_scroll = 0;
		}
		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
				done = true;
			else
				idx = vsrc_step(&src[s], idx, 1);
			newmouse = false;
		}

		if (newkey)
		{
			int newPalSel = palSel;
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_LEFT:
				idx = vsrc_step(&src[s], idx, -1);
				break;
			case SDL_SCANCODE_RIGHT:
				idx = vsrc_step(&src[s], idx, 1);
				break;
			case SDL_SCANCODE_PAGEUP:
				for (int n = 0; n < 10; ++n) idx = vsrc_step(&src[s], idx, -1);
				break;
			case SDL_SCANCODE_PAGEDOWN:
				for (int n = 0; n < 10; ++n) idx = vsrc_step(&src[s], idx, 1);
				break;
			case SDL_SCANCODE_HOME:
				idx = vsrc_first(&src[s]);
				break;
			case SDL_SCANCODE_END:
				idx = vsrc_last(&src[s]);
				break;
			case SDL_SCANCODE_UP:
				s = (s == 0) ? nsrc - 1 : s - 1;
				idx = vsrc_first(&src[s]);
				break;
			case SDL_SCANCODE_DOWN:
				s = (s + 1) % nsrc;
				idx = vsrc_first(&src[s]);
				break;
			case SDL_SCANCODE_LEFTBRACKET:
				newPalSel = (palSel <= -1) ? palette_count - 1 : palSel - 1;
				break;
			case SDL_SCANCODE_RIGHTBRACKET:
				newPalSel = (palSel >= palette_count - 1) ? -1 : palSel + 1;
				break;
			case SDL_SCANCODE_ESCAPE:
				done = true;
				break;
			default:
				break;
			}
			if (newPalSel != palSel)
			{
				palSel = newPalSel;
				set_palette(palSel < 0 ? savedPal : palettes[palSel], 0, 255);
			}
			newkey = false;
		}
	}

	/* restore whatever palette was on screen when we opened */
	set_palette(savedPal, 0, 255);

	for (size_t i = 0; i < COUNTOF(tileBufs); ++i)
		free(tileBufs[i]);
	for (int i = 0; i < nLoaded; ++i)
		free_sprite2s(&loadedSheets[i]);
	for (int i = 0; i < nArr; ++i)
		free_local_sprite_array(&localArrs[i]);

	wait_noinput(false, false, true);
}
