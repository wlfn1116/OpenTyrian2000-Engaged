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
#include "save_menu.h"

#include "config.h"
#include "console_platform.h"
#include "custom_episode.h"
#include "draw.h"
#include "endless.h"
#include "episodes.h"
#include "font.h"
#include "fonthand.h"
#include "game_loop.h"
#include "game_menu.h"
#include "game_state.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "net_savexfer.h"
#include "network.h"
#include "palette.h"
#include "picload.h"
#include "qa.h"
#include "savegame.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "timing.h"
#include "video.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

JE_boolean performSave;

// The episode a save will really initialize: a "Completed" save rolls over to the next one.
static int save_effective_episode(const JE_SaveFileType *rec)
{
	int episode = rec->episode;
	if (strcmp(rec->levelName, "Completed") == 0)
	{
		if (episode == EPISODE_AVAILABLE)
			episode = 1;
		else if (episode < EPISODE_AVAILABLE)
			episode++;
	}
	return episode;
}

// Match saved co-op loadouts and Endless state to the current session type.
bool save_type_compatible(const JE_SaveFileType *rec, JE_byte slot, bool net2p)
{
	const bool coop = save_record_is_coop(rec);
	if (isNetworkGame && net2p)
	{
		if (network_game_type == NETWORK_GAME_ENDLESS)
			return coop && endlessSlotHasRun(slot);
		if (network_game_type == NETWORK_GAME_CAMPAIGN)
			return coop && !endlessSlotHasRun(slot);
		if (coop)
			return false;

		/* A save must match the arcade ruleset that created its ships. Cross-mode
		 * loads can restore invalid hulls, loadouts, or life-counter ownership. */
		if (save_record_is_dual_arcade(rec) != arcade_separate_mode())
			return false;
		const uint sa1 = save_record_sa_ship(rec, 0), sa2 = save_record_sa_ship(rec, 1);
		if (network_game_type == NETWORK_GAME_SUPERTYRIAN)
			return sa1 == SA_SUPERTYRIAN && sa2 == SA_SUPERTYRIAN;
		if (network_game_type == NETWORK_GAME_SUPERARCADE)
			return sa1 >= 1 && sa1 <= SA && sa2 >= 1 && sa2 <= SA;
		return sa1 == SA_NONE && sa2 == SA_NONE;
	}
	// Local play: the linked pair and the one-player pages never wrote a dual-ship record, and
	// cannot fly one back.
	return !coop && !save_record_is_dual_arcade(rec);
}

bool save_custom_locked(const JE_SaveFileType *rec)
{
	return !isNetworkGame &&
	       customEpisodeSaveDepsMissing(rec->customEpFile, rec->customCollection);
}

// These are separate keyboard rows but share the bottom line on screen.
enum
{
	LOAD_SLOT_ROWS = 11,
	LOAD_ROW_EXIT = LOAD_SLOT_ROWS,
	LOAD_ROW_COUNT,
};

// Keep Upload's first help line left of the page arrow.
#define LOAD_HELP_X       103
#define LOAD_HELP_BUDGET  (213 - LOAD_HELP_X)

static const char loadHelpUpload[] = "Choose a save to send to the other device.";
static const char loadHelpUploadLine1[] = "Choose a save to send";

// uploadPick reuses the load list as the transfer source picker.
static int JE_loadScreenMode(bool net2p, bool saving, bool uploadPick)
{
	set_menu_centered(true);

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer and arrow sprites

	// Test saves must use the two-player page to retain both ships.
	if (saving && qa_net_disconnect_save > 0)
	{
		if (net2p && (qa_net_disconnect_save <= LOAD_SLOT_ROWS ||
		              qa_net_disconnect_save % LOAD_SLOT_ROWS == 0))
		{
			fprintf(stderr, "net gameplay: slot %d is not a two-player save slot\n",
			        qa_net_disconnect_save);
			fflush(stderr);
			exit(2);
		}
		performSave = true;
		JE_operation((JE_byte)qa_net_disconnect_save);
		return qa_net_disconnect_save;
	}

	bool restart = true;

	const bool xferSaving = saving && saveXferPending() != NULL;
	// Downloads stay on their original one-player or two-player page.
	const bool pinPage = net2p || xferSaving;

	size_t playersIndex = net2p ? 1 : 0;
	size_t selectedIndex = 0;

	const int xCenter = 160; // center of 320px menu field
	const int yMenuHeader = 5;
	const int xMenuItem = 10;
	const int xMenuItemName = xMenuItem;
	const int xMenuItemLastLevel = 120;
	const int xMenuItemEpisode = 250;
	const int wMenuItem = 300;
	const int yMenuItems = 30;
	const int dyMenuItems = 13;
	const int hMenuItem = 8;
	const int xLeftControl = 83;
	const int xRightControl = 213;
	const int wControl = 24;
	const int yControls = vga_height - 21;

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);
			// Restore the menu's palette-7 ramp for save dialogs drawn over picture 2.
			memcpy(&colors[224], &palettes[0][224], 16 * sizeof(colors[0]));
			fill_rectangle_wh(VGAScreen2, 0, vga_height - 8, vga_width, 8, 0);
		}

		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw header.
		draw_font_hv_shadow(VGAScreen, xCenter, yMenuHeader, miscText[38 + playersIndex], large_font, centered, 15, -3, false, 2);

		// Draw menu items.

		const size_t menuItemsCount = LOAD_ROW_COUNT;

		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const int y = yMenuItems + dyMenuItems * (i < LOAD_ROW_EXIT ? i : LOAD_ROW_EXIT);

			const bool selected = i == selectedIndex;

			if (i == LOAD_ROW_EXIT)
			{
				JE_textShade(VGAScreen, xMenuItemName, y, miscText[33], 13,
				             selected ? 6 : 2, FULL_SHADE);
				continue;
			}

			const JE_SaveFileType *const saveFile = &saveFiles[playersIndex * 11 + i];
			const JE_byte slot = (JE_byte)(playersIndex * 11 + i + 1);

			const bool disabled = saveFile->level == 0;
			// Online loads: a save whose episode this session lacks is shown but dimmed and
			// unselectable.  Saving overwrites the slot, so the lock doesn't apply there.
			const int saveEpisode = save_effective_episode(saveFile);
			const bool epLocked = net2p && !saving && !disabled &&
			                      (saveEpisode < 1 || saveEpisode > EPISODE_MAX || !episodeAvail[saveEpisode - 1]);
			// Upload accepts every non-empty save type.
			const bool typeLocked = !saving && !uploadPick && !disabled &&
			                      !save_type_compatible(saveFile, slot, net2p);
			// Uploads do not require the local custom container.
			const bool customLocked = !saving && !uploadPick && !disabled &&
			                      save_custom_locked(saveFile);

			char buffer[22];

			if (disabled)
			{
				JE_textShade(VGAScreen, xMenuItemName, y, miscText[2], 13, selected ? 6 : 0, FULL_SHADE);

				snprintf(buffer, sizeof buffer, "%s -----", miscTextB[2]);
				JE_textShade(VGAScreen, xMenuItemLastLevel, y, buffer, 5, selected ? 6 : 0, FULL_SHADE);
			}
			else
			{
				const int bright = selected ? 6 : ((epLocked || typeLocked || customLocked) ? 0 : 2);

				JE_textShade(VGAScreen, xMenuItemName, y, saveFile->name, 13, bright, FULL_SHADE);

				snprintf(buffer, sizeof buffer, "%s %s", miscTextB[2], saveFile->levelName);
				JE_textShade(VGAScreen, xMenuItemLastLevel, y, buffer, 5, bright, FULL_SHADE);

				// An Endless run's episode field is only its current zone's source level.
				if (endlessSlotHasRun(slot))
					SDL_strlcpy(buffer, "Endless", sizeof buffer);
				else if (saveFile->customEpFile[0] != '\0')
					SDL_strlcpy(buffer, "Custom", sizeof buffer);
				else
					snprintf(buffer, sizeof buffer, "%s %u", miscTextB[1], saveFile->episode);
				JE_textShade(VGAScreen, xMenuItemEpisode, y, buffer, 5, bright, FULL_SHADE);
			}
		}

		// Draw paging controls (fixed to the 2-player page for the online host).

		const bool leftControlVisible = !pinPage && playersIndex > 0;
		const bool rightControlVisible = !pinPage && playersIndex < 1;

		if (leftControlVisible)
			blit_sprite2x2(VGAScreen, xLeftControl, yControls, shopSpriteSheet, 279);

		if (rightControlVisible)
			blit_sprite2x2(VGAScreen, xRightControl, yControls, shopSpriteSheet, 281);

		const char *helpLine = miscText[55];
		unsigned int helpWidth = 25;
		if (uploadPick)
		{
			helpLine = loadHelpUpload;
			helpWidth = sizeof(loadHelpUploadLine1) - 1;
		}
		else if (xferSaving)
			helpLine = "Choose a slot to keep the downloaded save in.";
		else if (saving)
			helpLine = "Choose a slot to save your game into.";

		helpBoxColor = 15;
		JE_helpBox(VGAScreen, LOAD_HELP_X, vga_height - 18, helpLine, helpWidth);

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

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

			NETWORK_KEEP_ALIVE();  // the joiner is connected and waiting while the host browses

			float oldMouseX = mouse_xf;
			float oldMouseY = mouse_yf;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			mouseMoved = mouse_xf != oldMouseX || mouse_yf != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		// Handle interaction.

		bool leftAction = false;
		bool rightAction = false;
		bool action = false;
		bool done = false;
		bool backOut = false;

		if (mouseMoved || newmouse)
		{
			if (leftControlVisible &&
			    mouse_y >= yControls &&
			    mouse_x >= xLeftControl &&
			    mouse_x < xLeftControl + wControl)
			{
				if (newmouse && lastmouse_but == SDL_BUTTON_LEFT)
				{
					JE_playSampleNum(S_CURSOR);

					leftAction = true;
				}
			}
			else if (rightControlVisible &&
			         mouse_y >= yControls &&
			         mouse_x >= xRightControl &&
			         mouse_x < xRightControl + wControl)
			{
				if (newmouse && lastmouse_but == SDL_BUTTON_LEFT)
				{
					JE_playSampleNum(S_CURSOR);

					rightAction = true;
				}
			}
			else
			{
				if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem)
				{
					for (size_t i = 0; i < menuItemsCount; ++i)
					{
						const int yMenuItem = yMenuItems + dyMenuItems * i;

						if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
						{
							if (selectedIndex != i)
							{
								JE_playSampleNum(S_CURSOR);

								selectedIndex = i;
							}

							if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
							    lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem &&
							    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
							{
								action = true;
							}

							break;
						}
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				backOut = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_LEFT:
			{
				JE_playSampleNum(S_CURSOR);

				leftAction = true;
				break;
			}
			case SDL_SCANCODE_RIGHT:
			{
				JE_playSampleNum(S_CURSOR);

				rightAction = true;
				break;
			}
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == 0
					? menuItemsCount - 1
					: selectedIndex - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == menuItemsCount - 1
					? 0
					: selectedIndex + 1;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				backOut = true;
				break;
			}
			default:
				break;
			}
		}

		// Arrow keys also raise these actions; the online pin gates both input sources here.
		if (leftAction && !pinPage)
		{
			playersIndex = playersIndex == 0 ? 1 : 0;
		}
		else if (rightAction && !pinPage)
		{
			playersIndex = playersIndex == 1 ? 0 : 1;
		}
		else if (action)
		{
			if (selectedIndex == LOAD_ROW_EXIT)
			{
				JE_playSampleNum(S_SELECT);

				backOut = true;
			}
			else if (uploadPick)
			{
				const size_t saveFileIndex = playersIndex * 11 + selectedIndex;

				if (saveFiles[saveFileIndex].level == 0)
				{
					JE_playSampleNum(S_CLINK);
				}
				else
				{
					JE_playSampleNum(S_SELECT);
					fade_black(15);
					return (int)saveFileIndex + 1;
				}
			}
			else if (saving)
			{
				const size_t saveFileIndex = playersIndex * 11 + selectedIndex;

				// The LAST LEVEL row is the auto slot (JE_operation refuses slot % 11 == 0);
				// empty slots are fine; they are what saving is for.
				if ((saveFileIndex + 1) % 11 == 0)
				{
					JE_playSampleNum(S_CLINK);
				}
				else
				{
					JE_playSampleNum(S_SELECT);

					performSave = true;
					JE_operation(saveFileIndex + 1);
					if (xferSaving && saveXferPending() == NULL)
					{
						// Keep the download pending if the name dialog is cancelled.
						fade_black(15);
						return 0;
					}
					// Stay on the list (its per-frame backdrop restore erases the dialog), so a
					// mistyped name can be redone; Exit leaves when the player is satisfied.
				}
			}
			else
			{
				const size_t saveFileIndex = playersIndex * 11 + selectedIndex;
				const JE_SaveFileType *const saveFile = &saveFiles[saveFileIndex];
				const int saveEpisode = save_effective_episode(saveFile);

				if (saveFile->level == 0 ||  // "EMPTY SLOT"
				    (net2p && (saveEpisode < 1 || saveEpisode > EPISODE_MAX || !episodeAvail[saveEpisode - 1])) ||
				    save_custom_locked(saveFile) ||
				    !save_type_compatible(saveFile, (JE_byte)(saveFileIndex + 1), net2p))
				{
					JE_playSampleNum(S_CLINK);
				}
				else
				{
					JE_playSampleNum(S_SELECT);

					performSave = false;
					JE_operation(saveFileIndex + 1);

					fade_black(15);

					return gameLoaded ? (int)saveFileIndex + 1 : 0;
				}
			}
		}

		if (backOut)
			done = true;

		if (done)
		{
			fade_black(15);

			return 0;
		}
	}
}

int JE_loadScreen(bool net2p, bool saving)
{
	return JE_loadScreenMode(net2p, saving, false);
}

void JE_saveTransferUpload(void)
{
	const int slot = JE_loadScreenMode(false, false, true);
	if (slot > 0)
		saveXferUpload((JE_byte)slot);
}

void JE_saveTransferDownload(void)
{
	if (saveXferDownload())
	{
		JE_loadScreen(saveXferPendingTwoPlayer(), true);
		saveXferPendingClear();
	}
}

// Pin the Upload help wrap before the page arrow.
void qa_test_load_screen_help(void)
{
	const size_t line1 = sizeof(loadHelpUploadLine1) - 1;

	qa_check(strncmp(loadHelpUpload, loadHelpUploadLine1, line1) == 0
	         && loadHelpUpload[line1] == ' ',
	         "the load screen's upload help line wraps where its budget says it does");
	qa_check(JE_textWidth(loadHelpUploadLine1, TINY_FONT) <= LOAD_HELP_BUDGET,
	         "...and that first segment stops short of the page arrow");
}

// A pending download bypasses the usual live-game capture.
static void save_slot_commit(JE_byte slot, const char *name)
{
	if (saveXferPendingApply(slot, name))
		return;

	network_shop_sync_for_save();
	JE_saveGame(slot, name);   // persists or clears the endless run for this slot too
}

void JE_operation(JE_byte slot)
{
	JE_byte flash;
	char stemp[21];
	char tempStr[51];

	// This screen (name entry + on-screen SAVE/CANCEL buttons) needs the absolute pointer.
	const bool op_was_relative = mouseGetRelative();
	mouseSetRelative(false);

	if (!performSave)
	{
		if (saveFiles[slot-1].level > 0)
		{
			if (save_custom_locked(&saveFiles[slot-1]))
			{
				JE_playSampleNum(S_CLINK);
			}
			else
			{
				gameJustLoaded = true;
				JE_loadGame(slot);
				endlessLoadSlot(slot);  // if this slot holds an endless run, re-enter endless mode + restore it
				gameLoaded = true;
			}
		}
	}
	else if (slot % 11 != 0)
	{
		// Seed a downloaded save with its sender's name.
		const JE_SaveFileType *const xfer = saveXferPending();
		const char *const nameSeed = xfer != NULL ? xfer->name : saveFiles[slot-1].name;

		strcpy(stemp, "              ");
		memcpy(stemp, nameSeed, MIN(strlen(nameSeed), (size_t)14));
		temp = strlen(stemp);
		while (stemp[temp-1] == ' ' && --temp) { }  // trim the trailing pad spaces

		// Tests bypass the name and confirmation dialogs.
		if (qa_net_disconnect_save > 0)
		{
			SDL_strlcpy(stemp, "DISCONNECT", sizeof(stemp));
			save_slot_commit(slot, stemp);
			mouseSetRelative(op_was_relative);
			return;
		}

#ifdef PLATFORM_HANDHELD
		// No physical keyboard here: get the name from the software keyboard and fill the field
		// DIRECTLY here (deterministic).
		{
			char kb[15];
			int n = (temp < 14) ? temp : 14;
			memcpy(kb, stemp, (size_t)n);
			kb[n] = '\0';   // pre-fill the keyboard with the current (trimmed) name
			if (console_swkbd(kb, sizeof(kb), 14, kb, "Save name", false))
			{
				memset(stemp, ' ', 14);
				stemp[14] = '\0';
				temp = 0;
				for (const char *c = kb; *c != '\0' && temp < 14; ++c)
				{
					const char u = (unsigned char)*c <= 127U ? (char)toupper((unsigned char)*c) : 0;
					if (u == ' ' || font_ascii[(unsigned char)u] != -1)
						stemp[temp++] = u;
				}
			}
		}
#endif

		flash = 8 * 16 + 10;

		wait_noinput(false, true, false);

		JE_barShade(VGAScreen, 65, 55, 255, 155);

		bool quit = false;
		while (!quit)
		{
			service_SDL_events(true);

			blit_sprite(VGAScreen, 50, 50, OPTION_SHAPES, 35);  // message box

			JE_textShade(VGAScreen, 60, 55, miscText[1-1], 11, 4, DARKEN);
			JE_textShade(VGAScreen, 70, 70, levelName, 11, 4, DARKEN);

			do
			{
				flash = (flash == 8 * 16 + 10) ? 8 * 16 + 2 : 8 * 16 + 10;
				temp3 = (temp3 == 6) ? 2 : 6;

				strcpy(tempStr, miscText[2-1]);
				strncat(tempStr, stemp, temp);
				JE_outText(VGAScreen, 65, 89, tempStr, 8, 3);
				tempW = 65 + JE_textWidth(tempStr, TINY_FONT);
				JE_barShade(VGAScreen, tempW + 2, 90, tempW + 6, 95);
				fill_rectangle_xy(VGAScreen, tempW + 1, 89, tempW + 5, 94, flash);

				int text_x = 54 + 45 - (JE_textWidth(miscText[9], FONT_SHAPES) / 2);
				JE_outTextAdjust(VGAScreen, text_x, 128, miscText[9], 15, -5, FONT_SHAPES, true);

				text_x = 149 + 45 - (JE_textWidth(miscText[10], FONT_SHAPES) / 2);
				JE_outTextAdjust(VGAScreen, text_x, 128, miscText[10], 15, -5, FONT_SHAPES, true);

				for (int i = 0; i < 14; i++)
				{
					setDelay(1);

					NETWORK_KEEP_ALIVE();  // name entry for an online save; hold the link meanwhile

					push_joysticks_as_keyboard();
					service_wait_delay();

					JE_mouseStart();
					JE_showVGA();
					JE_mouseReplace();

					if (newkey || newmouse || new_text)
						break;
				}
			} while (!newkey && !newmouse && !new_text);

			if (mouseButton > 0)
			{
				if (lastmouse_x > 56 && lastmouse_x < 142 && lastmouse_y > 123 && lastmouse_y < 149)
				{
					quit = true;
					if (JE_saveRequest(slot, stemp))
						save_slot_commit(slot, stemp);
				}
				else if (lastmouse_x > 151 && lastmouse_x < 237 && lastmouse_y > 123 && lastmouse_y < 149)
				{
					quit = true;
					JE_playSampleNum(S_SPRING);
				}
			}
			else if (new_text)
			{
				for (size_t ti = 0U; last_text[ti] != '\0'; ++ti)
				{
					const char c = (unsigned char)last_text[ti] <= 127U ? toupper(last_text[ti]) : 0;
					if ((c == ' ' || font_ascii[(unsigned char)c] != -1) &&
					    temp < 14)
					{
						JE_playSampleNum(S_CURSOR);
						stemp[temp] = c;
						temp += 1;
					}
				}
			}
			else if (newkey)
			{
				switch (lastkey_scan)
				{
					case SDL_SCANCODE_BACKSPACE:
					case SDL_SCANCODE_DELETE:
						if (temp)
						{
							temp--;
							stemp[temp] = ' ';
							JE_playSampleNum(S_CLICK);
						}
						break;
					case SDL_SCANCODE_ESCAPE:
						quit = true;
						JE_playSampleNum(S_SPRING);
						break;
					case SDL_SCANCODE_RETURN:
						quit = true;
						if (JE_saveRequest(slot, stemp))
							save_slot_commit(slot, stemp);
						break;
					default:
						break;
				}
			}
		}
	}

	wait_noinput(false, true, false);

	mouseSetRelative(op_was_relative);
}
