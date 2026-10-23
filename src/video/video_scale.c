/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2010  The OpenTyrian Development Team
 * 
 * Scale2x, Scale3x
 * Copyright (C) 2001, 2002, 2003, 2004 Andrea Mazzoleni
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
#include "video_scale.h"

#include "palette.h"
#include "video.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void nn_32(SDL_Surface *src_surface, SDL_Texture *dst_texture);
static void nn_16(SDL_Surface *src_surface, SDL_Texture *dst_texture);

static void nn_fit_32(SDL_Surface *src_surface, SDL_Texture *dst_texture);
static void nn_fit_16(SDL_Surface *src_surface, SDL_Texture *dst_texture);

static void scale2x_32(SDL_Surface *src_surface, SDL_Texture *dst_texture);
static void scale2x_16(SDL_Surface *src_surface, SDL_Texture *dst_texture);
static void scale3x_32(SDL_Surface *src_surface, SDL_Texture *dst_texture);
static void scale3x_16(SDL_Surface *src_surface, SDL_Texture *dst_texture);

/* Cache native-scaling source columns to avoid repeated per-pixel divisions. */
static int *nn_fit_x;
static int nn_fit_x_cap;
static int nn_fit_src_w;
static int nn_fit_dst_w;

static const int *nn_fit_x_map(int src_w, int dst_w)
{
	if (src_w == nn_fit_src_w && dst_w == nn_fit_dst_w)
		return nn_fit_x;

	if (dst_w > nn_fit_x_cap)
	{
		int *grown = realloc(nn_fit_x, (size_t)dst_w * sizeof(*grown));
		if (grown == NULL)
			return NULL;
		nn_fit_x = grown;
		nn_fit_x_cap = dst_w;
	}

	for (int x = 0; x < dst_w; ++x)
		nn_fit_x[x] = (int)((Sint64)x * src_w / dst_w);

	nn_fit_src_w = src_w;
	nn_fit_dst_w = dst_w;
	return nn_fit_x;
}

void video_scale_deinit(void)
{
	free(nn_fit_x);
	nn_fit_x = NULL;
	nn_fit_x_cap = 0;
	nn_fit_src_w = 0;
	nn_fit_dst_w = 0;
}

void hq2x_32(SDL_Surface *src_surface, SDL_Texture *dst_texture);
void hq3x_32(SDL_Surface *src_surface, SDL_Texture *dst_texture);
void hq4x_32(SDL_Surface *src_surface, SDL_Texture *dst_texture);

uint scaler;

struct Scalers scalers[] =
{
	{ 1 * vga_width, 1 * vga_height, nn_16,      nn_32,      "None" },
	{ 2 * vga_width, 2 * vga_height, nn_16,      nn_32,      "2x" },
	{ 2 * vga_width, 2 * vga_height, scale2x_16, scale2x_32, "Scale2x" },
	{ 2 * vga_width, 2 * vga_height, NULL,       hq2x_32,    "hq2x" },
	{ 3 * vga_width, 3 * vga_height, nn_16,      nn_32,      "3x" },
	{ 3 * vga_width, 3 * vga_height, scale3x_16, scale3x_32, "Scale3x" },
	{ 3 * vga_width, 3 * vga_height, NULL,       hq3x_32,    "hq3x" },
	{ 4 * vga_width, 4 * vga_height, nn_16,      nn_32,      "4x" },
	{ 4 * vga_width, 4 * vga_height, NULL,       hq4x_32,    "hq4x" },
	{ 5 * vga_width, 5 * vga_height, nn_16,      nn_32,      "5x" },
	// Native maps one texel to each output pixel; video.c supplies the live size.
	// Keep it last because scaler_plain_equivalent matches entries by width.
	{ 1 * vga_width, 1 * vga_height, nn_fit_16,  nn_fit_32,  "Native" },
};
const uint scalers_count = COUNTOF(scalers);

void set_scaler_by_name(const char *name)
{
	for (uint i = 0; i < scalers_count; ++i)
	{
		if (strcmp(name, scalers[i].name) == 0)
		{
			scaler = i;
			break;
		}
	}
}

// Plain scalers only enlarge pixels (nearest-neighbour: None/2x/3x/4x/Native); the rest
// (Scale2x, hqNx) are pixel-art reconstruction algorithms.
bool scaler_is_plain(uint index)
{
	return index < scalers_count
	    && (scalers[index].scaler32 == nn_32 || scalers[index].scaler32 == nn_fit_32);
}

bool scaler_is_native(uint index)
{
	return index < scalers_count && scalers[index].scaler32 == nn_fit_32;
}

// Refresh the Native entry's output dimensions (video.c computes them from the
// live window size and scaling mode whenever they are about to be used).
void scaler_set_native_size(int w, int h)
{
	for (uint i = 0; i < scalers_count; ++i)
	{
		if (scalers[i].scaler32 == nn_fit_32)
		{
			scalers[i].width = w;
			scalers[i].height = h;
		}
	}
}

// The same-size plain scaler (hq2x -> 2x); returns `index` itself if already plain.
uint scaler_plain_equivalent(uint index)
{
	if (scaler_is_plain(index) || index >= scalers_count)
		return index;
	for (uint i = 0; i < scalers_count; ++i)
	{
		if (scalers[i].width == scalers[index].width && scaler_is_plain(i))
			return i;
	}
	return index;  // no plain scaler of that size (cannot happen with the current table)
}

typedef struct
{
	const Uint8 *src;
	int src_pitch;
	Uint8 *dst;
	int dst_pitch;
	int width, scale;
	size_t row_bytes;
} NnRows;

static void nn_rows_32(void *p, int row_begin, int row_end)
{
	const NnRows *const job = p;
	const int width = job->width, scale = job->scale;

	for (int y = row_begin; y < row_end; ++y)
	{
		const Uint8 *const src = job->src + (size_t)y * job->src_pitch;
		Uint8 *const row = job->dst + (size_t)y * scale * job->dst_pitch;
		Uint32 *dst = (Uint32 *)row;

		switch (scale)
		{
		case 2:
			for (int x = 0; x < width; ++x, dst += 2)
			{
				const Uint32 color = rgb_palette[src[x]];
				dst[0] = color;
				dst[1] = color;
			}
			break;
		case 3:
			for (int x = 0; x < width; ++x, dst += 3)
			{
				const Uint32 color = rgb_palette[src[x]];
				dst[0] = color;
				dst[1] = color;
				dst[2] = color;
			}
			break;
		case 4:
			for (int x = 0; x < width; ++x, dst += 4)
			{
				const Uint32 color = rgb_palette[src[x]];
				dst[0] = color;
				dst[1] = color;
				dst[2] = color;
				dst[3] = color;
			}
			break;
		default:
			for (int x = 0; x < width; ++x)
			{
				const Uint32 color = rgb_palette[src[x]];
				for (int k = 0; k < scale; ++k)
					*dst++ = color;
			}
			break;
		}

		for (int k = 1; k < scale; ++k)
			memcpy(row + (size_t)k * job->dst_pitch, row, job->row_bytes);
	}
}

void nn_32(SDL_Surface *src_surface, SDL_Texture *dst_texture)
{
	int dst_width, dst_height;
	SDL_QueryTexture(dst_texture, NULL, NULL, &dst_width, &dst_height);

	const int height = vga_height, // src_surface->h
	          width = vga_width,   // src_surface->w
	          scale = dst_width / width;
	assert(scale == dst_height / height);

	void *tmp_ptr;
	int dst_pitch;
	SDL_LockTexture(dst_texture, NULL, &tmp_ptr, &dst_pitch);

	if (scale == 1)
	{
		video_palette_rows(src_surface->pixels, src_surface->pitch, tmp_ptr, dst_pitch, width,
		                   height, dst_texture);
		return;
	}

	const NnRows job = { src_surface->pixels, src_surface->pitch, tmp_ptr, dst_pitch, width, scale,
	                     (size_t)dst_width * 4 };
	video_rows(VIDEO_ROWS_FLAT, nn_rows_32, (void *)&job, height, dst_width * dst_height,
	           dst_texture);
}

void nn_16(SDL_Surface *src_surface, SDL_Texture *dst_texture)
{
	Uint8 *src = src_surface->pixels, *src_temp;
	Uint8 *dst, *dst_temp;

	int src_pitch = src_surface->pitch;
	int dst_pitch;

	const int dst_Bpp = 2;         // dst_surface->format->BytesPerPixel
	int dst_width, dst_height;
	SDL_QueryTexture(dst_texture, NULL, NULL, &dst_width, &dst_height);
	
	const int height = vga_height, // src_surface->h
	          width = vga_width,   // src_surface->w
	          scale = dst_width / width;
	assert(scale == dst_height / height);

	void* tmp_ptr;
	SDL_LockTexture(dst_texture, NULL, &tmp_ptr, &dst_pitch);
	dst = tmp_ptr;
	
	for (int y = height; y > 0; y--)
	{
		src_temp = src;
		dst_temp = dst;
		
		for (int x = width; x > 0; x--)
		{
			const Uint16 color = (Uint16)rgb_palette[*src];
			for (int z = scale; z > 0; z--)
			{
				*(Uint16 *)dst = color;
				dst += dst_Bpp;
			}
			src++;
		}
		
		src = src_temp + src_pitch;
		dst = dst_temp + dst_pitch;
		
		for (int z = scale; z > 1; z--)
		{
			memcpy(dst, dst_temp, dst_width * dst_Bpp);
			dst += dst_pitch;
		}
	}

	SDL_UnlockTexture(dst_texture);
}

// Native nearest-neighbor samples exact output dimensions and reuses repeated rows.
typedef struct
{
	const Uint8 *src;
	int src_pitch, src_w, src_h;
	Uint8 *dst;
	int dst_pitch, dst_w, dst_h;
	const int *x_map;
} FitRows;

static void fit_rows_32(void *p, int row_begin, int row_end)
{
	const FitRows *const job = p;
	Uint8 *dst = job->dst + (size_t)row_begin * job->dst_pitch;
	const Uint8 *prev_row = NULL;
	int prev_sy = -1;

	for (int y = row_begin; y < row_end; ++y)
	{
		const int sy = (int)((Sint64)y * job->src_h / job->dst_h);
		if (sy == prev_sy)
		{
			memcpy(dst, prev_row, (size_t)job->dst_w * 4);
		}
		else
		{
			const Uint8 *src_row = job->src + (size_t)sy * job->src_pitch;
			Uint32 *out = (Uint32 *)dst;
			for (int x = 0; x < job->dst_w; ++x)
			{
				const int sx = job->x_map != NULL ? job->x_map[x]
				                                  : (int)((Sint64)x * job->src_w / job->dst_w);
				out[x] = rgb_palette[src_row[sx]];
			}
			prev_sy = sy;
			prev_row = dst;
		}
		dst += job->dst_pitch;
	}
}

static void fit_rows_16(void *p, int row_begin, int row_end)
{
	const FitRows *const job = p;
	Uint8 *dst = job->dst + (size_t)row_begin * job->dst_pitch;
	const Uint8 *prev_row = NULL;
	int prev_sy = -1;

	for (int y = row_begin; y < row_end; ++y)
	{
		const int sy = (int)((Sint64)y * job->src_h / job->dst_h);
		if (sy == prev_sy)
		{
			memcpy(dst, prev_row, (size_t)job->dst_w * 2);
		}
		else
		{
			const Uint8 *src_row = job->src + (size_t)sy * job->src_pitch;
			Uint16 *out = (Uint16 *)dst;
			for (int x = 0; x < job->dst_w; ++x)
			{
				const int sx = job->x_map != NULL ? job->x_map[x]
				                                  : (int)((Sint64)x * job->src_w / job->dst_w);
				out[x] = (Uint16)rgb_palette[src_row[sx]];
			}
			prev_sy = sy;
			prev_row = dst;
		}
		dst += job->dst_pitch;
	}
}

static void nn_fit(SDL_Surface *src_surface, SDL_Texture *dst_texture, VideoRowsFn rows_fn)
{
	int dst_width, dst_height, dst_pitch;
	SDL_QueryTexture(dst_texture, NULL, NULL, &dst_width, &dst_height);

	const int *const x_map = nn_fit_x_map(src_surface->w, dst_width);

	void *tmp_ptr;
	SDL_LockTexture(dst_texture, NULL, &tmp_ptr, &dst_pitch);

	const FitRows job = { src_surface->pixels, src_surface->pitch, src_surface->w, src_surface->h,
	                      tmp_ptr, dst_pitch, dst_width, dst_height, x_map };
	video_rows(VIDEO_ROWS_FIT, rows_fn, (void *)&job, dst_height, dst_width * dst_height,
	           dst_texture);
}

void nn_fit_32(SDL_Surface *src_surface, SDL_Texture *dst_texture)
{
	nn_fit(src_surface, dst_texture, fit_rows_32);
}

void nn_fit_16(SDL_Surface *src_surface, SDL_Texture *dst_texture)
{
	nn_fit(src_surface, dst_texture, fit_rows_16);
}

bool video_scale_test_fit_rows(char *detail, size_t detailSize)
{
	enum { SRC_W = 356, SRC_H = 200 };
	static const int dims[][2] = { { 960, 544 }, { 1000, 563 }, { 356, 200 }, { 7, 3 } };
	static Uint8 src[SRC_W * SRC_H];
	const char *fault = NULL;
	Uint32 seed = 0xf17f17u;

	for (int i = 0; i < SRC_W * SRC_H; ++i)
	{
		seed = seed * 1664525u + 1013904223u;
		src[i] = (Uint8)(seed >> 24);
	}

	for (size_t k = 0; k < COUNTOF(dims) && fault == NULL; ++k)
	{
		for (int bytes = 2; bytes <= 4 && fault == NULL; bytes += 2)
		{
			const int w = dims[k][0], h = dims[k][1], pitch = w * bytes + 8;
			Uint8 *const a = malloc((size_t)pitch * h);
			Uint8 *const b = malloc((size_t)pitch * h);
			if (a == NULL || b == NULL)
			{
				fault = "allocation failed";
			}
			else
			{
				memset(a, 0xcd, (size_t)pitch * h);
				memset(b, 0xcd, (size_t)pitch * h);
				const int *const x_map = nn_fit_x_map(SRC_W, w);
				FitRows one = { src, SRC_W, SRC_W, SRC_H, a, pitch, w, h, x_map };
				FitRows many = { src, SRC_W, SRC_W, SRC_H, b, pitch, w, h, x_map };
				const VideoRowsFn fn = bytes == 4 ? fit_rows_32 : fit_rows_16;
				fn(&one, 0, h);
				video_rows_threaded(fn, &many, h);
				for (int y = 0; y < h && fault == NULL; ++y)
				{
					const Uint8 *const row = a + (size_t)y * pitch;
					const Uint8 *const src_row = src + (size_t)((Sint64)y * SRC_H / h) * SRC_W;
					for (int x = 0; x < w && fault == NULL; ++x)
					{
						const Uint32 want = rgb_palette[src_row[(Sint64)x * SRC_W / w]];
						const Uint32 got = bytes == 4 ? ((const Uint32 *)row)[x]
						                              : ((const Uint16 *)row)[x];
						if (got != (bytes == 4 ? want : (Uint16)want))
							fault = "Native scaler rows differ from a direct lookup";
					}
				}
				if (fault == NULL && memcmp(a, b, (size_t)pitch * h) != 0)
					fault = "threaded Native scaler rows differ from one thread";
			}
			free(a);
			free(b);
		}
	}

	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

bool video_scale_test_nn_rows(char *detail, size_t detailSize)
{
	enum { SRC_PITCH = vga_width + 8 };
	static Uint8 src_pixels[SRC_PITCH * vga_height];
	const char *fault = NULL;
	Uint32 seed = 0x5ca1e5u;

	for (size_t i = 0; i < sizeof(src_pixels); ++i)
	{
		seed = seed * 1664525u + 1013904223u;
		src_pixels[i] = (Uint8)(seed >> 24);
	}
	SDL_Surface *const src = SDL_CreateRGBSurfaceFrom(src_pixels, vga_width, vga_height, 8,
	                                                  SRC_PITCH, 0, 0, 0, 0);
	if (src == NULL)
		fault = "could not wrap the source frame";

	const int saved_threads = render_threads;
	char message[96];
	for (int run = 0; run < 10 && fault == NULL; ++run)
	{
		const int scale = 1 + run / 2;
		const bool pool = (run & 1) != 0;
		render_threads = pool ? video_rows_max_threads() : 1;
		const int w = vga_width * scale, h = vga_height * scale;
		SDL_Surface *const target = SDL_CreateRGBSurface(0, w, h, 32, 0, 0, 0, 0);
		SDL_Renderer *const renderer = target != NULL ? SDL_CreateSoftwareRenderer(target) : NULL;
		SDL_Texture *texture = NULL;
		if (renderer != NULL)
			texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
			                            SDL_TEXTUREACCESS_STREAMING, w, h);

		void *pixels;
		int pitch;
		if (texture == NULL)
		{
			fault = "could not create a streaming texture";
		}
		else
		{
			if (SDL_LockTexture(texture, NULL, &pixels, &pitch) == 0)
			{
				memset(pixels, 0xcd, (size_t)pitch * h);
				SDL_UnlockTexture(texture);
			}
			nn_32(src, texture);
			if (SDL_LockTexture(texture, NULL, &pixels, &pitch) != 0)
			{
				fault = "could not read the texture back";
			}
			else
			{
				for (int y = 0; y < h && fault == NULL; ++y)
				{
					const Uint8 *const row_bytes = (const Uint8 *)pixels + (size_t)y * pitch;
					const Uint32 *const row = (const Uint32 *)row_bytes;
					const Uint8 *const src_row = src_pixels + (size_t)(y / scale) * SRC_PITCH;
					for (int x = 0; x < w && fault == NULL; ++x)
					{
						if (row[x] != rgb_palette[src_row[x / scale]])
						{
							snprintf(message, sizeof(message),
							         "%dx scaler rows%s differ from a direct lookup", scale,
							         pool ? " on the pool" : "");
							fault = message;
						}
					}
				}
				SDL_UnlockTexture(texture);
			}
			SDL_DestroyTexture(texture);
		}
		if (renderer != NULL)
			SDL_DestroyRenderer(renderer);
		SDL_FreeSurface(target);
	}
	render_threads = saved_threads;

	SDL_FreeSurface(src);
	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

void scale2x_32(SDL_Surface *src_surface, SDL_Texture *dst_texture)
{
	Uint8 *src = src_surface->pixels, *src_temp;
	Uint8 *dst, *dst_temp;

	int src_pitch = src_surface->pitch;
	int dst_pitch;

	const int dst_Bpp = 4,         // dst_surface->format->BytesPerPixel
	          height = vga_height, // src_surface->h
	          width = vga_width;   // src_surface->w

	void* tmp_ptr;
	SDL_LockTexture(dst_texture, NULL, &tmp_ptr, &dst_pitch);
	dst = tmp_ptr;
	
	int prevline, nextline;
	
	Uint32 E0, E1, E2, E3, B, D, E, F, H;
	for (int y = 0; y < height; y++)
	{
		src_temp = src;
		dst_temp = dst;
		
		prevline = (y > 0) ? -src_pitch : 0;
		nextline = (y < height - 1) ? src_pitch : 0;
		
		for (int x = 0; x < width; x++)
		{
			B = rgb_palette[*(src + prevline)];
			D = rgb_palette[*(x > 0 ? src - 1 : src)];
			E = rgb_palette[*src];
			F = rgb_palette[*(x < width - 1 ? src + 1 : src)];
			H = rgb_palette[*(src + nextline)];
			
			if (B != H && D != F)
			{
				E0 = D == B ? D : E;
				E1 = B == F ? F : E;
				E2 = D == H ? D : E;
				E3 = H == F ? F : E;
			}
			else
			{
				E0 = E1 = E2 = E3 = E;
			}
			
			*(Uint32 *)dst = E0;
			*(Uint32 *)(dst + dst_Bpp) = E1;
			*(Uint32 *)(dst + dst_pitch) = E2;
			*(Uint32 *)(dst + dst_pitch + dst_Bpp) = E3;
			
			src++;
			dst += 2 * dst_Bpp;
		}
		
		src = src_temp + src_pitch;
		dst = dst_temp + 2 * dst_pitch;
	}

	SDL_UnlockTexture(dst_texture);
}

void scale2x_16(SDL_Surface *src_surface, SDL_Texture *dst_texture)
{
	Uint8 *src = src_surface->pixels, *src_temp;
	Uint8 *dst, *dst_temp;

	int src_pitch = src_surface->pitch;
	int dst_pitch;

	const int dst_Bpp = 2,         // dst_surface->format->BytesPerPixel
	          height = vga_height, // src_surface->h
	          width = vga_width;   // src_surface->w

	void* tmp_ptr;
	SDL_LockTexture(dst_texture, NULL, &tmp_ptr, &dst_pitch);
	dst = tmp_ptr;
	
	int prevline, nextline;
	
	Uint16 E0, E1, E2, E3, B, D, E, F, H;
	for (int y = 0; y < height; y++)
	{
		src_temp = src;
		dst_temp = dst;
		
		prevline = (y > 0) ? -src_pitch : 0;
		nextline = (y < height - 1) ? src_pitch : 0;
		
		for (int x = 0; x < width; x++)
		{
			B = rgb_palette[*(src + prevline)];
			D = rgb_palette[*(x > 0 ? src - 1 : src)];
			E = rgb_palette[*src];
			F = rgb_palette[*(x < width - 1 ? src + 1 : src)];
			H = rgb_palette[*(src + nextline)];
			
			if (B != H && D != F)
			{
				E0 = D == B ? D : E;
				E1 = B == F ? F : E;
				E2 = D == H ? D : E;
				E3 = H == F ? F : E;
			}
			else
			{
				E0 = E1 = E2 = E3 = E;
			}
			
			*(Uint16 *)dst = E0;
			*(Uint16 *)(dst + dst_Bpp) = E1;
			*(Uint16 *)(dst + dst_pitch) = E2;
			*(Uint16 *)(dst + dst_pitch + dst_Bpp) = E3;
			
			src++;
			dst += 2 * dst_Bpp;
		}
		
		src = src_temp + src_pitch;
		dst = dst_temp + 2 * dst_pitch;
	}

	SDL_UnlockTexture(dst_texture);
}

void scale3x_32(SDL_Surface *src_surface, SDL_Texture *dst_texture)
{
	Uint8 *src = src_surface->pixels, *src_temp;
	Uint8 *dst, *dst_temp;

	int src_pitch = src_surface->pitch;
	int dst_pitch;

	const int dst_Bpp = 4,         // dst_surface->format->BytesPerPixel
	          height = vga_height, // src_surface->h
	          width = vga_width;   // src_surface->w

	void* tmp_ptr;
	SDL_LockTexture(dst_texture, NULL, &tmp_ptr, &dst_pitch);
	dst = tmp_ptr;
	
	int prevline, nextline;
	
	Uint32 E0, E1, E2, E3, E4, E5, E6, E7, E8, A, B, C, D, E, F, G, H, I;
	for (int y = 0; y < height; y++)
	{
		src_temp = src;
		dst_temp = dst;
		
		prevline = (y > 0) ? -src_pitch : 0;
		nextline = (y < height - 1) ? src_pitch : 0;
		
		for (int x = 0; x < width; x++)
		{
			A = rgb_palette[*(src + prevline - (x > 0 ? 1 : 0))];
			B = rgb_palette[*(src + prevline)];
			C = rgb_palette[*(src + prevline + (x < width - 1 ? 1 : 0))];
			D = rgb_palette[*(src - (x > 0 ? 1 : 0))];
			E = rgb_palette[*src];
			F = rgb_palette[*(src + (x < width - 1 ? 1 : 0))];
			G = rgb_palette[*(src + nextline - (x > 0 ? 1 : 0))];
			H = rgb_palette[*(src + nextline)];
			I = rgb_palette[*(src + nextline + (x < width - 1 ? 1 : 0))];
			
			if (B != H && D != F)
			{
				E0 = D == B ? D : E;
				E1 = (D == B && E != C) || (B == F && E != A) ? B : E;
				E2 = B == F ? F : E;
				E3 = (D == B && E != G) || (D == H && E != A) ? D : E;
				E4 = E;
				E5 = (B == F && E != I) || (H == F && E != C) ? F : E;
				E6 = D == H ? D : E;
				E7 = (D == H && E != I) || (H == F && E != G) ? H : E;
				E8 = H == F ? F : E;
			}
			else
			{
				E0 = E1 = E2 = E3 = E4 = E5 = E6 = E7 = E8 = E;
			}
			
			*(Uint32 *)dst = E0;
			*(Uint32 *)(dst + dst_Bpp) = E1;
			*(Uint32 *)(dst + 2 * dst_Bpp) = E2;
			*(Uint32 *)(dst + dst_pitch) = E3;
			*(Uint32 *)(dst + dst_pitch + dst_Bpp) = E4;
			*(Uint32 *)(dst + dst_pitch + 2 * dst_Bpp) = E5;
			*(Uint32 *)(dst + 2 * dst_pitch) = E6;
			*(Uint32 *)(dst + 2 * dst_pitch + dst_Bpp) = E7;
			*(Uint32 *)(dst + 2 * dst_pitch + 2 * dst_Bpp) = E8;
			
			src++;
			dst += 3 * dst_Bpp;
		}
		
		src = src_temp + src_pitch;
		dst = dst_temp + 3 * dst_pitch;
	}

	SDL_UnlockTexture(dst_texture);
}

void scale3x_16(SDL_Surface *src_surface, SDL_Texture *dst_texture)
{
	Uint8 *src = src_surface->pixels, *src_temp;
	Uint8 *dst, *dst_temp;

	int src_pitch = src_surface->pitch;
	int dst_pitch;

	const int dst_Bpp = 2,         // dst_surface->format->BytesPerPixel
	          height = vga_height, // src_surface->h
	          width = vga_width;   // src_surface->w

	void* tmp_ptr;
	SDL_LockTexture(dst_texture, NULL, &tmp_ptr, &dst_pitch);
	dst = tmp_ptr;
	
	int prevline, nextline;
	
	Uint16 E0, E1, E2, E3, E4, E5, E6, E7, E8, A, B, C, D, E, F, G, H, I;
	for (int y = 0; y < height; y++)
	{
		src_temp = src;
		dst_temp = dst;
		
		prevline = (y > 0) ? -src_pitch : 0;
		nextline = (y < height - 1) ? src_pitch : 0;
		
		for (int x = 0; x < width; x++)
		{
			A = rgb_palette[*(src + prevline - (x > 0 ? 1 : 0))];
			B = rgb_palette[*(src + prevline)];
			C = rgb_palette[*(src + prevline + (x < width - 1 ? 1 : 0))];
			D = rgb_palette[*(src - (x > 0 ? 1 : 0))];
			E = rgb_palette[*src];
			F = rgb_palette[*(src + (x < width - 1 ? 1 : 0))];
			G = rgb_palette[*(src + nextline - (x > 0 ? 1 : 0))];
			H = rgb_palette[*(src + nextline)];
			I = rgb_palette[*(src + nextline + (x < width - 1 ? 1 : 0))];
			
			if (B != H && D != F)
			{
				E0 = D == B ? D : E;
				E1 = (D == B && E != C) || (B == F && E != A) ? B : E;
				E2 = B == F ? F : E;
				E3 = (D == B && E != G) || (D == H && E != A) ? D : E;
				E4 = E;
				E5 = (B == F && E != I) || (H == F && E != C) ? F : E;
				E6 = D == H ? D : E;
				E7 = (D == H && E != I) || (H == F && E != G) ? H : E;
				E8 = H == F ? F : E;
			}
			else
			{
				E0 = E1 = E2 = E3 = E4 = E5 = E6 = E7 = E8 = E;
			}
			
			*(Uint16 *)dst = E0;
			*(Uint16 *)(dst + dst_Bpp) = E1;
			*(Uint16 *)(dst + 2 * dst_Bpp) = E2;
			*(Uint16 *)(dst + dst_pitch) = E3;
			*(Uint16 *)(dst + dst_pitch + dst_Bpp) = E4;
			*(Uint16 *)(dst + dst_pitch + 2 * dst_Bpp) = E5;
			*(Uint16 *)(dst + 2 * dst_pitch) = E6;
			*(Uint16 *)(dst + 2 * dst_pitch + dst_Bpp) = E7;
			*(Uint16 *)(dst + 2 * dst_pitch + 2 * dst_Bpp) = E8;
			
			src++;
			dst += 3 * dst_Bpp;
		}
		
		src = src_temp + src_pitch;
		dst = dst_temp + 3 * dst_pitch;
	}

	SDL_UnlockTexture(dst_texture);
}
