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
#ifndef BACKGRND_H
#define BACKGRND_H

#include "opentyr.h"

#include "SDL.h"

#include <stdint.h>

extern JE_word backPos, backPos2, backPos3;
extern JE_word backMove, backMove2, backMove3;
// Per-layer Endless scroll boost for this tick, including fractional carry.
extern int endlessScrollExtraPx1, endlessScrollExtraPx2, endlessScrollExtraPx3;

// TRUE per-tick vertical scroll (px) of each background layer [1..3], computed at draw time in
// backgrnd.c/tyrian2.c.
extern int bgScrollDeltaY[4];

// Offscreen rows retained below each background layer. Scroll modifiers widen the
// stable margin so interpolation cannot expose a black strip or change row count.
extern int bgMarginRows;

// Unfloored offsets and fractional deltas keep parallax aligned with anchored enemies.
extern float mapXOfs_f, mapX2Ofs_f, mapX3Ofs_f;
extern float oldMapXOfs_f, oldMapX3Ofs_f;  // un-floored mirrors of oldMapXOfs / oldMapX3Ofs
extern float bg_layer_dx[4], bg_layer_frac[4];

// Recorded horizontal anchors normalize bound entities across the mid-frame parallax update.
extern float bg_layer_xofs[4];
extern bool bg_layer_xofs_valid[4];

// Vertical scroll smoothing: bg_layer_dy (FLOAT average scroll rate) + bg_layer_yfrac (sub-pixel
// remainder) per layer, gated by bg_smooth_y_active.
extern float bg_layer_dy[4], bg_layer_yfrac[4];
extern bool bg_smooth_y_active;
// Current-tick scroll state for layer 3, which advances before recording its rows.
// Enemy banks use the lagged state above to retain their authored phase.
extern float bg_layer_yfrac_now[4], bg_layer_dy_now[4];

extern JE_word mapX, mapY, mapX2, mapX3, mapY2, mapY3;
extern JE_byte **mapYPos, **mapY2Pos, **mapY3Pos;
extern JE_integer mapXPos, oldMapXOfs, mapXOfs, mapX2Ofs, mapX2Pos, mapX3Pos, oldMapX3Ofs, mapX3Ofs, tempMapXOfs;
extern intptr_t mapXbpPos, mapX2bpPos, mapX3bpPos;
extern JE_byte map1YDelay, map1YDelayMax, map2YDelay, map2YDelayMax;
extern JE_boolean anySmoothies;  // any special background filter is active
extern JE_byte smoothie_data[9];

extern int starfield_speed;

// When false, draw_background_* render at the current scroll position without
// advancing it (interpolated re-draws between ticks); the sim tick leaves it true.
extern bool background_advance;

void JE_darkenBackground(JE_word neat);

// Edge mirroring reflects columns outside the map row; mirror_w 0 keeps stock reads.
void blit_background_row(SDL_Surface *surface, int x, int y, Uint8 **map, int mirror_w, int col0);
void blit_background_row_blend(SDL_Surface *surface, int x, int y, Uint8 **map, int mirror_w, int col0);
// Supersampled variant (render-list replay only): x,y are HI-buffer coordinates;
// each tile pixel is drawn as a scale x scale block, fully clipped, never recorded.
void blit_background_row_scaled(SDL_Surface *surface, int x, int y, Uint8 **map, int scale, bool blend, int mirror_w, int col0);

// Update layer 1's scroll anchor even when Astral Zone skips drawing the layer.
void bg_publish_layer_1_phase(void);

void draw_background_1(SDL_Surface *surface);
void draw_background_2(SDL_Surface *surface);
void draw_background_2_blend(SDL_Surface *surface);
void draw_background_3(SDL_Surface *surface);

// Advances the fade/flash ramp by one tick: sim state, call from the tick body only.
void JE_advanceLevelFade(void);
void JE_filterScreenApply(SDL_Surface *surface, JE_shortint col, JE_shortint generic_int);
// JE_filterScreenApply on an NxN supersampled surface (playfield region x scale).
void filter_screen_apply_scaled(SDL_Surface *surface, JE_shortint col, JE_shortint generic_int, int scale);

void JE_checkSmoothies(void);
void lava_filter(SDL_Surface *dst, SDL_Surface *src);
void water_filter(SDL_Surface *dst, SDL_Surface *src);
void iced_blur_filter(SDL_Surface *dst, SDL_Surface *src);
void blur_filter(SDL_Surface *dst, SDL_Surface *src);
/*smoothies #5 is used for 3*/
/*smoothies #9 is a vertical flip*/

// Supersampled smoothie filters retain original-scale offsets on NxN buffers.
// Their feedback remains contractive toward the source.
void lava_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale);
void water_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale);
void iced_blur_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale);
void blur_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale);

void initialize_starfield(void);
/* Limit star centers to inclusive surface bounds. Halos may extend one pixel past them. */
void starfield_set_clip(int x1, int y1, int x2, int y2);
void starfield_clear_clip(void);
void update_and_draw_starfield(SDL_Surface* surface, int move_speed);
void draw_starfield_star(SDL_Surface* surface, int x, int y, Uint8 color);
// Star at HI-buffer coordinates: centre + halo drawn as scale x scale blocks.
void draw_starfield_star_scaled(SDL_Surface* surface, int x, int y, Uint8 color, int scale);

#endif /* BACKGRND_H */
