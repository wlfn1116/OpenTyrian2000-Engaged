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
#include "video_crt.h"

#include "opentyr.h"
#include "video.h"

#include "SDL.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* CRT filter. Every layer multiplies or adds over the frame on the GPU; the textures belong to
 * main_window_renderer and are built on first use. The vignette follows crtview's crtemu.h. */
int crt_filter = CRT_FILTER_OFF;
int crt_strength = CRT_STRENGTH_MEDIUM;
int crt_curvature = CRT_CURVATURE_OFF;
int crt_mask = CRT_MASK_GRILLE;
int crt_corners = CRT_CORNERS_SQUARE;
int crt_rolling_bar = CRT_ROLLING_BAR_OFF;

#define CRT_GLOW_LEVELS   8     // halvings at most, enough for an 8K frame
#define CRT_VIGNETTE_W    64
#define CRT_VIGNETTE_H    36
#define CRT_GAP_SHARE     0.18f
#define CRT_RAMP_SHARE    0.15f
#define CRT_LINE_MIN_WEIGHT         26
#define CRT_PHOSPHORS_PER_GAME_PX   5
#define CRT_CURVE_MIN_PX_PER_GAME_ROW 5
#define CRT_CURVE_CELLS_X 64
#define CRT_CURVE_CELLS_Y 36
#define CRT_SLOT_ROWS           4
#define CRT_SLOT_GAP_ROWS       1
#define CRT_MASK_TILE_ROWS      64
#define CRT_CORNER_PX           64
#define CRT_CORNER_RADIUS_SHARE 0.04f
#define CRT_BAR_ROWS            64
#define CRT_BAR_HEIGHT_SHARE    0.2f
#define CRT_BAR_CROSS_MS        3000
#define CRT_BAR_PERIOD_MS       9000

typedef struct
{
	float gap;
	float fill;
	float glow;
	float glow_spread;
	float mask_lit;
	float mask_unlit;
} CrtLook;

static const CrtLook crt_looks[CRT_STRENGTH_COUNT] = {
	[CRT_STRENGTH_LIGHT]  = { .gap = 0.62f, .fill = 0.85f, .glow = 0.115f, .glow_spread = 0.10f,
	                          .mask_lit = 1.2f, .mask_unlit = 0.9f },
	[CRT_STRENGTH_MEDIUM] = { .gap = 0.45f, .fill = 0.80f, .glow = 0.24f, .glow_spread = 0.18f,
	                          .mask_lit = 1.4f, .mask_unlit = 0.8f },
	[CRT_STRENGTH_STRONG] = { .gap = 0.28f, .fill = 0.70f, .glow = 0.41f, .glow_spread = 0.26f,
	                          .mask_lit = 1.6f, .mask_unlit = 0.7f },
};

static const SDL_FPoint crt_bends[CRT_CURVATURE_COUNT] = {
	[CRT_CURVATURE_OFF]    = { 0.0f, 0.0f },
	[CRT_CURVATURE_SUBTLE] = { 1.0f / 32.0f, 1.0f / 24.0f },
	[CRT_CURVATURE_STRONG] = { 1.0f / 16.0f, 1.0f / 12.0f },
};

static const float crt_bar_depths[CRT_ROLLING_BAR_COUNT] = {
	[CRT_ROLLING_BAR_OFF]    = 0.0f,
	[CRT_ROLLING_BAR_SUBTLE] = 0.06f,
	[CRT_ROLLING_BAR_STRONG] = 0.12f,
};

typedef struct
{
	SDL_Rect dst;
	int frame_h;
	int strength;
	int scale;
} CrtLinesKey;

static bool crt_ready = false;   // the textures below exist for this renderer
static bool crt_broken = false;  // this renderer cannot draw them
static bool crt_glow_broken = false;
static bool crt_lines_unsupported = false;
static bool crt_strip_broken = false;
static SDL_Texture *crt_scanlines = NULL;
static SDL_Texture *crt_vignette = NULL;
static SDL_Texture *crt_mask_tex = NULL;   // 1 texel per output column
static int crt_mask_w = 0, crt_mask_strength = 0, crt_mask_kind = CRT_MASK_OFF, crt_mask_rows = 0;
static SDL_Texture *crt_corner_tex = NULL;
static SDL_Texture *crt_bar_tex = NULL;
static bool crt_corner_broken = false, crt_bar_broken = false;
static bool crt_bar_shown = false;
static SDL_Texture *crt_glow[CRT_GLOW_LEVELS];  // successive halvings of the frame
static int crt_glow_levels = 0, crt_glow_src_w = 0, crt_glow_src_h = 0;
static SDL_Texture *crt_glow_full = NULL, *crt_glow_half = NULL, *crt_glow_quarter = NULL;
static bool crt_curve_broken = false;
static int crt_curve_failed_w = 0, crt_curve_failed_h = 0;
static SDL_Texture *crt_curve_target = NULL;
static SDL_FPoint crt_curve_shown = { 0.0f, 0.0f };
#if SDL_VERSION_ATLEAST(2, 0, 18)
static SDL_Vertex crt_curve_vertices[(CRT_CURVE_CELLS_X + 1) * (CRT_CURVE_CELLS_Y + 1)];
static int crt_curve_indices[6 * CRT_CURVE_CELLS_X * CRT_CURVE_CELLS_Y];
static SDL_Rect crt_curve_mesh_dst;
static SDL_FPoint crt_curve_mesh_bend;
#endif

static CrtLinesKey crt_lines_key;
static bool crt_lines_valid = false;
static bool crt_lines_shown = false;
static Uint8 *crt_line_weight = NULL;
#if SDL_VERSION_ATLEAST(2, 0, 18)
static SDL_Vertex *crt_line_vertices = NULL;
static int *crt_line_indices = NULL;
static int crt_line_quads = 0;
#endif

static Uint32 crt_grey(int v)
{
	return 0xFF000000u | (Uint32)v * 0x010101u;
}

static SDL_Texture *crt_texture(int w, int h, const Uint32 *argb, SDL_BlendMode blend,
                                SDL_ScaleMode scale)
{
	SDL_Texture *t = SDL_CreateTexture(main_window_renderer, SDL_PIXELFORMAT_ARGB8888,
	                                   SDL_TEXTUREACCESS_STATIC, w, h);
	if (t == NULL)
		return NULL;

	if (SDL_UpdateTexture(t, NULL, argb, w * (int)sizeof(*argb)) != 0 ||
	    SDL_SetTextureBlendMode(t, blend) != 0 || SDL_SetTextureScaleMode(t, scale) != 0)
	{
		SDL_DestroyTexture(t);
		return NULL;
	}
	return t;
}

static SDL_Texture *crt_target(int w, int h)
{
	SDL_Texture *const t = SDL_CreateTexture(main_window_renderer, SDL_PIXELFORMAT_ARGB8888,
	                                         SDL_TEXTUREACCESS_TARGET, w, h);
	if (t != NULL)
		SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
	return t;
}

static void crt_destroy(SDL_Texture **t)
{
	if (*t != NULL)
		SDL_DestroyTexture(*t);
	*t = NULL;
}

static void crt_release_glow(void)
{
	for (int i = 0; i < crt_glow_levels; ++i)
		SDL_DestroyTexture(crt_glow[i]);
	crt_glow_levels = 0;
	crt_glow_src_w = crt_glow_src_h = 0;
}

void crt_release(void)
{
	crt_destroy(&crt_scanlines);
	crt_destroy(&crt_vignette);
	crt_destroy(&crt_mask_tex);
	crt_mask_w = 0;
	crt_destroy(&crt_corner_tex);
	crt_destroy(&crt_bar_tex);
	crt_release_glow();
	crt_destroy(&crt_glow_full);
	crt_destroy(&crt_glow_half);
	crt_destroy(&crt_glow_quarter);
	crt_destroy(&crt_curve_target);
	crt_curve_shown = crt_bends[CRT_CURVATURE_OFF];

	SDL_free(crt_line_weight);
	crt_line_weight = NULL;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	SDL_free(crt_line_vertices);
	SDL_free(crt_line_indices);
	crt_line_vertices = NULL;
	crt_line_indices = NULL;
	crt_line_quads = 0;
#endif
	crt_lines_valid = crt_lines_shown = false;
	crt_ready = crt_broken = crt_glow_broken = crt_lines_unsupported = crt_curve_broken = false;
	crt_strip_broken = crt_corner_broken = crt_bar_broken = crt_bar_shown = false;
	crt_curve_failed_w = crt_curve_failed_h = 0;
}

static bool crt_build(void)
{
	if (crt_ready || crt_broken || main_window_renderer == NULL)
		return crt_ready;

	// A CPU renderer would blend every output pixel several times per frame.
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(main_window_renderer, &info) != 0 ||
	    (info.flags & SDL_RENDERER_SOFTWARE))
	{
		crt_broken = true;
		return false;
	}

	// crtemu's 16xy(1-x)(1-y) falloff, flattened so only the edges darken.
	Uint32 vignette[CRT_VIGNETTE_W * CRT_VIGNETTE_H];
	for (int y = 0; y < CRT_VIGNETTE_H; ++y)
	{
		for (int x = 0; x < CRT_VIGNETTE_W; ++x)
		{
			const float u = (x + 0.5f) / CRT_VIGNETTE_W, v = (y + 0.5f) / CRT_VIGNETTE_H;
			const float falloff = powf(16.0f * u * v * (1.0f - u) * (1.0f - v), 0.25f);
			vignette[y * CRT_VIGNETTE_W + x] = crt_grey((int)(255.0f * (0.6f + 0.4f * falloff)));
		}
	}
	crt_vignette = crt_texture(CRT_VIGNETTE_W, CRT_VIGNETTE_H, vignette, SDL_BLENDMODE_MOD,
	                           SDL_ScaleModeLinear);

	if (crt_vignette == NULL)
	{
		crt_release();
		crt_broken = true;
		return false;
	}

	crt_ready = true;
	return true;
}

bool crt_filter_available(void)
{
	return crt_build();
}

static int crt_game_row(int y, int dst_h, int frame_h)
{
	const Sint64 texel = ((Sint64)2 * y + 1) * frame_h / ((Sint64)2 * dst_h);
	return (int)(texel * vga_height / frame_h);
}

static bool crt_line_weights(Uint8 *weight, int dst_h, int frame_h)
{
	if (dst_h < 2 * vga_height || frame_h <= 0)
		return false;

	const float row_px = (float)dst_h / vga_height;
	int gap_px = (int)(row_px * CRT_GAP_SHARE + 0.5f);
	if (gap_px < 1)
		gap_px = 1;
	const float gap_mid = 0.5f * gap_px;
	const float ramp_px = row_px * CRT_RAMP_SHARE;

	for (int top = 0; top < dst_h; )
	{
		const int row = crt_game_row(top, dst_h, frame_h);
		int end = top + 1;
		while (end < dst_h && crt_game_row(end, dst_h, frame_h) == row)
			++end;

		for (int y = top; y < end; ++y)
		{
			const float from_top = (float)(y - top) + 0.5f;
			const float above = from_top + gap_mid;
			const float below = fabsf((float)(end - top) - gap_mid - from_top);
			float t = ((above < below ? above : below) - gap_mid) / ramp_px;
			t = t <= 0.0f ? 0.0f : t >= 1.0f ? 1.0f : t * t * (3.0f - 2.0f * t);
			const int w = (int)(255.0f * (1.0f - t) + 0.5f);
			weight[y] = (Uint8)(w < CRT_LINE_MIN_WEIGHT ? 0 : w);
		}
		top = end;
	}
	return true;
}

#if SDL_VERSION_ATLEAST(2, 0, 18)
static void crt_line_geometry(const SDL_Rect *dst, int rows, int scale)
{
	static const int corners[6] = { 0, 1, 2, 0, 2, 3 };

	crt_line_quads = 0;
	SDL_Vertex *const vertices = SDL_realloc(crt_line_vertices,
	                                         (size_t)rows * 4 * sizeof(*vertices));
	if (vertices == NULL)
		return;
	crt_line_vertices = vertices;
	int *const indices = SDL_realloc(crt_line_indices, (size_t)rows * 6 * sizeof(*indices));
	if (indices == NULL)
		return;
	crt_line_indices = indices;

	const CrtLook *const look = &crt_looks[crt_strength];
	const float left = (float)dst->x, right = (float)(dst->x + dst->w);
	for (int top = 0; top < rows; )
	{
		const Uint8 weight = crt_line_weight[top];
		int end = top + 1;
		while (end < rows && crt_line_weight[end] == weight)
			++end;

		if (weight != 0)
		{
			const float dark = (1.0f - look->gap) * weight / 255.0f;
			const Uint8 fill = (Uint8)(255.0f * look->fill * dark + 0.5f);
			const SDL_Color color = { fill, fill, fill, (Uint8)(255.0f * dark + 0.5f) };
			const float y0 = (float)(dst->y + top * scale), y1 = (float)(dst->y + end * scale);
			const float v0 = (float)top / rows, v1 = (float)end / rows;

			SDL_Vertex *const v = &crt_line_vertices[4 * crt_line_quads];
			v[0] = (SDL_Vertex){ { left, y0 }, color, { 0.0f, v0 } };
			v[1] = (SDL_Vertex){ { right, y0 }, color, { 1.0f, v0 } };
			v[2] = (SDL_Vertex){ { right, y1 }, color, { 1.0f, v1 } };
			v[3] = (SDL_Vertex){ { left, y1 }, color, { 0.0f, v1 } };
			for (int i = 0; i < 6; ++i)
				crt_line_indices[6 * crt_line_quads + i] = 4 * crt_line_quads + corners[i];
			++crt_line_quads;
		}
		top = end;
	}
}
#endif

static bool crt_lines_prepare(const SDL_Rect *dst, int frame_h, int scale)
{
	const CrtLinesKey key = { *dst, frame_h, crt_strength, scale };
	if (crt_lines_valid && memcmp(&key, &crt_lines_key, sizeof(key)) == 0)
		return crt_lines_shown;

	crt_destroy(&crt_scanlines);
	crt_strip_broken = false;
	crt_lines_key = key;
	crt_lines_valid = true;
	crt_lines_shown = false;

	const int rows = dst->h / scale;
	Uint8 *const weight = SDL_realloc(crt_line_weight, (size_t)(rows > 0 ? rows : 1));
	if (weight == NULL)
		return false;
	crt_line_weight = weight;
	if (!crt_line_weights(crt_line_weight, rows, frame_h))
		return false;

#if SDL_VERSION_ATLEAST(2, 0, 18)
	crt_line_geometry(dst, rows, scale);
#endif
	crt_lines_shown = true;
	return true;
}

static bool crt_ensure_strip(int h)
{
	if (crt_scanlines != NULL || crt_strip_broken)
		return crt_scanlines != NULL;

	Uint32 *const strip = SDL_malloc((size_t)h * sizeof(*strip));
	if (strip == NULL)
	{
		crt_strip_broken = true;
		return false;
	}
	const float depth = 1.0f - crt_looks[crt_strength].gap;
	for (int y = 0; y < h; ++y)
		strip[y] = crt_grey((int)(255.0f * (1.0f - depth * crt_line_weight[y] / 255.0f) + 0.5f));
	crt_scanlines = crt_texture(1, h, strip, SDL_BLENDMODE_MOD, SDL_ScaleModeNearest);
	SDL_free(strip);
	crt_strip_broken = crt_scanlines == NULL;
	return crt_scanlines != NULL;
}

static void crt_draw_lines(SDL_Texture *frame, const SDL_Rect *dst, int scale)
{
	int frame_h = 0;
	if (SDL_QueryTexture(frame, NULL, NULL, NULL, &frame_h) != 0 ||
	    !crt_lines_prepare(dst, frame_h, scale))
		return;

#if SDL_VERSION_ATLEAST(2, 0, 18)
	if (!crt_lines_unsupported && crt_line_quads > 0)
	{
		SDL_BlendMode blend = SDL_BLENDMODE_NONE;
		SDL_GetTextureBlendMode(frame, &blend);
		SDL_SetTextureBlendMode(frame, SDL_BLENDMODE_MUL);
		const int drawn = SDL_RenderGeometry(main_window_renderer, frame, crt_line_vertices,
		                                     4 * crt_line_quads, crt_line_indices,
		                                     6 * crt_line_quads);
		SDL_SetTextureBlendMode(frame, blend);
		if (drawn == 0)
			return;
		crt_lines_unsupported = true;
	}
#endif
	if (crt_ensure_strip(dst->h / scale))
		SDL_RenderCopy(main_window_renderer, crt_scanlines, NULL, dst);
}

static Uint32 crt_mask_texel(int x, int phosphor_px, const CrtLook *look)
{
	const Uint32 alpha = (Uint32)(255.0f * (1.0f - look->mask_unlit) + 0.5f);
	const Uint32 lit = (Uint32)(255.0f * (look->mask_lit - look->mask_unlit) + 0.5f);
	return alpha << 24 | lit << (16 - 8 * (x / phosphor_px % 3));
}

static int crt_phosphor_px(int w)
{
	const int span = vga_width * CRT_PHOSPHORS_PER_GAME_PX;
	const int px = (w + span / 2) / span;
	return px < 1 ? 1 : px;
}

static void crt_slot_levels(const CrtLook *look, float *lit, float *unlit)
{
	const float rows = CRT_SLOT_ROWS, gap = CRT_SLOT_GAP_ROWS;
	*unlit = fmaxf(look->mask_unlit, (2.0f * rows + gap) / (3.0f * rows));
	*lit = (3.0f * rows - 3.0f * gap * *unlit) / (rows - gap) - 2.0f * *unlit;
}

static Uint32 crt_slot_texel(int x, int y, int phosphor_px, const CrtLook *look)
{
	float lit, unlit;
	crt_slot_levels(look, &lit, &unlit);
	const Uint32 alpha = (Uint32)(255.0f * (1.0f - unlit) + 0.5f);
	const int period = CRT_SLOT_ROWS * phosphor_px;
	const int stagger = (x / (3 * phosphor_px)) % 2 * (period / 2);
	if ((y + stagger) % period >= (CRT_SLOT_ROWS - CRT_SLOT_GAP_ROWS) * phosphor_px)
		return alpha << 24;
	const Uint32 value = (Uint32)(255.0f * (lit - unlit) + 0.5f);
	return alpha << 24 | value << (16 - 8 * (x / phosphor_px % 3));
}

static bool crt_ensure_mask(int w)
{
	if (crt_mask_w == w && crt_mask_strength == crt_strength && crt_mask_kind == crt_mask)
		return crt_mask_tex != NULL;

	crt_destroy(&crt_mask_tex);
	crt_mask_w = w;
	crt_mask_strength = crt_strength;
	crt_mask_kind = crt_mask;
	if (crt_mask == CRT_MASK_OFF)
		return false;

	const int phosphor_px = crt_phosphor_px(w);
	const int period = CRT_SLOT_ROWS * phosphor_px;
	const bool slot = crt_mask == CRT_MASK_SLOT;
	const int rows = slot ? period * MAX(1, CRT_MASK_TILE_ROWS / period) : 1;
	Uint32 *const texels = SDL_malloc((size_t)w * (size_t)rows * sizeof(*texels));
	if (texels == NULL)
		return false;
	const CrtLook *const look = &crt_looks[crt_strength];
	for (int y = 0; y < rows; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			const Uint32 texel = slot ? crt_slot_texel(x, y, phosphor_px, look)
			                          : crt_mask_texel(x, phosphor_px, look);
			texels[(size_t)y * (size_t)w + (size_t)x] = texel;
		}
	}
	crt_mask_tex = crt_texture(w, rows, texels, SDL_BLENDMODE_MUL, SDL_ScaleModeNearest);
	SDL_free(texels);
	crt_mask_rows = rows;
	return crt_mask_tex != NULL;
}

static void crt_draw_mask(const SDL_Rect *dst)
{
	if (!crt_ensure_mask(dst->w))
		return;
	if (crt_mask_rows <= 1)
	{
		SDL_RenderCopy(main_window_renderer, crt_mask_tex, NULL, dst);
		return;
	}
	for (int y = 0; y < dst->h; y += crt_mask_rows)
	{
		const int rows = MIN(crt_mask_rows, dst->h - y);
		const SDL_Rect src = { 0, 0, dst->w, rows };
		const SDL_Rect band = { dst->x, dst->y + y, dst->w, rows };
		SDL_RenderCopy(main_window_renderer, crt_mask_tex, &src, &band);
	}
}

static Uint8 crt_corner_alpha(int x, int y)
{
	int outside = 0;
	for (int sy = 0; sy < 4; ++sy)
	{
		for (int sx = 0; sx < 4; ++sx)
		{
			const float dx = (float)CRT_CORNER_PX - ((float)x + ((float)sx + 0.5f) / 4.0f);
			const float dy = (float)CRT_CORNER_PX - ((float)y + ((float)sy + 0.5f) / 4.0f);
			outside += dx * dx + dy * dy > (float)(CRT_CORNER_PX * CRT_CORNER_PX);
		}
	}
	return (Uint8)((outside * 255 + 8) / 16);
}

static bool crt_ensure_corners(void)
{
	if (crt_corner_tex != NULL || crt_corner_broken)
		return crt_corner_tex != NULL;

	const int side = 2 * CRT_CORNER_PX;
	Uint32 *const texels = SDL_malloc((size_t)side * (size_t)side * sizeof(*texels));
	if (texels != NULL)
	{
		for (int y = 0; y < side; ++y)
		{
			for (int x = 0; x < side; ++x)
			{
				const int qx = x < CRT_CORNER_PX ? x : side - 1 - x;
				const int qy = y < CRT_CORNER_PX ? y : side - 1 - y;
				texels[y * side + x] = (Uint32)crt_corner_alpha(qx, qy) << 24;
			}
		}
		crt_corner_tex = crt_texture(side, side, texels, SDL_BLENDMODE_BLEND, SDL_ScaleModeLinear);
		SDL_free(texels);
	}
	crt_corner_broken = crt_corner_tex == NULL;
	return crt_corner_tex != NULL;
}

static void crt_draw_corners(const SDL_Rect *area)
{
	if (crt_corners != CRT_CORNERS_ROUNDED || !crt_ensure_corners())
		return;

	const int r = MAX(2, (int)((float)area->h * CRT_CORNER_RADIUS_SHARE + 0.5f));
	for (int k = 0; k < 4; ++k)
	{
		const bool right = (k & 1) != 0, bottom = (k & 2) != 0;
		const SDL_Rect src = { right ? CRT_CORNER_PX : 0, bottom ? CRT_CORNER_PX : 0,
		                       CRT_CORNER_PX, CRT_CORNER_PX };
		const SDL_Rect corner = { right ? area->x + area->w - r : area->x,
		                          bottom ? area->y + area->h - r : area->y, r, r };
		SDL_RenderCopy(main_window_renderer, crt_corner_tex, &src, &corner);
	}
}

static Uint8 crt_bar_alpha(int y)
{
	const float t = ((float)y + 0.5f) / (float)CRT_BAR_ROWS;
	return (Uint8)(255.0f * (0.5f - 0.5f * cosf(2.0f * (float)M_PI * t)) + 0.5f);
}

static bool crt_bar_crossing(Uint32 now_ms)
{
	return now_ms % CRT_BAR_PERIOD_MS < CRT_BAR_CROSS_MS;
}

static bool crt_bar_moving(Uint32 now_ms, bool shown)
{
	return shown || crt_bar_crossing(now_ms);
}

static bool crt_bar_place(Uint32 now_ms, const SDL_Rect *area, SDL_FRect *bar)
{
	if (!crt_bar_crossing(now_ms) || area->h <= 0)
		return false;

	const float h = fmaxf(1.0f, (float)area->h * CRT_BAR_HEIGHT_SHARE);
	const float progress = (float)(now_ms % CRT_BAR_PERIOD_MS) / (float)CRT_BAR_CROSS_MS;
	*bar = (SDL_FRect){ (float)area->x, (float)area->y - h + progress * ((float)area->h + h),
	                    (float)area->w, h };
	return true;
}

static bool crt_ensure_bar(void)
{
	if (crt_bar_tex != NULL || crt_bar_broken)
		return crt_bar_tex != NULL;

	Uint32 texels[CRT_BAR_ROWS];
	for (int y = 0; y < CRT_BAR_ROWS; ++y)
		texels[y] = (Uint32)crt_bar_alpha(y) << 24;
	crt_bar_tex = crt_texture(1, CRT_BAR_ROWS, texels, SDL_BLENDMODE_BLEND, SDL_ScaleModeLinear);
	crt_bar_broken = crt_bar_tex == NULL;
	return crt_bar_tex != NULL;
}

static void crt_draw_rolling_bar(const SDL_Rect *area)
{
	SDL_FRect bar;
	if (crt_rolling_bar == CRT_ROLLING_BAR_OFF || !crt_bar_place(SDL_GetTicks(), area, &bar) ||
	    !crt_ensure_bar())
		return;
	SDL_SetTextureAlphaMod(crt_bar_tex, (Uint8)(255.0f * crt_bar_depths[crt_rolling_bar] + 0.5f));
	SDL_RenderCopyF(main_window_renderer, crt_bar_tex, NULL, &bar);
	crt_bar_shown = true;
}

bool crt_animating(Uint32 now_ms)
{
	return crt_rolling_bar != CRT_ROLLING_BAR_OFF && !crt_bar_broken && crt_build() &&
	       crt_bar_moving(now_ms, crt_bar_shown);
}

static bool crt_glow_ensure(int w, int h)
{
	if (crt_glow_broken)
		return false;

	if (crt_glow_quarter == NULL)
	{
		crt_glow_full = crt_target(vga_width, vga_height);
		crt_glow_half = crt_target(vga_width / 2, vga_height / 2);
		crt_glow_quarter = crt_target(vga_width / 4, vga_height / 4);
		if (crt_glow_full == NULL || crt_glow_half == NULL || crt_glow_quarter == NULL)
		{
			crt_destroy(&crt_glow_full);
			crt_destroy(&crt_glow_half);
			crt_destroy(&crt_glow_quarter);
			crt_glow_broken = true;
			return false;
		}
	}

	if (w != crt_glow_src_w || h != crt_glow_src_h)
	{
		crt_release_glow();
		int lw = w, lh = h;
		while (lh > 2 * vga_height && crt_glow_levels < CRT_GLOW_LEVELS)
		{
			lw = (lw + 1) / 2;
			lh = (lh + 1) / 2;
			SDL_Texture *const level = crt_target(lw, lh);
			if (level == NULL)
				break;
			crt_glow[crt_glow_levels++] = level;
		}
		crt_glow_src_w = w;
		crt_glow_src_h = h;
	}
	return true;
}

static void crt_glow_shrink(SDL_Texture *src, SDL_Texture *dst)
{
	int w = 0, h = 0;
	SDL_QueryTexture(dst, NULL, NULL, &w, &h);
	SDL_SetRenderTarget(main_window_renderer, dst);

	SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_SetTextureColorMod(src, 128, 128, 128);
	SDL_RenderCopy(main_window_renderer, src, NULL, NULL);
	SDL_SetTextureColorMod(src, 255, 255, 255);

	SDL_SetTextureBlendMode(src, SDL_BLENDMODE_ADD);
	SDL_SetTextureAlphaMod(src, 32);
	for (int i = 0; i < 4; ++i)
	{
		const SDL_FRect at = { i % 2 != 0 ? 0.5f : -0.5f, i / 2 != 0 ? 0.5f : -0.5f,
		                       (float)w, (float)h };
		SDL_RenderCopyF(main_window_renderer, src, NULL, &at);
	}
	SDL_SetTextureAlphaMod(src, 255);
}

// NULL when targets are unsupported.
static SDL_Texture *crt_glow_from(SDL_Texture *frame)
{
	int w = 0, h = 0;
	if (!SDL_RenderTargetSupported(main_window_renderer) ||
	    SDL_QueryTexture(frame, NULL, NULL, &w, &h) != 0 || !crt_glow_ensure(w, h))
		return NULL;

	// The scale mode is read when the queue runs, so flush before putting the frame's back.
	SDL_ScaleMode frame_scale = SDL_ScaleModeNearest;
	SDL_GetTextureScaleMode(frame, &frame_scale);
	SDL_SetTextureScaleMode(frame, SDL_ScaleModeLinear);

	SDL_Texture *src = frame;
	for (int i = 0; i < crt_glow_levels; ++i)
	{
		SDL_SetRenderTarget(main_window_renderer, crt_glow[i]);
		SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
		SDL_RenderCopy(main_window_renderer, src, NULL, NULL);
		src = crt_glow[i];
	}

	SDL_SetRenderTarget(main_window_renderer, crt_glow_full);
	SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(main_window_renderer, src, NULL, NULL);
	SDL_SetTextureBlendMode(src, SDL_BLENDMODE_MOD);
	SDL_RenderCopy(main_window_renderer, src, NULL, NULL);
	SDL_RenderCopy(main_window_renderer, src, NULL, NULL);
	SDL_SetTextureBlendMode(src, SDL_BLENDMODE_NONE);
	SDL_RenderFlush(main_window_renderer);
	SDL_SetTextureScaleMode(frame, frame_scale);

	crt_glow_shrink(crt_glow_full, crt_glow_half);
	crt_glow_shrink(crt_glow_half, crt_glow_quarter);

	SDL_SetRenderTarget(main_window_renderer, crt_glow_half);
	SDL_SetTextureBlendMode(crt_glow_quarter, SDL_BLENDMODE_BLEND);
	SDL_SetTextureAlphaMod(crt_glow_quarter,
	                       (Uint8)(255.0f * crt_looks[crt_strength].glow_spread + 0.5f));
	SDL_RenderCopy(main_window_renderer, crt_glow_quarter, NULL, NULL);
	SDL_SetTextureAlphaMod(crt_glow_quarter, 255);

	SDL_SetRenderTarget(main_window_renderer, crt_glow_full);
	SDL_SetTextureBlendMode(crt_glow_half, SDL_BLENDMODE_NONE);
	SDL_RenderCopy(main_window_renderer, crt_glow_half, NULL, NULL);
	SDL_SetRenderTarget(main_window_renderer, NULL);
	return crt_glow_full;
}

static void crt_bend(SDL_FPoint bend, float *x, float *y)
{
	const float px = *x, py = *y;
	*x = px * (1.0f + bend.x * py * py);
	*y = py * (1.0f + bend.y * px * px);
}

static void crt_unbend(SDL_FPoint bend, float *x, float *y)
{
	const float sx = *x, sy = *y;
	float px = sx, py = sy;
	for (int i = 0; i < 8; ++i)
	{
		px = sx / (1.0f + bend.x * py * py);
		py = sy / (1.0f + bend.y * px * px);
	}
	*x = px;
	*y = py;
}

void crt_curve_source(const SDL_Rect *output, float *x, float *y)
{
	if ((crt_curve_shown.x == 0.0f && crt_curve_shown.y == 0.0f) ||
	    output->w <= 0 || output->h <= 0)
		return;

	float px = 2.0f * (*x - (float)output->x) / (float)output->w - 1.0f;
	float py = 2.0f * (*y - (float)output->y) / (float)output->h - 1.0f;
	crt_bend(crt_curve_shown, &px, &py);
	*x = (float)output->x + 0.5f * (px + 1.0f) * (float)output->w;
	*y = (float)output->y + 0.5f * (py + 1.0f) * (float)output->h;
}

bool crt_curvature_available(void)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
	return crt_filter_available();
#else
	return false;
#endif
}

#if SDL_VERSION_ATLEAST(2, 0, 18)
static void crt_curve_mesh(const SDL_Rect *dst, SDL_FPoint bend)
{
	const SDL_Color white = { 255, 255, 255, 255 };
	for (int j = 0; j <= CRT_CURVE_CELLS_Y; ++j)
	{
		for (int i = 0; i <= CRT_CURVE_CELLS_X; ++i)
		{
			const float u = (float)i / CRT_CURVE_CELLS_X, v = (float)j / CRT_CURVE_CELLS_Y;
			float x = 2.0f * u - 1.0f, y = 2.0f * v - 1.0f;
			crt_unbend(bend, &x, &y);
			crt_curve_vertices[j * (CRT_CURVE_CELLS_X + 1) + i] = (SDL_Vertex){
				{ (float)dst->x + 0.5f * (x + 1.0f) * (float)dst->w,
				  (float)dst->y + 0.5f * (y + 1.0f) * (float)dst->h },
				white, { u, v } };
		}
	}

	int *index = crt_curve_indices;
	for (int j = 0; j < CRT_CURVE_CELLS_Y; ++j)
	{
		for (int i = 0; i < CRT_CURVE_CELLS_X; ++i)
		{
			const int a = j * (CRT_CURVE_CELLS_X + 1) + i, c = a + CRT_CURVE_CELLS_X + 1;
			const int corners[6] = { a, a + 1, c + 1, a, c + 1, c };
			memcpy(index, corners, sizeof(corners));
			index += 6;
		}
	}
	crt_curve_mesh_dst = *dst;
	crt_curve_mesh_bend = bend;
}
#endif

static int crt_curve_scale(int dst_h)
{
	const int rows = CRT_CURVE_MIN_PX_PER_GAME_ROW * vga_height;
	return dst_h <= 0 || dst_h >= rows ? 1 : (rows + dst_h - 1) / dst_h;
}

static SDL_Texture *crt_curve_begin(const SDL_Rect *dst, SDL_FPoint bend, bool lines, int *scale)
{
	*scale = 1;
#if SDL_VERSION_ATLEAST(2, 0, 18)
	if (crt_curve_broken || (bend.x == 0.0f && bend.y == 0.0f) || dst->w <= 0 || dst->h <= 0 ||
	    !SDL_RenderTargetSupported(main_window_renderer))
		return NULL;

	const int factor = lines ? crt_curve_scale(dst->h) : 1;
	int w = 0, h = 0;
	if (crt_curve_target != NULL)
		SDL_QueryTexture(crt_curve_target, NULL, NULL, &w, &h);
	if (w != factor * dst->w || h != factor * dst->h)
	{
		if (crt_curve_failed_w == factor * dst->w && crt_curve_failed_h == factor * dst->h)
			return NULL;
		crt_destroy(&crt_curve_target);
		crt_curve_target = crt_target(factor * dst->w, factor * dst->h);
		if (crt_curve_target == NULL ||
		    SDL_SetTextureBlendMode(crt_curve_target, SDL_BLENDMODE_NONE) != 0)
		{
			crt_destroy(&crt_curve_target);
			crt_curve_failed_w = factor * dst->w;
			crt_curve_failed_h = factor * dst->h;
			return NULL;
		}
	}

	if (memcmp(dst, &crt_curve_mesh_dst, sizeof(*dst)) != 0 ||
	    bend.x != crt_curve_mesh_bend.x || bend.y != crt_curve_mesh_bend.y)
		crt_curve_mesh(dst, bend);
	if (SDL_SetRenderTarget(main_window_renderer, crt_curve_target) != 0)
		return NULL;
	*scale = factor;
	return crt_curve_target;
#else
	(void)dst;
	(void)bend;
	(void)lines;
	return NULL;
#endif
}

static void crt_curve_end(SDL_Texture *curved, const SDL_Rect *dst)
{
	SDL_SetRenderTarget(main_window_renderer, NULL);
	SDL_RenderClear(main_window_renderer);
#if SDL_VERSION_ATLEAST(2, 0, 18)
	if (SDL_RenderGeometry(main_window_renderer, curved, crt_curve_vertices,
	                       (int)COUNTOF(crt_curve_vertices), crt_curve_indices,
	                       (int)COUNTOF(crt_curve_indices)) == 0)
		return;
#endif
	crt_curve_broken = true;
	crt_curve_shown = crt_bends[CRT_CURVATURE_OFF];
	SDL_RenderCopy(main_window_renderer, curved, NULL, dst);
}

// Clear the window and draw the frame into dst, with the CRT filter over it when it is on.
// Touch controls go on afterwards and stay unfiltered.
void crt_present_frame(SDL_Texture *frame, const SDL_Rect *dst)
{
	const bool wanted = crt_filter != CRT_FILTER_OFF || crt_curvature != CRT_CURVATURE_OFF ||
	                    crt_corners != CRT_CORNERS_SQUARE || crt_rolling_bar != CRT_ROLLING_BAR_OFF;
	const bool gpu = frame != NULL && wanted && crt_build();
	const bool crt = gpu && crt_filter != CRT_FILTER_OFF;
	const bool full = crt && crt_filter == CRT_FILTER_FULL;
	SDL_Texture *const glow = full ? crt_glow_from(frame) : NULL;
	const SDL_FPoint bend = crt_bends[gpu ? crt_curvature : CRT_CURVATURE_OFF];
	int scale = 1;
	SDL_Texture *const curved = crt_curve_begin(dst, bend, crt, &scale);
	const SDL_Rect area = curved != NULL ? (SDL_Rect){ 0, 0, scale * dst->w, scale * dst->h }
	                                     : *dst;
	crt_curve_shown = curved != NULL ? bend : crt_bends[CRT_CURVATURE_OFF];
	crt_bar_shown = false;

	SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
	SDL_RenderClear(main_window_renderer);
	if (frame != NULL)
		SDL_RenderCopy(main_window_renderer, frame, NULL, &area);
	if (crt)
		crt_draw_lines(frame, &area, scale);

	// Added after the scanlines, the glow fills their gaps where the picture is bright.
	if (glow != NULL)
	{
		SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);
		SDL_SetTextureAlphaMod(glow, (Uint8)(255.0f * crt_looks[crt_strength].glow + 0.5f));
		SDL_RenderCopy(main_window_renderer, glow, NULL, &area);
		SDL_SetTextureAlphaMod(glow, 255);
	}
	if (gpu)
	{
		crt_draw_rolling_bar(&area);
		crt_draw_corners(&area);
	}
	if (curved != NULL)
		crt_curve_end(curved, dst);
	if (!full)
		return;

	crt_draw_mask(dst);
	SDL_RenderCopy(main_window_renderer, crt_vignette, NULL, dst);
}

bool video_test_crt_rows(char *detail, size_t detailSize)
{
	enum { ROW_PX_MAX = 16 };
	static const int sizes[][2] = {
		{ 539, 200 }, { 719, 800 }, { 1078, 1078 }, { 1078, 1424 }, { 1438, 1000 },
		{ 2157, 1068 }, { 400, 400 }, { 600, 1600 }, { 800, 200 },
	};
	static Uint8 weight[2160];
	char fault[128] = "";

	if (crt_line_weights(weight, 2 * vga_height - 1, vga_height))
		snprintf(fault, sizeof(fault), "lines drawn at under two output rows per game row");

	for (size_t k = 0; k < COUNTOF(sizes) && fault[0] == '\0'; ++k)
	{
		const int dst_h = sizes[k][0], frame_h = sizes[k][1];
		if (!crt_line_weights(weight, dst_h, frame_h))
		{
			snprintf(fault, sizeof(fault), "no lines at %d rows from %d", dst_h, frame_h);
			break;
		}

		for (int y = 0; y < dst_h && fault[0] == '\0'; ++y)
		{
			const int want = frame_h == dst_h
			               ? y * vga_height / dst_h
			               : (int)floor(floor((y + 0.5) * frame_h / dst_h) * vga_height / frame_h);
			const int got = crt_game_row(y, dst_h, frame_h);
			if (got != want)
				snprintf(fault, sizeof(fault), "row %d of %d from %d is game row %d, not %d",
				         y, dst_h, frame_h, got, want);
		}

		Uint8 shape[ROW_PX_MAX + 1][ROW_PX_MAX];
		bool shape_seen[ROW_PX_MAX + 1] = { false };
		int gap_px = -1;
		for (int top = 0; top < dst_h && fault[0] == '\0'; )
		{
			const int row = crt_game_row(top, dst_h, frame_h);
			int end = top + 1;
			while (end < dst_h && crt_game_row(end, dst_h, frame_h) == row)
				++end;

			const int n = end - top;
			int dark = 0;
			while (dark < n && weight[end - 1 - dark] == 255)
				++dark;
			bool lit = false;
			for (int y = top; y < end; ++y)
				lit = lit || weight[y] == 0;

			if (n > ROW_PX_MAX)
				snprintf(fault, sizeof(fault), "game row %d is %d rows tall", row, n);
			else if (dark == 0 || (gap_px >= 0 && dark != gap_px))
				snprintf(fault, sizeof(fault),
				         "game row %d at %d rows from %d ends in %d dark rows",
				         row, dst_h, frame_h, dark);
			else if (!lit)
				snprintf(fault, sizeof(fault), "game row %d at %d rows from %d has no lit row",
				         row, dst_h, frame_h);
			else if (shape_seen[n] && memcmp(shape[n], &weight[top], (size_t)n) != 0)
				snprintf(fault, sizeof(fault), "two %d-row game rows differ at %d rows from %d",
				         n, dst_h, frame_h);

			if (n <= ROW_PX_MAX && !shape_seen[n])
			{
				memcpy(shape[n], &weight[top], (size_t)n);
				shape_seen[n] = true;
			}
			gap_px = dark;
			top = end;
		}
	}

	for (int s = 0; s < CRT_STRENGTH_COUNT && fault[0] == '\0'; ++s)
	{
		for (int px = 1; px <= 4 && fault[0] == '\0'; ++px)
		{
			float sum[3] = { 0.0f, 0.0f, 0.0f };
			for (int x = 0; x < 3 * px; ++x)
			{
				const Uint32 texel = crt_mask_texel(x, px, &crt_looks[s]);
				const float alpha = (float)(texel >> 24) / 255.0f;
				for (int c = 0; c < 3; ++c)
				{
					const Uint32 v = texel >> (16 - 8 * c) & 0xFF;
					sum[c] += (float)v / 255.0f + 1.0f - alpha;
					if ((v != 0) != (c == x / px % 3))
						snprintf(fault, sizeof(fault),
						         "mask column %d of width %d lights channel %d", x, px, c);
				}
			}
			for (int c = 0; c < 3 && fault[0] == '\0'; ++c)
				if (fabsf(sum[c] / (float)(3 * px) - 1.0f) > 0.01f)
					snprintf(fault, sizeof(fault), "mask channel %d averages %.3f at strength %d",
					         c, sum[c] / (float)(3 * px), s);
		}
	}

	static const int phosphors[][2] = {
		{ 356, 1 }, { 960, 1 }, { 1920, 1 }, { 3840, 2 }, { 5120, 3 }, { 7680, 4 },
	};
	for (size_t k = 0; k < COUNTOF(phosphors) && fault[0] == '\0'; ++k)
		if (crt_phosphor_px(phosphors[k][0]) != phosphors[k][1])
			snprintf(fault, sizeof(fault), "phosphors %d wide at %d columns, not %d",
			         crt_phosphor_px(phosphors[k][0]), phosphors[k][0], phosphors[k][1]);

	if (fault[0] != '\0' && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault[0] == '\0';
}

bool video_test_crt_curve(char *detail, size_t detailSize)
{
	char fault[128] = "";

	for (int k = CRT_CURVATURE_SUBTLE; k < CRT_CURVATURE_COUNT && fault[0] == '\0'; ++k)
	{
		const SDL_FPoint bend = crt_bends[k];
		for (int j = 0; j <= 20 && fault[0] == '\0'; ++j)
		{
			for (int i = 0; i <= 20 && fault[0] == '\0'; ++i)
			{
				const float sx = (float)i / 10.0f - 1.0f, sy = (float)j / 10.0f - 1.0f;
				float x = sx, y = sy;
				crt_unbend(bend, &x, &y);
				const bool on_screen = fabsf(x) <= 1.0f && fabsf(y) <= 1.0f;
				crt_bend(bend, &x, &y);
				if (!on_screen)
					snprintf(fault, sizeof(fault),
					         "curvature %d puts picture point %.1f,%.1f off screen", k, sx, sy);
				else if (fabsf(x - sx) > 1e-4f || fabsf(y - sy) > 1e-4f)
					snprintf(fault, sizeof(fault), "curvature %d does not invert at %.1f,%.1f",
					         k, sx, sy);
			}
		}

		float x = 1.0f, y = 0.0f;
		crt_unbend(bend, &x, &y);
		if (fault[0] == '\0' && (fabsf(x - 1.0f) > 1e-6f || y != 0.0f))
			snprintf(fault, sizeof(fault), "curvature %d moves the middle of the right edge", k);
		x = y = 1.0f;
		crt_unbend(bend, &x, &y);
		if (fault[0] == '\0' && (x > 0.99f || y > 0.99f))
			snprintf(fault, sizeof(fault), "curvature %d leaves the corners square", k);
	}

	const int composite_rows = CRT_CURVE_MIN_PX_PER_GAME_ROW * vga_height;
	for (int h = vga_height; h <= 1200 && fault[0] == '\0'; ++h)
	{
		const int f = crt_curve_scale(h);
		if (f < 1 || f * h < composite_rows || (f > 1 && (f - 1) * h >= composite_rows))
			snprintf(fault, sizeof(fault), "curved picture of %d rows is drawn %d times taller",
			         h, f);
	}

#if SDL_VERSION_ATLEAST(2, 0, 18)
	const SDL_FPoint saved_shown = crt_curve_shown;
	const SDL_Rect dst = { 37, 11, 1920, 1078 };
	for (int k = CRT_CURVATURE_SUBTLE; k < CRT_CURVATURE_COUNT && fault[0] == '\0'; ++k)
	{
		crt_curve_mesh(&dst, crt_bends[k]);
		crt_curve_shown = crt_bends[k];
		for (size_t n = 0; n < COUNTOF(crt_curve_vertices) && fault[0] == '\0'; ++n)
		{
			const SDL_Vertex *const v = &crt_curve_vertices[n];
			float x = v->position.x, y = v->position.y;
			crt_curve_source(&dst, &x, &y);
			const float want_x = (float)dst.x + v->tex_coord.x * (float)dst.w;
			const float want_y = (float)dst.y + v->tex_coord.y * (float)dst.h;
			const bool inside = v->position.x >= (float)dst.x - 0.01f
			                 && v->position.x <= (float)(dst.x + dst.w) + 0.01f
			                 && v->position.y >= (float)dst.y - 0.01f
			                 && v->position.y <= (float)(dst.y + dst.h) + 0.01f;
			if (!inside)
				snprintf(fault, sizeof(fault), "curvature %d mesh vertex %u leaves the picture",
				         k, (unsigned)n);
			else if (fabsf(x - want_x) > 0.05f || fabsf(y - want_y) > 0.05f)
				snprintf(fault, sizeof(fault),
				         "curvature %d pointer misses mesh vertex %u by %.2f,%.2f",
				         k, (unsigned)n, x - want_x, y - want_y);
		}
	}
	crt_curve_shown = saved_shown;
#endif

	if (fault[0] != '\0' && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault[0] == '\0';
}

bool video_test_crt_layers(char *detail, size_t detailSize)
{
	char fault[128] = "";

	for (int s = 0; s < CRT_STRENGTH_COUNT && fault[0] == '\0'; ++s)
	{
		for (int px = 1; px <= 4 && fault[0] == '\0'; ++px)
		{
			const int period = CRT_SLOT_ROWS * px;
			float sum[3] = { 0.0f, 0.0f, 0.0f };
			for (int x = 0; x < 6 * px && fault[0] == '\0'; ++x)
			{
				int dark = 0;
				for (int y = 0; y < period; ++y)
				{
					const Uint32 texel = crt_slot_texel(x, y, px, &crt_looks[s]);
					const float alpha = (float)(texel >> 24) / 255.0f;
					int lit = 0;
					for (int c = 0; c < 3; ++c)
					{
						const Uint32 v = texel >> (16 - 8 * c) & 0xFF;
						sum[c] += (float)v / 255.0f + 1.0f - alpha;
						if (v != 0 && c != x / px % 3)
							snprintf(fault, sizeof(fault),
							         "slot column %d of width %d lights channel %d", x, px, c);
						lit += v != 0;
					}
					dark += lit == 0;
				}
				if (fault[0] == '\0' && dark != CRT_SLOT_GAP_ROWS * px)
					snprintf(fault, sizeof(fault), "slot column %d of width %d has %d dark rows",
					         x, px, dark);
			}
			for (int c = 0; c < 3 && fault[0] == '\0'; ++c)
				if (fabsf(sum[c] / (float)(6 * px * period) - 1.0f) > 0.01f)
					snprintf(fault, sizeof(fault), "slot channel %d averages %.3f at strength %d",
					         c, sum[c] / (float)(6 * px * period), s);
			for (int y = 0; y < period && fault[0] == '\0'; ++y)
				if ((crt_slot_texel(0, y, px, &crt_looks[s]) & 0xFFFFFF) == 0 &&
				    (crt_slot_texel(3 * px, y, px, &crt_looks[s]) & 0xFFFFFF) == 0)
					snprintf(fault, sizeof(fault),
					         "neighbouring slots of width %d share dark row %d", px, y);
		}
	}

	bool soft = false;
	for (int y = 0; y < CRT_CORNER_PX && fault[0] == '\0'; ++y)
	{
		for (int x = 0; x < CRT_CORNER_PX && fault[0] == '\0'; ++x)
		{
			const Uint8 a = crt_corner_alpha(x, y);
			soft = soft || (a != 0 && a != 255);
			if (a != crt_corner_alpha(y, x))
				snprintf(fault, sizeof(fault), "corner coverage is not symmetric at %d,%d", x, y);
			else if ((x + 1 < CRT_CORNER_PX && crt_corner_alpha(x + 1, y) > a) ||
			         (y + 1 < CRT_CORNER_PX && crt_corner_alpha(x, y + 1) > a))
				snprintf(fault, sizeof(fault), "corner coverage grows inwards at %d,%d", x, y);
		}
	}
	if (fault[0] == '\0' && (crt_corner_alpha(0, 0) != 255 ||
	                          crt_corner_alpha(CRT_CORNER_PX - 1, CRT_CORNER_PX - 1) != 0 || !soft))
		snprintf(fault, sizeof(fault), "corner coverage is not an antialiased quarter circle");

	for (int y = 0; y < CRT_BAR_ROWS && fault[0] == '\0'; ++y)
		if (crt_bar_alpha(y) != crt_bar_alpha(CRT_BAR_ROWS - 1 - y))
			snprintf(fault, sizeof(fault), "rolling bar profile is not symmetric at row %d", y);
	if (fault[0] == '\0' && (crt_bar_alpha(0) > 8 || crt_bar_alpha(CRT_BAR_ROWS / 2) < 250))
		snprintf(fault, sizeof(fault), "rolling bar profile does not fade in and out");

	const SDL_Rect area = { 37, 11, 1920, 1078 };
	SDL_FRect bar, prev = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (Uint32 t = 0; t < CRT_BAR_PERIOD_MS && fault[0] == '\0'; t += 50)
	{
		const bool shown = crt_bar_place(t, &area, &bar);
		if (shown != (t < CRT_BAR_CROSS_MS))
			snprintf(fault, sizeof(fault), "rolling bar shown is %d at %u ms", shown, (unsigned)t);
		else if (shown && t > 0 && bar.y <= prev.y)
			snprintf(fault, sizeof(fault), "rolling bar does not move down at %u ms", (unsigned)t);
		if (shown)
			prev = bar;
	}
	if (fault[0] == '\0')
	{
		crt_bar_place(0, &area, &bar);
		const float top = bar.y + bar.h;
		crt_bar_place(CRT_BAR_CROSS_MS - 1, &area, &prev);
		if (fabsf(top - (float)area.y) > 0.01f || prev.y < (float)(area.y + area.h) - 2.0f ||
		    !crt_bar_place(CRT_BAR_PERIOD_MS, &area, &bar) || fabsf(bar.y - (top - bar.h)) > 0.01f)
			snprintf(fault, sizeof(fault), "rolling bar does not cross the whole picture");
	}
	for (Uint32 t = 0; t < 2 * CRT_BAR_PERIOD_MS && fault[0] == '\0'; ++t)
	{
		const bool drawn = crt_bar_place(t, &area, &bar);
		if (crt_bar_moving(t, false) != drawn)
			snprintf(fault, sizeof(fault), "rolling bar %s redraws at %u ms",
			         drawn ? "is drawn without asking for" : "asks off screen for", (unsigned)t);
		else if (!crt_bar_moving(t, true))
			snprintf(fault, sizeof(fault), "rolling bar left on screen at %u ms stays there",
			         (unsigned)t);
	}

	if (fault[0] != '\0' && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault[0] == '\0';
}
