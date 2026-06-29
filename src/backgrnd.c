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
#include "backgrnd.h"

#include "config.h"
#include "mtrand.h"
#include "opentyr.h"
#include "render_list.h"
#include "varz.h"
#include "video.h"

#include <assert.h>

/*Special Background 2 and Background 3*/

/*Back Pos 3*/
JE_word backPos, backPos2, backPos3;
JE_word backMove, backMove2, backMove3;

// See backgrnd.h: true during the sim tick (advance scroll as normal), false
// during interpolated re-draws so the layer can be re-rendered without moving.
bool background_advance = true;

/*Main Maps*/
JE_word mapX, mapY, mapX2, mapX3, mapY2, mapY3;
JE_byte **mapYPos, **mapY2Pos, **mapY3Pos;
JE_integer mapXPos, oldMapXOfs, mapXOfs, mapX2Ofs, mapX2Pos, mapX3Pos, oldMapX3Ofs, mapX3Ofs, tempMapXOfs;
intptr_t mapXbpPos, mapX2bpPos, mapX3bpPos;
JE_byte map1YDelay, map1YDelayMax, map2YDelay, map2YDelayMax;

JE_boolean  anySmoothies;
JE_byte     smoothie_data[9]; /* [1..9] */

void JE_darkenBackground(JE_word neat)  /* wild detail level */
{
	Uint8 *s = VGAScreen->pixels; /* screen pointer, 8-bit specific */
	int x, y;
	
	s += 24;
	
	for (y = 184; y; y--)
	{
		for (x = PLAYFIELD_WIDTH; x; x--)
		{
			*s = ((((*s & 0x0f) << 4) - (*s & 0x0f) + ((((x - neat - y) >> 2) + *(s - 2) + (y == 184 ? 0 : *(s - (VGAScreen->pitch - 1)))) & 0x0f)) >> 4) | (*s & 0xf0);
			s++;
		}
		s += VGAScreen->pitch - PLAYFIELD_WIDTH;
	}
}

void blit_background_row(SDL_Surface *surface, int x, int y, Uint8 **map)
{
	assert(surface->format->BitsPerPixel == 8);

	if (render_list_recording)
		rl_rec_bg_row(x, y, map, false);

	Uint8 *pixels = (Uint8 *)surface->pixels + (y * surface->pitch) + x,
	      *pixels_ll = (Uint8 *)surface->pixels,  // lower limit
	      *pixels_ul = (Uint8 *)surface->pixels + (surface->h * surface->pitch);  // upper limit
	
	// Use two extra tiles to ensure the playfield is fully covered when the
		// horizontal scroll position is negative.  A single extra tile left a
		// small uncovered strip on the right edge of the playfield, resulting in
		// intermittent black bars.
	const int tile_count = PLAYFIELD_WIDTH / 24 + 2;
	const int row_width = tile_count * 24;
	for (int y = 0; y < 28; y++)
	{
		// not drawing on screen yet; skip y
		if ((pixels + row_width) < pixels_ll)
		{
			pixels += surface->pitch;
			continue;
		}

		for (int tile = 0; tile < tile_count; tile++)
		{
			Uint8* data = *(map + tile);
			
			// no tile; skip tile
			if (data == NULL)
			{
				pixels += 24;
				continue;
			}
			
			data += y * 24;
			
			for (int x = 24; x; x--)
			{
				if (pixels >= pixels_ul)
					return;
				if (pixels >= pixels_ll && *data != 0)
					*pixels = *data;
				
				pixels++;
				data++;
			}
		}
		
		pixels += surface->pitch - row_width;
	}
}

void blit_background_row_blend(SDL_Surface *surface, int x, int y, Uint8 **map)
{
	assert(surface->format->BitsPerPixel == 8);

	if (render_list_recording)
		rl_rec_bg_row(x, y, map, true);

	Uint8 *pixels = (Uint8 *)surface->pixels + (y * surface->pitch) + x,
	      *pixels_ll = (Uint8 *)surface->pixels,  // lower limit
	      *pixels_ul = (Uint8 *)surface->pixels + (surface->h * surface->pitch);  // upper limit
	
	// See comment in blit_background_row() above for rationale on the
		// additional tile.
	const int tile_count = PLAYFIELD_WIDTH / 24 + 2;
	const int row_width = tile_count * 24;
	for (int y = 0; y < 28; y++)
	{
		// not drawing on screen yet; skip y
		if ((pixels + row_width) < pixels_ll)
		{
			pixels += surface->pitch;
			continue;
		}

		for (int tile = 0; tile < tile_count; tile++)
		{
			Uint8* data = *(map + tile);
			
			// no tile; skip tile
			if (data == NULL)
			{
				pixels += 24;
				continue;
			}
			
			data += y * 24;
			
			for (int x = 24; x; x--)
			{
				if (pixels >= pixels_ul)
					return;
				if (pixels >= pixels_ll && *data != 0)
					*pixels = (*data & 0xf0) | (((*pixels & 0x0f) + (*data & 0x0f)) / 2);
				
				pixels++;
				data++;
			}
		}
		
		pixels += surface->pitch - row_width;
	}
}

void draw_background_1(SDL_Surface *surface)
{
	SDL_FillRect(surface, NULL, 0);
	
	// Two extra tiles guarantee full coverage regardless of horizontal
		// scroll offset.
	const int tile_count = PLAYFIELD_WIDTH / 24 + 2;
	Uint8** map = (Uint8**)mapYPos + mapXbpPos - tile_count;

	rl_current_id = RL_ID_BG_BASE + 1;  // tag rows for cross-frame interpolation
	for (int i = -1; i < 7; i++)
	{
		blit_background_row(surface, mapXPos + PLAYFIELD_X_SHIFT, (i * 28) + backPos, map);

		map += 14;
	}
	rl_current_id = 0;
}

void draw_background_2(SDL_Surface *surface)
{
	if (background_advance && map2YDelayMax > 1 && backMove2 < 2)
		backMove2 = (map2YDelay == 1) ? 1 : 0;
	// Two extra tiles guarantee full coverage regardless of horizontal
		// scroll offset.
	const int tile_count = PLAYFIELD_WIDTH / 24 + 2;
	if (background2 != 0)
	{
		// water effect combines background 1 and 2 by synchronizing the x coordinate
		int x = (smoothies[1] ? mapXPos : mapX2Pos) + PLAYFIELD_X_SHIFT;

		Uint8** map = (Uint8**)mapY2Pos + (smoothies[1] ? mapXbpPos : mapX2bpPos) - tile_count;

		rl_current_id = RL_ID_BG_BASE + 2;
		for (int i = -1; i < 7; i++)
		{
			blit_background_row(surface, x, (i * 28) + backPos2, map);

			map += 14;
		}
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
}

void draw_background_2_blend(SDL_Surface *surface)
{
	if (background_advance && map2YDelayMax > 1 && backMove2 < 2)
		backMove2 = (map2YDelay == 1) ? 1 : 0;
	
	// Two extra tiles guarantee full coverage regardless of horizontal
		// scroll offset.
	const int tile_count = PLAYFIELD_WIDTH / 24 + 2;
	Uint8** map = (Uint8**)mapY2Pos + mapX2bpPos - tile_count;

	rl_current_id = RL_ID_BG_BASE + 2;
	for (int i = -1; i < 7; i++)
	{
		blit_background_row_blend(surface, mapX2Pos + PLAYFIELD_X_SHIFT, (i * 28) + backPos2, map);

		map += 14;
	}
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
	}

	// Two extra tiles guarantee full coverage regardless of horizontal
		// scroll offset.
	const int tile_count = PLAYFIELD_WIDTH / 24 + 2;
	Uint8** map = (Uint8**)mapY3Pos + mapX3bpPos - tile_count;

	rl_current_id = RL_ID_BG_BASE + 3;
	for (int i = -1; i < 7; i++)
	{
		// mapX3Pos already includes a 18-pixel left shift to correct the
		// cloud layer's widescreen misalignment.
		blit_background_row(surface, mapX3Pos + PLAYFIELD_X_SHIFT, (i * 28) + backPos3, map);

		map += 15;
	}
	rl_current_id = 0;
}

void JE_filterScreen(JE_shortint col, JE_shortint int_)
{
	Uint8 *s = NULL; /* screen pointer, 8-bit specific */
	int x, y;
	unsigned int temp;
	
	if (filterFade)
	{
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
	
	if (col != -99 && filtrationAvail)
	{
		s = VGAScreen->pixels;
		s += 24;
		
		col <<= 4;
		
		for (y = 184; y; y--)
		{
			for (x = PLAYFIELD_WIDTH; x; x--)
			{
				*s = col | (*s & 0x0f);
				s++;
			}
			s += VGAScreen->pitch - PLAYFIELD_WIDTH;
		}
	}
	
	if (int_ != -99 && explosionTransparent)
	{
		s = VGAScreen->pixels;
		s += 24;
		
		for (y = 184; y; y--)
		{
			for (x = PLAYFIELD_WIDTH; x; x--)
			{
				temp = (*s & 0x0f) + int_;
				*s = (*s & 0xf0) | (temp >= 0x1f ? 0 : (temp >= 0x0f ? 0x0f : temp));
				s++;
			}
			s += VGAScreen->pitch - PLAYFIELD_WIDTH;
		}
	}
}

void JE_checkSmoothies(void)
{
	anySmoothies = (processorType > 2 && (smoothies[1-1] || smoothies[2-1])) || (processorType > 1 && (smoothies[3-1] || smoothies[4-1] || smoothies[5-1]));
}

void lava_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	/* we don't need to check for over-reading the pixel surfaces since we only
		 * read from the top 185+1 scanlines, and the playfield width is vga_width */
	
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

				// value is average value of source pixel (2x), destination pixel above, and destination pixel below (all with waver)
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

void water_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	Uint8 hue = smoothie_data[1] << 4;
	
	/* we don't need to check for over-reading the pixel surfaces since we only
		 * read from the top 185+1 scanlines, and the playfield width is vga_width */
	
	const int dst_pitch = dst->pitch;
	Uint8 *dst_pixel = (Uint8 *)dst->pixels + (185 * dst_pitch);
	
	const Uint8 *src_pixel = (Uint8 *)src->pixels + (185 * src->pitch);
	
	int w = vga_width * 185 - 1;
	
	for (int y = 185 - 1; y >= 0; --y)
	{
		dst_pixel -= (dst_pitch - vga_width);  // in case pitch differs
		src_pixel -= (src->pitch - vga_width);  // in case pitch differs

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

				// pixel is copied from source if not blue
				// otherwise, value is average of value of source pixel and destination pixel below (with waver)
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

void iced_blur_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	Uint8 *dst_pixel = dst->pixels;
	const Uint8 *src_pixel = src->pixels;
	
	for (int y = 0; y < 184; ++y)
	{
		for (int x = 0; x < vga_width; ++x)
		{
			// value is average value of source pixel and destination pixel
			// hue is icy blue
			
			const Uint8 value = (*src_pixel & 0x0f) + (*dst_pixel & 0x0f);
			*dst_pixel = (value / 2) | 0x80;
			
			++dst_pixel;
			++src_pixel;
		}
		
		dst_pixel += (dst->pitch - vga_width);  // in case pitch differs
		src_pixel += (src->pitch - vga_width);  // in case pitch differs
	}
}

void blur_filter(SDL_Surface *dst, SDL_Surface *src)
{
	assert(src->format->BitsPerPixel == 8 && dst->format->BitsPerPixel == 8);
	
	Uint8 *dst_pixel = dst->pixels;
	const Uint8 *src_pixel = src->pixels;
	
	for (int y = 0; y < 184; ++y)
	{
		for (int x = 0; x < vga_width; ++x)
		{
			// value is average value of source pixel and destination pixel
			// hue is source pixel hue
			
			const Uint8 value = (*src_pixel & 0x0f) + (*dst_pixel & 0x0f);
			*dst_pixel = (value / 2) | (*src_pixel & 0xf0);
			
			++dst_pixel;
			++src_pixel;
		}
		
		dst_pixel += (dst->pitch - vga_width);  // in case pitch differs
		src_pixel += (src->pitch - vga_width);  // in case pitch differs
	}
}

/* Background Starfield.
 *
 * Each star is an independent (x column, y row) point. y is a float so the field
 * can drift smoothly at any speed, and ONLY y is advanced/interpolated — x is
 * fixed — so stars never bleed sideways. (The old model packed both into one
 * linear byte offset and interpolated it, which dragged a fraction of a row into
 * the column and made stars streak diagonally.) */
typedef struct
{
	int x;        // column (constant for a star's lifetime)
	float y;      // row (fractional, advances each tick, wraps at STARFIELD_WRAP)
	int speed;    // base rows of drift (scaled by STARFIELD_SPEED_SCALE)
	Uint8 color;
} StarfieldStar;

#define MAX_STARS 100
#define STARFIELD_HUE 0x90
#define STARFIELD_WRAP    184  // rows; star wraps back to the top past this
#define STARFIELD_VISIBLE 177  // rows; star is only drawn above this (keeps it out of the HUD)

// rows/tick per unit of (speed + move_speed). The speed knob: lower = slower.
// 1.0 == the original game's speed (which the rewrite now renders smoothly,
// without the old diagonal-streak bug that made it look far faster than it was).
#define STARFIELD_SPEED_SCALE 1.0f
static StarfieldStar starfield_stars[MAX_STARS];
int starfield_speed;

void initialize_starfield(void)
{
	for (int i = MAX_STARS - 1; i >= 0; --i)
	{
		starfield_stars[i].x = mt_rand() % vga_width;
		starfield_stars[i].y = (float)(mt_rand() % STARFIELD_WRAP);
		starfield_stars[i].speed = mt_rand() % 3 + 2;
		starfield_stars[i].color = mt_rand() % 16 + STARFIELD_HUE;
	}
}

// Draw a single star (center pixel plus a dimmer halo) at column x, row y.
// Factored out so the render list can re-draw stars during replay. Bounds are
// checked per axis so a star can't wrap a pixel into the neighbouring row.
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

void update_and_draw_starfield(SDL_Surface* surface, int move_speed)
{
	for (int i = MAX_STARS-1; i >= 0; --i)
	{
		StarfieldStar* star = &starfield_stars[i];

		// Drift down by a (usually sub-pixel) amount; only y moves.
		const float dy = (star->speed + move_speed) * STARFIELD_SPEED_SCALE;
		star->y += dy;

		// dy passed to the render list is this tick's row motion, for smooth
		// interpolation. On a wrap, pass 0 so the replay snaps to the new
		// position instead of streaking across the whole screen.
		float rec_dy = dy;
		if (star->y >= STARFIELD_WRAP)
		{
			star->y -= STARFIELD_WRAP;
			rec_dy = 0.0f;
		}

		draw_starfield_star(surface, star->x, (int)(star->y + 0.5f), star->color);

		if (render_list_recording)
			rl_rec_star(star->x, star->y, rec_dy, star->color);
	}
}
