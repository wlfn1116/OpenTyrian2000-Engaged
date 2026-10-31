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
#ifndef DEBUG_MENU_H
#define DEBUG_MENU_H

#include "opentyr.h"
#include "video.h"

#include "SDL.h"

/* debug submenu dimensions for in-game setup */
#define DEBUG_MENU_WIDTH  255
/* Centre the panel horizontally in the playfield (left of the HUD) so it stays
 * centred under the widescreen layout. */
#define DEBUG_MENU_X      ((PLAYFIELD_WIDTH - DEBUG_MENU_WIDTH) / 2)
#define DEBUG_MENU_Y      5
/* total height of debug menu area */
#define DEBUG_MENU_HEIGHT (vga_height - 5 - DEBUG_MENU_Y + 1)

/* Hue 15 is the grey/white ramp (palette indices 240..255), so these are safe, theme-neutral
 * shades for the chrome of the debug panels and the Extra menu. */
enum
{
	PANEL_BG       = 0xF1,  // body fill: near-black grey
	PANEL_TITLE_BG = 0xF3,  // title strip, a touch lighter than the body
	PANEL_DIVIDER  = 0xF6,  // line under the title
	PANEL_EDGE_HI  = 0xFB,  // top/left border highlight
	PANEL_EDGE_LO  = 0xF4,  // bottom/right border shade
	PANEL_SEL_BAR  = 0xF5,  // highlight bar behind the selected row
};

// Fill a panel, bevel its border, and draw its title strip; title is centred on mid_x.
void debug_panel_frame(int px0, int py0, int px1, int py1, int title_h, int mid_x,
                       const char *title);
/* Map a screen point to the absolute row index of a scrolled panel list, or -1
 * if the point isn't over a row. */
int debug_panel_row_at(int mx, int my, int px0, int px1, int items_top,
                       int row_h, int visibleRows, int scrollTop, int count);
/* scrollbar track + thumb (only when the list overflows the window) */
void debug_panel_scrollbar(int px1, int items_top, int row_h, int visibleRows, int scrollTop,
                           int count);
// Map shoulder-button edges to Page Up and Page Down. A real key wins
// if both inputs arrive together.
void debug_panel_shoulder_paging(bool was[2]);
const char *debug_menu_prompt_fault(void);

void JE_debugMenu(bool center);
// Rebuild loadout-derived state while preserving live armour and shield.
void debugLoadoutRefresh(bool overHud);
void extraShipLoadoutRefresh(uint pnum, bool overHud);

// Only specials with a complete HUD icon set are safe to equip.
bool debug_special_is_safe(int id);

void JE_drawDebugOverlays(void);
// Debug perf readout, drawn onto the finished frame by the present loop so nothing overdraws it.
void JE_drawPerfOverlay(SDL_Surface *dst, int scale);

#endif // DEBUG_MENU_H
