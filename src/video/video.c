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
#include "video.h"

#include "config.h"
#include "game_loop.h"  // expand_frame_to_hi()
#include "mouse.h"
#include "opentyr.h"
#include "palette.h"
#include "timing.h"
#include "touch_ui.h"
#include "video_crt.h"
#include "video_scale.h"

#include "console_platform.h"  // console_get_output_size()

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const scaling_mode_names[ScalingMode_MAX] = {
	"Center",
	"Integer",
	"Widescreen",
	"Classic",
};

int fullscreen_display;
#ifdef __APPLE__
bool output_vsync = true;
#else
bool output_vsync = false;
#endif
ScalingMode scaling_mode = SCALE_WIDESCREEN;  // fill the screen at true 16:9 by default

// Sub-pixel supersampling factor; 0 = Auto (follow the scaler). See video.h.
int render_supersample = 0;

#ifdef PLATFORM_HANDHELD
bool smoothie_full_res = false;  // handheld default: favor stable display-rate performance
#else
bool smoothie_full_res = true;
#endif

static void update_native_scaler_dims(void);
static void native_output_size(int *out_w, int *out_h);

// Native supersample factor. Round the output/logical ratio up so every output pixel is sampled.
static int display_supersample_factor(void)
{
	int w = vga_width, h = vga_height;
	native_output_size(&w, &h);

	const int fw = (w + vga_width - 1) / vga_width;
	const int fh = (h + vga_height - 1) / vga_height;
	int factor = fw > fh ? fw : fh;

	if (factor < 1)
		factor = 1;
	else if (factor > RENDER_SUPERSAMPLE_LIMIT)
		factor = RENDER_SUPERSAMPLE_LIMIT;
	return factor;
}

// Auto follows the scaler factor; Native follows the display and ignores RENDER_SUPERSAMPLE_MAX.
int effective_supersample(void)
{
	if (!smoothMotion)
		return 1;

	int factor = render_supersample;
	if (factor == RENDER_SUPERSAMPLE_NATIVE)
		return display_supersample_factor();

	if (factor == 0)
	{
		if (scaler_is_native(scaler))
			factor = display_supersample_factor();
		else
			factor = scalers[scaler].width / vga_width;

#ifndef __vita__
		// A 1x scaler would resolve Auto to 1x; no sub-pixel motion at all, a
		// pointless state. Auto therefore supersamples at least 2x.
		if (factor < 2)
			factor = 2;
#endif
	}
	if (factor < 1)
		factor = 1;
	else if (factor > RENDER_SUPERSAMPLE_MAX)
		factor = RENDER_SUPERSAMPLE_MAX;
	return factor;
}

bool show_fps = false;
int current_fps = 0;
static SDL_Rect last_output_rect = { 0, 0, vga_width, vga_height };

// Last presented output size, used to detect resolution changes that need repainting.
static int last_present_w = 0, last_present_h = 0;

#define VIDEO_REFRESH_MARGIN_MS 2

static Uint32 last_present_ms = 0;
static Uint32 refresh_frame_ms = 0;
static bool new_frame_presented = false;

SDL_Surface *VGAScreen, *VGAScreenSeg;
SDL_Surface *VGAScreen2;
SDL_Surface *game_screen;
static SDL_Surface* menu_screen;

// Supersampled frame JE_showVGA presents when a deferred cursor rides it; sized on demand.
static SDL_Surface *menu_hi = NULL;

static int current_x_offset = MENU_X_OFFSET;

SDL_Window *main_window = NULL;
SDL_Renderer *main_window_renderer = NULL;
SDL_PixelFormat *main_window_tex_format = NULL;
static SDL_Texture *main_window_texture = NULL;

// Texture for the supersampled present path, recreated on size change.
static SDL_Texture *hi_texture = NULL;  // streaming: the palette-converted hi frame

/* Texture used by the last present. Reuse it for overlay-only redraws, including screens
 * composed outside VGAScreen. Clear the handle when its texture is destroyed. */
static SDL_Texture *last_present_texture = NULL;
static int hi_texture_w = 0, hi_texture_h = 0;

static ScalerFunction scaler_function;

#ifndef __vita__  // side-gradient cache; unused on Vita (plain black pillarbox)
static Uint8 gradient_cache[256][MENU_X_OFFSET];
static Uint32 last_gradient_palette[256];
static bool gradient_cache_valid = false;
#endif

static void init_renderer(void);
static void deinit_renderer(void);
static bool video_rows_ready(void);
static void video_rows_shutdown(void);
static void video_rows_end_frame(void);
static void init_texture(void);
static void deinit_texture(void);

static int window_get_display_index(void);
static void window_center_in_display(int display_index);
static void native_windowed_size(int *out_w, int *out_h);
static void calc_dst_render_rect(SDL_Surface *src_surface, SDL_Rect *dst_rect);
static void scale_and_flip(SDL_Surface *);
static void blit_with_offset(SDL_Surface* src, SDL_Surface* dst, int x_offset);
#ifndef __vita__
static void update_gradient_cache(void);
#endif

void init_video(void)
{
	if (SDL_WasInit(SDL_INIT_VIDEO))
		return;

#ifdef _WIN32
	/* Request per-monitor DPI awareness before SDL video starts. Window sizes remain in
	 * points while the drawable uses the display's native pixels. */
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "1");
#endif

	if (SDL_InitSubSystem(SDL_INIT_VIDEO) == -1)
	{
		fprintf(stderr, "error: failed to initialize SDL video: %s\n", SDL_GetError());
		JE_tyrianExitWithoutSaving(1);
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
	int win_w = vga_width, win_h = vga_height;
#ifdef PLATFORM_HANDHELD
	// Create the window at the device's native output size for a 1:1 buffer. Switch keeps
	// the window resizable so SDL can follow dock transitions.
	console_get_output_size(&win_w, &win_h);
#endif

#ifdef __ANDROID__
	// A resizable window makes SDL request FULL_SENSOR orientation at creation, overriding
	// the manifest's landscape lock. This hint is what it honours instead.
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif

	/* Request native-pixel drawables on high-DPI backends. Other backends ignore the flag.
	 * See video_output_size(). */
	const Uint32 window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_ALLOW_HIGHDPI;

	main_window = SDL_CreateWindow(opentyrian_str,
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		win_w, win_h, window_flags);

	if (main_window == NULL)
	{
		fprintf(stderr, "error: failed to create window: %s\n", SDL_GetError());
		JE_tyrianExitWithoutSaving(EXIT_FAILURE);
	}

	reinit_fullscreen(fullscreen_display);
	init_renderer();
	init_texture();
	init_scaler(scaler);

	SDL_ShowWindow(main_window);

	SDL_SetRenderDrawColor(main_window_renderer, 0, 0, 0, 255);
	SDL_RenderClear(main_window_renderer);
	SDL_RenderPresent(main_window_renderer);

	// Seed the repaint watchdog with the starting output size so the first event-pump
	// poll doesn't see a spurious change (see video_repaint_if_stale()).
	video_output_size(&last_present_w, &last_present_h);

	if (render_threads != 1)
		video_rows_ready();
}

void deinit_video(void)
{
	video_rows_shutdown();
	deinit_texture();
	deinit_renderer();
	video_scale_deinit();

	SDL_DestroyWindow(main_window);
	main_window = NULL;

	SDL_FreeSurface(VGAScreenSeg);
	VGAScreen = VGAScreenSeg = NULL;
	SDL_FreeSurface(VGAScreen2);
	VGAScreen2 = NULL;
	SDL_FreeSurface(game_screen);
	game_screen = NULL;
	SDL_FreeSurface(menu_screen);
	menu_screen = NULL;
	SDL_FreeSurface(menu_hi);
	menu_hi = NULL;

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
		JE_tyrianExitWithoutSaving(EXIT_FAILURE);
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
		touch_ui_renderer_lost();   // the overlay caches textures this renderer owns
		crt_release();
		SDL_DestroyRenderer(main_window_renderer);
		main_window_renderer = NULL;
	}
}

static void init_texture(void)
{
	assert(main_window_renderer != NULL);

	// The Native scaler sizes its texture from the live window; refresh first.
	update_native_scaler_dims();

	int bpp = 32; // renderer texture format
	Uint32 format = bpp == 32 ? SDL_PIXELFORMAT_RGB888 : SDL_PIXELFORMAT_RGB565;
	int scaler_w = scalers[scaler].width;
	int scaler_h = scalers[scaler].height;

	main_window_tex_format = SDL_AllocFormat(format);

	main_window_texture = SDL_CreateTexture(main_window_renderer, format, SDL_TEXTUREACCESS_STREAMING, scaler_w, scaler_h);

	if (main_window_texture == NULL)
	{
		fprintf(stderr, "error: failed to create scaler texture %dx%dx%s: %s\n", scaler_w, scaler_h, SDL_GetPixelFormatName(format), SDL_GetError());
		JE_tyrianExitWithoutSaving(EXIT_FAILURE);
	}
}

static void deinit_texture(void)
{
	if (main_window_texture != NULL)
	{
		SDL_DestroyTexture(main_window_texture);
		main_window_texture = NULL;
		last_present_texture = NULL;
	}

	// The hi (supersample) texture belongs to the same renderer; drop it too so a
	// renderer recreate (vsync toggle, scaler change) can't leave it dangling.
	if (hi_texture != NULL)
	{
		SDL_DestroyTexture(hi_texture);
		hi_texture = NULL;
		hi_texture_w = hi_texture_h = 0;
		last_present_texture = NULL;
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

#ifdef PLATFORM_HANDHELD
	// These platforms have one always-fullscreen display and the SDL driver owns the window size.
	// Forcing FULLSCREEN_DESKTOP here pinned the Switch buffer to 1080p; leave it untouched.
	return;
#endif

	if (fullscreen_display >= SDL_GetNumVideoDisplays())
	{
		fullscreen_display = 0;
	}

	SDL_SetWindowFullscreen(main_window, SDL_FALSE);
	{
		// Native has no fixed output size (it tracks the window); give it the
		// largest clean integer-multiple window that fits the desktop instead.
		int win_w = scalers[scaler].width, win_h = scalers[scaler].height;
		if (scaler_is_native(scaler))
			native_windowed_size(&win_w, &win_h);
		SDL_SetWindowSize(main_window, win_w, win_h);
	}

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
#ifdef PLATFORM_HANDHELD
	// The SDL driver owns the window size here; the present path re-reads the live window size
	// every frame (calc_dst_render_rect), so there's nothing to reconcile.
	return;
#endif

	int w, h;
	int scaler_w, scaler_h;

	// Tell video to reinit if the window was manually resized by the user.
	// Also enforce a minimum size on the window.

	SDL_GetWindowSize(main_window, &w, &h);
	if (scaler_is_native(scaler))
	{
		// Native's output tracks the window, so it imposes no fixed minimum
		// beyond the logical screen itself.
		scaler_w = vga_width;
		scaler_h = vga_height;
	}
	else
	{
		scaler_w = scalers[scaler].width;
		scaler_h = scalers[scaler].height;
	}

	// Convert the scaler's pixel size to window points before clamping.
	{
		float scale_x, scale_y;
		video_output_pixel_scale(&scale_x, &scale_y);
		scaler_w = (int)((float)scaler_w / scale_x);
		scaler_h = (int)((float)scaler_h / scale_y);
	}

	if (w < scaler_w || h < scaler_h)
	{
		w = w < scaler_w ? scaler_w : w;
		h = h < scaler_h ? scaler_h : h;

		SDL_SetWindowSize(main_window, w, h);
	}
}

void toggle_fullscreen(void)
{
#ifdef PLATFORM_HANDHELD
	return;  // always fullscreen on these platforms; nothing to toggle
#endif

	if (fullscreen_display != -1)
		reinit_fullscreen(-1);
	else
		reinit_fullscreen(SDL_GetWindowDisplayIndex(main_window));
}

bool init_scaler(unsigned int new_scaler)
{
	int bpp = main_window_tex_format->BitsPerPixel; // preserve the active texture depth

	scaler = new_scaler;

#ifdef PLATFORM_HANDHELD
	// Driver-owned panels keep their native window size; only the scaler texture changes.
#else
	if (fullscreen_display == -1)
	{
		// In windowed mode, resize to the scaler output. Native uses the largest
		// fitting integer multiple and must be sized before texture creation.
		int w = scalers[scaler].width,
		    h = scalers[scaler].height;
		if (scaler_is_native(scaler))
			native_windowed_size(&w, &h);
		SDL_SetWindowSize(main_window, w, h);
		window_center_in_display(window_get_display_index());
	}
#endif

	deinit_texture();
	init_texture();

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
	if (screen == NULL || SDL_MUSTLOCK(screen) || screen->format->BitsPerPixel < 8)
	{
		SDL_FillRect(screen, NULL, 0);
		return;
	}

	const SDL_Rect clip = screen->clip_rect;
	if (clip.w <= 0 || clip.h <= 0)
		return;

	const size_t bpp = screen->format->BytesPerPixel;
	const size_t row_bytes = (size_t)clip.w * bpp;
	Uint8 *row = (Uint8 *)screen->pixels + (size_t)clip.y * screen->pitch + (size_t)clip.x * bpp;
	if (row_bytes == (size_t)screen->pitch)
	{
		memset(row, 0, row_bytes * clip.h);
		return;
	}
	for (int y = 0; y < clip.h; ++y, row += screen->pitch)
		memset(row, 0, row_bytes);
}

SDL_Surface *video_compose_frame(void)
{
	if (current_x_offset == 0)
		return VGAScreen;

	blit_with_offset(VGAScreen, menu_screen, current_x_offset);
	return menu_screen;
}

SDL_Surface *video_ensure_scaled_surface(SDL_Surface **surf, int scale)
{
	const int w = vga_width * scale, h = vga_height * scale;
	if (*surf != NULL && ((*surf)->w != w || (*surf)->h != h))
	{
		SDL_FreeSurface(*surf);
		*surf = NULL;
	}
	if (*surf == NULL)
		*surf = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
	return *surf;
}

void JE_showVGA(void)
{
	SDL_Surface *frame = video_compose_frame();

	// A visible deferred cursor rides a block-expanded hi frame at its fractional position.
	if (mouse_cursor_wants_hi())
	{
		const int ss = effective_supersample();
		SDL_Surface *const hi = ss > 1 ? video_ensure_scaled_surface(&menu_hi, ss) : NULL;
		if (hi != NULL)
		{
			expand_frame_to_hi(frame, hi, ss);
			JE_drawMouseToHiFrame(hi, ss, current_x_offset);
			present_hi(hi);
			return;
		}
	}

	if (frame != VGAScreen)
	{
		// Draw the cursor onto the composited frame (after the side gradient is
		// built from the clean content) so it doesn't smear into the pillarbox.
		JE_drawMouseToMenuScreen(frame, current_x_offset);
	}
	else if (mouse_cursor_deferred())
	{
		// No hi frame to land on: draw on a menu_screen copy, never into VGAScreen.
		if (mouse_cursor_wants_hi())
		{
			for (int y = 0; y < vga_height; ++y)
				memcpy((Uint8 *)menu_screen->pixels + y * menu_screen->pitch,
				       (const Uint8 *)VGAScreen->pixels + y * VGAScreen->pitch, vga_width);
			frame = menu_screen;
		}
		JE_drawMouseToMenuScreen(frame, current_x_offset);
	}

	scale_and_flip(frame);
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

/* Renderer output size in pixels, falling back to window points before a renderer exists. */
void video_output_size(int *out_w, int *out_h)
{
	int w = 0, h = 0;

	if (main_window_renderer == NULL
	    || SDL_GetRendererOutputSize(main_window_renderer, &w, &h) != 0 || w <= 0 || h <= 0)
	{
		if (main_window != NULL)
			SDL_GetWindowSize(main_window, &w, &h);
	}

	if (out_w != NULL)
		*out_w = w;
	if (out_h != NULL)
		*out_h = h;
}

// Output pixels per window point, used to map point-based input and limits. Pointers may be NULL.
void video_output_pixel_scale(float *out_sx, float *out_sy)
{
	float sx = 1.f, sy = 1.f;

	int win_w = 0, win_h = 0, out_w = 0, out_h = 0;
	if (main_window != NULL)
	{
		SDL_GetWindowSize(main_window, &win_w, &win_h);
		video_output_size(&out_w, &out_h);

		if (win_w > 0 && out_w > 0)
			sx = (float)out_w / (float)win_w;
		if (win_h > 0 && out_h > 0)
			sy = (float)out_h / (float)win_h;
	}

	if (out_sx != NULL)
		*out_sx = sx;
	if (out_sy != NULL)
		*out_sy = sy;
}

// Fitted output size used by the Native scaler and Native sub-pixel factor. Before window creation,
// return the logical size.
static void native_output_size(int *out_w, int *out_h)
{
	SDL_Rect r = { 0, 0, vga_width, vga_height };

	if (main_window != NULL)
	{
		int px_w, px_h;
		video_output_size(&px_w, &px_h);

		const float pixel_aspect = (float)vga_width / (float)vga_height;

		switch (scaling_mode)
		{
		case SCALE_INTEGER:
			while (r.w + vga_width <= px_w && r.h + vga_height <= px_h)
			{
				r.w += vga_width;
				r.h += vga_height;
			}
			break;
		case SCALE_CLASSIC_PAR:
			fit_rect_to_aspect(&r, px_w, px_h, pixel_aspect * (5.f / 6.f));
			break;
		case SCALE_CENTER:  // no fixed size to center on; fill like Widescreen
		case SCALE_WIDESCREEN:
		default:
			fit_rect_to_aspect(&r, px_w, px_h, pixel_aspect);
			break;
		}
	}

	*out_w = r.w < vga_width ? vga_width : r.w;
	*out_h = r.h < vga_height ? vga_height : r.h;
}

// Refresh the Native scaler's table entry from the live output size; cheap, so callers
// run it before any use of the entry's dimensions.
static void update_native_scaler_dims(void)
{
	if (main_window == NULL)
		return;

	int w, h;
	native_output_size(&w, &h);
	scaler_set_native_size(w, h);
}

// Native window size is the largest clean integer multiple that fits the desktop.
static void native_windowed_size(int *out_w, int *out_h)
{
	int factor = 1;

	int display = window_get_display_index();
	if (display < 0)
		display = 0;

	SDL_Rect bounds;
	if (SDL_GetDisplayUsableBounds(display, &bounds) == 0)
	{
		const int fw = bounds.w / vga_width,
		          fh = bounds.h / vga_height;
		factor = fw < fh ? fw : fh;
		if (factor < 1)
			factor = 1;
	}

	*out_w = vga_width * factor;
	*out_h = vga_height * factor;
}

static void calc_dst_render_rect(SDL_Surface *const src_surface, SDL_Rect *const dst_rect)
{
	// Decides how the logical output texture (after software scaling applied) will fit
	// in the window.

	int out_w, out_h;
	video_output_size(&out_w, &out_h);

	// Square-pixel ratio of the framebuffer itself (356:200 = 16:9).
	const float pixel_aspect = (float)src_surface->w / (float)src_surface->h;

	switch (scaling_mode)
	{
	case SCALE_CENTER:
		// The texture's own size, in the points the window was sized in. Measuring it in
		// output pixels would shrink the frame to a corner of a high-DPI drawable.
		{
			float scale_x, scale_y;
			video_output_pixel_scale(&scale_x, &scale_y);

			SDL_QueryTexture(main_window_texture, NULL, NULL, &dst_rect->w, &dst_rect->h);
			dst_rect->w = (int)((float)dst_rect->w * scale_x);
			dst_rect->h = (int)((float)dst_rect->h * scale_y);
		}
		break;
	case SCALE_INTEGER:
		dst_rect->w = src_surface->w;
		dst_rect->h = src_surface->h;
		while (dst_rect->w + src_surface->w <= out_w && dst_rect->h + src_surface->h <= out_h)
		{
			dst_rect->w += src_surface->w;
			dst_rect->h += src_surface->h;
		}
		break;
	case SCALE_WIDESCREEN:
		// True widescreen: square pixels, i.e. the buffer's own ratio (16:9).
		fit_rect_to_aspect(dst_rect, out_w, out_h, pixel_aspect);
		break;
	case SCALE_CLASSIC_PAR:
		// Original DOS pixel aspect (PAR 5/6): taller pixels, ~3:2 overall.
		fit_rect_to_aspect(dst_rect, out_w, out_h, pixel_aspect * (5.f / 6.f));
		break;
	case ScalingMode_MAX:
		assert(false);
		break;
	}

	dst_rect->x = (out_w - dst_rect->w) / 2;
	dst_rect->y = (out_h - dst_rect->h) / 2;
}

// Sample the presented-frame rate once a second for the optional FPS counter.
// Shared by both present paths (scale_and_flip and present_hi).
static void sample_fps(void)
{
	static Uint32 fps_sample_start = 0;
	static int fps_frames = 0;

	++fps_frames;
	const Uint32 now = SDL_GetTicks();
	if (fps_sample_start == 0)
		fps_sample_start = now;
	else if (now - fps_sample_start >= 1000)
	{
		current_fps = (int)(fps_frames * 1000u / (now - fps_sample_start));
		fps_frames = 0;
		fps_sample_start = now;
	}
}

static void video_presented(bool new_frame)
{
	last_present_ms = SDL_GetTicks();
	refresh_frame_ms = 0;
	if (new_frame)
		new_frame_presented = true;
}

static void scale_and_flip(SDL_Surface *src_surface)
{
	assert(src_surface->format->BitsPerPixel == 8);

	// Recreate Native's texture when the exact output size changes.
	if (scaler_is_native(scaler))
	{
		update_native_scaler_dims();

		int tw = 0, th = 0;
		if (main_window_texture != NULL)
			SDL_QueryTexture(main_window_texture, NULL, NULL, &tw, &th);
		if (tw != scalers[scaler].width || th != scalers[scaler].height)
		{
			SDL_Texture *fresh = SDL_CreateTexture(main_window_renderer,
				main_window_tex_format->format, SDL_TEXTUREACCESS_STREAMING,
				scalers[scaler].width, scalers[scaler].height);
			if (fresh != NULL)
			{
				if (main_window_texture != NULL)
					SDL_DestroyTexture(main_window_texture);
				main_window_texture = fresh;
				last_present_texture = NULL;
			}
			// else: keep presenting through the old texture (stretched, not 1:1)
		}
	}

	// Do software scaling
	assert(scaler_function != NULL);
	scaler_function(src_surface, main_window_texture);

	SDL_Rect dst_rect;
	calc_dst_render_rect(src_surface, &dst_rect);

	crt_present_frame(main_window_texture, &dst_rect);
	touch_ui_render(main_window_renderer, &dst_rect);
	SDL_RenderPresent(main_window_renderer);
	video_presented(true);

	sample_fps();
	video_rows_end_frame();

	// Save output rect to be used by mouse functions
	last_output_rect = dst_rect;
	last_present_texture = main_window_texture;
}

// Re-present the existing output texture for modal system overlays such as the Vita IME.
void video_repeat_last_present(void)
{
	if (main_window_renderer == NULL)
		return;

	crt_present_frame(last_present_texture, &last_output_rect);
	touch_ui_render(main_window_renderer, &last_output_rect);
	SDL_RenderPresent(main_window_renderer);
	video_presented(false);
}

bool video_take_new_frame(void)
{
	const bool presented = new_frame_presented;
	new_frame_presented = false;
	return presented;
}

static Uint32 video_refresh_frame_ms(int hz, bool vsync, int cap)
{
	if (hz <= 0)
		hz = 60;
	if (!vsync && cap > 0 && cap < hz)
		hz = cap;
	return (Uint32)((1000 + hz - 1) / hz);
}

static Uint32 video_idle_nap(Uint32 since_ms, Uint32 nap_ms, Uint32 frame_due_ms, Uint32 frame_ms,
                             bool vsync)
{
	if (frame_due_ms < frame_ms + VIDEO_REFRESH_MARGIN_MS)
		return nap_ms;

	const Uint32 due_ms = vsync ? MAX(frame_ms / 2, 1u) : frame_ms;
	return since_ms < due_ms ? MIN(nap_ms, due_ms - since_ms) : 0;
}

static int video_display_hz(void)
{
	SDL_DisplayMode mode = { 0 };
	const int display = SDL_GetWindowDisplayIndex(main_window);
	return display >= 0 && SDL_GetCurrentDisplayMode(display, &mode) == 0 ? mode.refresh_rate : 0;
}

Uint32 video_idle_refresh(Uint32 nap_ms, Uint32 frame_due_ms)
{
	const Uint32 now = SDL_GetTicks();
	if (last_present_texture == NULL || !crt_animating(now) ||
	    (SDL_GetWindowFlags(main_window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) != 0)
		return nap_ms;

	if (refresh_frame_ms == 0)
		refresh_frame_ms = video_refresh_frame_ms(video_display_hz(), output_vsync, fps_cap);
	const Uint32 nap = video_idle_nap(now - last_present_ms, nap_ms, frame_due_ms, refresh_frame_ms,
	                                  output_vsync);
	if (nap == 0)
		video_repeat_last_present();
	return nap;
}

// (Re)create the hi texture at the given size. Returns false on failure so the
// caller can fall back to the classic present path.
static bool ensure_hi_texture(int w, int h)
{
	if (hi_texture != NULL && hi_texture_w == w && hi_texture_h == h)
		return true;

	if (hi_texture != NULL)
	{
		SDL_DestroyTexture(hi_texture);
		hi_texture = NULL;
		last_present_texture = NULL;
	}

	hi_texture = SDL_CreateTexture(main_window_renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, w, h);
	if (hi_texture == NULL)
	{
		hi_texture_w = hi_texture_h = 0;
		return false;
	}
	hi_texture_w = w;
	hi_texture_h = h;
	return true;
}

typedef enum
{
	VIDEO_ROWS_UNTRIED,
	VIDEO_ROWS_READY,
	VIDEO_ROWS_OFF,
} VideoRowsState;

#define VIDEO_ROWS_MAX_HELPERS  15
#define VIDEO_ROWS_MIN_PIXELS   (64 * 1024)
#define VIDEO_ROWS_TUNE_FRAMES  20
#define VIDEO_ROWS_WATCH_FRAMES 120
#define VIDEO_ROWS_PROBE_MS     2000

static struct
{
	VideoRowsState state;
	int helpers;
	bool stuck;
	SDL_Thread *thread[VIDEO_ROWS_MAX_HELPERS];
	SDL_sem *go[VIDEO_ROWS_MAX_HELPERS];
	SDL_sem *done;
	SDL_atomic_t quit;
	VideoRowsFn fn;
	void *job;
	int begin[VIDEO_ROWS_MAX_HELPERS];
	int end[VIDEO_ROWS_MAX_HELPERS];
} video_rows_pool;

int render_threads = 0;

static const int video_rows_counts[] = { 1, 2, 4, 8, 16 };

typedef struct
{
	int pixels;
	int tune_frame;
	int candidates;
	int count[COUNTOF(video_rows_counts) + 1];
	Uint64 samples[COUNTOF(video_rows_counts) + 1][VIDEO_ROWS_TUNE_FRAMES];
	Uint64 median[COUNTOF(video_rows_counts) + 1];
	Uint64 p95[COUNTOF(video_rows_counts) + 1];
	int chosen;
	Uint64 tuned;
	int watch_frames, watch_high, watch_low, watch_windows;
	Uint32 calls;
	bool small;
	bool rounds;
	bool round_open;
	int round_candidate;
	int round_pixels;
	Uint64 round_spent;
} VideoRowsSite;

static VideoRowsSite video_rows_sites[VIDEO_ROWS_SITES] = {
	[VIDEO_ROWS_REPLAY] = { .rounds = true },
	[VIDEO_ROWS_SMOOTHIE] = { .rounds = true },
	[VIDEO_ROWS_TICK] = { .rounds = true },
};
static const char *const video_rows_site_names[VIDEO_ROWS_SITES] = {
	"hi-res present", "Native scaler", "playfield replay", "hi-res compose", "smoothie layers",
	"tick replay", "plain scaler"
};

static int SDLCALL video_rows_worker(void *arg)
{
	const int i = (int)(intptr_t)arg;
	for (;;)
	{
		SDL_SemWait(video_rows_pool.go[i]);
		if (SDL_AtomicGet(&video_rows_pool.quit))
			return 0;
		video_rows_pool.fn(video_rows_pool.job, video_rows_pool.begin[i], video_rows_pool.end[i]);
		SDL_SemPost(video_rows_pool.done);
	}
}

static void video_rows_dispatch(VideoRowsFn fn, void *job, int rows, int helpers)
{
	const int bands = helpers + 1;
	video_rows_pool.fn = fn;
	video_rows_pool.job = job;
	for (int i = 0; i < helpers; ++i)
	{
		video_rows_pool.begin[i] = (int)((Sint64)rows * (i + 1) / bands);
		video_rows_pool.end[i] = (int)((Sint64)rows * (i + 2) / bands);
		SDL_SemPost(video_rows_pool.go[i]);
	}
	fn(job, 0, (int)((Sint64)rows / bands));
}

static void video_rows_run(VideoRowsFn fn, void *job, int rows, int helpers)
{
	video_rows_dispatch(fn, job, rows, helpers);
	for (int i = 0; i < helpers; ++i)
		SDL_SemWait(video_rows_pool.done);
}

typedef struct
{
	const Uint8 *src;
	Uint32 *dst;
	int width;
} VideoRowsProbe;

static void video_rows_probe_rows(void *p, int row_begin, int row_end)
{
	const VideoRowsProbe *const job = p;
	for (int y = row_begin; y < row_end; ++y)
		for (int x = 0; x < job->width; ++x)
			job->dst[y * job->width + x] = (job->src[y * job->width + x] * 2654435761u) ^ (Uint32)y;
}

static bool video_rows_probe(void)
{
	enum { PROBE_W = 64, PROBE_H = 32 };
	static Uint8 src[PROBE_W * PROBE_H];
	static Uint32 dst[PROBE_W * PROBE_H], ref[PROBE_W * PROBE_H];
	static VideoRowsProbe job;

	for (int i = 0; i < PROBE_W * PROBE_H; ++i)
		src[i] = (Uint8)(i * 37 + 11);
	job = (VideoRowsProbe){ src, ref, PROBE_W };
	video_rows_probe_rows(&job, 0, PROBE_H);
	job.dst = dst;

	video_rows_dispatch(video_rows_probe_rows, &job, PROBE_H, video_rows_pool.helpers);
	for (int i = 0; i < video_rows_pool.helpers; ++i)
	{
		if (SDL_SemWaitTimeout(video_rows_pool.done, VIDEO_ROWS_PROBE_MS) != 0)
		{
			video_rows_pool.stuck = true;
			return false;
		}
	}
	return memcmp(dst, ref, sizeof(dst)) == 0;
}

static void video_rows_stop(void)
{
	SDL_AtomicSet(&video_rows_pool.quit, 1);
	for (int i = 0; i < video_rows_pool.helpers; ++i)
		SDL_SemPost(video_rows_pool.go[i]);

	if (video_rows_pool.stuck)
	{
		for (int i = 0; i < video_rows_pool.helpers; ++i)
			SDL_DetachThread(video_rows_pool.thread[i]);
		return;
	}

	for (int i = 0; i < video_rows_pool.helpers; ++i)
	{
		SDL_WaitThread(video_rows_pool.thread[i], NULL);
		SDL_DestroySemaphore(video_rows_pool.go[i]);
		video_rows_pool.thread[i] = NULL;
		video_rows_pool.go[i] = NULL;
	}
	SDL_DestroySemaphore(video_rows_pool.done);
	video_rows_pool.done = NULL;
	video_rows_pool.helpers = 0;
}

int video_rows_max_threads(void)
{
#if defined(__vita__) || defined(__SWITCH__)
	const int cpus = 3;
#else
	const int cpus = SDL_GetCPUCount();
#endif
	if (cpus < 1)
		return 1;
	return cpus > VIDEO_ROWS_MAX_HELPERS + 1 ? VIDEO_ROWS_MAX_HELPERS + 1 : cpus;
}

static bool video_rows_start(void)
{
	const int helpers = video_rows_max_threads() - 1;
	if (helpers < 1)
		return false;

	SDL_AtomicSet(&video_rows_pool.quit, 0);
	video_rows_pool.stuck = false;
	video_rows_pool.helpers = 0;
	video_rows_pool.done = SDL_CreateSemaphore(0);
	if (video_rows_pool.done == NULL)
		return false;

	for (int i = 0; i < helpers; ++i)
	{
		video_rows_pool.go[i] = SDL_CreateSemaphore(0);
		if (video_rows_pool.go[i] == NULL)
			break;
		video_rows_pool.thread[i] = SDL_CreateThread(video_rows_worker, "video_rows",
		                                             (void *)(intptr_t)i);
		if (video_rows_pool.thread[i] == NULL)
		{
			SDL_DestroySemaphore(video_rows_pool.go[i]);
			video_rows_pool.go[i] = NULL;
			break;
		}
		video_rows_pool.helpers = i + 1;
	}

	if (video_rows_pool.helpers > 0 && video_rows_probe())
		return true;

	video_rows_stop();
	return false;
}

static bool video_rows_ready(void)
{
	if (video_rows_pool.state == VIDEO_ROWS_UNTRIED)
	{
		video_rows_pool.state = video_rows_start() ? VIDEO_ROWS_READY : VIDEO_ROWS_OFF;
		if (video_rows_pool.state == VIDEO_ROWS_OFF)
			fprintf(stderr, "video: frame rows stay on one thread\n");
	}
	return video_rows_pool.state == VIDEO_ROWS_READY;
}

static void video_rows_shutdown(void)
{
	if (video_rows_pool.state != VIDEO_ROWS_READY)
		return;
	video_rows_stop();
	video_rows_pool.state = VIDEO_ROWS_UNTRIED;
}

static void video_rows_tune_begin(VideoRowsSite *s, int pixels, int pool_threads)
{
	s->pixels = pixels;
	s->tune_frame = 0;
	s->chosen = 0;
	s->candidates = 0;
	for (size_t i = 0; i < COUNTOF(video_rows_counts) && video_rows_counts[i] <= pool_threads; ++i)
		s->count[s->candidates++] = video_rows_counts[i];
	if (s->count[s->candidates - 1] != pool_threads)
		s->count[s->candidates++] = pool_threads;
	s->watch_frames = s->watch_high = s->watch_low = s->watch_windows = 0;
}

static void video_rows_spread(const Uint64 *samples, Uint64 *median, Uint64 *p95)
{
	Uint64 sorted[VIDEO_ROWS_TUNE_FRAMES];
	for (int i = 0; i < VIDEO_ROWS_TUNE_FRAMES; ++i)
	{
		int j = i;
		for (; j > 0 && sorted[j - 1] > samples[i]; --j)
			sorted[j] = sorted[j - 1];
		sorted[j] = samples[i];
	}
	*median = (sorted[(VIDEO_ROWS_TUNE_FRAMES - 1) / 2] + sorted[VIDEO_ROWS_TUNE_FRAMES / 2]) / 2;
	*p95 = sorted[(VIDEO_ROWS_TUNE_FRAMES * 95 + 99) / 100 - 1];
}

static void video_rows_measure(VideoRowsSite *s)
{
	for (int i = 0; i < s->candidates; ++i)
		video_rows_spread(s->samples[i], &s->median[i], &s->p95[i]);
}

static int video_rows_pick(const VideoRowsSite *s)
{
	int pick = 0;
	for (int i = 1; i < s->candidates; ++i)
		if (s->median[i] * 10 < s->median[pick] * 9 && s->p95[i] <= s->p95[pick])
			pick = i;
	return pick;
}

static void video_rows_tune_end(int site, VideoRowsSite *s)
{
	video_rows_measure(s);
	const int pick = video_rows_pick(s);
	s->chosen = s->count[pick];
	s->tuned = s->median[pick];

	const double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
	char timings[256];
	size_t used = 0;
	for (int i = 0; i < s->candidates && used < sizeof(timings); ++i)
		used += (size_t)snprintf(timings + used, sizeof(timings) - used, "%s%.3f/%.3f ms on %d",
		                         i != 0 ? ", " : "", (double)s->median[i] * ms,
		                         (double)s->p95[i] * ms, s->count[i]);
	if (s->chosen == 1)
		fprintf(stderr, "video: %s rows on one thread (median/p95 %s)\n",
		        video_rows_site_names[site], timings);
	else
		fprintf(stderr, "video: %s rows on %d threads (median/p95 %s)\n",
		        video_rows_site_names[site], s->chosen, timings);
}

static void video_rows_watch(int site, VideoRowsSite *s, Uint64 spent)
{
	if (s->tuned == 0)
		return;
	if (spent > s->tuned * 2)
		++s->watch_high;
	else if (spent * 2 < s->tuned)
		++s->watch_low;
	if (++s->watch_frames < VIDEO_ROWS_WATCH_FRAMES)
		return;

	const bool moved = s->watch_high * 2 > VIDEO_ROWS_WATCH_FRAMES
	                || s->watch_low * 2 > VIDEO_ROWS_WATCH_FRAMES;
	s->watch_windows = moved ? s->watch_windows + 1 : 0;
	s->watch_frames = s->watch_high = s->watch_low = 0;
	if (s->watch_windows < 2)
		return;

	const double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
	fprintf(stderr, "video: %s rows moved from %.3f ms per frame; tuning again\n",
	        video_rows_site_names[site], (double)s->tuned * ms);
	video_rows_tune_begin(s, s->pixels, video_rows_pool.helpers + 1);
}

static void video_rows_sample(int site, VideoRowsSite *s, int candidate, Uint64 spent)
{
	if (candidate < 0)
	{
		video_rows_watch(site, s, spent);
		return;
	}
	s->samples[candidate][s->tune_frame / s->candidates] = spent;
	if (++s->tune_frame == s->candidates * VIDEO_ROWS_TUNE_FRAMES)
		video_rows_tune_end(site, s);
}

static void video_rows_round_begin(VideoRowsSite *s, int pixels, int pool_threads)
{
	if (s->candidates == 0)
		video_rows_tune_begin(s, pixels, pool_threads);
	s->round_open = true;
	s->round_pixels = 0;
	s->round_spent = 0;
	s->round_candidate = s->chosen != 0 ? -1 : s->tune_frame % s->candidates;
}

static void video_rows_end_frame(void)
{
	for (int site = 0; site < VIDEO_ROWS_SITES; ++site)
	{
		VideoRowsSite *const s = &video_rows_sites[site];
		if (!s->round_open)
			continue;
		s->round_open = false;
		if (s->round_pixels != s->pixels)
		{
			video_rows_tune_begin(s, s->round_pixels, video_rows_pool.helpers + 1);
			continue;
		}
		video_rows_sample(site, s, s->round_candidate, s->round_spent);
	}
}

void video_rows(int site, VideoRowsFn fn, void *job, int rows, int pixels, SDL_Texture *unlock)
{
	assert(site >= 0 && site < VIDEO_ROWS_SITES);
	VideoRowsSite *const s = &video_rows_sites[site];
	++s->calls;
	s->small = pixels < VIDEO_ROWS_MIN_PIXELS || rows < 2;

	int threads = 1;
	int candidate = -1;
	bool timed = false;
	if (!s->small && render_threads != 1 && video_rows_ready())
	{
		const int pool_threads = video_rows_pool.helpers + 1;
		if (render_threads > 1)
		{
			threads = MIN(render_threads, pool_threads);
		}
		else if (s->rounds)
		{
			if (!s->round_open)
				video_rows_round_begin(s, pixels, pool_threads);
			if (pixels > s->round_pixels)
				s->round_pixels = pixels;
			candidate = s->round_candidate;
			threads = candidate >= 0 ? s->count[candidate] : s->chosen;
			timed = true;
		}
		else
		{
			if (s->pixels != pixels)
				video_rows_tune_begin(s, pixels, pool_threads);
			if (s->chosen != 0)
			{
				threads = s->chosen;
			}
			else
			{
				candidate = s->tune_frame % s->candidates;
				threads = s->count[candidate];
			}
			timed = true;
		}
	}

	const Uint64 start = timed ? SDL_GetPerformanceCounter() : 0;
	video_rows_run(fn, job, rows, MIN(threads, rows) - 1);
	if (unlock != NULL)
		SDL_UnlockTexture(unlock);
	if (!timed)
		return;

	const Uint64 spent = SDL_GetPerformanceCounter() - start;
	if (s->rounds)
		s->round_spent += spent;
	else
		video_rows_sample(site, s, candidate, spent);
}

bool video_rows_threaded(VideoRowsFn fn, void *job, int rows)
{
	if (!video_rows_ready())
	{
		fn(job, 0, rows);
		return false;
	}
	video_rows_run(fn, job, rows, video_rows_pool.helpers);
	return true;
}

void video_rows_stats(int site, VideoRowsStats *stats)
{
	assert(site >= 0 && site < VIDEO_ROWS_SITES);
	const VideoRowsSite *const s = &video_rows_sites[site];
	*stats = (VideoRowsStats){ .calls = s->calls, .site = site, .mode = VIDEO_ROWS_UNUSED };
	if (s->calls == 0)
		return;
	if (s->small)
	{
		stats->mode = VIDEO_ROWS_SMALL;
		return;
	}
	if (render_threads == 1)
	{
		stats->mode = VIDEO_ROWS_FIXED;
		stats->threads = 1;
		return;
	}
	if (video_rows_pool.state != VIDEO_ROWS_READY)
	{
		stats->mode = VIDEO_ROWS_NO_POOL;
		return;
	}

	const int pool_threads = video_rows_pool.helpers + 1;
	if (render_threads > 1)
	{
		stats->mode = VIDEO_ROWS_FIXED;
		stats->threads = MIN(render_threads, pool_threads);
		return;
	}
	if (s->chosen == 0)
	{
		stats->mode = VIDEO_ROWS_TUNING;
		stats->threads = pool_threads;
		return;
	}

	int shown = 1;
	for (int i = 1; i < s->candidates; ++i)
	{
		const bool better = s->chosen > 1 ? s->count[i] == s->chosen
		                                  : s->median[i] < s->median[shown];
		if (better)
			shown = i;
	}
	const double ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
	stats->mode = s->chosen > 1 ? VIDEO_ROWS_THREADED : VIDEO_ROWS_ONE_THREAD;
	stats->threads = s->chosen;
	stats->one_ms = (double)s->median[0] * ms;
	stats->many_ms = (double)s->median[shown] * ms;
}

typedef struct
{
	const Uint8 *src;
	int src_pitch;
	Uint8 *dst;
	int dst_pitch;
	int width;
} PaletteRows;

static void palette_rows(void *p, int row_begin, int row_end)
{
	const PaletteRows *const job = p;

	// Eight lookups per iteration. This pass converts vga_width*vga_height*N*N pixels every
	// frame, and a one-pixel loop stalls on the dependent index load and palette gather.
	const Uint8 *src_row = job->src + (size_t)row_begin * job->src_pitch;
	Uint8 *dst_row = job->dst + (size_t)row_begin * job->dst_pitch;
	for (int y = row_begin; y < row_end; ++y)
	{
		const Uint8 *src = src_row;
		Uint32 *dst = (Uint32 *)dst_row;

		int x = job->width;
		for (; x >= 8; x -= 8, src += 8, dst += 8)
		{
			dst[0] = rgb_palette[src[0]];
			dst[1] = rgb_palette[src[1]];
			dst[2] = rgb_palette[src[2]];
			dst[3] = rgb_palette[src[3]];
			dst[4] = rgb_palette[src[4]];
			dst[5] = rgb_palette[src[5]];
			dst[6] = rgb_palette[src[6]];
			dst[7] = rgb_palette[src[7]];
		}
		for (; x > 0; --x)
			*dst++ = rgb_palette[*src++];

		src_row += job->src_pitch;
		dst_row += job->dst_pitch;
	}
}

void video_palette_rows(const Uint8 *src, int src_pitch, Uint8 *dst, int dst_pitch, int width,
                        int rows, SDL_Texture *unlock)
{
	const PaletteRows job = { src, src_pitch, dst, dst_pitch, width };
	video_rows(VIDEO_ROWS_FLAT, palette_rows, (void *)&job, rows, width * rows, unlock);
}

void present_hi(SDL_Surface *hi)
{
	assert(hi->format->BitsPerPixel == 8);

	if (!ensure_hi_texture(hi->w, hi->h))
	{
		// Fall back to the logical frame if the high-resolution texture cannot be built.
		scale_and_flip(VGAScreen);
		return;
	}

	// Palette-convert the 8-bit hi frame 1:1 into the texture (rgb_palette is
	// already in the texture's pixel format).
	{
		void *tex_pixels = NULL;
		int tex_pitch = 0;
		if (SDL_LockTexture(hi_texture, NULL, &tex_pixels, &tex_pitch) != 0)
			return;

		const PaletteRows job = { hi->pixels, hi->pitch, tex_pixels, tex_pitch, hi->w };
		video_rows(VIDEO_ROWS_HI, palette_rows, (void *)&job, hi->h, hi->w * hi->h, hi_texture);
	}

	// Supersampling uses the classic destination rectangle and never changes output size.
	SDL_Rect dst_rect;
	calc_dst_render_rect(VGAScreen, &dst_rect);

	// Present supersampled output with one unfiltered nearest-neighbor copy.
	SDL_SetTextureScaleMode(hi_texture, SDL_ScaleModeNearest);

	crt_present_frame(hi_texture, &dst_rect);
	touch_ui_render(main_window_renderer, &dst_rect);
	SDL_RenderPresent(main_window_renderer);
	video_presented(true);

	sample_fps();
	video_rows_end_frame();

	// Keep the mouse mapping in sync with what is actually on screen.
	last_output_rect = dst_rect;
	last_present_texture = hi_texture;
}

bool video_test_parallel_rows(char *detail, size_t detailSize)
{
	static const int sizes[][2] = { { 1424, 800 }, { 997, 601 }, { 13, 3 }, { 5, 2 }, { 1, 1 } };
	const char *fault = NULL;
	bool threaded = false;
	Uint32 seed = 0x5eed1234u;

	for (size_t k = 0; k < COUNTOF(sizes) && fault == NULL; ++k)
	{
		const int w = sizes[k][0], h = sizes[k][1];
		const int src_pitch = w + 3, dst_pitch = w * 4 + 12;
		Uint8 *const src = malloc((size_t)src_pitch * h);
		Uint8 *const a = malloc((size_t)dst_pitch * h);
		Uint8 *const b = malloc((size_t)dst_pitch * h);
		if (src == NULL || a == NULL || b == NULL)
		{
			fault = "allocation failed";
		}
		else
		{
			for (size_t i = 0; i < (size_t)src_pitch * h; ++i)
			{
				seed = seed * 1664525u + 1013904223u;
				src[i] = (Uint8)(seed >> 24);
			}
			memset(a, 0xcd, (size_t)dst_pitch * h);
			memset(b, 0xcd, (size_t)dst_pitch * h);
			PaletteRows one = { src, src_pitch, a, dst_pitch, w };
			PaletteRows many = { src, src_pitch, b, dst_pitch, w };
			palette_rows(&one, 0, h);
			threaded |= video_rows_threaded(palette_rows, &many, h);
			for (int y = 0; y < h && fault == NULL; ++y)
			{
				const Uint32 *const row = (const Uint32 *)(a + (size_t)y * dst_pitch);
				for (int x = 0; x < w && fault == NULL; ++x)
					if (row[x] != rgb_palette[src[(size_t)y * src_pitch + x]])
						fault = "palette rows differ from a direct lookup";
			}
			if (fault == NULL && memcmp(a, b, (size_t)dst_pitch * h) != 0)
				fault = "threaded palette rows differ from one thread";
			for (int t = 2; fault == NULL && threaded && t <= video_rows_pool.helpers + 1; ++t)
			{
				memset(b, 0xcd, (size_t)dst_pitch * h);
				video_rows_run(palette_rows, &many, h, t - 1);
				if (memcmp(a, b, (size_t)dst_pitch * h) != 0)
					fault = "palette rows on part of the pool differ from one thread";
			}
		}
		free(src);
		free(a);
		free(b);
	}

	printf("# frame rows: %d helper thread(s), threaded path %s\n", video_rows_pool.helpers,
	       threaded ? "ran" : "unavailable");
	if (fault == NULL && !threaded && SDL_GetCPUCount() > 1)
		fault = "frame-row threads did not start on a multi-core machine";
	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

bool video_test_rows_stats(char *detail, size_t detailSize)
{
	enum { W = 512, H = VIDEO_ROWS_MIN_PIXELS / W };
	Uint8 *const src = calloc((size_t)W * H, 1);
	Uint8 *const dst = malloc((size_t)W * H * 4);
	const char *fault = NULL;

	if (src == NULL || dst == NULL)
	{
		fault = "allocation failed";
	}
	else
	{
		PaletteRows job = { src, W, dst, W * 4, W };
		VideoRowsStats before, hi_before, after;

		video_rows_stats(VIDEO_ROWS_FIT, &before);
		video_rows_stats(VIDEO_ROWS_HI, &hi_before);
		video_rows(VIDEO_ROWS_FIT, palette_rows, &job, H - 1, W * (H - 1), NULL);
		video_rows_stats(VIDEO_ROWS_FIT, &after);
		if (after.calls != before.calls + 1 || after.site != VIDEO_ROWS_FIT
		    || after.mode != VIDEO_ROWS_SMALL)
			fault = "a job under the pixel threshold was not reported as small";

		video_rows_stats(VIDEO_ROWS_HI, &after);
		if (fault == NULL && after.calls != hi_before.calls)
			fault = "a call to one site changed another site's count";

		video_rows(VIDEO_ROWS_HI, palette_rows, &job, 1, W * H, NULL);
		video_rows_stats(VIDEO_ROWS_HI, &after);
		if (fault == NULL && (after.calls != hi_before.calls + 1 || after.mode != VIDEO_ROWS_SMALL))
			fault = "a one-row job was not reported as small";

		const int saved_threads = render_threads;
		render_threads = 0;
		const bool pool = video_rows_ready();
		VideoRowsSite *const s = &video_rows_sites[VIDEO_ROWS_HI];
		s->pixels = 0;
		for (int i = 1; fault == NULL; ++i)
		{
			video_rows(VIDEO_ROWS_HI, palette_rows, &job, H, W * H, NULL);
			video_rows_stats(VIDEO_ROWS_HI, &after);
			if (!pool)
			{
				if (after.mode != VIDEO_ROWS_NO_POOL)
					fault = "the reported mode does not follow the tuning run";
				break;
			}

			const bool last = i == s->candidates * VIDEO_ROWS_TUNE_FRAMES;
			VideoRowsMode want = VIDEO_ROWS_TUNING;
			int want_threads = video_rows_pool.helpers + 1;
			if (last)
			{
				want = s->chosen > 1 ? VIDEO_ROWS_THREADED : VIDEO_ROWS_ONE_THREAD;
				want_threads = s->chosen;
			}
			if (after.mode != want || (s->chosen != 0) != last)
				fault = "the reported mode does not follow the tuning run";
			else if (after.threads != want_threads)
				fault = "the reported thread count does not match the tuning run";
			if (last)
				break;
		}
		if (fault == NULL && pool && (after.one_ms <= 0.0 || after.many_ms <= 0.0))
			fault = "a finished tuning run reported no timings";

		render_threads = 2;
		video_rows(VIDEO_ROWS_HI, palette_rows, &job, H, W * H, NULL);
		video_rows_stats(VIDEO_ROWS_HI, &after);
		if (fault == NULL && pool && (after.mode != VIDEO_ROWS_FIXED || after.threads != 2))
			fault = "a fixed thread count was not reported";

		render_threads = 1;
		video_rows(VIDEO_ROWS_HI, palette_rows, &job, H, W * H, NULL);
		video_rows_stats(VIDEO_ROWS_HI, &after);
		if (fault == NULL && (after.mode != VIDEO_ROWS_FIXED || after.threads != 1))
			fault = "a one-thread setting was not reported";
		render_threads = saved_threads;
	}

	free(src);
	free(dst);
	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

typedef struct
{
	PaletteRows rows;
	SDL_atomic_t bands;
} CountedRows;

static void counted_rows(void *p, int row_begin, int row_end)
{
	CountedRows *const job = p;
	SDL_AtomicIncRef(&job->bands);
	palette_rows(&job->rows, row_begin, row_end);
}

bool video_test_rows_rounds(char *detail, size_t detailSize)
{
	enum { W = 512, H = 2 * VIDEO_ROWS_MIN_PIXELS / W };
	Uint8 *const src = calloc((size_t)W * H, 1);
	Uint8 *const dst = malloc((size_t)W * H * 4);
	const char *fault = NULL;

	const int saved_threads = render_threads;
	render_threads = 0;
	if (src == NULL || dst == NULL)
	{
		fault = "allocation failed";
	}
	else if (video_rows_ready())
	{
		VideoRowsSite *const s = &video_rows_sites[VIDEO_ROWS_TICK];
		const VideoRowsSite saved = *s;
		s->pixels = 0;
		s->candidates = 0;
		s->chosen = 0;
		s->round_open = false;

		CountedRows job = { { src, W, dst, W * 4, W } };
		VideoRowsStats stats;
		int measured = 0;
		for (int round = 0; fault == NULL; ++round)
		{
			SDL_AtomicSet(&job.bands, 0);
			video_rows(VIDEO_ROWS_TICK, counted_rows, &job, H / 2, W * (H / 2), NULL);
			const int first = SDL_AtomicGet(&job.bands);
			SDL_AtomicSet(&job.bands, 0);
			video_rows(VIDEO_ROWS_TICK, counted_rows, &job, H, W * H, NULL);
			const int second = SDL_AtomicGet(&job.bands);
			const int candidate = s->round_candidate;
			video_rows_end_frame();
			video_rows_stats(VIDEO_ROWS_TICK, &stats);

			if (first != second)
			{
				fault = "the calls of one round ran on different thread counts";
			}
			else if (round == 0)
			{
				if (s->tune_frame != 0 || s->pixels != W * H)
					fault = "a round with a larger job did not restart the tuning run";
			}
			else if (candidate < 0 || first != s->count[candidate])
			{
				fault = "a round did not run on its candidate thread count";
			}
			else if (++measured == s->candidates * VIDEO_ROWS_TUNE_FRAMES)
			{
				if (s->chosen == 0 || stats.mode == VIDEO_ROWS_TUNING)
					fault = "the tuning run did not finish after its rounds";
				break;
			}
			else if (stats.mode != VIDEO_ROWS_TUNING)
			{
				fault = "the tuning run finished before its last round";
			}
		}

		if (fault == NULL)
		{
			video_rows(VIDEO_ROWS_TICK, counted_rows, &job, H / 2, W * (H / 2), NULL);
			video_rows_end_frame();
			video_rows_stats(VIDEO_ROWS_TICK, &stats);
			if (stats.mode != VIDEO_ROWS_TUNING || s->pixels != W * (H / 2))
				fault = "a round with a different job size did not restart the tuning run";
		}
		*s = saved;
	}
	render_threads = saved_threads;

	free(src);
	free(dst);
	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

bool video_test_rows_pick(char *detail, size_t detailSize)
{
	enum { STEADY = 1000 };
	static const struct
	{
		Uint64 typical, odd;
		int odd_frames, want;
		const char *fault;
	} cases[] = {
		{ 800, 800, 0, 2, "a steady 20% gain was not taken" },
		{ 700, 5000, 2, 1, "a count that spikes on two frames in twenty was taken" },
		{ STEADY, 100, 1, 1, "one fast frame decided the pick" },
		{ 800, 50000, 1, 2, "one slow frame outweighed a steady gain" },
		{ 950, 950, 0, 1, "a gain under 10% was taken" },
	};
	static VideoRowsSite s;
	const char *fault = NULL;

	for (size_t k = 0; k < COUNTOF(cases) && fault == NULL; ++k)
	{
		s = (VideoRowsSite){ .candidates = 2, .count = { 1, 2 } };
		for (int i = 0; i < VIDEO_ROWS_TUNE_FRAMES; ++i)
		{
			s.samples[0][i] = STEADY;
			s.samples[1][i] = i < cases[k].odd_frames ? cases[k].odd : cases[k].typical;
		}
		video_rows_measure(&s);
		if (s.count[video_rows_pick(&s)] != cases[k].want)
			fault = cases[k].fault;
	}

	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

bool video_test_rows_watch(char *detail, size_t detailSize)
{
	enum { STEADY = 1000, PIXELS = 4321 };
	static const struct
	{
		Uint64 cost[2];
		bool alternate, retune;
		const char *fault;
	} cases[] = {
		{ { 2500, 2500 }, false, true, "two slower windows did not start a new tuning run" },
		{ { 400, 400 }, false, true, "two faster windows did not start a new tuning run" },
		{ { 2500, STEADY }, false, false, "one slower window started a new tuning run" },
		{ { 2500, 2500 }, true, false, "windows half at the tuned cost started a new tuning run" },
	};
	static VideoRowsSite s;
	const char *fault = NULL;

	for (size_t k = 0; k < COUNTOF(cases) && fault == NULL; ++k)
	{
		s = (VideoRowsSite){ .pixels = PIXELS, .candidates = 2, .count = { 1, 2 }, .chosen = 2,
		                     .tuned = STEADY };
		for (int window = 0; window < 2; ++window)
			for (int frame = 0; frame < VIDEO_ROWS_WATCH_FRAMES; ++frame)
				video_rows_watch(VIDEO_ROWS_REPLAY, &s,
				                 cases[k].alternate && (frame & 1) != 0 ? STEADY
				                                                        : cases[k].cost[window]);
		const bool retuned = s.chosen == 0 && s.tune_frame == 0 && s.pixels == PIXELS;
		if (retuned != cases[k].retune)
			fault = cases[k].fault;
	}

	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

static double video_test_present_end(double t, bool vsync, double vblank_ms)
{
	return vsync ? (floor(t / vblank_ms) + 1.0) * vblank_ms : t + 0.5;
}

bool video_test_idle_refresh(char *detail, size_t detailSize)
{
	static const struct
	{
		int hz, cap;
		bool vsync;
		Uint32 frame_ms;
	} rates[] = {
		{ 60, 0, true, 17 }, { 144, 0, true, 7 }, { 0, 0, true, 17 }, { 60, 30, true, 17 },
		{ 60, 0, false, 17 }, { 60, 30, false, 34 }, { 60, 144, false, 17 }, { 240, 0, false, 5 },
	};
	static const struct
	{
		int hz;
		double actual_hz;
		bool vsync;
	} displays[] = {
		{ 60, 60.0, true }, { 60, 59.94, true }, { 120, 120.0, true }, { 144, 143.86, true },
		{ 60, 60.0, false }, { 144, 144.0, false },
	};
	static const double overshoots_ms[] = { 0.0, 1.0, 1.9 };
	static const Uint32 holds_ms[] = { 19, 30, 45, 120, 500 };
	char fault[128] = "";

	for (size_t k = 0; k < COUNTOF(rates) && fault[0] == '\0'; ++k)
	{
		const Uint32 got = video_refresh_frame_ms(rates[k].hz, rates[k].vsync, rates[k].cap);
		if (got != rates[k].frame_ms)
			snprintf(fault, sizeof(fault), "%d Hz, cap %d%s re-presents every %u ms, not %u",
			         rates[k].hz, rates[k].cap, rates[k].vsync ? ", vsync" : "", (unsigned)got,
			         (unsigned)rates[k].frame_ms);
	}
	if (fault[0] == '\0' && (video_idle_nap(0, 10, 18, 17, true) != 10 ||
	                         video_idle_nap(50, 10, 18, 17, true) != 10))
		snprintf(fault, sizeof(fault), "a wait too close to its frame shortens its sleep");

	for (size_t d = 0; d < COUNTOF(displays) && fault[0] == '\0'; ++d)
	{
		const bool vsync = displays[d].vsync;
		const Uint32 frame_ms = video_refresh_frame_ms(displays[d].hz, vsync, 0);
		const double vblank_ms = 1000.0 / displays[d].actual_hz;
		for (size_t o = 0; o < COUNTOF(overshoots_ms) && fault[0] == '\0'; ++o)
		{
			const double over = overshoots_ms[o];
			double t = 0.25, prev = -1.0;
			Uint32 last = 0;
			int refreshes = 0;
			while (t < 3000.0 && fault[0] == '\0')
			{
				const Uint32 nap = video_idle_nap((Uint32)t - last, 1, VIDEO_NO_FRAME_DUE,
				                                  frame_ms, vsync);
				if (nap != 0)
				{
					t += nap + over;
					continue;
				}
				t = video_test_present_end(t, vsync, vblank_ms);
				const double gap = t - prev;
				if (prev >= 0.0 && vsync && fabs(gap - vblank_ms) > 0.01)
					snprintf(fault, sizeof(fault),
					         "an idle %d Hz vsync wait re-presents %.2f ms apart, not every vblank",
					         displays[d].hz, gap);
				else if (prev >= 0.0 && !vsync &&
				         (gap <= frame_ms - 1.0 || gap > frame_ms + 2.0 + over))
					snprintf(fault, sizeof(fault), "an idle %d Hz wait re-presents %.2f ms apart",
					         displays[d].hz, gap);
				prev = t;
				last = (Uint32)t;
				++refreshes;
			}
			const double slowest_ms = vsync ? vblank_ms : frame_ms + 2.0 + over;
			const int want = (int)(3000.0 / slowest_ms) - 2;
			if (fault[0] == '\0' && refreshes < want)
				snprintf(fault, sizeof(fault),
				         "an idle %d Hz wait re-presents %d times in 3 s, not %d", displays[d].hz,
				         refreshes, want);
		}

		for (size_t h = 0; h < COUNTOF(holds_ms) && fault[0] == '\0'; ++h)
		{
			const Uint32 hold = holds_ms[h];
			const double over = overshoots_ms[COUNTOF(overshoots_ms) - 1];
			double t = 0.4;
			Uint32 last = 0;
			int refreshes = 0;
			while (t < hold && fault[0] == '\0')
			{
				const Uint32 left = hold - (Uint32)t;
				const Uint32 nap = video_idle_nap((Uint32)t - last, MIN(left, 10u), left, frame_ms,
				                                  vsync);
				if (nap != 0)
				{
					t += nap + over;
					continue;
				}
				t = video_test_present_end(t, vsync, vblank_ms);
				if (t > hold)
					snprintf(fault, sizeof(fault),
					         "a %d Hz re-present runs %.2f ms into the frame due at %u ms",
					         displays[d].hz, t - hold, (unsigned)hold);
				last = (Uint32)t;
				++refreshes;
			}
			const Uint32 lead_ms = 3 * frame_ms;
			const int want = hold > lead_ms ? (int)((hold - lead_ms) / (2 * frame_ms)) : 0;
			if (fault[0] == '\0' && refreshes < want)
				snprintf(fault, sizeof(fault), "a %u ms hold at %d Hz re-presents %d times, not %d",
				         (unsigned)hold, displays[d].hz, refreshes, want);
		}
	}

	if (fault[0] != '\0' && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault[0] == '\0';
}

// Restore an invalidated drawable from the current logical frame while input-wait loops are idle.
static void video_repaint(void)
{
	if (main_window_renderer == NULL)
		return;
	JE_showVGA();
}

// Repaint after an external size change or a forced expose/target reset. The
// common event-pump path performs only a size query.
void video_repaint_if_stale(bool force)
{
	if (main_window == NULL || main_window_renderer == NULL)
		return;

	int w = 0, h = 0;
	video_output_size(&w, &h);

	if (force || w != last_present_w || h != last_present_h)
	{
		last_present_w = w;
		last_present_h = h;
		video_repaint();
	}
}

#ifndef __vita__  // gradient helpers; unused on Vita, which draws a plain black pillarbox
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
				const SDL_Color shade = { r, g, b, SDL_ALPHA_OPAQUE };
				gradient_cache[c][i] = palette_nearest(colors, 0, shade);
			}
		}

		gradient_cache_valid = true;
	}
}
#endif  // !__vita__

static void blit_with_offset(SDL_Surface* src, SDL_Surface* dst, int x_offset)
{
#ifndef __vita__
	// The side gradient's nearest-palette-index rebuild (256-colour search per cell) reruns
	// every menu-fade frame and dominates the Vita's slow CPU, so Vita draws plain black bars.
	update_gradient_cache();
#endif

	for (int y = 0; y < vga_height; ++y)
	{
		Uint8* src_row = (Uint8*)src->pixels + y * src->pitch;
		Uint8* dst_row = (Uint8*)dst->pixels + y * dst->pitch;

		memcpy(dst_row + x_offset, src_row, LEGACY_WIDTH);

#ifdef __vita__
		// Plain black pillarbox bars (index 0 = black, as the gradient's own edge column is).
		memset(dst_row, 0, x_offset);
		memset(dst_row + x_offset + LEGACY_WIDTH, 0, x_offset);
#else
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
#endif
	}
}

void set_menu_centered(bool centered)
{
	current_x_offset = centered ? MENU_X_OFFSET : 0;
}

// Current pillarbox offset (0 when not centering a legacy-width screen); the mouse
// module uses this to decide whether to defer the cursor draw to the composited frame.
int video_get_menu_x_offset(void)
{
	return current_x_offset;
}

/** Maps a specified point in window coordinates to game screen coordinates. */
void mapWindowPointToScreen(Sint32 *const inout_x, Sint32 *const inout_y)
{
	// SDL reports events in window points; last_output_rect is in output pixels.
	float scale_x, scale_y;
	video_output_pixel_scale(&scale_x, &scale_y);
	float fx = (float)*inout_x * scale_x, fy = (float)*inout_y * scale_y;
	crt_curve_source(&last_output_rect, &fx, &fy);
	const Sint32 x = (Sint32)fx;
	const Sint32 y = (Sint32)fy;

	*inout_x = (2 * (x - last_output_rect.x) + 1) * VGAScreen->w / (2 * last_output_rect.w) - current_x_offset;
	*inout_y = (2 * (y - last_output_rect.y) + 1) * VGAScreen->h / (2 * last_output_rect.h);
}

/** Float variant: same pixel-center convention, without the flooring. */
void mapWindowPointToScreenF(float *const inout_x, float *const inout_y)
{
	float scale_x, scale_y;
	video_output_pixel_scale(&scale_x, &scale_y);
	float x = *inout_x * scale_x, y = *inout_y * scale_y;
	crt_curve_source(&last_output_rect, &x, &y);

	if (last_output_rect.w > 0)
		*inout_x = (x - (float)last_output_rect.x + 0.5f)
		         * (float)VGAScreen->w / (float)last_output_rect.w - (float)current_x_offset;
	if (last_output_rect.h > 0)
		*inout_y = (y - (float)last_output_rect.y + 0.5f)
		         * (float)VGAScreen->h / (float)last_output_rect.h;
}

/** Scales a distance in window coordinates to game screen coordinates. */
void scaleWindowDistanceToScreen(Sint32 *const inout_x, Sint32 *const inout_y)
{
	float scale_x, scale_y;
	video_output_pixel_scale(&scale_x, &scale_y);
	const Sint32 x = (Sint32)((float)*inout_x * scale_x);
	const Sint32 y = (Sint32)((float)*inout_y * scale_y);

	*inout_x = (2 * x + 1) * VGAScreen->w / (2 * last_output_rect.w);
	*inout_y = (2 * y + 1) * VGAScreen->h / (2 * last_output_rect.h);
}

/** Float variant: no integer rounding, so callers that sample small deltas every
 *  render frame (rather than once per tick) don't lose fine/diagonal motion. */
void scaleWindowDistanceToScreenF(float *const inout_x, float *const inout_y)
{
	float scale_x, scale_y;
	video_output_pixel_scale(&scale_x, &scale_y);

	if (last_output_rect.w > 0)
		*inout_x = *inout_x * scale_x * (float)VGAScreen->w / (float)last_output_rect.w;
	if (last_output_rect.h > 0)
		*inout_y = *inout_y * scale_y * (float)VGAScreen->h / (float)last_output_rect.h;
}
