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
#include "video.h"

#include "keyboard.h"
#include "opentyr.h"
#include "palette.h"
#include "video_scale.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

const char *const scaling_mode_names[ScalingMode_MAX] = {
	"Center",
	"Integer",
	"Widescreen",
	"Classic",
};

int fullscreen_display;
bool output_vsync = true;  // present in sync with the display's refresh rate
ScalingMode scaling_mode = SCALE_WIDESCREEN;  // fill the screen at true 16:9 by default
static SDL_Rect last_output_rect = { 0, 0, vga_width, vga_height };

SDL_Surface *VGAScreen, *VGAScreenSeg;
SDL_Surface *VGAScreen2;
SDL_Surface *game_screen;
static SDL_Surface* menu_screen;

static int current_x_offset = MENU_X_OFFSET;

SDL_Window *main_window = NULL;
static SDL_Renderer *main_window_renderer = NULL;
SDL_PixelFormat *main_window_tex_format = NULL;
static SDL_Texture *main_window_texture = NULL;

static ScalerFunction scaler_function;

static Uint8 gradient_cache[256][MENU_X_OFFSET];
static Uint32 last_gradient_palette[256];
static bool gradient_cache_valid = false;

static void init_renderer(void);
static void deinit_renderer(void);
static void init_texture(void);
static void deinit_texture(void);

static int window_get_display_index(void);
static void window_center_in_display(int display_index);
static void calc_dst_render_rect(SDL_Surface *src_surface, SDL_Rect *dst_rect);
static void scale_and_flip(SDL_Surface *);
static void blit_with_offset(SDL_Surface* src, SDL_Surface* dst, int x_offset);
static Uint8 nearest_palette_index(Uint8 r, Uint8 g, Uint8 b);
static void update_gradient_cache(void);

void init_video(void)
{
	if (SDL_WasInit(SDL_INIT_VIDEO))
		return;

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) == -1)
	{
		fprintf(stderr, "error: failed to initialize SDL video: %s\n", SDL_GetError());
		exit(1);
	}

	// Create the software surfaces that the game renders to. These are all 356x200x8 (16:9)
	// regardless of the window size or monitor resolution.
	VGAScreen = VGAScreenSeg = SDL_CreateRGBSurface(0, vga_width, vga_height, 8, 0, 0, 0, 0);
	VGAScreen2 = SDL_CreateRGBSurface(0, vga_width, vga_height, 8, 0, 0, 0, 0);
	game_screen = SDL_CreateRGBSurface(0, vga_width, vga_height, 8, 0, 0, 0, 0);
	menu_screen = SDL_CreateRGBSurface(0, vga_width, vga_height, 8, 0, 0, 0, 0);

	// The game code writes to surface->pixels directly without locking, so make sure that we
	// indeed created software surfaces that support this.
	assert(!SDL_MUSTLOCK(VGAScreen));
	assert(!SDL_MUSTLOCK(VGAScreen2));
	assert(!SDL_MUSTLOCK(game_screen));
	assert(!SDL_MUSTLOCK(menu_screen));

	JE_clr256(VGAScreen);

	// Create the window with a temporary initial size, hidden until we set up the
	// scaler and find the true window size
	main_window = SDL_CreateWindow(opentyrian_str,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		vga_width, vga_height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);

	if (main_window == NULL)
	{
		fprintf(stderr, "error: failed to create window: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}

	reinit_fullscreen(fullscreen_display);
	init_renderer();
	init_texture();
	init_scaler(scaler);

	SDL_ShowWindow(main_window);

	SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
	SDL_RenderClear(main_window_renderer);
	SDL_RenderPresent(main_window_renderer);
}

void deinit_video(void)
{
	deinit_texture();
	deinit_renderer();

	SDL_DestroyWindow(main_window);

	SDL_FreeSurface(VGAScreenSeg);
	SDL_FreeSurface(VGAScreen2);
	SDL_FreeSurface(game_screen);
	SDL_FreeSurface(menu_screen);

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

static void init_renderer(void)
{
	Uint32 flags = output_vsync ? SDL_RENDERER_PRESENTVSYNC : 0;
	main_window_renderer = SDL_CreateRenderer(main_window, -1, flags);

	if (main_window_renderer == NULL && flags != 0)
	{
		// The driver may be unable to provide a vsync'd renderer; fall back.
		output_vsync = false;
		main_window_renderer = SDL_CreateRenderer(main_window, -1, 0);
	}

	if (main_window_renderer == NULL)
	{
		fprintf(stderr, "error: failed to create renderer: %s\n", SDL_GetError());
		exit(EXIT_FAILURE);
	}
}

// Toggle display-synced presentation. Recreates the renderer (and its texture)
// so the new vsync setting takes effect immediately.
void set_vsync(bool enabled)
{
	if (output_vsync == enabled && main_window_renderer != NULL)
		return;

	output_vsync = enabled;

	if (main_window_renderer != NULL)
	{
		deinit_texture();
		deinit_renderer();
		init_renderer();
		init_texture();
	}
}

static void deinit_renderer(void)
{
	if (main_window_renderer != NULL)
	{
		SDL_DestroyRenderer(main_window_renderer);
		main_window_renderer = NULL;
	}
}

static void init_texture(void)
{
	assert(main_window_renderer != NULL);

	int bpp = 32; // TODOSDL2
	Uint32 format = bpp == 32 ? SDL_PIXELFORMAT_RGB888 : SDL_PIXELFORMAT_RGB565;
	int scaler_w = scalers[scaler].width;
	int scaler_h = scalers[scaler].height;

	main_window_tex_format = SDL_AllocFormat(format);

	main_window_texture = SDL_CreateTexture(main_window_renderer, format, SDL_TEXTUREACCESS_STREAMING, scaler_w, scaler_h);

	if (main_window_texture == NULL)
	{
		fprintf(stderr, "error: failed to create scaler texture %dx%dx%s: %s\n", scaler_w, scaler_h, SDL_GetPixelFormatName(format), SDL_GetError());
		exit(EXIT_FAILURE);
	}
}

static void deinit_texture(void)
{
	if (main_window_texture != NULL)
	{
		SDL_DestroyTexture(main_window_texture);
		main_window_texture = NULL;
	}

	if (main_window_tex_format != NULL)
	{
		SDL_FreeFormat(main_window_tex_format);
		main_window_tex_format = NULL;
	}
}

static int window_get_display_index(void)
{
	return SDL_GetWindowDisplayIndex(main_window);
}

static void window_center_in_display(int display_index)
{
	int win_w, win_h;
	SDL_GetWindowSize(main_window, &win_w, &win_h);

	SDL_Rect bounds;
	SDL_GetDisplayBounds(display_index, &bounds);

	SDL_SetWindowPosition(main_window, bounds.x + (bounds.w - win_w) / 2, bounds.y + (bounds.h - win_h) / 2);
}

void reinit_fullscreen(int new_display)
{
	fullscreen_display = new_display;

	if (fullscreen_display >= SDL_GetNumVideoDisplays())
	{
		fullscreen_display = 0;
	}

	SDL_SetWindowFullscreen(main_window, SDL_FALSE);
	SDL_SetWindowSize(main_window, scalers[scaler].width, scalers[scaler].height);

	if (fullscreen_display == -1)
	{
		window_center_in_display(window_get_display_index());
	}
	else
	{
		window_center_in_display(fullscreen_display);

		if (SDL_SetWindowFullscreen(main_window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0)
		{
			reinit_fullscreen(-1);
			return;
		}
	}
}

void video_on_win_resize(void)
{
	int w, h;
	int scaler_w, scaler_h;

	// Tell video to reinit if the window was manually resized by the user.
	// Also enforce a minimum size on the window.

	SDL_GetWindowSize(main_window, &w, &h);
	scaler_w = scalers[scaler].width;
	scaler_h = scalers[scaler].height;

	if (w < scaler_w || h < scaler_h)
	{
		w = w < scaler_w ? scaler_w : w;
		h = h < scaler_h ? scaler_h : h;

		SDL_SetWindowSize(main_window, w, h);
	}
}

void toggle_fullscreen(void)
{
	if (fullscreen_display != -1)
		reinit_fullscreen(-1);
	else
		reinit_fullscreen(SDL_GetWindowDisplayIndex(main_window));
}

bool init_scaler(unsigned int new_scaler)
{
	int w = scalers[new_scaler].width,
	    h = scalers[new_scaler].height;
	int bpp = main_window_tex_format->BitsPerPixel; // TODOSDL2

	scaler = new_scaler;

	deinit_texture();
	init_texture();

	if (fullscreen_display == -1)
	{
		// Changing scalers, when not in fullscreen mode, forces the window
		// to resize to exactly match the scaler's output dimensions.
		SDL_SetWindowSize(main_window, w, h);
		window_center_in_display(window_get_display_index());
	}

	switch (bpp)
	{
	case 32:
		scaler_function = scalers[scaler].scaler32;
		break;
	case 16:
		scaler_function = scalers[scaler].scaler16;
		break;
	default:
		scaler_function = NULL;
		break;
	}

	if (scaler_function == NULL)
	{
		assert(false);
		return false;
	}

	return true;
}

bool set_scaling_mode_by_name(const char *name)
{
	for (int i = 0; i < ScalingMode_MAX; ++i)
	{
		 if (strcmp(name, scaling_mode_names[i]) == 0)
		 {
			 scaling_mode = i;
			 return true;
		 }
	}
	return false;
}

void JE_clr256(SDL_Surface *screen)
{
	SDL_FillRect(screen, NULL, 0);
}

void JE_showVGA(void)
{
	if (current_x_offset != 0)
	{
		blit_with_offset(VGAScreen, menu_screen, current_x_offset);
		scale_and_flip(menu_screen);
	}
	else
	{
		scale_and_flip(VGAScreen);
	}
}

// Fit a centred rectangle of the given display aspect ratio (width / height)
// inside the window, preserving the ratio (letterbox or pillarbox as needed).
static void fit_rect_to_aspect(SDL_Rect *const r, int win_w, int win_h, float aspect)
{
	if ((float)win_h * aspect > (float)win_w)
	{
		r->w = win_w;
		r->h = (int)((float)win_w / aspect);
	}
	else
	{
		r->w = (int)((float)win_h * aspect);
		r->h = win_h;
	}
}

static void calc_dst_render_rect(SDL_Surface *const src_surface, SDL_Rect *const dst_rect)
{
	// Decides how the logical output texture (after software scaling applied) will fit
	// in the window.

	int win_w, win_h;
	SDL_GetWindowSize(main_window, &win_w, &win_h);

	// Square-pixel ratio of the framebuffer itself (356:200 = 16:9).
	const float pixel_aspect = (float)src_surface->w / (float)src_surface->h;

	switch (scaling_mode)
	{
	case SCALE_CENTER:
		SDL_QueryTexture(main_window_texture, NULL, NULL, &dst_rect->w, &dst_rect->h);
		break;
	case SCALE_INTEGER:
		dst_rect->w = src_surface->w;
		dst_rect->h = src_surface->h;
		while (dst_rect->w + src_surface->w <= win_w && dst_rect->h + src_surface->h <= win_h)
		{
			dst_rect->w += src_surface->w;
			dst_rect->h += src_surface->h;
		}
		break;
	case SCALE_WIDESCREEN:
		// True widescreen: square pixels, i.e. the buffer's own ratio (16:9).
		fit_rect_to_aspect(dst_rect, win_w, win_h, pixel_aspect);
		break;
	case SCALE_CLASSIC_PAR:
		// Original DOS pixel aspect (PAR 5/6): taller pixels, ~3:2 overall.
		fit_rect_to_aspect(dst_rect, win_w, win_h, pixel_aspect * (5.f / 6.f));
		break;
	case ScalingMode_MAX:
		assert(false);
		break;
	}

	dst_rect->x = (win_w - dst_rect->w) / 2;
	dst_rect->y = (win_h - dst_rect->h) / 2;
}

static void scale_and_flip(SDL_Surface *src_surface)
{
	assert(src_surface->format->BitsPerPixel == 8);

	// Do software scaling
	assert(scaler_function != NULL);
	scaler_function(src_surface, main_window_texture);

	SDL_Rect dst_rect;
	calc_dst_render_rect(src_surface, &dst_rect);

	// Clear the window and blit the output texture to it
	SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
	SDL_RenderClear(main_window_renderer);
	SDL_RenderCopy(main_window_renderer, main_window_texture, NULL, &dst_rect);
	SDL_RenderPresent(main_window_renderer);

	// Save output rect to be used by mouse functions
	// Save output rect to be used by mouse functions
	last_output_rect = dst_rect;
}

static Uint8 nearest_palette_index(Uint8 r, Uint8 g, Uint8 b)
{
	int best = 0;
	int best_dist = INT_MAX;

	for (int i = 0; i < 256; ++i)
	{
		int dr = (int)colors[i].r - r;
		int dg = (int)colors[i].g - g;
		int db = (int)colors[i].b - b;
		int dist = dr * dr + dg * dg + db * db;
		if (dist < best_dist)
		{
			best_dist = dist;
			best = i;
			if (dist == 0)
				break;
		}
	}

	return (Uint8)best;
}

static void update_gradient_cache(void)
{
	if (!gradient_cache_valid || memcmp(last_gradient_palette, rgb_palette, sizeof(rgb_palette)) != 0)
	{
		memcpy(last_gradient_palette, rgb_palette, sizeof(rgb_palette));

		for (int c = 0; c < 256; ++c)
		{
			SDL_Color col = colors[c];
			gradient_cache[c][0] = 0;
			for (int i = 1; i < MENU_X_OFFSET; ++i)
			{
				float factor = (float)i / MENU_X_OFFSET;
				Uint8 r = (Uint8)(col.r * factor);
				Uint8 g = (Uint8)(col.g * factor);
				Uint8 b = (Uint8)(col.b * factor);
				gradient_cache[c][i] = nearest_palette_index(r, g, b);
			}
		}

		gradient_cache_valid = true;
	}
}

static void blit_with_offset(SDL_Surface* src, SDL_Surface* dst, int x_offset)
{
	update_gradient_cache();

	for (int y = 0; y < vga_height; ++y)
	{
		Uint8* src_row = (Uint8*)src->pixels + y * src->pitch;
		Uint8* dst_row = (Uint8*)dst->pixels + y * dst->pitch;

		memcpy(dst_row + x_offset, src_row, LEGACY_WIDTH);

		Uint8 left_color = src_row[0];
		for (int i = 0; i < x_offset; ++i)
		{
			dst_row[i] = gradient_cache[left_color][i];
		}

		Uint8 right_color = src_row[LEGACY_WIDTH - 1];
		for (int i = 0; i < x_offset; ++i)
		{
			dst_row[x_offset + LEGACY_WIDTH + i] = gradient_cache[right_color][x_offset - 1 - i];
		}
	}
}

void set_menu_centered(bool centered)
{
	current_x_offset = centered ? MENU_X_OFFSET : 0;
}

/** Maps a specified point in game screen coordinates to window coordinates. */
void mapScreenPointToWindow(Sint32 *const inout_x, Sint32 *const inout_y)
{
	Sint32 x = *inout_x + current_x_offset;
	*inout_x = (2 * x + 1) * last_output_rect.w / (2 * VGAScreen->w) + last_output_rect.x;
	*inout_y = (2 * *inout_y + 1) * last_output_rect.h / (2 * VGAScreen->h) + last_output_rect.y;
}

/** Maps a specified point in window coordinates to game screen coordinates. */
void mapWindowPointToScreen(Sint32 *const inout_x, Sint32 *const inout_y)
{
	*inout_x = (2 * (*inout_x - last_output_rect.x) + 1) * VGAScreen->w / (2 * last_output_rect.w) - current_x_offset;
	*inout_y = (2 * (*inout_y - last_output_rect.y) + 1) * VGAScreen->h / (2 * last_output_rect.h);
}

/** Scales a distance in window coordinates to game screen coordinates. */
void scaleWindowDistanceToScreen(Sint32 *const inout_x, Sint32 *const inout_y)
{
	*inout_x = (2 * *inout_x + 1) * VGAScreen->w / (2 * last_output_rect.w);
	*inout_y = (2 * *inout_y + 1) * VGAScreen->h / (2 * last_output_rect.h);
}

/** Float variant: scales a distance with no integer rounding. The integer
 *  version above rounds each call toward zero, which is fine when called once
 *  per tick with the whole accumulated motion, but loses fine/diagonal control
 *  when a caller samples small deltas every render frame. */
void scaleWindowDistanceToScreenF(float *const inout_x, float *const inout_y)
{
	if (last_output_rect.w > 0)
		*inout_x = *inout_x * (float)VGAScreen->w / (float)last_output_rect.w;
	if (last_output_rect.h > 0)
		*inout_y = *inout_y * (float)VGAScreen->h / (float)last_output_rect.h;
}
