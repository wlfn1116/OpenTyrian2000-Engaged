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
#include "help.h"

#include "config.h"
#include "draw.h"
#include "font.h"
#include "fonthand.h"
#include "game_state.h"
#include "helptext.h"
#include "hud.h"
#include "joystick.h"
#include "keyboard.h"
#include "loudness.h"
#include "menus.h"
#include "mouse.h"
#include "musmast.h"
#include "network.h"
#include "palette.h"
#include "picload.h"
#include "render_list.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "timing.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

#define MAX_PAGE 8
#define TOPICS 6
static const JE_byte topicStart[TOPICS] = { 0, 1, 2, 3, 7, 255 };

static bool helpSystemPage(Uint8 *topic, bool *restart);

void JE_helpSystem(JE_byte startTopic)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	Uint8 topic = startTopic;

	bool restart = true;

	const size_t menuItemsCount = COUNTOF(topicName) - 1;
	size_t selectedIndex = 0;

	/* Menus render on a 320px virtual screen centered in the wider VGA buffer; center on
	 * LEGACY_WIDTH, not vga_width, or text drifts when the menu is blitted over. */
	const int xCenter = LEGACY_WIDTH / 2;
	const int yMenuHeader = 30;
	const int yMenuItems = 60;
	/* reduce spacing to fit new Debug option */
	const int dyMenuItems = 17;
	const int hMenuItem = 13;
	int wMenuItem[COUNTOF(topicName) - 1] = { 0 };

	for (; ; )
	{
		if (restart)
		{
			play_song(SONG_MAPVIEW);

			JE_loadPic(VGAScreen2, 2, false);
		}

		if (topic > 1)
		{
			if (!helpSystemPage(&topic, &restart))
				return;

			selectedIndex = (size_t)topic - 1;
			topic = 1;
			continue;
		}

		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw header.
		draw_font_hv_shadow(VGAScreen, xCenter, yMenuHeader, topicName[0], large_font, centered, 15, -3, false, 2);

		// Draw menu items.
		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const char *const text = topicName[i + 1];

			wMenuItem[i] = JE_textWidth(text, normal_font);
			const int y = yMenuItems + dyMenuItems * i;

			const bool selected = i == selectedIndex;

			draw_font_hv_shadow(VGAScreen, xCenter, y, text, normal_font, centered, 15, -3 + (selected ? 2 : 0), false, 2);
		}

		mouseCursor = MOUSE_POINTER_NORMAL;

		if (restart)
		{
			fade_palette(colors, 10, 0, 255);

			restart = false;
		}

		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool mouseMoved = false;
		do
		{
			SDL_Delay(1);  // fine poll so the cursor redraws at display rate on motion

			float oldMouseX = mouse_xf;
			float oldMouseY = mouse_yf;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			mouseMoved = mouse_xf != oldMouseX || mouse_yf != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		// Handle interaction.

		bool action = false;
		bool done = false;

		if (mouseMoved || newmouse)
			action = menuPointerSelect(&selectedIndex, menuItemsCount, xCenter, yMenuItems,
			                           dyMenuItems, hMenuItem, wMenuItem);

		menuKeySelect(&selectedIndex, menuItemsCount, &action, &done);

		if (action)
		{
			JE_playSampleNum(S_SELECT);

			topic = selectedIndex + 2;

			if (selectedIndex == menuItemsCount - 1)
				done = true;
		}

		if (done)
		{
			fade_black(15);

			return;
		}
	}
}

static bool helpSystemPage(Uint8 *topic, bool *restart)
{
	Uint8 page = topicStart[*topic - 1];

	/* See comment in JE_helpSystem regarding the virtual screen width. */
	const int xCenter = LEGACY_WIDTH / 2;

	for (; ; )
	{
		if (page == 0)
		{
			*topic = 1;
			return true;
		}
		else if (page > MAX_PAGE)
		{
			*topic = COUNTOF(topicName) - 1;
			return true;
		}

		for (Uint8 temp = 0; temp < COUNTOF(topicName); ++temp)
		{
			if (topicStart[temp] <= page)
				*topic = temp + 1;
			else
				break;
		}

		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		fill_rectangle_wh(VGAScreen, 0, vga_height - 8, vga_width, 8, 0);

		const char *const text = topicName[*topic - 1];

		// Draw header.
		draw_font_hv_shadow(VGAScreen, xCenter, 1, text, normal_font, centered, 15, -3, false, 2);

		// Draw footer.
		JE_char buffer[128];

		snprintf(buffer, sizeof buffer, "%s %d", miscText[24], page - topicStart[*topic - 1] + 1);
		draw_font_hv(VGAScreen, 10, vga_height - 8, buffer, small_font, left_aligned, 13, 5);

		snprintf(buffer, sizeof buffer, "%s %d of %d", miscText[25], page, MAX_PAGE);
		draw_font_hv(VGAScreen, vga_width - 10, vga_height - 8, buffer, small_font, right_aligned, 13, 5);

		// Draw text.

		helpBoxBrightness = 3;
		verticalHeight = 8;

		switch (page)
		{
		case 1: /* One-Player Menu */
			JE_HBox(VGAScreen, 10,  20,  2, 60);
			JE_HBox(VGAScreen, 10,  50,  5, 60);
			JE_HBox(VGAScreen, 10,  80, 21, 60);
			JE_HBox(VGAScreen, 10, 110,  1, 60);
			JE_HBox(VGAScreen, 10, 140, 28, 60);
			break;
		case 2: /* Two-Player Menu */
			JE_HBox(VGAScreen, 10,  20,  1, 60);
			JE_HBox(VGAScreen, 10,  60,  2, 60);
			JE_HBox(VGAScreen, 10, 100, 21, 60);
			JE_HBox(VGAScreen, 10, 140, 28, 60);
			break;
		case 3: /* Upgrade Ship */
			JE_HBox(VGAScreen, 10,  20,  5, 60);
			JE_HBox(VGAScreen, 10,  70,  6, 60);
			JE_HBox(VGAScreen, 10, 110,  7, 60);
			break;
		case 4:
			JE_HBox(VGAScreen, 10,  20,  8, 60);
			JE_HBox(VGAScreen, 10,  55,  9, 60);
			JE_HBox(VGAScreen, 10,  87, 10, 60);
			JE_HBox(VGAScreen, 10, 120, 11, 60);
			JE_HBox(VGAScreen, 10, 170, 13, 60);
			break;
		case 5:
			JE_HBox(VGAScreen, 10,  20, 14, 60);
			JE_HBox(VGAScreen, 10,  80, 15, 60);
			JE_HBox(VGAScreen, 10, 120, 16, 60);
			break;
		case 6:
			JE_HBox(VGAScreen, 10,  20, 17, 60);
			JE_HBox(VGAScreen, 10,  40, 18, 60);
			JE_HBox(VGAScreen, 10, 130, 20, 60);
			break;
		case 7: /* Options */
			JE_HBox(VGAScreen, 10,  20, 21, 60);
			JE_HBox(VGAScreen, 10,  70, 22, 60);
			JE_HBox(VGAScreen, 10, 110, 23, 60);
			JE_HBox(VGAScreen, 10, 140, 24, 60);
			break;
		case 8:
			JE_HBox(VGAScreen, 10,  20, 25, 60);
			JE_HBox(VGAScreen, 10,  60, 26, 60);
			JE_HBox(VGAScreen, 10, 100, 27, 60);
			JE_HBox(VGAScreen, 10, 140, 28, 60);
			JE_HBox(VGAScreen, 10, 170, 29, 60);
			break;
		}

		helpBoxBrightness = 1;
		verticalHeight = 7;

		if (*restart)
		{
			fade_palette(colors, 10, 0, 255);

			*restart = false;
		}

		do
		{
			mouseCursor = mouse_x < xCenter ? MOUSE_POINTER_LEFT : MOUSE_POINTER_RIGHT;

			service_SDL_events(true);

			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();

			// Present at display rate for a smooth cursor; vsync-on paces via showVGA.
			if (!output_vsync)
				limit_render_fps();

			push_joysticks_as_keyboard();
			service_SDL_events(false);
		} while (!(newkey || newmouse));

		// Handle interaction.

		bool done = false;

		if (newmouse)
		{
			switch (lastmouse_but)
			{
			case SDL_BUTTON_LEFT:
			{
				JE_playSampleNum(S_CURSOR);

				if (mouse_x < xCenter)
					page -= 1;
				else
					page += 1;
				break;
			}
			case SDL_BUTTON_RIGHT:
			{
				JE_playSampleNum(S_SPRING);

				done = true;
				break;
			}
			default:
				break;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_LEFT:
			{
				JE_playSampleNum(S_CURSOR);

				page -= 1;
				break;
			}
			case SDL_SCANCODE_RIGHT:
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				JE_playSampleNum(S_CURSOR);

				page += 1;
				break;
			}
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				done = true;
				break;
			}
			default:
				break;
			}
		}

		if (done)
		{
			fade_black(15);

			return false;
		}
	}
}

void JE_inGameHelp(void)
{
	// Presents its own frames inside the gameplay tick's render-list recording
	// window; suspend recording (see JE_doInGameSetup for the rationale).
	const bool rl_was_recording = render_list_recording;
	render_list_recording = false;

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	JE_clearKeyboard();

	JE_barShade(VGAScreen, 1, 1, 262, vga_height - 18); /*Main Box*/
	JE_barShade(VGAScreen, 3, 3, 260, vga_height - 20);
	JE_barShade(VGAScreen, 5, 5, 258, vga_height - 22);
	JE_barShade(VGAScreen, 7, 7, 256, vga_height - 24);
	fill_rectangle_xy(VGAScreen, 9, 9, 254, vga_height - 26, 0);

	if (split_arcade_mode())  // Two-Player Help
	{
		helpBoxColor = 3;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 20,  4, 36, 50);

		// weapon help
		blit_sprite(VGAScreenSeg, 2, 21, OPTION_SHAPES, 43);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 55, 20, 37, 40);

		// sidekick help
		blit_sprite(VGAScreenSeg, 5, 36, OPTION_SHAPES, 41);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 40, 43, 34, 44);

		// shield/armor help
		blit_sprite(VGAScreenSeg, 2, 79, OPTION_SHAPES, 42);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 54, 84, 35, 40);

		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 5, 126, 38, 55);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 5, 160, 39, 55);
	}
	else
	{
		// power bar help
		blit_sprite(VGAScreenSeg, 15, 5, OPTION_SHAPES, 40);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 40, 10, 31, 45);

		// weapon help
		blit_sprite(VGAScreenSeg, 5, 37, OPTION_SHAPES, 39);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 40, 40, 32, 44);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 40, 60, 33, 44);

		// sidekick help
		blit_sprite(VGAScreenSeg, 5, 98, OPTION_SHAPES, 41);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 40, 103, 34, 44);

		// shield/armor help
		blit_sprite(VGAScreenSeg, 2, 138, OPTION_SHAPES, 42);
		helpBoxColor = 5;
		helpBoxBrightness = 3;
		JE_HBox(VGAScreen, 54, 143, 35, 40);
	}

	// "press a key"
	blit_sprite(VGAScreenSeg, 16, vga_height - 11, OPTION_SHAPES, 36);  // in-game text area
	JE_outText(VGAScreenSeg, 120 - JE_textWidth(miscText[5 - 1], TINY_FONT) / 2 + 20, vga_height - 10, miscText[5 - 1], 0, 4);

	do
	{
		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		// Present at display rate for a smooth cursor; vsync-on paces via showVGA.
		if (!output_vsync)
			limit_render_fps();

		push_joysticks_as_keyboard();
		service_SDL_events(false);

		NETWORK_KEEP_ALIVE();
	} while (!(newkey || newmouse));

	textErase = 1;

	VGAScreen = temp_surface;

	render_list_recording = rl_was_recording;
}
