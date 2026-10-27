#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "background.h"
#include "config.h"
#include "game_state.h"
#include "palette.h"
#include "render_list.h"
#include "rollback.h"
#include "sprite.h"
#include "timing.h"
#include "video.h"
#include "video_scale.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void qa_test_present_paths(void)
{
	static const struct { int w, h, bpp; bool clip; } shapes[] = {
		{ 356, 200, 8, false }, { 357, 5, 8, false }, { 41, 9, 8, true },
		{ 7, 3, 32, false }, { 9, 4, 32, true },
	};
	bool cleared = true;
	for (size_t i = 0; i < COUNTOF(shapes); ++i)
	{
		const int w = shapes[i].w, h = shapes[i].h, bpp = shapes[i].bpp;
		SDL_Surface *const a = SDL_CreateRGBSurface(0, w, h, bpp, 0, 0, 0, 0);
		SDL_Surface *const b = SDL_CreateRGBSurface(0, w, h, bpp, 0, 0, 0, 0);
		if (a == NULL || b == NULL)
		{
			cleared = false;
		}
		else
		{
			if (shapes[i].clip)
			{
				const SDL_Rect r = { 2, 1, w - 5, h - 3 };
				SDL_SetClipRect(a, &r);
				SDL_SetClipRect(b, &r);
			}
			memset(a->pixels, 0xa5, (size_t)a->pitch * a->h);
			memset(b->pixels, 0xa5, (size_t)b->pitch * b->h);
			SDL_FillRect(a, NULL, 0);
			JE_clr256(b);
			cleared = cleared && memcmp(a->pixels, b->pixels, (size_t)a->pitch * a->h) == 0;
		}
		SDL_FreeSurface(a);
		SDL_FreeSurface(b);
	}
	qa_check(cleared,
	         "JE_clr256 clears exactly what SDL_FillRect clears, clip rect and padding included");

	Uint32 savedPalette[256];
	memcpy(savedPalette, rgb_palette, sizeof(savedPalette));
	for (unsigned i = 0; i < 256; ++i)
		rgb_palette[i] = (i * 0x01010101u) ^ 0x9e3779b9u;

	char detail[160];
	char label[256];
	detail[0] = '\0';
	const bool hiRows = video_test_parallel_rows(detail, sizeof(detail));
	snprintf(label, sizeof(label), "threaded hi-res palette rows match one thread%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(hiRows, label);

	detail[0] = '\0';
	const bool fitRows = video_scale_test_fit_rows(detail, sizeof(detail));
	snprintf(label, sizeof(label), "threaded Native scaler rows match one thread%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(fitRows, label);

	detail[0] = '\0';
	const bool rowStats = video_test_rows_stats(detail, sizeof(detail));
	snprintf(label, sizeof(label), "frame-row stats follow the tuning run%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(rowStats, label);

	detail[0] = '\0';
	const bool rowRounds = video_test_rows_rounds(detail, sizeof(detail));
	snprintf(label, sizeof(label), "frame-row rounds tune on whole frames%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(rowRounds, label);

	detail[0] = '\0';
	const bool rowPick = video_test_rows_pick(detail, sizeof(detail));
	snprintf(label, sizeof(label), "frame-row tuning picks on median and 95th percentile%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(rowPick, label);

	detail[0] = '\0';
	const bool rowWatch = video_test_rows_watch(detail, sizeof(detail));
	snprintf(label, sizeof(label), "frame-row tuning restarts when a site's cost moves%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(rowWatch, label);

	detail[0] = '\0';
	const bool lockFail = video_scale_test_lock_failure(detail, sizeof(detail));
	snprintf(label, sizeof(label), "every scaler skips a texture it cannot lock%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(lockFail, label);

	detail[0] = '\0';
	const bool nnRows = video_scale_test_nn_rows(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "plain scaler rows at 1x to 5x, on one thread and the pool, match a direct lookup%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(nnRows, label);

	detail[0] = '\0';
	const bool crtRows = video_test_crt_rows(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "CRT lines snap to the frame's game rows and the phosphor mask keeps brightness%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(crtRows, label);

	detail[0] = '\0';
	const bool crtCurve = video_test_crt_curve(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "CRT curvature keeps the whole picture on screen and the pointer on the mesh%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(crtCurve, label);

	detail[0] = '\0';
	const bool crtLayers = video_test_crt_layers(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "CRT slot mask keeps brightness, corners stay smooth, the rolling bar rolls down and "
	         "keeps redrawing until it leaves%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(crtLayers, label);

	detail[0] = '\0';
	const bool idleRefresh = video_test_idle_refresh(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "idle waits re-present at the display rate without holding up a frame%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(idleRefresh, label);

	detail[0] = '\0';
	const bool waitFrames = timing_test_wait_frames(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "waits pacing a new frame leave presenting to it; holds and idle waits do not%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(waitFrames, label);

	memcpy(rgb_palette, savedPalette, sizeof(savedPalette));
}

static void qa_scalar_sprite2_block(SDL_Surface *surface, int x, int y, int scale, Uint8 d,
                                    Blit2Op op, Uint8 filter)
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
			switch (op)
			{
			case BLIT2_COPY:   *p = d; break;
			case BLIT2_BLEND:  *p = (((d & 0x0f) + (*p & 0x0f)) / 2) | (d & 0xf0); break;
			case BLIT2_DARKEN: *p = ((*p & 0x0f) / 2) + (*p & 0xf0); break;
			case BLIT2_FILTER: *p = filter | (d & 0x0f); break;
			case BLIT2_SOLID:  *p = filter; break;
			default: break;
			}
		}
	}
}

static void qa_scalar_sprite2_scaled(SDL_Surface *surface, int x, int y, const Uint8 *data,
                                     int scale, Blit2Op op, Uint8 filter)
{
	for (; *data != 0x0f; ++data)
	{
		x += (*data & 0x0f) * scale;
		unsigned int count = (*data & 0xf0) >> 4;

		if (count == 0)
		{
			y += scale;
			x -= 12 * scale;
		}
		else
		{
			if (y >= surface->h)
				return;
			while (count--)
			{
				++data;
				qa_scalar_sprite2_block(surface, x, y, scale, *data, op, filter);
				x += scale;
			}
		}
	}
}

void qa_test_scaled_sprite_blocks(void)
{
	static const Uint8 rle[] = {
		0x52, 0x21, 0x22, 0x23, 0x24, 0x25, 0x31, 0x96, 0x97, 0x98, 0x01,
		0xc0, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x00,
		0x16, 0xe1, 0x05,
		0x40, 0x41, 0x42, 0x43, 0x44, 0x44, 0x5a, 0x5b, 0x5c, 0x5d, 0x00,
		0x2a, 0x77, 0x78, 0x00,
		0x0f,
	};
	static Uint16 sheet_words[(2 + sizeof(rle) + 1) / 2];
	Uint8 *const sheet_bytes = (Uint8 *)sheet_words;
	sheet_bytes[0] = 2;
	sheet_bytes[1] = 0;
	memcpy(sheet_bytes + 2, rle, sizeof(rle));
	const Sprite2_array sheet = { 2 + sizeof(rle), sheet_bytes };

	static const Blit2Op ops[] = {
		BLIT2_COPY, BLIT2_BLEND, BLIT2_DARKEN, BLIT2_FILTER, BLIT2_SOLID,
	};
	enum { W = 61, H = 37, PITCH = W + 13 };
	static Uint8 buf_a[PITCH * H], buf_b[PITCH * H];
	SDL_Surface *const a = SDL_CreateRGBSurfaceFrom(buf_a, W, H, 8, PITCH, 0, 0, 0, 0);
	SDL_Surface *const b = SDL_CreateRGBSurfaceFrom(buf_b, W, H, 8, PITCH, 0, 0, 0, 0);
	Uint32 seed = 0xb10cc5u;
	bool same = a != NULL && b != NULL;

	for (int scale = 1; scale <= 6 && same; ++scale)
	{
		const int spots[][2] = {
			{ 3, 2 }, { -5 * scale, -2 * scale }, { W - 7 * scale, H - 3 * scale },
			{ 1 - 12 * scale, 4 }, { W - 1, 0 }, { W + 2, 1 }, { 0, H + 1 }, { -3, -4 * scale },
		};
		for (size_t s = 0; s < COUNTOF(spots) && same; ++s)
		{
			for (size_t o = 0; o < COUNTOF(ops) && same; ++o)
			{
				for (size_t i = 0; i < sizeof(buf_a); ++i)
				{
					seed = seed * 1664525u + 1013904223u;
					buf_a[i] = buf_b[i] = (Uint8)(seed >> 24);
				}
				qa_scalar_sprite2_scaled(a, spots[s][0], spots[s][1], rle, scale, ops[o], 0x50);
				blit_sprite2_scaled(b, spots[s][0], spots[s][1], sheet, 1, scale, ops[o], 0x50);
				same = memcmp(buf_a, buf_b, sizeof(buf_a)) == 0;
			}
		}
	}
	SDL_FreeSurface(a);
	SDL_FreeSurface(b);
	qa_check(same,
	         "scaled sprite blocks match the per-pixel blit, clipped edges and padding included");
}

static void qa_scalar_sprite_table_scaled(SDL_Surface *surface, int x, int y,
                                          const Sprite *cur_sprite, int scale, BlitTableOp op,
                                          Uint8 hue, Sint8 value, bool black)
{
	const Uint8 *data = cur_sprite->data;
	const Uint8 *const data_ul = data + cur_sprite->size;
	const unsigned int width = cur_sprite->width;
	unsigned int x_offset = 0;

	hue <<= 4;

	int cx = x, cy = y;

	for (; data < data_ul; ++data)
	{
		switch (*data)
		{
		case 255:
			data++;
			cx += *data * scale;
			x_offset += *data;
			break;

		case 254:
			cx += (width - x_offset) * scale;
			x_offset = width;
			break;

		case 253:
			cx += scale;
			x_offset++;
			break;

		default:
			if (cy >= surface->h)
				return;

			{
				int x0 = cx < 0 ? 0 : cx;
				int y0 = cy < 0 ? 0 : cy;
				int x1 = cx + scale, y1 = cy + scale;
				if (x1 > surface->w)
					x1 = surface->w;
				if (y1 > surface->h)
					y1 = surface->h;

				Uint8 flat = 0;
				bool have_flat = false;
				switch (op)
				{
				case BLITT_COPY:
					flat = *data;
					have_flat = true;
					break;
				case BLITT_HV_UNSAFE:
					flat = hue | (Uint8)((*data & 0x0f) + value);
					have_flat = true;
					break;
				case BLITT_HV:
				{
					Uint8 temp_value = (*data & 0x0f) + value;
					if (temp_value > 0xf)
						temp_value = (temp_value >= 0x1f) ? 0x0 : 0xf;
					flat = hue | temp_value;
					have_flat = true;
					break;
				}
				case BLITT_DARK:
					if (black)
					{
						flat = 0x00;
						have_flat = true;
					}
					break;
				default:
					break;
				}

				for (int yy = y0; yy < y1; ++yy)
				{
					Uint8 *p = (Uint8 *)surface->pixels + yy * surface->pitch + x0;
					for (int xx = x0; xx < x1; ++xx, ++p)
					{
						if (have_flat)
						{
							*p = flat;
							continue;
						}
						switch (op)
						{
						case BLITT_BLEND:
							*p = (*data & 0xf0) | (((*p & 0x0f) + (*data & 0x0f)) / 2);
							break;
						case BLITT_HV_BLEND:
						{
							Uint8 temp_value = (*data & 0x0f) + value;
							if (temp_value > 0xf)
								temp_value = (temp_value >= 0x1f) ? 0x0 : 0xf;
							*p = hue | (((*p & 0x0f) + temp_value) / 2);
							break;
						}
						case BLITT_DARK:
							*p = (*p & 0xf0) | ((*p & 0x0f) / 2);
							break;
						default:
							break;
						}
					}
				}
			}

			cx += scale;
			x_offset++;
			break;
		}
		if (x_offset >= width)
		{
			cy += scale;
			cx = x;
			x_offset = 0;
		}
	}
}

void qa_test_scaled_table_sprites(void)
{
	static Uint8 pixels[] = {
		0x21, 0x22, 253, 0x23, 255, 2, 0x24, 0x25,
		254,
		0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
		255, 5, 0x41, 0x42, 0x43,
		0x51, 254,
	};
	enum { TABLE = EXTRA_SHAPES, INDEX = 0 };
	const Sprite saved_sprite = sprite_table[TABLE].sprite[INDEX];
	const unsigned int saved_count = sprite_table[TABLE].count;
	const Sprite test_sprite = { 8, 5, (Uint16)sizeof(pixels), pixels };
	sprite_table[TABLE].sprite[INDEX] = test_sprite;
	if (sprite_table[TABLE].count <= INDEX)
		sprite_table[TABLE].count = INDEX + 1;

	static const struct { BlitTableOp op; bool black; } ops[] = {
		{ BLITT_COPY, false }, { BLITT_BLEND, false }, { BLITT_HV_UNSAFE, false },
		{ BLITT_HV, false }, { BLITT_HV_BLEND, false }, { BLITT_DARK, false }, { BLITT_DARK, true },
	};
	static const Sint8 values[] = { 3, -9, 14 };
	enum { W = 53, H = 31, PITCH = W + 11 };
	static Uint8 buf_a[PITCH * H], buf_b[PITCH * H];
	SDL_Surface *const a = SDL_CreateRGBSurfaceFrom(buf_a, W, H, 8, PITCH, 0, 0, 0, 0);
	SDL_Surface *const b = SDL_CreateRGBSurfaceFrom(buf_b, W, H, 8, PITCH, 0, 0, 0, 0);
	Uint32 seed = 0x7ab1e5u;
	bool same = a != NULL && b != NULL;

	for (int scale = 1; scale <= 6 && same; ++scale)
	{
		const int spots[][2] = {
			{ 2, 3 }, { -3 * scale, -scale }, { W - 5 * scale, H - 2 * scale },
			{ 1 - 8 * scale, 2 }, { W - 1, 0 }, { W + 1, 1 }, { 0, H }, { 4, -5 * scale },
		};
		for (size_t s = 0; s < COUNTOF(spots) && same; ++s)
		{
			for (size_t o = 0; o < COUNTOF(ops) && same; ++o)
			{
				for (size_t v = 0; v < COUNTOF(values) && same; ++v)
				{
					for (size_t i = 0; i < sizeof(buf_a); ++i)
					{
						seed = seed * 1664525u + 1013904223u;
						buf_a[i] = buf_b[i] = (Uint8)(seed >> 24);
					}
					qa_scalar_sprite_table_scaled(a, spots[s][0], spots[s][1], &test_sprite, scale,
					                              ops[o].op, 5, values[v], ops[o].black);
					blit_sprite_table_scaled(b, spots[s][0], spots[s][1], TABLE, INDEX, scale,
					                         ops[o].op, 5, values[v], ops[o].black);
					same = memcmp(buf_a, buf_b, sizeof(buf_a)) == 0;
				}
			}
		}
	}
	SDL_FreeSurface(a);
	SDL_FreeSurface(b);
	sprite_table[TABLE].sprite[INDEX] = saved_sprite;
	sprite_table[TABLE].count = saved_count;
	qa_check(same, "scaled table sprites match the per-pixel blit, clipped edges included");
}

enum
{
	QA_BG_TILE_W = 24,
	QA_BG_TILE_COUNT = PLAYFIELD_WIDTH / 24 + 2,
	QA_BG_EDGE_TILES = 1,
};

static Uint8 *qa_scalar_mirror_tile(Uint8 **map, int tile, int mirror_w, int col0, bool *flip)
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

static int qa_scalar_edge_px(int mirror_w, int x, int surface_w, int scale)
{
	if (mirror_w == 0)
		return 0;
	const int room = (surface_w - x - QA_BG_TILE_COUNT * QA_BG_TILE_W * scale) / scale;
	if (room <= 0)
		return 0;
	return room < QA_BG_EDGE_TILES * QA_BG_TILE_W ? room : QA_BG_EDGE_TILES * QA_BG_TILE_W;
}

static void qa_scalar_background_row(SDL_Surface *surface, int x, int y, Uint8 **map,
                                     int mirror_w, int col0, bool blend)
{
	Uint8 *pixels = (Uint8 *)surface->pixels + (y * surface->pitch) + x,
	      *pixels_ll = (Uint8 *)surface->pixels,
	      *pixels_ul = (Uint8 *)surface->pixels + (surface->h * surface->pitch);

	const int edge_px = qa_scalar_edge_px(mirror_w, x, surface->w, 1);
	const int tile_count = QA_BG_TILE_COUNT + (edge_px > 0 ? 1 : 0);
	const int row_width = QA_BG_TILE_COUNT * QA_BG_TILE_W + edge_px;
	for (int ty = 0; ty < 28; ty++)
	{
		if ((pixels + row_width) < pixels_ll)
		{
			pixels += surface->pitch;
			continue;
		}

		for (int tile = 0; tile < tile_count; tile++)
		{
			const int tile_w = (tile < QA_BG_TILE_COUNT) ? QA_BG_TILE_W : edge_px;

			bool flip;
			Uint8 *data = qa_scalar_mirror_tile(map, tile, mirror_w, col0, &flip);
			if (data == NULL)
			{
				pixels += tile_w;
				continue;
			}

			data += ty * 24 + (flip ? 23 : 0);
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

static void qa_scalar_background_row_scaled(SDL_Surface *surface, int x, int y, Uint8 **map,
                                            int scale, bool blend, int mirror_w, int col0)
{
	const int edge_px = qa_scalar_edge_px(mirror_w, x, surface->w, scale);
	const int tile_count = QA_BG_TILE_COUNT + (edge_px > 0 ? 1 : 0);

	for (int ty = 0; ty < 28; ++ty)
	{
		const int hy = y + ty * scale;
		if (hy >= surface->h)
			return;

		int by0 = hy < 0 ? 0 : hy;
		int by1 = hy + scale;
		if (by1 > surface->h)
			by1 = surface->h;
		if (by1 <= by0)
			continue;

		int hx = x;
		for (int tile = 0; tile < tile_count; ++tile)
		{
			const int tile_w = (tile < QA_BG_TILE_COUNT) ? QA_BG_TILE_W : edge_px;

			bool flip;
			const Uint8 *data = qa_scalar_mirror_tile(map, tile, mirror_w, col0, &flip);
			if (data == NULL)
			{
				hx += tile_w * scale;
				continue;
			}

			data += ty * QA_BG_TILE_W + (flip ? QA_BG_TILE_W - 1 : 0);
			const int dstep = flip ? -1 : 1;

			for (int sx = 0; sx < tile_w; ++sx, data += dstep, hx += scale)
			{
				const Uint8 d = *data;
				if (d == 0)
					continue;

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

static void qa_scalar_filter_screen(SDL_Surface *surface, JE_shortint col, JE_shortint int_,
                                    int scale)
{
	const int left = PLAYFIELD_LEFT * scale;
	const int width = PLAYFIELD_WIDTH * scale;
	const int rows = 184 * scale;
	Uint8 *s = NULL;
	int x, y;
	unsigned int temp;

	if (col != -99 && filtrationAvail)
	{
		s = (Uint8 *)surface->pixels + left;

		col <<= 4;

		for (y = rows; y; y--)
		{
			for (x = width; x; x--)
			{
				*s = col | (*s & 0x0f);
				s++;
			}
			s += surface->pitch - width;
		}
	}

	if (int_ != -99 && explosionTransparent)
	{
		s = (Uint8 *)surface->pixels + left;

		for (y = rows; y; y--)
		{
			for (x = width; x; x--)
			{
				temp = (*s & 0x0f) + int_;
				*s = (*s & 0xf0) | (temp >= 0x1f ? 0 : (temp >= 0x0f ? 0x0f : temp));
				s++;
			}
			s += surface->pitch - width;
		}
	}
}

static void qa_scalar_lava_filter(SDL_Surface *dst, SDL_Surface *src)
{
	const int dst_pitch = dst->pitch;
	Uint8 *dst_pixel = (Uint8 *)dst->pixels + (185 * dst_pitch);
	const Uint8 * const dst_pixel_ll = (Uint8 *)dst->pixels;

	const int src_pitch = src->pitch;
	const Uint8 *src_pixel = (Uint8 *)src->pixels + (185 * src->pitch);
	const Uint8 * const src_pixel_ll = (Uint8 *)src->pixels;

	int w = vga_width * 185 - 1;

	for (int y = 185 - 1; y >= 0; --y)
	{
		dst_pixel -= (dst_pitch - vga_width);
		src_pixel -= (src_pitch - vga_width);

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

static void qa_scalar_water_filter(SDL_Surface *dst, SDL_Surface *src)
{
	Uint8 hue = smoothie_data[1] << 4;

	const int dst_pitch = dst->pitch;
	Uint8 *dst_pixel = (Uint8 *)dst->pixels + (185 * dst_pitch);

	const Uint8 *src_pixel = (Uint8 *)src->pixels + (185 * src->pitch);

	int w = vga_width * 185 - 1;

	for (int y = 185 - 1; y >= 0; --y)
	{
		dst_pixel -= (dst_pitch - vga_width);
		src_pixel -= (src->pitch - vga_width);

		for (int x = vga_width; x > 0; )
		{
			int waver = abs(((w >> 10) & 0x07) - 4) - 1;
			w -= 8;

			int count = MIN(8, x);
			x -= count;

			for (int xi = 0; xi < count; ++xi)
			{
				--dst_pixel;
				--src_pixel;

				if ((*src_pixel & 0x30) == 0)
				{
					*dst_pixel = *src_pixel;
				}
				else
				{
					Uint8 value = *src_pixel & 0x0f;
					value += *(dst_pixel + waver + dst_pitch) & 0x0f;
					*dst_pixel = (value / 2) | hue;
				}
			}
		}
	}
}

static void qa_scalar_average(SDL_Surface *dst, SDL_Surface *src, int scale, bool keep_hue)
{
	const int W = vga_width * scale;
	const int H = 184 * scale;

	Uint8 *dst_pixel = dst->pixels;
	const Uint8 *src_pixel = src->pixels;

	for (int y = 0; y < H; ++y)
	{
		for (int x = 0; x < W; ++x)
		{
			const Uint8 value = (*src_pixel & 0x0f) + (*dst_pixel & 0x0f);
			*dst_pixel = (value / 2) | (keep_hue ? (*src_pixel & 0xf0) : 0x80);

			++dst_pixel;
			++src_pixel;
		}

		dst_pixel += (dst->pitch - W);
		src_pixel += (src->pitch - W);
	}
}

static void qa_scalar_lava_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale)
{
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

			for (int x1 = vga_width - 1; x1 >= 0; --x1)
			{
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

static void qa_scalar_water_filter_scaled(SDL_Surface *dst, SDL_Surface *src, int scale)
{
	const Uint8 hue = smoothie_data[1] << 4;

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

			for (int x1 = vga_width - 1; x1 >= 0; --x1)
			{
				const int w1 = row1 + x1;
				const int waver = (abs(((w1 >> 10) & 0x07) - 4) - 1) * scale;

				for (int sub_x = scale - 1; sub_x >= 0; --sub_x)
				{
					const int x = x1 * scale + sub_x;
					if ((sp[x] & 0x30) == 0)
					{
						dp[x] = sp[x];
						continue;
					}

					int xs = x + waver;
					if (xs < 0)
						xs = 0;
					else if (xs >= W)
						xs = W - 1;

					Uint8 value = sp[x] & 0x0f;
					value += dp[xs + row_step] & 0x0f;
					dp[x] = (Uint8)((value / 2) | hue);
				}
			}
		}
	}
}

static void qa_scalar_spotlight_row(Uint8 *s, const Uint8 *src, int width, int y, int lightx,
                                    int lighty, int band, int scale)
{
	if (lighty > y)
	{
		for (int x = width; x; x--)
		{
			*s = (*src & 0xf0) | ((*src >> 2) & 0x03);
			s++;
			src++;
		}
	}
	else
	{
		for (int x = width; x; x--)
		{
			const int lightdist = abs(lightx - x) + lighty;
			if (lightdist < y)
				*s = *src;
			else if (lightdist - y <= band)
				*s = (*src & 0xf0) | (((*src & 0x0f) + (3 * (band - (lightdist - y))) / scale) / 4);
			else
				*s = (*src & 0xf0) | ((*src & 0x0f) >> 2);
			s++;
			src++;
		}
	}
}

static Uint32 qa_swar_state = 0x2545F491u;

static Uint32 qa_swar_next(void)
{
	qa_swar_state ^= qa_swar_state << 13;
	qa_swar_state ^= qa_swar_state >> 17;
	qa_swar_state ^= qa_swar_state << 5;
	return qa_swar_state;
}

static int qa_swar_range(int lo, int hi)
{
	return lo + (int)(qa_swar_next() % (Uint32)(hi - lo + 1));
}

static void qa_swar_fill(SDL_Surface *s)
{
	Uint8 *const p = s->pixels;
	for (size_t i = 0; i < (size_t)s->pitch * (size_t)s->h; ++i)
		p[i] = (Uint8)(qa_swar_next() >> 24);
}

static void qa_swar_copy(SDL_Surface *dst, const SDL_Surface *src)
{
	memcpy(dst->pixels, src->pixels, (size_t)src->pitch * (size_t)src->h);
}

static bool qa_swar_same(const SDL_Surface *a, const SDL_Surface *b)
{
	return memcmp(a->pixels, b->pixels, (size_t)a->pitch * (size_t)a->h) == 0;
}

static SDL_Surface *qa_swar_surface(int w, int h)
{
	return SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
}

enum { QA_SWAR_CELL_W = 32, QA_SWAR_CELL_ROWS = 40 };

static Uint8 qa_swar_tiles[48][QA_BG_TILE_W * 28];
static Uint8 *qa_swar_cells[QA_SWAR_CELL_W * QA_SWAR_CELL_ROWS];

static void qa_swar_fill_cells(int empty_pct)
{
	for (size_t i = 0; i < COUNTOF(qa_swar_cells); ++i)
	{
		const bool empty = (int)(qa_swar_next() % 100) < empty_pct;
		qa_swar_cells[i] = empty ? NULL : qa_swar_tiles[qa_swar_next() % COUNTOF(qa_swar_tiles)];
	}
}

static Uint8 **qa_swar_map(int mirror_w, int col0)
{
	const int r = qa_swar_range(1, QA_SWAR_CELL_ROWS - 2);
	Uint8 **const row = &qa_swar_cells[r * QA_SWAR_CELL_W + 8];
	return mirror_w ? row + col0 : row + qa_swar_range(-4, 4);
}

void qa_test_swar_pixel_loops(void)
{
	const bool saved_resim = rollback_resim_silent;
	const bool saved_recording = render_list_recording;
	const JE_boolean saved_filtration = filtrationAvail;
	const JE_boolean saved_transparent = explosionTransparent;
	const JE_byte saved_water_hue = smoothie_data[1];
	rollback_resim_silent = false;
	render_list_recording = false;

	for (size_t t = 0; t < COUNTOF(qa_swar_tiles); ++t)
		for (size_t i = 0; i < sizeof(qa_swar_tiles[t]); ++i)
			qa_swar_tiles[t][i] = (qa_swar_next() % 6 == 0) ? 0 : (Uint8)(qa_swar_next() >> 24);

	bool rows_copy = true, rows_blend = true, made = true;
	for (int trial = 0; trial < 1500 && made; ++trial)
	{
		const int h = (trial % 5 == 0) ? qa_swar_range(20, vga_height) : vga_height;
		SDL_Surface *const a = qa_swar_surface(vga_width, h);
		SDL_Surface *const b = qa_swar_surface(vga_width, h);
		made = a != NULL && b != NULL;
		if (made)
		{
			qa_swar_fill(a);
			qa_swar_copy(b, a);
			qa_swar_fill_cells(qa_swar_range(0, 95));
			const int mirror_w = (qa_swar_next() & 1) ? 14 : 0;
			const int col0 = mirror_w ? qa_swar_range(-3, 3) : 0;
			Uint8 **const map = qa_swar_map(mirror_w, col0);
			const int x = qa_swar_range(-40, 60);
			const int y = qa_swar_range(-60, h + 30);
			const bool blend = (qa_swar_next() & 1) != 0;

			qa_scalar_background_row(a, x, y, map, mirror_w, col0, blend);
			if (blend)
				blit_background_row_blend(b, x, y, map, mirror_w, col0);
			else
				blit_background_row(b, x, y, map, mirror_w, col0);

			if (!qa_swar_same(a, b))
			{
				if (blend)
					rows_blend = false;
				else
					rows_copy = false;
			}
		}
		SDL_FreeSurface(a);
		SDL_FreeSurface(b);
	}
	qa_check(made && rows_copy,
	         "SWAR background rows match the scalar copy for every clip and mirror case");
	qa_check(made && rows_blend,
	         "SWAR background rows match the scalar blend for every clip and mirror case");

	bool rows_scaled = true;
	for (int scale = 1; scale <= 5 && made; ++scale)
	{
		const int h = vga_height * scale;
		SDL_Surface *const a = qa_swar_surface(vga_width * scale, h);
		SDL_Surface *const b = qa_swar_surface(vga_width * scale, h);
		SDL_Surface *const c = qa_swar_surface(vga_width * scale, h / 3);
		SDL_Surface *const d = qa_swar_surface(vga_width * scale, h / 3);
		made = a != NULL && b != NULL && c != NULL && d != NULL;
		if (made)
		{
			qa_swar_fill(a);
			qa_swar_fill(c);
			for (int trial = 0; trial < 60; ++trial)
			{
				SDL_Surface *const ref = (trial % 4 == 0) ? c : a;
				SDL_Surface *const out = (trial % 4 == 0) ? d : b;
				qa_swar_copy(out, ref);
				qa_swar_fill_cells(qa_swar_range(0, 95));
				const int mirror_w = (qa_swar_next() & 1) ? 14 : 0;
				const int col0 = mirror_w ? qa_swar_range(-3, 3) : 0;
				Uint8 **const map = qa_swar_map(mirror_w, col0);
				const int x = qa_swar_range(-40 * scale, 60 * scale);
				const int y = qa_swar_range(-60 * scale, ref->h + 30 * scale);
				const bool blend = (qa_swar_next() & 1) != 0;

				qa_scalar_background_row_scaled(ref, x, y, map, scale, blend, mirror_w, col0);
				blit_background_row_scaled(out, x, y, map, scale, blend, mirror_w, col0);
				if (!qa_swar_same(ref, out))
					rows_scaled = false;
			}
		}
		SDL_FreeSurface(a);
		SDL_FreeSurface(b);
		SDL_FreeSurface(c);
		SDL_FreeSurface(d);
	}
	qa_check(made && rows_scaled,
	         "SWAR supersampled background rows match the scalar loop at 1x to 5x");

	static const JE_shortint hues[] = { -99, 0, 1, 7, 8, 15, 16, 20 };
	bool grade = true, grade_scaled = true;
	for (int scale = 1; scale <= 5 && made; ++scale)
	{
		SDL_Surface *const a = qa_swar_surface(vga_width * scale, vga_height * scale);
		SDL_Surface *const b = qa_swar_surface(vga_width * scale, vga_height * scale);
		made = a != NULL && b != NULL;
		if (made)
		{
			qa_swar_fill(a);
			for (size_t k = 0; k < COUNTOF(hues); ++k)
			{
				for (int brightness = -128; brightness <= 127; ++brightness)
				{
					if (scale > 1 && qa_swar_next() % 16 != 0)
						continue;

					filtrationAvail = qa_swar_next() % 8 != 0;
					explosionTransparent = qa_swar_next() % 8 != 0;
					const JE_shortint col = hues[k];
					const JE_shortint bright = (JE_shortint)(brightness == -128 ? -99 : brightness);

					qa_swar_copy(b, a);
					qa_scalar_filter_screen(a, col, bright, scale);
					if (scale == 1)
						JE_filterScreenApply(b, col, bright);
					else
						filter_screen_apply_scaled(b, col, bright, scale);

					if (!qa_swar_same(a, b))
					{
						if (scale == 1)
							grade = false;
						else
							grade_scaled = false;
					}
					if (qa_swar_next() % 4 == 0)
						qa_swar_fill(a);
				}
			}
		}
		SDL_FreeSurface(a);
		SDL_FreeSurface(b);
	}
	filtrationAvail = true;
	explosionTransparent = true;
	qa_check(made && grade, "SWAR full-screen grade matches the scalar loop for every brightness");
	qa_check(made && grade_scaled,
	         "SWAR supersampled full-screen grade matches the scalar loop at 2x to 5x");

	static const char *const filter_names[] = { "lava", "water", "iced blur", "blur" };
	for (int f = 0; f < 4 && made; ++f)
	{
		bool same = true;
		for (int scale = 1; scale <= 5 && made; ++scale)
		{
			const int w = vga_width * scale, h = vga_height * scale;
			SDL_Surface *const s1 = qa_swar_surface(w, h);
			SDL_Surface *const d1 = qa_swar_surface(w, h);
			SDL_Surface *const s2 = qa_swar_surface(w, h);
			SDL_Surface *const d2 = qa_swar_surface(w, h);
			made = s1 != NULL && d1 != NULL && s2 != NULL && d2 != NULL;
			for (int trial = 0; made && trial < (scale == 1 ? 24 : 6); ++trial)
			{
				smoothie_data[1] = (JE_byte)(qa_swar_next() >> 24);
				qa_swar_fill(s1);
				qa_swar_fill(d1);
				qa_swar_copy(s2, s1);
				qa_swar_copy(d2, d1);

				const bool in_place = trial % 3 == 0;
				SDL_Surface *const src1 = in_place ? d1 : s1;
				SDL_Surface *const src2 = in_place ? d2 : s2;
				for (int pass = 0; pass < 2; ++pass)
				{
					const bool hi = scale > 1 || pass == 1;
					switch (f)
					{
					case 0:
						if (hi)
						{
							qa_scalar_lava_filter_scaled(d1, src1, scale);
							lava_filter_scaled(d2, src2, scale);
						}
						else
						{
							qa_scalar_lava_filter(d1, src1);
							lava_filter(d2, src2);
						}
						break;
					case 1:
						if (hi)
						{
							qa_scalar_water_filter_scaled(d1, src1, scale);
							water_filter_scaled(d2, src2, scale);
						}
						else
						{
							qa_scalar_water_filter(d1, src1);
							water_filter(d2, src2);
						}
						break;
					case 2:
						qa_scalar_average(d1, src1, scale, false);
						if (hi)
							iced_blur_filter_scaled(d2, src2, scale);
						else
							iced_blur_filter(d2, src2);
						break;
					default:
						qa_scalar_average(d1, src1, scale, true);
						if (hi)
							blur_filter_scaled(d2, src2, scale);
						else
							blur_filter(d2, src2);
						break;
					}
					if (!qa_swar_same(d1, d2) || !qa_swar_same(s1, s2))
						same = false;
				}
			}
			SDL_FreeSurface(s1);
			SDL_FreeSurface(d1);
			SDL_FreeSurface(s2);
			SDL_FreeSurface(d2);
		}

		char what[96];
		snprintf(what, sizeof(what),
		         "SWAR %s filter matches the scalar loop at 1x to 5x, in place too",
		         filter_names[f]);
		qa_check(made && same, what);
	}
	smoothie_data[1] = saved_water_hue;

	bool spotlight = true;
	for (int scale = 1; scale <= 4; ++scale)
	{
		const int width = PLAYFIELD_WIDTH * scale;
		Uint8 src[PLAYFIELD_WIDTH * 4], ref[PLAYFIELD_WIDTH * 4], out[PLAYFIELD_WIDTH * 4];
		for (int trial = 0; trial < 3000; ++trial)
		{
			for (int i = 0; i < width; ++i)
			{
				src[i] = (Uint8)(qa_swar_next() >> 24);
				ref[i] = (Uint8)(qa_swar_next() >> 24);
			}
			memcpy(out, ref, (size_t)width);
			const int y = qa_swar_range(1, 184 * scale);
			const int lightx = qa_swar_range(-60 * scale, 420 * scale);
			const int lighty = qa_swar_range(-40 * scale, 260 * scale);

			qa_scalar_spotlight_row(ref, src, width, y, lightx, lighty, 5 * scale, scale);
			spotlight_filter_row(out, src, width, y, lightx, lighty, 5 * scale, scale);
			if (memcmp(ref, out, (size_t)width) != 0)
				spotlight = false;
		}
	}
	qa_check(spotlight, "SWAR spotlight rows match the scalar composite at 1x to 4x");

	filtrationAvail = saved_filtration;
	explosionTransparent = saved_transparent;
	rollback_resim_silent = saved_resim;
	render_list_recording = saved_recording;
}

typedef struct
{
	SDL_Surface *dst;
	const BgRowDraw *rows;
	size_t count;
	int scale;
} QaBgBandJob;

static void qa_bg_band_rows(void *p, int row_begin, int row_end)
{
	const QaBgBandJob *const job = p;
	SDL_Surface band = *job->dst;
	band.pixels = (Uint8 *)job->dst->pixels + (size_t)row_begin * job->dst->pitch;
	band.h = row_end - row_begin;
	blit_background_row_scaled_batch(&band, job->rows, job->count, row_begin, job->scale);
}

void qa_test_background_bands(void)
{
	for (size_t t = 0; t < COUNTOF(qa_swar_tiles); ++t)
		for (size_t i = 0; i < sizeof(qa_swar_tiles[t]); ++i)
			qa_swar_tiles[t][i] = (qa_swar_next() % 6 == 0) ? 0 : (Uint8)(qa_swar_next() >> 24);

	BgRowDraw rows[12];
	bool same = true, made = true;
	for (int trial = 0; trial < 60 && same && made; ++trial)
	{
		const int scale = 2 + trial % 4;
		const int w = vga_width * scale, h = vga_height * scale;
		SDL_Surface *const a = qa_swar_surface(w, h);
		SDL_Surface *const b = qa_swar_surface(w, h);
		made = a != NULL && b != NULL;
		if (made)
		{
			qa_swar_fill(a);
			qa_swar_copy(b, a);
			qa_swar_fill_cells(qa_swar_range(0, 60));
			const size_t count = (size_t)qa_swar_range(1, (int)COUNTOF(rows));
			for (size_t r = 0; r < count; ++r)
			{
				const int mirror_w = (qa_swar_next() & 1) ? 14 : 0;
				const int col0 = mirror_w ? qa_swar_range(-3, 3) : 0;
				rows[r].map = qa_swar_map(mirror_w, col0);
				rows[r].x = qa_swar_range(-40 * scale, 60 * scale);
				rows[r].y = qa_swar_range(-60 * scale, h + 30 * scale);
				rows[r].blend = (qa_swar_next() & 1) != 0;
				rows[r].mirror_w = mirror_w;
				rows[r].col0 = col0;
			}

			for (size_t r = 0; r < count; ++r)
				blit_background_row_scaled(a, rows[r].x, rows[r].y, rows[r].map, scale,
				                           rows[r].blend, rows[r].mirror_w, rows[r].col0);
			QaBgBandJob job = { b, rows, count, scale };
			video_rows_threaded(qa_bg_band_rows, &job, h);
			same = qa_swar_same(a, b);
		}
		SDL_FreeSurface(a);
		SDL_FreeSurface(b);
	}
	qa_check(made && same, "background rows drawn in thread bands match one full-surface pass");
}

typedef struct
{
	int filter;
	SDL_Surface *dst, *src;
	int scale;
} QaFilterRowsJob;

static void qa_filter_subrows(int filter, SDL_Surface *dst, SDL_Surface *src, int scale,
                              int sub_first, int sub_last)
{
	switch (filter)
	{
	case 0:
		lava_filter_scaled_subrows(dst, src, scale, sub_first, sub_last);
		break;
	case 1:
		water_filter_scaled_subrows(dst, src, scale, sub_first, sub_last);
		break;
	case 2:
		iced_blur_filter_scaled_subrows(dst, src, scale, sub_first, sub_last);
		break;
	default:
		blur_filter_scaled_subrows(dst, src, scale, sub_first, sub_last);
		break;
	}
}

static void qa_filter_rows_job(void *p, int sub_first, int sub_last)
{
	const QaFilterRowsJob *const job = p;
	qa_filter_subrows(job->filter, job->dst, job->src, job->scale, sub_first, sub_last);
}

void qa_test_filter_subrows(void)
{
	const JE_byte saved_water_hue = smoothie_data[1];
	bool made = true, any_order = true, threaded = true;
	for (int filter = 0; filter < 4 && made; ++filter)
	{
		for (int scale = 2; scale <= 5 && made; ++scale)
		{
			const int w = vga_width * scale, h = vga_height * scale;
			SDL_Surface *const s1 = qa_swar_surface(w, h);
			SDL_Surface *const d1 = qa_swar_surface(w, h);
			SDL_Surface *const s2 = qa_swar_surface(w, h);
			SDL_Surface *const d2 = qa_swar_surface(w, h);
			SDL_Surface *const d3 = qa_swar_surface(w, h);
			made = s1 != NULL && d1 != NULL && s2 != NULL && d2 != NULL && d3 != NULL;
			for (int trial = 0; made && trial < 4; ++trial)
			{
				smoothie_data[1] = (JE_byte)(qa_swar_next() >> 24);
				qa_swar_fill(s1);
				qa_swar_fill(d1);
				qa_swar_copy(s2, s1);
				qa_swar_copy(d2, d1);
				qa_swar_copy(d3, d1);

				const bool in_place = trial % 2 == 1;
				SDL_Surface *const src1 = in_place ? d1 : s1;
				switch (filter)
				{
				case 0:
					qa_scalar_lava_filter_scaled(d1, src1, scale);
					break;
				case 1:
					qa_scalar_water_filter_scaled(d1, src1, scale);
					break;
				default:
					qa_scalar_average(d1, src1, scale, filter == 3);
					break;
				}

				for (int j = 0; j < scale; ++j)
				{
					const int sub = (scale - 1 - j + trial) % scale;
					qa_filter_subrows(filter, d2, in_place ? d2 : s2, scale, sub, sub + 1);
				}
				QaFilterRowsJob job = { filter, d3, in_place ? d3 : s2, scale };
				video_rows_threaded(qa_filter_rows_job, &job, scale);

				if (!qa_swar_same(d1, d2))
					any_order = false;
				if (!qa_swar_same(d1, d3))
					threaded = false;
			}
			SDL_FreeSurface(s1);
			SDL_FreeSurface(d1);
			SDL_FreeSurface(s2);
			SDL_FreeSurface(d2);
			SDL_FreeSurface(d3);
		}
	}
	smoothie_data[1] = saved_water_hue;
	qa_check(made && any_order,
	         "scaled smoothie filters give the same frame one sub-row set at a time, in any order");
	qa_check(made && threaded,
	         "scaled smoothie filters split by sub-row set across threads match one pass");
}

typedef struct
{
	int what;
	int x, y;
	int a, b;
	Uint8 **map;
	int mirror_w, col0;
} QaFlatDraw;

enum { QA_FLAT_KINDS = 21, QA_FLAT_TABLE = EXTRA_SHAPES, QA_FLAT_INDEX = 0 };

static void qa_flat_draw(SDL_Surface *s, const QaFlatDraw *d, Sprite2_array sheet, int y_offset,
                         bool band)
{
	const int x = d->x, y = d->y - y_offset;
	const Uint8 hue = (Uint8)(d->a & 0x0f);
	const Sint8 value = (Sint8)(d->b % 31 - 15);
	const int bank = (d->b & 1) ? -1 : hue;
	const Uint8 opacity = (Uint8)(d->b % 17);
	const JE_shortint grade_col = (JE_shortint)(d->a % 9 == 0 ? -99 : d->a % 16);
	const JE_shortint grade_bright = (JE_shortint)(d->b % 11 == 0 ? -99 : d->b % 40 - 20);
	switch (d->what)
	{
	case 0:  blit_sprite2_clip(s, x, y, sheet, 1); break;
	case 1:  blit_sprite2_blend_clip(s, x, y, sheet, 1); break;
	case 2:  blit_sprite2_darken_clip(s, x, y, sheet, 1); break;
	case 3:  blit_sprite2_solid_clip(s, x, y, sheet, 1, (Uint8)d->a); break;
	case 4:  blit_sprite2_filter(s, x, y, sheet, 1, (Uint8)(d->a & 0xf0)); break;
	case 5:  blit_sprite2_filter_clip(s, x, y, sheet, 1, (Uint8)(d->a & 0xf0)); break;
	case 6:  blit_sprite2_filter_bright_clip(s, x, y, sheet, 1, (Uint8)d->a); break;
	case 7:  blit_sprite2_blend_filter_clip(s, x, y, sheet, 1, (Uint8)d->a); break;
	case 8:  blit_sprite2_alpha_clip(s, x, y, sheet, 1, bank, opacity); break;
	case 9:  blit_sprite(s, x, y, QA_FLAT_TABLE, QA_FLAT_INDEX); break;
	case 10: blit_sprite_blend(s, x, y, QA_FLAT_TABLE, QA_FLAT_INDEX); break;
	case 11: blit_sprite_hv(s, x, y, QA_FLAT_TABLE, QA_FLAT_INDEX, hue, value); break;
	case 12: blit_sprite_hv_blend(s, x, y, QA_FLAT_TABLE, QA_FLAT_INDEX, hue, value); break;
	case 13: blit_sprite_hv_unsafe(s, x, y, QA_FLAT_TABLE, QA_FLAT_INDEX, hue, value); break;
	case 14: blit_sprite_dark(s, x, y, QA_FLAT_TABLE, QA_FLAT_INDEX, (d->b & 1) != 0); break;
	case 15: blit_sprite_alpha(s, x, y, QA_FLAT_TABLE, QA_FLAT_INDEX, bank, opacity); break;
	case 16: blit_background_row(s, x, y, d->map, d->mirror_w, d->col0); break;
	case 17: blit_background_row_blend(s, x, y, d->map, d->mirror_w, d->col0); break;
	case 18:
		rl_draw_hp_bar(s, x, y, d->a % 40 + 1,
		               (d->b % 45) * RL_BAR_FILL_ONE + d->a % RL_BAR_FILL_ONE, (Uint8)d->a,
		               (d->b & 2) != 0, (Uint8)(d->b * 37), (Uint8)(d->a >> 1));
		break;
	case 19:
		if (band)
			draw_starfield_star_band(s, x, d->y, (Uint8)d->a, 1, y_offset);
		else
			draw_starfield_star(s, x, d->y, (Uint8)d->a);
		break;
	default:
		if (band)
			filter_screen_apply_scaled_band(s, grade_col, grade_bright, 1, y_offset);
		else
			JE_filterScreenApply(s, grade_col, grade_bright);
		break;
	}
}

void qa_test_flat_bands(void)
{
	const bool saved_resim = rollback_resim_silent;
	const bool saved_recording = render_list_recording;
	const JE_boolean saved_filtration = filtrationAvail;
	const JE_boolean saved_transparent = explosionTransparent;
	SDL_Surface *const saved_vga = VGAScreen;
	rollback_resim_silent = false;
	render_list_recording = false;
	filtrationAvail = true;
	explosionTransparent = true;

	static const Uint8 rle[] = {
		0x52, 0x21, 0x22, 0x23, 0x24, 0x25, 0x31, 0x96, 0x97, 0x98, 0x01,
		0xc0, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x00,
		0x16, 0xe1, 0x05,
		0x40, 0x41, 0x42, 0x43, 0x44, 0x44, 0x5a, 0x5b, 0x5c, 0x5d, 0x00,
		0x2a, 0x77, 0x78, 0x00,
		0x0f,
	};
	static Uint16 sheet_words[(2 + sizeof(rle) + 1) / 2];
	Uint8 *const sheet_bytes = (Uint8 *)sheet_words;
	sheet_bytes[0] = 2;
	sheet_bytes[1] = 0;
	memcpy(sheet_bytes + 2, rle, sizeof(rle));
	const Sprite2_array sheet = { 2 + sizeof(rle), sheet_bytes };

	static Uint8 table_pixels[] = {
		0x21, 0x22, 253, 0x23, 255, 2, 0x24, 0x25,
		254,
		0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
		255, 5, 0x41, 0x42, 0x43,
		0x51, 254,
	};
	const Sprite saved_sprite = sprite_table[QA_FLAT_TABLE].sprite[QA_FLAT_INDEX];
	const unsigned int saved_count = sprite_table[QA_FLAT_TABLE].count;
	sprite_table[QA_FLAT_TABLE].sprite[QA_FLAT_INDEX] =
	    (Sprite){ 8, 5, (Uint16)sizeof(table_pixels), table_pixels };
	if (sprite_table[QA_FLAT_TABLE].count <= QA_FLAT_INDEX)
		sprite_table[QA_FLAT_TABLE].count = QA_FLAT_INDEX + 1;

	for (size_t t = 0; t < COUNTOF(qa_swar_tiles); ++t)
		for (size_t i = 0; i < sizeof(qa_swar_tiles[t]); ++i)
			qa_swar_tiles[t][i] = (qa_swar_next() % 6 == 0) ? 0 : (Uint8)(qa_swar_next() >> 24);

	enum { W = vga_width, H = vga_height, PITCH = W + 12 };
	static Uint8 buf_a[PITCH * H], buf_b[PITCH * H];
	SDL_Surface *const a = SDL_CreateRGBSurfaceFrom(buf_a, W, H, 8, PITCH, 0, 0, 0, 0);
	SDL_Surface *const b = SDL_CreateRGBSurfaceFrom(buf_b, W, H, 8, PITCH, 0, 0, 0, 0);
	bool same = a != NULL && b != NULL;
	VGAScreen = a;

	QaFlatDraw draws[16];
	for (int trial = 0; trial < 600 && same; ++trial)
	{
		for (size_t i = 0; i < sizeof(buf_a); ++i)
			buf_a[i] = buf_b[i] = (qa_swar_next() % 3 == 0) ? 0 : (Uint8)(qa_swar_next() >> 24);
		qa_swar_fill_cells(qa_swar_range(0, 60));

		const int count = qa_swar_range(1, (int)COUNTOF(draws));
		for (int k = 0; k < count; ++k)
		{
			QaFlatDraw *const d = &draws[k];
			d->what = (trial * 7 + k) % QA_FLAT_KINDS;
			d->x = qa_swar_range(-30, W + 10);
			d->y = qa_swar_range(-35, H + 5);
			d->a = qa_swar_range(0, 255);
			d->b = qa_swar_range(0, 255);
			d->mirror_w = (qa_swar_next() & 1) ? 14 : 0;
			d->col0 = d->mirror_w ? qa_swar_range(-3, 3) : 0;
			d->map = qa_swar_map(d->mirror_w, d->col0);
		}

		int cuts[6];
		const int bands = qa_swar_range(2, 5);
		cuts[0] = 0;
		cuts[bands] = H;
		for (int k = 1; k < bands; ++k)
			cuts[k] = qa_swar_range(1, H - 1);
		for (int k = 2; k < bands; ++k)
		{
			for (int j = k; j > 1 && cuts[j - 1] > cuts[j]; --j)
			{
				const int t = cuts[j];
				cuts[j] = cuts[j - 1];
				cuts[j - 1] = t;
			}
		}

		for (int k = 0; k < count; ++k)
			qa_flat_draw(a, &draws[k], sheet, 0, false);
		for (int band = 0; band < bands; ++band)
		{
			SDL_Surface view = *b;
			view.pixels = buf_b + (size_t)cuts[band] * PITCH;
			view.h = cuts[band + 1] - cuts[band];
			view.clip_rect = (SDL_Rect){ 0, 0, view.w, view.h };
			for (int k = 0; k < count; ++k)
				qa_flat_draw(&view, &draws[k], sheet, cuts[band], true);
		}
		same = memcmp(buf_a, buf_b, sizeof(buf_a)) == 0;
	}

	SDL_FreeSurface(a);
	SDL_FreeSurface(b);
	VGAScreen = saved_vga;
	sprite_table[QA_FLAT_TABLE].sprite[QA_FLAT_INDEX] = saved_sprite;
	sprite_table[QA_FLAT_TABLE].count = saved_count;
	filtrationAvail = saved_filtration;
	explosionTransparent = saved_transparent;
	rollback_resim_silent = saved_resim;
	render_list_recording = saved_recording;
	qa_check(same, "1x replay drawers split into row bands match one full-surface pass");

	char detail[160] = "";
	const bool sparks = rl_test_superpixel_bands(detail, sizeof(detail));
	char label[256];
	snprintf(label, sizeof(label),
	         "explosion sparks split into row bands match one pass at 1x to 4x%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(sparks, label);

	detail[0] = '\0';
	const bool bars = rl_test_hp_bar_fill(detail, sizeof(detail));
	snprintf(label, sizeof(label),
	         "enemy health bars fill to the sub-pixel at 1x to 4x and carry their fill between "
	         "ticks%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(bars, label);
}
