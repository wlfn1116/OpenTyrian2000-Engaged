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
#ifndef VIDEO_H
#define VIDEO_H

#include "opentyr.h"

#include "SDL.h"

#define vga_width 356
#define vga_height 200

 /*
  * Original Tyrian rendered to a 320x200 framebuffer with the rightmost columns
  * reserved for the HUD.  The framebuffer has been widened to 356x200, which is
  * exactly 16:9 (356 / 200 = 1.7778) when displayed with square pixels, so the
  * gameplay area is true widescreen while the HUD keeps its original width
  * pinned to the right edge.  356 is also the practical ceiling: the background
  * tile maps are only 14 columns (336px) wide, so the playfield cannot grow past
  * what those tiles cover.
  *
  * LEGACY_WIDTH is the original 320px framebuffer that the menus, shop and HUD
  * art are still authored against.  The macros below provide the gameplay and
  * HUD widths, the pillarbox margin used to centre a legacy 320px screen inside
  * the widescreen buffer, and a helper that maps legacy hard-coded X coordinates
  * into the new HUD position.
  */
#define LEGACY_WIDTH 320
#define HUD_WIDTH 57
#define PLAYFIELD_WIDTH (vga_width - HUD_WIDTH)
#define PLAYFIELD_X_SHIFT (-12)
#define HUD_X(x) ((x) + (vga_width - LEGACY_WIDTH))

// Pillarbox margin (per side) when a 320px-wide legacy screen is centred in the
// widescreen buffer.  Derived from the width delta so it stays correct if
// vga_width changes; also sizes the gradient fade table in video.c.
#define MENU_X_OFFSET ((vga_width - LEGACY_WIDTH) / 2)

typedef enum {
	SCALE_CENTER,
	SCALE_INTEGER,
	SCALE_WIDESCREEN,   // fit the buffer at its own pixel ratio (square pixels = true 16:9)
	SCALE_CLASSIC_PAR,  // fit at the original DOS pixel aspect (taller pixels, ~3:2 overall)
	ScalingMode_MAX
} ScalingMode;

extern const char *const scaling_mode_names[ScalingMode_MAX];

extern int fullscreen_display; // -1 means windowed
extern bool output_vsync;      // present in sync with the display refresh rate
extern ScalingMode scaling_mode;

extern SDL_Surface *VGAScreen, *VGAScreenSeg;
extern SDL_Surface *game_screen;
extern SDL_Surface *VGAScreen2;

extern SDL_Window *main_window;
extern SDL_PixelFormat *main_window_tex_format;

void init_video(void);

void video_on_win_resize(void);
void reinit_fullscreen(int new_display);
void toggle_fullscreen(void);
bool init_scaler(unsigned int new_scaler);
bool set_scaling_mode_by_name(const char *name);

void deinit_video(void);

void JE_clr256(SDL_Surface *);
void JE_showVGA(void);
void set_vsync(bool enabled);

void set_menu_centered(bool centered);

void mapScreenPointToWindow(Sint32 *inout_x, Sint32 *inout_y);
void mapWindowPointToScreen(Sint32 *inout_x, Sint32 *inout_y);
void scaleWindowDistanceToScreen(Sint32 *inout_x, Sint32 *inout_y);
void scaleWindowDistanceToScreenF(float *inout_x, float *inout_y);  // float, no rounding loss

#endif /* VIDEO_H */
