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
#include "sprite_picker.h"

#include "draw.h"
#include "editor_ui.h"
#include "font.h"
#include "game_menu.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "sfx.h"
#include "sndmast.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include "SDL.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
	SP_X0 = 7, SP_X1 = 313, SP_Y0 = 7, SP_Y1 = 183,
	SP_TAB_Y0 = 9, SP_TAB_Y1 = 19, SP_RULE_Y = 21,
	SP_GRID_X0 = 10, SP_GRID_Y0 = 24, SP_GRID_W = 232, SP_GRID_H = 154, SP_PAD = 2,
	SP_BAR_X = 245,
	SP_INFO_X0 = 249, SP_INFO_X1 = 310, SP_INFO_Y0 = 24, SP_INFO_H = 64,
	SP_SHEET_COLS = 19, SP_TILE_W = 12, SP_TILE_H = 14, SP_HULL_W = 57, SP_HULL_H = 38,
	SP_CELL_MAX = 64, SP_MARGIN = 2,
	SP_PAGES_MAX = 4, SP_TYPED_MAX = 5,
	SP_DRAG_PX = 4,
};

enum
{
	C_BACK = 0x60, C_CHECK = 0x61,
};

typedef struct
{
	int cols, rows, visible, cellW, cellH, x0, span, maxScroll;
} SpGrid;

typedef struct
{
	SpritePickerSpec spec;
	int page;
	int cursor[SP_PAGES_MAX];
	float scroll[SP_PAGES_MAX];
	int originalPage, originalCell;
	int hoverTab;
	char typed[SP_TYPED_MAX + 1];
	bool pressed, dragging, thumb;
	int pressY, pressCell, lastY;
	float pressScroll, fling;
	Uint32 lastMs;
} SpState;

static SDL_Surface *spScratch, *spStrip;

static int spCells(const SpritePickerPage *p)
{
	if (p->kind != SPRITE_PICKER_SHEET)
		return MAX(p->count, 0);
	const int last = p->first + p->count - 1 + (p->big ? SP_SHEET_COLS + 1 : 0);
	return clampi(last, 0, sprite2_count(p->sheet));
}

static bool spSelectable(const SpritePickerPage *p, int cell)
{
	if (cell < 0 || cell >= spCells(p))
		return false;
	if (p->kind == SPRITE_PICKER_SHEET)
		return cell + 1 >= p->first && cell + 1 < p->first + p->count;
	return true;
}

static int spValue(const SpritePickerPage *p, int cell)
{
	switch (p->kind)
	{
	case SPRITE_PICKER_SHEET:
		return p->valueBase + cell + 1;
	case SPRITE_PICKER_TABLE:
		return p->valueBase + p->first + cell;
	default:
		return (p->values != NULL) ? p->values[cell] : p->valueBase + cell;
	}
}

static int spCellOf(const SpritePickerPage *p, int value)
{
	const int cells = spCells(p);
	for (int cell = 0; cell < cells; ++cell)
		if (spSelectable(p, cell) && spValue(p, cell) == value)
			return cell;
	return -1;
}

static int spFirstSelectable(const SpritePickerPage *p)
{
	const int cells = spCells(p);
	for (int cell = 0; cell < cells; ++cell)
		if (spSelectable(p, cell))
			return cell;
	return 0;
}

static int spLastSelectable(const SpritePickerPage *p)
{
	for (int cell = spCells(p) - 1; cell > 0; --cell)
		if (spSelectable(p, cell))
			return cell;
	return 0;
}

static void spGrid(const SpritePickerPage *p, SpGrid *g)
{
	const bool sheet = (p->kind == SPRITE_PICKER_SHEET);
	g->cellW = sheet ? SP_TILE_W : clampi(p->cellW, 1, SP_GRID_W - 2 * SP_PAD);
	g->cellH = sheet ? SP_TILE_H : clampi(p->cellH, 1, SP_CELL_MAX);
	g->cols = sheet ? SP_SHEET_COLS : MAX((SP_GRID_W - 2 * SP_PAD) / g->cellW, 1);
	g->span = (sheet && p->big) ? 2 : 1;
	g->rows = (spCells(p) + g->cols - 1) / g->cols;
	if (g->span > 1)
		g->rows = MAX(g->rows, spLastSelectable(p) / g->cols + g->span);
	g->visible = MAX(SP_GRID_H / g->cellH, 1);
	g->x0 = SP_GRID_X0 + (SP_GRID_W - g->cols * g->cellW) / 2;
	g->maxScroll = MAX(g->rows * g->cellH + 2 * SP_PAD - SP_GRID_H, 0);
}

static const SpritePickerPage *spPage(const SpState *s)
{
	return &s->spec.pages[s->page];
}

static int spTop(const SpState *s)
{
	return (int)s->scroll[s->page];
}

static void spSetScroll(SpState *s, float scroll)
{
	SpGrid g;
	spGrid(spPage(s), &g);
	s->scroll[s->page] = (scroll < 0.0f) ? 0.0f
	                   : (scroll > (float)g.maxScroll) ? (float)g.maxScroll : scroll;
}

static bool spEnsureScratch(int w, int h)
{
	if (spScratch != NULL && spScratch->w >= w && spScratch->h >= h)
		return true;
	if (spScratch != NULL)
		SDL_FreeSurface(spScratch);
	spScratch = SDL_CreateRGBSurface(0, MAX(w, 256), MAX(h, 256), 8, 0, 0, 0, 0);
	return spScratch != NULL;
}

void spritePickerDrawFit(SDL_Surface *dst, unsigned int table, unsigned int index, int x, int y,
                         int w, int h, int maxScale)
{
	if (index >= sprite_table[table].count || !sprite_exists(table, index))
		return;
	const int sw = get_sprite_width(table, index), sh = get_sprite_height(table, index);
	if (sw == 0 || sh == 0 || w <= 0 || h <= 0 || !spEnsureScratch(sw, sh))
		return;

	SDL_FillRect(spScratch, NULL, 0);
	blit_sprite(spScratch, 0, 0, table, index);

	const int shrink = MAX(MAX((sw + w - 1) / w, (sh + h - 1) / h), 1);
	const int grow = (shrink > 1) ? 1 : clampi(MIN(w / sw, h / sh), 1, MAX(maxScale, 1));
	const int dw = sw * grow / shrink, dh = sh * grow / shrink;
	const int ox = x + (w - dw) / 2, oy = y + (h - dh) / 2;
	for (int dy = 0; dy < dh; ++dy)
	{
		if (oy + dy < 0 || oy + dy >= dst->h)
			continue;
		const Uint8 *const src =
		    (const Uint8 *)spScratch->pixels + dy * shrink / grow * spScratch->pitch;
		Uint8 *const row = (Uint8 *)dst->pixels + (oy + dy) * dst->pitch;
		for (int dx = 0; dx < dw; ++dx)
		{
			const Uint8 c = src[dx * shrink / grow];
			if (c != 0 && ox + dx >= 0 && ox + dx < dst->w)
				row[ox + dx] = c;
		}
	}
}

static void spDraw2x2(SDL_Surface *dst, const Sprite2_array *sheet, unsigned int index, int x,
                      int y, int w, int h)
{
	if ((int)index + SP_SHEET_COLS + 1 > sprite2_count(sheet))
		return;
	const int scale = (w >= 4 * SP_TILE_W && h >= 4 * SP_TILE_H) ? 2 : 1;
	blit_sprite2x2_scaled(dst, x + (w - 2 * SP_TILE_W * scale) / 2,
	                      y + (h - 2 * SP_TILE_H * scale) / 2, *sheet, index, scale, BLIT2_COPY, 0);
}

void spritePickerDrawHull(const Sprite2_array *sheet, JE_word gr, int x, int y, int w, int h)
{
	if (gr <= 1)
		shop_draw_hull(sheet, gr, x + w / 2 - SHOP_WIDE_HULL_HALF, y + (h - 2 * SP_TILE_H) / 2);
	else
		spDraw2x2(VGAScreen, sheet, gr, x, y, w, h);
}

SpritePickerPage spritePickerHullPage(const char *name, const int *values, int count,
                                      void (*draw)(int value, int x, int y, int w, int h),
                                      const char *(*label)(int value, char *buf, size_t n))
{
	const SpritePickerPage page = { .name = name, .kind = SPRITE_PICKER_LIST, .count = count,
	                                .cellW = SP_HULL_W, .cellH = SP_HULL_H, .values = values,
	                                .listDraw = draw, .listName = label };
	return page;
}

int spritePickerIndexOf(const int *values, int count, int value)
{
	for (int i = 0; i < count; ++i)
		if (values[i] == value)
			return i;
	return -1;
}

int spritePickerCycle(const int *values, int count, int current, int dir)
{
	if (count == 0)
		return current;
	const int i = spritePickerIndexOf(values, count, current);
	if (i < 0)
		return values[dir < 0 ? count - 1 : 0];
	return values[(i + (dir < 0 ? count - 1 : 1)) % count];
}

static void spDrawCell(SDL_Surface *dst, const SpritePickerPage *p, int cell, int x, int y, int w,
                       int h)
{
	switch (p->kind)
	{
	case SPRITE_PICKER_SHEET:
		if (cell + 1 <= sprite2_count(p->sheet))
			blit_sprite2(dst, x, y, *p->sheet, (unsigned int)cell + 1);
		break;
	case SPRITE_PICKER_TABLE:
		spritePickerDrawFit(dst, p->table, (unsigned int)(p->first + cell), x + 1, y + 1, w - 2,
		                    h - 2, 1);
		break;
	default:
		if (p->listDraw != NULL)
			p->listDraw(spValue(p, cell), x, y, w, h);
		break;
	}
}

static void spDrawPreview(const SpritePickerPage *p, int cell, int x0, int y0, int w, int h)
{
	if (p->kind == SPRITE_PICKER_TABLE)
	{
		spritePickerDrawFit(VGAScreen, p->table, (unsigned int)(p->first + cell), x0 + 2, y0 + 2,
		                    w - 4, h - 4, 3);
		return;
	}
	if (p->kind == SPRITE_PICKER_LIST)
	{
		if (p->listDraw != NULL)
			p->listDraw(spValue(p, cell), x0, y0, w, h);
		return;
	}

	const unsigned int index = (unsigned int)cell + 1;
	if ((int)index > sprite2_count(p->sheet))
		return;
	if (p->big)
	{
		spDraw2x2(VGAScreen, p->sheet, index, x0, y0, w, h);
		return;
	}

	int ix0, iy0, ix1, iy1;
	if (!sprite2_ink_bounds(*p->sheet, index, &ix0, &iy0, &ix1, &iy1))
		return;
	const int iw = ix1 - ix0 + 1, ih = iy1 - iy0 + 1;
	const int scale = clampi(MIN((w - 4) / iw, (h - 4) / ih), 1, 4);
	blit_sprite2_scaled(VGAScreen, x0 + (w - iw * scale) / 2 - ix0 * scale,
	                    y0 + (h - ih * scale) / 2 - iy0 * scale, *p->sheet, index, scale,
	                    BLIT2_COPY, 0);
}

static void spTabRect(const SpritePickerSpec *spec, int tab, int *x0, int *x1)
{
	int x = SP_X1 - 3;
	*x0 = *x1 = x;
	for (int t = spec->pageCount - 1; t >= tab; --t)
	{
		*x1 = x;
		*x0 = x - JE_textWidth(spec->pages[t].name, small_font) - 8 + 1;
		x = *x0 - 2;
	}
}

static void spBlockRect(const SpGrid *g, int cell, int firstRow, int *x0, int *y0, int *x1, int *y1)
{
	const int left = SP_MARGIN + (g->x0 - SP_GRID_X0);
	*x0 = left + cell % g->cols * g->cellW - 1;
	*y0 = SP_MARGIN + (cell / g->cols - firstRow) * g->cellH - 1;
	*x1 = MIN(*x0 + g->span * g->cellW + 1, left + g->cols * g->cellW);
	*y1 = *y0 + g->span * g->cellH + 1;
}

static void spDrawGrid(const SpState *s, const SpGrid *g)
{
	const SpritePickerPage *const p = spPage(s);
	if (spStrip == NULL)
		spStrip = SDL_CreateRGBSurface(0, SP_GRID_W + 2 * SP_MARGIN,
		                               SP_GRID_H + 2 * SP_CELL_MAX + 2 * SP_MARGIN, 8, 0, 0, 0, 0);
	if (spStrip == NULL)
		return;

	const int top = spTop(s), firstRow = MAX(top - SP_PAD, 0) / g->cellH;
	const int base = SP_PAD + firstRow * g->cellH - SP_MARGIN;
	const int rowsDrawn =
	    MIN((top + SP_GRID_H - SP_PAD + g->cellH - 1) / g->cellH, g->rows) - firstRow;
	const int left = SP_MARGIN + (g->x0 - SP_GRID_X0);
	SDL_FillRect(spStrip, NULL, C_BACK);
	for (int r = 0; r < rowsDrawn; ++r)
		for (int c = 0; c < g->cols; ++c)
			if ((firstRow + r + c) % 2 != 0)
				fill_rectangle_wh(spStrip, left + c * g->cellW, SP_MARGIN + r * g->cellH,
				                  (uint)g->cellW, (uint)g->cellH, C_CHECK);

	SDL_Surface *const saved = VGAScreen;
	VGAScreen = spStrip;
	const int cells = spCells(p);
	for (int r = 0; r < rowsDrawn; ++r)
		for (int c = 0; c < g->cols; ++c)
		{
			const int cell = (firstRow + r) * g->cols + c;
			if (cell < cells)
				spDrawCell(spStrip, p, cell, left + c * g->cellW, SP_MARGIN + r * g->cellH,
				           g->cellW, g->cellH);
		}
	VGAScreen = saved;

	int x0, y0, x1, y1;
	if (s->originalPage == s->page && s->originalCell != s->cursor[s->page])
	{
		spBlockRect(g, s->originalCell, firstRow, &x0, &y0, &x1, &y1);
		draw_corner_clamps(spStrip, x0, y0, x1, y1, 2, C_SEL);
	}
	if (spSelectable(p, s->cursor[s->page]))
	{
		spBlockRect(g, s->cursor[s->page], firstRow, &x0, &y0, &x1, &y1);
		draw_rect_outline(spStrip, x0, y0, x1, y1, C_HI);
	}

	for (int y = 0; y < SP_GRID_H; ++y)
		memcpy((Uint8 *)VGAScreen->pixels + (SP_GRID_Y0 + y) * VGAScreen->pitch + SP_GRID_X0,
		       (const Uint8 *)spStrip->pixels + (top - base + y) * spStrip->pitch + SP_MARGIN,
		       SP_GRID_W);
}

static void spDrawText(int x, int y, const char *text, Sint8 value)
{
	char fit[48];
	draw_font_hv_shadow(VGAScreen, x, y,
	                    font_fit_text(text, small_font, SP_INFO_X1 - SP_INFO_X0, fit, sizeof(fit)),
	                    small_font, centered, 15, value, false, 1);
}

static int spDrawWrapped(int x, int y, const char *text, Sint8 value)
{
	char first[48];
	SDL_strlcpy(first, text, sizeof(first));
	if (JE_textWidth(first, small_font) <= SP_INFO_X1 - SP_INFO_X0)
	{
		spDrawText(x, y, first, value);
		return 1;
	}
	for (size_t cut = strlen(first); cut > 0; --cut)
	{
		if (first[cut] != ' ')
			continue;
		first[cut] = '\0';
		if (JE_textWidth(first, small_font) <= SP_INFO_X1 - SP_INFO_X0)
		{
			spDrawText(x, y, first, value);
			spDrawText(x, y + 10, text + cut + 1, value);
			return 2;
		}
	}
	spDrawText(x, y, text, value);
	return 1;
}

static void spDraw(const SpState *s)
{
	const SpritePickerSpec *const spec = &s->spec;
	const SpritePickerPage *const p = spPage(s);
	SpGrid g;
	spGrid(p, &g);

	editorDrawPanel(SP_X0, SP_Y0, SP_X1, SP_Y1);
	draw_font_hv_shadow(VGAScreen, SP_X0 + 5, SP_TAB_Y0 + 2, spec->title, small_font, left_aligned,
	                    15, 6, false, 1);
	for (int t = 0; t < spec->pageCount && spec->pageCount > 1; ++t)
	{
		int x0, x1;
		spTabRect(spec, t, &x0, &x1);
		const bool cur = (t == s->page);
		const Uint8 fill = cur ? C_SEL : C_DIV;
		fill_rectangle_xy(VGAScreen, x0, SP_TAB_Y0, x1, SP_TAB_Y1,
		                  (Uint8)(t == s->hoverTab ? fill - 1 : fill));
		draw_font_hv_shadow(VGAScreen, (x0 + x1 + 1) / 2, SP_TAB_Y0 + 2, spec->pages[t].name,
		                    small_font, centered, 15, (cur || t == s->hoverTab) ? 6 : 4, false, 1);
	}
	fill_rectangle_xy(VGAScreen, SP_X0 + 2, SP_RULE_Y, SP_X1 - 2, SP_RULE_Y, C_DIV);

	fill_rectangle_xy(VGAScreen, SP_GRID_X0 - 2, SP_GRID_Y0 - 2, SP_GRID_X0 + SP_GRID_W + 1,
	                  SP_GRID_Y0 + SP_GRID_H + 1, C_WELL);
	spDrawGrid(s, &g);

	const int top = spTop(s);
	if (g.maxScroll > 0)
	{
		const int total = g.rows * g.cellH + 2 * SP_PAD;
		const int thumb0 = SP_GRID_Y0 + top * SP_GRID_H / total;
		const int thumb1 = SP_GRID_Y0 + (top + SP_GRID_H) * SP_GRID_H / total - 1;
		fill_rectangle_xy(VGAScreen, SP_BAR_X, SP_GRID_Y0, SP_BAR_X + 1, SP_GRID_Y0 + SP_GRID_H - 1,
		                  C_DIV);
		fill_rectangle_xy(VGAScreen, SP_BAR_X, thumb0, SP_BAR_X + 1, thumb1,
		                  s->thumb ? C_SEL : C_HI);
	}

	const int cursor = s->cursor[s->page];
	const int iy1 = SP_INFO_Y0 + SP_INFO_H - 1, mid = (SP_INFO_X0 + SP_INFO_X1 + 1) / 2;
	editorDrawWell(SP_INFO_X0, SP_INFO_Y0, SP_INFO_X1, iy1, C_BACK, C_DIV, C_HI);
	if (spSelectable(p, cursor))
		spDrawPreview(p, cursor, SP_INFO_X0 + 1, SP_INFO_Y0 + 1, SP_INFO_X1 - SP_INFO_X0 - 1,
		              SP_INFO_H - 2);

	char line[48];
	if (s->typed[0] != '\0')
		snprintf(line, sizeof(line), "No. %s%s", s->typed,
		         (SDL_GetTicks() / 400) % 2 == 0 ? "_" : "");
	else if (spSelectable(p, cursor))
		snprintf(line, sizeof(line), "No. %d", spValue(p, cursor));
	else
		line[0] = '\0';
	spDrawText(mid, iy1 + 5, line, s->typed[0] != '\0' ? 7 : 6);

	char name[48];
	const char *label = p->name;
	if (p->kind == SPRITE_PICKER_LIST && p->listName != NULL && spSelectable(p, cursor))
		label = p->listName(spValue(p, cursor), name, sizeof(name));
	const int nameLines = spDrawWrapped(mid, iy1 + 15, (label != NULL) ? label : "", 3);

	if (s->originalPage >= 0)
	{
		snprintf(line, sizeof(line), "Was %d",
		         spValue(&spec->pages[s->originalPage], s->originalCell));
		spDrawText(mid, iy1 + 15 + 10 * nameLines, line, 2);
	}

	const char *const hint = (spec->pageCount > 1)
	                       ? "Tap or Enter picks, drag scrolls, type a number, Tab pages."
	                       : "Tap or Enter picks, drag scrolls, type a number. Esc cancels.";
	editorDrawHint(hint);
}

static void spEnsureVisible(SpState *s)
{
	SpGrid g;
	spGrid(spPage(s), &g);
	const int rowTop = SP_PAD + s->cursor[s->page] / g.cols * g.cellH - 1;
	const int rowBottom = rowTop + g.span * g.cellH + 2;
	if (rowTop < spTop(s))
		spSetScroll(s, (float)rowTop);
	else if (rowBottom > spTop(s) + SP_GRID_H)
		spSetScroll(s, (float)(rowBottom - SP_GRID_H));
}

static void spMove(SpState *s, int delta)
{
	const SpritePickerPage *const p = spPage(s);
	const int cell = clampi(s->cursor[s->page] + delta, spFirstSelectable(p), spLastSelectable(p));
	s->typed[0] = '\0';
	s->fling = 0.0f;
	if (cell == s->cursor[s->page])
		return;
	s->cursor[s->page] = cell;
	spEnsureVisible(s);
	JE_playSampleNum(S_CURSOR);
}

static void spSwitchPage(SpState *s, int page)
{
	const int count = s->spec.pageCount;
	s->page = (page % count + count) % count;
	s->typed[0] = '\0';
	s->fling = 0.0f;
	JE_playSampleNum(S_CURSOR);
}

static void spJump(SpState *s, int value)
{
	for (int k = 0; k < s->spec.pageCount; ++k)
	{
		const int t = (s->page + k) % s->spec.pageCount;
		const int cell = spCellOf(&s->spec.pages[t], value);
		if (cell >= 0)
		{
			s->page = t;
			s->cursor[t] = cell;
			spEnsureVisible(s);
			return;
		}
	}
}

static int spCellAt(const SpState *s, const SpGrid *g, int mx, int my)
{
	if (mx < g->x0 || mx >= g->x0 + g->cols * g->cellW || my < SP_GRID_Y0 ||
	    my >= SP_GRID_Y0 + SP_GRID_H)
		return -1;
	const int y = my - SP_GRID_Y0 + spTop(s) - SP_PAD;
	if (y < 0)
		return -1;
	const int cell = y / g->cellH * g->cols + (mx - g->x0) / g->cellW;
	return spSelectable(spPage(s), cell) ? cell : -1;
}

static bool spInGrid(int mx, int my)
{
	return mx >= SP_GRID_X0 && mx < SP_GRID_X0 + SP_GRID_W && my >= SP_GRID_Y0 &&
	       my < SP_GRID_Y0 + SP_GRID_H;
}

static bool spOnThumbTrack(int mx, int my)
{
	return mx >= SP_BAR_X - 3 && mx <= SP_BAR_X + 4 && my >= SP_GRID_Y0 - 2 &&
	       my <= SP_GRID_Y0 + SP_GRID_H + 1;
}

static void spThumbTo(SpState *s, int my)
{
	SpGrid g;
	spGrid(spPage(s), &g);
	spSetScroll(s, (float)((my - SP_GRID_Y0) * (g.rows * g.cellH + 2 * SP_PAD) / SP_GRID_H -
	                       SP_GRID_H / 2));
}

static void spHandleMouse(SpState *s, int *prevX, int *prevY, bool *done, bool *picked, int *value)
{
	const SpritePickerPage *const p = spPage(s);
	SpGrid g;
	spGrid(p, &g);
	const Uint32 now = SDL_GetTicks();
	const float dt = (float)MIN(now - s->lastMs, 100u);
	s->lastMs = now;

	if (mouse_scroll != 0)
	{
		spSetScroll(s, s->scroll[s->page] - (float)(mouse_scroll * MAX(g.cellH, 2 * SP_TILE_H)));
		s->fling = 0.0f;
		mouse_scroll = 0;
	}

	s->hoverTab = -1;
	for (int t = 0; t < s->spec.pageCount && s->spec.pageCount > 1 && !mouseInactive; ++t)
	{
		int x0, x1;
		spTabRect(&s->spec, t, &x0, &x1);
		if (mouse_x >= x0 && mouse_x <= x1 && mouse_y >= SP_TAB_Y0 && mouse_y <= SP_TAB_Y1)
			s->hoverTab = t;
	}

	if (s->pressed && !mousedown)
	{
		const bool tap = !s->thumb && !s->dragging && s->pressCell >= 0 &&
		                 spCellAt(s, &g, mouse_x, mouse_y) == s->pressCell;
		s->pressed = s->thumb = s->dragging = false;
		if (tap)
		{
			*value = spValue(p, s->pressCell);
			*picked = *done = true;
			JE_playSampleNum(S_SELECT);
			return;
		}
	}

	if (s->pressed)
	{
		if (s->thumb)
		{
			spThumbTo(s, mouse_y);
		}
		else
		{
			if (!s->dragging && abs(mouse_y - s->pressY) > SP_DRAG_PX)
				s->dragging = true;
			if (s->dragging)
			{
				spSetScroll(s, s->pressScroll - (float)(mouse_y - s->pressY));
				if (dt > 0.0f)
					s->fling = 0.6f * s->fling + 0.4f * (float)(s->lastY - mouse_y) / dt;
			}
		}
		s->lastY = mouse_y;
	}
	else if (s->fling != 0.0f)
	{
		const float before = s->scroll[s->page];
		spSetScroll(s, before + s->fling * dt);
		s->fling *= powf(0.994f, dt);
		if (fabsf(s->fling) < 0.02f || s->scroll[s->page] == before)
			s->fling = 0.0f;
	}

	const int hoverCell = (mouseInactive || s->pressed) ? -1 : spCellAt(s, &g, mouse_x, mouse_y);
	const bool moved = (mouse_x != *prevX || mouse_y != *prevY);
	*prevX = mouse_x;
	*prevY = mouse_y;
	if (hoverCell >= 0 && moved && hoverCell != s->cursor[s->page])
	{
		s->cursor[s->page] = hoverCell;
		s->typed[0] = '\0';
	}

	if (!newmouse)
		return;
	newmouse = false;
	if (lastmouse_but == SDL_BUTTON_RIGHT)
	{
		*done = true;
		return;
	}
	if (s->hoverTab >= 0)
	{
		if (s->hoverTab != s->page)
			spSwitchPage(s, s->hoverTab);
		return;
	}

	s->fling = 0.0f;
	if (g.maxScroll > 0 && spOnThumbTrack(mouse_x, mouse_y))
	{
		s->pressed = s->thumb = true;
		spThumbTo(s, mouse_y);
		return;
	}
	if (!spInGrid(mouse_x, mouse_y))
		return;
	const int cell = spCellAt(s, &g, mouse_x, mouse_y);
	s->pressed = true;
	s->dragging = false;
	s->pressY = s->lastY = mouse_y;
	s->pressScroll = s->scroll[s->page];
	s->pressCell = cell;
}

static void spHandleText(SpState *s)
{
	for (size_t i = 0; last_text[i] != '\0'; ++i)
	{
		const size_t len = strlen(s->typed);
		if (last_text[i] >= '0' && last_text[i] <= '9' && len < SP_TYPED_MAX)
		{
			s->typed[len] = last_text[i];
			s->typed[len + 1] = '\0';
		}
	}
	if (s->typed[0] != '\0')
		spJump(s, atoi(s->typed));
}

static void spHandleKey(SpState *s, bool *done, bool *picked, int *value)
{
	const SpritePickerPage *const p = spPage(s);
	SpGrid g;
	spGrid(p, &g);
	switch (lastkey_scan)
	{
	case SDL_SCANCODE_LEFT:
		spMove(s, -1);
		break;
	case SDL_SCANCODE_RIGHT:
		spMove(s, 1);
		break;
	case SDL_SCANCODE_UP:
		spMove(s, -g.cols * g.span);
		break;
	case SDL_SCANCODE_DOWN:
		spMove(s, g.cols * g.span);
		break;
	case SDL_SCANCODE_PAGEUP:
		spMove(s, -g.cols * MAX(g.visible - g.span, 1));
		break;
	case SDL_SCANCODE_PAGEDOWN:
		spMove(s, g.cols * MAX(g.visible - g.span, 1));
		break;
	case SDL_SCANCODE_HOME:
		spMove(s, -spCells(p));
		break;
	case SDL_SCANCODE_END:
		spMove(s, spCells(p));
		break;
	case SDL_SCANCODE_TAB:
		if (s->spec.pageCount > 1)
			spSwitchPage(s, s->page + ((lastkey_mod & KMOD_SHIFT) != 0 ? -1 : 1));
		break;
	case SDL_SCANCODE_BACKSPACE:
		if (s->typed[0] != '\0')
			s->typed[strlen(s->typed) - 1] = '\0';
		break;
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
	case SDL_SCANCODE_SPACE:
		if (s->typed[0] != '\0')
			*value = atoi(s->typed);
		else if (spSelectable(p, s->cursor[s->page]))
			*value = spValue(p, s->cursor[s->page]);
		else
		{
			JE_playSampleNum(S_SPRING);
			break;
		}
		*picked = *done = true;
		JE_playSampleNum(S_SELECT);
		break;
	case SDL_SCANCODE_ESCAPE:
		if (s->typed[0] != '\0')
			s->typed[0] = '\0';
		else
			*done = true;
		break;
	default:
		break;
	}
}

bool spritePickerRun(const SpritePickerSpec *spec, int *value)
{
	if (spec == NULL || spec->pages == NULL || spec->pageCount <= 0 || value == NULL)
		return false;

	SpState s;
	memset(&s, 0, sizeof(s));
	s.spec = *spec;
	s.spec.pageCount = MIN(spec->pageCount, SP_PAGES_MAX);
	s.originalPage = s.originalCell = -1;
	s.hoverTab = -1;
	s.pressCell = -1;
	s.lastMs = SDL_GetTicks();
	for (int t = 0; t < s.spec.pageCount; ++t)
		s.cursor[t] = spFirstSelectable(&s.spec.pages[t]);
	for (int t = 0; t < s.spec.pageCount && s.originalPage < 0; ++t)
	{
		const int cell = spCellOf(&s.spec.pages[t], *value);
		if (cell >= 0)
		{
			s.originalPage = t;
			s.originalCell = s.cursor[t] = cell;
		}
	}
	for (int t = 0; t < s.spec.pageCount; ++t)
	{
		SpGrid g;
		spGrid(&s.spec.pages[t], &g);
		s.page = t;
		spSetScroll(&s, (float)(SP_PAD + s.cursor[t] / g.cols * g.cellH + g.span * g.cellH / 2 -
		                        SP_GRID_H / 2));
	}
	s.page = MAX(s.originalPage, 0);

	newkey = newmouse = new_text = false;
	mouse_scroll = 0;
	int prevX = mouse_x, prevY = mouse_y;
	bool done = false, picked = false;
	const double tick_ms = editorTickMs();
	double nextTick_ms = SDL_GetTicks();
	while (!done)
	{
		if (s.spec.idle != NULL)
			s.spec.idle();
		if (tick_schedule_due(&nextTick_ms, tick_ms, SDL_GetTicks()) && s.spec.tick != NULL)
			s.spec.tick();
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);
		spDraw(&s);
		touch_ui_set_layout(TOUCH_LAYOUT_LIST);
		mouseCursor = MOUSE_POINTER_NORMAL;

		setDelay(1);
		for (;;)
		{
			push_joysticks_as_keyboard();
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (newkey || newmouse || new_text || mouse_scroll != 0 || mouse_x != prevX ||
			    mouse_y != prevY || s.pressed != (mousedown != 0) || s.fling != 0.0f ||
			    getDelayTicks() == 0)
				break;
			if (!output_vsync)
				limit_render_fps();
		}

		spHandleMouse(&s, &prevX, &prevY, &done, &picked, value);
		if (!done && new_text)
			spHandleText(&s);
		if (!done && newkey)
			spHandleKey(&s, &done, &picked, value);
		newkey = newmouse = new_text = false;
	}
	return picked;
}
