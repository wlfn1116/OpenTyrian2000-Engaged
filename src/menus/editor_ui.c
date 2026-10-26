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
#include "editor_ui.h"

#include "draw.h"
#include "font.h"
#include "timing.h"
#include "video.h"

double editorTickMs(void)
{
	return EDITOR_TICK_DELAY * (double)get_delay_period();
}

void editorDrawHint(const char *text)
{
	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, vga_height - 12, text, small_font, centered,
	                    15, 2, false, 1);
}

void editorDrawPanel(int x0, int y0, int x1, int y1)
{
	fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, C_PANEL);
	JE_rectangle(VGAScreen, x0, y0, x1, y1, C_HI);
}

void editorDrawWell(int x0, int y0, int x1, int y1, Uint8 fill, Uint8 rim, Uint8 clamp)
{
	fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, fill);
	JE_rectangle(VGAScreen, x0, y0, x1, y1, rim);
	draw_corner_clamps(VGAScreen, x0 - 1, y0 - 1, x1 + 1, y1 + 1, 3, clamp);
}
