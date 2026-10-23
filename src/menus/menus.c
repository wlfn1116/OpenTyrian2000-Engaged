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
#include "menus.h"

#include "config.h"
#include "custom_episode.h"
#include "episodes.h"
#include "font.h"
#include "game_state.h"  // SA_NONE, for clearing the one-player rulesets before an online session
#include "keyboard.h"
#include "mouse.h"
#include "net_lobby.h"
#include "network.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "qa.h"
#include "save_menu.h"
#include "savegame.h"
#include "sfx.h"
#include "sprite.h"
#include "timing.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

char episode_name[6][31];
char difficulty_name[7][21];
char gameplay_name[GAMEPLAY_NAME_COUNT][26];
char timed_battle_name[4][23];

static bool menuPresentAndWait(bool *restart)
{
	if (*restart)
	{
		mouseCursor = MOUSE_POINTER_NORMAL;

		fade_palette(colors, 10, 0, 255);

		*restart = false;
	}

	service_SDL_events(true);

	JE_mouseStart();
	JE_showVGA();
	JE_mouseReplace();
	if (!output_vsync)
		limit_render_fps();  // pace the cursor redraw to the render-fps cap

	return wait_menu_input();
}

static void menuDrawItem(const char *text, int xCenter, int y, Sint8 brightness, int *width)
{
	*width = JE_textWidth(text, normal_font);
	draw_font_hv_shadow(VGAScreen, xCenter - *width / 2, y, text, normal_font, left_aligned, 15,
	                    brightness, false, 2);
}

// Find menu item that was hovered or clicked.
bool menuPointerSelect(size_t *selectedIndex, size_t count, int xCenter, int yMenuItems,
                       int dyMenuItems, int hMenuItem, const int *wMenuItem)
{
	for (size_t i = 0; i < count; ++i)
	{
		const int xMenuItem = xCenter - wMenuItem[i] / 2;
		if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem[i])
		{
			const int yMenuItem = yMenuItems + dyMenuItems * (int)i;
			if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
			{
				if (*selectedIndex != i)
				{
					JE_playSampleNum(S_CURSOR);

					*selectedIndex = i;
				}

				return newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
				       lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem[i] &&
				       lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem;
			}
		}
	}
	return false;
}

void menuKeySelect(size_t *selectedIndex, size_t count, bool *action, bool *cancel)
{
	if (newmouse)
	{
		if (lastmouse_but == SDL_BUTTON_RIGHT)
		{
			JE_playSampleNum(S_SPRING);

			*cancel = true;
		}
	}
	else if (newkey)
	{
		switch (lastkey_scan)
		{
		case SDL_SCANCODE_UP:
		{
			JE_playSampleNum(S_CURSOR);

			*selectedIndex = *selectedIndex == 0
				? count - 1
				: *selectedIndex - 1;
			break;
		}
		case SDL_SCANCODE_DOWN:
		{
			JE_playSampleNum(S_CURSOR);

			*selectedIndex = *selectedIndex == count - 1
				? 0
				: *selectedIndex + 1;
			break;
		}
		case SDL_SCANCODE_SPACE:
		case SDL_SCANCODE_RETURN:
		{
			*action = true;
			break;
		}
		case SDL_SCANCODE_ESCAPE:
		{
			JE_playSampleNum(S_SPRING);

			*cancel = true;
			break;
		}
		default:
			break;
		}
	}
}

typedef struct
{
	const char *header;
	const char *message;
	int yMessage;
	const char *const *items;
	size_t count;
	const JE_boolean *available;
	size_t availableCount;
	int yMenuItems;
	int dyMenuItems;
	int *widths;
	size_t selectedIndex;
	bool restart;
} MenuList;

static int menuRunList(MenuList *menu)
{
	/* Menus render on a 320px virtual screen centered in the wider VGA buffer; center on
	 * the virtual width, not vga_width, or text drifts when the menu is blitted over. */
	const int xCenter = 320 / 2;
	const int yMenuHeader = 20;
	const int hMenuItem = 13;

	for (; ; )
	{
		if (menu->restart)
		{
			JE_loadPic(VGAScreen2, 2, false);

			// Draw header.
			draw_font_hv_shadow(VGAScreen2, xCenter, yMenuHeader, menu->header, large_font,
			                    centered, 15, -3, false, 2);

			// The message is part of the backdrop; only the options redraw per frame.
			if (menu->message != NULL)
				JE_dString(VGAScreen2, JE_fontCenter(menu->message, SMALL_FONT_SHAPES),
				           menu->yMessage, menu->message, SMALL_FONT_SHAPES);
		}

		// Restore background and header.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw menu items.
		for (size_t i = 0; i < menu->count; ++i)
		{
			const bool selected = i == menu->selectedIndex;
			const bool disabled = menu->available != NULL && i < menu->availableCount
			                      && !menu->available[i];

			menuDrawItem(menu->items[i], xCenter, menu->yMenuItems + menu->dyMenuItems * (int)i,
			             -4 + (selected ? 2 : 0) + (disabled ? -4 : 0), &menu->widths[i]);
		}

		const bool mouseMoved = menuPresentAndWait(&menu->restart);

		// Handle interaction.

		bool action = false;
		bool cancel = false;

		if (mouseMoved || newmouse)
			action = menuPointerSelect(&menu->selectedIndex, menu->count, xCenter, menu->yMenuItems,
			                           menu->dyMenuItems, hMenuItem, menu->widths);

		menuKeySelect(&menu->selectedIndex, menu->count, &action, &cancel);

		if (action)
			return (int)menu->selectedIndex;

		if (cancel)
		{
			fade_black(15);

			return -1;
		}
	}
}

bool gameplaySelect(void)
{
	enum MenuItemIndex
	{
		MENU_ITEM_1_PLAYER_FULL_GAME = 0,
		MENU_ITEM_ENDLESS,             // inserted just below the full game
		MENU_ITEM_1_PLAYER_ARCADE,
		MENU_ITEM_1_PLAYER_TIMED_BATTLE,
		MENU_ITEM_2_PLAYER_ARCADE,
		MENU_ITEM_NETWORK,
		MENU_ITEM_COUNT,
	};

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	// Endless is inserted after Full Game; later labels keep their gameplay_name indices.
	const char *items[MENU_ITEM_COUNT];
	for (size_t i = 0; i < MENU_ITEM_COUNT; ++i)
		items[i] = (i == MENU_ITEM_ENDLESS)             ? "1 Player Endless"
		         : (i == MENU_ITEM_NETWORK)             ? "Online Multiplayer"
		         : (i == MENU_ITEM_1_PLAYER_FULL_GAME)  ? gameplay_name[1]
		         :                                        gameplay_name[i];

	int wMenuItem[MENU_ITEM_COUNT] = { 0 };
	MenuList menu = {
		.header = gameplay_name[0],
		.items = items,
		.count = MENU_ITEM_COUNT,
		.yMenuItems = 54,
		.dyMenuItems = 24,
		.widths = wMenuItem,
		.selectedIndex = MENU_ITEM_1_PLAYER_FULL_GAME,
		.restart = true,
	};

	for (; ; )
	{
		const int choice = menuRunList(&menu);
		if (choice < 0)
			return false;

		switch (choice)
		{
		case MENU_ITEM_1_PLAYER_FULL_GAME:
		case MENU_ITEM_1_PLAYER_ARCADE:
		case MENU_ITEM_1_PLAYER_TIMED_BATTLE:
		case MENU_ITEM_2_PLAYER_ARCADE:
		{
			JE_playSampleNum(S_SELECT);

			fade_black(10);

			onePlayerAction = choice == MENU_ITEM_1_PLAYER_ARCADE;
			timedBattleMode = choice == MENU_ITEM_1_PLAYER_TIMED_BATTLE;
			twoPlayerMode = choice == MENU_ITEM_2_PLAYER_ARCADE;
			endlessMode = false;
			return true;
		}
		case MENU_ITEM_ENDLESS:
		{
			JE_playSampleNum(S_SELECT);

			fade_black(10);

			onePlayerAction = false;
			timedBattleMode = false;
			twoPlayerMode = false;
			endlessMode = true;  // newGame() runs the endless setup (difficulty + first shop)
			return true;
		}
		case MENU_ITEM_NETWORK:
		{
			JE_playSampleNum(S_SELECT);

			// Continue only after the lobby returns with an initialized peer connection.
			if (!networkLobby())
			{
				menu.restart = true;
				break;
			}

			onePlayerAction = false;
			timedBattleMode = false;
			endlessMode = false;
			coopCampaignMode = false;
			coopEndlessMode = false;
			superTyrian = false;
			superArcadeMode = SA_NONE;
			endlessMode = network_game_type == NETWORK_GAME_ENDLESS;
			twoPlayerMode = true;  // networkStartScreen() sets the rest up
			return true;
		}
		default:
			break;
		}
	}
}

/* Lists installed custom episodes in a six-row window. */
static bool customEpisodeSelect(void)
{
	const int count = customEpisodeCount();
	if (count <= 0)
		return false;

	bool restart = true;

	enum { CUSTOM_ROWS_MAX = 6 };
	const int visible = MIN(count, CUSTOM_ROWS_MAX);
	int selectedIndex = 0;
	int scrollTop = 0;

	const int xCenter = 320 / 2;
	const int yMenuHeader = 20;
	const int yMenuItems = 46;
	const int dyMenuItems = 24;
	const int hMenuItem = 13;
	int wMenuItem[CUSTOM_ROWS_MAX] = { 0 };

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);
			draw_font_hv_shadow(VGAScreen2, xCenter, yMenuHeader, "Custom Episodes", large_font, centered, 15, -3, false, 2);
		}

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Keep the selection inside the visible window.
		if (selectedIndex < scrollTop)
			scrollTop = selectedIndex;
		if (selectedIndex >= scrollTop + visible)
			scrollTop = selectedIndex - visible + 1;

		for (int row = 0; row < visible; ++row)
		{
			const int idx = scrollTop + row;
			const char *const text = customEpisodeTitle(idx);

			menuDrawItem(text, xCenter, yMenuItems + dyMenuItems * row,
			             -4 + (idx == selectedIndex ? 2 : 0), &wMenuItem[row]);
		}

		// Mark rows above or below the window.
		if (scrollTop > 0)
			draw_font_hv_shadow(VGAScreen, xCenter, yMenuItems - 12, "...", normal_font, centered, 15, -5, false, 2);
		if (scrollTop + visible < count)
			draw_font_hv_shadow(VGAScreen, xCenter, yMenuItems + dyMenuItems * visible - 4, "...", normal_font, centered, 15, -5, false, 2);

		// Show the author only when supplied.
		if (customEpisodeAuthor(selectedIndex)[0] != '\0')
		{
			char byline[CUSTOM_EPISODE_TITLE_LEN + 4];
			snprintf(byline, sizeof(byline), "by %s", customEpisodeAuthor(selectedIndex));
			draw_font_hv_shadow(VGAScreen, xCenter, 182, byline, small_font, centered, 15, -5, false, 2);
		}

		const bool mouseMoved = menuPresentAndWait(&restart);

		bool action = false;
		bool cancel = false;

		size_t row = (size_t)(selectedIndex - scrollTop);
		if (mouseMoved || newmouse)
			action = menuPointerSelect(&row, (size_t)visible, xCenter, yMenuItems, dyMenuItems,
			                           hMenuItem, wMenuItem);
		selectedIndex = scrollTop + (int)row;

		size_t selected = (size_t)selectedIndex;
		menuKeySelect(&selected, (size_t)count, &action, &cancel);
		selectedIndex = (int)selected;

		if (action)
		{
			JE_playSampleNum(S_SELECT);
			fade_black(10);

			if (JE_initEpisodeCustom(selectedIndex))
			{
				initial_episode_num = episodeNum;
				return true;
			}

			// Keep the picker open when extraction fails.
			JE_playSampleNum(S_SPRING);
			restart = true;
		}

		if (cancel)
		{
			fade_black(10);
			return false;
		}
	}
}

bool episodeSelect(void)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	// Hide Custom when no valid container is installed.
	customEpisodeScan();
	const bool haveCustom = customEpisodeCount() > 0;
	const size_t stockCount = EPISODE_AVAILABLE;

	const char *items[EPISODE_AVAILABLE + 1];
	for (size_t i = 0; i < stockCount; ++i)
		items[i] = episode_name[i + 1];
	items[stockCount] = "Custom";

	int wMenuItem[EPISODE_AVAILABLE + 1] = { 0 };
	MenuList menu = {
		.header = episode_name[0],
		.items = items,
		.count = stockCount + (haveCustom ? 1 : 0),
		.available = episodeAvail,
		.availableCount = stockCount,
		// Six rows require tighter spacing.
		.yMenuItems = haveCustom ? 46 : 50,
		.dyMenuItems = haveCustom ? 24 : 30,
		.widths = wMenuItem,
		.selectedIndex = 0,
		.restart = true,
	};

	for (; ; )
	{
		const int choice = menuRunList(&menu);
		if (choice < 0)
			return false;

		if ((size_t)choice >= stockCount)
		{
			JE_playSampleNum(S_SELECT);

			fade_black(10);

			if (customEpisodeSelect())
				return true;

			menu.restart = true;   // backed out of the Custom list; fade this menu back in
		}
		else if (episodeAvail[choice])
		{
			JE_playSampleNum(S_SELECT);

			fade_black(10);

			JE_initEpisode(choice + 1);
			initial_episode_num = episodeNum;
			return true;
		}
		else
		{
			JE_playSampleNum(S_SPRING);
		}
	}
}

bool difficultySelect(void)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	// Show every difficulty without the classic unlock codes.
	const char *items[COUNTOF(difficulty_name) - 1];
	for (size_t i = 0; i < COUNTOF(items); ++i)
		items[i] = difficulty_name[i + 1];

	int wMenuItem[COUNTOF(items)] = { 0 };
	MenuList menu = {
		.header = difficulty_name[0],
		.items = items,
		.count = COUNTOF(items),
		.yMenuItems = 54,
		.dyMenuItems = 24,
		.widths = wMenuItem,
		.selectedIndex = 1,
		.restart = true,
	};

	const int choice = menuRunList(&menu);
	if (choice < 0)
		return false;

	JE_playSampleNum(S_SELECT);

	switch (choice)
	{
	case 0:
		difficultyLevel = DIFFICULTY_EASY;
		break;
	case 1:
		difficultyLevel = DIFFICULTY_NORMAL;
		break;
	case 2:
		difficultyLevel = DIFFICULTY_HARD;
		break;
	case 3:
		difficultyLevel = DIFFICULTY_IMPOSSIBLE;
		break;
	case 4:
		difficultyLevel = DIFFICULTY_SUICIDE;
		break;
	case 5:
		difficultyLevel = DIFFICULTY_LORD_OF_GAME;
		break;
	}

	fade_black(10);

	return true;
}

// Host start choice. Returns a loaded slot (1-based), 0 for new game, or -1 to cancel.
int networkHostStartSelect(void)
{
	// A gameplay wire test has no player at the menu: it loads the planted save the way the
	// menu would, or starts a new game.
	if (qa_net_gameplay_ticks > 0)
	{
		if (qa_net_resume_slot > 0 && qa_net_resume_slot <= SAVE_FILES_NUM)
		{
			JE_loadGame((JE_byte)qa_net_resume_slot);
			fprintf(stderr, "net gameplay: host auto-loads slot %d\n", qa_net_resume_slot);
			fflush(stderr);
			return qa_net_resume_slot;
		}
		fprintf(stderr, "net gameplay: host auto-selects New Game\n");
		fflush(stderr);
		return 0;
	}

	static const char *const menu_item[] = { "New Game", "Load Game" };

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	int wMenuItem[COUNTOF(menu_item)] = { 0 };
	MenuList menu = {
		// Same header the lobby screens carry.
		.header = "Online Multiplayer",
		.items = menu_item,
		.count = COUNTOF(menu_item),
		// Only two options, so center the pair on the screen rather than stacking them under
		// the header: the block spans dyMenuItems + hMenuItem, so 81 puts its center on y=99.5.
		.yMenuItems = 81,
		.dyMenuItems = 24,  // half a row of breathing space between the two options
		.widths = wMenuItem,
		.selectedIndex = 0,
		.restart = true,
	};

	for (; ; )
	{
		const int choice = menuRunList(&menu);
		if (choice < 0)
			return -1;

		JE_playSampleNum(S_SELECT);

		fade_black(10);

		if (choice == 0)
			return 0;

		const int slot = JE_loadScreen(true, false);
		if (slot > 0)
			return slot;

		menu.restart = true;  // backed out of the load menu; redraw this screen
	}
}

// Offer to keep the pre-level backup after an online session ends unexpectedly.
bool networkDisconnectSavePrompt(const char *message)
{
	static const char *const menu_item[] = { "Save Game", "Don't Save" };

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	// Tests accept the disconnect-save offer.
	if (qa_net_disconnect_save > 0)
	{
		fprintf(stderr, "net gameplay: host takes the disconnect offer (%s)\n", message);
		fflush(stderr);
		return true;
	}

	wait_noinput(true, true, true);
	newkey = newmouse = false;

	int wMenuItem[COUNTOF(menu_item)] = { 0 };
	MenuList menu = {
		.header = "Online Multiplayer",
		.message = message,
		.yMessage = 60,
		.items = menu_item,
		.count = COUNTOF(menu_item),
		.yMenuItems = 110,
		.dyMenuItems = 24,
		.widths = wMenuItem,
		.selectedIndex = 0,
		.restart = true,
	};

	const int choice = menuRunList(&menu);
	if (choice < 0)
		return false;

	JE_playSampleNum(S_SELECT);

	fade_black(10);

	return choice == 0;
}

bool timedBattleSelect(void)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	const char *items[COUNTOF(timed_battle_name) - 1];
	for (size_t i = 0; i < COUNTOF(items); ++i)
		items[i] = timed_battle_name[i + 1];

	int wMenuItem[COUNTOF(items)] = { 0 };
	MenuList menu = {
		.header = timed_battle_name[0],
		.items = items,
		.count = COUNTOF(items),
		.yMenuItems = 54,
		.dyMenuItems = 24,
		.widths = wMenuItem,
		.selectedIndex = 0,
		.restart = true,
	};

	const int choice = menuRunList(&menu);
	if (choice < 0)
		return false;

	JE_playSampleNum(S_SELECT);

	fade_black(10);

	// Time Battle selections map directly to episodes 1 and 5.
	JE_initEpisode(choice == 0 ? 1 : 5);
	timeBattleSelection = choice + 1;
	return true;
}
