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
#include "backgrnd.h"

#include "config.h"
#include "mtrand.h"
#include "opentyr.h"
#include "render_list.h"
#include "rollback.h"
#include "swar.h"
#include "varz.h"
#include "video.h"

#include <assert.h>

// Two extra tiles cover negative horizontal scroll without exposing the right edge.
#define BG_TILE_W 24
#define BG_TILE_COUNT (PLAYFIELD_WIDTH / BG_TILE_W + 2)

// Mirrored Layers only: extra tiles appended past the right end of a row (see bg_edge_px).
#define BG_EDGE_TILES 1

/*Special Background 2 and Background 3*/

/*Back Pos 3*/
JE_word backPos, backPos2, backPos3;
JE_word backMove, backMove2, backMove3;

// Per-layer Endless smooth-scroll increments; zero when inactive.
int endlessScrollExtraPx1 = 0, endlessScrollExtraPx2 = 0, endlessScrollExtraPx3 = 0;

// See backgrnd.h: true during the sim tick (advance scroll as normal), false
// during interpolated re-draws so the layer can be re-rendered without moving.
bool background_advance = true;

// See backgrnd.h. bgScrollDeltaY holds the true per-tick vertical scroll of each layer;
// bgMarginRows widens the interpolation's bottom margin under a speed modifier.
int bgScrollDeltaY[4] = { 0, 0, 0, 0 };
int bgMarginRows = 1;

// Unfloored parallax offsets captured where each layer is drawn.
float mapXOfs_f, mapX2Ofs_f, mapX3Ofs_f;
// Previous-tick offsets used by layer-bound enemies.
float oldMapXOfs_f, oldMapX3Ofs_f;
float bg_layer_dx[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };  // index 1..3
float bg_layer_frac[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
float bg_layer_xofs[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
bool  bg_layer_xofs_valid[4] = { false, false, false, false };
static float bg_layer_ofs_prev[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

// Vertical scroll rate and subpixel remainder for each layer.
float bg_layer_dy[4]    = { 0.0f, 0.0f, 0.0f, 0.0f };
float bg_layer_yfrac[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
bool  bg_smooth_y_active = false;

// Current-tick values for layer 3, which advances before drawing. Layers 1 and 2 use the
// lagged values above to match their recorded rows and entities.
float bg_layer_yfrac_now[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
float bg_layer_dy_now[4]    = { 0.0f, 0.0f, 0.0f, 0.0f };

// Record a layer's float delta and fractional offset. Loads and warps snap the delta.
static void bg_set_layer_dx(int layer, float cur_f, int cur_int)
{
	float d = cur_f - bg_layer_ofs_prev[layer];
	if (d > 12.0f || d < -12.0f)
		d = 0.0f;
	bg_layer_dx[layer] = d;
	bg_layer_frac[layer] = cur_f - (float)cur_int;
	bg_layer_xofs[layer] = cur_f;
	bg_layer_xofs_valid[layer] = true;
	bg_layer_ofs_prev[layer] = cur_f;
}

// Track full vertical layer deltas, including whole-tile mapY steps.
// Level loads and wraps produce wild deltas and are snapped.
static int  bgPrevMapY[4]    = { 0, 0, 0, 0 };
static int  bgPrevBackPos[4] = { 0, 0, 0, 0 };
static bool bgPrevValid[4]   = { false, false, false, false };

static void bg_update_scroll_delta(int layer, int mapY_now, int backPos_now)
{
	if (bgPrevValid[layer])
	{
		int d = (bgPrevMapY[layer] - mapY_now) * 28 + (backPos_now - bgPrevBackPos[layer]);
		if (d < 0 || d > 300)  // level reset / map wrap: not a real scroll step -> snap
			d = 0;
		bgScrollDeltaY[layer] = d;
	}
	else
	{
		bgScrollDeltaY[layer] = 0;
	}
	bgPrevMapY[layer] = mapY_now;
	bgPrevBackPos[layer] = backPos_now;
	bgPrevValid[layer] = true;
}

/*Main Maps*/
JE_word mapX, mapY, mapX2, mapX3, mapY2, mapY3;
JE_byte **mapYPos, **mapY2Pos, **mapY3Pos;
JE_integer mapXPos, oldMapXOfs, mapXOfs, mapX2Ofs, mapX2Pos, mapX3Pos, oldMapX3Ofs, mapX3Ofs, tempMapXOfs;
intptr_t mapXbpPos, mapX2bpPos, mapX3bpPos;
JE_byte map1YDelay, map1YDelayMax, map2YDelay, map2YDelayMax;

JE_boolean  anySmoothies;
JE_byte     smoothie_data[9]; /* [1..9] */

void darken_background_apply(SDL_Surface *surface, JE_word neat)
{
	Uint8 *s = surface->pixels; /* screen pointer, 8-bit specific */
	int x, y;
	
	s += PLAYFIELD_LEFT;
	
	for (y = 184; y; y--)
	{
		for (x = PLAYFIELD_WIDTH; x; x--)
		{
			*s = ((((*s & 0x0f) << 4) - (*s & 0x0f) + ((((x - neat - y) >> 2) + *(s - 2) + (y == 184 ? 0 : *(s - (surface->pitch - 1)))) & 0x0f)) >> 4) | (*s & 0xf0);
			s++;
		}
		s += surface->pitch - PLAYFIELD_WIDTH;
	}
}

void darken_background_apply_scaled(SDL_Surface *surface, JE_word neat, int scale)
{
	const int pitch = surface->pitch;

	for (int hy = 0; hy < 184 * scale; ++hy)
	{
		const int y = 184 - hy / scale;
		Uint8 *s = (Uint8 *)surface->pixels + hy * pitch + PLAYFIELD_LEFT * scale;
		for (int hx = 0; hx < PLAYFIELD_WIDTH * scale; ++hx, ++s)
		{
			const int x = PLAYFIELD_WIDTH - hx / scale;
			const int above = (y == 184) ? 0 : *(s - scale * pitch + scale);
			const int mix = (((x - neat - y) >> 2) + *(s - 2 * scale) + above) & 0x0f;
			*s = ((((*s & 0x0f) << 4) - (*s & 0x0f) + mix) >> 4) | (*s & 0xf0);
		}
	}
}

void JE_darkenBackground(JE_word neat)  /* wild detail level */
{
	if (render_list_recording)
		rl_rec_darken(neat);
	darken_background_apply(VGAScreen, neat);
}

// Mirror out-of-row columns so wider parallax never reads the adjacent map row.
static Uint8 *bg_mirror_tile(Uint8 **map, int tile, int mirror_w, int col0, bool *flip)
{
	*flip = false;
	if (mirror_w == 0)
		return map[tile];
	const int c = col0 + tile;
	if (c >= 0 && c < mirror_w)
		return map[tile];
	*flip = true;
	return (map - col0)[c < 0 ? -1 - c : 2 * mirror_w - 1 - c];
}

// Extend mirrored rows to the surface edge so smoothie filters never sample black.
static int bg_edge_px(int mirror_w, int x, int surface_w, int scale)
{
	if (mirror_w == 0)
		return 0;
	const int room = (surface_w - x - BG_TILE_COUNT * BG_TILE_W * scale) / scale;
	if (room <= 0)
		return 0;
	return room < BG_EDGE_TILES * BG_TILE_W ? room : BG_EDGE_TILES * BG_TILE_W;
}

static inline void blit_tile_row(Uint8 *dst, const Uint8 *data, bool flip, bool blend)
{
	for (int i = 0; i < BG_TILE_W; i += SWAR8_BYTES)
	{
		const Swar8 s = flip ? swar8_reverse(swar8_read(data + BG_TILE_W - SWAR8_BYTES - i))
		                     : swar8_read(data + i);
		const Swar8 d = swar8_read(dst + i);
		const Swar8 v = blend ? ((s & SWAR8(0xF0)) | swar8_avg_low(s, d)) : s;
		swar8_write(dst + i, swar8_select(d, v, swar8_nonzero(s)));
	}
}

static inline void blit_background_rows(SDL_Surface *surface, int x, int y, Uint8 **map,
                                        int mirror_w, int col0, bool blend)
{
	Uint8 *pixels = (Uint8 *)surface->pixels + (y * surface->pitch) + x,
	      *pixels_ll = (Uint8 *)surface->pixels,  // lower limit
	      *pixels_ul = (Uint8 *)surface->pixels + (surface->h * surface->pitch);  // upper limit

	const int edge_px = bg_edge_px(mirror_w, x, surface->w, 1);
	const int tile_count = BG_TILE_COUNT + (edge_px > 0 ? 1 : 0);
	const int row_width = BG_TILE_COUNT * BG_TILE_W + edge_px;
	for (int ty = 0; ty < 28; ty++)
	{
		// not drawing on screen yet; skip y
		if ((pixels + row_width) < pixels_ll)
		{
			pixels += surface->pitch;
			continue;
		}

		const bool inside = pixels >= pixels_ll && pixels + row_width <= pixels_ul;

		for (int tile = 0; tile < tile_count; tile++)
		{
			// the appended edge tile (bg_edge_px) is clipped to the surface width
			const int tile_w = (tile < BG_TILE_COUNT) ? BG_TILE_W : edge_px;

			bool flip;
			const Uint8 *data = bg_mirror_tile(map, tile, mirror_w, col0, &flip);

			// no tile; skip tile
			if (data == NULL)
			{
				pixels += tile_w;
				continue;
			}

			data += ty * BG_TILE_W;

			if (inside && tile_w == BG_TILE_W)
			{
				blit_tile_row(pixels, data, flip, blend);
				pixels += BG_TILE_W;
				continue;
			}

			data += flip ? BG_TILE_W - 1 : 0;
			const int step = flip ? -1 : 1;

			for (int px = tile_w; px; px--)
			{
				if (pixels >= pixels_ul)
					return;
				if (pixels >= pixels_ll && *data != 0)
				{
					if (blend)
						*pixels = (*data & 0xf0) | (((*pixels & 0x0f) + (*data & 0x0f)) / 2);
					else
						*pixels = *data;
				}

				pixels++;
				data += step;
			}
		}

		pixels += surface->pitch - row_width;
	}
}

void blit_background_row(SDL_Surface *surface, int x, int y, Uint8 **map, int mirror_w, int col0)
{
	assert(surface->format->BitsPerPixel == 8);

	// Silent rollback re-simulation: state only, no pixels (see sprite.c).
	if (rollback_resim_silent)
		return;

	if (render_list_recording)
		rl_rec_bg_row(x, y, map, false, mirror_w, col0);

	blit_background_rows(surface, x, y, map, mirror_w, col0, false);
}

void blit_background_row_blend(SDL_Surface *surface, int x, int y, Uint8 **map, int mirror_w, int col0)
{
	assert(surface->format->BitsPerPixel == 8);

	// Silent rollback re-simulation: state only, no pixels (see sprite.c).
	if (rollback_resim_silent)
		return;

	if (render_list_recording)
		rl_rec_bg_row(x, y, map, true, mirror_w, col0);

	blit_background_rows(surface, x, y, map, mirror_w, col0, true);
}

static void blit_background_row_scaled_px(SDL_Surface *surface, int x, int y, Uint8 **map,
                                          int scale, bool blend, int mirror_w, int col0)
{
	assert(surface->format->BitsPerPixel == 8);

	const int edge_px = bg_edge_px(mirror_w, x, surface->w, scale);
	const int tile_count = BG_TILE_COUNT + (edge_px > 0 ? 1 : 0);

	for (int ty = 0; ty < 28; ++ty)
	{
		const int hy = y + ty * scale;
		if (hy >= surface->h)
			return;  // rows only grow downward

		int by0 = hy < 0 ? 0 : hy;
		int by1 = hy + scale;
		if (by1 > surface->h)
			by1 = surface->h;
		if (by1 <= by0)
			continue;  // this tile row is fully above the top edge

		int hx = x;
		for (int tile = 0; tile < tile_count; ++tile)
		{
			// the appended edge tile (bg_edge_px) is clipped to the surface width
			const int tile_w = (tile < BG_TILE_COUNT) ? BG_TILE_W : edge_px;

			bool flip;
			const Uint8 *data = bg_mirror_tile(map, tile, mirror_w, col0, &flip);

			if (data == NULL)  // no tile; skip
			{
				hx += tile_w * scale;
				continue;
			}

			data += ty * BG_TILE_W + (flip ? BG_TILE_W - 1 : 0);
			const int dstep = flip ? -1 : 1;

			for (int sx = 0; sx < tile_w; ++sx, data += dstep, hx += scale)
			{
				const Uint8 d = *data;
				if (d == 0)
					continue;  // transparent

				int bx0 = hx < 0 ? 0 : hx;
				int bx1 = hx + scale;
				if (bx1 > surface->w)
					bx1 = surface->w;
				if (bx1 <= bx0)
					continue;

				for (int yy = by0; yy < by1; ++yy)
				{
					Uint8 *p = (Uint8 *)surface->pixels + yy * surface->pitch + bx0;
					if (!blend)
					{
						for (int xx = bx0; xx < bx1; ++xx)
							*p++ = d;
					}
					else
					{
						for (int xx = bx0; xx < bx1; ++xx, ++p)
							*p = (d & 0xf0) | (((*p & 0x0f) + (d & 0x0f)) / 2);
					}
				}
			}
		}
	}
}

static inline void blit_expanded_row(Uint8 *dst, const Uint8 *src, int n, bool blend)
{
	int i = 0;
	for (; i + SWAR8_BYTES <= n; i += SWAR8_BYTES)
	{
		const Swar8 s = swar8_read(src + i);
		if (s == 0)
			continue;
		const Swar8 d = swar8_read(dst + i);
		const Swar8 v = blend ? ((s & SWAR8(0xF0)) | swar8_avg_low(s, d)) : s;
		swar8_write(dst + i, swar8_select(d, v, swar8_nonzero(s)));
	}
	for (; i < n; ++i)
	{
		const Uint8 d = src[i];
		if (d != 0)
			dst[i] = blend ? (Uint8)((d & 0xf0) | (((dst[i] & 0x0f) + (d & 0x0f)) / 2)) : d;
	}
}

// Supersampled tile row (render-list replay only; see backgrnd.h). One loop serves
// both the copy and blend ops; the pixel math matches the 1x blitters above.
void blit_background_row_scaled(SDL_Surface *surface, int x, int y, Uint8 **map, int scale, bool blend, int mirror_w, int col0)
{
	static Uint8 expanded[(BG_TILE_COUNT + BG_EDGE_TILES) * BG_TILE_W * RENDER_SUPERSAMPLE_LIMIT
	                      + sizeof(Swar8)];

	assert(surface->format->BitsPerPixel == 8);

	const int edge_px = bg_edge_px(mirror_w, x, surface->w, scale);
	const int tile_count = BG_TILE_COUNT + (edge_px > 0 ? 1 : 0);
	const int row_hi = (BG_TILE_COUNT * BG_TILE_W + edge_px) * scale;
	if (scale < 1 || row_hi + SWAR8_BYTES > (int)sizeof(expanded))
	{
		blit_background_row_scaled_px(surface, x, y, map, scale, blend, mirror_w, col0);
		return;
	}

	const int c0 = x < 0 ? -x : 0;
	const int c1 = (x + row_hi > surface->w) ? surface->w - x : row_hi;
	if (c1 <= c0)
		return;

	for (int ty = 0; ty < 28; ++ty)
	{
		const int hy = y + ty * scale;
		if (hy >= surface->h)
			return;

		const int by0 = hy < 0 ? 0 : hy;
		const int by1 = (hy + scale > surface->h) ? surface->h : hy + scale;
		if (by1 <= by0)
			continue;

		int span_start[BG_TILE_COUNT + BG_EDGE_TILES], span_end[BG_TILE_COUNT + BG_EDGE_TILES];
		int spans = 0;
		int col = 0;
		for (int tile = 0; tile < tile_count; ++tile)
		{
			const int tile_w = (tile < BG_TILE_COUNT) ? BG_TILE_W : edge_px;
			const int span_w = tile_w * scale;

			bool flip;
			const Uint8 *data = bg_mirror_tile(map, tile, mirror_w, col0, &flip);

			if (data != NULL)
			{
				data += ty * BG_TILE_W + (flip ? BG_TILE_W - 1 : 0);
				const int dstep = flip ? -1 : 1;

				Uint8 *e = expanded + col;
				for (int sx = 0; sx < tile_w; ++sx, data += dstep, e += scale)
					for (int k = 0; k < scale; k += SWAR8_BYTES)
						swar8_write(e + k, SWAR8(*data));

				if (spans > 0 && span_end[spans - 1] == col)
				{
					span_end[spans - 1] = col + span_w;
				}
				else
				{
					span_start[spans] = col;
					span_end[spans] = col + span_w;
					++spans;
				}
			}

			col += span_w;
		}

		for (int i = 0; i < spans; ++i)
		{
			const int a = MAX(span_start[i], c0);
			const int b = MIN(span_end[i], c1);
			if (a >= b)
				continue;

			for (int yy = by0; yy < by1; ++yy)
			{
				Uint8 *const dst = (Uint8 *)surface->pixels + yy * surface->pitch + (x + a);
				blit_expanded_row(dst, expanded + a, b - a, blend);
			}
		}
	}
}

static Uint8 **bg_margin_row(Uint8 **next, int m, int width, Uint8 **end)
{
	Uint8 **row = next + width * m;
	while (row >= next && row + BG_TILE_COUNT + BG_EDGE_TILES > end)
		row -= width;
	return row;
}

// Prepare a row walk, preserving mirror phase while guarding the map base.
static Uint8 **bg_mirror_setup(Uint8 **map, Uint8 **base, int width, int col0, int *out_w, int *out_c0)
{
	if (!mirroredLayers)
		return (extraParallax && map < base) ? base : map;
	if (map - col0 < base)
	{
		map = base;
		col0 = 0;
	}
	*out_w = width;
	*out_c0 = col0;
	return map;
}

// See backgrnd.h: layer 1's per-tick scroll delta and pan phase, published separately from its
// rows so a tick that skips the draw still leaves them current.
void bg_publish_layer_1_phase(void)
{
	bg_update_scroll_delta(1, (int)mapY, (int)backPos);
	bg_set_layer_dx(1, mapXOfs_f, mapXOfs);  // float delta + frac for smooth horizontal pan
}

void draw_background_1(SDL_Surface *surface)
{
	JE_clr256(surface);

	const int tile_count = BG_TILE_COUNT;
	Uint8** map = (Uint8**)mapYPos + mapXbpPos - tile_count;
	int mirror_w = 0, col0 = 0;
	map = bg_mirror_setup(map, (Uint8**)&megaData1.mainmap[0][0], 14, (int)mapXbpPos - 1, &mirror_w, &col0);

	bg_publish_layer_1_phase();

	rl_current_id = RL_ID_BG_BASE + 1;  // tag rows for cross-frame interpolation
	for (int i = -1; i < 7; i++)
	{
		blit_background_row(surface, mapXPos + PLAYFIELD_X_SHIFT, (i * 28) + backPos, map, mirror_w, col0);

		map += 14;
	}
	// Continue the map below the screen; modifiers may require several margin rows.
	Uint8 **const end = (Uint8 **)&megaData1.mainmap[0][0] + COUNTOF(megaData1.mainmap) * 14;
	for (int m = 0; m < bgMarginRows; ++m)
		blit_background_row(surface, mapXPos + PLAYFIELD_X_SHIFT, ((7 + m) * 28) + backPos,
		                    bg_margin_row(map, m, 14, end), mirror_w, col0);
	rl_current_id = 0;
}

void draw_background_2(SDL_Surface *surface)
{
	if (background_advance && map2YDelayMax > 1 && backMove2 < 2)
		backMove2 = (map2YDelay == 1) ? 1 : 0;
	const int tile_count = BG_TILE_COUNT;
	if (background2 != 0)
	{
		// water effect combines background 1 and 2 by synchronizing the x coordinate
		int x = (smoothies[1] ? mapXPos : mapX2Pos) + PLAYFIELD_X_SHIFT;

		Uint8** map = (Uint8**)mapY2Pos + (smoothies[1] ? mapXbpPos : mapX2bpPos) - tile_count;
		int mirror_w = 0, col0 = 0;
		map = bg_mirror_setup(map, (Uint8**)&megaData2.mainmap[0][0], 14,
		                      (int)(smoothies[1] ? mapXbpPos : mapX2bpPos) - 1, &mirror_w, &col0);

		bg_update_scroll_delta(2, (int)mapY2, (int)backPos2);

		rl_current_id = RL_ID_BG_BASE + 2;
		// smoothies[1] syncs layer 2 to layer 1's X phase (see x above), so use that offset.
		bg_set_layer_dx(2, smoothies[1] ? mapXOfs_f : mapX2Ofs_f, smoothies[1] ? mapXOfs : mapX2Ofs);
		for (int i = -1; i < 7; i++)
		{
			blit_background_row(surface, x, (i * 28) + backPos2, map, mirror_w, col0);

			map += 14;
		}
		// Extra bottom margin row(s) for the interpolation (see draw_background_1).
		Uint8 **const end = (Uint8 **)&megaData2.mainmap[0][0] + COUNTOF(megaData2.mainmap) * 14;
		for (int m = 0; m < bgMarginRows; ++m)
			blit_background_row(surface, x, ((7 + m) * 28) + backPos2,
			                    bg_margin_row(map, m, 14, end), mirror_w, col0);
		rl_current_id = 0;
	}

	/*Set Movement of background*/
	if (background_advance && --map2YDelay == 0)
	{
		map2YDelay = map2YDelayMax;
		
		backPos2 += backMove2;
		
		if (backPos2 >  27)
		{
			backPos2 -= 28;
			mapY2--;
			mapY2Pos -= 14;  /*Map Width*/
		}
	}

	// Apply Endless overclock with fractional carry while preserving layer 2 parallax.
	if (background_advance)
	{
		backPos2 += endlessScrollExtraPx2;
		while (backPos2 > 27)
		{
			backPos2 -= 28;
			mapY2--;
			mapY2Pos -= 14;
		}
	}
}

void draw_background_2_blend(SDL_Surface *surface)
{
	if (background_advance && map2YDelayMax > 1 && backMove2 < 2)
		backMove2 = (map2YDelay == 1) ? 1 : 0;
	
	const int tile_count = BG_TILE_COUNT;
	Uint8** map = (Uint8**)mapY2Pos + mapX2bpPos - tile_count;
	int mirror_w = 0, col0 = 0;
	map = bg_mirror_setup(map, (Uint8**)&megaData2.mainmap[0][0], 14, (int)mapX2bpPos - 1, &mirror_w, &col0);

	bg_update_scroll_delta(2, (int)mapY2, (int)backPos2);

	rl_current_id = RL_ID_BG_BASE + 2;
	bg_set_layer_dx(2, mapX2Ofs_f, mapX2Ofs);  // blend variant always draws at mapX2Pos
	for (int i = -1; i < 7; i++)
	{
		blit_background_row_blend(surface, mapX2Pos + PLAYFIELD_X_SHIFT, (i * 28) + backPos2, map, mirror_w, col0);

		map += 14;
	}
	// Extra bottom margin row(s) for the interpolation (see draw_background_1).
	Uint8 **const end = (Uint8 **)&megaData2.mainmap[0][0] + COUNTOF(megaData2.mainmap) * 14;
	for (int m = 0; m < bgMarginRows; ++m)
		blit_background_row_blend(surface, mapX2Pos + PLAYFIELD_X_SHIFT, ((7 + m) * 28) + backPos2,
		                          bg_margin_row(map, m, 14, end), mirror_w, col0);
	rl_current_id = 0;
	
	/*Set Movement of background*/
	if (background_advance && --map2YDelay == 0)
	{
		map2YDelay = map2YDelayMax;
		
		backPos2 += backMove2;
		
		if (backPos2 >  27)
		{
			backPos2 -= 28;
			mapY2--;
			mapY2Pos -= 14;  /*Map Width*/
		}
	}

	// Endless SMOOTH overclock: extra px to match layer 1 and the event pointer (see draw_background_2).
	if (background_advance)
	{
		backPos2 += endlessScrollExtraPx2;
		while (backPos2 > 27)
		{
			backPos2 -= 28;
			mapY2--;
			mapY2Pos -= 14;
		}
	}
}

void draw_background_3(SDL_Surface *surface)
{
	/* Movement of background */
	if (background_advance)
	{
		backPos3 += backMove3;

		if (backPos3 > 27)
		{
			backPos3 -= 28;
			mapY3--;
			mapY3Pos -= 15;   /*Map Width*/
		}

		// Endless SMOOTH overclock: extra px to match layer 1/2 and the event pointer (see above).
		backPos3 += endlessScrollExtraPx3;
		while (backPos3 > 27)
		{
			backPos3 -= 28;
			mapY3--;
			mapY3Pos -= 15;
		}
	}

	const int tile_count = BG_TILE_COUNT;
	Uint8** map = (Uint8**)mapY3Pos + mapX3bpPos - tile_count;
	// Layer 3 rows are 15 tiles wide and mapY3Pos already carries the -1 pointer bias,
	// so map[0] sits at column mapX3bpPos (no -1 like the 14-wide layers).
	int mirror_w = 0, col0 = 0;
	map = bg_mirror_setup(map, (Uint8**)&megaData3.mainmap[0][0], 15, (int)mapX3bpPos, &mirror_w, &col0);

	bg_update_scroll_delta(3, (int)mapY3, (int)backPos3);

	rl_current_id = RL_ID_BG_BASE + 3;
	// A welded layer pans from layer 1's recorded anchor, not its later live offset.
	const float x_anchor_f = (background3x1 && bg_layer_xofs_valid[1]) ? bg_layer_xofs[1] : mapX3Ofs_f;
	bg_set_layer_dx(3, x_anchor_f, mapX3Ofs);
	for (int i = -1; i < 7; i++)
	{
		// Layer 3 shares PLAYFIELD_X_SHIFT with layers 1/2; no per-layer correction.
		blit_background_row(surface, mapX3Pos + PLAYFIELD_X_SHIFT, (i * 28) + backPos3, map, mirror_w, col0);

		map += 15;
	}
	// Extra bottom margin row(s) for the interpolation (see draw_background_1; bg3 rows
	// are 15 map entries wide).
	Uint8 **const end = (Uint8 **)&megaData3.mainmap[0][0] + COUNTOF(megaData3.mainmap) * 15;
	for (int m = 0; m < bgMarginRows; ++m)
		blit_background_row(surface, mapX3Pos + PLAYFIELD_X_SHIFT, ((7 + m) * 28) + backPos3,
		                    bg_margin_row(map, m, 15, end), mirror_w, col0);
	rl_current_id = 0;
}

static void filter_hue_rows(Uint8 *s, int pitch, int width, int rows, Uint8 hue)
{
	const Swar8 hues = SWAR8(hue);

	for (; rows > 0; --rows, s += pitch)
	{
		int x = 0;
		for (; x + SWAR8_BYTES <= width; x += SWAR8_BYTES)
			swar8_write(s + x, hues | (swar8_read(s + x) & SWAR8(0x0F)));
		for (; x < width; ++x)
			s[x] = (Uint8)(hue | (s[x] & 0x0f));
	}
}

static void filter_value_rows(Uint8 *s, int pitch, int width, int rows, int brightness)
{
	if (brightness == 0)
		return;

	int mode;
	Swar8 addend = 0;
	if (brightness <= -0xF || brightness > 0x1E)
	{
		mode = 0;
	}
	else if (brightness < 0)
	{
		mode = 1;
		addend = SWAR8(brightness + 0x10);
	}
	else if (brightness <= 0xF)
	{
		mode = 2;
		addend = SWAR8(brightness);
	}
	else
	{
		mode = 3;
		addend = SWAR8(brightness - 0xF);
	}

	for (; rows > 0; --rows, s += pitch)
	{
		int x = 0;
		for (; x + SWAR8_BYTES <= width; x += SWAR8_BYTES)
		{
			const Swar8 px = swar8_read(s + x);
			Swar8 values = 0;
			if (mode != 0)
			{
				const Swar8 sum = (px & SWAR8(0x0F)) + addend;
				const Swar8 bit = sum & SWAR8(0x10);
				const Swar8 mask = bit - (bit >> 4);
				if (mode == 1)
					values = sum & mask;
				else if (mode == 2)
					values = (sum & SWAR8(0x0F)) | mask;
				else
					values = SWAR8(0x0F) & ~mask;
			}
			swar8_write(s + x, (px & SWAR8(0xF0)) | values);
		}
		for (; x < width; ++x)
		{
			const unsigned int temp = (s[x] & 0x0f) + brightness;
			s[x] = (Uint8)((s[x] & 0xf0) | (temp >= 0x1f ? 0 : (temp >= 0x0f ? 0x0f : temp)));
		}
	}
}

// Apply palette bank and brightness directly, without JE_filterScreen's fade side effects.
void JE_filterScreenApply(SDL_Surface *surface, JE_shortint col, JE_shortint int_)
{
	Uint8 *const s = (Uint8 *)surface->pixels + PLAYFIELD_LEFT;

	if (col != -99 && filtrationAvail)
		filter_hue_rows(s, surface->pitch, PLAYFIELD_WIDTH, 184, (Uint8)((unsigned int)col << 4));

	if (int_ != -99 && explosionTransparent)
		filter_value_rows(s, surface->pitch, PLAYFIELD_WIDTH, 184, int_);
}

// JE_filterScreenApply for an NxN supersampled surface: identical passes over the
// scaled playfield region.
void filter_screen_apply_scaled(SDL_Surface *surface, JE_shortint col, JE_shortint int_, int scale)
{
	Uint8 *const s = (Uint8 *)surface->pixels + PLAYFIELD_LEFT * scale;

	if (col != -99 && filtrationAvail)
		filter_hue_rows(s, surface->pitch, PLAYFIELD_WIDTH * scale, 184 * scale,
		                (Uint8)((unsigned int)col << 4));

	if (int_ != -99 && explosionTransparent)
		filter_value_rows(s, surface->pitch, PLAYFIELD_WIDTH * scale, 184 * scale, int_);
}

// Advance fade simulation independently of presentation and rollback re-simulation.
void JE_advanceLevelFade(void)
{
	if (!filterActive || !filterFade)
		return;

	levelBrightness += levelBrightnessChg;
	if ((filterFadeStart && levelBrightness < -14) || levelBrightness > 14)
	{
		levelBrightnessChg = -levelBrightnessChg;
		filterFadeStart = false;
		levelFilter = levelFilterNew;
	}
	if (!filterFadeStart && levelBrightness == 0)
	{
		filterFade = false;
		levelBrightness = -99;
	}
}

void JE_checkSmoothies(void)
{
	anySmoothies = (processorType > 2 && (smoothies[1-1] || smoothies[2-1])) || (processorType > 1 && (smoothies[3-1] || smoothies[4-1] || smoothies[5-1]));
}

static void lava_filter_px(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	/* Reads stay within the top 186 scanlines and the playfield width. */
	
	const int dst_pitch = dst->pitch;
	Uint8 *dst_pixel = (Uint8 *)dst->pixels + (185 * dst_pitch);
	const Uint8 * const dst_pixel_ll = (Uint8 *)dst->pixels;  // lower limit
	
	const int src_pitch = src->pitch;
	const Uint8 *src_pixel = (Uint8 *)src->pixels + (185 * src->pitch);
	const Uint8 * const src_pixel_ll = (Uint8 *)src->pixels;  // lower limit
	
	int w = vga_width * 185 - 1;
	
	for (int y = 185 - 1; y >= 0; --y)
	{
		dst_pixel -= (dst_pitch - vga_width);  // in case pitch differs
		src_pixel -= (src_pitch - vga_width);  // in case pitch differs

		for (int x = vga_width; x > 0; )
		{
			int waver = abs(((w >> 9) & 0x0f) - 8) - 1;
			w -= 8;

			int count = MIN(8, x);
			x -= count;

			for (int xi = 0; xi < count; ++xi)
			{
				--dst_pixel;
				--src_pixel;

				// Average the doubled source with the destination pixels above and below.
				// hue is red
				Uint8 value = 0;

				if (src_pixel + waver >= src_pixel_ll)
					value += (*(src_pixel + waver) & 0x0f) * 2;
				value += *(dst_pixel + waver + dst_pitch) & 0x0f;
				if (dst_pixel + waver - dst_pitch >= dst_pixel_ll)
					value += *(dst_pixel + waver - dst_pitch) & 0x0f;

				*dst_pixel = (value / 4) | 0x70;
			}
		}
	}
}

void lava_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	if (dst->pixels == src->pixels)
	{
		lava_filter_px(dst, src);
		return;
	}

	const int dst_pitch = dst->pitch;
	const int src_pitch = src->pitch;
	Uint8 *const dst_px = (Uint8 *)dst->pixels;
	const Uint8 *const src_px = (const Uint8 *)src->pixels;

	int w = vga_width * 185 - 1;

	for (int y = 185 - 1; y >= 0; --y)
	{
		Uint8 *const drow = dst_px + y * dst_pitch;
		const Uint8 *const srow = src_px + y * src_pitch;

		for (int x = vga_width; x > 0; )
		{
			const int waver = abs(((w >> 9) & 0x0f) - 8) - 1;
			w -= 8;

			const int count = MIN(8, x);
			x -= count;

			if (count == 8 && y >= 2)
			{
				const Uint8 *const s = srow + x + waver;
				const Uint8 *const below = drow + dst_pitch + x + waver;
				const Uint8 *const above = drow - dst_pitch + x + waver;
				for (int i = 0; i < 8; i += SWAR8_BYTES)
				{
					Swar8 v = (swar8_read(s + i) & SWAR8(0x0F)) << 1;
					v += swar8_read(below + i) & SWAR8(0x0F);
					v += swar8_read(above + i) & SWAR8(0x0F);
					swar8_write(drow + x + i, ((v >> 2) & SWAR8(0x0F)) | SWAR8(0x70));
				}
				continue;
			}

			for (int xi = x + count - 1; xi >= x; --xi)
			{
				Uint8 *const dp = drow + xi;
				const Uint8 *const sp = srow + xi;
				Uint8 value = 0;

				if (sp + waver >= src_px)
					value += (*(sp + waver) & 0x0f) * 2;
				value += *(dp + waver + dst_pitch) & 0x0f;
				if (dp + waver - dst_pitch >= dst_px)
					value += *(dp + waver - dst_pitch) & 0x0f;

				*dp = (value / 4) | 0x70;
			}
		}
	}
}

void water_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	Uint8 hue = smoothie_data[1] << 4;
	const Swar8 hues = SWAR8(hue);

	/* Reads stay within the top 186 scanlines and the playfield width. */

	const int dst_pitch = dst->pitch;
	Uint8 *const dst_px = (Uint8 *)dst->pixels;
	const Uint8 *const src_px = (const Uint8 *)src->pixels;

	int w = vga_width * 185 - 1;

	for (int y = 185 - 1; y >= 0; --y)
	{
		Uint8 *const drow = dst_px + y * dst_pitch;
		const Uint8 *const srow = src_px + y * src->pitch;

		for (int x = vga_width; x > 0; )
		{
			const int waver = abs(((w >> 10) & 0x07) - 4) - 1;
			w -= 8;

			const int count = MIN(8, x);
			x -= count;

			// pixel is copied from source if not blue
			// otherwise, value is average of value of source pixel and destination pixel below (with waver)
			if (count == 8)
			{
				const Uint8 *const below = drow + dst_pitch + x + waver;
				for (int i = 0; i < 8; i += SWAR8_BYTES)
				{
					const Swar8 s = swar8_read(srow + x + i);
					const Swar8 water = hues | swar8_avg_low(s, swar8_read(below + i));
					const Swar8 blue = swar8_nonzero(s & SWAR8(0x30));
					swar8_write(drow + x + i, swar8_select(s, water, blue));
				}
				continue;
			}

			for (int xi = x + count - 1; xi >= x; --xi)
			{
				const Uint8 s = srow[xi];
				if ((s & 0x30) == 0)
				{
					drow[xi] = s;
				}
				else
				{
					Uint8 value = s & 0x0f;
					value += drow[xi + waver + dst_pitch] & 0x0f;
					drow[xi] = (value / 2) | hue;
				}
			}
		}
	}
}

static void average_rows(Uint8 *dst, int dst_pitch, const Uint8 *src, int src_pitch,
                         int width, int rows, int hue)
{
	const Swar8 hues = SWAR8(hue < 0 ? 0 : hue);

	for (; rows > 0; --rows, dst += dst_pitch, src += src_pitch)
	{
		int x = 0;
		for (; x + SWAR8_BYTES <= width; x += SWAR8_BYTES)
		{
			const Swar8 s = swar8_read(src + x);
			const Swar8 h = hue < 0 ? (s & SWAR8(0xF0)) : hues;
			swar8_write(dst + x, h | swar8_avg_low(s, swar8_read(dst + x)));
		}
		for (; x < width; ++x)
		{
			const Uint8 value = (src[x] & 0x0f) + (dst[x] & 0x0f);
			dst[x] = (Uint8)((value / 2) | (hue < 0 ? (src[x] & 0xf0) : hue));
		}
	}
}

void iced_blur_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	// value is average value of source pixel and destination pixel
	// hue is icy blue
	average_rows(dst->pixels, dst->pitch, src->pixels, src->pitch, vga_width, 184, 0x80);
}

void blur_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	// value is average value of source pixel and destination pixel
	// hue is source pixel hue
	average_rows(dst->pixels, dst->pitch, src->pixels, src->pitch, vga_width, 184, -1);
}

static void lava_filter_scaled_px(SDL_Surface *dst, SDL_Surface *src, int scale)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	const int W = vga_width * scale;
	const int dst_pitch = dst->pitch;
	const int src_pitch = src->pitch;
	const int row_step = dst_pitch * scale;  // one 1x row's distance in the hi buffer
	Uint8 *const dst_px = (Uint8 *)dst->pixels;
	const Uint8 *const src_px = (const Uint8 *)src->pixels;

	for (int y1 = 184; y1 >= 0; --y1)
	{
		const int row1 = y1 * vga_width;
		for (int sub_y = scale - 1; sub_y >= 0; --sub_y)
		{
			const int y = y1 * scale + sub_y;
			Uint8 *const dp = dst_px + y * dst_pitch;
			const Uint8 *const sp = src_px + y * src_pitch;

			for (int x1 = vga_width - 1; x1 >= 0; --x1)
			{
				// Every sub-pixel in a source-sized cell shares its waver.
				const int w1 = row1 + x1;
				const int waver = (abs(((w1 >> 9) & 0x0f) - 8) - 1) * scale;

				for (int sub_x = scale - 1; sub_x >= 0; --sub_x)
				{
					const int x = x1 * scale + sub_x;
					int xs = x + waver;
					if (xs < 0)
						xs = 0;
					else if (xs >= W)
						xs = W - 1;

					int value = (sp[xs] & 0x0f) * 2;
					value += dp[xs + row_step] & 0x0f;
					if (y >= scale)
						value += dp[xs - row_step] & 0x0f;

					dp[x] = (Uint8)((value / 4) | 0x70);
				}
			}
		}
	}
}

static inline void lava_span_scaled(Uint8 *dp, const Uint8 *sp, int row_step, bool above, int W,
                                    int waver, int x0, int x1)
{
	int ia = MAX(x0, -waver), ib = MIN(x1, W - waver);
	if (ib < ia)
		ia = ib = x1;

	for (int x = x0; x < x1; )
	{
		if (x >= ia && x + SWAR8_BYTES <= ib)
		{
			const Uint8 *const s = sp + x + waver;
			Swar8 v = (swar8_read(s) & SWAR8(0x0F)) << 1;
			v += swar8_read(dp + x + waver + row_step) & SWAR8(0x0F);
			if (above)
				v += swar8_read(dp + x + waver - row_step) & SWAR8(0x0F);
			swar8_write(dp + x, ((v >> 2) & SWAR8(0x0F)) | SWAR8(0x70));
			x += SWAR8_BYTES;
			continue;
		}

		int xs = x + waver;
		if (xs < 0)
			xs = 0;
		else if (xs >= W)
			xs = W - 1;

		int value = (sp[xs] & 0x0f) * 2;
		value += dp[xs + row_step] & 0x0f;
		if (above)
			value += dp[xs - row_step] & 0x0f;

		dp[x] = (Uint8)((value / 4) | 0x70);
		++x;
	}
}

/* Supersampled smoothie filters preserve the 1x math and scan order on an NxN
 * buffer. See backgrnd.h for spatial scaling. */
void lava_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	if (dst->pixels == src->pixels)
	{
		lava_filter_scaled_px(dst, src, scale);
		return;
	}

	const int W = vga_width * scale;
	const int dst_pitch = dst->pitch;
	const int src_pitch = src->pitch;
	const int row_step = dst_pitch * scale;
	Uint8 *const dst_px = (Uint8 *)dst->pixels;
	const Uint8 *const src_px = (const Uint8 *)src->pixels;

	for (int y1 = 184; y1 >= 0; --y1)
	{
		const int row1 = y1 * vga_width;
		for (int sub_y = scale - 1; sub_y >= 0; --sub_y)
		{
			const int y = y1 * scale + sub_y;
			Uint8 *const dp = dst_px + y * dst_pitch;
			const Uint8 *const sp = src_px + y * src_pitch;

			for (int x1_end = vga_width; x1_end > 0; )
			{
				const int w_last = row1 + x1_end - 1;
				int x1_start = (w_last & ~511) - row1;
				if (x1_start < 0)
					x1_start = 0;

				const int waver = (abs(((w_last >> 9) & 0x0f) - 8) - 1) * scale;
				lava_span_scaled(dp, sp, row_step, y >= scale, W, waver,
				                 x1_start * scale, x1_end * scale);
				x1_end = x1_start;
			}
		}
	}
}

static inline void water_span_scaled(Uint8 *dp, const Uint8 *sp, int row_step, Uint8 hue, int W,
                                     int waver, int x0, int x1)
{
	const Swar8 hues = SWAR8(hue);

	int ia = MAX(x0, -waver), ib = MIN(x1, W - waver);
	if (ib < ia)
		ia = ib = x1;

	for (int x = x0; x < x1; )
	{
		if (x >= ia && x + SWAR8_BYTES <= ib)
		{
			const Swar8 s = swar8_read(sp + x);
			const Swar8 water = hues | swar8_avg_low(s, swar8_read(dp + x + waver + row_step));
			const Swar8 blue = swar8_nonzero(s & SWAR8(0x30));
			swar8_write(dp + x, swar8_select(s, water, blue));
			x += SWAR8_BYTES;
			continue;
		}

		const Uint8 s = sp[x];
		if ((s & 0x30) == 0)
		{
			dp[x] = s;
		}
		else
		{
			int xs = x + waver;
			if (xs < 0)
				xs = 0;
			else if (xs >= W)
				xs = W - 1;

			Uint8 value = s & 0x0f;
			value += dp[xs + row_step] & 0x0f;
			dp[x] = (Uint8)((value / 2) | hue);
		}
		++x;
	}
}

void water_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	const Uint8 hue = smoothie_data[1] << 4;

	const int W = vga_width * scale;
	const int dst_pitch = dst->pitch;
	const int src_pitch = src->pitch;
	const int row_step = dst_pitch * scale;  // one 1x row's distance in the hi buffer
	Uint8 *const dst_px = (Uint8 *)dst->pixels;
	const Uint8 *const src_px = (const Uint8 *)src->pixels;

	for (int y1 = 184; y1 >= 0; --y1)
	{
		const int row1 = y1 * vga_width;
		for (int sub_y = scale - 1; sub_y >= 0; --sub_y)
		{
			const int y = y1 * scale + sub_y;
			Uint8 *const dp = dst_px + y * dst_pitch;
			const Uint8 *const sp = src_px + y * src_pitch;

			for (int x1_end = vga_width; x1_end > 0; )
			{
				const int w_last = row1 + x1_end - 1;
				int x1_start = (w_last & ~1023) - row1;
				if (x1_start < 0)
					x1_start = 0;

				const int waver = (abs(((w_last >> 10) & 0x07) - 4) - 1) * scale;
				water_span_scaled(dp, sp, row_step, hue, W, waver,
				                  x1_start * scale, x1_end * scale);
				x1_end = x1_start;
			}
		}
	}
}

void iced_blur_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	// Average of source and previous-frame destination; hue icy blue.
	average_rows(dst->pixels, dst->pitch, src->pixels, src->pitch,
	             vga_width * scale, 184 * scale, 0x80);
}

void blur_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);

	// Average of source and previous-frame destination; source hue kept.
	average_rows(dst->pixels, dst->pitch, src->pixels, src->pitch,
	             vga_width * scale, 184 * scale, -1);
}

static void spotlight_dark(Uint8 *dst, const Uint8 *src, int n)
{
	int i = 0;
	for (; i + SWAR8_BYTES <= n; i += SWAR8_BYTES)
	{
		const Swar8 v = swar8_read(src + i);
		swar8_write(dst + i, (v & SWAR8(0xF0)) | ((v >> 2) & SWAR8(0x03)));
	}
	for (; i < n; ++i)
		dst[i] = (Uint8)((src[i] & 0xf0) | ((src[i] & 0x0f) >> 2));
}

void spotlight_filter_row(Uint8 *dst, const Uint8 *src, int width, int y, int lightx, int lighty,
                          int band, int scale)
{
	if (lighty > y)
	{
		spotlight_dark(dst, src, width);
		return;
	}

	const int reach = y - lighty;
	int xa = lightx + reach + band;
	int xb = lightx - reach - band;
	if (xa > width)
		xa = width;
	if (xb < 1)
		xb = 1;
	if (xa < xb)
	{
		spotlight_dark(dst, src, width);
		return;
	}

	const int ca = width - xa;
	const int cb = width - xb;
	spotlight_dark(dst, src, ca);

	for (int c = ca; c <= cb; )
	{
		const int x = width - c;
		const int lightdist = abs(lightx - x) + lighty;
		if (lightdist < y)
		{
			int lit_end = width - (lightx - reach + 1);
			if (lit_end > cb)
				lit_end = cb;
			memcpy(dst + c, src + c, (size_t)(lit_end - c + 1));
			c = lit_end + 1;
			continue;
		}

		if (lightdist - y <= band)
		{
			const int light = (3 * (band - (lightdist - y))) / scale;
			dst[c] = (Uint8)((src[c] & 0xf0) | (((src[c] & 0x0f) + light) / 4));
		}
		else
			dst[c] = (Uint8)((src[c] & 0xf0) | ((src[c] & 0x0f) >> 2));
		++c;
	}

	spotlight_dark(dst + cb + 1, src + cb + 1, width - cb - 1);
}

/* Background Starfield. Each star is an (x column, float y row) point; only y is
 * advanced and interpolated. Fixed x coordinates prevent horizontal smearing. */
typedef struct
{
	int x;        // column (constant for a star's lifetime)
	float y;      // row (fractional, advances each tick, wraps at STARFIELD_WRAP)
	int speed;    // base rows of drift (scaled by STARFIELD_SPEED_SCALE)
	Uint8 color;
} StarfieldStar;

#define MAX_STARS 330  // sized so on-screen density stays constant with stars respawning above the top edge
#define STARFIELD_HUE 0x90
#define STARFIELD_WRAP    184  // rows; star recycles once it drifts past this (the playfield bottom edge)
#define STARFIELD_VISIBLE 184  // rows; stars draw above this. Matches the 184-row playfield so they
                               // fill it to the bottom (a shorter value leaves a black stripe on space levels).

// Recycle stars through a rotating band above the screen to avoid visible pop-in.
#define STARFIELD_SPAWN_MIN    4
#define STARFIELD_SPAWN_SPREAD 32

// rows/tick per unit of (speed + move_speed); 1.0 == the original game's speed.
#define STARFIELD_SPEED_SCALE 1.0f
static StarfieldStar starfield_stars[MAX_STARS];
int starfield_speed;
// Rotate respawn height without touching the gameplay RNG stream.
static int starfield_spawn_phase;

void initialize_starfield(void)
{
	starfield_spawn_phase = 0;
	for (int i = MAX_STARS - 1; i >= 0; --i)
	{
		starfield_stars[i].x = mt_rand() % vga_width;
		starfield_stars[i].y = (float)(mt_rand() % STARFIELD_WRAP);
		starfield_stars[i].speed = mt_rand() % 3 + 2;
		starfield_stars[i].color = mt_rand() % 16 + STARFIELD_HUE;
	}
}

// Draw one star (center pixel plus dimmer halo); factored out for render-list replay.
// Bounds are checked per axis so halo pixels can't wrap into the neighbouring row.
void draw_starfield_star(SDL_Surface* surface, int x, int y, Uint8 color)
{
	if (x < 0 || x >= surface->w || y < 0 || y >= STARFIELD_VISIBLE)
		return;

	Uint8* p = (Uint8*)surface->pixels;
	const int pitch = surface->pitch;
	const int pos = y * pitch + x;

	if (p[pos] == 0)
		p[pos] = color;

	// If star is bright enough, draw surrounding pixels
	if (color - 4 >= STARFIELD_HUE)
	{
		if (x + 1 < surface->w && p[pos + 1] == 0)
			p[pos + 1] = color - 4;
		if (x - 1 >= 0 && p[pos - 1] == 0)
			p[pos - 1] = color - 4;
		if (y + 1 < surface->h && p[pos + pitch] == 0)
			p[pos + pitch] = color - 4;
		if (y - 1 >= 0 && p[pos - pitch] == 0)
			p[pos - pitch] = color - 4;
	}
}

// One scale x scale block of star light: written only over black pixels, like the
// per-pixel check in draw_starfield_star.
static void star_block(SDL_Surface *surface, int x, int y, Uint8 color, int scale)
{
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + scale, y1 = y + scale;
	if (x1 > surface->w)
		x1 = surface->w;
	if (y1 > surface->h)
		y1 = surface->h;

	for (int yy = y0; yy < y1; ++yy)
	{
		Uint8 *p = (Uint8 *)surface->pixels + yy * surface->pitch + x0;
		for (int xx = x0; xx < x1; ++xx, ++p)
		{
			if (*p == 0)
				*p = color;
		}
	}
}

void draw_starfield_star_scaled(SDL_Surface* surface, int x, int y, Uint8 color, int scale)
{
	// Same visibility rule as the 1x draw, in HI coordinates. The interpolated row lands on the
	// 1/scale-pixel grid, so slow stars glide.
	if (x < 0 || x >= surface->w || y < 0 || y >= STARFIELD_VISIBLE * scale)
		return;

	star_block(surface, x, y, color, scale);

	// If star is bright enough, draw the surrounding halo blocks
	if (color - 4 >= STARFIELD_HUE)
	{
		star_block(surface, x + scale, y, color - 4, scale);
		star_block(surface, x - scale, y, color - 4, scale);
		star_block(surface, x, y + scale, color - 4, scale);
		star_block(surface, x, y - scale, color - 4, scale);
	}
}

// Optional inclusive bounds for star centers, in surface pixels.
static bool starfield_clipped = false;
static int starfield_clip[4];

void starfield_set_clip(int x1, int y1, int x2, int y2)
{
	starfield_clip[0] = x1;
	starfield_clip[1] = y1;
	starfield_clip[2] = x2;
	starfield_clip[3] = y2;
	starfield_clipped = true;
}

void starfield_clear_clip(void)
{
	starfield_clipped = false;
}

static bool starfield_star_visible(int x, int y)
{
	return !starfield_clipped
	    || (x >= starfield_clip[0] && x <= starfield_clip[2]
	     && y >= starfield_clip[1] && y <= starfield_clip[3]);
}

void update_and_draw_starfield(SDL_Surface* surface, int move_speed)
{
	for (int i = MAX_STARS-1; i >= 0; --i)
	{
		StarfieldStar* star = &starfield_stars[i];

		// Drift down by a (usually sub-pixel) amount; only y moves.
		const float dy = (star->speed + move_speed) * STARFIELD_SPEED_SCALE;
		star->y += dy;

		// Record this tick's row motion for interpolation; on a wrap pass 0 so the
		// replay snaps to the new position instead of streaking across the screen.
		float rec_dy = dy;
		if (star->y >= STARFIELD_WRAP)
		{
			// Respawn above the top edge so interpolation slides the star into view
			// instead of showing it frozen at row zero for a tick.
			star->y = -(float)(STARFIELD_SPAWN_MIN + (starfield_spawn_phase % STARFIELD_SPAWN_SPREAD));
			starfield_spawn_phase += 13;  // step coprime with SPREAD -> even coverage of the band
			rec_dy = 0.0f;                // snap on the wrap; the star is off-screen so nothing streaks
		}

		if (starfield_star_visible(star->x, (int)(star->y + 0.5f)))
			draw_starfield_star(surface, star->x, (int)(star->y + 0.5f), star->color);

		if (render_list_recording)
			rl_rec_star(star->x, star->y, rec_dy, star->color);
	}
}

/* Register private starfield simulation state. Public scroll state is registered
 * centrally in rollback_state.c. */
#include "rollback.h"

void backgrnd_register_rollback(void)
{
	rollback_register("bg.starfieldStars", starfield_stars, sizeof(starfield_stars));
	rollback_register("bg.starfieldPhase", &starfield_spawn_phase, sizeof(starfield_spawn_phase));
	/* Cross-tick scroll-delta trackers: they feed bg_layer_dy and, through the
	 * draw pass, the per-enemy scroll stamps; a replayed tick must see them. */
	rollback_register("bg.layerOfsPrev",   bg_layer_ofs_prev, sizeof(bg_layer_ofs_prev));
	rollback_register("bg.prevMapY",       bgPrevMapY, sizeof(bgPrevMapY));
	rollback_register("bg.prevBackPos",    bgPrevBackPos, sizeof(bgPrevBackPos));
	rollback_register("bg.prevValid",      bgPrevValid, sizeof(bgPrevValid));
}
