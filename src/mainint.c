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
#include "mainint.h"

#include "backgrnd.h"
#include "config.h"
#include "crashlog.h"
#include "editship.h"
#include "endless.h"
#include "custom_episode.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "game_menu.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "lds_play.h"
#include "loudness.h"
#include "menus.h"
#include "mouse.h"
#include "mtrand.h"
#include "musmast.h"
#include "net_savexfer.h"
#include "net_style.h"
#include "network.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyr.h"
#include "palette.h"
#include "params.h"
#include "pcxmast.h"
#include "picload.h"
#include "player.h"
#include "qa.h"
#include "render_list.h"
#include "rollback.h"
#include "touch_ui.h"
#include "net_rollback.h"
#include "shots.h"
#include "sndmast.h"
#include "sim_math.h"
#include "sprite.h"
#include "console_platform.h"
#include "tyrian2.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"
#include "lvlmast.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

bool button[4];

#define MAX_PAGE 8
#define TOPICS 6
const JE_byte topicStart[TOPICS] = { 0, 1, 2, 3, 7, 255 };

JE_shortint constantLastX;
JE_word textErase;
Sint64 upgradeCost;
Sint64 downgradeCost;
JE_boolean performSave;
JE_boolean jumpSection;
JE_boolean useLastBank; /* Use the last 16 colors for DisplayText. */

bool pause_pressed = false, ingamemenu_pressed = false, changefire_pressed = false;

/* debug submenu dimensions for in-game setup */
#define DEBUG_MENU_WIDTH  255
/* Centre the panel horizontally in the playfield (left of the HUD) so it stays
 * centred under the widescreen layout. */
#define DEBUG_MENU_X      ((PLAYFIELD_WIDTH - DEBUG_MENU_WIDTH) / 2)
#define DEBUG_MENU_Y      5
/* total height of debug menu area */
#define DEBUG_MENU_HEIGHT (vga_height - 5 - DEBUG_MENU_Y + 1)

static Uint8 debug_menu_backup[DEBUG_MENU_WIDTH * DEBUG_MENU_HEIGHT];

typedef struct
{
	PlayerItems items;
	bool valid;
}
ExtraShipReturn;

// Each seat keeps its standard loadout for restoration.
static ExtraShipReturn extraShipReturn[2];

static void extraShipLoadoutRefresh(uint pnum, bool overHud);

static bool extraShipsAllowedForMode(bool network, bool twoPlayer, bool coop, bool separate,
	                                 bool timedBattle, bool superTyrianActive, int superArcade)
{
	if (timedBattle || superTyrianActive || superArcade != SA_NONE)
		return false;
	return network ? twoPlayer && (coop || separate) : !twoPlayer;
}

static bool extraShipsAllowedInGame(void)
{
	return extraShipsAllowedForMode(isNetworkGame, twoPlayerMode, coop_mode_active(),
	                                arcade_separate_mode(), timedBattleMode,
	                                superTyrian, superArcadeMode);
}

static bool isExtraShipId(JE_byte ship)
{
	return ship >= 91 && ship <= 100;
}

static void extraShipReturnReset(void)
{
	memset(extraShipReturn, 0, sizeof(extraShipReturn));
}

static void extraShipRememberStandard(uint pnum)
{
	if (pnum >= COUNTOF(extraShipReturn) || isExtraShipId(player[pnum].items.ship))
		return;

	extraShipReturn[pnum].items = player[pnum].items;
	extraShipReturn[pnum].valid = true;
}

static bool extraShipRestoreStandard(uint pnum)
{
	if (pnum >= COUNTOF(extraShipReturn) || !extraShipReturn[pnum].valid)
		return false;

	PlayerItems *const items = &player[pnum].items;
	PlayerItems restored = extraShipReturn[pnum].items;
	for (uint i = 0; i < COUNTOF(restored.weapon); ++i)
		restored.weapon[i].power = items->weapon[i].power;
	restored.sidekick_series = items->sidekick_series;
	restored.sidekick_level = items->sidekick_level;
	restored.super_arcade_mode = items->super_arcade_mode;
	*items = restored;
	return true;
}

/* Keep the last message so a visible pass can repaint anything dropped by
 * silent rollback re-simulation. See hud_message_dirty. */
static char text_window_tint[32];  // opening words, drawn in their own bank ahead of the left text
static unsigned int text_window_tint_bank;
static char text_window_left[96];
static char text_window_right[32];
static int text_window_right_x;
bool hud_message_dirty = false;

/* The erase is unconditional: textErase == 0 no longer proves the bar is clean, because the
 * countdown's own erase can be swallowed when the 1->0 crossing lands in a silent rollback
 * re-simulation pass (sprite blits are no-ops there). */
void JE_repaintTextWindow(void)
{
	blit_sprite(VGAScreenSeg, 16, vga_height - 11, OPTION_SHAPES, 36);  // in-game text area

	// The tinted opening advances x exactly as JE_outText would have, so the words that follow
	// sit where they would in one string.
	int x = 20;
	if (text_window_tint[0] != '\0')
	{
		JE_outText(VGAScreenSeg, x, vga_height - 10, text_window_tint, text_window_tint_bank, 4);
		x += JE_textWidth(text_window_tint, TINY_FONT);
	}

	JE_outText(VGAScreenSeg, x, vga_height - 10, text_window_left, 0, 4);
	if (text_window_right[0] != '\0')
		JE_outText(VGAScreenSeg, text_window_right_x - JE_textWidth(text_window_right, TINY_FONT), vga_height - 10, text_window_right, 0, 4);
}

// Draws a message at the bottom text window on the playing screen.
void JE_drawTextWindow(const char *text)
{
	SDL_strlcpy(text_window_left, text, sizeof(text_window_left));
	text_window_tint[0] = '\0';
	text_window_right[0] = '\0';
	hud_message_dirty = rollback_resim_silent;

	textErase = 100;
	JE_repaintTextWindow();
}

// Draw a split message-bar line: left text at x=20 and right text ending at right_x.
void JE_drawTextWindowSplit(const char *tint, unsigned int tint_bank, const char *left,
                            const char *right, int right_x)
{
	SDL_strlcpy(text_window_tint, (tint != NULL) ? tint : "", sizeof(text_window_tint));
	text_window_tint_bank = tint_bank;
	SDL_strlcpy(text_window_left, left, sizeof(text_window_left));
	SDL_strlcpy(text_window_right, right, sizeof(text_window_right));
	text_window_right_x = right_x;
	hud_message_dirty = rollback_resim_silent;

	textErase = 100;
	JE_repaintTextWindow();
}

void JE_outCharGlow(JE_word x, JE_word y, const char *s)
{
	JE_integer maxloc, loc, z;
	JE_shortint glowcol[60]; /* [1..60] */
	JE_shortint glowcolc[60]; /* [1..60] */
	JE_word textloc[60]; /* [1..60] */
	JE_byte bank;

	setDelay2(1);

	bank = (warningRed) ? 7 : ((useLastBank) ? 15 : 14);

	if (s[0] == '\0')
		return;

	if (frameCountMax == 0)
	{
		JE_textShade(VGAScreen, x, y, s, bank, 0, PART_SHADE);
		JE_showVGA();
	}
	else
	{
		maxloc = strlen(s);
		for (z = 0; z < 60; z++)
		{
			glowcol[z] = -8;
			glowcolc[z] = 1;
		}

		loc = x;
		for (z = 0; z < maxloc; z++)
		{
			textloc[z] = loc;

			int sprite_id = font_ascii[(unsigned char)s[z]];

			if (s[z] == ' ')
				loc += 6;
			else if (sprite_id != -1)
				loc += sprite(TINY_FONT, sprite_id)->width + 1;
		}

		for (loc = 0; (unsigned)loc < strlen(s) + 28; loc++)
		{
			if (!ESCPressed)
			{
				setDelay(frameCountMax);

				NETWORK_KEEP_ALIVE();

				int sprite_id = -1;

				for (z = loc - 28; z <= loc; z++)
				{
					if (z >= 0 && z < maxloc)
					{
						sprite_id = font_ascii[(unsigned char)s[z]];

						if (sprite_id != -1)
						{
							blit_sprite_hv(VGAScreen, textloc[z], y, TINY_FONT, sprite_id, bank, glowcol[z]);

							glowcol[z] += glowcolc[z];
							if (glowcol[z] > 9)
								glowcolc[z] = -1;
						}
					}
				}
				if (sprite_id != -1 && --z < maxloc)
					blit_sprite_dark(VGAScreen, textloc[z] + 1, y + 1, TINY_FONT, sprite_id, true);

				if (JE_anyButton())
					frameCountMax = 0;

				do
				{
					if (levelWarningDisplay)
						JE_updateWarning(VGAScreen);

					SDL_Delay(16);
				} while (!(getDelayTicks() == 0 || ESCPressed));

				JE_showVGA();
			}
		}
	}
}

void JE_drawPortConfigButtons(void) // rear weapon pattern indicator
{
	if (split_arcade_mode())
		return;
	const uint player_index = gameplay_local_player_index();

	const int x_lit = HUD_X(285);
	const int x_unlit = HUD_X(302);
	if (player[player_index].weapon_mode == 1)
	{
		blit_sprite(VGAScreenSeg, x_lit, 44, OPTION_SHAPES, 18);  // lit
		blit_sprite(VGAScreenSeg, x_unlit, 44, OPTION_SHAPES, 19);  // unlit
	}
	else // == 2
	{
		blit_sprite(VGAScreenSeg, x_lit, 44, OPTION_SHAPES, 19);  // unlit
		blit_sprite(VGAScreenSeg, x_unlit, 44, OPTION_SHAPES, 18);  // lit
	}
}

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
		{
			// Find menu item that was hovered or clicked.
			for (size_t i = 0; i < menuItemsCount; ++i)
			{
				const int xMenuItem = xCenter - wMenuItem[i] / 2;
				if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem[i])
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
						    lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem[i] &&
						    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
						{
							action = true;
						}

						break;
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				done = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
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

				done = true;
				break;
			}
			default:
				break;
			}
		}

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

// cost to upgrade a weapon power from power-1 (where power == 0 indicates an unupgraded weapon)
Sint64 weapon_upgrade_cost(Sint64 base_cost, unsigned int power)
{
	assert(power <= 11);

	unsigned int temp = 0;

	// 0 1 3 6 10 15 21 29 ...
	for (; power > 0; power--)
		temp += power;

	return base_cost * temp;
}

/* Prices are Sint64 like the wallets they are checked against; the widest product here (a 16-bit
 * base cost, the power ladder, the expert and endless multipliers) stays far below 2^63. */
Sint64 JE_getCost(JE_byte itemType, JE_word itemNum)
{
	Sint64 cost = 0;

	switch (itemType)
	{
	case 2:
		// ships[] stops at SHIP_DRAGONWING, but only ids above 90 are extra ships. Test the
		// array's own bound, not the extra-ship one, or an id in between reads past the end.
		cost = (itemNum > SHIP_DRAGONWING) ? 100 : ships[itemNum].cost;
		break;
	case 3:
	case 4:
		cost = weaponPort[itemNum].cost;

		const uint port = itemType - 3,
			item_power = player[JE_shopPlayerIndex()].items.weapon[port].power - 1;

		downgradeCost = weapon_upgrade_cost(cost, item_power);
		upgradeCost = weapon_upgrade_cost(cost, item_power + 1);
		break;
	case 5:
		cost = shields[itemNum].cost;
		break;
	case 6:
		cost = powerSys[itemNum].cost;
		break;
	case 7:
	case 8:
		cost = options[itemNum].cost;
		break;
	}

	if (expertMode)
	{
		// purchase price scales with the shop knob; power upgrades with their own knob
		cost = cost * expertShopCostMult;

		if (itemType == 3 || itemType == 4)
		{
			downgradeCost = downgradeCost * expertUpgradeCostMult;
			upgradeCost = upgradeCost * expertUpgradeCostMult;
		}
	}

	if (endlessMode)
	{
		// Endless: shop prices inflate with depth (+19%/level, capped 100x) since income also
		// scales; the first five zones ramp at half slope.
		int pct;
		if (endlessRunDepth < 5)
			pct = 100 + endlessRunDepth * 19 / 2;                 // first 5 zones: gentle half-slope
		else
			pct = 100 + 4 * 19 / 2 + (endlessRunDepth - 4) * 19;  // then full +19%/depth (continuous at zone 5)
		if (pct > 10000)
			pct = 10000;                            // cap at 100x
		pct += endlessShopTaxPercent();             // Loan Shark: a permanent debt tax on top of the depth cap
		if (endlessActiveMods & ENDLESS_MOD_FAVOR)  // Merchant's Favor: the outpost slashes prices
			pct = pct * 65 / 100;
		// Financier perk: better terms, in basis points because the per-stack cut isn't a whole
		// percent. Multiplies like Favor does, so the two compound instead of one overriding the other.
		pct = pct * endlessPerkShopCostBp() / 10000;

		cost = cost * pct / 100;

		if (itemType == 3 || itemType == 4)
		{
			downgradeCost = downgradeCost * pct / 100;
			upgradeCost   = upgradeCost * pct / 100;
		}
	}

	return cost;
}


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

// Which sessions may load `slot`. The record's co-op tag says it carries two full loadouts;
// the slot's Endless half says whether a run sits behind it, which is what separates the two
// online co-op lobbies from each other.
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
			// The save-name/overwrite dialogs (JE_operation) blit the OPTION_SHAPES message
			// box, whose 224..239 ramp is fire-red under this pic's palette 7 but brown under
			// the menu palette it was drawn for. Pic 2 never touches that ramp, so graft the
			// menu values in.
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

Sint64 JE_totalScore(const Player *this_player)
{
	Sint64 temp = this_player->cash;

	temp += JE_getValue(2, this_player->items.ship);
	temp += JE_getValue(3, this_player->items.weapon[FRONT_WEAPON].id);
	temp += JE_getValue(4, this_player->items.weapon[REAR_WEAPON].id);
	temp += JE_getValue(5, this_player->items.shield);
	temp += JE_getValue(6, this_player->items.generator);
	temp += JE_getValue(7, this_player->items.sidekick[LEFT_SIDEKICK]);
	temp += JE_getValue(8, this_player->items.sidekick[RIGHT_SIDEKICK]);

	return temp;
}

Sint64 JE_getValue(JE_byte itemType, JE_word itemNum)
{
	Sint64 value = 0;

	switch (itemType)
	{
	case 2:
		// Extra ships (id above 90) have no shop row and sell for nothing.
		if (itemNum <= SHIP_DRAGONWING)
			value = ships[itemNum].cost;
		break;
	case 3:
	case 4:;
		const Sint64 base_value = weaponPort[itemNum].cost;

		const uint port = itemType - 3;
		const uint item_power = player[JE_shopPlayerIndex()].items.weapon[port].power - 1;

		value = base_value;
		for (unsigned int i = 1; i <= item_power; ++i)
			value += weapon_upgrade_cost(base_value, i);
		break;
	case 5:
		value = shields[itemNum].cost;
		break;
	case 6:
		value = powerSys[itemNum].cost;
		break;
	case 7:
	case 8:
		value = options[itemNum].cost;
		break;
	}

	return value;
}

void JE_nextEpisode(void)
{
	strcpy(lastLevelName, "Completed");

	// The base episode does not own a custom run's high score.
	if (episodeNum == initial_episode_num && !gameHasRepeated && !isNetworkGame && !constantPlay &&
	    !endlessMode && !customEpisodeActive())
	{
		JE_highScoreCheck();
	}

	// Online co-op Campaign has its own board and no name-entry dialog; it owns the conditions
	// for a record, so this is the one place that offers it an episode.
	coopCampaignScoreNote();

	/* Wire campaign run: the board's inputs at the episode boundary. A lobby row picks the
	 * episode, so nothing else proves both peers established where the run began, or that they
	 * agree on the pair's combined cash by the time it is scored. */
	if (qa_net_gameplay_ticks > 0 && coopCampaignMode)
	{
		const int e = initial_episode_num - 1;
		printf("NET CAMPAIGN RECORD player=%u start=%u episode=%u cash=%lld recorded=%lld\n",
		       thisPlayerNum, (unsigned)initial_episode_num, (unsigned)episodeNum,
		       (long long)(player[0].cash + player[1].cash),
		       (e >= 0 && e < COOP_CAMPAIGN_SCORE_EPISODES) ? (long long)coopCampaignScores[e].score : -1LL);
		fflush(stdout);
	}

	unsigned int newEpisode = JE_findNextEpisode();

	if (jumpBackToEpisode1)
	{
		// Roll before the credits. Their animation consumes a variable number of random values, but
		// both peers are still on the same stream here.
		const bool superCarrot = (mt_rand() % 6) == 0;

		if (episodeNum > 2 &&
			!constantPlay && !endlessMode)
		{
			JE_playCredits();
		}

		// randomly give player the SuperCarrot
		if (superCarrot)
		{
			// Every ship flying its own arsenal gets the whole joke. Vanilla's lone player-two
			// line below is the linked pair's shared one, where the rear bay IS ship two.
			for (uint p = 0; p < (dual_ship_mode() ? COUNTOF(player) : 1u); ++p)
			{
				player[p].items.ship = 2;                      // SuperCarrot
				player[p].items.weapon[FRONT_WEAPON].id = 23;  // Banana Blast
				player[p].items.weapon[REAR_WEAPON].id = 24;   // Banana Blast Rear

				for (uint i = 0; i < COUNTOF(player[p].items.weapon); ++i)
					player[p].items.weapon[i].power = 1;

				player[p].last_items = player[p].items;
			}

			if (!dual_ship_mode())
				player[1].items.weapon[REAR_WEAPON].id = 24;   // Banana Blast Rear
		}
	}

	if (newEpisode != episodeNum)
		JE_initEpisode(newEpisode);

	gameLoaded = true;
	mainLevel = FIRST_LEVEL;
	saveLevel = FIRST_LEVEL;

	play_song(26);

	JE_clr256(VGAScreen);
	memcpy(colors, palettes[6-1], sizeof(colors));

	const char *episodeBanner = customEpisodeActive()
		? customEpisodeActiveTitle() : episode_name[episodeNum];
	JE_dString(VGAScreen, JE_fontCenter(episodeBanner, SMALL_FONT_SHAPES), 130, episodeBanner, SMALL_FONT_SHAPES);
	JE_dString(VGAScreen, JE_fontCenter(miscText[5-1], SMALL_FONT_SHAPES), 185, miscText[5-1], SMALL_FONT_SHAPES);

	const bool waitForEpisodeBanner = !constantPlay && qa_net_gameplay_ticks == 0;
	if (waitForEpisodeBanner)
		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	// A press surviving the fades would drop the banner on its first JE_anyButton.
	wait_noinput(true, true, true);
	newkey = newmouse = false;
	// A gameplay wire test has no player to press past the episode banner.
	if (waitForEpisodeBanner)
	{
		do
		{
			touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
			touch_ui_idle_repaint();
			NETWORK_KEEP_ALIVE();

			SDL_Delay(16);
		} while (!JE_anyButton());
	}

	fade_black(15);
}

void JE_initPlayerData(void)
{
	/* JE: New Game Items/Data */
	extraShipReturnReset();

	player[0].items.ship = 1;                     // USP Talon
	player[0].items.weapon[FRONT_WEAPON].id = 1;  // Pulse Cannon
	player[0].items.weapon[REAR_WEAPON].id = 0;   // None
	player[0].items.shield = 4;                   // Gencore High Energy Shield
	player[0].items.generator = 2;                // Advanced MR-12
	for (uint i = 0; i < COUNTOF(player[0].items.sidekick); ++i)
		player[0].items.sidekick[i] = 0;          // None
	player[0].items.special = 0;                  // None
	// Which Super Arcade ruleset a ship flies now lives on the ship (player_sa_ship), because
	// online Super Arcade gives the two players different ones.
	player[0].items.super_arcade_mode = SA_NONE;

	player[0].last_items = player[0].items;

	player[1].items = player[0].items;
	player[1].items.weapon[REAR_WEAPON].id = 15;  // Vulcan Cannon
	player[1].items.sidekick_level = 101;         // 101, 102, 103
	player[1].items.sidekick_series = 0;          // None

	gameHasRepeated = false;
	onePlayerAction = false;
	superArcadeMode = SA_NONE;
	superTyrian = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	arcadeSeparateMode = false;
	timedBattleMode = false;
	endlessMode = false;

	secretHint = (mt_rand() % 3) + 1;

	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		for (uint i = 0; i < COUNTOF(player->items.weapon); ++i)
		{
			player[p].items.weapon[i].power = 1;
		}

		player[p].weapon_mode = 1;
		player[p].armor = player[p].hull_armor = ships[player[p].items.ship].dmg;

		player[p].is_dragonwing = (p == 1);
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;

	}

	mainLevel = FIRST_LEVEL;
	saveLevel = FIRST_LEVEL;

	strcpy(lastLevelName, miscText[19]);
}

void JE_sortHighScores(void)
{
	T2KHighScoreType tempHiScore;
	for (int table = 0; table < 20; ++table)
	{
		if (t2kHighScores[table][1].score > t2kHighScores[table][0].score)
		{
			memcpy(&tempHiScore,             &t2kHighScores[table][0], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][0], &t2kHighScores[table][1], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][1], &tempHiScore,             sizeof(T2KHighScoreType));
		}
		if (t2kHighScores[table][2].score > t2kHighScores[table][1].score)
		{
			memcpy(&tempHiScore,             &t2kHighScores[table][1], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][1], &t2kHighScores[table][2], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][2], &tempHiScore,             sizeof(T2KHighScoreType));
		}
		if (t2kHighScores[table][1].score > t2kHighScores[table][0].score)
		{
			memcpy(&tempHiScore,             &t2kHighScores[table][0], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][0], &t2kHighScores[table][1], sizeof(T2KHighScoreType));
			memcpy(&t2kHighScores[table][1], &tempHiScore,             sizeof(T2KHighScoreType));
		}
	}
}

// The Endless high-score page.

/* One list at a time, each a step narrower than the last, and each row the deepest figure in the
 * list it opens. The four Base Level rules are a level of this rather than four boards to page
 * between; endlessBestZoneAnyRule holds why summarising them is sound. */
typedef enum
{
	ENDLESS_PAGE_MODES = 0,   // crew size and run mode, at its deepest zone under any rule
	ENDLESS_PAGE_RULES,       // ...that mode's four Base Level rules
	ENDLESS_PAGE_DIFFS,       // ...and that rule's difficulties, the one list an erase happens on
	ENDLESS_PAGE_LEVELS
} EndlessPageLevel;

/* One row per run mode and crew size: the three solo modes, then the three co-op ones. A rule's
 * breakdown has one row per difficulty, plus a leading row for its record on any of them. */
#define ENDLESS_PAGE_MODE_ROWS  (ENDLESS_RUNMODE_COUNT * ENDLESS_PLAYER_TABLES)
#define ENDLESS_PAGE_ROW_TABLE(row) ((row) / ENDLESS_RUNMODE_COUNT)
#define ENDLESS_PAGE_ROW_MODE(row)  ((row) % ENDLESS_RUNMODE_COUNT)
#define ENDLESS_PAGE_DIFF_ROWS  (ENDLESS_DIFFICULTY_COUNT + 1)
#define ENDLESS_PAGE_ROW_ANY    0   // the any-difficulty row; the rest are difficulty slot + 1

/* Everything the page navigates between, so the two halves pass one thing around. Each level keeps
 * its own selection, which is what the level under it is a breakdown of. */
typedef struct
{
	int  level;          // EndlessPageLevel: which list is showing
	int  mode;           // selected mode row, carrying both the crew size and the run mode
	int  variant;        // selected Base Level rule (an EndlessBaseRule)
	int  row;            // selected breakdown row (ENDLESS_PAGE_ROW_ANY, else difficulty slot + 1)
	bool confirmErase;   // an erase is waiting on an answer
	int  confirmChoice;  // which answer the cursor sits on
} EndlessPageState;

/* Page geometry, shared by the draw and input halves below. */
static const int endlessPageXCenter = 160;   // center of the 320px menu field
static const int endlessPageBandY0 = 74, endlessPageBandH = 70;
static const int endlessPageRowDy = 14, endlessPageRowH = 12;
static const int endlessPageDiffDy = 11, endlessPageDiffH = 10;
static const int endlessPageDiffGap = 5;   // sets the any-difficulty row apart from the six under it
static const int endlessPageNoteY = 160;   // first note line, keyed to the band and not to a list
static const int endlessPageConfirmY0 = 100, endlessPageConfirmDy = 14, endlessPageConfirmH = 13;

static const char *const endlessPageConfirmChoice[] = { "No, Keep It", "Yes, Erase It" };

// The note lines are the widest thing on the page, so they set the block the rows line up inside.
// Each list takes the one after its own level, and all three size to the whole set so their
// columns agree.
static const char *const endlessPageNote[] =
{
	"C = a custom weapon/ship was used during the run.",
	"Select a mode to break it down by base level.",
	"Select a base level to break it down by difficulty.",
	"Selecting a record erases it, once you confirm.",
};
COMPILE_TIME_ASSERT(endless_page_notes_per_level,
                    COUNTOF(endlessPageNote) == ENDLESS_PAGE_LEVELS + 1);

// Label for a breakdown row: the any-difficulty row, else the difficulty's own name.
static const char *endlessPageDiffName(int row)
{
	if (row == ENDLESS_PAGE_ROW_ANY)
		return "Any Difficulty";
	return difficultyNameB[endlessDifficultyLevel[row - 1]];
}

// The zone and custom mark a breakdown row shows, under the rule the page has narrowed to.
static int endlessPageDiffZone(int variant, int players, EndlessRunMode mode, int row)
{
	return (row == ENDLESS_PAGE_ROW_ANY) ? endlessBestZoneAny(variant, players, mode)
	                                     : endlessBestZoneForDifficulty(variant, players, mode, row - 1);
}

static const char *endlessPageDiffMark(int variant, int players, EndlessRunMode mode, int row)
{
	return (row == ENDLESS_PAGE_ROW_ANY) ? endlessRecordAnyCustomMark(variant, players, mode)
	                                     : endlessRecordDiffCustomMark(variant, players, mode, row - 1);
}

// A mode row's label carries its crew size, since the two sets sit in one list.
static const char *endlessPageModeName(int row)
{
	static char buf[32];
	snprintf(buf, sizeof(buf), "%s %s",
	         (ENDLESS_PAGE_ROW_TABLE(row) == 1) ? "2P" : "1P",
	         endlessRunModeName((EndlessRunMode)ENDLESS_PAGE_ROW_MODE(row)));
	return buf;
}

static int endlessPageRows(int level)
{
	if (level == ENDLESS_PAGE_MODES)
		return ENDLESS_PAGE_MODE_ROWS;
	return (level == ENDLESS_PAGE_RULES) ? ENDLESS_BASE_TABLES : ENDLESS_PAGE_DIFF_ROWS;
}

// One row of whichever list is showing: what it is called, how deep it got, and its custom mark.
typedef struct { const char *label, *mark; int zone; } EndlessPageRow;

static EndlessPageRow endlessPageRowAt(const EndlessPageState *page, int level, int row)
{
	const int table = ENDLESS_PAGE_ROW_TABLE(page->mode);
	const EndlessRunMode mode = (EndlessRunMode)ENDLESS_PAGE_ROW_MODE(page->mode);
	EndlessPageRow out = { "", "", 0 };

	switch (level)
	{
	case ENDLESS_PAGE_MODES:
	{
		const int rowTable = ENDLESS_PAGE_ROW_TABLE(row);
		const EndlessRunMode rowMode = (EndlessRunMode)ENDLESS_PAGE_ROW_MODE(row);
		out.label = endlessPageModeName(row);
		out.zone  = endlessBestZoneAnyRule(rowTable, rowMode);
		out.mark  = endlessRecordAnyRuleCustomMark(rowTable, rowMode);
		break;
	}
	case ENDLESS_PAGE_RULES:
	{
		// In the Base Level row's order, which pairs each rule with its Shuffle twin.
		const int rule = (int)endlessBaseRuleAtMenuIndex(row);
		out.label = endlessBaseLevelRuleName(rule);
		out.zone  = endlessBestZoneAny(rule, table, mode);
		out.mark  = endlessRecordAnyCustomMark(rule, table, mode);
		break;
	}
	default:
		out.label = endlessPageDiffName(row);
		out.zone  = endlessPageDiffZone(page->variant, table, mode, row);
		out.mark  = endlessPageDiffMark(page->variant, table, mode, row);
		break;
	}
	return out;
}

// Where the cursor sits on the list showing, and where a click puts it. Each level identifies its
// selection in its own terms, so both go through here.
static int endlessPageSelected(const EndlessPageState *page, int level)
{
	if (level == ENDLESS_PAGE_MODES)
		return page->mode;
	if (level == ENDLESS_PAGE_RULES)
		return endlessBaseRuleMenuIndex((EndlessBaseRule)page->variant);
	return page->row;
}

static void endlessPageSelect(EndlessPageState *page, int level, int row)
{
	if (level == ENDLESS_PAGE_MODES)
		page->mode = row;
	else if (level == ENDLESS_PAGE_RULES)
		page->variant = (int)endlessBaseRuleAtMenuIndex(row);
	else
		page->row = row;
}

// How the page names where it has narrowed to, in the words of the rows it came through.
static const char *endlessPageTrail(const EndlessPageState *page, int level)
{
	static char buf[64];
	const char *const table = endlessRecordTableName(ENDLESS_PAGE_ROW_TABLE(page->mode));
	const char *const mode = endlessRunModeName((EndlessRunMode)ENDLESS_PAGE_ROW_MODE(page->mode));

	if (level <= ENDLESS_PAGE_RULES)
		snprintf(buf, sizeof(buf), "%s %s", table, mode);
	else
		snprintf(buf, sizeof(buf), "%s %s, %s", table, mode,
		         endlessBaseLevelRuleName(page->variant));
	return buf;
}

// Center that block and hang the columns off its edges: labels start at the left, zones end
// right-aligned on xZoneRight, and the custom mark takes the strip after it so a marked record
// still ends flush with the notes.
static void endlessPageColumns(int *xLabel, int *xZoneRight)
{
	int blockW = 0;
	for (int i = 0; i < (int)COUNTOF(endlessPageNote); ++i)
		blockW = MAX(blockW, JE_textWidth(endlessPageNote[i], small_font));

	*xLabel = endlessPageXCenter - blockW / 2;
	*xZoneRight = *xLabel + blockW - JE_textWidth(" C", small_font);
}

// One "Label ....... 58 C" row. The mark's own leading space is the gap after the zone.
static void endlessPageDrawRow(int xLabel, int xZoneRight, int y, const char *label,
                               int zoneValue, const char *mark, bool selected)
{
	char text[32], zone[16];
	snprintf(text, sizeof(text), "%s:", label);
	if (zoneValue > 0)
		snprintf(zone, sizeof(zone), "%d", zoneValue);
	else
		SDL_strlcpy(zone, "None", sizeof(zone));

	const int labelShade = selected ? 6 : 0, zoneShade = selected ? 6 : 2;
	JE_textShade(VGAScreen, xLabel, y, text, 15, labelShade, FULL_SHADE);
	JE_textShade(VGAScreen, xZoneRight - JE_textWidth(zone, small_font), y, zone, 15, zoneShade, FULL_SHADE);
	JE_textShade(VGAScreen, xZoneRight, y, mark, 15, zoneShade, FULL_SHADE);
}

// Row baseline on any of the lists. The breakdown's any-difficulty row summarises the six below it,
// so it stands off from them rather than reading as one of them.
static int endlessPageRowY(int level, int row)
{
	if (level == ENDLESS_PAGE_DIFFS)
	{
		return endlessPageBandY0 + endlessPageDiffDy * row
		       + ((row > ENDLESS_PAGE_ROW_ANY) ? endlessPageDiffGap : 0);
	}

	const int span = endlessPageRowDy * (endlessPageRows(level) - 1);
	return endlessPageBandY0 + (endlessPageBandH - span) / 2 + endlessPageRowDy * row;
}

// How tall a row is to the mouse, which follows the pitch its list is drawn at.
static int endlessPageHitH(int level)
{
	return (level == ENDLESS_PAGE_DIFFS) ? endlessPageDiffH : endlessPageRowH;
}

/* The co-op Campaign board's columns take the width of the legacy 320 field, like the episode
 * boards on this screen: two ten-character names followed by the difficulty and the credit rule
 * need all of it. The record line is indented under its figure. */
static const int coopPageXLabel = 20, coopPageXRight = 300, coopPageXIndent = 8;

int coopCampaignRecordLineWidthPx(void)
{
	return coopPageXRight - (coopPageXLabel + coopPageXIndent);
}

/* Player names come from the lobby, so the pair can be wider than the row on its own. The terms
 * are what makes two figures comparable, so the names give way to them. */
void coopCampaignRecordLine(char *out, size_t outSize, const CoopCampaignScore *record, int widthPx)
{
	const char *const credit = coopCampaignCreditName(record->credit);
	char terms[48];
	snprintf(terms, sizeof(terms), "  (%s%s%s)",
	         difficultyNameB[MIN(record->difficulty, DIFFICULTY_10)],
	         credit != NULL ? ", " : "", credit != NULL ? credit : "");

	char names[sizeof(record->name)];
	SDL_strlcpy(names, record->name, sizeof(names));

	const int budgetPx = widthPx - JE_textWidth(terms, small_font);
	size_t keep = strlen(names);
	while (keep > 0 && JE_textWidth(names, small_font) > budgetPx)
	{
		--keep;
		SDL_strlcpy(names + keep, "...", sizeof(names) - keep);
	}

	snprintf(out, outSize, "%s%s", names, terms);
}

/* The online co-op Campaign board: the best combined cash each episode has been finished with,
 * and who was flying. Read-only, so it needs no selection or erase machinery. */
static void JE_drawCoopCampaignPage(void)
{
	static const char note[] = "The best combined cash for each episode, online.";

	/* Two tiny-font lines per episode. Both carry a full shade, and the difficulty's parentheses
	 * are the font's tallest glyphs, so each pitch has to clear a descender and its outline. */
	const int yFirst = 75, yPitch = 18, yNameOffset = 8, yNoteGap = 14;

	draw_font_hv_shadow(VGAScreen, endlessPageXCenter, 55, "Two Players, One Campaign",
	                    normal_font, centered, 15, -3, false, 2);

	for (int e = 0; e < COOP_CAMPAIGN_SCORE_EPISODES; ++e)
	{
		const int y = yFirst + yPitch * e;
		char label[40], value[32];
		snprintf(label, sizeof(label), "%s:", episode_name[e + 1]);
		if (coopCampaignScores[e].score > 0)
			snprintf(value, sizeof(value), "%lld", (long long)coopCampaignScores[e].score);
		else
			SDL_strlcpy(value, "None", sizeof(value));

		JE_textShade(VGAScreen, coopPageXLabel, y, label, 15, 0, FULL_SHADE);
		JE_textShade(VGAScreen, coopPageXRight - JE_textWidth(value, small_font), y, value,
		             15, 2, FULL_SHADE);

		/* The names go on their own indented line, so a long pair cannot run into the figure. The
		 * credit rule joins the difficulty: both decide what the figure above them is worth. */
		if (coopCampaignScores[e].score > 0 && coopCampaignScores[e].name[0] != '\0')
		{
			char who[80];
			coopCampaignRecordLine(who, sizeof(who), &coopCampaignScores[e],
			                       coopCampaignRecordLineWidthPx());
			JE_textShade(VGAScreen, coopPageXLabel + coopPageXIndent, y + yNameOffset, who,
			             15, 4, FULL_SHADE);
		}
	}

	const int yNote = yFirst + yPitch * (COOP_CAMPAIGN_SCORE_EPISODES - 1) + yNameOffset + yNoteGap;
	JE_textShade(VGAScreen, endlessPageXCenter - JE_textWidth(note, small_font) / 2, yNote,
	             note, 15, 2, FULL_SHADE);
}

static void JE_drawEndlessRecordPage(const EndlessPageState *page)
{
	int xLabel, xZoneRight;
	endlessPageColumns(&xLabel, &xZoneRight);

	if (page->confirmErase)
	{
		/* Name exactly what is about to go: the trail carries the crew size, mode and rule it was
		 * reached through, and the question the difficulty. The any-difficulty row holds no record
		 * of its own, so erasing it takes the deepest one under it and falls back to what is left. */
		char question[80];
		if (page->row == ENDLESS_PAGE_ROW_ANY)
			SDL_strlcpy(question, "Erase its deepest record?", sizeof(question));
		else
			snprintf(question, sizeof(question), "Erase its record on %s?",
			         endlessPageDiffName(page->row));

		const char *const trail = endlessPageTrail(page, ENDLESS_PAGE_DIFFS);
		draw_font_hv_shadow(VGAScreen, endlessPageXCenter, 55, "Are You Sure?",
		                    normal_font, centered, 15, -3, false, 2);
		JE_textShade(VGAScreen, endlessPageXCenter - JE_textWidth(trail, small_font) / 2, 76,
		             trail, 15, 4, FULL_SHADE);
		JE_textShade(VGAScreen, endlessPageXCenter - JE_textWidth(question, small_font) / 2, 86,
		             question, 15, 2, FULL_SHADE);

		for (int i = 0; i < (int)COUNTOF(endlessPageConfirmChoice); ++i)
		{
			draw_font_hv_shadow(VGAScreen, endlessPageXCenter, endlessPageConfirmY0 + endlessPageConfirmDy * i,
			                    endlessPageConfirmChoice[i], normal_font, centered, 15,
			                    i == page->confirmChoice ? -1 : -4, false, 2);
		}

		static const char note[] = "A record cannot be brought back.";
		JE_textShade(VGAScreen, endlessPageXCenter - JE_textWidth(note, small_font) / 2,
		             endlessPageConfirmY0 + endlessPageConfirmDy * (int)COUNTOF(endlessPageConfirmChoice) + 10,
		             note, 15, 2, FULL_SHADE);
		return;
	}

	// The top list is the whole board, so it names the figures; the ones under it name their slice
	// of it instead.
	draw_font_hv_shadow(VGAScreen, endlessPageXCenter, 55,
	                    (page->level == ENDLESS_PAGE_MODES) ? "Furthest Zone"
	                                                        : endlessPageTrail(page, page->level),
	                    normal_font, centered, 15, -3, false, 2);

	const int rows = endlessPageRows(page->level);
	const int selected = endlessPageSelected(page, page->level);
	for (int i = 0; i < rows; ++i)
	{
		const EndlessPageRow row = endlessPageRowAt(page, page->level, i);
		endlessPageDrawRow(xLabel, xZoneRight, endlessPageRowY(page->level, i),
		                   row.label, row.zone, row.mark, i == selected);
	}

	JE_textShade(VGAScreen, xLabel, endlessPageNoteY, endlessPageNote[0], 15, 2, FULL_SHADE);
	JE_textShade(VGAScreen, xLabel, endlessPageNoteY + 10, endlessPageNote[1 + page->level],
	             15, 2, FULL_SHADE);
}

// Answer a pending erase. Confirming is the only path that touches a record.
static void JE_endlessRecordPageAnswer(EndlessPageState *page, int choice)
{
	if (choice == 1)
	{
		const int table = ENDLESS_PAGE_ROW_TABLE(page->mode);
		const EndlessRunMode mode = (EndlessRunMode)ENDLESS_PAGE_ROW_MODE(page->mode);
		if (page->row == ENDLESS_PAGE_ROW_ANY)
			endlessClearDeepestRecord(page->variant, table, mode);
		else
			endlessClearRecordDifficulty(page->variant, table, mode, page->row - 1);
		JE_playSampleNum(S_ITEM);
	}
	else
	{
		JE_playSampleNum(S_SPRING);
	}
	page->confirmErase = false;
}

// Ask about the selected breakdown row, unless it has no record to lose.
static void JE_endlessRecordPageArm(EndlessPageState *page)
{
	if (endlessPageDiffZone(page->variant, ENDLESS_PAGE_ROW_TABLE(page->mode),
	                        (EndlessRunMode)ENDLESS_PAGE_ROW_MODE(page->mode), page->row) > 0)
	{
		page->confirmErase = true;
		page->confirmChoice = 0;   // always opens on "No, Keep It"
		JE_playSampleNum(S_SELECT);
	}
	else
	{
		JE_playSampleNum(S_SPRING);   // nothing to erase
	}
}

// Step in from the selected row, or arm an erase once there is nothing left to narrow.
static void JE_endlessRecordPageEnter(EndlessPageState *page)
{
	if (page->level == ENDLESS_PAGE_DIFFS)
	{
		JE_endlessRecordPageArm(page);
		return;
	}

	// Open the level below on its first row, so stepping into a different mode or rule starts from
	// that one's own summary.
	++page->level;
	endlessPageSelect(page, page->level, 0);
	JE_playSampleNum(S_SELECT);
}

// Endless-page input. Returns true when it consumed the tick, which leaves the shared paging and
// exit handling untouched. A pending erase consumes everything, so nothing else can act under it.
static bool JE_endlessRecordPageInput(EndlessPageState *page)
{
	const int rows = endlessPageRows(page->level);
	const int rowH = endlessPageHitH(page->level);

	if (newmouse)
	{
		if (page->confirmErase)
		{
			if (lastmouse_but == SDL_BUTTON_LEFT)
			{
				for (int i = 0; i < (int)COUNTOF(endlessPageConfirmChoice); ++i)
				{
					const int y = endlessPageConfirmY0 + endlessPageConfirmDy * i;
					const int w = JE_textWidth(endlessPageConfirmChoice[i], normal_font);
					if (mouse_y >= y && mouse_y < y + endlessPageConfirmH
					    && mouse_x >= endlessPageXCenter - w / 2 && mouse_x < endlessPageXCenter + w / 2)
					{
						JE_endlessRecordPageAnswer(page, i);
						return true;
					}
				}
			}
			else if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);
				page->confirmErase = false;
			}
			return true;   // a pending answer never falls through to paging or exit
		}

		if (lastmouse_but == SDL_BUTTON_RIGHT && page->level > ENDLESS_PAGE_MODES)
		{
			JE_playSampleNum(S_SPRING);   // back out one level; only the mode list leaves the screen
			--page->level;
			return true;
		}

		if (lastmouse_but == SDL_BUTTON_LEFT)
		{
			int xLabel, xZoneRight;
			endlessPageColumns(&xLabel, &xZoneRight);

			for (int i = 0; i < rows; ++i)
			{
				const int y = endlessPageRowY(page->level, i);
				// The whole label-to-record span is clickable, gaps included.
				if (mouse_y >= y && mouse_y < y + rowH
				    && mouse_x >= xLabel && mouse_x < endlessPageXCenter + (endlessPageXCenter - xLabel))
				{
					endlessPageSelect(page, page->level, i);
					JE_endlessRecordPageEnter(page);
					return true;
				}
			}
		}
		return false;
	}

	if (!newkey)
		return false;

	switch (lastkey_scan)
	{
	case SDL_SCANCODE_UP:
	case SDL_SCANCODE_DOWN:
	{
		const int step = (lastkey_scan == SDL_SCANCODE_UP) ? -1 : 1;
		if (page->confirmErase)
		{
			const int n = (int)COUNTOF(endlessPageConfirmChoice);
			page->confirmChoice = (page->confirmChoice + n + step) % n;
		}
		else
		{
			endlessPageSelect(page, page->level,
			                  (endlessPageSelected(page, page->level) + rows + step) % rows);
		}
		JE_playSampleNum(S_CURSOR);
		return true;
	}
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
	{
		if (page->confirmErase)
			JE_endlessRecordPageAnswer(page, page->confirmChoice);
		else
			JE_endlessRecordPageEnter(page);
		return true;
	}
	default:
		// Esc unwinds one level at a time; a pending answer or a narrowed list swallows the rest.
		if (page->confirmErase)
		{
			if (lastkey_scan == SDL_SCANCODE_ESCAPE)
			{
				JE_playSampleNum(S_SPRING);
				page->confirmErase = false;
			}
			return true;
		}
		if (page->level > ENDLESS_PAGE_MODES)
		{
			if (lastkey_scan == SDL_SCANCODE_ESCAPE)
			{
				JE_playSampleNum(S_SPRING);
				--page->level;
			}
			return true;
		}
		return false;
	}
}

void JE_highScoreScreen(void)
{
	set_menu_centered(true);

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer and arrow sprites

	// The press that opened the boards would page straight through them.
	wait_noinput(true, true, true);
	newkey = newmouse = false;

	bool restart = true;

	size_t episodeIndex = 0;
	/* Five episodes, three timed battles, the online co-op Campaign board, and the Endless board.
	 * Endless keeps four sets of records, one per base-level rule, but they are a level of that
	 * board's own drill-down rather than four pages to be paged past. */
	const size_t endlessPage = 9;
	const size_t episodeCount = endlessPage + 1;
	const size_t coopPage = endlessPage - 1;

	// Endless page state: which list is showing, its selection at each level, and a pending erase.
	EndlessPageState endlessPageState = {
		ENDLESS_PAGE_MODES, 0, ENDLESS_BASE_VARIED, ENDLESS_PAGE_ROW_ANY, false, 0
	};

	const int xCenter = 160; // center of 320px menu field
	const int yMenuHeader = 3;
	const int yEpisodeHeader = 30;
	const int xLeftControl = 83;
	const int xRightControl = 213;
	const int wControl = 24;
	const int yControls = vga_height - 21;

	char buffer[64];
	int boardOnePlayer, boardTwoPlayer;

	for (; ; )
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);
			fill_rectangle_wh(VGAScreen2, 0, vga_height - 8, vga_width, 8, 0);

			// Draw header.
			draw_font_hv_shadow(VGAScreen2, xCenter, yMenuHeader, miscText[50], large_font, centered, 15, -3, false, 2);
		}

		// Restore background and header.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		const bool onEndlessPage = episodeIndex == endlessPage;
		if (onEndlessPage)
		{
			SDL_strlcpy(buffer, "Endless", sizeof(buffer));

			// No score board: the Endless page lists the per-mode zone records instead.
			boardOnePlayer = -1;
			boardTwoPlayer = -1;
		}
		else if (episodeIndex == coopPage)
		{
			SDL_strlcpy(buffer, "2 Player Campaign", sizeof(buffer));

			// Its own board, drawn below: one best run per episode rather than a top three.
			boardOnePlayer = -1;
			boardTwoPlayer = -1;
		}
		else if (episodeIndex < 5)
		{
			snprintf(buffer, sizeof(buffer), "%s", episode_name[episodeIndex + 1]);

			// Regular episode boards
			boardOnePlayer = 10 + (episodeIndex * 2);
			boardTwoPlayer = 11 + (episodeIndex * 2);
		}
		else
		{
			snprintf(buffer, sizeof(buffer), "%s %s", timed_battle_name[0], timed_battle_name[episodeIndex - 4]);

			// Timed Battle boards
			boardOnePlayer = episodeIndex - 5;
			boardTwoPlayer = -1;
		}

		// Draw episode header.
		draw_font_hv_shadow(VGAScreen, xCenter, yEpisodeHeader, buffer, normal_font, centered, 15, -3, false, 2);

		if (onEndlessPage)
			JE_drawEndlessRecordPage(&endlessPageState);
		else if (episodeIndex == coopPage)
			JE_drawCoopCampaignPage();

		// Draw 1-player scores.
		if (boardOnePlayer >= 0)
			draw_font_hv_shadow(VGAScreen, xCenter, 55, miscText[46], normal_font, centered, 15, -3, false, 2);

		for (Uint8 i = 0; boardOnePlayer >= 0 && i < 3; ++i)
		{
			const int y = 75 + 10 * i;

			if (t2kHighScores[boardOnePlayer][i].difficulty > 9)
				t2kHighScores[boardOnePlayer][i].difficulty = 0;

			const int rank = t2kHighScores[boardOnePlayer][i].difficulty;
			const Sint64 score = t2kHighScores[boardOnePlayer][i].score;
			const char *playerName = t2kHighScores[boardOnePlayer][i].playerName;

			snprintf(buffer, sizeof buffer, "~#%d:~  %lld", i + 1, (long long)score);
			JE_textShade(VGAScreen, 20, y, buffer, 15, 0, FULL_SHADE);
			JE_textShade(VGAScreen, 110, y, playerName, 15, 2, FULL_SHADE);
			JE_textShade(VGAScreen, 250, y, difficultyNameB[rank], 15, rank + (rank == 0 ? 0 : -1), FULL_SHADE);
		}

		// Draw 2-player scores.
		if (boardTwoPlayer >= 0)
		{
			draw_font_hv_shadow(VGAScreen, xCenter, 120, miscText[47], normal_font, centered, 15, -3, false, 2);

			for (Uint8 i = 0; i < 3; ++i)
			{
				const int y = 135 + 10 * i;

				if (t2kHighScores[boardTwoPlayer][i].difficulty > 9)
					t2kHighScores[boardTwoPlayer][i].difficulty = 0;

				const int rank = t2kHighScores[boardTwoPlayer][i].difficulty;
				const Sint64 score = t2kHighScores[boardTwoPlayer][i].score;
				const char *teamName = t2kHighScores[boardTwoPlayer][i].playerName;

				snprintf(buffer, sizeof buffer, "~#%d:~  %lld", i + 1, (long long)score);
				JE_textShade(VGAScreen, 20, y, buffer, 15, 0, FULL_SHADE);
				JE_textShade(VGAScreen, 110, y, teamName, 15, 2, FULL_SHADE);
				JE_textShade(VGAScreen, 250, y, difficultyNameB[rank], 15, rank + (rank == 0 ? 0 : -1), FULL_SHADE);
			}			
		}

		// Draw paging controls. A breakdown or a pending erase owns the page, so paging away is
		// not offered until the player has backed out of it.
		const bool endlessBusy = onEndlessPage
		                      && (endlessPageState.level > ENDLESS_PAGE_MODES
		                          || endlessPageState.confirmErase);
		const bool leftControlVisible = episodeIndex > 0 && !endlessBusy;
		const bool rightControlVisible = episodeIndex < episodeCount - 1 && !endlessBusy;

		if (leftControlVisible)
			blit_sprite2x2(VGAScreen, xLeftControl, yControls, shopSpriteSheet, 279);

		if (rightControlVisible)
			blit_sprite2x2(VGAScreen, xRightControl, yControls, shopSpriteSheet, 281);

		// The paging hint is wrong while the page owns the input: left and right do nothing then.
		if (!endlessBusy)
		{
			helpBoxColor = 15;
			JE_helpBox(VGAScreen, 103, vga_height - 18, miscText[56], 25);
		}

		if (restart)
		{
			mouseCursor = MOUSE_POINTER_NORMAL;

			fade_palette(colors, 10, 0, 255);

			restart = false;
		}

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
		} while (!(newkey || newmouse));

		// Handle interaction.

		if (onEndlessPage && JE_endlessRecordPageInput(&endlessPageState))
			continue;

		bool leftAction = false;
		bool rightAction = false;
		bool done = false;

		if (newmouse)
		{
			switch (lastmouse_but)
			{
			case SDL_BUTTON_LEFT:
			{
				if (leftControlVisible &&
				    mouse_y >= yControls &&
				    mouse_x >= xLeftControl &&
				    mouse_x < xLeftControl + wControl)
				{
					JE_playSampleNum(S_CURSOR);

					leftAction = true;
				}
				else if (rightControlVisible &&
				         mouse_y >= yControls &&
				         mouse_x >= xRightControl &&
				         mouse_x < xRightControl + wControl)
				{
					JE_playSampleNum(S_CURSOR);

					rightAction = true;
				}
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

				leftAction = true;
				break;
			}
			case SDL_SCANCODE_RIGHT:
			{
				JE_playSampleNum(S_CURSOR);

				rightAction = true;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
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

		if (leftAction)
		{
			episodeIndex = episodeIndex == 0
				? episodeCount - 1
				: episodeIndex - 1;
		}
		else if (rightAction)
		{
			episodeIndex = episodeIndex == episodeCount - 1
				? 0
				: episodeIndex + 1;
		}

		if (done)
		{
			fade_black(15);

			return;
		}
	}
}

void JE_gammaCorrect_func(JE_byte *col, JE_real r)
{
	int temp = roundf(*col * r);
	if (temp > 255)
	{
		temp = 255;
	}
	*col = temp;
}

void JE_gammaCorrect(Palette *colorBuffer, JE_byte gamma)
{
	int x;
	JE_real r = 1 + (JE_real)gamma / 10;

	for (x = 0; x < 256; x++)
	{
		JE_gammaCorrect_func(&(*colorBuffer)[x].r, r);
		JE_gammaCorrect_func(&(*colorBuffer)[x].g, r);
		JE_gammaCorrect_func(&(*colorBuffer)[x].b, r);
	}
}

JE_boolean JE_gammaCheck(void)
{
	bool temp = keysactive[SDL_SCANCODE_F11] != 0;
	if (temp)
	{
		keysactive[SDL_SCANCODE_F11] = false;
		newkey = false;
		gammaCorrection = (gammaCorrection + 1) % 4;
		memcpy(colors, palettes[pcxpal[3-1]], sizeof(colors));
		JE_gammaCorrect(&colors, gammaCorrection);
		set_palette(colors, 0, 255);
	}
	return temp;
}

void JE_drawNetworkNotice(const char *text)
{
	// Drawn into VGAScreenSeg, so this is display space: the playfield is x in [0,
	// PLAYFIELD_WIDTH) and y in [0, 184), with the HUD to the right of it.
	const int playfieldRows = 184;
	const int panelW = MIN(JE_textWidth(text, normal_font) + 28, PLAYFIELD_WIDTH - 16);
	const int panelH = 26;

	const int px0 = (PLAYFIELD_WIDTH - panelW) / 2, px1 = px0 + panelW;
	const int py0 = (playfieldRows - panelH) / 2, py1 = py0 + panelH;

	JE_barShade(VGAScreen, px0, py0, px1, py1);
	JE_barShade(VGAScreen, px0 + 2, py0 + 2, px1 - 2, py1 - 2);
	JE_dStringOutlined(VGAScreen, (px0 + px1) / 2 - JE_textWidth(text, normal_font) / 2, py0 + 7,
	                   text, SMALL_FONT_SHAPES);
}

void JE_doInGameSetup(void)
{
	// A modal UI mid-tick (which may even change sim-affecting settings) makes
	// the tick non-replayable; skip self-test verification of this tick.
	rollback_taint("ingame-setup");

	// These menus present their own frames inside the gameplay tick's recording
	// window (rl_begin_record..rl_end_record in JE_main). Left on, their per-frame
	// draws flood the command buffer (runaway memory, multi-second replay), so
	// suspend recording for the duration.
	const bool rl_was_recording = render_list_recording;
	render_list_recording = false;

	mouseSetRelative(false);

	haltGame = false;

#ifdef WITH_NETWORK
	// A rollback session reaches this menu on a frame both machines confirmed and needs no
	// handshake here; its release also carries the menu frame's input records and, unlike
	// PACKET_WAITING, means nothing to the level-end paths. See doc/notes.md#rollback.
	const bool rollback = nrb_active();
	const Uint16 release = rollback ? PACKET_GAME_MENU : PACKET_WAITING;
	if (isNetworkGame && !rollback)
	{
		network_prepare(PACKET_GAME_MENU);
		network_send(4);  // PACKET_GAME_MENU

		// Present the frozen frame with the cursor composited while we wait for
		// the peer's menu packet, like the rendezvous loops below.
		SDL_Surface *const save_surface = VGAScreen;
		VGAScreen = VGAScreenSeg;

		while (true)
		{
			service_SDL_events(false);

			mouseCursor = MOUSE_POINTER_NORMAL;
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (!output_vsync)
				limit_render_fps();

			if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_GAME_MENU)
			{
				network_update();
				break;
			}

			network_update();
			network_check();
		}

		VGAScreen = save_surface;
	}
#endif

	if (yourInGameMenuRequest)
	{
		// A gameplay wire test has no player at the menu: it closes immediately, no quit.
		if (qa_net_gameplay_ticks == 0 && JE_inGameSetup())
		{
			reallyEndLevel = true;
			playerEndLevel = true;
		}
		quitRequested = false;

		keysactive[SDL_SCANCODE_ESCAPE] = false;

#ifdef WITH_NETWORK
		if (isNetworkGame)
		{
			if (!playerEndLevel)
			{
				network_prepare(release);
				network_send(rollback ? 4 + nrb_menu_release_fill(&packet_out_temp->data[4]) : 4);
			}
			else
			{
				network_prepare(PACKET_GAME_QUIT);
				network_send(4);  // PACKET_GAMEQUIT
			}
		}
#endif
	}

#ifdef WITH_NETWORK
	if (isNetworkGame)
	{
		SDL_Surface *temp_surface = VGAScreen;
		VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

		if (!yourInGameMenuRequest)
		{
			JE_drawNetworkNotice("Other player in options menu.");
			JE_showVGA();

			while (true)
			{
				service_SDL_events(false);
				// Keep the mouse cursor alive while we wait on the other player.
				mouseCursor = MOUSE_POINTER_NORMAL;
				JE_mouseStart();
				JE_showVGA();
				JE_mouseReplace();
				if (!output_vsync)
					limit_render_fps();

				// The other player may be in the debug menu: adopt whatever it rewrote before the
				// release that frees us.  Reliable and ordered, so it always arrives first.
				if (network_debug_sync_pump(true))
					continue;

				if (packet_in[0])
				{
					if (SDLNet_Read16(&packet_in[0]->data[0]) == release)
					{
						// Consume the release, or it sits at the head of the reliable queue
						// for the rest of the level and the next rendezvous reads it as its
						// own, released one packet early from then on.
						network_update();
						network_check();
						break;
					}
					else if (SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_GAME_QUIT)
					{
						reallyEndLevel = true;
						playerEndLevel = true;
						if (coopEndlessMode)
							endlessCoopPeerQuitLevel();

						// Left queued on purpose: the level-end paths are the ones that read
						// a peer's quit (see nrb_peer_left_level).
						network_check();
						break;
					}
				}

				network_update();
				network_check();
			}
		}

		while (!network_is_sync())
		{
			service_SDL_events(false);
			mouseCursor = MOUSE_POINTER_NORMAL;
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (!output_vsync)
				limit_render_fps();

			// Both players can reach the menu on the same frame, in which case neither ran the
			// wait loop above and this is where a debug block from the other one turns up.
			if (network_debug_sync_pump(true))
				continue;

			network_check();
		}

		VGAScreen = temp_surface; /* side-effect of game_screen */
	}
#endif

	yourInGameMenuRequest = false;

	mouseSetRelative(true);

	render_list_recording = rl_was_recording;
}

// Pause-menu access to invincibility, cheats, and debug shortcuts.
// A true result tells the caller to close the pause menu and resume play.
// Slot 0 restores the standard loadout; slots 1 through 10 select custom ships.
static int extraMenuNextCustomShipSlot(int slot, int dir, bool canReturn)
{
	slot = (slot + (dir > 0 ? 1 : 10)) % 11;
	if (slot == 0 && !canReturn)
		slot = (dir > 0) ? 1 : 10;
	return slot;
}

// Switch loadouts while preserving the live armor and shield ratios.
static void extraMenuCycleCustomShip(uint pnum, int dir)
{
	Player *const p = &player[pnum];
	const JE_byte *const table = extraShipsFor(pnum);

	int slot = isExtraShipId(p->items.ship) ? p->items.ship - 90 : 0;
	extraShipRememberStandard(pnum);
	slot = extraMenuNextCustomShipSlot(slot, dir, extraShipReturn[pnum].valid);

	rollback_taint(pnum == 0 ? "edit-ship-1" : "edit-ship-2");
	if (slot == 0)
	{
		if (extraShipRestoreStandard(pnum))
			extraShipLoadoutRefresh(pnum, true);
		return;
	}

	const int base = (slot - 1) * 15;
	endlessNoteCustomShip();  // an Endless record earns its "C" for a custom hull too

	p->items.ship = (JE_byte)(90 + slot);
	p->items.weapon[FRONT_WEAPON].id = extraShipResolvePort(pnum, table[base + 1]);
	p->items.weapon[REAR_WEAPON].id = extraShipResolvePort(pnum, table[base + 2]);
	// A file from another machine may name a special whose icon the HUD cannot draw.
	p->items.special = debug_special_is_safe(table[base + 3]) ? table[base + 3] : 0;
	p->items.sidekick[LEFT_SIDEKICK] = table[base + 4];
	p->items.sidekick[RIGHT_SIDEKICK] = table[base + 5];
	p->items.generator = table[base + 6];
	p->items.shield = table[base + 8];

	extraShipLoadoutRefresh(pnum, true);
}

static void extraMenuDrawShipPreview(uint pnum, int cx, int y, int banking)
{
	Sprite2_array *const sheet = pnum == 1 ? shipGr2ptr : shipGrPtr;
	const JE_word gr = pnum == 1 ? shipGr2 : shipGr;
	if (sheet == NULL || sheet->data == NULL)
		return;
	const NetShipStyle style = netStyleForSeat(pnum);
	const int pose = banking * 2;

	if (gr == 0)
	{
		blit_sprite2x2_alpha(VGAScreen, cx - 2 * SHOP_WIDE_HULL_HALF, y, *sheet, 13 + pose,
		                     style.bank, style.opacity);
		blit_sprite2x2_alpha(VGAScreen, cx, y, *sheet, 51 + pose, style.bank, style.opacity);
	}
	else if (gr == 1)
	{
		// Nort Ship keeps its center hull and adds an edge sprite for banked poses.
		blit_sprite2x2_alpha(VGAScreen, cx - 2 * SHOP_WIDE_HULL_HALF, y, *sheet, 220,
		                     style.bank, style.opacity);
		blit_sprite2x2_alpha(VGAScreen, cx, y, *sheet, 222, style.bank, style.opacity);
		unsigned int trim = 0;
		int trimX = 0;
		switch (banking)
		{
		case -2: trim = 59; trimX = cx + SHOP_WIDE_HULL_HALF; break;
		case -1: trim = 58; trimX = cx + SHOP_WIDE_HULL_HALF; break;
		case  1: trim = 39; trimX = cx - 2 * SHOP_WIDE_HULL_HALF; break;
		case  2: trim = 40; trimX = cx - 2 * SHOP_WIDE_HULL_HALF; break;
		default: break;
		}
		if (trim != 0)
			blit_sprite2_alpha(VGAScreen, trimX, y + 14, *sheet, trim,
			                   style.bank, style.opacity);
	}
	else
		blit_sprite2x2_alpha(VGAScreen, cx - SHOP_WIDE_HULL_HALF, y, *sheet, gr + pose,
		                     style.bank, style.opacity);
}

void qa_test_extra_ship_return(void)
{
	Player savedPlayers[COUNTOF(player)];
	memcpy(savedPlayers, player, sizeof(savedPlayers));
	ExtraShipReturn savedReturn[COUNTOF(extraShipReturn)];
	memcpy(savedReturn, extraShipReturn, sizeof(savedReturn));

	extraShipReturnReset();
	PlayerItems standard = player[0].items;
	standard.ship = 7;
	standard.generator = 3;
	standard.shield = 6;
	standard.weapon[FRONT_WEAPON].id = 14;
	standard.weapon[REAR_WEAPON].id = 15;
	standard.sidekick[LEFT_SIDEKICK] = 16;
	standard.sidekick[RIGHT_SIDEKICK] = 17;
	standard.special = 8;
	player[0].items = standard;
	extraShipRememberStandard(0);

	player[0].items.ship = 91;
	player[0].items.generator = 9;
	player[0].items.shield = 10;
	player[0].items.weapon[FRONT_WEAPON].id = 11;
	player[0].items.weapon[REAR_WEAPON].id = 12;
	player[0].items.weapon[FRONT_WEAPON].power = 9;
	player[0].items.sidekick[LEFT_SIDEKICK] = 13;
	player[0].items.sidekick[RIGHT_SIDEKICK] = 14;
	player[0].items.special = 15;
	extraShipRememberStandard(0);  // an extra ship must not replace the return point
	const bool restored = extraShipRestoreStandard(0);
	PlayerItems expected = standard;
	expected.weapon[FRONT_WEAPON].power = 9;
	qa_check(restored && memcmp(&player[0].items, &expected, sizeof(expected)) == 0,
	         "the extra-ship return point restores every item the custom ship replaced");
	qa_check(player[0].items.weapon[FRONT_WEAPON].power == 9,
	         "returning to the standard loadout keeps upgrades earned after the switch");

	PlayerItems standard2 = player[1].items;
	standard2.ship = 8;
	standard2.generator = 4;
	standard2.shield = 7;
	player[1].items = standard2;
	extraShipRememberStandard(1);
	player[0].items.ship = 91;
	player[1].items.ship = 92;
	player[1].items.generator = 10;
	player[1].items.weapon[FRONT_WEAPON].power = 8;
	const PlayerItems seat0Custom = player[0].items;
	PlayerItems expected2 = standard2;
	expected2.weapon[FRONT_WEAPON].power = 8;
	qa_check(extraShipRestoreStandard(1)
	         && memcmp(&player[1].items, &expected2, sizeof(expected2)) == 0
	         && memcmp(&player[0].items, &seat0Custom, sizeof(seat0Custom)) == 0,
	         "each online seat restores its own standard loadout without changing the other seat");

	qa_check(player_carry_gauge(80, 80, 200) == 200
	         && player_carry_gauge(40, 80, 200) == 100
	         && player_carry_gauge(300, 200, 80) == 80
	         && player_carry_gauge(0, 0, 250) == 250,
	         "custom-ship gauges preserve full, damaged, clamped, and uninitialized states");
	qa_check(extraMenuNextCustomShipSlot(0, +1, true) == 1
	         && extraMenuNextCustomShipSlot(0, -1, true) == 10
	         && extraMenuNextCustomShipSlot(10, +1, true) == 0
	         && extraMenuNextCustomShipSlot(1, -1, true) == 0,
	         "the custom-ship cycler puts the standard loadout before slots 1 through 10");
	qa_check(extraMenuNextCustomShipSlot(10, +1, false) == 1
	         && extraMenuNextCustomShipSlot(1, -1, false) == 10,
	         "a custom loadout with no known predecessor skips the unavailable return point");
	qa_check(extraShipPreviewBank(0) == 0
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS) == -1
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS * 2) == -2
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS * 4) == 0
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS * 6) == 2
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS * 8) == 0,
		         "the custom-ship preview sweeps from center to both sides and back");
	struct ExtraShipModeCase
	{
		bool network, twoPlayer, coop, separate, timedBattle, superTyrian, allowed;
		int superArcade;
		const char *name;
	};
	static const struct ExtraShipModeCase modeCases[] = {
		{ false, false, false, false, false, false, true,  SA_NONE,      "1 Player Campaign" },
		{ false, false, false, false, false, false, true,  SA_NONE,      "1 Player Endless" },
		{ false, false, false, false, false, false, true,  SA_NONE,      "1 Player Arcade" },
		{ true,  true,  true,  false, false, false, true,  SA_NONE,      "Online Campaign" },
		{ true,  true,  true,  false, false, false, true,  SA_NONE,      "Online Endless" },
		{ true,  true,  false, true,  false, false, true,  SA_NONE,      "Online Separate Arcade" },
		{ false, false, false, false, true,  false, false, SA_NONE,      "1 Player Timed Battle" },
		{ false, true,  false, false, false, false, false, SA_NONE,      "2 Player Arcade" },
		{ false, false, false, false, false, false, false, SA_NORTSHIPZ, "1 Player Super Arcade" },
		{ false, false, false, false, false, true,  false, SA_NONE,      "SuperTyrian" },
		{ true,  true,  false, false, false, false, false, SA_NONE,      "Online Linked Arcade" },
		{ true,  true,  false, true,  true,  false, false, SA_NONE,      "Online Timed Battle" },
		{ true,  true,  false, true,  false, true,  false, SA_NONE,      "Online SuperTyrian" },
		{ true,  true,  false, true,  false, false, false, SA_NORTSHIPZ, "Online Super Arcade" },
	};
	for (uint i = 0; i < COUNTOF(modeCases); ++i)
	{
		const struct ExtraShipModeCase *const c = &modeCases[i];
		char label[128];
		snprintf(label, sizeof(label), "custom-ship availability matches %s", c->name);
		qa_check(extraShipsAllowedForMode(c->network, c->twoPlayer, c->coop, c->separate,
		                                      c->timedBattle, c->superTyrian, c->superArcade)
		         == c->allowed, label);
	}

	memcpy(player, savedPlayers, sizeof(savedPlayers));
	memcpy(extraShipReturn, savedReturn, sizeof(extraShipReturn));
}

bool JE_extraMenu(void)
{
	enum { PAGE_ROOT, PAGE_CHEATS, PAGE_DEBUG };

	// The cheat combos (and invincibility) are only valid in a normal solo game,
	// matching the guards on the key handlers in JE_mainKeyboardInput.
	const bool cheatsAllowed = !isNetworkGame && !twoPlayerMode && !superTyrian && superArcadeMode == SA_NONE;

	const uint shipSeat = (isNetworkGame && thisPlayerNum >= 1) ? (uint)(thisPlayerNum - 1) : 0;
	const bool shipRowAllowed = extraShipsAllowedInGame() && extraAvailFor(shipSeat);

	// Footer help is drawn as two short lines (description + key combo) so long
	// combos never overflow the panel.
	static const char *const rootLabels[] = { "Invincibility", "Custom Ship", "Cheat Codes...", "Debug Codes...", "Return" };
	static const char *const rootDesc[] = {
		"Don't die when armor runs out.",
		"Cycle editor ships or restore your loadout.",
		"Trigger the F-key cheat combos.",
		"The Backspace debug-key combos.",
		"Return to the pause menu.",
	};
	static const char *const rootCombo[] = { "", "Tab+Number in flight (offline)", "", "", "" };

	static const char *const cheatLabels[] = { "Nort Ship", "Self-Destruct", "Skip Level", "Return" };
	static const char *const cheatDesc[] = {
		"Switch to the Nort Ship.",
		"Zero your ship's armor.",
		"Jump to the next level.",
		"Back to Extra.",
	};
	static const char *const cheatCombo[] = { "F2+F4+F6+F7+F9+\\+/", "F2+F3+F4", "F2+F6+F7", "" };

	static const char *const debugLabels[] = { "Debug Overlay", "Hyper-Speed", "Level Filter", "Random Music", "Screenshot Pause", "Return" };
	static const char *const debugDesc[] = {
		"Show the debug info overlay.",
		"Fast-forward gameplay.",
		"Tint the level colors.",
		"Play a random music track.",
		"Freeze; hides the pause text.",
		"Back to Extra.",
	};
	static const char *const debugCombo[] = { "F10+Backspace", "Backspace+1", "Backspace+minus", "Backspace+ScrollLock", "Backspace+NumLock", "" };

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	const bool wasRelative = mouseGetRelative();
	mouseSetRelative(false);

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // mouse pointer sprites

	// The panel is opaque and covers only a sub-region; the pause menu's own loop
	// repaints the full background from VGAScreen2 when we return, so there's no
	// need to save/restore the screen here.

	wait_noinput(false, false, true);
	newkey = newmouse = false;  // don't let the click/key that opened us leak in

#ifdef WITH_NETWORK
	if (isNetworkGame)
		network_debug_sync_mark();
#endif

	// Panel geometry: reuse the debug menu's horizontal span, compact and centered.
	const int px0 = DEBUG_MENU_X;
	const int px1 = DEBUG_MENU_X + DEBUG_MENU_WIDTH - 1;
	const int title_h = 15;
	const int row_h = 16;
	const int footer_h = 26;  // two footer lines (description + key combo)
	const int maxRows = 6;
	const int panel_h = title_h + 4 + maxRows * row_h + footer_h;
	int py0 = DEBUG_MENU_Y + (DEBUG_MENU_HEIGHT - panel_h) / 2;
	if (py0 < DEBUG_MENU_Y)
		py0 = DEBUG_MENU_Y;
	const int py1 = py0 + panel_h;
	const int items_top = py0 + title_h + 4;
	const int mid_x = (px0 + px1) / 2;
	const int preview_x0 = px1 - 75;
	const int preview_x1 = px1 - 10;
	const int preview_y0 = items_top + 2 * row_h + 1;
	const int preview_y1 = py1 - footer_h - 5;

	enum {
		C_PANEL_BG = 0xF1, C_TITLE_BG = 0xF3, C_DIVIDER = 0xF6,
		C_EDGE_HI = 0xFB, C_EDGE_LO = 0xF4, C_SEL_BAR = 0xF5
	};

	int page = PAGE_ROOT;
	size_t selected = 0;
	int prev_mx = mouse_x, prev_my = mouse_y;
	Uint32 shipPreviewStart = SDL_GetTicks();
	bool shipPreviewWasShown = false;

	bool closeMenu = false;
	bool returnToGame = false;

	while (!closeMenu)
	{
		// Rows for the current page.
		const char *const *labels;
		const char *const *descs;
		const char *const *combos;
		int count;
		const char *title;
		switch (page)
		{
		case PAGE_CHEATS: labels = cheatLabels; descs = cheatDesc; combos = cheatCombo; count = 4; title = "CHEAT  CODES"; break;
		case PAGE_DEBUG:  labels = debugLabels; descs = debugDesc; combos = debugCombo; count = 6; title = "DEBUG  CODES"; break;
		default:          labels = rootLabels;  descs = rootDesc;  combos = rootCombo;  count = 5; title = "EXTRA"; break;
		}

		if ((int)selected >= count)
			selected = count - 1;

		// Opaque panel, fully repainted every frame.
		fill_rectangle_xy(VGAScreen, px0, py0, px1, py1, C_PANEL_BG);
		fill_rectangle_xy(VGAScreen, px0, py0, px1, py0, C_EDGE_HI);
		fill_rectangle_xy(VGAScreen, px0, py0, px0, py1, C_EDGE_HI);
		fill_rectangle_xy(VGAScreen, px0, py1, px1, py1, C_EDGE_LO);
		fill_rectangle_xy(VGAScreen, px1, py0, px1, py1, C_EDGE_LO);
		fill_rectangle_xy(VGAScreen, px0 + 1, py0 + 1, px1 - 1, py0 + title_h - 1, C_TITLE_BG);
		fill_rectangle_xy(VGAScreen, px0 + 1, py0 + title_h, px1 - 1, py0 + title_h, C_DIVIDER);
		draw_font_hv_shadow(VGAScreen, mid_x, py0 + 3, title, normal_font, centered, 15, 4, true, 1);

		for (int i = 0; i < count; ++i)
		{
			const int ry = items_top + i * row_h;
			const bool sel = ((int)selected == i);

			// Grey out cheat rows (and the paths to them) when cheats aren't valid.
			bool enabled = true;
			if (page == PAGE_ROOT && (i == 0 || i == 2))
				enabled = cheatsAllowed;
			else if (page == PAGE_ROOT && i == 1)
				enabled = shipRowAllowed;
			else if (page == PAGE_CHEATS && i < 3)
				enabled = cheatsAllowed;

			if (sel)
				fill_rectangle_xy(VGAScreen, px0 + 3, ry - 1, px1 - 3, ry + row_h - 3, C_SEL_BAR);

			const int bright = enabled ? (sel ? 5 : -1) : -6;
			draw_font_hv_shadow(VGAScreen, px0 + 14, ry, labels[i], small_font, left_aligned, 15, bright, true, 1);

			// Value column (toggles / cyclers).
			char valbuf[16];
			const char *value = NULL;
			if (page == PAGE_ROOT && i == 0)
				value = youAreCheating ? "On" : "Off";
			else if (page == PAGE_ROOT && i == 1)
			{
				if (player[shipSeat].items.ship > 90 && player[shipSeat].items.ship <= 100)
				{
					snprintf(valbuf, sizeof(valbuf), "Ship %d", player[shipSeat].items.ship - 90);
					value = valbuf;
				}
				else
					value = "Standard";
			}
			else if (page == PAGE_DEBUG && i == 0)
				value = debug ? "On" : "Off";
			else if (page == PAGE_DEBUG && i == 1)
				value = fastPlay == 0 ? "Off" : (fastPlay == 1 ? "On" : "Max");
			else if (page == PAGE_DEBUG && i == 2)
			{
				if (levelFilter == -99)
					value = "Off";
				else
				{
					snprintf(valbuf, sizeof(valbuf), "%d", levelFilter);
					value = valbuf;
				}
			}
			else if (page == PAGE_DEBUG && i == 4)
				value = superPause ? "On" : "Off";

			if (value != NULL)
				draw_font_hv_shadow(VGAScreen, px1 - 12, ry, value, small_font, right_aligned, 15, bright, true, 1);

			if (sel)
				draw_font_hv(VGAScreen, px0 + 5, ry, ">", small_font, left_aligned, 15, 6);
		}

		const bool showShipPreview = page == PAGE_ROOT && selected == 1 && shipRowAllowed;
		if (showShipPreview && !shipPreviewWasShown)
			shipPreviewStart = SDL_GetTicks();
		shipPreviewWasShown = showShipPreview;
		if (showShipPreview)
		{
			fill_rectangle_xy(VGAScreen, preview_x0, preview_y0, preview_x1, preview_y1, 0);
			JE_rectangle(VGAScreen, preview_x0, preview_y0, preview_x1, preview_y1, C_EDGE_HI);
			// A composed hull is 28 pixels tall; center it in the preview box.
			extraMenuDrawShipPreview(shipSeat, (preview_x0 + preview_x1) / 2,
			                         (preview_y0 + preview_y1 - 27) / 2,
			                         extraShipPreviewBank(SDL_GetTicks() - shipPreviewStart));
		}

		// Footer (two lines so long key combos don't overflow the panel): the
		// selected row's description, then its key combo.
		fill_rectangle_xy(VGAScreen, px0 + 1, py1 - footer_h, px1 - 1, py1 - footer_h, C_DIVIDER);
		draw_font_hv(VGAScreen, px0 + 6, py1 - footer_h + 4, descs[selected], small_font, left_aligned, 15, -3);
		if (combos[selected][0] != '\0')
		{
			char keyline[40];
			snprintf(keyline, sizeof(keyline), "Keys: %s", combos[selected]);
			draw_font_hv(VGAScreen, px0 + 6, py1 - footer_h + 15, keyline, small_font, left_aligned, 15, -3);
		}

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		NETWORK_KEEP_ALIVE();  // nothing else pumps packets while this panel is up

		// Mouse: hover to select, wheel to move, left-click to activate, right-click back.
		bool activate = false, goBack = false;
		int adjustDir = 0;  // -1/+1 from Left/Right on a toggle/cycler row

		if (mouse_scroll != 0)
		{
			int ns = (int)selected - mouse_scroll;
			ns = MAX(0, MIN(count - 1, ns));
			selected = (size_t)ns;
			mouse_scroll = 0;
		}
		const bool overShipPreview = showShipPreview &&
		                                 mouse_x >= preview_x0 && mouse_x <= preview_x1 &&
		                                 mouse_y >= preview_y0 && mouse_y <= preview_y1;
		if (mouse_x != prev_mx || mouse_y != prev_my)
		{
			prev_mx = mouse_x;
			prev_my = mouse_y;
			if (!overShipPreview && mouse_x >= px0 && mouse_x <= px1 && mouse_y >= items_top)
			{
				const int vis = (mouse_y - items_top) / row_h;
				if (vis >= 0 && vis < count)
					selected = (size_t)vis;
			}
		}
		if (newmouse)
		{
			const bool clickedShipPreview = showShipPreview &&
			                                lastmouse_x >= preview_x0 && lastmouse_x <= preview_x1 &&
			                                lastmouse_y >= preview_y0 && lastmouse_y <= preview_y1;
			if (lastmouse_but == SDL_BUTTON_LEFT && clickedShipPreview)
			{
				activate = true;
			}
			else if (lastmouse_but == SDL_BUTTON_LEFT && lastmouse_x >= px0 && lastmouse_x <= px1 && lastmouse_y >= items_top)
			{
				const int vis = (lastmouse_y - items_top) / row_h;
				if (vis >= 0 && vis < count)
				{
					selected = (size_t)vis;
					activate = true;
				}
			}
			else if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				goBack = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				selected = selected == 0 ? (size_t)(count - 1) : selected - 1;
				break;
			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				selected = (int)selected == count - 1 ? 0 : selected + 1;
				break;
			case SDL_SCANCODE_LEFT:
				adjustDir = -1;
				break;
			case SDL_SCANCODE_RIGHT:
				adjustDir = +1;
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				activate = true;
				break;
			case SDL_SCANCODE_ESCAPE:
				goBack = true;
				break;
			default:
				break;
			}
		}

		if (goBack)
		{
			JE_playSampleNum(S_SPRING);
			if (page == PAGE_ROOT)
				closeMenu = true;
			else
			{
				selected = (page == PAGE_CHEATS) ? 2 : 3;  // land back on the row that opened it
				page = PAGE_ROOT;
			}
			continue;
		}

		// Left/Right adjusts the toggles and cyclers in place.
		if (adjustDir != 0)
		{
			if (page == PAGE_ROOT && selected == 0 && cheatsAllowed)
			{
				youAreCheating = !youAreCheating;
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_ROOT && selected == 1 && shipRowAllowed)
			{
				extraMenuCycleCustomShip(shipSeat, adjustDir);
				shipPreviewStart = SDL_GetTicks();
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_DEBUG && selected == 0)
			{
				debug = !debug;
				debugHist = 1; debugHistCount = 1; lastDebugTime = SDL_GetTicks();
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_DEBUG && selected == 1)
			{
				fastPlay = (fastPlay + (adjustDir > 0 ? 1 : 2)) % 3;
				JE_setNewGameSpeed();
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_DEBUG && selected == 2)
			{
				if (adjustDir > 0)
				{
					if (levelFilter == -99) levelFilter = 0;
					else { levelFilter++; if (levelFilter == 16) levelFilter = -99; }
				}
				else
				{
					if (levelFilter == -99) levelFilter = 15;
					else if (levelFilter == 0) levelFilter = -99;
					else levelFilter--;
				}
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_DEBUG && selected == 4)
			{
				superPause = !superPause;
				JE_playSampleNum(S_CURSOR);
			}
			continue;
		}

		if (!activate)
			continue;

		// Activate the selected row.
		if (page == PAGE_ROOT)
		{
			switch (selected)
			{
			case 0:  // Invincibility
				if (cheatsAllowed) { youAreCheating = !youAreCheating; JE_playSampleNum(S_SELECT); }
				else JE_playSampleNum(S_SPRING);
				break;
			case 1:  // Custom Ship: a tap cycles forward, so it works without arrow keys
				if (shipRowAllowed)
				{
					extraMenuCycleCustomShip(shipSeat, +1);
					shipPreviewStart = SDL_GetTicks();
					JE_playSampleNum(S_SELECT);
				}
				else JE_playSampleNum(S_SPRING);
				break;
			case 2:  // Cheat Codes...
				if (cheatsAllowed) { page = PAGE_CHEATS; selected = 0; JE_playSampleNum(S_SELECT); }
				else JE_playSampleNum(S_SPRING);
				break;
			case 3:  // Debug Codes...
				page = PAGE_DEBUG; selected = 0; JE_playSampleNum(S_SELECT);
				break;
			default:  // Return
				JE_playSampleNum(S_SELECT);
				closeMenu = true;
				break;
			}
		}
		else if (page == PAGE_CHEATS)
		{
			if (selected == 3)  // Return
			{
				JE_playSampleNum(S_SELECT);
				page = PAGE_ROOT; selected = 2;
			}
			else if (!cheatsAllowed)
			{
				JE_playSampleNum(S_SPRING);
			}
			else
			{
				JE_playSampleNum(S_SELECT);
				switch (selected)
				{
				case 0:  // Nort Ship
					player[0].items.ship = 12;
					player[0].items.special = 13;
					player[0].items.weapon[FRONT_WEAPON].id = 36;
					player[0].items.weapon[REAR_WEAPON].id = 37;
					shipGr = 1;
					break;
				case 1:  // Self-Destruct (zero armor)
					youAreCheating = false;
					for (uint i = 0; i < COUNTOF(player); ++i)
						player[i].armor = 0;
					break;
				case 2:  // Skip Level
					levelTimer = true;
					levelTimerCountdown = 0;
					endLevel = true;
					levelEnd = 40;
					break;
				default:
					break;
				}
				returnToGame = true;
				closeMenu = true;
			}
		}
		else  // PAGE_DEBUG
		{
			JE_playSampleNum(S_SELECT);
			switch (selected)
			{
			case 0:  // Debug Overlay
				debug = !debug;
				debugHist = 1; debugHistCount = 1; lastDebugTime = SDL_GetTicks();
				break;
			case 1:  // Hyper-Speed
				fastPlay = (fastPlay + 1) % 3;
				JE_setNewGameSpeed();
				break;
			case 2:  // Level Filter
				if (levelFilter == -99) levelFilter = 0;
				else { levelFilter++; if (levelFilter == 16) levelFilter = -99; }
				break;
			case 3:  // Random Music
				play_song(mt_rand() % MUSIC_NUM);
				break;
			case 4:  // Screenshot Pause
				superPause = !superPause;
				break;
			default:  // Return
				page = PAGE_ROOT; selected = 3;
				break;
			}
		}
	}

#ifdef WITH_NETWORK
	// Publish a ship switch through the pause menu's existing simulation-state sync.
	if (isNetworkGame)
		network_debug_sync_send();
#endif

	// Restore input/mouse state; the caller repaints the pause menu.
	newkey = newmouse = false;
	mouseSetRelative(wasRelative);
	VGAScreen = temp_surface;

	return returnToGame;
}

JE_boolean JE_inGameSetup(void)
{
	bool result = false;

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	enum MenuItemIndex
	{
		MENU_ITEM_MUSIC_VOLUME = 0,
		MENU_ITEM_EFFECTS_VOLUME,
		MENU_ITEM_SHIP_SENS,         // "Sensitivity" slider: touch on consoles, mouse on desktop
		MENU_ITEM_DETAIL_LEVEL,
		MENU_ITEM_GAME_SPEED,
		MENU_ITEM_EXTRA,
		MENU_ITEM_DEBUG,
		MENU_ITEM_RETURN_TO_GAME,
		MENU_ITEM_QUIT,
	};

	// Indexed by id (help for the sensitivity/Extra/Debug/Return/Quit rows is overridden below).
	const size_t helpIndexes[] = { 14, 14, 14, 27, 28, 26, 26, 26, 26 };

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	/* Indexed by MenuItemIndex (id), not by visible row. */
	const char* const menuNames[] =
	{
			inGameText[0],
			inGameText[1],
			SHIP_SENS_NAME,
			inGameText[2],
			inGameText[3],
			"Extra",
			"Debug Menu",
			inGameText[4],
			inGameText[5],
	};

	/* Visible rows: the Debug Menu row only appears when Debug Mode is enabled
	 * in the Enhancements menu, and Game Speed is hidden in netplay (both sides
	 * must tick in lockstep, so changing it would desync). */
	enum MenuItemIndex items[COUNTOF(menuNames)];
	size_t menuItemsCount = 0;
	for (size_t i = 0; i < COUNTOF(menuNames); ++i)
	{
		if (i == MENU_ITEM_DEBUG && !debugMode)
			continue;
		if (i == MENU_ITEM_GAME_SPEED && isNetworkGame)
			continue;
		items[menuItemsCount++] = (enum MenuItemIndex)i;
	}

	size_t selectedIndex = 0;

	const int hMenuItem = 13;

	// JE_barShade bounds are inclusive.
	const int yMenuBoxTop = 13, yMenuBoxBottom = 148;

	// Center the visible rows. Nine rows use the tightest 14 px pitch.
	const int menuRows = (int)menuItemsCount;
	const int yMenuMargin = 5;
	const int hMenuRowSpan = yMenuBoxBottom - yMenuBoxTop - 2 * yMenuMargin;
	const int dyMenuItems = menuRows > 1 ? (hMenuRowSpan - hMenuItem) / (menuRows - 1) : 0;
	const int hMenuBlock = dyMenuItems * (menuRows - 1) + hMenuItem;
	const int yMenuItems = yMenuBoxTop + (yMenuBoxBottom - yMenuBoxTop - hMenuBlock) / 2;

	/* Both boxes are authored flush against the left edge (x=3); centre each of them in the
	 * PLAYFIELD, which composite_playfield lays down at screen x 0, PLAYFIELD_WIDTH (299) wide,
	 * with the HUD owning everything to its right. */
	const int xOfs = (PLAYFIELD_WIDTH - 215) / 2 - 3;      /* main box: x 3..217 */
	const int xHelpOfs = (PLAYFIELD_WIDTH - 255) / 2 - 3;  /* help box: x 3..257 */
	const int xHelpMid = 3 + xHelpOfs + 255 / 2;

	/* Help box rows (JE_barShade bounds are inclusive), and the text row centred in them. The tiny
	 * font draws ink 0..7 rows below the origin with the baseline at row 5, so three rows above the
	 * box middle centres the cap-to-baseline body and leaves descenders in the lower half. */
	const int yHelpBoxTop = 152, yHelpBoxBottom = 168;
	const int yHelpText = (yHelpBoxTop + yHelpBoxBottom) / 2 - 3;

	const int xMenuItem = 10 + xOfs;
	const int xMenuItemName = xMenuItem;
	const int wMenuItemName = 110;
	const int xMenuItemValue = xMenuItemName + wMenuItemName;
	const int wMenuItemValue = 90;
	const int wMenuItem = wMenuItemName + wMenuItemValue;

	for (bool done = false; !done; )
	{
		if (restart)
		{
			JE_barShade(VGAScreen, 3 + xOfs, yMenuBoxTop,     217 + xOfs, yMenuBoxBottom);
			JE_barShade(VGAScreen, 5 + xOfs, yMenuBoxTop + 2, 215 + xOfs, yMenuBoxBottom - 2);

			// Help box
			JE_barShade(VGAScreen, 3 + xHelpOfs, yHelpBoxTop,     257 + xHelpOfs, yHelpBoxBottom);
			JE_barShade(VGAScreen, 5 + xHelpOfs, yHelpBoxTop + 2, 255 + xHelpOfs, yHelpBoxBottom - 2);

			memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

			mouseCursor = MOUSE_POINTER_NORMAL;

			restart = false;
		}

		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw menu items.
		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const int y = yMenuItems + dyMenuItems * i;

			const enum MenuItemIndex itemId = items[i];

			const char* const name = menuNames[itemId];

			const bool selected = i == selectedIndex;

			draw_font_hv_shadow(VGAScreen, xMenuItemName, y, name, normal_font, left_aligned, 15, -4 + (selected ? 2 : 0), false, 2);

			switch (itemId)
			{
			case MENU_ITEM_MUSIC_VOLUME:
			{
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, music_disabled ? 12 : 16, (tyrMusicVolume + 6) / 12, 3, 13);
				break;
			}
			case MENU_ITEM_EFFECTS_VOLUME:
			{
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, samples_disabled ? 12 : 16, (fxVolume + 6) / 12, 3, 13);
				break;
			}
			case MENU_ITEM_SHIP_SENS:
			{
				// Same bar style as the volume sliders; middle == the classic 1:1 feel. The marker
				// slot goes bright once the fill reaches it; compare drawn bar counts (amt vs mark),
				// not the raw value, so it flips exactly on the middle bar.
				const int amt = (ship_sensitivity + 6) / 12;
				const int mark = (SHIP_SENS_DEFAULT + 6) / 12;
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, 16, amt, 3, 13);
				JE_barDrawMark(VGAScreen, xMenuItemValue, y,
				               amt >= mark ? SHIP_SENS_MARK_COL : SHIP_SENS_MARK_COL_DIM, mark, 3, 13);
				break;
			}
			case MENU_ITEM_DETAIL_LEVEL:
			{
				draw_font_hv_shadow(VGAScreen, xMenuItemValue, y, detailLevel[processorType-1], normal_font, left_aligned, 15, -4 + (selected ? 2 : 0), false, 2);
				break;
			}
			case MENU_ITEM_GAME_SPEED:
			{
				draw_font_hv_shadow(VGAScreen, xMenuItemValue, y, gameSpeedText[gameSpeed - 1], normal_font, left_aligned, 15, -4 + (selected ? 2 : 0), false, 2);
				break;
			}
			case MENU_ITEM_DEBUG:
			{
				break;
			}
			default:
				break;
			}
		}

		// Draw help text.
		const enum MenuItemIndex selectedId = items[selectedIndex];
		const char* pause_help = mainMenuHelp[helpIndexes[selectedId]];
		if (selectedId == MENU_ITEM_EXTRA)
			pause_help = "Cheats and bonus options.";
		else if (selectedId == MENU_ITEM_DEBUG)
			pause_help = "Open debug menu.";
		else if (selectedId == MENU_ITEM_RETURN_TO_GAME)
			pause_help = "Return to game.";
		else if (selectedId == MENU_ITEM_QUIT)
			pause_help = endlessMode ? "Give up the level; return to the outpost." : "Quit playing the level.";
		else if (selectedId == MENU_ITEM_SHIP_SENS)
			pause_help = SHIP_SENS_HELP;
		/* Center within the help box. Overwide text clamps to its left edge, preventing
		 * negative-x row wrapping while retaining the prior right-edge overflow. */
		int xHelpText = xHelpMid - JE_textWidth(pause_help, TINY_FONT) / 2;
		if (xHelpText < 5 + xHelpOfs)
			xHelpText = 5 + xHelpOfs;
		JE_outTextAdjust(VGAScreen, xHelpText, yHelpText, pause_help, 14, 6, TINY_FONT, true);

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

			NETWORK_KEEP_ALIVE();

			mouseMoved = mouse_xf != oldMouseX || mouse_yf != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		// Handle interaction.

		bool action = false;
		bool leftAction = false;
		bool rightAction = false;

		if (mouseMoved || newmouse)
		{
			// Find menu item that was hovered or clicked.
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
							// Act on menu item via name.
							if (lastmouse_x >= xMenuItemName && lastmouse_x < xMenuItemName + wMenuItemName)
							{
								action = true;
							}

							// Act on menu item via value.
							else if (lastmouse_x >= xMenuItemValue && lastmouse_x < xMenuItemValue + wMenuItemValue)
							{
								switch (items[i])
								{
								case MENU_ITEM_MUSIC_VOLUME:
								{
									JE_playSampleNum(S_CURSOR);

									const int w = ((255 + 6) / 12) * (3 + 1) - 1;

									int value = (lastmouse_x - xMenuItemValue) * 255 / (w - 1);
									tyrMusicVolume = MIN(MAX(0, value), 255);

									set_volume(tyrMusicVolume, fxVolume);
									break;
								}
								case MENU_ITEM_EFFECTS_VOLUME:
								{
									const int w = ((255 + 6) / 12) * (3 + 1) - 1;

									int value = (lastmouse_x - xMenuItemValue) * 255 / (w - 1);
									fxVolume = MIN(MAX(0, value), 255);

									set_volume(tyrMusicVolume, fxVolume);

									JE_playSampleNum(S_CURSOR);
									break;
								}
								case MENU_ITEM_SHIP_SENS:
								{
									const int w = ((SHIP_SENS_MAX + 6) / 12) * (3 + 1) - 1;

									int value = (lastmouse_x - xMenuItemValue) * SHIP_SENS_MAX / (w - 1);
									ship_sensitivity = MIN(MAX(0, value), SHIP_SENS_MAX);

									JE_playSampleNum(S_CURSOR);
									break;
								}
								case MENU_ITEM_DETAIL_LEVEL:
								case MENU_ITEM_GAME_SPEED:
								{
									rightAction = true;
									break;
								}
								default:
									break;
								}
							}
						}

						break;
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				done = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
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
			case SDL_SCANCODE_LEFT:
			{
				leftAction = true;
				break;
			}
			case SDL_SCANCODE_RIGHT:
			{
				rightAction = true;
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

				done = true;
				break;
			}
			case SDL_SCANCODE_W:
			{
				if (items[selectedIndex] == MENU_ITEM_DETAIL_LEVEL)
				{
					processorType = 6;
					JE_initProcessorType();
				}
				break;
			}
			default:
				break;
			}
		}

		if (action)
		{
			switch (items[selectedIndex])
			{
			case MENU_ITEM_MUSIC_VOLUME:
			{
				JE_playSampleNum(S_SELECT);

				set_music_disabled(!music_disabled);
				break;
			}
			case MENU_ITEM_EFFECTS_VOLUME:
			{
				samples_disabled = !samples_disabled;

				JE_playSampleNum(S_SELECT);
				break;
			}
			case MENU_ITEM_EXTRA:
			{
				JE_playSampleNum(S_SELECT);

				// JE_extraMenu draws its own opaque panel and restores the screen
				// on exit; it returns true if a triggered cheat wants the game back.
				if (JE_extraMenu())
					done = true;
				break;
			}
			case MENU_ITEM_DEBUG:
			{
				JE_playSampleNum(S_SELECT);

				/* capture debug menu area */
				for (int yy = 0; yy < DEBUG_MENU_HEIGHT; ++yy)
				{
					memcpy(&debug_menu_backup[yy * DEBUG_MENU_WIDTH],
						(Uint8*)VGAScreen2->pixels +
						(DEBUG_MENU_Y + yy) * VGAScreen2->pitch + DEBUG_MENU_X,
						DEBUG_MENU_WIDTH);
				}

				JE_debugMenu(false);

				/* restore debug menu area */
				for (int yy = 0; yy < DEBUG_MENU_HEIGHT; ++yy)
				{
					memcpy((Uint8*)VGAScreen->pixels +
						(DEBUG_MENU_Y + yy) * VGAScreen->pitch + DEBUG_MENU_X,
						&debug_menu_backup[yy * DEBUG_MENU_WIDTH],
						DEBUG_MENU_WIDTH);
				}

				restart = true;
				continue; /* redraw menu after exiting debug */
			}
			case MENU_ITEM_RETURN_TO_GAME:
			{
				JE_playSampleNum(S_SELECT);

				done = true;
				break;
			}
			case MENU_ITEM_QUIT:
			{
				JE_playSampleNum(S_SELECT);

				if (constantPlay)
					JE_tyrianHalt(0);

				if (isNetworkGame)
				{
					/*Tell other computer to exit*/
					// Endless quits to the outpost rather than out of the session, and the level
					// warning screen halts the game outright on a set haltGame; playerEndLevel is
					// what sends the peer the quit either way.
					haltGame = !endlessMode;
					playerEndLevel = true;
				}

				// Endless: don't end the run; flag the game loop to revert this level and reopen
				// the outpost LOCKED to the launch-time choices (see tyrian2.c JE_main). The level
				// still ends here (result/reallyEndLevel); the loop decides what happens next.
				if (endlessMode)
					endlessQuitToOutpost = true;

				result = true;
				done = true;
				break;
			}
			default:
				break;
			}
		}
		else if (leftAction || rightAction)
		{
			const int dir = leftAction ? -1 : 1;

			switch (items[selectedIndex])
			{
			case MENU_ITEM_MUSIC_VOLUME:
			{
				JE_playSampleNum(S_CURSOR);

				JE_changeVolume(&tyrMusicVolume, dir * 12, &fxVolume, 0);
				break;
			}
			case MENU_ITEM_EFFECTS_VOLUME:
			{
				JE_changeVolume(&tyrMusicVolume, 0, &fxVolume, dir * 12);

				JE_playSampleNum(S_CURSOR);
				break;
			}
			case MENU_ITEM_SHIP_SENS:
			{
				ship_sensitivity = MIN(MAX(0, ship_sensitivity + dir * 12), SHIP_SENS_MAX);

				JE_playSampleNum(S_CURSOR);
				break;
			}
			case MENU_ITEM_DETAIL_LEVEL:
			{
				JE_playSampleNum(S_CURSOR);

				if (dir > 0)
					processorType = processorType < 4 ? processorType + 1 : 1;
				else
					processorType = processorType > 1 ? processorType - 1 : 4;
				JE_initProcessorType();
				JE_setNewGameSpeed();
				break;
			}
			case MENU_ITEM_GAME_SPEED:
			{
				JE_playSampleNum(S_CURSOR);

				if (dir > 0)
					gameSpeed = gameSpeed < 5 ? gameSpeed + 1 : 1;
				else
					gameSpeed = gameSpeed > 1 ? gameSpeed - 1 : 5;
				JE_initProcessorType();
				JE_setNewGameSpeed();
				break;
			}
			default:
				break;
			}
		}
	}

	VGAScreen = temp_surface; /* side-effect of game_screen */

	return result;
}


/* Relaxed-mode death prompt. Esc and right-click are inert; one of the three rows must be chosen. */
/* The Relaxed death prompt's rows, one per EndlessDeathChoice. At file scope so
 * qa_test_endless_death_menu below can measure every line against the panel's width clamp. */
static const char *const endlessDeathRowName[] = { "Restart Zone", "Return to Outpost", "End Run" };
static const char *const endlessDeathRowHelp[] =
{
	"Fly this zone again as you launched it.",
	"Back to the outpost for a new course.",
	"End the run and see the summary.",
};
static const char endlessDeathTitle[] = "SHIP DESTROYED";

/* Pinned without the input loop: the rows cover the whole choice enum, and every line fits
 * the panel, whose width self-sizes but clamps at the playfield's edge and would clip. */
void qa_test_endless_death_menu(void)
{
	COMPILE_TIME_ASSERT(death_rows_cover_choices,
	                    COUNTOF(endlessDeathRowName) == ENDLESS_DEATH_END_RUN + 1);
	COMPILE_TIME_ASSERT(death_help_covers_choices,
	                    COUNTOF(endlessDeathRowHelp) == ENDLESS_DEATH_END_RUN + 1);

	char label[128];
	const int widthMax = PLAYFIELD_WIDTH - 32 - 24;  // the panel clamp, minus its padding

	qa_check(JE_textWidth(endlessDeathTitle, normal_font) <= widthMax,
	         "the death prompt's title fits its panel");
	for (uint i = 0; i < COUNTOF(endlessDeathRowName); ++i)
	{
		snprintf(label, sizeof(label), "death prompt row '%s' fits its panel",
		         endlessDeathRowName[i]);
		qa_check(JE_textWidth(endlessDeathRowName[i], normal_font) <= widthMax, label);
		snprintf(label, sizeof(label), "death prompt help '%s' fits its panel",
		         endlessDeathRowHelp[i]);
		qa_check(JE_textWidth(endlessDeathRowHelp[i], small_font) <= widthMax, label);
	}
}

EndlessDeathChoice JE_endlessDeathMenu(void)
{
	SDL_Surface *const temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	// Font bodies occupy different parts of bank 15, so their shade offsets use opposite signs.
	const int titleValue = -1, rowValueOn = -2, rowValueOff = -4, helpValue = 4;

	// Size the panel to its widest line.
	int contentW = JE_textWidth(endlessDeathTitle, normal_font);
	for (int i = 0; i < (int)COUNTOF(endlessDeathRowName); ++i)
	{
		contentW = MAX(contentW, JE_textWidth(endlessDeathRowName[i], normal_font));
		contentW = MAX(contentW, JE_textWidth(endlessDeathRowHelp[i], small_font));
	}

	// Center within the playfield, excluding the right-side HUD.
	const int panelW = MIN(contentW + 24, PLAYFIELD_WIDTH - 32);
	const int px0 = (PLAYFIELD_WIDTH - panelW) / 2, px1 = px0 + panelW;

	// Center within the 184-row playfield above the message bar.
	const int playfieldRows = 184, panelH = 104;
	const int py0 = (playfieldRows - panelH) / 2, py1 = py0 + panelH;

	const int midX = (px0 + px1) / 2;
	const int rowY0 = py0 + 34, rowPitch = 18, rowH = 14;
	const int rowX0 = px0 + 6, rowX1 = px1 - 6;

	// Borrow the menu's bank-15 text ramp; level palettes do not define it consistently.
	SDL_Color savedRamp[16];
	memcpy(savedRamp, &colors[240], sizeof(savedRamp));
	memcpy(&colors[240], &palettes[0][240], sizeof(savedRamp));
	set_palette(colors, 240, 255);

	JE_barShade(VGAScreen, px0, py0, px1, py1);
	JE_barShade(VGAScreen, px0 + 2, py0 + 2, px1 - 2, py1 - 2);

	memcpy(VGAScreen2->pixels, VGAScreen->pixels, (size_t)VGAScreen2->pitch * VGAScreen2->h);

	int selected = 0;
	bool firstFrame = true;

	// Start the fade when the panel appears, even if the wreck animation was skipped.
	MusicFadeOut deathFade;
	music_fade_out_init(&deathFade);

	for (bool done = false; !done; )
	{
		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		draw_font_hv_shadow(VGAScreen, midX, py0 + 10, endlessDeathTitle, normal_font, centered, 15, titleValue, false, 2);

		for (int i = 0; i < (int)COUNTOF(endlessDeathRowName); ++i)
		{
			draw_font_hv_shadow(VGAScreen, midX, rowY0 + rowPitch * i, endlessDeathRowName[i], normal_font, centered,
			                    15, i == selected ? rowValueOn : rowValueOff, false, 2);
		}

		draw_font_hv_shadow(VGAScreen, midX, py1 - 16, endlessDeathRowHelp[selected], small_font, centered, 15, helpValue, true, 1);

		service_SDL_events(true);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		if (firstFrame)
		{
			// Ignore controls held through the fatal hit without delaying the first presentation or fade.
			firstFrame = false;

			service_SDL_events(false);
			while (keydown || mousedown || joydown)
			{
				music_fade_out_tick(&deathFade);

				NETWORK_KEEP_ALIVE();
				SDL_Delay(1);
				poll_joysticks();
				service_SDL_events(false);
			}

			newkey = newmouse = false;
		}

		bool mouseMoved = false;
		do
		{
			music_fade_out_tick(&deathFade);

			SDL_Delay(1);  // fine poll so the cursor redraws at display rate on motion

			const float oldMouseX = mouse_xf;
			const float oldMouseY = mouse_yf;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			// Keep draining the peer while this screen is open. Otherwise repeated wait
			// packets fill the receive queue and later traffic is lost.
			NETWORK_KEEP_ALIVE();
			while (network_shop_pump())
				;

			mouseMoved = mouse_xf != oldMouseX || mouse_yf != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		bool action = false;

		if ((mouseMoved || newmouse) && mouse_x >= rowX0 && mouse_x < rowX1)
		{
			for (int i = 0; i < (int)COUNTOF(endlessDeathRowName); ++i)
			{
				const int y = rowY0 + rowPitch * i;
				if (mouse_y < y - 2 || mouse_y >= y - 2 + rowH)
					continue;

				if (selected != i)
				{
					JE_playSampleNum(S_CURSOR);

					selected = i;
				}

				if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
				    lastmouse_x >= rowX0 && lastmouse_x < rowX1 &&
				    lastmouse_y >= y - 2 && lastmouse_y < y - 2 + rowH)
				{
					action = true;
				}

				break;
			}
		}

		if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selected = selected == 0 ? (int)COUNTOF(endlessDeathRowName) - 1 : selected - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selected = selected == (int)COUNTOF(endlessDeathRowName) - 1 ? 0 : selected + 1;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			default:
				break;
			}
		}

		if (action)
		{
			JE_playSampleNum(S_SELECT);

			done = true;
		}
	}

	// Do not let the confirming press carry into the next screen. A row picked inside that
	// first half second leaves the ramp part-way, so run it out here instead of cutting the track
	// off at whatever volume it had reached.
	service_SDL_events(false);
	while (keydown || mousedown || joydown || !deathFade.done)
	{
		music_fade_out_tick(&deathFade);

		NETWORK_KEEP_ALIVE();
		SDL_Delay(1);
		poll_joysticks();
		service_SDL_events(false);
	}

	// Put the level's ramp back in `colors` for whoever sets the palette next, but do NOT push it
	// to the screen: the panel is still up while the caller fades to black, and re-applying the
	// level ramp under it wrecks the letters again.
	memcpy(&colors[240], savedRamp, sizeof(savedRamp));

	VGAScreen = temp_surface; /* side-effect of game_screen */

	return (EndlessDeathChoice)selected;
}

/* Map a screen point to the absolute row index of a scrolled panel list, or -1
 * if the point isn't over a row. Shared by the debug menu and its submenus. */
static int panel_row_at(int mx, int my, int px0, int px1, int items_top,
                        int row_h, int visibleRows, int scrollTop, int count)
{
	if (mx < px0 || mx > px1 || my < items_top)
		return -1;
	const int vis = (my - items_top) / row_h;
	if (vis < 0 || vis >= visibleRows)
		return -1;
	const int idx = scrollTop + vis;
	return (idx >= 0 && idx < count) ? idx : -1;
}

/* Expert-mode tuning submenu of the debug menu; rows come from the shared
 * expertSettings table plus a synthetic final "Reset Defaults" row. */
static void JE_expertSettingsMenu(int off_x, int off_y)
{
	const size_t resetRow = (size_t)expertSettingsCount;  // synthetic last row
	const size_t rowCount = resetRow + 1;
	size_t selected = 0;
	int prev_mx = mouse_x, prev_my = mouse_y;  // for motion-based hover

	wait_noinput(false, false, true);
	newkey = newmouse = false;  // don't let the click/key that opened us leak in

	const int px0 = DEBUG_MENU_X + off_x, py0 = DEBUG_MENU_Y + off_y;
	const int px1 = DEBUG_MENU_X + DEBUG_MENU_WIDTH - 1 + off_x, py1 = vga_height - 5 + off_y;
	const int title_h = 15;
	const int items_top = py0 + title_h + 3;
	const int items_bottom = py1 - 9;
	const int row_h = MAX(1, (items_bottom - items_top) / (int)rowCount);  // >= 1: divisor in panel_row_at
	const int mid_x = (px0 + px1) / 2;

	enum {
		C_PANEL_BG = 0xF1, C_TITLE_BG = 0xF3, C_DIVIDER = 0xF6,
		C_EDGE_HI  = 0xFB, C_EDGE_LO  = 0xF4, C_SEL_BAR = 0xF5
	};

	bool done = false;
	while (!done)
	{
		fill_rectangle_xy(VGAScreen, px0, py0, px1, py1, C_PANEL_BG);

		fill_rectangle_xy(VGAScreen, px0, py0, px1, py0, C_EDGE_HI);
		fill_rectangle_xy(VGAScreen, px0, py0, px0, py1, C_EDGE_HI);
		fill_rectangle_xy(VGAScreen, px0, py1, px1, py1, C_EDGE_LO);
		fill_rectangle_xy(VGAScreen, px1, py0, px1, py1, C_EDGE_LO);

		fill_rectangle_xy(VGAScreen, px0 + 1, py0 + 1, px1 - 1, py0 + title_h - 1, C_TITLE_BG);
		fill_rectangle_xy(VGAScreen, px0 + 1, py0 + title_h, px1 - 1, py0 + title_h, C_DIVIDER);
		draw_font_hv_shadow(VGAScreen, mid_x, py0 + 3, "EXPERT  SETTINGS", normal_font, centered, 15, 4, true, 1);

		for (size_t i = 0; i < rowCount; ++i)
		{
			int ry = items_top + (int)i * row_h;
			bool sel = (i == selected);

			const char* label;
			char buf[24];
			if (i == resetRow)
			{
				label = "Reset Defaults";
				buf[0] = '\0';
			}
			else
			{
				const ExpertSetting* s = &expertSettings[i];
				label = s->label;
				if (s->fmt == 'x')
					snprintf(buf, sizeof(buf), "x%d", *s->value);
				else
					snprintf(buf, sizeof(buf), "%d%%", *s->value);
			}

			if (sel)
				fill_rectangle_xy(VGAScreen, px0 + 3, ry - 1, px1 - 3, ry + row_h - 2, C_SEL_BAR);

			draw_font_hv_shadow(VGAScreen, px0 + 12, ry, label, small_font, left_aligned,
			                    15, sel ? 5 : -1, true, 1);
			draw_font_hv_shadow(VGAScreen, px1 - 9, ry, buf, small_font, right_aligned,
			                    15, sel ? 5 : 0, true, 1);

			if (sel)
				draw_font_hv(VGAScreen, px0 + 5, ry, ">", small_font, left_aligned, 15, 6);
		}

		draw_font_hv(VGAScreen, mid_x, py1 - 8, "Left/Right: change    Esc: back",
		             small_font, centered, 15, -3);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		NETWORK_KEEP_ALIVE();  // nothing else pumps packets while this panel is up

		/* wheel moves the selection; hover highlights on pointer motion; left-click
		 * advances a value (Enter on the Reset row), right-click decreases it */
		{
			if (mouse_scroll != 0)
			{
				int ns = (int)selected - mouse_scroll;
				if (ns < 0)
					ns = 0;
				if (ns > (int)rowCount - 1)
					ns = (int)rowCount - 1;
				selected = (size_t)ns;
				mouse_scroll = 0;
			}
			if (mouse_x != prev_mx || mouse_y != prev_my)
			{
				const int hov = panel_row_at(mouse_x, mouse_y, px0, px1, items_top,
				                             row_h, (int)rowCount, 0, (int)rowCount);
				if (hov >= 0)
					selected = (size_t)hov;
			}
			prev_mx = mouse_x;
			prev_my = mouse_y;
			if (newmouse)
			{
				const int r = panel_row_at(lastmouse_x, lastmouse_y, px0, px1, items_top,
				                           row_h, (int)rowCount, 0, (int)rowCount);
				if (r >= 0)
				{
					selected = (size_t)r;
					newkey = true;
					lastkey_scan = (lastmouse_but == SDL_BUTTON_RIGHT) ? SDL_SCANCODE_LEFT
					             : ((size_t)r == resetRow ? SDL_SCANCODE_RETURN : SDL_SCANCODE_RIGHT);
				}
				newmouse = false;
			}
		}

		if (newkey)
		{
			ExpertSetting* s = (selected == resetRow) ? NULL : &expertSettings[selected];
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				selected = (selected == 0) ? rowCount - 1 : selected - 1;
				break;
			case SDL_SCANCODE_DOWN:
				selected = (selected + 1) % rowCount;
				break;
			case SDL_SCANCODE_LEFT:
				if (s != NULL && *s->value - s->step >= s->lo)
					*s->value -= s->step;
				break;
			case SDL_SCANCODE_RIGHT:
				if (s != NULL && *s->value + s->step <= s->hi)
					*s->value += s->step;
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				if (s == NULL)  // Reset Defaults
					for (int j = 0; j < expertSettingsCount; ++j)
						*expertSettings[j].value = expertSettings[j].def;
				break;
			case SDL_SCANCODE_ESCAPE:
				done = true;
				break;
			default:
				break;
			}

			newkey = false;
		}
	}

	wait_noinput(false, false, true);
}

/* God Mode: a single 4-state debug-menu option backed by the two independent
 * infinite-shield / infinite-armor cheat flags the engine already honours. */
enum { GOD_OFF = 0, GOD_ON, GOD_ARMOR_ONLY, GOD_SHIELD_ONLY };
static const char *const debug_god_mode_names[] = { "OFF", "ON", "Armor Only", "Shield Only" };

static int debug_god_mode_get(void)
{
	if (cheatInfiniteShields)
		return cheatInfiniteArmor ? GOD_ON : GOD_SHIELD_ONLY;
	return cheatInfiniteArmor ? GOD_ARMOR_ONLY : GOD_OFF;
}
static void debug_god_mode_set(int g)
{
	cheatInfiniteShields = (g == GOD_ON || g == GOD_SHIELD_ONLY);
	cheatInfiniteArmor   = (g == GOD_ON || g == GOD_ARMOR_ONLY);
}

/* Draw a main-table sprite magnified to scale*scale blocks, writing only inside
 * the inclusive clip box (blit_sprite() can neither clip horizontally nor magnify). */
static void draw_sprite_obj_scaled_clip(SDL_Surface *s, int x, int y, const Sprite *sp, int scale,
                                        int cx0, int cy0, int cx1, int cy1)
{
	if (sp == NULL || sp->data == NULL || scale < 1)
		return;

	const Uint8 *data = sp->data;
	const Uint8 *const data_ul = data + sp->size;
	const int width = (int)sp->width;

	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 > s->w - 1) cx1 = s->w - 1;
	if (cy1 > s->h - 1) cy1 = s->h - 1;

	Uint8 *const pixels = (Uint8 *)s->pixels;
	int col = 0, row = 0;
	for (; data < data_ul; ++data)
	{
		switch (*data)
		{
		case 255:  // run of transparent pixels
			++data;
			col += *data;
			break;
		case 254:  // next row
			col = width;
			break;
		case 253:  // one transparent pixel
			++col;
			break;
		default:   // opaque pixel -> scale*scale block
		{
			const int bx = x + col * scale, by = y + row * scale;
			for (int dy = 0; dy < scale; ++dy)
			{
				const int py = by + dy;
				if (py < cy0 || py > cy1)
					continue;
				Uint8 *const prow = pixels + py * s->pitch;
				for (int dx = 0; dx < scale; ++dx)
				{
					const int px = bx + dx;
					if (px >= cx0 && px <= cx1)
						prow[px] = *data;
				}
			}
			++col;
			break;
		}
		}
		if (col >= width)
		{
			col = 0;
			++row;
		}
	}
}

/* Convenience wrapper for a sprite addressed by main-table index. */
static void draw_sprite_scaled_clip(SDL_Surface *s, int x, int y, unsigned int table,
                                    unsigned int index, int scale,
                                    int cx0, int cy0, int cx1, int cy1)
{
	if (table >= SPRITE_TABLES_MAX || !sprite_exists(table, index))
		return;
	draw_sprite_obj_scaled_clip(s, x, y, sprite(table, index), scale, cx0, cy0, cx1, cy1);
}

/* Load a Sprite_array .shp (u16 count, then per sprite a "populated" flag and,
 * when set, u16 width/height/size + size bytes). Fails safely on a missing file
 * or implausible values; *out must be freed with free_local_sprite_array. */
static void free_local_sprite_array(Sprite_array *a)
{
	for (unsigned int i = 0; i < a->count && i < SPRITES_PER_TABLE_MAX; ++i)
	{
		free(a->sprite[i].data);
		a->sprite[i].data = NULL;
	}
	a->count = 0;
}
static bool load_sprite_array_file(const char *filename, Sprite_array *out)
{
	memset(out, 0, sizeof(*out));

	FILE *f = dir_fopen(data_dir(), filename, "rb");
	if (f == NULL)
		return false;

	Uint16 count = 0;
	if (fread(&count, sizeof(count), 1, f) != 1)
	{
		fclose(f);
		return false;
	}
	count = SDL_SwapLE16(count);
	if (count == 0 || count > SPRITES_PER_TABLE_MAX)  // not this format / empty
	{
		fclose(f);
		return false;
	}

	out->count = count;
	bool any = false;
	for (unsigned int i = 0; i < count; ++i)
	{
		Sprite *const sp = &out->sprite[i];
		Uint8 populated = 0;
		if (fread(&populated, 1, 1, f) != 1)
			break;
		if (!populated)
			continue;

		Uint16 whs[3];
		if (fread(whs, sizeof(whs[0]), 3, f) != 3)
			break;
		sp->width  = SDL_SwapLE16(whs[0]);
		sp->height = SDL_SwapLE16(whs[1]);
		sp->size   = SDL_SwapLE16(whs[2]);
		if (sp->size == 0 || sp->size > 64u * 1024u)  // sanity guard
			break;

		sp->data = malloc(sp->size);
		if (sp->data == NULL || fread(sp->data, 1, sp->size, f) != sp->size)
		{
			free(sp->data);
			sp->data = NULL;
			break;
		}
		any = true;
	}
	fclose(f);

	if (!any)
	{
		free_local_sprite_array(out);
		return false;
	}
	return true;
}

static const char *const spriteTableNames[SPRITE_TABLES_MAX] = {
	"Font", "Small Font", "Tiny Font", "Planets",
	"Faces", "Options / Help", "Weapons", "Extra / Endings"
};

/* Compiled Sprite2 sheet helpers.
 * 12px-wide RLE sprites preceded by a table of 16-bit byte offsets: count is
 * the first offset / 2 and 1-based sprite N starts at offsets[N-1]. */
static int sprite2_count(const Sprite2_array *a)
{
	if (a->data == NULL || a->size < 2)
		return 0;
	return SDL_SwapLE16(((const Uint16 *)a->data)[0]) / 2;
}
static int sprite2_height(const Sprite2_array *a, int index1)
{
	if (index1 < 1 || index1 > sprite2_count(a))
		return 0;
	const size_t off = SDL_SwapLE16(((const Uint16 *)a->data)[index1 - 1]);
	if (off >= a->size)
		return 0;
	const Uint8 *data = a->data + off;
	const Uint8 *const end = a->data + a->size;  // never read past the buffer
	int rows = 1;
	for (; data < end && *data != 0x0f; ++data)
	{
		const int count = (*data & 0xf0) >> 4;
		if (count == 0)
			++rows;
		else
			data += count;  // step past the opaque pixel bytes
	}
	return rows;
}
static void draw_sprite2_scaled_clip(SDL_Surface *s, int x, int y, const Sprite2_array *a,
                                     int index1, int scale, int cx0, int cy0, int cx1, int cy1)
{
	if (a->data == NULL || index1 < 1 || index1 > sprite2_count(a) || scale < 1)
		return;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 > s->w - 1) cx1 = s->w - 1;
	if (cy1 > s->h - 1) cy1 = s->h - 1;

	const size_t off = SDL_SwapLE16(((const Uint16 *)a->data)[index1 - 1]);
	if (off >= a->size)
		return;
	Uint8 *const pixels = (Uint8 *)s->pixels;
	const Uint8 *data = a->data + off;
	const Uint8 *const end = a->data + a->size;  // never read past the buffer
	int col = 0, row = 0;
	for (; data < end && *data != 0x0f; ++data)
	{
		col += *data & 0x0f;  // transparent skip
		int count = (*data & 0xf0) >> 4;
		if (count == 0)
		{
			++row;
			col = 0;
		}
		else
		{
			while (count-- && data + 1 < end)
			{
				++data;
				const int bx = x + col * scale, by = y + row * scale;
				for (int dy = 0; dy < scale; ++dy)
				{
					const int py = by + dy;
					if (py < cy0 || py > cy1) continue;
					Uint8 *const prow = pixels + py * s->pitch;
					for (int dx = 0; dx < scale; ++dx)
					{
						const int px = bx + dx;
						if (px >= cx0 && px <= cx1)
							prow[px] = *data;
					}
				}
				++col;
			}
		}
	}
}

/* Background tiles are raw 24x28 one-byte pixels. */
#define TILE_W 24
#define TILE_H 28
static void draw_tile_scaled_clip(SDL_Surface *s, int x, int y, const Uint8 *tile,
                                  int scale, int cx0, int cy0, int cx1, int cy1)
{
	if (tile == NULL || scale < 1)
		return;
	if (cx0 < 0) cx0 = 0;
	if (cy0 < 0) cy0 = 0;
	if (cx1 > s->w - 1) cx1 = s->w - 1;
	if (cy1 > s->h - 1) cy1 = s->h - 1;

	Uint8 *const pixels = (Uint8 *)s->pixels;
	for (int r = 0; r < TILE_H; ++r)
		for (int c = 0; c < TILE_W; ++c)
		{
			const Uint8 v = tile[r * TILE_W + c];  // tiles are opaque; draw every pixel
			const int bx = x + c * scale, by = y + r * scale;
			for (int dy = 0; dy < scale; ++dy)
			{
				const int py = by + dy;
				if (py < cy0 || py > cy1) continue;
				Uint8 *const prow = pixels + py * s->pitch;
				for (int dx = 0; dx < scale; ++dx)
				{
					const int px = bx + dx;
					if (px >= cx0 && px <= cx1)
						prow[px] = v;
				}
			}
		}
}

/* Unified sprite source model.
 * Main shape tables, compiled (Sprite2) sheets and background tile banks
 * behind one count / dims / exists / draw interface for the viewer. */
typedef enum { VS_MAIN, VS_SPRITE2, VS_TILE, VS_SPRITE_ARRAY } VSKind;
typedef struct
{
	const char *name;
	VSKind kind;
	int count;
	unsigned int table;          // VS_MAIN
	const Sprite2_array *sheet;  // VS_SPRITE2
	const Uint8 *tileBase;       // VS_TILE: pointer to first tile's pixels
	size_t tileStride;           // VS_TILE: bytes between successive tiles
	const Sprite_array *localArr;// VS_SPRITE_ARRAY: a freshly-loaded shape table
} VSource;

static int vsrc_add(VSource *list, int n, const char *name, VSKind kind, int count,
                    unsigned int table, const Sprite2_array *sheet,
                    const Uint8 *tileBase, size_t tileStride)
{
	list[n].name = name;
	list[n].kind = kind;
	list[n].count = count;
	list[n].table = table;
	list[n].sheet = sheet;
	list[n].tileBase = tileBase;
	list[n].tileStride = tileStride;
	list[n].localArr = NULL;
	return n + 1;
}
static bool vsrc_exists(const VSource *s, int i)
{
	if (i < 0 || i >= s->count)
		return false;
	if (s->kind == VS_MAIN)
		return sprite_exists(s->table, i);
	if (s->kind == VS_SPRITE_ARRAY)
		return i < (int)s->localArr->count && s->localArr->sprite[i].data != NULL;
	return true;  // sprite2 / tile banks have no gaps
}
static void vsrc_dims(const VSource *s, int i, int *w, int *h)
{
	switch (s->kind)
	{
	case VS_MAIN:    *w = get_sprite_width(s->table, i); *h = get_sprite_height(s->table, i); break;
	case VS_SPRITE2: *w = 12; *h = sprite2_height(s->sheet, i + 1); break;
	case VS_TILE:    *w = TILE_W; *h = TILE_H; break;
	case VS_SPRITE_ARRAY: *w = s->localArr->sprite[i].width; *h = s->localArr->sprite[i].height; break;
	}
}
static void vsrc_draw(SDL_Surface *surf, const VSource *s, int i, int x, int y, int scale,
                      int cx0, int cy0, int cx1, int cy1)
{
	switch (s->kind)
	{
	case VS_MAIN:    draw_sprite_scaled_clip(surf, x, y, s->table, i, scale, cx0, cy0, cx1, cy1); break;
	case VS_SPRITE2: draw_sprite2_scaled_clip(surf, x, y, s->sheet, i + 1, scale, cx0, cy0, cx1, cy1); break;
	case VS_TILE:    draw_tile_scaled_clip(surf, x, y, s->tileBase + (size_t)i * s->tileStride, scale, cx0, cy0, cx1, cy1); break;
	case VS_SPRITE_ARRAY: draw_sprite_obj_scaled_clip(surf, x, y, &s->localArr->sprite[i], scale, cx0, cy0, cx1, cy1); break;
	}
}
static int vsrc_first(const VSource *s)
{
	for (int i = 0; i < s->count; ++i)
		if (vsrc_exists(s, i))
			return i;
	return 0;
}
static int vsrc_last(const VSource *s)
{
	for (int i = s->count - 1; i >= 0; --i)
		if (vsrc_exists(s, i))
			return i;
	return 0;
}
static int vsrc_step(const VSource *s, int cur, int dir)
{
	for (int i = cur + dir; i >= 0 && i < s->count; i += dir)
		if (vsrc_exists(s, i))
			return i;
	return cur;  // already at the first/last existing entry
}

/* Load every non-blank 24x28 tile from shapes<c>.dat (up to 600 entries of
 * [1-byte blank flag][672 pixel bytes]) so all tilesets are browsable; the
 * per-level megaData banks only hold the current map's tiles. Returns the
 * tile count; caller frees *outBuf (NULL when there are none). */
static int load_tileset_file(char c, Uint8 **outBuf)
{
	*outBuf = NULL;

	char name[16];
	snprintf(name, sizeof(name), "shapes%c.dat", c);
	FILE *f = dir_fopen(data_dir(), name, "rb");
	if (f == NULL)
		return 0;

	Uint8 *buf = malloc((size_t)600 * TILE_W * TILE_H);
	int count = 0;
	if (buf != NULL)
	{
		for (int z = 0; z < 600; ++z)
		{
			Uint8 blank;
			if (fread(&blank, 1, 1, f) != 1)
				break;
			if (blank)
				continue;  // blank tile carries no pixel data
			if (fread(buf + (size_t)count * TILE_W * TILE_H, 1, TILE_W * TILE_H, f) != (size_t)(TILE_W * TILE_H))
				break;
			++count;
		}
	}
	fclose(f);

	if (count == 0)
	{
		free(buf);
		return 0;
	}
	*outBuf = buf;
	return count;
}

/* Load compiled sheet newsh<c>.shp (enemy banks, shop, explosions, ...) straight
 * from disk, so the viewer isn't limited to the current level's enemies.
 * Returns false, leaving *out empty, when the file is absent or spriteless. */
static bool try_load_newsh(char c, Sprite2_array *out)
{
	out->data = NULL;
	out->size = 0;

	char fname[16];
	snprintf(fname, sizeof(fname), "newsh%c.shp", c);
	FILE *f = dir_fopen(data_dir(), fname, "rb");
	if (f == NULL)
		return false;

	out->size = ftell_eof(f);
	if (out->size > 2)
		JE_loadCompShapesB(out, f);  // mallocs out->data and reads the file
	fclose(f);

	if (out->data == NULL || sprite2_count(out) <= 0)
	{
		free_sprite2s(out);
		return false;
	}
	return true;
}

/* Sprite browser submenu of the debug menu: magnified checker-backed preview
 * plus a filmstrip of neighbours, over every graphics source, with palette
 * switching so sprites built for a different palette display correctly. */
static void JE_spriteViewer(int off_x, int off_y)
{
	wait_noinput(false, false, true);
	newkey = newmouse = false;  // don't let the click/key that opened us leak in

	/* Build the source list from whatever graphics are currently loaded. It only
	 * borrows source pointers, so reusable storage keeps this debug UI's stack small. */
	static VSource src[128];
	int nsrc = 0;
	for (unsigned int t = 0; t < SPRITE_TABLES_MAX; ++t)
		if (sprite_table[t].count > 0)
			nsrc = vsrc_add(src, nsrc, spriteTableNames[t], VS_MAIN, (int)sprite_table[t].count, t, NULL, NULL, 0);

	/* The always-in-memory compiled sheets that live inside tyrian.shp (not
	 * standalone files), so they can't be loaded by name like the newsh*.shp. */
	static const struct { const char *name; const Sprite2_array *a; } sheets[] = {
		{ "Player Shots",   &spriteSheet8 },
		{ "Player Ships",   &spriteSheet9 },
		{ "Power-ups",      &spriteSheet10 },
		{ "Coins / Cubes",  &spriteSheet11 },
		{ "Player Shots 2", &spriteSheet12 },
		{ "T2000 Ships",    &spriteSheetT2000 },
	};
	for (size_t i = 0; i < COUNTOF(sheets); ++i)
	{
		const int c = sprite2_count(sheets[i].a);
		if (c > 0)
			nsrc = vsrc_add(src, nsrc, sheets[i].name, VS_SPRITE2, c, 0, sheets[i].a, NULL, 0);
	}

	/* Load every compiled object sheet (newsh*.shp) from disk, iterating only
	 * the on-disk casing so a case-folding filesystem can't load one twice. */
	static const char newshChars[] = "0123456789abcdefghijklmnopqrstuvwxyz#$%'(@^~";
	Sprite2_array loadedSheets[64];
	char loadedNames[64][16];
	int nLoaded = 0;
	for (const char *pc = newshChars; *pc != '\0' && nLoaded < (int)COUNTOF(loadedSheets)
	                                 && nsrc < (int)COUNTOF(src); ++pc)
	{
		if (try_load_newsh(*pc, &loadedSheets[nLoaded]))
		{
			snprintf(loadedNames[nLoaded], sizeof(loadedNames[0]), "newsh %c", *pc);
			nsrc = vsrc_add(src, nsrc, loadedNames[nLoaded], VS_SPRITE2,
			                sprite2_count(&loadedSheets[nLoaded]), 0, &loadedSheets[nLoaded], NULL, 0);
			++nLoaded;
		}
	}

	/* Stand-alone shape tables outside tyrian.shp: the ending/credits sprites
	 * (estsc.shp) plus the unused-but-present estpa / user ship files. */
	static const struct { const char *name; const char *file; } arrFiles[] = {
		{ "Ending: estsc", "estsc.shp" },
		{ "Ending: estpa", "estpa.shp" },
		{ "User ship 1",   "user1.shp" },
		{ "User ship 2",   "user2.shp" },
	};
	Sprite_array localArrs[COUNTOF(arrFiles)];
	int nArr = 0;
	for (size_t i = 0; i < COUNTOF(arrFiles) && nsrc < (int)COUNTOF(src); ++i)
	{
		if (load_sprite_array_file(arrFiles[i].file, &localArrs[nArr]))
		{
			const int at = nsrc;
			nsrc = vsrc_add(src, nsrc, arrFiles[i].name, VS_SPRITE_ARRAY,
			                (int)localArrs[nArr].count, 0, NULL, NULL, 0);
			src[at].localArr = &localArrs[nArr];
			++nArr;
		}
	}

	/* Every level's tiles come from one of these five shapes<c>.dat files; load
	 * them all so the whole game's tilesets are browsable from anywhere. */
	static const char tileFileChars[] = { ')', 'w', 'x', 'y', 'z' };
	static const char *const tileSetNames[] = {
		"Tiles: Set )", "Tiles: Set W", "Tiles: Set X", "Tiles: Set Y", "Tiles: Set Z"
	};
	Uint8 *tileBufs[COUNTOF(tileFileChars)] = { NULL };
	for (size_t i = 0; i < COUNTOF(tileFileChars); ++i)
	{
		const int c = load_tileset_file(tileFileChars[i], &tileBufs[i]);
		if (c > 0)
			nsrc = vsrc_add(src, nsrc, tileSetNames[i], VS_TILE, c, 0, NULL,
			                tileBufs[i], (size_t)TILE_W * TILE_H);
	}

	if (nsrc == 0)  // nothing loaded at all
	{
		for (size_t i = 0; i < COUNTOF(tileBufs); ++i)
			free(tileBufs[i]);
		for (int i = 0; i < nLoaded; ++i)
			free_sprite2s(&loadedSheets[i]);
		for (int i = 0; i < nArr; ++i)
			free_local_sprite_array(&localArrs[i]);
		return;
	}

	const int px0 = DEBUG_MENU_X + off_x, py0 = DEBUG_MENU_Y + off_y;
	const int px1 = DEBUG_MENU_X + DEBUG_MENU_WIDTH - 1 + off_x, py1 = vga_height - 5 + off_y;
	const int title_h = 15;
	const int mid_x = (px0 + px1) / 2;

	enum {
		C_PANEL_BG = 0xF1, C_TITLE_BG = 0xF3, C_DIVIDER = 0xF6,
		C_EDGE_HI  = 0xFB, C_EDGE_LO  = 0xF4,
		C_CHECK_A  = 0xF2, C_CHECK_B  = 0xF4, C_CELL_SEL = 0xFB
	};

	/* Remember the live palette so we can recolour for previews and restore it
	 * on the way out (palSel == -1 means "use the palette already on screen"). */
	Palette savedPal;
	memcpy(savedPal, colors, sizeof(savedPal));
	int palSel = -1;

	int s = 0;            // current source
	int idx = vsrc_first(&src[s]);

	bool done = false;
	while (!done)
	{
		/* panel + beveled border */
		fill_rectangle_xy(VGAScreen, px0, py0, px1, py1, C_PANEL_BG);
		fill_rectangle_xy(VGAScreen, px0, py0, px1, py0, C_EDGE_HI);
		fill_rectangle_xy(VGAScreen, px0, py0, px0, py1, C_EDGE_HI);
		fill_rectangle_xy(VGAScreen, px0, py1, px1, py1, C_EDGE_LO);
		fill_rectangle_xy(VGAScreen, px1, py0, px1, py1, C_EDGE_LO);

		fill_rectangle_xy(VGAScreen, px0 + 1, py0 + 1, px1 - 1, py0 + title_h - 1, C_TITLE_BG);
		fill_rectangle_xy(VGAScreen, px0 + 1, py0 + title_h, px1 - 1, py0 + title_h, C_DIVIDER);
		draw_font_hv_shadow(VGAScreen, mid_x, py0 + 3, "SPRITE  VIEWER", normal_font, centered, 15, 4, true, 1);

		int sw = 0, sh = 0;
		vsrc_dims(&src[s], idx, &sw, &sh);

		/* info lines */
		const int line_h = 11;
		const int ct = py0 + title_h + 4;
		char buf[48];

		snprintf(buf, sizeof(buf), "Source:  %s", src[s].name);
		draw_font_hv_shadow(VGAScreen, px0 + 10, ct, buf, small_font, left_aligned, 15, 0, true, 1);
		snprintf(buf, sizeof(buf), "%d / %d", s + 1, nsrc);
		draw_font_hv_shadow(VGAScreen, px1 - 10, ct, buf, small_font, right_aligned, 15, 0, true, 1);

		snprintf(buf, sizeof(buf), "Sprite %d / %d    %dx%d", idx, src[s].count - 1, sw, sh);
		draw_font_hv_shadow(VGAScreen, px0 + 10, ct + line_h, buf, small_font, left_aligned, 15, 0, true, 1);
		if (palSel < 0)
			snprintf(buf, sizeof(buf), "Pal: native");
		else
			snprintf(buf, sizeof(buf), "Pal: %d / %d", palSel + 1, palette_count);
		draw_font_hv_shadow(VGAScreen, px1 - 10, ct + line_h, buf, small_font, right_aligned, 15, 0, true, 1);

		/* preview box */
		const int bx0 = px0 + 10, bx1 = px1 - 10;
		const int by0 = ct + 2 * line_h + 6, by1 = py1 - 50;

		/* inset frame */
		fill_rectangle_xy(VGAScreen, bx0, by0, bx1, by0, C_EDGE_LO);
		fill_rectangle_xy(VGAScreen, bx0, by0, bx0, by1, C_EDGE_LO);
		fill_rectangle_xy(VGAScreen, bx0, by1, bx1, by1, C_EDGE_HI);
		fill_rectangle_xy(VGAScreen, bx1, by0, bx1, by1, C_EDGE_HI);

		/* checkerboard so transparent / black pixels are visible */
		for (int yy = by0 + 1; yy < by1; yy += 8)
			for (int xx = bx0 + 1; xx < bx1; xx += 8)
			{
				const int c = (((xx - bx0) / 8 + (yy - by0) / 8) & 1) ? C_CHECK_A : C_CHECK_B;
				fill_rectangle_xy(VGAScreen, xx, yy, MIN(xx + 7, bx1 - 1), MIN(yy + 7, by1 - 1), c);
			}

		/* magnify to fill the box (integer scale, capped) */
		int scale = 1;
		if (sw > 0 && sh > 0)
		{
			const int boxw = bx1 - bx0 - 6, boxh = by1 - by0 - 6;
			while (scale < 8 && sw * (scale + 1) <= boxw && sh * (scale + 1) <= boxh)
				++scale;
			const int dw = sw * scale, dh = sh * scale;
			const int dx = (bx0 + bx1) / 2 - dw / 2, dy = (by0 + by1) / 2 - dh / 2;
			vsrc_draw(VGAScreen, &src[s], idx, dx, dy, scale, bx0 + 1, by0 + 1, bx1 - 1, by1 - 1);
		}

		/* zoom indicator */
		snprintf(buf, sizeof(buf), "x%d", scale);
		draw_font_hv_shadow(VGAScreen, bx1 - 3, by0 + 2, buf, small_font, right_aligned, 15, -2, true, 1);

		/* filmstrip of neighbouring sprites */
		const int strip_y = by1 + 4;
		const int strip_bot = py1 - 24;
		const int cell_h = strip_bot - strip_y;
		const int cells = 7;
		const int cell_w = (bx1 - bx0) / cells;
		for (int k = 0; k < cells; ++k)
		{
			const int si = idx - cells / 2 + k;
			const int cxL = bx0 + k * cell_w;
			const bool cur = (k == cells / 2);

			fill_rectangle_xy(VGAScreen, cxL, strip_y, cxL + cell_w - 2, strip_y + cell_h,
			                  cur ? C_CHECK_A : C_PANEL_BG);
			if (cur)
			{
				fill_rectangle_xy(VGAScreen, cxL, strip_y, cxL + cell_w - 2, strip_y, C_CELL_SEL);
				fill_rectangle_xy(VGAScreen, cxL, strip_y + cell_h, cxL + cell_w - 2, strip_y + cell_h, C_CELL_SEL);
				fill_rectangle_xy(VGAScreen, cxL, strip_y, cxL, strip_y + cell_h, C_CELL_SEL);
				fill_rectangle_xy(VGAScreen, cxL + cell_w - 2, strip_y, cxL + cell_w - 2, strip_y + cell_h, C_CELL_SEL);
			}

			if (vsrc_exists(&src[s], si))
			{
				int tw = 0, th = 0;
				vsrc_dims(&src[s], si, &tw, &th);
				const int tx = cxL + (cell_w - 2) / 2 - tw / 2;
				const int ty = strip_y + cell_h / 2 - th / 2;
				vsrc_draw(VGAScreen, &src[s], si, tx, ty, 1,
				          cxL + 1, strip_y + 1, cxL + cell_w - 3, strip_y + cell_h - 1);
			}
		}

		/* two-line footer, kept inside the panel */
		draw_font_hv(VGAScreen, mid_x, py1 - 20,
		             "L/R sprite    U/D source    [ ] palette",
		             small_font, centered, 15, -3);
		draw_font_hv(VGAScreen, mid_x, py1 - 11,
		             "Home/End   PgUp/PgDn   Esc back",
		             small_font, centered, 15, -3);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		NETWORK_KEEP_ALIVE();  // nothing else pumps packets while this panel is up

		/* wheel walks sprites; left-click = next, right-click = exit */
		if (mouse_scroll != 0)
		{
			idx = vsrc_step(&src[s], idx, mouse_scroll > 0 ? -1 : 1);
			mouse_scroll = 0;
		}
		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
				done = true;
			else
				idx = vsrc_step(&src[s], idx, 1);
			newmouse = false;
		}

		if (newkey)
		{
			int newPalSel = palSel;
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_LEFT:
				idx = vsrc_step(&src[s], idx, -1);
				break;
			case SDL_SCANCODE_RIGHT:
				idx = vsrc_step(&src[s], idx, 1);
				break;
			case SDL_SCANCODE_PAGEUP:
				for (int n = 0; n < 10; ++n) idx = vsrc_step(&src[s], idx, -1);
				break;
			case SDL_SCANCODE_PAGEDOWN:
				for (int n = 0; n < 10; ++n) idx = vsrc_step(&src[s], idx, 1);
				break;
			case SDL_SCANCODE_HOME:
				idx = vsrc_first(&src[s]);
				break;
			case SDL_SCANCODE_END:
				idx = vsrc_last(&src[s]);
				break;
			case SDL_SCANCODE_UP:
				s = (s == 0) ? nsrc - 1 : s - 1;
				idx = vsrc_first(&src[s]);
				break;
			case SDL_SCANCODE_DOWN:
				s = (s + 1) % nsrc;
				idx = vsrc_first(&src[s]);
				break;
			case SDL_SCANCODE_LEFTBRACKET:
				newPalSel = (palSel <= -1) ? palette_count - 1 : palSel - 1;
				break;
			case SDL_SCANCODE_RIGHTBRACKET:
				newPalSel = (palSel >= palette_count - 1) ? -1 : palSel + 1;
				break;
			case SDL_SCANCODE_ESCAPE:
				done = true;
				break;
			default:
				break;
			}
			if (newPalSel != palSel)
			{
				palSel = newPalSel;
				set_palette(palSel < 0 ? savedPal : palettes[palSel], 0, 255);
			}
			newkey = false;
		}
	}

	/* restore whatever palette was on screen when we opened */
	set_palette(savedPal, 0, 255);

	for (size_t i = 0; i < COUNTOF(tileBufs); ++i)
		free(tileBufs[i]);
	for (int i = 0; i < nLoaded; ++i)
		free_sprite2s(&loadedSheets[i]);
	for (int i = 0; i < nArr; ++i)
		free_local_sprite_array(&localArrs[i]);

	wait_noinput(false, false, true);
}

// The special-weapon id a twiddle (keyboardCombos row) triggers: its entry in
// (100, 100+SPECIAL_NUM], returned as 1..SPECIAL_NUM, or 0 if none is valid.
static int twiddle_special_id(int row)
{
	for (int k = 0; k < 8; ++k)
	{
		const int v = keyboardCombos[row][k];
		if (v > 100 && v <= 100 + SPECIAL_NUM)
			return v - 100;
	}
	return 0;
}

// Is this special safe to equip? The HUD blits special[id].itemgraphic every frame, so an
// out-of-range icon crashes instantly; guard on a real HUD icon, name, and effect type. Unlike
// Endless keeps Invulnerability because it is safe and useful for debugging.
bool debug_special_is_safe(int id)
{
	if (id == 0)
		return true;  // None; clears the equipped special, no icon drawn
	if (id < 1 || id > SPECIAL_NUM)
		return false;

	// Sprite count of spriteSheet10: entry[0] of the Uint16 offset table is the byte offset to
	// sprite 1, which makes entry[0] / 2 the sprite count.
	unsigned iconMax = 0;
	if (spriteSheet10.data != NULL && spriteSheet10.size >= sizeof(Uint16))
		iconMax = SDL_SwapLE16(((Uint16 *)spriteSheet10.data)[0]) / (unsigned)sizeof(Uint16);

	return special[id].name[0] != '\0'
	    && special[id].stype >= 1 && special[id].stype <= 18
	    && special[id].itemgraphic >= 1 && special[id].itemgraphic <= iconMax;
}

// Debug-only: fault on purpose so the crash logger runs end-to-end (Force Crash row). Pointer must
// be a volatile file-scope global or /O2 folds the null store away.
static int *volatile debug_crash_ptr;  // NULL; never assigned -> the dereference faults
static void debug_force_crash(void)
{
	*debug_crash_ptr = 0xDEAD;
}

/* Debug row identity is independent of display order. New rows need an enum,
 * label, help entry, and a place in dbgRows. */
enum {
	DBG_PLAYER,
	DBG_SHIP, DBG_FRONT_WEAPON, DBG_FRONT_POWER, DBG_REAR_WEAPON, DBG_REAR_POWER,
	DBG_SHIELD, DBG_GENERATOR, DBG_SIDEKICK_L, DBG_SIDEKICK_R, DBG_SPECIAL,
	DBG_TWIDDLE, DBG_AUTOFIRE_TWIDDLE, DBG_TOGGLE_FIRE,
	DBG_AUTOFIRE_SPECIAL, DBG_AUTOFIRE_CHARGE, DBG_INSTANT_CHARGE, DBG_INF_SIDEKICK_AMMO, DBG_INF_GENERATOR,
	DBG_GOD_MODE, DBG_NOCLIP, DBG_EXPERT_MODE, DBG_EXPERT_SETTINGS, DBG_AUTO_DIFFICULTY,
	DBG_DIFFICULTY, DBG_ADD_CASH, DBG_NO_ENEMY_FIRE, DBG_SKIP_LEVEL,
	DBG_PLAY_SOUND, DBG_PLAY_MUSIC, DBG_SPRITE_VIEWER, DBG_HITBOX, DBG_PERF,
	DBG_ENDLESS_FX, DBG_ENDLESS_TUNE,
	DBG_HANG_TIMEOUT, DBG_ROLLBACK_SELFTEST, DBG_FORCE_CRASH,
	DBG_ROW_COUNT
};

static const char *const dbgLabel[DBG_ROW_COUNT] = {
	[DBG_PLAYER]              = "Edit Player",
	[DBG_SHIP]                = "Ship",
	[DBG_FRONT_WEAPON]        = "Front Weapon",
	[DBG_FRONT_POWER]         = "Front Power",
	[DBG_REAR_WEAPON]         = "Rear Weapon",
	[DBG_REAR_POWER]          = "Rear Power",
	[DBG_SHIELD]              = "Shield",
	[DBG_GENERATOR]           = "Generator",
	[DBG_SIDEKICK_L]          = "Sidekick L",
	[DBG_SIDEKICK_R]          = "Sidekick R",
	[DBG_SPECIAL]             = "Special",
	[DBG_TWIDDLE]             = "Twiddle",
	[DBG_AUTOFIRE_TWIDDLE]    = "Autofire Twiddle",
	[DBG_TOGGLE_FIRE]         = "Toggle Fire",
	[DBG_AUTOFIRE_SPECIAL]    = "Autofire Special",
	[DBG_AUTOFIRE_CHARGE]     = "Autofire Charge Sidekicks",
	[DBG_INSTANT_CHARGE]      = "Instant Charge Sidekicks",
	[DBG_INF_SIDEKICK_AMMO]   = "Inf Sidekick Ammo",
	[DBG_INF_GENERATOR]       = "Inf Generator",
	[DBG_GOD_MODE]            = "God Mode",
	[DBG_NOCLIP]              = "Noclip",
	[DBG_EXPERT_MODE]         = "Expert Mode",
	[DBG_EXPERT_SETTINGS]     = "Expert Settings",
	[DBG_AUTO_DIFFICULTY]     = "Auto-Adjust Difficulty",
	[DBG_DIFFICULTY]          = "Difficulty",
	[DBG_ADD_CASH]            = "Add Cash",
	[DBG_NO_ENEMY_FIRE]       = "No Enemy Fire",
	[DBG_SKIP_LEVEL]          = "Skip to Next Level",
	[DBG_PLAY_SOUND]          = "Play Sound",
	[DBG_PLAY_MUSIC]          = "Play Music",
	[DBG_SPRITE_VIEWER]       = "Sprite Viewer",
	[DBG_HITBOX]              = "Hitbox Overlay",
	[DBG_PERF]                = "Perf Overlay",
	[DBG_HANG_TIMEOUT]        = "Hang Watchdog",
	[DBG_ROLLBACK_SELFTEST]   = "Rollback Self-Test",
	[DBG_ENDLESS_FX]          = "Endless Effects",
	[DBG_ENDLESS_TUNE]        = "Endless Mods and Scaling",
	[DBG_FORCE_CRASH]         = "Force Crash (test)",
};

/* One line per row, shown under the list while that row is selected: what it DOES, not what it is
 * called. Keep each under ~40 characters; that is the panel's inner width in small_font. */
static const char *const dbgHelp[DBG_ROW_COUNT] = {
	[DBG_PLAYER]              = "Whose gear the rows below edit",
	[DBG_SHIP]                = "Swap the hull; red = no such ship",
	[DBG_FRONT_WEAPON]        = "Front gun; red = no such weapon",
	[DBG_FRONT_POWER]         = "Front gun power level, 1 to 11",
	[DBG_REAR_WEAPON]         = "Rear gun; red = no such weapon",
	[DBG_REAR_POWER]          = "Rear gun power level, 1 to 11",
	[DBG_SHIELD]              = "Shield type, which sets max shield",
	[DBG_GENERATOR]           = "Generator type, which sets power",
	[DBG_SIDEKICK_L]          = "Left sidekick pod",
	[DBG_SIDEKICK_R]          = "Right sidekick pod",
	[DBG_SPECIAL]             = "Equipped special (safe ids only)",
	[DBG_TWIDDLE]             = "Pick a twiddle special; Enter fires it",
	[DBG_AUTOFIRE_TWIDDLE]    = "Refire that twiddle while fire is held",
	[DBG_TOGGLE_FIRE]         = "Fire button toggles auto-fire",
	[DBG_AUTOFIRE_SPECIAL]    = "Fire your special as you shoot",
	[DBG_AUTOFIRE_CHARGE]     = "When charge pods release on their own",
	[DBG_INSTANT_CHARGE]      = "Charge pods sit at full charge",
	[DBG_INF_SIDEKICK_AMMO]   = "Sidekick ammo never runs out",
	[DBG_INF_GENERATOR]       = "Generator power never drains",
	[DBG_GOD_MODE]            = "Invulnerable: armor, shield, or both",
	[DBG_NOCLIP]              = "Fly through everything (+ ghost look)",
	[DBG_EXPERT_MODE]         = "Harder rules: tough enemies, dear shop",
	[DBG_EXPERT_SETTINGS]     = "Opens the expert multipliers",
	[DBG_AUTO_DIFFICULTY]     = "Score bumps difficulty between levels",
	[DBG_DIFFICULTY]          = "The difficulty being played right now",
	[DBG_ADD_CASH]            = "Type an amount, or L/R for max cash",
	[DBG_NO_ENEMY_FIRE]       = "Enemies never shoot",
	[DBG_SKIP_LEVEL]          = "Ends the level now and moves on",
	[DBG_PLAY_SOUND]          = "L/R picks a sample; Enter plays it",
	[DBG_PLAY_MUSIC]          = "L/R picks a track; Enter plays it",
	[DBG_SPRITE_VIEWER]       = "Opens the sprite sheet browser",
	[DBG_HITBOX]              = "Draw hit boxes on enemies and ship",
	[DBG_PERF]                = "FPS, enemy and shot counts on screen",
	[DBG_HANG_TIMEOUT]        = "Seconds of freeze before the log fires",
	[DBG_ROLLBACK_SELFTEST]   = "Replay every tick to verify rollback",
	[DBG_ENDLESS_FX]          = "Endless mods/perks in a normal game",
	[DBG_ENDLESS_TUNE]        = "Opens the mod, perk and scaling panel",
	[DBG_FORCE_CRASH]         = "Faults on purpose to test the crash log",
};

/* The display order: the rows above under non-selectable headings (id < 0). Grouping is the whole
 * point; a flat 34-row list of hull ids, cheats and diagnostics reads as noise. */
static const struct { int id; const char *heading; } dbgRows[] = {
	{ -1, "SURVIVAL" },   // first: the rows most often reached for mid-level
	{ DBG_GOD_MODE, NULL }, { DBG_NOCLIP, NULL }, { DBG_NO_ENEMY_FIRE, NULL },
	{ DBG_ADD_CASH, NULL },
	{ -1, "LOADOUT" },
	{ DBG_PLAYER, NULL },
	{ DBG_SHIP, NULL }, { DBG_FRONT_WEAPON, NULL }, { DBG_FRONT_POWER, NULL },
	{ DBG_REAR_WEAPON, NULL }, { DBG_REAR_POWER, NULL }, { DBG_SHIELD, NULL },
	{ DBG_GENERATOR, NULL }, { DBG_SIDEKICK_L, NULL }, { DBG_SIDEKICK_R, NULL },
	{ DBG_SPECIAL, NULL },
	{ -1, "FIRING" },
	{ DBG_TOGGLE_FIRE, NULL }, { DBG_TWIDDLE, NULL }, { DBG_AUTOFIRE_TWIDDLE, NULL },
	{ DBG_AUTOFIRE_SPECIAL, NULL }, { DBG_AUTOFIRE_CHARGE, NULL }, { DBG_INSTANT_CHARGE, NULL },
	{ DBG_INF_SIDEKICK_AMMO, NULL }, { DBG_INF_GENERATOR, NULL },
	{ -1, "DIFFICULTY" },
	{ DBG_EXPERT_MODE, NULL }, { DBG_EXPERT_SETTINGS, NULL }, { DBG_AUTO_DIFFICULTY, NULL },
	{ DBG_DIFFICULTY, NULL },
	{ -1, "ENDLESS EFFECTS" },
	{ DBG_ENDLESS_FX, NULL }, { DBG_ENDLESS_TUNE, NULL },
	{ -1, "LEVEL" },
	{ DBG_SKIP_LEVEL, NULL },
	{ -1, "DIAGNOSTICS" },
	{ DBG_PLAY_SOUND, NULL }, { DBG_PLAY_MUSIC, NULL }, { DBG_SPRITE_VIEWER, NULL },
	{ DBG_HITBOX, NULL }, { DBG_PERF, NULL }, { DBG_HANG_TIMEOUT, NULL },
	{ DBG_ROLLBACK_SELFTEST, NULL }, { DBG_FORCE_CRASH, NULL },
};
#define DBG_DISPLAY_ROWS  ((int)COUNTOF(dbgRows))
#define DBG_HEADING_COUNT 7
// Catches the slip that would otherwise go unnoticed: a row added to the enum but never placed in
// dbgRows, leaving it unreachable in the menu. Bump DBG_HEADING_COUNT when adding a heading.
COMPILE_TIME_ASSERT(dbg_rows_cover_every_row, DBG_DISPLAY_ROWS == DBG_ROW_COUNT + DBG_HEADING_COUNT);

/* dbgVis maps visible rows to dbgRows for the current mode. Rebuilt whenever the menu opens. */
static int dbgVis[DBG_DISPLAY_ROWS];
static int dbgVisCount;

static bool dbgRowApplies(int id)
{
	switch (id)
	{
	case DBG_PLAYER:
		return twoPlayerMode;
	case DBG_ENDLESS_FX:
	case DBG_ENDLESS_TUNE:
	case DBG_ROLLBACK_SELFTEST:  // netplay drives the rollback engine for real; the self-test never arms there
		return !isNetworkGame;
	default:
		return true;
	}
}

static void dbgBuildVisibleRows(void)
{
	dbgVisCount = 0;
	for (int i = 0; i < DBG_DISPLAY_ROWS; ++i)
		if (dbgRows[i].id < 0 || dbgRowApplies(dbgRows[i].id))
			dbgVis[dbgVisCount++] = i;

	// Drop a heading whose whole group went with it, or it would sit over the next group's rows.
	int keep = 0;
	for (int i = 0; i < dbgVisCount; ++i)
	{
		const bool heading = dbgRows[dbgVis[i]].id < 0;
		if (heading && (i + 1 == dbgVisCount || dbgRows[dbgVis[i + 1]].id < 0))
			continue;
		dbgVis[keep++] = dbgVis[i];
	}
	dbgVisCount = keep;
}

static bool dbgRowIsHeading(int r)
{
	return r < 0 || r >= dbgVisCount || dbgRows[dbgVis[r]].id < 0;
}

static int dbgRowId(int r)
{
	return dbgRows[dbgVis[r]].id;
}

/* Step one selectable row in `dir`, wrapping past the headings. */
static int dbgRowStep(int r, int dir)
{
	for (int n = 0; n < dbgVisCount; ++n)
	{
		r += dir;
		if (r < 0)
			r = dbgVisCount - 1;
		else if (r >= dbgVisCount)
			r = 0;
		if (!dbgRowIsHeading(r))
			return r;
	}
	return r;
}

/* The nearest selectable row at or after `r` (then searching back); for any jump that can land
 * on a heading: clamping, paging, Home/End, a click. */
static int dbgRowSnap(int r)
{
	if (r < 0)
		r = 0;
	if (r > dbgVisCount - 1)
		r = dbgVisCount - 1;
	for (int i = r; i < dbgVisCount; ++i)
		if (!dbgRowIsHeading(i))
			return i;
	for (int i = r; i >= 0; --i)
		if (!dbgRowIsHeading(i))
			return i;
	return r;
}

/* Toggle campaign modifier effects outside a real Endless run. Arm through the shared path to clear
 * stale outpost purchases, then persist immediately for crash-safe debug sessions. */
static void debug_toggle_campaign_mods(void)
{
	if (endlessMode)
		return;
	if (!endlessCampaignMods)
		endlessCampaignModsArm();
	endlessCampaignMods = !endlessCampaignMods;
	save_opentyrian_config();
}

/* Flip the rollback self-test. Goes through rollback_selftest_set() rather than the flag, so
 * switching it on mid-level also arms the registry and snapshot ring. */
static void debug_toggle_rollback_selftest(void)
{
	rollback_selftest_set(!rollback_selftest);
	save_opentyrian_config();
}

/* Does this row write into the edited player's items? Those all need the refresh below; nothing
 * else does. (The Edit Player row itself changes no items, only which player they belong to.) */
static bool dbgRowIsLoadout(int id)
{
	switch (id)
	{
	case DBG_SHIP:   case DBG_FRONT_WEAPON: case DBG_FRONT_POWER:
	case DBG_REAR_WEAPON: case DBG_REAR_POWER: case DBG_SHIELD:
	case DBG_GENERATOR: case DBG_SIDEKICK_L: case DBG_SIDEKICK_R:
	case DBG_SPECIAL:
		return true;
	default:
		return false;
	}
}

/* Rebuild cached player state after a mid-level debug loadout edit. */
/* True only when the debug menu overlays the gameplay HUD. */
static bool debugMenuOverHud = false;

/* Debug ship changes take full armor. Custom-ship changes preserve live gauge ratios. */
// Refresh the sidebar in menu backdrops captured before a loadout edit.
static void hud_strip_snapshot(void)
{
	if (VGAScreenSeg == NULL || VGAScreen2 == NULL || rollback_resim_silent)
		return;

	const int x0 = PLAYFIELD_WIDTH;
	const int w = vga_width - x0;
	if (w <= 0)
		return;

	for (int y = 0; y < vga_height; ++y)
	{
		memcpy((Uint8 *)VGAScreen2->pixels + (size_t)y * VGAScreen2->pitch + x0,
		       (Uint8 *)VGAScreenSeg->pixels + (size_t)y * VGAScreenSeg->pitch + x0, (size_t)w);
	}
}

static void debug_apply_loadout_change(int pnum, bool shipChanged, bool carryGauges)
{
	uint keptArmor[COUNTOF(player)];
	uint keptArmorMax[COUNTOF(player)];
	uint keptShield[COUNTOF(player)];
	uint keptShieldMax[COUNTOF(player)];
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		keptArmor[i] = player[i].armor;
		keptArmorMax[i] = player[i].initial_armor;
		keptShield[i] = player[i].shield;
		keptShieldMax[i] = player[i].shield_max;
	}

	// A weapon that no longer exists can leave the multi-shot phase pointing at a pattern the new
	// one doesn't have, and weapon_mode can outrun the new port's configuration count.
	memset(shotMultiPos, 0, sizeof(shotMultiPos));
	for (uint i = 0; i < COUNTOF(player); ++i)
		if (player[i].weapon_mode > JE_portConfigs(&player[i]))
			player[i].weapon_mode = 1;

	JE_getShipInfo();   // shipGr + shipGrPtr, hull, initial_armor, hit box, powerAdd

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		// JE_getShipInfo rewrites BOTH players' armor from their hulls; only the player whose
		// hull actually changed should take that, so everyone else keeps what they had.
		if ((int)i == pnum && carryGauges)
			player[i].armor = player_carry_gauge(keptArmor[i], keptArmorMax[i],
			                                      player[i].initial_armor);
		else if ((int)i != pnum || !shipChanged)
			player[i].armor = keptArmor[i];
		if (player[i].armor > player[i].initial_armor)
			player[i].armor = player[i].initial_armor;

		// The shield ceiling is set only at level start (tyrian2.c), so without this a shield swap
		// keeps the old one's maximum and the gauge scale goes with it.
		player[i].shield_max = arcade_shield_max(&player[i]);
		if ((int)i == pnum && carryGauges)
			player[i].shield = player_carry_gauge(keptShield[i], keptShieldMax[i],
			                                      player[i].shield_max);
		else
			player[i].shield = keptShield[i];
		if (player[i].shield > player[i].shield_max)
			player[i].shield = player[i].shield_max;
	}

	// Repaint the event-driven gauges on a gameplay HUD after changing equipment.
	// JE_drawOptions also paints, so calling it over shop art would leave HUD elements behind.
	if (debugMenuOverHud)
	{
		JE_wipeShieldArmorBars();
		JE_drawArmor();
		JE_drawShield();
		JE_drawOptions();   // re-seeds the sidekick pods' ammo, refill cadence and style from options[]
		JE_drawPortConfigButtons();  // the mode arrows belong to the rear gun that just changed

		// Refresh the captured menu background immediately.
		hud_strip_snapshot();

		// Repaint again after paths that replace the whole screen on exit.
		hud_sidekicks_dirty = true;
		hud_bars_dirty = true;
	}
}

static void extraShipLoadoutRefresh(uint pnum, bool overHud)
{
	const bool wasOverHud = debugMenuOverHud;
	SDL_Surface *const savedScreen = VGAScreen;
	debugMenuOverHud = overHud;
	if (overHud)
		VGAScreen = VGAScreenSeg;
	debug_apply_loadout_change((int)pnum, false, true);
	VGAScreen = savedScreen;
	debugMenuOverHud = wasOverHud;
}

/* Peer side of a networked debug edit (network.c): the wire block already carried the armor and
 * shield the editing machine ended up with, so nothing is re-derived here; this only rebuilds
 * the caches that hang off items[]. */
void debugLoadoutRefresh(bool overHud)
{
	const bool wasOverHud = debugMenuOverHud;
	debugMenuOverHud = overHud;
	debug_apply_loadout_change(-1, false, false);
	debugMenuOverHud = wasOverHud;
}

void JE_debugMenu(bool center)
{
	SDL_Surface* temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	// See debugMenuOverHud: uncentered == opened from inside a level, which is the only place the
	// gameplay HUD exists to be repainted. Saved and restored so a nested open can't clear it.
	const bool wasOverHud = debugMenuOverHud;
	debugMenuOverHud = !center;

	// gameplay runs the mouse in relative mode; switch to absolute so the menu
	// can use the pointer, and restore on exit.
	const bool wasRelative = mouseGetRelative();
	mouseSetRelative(false);

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // mouse pointer sprites

	int off_x = 0, off_y = 0;
	if (center)
	{
		int menu_width = MIN(vga_width, LEGACY_WIDTH);
		off_x = (menu_width - DEBUG_MENU_WIDTH) / 2 - DEBUG_MENU_X;
		off_y = (vga_height - DEBUG_MENU_HEIGHT) / 2 - DEBUG_MENU_Y + 1;
	}

	dbgBuildVisibleRows();
	const int menuCount = dbgVisCount;
	int selected = dbgRowSnap(0);   // first real row, never the heading above it

#ifdef WITH_NETWORK
	// Baseline for the change test at close: only a real edit is worth putting on the wire.
	// Taken before the twiddle re-arm below, so that counts as an edit too; otherwise it
	// would quietly overwrite a twiddle the peer had published and never republish it.
	if (isNetworkGame)
		network_debug_sync_mark();
#endif

	/* transient debug-action values; persist across menu opens within a session */
	static int dbgSoundId = 1, dbgMusicId = 0, dbgTwiddleId = 0;
	debugTwiddleSpecial = (JE_byte)twiddle_special_id(dbgTwiddleId);  // keep the armed twiddle in sync

	/* Which player the LOADOUT rows (and Add Cash) act on, 0-based. Two-player games open on your
	 * own ship; across the network that is the one you flew in with; locally, player 1. Not
	 * remembered between opens: the whole menu reads wrong if you don't notice it is set to the
	 * other ship. */
	int dbgPlayer = 0;
	if (twoPlayerMode && isNetworkGame && thisPlayerNum >= 1 && thisPlayerNum <= COUNTOF(player))
		dbgPlayer = (int)thisPlayerNum - 1;

	/* Add Cash is an inline numeric field: while the row is selected you type a value (digits
	 * append, Backspace deletes) and Enter sets cash to it. Starts empty each open; the row shows
	 * the live cash when you're not on it. See the value display and input switch below. */
	const int cashMaxDigits = 12;              // CASH_MAX is 12 digits; the field can type the whole ceiling
	const Sint64 cashMax = CASH_MAX;
	char dbgCashStr[16] = "";
	char dbgHangStr[8] = "";                   // inline typed field for the Hang Watchdog row (seconds)

	wait_noinput(false, false, true);
	newkey = newmouse = false;  // don't let the click/key that opened us leak in

	/* Panel geometry. Spans the DEBUG_MENU_X..+WIDTH area (centred in the
	 * playfield) that the in-game caller saves and restores around this menu. */
	const int px0 = DEBUG_MENU_X + off_x, py0 = DEBUG_MENU_Y + off_y;
	const int px1 = DEBUG_MENU_X + DEBUG_MENU_WIDTH - 1 + off_x, py1 = vga_height - 5 + off_y;
	const int title_h = 15;                 /* height of the title strip      */
	const int items_top = py0 + title_h + 3;
	const int items_bottom = py1 - 18;      /* two footer lines: the row's help, then the keys */
	/* Fixed row density (matching the original ~19-row menu); the list scrolls
	 * when there are more items than fit, so rows never get squashed. */
	const int kVisibleTarget = 19;
	const int visibleRows = (menuCount < kVisibleTarget) ? menuCount : kVisibleTarget;
	const int row_h = (items_bottom - items_top) / visibleRows;
	const int mid_x = (px0 + px1) / 2;
	int scrollTop = 0;  /* index of the first visible row */
	int prev_mx = mouse_x, prev_my = mouse_y;  /* for motion-based hover */

	/* hue 15 is the grey/white ramp (palette indices 240..255), so these are
	 * safe, theme-neutral shades for the panel chrome. */
	enum {
		C_PANEL_BG = 0xF1,  /* body fill: near-black grey                 */
		C_TITLE_BG = 0xF3,  /* title strip, a touch lighter than the body */
		C_DIVIDER  = 0xF6,  /* line under the title                       */
		C_EDGE_HI  = 0xFB,  /* top/left border highlight                  */
		C_EDGE_LO  = 0xF4,  /* bottom/right border shade                  */
		C_SEL_BAR  = 0xF5   /* highlight bar behind the selected row      */
	};

	bool done = false;
	while (!done)
	{
		/* Solid panel, fully repainted every frame: nothing from the game
		 * behind can bleed through and no shading/text can accumulate. */
		fill_rectangle_xy(VGAScreen, px0, py0, px1, py1, C_PANEL_BG);

		/* beveled border */
		fill_rectangle_xy(VGAScreen, px0, py0, px1, py0, C_EDGE_HI); /* top    */
		fill_rectangle_xy(VGAScreen, px0, py0, px0, py1, C_EDGE_HI); /* left   */
		fill_rectangle_xy(VGAScreen, px0, py1, px1, py1, C_EDGE_LO); /* bottom */
		fill_rectangle_xy(VGAScreen, px1, py0, px1, py1, C_EDGE_LO); /* right  */

		/* title strip + heading */
		fill_rectangle_xy(VGAScreen, px0 + 1, py0 + 1, px1 - 1, py0 + title_h - 1, C_TITLE_BG);
		fill_rectangle_xy(VGAScreen, px0 + 1, py0 + title_h, px1 - 1, py0 + title_h, C_DIVIDER);
		draw_font_hv_shadow(VGAScreen, mid_x, py0 + 3, "DEBUG  MENU", normal_font, centered, 15, 4, true, 1);

		/* keep the selection within the scrolled window */
		if (selected < scrollTop)
			scrollTop = selected;
		else if (selected >= scrollTop + visibleRows)
			scrollTop = selected - visibleRows + 1;
		/* keep a heading on screen with the first row under it, so a selection never floats
		 * context-free at the top of the window */
		if (scrollTop > 0 && scrollTop == selected && dbgRowIsHeading(selected - 1))
			--scrollTop;
		if (scrollTop > menuCount - visibleRows)
			scrollTop = menuCount - visibleRows;
		if (scrollTop < 0)
			scrollTop = 0;

		for (int vis = 0; vis < visibleRows; ++vis)
		{
			int i = scrollTop + vis;
			int ry = items_top + vis * row_h;
			bool sel = (i == selected);

			if (dbgRowIsHeading(i))
			{
				const char *const heading = dbgRows[dbgVis[i]].heading;
				draw_font_hv_shadow(VGAScreen, px0 + 6, ry, heading, small_font, left_aligned, 15, 3, true, 1);
				const int rule_x = px0 + 10 + JE_textWidth(heading, small_font);
				if (rule_x < px1 - 9)
					fill_rectangle_xy(VGAScreen, rule_x, ry + 3, px1 - 9, ry + 3, C_DIVIDER);
				continue;
			}

			const int id = dbgRowId(i);
			const PlayerItems *const it = &player[dbgPlayer].items;
			char buf[40];
			bool invalid = false;
			switch (id)
			{
			case DBG_PLAYER:
				// Across the network, name which ship is yours; the two machines number the
				// players the same way, so "2" alone doesn't say whose gear you're about to rewrite.
				if (isNetworkGame && (int)thisPlayerNum == dbgPlayer + 1)
					snprintf(buf, sizeof(buf), "%d (you)", dbgPlayer + 1);
				else
					snprintf(buf, sizeof(buf), "%d", dbgPlayer + 1);
				break;
			case DBG_SHIP:
				if (it->ship <= SHIP_DRAGONWING)
					snprintf(buf, sizeof(buf), "%s", ships[it->ship].name);
				else if (it->ship >= 91 && it->ship <= 100 && extraAvailFor((uint)dbgPlayer))
					snprintf(buf, sizeof(buf), "Extra Ship %d", it->ship - 90);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->ship);
					invalid = true;
				}
				break;
			case DBG_FRONT_WEAPON:
				if (it->weapon[FRONT_WEAPON].id <= PORT_NUM)
					snprintf(buf, sizeof(buf), "%s", weaponPort[it->weapon[FRONT_WEAPON].id].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->weapon[FRONT_WEAPON].id);
					invalid = true;
				}
				break;
			case DBG_FRONT_POWER:
				snprintf(buf, sizeof(buf), "%d", it->weapon[FRONT_WEAPON].power);
				break;
			case DBG_REAR_WEAPON:
				if (it->weapon[REAR_WEAPON].id <= PORT_NUM)
					snprintf(buf, sizeof(buf), "%s", weaponPort[it->weapon[REAR_WEAPON].id].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->weapon[REAR_WEAPON].id);
					invalid = true;
				}
				break;
			case DBG_REAR_POWER:
				snprintf(buf, sizeof(buf), "%d", it->weapon[REAR_WEAPON].power);
				break;
			case DBG_SHIELD:
				if (it->shield <= SHIELD_NUM)
					snprintf(buf, sizeof(buf), "%s", shields[it->shield].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->shield);
					invalid = true;
				}
				break;
			case DBG_GENERATOR:
				if (it->generator <= POWER_NUM)
					snprintf(buf, sizeof(buf), "%s", powerSys[it->generator].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->generator);
					invalid = true;
				}
				break;
			case DBG_SIDEKICK_L:
				if (it->sidekick[LEFT_SIDEKICK] <= OPTION_NUM)
					snprintf(buf, sizeof(buf), "%s", options[it->sidekick[LEFT_SIDEKICK]].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->sidekick[LEFT_SIDEKICK]);
					invalid = true;
				}
				break;
			case DBG_SIDEKICK_R:
				if (it->sidekick[RIGHT_SIDEKICK] <= OPTION_NUM)
					snprintf(buf, sizeof(buf), "%s", options[it->sidekick[RIGHT_SIDEKICK]].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->sidekick[RIGHT_SIDEKICK]);
					invalid = true;
				}
				break;
			case DBG_SPECIAL:
				if (it->special <= SPECIAL_NUM)
					snprintf(buf, sizeof(buf), "%s", JE_specialName(it->special));
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->special);
					invalid = true;
				}
				break;
			case DBG_TWIDDLE:
			{
				const int sid = twiddle_special_id(dbgTwiddleId);
				if (sid >= 1 && sid <= SPECIAL_NUM)
					snprintf(buf, sizeof(buf), "%s", JE_specialName((JE_byte)sid));
				else
				{
					snprintf(buf, sizeof(buf), "#%d", dbgTwiddleId + 1);
					invalid = true;
				}
				break;
			}
			case DBG_AUTOFIRE_TWIDDLE:
				sprintf(buf, "%s", debugAutofireTwiddle ? "ON" : "OFF");
				break;
			case DBG_TOGGLE_FIRE:
				sprintf(buf, "%s", debugToggleFire ? "ON" : "OFF");
				break;
			case DBG_AUTOFIRE_SPECIAL:
				sprintf(buf, "%s", autoFireSpecial ? "ON" : "OFF");
				break;
			case DBG_AUTOFIRE_CHARGE:
			{
				static const char *const modes[CHARGE_AUTOFIRE_NUM] = { "No", "Yes", "Fully charged only", "Yes (fastest)" };
				snprintf(buf, sizeof(buf), "%s", modes[chargeSidekickAutofire % CHARGE_AUTOFIRE_NUM]);
				break;
			}
			case DBG_INSTANT_CHARGE:
				sprintf(buf, "%s", cheatInstantCharge ? "ON" : "OFF");
				break;
			case DBG_INF_SIDEKICK_AMMO:
				sprintf(buf, "%s", cheatInfiniteSidekickAmmo ? "ON" : "OFF");
				break;
			case DBG_INF_GENERATOR:
				sprintf(buf, "%s", cheatInfiniteGenerator ? "ON" : "OFF");
				break;
			case DBG_GOD_MODE:
				snprintf(buf, sizeof(buf), "%s", debug_god_mode_names[debug_god_mode_get()]);
				break;
			case DBG_NOCLIP:
			{
				static const char *const modes[NOCLIP_NUM] = { "OFF", "ON", "ON (Transparent)" };
				snprintf(buf, sizeof(buf), "%s", modes[noclipMode % NOCLIP_NUM]);
				break;
			}
			case DBG_EXPERT_MODE:
				sprintf(buf, "%s", expertMode ? "ON" : "OFF");
				break;
			case DBG_EXPERT_SETTINGS:
				sprintf(buf, "%s", ">>");  // drill-in to Expert Settings submenu
				break;
			case DBG_AUTO_DIFFICULTY:
				sprintf(buf, "%s", difficultyAdjust ? "ON" : "OFF");
				break;
			case DBG_DIFFICULTY:
				snprintf(buf, sizeof(buf), "%s", difficultyNameB[difficultyLevel]);
				break;
			case DBG_ADD_CASH:  // editable: type a value while selected, Enter sets cash to it
				if (sel)
					snprintf(buf, sizeof(buf), "%s|", dbgCashStr);  // your input + '|' caret (empty => just the caret)
				else
					snprintf(buf, sizeof(buf), "%lld", (long long)player[dbgPlayer].cash);  // live cash when not editing
				break;
			case DBG_NO_ENEMY_FIRE:
				sprintf(buf, "%s", cheatNoEnemyFire ? "ON" : "OFF");
				break;
			case DBG_SKIP_LEVEL:  // action
				sprintf(buf, "%s", "[Enter]");
				break;
			case DBG_PLAY_SOUND:  // Left/Right id, Enter plays
				snprintf(buf, sizeof(buf), "%d", dbgSoundId);
				break;
			case DBG_PLAY_MUSIC:  // Left/Right id, Enter plays
				snprintf(buf, sizeof(buf), "%d", dbgMusicId);
				break;
			case DBG_SPRITE_VIEWER:
				sprintf(buf, "%s", ">>");  // drill-in to Sprite Viewer submenu
				break;
			case DBG_HITBOX:
				sprintf(buf, "%s", debugHitboxOverlay ? "ON" : "OFF");
				break;
			case DBG_PERF:
				sprintf(buf, "%s", debugPerfOverlay ? "ON" : "OFF");
				break;
			case DBG_HANG_TIMEOUT:  // editable: type seconds while selected, Enter applies (clamped)
				if (sel)
					snprintf(buf, sizeof(buf), "%s|", dbgHangStr);  // your input + '|' caret
				else
					snprintf(buf, sizeof(buf), "%ds", crashlog_get_hang_timeout());  // live value
				break;
			case DBG_ENDLESS_FX:
				// Endless runs always enable this layer, so report its fixed state.
				if (endlessMode)
					sprintf(buf, "%s", "ENDLESS");
				else
					sprintf(buf, "%s", endlessCampaignMods ? "ON" : "OFF");
				break;
			case DBG_ENDLESS_TUNE:
			{
				// Summarise what the layer is actually carrying, so the panel's state is visible
				// without opening it: mods, perk stacks and pinned levers.
				int perks = 0;
				for (int p = 0; p < endlessPerkCount(); ++p)
					perks += endlessPerkGetOwned(p);
				const int pins = endlessScalingOverrideCount();
				const int mods = endlessPopCount64(endlessActiveMods);
				if (mods || perks || pins)
					snprintf(buf, sizeof(buf), "%dm %dp %dx", mods, perks, pins);
				else
					sprintf(buf, "%s", ">>");
				break;
			}
			case DBG_ROLLBACK_SELFTEST:
				// Show the running failure count while verification is armed.
				if (!rollback_selftest)
					sprintf(buf, "%s", "OFF");
				else if (endlessFxActive())
					sprintf(buf, "%s", "ON (idle: endless)");  // the effect layer is outside the registry
				else if (rollback_selftest_failures > 0)
					snprintf(buf, sizeof(buf), "ON  %lu FAIL", rollback_selftest_failures);
				else if (rollback_selftest_ticks > 0)
					snprintf(buf, sizeof(buf), "ON  %lu ok", rollback_selftest_ticks);
				else
					sprintf(buf, "%s", "ON");
				break;
			case DBG_FORCE_CRASH:  // Crash-logger test action.
				sprintf(buf, "%s", "[Enter]");
				break;
			default:
				buf[0] = '\0';
				break;
			}

			/* trim trailing whitespace */
			for (int j = (int)strlen(buf) - 1; j >= 0 && isspace((unsigned char)buf[j]); --j)
				buf[j] = '\0';

			/* highlight bar behind the active row */
			if (sel)
				fill_rectangle_xy(VGAScreen, px0 + 3, ry - 1, px1 - 3, ry + row_h - 2, C_SEL_BAR);

			/* label (left) */
			draw_font_hv_shadow(VGAScreen, px0 + 14, ry, dbgLabel[id], small_font, left_aligned,
			                    15, sel ? 5 : -1, true, 1);

			/* value (right); dim plain "OFF", red for invalid entries */
			Sint8 val_bright = sel ? 5 : 0;
			if (!invalid && strcmp(buf, "OFF") == 0)
				val_bright -= 4;
			draw_font_hv_shadow(VGAScreen, px1 - 9, ry, buf, small_font, right_aligned,
			                    invalid ? 4 : 15, val_bright, true, 1);

			/* selection marker */
			if (sel)
				draw_font_hv(VGAScreen, px0 + 5, ry, ">", small_font, left_aligned, 15, 6);
		}

		/* scrollbar track + thumb (only when the list overflows the window) */
		if (menuCount > visibleRows)
		{
			const int track_top = items_top - 1;
			const int track_bot = items_top + visibleRows * row_h - 2;
			const int track_h = track_bot - track_top;
			fill_rectangle_xy(VGAScreen, px1 - 4, track_top, px1 - 3, track_bot, C_EDGE_LO);

			int thumb_h = track_h * visibleRows / menuCount;
			if (thumb_h < 4)
				thumb_h = 4;
			const int denom = menuCount - visibleRows;
			const int thumb_y = track_top + (denom > 0 ? (track_h - thumb_h) * scrollTop / denom : 0);
			fill_rectangle_xy(VGAScreen, px1 - 4, thumb_y, px1 - 3, thumb_y + thumb_h, C_EDGE_HI);
		}

		/* Footer line 1: what the selected row DOES; 34 cheats and diagnostics is far too many
		 * to carry their meaning in the label alone. Line 2: the keys, named for the platform. */
		const int shownId = dbgRowId(selected);
		draw_font_hv(VGAScreen, mid_x, py1 - 17, dbgHelp[shownId], small_font, centered, 15, 1);
#if defined(__SWITCH__) || defined(__vita__)
		draw_font_hv(VGAScreen, mid_x, py1 - 8,
		             (shownId == DBG_ADD_CASH || shownId == DBG_HANG_TIMEOUT)
		                 ? "A types a number   B close"
		                 : "Left/Right change   A use   B close   L/R page",
		             small_font, centered, 15, -3);
#else
		draw_font_hv(VGAScreen, mid_x, py1 - 8,
		             (shownId == DBG_ADD_CASH || shownId == DBG_HANG_TIMEOUT)
		                 ? "Type a number   Enter set   Esc close"
		                 : "Left/Right change   Enter use   Esc close",
		             small_font, centered, 15, -3);
#endif

		// A scrolling list, so a tap only reaches the rows already drawn: this screen keeps
		// the cursor keys.
		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		// The whole game stops while this panel is open, including the packet pump the peer's
		// liveness test reads. Without this, the peer declares a disconnect
		// after NET_TIME_OUT and halts the game.
		NETWORK_KEEP_ALIVE();

#if defined(__SWITCH__) || defined(__vita__)
		// The shoulder buttons page the list, read raw and synthesized into PageUp/PageDown; same
		// pattern as the endless debug jump screen (game_menu.c); see the note there.
		{
#if defined(__SWITCH__)
			static const int shoulder_btn[2] = { 6, 7 };  // switch-sdl2: 6 = L, 7 = R
#else
			static const int shoulder_btn[2] = { 4, 5 };  // Vita: 4 = L, 5 = R
#endif
			static bool shoulder_was[2];
			for (int b = 0; b < 2; ++b)
			{
				const bool down = joysticks > 0 && joystick[0].handle != NULL &&
				                  SDL_JoystickGetButton(joystick[0].handle, shoulder_btn[b]) != 0;
				if (down && !shoulder_was[b] && !newkey)
				{
					newkey = true;
					lastkey_scan = (b == 0) ? SDL_SCANCODE_PAGEUP : SDL_SCANCODE_PAGEDOWN;
				}
				shoulder_was[b] = down;
			}
		}
#endif

		/* wheel scrolls the selection; hover highlights on pointer motion; a click
		 * acts on the row (left = activate/advance, right = reverse, Enter rows drill in) */
		{
			if (mouse_scroll != 0)
			{
				const int dir = (mouse_scroll > 0) ? -1 : 1;
				for (int n = (mouse_scroll > 0) ? mouse_scroll : -mouse_scroll; n > 0; --n)
					selected = dbgRowStep(selected, dir);
				mouse_scroll = 0;
			}
			if (mouse_x != prev_mx || mouse_y != prev_my)
			{
				const int hov = panel_row_at(mouse_x, mouse_y, px0, px1, items_top,
				                             row_h, visibleRows, scrollTop, menuCount);
				if (hov >= 0 && !dbgRowIsHeading(hov))
					selected = hov;
			}
			prev_mx = mouse_x;
			prev_my = mouse_y;
			if (newmouse)
			{
				const int r = panel_row_at(lastmouse_x, lastmouse_y, px0, px1, items_top,
				                           row_h, visibleRows, scrollTop, menuCount);
				if (r >= 0 && !dbgRowIsHeading(r))
				{
					const int rid = dbgRowId(r);
					selected = r;
					const bool enterRow = (rid == DBG_EXPERT_SETTINGS || rid == DBG_ADD_CASH ||
					                       rid == DBG_SKIP_LEVEL || rid == DBG_PLAY_SOUND ||
					                       rid == DBG_PLAY_MUSIC || rid == DBG_SPRITE_VIEWER ||
					                       rid == DBG_TWIDDLE || rid == DBG_FORCE_CRASH ||
					                       rid == DBG_ENDLESS_TUNE || rid == DBG_HANG_TIMEOUT);
					newkey = true;
					lastkey_scan = (lastmouse_but == SDL_BUTTON_RIGHT) ? SDL_SCANCODE_LEFT
					             : (enterRow ? SDL_SCANCODE_RETURN : SDL_SCANCODE_RIGHT);
				}
				newmouse = false;
			}
		}

		if (newkey)
		{
			// Read the row id HERE, not with the footer above: a click this same frame moves
			// `selected` and then synthesizes the key, so the id must follow that move.
			const int selId = dbgRowId(selected);
			// For the loadout refresh below: only an actual hull SWAP may re-armor the player, so
			// compare rather than assume the Ship row was touched (Left at id 0 changes nothing).
			const int editPlayer = dbgPlayer;   // the Edit Player row may move the selector below
			PlayerItems *const edit = &player[editPlayer].items;
			const JE_byte shipBefore = edit->ship;
			const bool editKey = (lastkey_scan == SDL_SCANCODE_LEFT || lastkey_scan == SDL_SCANCODE_RIGHT ||
			                      lastkey_scan == SDL_SCANCODE_RETURN || lastkey_scan == SDL_SCANCODE_KP_ENTER ||
			                      lastkey_scan == SDL_SCANCODE_SPACE);
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				selected = dbgRowStep(selected, -1);
				break;
			case SDL_SCANCODE_DOWN:
				selected = dbgRowStep(selected, 1);
				break;
			case SDL_SCANCODE_PAGEUP:
				selected = dbgRowSnap(selected - visibleRows);
				break;
			case SDL_SCANCODE_PAGEDOWN:
				selected = dbgRowSnap(selected + visibleRows);
				break;
			case SDL_SCANCODE_HOME:
				selected = dbgRowSnap(0);
				break;
			case SDL_SCANCODE_END:
				selected = dbgRowSnap(menuCount - 1);
				break;
			case SDL_SCANCODE_LEFT:
				switch (selId)
				{
				case DBG_PLAYER: dbgPlayer = (dbgPlayer + (int)COUNTOF(player) - 1) % (int)COUNTOF(player); break;
				case DBG_SHIP:
					// Extra ships occupy IDs 91 through 100.
					if (edit->ship > 91) --edit->ship;
					else if (edit->ship == 91) edit->ship = SHIP_DRAGONWING;
					else if (edit->ship > 0) --edit->ship;
					if (edit->ship > 90)
						endlessNoteCustomShip();
					break;
				case DBG_FRONT_WEAPON: if (edit->weapon[FRONT_WEAPON].id > 0) --edit->weapon[FRONT_WEAPON].id; break;
				case DBG_FRONT_POWER: if (edit->weapon[FRONT_WEAPON].power > 1) --edit->weapon[FRONT_WEAPON].power; break;
				case DBG_REAR_WEAPON: if (edit->weapon[REAR_WEAPON].id > 0) --edit->weapon[REAR_WEAPON].id; break;
				case DBG_REAR_POWER: if (edit->weapon[REAR_WEAPON].power > 1) --edit->weapon[REAR_WEAPON].power; break;
				case DBG_SHIELD: if (edit->shield > 0) --edit->shield; break;
				case DBG_GENERATOR: if (edit->generator > 0) --edit->generator; break;
				case DBG_SIDEKICK_L: if (edit->sidekick[LEFT_SIDEKICK] > 0) --edit->sidekick[LEFT_SIDEKICK]; break;
				case DBG_SIDEKICK_R: if (edit->sidekick[RIGHT_SIDEKICK] > 0) --edit->sidekick[RIGHT_SIDEKICK]; break;
				case DBG_SPECIAL:  // step to the previous crash-safe special (skip bad-icon slots)
					for (int nid = (int)edit->special - 1; nid >= 0; --nid)
						if (debug_special_is_safe(nid)) { edit->special = (JE_byte)nid; break; }
					break;
				case DBG_TWIDDLE:
					dbgTwiddleId = (dbgTwiddleId + (int)COUNTOF(keyboardCombos) - 1) % (int)COUNTOF(keyboardCombos);
					debugTwiddleSpecial = (JE_byte)twiddle_special_id(dbgTwiddleId);
					break;
				case DBG_AUTOFIRE_TWIDDLE: debugAutofireTwiddle = !debugAutofireTwiddle; break;
				case DBG_TOGGLE_FIRE: debugToggleFire = !debugToggleFire; debugToggleFireActive = false; break;
				case DBG_AUTOFIRE_SPECIAL: autoFireSpecial = !autoFireSpecial; break;
				case DBG_AUTOFIRE_CHARGE: chargeSidekickAutofire = (chargeSidekickAutofire + CHARGE_AUTOFIRE_NUM - 1) % CHARGE_AUTOFIRE_NUM; break;
				case DBG_INSTANT_CHARGE: cheatInstantCharge = !cheatInstantCharge; break;
				case DBG_INF_SIDEKICK_AMMO: cheatInfiniteSidekickAmmo = !cheatInfiniteSidekickAmmo; break;
				case DBG_INF_GENERATOR: cheatInfiniteGenerator = !cheatInfiniteGenerator; break;
				case DBG_GOD_MODE: debug_god_mode_set((debug_god_mode_get() + 3) % 4); break;
				case DBG_NOCLIP: noclipMode = (noclipMode + NOCLIP_NUM - 1) % NOCLIP_NUM; break;
				case DBG_EXPERT_MODE: expertMode = !expertMode; break;
				case DBG_EXPERT_SETTINGS: break;  // opens on Right/Enter
				case DBG_AUTO_DIFFICULTY: difficultyAdjust = !difficultyAdjust; break;
				case DBG_DIFFICULTY: if (difficultyLevel > DIFFICULTY_WIMP) --difficultyLevel; break;
				case DBG_ADD_CASH:
					player_set_cash(&player[editPlayer], cashMax);
					endlessCashDebugOverwrite();
					snprintf(dbgCashStr, sizeof(dbgCashStr), "%lld", (long long)cashMax);
					break;
				case DBG_NO_ENEMY_FIRE: cheatNoEnemyFire = !cheatNoEnemyFire; break;
				case DBG_PLAY_SOUND: if (dbgSoundId > 1) --dbgSoundId; break;
				case DBG_PLAY_MUSIC: if (dbgMusicId > 0) --dbgMusicId; break;
				case DBG_SPRITE_VIEWER: break;  // opens on Right/Enter
				// The layer is already active during an Endless run; changing the flag there
				// would desynchronize the row's "ENDLESS" readout.
				case DBG_ENDLESS_FX: debug_toggle_campaign_mods(); break;
				case DBG_ENDLESS_TUNE: break;  // opens on Right/Enter
				case DBG_HITBOX: debugHitboxOverlay = !debugHitboxOverlay; break;
				case DBG_PERF: debugPerfOverlay = !debugPerfOverlay; break;
				case DBG_ROLLBACK_SELFTEST: debug_toggle_rollback_selftest(); break;
				default: break;  // Hang Watchdog / Skip Level are Enter-only actions
				}
				break;
			case SDL_SCANCODE_RIGHT:
				switch (selId)
				{
				// Each of these indexes an array sized [X_NUM + 1], so stepping past X_NUM is an
				// out-of-bounds read when the item is first inspected, which is where the
				// garbage ship graphics came from. Clamp at the top the way Left already does at 0.
				case DBG_PLAYER: dbgPlayer = (dbgPlayer + 1) % (int)COUNTOF(player); break;
				case DBG_SHIP:
					if (edit->ship < SHIP_DRAGONWING) ++edit->ship;
					else if (edit->ship == SHIP_DRAGONWING &&
					         extraShipsAllowedInGame() && extraAvailFor((uint)editPlayer)) edit->ship = 91;
					else if (extraShipsAllowedInGame() &&
					         edit->ship >= 91 && edit->ship < 100) ++edit->ship;
					if (edit->ship > 90)
						endlessNoteCustomShip();
					break;
				case DBG_FRONT_WEAPON: if (edit->weapon[FRONT_WEAPON].id < PORT_NUM) ++edit->weapon[FRONT_WEAPON].id; break;
				case DBG_FRONT_POWER: if (edit->weapon[FRONT_WEAPON].power < 11) ++edit->weapon[FRONT_WEAPON].power; break;
				case DBG_REAR_WEAPON: if (edit->weapon[REAR_WEAPON].id < PORT_NUM) ++edit->weapon[REAR_WEAPON].id; break;
				case DBG_REAR_POWER: if (edit->weapon[REAR_WEAPON].power < 11) ++edit->weapon[REAR_WEAPON].power; break;
				case DBG_SHIELD: if (edit->shield < SHIELD_NUM) ++edit->shield; break;
				case DBG_GENERATOR: if (edit->generator < POWER_NUM) ++edit->generator; break;
				case DBG_SIDEKICK_L: if (edit->sidekick[LEFT_SIDEKICK] < OPTION_NUM) ++edit->sidekick[LEFT_SIDEKICK]; break;
				case DBG_SIDEKICK_R: if (edit->sidekick[RIGHT_SIDEKICK] < OPTION_NUM) ++edit->sidekick[RIGHT_SIDEKICK]; break;
				case DBG_SPECIAL:  // step to the next crash-safe special (skip bad-icon slots)
					for (int nid = (int)edit->special + 1; nid <= SPECIAL_NUM; ++nid)
						if (debug_special_is_safe(nid)) { edit->special = (JE_byte)nid; break; }
					break;
				case DBG_TWIDDLE:
					dbgTwiddleId = (dbgTwiddleId + 1) % (int)COUNTOF(keyboardCombos);
					debugTwiddleSpecial = (JE_byte)twiddle_special_id(dbgTwiddleId);
					break;
				case DBG_AUTOFIRE_TWIDDLE: debugAutofireTwiddle = !debugAutofireTwiddle; break;
				case DBG_TOGGLE_FIRE: debugToggleFire = !debugToggleFire; debugToggleFireActive = false; break;
				case DBG_AUTOFIRE_SPECIAL: autoFireSpecial = !autoFireSpecial; break;
				case DBG_AUTOFIRE_CHARGE: chargeSidekickAutofire = (chargeSidekickAutofire + 1) % CHARGE_AUTOFIRE_NUM; break;
				case DBG_INSTANT_CHARGE: cheatInstantCharge = !cheatInstantCharge; break;
				case DBG_INF_SIDEKICK_AMMO: cheatInfiniteSidekickAmmo = !cheatInfiniteSidekickAmmo; break;
				case DBG_INF_GENERATOR: cheatInfiniteGenerator = !cheatInfiniteGenerator; break;
				case DBG_GOD_MODE: debug_god_mode_set((debug_god_mode_get() + 1) % 4); break;
				case DBG_NOCLIP: noclipMode = (noclipMode + 1) % NOCLIP_NUM; break;
				case DBG_EXPERT_MODE: expertMode = !expertMode; break;
				case DBG_EXPERT_SETTINGS: JE_expertSettingsMenu(off_x, off_y); break;
				case DBG_AUTO_DIFFICULTY: difficultyAdjust = !difficultyAdjust; break;
				case DBG_DIFFICULTY: if (difficultyLevel < DIFFICULTY_10) ++difficultyLevel; break;
				case DBG_ADD_CASH:
					player_set_cash(&player[editPlayer], cashMax);
					endlessCashDebugOverwrite();
					snprintf(dbgCashStr, sizeof(dbgCashStr), "%lld", (long long)cashMax);
					break;
				case DBG_NO_ENEMY_FIRE: cheatNoEnemyFire = !cheatNoEnemyFire; break;
				case DBG_PLAY_SOUND: if (dbgSoundId < SOUND_COUNT) ++dbgSoundId; break;
				case DBG_PLAY_MUSIC: if (dbgMusicId < MUSIC_NUM - 1) ++dbgMusicId; break;
				case DBG_SPRITE_VIEWER: JE_spriteViewer(off_x, off_y); break;
				case DBG_ENDLESS_FX: debug_toggle_campaign_mods(); break;
				case DBG_ENDLESS_TUNE: endlessDebugTuneScreen(); break;
				case DBG_HITBOX: debugHitboxOverlay = !debugHitboxOverlay; break;
				case DBG_PERF: debugPerfOverlay = !debugPerfOverlay; break;
				case DBG_ROLLBACK_SELFTEST: debug_toggle_rollback_selftest(); break;
				default: break;  // Hang Watchdog / Skip Level are Enter-only actions
				}
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				switch (selId)
				{
				case DBG_EXPERT_SETTINGS:  // drill-in
					JE_expertSettingsMenu(off_x, off_y);
					break;
				case DBG_SPRITE_VIEWER:  // drill-in
					JE_spriteViewer(off_x, off_y);
					break;
				case DBG_ENDLESS_TUNE:  // drill-in: sector mods, personal buffs, perks, zone scaling
					endlessDebugTuneScreen();
					break;
				case DBG_TWIDDLE:  // request a one-shot fire of the selected twiddle's special
				{
					debugTwiddleSpecial = (JE_byte)twiddle_special_id(dbgTwiddleId);
					if (debugTwiddleSpecial > 0)
					{
						debugTwiddleTrigger = true;
						done = true;  // close the menu so it fires when gameplay resumes
					}
					break;
				}
				// Declare debug wallet overwrites without classifying them as earned income.
				case DBG_ADD_CASH:
#ifdef PLATFORM_HANDHELD
					// No physical keyboard here: pop the software keyboard to fill the field.
					console_swkbd(dbgCashStr, sizeof(dbgCashStr), cashMaxDigits, dbgCashStr, "Add Cash", true);
#endif
					if (dbgCashStr[0])  // ignore a bare Enter on an empty field (don't zero cash by accident)
					{
						Sint64 v = 0;
						for (const char *c = dbgCashStr; *c >= '0' && *c <= '9'; ++c)
							v = v * 10 + (*c - '0');
						player_set_cash(&player[editPlayer], v);
						endlessCashDebugOverwrite();
					}
					break;
				case DBG_HANG_TIMEOUT:  // apply the typed watchdog timeout in seconds (clamps to range)
#ifdef PLATFORM_HANDHELD
					console_swkbd(dbgHangStr, sizeof(dbgHangStr), sizeof(dbgHangStr) - 1, dbgHangStr, "Hang timeout (seconds)", true);
#endif
					if (dbgHangStr[0])  // ignore a bare Enter on an empty field
					{
						int v = 0;
						for (const char *c = dbgHangStr; *c >= '0' && *c <= '9'; ++c)
							v = v * 10 + (*c - '0');
						crashlog_set_hang_timeout(v);  // clamps into [MIN, MAX]
						dbgHangStr[0] = '\0';  // committed -> clear so the row shows the applied (clamped) value
					}
					break;
				case DBG_SKIP_LEVEL:  // flag it and close so the game processes it
					// In a network game the level may only end on both machines at once, so ask
					// through the request bit both sims consume on the same frame (RB_REQ_SKIPLEVEL)
					// rather than tearing this machine's level down on its own.
					if (isNetworkGame && !center)
						skipLevelRequest = true;
					else
						reallyEndLevel = true;
					done = true;
					break;
				case DBG_FORCE_CRASH:
					debug_force_crash();
					break;
				case DBG_PLAY_SOUND:  // Play the selected sound sample
					JE_playSampleNum((JE_byte)dbgSoundId);
					break;
				case DBG_PLAY_MUSIC:  // Play the selected music track
					play_song((unsigned int)dbgMusicId);
					break;
				default:
					break;
				}
				break;
			case SDL_SCANCODE_ESCAPE:
				done = true;
				break;
			default:
			{
				/* Inline typed numeric fields (Add Cash, Hang Watchdog): digits append,
				 * Backspace/Delete removes. Read scancodes directly; this menu's event
				 * pump can drop SDL_TEXTINPUT. */
				char *editStr = (selId == DBG_ADD_CASH)     ? dbgCashStr
				              : (selId == DBG_HANG_TIMEOUT) ? dbgHangStr
				                                            : NULL;
				const int editMax = (selId == DBG_ADD_CASH) ? cashMaxDigits : 4;
				if (editStr != NULL)
				{
					int digit = -1;
					if (lastkey_scan >= SDL_SCANCODE_1 && lastkey_scan <= SDL_SCANCODE_9)
						digit = lastkey_scan - SDL_SCANCODE_1 + 1;
					else if (lastkey_scan == SDL_SCANCODE_0)
						digit = 0;
					else if (lastkey_scan >= SDL_SCANCODE_KP_1 && lastkey_scan <= SDL_SCANCODE_KP_9)
						digit = lastkey_scan - SDL_SCANCODE_KP_1 + 1;
					else if (lastkey_scan == SDL_SCANCODE_KP_0)
						digit = 0;

					if (digit >= 0)
					{
						size_t l = strlen(editStr);
						if (strcmp(editStr, "0") == 0)  // replace a lone leading zero
							l = 0;
						if ((int)l < editMax)
						{
							editStr[l] = (char)('0' + digit);
							editStr[l + 1] = '\0';
						}
					}
					else if (lastkey_scan == SDL_SCANCODE_BACKSPACE || lastkey_scan == SDL_SCANCODE_DELETE)
					{
						size_t l = strlen(editStr);
						if (l > 0)
							editStr[l - 1] = '\0';
					}
				}
				break;
			}
			}

			// One place, after every handler: a loadout row may have just rewritten the edited
			// player's items, and the engine caches far too much off those to leave it until the
			// next level start.
			if (editKey && dbgRowIsLoadout(selId))
				debug_apply_loadout_change(editPlayer, edit->ship != shipBefore, false);

			newkey = false;
		}
	}

#ifdef WITH_NETWORK
	// Publish whatever was changed: every loadout row, cheat and difficulty here is simulation
	// state, so an edit the peer never hears about leaves the two machines playing different
	// games.
	if (isNetworkGame)
		network_debug_sync_send();
#endif

	// Nothing fades on the way out of here, so the cursor keys would sit over the screen
	// underneath until the request went stale. Drop them with the panel.
	touch_ui_clear_layout();

	mouseSetRelative(wasRelative);
	debugMenuOverHud = wasOverHud;

	VGAScreen = temp_surface;
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
	JE_wipeKey();

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

void JE_highScoreCheck(void)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprite

	Sint32 temp_score;

	for (int temp_p = 0; temp_p < (twoPlayerMode ? 2 : 1); ++temp_p)
	{
		JE_sortHighScores();

		int p = temp_p;
		int table = 10 + (initial_episode_num - 1) * 2;

		if (timedBattleMode)
		{
			// timed battle score is just money
			temp_score = player[0].cash;
			table = timeBattleSelection - 1;
		}
		else if (twoPlayerMode)
		{
			// ask for the highest scorer first
			if (player[0].cash < player[1].cash)
				p = (temp_p == 0) ? 1 : 0;

			temp_score = (p == 0) ? player[0].cash : player[1].cash;
			++table;
		}
		else
		{
			// single player highscore includes cost of upgrades
			temp_score = JE_totalScore(&player[0]);
		}

		int slot;
		for (slot = 0; slot < 3; ++slot)
		{
			if (temp_score > t2kHighScores[table][slot].score)
				break;
		}

		// Check for a high score.
		if (slot < 3)
		{
			// shift down old scores
			for (int i = 2; i > slot; --i)
				memcpy(&t2kHighScores[table][i], &t2kHighScores[table][i - 1], sizeof(T2KHighScoreType));

			wait_noinput(false, true, false);

			JE_clr256(VGAScreen);
			JE_showVGA();
			memcpy(colors, palettes[0], sizeof(colors));

			if (!timedBattleMode) // Doesn't play this music
				play_song(33);

			// Not part of the above condition
			{
				/* Enter Thy name */

				JE_byte flash = 8 * 16 + 10;
				JE_boolean fadein = true;
				JE_boolean quit = false, cancel = false;
				char stemp[30], tempstr[30];
				char buffer[256];

				// Absolute pointer for the on-screen OK/CANCEL buttons here too (same as
				// JE_operation): a Switch touch must click, not steer the ship. Restored
				// after the entry loop below.
				const bool hs_was_relative = mouseGetRelative();
				mouseSetRelative(false);

				strcpy(stemp, "                             ");
				temp = 0;

#ifdef PLATFORM_HANDHELD
				// No physical keyboard here: get the name from the software keyboard
				// and fill the field directly (see JE_operation for why not injected as an event).
				{
					char kb[29];
					kb[0] = '\0';
					if (console_swkbd(kb, sizeof(kb), 28, NULL, "Enter your name", false))
					{
						for (const char *c = kb; *c != '\0' && temp < 28; ++c)
						{
							const char u = (unsigned char)*c <= 127U ? (char)toupper((unsigned char)*c) : 0;
							if (u == ' ' || font_ascii[(unsigned char)u] != -1)
								stemp[temp++] = u;
						}
					}
				}
#endif

				// As astoundingly ugly as this makes the shade below look, this is in fact what Tyrian 2000 does.
				if (timedBattleMode)
					JE_loadPic(VGAScreen, 13, false);

				JE_barShade(VGAScreen, 65, 55, 255, 155);

				do
				{
					service_SDL_events(true);

					JE_dString(VGAScreen, JE_fontCenter(miscText[51], FONT_SHAPES), 3, miscText[51], FONT_SHAPES);

					temp3 = twoPlayerMode ? 58 + p : 53;

					JE_dString(VGAScreen, JE_fontCenter(miscText[temp3-1], SMALL_FONT_SHAPES), 30, miscText[temp3-1], SMALL_FONT_SHAPES);

					blit_sprite(VGAScreenSeg, 50, 50, OPTION_SHAPES, 35);  // message box

					if (twoPlayerMode)
					{
						sprintf(buffer, "%s %s", miscText[48 + p], miscText[53]);
						JE_textShade(VGAScreen, 60, 55, buffer, 11, 4, FULL_SHADE);
					}
					else
					{
						JE_textShade(VGAScreen, 60, 55, miscText[53], 11, 4, FULL_SHADE);
					}

					sprintf(buffer, "%s %lld", miscText[37], (long long)temp_score);
					JE_textShade(VGAScreen, 70, 70, buffer, 11, 4, FULL_SHADE);

					do
					{
						flash = (flash == 8 * 16 + 10) ? 8 * 16 + 2 : 8 * 16 + 10;
						temp3 = (temp3 == 6) ? 2 : 6;

						strncpy(tempstr, stemp, temp);
						tempstr[temp] = '\0';
						JE_outText(VGAScreen, 65, 89, tempstr, 8, 3);
						tempW = 65 + JE_textWidth(tempstr, TINY_FONT);
						JE_barShade(VGAScreen, tempW + 2, 90, tempW + 6, 95);
						fill_rectangle_xy(VGAScreen, tempW + 1, 89, tempW + 5, 94, flash);

						for (int i = 0; i < 14; i++)
						{
							setDelay(1);

							JE_mouseStart();
							JE_showVGA();
							if (fadein)
							{
								fade_palette(colors, 15, 0, 255);
								fadein = false;
							}
							JE_mouseReplace();

							push_joysticks_as_keyboard();
							service_wait_delay();

							if (newkey || newmouse)
								break;
						}

					} while (!newkey && !newmouse && !new_text);

					if (!playing)
						play_song(31);

					if (mouseButton > 0)
					{
						if (mouseX > 56 && mouseX < 142 && mouseY > 123 && mouseY < 149)
						{
							quit = true;
						}
						else if (mouseX > 151 && mouseX < 237 && mouseY > 123 && mouseY < 149)
						{
							quit = true;
							cancel = true;
						}
					}
					else if (new_text)
					{
						for (size_t ti = 0U; last_text[ti] != '\0'; ++ti)
						{
							const char c = (unsigned char)last_text[ti] <= 127U ? toupper(last_text[ti]) : 0;
							if ((c == ' ' || font_ascii[(unsigned char)c] != -1) &&
							    temp < 28)
							{
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
								}
								break;
							case SDL_SCANCODE_ESCAPE:
								quit = true;
								cancel = true;
								break;
							case SDL_SCANCODE_RETURN:
								quit = true;
								break;
							default:
								break;
						}
					}
				} while (!quit);

				mouseSetRelative(hs_was_relative);

				// Timed Battle mode doesn't allow cancelling, so we ignore it
				if (!cancel || timedBattleMode)
				{
					t2kHighScores[table][slot].score = temp_score;
					strcpy(t2kHighScores[table][slot].playerName, stemp);
					t2kHighScores[table][slot].difficulty = difficultyLevel;
				}

				fade_black(15);
				JE_loadPic(VGAScreen, 2, false);

				JE_dString(VGAScreen, JE_fontCenter(miscText[50], FONT_SHAPES), 10, miscText[50], FONT_SHAPES);

				if (timedBattleMode)
					JE_dString(VGAScreen, JE_fontCenter(timed_battle_name[timeBattleSelection], SMALL_FONT_SHAPES), 35, timed_battle_name[timeBattleSelection], SMALL_FONT_SHAPES);
				else
					JE_dString(VGAScreen, JE_fontCenter(episode_name[episodeNum], SMALL_FONT_SHAPES), 35, episode_name[episodeNum], SMALL_FONT_SHAPES);

				for (int i = 0; i < 3; ++i)
				{
					if (i != slot)
					{
						sprintf(buffer, "~#%d:~  %lld", i+1, (long long)t2kHighScores[table][i].score);
						JE_textShade(VGAScreen,  20, (i * 12) + 65, buffer, 15, 0, FULL_SHADE);
						JE_textShade(VGAScreen, 150, (i * 12) + 65, t2kHighScores[table][i].playerName, 15, 2, FULL_SHADE);
					}
				}

				JE_showVGA();

				fade_palette(colors, 15, 0, 255);

				// A surviving press would zero frameCountMax in the glow and skip the wait.
				wait_noinput(true, true, true);
				newkey = newmouse = false;

				sprintf(buffer, "~#%d:~  %lld", slot+1, (long long)t2kHighScores[table][slot].score);

				frameCountMax = 6;
				textGlowFont = TINY_FONT;

				textGlowBrightness = 10;
				JE_outTextGlow(VGAScreenSeg,  20, (slot * 12) + 65, buffer);
				textGlowBrightness = 10;
				JE_outTextGlow(VGAScreenSeg, 150, (slot * 12) + 65, t2kHighScores[table][slot].playerName);
				textGlowBrightness = 10;
				JE_outTextGlow(VGAScreenSeg, JE_fontCenter(miscText[4], TINY_FONT), vga_height, miscText[4]);

				JE_showVGA();

				if (frameCountMax != 0)
					wait_input(true, true, true);

				fade_black(15);
			}

		}
	}
}

/* Online Timed Battle shows both settled scores without opening the blocking
 * solo name-entry dialog. Names already came from the handshake. */
void JE_timedBattleResult(void)
{
	const uint me = thisPlayerNum >= 2 ? 1u : 0u;
	const char *name[2];
	name[me] = network_player_name[0] ? network_player_name : "You";
	name[1 - me] = network_opponent_name[0] ? network_opponent_name : "Opponent";

	char buffer[128];

	// Laid out in the legacy 320 space the name-entry screen uses, so it has to be pillarboxed
	// whichever exit reached it: the clock running out arrives from the script loop with that
	// already on, but a pair that died out arrives straight from the level, where it is off.
	const bool prevCentered = (video_get_menu_x_offset() != 0);
	set_menu_centered(true);

	fade_black(15);
	// The same backdrop, title and type as the card the session opened on, so the two read as a
	// pair. Its own name for the mode: timed_battle_name[0] is the picker's header, and this is
	// not a picker.
	JE_loadPic(VGAScreen, 2, false);
	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, 20, "Timed Battle", large_font,
	                    centered, 15, -3, false, 2);
	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, 48, timed_battle_name[timeBattleSelection],
	                    normal_font, centered, 15, -3, false, 2);

	const uint winner = player[0].cash == player[1].cash ? 2u
	                  : player[0].cash > player[1].cash ? 0u : 1u;

	for (uint i = 0; i < 2; ++i)
	{
		// Name on the left of a fixed column and total right-aligned on it, so a long name cannot
		// push a total off the panel and neither row's figures wander as the numbers change.
		snprintf(buffer, sizeof(buffer), "%lld", (long long)player[i].cash);
		// The menu range for this bank and font: the top of it is white, which this backdrop is
		// far too dark for. The winner takes the step a selected menu row takes, and no more.
		const int value = (i == winner) ? -2 : -4;
		draw_font_hv_shadow(VGAScreen, 60, 84 + 18 * (int)i, name[i], normal_font,
		                    left_aligned, 15, value, false, 2);
		draw_font_hv_shadow(VGAScreen, 260, 84 + 18 * (int)i, buffer, normal_font,
		                    right_aligned, 15, value, false, 2);
	}

	if (winner < 2)
		snprintf(buffer, sizeof(buffer), "%s wins the race.", name[winner]);
	else
		snprintf(buffer, sizeof(buffer), "A draw.");
	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, 130, buffer, normal_font, centered, 15, -3, false, 2);

	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, 172, "Press a key", small_font,
	                    centered, 15, 2, false, 1);

	JE_showVGA();
	// Offer Select before the card fades in and keep it live through the wait.
	touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
	fade_palette(colors, 15, 0, 255);

	/* Release the input that reached this card. The rendezvous then accepts a fresh local press or
	 * the peer's dismissal, so either player can continue without closing the UDP socket early. */
	if (!constantPlay && qa_net_gameplay_ticks == 0)
		wait_noinput(true, true, true);

	/* Headless scenario 20 makes the guest the sole initiator. The host must leave through the
	 * peer-dismiss path that interactive online play uses. */
	const bool auto_dismiss = constantPlay ||
	                          (qa_net_gameplay_ticks > 0 && thisPlayerNum != networkHostPlayerNum);
	network_end_screen_rendezvous(auto_dismiss);

	fade_black(15);
	set_menu_centered(prevCentered);
}

// increases game difficulty based on player's total score / total of players' scores
/* Endless keeps its starting difficulty for the whole run; depth supplies the
 * difficulty curve. Restore temporary script or episode changes after each level. */
bool difficulty_adjust_active(void)
{
	return difficultyAdjust && !endlessFxActive();
}

void adjust_difficulty(void)
{
	const float score_multiplier[10] =
	{
		0,     // Wimp  (doesn't exist)
		0.4f,  // Easy
		0.8f,  // Normal
		1.3f,  // Hard
		1.6f,  // Impossible
		2,     // Insanity
		2,     // Suicide
		3,     // Maniacal
		3,     // Lord of Game
		3,     // Nortaneous
	};

	assert(initialDifficulty > 0 && initialDifficulty < 10);

	// Vanilla's two-player branch counts raw cash: the linked arcade has no shops, so cash IS
	// the score. Co-op ships shop like solo players -- score each like one (cash spent lives
	// on as equipment value) and average the pair, or a stocked-up pair reads as broke.
	const bool linkedScoring = twoPlayerMode && !coop_mode_active();

	const Sint64 score = linkedScoring ? player[0].cash + player[1].cash
	                   : twoPlayerMode ? (JE_totalScore(&player[0]) + JE_totalScore(&player[1])) / 2
	                                   : JE_totalScore(&player[0]),
	             adjusted_score = (Sint64)roundf(score * score_multiplier[initialDifficulty]);

	uint new_difficulty = 0;

	if (linkedScoring)
	{
		if (adjusted_score < 10000)
			new_difficulty = DIFFICULTY_EASY;
		else if (adjusted_score < 20000)
			new_difficulty = DIFFICULTY_NORMAL;
		else if (adjusted_score < 50000)
			new_difficulty = DIFFICULTY_HARD;
		else if (adjusted_score < 80000)
			new_difficulty = DIFFICULTY_IMPOSSIBLE;
		else if (adjusted_score < 125000)
			new_difficulty = DIFFICULTY_INSANITY;
		else if (adjusted_score < 200000)
			new_difficulty = DIFFICULTY_SUICIDE;
		else if (adjusted_score < 400000)
			new_difficulty = DIFFICULTY_MANIACAL;
		else if (adjusted_score < 600000)
			new_difficulty = DIFFICULTY_LORD_OF_GAME;
		else
			new_difficulty = DIFFICULTY_NORTANEOUS;
	}
	else
	{
		if (adjusted_score < 40000)
			new_difficulty = DIFFICULTY_EASY;
		else if (adjusted_score < 70000)
			new_difficulty = DIFFICULTY_NORMAL;
		else if (adjusted_score < 150000)
			new_difficulty = DIFFICULTY_HARD;
		else if (adjusted_score < 300000)
			new_difficulty = DIFFICULTY_IMPOSSIBLE;
		else if (adjusted_score < 600000)
			new_difficulty = DIFFICULTY_INSANITY;
		else if (adjusted_score < 1000000)
			new_difficulty = DIFFICULTY_SUICIDE;
		else if (adjusted_score < 2000000)
			new_difficulty = DIFFICULTY_MANIACAL;
		else if (adjusted_score < 3000000)
			new_difficulty = DIFFICULTY_LORD_OF_GAME;
		else
			new_difficulty = DIFFICULTY_NORTANEOUS;
	}

	difficultyLevel = MAX((unsigned)difficultyLevel, new_difficulty);
}

bool load_next_demo(void)
{
	if (++demo_num > 5)
		demo_num = 1;

	char demo_filename[9];
	snprintf(demo_filename, sizeof(demo_filename), "demo.%d", demo_num);
	demo_file = dir_fopen_die(data_dir(), demo_filename, "rb"); // shipped demo files are required

	difficultyLevel = DIFFICULTY_NORMAL;
	bonusLevelCurrent = false;

	Uint8 temp;
	fread_u8_die(&temp, 1, demo_file);
	JE_initEpisode(temp);

	fread_die(levelName, 1, 10, demo_file);
	levelName[10] = '\0';

	fread_u8_die(&lvlFileNum, 1, demo_file);

	fread_u8_die(&player[0].items.weapon[FRONT_WEAPON].id,  1, demo_file);
	fread_u8_die(&player[0].items.weapon[REAR_WEAPON].id,   1, demo_file);
	fread_u8_die(&player[0].items.super_arcade_mode,        1, demo_file);
	fread_u8_die(&player[0].items.sidekick[LEFT_SIDEKICK],  1, demo_file);
	fread_u8_die(&player[0].items.sidekick[RIGHT_SIDEKICK], 1, demo_file);
	fread_u8_die(&player[0].items.generator,                1, demo_file);

	fread_u8_die(&player[0].items.sidekick_level,           1, demo_file); // could probably ignore
	fread_u8_die(&player[0].items.sidekick_series,          1, demo_file); // could probably ignore

	fread_u8_die(&initial_episode_num,                      1, demo_file); // could probably ignore

	fread_u8_die(&player[0].items.shield,                   1, demo_file);
	fread_u8_die(&player[0].items.special,                  1, demo_file);
	fread_u8_die(&player[0].items.ship,                     1, demo_file);

	for (uint i = 0; i < 2; ++i)
		fread_u8_die(&player[0].items.weapon[i].power,      1, demo_file);

	Uint8 unused[3];
	fread_u8_die(unused, 3, demo_file);

	fread_u8_die(&levelSong, 1, demo_file);

	demo_keys = 0;

	Uint8 temp2[2] = { 0, 0 };
	fread_u8(temp2, 2, demo_file);
	demo_keys_wait = (temp2[0] << 8) | temp2[1];

	printf("loaded demo '%s'\n", demo_filename);

	return true;
}

bool replay_demo_keys(void)
{
	while (demo_keys_wait == 0)
	{
		demo_keys = 0;
		fread_u8(&demo_keys, 1, demo_file);

		Uint8 temp2[2] = { 0, 0 };
		fread_u8(temp2, 2, demo_file);
		demo_keys_wait = (temp2[0] << 8) | temp2[1];

		if (feof(demo_file))
		{
			// no more keys
			return false;
		}
	}

	demo_keys_wait--;

	if (demo_keys & (1 << 0))
		player[0].y -= CURRENT_KEY_SPEED;
	if (demo_keys & (1 << 1))
		player[0].y += CURRENT_KEY_SPEED;

	if (demo_keys & (1 << 2))
		player[0].x -= CURRENT_KEY_SPEED;
	if (demo_keys & (1 << 3))
		player[0].x += CURRENT_KEY_SPEED;

	button[0] = (bool)(demo_keys & (1 << 4));
	button[3] = (bool)(demo_keys & (1 << 5));
	button[1] = (bool)(demo_keys & (1 << 6));
	button[2] = (bool)(demo_keys & (1 << 7));

	return true;
}

/*Street Fighter codes*/
void JE_SFCodes(JE_byte playerNum_, JE_integer PX_, JE_integer PY_, JE_integer mouseX_, JE_integer mouseY_)
{
	JE_byte temp, temp2, temp3, temp4, temp5;

	uint ship = player[playerNum_-1].items.ship;

	/* The linked pair's second half flies the Dragonwing's rear bay rather than a ship of its own,
	 * so it twiddles off the shared "2nd Player ship" row. Every mode where player two owns a ship
	 * (co-op, separate arcade) uses that ship's own row, like player one. */
	if (playerNum_ == 2 && !dual_ship_mode())
	{
		ship = 0;
	}

	// A Dragonwing bought at the Endless outpost twiddles off that same shared row.
	if (ship == SHIP_DRAGONWING)
	{
		ship = 0;
	}

	// SuperTyrian hands one ship every combo; otherwise the ship needs a row of its own, which a
	// shipedit "extra" ship (id above 90) has not got.
	if (!superTyrian && ship >= COUNTOF(shipCombos))
		return;

	/*Get direction*/
	temp2 = (mouseY_ > PY_) +    /*UP*/
	        (mouseY_ < PY_) +    /*DOWN*/
	        (PX_ < mouseX_) +    /*LEFT*/
	        (PX_ > mouseX_);     /*RIGHT*/
	temp = (mouseY_ > PY_) * 1 + /*UP*/
	       (mouseY_ < PY_) * 2 + /*DOWN*/
	       (PX_ < mouseX_) * 3 + /*LEFT*/
	       (PX_ > mouseX_) * 4;  /*RIGHT*/

	if (temp == 0) // no direction being pressed
	{
		if (!button[0]) // if fire button is released
		{
			temp = 9;
			temp2 = 1;
		}
		else
		{
			temp2 = 0;
			temp = 99;
		}
	}

	if (temp2 != 1) // more than one direction pressed: neither advance nor cancel
		return;

	temp += button[0] * 4;

	temp3 = superTyrian ? 21 : 3;
	for (temp2 = 0; temp2 < temp3; temp2++)
	{
		/*Use SuperTyrian ShipCombos or not?*/
		temp5 = superTyrian ? shipCombosB[temp2] : shipCombos[ship][temp2];

		// temp5 == selected combo in ship
		if (temp5 == 0) /* combo doesn't exists */
		{
			// mark twiddles as cancelled/finished
			SFCurrentCode[playerNum_-1][temp2] = 0;
		}
		else
		{
			// get next combo key
			temp4 = keyboardCombos[temp5-1][SFCurrentCode[playerNum_-1][temp2]];

			// correct key
			if (temp4 == temp)
			{
				SFCurrentCode[playerNum_-1][temp2]++;

				temp4 = keyboardCombos[temp5-1][SFCurrentCode[playerNum_-1][temp2]];
				if (temp4 > 100 && temp4 <= 100 + SPECIAL_NUM)
				{
					SFCurrentCode[playerNum_-1][temp2] = 0;
					SFExecuted[playerNum_-1] = temp4 - 100;
				}
			}
			else
			{
				/* Strict: anything that is not the next step throws the combo away, including the
				 * expected direction with the fire button in the wrong state. Only the code just
				 * consumed (a held direction) and code 9 (everything released) are exempt. See
				 * doc/notes.md#twiddles-and-specials. */
				if (temp != 9 &&
				    (SFCurrentCode[playerNum_-1][temp2] == 0 ||
				     keyboardCombos[temp5-1][SFCurrentCode[playerNum_-1][temp2]-1] != temp))
				{
					SFCurrentCode[playerNum_-1][temp2] = 0;
				}
			}
		}
	}
}

/* Collapse movement into the 2:1 twiddle cone. Shallow diagonals remain neutral; Topsy Turvy
 * mirrors horizontal intent here. See doc/notes.md#twiddles-and-specials. */
void SF_twiddleTarget(int px, int py, int dx, int dy, int *out_x, int *out_y)
{
	if (smoothies[9-1])
		dx = -dx;

	if (abs(dx) > 2 * abs(dy))
		dy = 0;
	else if (abs(dy) > 2 * abs(dx))
		dx = 0;

	*out_x = px - (dx > 0 ? 1 : dx < 0 ? -1 : 0);
	*out_y = py - (dy > 0 ? 1 : dy < 0 ? -1 : 0);
}

// A credits row is a blank spacer when it's the lone "." marker (or empty); the same test the
// roll's draw loop below uses.
static bool credits_line_blank(const char *s)
{
	return s[0] == '\0' || strcmp(s, ".") == 0;
}

void JE_playCredits(void)
{
	// tyrian.cdt holds exactly lines_file encrypted records; the fork's credit is spliced into them
	// in code, so the shipped data file is left untouched. Each line is a colour byte (colour =
	// c - 65, see the draw below) followed by the text; "." is a blank spacer row.
	enum { lines_file = 126 };
	enum { lines_extra = 3 };
	enum { lines_max = lines_file + lines_extra };
	enum { line_max_length = 65 };

	char credstr[lines_max][line_max_length + 1];

	int lines = 0;

	JE_byte currentpic = 0, fade = 0;
	JE_shortint fadechg = 1;
	JE_byte currentship = 0;
	JE_integer shipx = 0, shipxwait = 0;
	JE_shortint shipxc = 0, shipxca = 0;

	load_sprites_file(EXTRA_SHAPES, "estsc.shp");

	setDelay2(1000);

	play_song(8);

	// load credits text
	FILE *f = dir_fopen_die(data_dir(), "tyrian.cdt", "rb");
	for (lines = 0; lines < lines_file; ++lines)
	{
		read_encrypted_pascal_string(credstr[lines], sizeof(credstr[lines]), f);
	}
	fclose(f);

	// Insert the fork credit into the blank run before the final card. Finding the
	// gap from the end keeps the original fade and held frame in place.
	static const char *const credits_extra[lines_extra] = {
		"Mwlfn",
		"LOpenTyrian 2000",
		"LEngaged",
	};
	int endLine = lines_file - 1;
	while (endLine > 0 && credits_line_blank(credstr[endLine]))
		--endLine;
	int ins = endLine;
	while (ins > 0 && credits_line_blank(credstr[ins - 1]))
		--ins;
	ins += 4;
	if (ins > endLine)
		ins = endLine;  // a blank run too short to sit inside: go directly above the End card
	memmove(credstr[ins + lines_extra], credstr[ins], (lines_file - ins) * sizeof(credstr[0]));
	for (int i = 0; i < lines_extra; ++i)
		SDL_strlcpy(credstr[ins + i], credits_extra[i], sizeof(credstr[0]));
	lines = lines_max;

	memcpy(colors, palettes[6-1], sizeof(colors));
	JE_clr256(VGAScreen);
	JE_showVGA();
	fade_palette(colors, 2, 0, 255);

	// A press surviving the fades would end the roll on its first JE_anyButton.
	wait_noinput(true, true, true);
	newkey = newmouse = false;

	const int ticks_max = lines * 20 * 3;

	// Smooth-motion pacing for the credits sim: it advances one tick per real tick-period, but we
	// present every display frame (vsync-aligned) in between so the flying ships and portrait fade
	// glide rather than stepping at ~35fps.
	const float cred_period = get_delay_period();
	const float cred_counter_to_ms = 1000.0f / (float)SDL_GetPerformanceFrequency();
	Uint64 cred_last = SDL_GetPerformanceCounter();
	float cred_accum = 0.0f;

	for (int ticks = 0; ticks < ticks_max; ++ticks)
	{
		setDelay(1);
		JE_clr256(VGAScreen);

		blit_sprite_hv(VGAScreenSeg, 319 - sprite(EXTRA_SHAPES, currentpic)->width, 100 - (sprite(EXTRA_SHAPES, currentpic)->height / 2), EXTRA_SHAPES, currentpic, 0x0, fade - 15);

		fade += fadechg;
		if (fade == 0 && fadechg == -1)
		{
			fadechg = 1;
			++currentpic;
			if (currentpic >= sprite_table[EXTRA_SHAPES].count)
				currentpic = 0;
		}
		if (fade == 15)
			fadechg = 0;

		if (getDelayTicks2() == 0)
		{
			fadechg = -1;
			setDelay2(900);
		}

		if (ticks % 200 == 0)
		{
			currentship = (mt_rand() % 11) + 1;
			shipxwait = (mt_rand() % 80) + 10;
			if ((mt_rand() % 2) == 1)
			{
				shipx = 1;
				shipxc = 0;
				shipxca = 1;
			}
			else
			{
				shipx = 900;
				shipxc = 0;
				shipxca = -1;
			}
		}

		shipxwait--;
		if (shipxwait == 0)
		{
			if (shipx == 1 || shipx == 900)
				shipxc = 0;
			shipxca = -shipxca;
			shipxwait = (mt_rand() % 40) + 15;
		}
		shipxc += shipxca;
		shipx += shipxc;
		if (shipx < 1)
		{
			shipx = 1;
			shipxwait = 1;
		}
		if (shipx > 900)
		{
			shipx = 900;
			shipxwait = 1;
		}
		int tmp_unknown = shipxc * shipxc;
		if (450 + tmp_unknown < 0 || 450 + tmp_unknown > 900)
		{
			if (shipxca < 0 && shipxc < 0)
				shipxwait = 1;
			if (shipxca > 0 && shipxc > 0)
				shipxwait = 1;
		}

		uint ship_sprite = ships[currentship].shipgraphic;
		if (shipxc < -10)
			ship_sprite -= (shipxc < -20) ? 4 : 2;
		else if (shipxc > 10)
			ship_sprite += (shipxc > 20) ? 4 : 2;

		blit_sprite2x2(VGAScreen, shipx / 40, (vga_height - 16) - (ticks % vga_height), spriteSheet9, ship_sprite);

		const int bottom_line = (ticks / 3) / 20;
		int y = 20 - ((ticks / 3) % 20);

		for (int line = bottom_line - 10; line < bottom_line; ++line)
		{
			if (line >= 0 && line < lines_max)
			{
				if (strcmp(&credstr[line][0], ".") != 0 && strlen(credstr[line]))
				{
					const Uint8 color = credstr[line][0] - 65;
					const char *text = &credstr[line][1];

					const int x = 110 - JE_textWidth(text, SMALL_FONT_SHAPES) / 2;

					JE_outTextAdjust(VGAScreen, x + abs((y / 18) % 4 - 2) - 1, y - 1, text, color, -8, SMALL_FONT_SHAPES, false);
					JE_outTextAdjust(VGAScreen, x,                             y,     text, color, -2, SMALL_FONT_SHAPES, false);
				}
			}

			y += 20;
		}

		fill_rectangle_xy(VGAScreen, 0,  0, 319, 10, 0);
		fill_rectangle_xy(VGAScreen, 0, vga_height - 10, vga_width - 1, vga_height - 1, 0);

		if (currentpic == sprite_table[EXTRA_SHAPES].count - 1)
			JE_outTextAdjust(VGAScreen, 5, vga_height, miscText[54], 2, -2, SMALL_FONT_SHAPES, false);  // levels-in-episode

		if (bottom_line == lines_max - 8)
			fade_song();

		if (ticks == ticks_max - 1)
		{
			--ticks;
			play_song(9);
		}

		NETWORK_KEEP_ALIVE();
		// Credits can run for minutes. Drain outpost traffic so it cannot block the rendezvous.
		while (network_shop_pump())
			;

		if (smoothMotion)
		{
			// Present at the display refresh for the span of one sim tick. The ship
			// flight and portrait fade glide; the 1px-quantised text scroll still
			// steps (8-bit indexed, no sub-pixel) but at an even, vsync-aligned cadence.
			bool creditsDone = false;
			for (;;)
			{
				JE_showVGA();
				if (!output_vsync)
					limit_render_fps();
				if (JE_anyButton())
				{
					creditsDone = true;
					break;
				}

				const Uint64 now = SDL_GetPerformanceCounter();
				cred_accum += (float)(now - cred_last) * cred_counter_to_ms;
				cred_last = now;
				if (cred_accum >= cred_period)
				{
					cred_accum -= cred_period;
					if (cred_accum > cred_period)
						cred_accum = cred_period;  // never bank more than one tick of backlog
					break;
				}
			}
			if (creditsDone)
				break;
		}
		else
		{
			JE_showVGA();

			wait_delay();

			if (JE_anyButton())
				break;
		}
	}

	fade_black(10);

	free_sprites(EXTRA_SHAPES);
}

void JE_endLevelAni(void)
{
	JE_word x, y;
	JE_byte temp;
	char tempStr[256];

	Sint8 i;

	Sint64 endlessInterest = 0, endlessBonus = 0;  // endless: the level-clear payout, shown below

	if (difficulty_adjust_active())
		adjust_difficulty();

	for (uint p = 0; p < (dual_ship_mode() ? COUNTOF(player) : 1u); ++p)
		player[p].last_items = player[p].items;
	strcpy(lastLevelName, levelName);

	JE_wipeKey();
	frameCountMax = 4;
	textGlowFont = SMALL_FONT_SHAPES;

	SDL_Color white = { 255, 255, 255 };
	set_colors(white, 254, 254);

	if (!levelTimer || levelTimerCountdown > 0 || !(episodeNum == 4))
		JE_playSampleNum(V_LEVEL_END);
	else
		play_song(21);

	if (bonusLevel)
	{
		JE_outTextGlow(VGAScreenSeg, 20, 20, miscText[17-1]);
	}
	else if (all_players_alive())
	{
		sprintf(tempStr, "%s %s", miscText[27-1], levelName); // "Completed"
		JE_outTextGlow(VGAScreenSeg, 20, 20, tempStr);
	}
	else
	{
		sprintf(tempStr, "%s %s", miscText[62-1], levelName); // "Exiting"
		JE_outTextGlow(VGAScreenSeg, 20, 20, tempStr);
	}

	// Endless banks the level-clear payout now, so the cash total printed just below already
	// includes it (the breakdown is shown further down, where data cubes would normally be).
	if (endlessMode)
		endlessApplyLevelPayout(&endlessInterest, &endlessBonus);

	if (twoPlayerMode)
	{
		for (uint i = 0; i < 2; ++i)
		{
			char label[80];
			JE_playerScoreLabel((JE_byte)(i + 1), label, sizeof(label));
			snprintf(tempStr, sizeof(tempStr), "%s %lld", label, (long long)player[i].cash);
			JE_outTextGlow(VGAScreenSeg, 30, 50 + 20 * i, tempStr);
		}
	}
	else
	{
		sprintf(tempStr, "%s %lld", miscText[28-1], (long long)player[0].cash);
		JE_outTextGlow(VGAScreenSeg, 30, 50, tempStr);
	}

	// Time left is one clock for the whole level, so an online pair that beat it beat it together
	// and both purses take the same bonus.
	const int timeBonus = timedBattleMode ? (levelTimerCountdown / 10) * 100 : 0;
	if (timedBattleMode)
	{
		for (uint p = 0; p < (dual_ship_mode() ? COUNTOF(player) : 1u); ++p)
			player_add_cash(&player[p], timeBonus);

		if (!dual_ship_mode())
		{
			sprintf(tempStr, "%s %d", miscTextB[6], timeBonus);
			JE_outTextGlow(VGAScreenSeg, 40, 75, tempStr);
		}
	}

	temp = (totalEnemy == 0) ? 0 : roundf(enemyKilled * 100 / totalEnemy);
	sprintf(tempStr, "%s %d%%", miscText[63-1], temp);
	JE_outTextGlow(VGAScreenSeg, 40, 90, tempStr);

	if (timedBattleMode && dual_ship_mode())
	{
		sprintf(tempStr, "%s %d", miscTextB[6], timeBonus);
		JE_outTextGlow(VGAScreenSeg, 40, 108, tempStr);
	}

	if (timedBattleMode && dual_ship_mode())
	{
		// Two racers, two life counts. The parade below is a solo flourish with no room to run
		// twice -- a full eleven ships already reach where its own total sits -- so the pair get
		// a named line each instead, under the shared time bonus.
		for (uint p = 0; p < COUNTOF(player); ++p)
		{
			JE_playSampleNum(S_ITEM);

			x = *player[p].lives * 1000;
			snprintf(tempStr, sizeof(tempStr), "%s %s %d",
			         JE_getName((JE_byte)(p + 1)), miscTextB[7], x);
			JE_outTextGlow(VGAScreenSeg, 30, 128 + 18 * (int)p, tempStr);
			player_add_cash(&player[p], x);
		}
	}
	else if (timedBattleMode)
	{
		// The ships parade across a row of their own: at eleven lives they run past x=200, which
		// is where the total used to sit beside them, and the two ended up drawn through each other.
		for (temp = 1; temp <= *player[0].lives; temp++)
		{
			JE_playSampleNum(S_ITEM);
			x = 20 + 15 * temp;
			y = 112;

			for (i = -15; i <= 10; i++)
			{
				setDelay(frameCountMax);

				blit_sprite_hv(VGAScreenSeg, x, y, OPTION_SHAPES, 46, 0x9, i);

				if (JE_anyButton())
					frameCountMax = 0;

				JE_showVGA();

				wait_delay();
			}
			for (i = 10; i >= 0; i--)
			{
				setDelay(frameCountMax);

				blit_sprite_hv(VGAScreenSeg, x, y, OPTION_SHAPES, 46, 0x9, i);

				if (JE_anyButton())
					frameCountMax = 0;

				JE_showVGA();

				wait_delay();
			}
		}
		x = *player[0].lives * 1000;
		sprintf(tempStr, "%s %d", miscTextB[7], x);
		JE_outTextGlow(VGAScreenSeg, 40, 143, tempStr);
		player_add_cash(&player[0], x);
	}
	else if (endlessMode)
	{
		// Endless earns cash, not data cubes; show the clear payout just banked above. No '+' or
		// parentheses: SMALL_FONT_SHAPES silently drops those glyphs.
		char payStr[64];
		snprintf(payStr, sizeof(payStr), "Zone Bonus:  %lld", (long long)endlessBonus);
		JE_outTextGlow(VGAScreenSeg, 30, 120, payStr);
		if (endlessInterest > 0)
		{
			snprintf(payStr, sizeof(payStr), "Bank Interest:  %lld", (long long)endlessInterest);
			JE_outTextGlow(VGAScreenSeg, 30, 138, payStr);
		}
	}
	else if (!arcade_rules_active())
	{
		JE_outTextGlow(VGAScreenSeg, 30, 120, miscText[4-1]);   /*Cubes*/

		if (cubeMax > 0)
		{
			if (cubeMax > 4)
				cubeMax = 4;

			if (frameCountMax != 0)
				frameCountMax = 1;

			for (temp = 1; temp <= cubeMax; temp++)
			{
				NETWORK_KEEP_ALIVE();

				JE_playSampleNum(S_ITEM);
				x = 20 + 30 * temp;
				y = 135;
				JE_drawCube(VGAScreenSeg, x, y, 9, 0);
				JE_showVGA();

				for (i = -15; i <= 10; i++)
				{
					setDelay(frameCountMax);

					blit_sprite_hv(VGAScreenSeg, x, y, OPTION_SHAPES, 25, 0x9, i);

					if (JE_anyButton())
						frameCountMax = 0;

					JE_showVGA();

					wait_delay();
				}
				for (i = 10; i >= 0; i--)
				{
					setDelay(frameCountMax);

					blit_sprite_hv(VGAScreenSeg, x, y, OPTION_SHAPES, 25, 0x9, i);

					if (JE_anyButton())
						frameCountMax = 0;

					JE_showVGA();

					wait_delay();
				}
			}
		}
		else
		{
			JE_outTextGlow(VGAScreenSeg, 50, 135, miscText[15-1]);
		}

	}

	if (frameCountMax != 0)
	{
		frameCountMax = 6;
		temp = 1;
	}
	else
	{
		temp = 0;
	}
	temp2 = twoPlayerMode ? 150 : 160;
	// An online battle stacks a time bonus and two ship-bonus rows under the two scores, so the
	// prompt drops to where Endless keeps its own, below its payout block.
	if (timedBattleMode && dual_ship_mode())
		temp2 = 168;
	// Endless adds a payout block under the scores (Zone Bonus, and Bank Interest below it), and
	// two players add a second score line above that, so the two-player y lands this right on top
	// of the last of them.
	if (endlessMode)
	{
		temp2 = 168;
		JE_outTextGlow(VGAScreenSeg, JE_fontCenter(miscText[5-1], SMALL_FONT_SHAPES), temp2,
		               miscText[5-1]);
	}
	else
		JE_outTextGlow(VGAScreenSeg, 90, temp2, miscText[5-1]);

	// A gameplay wire test has no player to press past the level-complete screen.
	if (!constantPlay && qa_net_gameplay_ticks == 0)
	{
		do
		{
			touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
			touch_ui_idle_repaint();
			setDelay(1);

			NETWORK_KEEP_ALIVE();

			wait_delay();
		} while (!(JE_anyButton() || (frameCountMax == 0 && temp == 1)));
	}

	wait_noinput(false, false, true); // debounce the dismissing input

	fade_black(15);
	JE_clr256(VGAScreen);
}

void JE_drawCube(SDL_Surface * screen, JE_word x, JE_word y, JE_byte filter, JE_byte brightness)
{
	blit_sprite_dark(screen, x + 4, y + 4, OPTION_SHAPES, 25, false);
	blit_sprite_dark(screen, x + 3, y + 3, OPTION_SHAPES, 25, false);
	blit_sprite_hv(screen, x, y, OPTION_SHAPES, 25, filter, brightness);
}

void JE_handleChat(void)
{
	// STUB(); Annoying piece of crap =P
}

bool str_pop_int(char *str, int *val)
{
	bool success = false;

	char buf[256];
	assert(strlen(str) < sizeof(buf));

	// grab the value from str
	char *end;
	*val = strtol(str, &end, 10);

	if (end != str)
	{
		success = true;

		// shift the rest to the beginning
		strcpy(buf, end);
		strcpy(str, buf);
	}

	return success;
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

/* Draw a 1px rectangle outline, clamped to the surface. */
static void debug_box(SDL_Surface *s, int x0, int y0, int x1, int y1, Uint8 col)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > s->w - 1) x1 = s->w - 1;
	if (y1 > s->h - 1) y1 = s->h - 1;
	if (x1 < x0 || y1 < y0)
		return;
	fill_rectangle_xy(s, x0, y0, x1, y0, col);  // top
	fill_rectangle_xy(s, x0, y1, x1, y1, col);  // bottom
	fill_rectangle_xy(s, x0, y0, x0, y1, col);  // left
	fill_rectangle_xy(s, x1, y0, x1, y1, col);  // right
}

// Hitboxes use simulation coordinates and snap between ticks. The performance
// overlay is drawn at presentation time.
static void JE_drawDebugOverlays(void)
{
	enum { COL_ENEMY = 124, COL_PLAYER = 0xFB, COL_PSHOT = 0xFB, COL_ESHOT = 124 };

	if (debugHitboxOverlay)
	{
		// shootable enemies: the box player shots are tested against, drawn where that test puts
		// it. Centered Hitboxes centres it on the enemy sprite (its quadrants are drawn at
		// +/-6 x, +/-7 y of (ex,ey)); Classic keeps the vanilla anchor and its upward bias.
		for (int i = 0; i < 100; ++i)
		{
			if (enemyAvail[i] != 0)
				continue;
			const bool small = (enemy[i].enemycycle > 0);
			const int cx = enemy[i].ex + enemy[i].mapoffset + (centeredShotHitboxes ? 6 : 0);
			const int cy = enemy[i].ey + (centeredShotHitboxes ? 7 : (small ? -6 : -12));
			const int hw = small ? 13 : 25;
			const int hh = small ? 15 : 29;
			debug_box(VGAScreen, cx - hw, cy - hh, cx + hw, cy + hh, COL_ENEMY);
		}

		// player ship body box, centred on the hull sprite (drawn ~7px right/down
		// of x,y) so it matches the centred collision in JE_playerCollide
		for (uint p = 0; p < (twoPlayerMode ? 2u : 1u); ++p)
		{
			if (!player[p].is_alive)
				continue;
			const int cx = player[p].x + 7, cy = player[p].y + 7;
			debug_box(VGAScreen, cx - 12, cy - 14, cx + 12, cy + 14, COL_PLAYER);

			// ...and within it the box enemy shots are tested against, which Centered Hitboxes
			// moves onto the middle of the hull and Classic leaves on the ship's own position.
			const int sx = cx - (centeredShotHitboxes ? 0 : 7);
			const int sy = cy - (centeredShotHitboxes ? 0 : 7);
			const int hw = endlessHitboxScale((int)player[p].shot_hit_area_x);
			const int hh = endlessHitboxScale((int)player[p].shot_hit_area_y);
			debug_box(VGAScreen, sx - hw, sy - hh, sx + hw, sy + hh, COL_PLAYER);
		}

		// projectiles, marked at the point each one's own hit test is taken from. The last slot is
		// the Zinglon pillar's flag rather than a shot, and carries no shot data to read.
		for (int i = 0; i < MAX_PWEAPON - 1; ++i)
		{
			if (shotAvail[i] == 0)
				continue;
			int dx, dy;
			player_shot_hit_offset(playerShotData[i].shotGr + playerShotData[i].shotAni, &dx, &dy);
			const int sx = playerShotData[i].shotX + dx, sy = playerShotData[i].shotY + dy;
			debug_box(VGAScreen, sx - 1, sy - 1, sx + 1, sy + 1, COL_PSHOT);
		}
		for (int i = 0; i < ENEMY_SHOT_MAX; ++i)
		{
			if (enemyShotAvail[i] != 0)
				continue;
			int dx, dy;
			enemy_shot_hit_offset(enemyShot[i].sgr, enemyShot[i].animate, &dx, &dy);
			const int sx = enemyShot[i].sx + dx, sy = enemyShot[i].sy + dy;
			debug_box(VGAScreen, sx - 1, sy - 1, sx + 1, sy + 1, COL_ESHOT);
		}
	}
}

/* Drawn by the present loop onto the finished frame, after the playfield composite and the HUD, so
 * nothing can cover it and the text never reaches game_screen (where a smoothie filter would pull
 * it into the feedback). dst is the composited frame, whose playfield starts at x=0. */
void JE_drawPerfOverlay(SDL_Surface *dst, int scale)
{
	if (!debugPerfOverlay)
		return;

	int activeEnemies = 0, pShots = 0, eShots = 0;
	for (int i = 0; i < 100; ++i)
		if (enemyAvail[i] != 1)
			++activeEnemies;
	for (int i = 0; i < MAX_PWEAPON; ++i)
		if (shotAvail[i] != 0)
			++pShots;
	for (int i = 0; i < ENEMY_SHOT_MAX; ++i)
		if (enemyShotAvail[i] == 0)
			++eShots;

	const int ms = (current_fps > 0) ? (1000 / current_fps) : 0;
	const int px = 4;  // just inside the playfield's left edge
	int py = 4;
	char buf[40];

	snprintf(buf, sizeof(buf), "FPS %d (%dms)", current_fps, ms);
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
	snprintf(buf, sizeof(buf), "ENEMIES %d", activeEnemies);
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
	snprintf(buf, sizeof(buf), "SHOTS P%d E%d", pShots, eShots);
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
	snprintf(buf, sizeof(buf), "ALPHA %d%%", (int)(debug_interp_alpha * 100.0f + 0.5f));
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;

	// PRED is prediction lead; RB is rollback rate/depth; DESYNC counts canary mismatches.
	if (nrb_active())
	{
		Uint32 predict, depth, rate, desyncs;
		nrb_stats(&predict, &depth, &rate, &desyncs);

		snprintf(buf, sizeof(buf), "PRED %u", (unsigned)predict);
		JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
		snprintf(buf, sizeof(buf), "RB %u%%/%u", (unsigned)rate, (unsigned)depth);
		JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
		if (desyncs != 0)
		{
			snprintf(buf, sizeof(buf), "DESYNC %u", (unsigned)desyncs);
			JE_textShadeScaled(dst, px, py, buf, 15, 2, scale);
		}
	}
}

#ifdef WITH_NETWORK
/* Local ship replay history.
 * Index local lockstep state by packet sequence, not queue position, so resends and duplicates
 * cannot shift the replayed tick. */
#define NET_OWN_RING 16  // >= NET_PACKET_QUEUE, so an entry outlives the queue that names it

static Sint16 net_own_x[NET_OWN_RING], net_own_y[NET_OWN_RING];
static Uint16 net_own_buttons[NET_OWN_RING];

static void net_own_state_store(Uint16 sync, int x, int y, Uint16 buttons)
{
	const unsigned slot = (unsigned)sync % NET_OWN_RING;
	net_own_x[slot] = (Sint16)x;
	net_own_y[slot] = (Sint16)y;
	net_own_buttons[slot] = buttons;
}

static void net_own_state_load(Uint16 sync, int *x, int *y, Uint16 *buttons)
{
	const unsigned slot = (unsigned)sync % NET_OWN_RING;
	*x = net_own_x[slot];
	*y = net_own_y[slot];
	*buttons = net_own_buttons[slot];
}
#endif

/* Bottom-band HUD layout; see mainint.h for precedence. */

// The score row's text baseline. FULL_SHADE outlines the glyphs, so the rows actually touched
// are HUD_SCORE_Y-1 .. HUD_SCORE_Y+8.
#define HUD_SCORE_Y      (vga_height - 25)
#define HUD_ROW_H        10   // row pitch that keeps two shadowed TINY_FONT lines clear of each other
#define HUD_SUPERBOMB_Y  160  // row the superbomb icons blit at, in both bottom corners

uint hud_superbomb_count(uint p)
{
	return player_is_out(p) ? 0 : player[p].superbombs;
}

// Rightmost column player 1's superbomb row reaches (icons march right from x30).
static int hud_superbomb_p1_right(void)
{
	return 30 + 12 * (int)hud_superbomb_count(0);
}

int hud_fps_row(void)
{
	// Stack above player 2's score and superbombs; one-player leaves this corner free.
	if (!twoPlayerMode || (galagaMode && !coop_mode_active()))
		return HUD_SCORE_Y + 1;

	if (hud_superbomb_count(1) > 0)
		return HUD_SUPERBOMB_Y - HUD_ROW_H + 1;

	return HUD_SCORE_Y - HUD_ROW_H + 1;
}

int hud_bottom_band_top(void)
{
	int top = HUD_SCORE_Y - 1;  // both scores share this row; the shadow starts one above

	// Superbomb icons form their own row above the scores at both playfield edges, so a
	// centred full-width bar has to clear them as well.
	if (hud_superbomb_count(0) > 0 || hud_superbomb_count(1) > 0)
		top = HUD_SUPERBOMB_Y;

	if (show_fps)
	{
		const int fps_top = hud_fps_row() - 1;
		if (fps_top < top)
			top = fps_top;
	}

	return top;
}

/* Horizontal extent of the top HUD clusters. Keep in step with JE_inGameDisplays. */
static int hud_player_name_width(int index)
{
	char name[21];
	SDL_strlcpy(name, isNetworkGame ? JE_getName(index + 1)
	                                : miscText[(index == 0 ? 49 : 50) - 1], sizeof(name));
	return JE_textWidth(name, TINY_FONT);
}

static bool hud_lives_shown(void)
{
	return arcade_rules_active();
}

uint hud_lives_count(uint p)
{
	return player_is_out(p) ? 0 : *player[p].lives;
}

/* One icon per life the player still has, so the row reads the same number the outpost's
 * "Lives:" row does. Past this many the row collapses to a single icon plus a count, which
 * caps its width where the old extra-lives row (icons for lives - 1) already capped.
 */
#define HUD_LIVES_ICONS_MAX 4

/* Left edge of player 2's collapsed lives count, mirrored from player 1's layout. */
static int hud_lives_count_left(const char *count)
{
	return PLAYFIELD_WIDTH + 4 - JE_textWidth(count, TINY_FONT);
}

/* Special block geometry. Player one's icon sits inside the left playfield edge with its ready
 * light just past it; player two's mirrors that against the right, right-aligned on the same
 * anchor its name label uses. */
#define HUD_SPECIAL_GAP       1   // between the icon and its ready light
#define HUD_SPECIAL_P1_X     25
#define HUD_SPECIAL_P2_RIGHT (PLAYFIELD_WIDTH + 22)   // the anchor hud_top_right_left_edge mirrors

bool hud_special_block_shown(uint p)
{
	return p < COUNTOF(player) && p == gameplay_local_player_index()
	    && player[p].items.special > 0;
}

bool hud_special_on_right(uint p)
{
	// Only the arcade puts a name and lives row under the block, so only there does ship two
	// have a corner of its own to mirror into. Co-op has neither row, and both machines keep
	// the historical left corner.
	return p == 1 && hud_lives_shown();
}

int hud_special_icon_x(uint p)
{
	return hud_special_on_right(p) ? HUD_SPECIAL_P2_RIGHT - HUD_SPECIAL_ICON_W
	                               : HUD_SPECIAL_P1_X;
}

int hud_special_light_x(uint p)
{
	const int iconX = hud_special_icon_x(p);
	return hud_special_on_right(p) ? iconX - HUD_SPECIAL_GAP - HUD_SPECIAL_LIGHT_W
	                               : iconX + HUD_SPECIAL_ICON_W + HUD_SPECIAL_GAP;
}

int hud_lives_row_y(uint p)
{
	return hud_special_block_shown(p) ? HUD_LIVES_Y_SPECIAL : HUD_LIVES_Y;
}

/* Special-ready meter: drain while the effect burns, then refill during recharge.
 * JE_doSpecialShot publishes the clocks; this code only draws them. */
// Both frames paint the same block of the 12px sprite cell: columns 3..8 of rows 1..12.
#define HUD_LIGHT_ROW_FIRST  1
#define HUD_LIGHT_ROW_LAST  12
#define HUD_LIGHT_ROWS      (HUD_LIGHT_ROW_LAST - HUD_LIGHT_ROW_FIRST + 1)
#define HUD_LIGHT_INK_X      3
#define HUD_LIGHT_INK_W      6
#define HUD_LIGHT_FLASH     12   // ticks the ready pop lasts; also its opening brightness

static int  hud_light_charge_left = 0;  // recharge ticks still to run
static int  hud_light_burn_left = 0;    // ...and burn ticks, while a fired special is still going
static bool hud_light_armed = false;    // the special could be fired this tick (sampled in varz.c)
static bool hud_light_fired = false;    // ...and one went off during it
static bool hud_light_await_pop = false;  // a fire is waiting on the special coming back
static bool hud_light_published = false;
static bool hud_light_sampled_this_tick = false;
static bool hud_light_rearm_pending = false;  // a special was equipped; its lockout starts a phase
static int  hud_light_charge_full = 0;  // what each phase started from, so both scale to themselves
static int  hud_light_burn_full = 0;
static int  hud_light_flash = 0;
static bool hud_light_was_ready = false;   // the meter at the previous live tick, and whether
static bool hud_light_meter_seen = false;  // there has been one; a reset leaves nothing to compare

// Render state for the display-rate repaint: the lit row count at the previous and current tick
// (the clocks step once a tick, so without these the meter would climb one whole row at a time),
// and where the tick drew it.
static float hud_light_fill_prev = 0.0f, hud_light_fill_cur = 0.0f;
static int   hud_light_x = 0, hud_light_y = 0;
static bool  hud_light_shown = false;

static void hud_special_light_tick_begin(void)
{
	hud_light_sampled_this_tick = false;
	hud_light_published = false;

	// An equip lands after this tick's clocks are published, so its request is taken at the next
	// tick's opening. Only a live tick owns the scale; a silent pass leaves the request for the
	// tick that draws. The burn keeps its scale: it belongs to the special that started it.
	if (hud_light_rearm_pending && !rollback_resim_silent)
	{
		hud_light_rearm_pending = false;
		hud_light_charge_full = 0;
	}
}

// Equipping a special opens a meter phase. See doc/notes.md#gauges-and-effects.
void hud_special_light_rearm(uint p)
{
	if (hud_special_block_shown(p))
		hud_light_rearm_pending = true;
}

void hud_special_light_publish(int charge_ticks, int burn_ticks, bool armed, bool fired)
{
	hud_light_charge_left = charge_ticks;
	hud_light_burn_left = burn_ticks;

	/* The linked pair runs the shared special once from each player's movement pass. The second
	 * pass owns the final clocks, but it must not erase a ready or fired edge from the first. */
	if (hud_light_sampled_this_tick)
	{
		hud_light_armed |= armed;
		hud_light_fired |= fired;
	}
	else
	{
		hud_light_armed = armed;
		hud_light_fired = fired;
		hud_light_sampled_this_tick = true;
	}
	hud_light_published = true;
}

void hud_special_light_reset(void)
{
	hud_light_charge_left = 0;
	hud_light_burn_left = 0;
	hud_light_armed = false;
	hud_light_fired = false;
	// No fire outstanding, so a level that opens with a ready special does not pop for it. Level
	// setup calls this; without it the previous level's unfinished recharge carries over and pops
	// the moment the new level arms.
	hud_light_await_pop = false;
	hud_light_was_ready = false;
	hud_light_meter_seen = false;
	hud_light_published = false;
	hud_light_sampled_this_tick = false;
	hud_light_rearm_pending = false;
	hud_light_charge_full = 0;
	hud_light_burn_full = 0;
	hud_light_flash = 0;
	hud_light_fill_prev = hud_light_fill_cur = 0.0f;
	hud_light_shown = false;
}

/* One paint of the meter: the charged frame fills the spent one from the bottom up to `fill` rows.
 * `fill` is fractional between ticks and the window is handed over in sub-rows, so at scale > 1
 * the boundary between the two paints lands inside a row rather than jumping a whole one.
 */
static void hud_special_light_paint(SDL_Surface *dst, int scale, int x, int y, float fill, int bright)
{
	const int rows = HUD_LIGHT_ROWS * scale;
	int lit = (int)(fill * (float)scale);  // lit sub-rows, measured up from the bar's bottom
	if (lit < 0)
		lit = 0;
	else if (lit > rows)
		lit = rows;

	const int first = HUD_LIGHT_ROW_FIRST * scale;
	const int last = (HUD_LIGHT_ROW_LAST + 1) * scale - 1;
	const int split = last - lit;  // last spent sub-row; the charged paint runs split+1 .. last

	if (split >= first)
		blit_sprite2_rows_bright_scaled(dst, x, y, spriteSheet9, 93, first, split, 0, scale);
	if (lit > 0)
		blit_sprite2_rows_bright_scaled(dst, x, y, spriteSheet9, 94, split + 1, last, bright, scale);
}

void hud_special_light_present(SDL_Surface *dst, int scale, float alpha)
{
	if (!hud_light_shown)
		return;

	// The pop decrements by exactly 1 a tick, so the previous value is always cur+1 -- the same
	// interpolation the HUD gauge and boss-bar flashes use.
	const int bright = hud_light_flash > 0 ? (int)(hud_light_flash + 1.0f - alpha + 0.5f) : 0;

	hud_special_light_paint(dst, scale, hud_light_x, hud_light_y,
	                        hud_light_fill_prev + (hud_light_fill_cur - hud_light_fill_prev) * alpha,
	                        bright);
}

// Full meter: nothing burning and nothing left to recharge.
static bool hud_special_light_ready(void)
{
	return hud_light_burn_left == 0 && hud_light_charge_left == 0;
}

static void hud_special_light_step_flash(void)
{
	// A meter that has left full since the previous live tick owes a pop, whether or not this
	// machine ever saw the shot's edge. See doc/notes.md#gauges-and-effects.
	const bool ready = hud_special_light_ready();
	if (hud_light_meter_seen && hud_light_was_ready && !ready)
		hud_light_await_pop = true;
	hud_light_was_ready = ready;
	hud_light_meter_seen = true;

	// Check before arming the next shot so one-tick recharges and same-tick refires are visible.
	if (hud_light_await_pop && hud_light_armed)
	{
		hud_light_flash = HUD_LIGHT_FLASH;
		hud_light_await_pop = false;
		if (qa_net_gameplay_ticks > 0 && (qa_net_scenario == 5 || qa_net_scenario == 19))
			++qa_net_special_flashes;
	}
	else if (hud_light_flash > 0)
	{
		--hud_light_flash;
	}

	if (hud_light_fired)
		hud_light_await_pop = true;
}

// Build the icon of a special that shares one: the bare ship body, then its replacement sprite
// centred on the upper half (unusedSpecialTops in episodes.c). Drawing the shipped 2x2 for those
// eleven would show the shared art, including the pixels that reach into its lower half.
void draw_special_icon(SDL_Surface *surface, int x, int y, JE_byte id)
{
	JE_word top = 0;
	const Sprite2_array *const sheet = JE_specialIconTop(id, &top);
	int x0, y0, x1, y1;

	if (sheet == NULL || !sprite2_ink_bounds(*sheet, top, &x0, &y0, &x1, &y1))
	{
		blit_sprite2x2(surface, x, y, spriteSheet10, special[id].itemgraphic);
		return;
	}

	const int half = HUD_SPECIAL_ICON_H / 2;
	blit_sprite2(surface, x,      y + half, spriteSheet10, SPECIAL_ICON_SHIP_GR + 19);
	blit_sprite2(surface, x + 12, y + half, spriteSheet10, SPECIAL_ICON_SHIP_GR + 20);

	// An odd-width sprite cannot sit dead centre, and the spare column reads better on its left.
	const int freeW = HUD_SPECIAL_ICON_W - (x1 - x0 + 1);
	const int freeH = half - (y1 - y0 + 1);
	blit_sprite2(surface, x + (freeW + 1) / 2 - x0, y + freeH / 2 - y0, *sheet, top);
}

/* Advance the special meter from its published clocks. Burn and recharge meet at empty, and the
 * fractional fill prevents long clocks from jumping a whole row at a time. */
static float hud_special_light_step(void)
{
	const bool burning = hud_light_burn_left > 0;

	// Specials that set no recharge at all -- the Repulsor, Attractor and the repair pair, whose
	// whole limit is releasing and re-pressing fire -- keep a full meter, because it is: nothing
	// is recharging to drain.
	const bool ready = hud_special_light_ready();

	if (!rollback_resim_silent)
	{
		// Each phase is measured against its own opening value, so a short recharge can't inherit
		// a long one's scale and open half full.
		if (hud_light_burn_left > hud_light_burn_full)
			hud_light_burn_full = hud_light_burn_left;
		else if (!burning)
			hud_light_burn_full = 0;

		if (burning || hud_light_charge_left == 0)
			hud_light_charge_full = 0;
		else if (hud_light_charge_left > hud_light_charge_full)
			hud_light_charge_full = hud_light_charge_left;

		hud_special_light_step_flash();
	}

	if (ready)
		return HUD_LIGHT_ROWS;
	if (burning)
		return hud_light_burn_full > 0
		     ? (float)hud_light_burn_left * HUD_LIGHT_ROWS / hud_light_burn_full : 0.0f;
	if (hud_light_charge_full > 0)
		return (float)(hud_light_charge_full - hud_light_charge_left)
		     * HUD_LIGHT_ROWS / hud_light_charge_full;

	return 0.0f;
}

static void draw_special_ready_light(int x, int y)
{
	// Galaga's wing flies without a special shot at all, so JE_doSpecialShot never runs and there
	// is no charge to show; anything else that gates it out drops the light the same way.
	if (!hud_light_published)
	{
		hud_special_light_reset();
		return;
	}
	hud_light_published = false;

	const float filled = hud_special_light_step();

	if (!rollback_resim_silent)
	{
		hud_light_fill_prev = hud_light_fill_cur;
		hud_light_fill_cur = filled;
		hud_light_x = x;
		hud_light_y = y;
		hud_light_shown = true;
	}

	// The light is opaque over its ink block, and only the residual carries it onto interpolated
	// frames; without the mark, a pixel that happens to match the playfield under it is dropped
	// there and the bar shows a hole.
	rl_mark_overlay_rect(x + HUD_LIGHT_INK_X, y + HUD_LIGHT_ROW_FIRST, HUD_LIGHT_INK_W, HUD_LIGHT_ROWS);

	hud_special_light_paint(VGAScreen, 1, x, y, filled, hud_light_flash);
}

int hud_top_left_right_edge(void)
{
	int right = 0;
	const uint local_player = gameplay_local_player_index();

	if (hud_special_block_shown(local_player) && !hud_special_on_right(local_player))
		right = hud_special_light_x(local_player) + HUD_SPECIAL_LIGHT_W;

	if (hud_lives_shown())
	{
		const uint lives = hud_lives_count(0);

		// The label sits at x28; the lives row starts at x30 and steps right, or collapses to
		// a single icon plus a count at x45 once past HUD_LIVES_ICONS_MAX.
		const int name_right = 28 + hud_player_name_width(0);
		const int lives_right = (lives > HUD_LIVES_ICONS_MAX) ? 45 + JE_textWidth("99", TINY_FONT)
		                      : (lives >= 1) ? 30 + (int)lives * 12
		                                     : 30;

		if (name_right > right)  right = name_right;
		if (lives_right > right) right = lives_right;
	}

	return right;
}

/* The linked pair publishes the same shared special twice per tick. Exercise the two edge cases
 * whose fired sample used to be overwritten by the second pass: an instant Repulsor-style special
 * and a one-tick recharge that fires again on its ready tick. */
void qa_test_special_light_events(void)
{
	hud_special_light_reset();

	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, true);
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();
	qa_check(hud_light_await_pop && hud_light_flash == 0,
	         "linked special light retains an instant special's fired edge across player two's pass");

	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();
	qa_check(!hud_light_await_pop && hud_light_flash == HUD_LIGHT_FLASH,
	         "linked instant special flashes on the next ready tick like single-player");

	hud_special_light_reset();
	hud_special_light_tick_begin();
	hud_special_light_publish(1, 0, true, true);
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();

	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, true);
	hud_special_light_publish(0, 0, false, false);
	hud_special_light_step_flash();
	qa_check(hud_light_await_pop && hud_light_flash == HUD_LIGHT_FLASH,
	         "linked one-tick recharge flashes and retains a same-tick refire for the next cycle");

	/* Rollback: the peer's shot and its whole recharge land on re-simulated ticks, so no live tick
	 * ever sees the fired edge. The meter having left full is what still owes the pop. */
	hud_special_light_reset();
	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();

	hud_special_light_tick_begin();
	hud_special_light_publish(4, 0, false, false);
	hud_special_light_step_flash();
	qa_check(hud_light_await_pop && hud_light_flash == 0,
	         "a meter that left full owes a pop for the shot only re-simulation saw");

	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();
	qa_check(!hud_light_await_pop && hud_light_flash == HUD_LIGHT_FLASH,
	         "that pop arrives when the meter comes back");

	/* A level that opens part way through a recharge has no full meter behind it, so it stays
	 * silent the way an outstanding fire from the previous level does. */
	hud_special_light_reset();
	hud_special_light_tick_begin();
	hud_special_light_publish(4, 0, false, false);
	hud_special_light_step_flash();
	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();
	qa_check(!hud_light_await_pop && hud_light_flash == 0,
	         "a recharge carried into a new level does not pop when it finishes");

	/* Ten ticks of lockout left of a two hundred tick recharge would open the bar at 95% without a
	 * phase of its own. The equip lands after that tick's clocks are published, so the request is
	 * raised between a publish and the next tick's opening, as a pickup raises it. */
	const JE_byte saved_special = player[0].items.special;
	player[0].items.special = 1;  // the local ship needs a block for the meter to take the request

	hud_special_light_reset();
	hud_special_light_tick_begin();
	hud_special_light_publish(200, 0, false, false);
	hud_special_light_step();

	hud_special_light_tick_begin();
	hud_special_light_publish(120, 0, false, false);
	const float part_way = hud_special_light_step();
	qa_check(part_way > 4.0f && part_way < 5.5f,
	         "the meter fills across the recharge it is measuring");

	hud_special_light_rearm(0);
	hud_special_light_tick_begin();
	hud_special_light_publish(9, 0, false, false);
	qa_check(hud_special_light_step() == 0.0f,
	         "a special equipped mid-recharge opens its meter from empty");

	hud_special_light_tick_begin();
	hud_special_light_publish(4, 0, false, false);
	const float new_phase = hud_special_light_step();
	qa_check(new_phase > 6.0f && new_phase < 7.0f,
	         "and climbs across the lockout it arrived with");

	// A ship whose block is not on screen leaves the meter alone.
	player[0].items.special = 0;
	hud_special_light_rearm(0);
	qa_check(!hud_light_rearm_pending,
	         "a special equipped off screen does not rescale the meter");
	player[0].items.special = saved_special;

	hud_special_light_reset();
}

int hud_top_right_left_edge(void)
{
	int left = PLAYFIELD_RIGHT + 1;  // nothing claimed over there
	const uint local_player = gameplay_local_player_index();

	if (hud_special_block_shown(local_player) && hud_special_on_right(local_player))
		left = hud_special_light_x(local_player);

	if (!twoPlayerMode || (galagaMode && !coop_mode_active()) || !hud_lives_shown())
		return left;

	const uint lives = hud_lives_count(1);

	// Mirror of the above: the label is right-aligned to PLAYFIELD_WIDTH + 22 and the lives
	// row starts at PLAYFIELD_WIDTH + 7 stepping left. "99" stands in for the widest count.
	const int name_left = PLAYFIELD_WIDTH + 22 - hud_player_name_width(1);
	const int lives_left = (lives > HUD_LIVES_ICONS_MAX) ? hud_lives_count_left("99")
	                     : (lives >= 1) ? PLAYFIELD_WIDTH + 7 - ((int)lives - 1) * 12
	                                    : PLAYFIELD_WIDTH + 7;

	if (name_left < left)  left = name_left;
	if (lives_left < left) left = lives_left;

	return left;
}

// The FPS and kill counters slide left around a right-edge bar instead.

// Bottom row of the top-left cluster (special block, arcade name/lives rows), or -1.
int hud_top_left_bottom_edge(void)
{
	int bottom = -1;
	const uint local_player = gameplay_local_player_index();

	if (hud_special_block_shown(local_player) && !hud_special_on_right(local_player))
		bottom = HUD_SPECIAL_ICON_Y + HUD_SPECIAL_ICON_H - 1;

	// The deepest ink is the collapsed count at y+3 (shaded TINY_FONT rows end at y+11).
	// Using y+11 for plain icon rows too keeps the span steady across the collapse point.
	if (hud_lives_shown())
	{
		const int rowBottom = hud_lives_row_y(0) + 11;
		if (rowBottom > bottom)
			bottom = rowBottom;
	}

	return bottom;
}

// Bottom row of the top-right cluster, or -1. Player two's rows mirror in only when
// JE_inGameDisplays draws a second lives row.
int hud_top_right_bottom_edge(void)
{
	int bottom = -1;
	const uint local_player = gameplay_local_player_index();

	if (hud_special_block_shown(local_player) && hud_special_on_right(local_player))
		bottom = HUD_SPECIAL_ICON_Y + HUD_SPECIAL_ICON_H - 1;

	if (hud_lives_shown() && !(onePlayerAction && !dual_ship_mode()))
	{
		const int rowBottom = hud_lives_row_y(1) + 11;
		if (rowBottom > bottom)
			bottom = rowBottom;
	}

	return bottom;
}

// Top row of the bottom-left corner's claims: player one's score, and the superbomb row above
// it. A dual-ship session draws only the local ship's bombs, and draws them in this corner.
int hud_bottom_left_top_edge(void)
{
	int top = HUD_SCORE_Y - 1;

	if (hud_superbomb_count(dual_ship_mode() ? gameplay_local_player_index() : 0) > 0)
		top = HUD_SUPERBOMB_Y;

	return top;
}

// Top row of the bottom-right corner's claims (player two's score and superbombs), or
// vga_height with the corner empty.
int hud_bottom_right_top_edge(void)
{
	int top = vga_height;

	if (twoPlayerMode && (!galagaMode || coop_mode_active()))
	{
		top = HUD_SCORE_Y - 1;
		// A dual-ship session draws only the local ship's bombs, in the left corner.
		if (!dual_ship_mode() && hud_superbomb_count(1) > 0)
			top = HUD_SUPERBOMB_Y;
	}

	return top;
}

void JE_inGameDisplays(void)
{
	char stemp[21];
	char tempstr[256];

	// Mirror player scores inside the playfield edges. Account for JE_textWidth's trailing pixel and
	// FULL_SHADE's one-pixel outline.
	const int SCORE_INSET = 3;

	// Galaga hides the wing's score, but co-op's second ship is a player with a wallet.
	for (uint i = 0; i < ((twoPlayerMode && (!galagaMode || coop_mode_active())) ? 2 : 1); ++i)
	{
		// Name the two scores whenever the ships are independent; the linked pair reads as one
		// team and has no room for it.
		if (dual_ship_mode())
			snprintf(tempstr, sizeof(tempstr), "%s %lld", JE_getName(i + 1), (long long)player[i].cash);
		else
			snprintf(tempstr, sizeof(tempstr), "%lld", (long long)player[i].cash);

		// Ink spans [x, x + width - 2] (width carries that trailing pixel); the shadow widens
		// it to [x - 1, x + width - 1]. Setting the right shadow edge to PLAYFIELD_RIGHT -
		// (SCORE_INSET - 1); the mirror of player 1's left shadow edge; rearranges to:
		const int width = JE_textWidth(tempstr, TINY_FONT);
		const int x = (i == 0)
		            ? PLAYFIELD_LEFT + SCORE_INSET
		            : PLAYFIELD_RIGHT - width - SCORE_INSET + 2;

		// Endless draws in the light cone's pale ink throughout: its random level order leaves no
		// fixed backdrop for the dark green default to sit against.
		if (smoothies[6 - 1] || endlessMode)
			JE_textShade(VGAScreen, x, vga_height - 25, tempstr, 8, 8, FULL_SHADE);
		else
			JE_textShade(VGAScreen, x, vga_height - 25, tempstr, 2, 4, FULL_SHADE);
	}

	// Show the local ship's Endless kill buff in the bottom-right. Shift it up or
	// left when a boss bar occupies that corner.
	endlessSetFxPlayer(gameplay_local_player_index());
	if (endlessFxActive() && endlessTurbodriveActive())
	{
		const int bank = endlessKillBuffColorBank();
		const int baseRightX = PLAYFIELD_LEFT + PLAYFIELD_WIDTH - 5 + 2;  // +2px right of the FPS counter's edge
		const int rightX = baseRightX - boss_bar_hud_left_shift(baseRightX);
		const int yBase = 7;  // +7px down from the original placement

		// This readout is last in the bottom-band precedence, so it lifts clear of everything else
		// rather than the other way round: the FPS counter below it, and a BOTTOM boss bar that may
		// have grown upward into this space.
		int floorRow = vga_height;  // nothing below to avoid
		if (show_fps)
		{
			const int fps_top = hud_fps_row() - 1;
			if (fps_top < floorRow)
				floorRow = fps_top;
		}
		{
			const int bar_top = boss_bar_bottom_band_top();
			if (bar_top < floorRow)
				floorRow = bar_top;
		}
		// Endless is one-player, so superbomb icons march rightward from the far side;
		// only a long row reaches under this readout.
		if (hud_superbomb_count(0) > 0
		    && hud_superbomb_p1_right() >= rightX - 60
		    && HUD_SUPERBOMB_Y < floorRow)
		{
			floorRow = HUD_SUPERBOMB_Y;
		}

		const int naturalBottom = vga_height - 26 + yBase;  // the timer bar's bottom row
		int yShift = naturalBottom - (floorRow - 2);
		if (yShift < 0)
			yShift = 0;

		char buf[48];

		snprintf(buf, sizeof(buf), "x%d", endlessKillBuffComboCount());
		JE_textShade(VGAScreen, rightX - JE_textWidth(buf, TINY_FONT), vga_height - 45 + yBase - yShift, buf, bank, 5, FULL_SHADE);

		// Middle line reports the active buff/curse cleanly: a BOON shows only the effects it grants
		//   fire boost (Turbodrive/Overdrive) -> "FIRE xN", damage stacks (Overdrive/Overblast) -> "DMG+N%";
		//   an evil curse shows its one-word name; JAMMED (Backfire) / BURNOUT / MISFIRE.
		if (endlessKillFireIsEvil())
		{
			snprintf(buf, sizeof(buf), "%s", endlessKillFireEvilName());
		}
		else
		{
			const int fireMult = endlessKillBuffFireMultiplier();
			const int dmgPct = endlessKillBuffDamagePercent();
			if (fireMult > 1 && dmgPct > 0)
				snprintf(buf, sizeof(buf), "FIREx%d DMG+%d%%", fireMult, dmgPct);
			else if (fireMult > 1)
				snprintf(buf, sizeof(buf), "FIREx%d", fireMult);
			else
				snprintf(buf, sizeof(buf), "DMG+%d%%", dmgPct);   // Overblast: damage only, no fire boost
		}
		JE_textShade(VGAScreen, rightX - JE_textWidth(buf, TINY_FONT), vga_height - 37 + yBase - yShift, buf, bank, 3, FULL_SHADE);

		const int bw = 60;
		const int mt = endlessKillBuffTicksMax();
		const int fillw = (mt > 0) ? bw * endlessKillBuffTicksLeft() / mt : 0;
		const int barX = rightX - bw;
		const int barY0 = vga_height - 28 + yBase - yShift;
		const int barY1 = vga_height - 26 + yBase - yShift;
		rl_mark_overlay_rect(barX, barY0, rightX - barX + 1, barY1 - barY0 + 1);
		fill_rectangle_xy(VGAScreen, barX, barY0, rightX, barY1, bank * 16 + 2);  // dark track
		// Fill with a very weak vertical gradient within the buff's palette bank: brightest on the
		// top row, one shade darker per row down the bar's 3px height; a subtle top-to-bottom shade.
		if (fillw > 0)
			for (int y = barY0; y <= barY1; ++y)
			{
				int shade = 11 - (y - barY0);  // +11 top -> +9 bottom (one step per row; the bar is 3px tall)
				fill_rectangle_xy(VGAScreen, barX, y, barX + fillw, y, bank * 16 + shade);
			}
	}

	/*Special Weapon?*/
	const uint local_player = gameplay_local_player_index();
	if (hud_special_block_shown(local_player))
	{
		draw_special_icon(VGAScreen, hud_special_icon_x(local_player), HUD_SPECIAL_ICON_Y,
		                  player[local_player].items.special);
		draw_special_ready_light(hud_special_light_x(local_player), HUD_SPECIAL_LIGHT_Y);
	}
	else
	{
		hud_special_light_reset();  // no block on screen; the next special charges from empty
	}

	/*Lives Left*/
	if (arcade_rules_active())
	{
		// One-player Action draws one row -- except in co-op, whose mini-games fly two
		// full ships and give each its own row, the same as Separate arcade.
		for (int temp = 0; temp < ((onePlayerAction && !dual_ship_mode()) ? 1 : 2); temp++)
		{
			const uint lives = hud_lives_count((uint)temp);

			// Only the ship carrying the special block moves down for it, so a Separate-arcade
			// joiner drops its own row and leaves player one's where it was.
			int y = hud_lives_row_y((uint)temp);
			// P2 anchors ride the widened playfield edge (legacy 270/250 would float
			// mid-field now), matching the name label's PLAYFIELD_WIDTH mapping below.
			tempW = (temp == 0) ? 30 : PLAYFIELD_WIDTH + 7;

			if (lives > HUD_LIVES_ICONS_MAX)
			{
				blit_sprite2(VGAScreen, tempW, y, spriteSheet9, 285);
				sprintf(tempstr, "%u", lives);
				// Both counts sit 3px from the icon: P1's runs right from x45, P2's is right-aligned
				// so its last pixel lands 3px left of the icon, whatever the digit count.
				tempW = (temp == 0) ? 45 : hud_lives_count_left(tempstr);
				JE_textShade(VGAScreen, tempW, y + 3, tempstr, 15, 1, FULL_SHADE);
			}
			else
			{
				for (uint i = 0; i < lives; ++i)
				{
					blit_sprite2(VGAScreen, tempW, y, spriteSheet9, 285);

					tempW += (temp == 0) ? 12 : -12;
				}
			}

			strcpy(stemp, (temp == 0) ? miscText[49-1] : miscText[50-1]);
			if (isNetworkGame)
			{
				strcpy(stemp, JE_getName(temp+1));
			}

			tempW = (temp == 0) ? 28 : (PLAYFIELD_WIDTH + 22 - JE_textWidth(stemp, TINY_FONT));
			JE_textShade(VGAScreen, tempW, y - HUD_LIVES_NAME_RISE, stemp, 2, 6, FULL_SHADE);
		}
	}

	/*Super Bombs!!*/
	// Each ship carries its own stock in a dual-ship session, so this machine shows only its
	// own row; the linked pair shows both, player two's counted in from the right edge.
	const uint first_bomb_player = dual_ship_mode() ? local_player : 0;
	const uint last_bomb_player = dual_ship_mode() ? local_player + 1 : COUNTOF(player);
	for (uint i = first_bomb_player; i < last_bomb_player; ++i)
	{
		int x = dual_ship_mode() || i == 0 ? 30 : PLAYFIELD_WIDTH + 7;

		for (uint j = hud_superbomb_count(i); j > 0; --j)
		{
			blit_sprite2(VGAScreen, x, 160, spriteSheet9, 304);
			x += (dual_ship_mode() || i == 0) ? 12 : -12;
		}
	}

	if (youAreCheating)
	{
		JE_outText(VGAScreen, 90, 170, "Cheaters always prosper.", 3, 4);
	}

	/* Optional FPS counter, bottom-right of the playfield (game_screen space, so
	 * the +24 composite offset keeps it just inside the right edge before the HUD). */
	if (show_fps)
	{
		char fps_str[16];
		snprintf(fps_str, sizeof(fps_str), "%d FPS", current_fps);

		// A right-side vertical boss bar owns the last few columns; slide left to clear it,
		// exactly as the endless readout does.
		const int fps_right_base = PLAYFIELD_LEFT + PLAYFIELD_WIDTH - 5;  // ~x318
		const int fps_right = fps_right_base - boss_bar_hud_left_shift(fps_right_base);
		const int fps_x = fps_right - JE_textWidth(fps_str, TINY_FONT);

		// Player 2's score is right-aligned into this same corner, so the counter stacks
		// above it when both are on screen (hud_fps_row).
		JE_textShade(VGAScreen, fps_x, hud_fps_row(), fps_str, 15, 2, FULL_SHADE);
	}

	JE_drawDebugOverlays();
}

void JE_mainKeyboardInput(void)
{
	JE_gammaCheck();

	/* { Network Request Commands } */

	if (!isNetworkGame)
	{
		/* { Edited Ships } for Player 1 */
		if (extraAvail && extraShipsAllowedInGame() && keysactive[SDL_SCANCODE_TAB])
		{
			for (x = SDL_SCANCODE_1; x <= SDL_SCANCODE_0; x++)
			{
				if (keysactive[x])
				{
					rollback_taint("edit-ship-1");
					extraShipRememberStandard(0);
					endlessNoteCustomShip();
					int z = x - SDL_SCANCODE_1 + 1;
					player[0].items.ship = 90 + z;                     /*Ships*/
					z = (z - 1) * 15;
					player[0].items.weapon[FRONT_WEAPON].id = extraShipResolvePort(0, extraShips[z + 1]);
					player[0].items.weapon[REAR_WEAPON].id = extraShipResolvePort(0, extraShips[z + 2]);
					player[0].items.special = extraShips[z + 3];
					player[0].items.sidekick[LEFT_SIDEKICK] = extraShips[z + 4];
					player[0].items.sidekick[RIGHT_SIDEKICK] = extraShips[z + 5];
					player[0].items.generator = extraShips[z + 6];
					/*Armor*/
					player[0].items.shield = extraShips[z + 8];
					extraShipLoadoutRefresh(0, true);

					keysactive[x] = false;
				}
			}
		}
	}

	/* { In-Game Help } */
	if (keysactive[SDL_SCANCODE_F1])
	{
		if (isNetworkGame)
		{
			helpRequest = true;
		}
		else
		{
			JE_inGameHelp();
			skipStarShowVGA = true;
		}
	}

	/* {!Activate Nort Ship!} */
	if (keysactive[SDL_SCANCODE_F2] && keysactive[SDL_SCANCODE_F4] && keysactive[SDL_SCANCODE_F6] && keysactive[SDL_SCANCODE_F7] &&
	    keysactive[SDL_SCANCODE_F9] && keysactive[SDL_SCANCODE_BACKSLASH] && keysactive[SDL_SCANCODE_SLASH])
	{
		if (isNetworkGame)
		{
			nortShipRequest = true;
		}
		else
		{
			rollback_taint("nort-ship");
			player[0].items.ship = 12;                     // Nort Ship
			player[0].items.special = 13;                  // Astral Zone
			player[0].items.weapon[FRONT_WEAPON].id = 36;  // NortShip Super Pulse
			player[0].items.weapon[REAR_WEAPON].id = 37;   // NortShip Spreader
			shipGr = 1;
		}
	}

	/* {Cheating} */
	if (!isNetworkGame && !twoPlayerMode && !superTyrian && superArcadeMode == SA_NONE)
	{
		if (keysactive[SDL_SCANCODE_F2] && keysactive[SDL_SCANCODE_F3] && keysactive[SDL_SCANCODE_F6])
		{
			rollback_taint("cheat-toggle");
			youAreCheating = !youAreCheating;
			keysactive[SDL_SCANCODE_F2] = false;
		}

		if (keysactive[SDL_SCANCODE_F2] && keysactive[SDL_SCANCODE_F3] && (keysactive[SDL_SCANCODE_F4] || keysactive[SDL_SCANCODE_F5]))
		{
			rollback_taint("cheat-armor");
			for (uint i = 0; i < COUNTOF(player); ++i)
				player[i].armor = 0;

			youAreCheating = !youAreCheating;
			JE_drawTextWindow(miscText[63-1]);
		}

		if (constantPlay && keysactive[SDL_SCANCODE_C])
		{
			rollback_taint("cheat-constant");
			youAreCheating = !youAreCheating;
			keysactive[SDL_SCANCODE_C] = false;
		}
	}

	if (superTyrian)
	{
		youAreCheating = false;
	}

	/* {Personal Commands} */

	/* {DEBUG} */
	if (keysactive[SDL_SCANCODE_F10] && keysactive[SDL_SCANCODE_BACKSPACE])
	{
		keysactive[SDL_SCANCODE_F10] = false;
		debug = !debug;

		debugHist = 1;
		debugHistCount = 1;

		/* YKS: clock ticks since midnight replaced by SDL_GetTicks */
		lastDebugTime = SDL_GetTicks();
	}

	/* {CHEAT-SKIP LEVEL} */
	if (keysactive[SDL_SCANCODE_F2] && keysactive[SDL_SCANCODE_F6] && (keysactive[SDL_SCANCODE_F7] || keysactive[SDL_SCANCODE_F8]) && !keysactive[SDL_SCANCODE_F9] &&
	    !superTyrian && superArcadeMode == SA_NONE)
	{
		if (isNetworkGame)
		{
			skipLevelRequest = true;
		}
		else
		{
			rollback_taint("skip-level");
			levelTimer = true;
			levelTimerCountdown = 0;
			endLevel = true;
			levelEnd = 40;
		}
	}

	/* pause game */
	pause_pressed = pause_pressed || keysactive[SDL_SCANCODE_P];

	/* in-game setup */
	ingamemenu_pressed = ingamemenu_pressed || keysactive[SDL_SCANCODE_ESCAPE];

	if (keysactive[SDL_SCANCODE_BACKSPACE])
	{
		/* toggle screenshot pause */
		if (keysactive[SDL_SCANCODE_NUMLOCKCLEAR])
			superPause = !superPause;

		/* {SMOOTHIES} */
		if (keysactive[SDL_SCANCODE_F12] && keysactive[SDL_SCANCODE_SCROLLLOCK])
		{
			rollback_taint("smoothies-key");
			for (temp = SDL_SCANCODE_2; temp <= SDL_SCANCODE_9; temp++)
				if (keysactive[temp])
					smoothies[temp-SDL_SCANCODE_2] = !smoothies[temp-SDL_SCANCODE_2];
			if (keysactive[SDL_SCANCODE_0])
				smoothies[8] = !smoothies[8];
		}
		else

		/* {CYCLE THROUGH FILTER COLORS} */
		if (keysactive[SDL_SCANCODE_MINUS])
		{
			rollback_taint("filter-key");
			if (levelFilter == -99)
			{
				levelFilter = 0;
			}
			else
			{
				levelFilter++;
				if (levelFilter == 16)
					levelFilter = -99;
			}
		}
		else

		/* {HYPER-SPEED} */
		if (keysactive[SDL_SCANCODE_1])
		{
			rollback_taint("hyper-speed");
			fastPlay++;
			if (fastPlay > 2)
				fastPlay = 0;
			keysactive[SDL_SCANCODE_1] = false;
			JE_setNewGameSpeed();
		}

		/* {IN-GAME RANDOM MUSIC SELECTION} */
		if (keysactive[SDL_SCANCODE_SCROLLLOCK])
		{
			// Draws mt_rand outside the recorded input path; taints the tick.
			rollback_taint("random-music");
			play_song(mt_rand() % MUSIC_NUM);
		}
	}
}

void JE_pauseGame(void)
{
	// Pause is offline-only. It halts this machine alone, so online it would strand the
	// other player; this guard holds whatever route the call arrived by.
	if (isNetworkGame)
		return;

	// A modal UI mid-tick makes the tick non-replayable; skip self-test verify.
	rollback_taint("pause");

	// The pause overlay presents frames while the tick's render-list recording is
	// active; suspend it (see JE_doInGameSetup).
	const bool rl_was_recording = render_list_recording;
	render_list_recording = false;

	mouseSetRelative(false);

	JE_boolean done = false;
	JE_word mouseX, mouseY;

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	if (!superPause)
	{
		JE_dString(VGAScreenSeg, 120, 90, miscText[22], FONT_SHAPES);

		VGAScreen = VGAScreenSeg;
		JE_showVGA();
	}

	set_volume(tyrMusicVolume / 2, fxVolume);

	wait_noinput(false, false, true); // debounce before the next input loop

	do
	{
		setDelay(2);

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		if ((newkey && lastkey_scan != SDL_SCANCODE_LCTRL && lastkey_scan != SDL_SCANCODE_RCTRL && lastkey_scan != SDL_SCANCODE_LALT && lastkey_scan != SDL_SCANCODE_RALT) ||
			JE_mousePosition(&mouseX, &mouseY) > 0)
		{
			done = true;
		}

		wait_delay();
	} while (!done);

	set_volume(tyrMusicVolume, fxVolume);

	VGAScreen = temp_surface; /* side-effect of game_screen */

	mouseSetRelative(true);

	render_list_recording = rl_was_recording;
}

// Player seat currently being drawn by JE_playerMovement.
static uint ship_draw_seat = 0;

// Combine the online style with kill-fire tint; kill-fire tint wins.
static NetShipStyle ship_draw_style(void)
{
	NetShipStyle style = netStyleForSeat(ship_draw_seat);
	const int tint = endlessShipTintFilter();
	if (tint)
		style.bank = (Sint8)(tint >> 4);
	return style;
}

// Shared hull, trim, and sidekick blits preserve the caller's render-list tag.
static void blit_ship2x2(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	const NetShipStyle style = ship_draw_style();
	blit_sprite2x2_alpha(surface, x, y, sheet, index, style.bank, style.opacity);
}

static void blit_ship2(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	const NetShipStyle style = ship_draw_style();
	blit_sprite2_alpha(surface, x, y, sheet, index, style.bank, style.opacity);
}

// Faded ships omit shadows because shadows cannot share sprite opacity.
static bool ship_draw_casts_shadow(void)
{
	return netStyleForSeat(ship_draw_seat).opacity >= NET_STYLE_SOLID;
}

// Hull indicators fade with the ship but keep their original bank.
static void blit_ship_indicator2(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	blit_sprite2_alpha(surface, x, y, sheet, index, -1, netStyleForSeat(ship_draw_seat).opacity);
}

// Damage and noclip blends ignore drive tint and halve the online opacity.
static void blit_ship2x2_blend(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	const NetShipStyle style = netStyleForSeat(ship_draw_seat);
	if (netStyleIsPlain(style))
		blit_sprite2x2_blend(surface, x, y, sheet, index);
	else
		blit_sprite2x2_alpha(surface, x, y, sheet, index, style.bank, (Uint8)(style.opacity / 2));
}

// Resting/home position for a front-mounted (style 2) option.
static int front_option_home_x(const Player *this_player, uint i)
{
	const bool both = this_player->sidekick[LEFT_SIDEKICK].style == 2
	               && this_player->sidekick[RIGHT_SIDEKICK].style == 2;
	if (!both)
		return this_player->x;
	return (i == LEFT_SIDEKICK) ? this_player->x - FRONT_OPTION_SPREAD
	                            : this_player->x + FRONT_OPTION_SPREAD;
}

// Front-mounted (launchable) option physics for one sidekick slot.
static void JE_frontOption(Player *this_player, uint i, int home_x, JE_boolean launch_pressed)
{
	int temp;

	if (!optionAttachmentLinked[i])
	{
		this_player->sidekick[i].y += optionAttachmentMove[i] / 2;
		if (optionAttachmentMove[i] >= -2)
		{
			if (optionAttachmentReturn[i])
				temp = 2;
			else
				temp = 0;

			if (this_player->sidekick[i].y > (this_player->y - 20) + 5)
			{
				temp = 2;
				optionAttachmentMove[i] -= 1 + optionAttachmentReturn[i];
			}
			else if (this_player->sidekick[i].y > (this_player->y - 20) - 0)
			{
				temp = 3;
				if (optionAttachmentMove[i] > 0)
					optionAttachmentMove[i]--;
				else
					optionAttachmentMove[i]++;
			}
			else if (this_player->sidekick[i].y > (this_player->y - 20) - 5)
			{
				temp = 2;
				optionAttachmentMove[i]++;
			}
			else if (optionAttachmentMove[i] < 2 + optionAttachmentReturn[i] * 4)
			{
				optionAttachmentMove[i] += 1 + optionAttachmentReturn[i];
			}

			if (optionAttachmentReturn[i])
				temp = temp * 2;
			if (abs(this_player->sidekick[i].x - home_x) < temp)
				temp = 1;

			if (this_player->sidekick[i].x > home_x)
				this_player->sidekick[i].x -= temp;
			else if (this_player->sidekick[i].x < home_x)
				this_player->sidekick[i].x += temp;

			if (abs(this_player->sidekick[i].y - (this_player->y - 20)) + abs(this_player->sidekick[i].x - home_x) < 8)
			{
				optionAttachmentLinked[i] = true;
				soundQueue[2] = S_CLINK;
			}

			if (launch_pressed)
				optionAttachmentReturn[i] = true;
		}
		else  // sidekick needs to catch up to player
		{
			optionAttachmentMove[i] += 1 + optionAttachmentReturn[i];
			JE_setupExplosion(this_player->sidekick[i].x + 1, this_player->sidekick[i].y + 10, 0, 0, false, false);
		}
	}
	else
	{
		this_player->sidekick[i].x = home_x;
		this_player->sidekick[i].y = this_player->y - 20;
		if (launch_pressed)
		{
			optionAttachmentLinked[i] = false;
			optionAttachmentReturn[i] = false;
			optionAttachmentMove[i] = -20;
			soundQueue[3] = S_WEAPON_26;
		}
	}

	if (this_player->sidekick[i].y < 10)
		this_player->sidekick[i].y = 10;
}

static Uint16 link_gun_angle_to_wire(float angle)
{
	while (angle < 0.0f)
		angle += (float)(2.0 * M_PI);
	while (angle >= (float)(2.0 * M_PI))
		angle -= (float)(2.0 * M_PI);

	// Quantized to 256 steps (~1.4 degrees): raw stick jitter in the low bits reads as a fresh
	// aim every tick and needlessly buys rollback corrections or delay-state churn.
	return (Uint16)(angle * (float)(65536.0 / (2.0 * M_PI))) & 0xFF00;
}

static float link_gun_angle_from_wire(Uint16 angle)
{
	return (float)angle * (float)(2.0 * M_PI / 65536.0);
}

/* The RB_MOVE_* bits for a tick's displacement (positive right and down): the dominant axis as the
 * classic |dx|>|dy| turret test chose it, and RB_MOVE_DIAG for a tick SF_twiddleTarget keeps on
 * both axes, so the peer's twiddle detector reads the same neutral tick. */
Uint16 rb_move_bits(int dx, int dy)
{
	Uint16 bits = 0;
	if (abs(dx) > abs(dy))
		bits |= (dx > 0) ? RB_MOVE_RIGHT : RB_MOVE_LEFT;
	else if (dy != 0)
		bits |= (dy > 0) ? RB_MOVE_DOWN : RB_MOVE_UP;

	int tx, ty;
	SF_twiddleTarget(0, 0, dx, dy, &tx, &ty);
	if (tx != 0 && ty != 0)
		bits |= RB_MOVE_DIAG;
	return bits;
}

/* The direction the twiddle detector rebuilds from those bits. Any two-axis target reads as
 * neutral, so a DIAG tick becomes one diagonal; which one does not matter. */
void rb_move_dir(Uint16 bits, int *out_dx, int *out_dy)
{
	*out_dx = (bits & RB_MOVE_RIGHT) ? 1 : (bits & RB_MOVE_LEFT) ? -1 : 0;
	*out_dy = (bits & RB_MOVE_DOWN) ? 1 : (bits & RB_MOVE_UP) ? -1 : 0;
	if (bits & RB_MOVE_DIAG)
	{
		*out_dx = 1;
		*out_dy = 1;
	}
}

/* Capture the effective per-tick input tuple for a player after the movement
 * routine ran: absolute post-movement position, aim anchors, banking accel,
 * buttons and link-gun state.  This tuple is the simulation's only input door
 * in rollback netplay and in the self-test replay. */
static void rb_fill_tuple(RbInput *in, const Player *this_player,
                          JE_word mx, JE_word my,
                          JE_integer accelXC, JE_integer accelYC,
                          bool link_analog, float link_angle)
{
	memset(in, 0, sizeof(*in));
	in->x = (Sint16)this_player->x;
	in->y = (Sint16)this_player->y;
	in->velX = (Sint16)(this_player->x_velocity > 127 ? 127 :
	                    (this_player->x_velocity < -127 ? -127 : this_player->x_velocity));
	in->velY = (Sint16)(this_player->y_velocity > 127 ? 127 :
	                    (this_player->y_velocity < -127 ? -127 : this_player->y_velocity));
	in->mouseX = (Sint16)mx;
	in->mouseY = (Sint16)my;
	/* The wire carries one signed byte per axis; the sim never produces more. */
	in->accelX = (Sint16)(accelXC > 127 ? 127 : (accelXC < -127 ? -127 : accelXC));
	in->accelY = (Sint16)(accelYC > 127 ? 127 : (accelYC < -127 ? -127 : accelYC));

	Uint16 buttons = 0;
	for (int i = 4 - 1; i >= 0; i--)
	{
		buttons <<= 1;
		buttons |= button[i] ? 1 : 0;
	}
	in->buttons = buttons;

	// Movement intent for the docked-link tests and the twiddle detector: this tick's real input
	// displacement, captured before the dock pin rewrites x/y.
	in->buttons |= rb_move_bits((int)in->x - (int)in->mouseX, (int)in->y - (int)in->mouseY);

	if (link_analog)
	{
		in->buttons |= RB_LINK_ANALOG;
		in->linkAngle = link_gun_angle_to_wire(link_angle);
	}
}

static void rb_apply_tuple(const RbInput *in, Player *this_player,
                           JE_integer *accelXC_, JE_integer *accelYC_,
                           bool *link_analog, float *link_angle)
{
	Uint16 buttons = in->buttons;
	for (int i = 0; i < 4; i++)
	{
		button[i] = buttons & 1;
		buttons >>= 1;
	}
	this_player->x = in->x;
	this_player->y = in->y;
	this_player->x_velocity = (int)in->velX;
	this_player->y_velocity = (int)in->velY;
	*accelXC_ = (JE_integer)in->accelX;
	*accelYC_ = (JE_integer)in->accelY;
	*link_analog = (in->buttons & RB_LINK_ANALOG) != 0;
	*link_angle = link_gun_angle_from_wire(in->linkAngle);
}

/* Repaint the displayed ship's ammo gauge. Silent rollback passes set the HUD dirty instead of
 * drawing predicted values. */
static void JE_drawSidekickAmmoGauge(JE_byte playerNum, uint slot, int ammo, int ammo_max, bool wipe)
{
	if ((uint)(playerNum - 1) != hud_sidekick_player_index())
		return;

	if (rollback_resim_silent)
	{
		hud_sidekicks_dirty = true;
		return;
	}

	const int y = hud_sidekick_ammo_y(slot);
	const int hud_x = HUD_X(284);

	if (wipe)
		fill_rectangle_xy(VGAScreenSeg, hud_x, y, hud_x + 28, y + 2, 0);
	draw_segmented_gauge(VGAScreenSeg, hud_x, y, 112, 2, 2, AMMO_GAUGE_STEP(ammo_max), ammo);
}

/* Apply ship-owned Endless effects. Co-op calls this for both ships; other modes keep the
 * player-one effect owner. */
void endlessPerShipTick(Player *this_player)
{
	if (!endlessFxActive() || !(coopEndlessMode || this_player == &player[0]))
		return;

	if (!vt_ship_owns())  // the VT ship (normal play) applies gravity in vt_ship_step
	{
		// X is nonzero only for an omnidirectional well; both axes are clamped to the playfield
		// at the end of JE_playerMovement, so a sideways/up pull just pins the ship at that edge.
		const uint p = (uint)(this_player - player);
		this_player->x += endlessGravityPullX(p);
		this_player->y += endlessGravityPullY(p);
	}

	// Rapid Cyclers and Turbodrive both reduce the firing delay. Let the counter
	// reach zero so stacked bonuses can fire once per tick.
	const int dec = endlessPerkFireDecrements()
	              + (endlessTurbodriveActive() ? endlessKillBuffFireDecrements() : 0);
	for (unsigned i = 0; i < COUNTOF(shotRepeat); i++)
		for (int k = 0; k < dec && shotRepeat[i] > 0; k++)
			--shotRepeat[i];

	// Rapid Recharge perk: extra decrements to the special cooldown gate + each sidekick's
	// ammo-refill counter (skips main guns). The accumulator is stateful and read exactly once.
	const int specDec = endlessPerkSpecialCooldownDecrements();
	for (int k = 0; k < specDec && shotRepeat[SHOT_SPECIAL] > 0; k++)
		--shotRepeat[SHOT_SPECIAL];

	for (uint i = 0; i < COUNTOF(this_player->sidekick); i++)
	{
		if (this_player->sidekick[i].ammo_max <= 0)
			continue;  // only weapons that actually use recharging ammo
		for (int k = 0; k < specDec && this_player->sidekick[i].ammo_refill_ticks > 0; k++)
			--this_player->sidekick[i].ammo_refill_ticks;
	}
}

/* Spend a charged Opening Salvo on the volley this ship is about to fire, and answer whether
 * one was there. See doc/notes.md#perks. */
bool endlessArmOpeningSalvoForTick(Player *this_player, JE_byte playerNum_)
{
	if (!endlessFxActive() || !(coopEndlessMode || this_player == &player[0]))
		return false;
	if (twoPlayerLinked && playerNum_ == 2)
		return false;
	if (twoPlayerMode && !dual_ship_mode() && playerNum_ != 1)
		return false;
	if (this_player->items.weapon[SHOT_FRONT].id == 0 || shotRepeat[SHOT_FRONT] > 0 || !button[1-1])
		return false;

	return endlessOpeningSalvoConsume();
}

void JE_playerMovement(Player *this_player,
                       JE_byte inputDevice,
                       JE_byte playerNum_,
                       JE_word shipGr_,
                       Sprite2_array *shipGrPtr_,
                       JE_word *mouseX_, JE_word *mouseY_)
{
	JE_integer mouseXC, mouseYC;
	JE_integer accelXC, accelYC;

	// Everything from here to the ship blit is this ship's: its own drives, tint and cadence.
	endlessSetFxPlayer((uint)(this_player - &player[0]));
	ship_draw_seat = (uint)(this_player - &player[0]);

	if (playerNum_ == 2 || !twoPlayerMode || dual_ship_mode())
	{
		tempW = weaponPort[this_player->items.weapon[REAR_WEAPON].id].opnum;

		if (this_player->weapon_mode > tempW)
			this_player->weapon_mode = 1;
	}

	/* Endless run-wide per-tick hooks. These advance the run's own clocks, so they run ONCE a
	 * tick and player 1 is the ship that carries them. Everything a ship owns for itself is in
	 * the per-ship block below, which every ship reaches. */
	if (endlessFxActive() && this_player == &player[0])
	{
		endlessGameplayTick();
		if (endlessConsumeArmorHudDirty())  // the Overheat DoT just shaved hull; repaint the event-driven armor bar
		{
			JE_wipeShieldArmorBars();
			VGAScreen = VGAScreenSeg;
			JE_drawShield();
			JE_drawArmor();
			VGAScreen = game_screen;
		}
	}

	endlessPerShipTick(this_player);

#ifdef WITH_NETWORK
	// Lockstep state packets; rollback mode replaces them with the input stream.
	if (isNetworkGame && thisPlayerNum == playerNum_ && !nrb_active())
	{
		network_state_prepare();
		memset(&packet_state_out[0]->data[4], 0, 10);
	}
#endif

redo:

	if (isNetworkGame)
	{
		inputDevice = 0;
	}

	mouseXC = 0;
	mouseYC = 0;
	accelXC = 0;
	accelYC = 0;

	// Select the ship-physics path. Rollback peers use the host's choice; a docked Dragonwing still
	// follows the fixed-tick path that pins it to player 1.
	const bool vt_sim_owns = (isNetworkGame && nrb_active())
	                       ? (nrb_session_vt() && frameCountMax > 0 && !endLevel)
	                       : vt_ship_owns();
	const bool vt = vt_sim_owns && !(playerNum_ == 2 && twoPlayerLinked);
	// Which paths this machine's LIVE INPUT flows through; always the local
	// setup: these only shape the tuple this machine records, so they are free
	// to differ per machine.
	const bool vt_input = vt_ship_owns() && !(playerNum_ == 2 && twoPlayerLinked);

	bool link_gun_analog = false;
	float link_gun_angle = 0;

	/* Draw Player */
	if (!this_player->is_alive)
	{
		if (this_player->exploding_ticks > 0)
		{
			--this_player->exploding_ticks;

			// The wreck is gone: the ship either respawns below or is out with empty gauges.
			// Both states need one repaint, and no other event guarantees it.
			if (this_player->exploding_ticks == 0)
				hud_bars_dirty = true;

			if (levelEndFxWait > 0)
			{
				levelEndFxWait--;
			}
			else
			{
				levelEndFxWait = (mt_rand() % 6) + 3;
				if ((mt_rand() % 3) == 1)
					soundQueue[6] = S_EXPLOSION_9;
				else
					soundQueue[5] = S_EXPLOSION_11;
			}

			int explosion_x = this_player->x + (mt_rand() % 32) - 16;
			int explosion_y = this_player->y + (mt_rand() % 32) - 16;
			JE_setupExplosionLarge(false, 0, explosion_x, explosion_y + 7);
			JE_setupExplosionLarge(false, 0, this_player->x, this_player->y + 7);

			if (levelEnd > 0)
				levelEnd--;
		}
		else
		{
			if (arcade_rules_active())
			{
				if (*this_player->lives > 1)  // respawn if any extra lives
				{
					--(*this_player->lives);
					// One life poorer, so both ceilings come back down before the refill below
					// re-derives the respawn armour and shield from them.
					arcade_rescale_to_lives(this_player);

					reallyEndLevel = false;
					shotMultiPos[playerNum_-1] = 0;
					calc_purple_balls_needed(this_player);
					twoPlayerLinked = false;
					// Galaga loses the spawned wing on a respawn; co-op's second ship is a
					// player, not a wing, so the pair stays a pair.
					if (galagaMode && !coop_mode_active())
						twoPlayerMode = false;
					this_player->y = SHIP_BOTTOM_MARGIN;
					this_player->invulnerable_ticks = 100;
					this_player->is_alive = true;
					endLevel = false;

					// Life-scaled arcade ships respawn with both gauges at their new ceilings.
					const bool boostedRefill = arcade_life_scaling_active();

					if (galagaMode || episodeNum == 4 || boostedRefill)
						this_player->armor = this_player->initial_armor;
					else
						this_player->armor = this_player->initial_armor / 2;

					if (galagaMode)
						this_player->shield = 0;  // galaga starts you shieldless, boost or not
					else if (boostedRefill)
						this_player->shield = this_player->shield_max;
					else
						this_player->shield = this_player->shield_max / 2;

					VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
					JE_drawArmor();
					JE_drawShield();
					VGAScreen = game_screen; /* side-effect of game_screen */
					goto redo;
				}
				else
				{
					if (galagaMode && !coop_mode_active())
						twoPlayerMode = false;
					if (allPlayersGone && isNetworkGame)
						reallyEndLevel = true;
				}

			}
		}
	}
	else if (constantDie)
	{
		// finished exploding?  start dying again
		if (this_player->exploding_ticks == 0)
		{
			this_player->shield = 0;

			if (this_player->armor > 0)
			{
				--this_player->armor;
			}
			else
			{
				this_player->is_alive = false;
				this_player->exploding_ticks = 60;
				if (coopEndlessMode)
					endlessPlayerDowned[this_player - &player[0]] = true;
				levelEnd = 40;
			}

			JE_wipeShieldArmorBars();
			VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
			JE_drawArmor();
			VGAScreen = game_screen; /* side-effect of game_screen */

			// Preserve the mode's repeating instant-death loop.
			if (*player[0].lives < ARCADE_LIVES_MAX)
			{
				++(*player[0].lives);
				arcade_rescale_to_lives(&player[0]);
			}
		}
	}

	if (!this_player->is_alive)
	{
		explosionFollowAmountX = explosionFollowAmountY = 0;
		return;
	}

	if (!endLevel)
	{
		*mouseX_ = this_player->x;
		*mouseY_ = this_player->y;
		// Endless SLUGGISH (classic non-VT path): snapshot the tick-start position so the whole net
		// move can be rescaled at the end. Y is snapshotted separately because the inverted-control
		// flip below rewrites *mouseY_.
		const int sluggishStartX = this_player->x;
		const int sluggishStartY = this_player->y;
		button[1-1] = false;
		button[2-1] = false;
		button[3-1] = false;
		button[4-1] = false;

		// Wire-test gameplay: scripted fire, so the weapons and every sidekick mount do real
		// work. Applied where the devices would have been sampled; a replay pass takes its
		// input from the recorded tuple instead.
		if (qa_net_gameplay_ticks > 0 && (!isNetworkGame || playerNum_ == thisPlayerNum)
		    && !rollback_resim && !endLevel)
		{
			button[0] = true;
			button[1] = ((nrb_frame() >> 4) & 1) != 0;
			button[2] = ((nrb_frame() >> 5) & 1) != 0;
		}

		// Movement intent consumed by the linking routines below: taken from this
		// player's rollback tuple when one exists (wire-carried, so both machines
		// agree), else derived classically from the tick's position delta.
		Uint16 linkIntent = 0;
		bool   haveLinkIntent = false;

		/* Movement. */

		// Netplay with the variable-timestep ship: fold the motion VT accumulated since the last tick
		// into the ship now, between the snapshot above and the netcode below that reads (and
		// reverts) the difference.
		if (isNetworkGame && playerNum_ == thisPlayerNum && !rollback_resim)
			vt_ship_commit_net(playerNum_ - 1);

		// The live-sampling block never runs on a rollback/self-test replay pass:
		// a replayed tick takes its input from the recorded tuples instead.
		if ((!isNetworkGame || playerNum_ == thisPlayerNum) && !rollback_resim)
		{
			if (endLevel)
			{
				this_player->y -= 2;
			}
			else
			{
				if (record_demo || play_demo)
					inputDevice = 1;  // keyboard is required device for demo recording

				// demo playback input
				if (play_demo)
				{
					if (!replay_demo_keys())
					{
						endLevel = true;
						levelEnd = 40;
						// One-shot input event outside the movement tuple; recorded
						// so a self-test replay reproduces it.
						if (rollback_selftest_active())
							rollback_st_event(RB_EV_DEMO_END);
					}
				}

				/* joystick input */
				if ((inputDevice == 0 || inputDevice >= 3) && joysticks > 0)
				{
					int j = inputDevice  == 0 ? 0 : inputDevice - 3;
					int j_max = inputDevice == 0 ? joysticks : inputDevice - 3 + 1;
					for (; j < j_max; j++)
					{
						poll_joystick(j);

						if (joystick[j].analog)
						{
							mouseXC += joystick_axis_reduce(j, joystick[j].x);
							mouseYC += joystick_axis_reduce(j, joystick[j].y);

							link_gun_analog = joystick_analog_angle(j, &link_gun_angle);
						}
						else if (!vt_input)
						{
							this_player->x += (joystick[j].direction[3] ? -CURRENT_KEY_SPEED : 0) + (joystick[j].direction[1] ? CURRENT_KEY_SPEED : 0);
							this_player->y += (joystick[j].direction[0] ? -CURRENT_KEY_SPEED : 0) + (joystick[j].direction[2] ? CURRENT_KEY_SPEED : 0);
						}

						button[0] |= joystick[j].action[0];
						button[1] |= joystick[j].action[2];
						button[2] |= joystick[j].action[3];
						button[3] |= joystick[j].action_pressed[1];

						ingamemenu_pressed |= joystick[j].action_pressed[4];
						pause_pressed |= joystick[j].action_pressed[5];
					}
				}

				// Drain the render-rate controller/touch latch even when no controller is attached.
				button[3] |= changefire_pressed;
				changefire_pressed = false;

				service_SDL_events(false);

				// Classic-path (Smooth Motion off) direct mouse/touch movement for this
				// tick, in whole ship px: computed at the mouse read below, applied in
				// the movement block after the accel capture (VT parity: no momentum).
				int mouseDirectDX = 0, mouseDirectDY = 0;

				/* mouse input */
				if ((inputDevice == 0 || inputDevice == 2) && has_mouse)
				{
#ifdef PLATFORM_HANDHELD
					/* Touch auto-fire shares mouse_pressed[0]. Toggle Fire ignores it so drags
					 * neither shoot nor flip the latch; physical buttons still reach it. */
					if (!debugToggleFire)
						button[0] |= mouse_pressed[0];
#else
					button[0] |= mouse_pressed[0];
#endif
					button[1] |= mouse_pressed[1];
					button[2] |= mouse_pressed[2];
					button[3] |= mouse_pressed[3];

					if (!vt_input)
					{
						if (!isNetworkGame)
						{
							// Match VT mouse scaling and retain sub-pixel motion. The old
							// integer path lost slow movement and clipped fast flicks.
							static float carryX[2], carryY[2];
							const int pi = playerNum_ - 1;
							float mxr, myr;
							mouseGetRelativeMotionF(&mxr, &myr);
							if (smoothies[9-1])
								myr = -myr;  // inverted-control levels, same as the VT path
							carryX[pi] += mxr * VT_MOUSE_SENS;
							carryY[pi] += myr * VT_MOUSE_SENS;
							mouseDirectDX = (int)carryX[pi];
							mouseDirectDY = (int)carryY[pi];
							carryX[pi] -= (float)mouseDirectDX;
							carryY[pi] -= (float)mouseDirectDY;
						}
						else
						{
							// Network lockstep: keep the original integer read feeding
							// the synced mouseXC pipeline.
							Sint32 mouseXR;
							Sint32 mouseYR;
							mouseGetRelativePosition(&mouseXR, &mouseYR);
							mouseXC += mouseXR;
							mouseYC += mouseYR;
						}
					}
				}

				/* keyboard input */
				if ((inputDevice == 0 || inputDevice == 1) && !play_demo)
				{
					if (!vt_input)
					{
						if (keysactive[keySettings[KEY_SETTING_UP]])
							this_player->y -= CURRENT_KEY_SPEED;
						if (keysactive[keySettings[KEY_SETTING_DOWN]])
							this_player->y += CURRENT_KEY_SPEED;

						if (keysactive[keySettings[KEY_SETTING_LEFT]])
							this_player->x -= CURRENT_KEY_SPEED;
						if (keysactive[keySettings[KEY_SETTING_RIGHT]])
							this_player->x += CURRENT_KEY_SPEED;
					}

					button[0] = button[0] || keysactive[keySettings[KEY_SETTING_FIRE]];
					button[3] = button[3] || keysactive[keySettings[KEY_SETTING_CHANGE_FIRE]];
					button[1] = button[1] || keysactive[keySettings[KEY_SETTING_LEFT_SIDEKICK]];
					button[2] = button[2] || keysactive[keySettings[KEY_SETTING_RIGHT_SIDEKICK]];

					if (constantPlay)
					{
						for (unsigned int i = 0; i < 4; i++)
							button[i] = true;

						++this_player->y;
						this_player->x += constantLastX;
					}

					// Record input transitions in the legacy demo stream.
					if (record_demo)
					{
						bool new_input = false;

						for (unsigned int i = 0; i < 8; i++)
						{
							bool temp = demo_keys & (1 << i);
							if (temp != keysactive[keySettings[i]])
								new_input = true;
						}

						demo_keys_wait++;

						if (new_input)
						{
							Uint8 temp2[2] = { demo_keys_wait >> 8, demo_keys_wait };
							fwrite_u8(temp2, 2, demo_file);

							demo_keys = 0;
							for (unsigned int i = 0; i < 8; i++)
								demo_keys |= keysactive[keySettings[i]] ? (1 << i) : 0;

							fwrite_u8(&demo_keys, 1, demo_file);

							demo_keys_wait = 0;
						}
					}
				}

				/* Debug Toggle Fire changes state only on a press edge. Console touch
				 * drag is excluded above and cannot toggle it. */
				if (debugToggleFire && playerNum_ == 1 && !play_demo && !record_demo)
				{
					static bool toggleFirePrevHeld = false;
					if (button[0] && !toggleFirePrevHeld)
						debugToggleFireActive = !debugToggleFireActive;
					toggleFirePrevHeld = button[0];
					button[0] = debugToggleFireActive;
				}

				if (smoothies[9-1])
				{
					// The classic reads above moved the ship by the raw key, and mirroring the
					// snapshot is what inverts that displacement. VT moved it inverted already
					// (vt_ship_step); mirrored again, the wire would carry the un-inverted one.
					if (!vt_input)
						*mouseY_ = this_player->y - (*mouseY_ - this_player->y);
					mouseYC = -mouseYC;
				}

				accelXC += this_player->x - *mouseX_;
				accelYC += this_player->y - *mouseY_;

				if (mouseXC > 30)
					mouseXC = 30;
				else if (mouseXC < -30)
					mouseXC = -30;
				if (mouseYC > 30)
					mouseYC = 30;
				else if (mouseYC < -30)
					mouseYC = -30;

				if (!vt_input)
				{
					if (mouseXC > 0)
						this_player->x += (mouseXC + 3) / 4;
					else if (mouseXC < 0)
						this_player->x += (mouseXC - 3) / 4;
					if (mouseYC > 0)
						this_player->y += (mouseYC + 3) / 4;
					else if (mouseYC < 0)
						this_player->y += (mouseYC - 3) / 4;

					// Direct mouse/touch move (non-network classic path): after the
					// accel capture above so, like the VT ship, the mouse steers
					// position without feeding momentum.
					this_player->x += mouseDirectDX;
					this_player->y += mouseDirectDY;
				}

				if (mouseXC > 3)
					accelXC++;
				else if (mouseXC < -2)
					accelXC--;
				if (mouseYC > 2)
					accelYC++;
				else if (mouseYC < -2)
					accelYC--;

				// Smooth-Motion-off Sluggish path. Scale the completed movement and keep a fractional
				// carry per ship so the modifier has the same long-run rate for every input source.
				{
					const uint sluggishIdx = (playerNum_ >= 2) ? 1u : 0u;
					const float ms = endlessMoveScale();
					if (ms < 1.0f)
					{
						static float carryXs[2] = { 0.0f, 0.0f }, carryYs[2] = { 0.0f, 0.0f };
						float carryX = carryXs[sluggishIdx], carryY = carryYs[sluggishIdx];
						const float wantX = (float)(this_player->x - sluggishStartX) * ms + carryX;
						const float wantY = (float)(this_player->y - sluggishStartY) * ms + carryY;
						const int   dX = (int)(wantX >= 0.0f ? wantX + 0.5f : wantX - 0.5f);
						const int   dY = (int)(wantY >= 0.0f ? wantY + 0.5f : wantY - 0.5f);
						carryXs[sluggishIdx] = wantX - (float)dX;
						carryYs[sluggishIdx] = wantY - (float)dY;
						this_player->x = sluggishStartX + dX;
						this_player->y = sluggishStartY + dY;
					}
				}

			}   /*endLevel*/

#ifdef WITH_NETWORK
			/* Live-network regression for the delay packet's linked-gun fields. Player two supplies
			 * stable analog aim and alternating movement so the fused turret consumes both fields. */
			if (isNetworkGame && qa_net_scenario == 19 && playerNum_ == thisPlayerNum
			    && playerNum_ == 2 && !rollback_resim && !endLevel)
			{
				this_player->x = MIN(MAX(this_player->x + ((curLoc & 1) ? 2 : -2), 60), 240);
				button[0] = true;
				link_gun_analog = true;
				link_gun_angle = 1.25f;
			}

			if (isNetworkGame && playerNum_ == thisPlayerNum && nrb_active())
			{
				// Consume local input immediately and send it redundantly for the peer's
				// rollback timeline. Wire tests vary it to defeat held-input prediction.
				if (qa_net_gameplay_ticks > 0)
				{
					const int wiggle = ((nrb_frame() >> 3) & 1) ? 2 : -2;
					this_player->x = MIN(MAX(this_player->x + wiggle, 60), 240);
				}

				RbInput in;
				rb_fill_tuple(&in, this_player, *mouseX_, *mouseY_,
				              accelXC, accelYC, link_gun_analog, link_gun_angle);
				if (thisPlayerNum == networkHostPlayerNum)
					in.difficulty = (Uint8)difficultyLevel;  // host dictates
				nrb_record_local(&in);
				linkIntent = in.buttons;
				haveLinkIntent = true;

				// Adopt the wire-quantized analog gun angle locally too: the peer
				// can only ever apply the quantized value, and both simulations
				// must feed linkGunDirec the bit-identical float.
				if (in.buttons & RB_LINK_ANALOG)
					link_gun_angle = link_gun_angle_from_wire(in.linkAngle);
			}
			else if (isNetworkGame && playerNum_ == thisPlayerNum)
			{
				Uint16 buttons = 0;
				for (int i = 4 - 1; i >= 0; i--)
				{
					buttons <<= 1;
					buttons |= button[i];
				}
				/* Linked Dragonwing control depends on movement intent. Deriving it from the
				 * final position and a private mouse anchor can produce different results. */
				buttons |= rb_move_bits(this_player->x - *mouseX_, this_player->y - *mouseY_);

				// Absolute positions are idempotent when a lost packet is reconstructed.
				SDLNet_Write16(this_player->x, &packet_state_out[0]->data[4]);
				SDLNet_Write16(this_player->y, &packet_state_out[0]->data[6]);
				SDLNet_Write16(accelXC,        &packet_state_out[0]->data[8]);
				SDLNet_Write16(accelYC,        &packet_state_out[0]->data[10]);
				SDLNet_Write16(buttons,        &packet_state_out[0]->data[12]);
				SDLNet_Write16(link_gun_analog ? 1 : 0,
				               &packet_state_out[0]->data[NET_STATE_LINK_FLAGS]);
				const Uint16 link_angle_wire = link_gun_analog
				                               ? link_gun_angle_to_wire(link_gun_angle) : 0;
				SDLNet_Write16(link_angle_wire,
				               &packet_state_out[0]->data[NET_STATE_LINK_ANGLE]);

				// Key this history by the tick named in the remote packet. The inbound and
				// outbound queues can shift independently after loss recovery.
				net_own_state_store(SDLNet_Read16(&packet_state_out[0]->data[2]),
				                    this_player->x, this_player->y, buttons);

				this_player->x = *mouseX_;
				this_player->y = *mouseY_;

				button[0] = false;
				button[1] = false;
				button[2] = false;
				button[3] = false;

				accelXC = 0;
				accelYC = 0;
				link_gun_analog = false;
				link_gun_angle = 0.0f;
			}
#endif

			// Self-test: capture the effective tuple for BOTH players so a replay
			// of this tick can reproduce the live-sampled movement exactly.
			if (rollback_selftest_active())
			{
				RbInput in;
				rb_fill_tuple(&in, this_player, *mouseX_, *mouseY_,
				              accelXC, accelYC, link_gun_analog, link_gun_angle);
				rollback_st_record(playerNum_ - 1, &in);
			}
		}  /*isNetworkGame*/

		/* --- Movement Routine Ending --- */

		moveOk = true;

		// Self-test replay: both players' movement comes from the recorded tuples.
		if (rollback_selftest_active() && rollback_resim)
		{
			const RbInput *st = rollback_st_get(playerNum_ - 1);
			rb_apply_tuple(st, this_player, &accelXC, &accelYC,
			               &link_gun_analog, &link_gun_angle);
			*mouseX_ = (JE_word)st->mouseX;
			*mouseY_ = (JE_word)st->mouseY;
			if (playerNum_ == 1 && (rollback_st_events() & RB_EV_DEMO_END))
			{
				endLevel = true;
				levelEnd = 40;
			}
		}

#ifdef WITH_NETWORK
		if (isNetworkGame && nrb_active())
		{
			if (playerNum_ != thisPlayerNum)
			{
				// Peer's ship: the arrived truth for this frame, or a prediction
				// that a later rollback corrects.  Identical apply shape to the
				// local tuple, so both machines' sims see the same kind of input.
				RbInput in;
				nrb_get_remote(nrb_frame(), &in);
				if (thisPlayerNum != networkHostPlayerNum)
				{
					// Host-authoritative, but clamped like every other value adopted from
					// the wire: difficultyLevel indexes difficultyNameB[], so a corrupt
					// byte here is an out-of-bounds read rather than a wrong difficulty.
					const JE_shortint d = (JE_shortint)in.difficulty;
					difficultyLevel = (d >= DIFFICULTY_WIMP && d <= DIFFICULTY_10)
					                ? d : DIFFICULTY_NORMAL;
				}
				rb_apply_tuple(&in, this_player, &accelXC, &accelYC,
				               &link_gun_analog, &link_gun_angle);
				linkIntent = in.buttons;
				haveLinkIntent = true;
			}
			else if (rollback_resim)
			{
				// Replaying our own past frame: consume the recorded tuple.
				RbInput in;
				nrb_get_local(nrb_frame(), &in);
				rb_apply_tuple(&in, this_player, &accelXC, &accelYC,
				               &link_gun_analog, &link_gun_angle);
				linkIntent = in.buttons;
				haveLinkIntent = true;
			}
			// else: normal pass, local player; the live values stand as-is.
		}
		else if (isNetworkGame && !network_state_is_reset())
		{
			if (playerNum_ != thisPlayerNum)
			{
				if (thisPlayerNum != networkHostPlayerNum)
					difficultyLevel = SDLNet_Read16(&packet_state_in[0]->data[16]);

				Uint16 buttons = SDLNet_Read16(&packet_state_in[0]->data[12]);
				linkIntent = buttons;
				haveLinkIntent = true;
				for (int i = 0; i < 4; i++)
				{
					button[i] = buttons & 1;
					buttons >>= 1;
				}

				// Absolute, so assign rather than accumulate (see the send side above).
				this_player->x = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[4]);
				this_player->y = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[6]);
				accelXC = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[8]);
				accelYC = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[10]);
				link_gun_analog = (SDLNet_Read16(
					&packet_state_in[0]->data[NET_STATE_LINK_FLAGS]) & 1) != 0;
				link_gun_angle = link_gun_angle_from_wire(SDLNet_Read16(
					&packet_state_in[0]->data[NET_STATE_LINK_ANGLE]));
			}
			else
			{
				// Replay OUR ship at the same logical tick the remote packet names, taken from the
				// sync-keyed history rather than by counting back through the outbound queue; see
				// net_own_state_store.
				const Uint16 tick = SDLNet_Read16(&packet_state_in[0]->data[2]);

				int own_x, own_y;
				Uint16 buttons;
				net_own_state_load(tick, &own_x, &own_y, &buttons);
				linkIntent = buttons;
				haveLinkIntent = true;

				for (int i = 0; i < 4; i++)
				{
					button[i] = buttons & 1;
					buttons >>= 1;
				}

				this_player->x = own_x;
				this_player->y = own_y;
				accelXC = (Sint16)SDLNet_Read16(&packet_state_out[network_delay]->data[8]);
				accelYC = (Sint16)SDLNet_Read16(&packet_state_out[network_delay]->data[10]);
				link_gun_analog = (SDLNet_Read16(
					&packet_state_out[network_delay]->data[NET_STATE_LINK_FLAGS]) & 1) != 0;
				link_gun_angle = link_gun_angle_from_wire(SDLNet_Read16(
					&packet_state_out[network_delay]->data[NET_STATE_LINK_ANGLE]));
			}
		}
#endif

		/*Street-Fighter codes*/
		// Network play derives twiddle direction from synchronized movement. Self-test replay
		// uses the recorded target; neither path may sample current local controls.
		if (rollback_selftest_active() && rollback_resim)
		{
			const RbInput *st = rollback_st_get(playerNum_ - 1);
			JE_SFCodes(playerNum_, this_player->x, this_player->y, st->sfTx, st->sfTy);
		}
		else if (isNetworkGame && haveLinkIntent)
		{
			/* Network tuples carry the dominant movement direction and the outside-the-cone
			 * flag. Rebuild the one-pixel target from shared intent so twiddle recognition
			 * stays deterministic. */
			int dirx, diry, tx, ty;
			rb_move_dir(linkIntent, &dirx, &diry);
			SF_twiddleTarget(this_player->x, this_player->y, dirx, diry, &tx, &ty);
			JE_SFCodes(playerNum_, this_player->x, this_player->y, tx, ty);
		}
		else if (isNetworkGame)
		{
			/* Delay-Based mode has no consumable tuple during its initial queue fill.
			 * Feed a neutral direction on both peers during that interval. */
			JE_SFCodes(playerNum_, this_player->x, this_player->y,
			           this_player->x, this_player->y);
		}
		else if (vt && !isNetworkGame)
		{
			// Movement is skipped above, so *mouseX_/*mouseY_ stay at the ship position:
			// JE_SFCodes sees no direction and twiddles never fire. Rebuild the direction
			// from the raw controls as a 1px-offset target, leaving *mouseX_/*mouseY_
			// (used for aiming below) untouched.
			int dirx = 0, diry = 0;
			if ((inputDevice == 0 || inputDevice == 1) && !play_demo)
			{
				if (keysactive[keySettings[KEY_SETTING_LEFT]])  --dirx;
				if (keysactive[keySettings[KEY_SETTING_RIGHT]]) ++dirx;
				if (keysactive[keySettings[KEY_SETTING_UP]])    --diry;
				if (keysactive[keySettings[KEY_SETTING_DOWN]])  ++diry;
			}
			if ((inputDevice == 0 || inputDevice >= 3) && joysticks > 0)
			{
				int j     = inputDevice == 0 ? 0 : inputDevice - 3;
				int j_max = inputDevice == 0 ? joysticks : inputDevice - 3 + 1;
				for (; j < j_max; j++)
				{
					if (joystick[j].direction[3]) --dirx;  // left
					if (joystick[j].direction[1]) ++dirx;  // right
					if (joystick[j].direction[0]) --diry;  // up
					if (joystick[j].direction[2]) ++diry;  // down
				}
			}
			// Mouse steers the VT ship at render rate, so it isn't in the inputs above.
			// Fold in its accumulated direction raw (the inverted-control flip below must
			// apply to it too); call unconditionally to drain the accumulator.
			{
				int mdirx = 0, mdiry = 0;
				vt_ship_twiddle_dir(playerNum_ - 1, &mdirx, &mdiry);
				dirx += mdirx;
				diry += mdiry;
			}
			if (smoothies[9-1])  // inverted-control levels flip the vertical axis
				diry = -diry;
			if (mouseXC < 0) --dirx; else if (mouseXC > 0) ++dirx;  // analog stick (accumulated above;
			if (mouseYC < 0) --diry; else if (mouseYC > 0) ++diry;  //   mouseYC already flipped if inverted)

			int tx, ty;
			SF_twiddleTarget(this_player->x, this_player->y, dirx, diry, &tx, &ty);
			if (rollback_selftest_active())
				rollback_st_record_sf(playerNum_ - 1, (Sint16)tx, (Sint16)ty);
			JE_SFCodes(playerNum_, this_player->x, this_player->y, tx, ty);
		}
		else
		{
			// Classic movement: the tick's displacement is the intent, the same quantity the
			// wire tuple measures.
			int tx, ty;
			SF_twiddleTarget(this_player->x, this_player->y,
			               (int)this_player->x - (int)*mouseX_,
			               (int)this_player->y - (int)*mouseY_, &tx, &ty);
			if (rollback_selftest_active() && !rollback_resim)
				rollback_st_record_sf(playerNum_ - 1, (Sint16)tx, (Sint16)ty);
			JE_SFCodes(playerNum_, this_player->x, this_player->y, tx, ty);
		}

		if (moveOk)
		{
			/* Linking. */

#ifdef WITH_NETWORK
			// Keep the delay/analog wire regression in the fused state whose turret reads this field.
			if (isNetworkGame && qa_net_scenario == 19)
				twoPlayerLinked = true;
#endif

			// Rollback uses tuple intent because docked positions include each peer's predicted carrier.
			const bool linkMoved = haveLinkIntent
				? (linkIntent & RB_MOVE_MASK) != 0
				: (this_player->x != *mouseX_ || this_player->y != *mouseY_);

			if (split_arcade_mode() && !twoPlayerLinked && !linkMoved &&
			    abs(player[0].x - player[1].x) < 8 && abs(player[0].y - player[1].y) < 8 &&
			    player[0].is_alive && player[1].is_alive && !galagaMode)
			{
				twoPlayerLinked = true;
			}

			// Fuse/unfuse cues are played presentation-side in tyrian2.c (link_cue_state):
			// queueing them here loses them to the sidekick-fire slot and to rollback.

			if (playerNum_ == 1 && (button[3-1] || button[2-1]) && !galagaMode)
				twoPlayerLinked = false;

			if (twoPlayerMode && twoPlayerLinked && playerNum_ == 2 && linkMoved)
			{
				if (button[0])
				{
					if (link_gun_analog)
					{
						linkGunDirec = link_gun_angle;
					}
					else
					{
						JE_real tempR;

						if (haveLinkIntent)
						{
							if (linkIntent & (RB_MOVE_LEFT | RB_MOVE_RIGHT))
								tempR = (linkIntent & RB_MOVE_RIGHT) ? M_PI_2 : (M_PI + M_PI_2);
							else
								tempR = (linkIntent & RB_MOVE_DOWN) ? 0 : M_PI;
						}
						else if (abs(this_player->x - *mouseX_) > abs(this_player->y - *mouseY_))
							tempR = (this_player->x - *mouseX_ > 0) ? M_PI_2 : (M_PI + M_PI_2);
						else
							tempR = (this_player->y - *mouseY_ > 0) ? 0 : M_PI;

						if (fabsf(linkGunDirec - tempR) < 0.3f)
							linkGunDirec = tempR;
						else if (linkGunDirec < tempR && linkGunDirec - tempR > -3.24f)
							linkGunDirec += 0.2f;
						else if (linkGunDirec - tempR < M_PI)
							linkGunDirec -= 0.2f;
						else
							linkGunDirec += 0.2f;
					}

					if (linkGunDirec >= (2 * M_PI))
						linkGunDirec -= (2 * M_PI);
					else if (linkGunDirec < 0)
						linkGunDirec += (2 * M_PI);
				}
				else if (!galagaMode)
				{
					twoPlayerLinked = false;
				}
			}
		}
	}

	if (levelEnd > 0 && all_players_dead())
		reallyEndLevel = true;

	/* End Level Fade-Out */
	if (this_player->is_alive && endLevel)
	{
		if (levelEnd == 0)
		{
			reallyEndLevel = true;
		}
		else
		{
			this_player->y -= levelEndWarp;
			if (this_player->y < -vga_height)
				reallyEndLevel = true;

			int trail_spacing = 1;
			int trail_y = this_player->y;
			int num_trails = abs(41 - levelEnd);
			if (num_trails > 20)
				num_trails = 20;

			for (int i = 0; i < num_trails; i++)
			{
				trail_y += trail_spacing;
				trail_spacing++;
			}

			// Give the whole warp comet the ship id so the render-rate override
			// moves it as one rigid, interpolated object.
			rl_current_id = RL_ID_SHIP_BASE + playerNum_;
			for (int i = 1; i < num_trails; i++)
			{
				trail_y -= trail_spacing;
				trail_spacing--;

				if (trail_y > 0 && trail_y < 170)
				{
					if (shipGr_ == 0)
					{
						blit_ship2x2(VGAScreen, this_player->x - 17, trail_y - 7, *shipGrPtr_, 13);
						blit_ship2x2(VGAScreen, this_player->x + 7 , trail_y - 7, *shipGrPtr_, 51);
					}
					else if (shipGr_ == 1)
					{
						blit_ship2x2(VGAScreen, this_player->x - 17, trail_y - 7, *shipGrPtr_, 220);
						blit_ship2x2(VGAScreen, this_player->x + 7 , trail_y - 7, *shipGrPtr_, 222);
					}
					else
					{
						blit_ship2x2(VGAScreen, this_player->x - 5, trail_y - 7, *shipGrPtr_, shipGr_);
					}
				}
			}
			rl_current_id = 0;
		}
	}

	if (play_demo)
	{
		const int playfield_left = PLAYFIELD_LEFT;
		const int insert_coin_x = playfield_left + (PLAYFIELD_WIDTH - JE_textWidth(miscText[7], SMALL_FONT_SHAPES)) / 2;
		const int insert_coin_y = 10;
		JE_dString(VGAScreen, insert_coin_x, insert_coin_y, miscText[7], SMALL_FONT_SHAPES); // insert coin
	}

	if (this_player->is_alive && !endLevel)
	{
		if (!twoPlayerLinked || playerNum_ < 2)
		{
		if (!split_arcade_mode() || shipGr2 != 0)  // if not dragonwing
			{
				if (this_player->sidekick[LEFT_SIDEKICK].style == 0)
				{
					this_player->sidekick[LEFT_SIDEKICK].x = *mouseX_ - 14;
					this_player->sidekick[LEFT_SIDEKICK].y = *mouseY_;
				}

				if (this_player->sidekick[RIGHT_SIDEKICK].style == 0)
				{
					this_player->sidekick[RIGHT_SIDEKICK].x = *mouseX_ + 16;
					this_player->sidekick[RIGHT_SIDEKICK].y = *mouseY_;
				}
			}

			if (!vt)
			{
			if (this_player->x_friction_ticks > 0)
			{
				--this_player->x_friction_ticks;
			}
			else
			{
				this_player->x_friction_ticks = 1;

				if (this_player->x_velocity < 0)
					++this_player->x_velocity;
				else if (this_player->x_velocity > 0)
					--this_player->x_velocity;
			}

			if (this_player->y_friction_ticks > 0)
			{
				--this_player->y_friction_ticks;
			}
			else
			{
				this_player->y_friction_ticks = 2;

				if (this_player->y_velocity < 0)
					++this_player->y_velocity;
				else if (this_player->y_velocity > 0)
					--this_player->y_velocity;
			}

			this_player->x_velocity += accelXC;
			this_player->y_velocity += accelYC;

			this_player->x_velocity = MIN(MAX(-4, this_player->x_velocity), 4);
			this_player->y_velocity = MIN(MAX(-4, this_player->y_velocity), 4);

			this_player->x += this_player->x_velocity;
			this_player->y += this_player->y_velocity;
			}

			// if player moved, add new ship x, y history entry
			if (this_player->x - *mouseX_ != 0 || this_player->y - *mouseY_ != 0)
			{
				for (uint i = 1; i < COUNTOF(player->old_x); ++i)
				{
					this_player->old_x[i - 1] = this_player->old_x[i];
					this_player->old_y[i - 1] = this_player->old_y[i];
				}
				this_player->old_x[COUNTOF(player->old_x) - 1] = this_player->x;
				this_player->old_y[COUNTOF(player->old_x) - 1] = this_player->y;
			}
		}
		else  /*twoPlayerLinked*/
		{
			if (shipGr_ == 0)
				this_player->x = player[0].x - 1;
			else
				this_player->x = player[0].x;
			this_player->y = player[0].y + 8;

			this_player->x_velocity = player[0].x_velocity;
			this_player->y_velocity = 4;

			// Keep docked position history current for trailing sidekicks. Compare with the newest stored
			// position because the dock pin rewrites x/y every tick.
			if (this_player->x != this_player->old_x[COUNTOF(player->old_x) - 1] ||
			    this_player->y != this_player->old_y[COUNTOF(player->old_x) - 1])
			{
				for (uint i = 1; i < COUNTOF(player->old_x); ++i)
				{
					this_player->old_x[i - 1] = this_player->old_x[i];
					this_player->old_y[i - 1] = this_player->old_y[i];
				}
				this_player->old_x[COUNTOF(player->old_x) - 1] = this_player->x;
				this_player->old_y[COUNTOF(player->old_x) - 1] = this_player->y;
			}

			// turret direction marker/shield
			shotMultiPos[SHOT_MISC] = 0;
			b = player_shot_create(0, SHOT_MISC, this_player->x + 1 + roundf(sim_sinf(linkGunDirec + 0.2f) * 26), this_player->y + roundf(sim_cosf(linkGunDirec + 0.2f) * 26), *mouseX_, *mouseY_, 148, playerNum_);
			link_marker_slot[0] = (b >= 0 && b < MAX_PWEAPON) ? b : -1;
			shotMultiPos[SHOT_MISC] = 0;
			b = player_shot_create(0, SHOT_MISC, this_player->x + 1 + roundf(sim_sinf(linkGunDirec - 0.2f) * 26), this_player->y + roundf(sim_cosf(linkGunDirec - 0.2f) * 26), *mouseX_, *mouseY_, 148, playerNum_);
			link_marker_slot[1] = (b >= 0 && b < MAX_PWEAPON) ? b : -1;
			shotMultiPos[SHOT_MISC] = 0;
			b = player_shot_create(0, SHOT_MISC, this_player->x + 1 + roundf(sim_sinf(linkGunDirec) * 26), this_player->y + roundf(sim_cosf(linkGunDirec) * 26), *mouseX_, *mouseY_, 147, playerNum_);
			link_marker_slot[2] = (b >= 0 && b < MAX_PWEAPON) ? b : -1;

			if (shotRepeat[SHOT_REAR] > 0)
			{
				--shotRepeat[SHOT_REAR];
			}
			else if (button[1-1])
			{
				const JE_byte rear_weapon_id = this_player->items.weapon[REAR_WEAPON].id;
				if (rear_weapon_id > 0 && rear_weapon_id <= COUNTOF(linkGunWeapons))
				{
					shotMultiPos[SHOT_REAR] = 0;
					b = player_shot_create(0, SHOT_REAR,
					                       this_player->x + 1 + roundf(sim_sinf(linkGunDirec) * 20),
					                       this_player->y + roundf(sim_cosf(linkGunDirec) * 20),
					                       *mouseX_, *mouseY_, linkGunWeapons[rear_weapon_id - 1],
					                       playerNum_);
					player_shot_set_direction(b, rear_weapon_id, linkGunDirec);
				}
			}
		}
	}

	if (!endLevel)
	{
		if (this_player->x > PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN)
		{
			this_player->x = PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN;
			constantLastX = -constantLastX;
		}
		if (this_player->x < SHIP_LEFT_MARGIN)
		{
			this_player->x = SHIP_LEFT_MARGIN;
			constantLastX = -constantLastX;
		}

		// Both seats share one floor, online and local; a docked wing pinned 8px
		// below player one clamps to the same edge.
		if (this_player->y > SHIP_BOTTOM_MARGIN)
			this_player->y = SHIP_BOTTOM_MARGIN;

		if (this_player->y < SHIP_TOP_MARGIN)
			this_player->y = SHIP_TOP_MARGIN;

		// Determines the ship banking sprite to display, depending on horizontal velocity and acceleration
		int ship_banking;
		if (vt)
		{
			// Under VT x is constant within a tick and mouse steering never touches
			// x_velocity, so the vanilla formula shows no tilt; use the inter-tick
			// horizontal movement, which captures keyboard, joystick and mouse.
			int dvx, dvy;
			vt_ship_shot_delta(playerNum_ - 1, &dvx, &dvy);
			ship_banking = dvx / 2;
		}
		else
		{
			ship_banking = this_player->x_velocity / 2 + (this_player->x - *mouseX_) / 6;
		}
		ship_banking = MAX(-2, MIN(ship_banking, 2));

		int ship_sprite = ship_banking * 2 + shipGr_;

		explosionFollowAmountX = this_player->x - this_player->last_x_explosion_follow;
		explosionFollowAmountY = this_player->y - this_player->last_y_explosion_follow;

		if (explosionFollowAmountY < 0)
			explosionFollowAmountY = 0;

		this_player->last_x_explosion_follow = this_player->x;
		this_player->last_y_explosion_follow = this_player->y;

		// Tag the ship (shadow + hull) for cross-frame interpolation.
		rl_current_id = RL_ID_SHIP_BASE + playerNum_;

		// Recenter the cast shadow against background-2 parallax. Extra Parallax
		// widens that sweep, so it needs the larger midpoint offset.
		const int shadow_light_dx = extraParallax ? 34 : 18;

		if (shipGr_ == 0)
		{
			if (background2 && ship_draw_casts_shadow())
			{
				blit_sprite2x2_darken(VGAScreen, this_player->x - 17 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite + 13);
				blit_sprite2x2_darken(VGAScreen, this_player->x + 7 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite + 51);
				if (superWild)
				{
					blit_sprite2x2_darken(VGAScreen, this_player->x - 16 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite + 13);
					blit_sprite2x2_darken(VGAScreen, this_player->x + 6 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite + 51);
				}
			}
		}
		else if (shipGr_ == 1)
		{
			if (background2 && ship_draw_casts_shadow())
			{
				blit_sprite2x2_darken(VGAScreen, this_player->x - 17 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, 220);
				blit_sprite2x2_darken(VGAScreen, this_player->x + 7 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, 222);
			}
		}
		else
		{
			if (background2 && ship_draw_casts_shadow())
			{
				blit_sprite2x2_darken(VGAScreen, this_player->x - 5 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite);
				if (superWild)
				{
					blit_sprite2x2_darken(VGAScreen, this_player->x - 4 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite);
				}
			}
		}

		// Noclip transparent mode draws the hull semi-transparent (same blended blit
		// as the post-hit invulnerability flash), so it reads as a "ghost" ship.
		if (this_player->invulnerable_ticks > 0 || noclipMode == NOCLIP_TRANSPARENT)
		{
			if (this_player->invulnerable_ticks > 0)
				--this_player->invulnerable_ticks;

			if (shipGr_ == 0)
			{
				blit_ship2x2_blend(VGAScreen, this_player->x - 17, this_player->y - 7, *shipGrPtr_, ship_sprite + 13);
				blit_ship2x2_blend(VGAScreen, this_player->x + 7 , this_player->y - 7, *shipGrPtr_, ship_sprite + 51);
			}
			else if (shipGr_ == 1)
			{
				blit_ship2x2_blend(VGAScreen, this_player->x - 17, this_player->y - 7, *shipGrPtr_, 220);
				blit_ship2x2_blend(VGAScreen, this_player->x + 7 , this_player->y - 7, *shipGrPtr_, 222);
			}
			else
				blit_ship2x2_blend(VGAScreen, this_player->x - 5, this_player->y - 7, *shipGrPtr_, ship_sprite);
		}
		else
		{
			if (shipGr_ == 0)
			{
				blit_ship2x2(VGAScreen, this_player->x - 17, this_player->y - 7, *shipGrPtr_, ship_sprite + 13);
				blit_ship2x2(VGAScreen, this_player->x + 7, this_player->y - 7, *shipGrPtr_, ship_sprite + 51);
			}
			else if (shipGr_ == 1)
			{
				blit_ship2x2(VGAScreen, this_player->x - 17, this_player->y - 7, *shipGrPtr_, 220);
				blit_ship2x2(VGAScreen, this_player->x + 7, this_player->y - 7, *shipGrPtr_, 222);

				// Keep banking trim separate so its changing blit count does not snap
				// the interpolated hull.
				rl_current_id = RL_ID_SHIP_TRIM_BASE + playerNum_;
				int ship_banking = 0;
				switch (ship_sprite)
				{
				case 5:
					blit_ship2(VGAScreen, this_player->x - 17, this_player->y + 7, *shipGrPtr_, 40);
					tempW = this_player->x - 7;
					ship_banking = -2;
					break;
				case 3:
					blit_ship2(VGAScreen, this_player->x - 17, this_player->y + 7, *shipGrPtr_, 39);
					tempW = this_player->x - 7;
					ship_banking = -1;
					break;
				case 1:
					ship_banking = 0;
					break;
				case -1:
					blit_ship2(VGAScreen, this_player->x + 19, this_player->y + 7, *shipGrPtr_, 58);
					tempW = this_player->x + 9;
					ship_banking = 1;
					break;
				case -3:
					blit_ship2(VGAScreen, this_player->x + 19, this_player->y + 7, *shipGrPtr_, 59);
					tempW = this_player->x + 9;
					ship_banking = 2;
					break;
				}
				rl_current_id = RL_ID_SHIP_BASE + playerNum_;  // back to the hull for anything after
				if (ship_banking != 0)  // NortSparks
				{
					if (shotRepeat[SHOT_NORTSPARKS] > 0)
					{
						--shotRepeat[SHOT_NORTSPARKS];
					}
					else
					{
						// Sequenced: as arguments the two draws were unordered, and MSVC and GCC
						// pick opposite orders, so the jitter landed on opposite axes per platform.
						const int spark_x = tempW + (mt_rand() % 8) - 4;
						const int spark_y = this_player->y + (mt_rand() % 8) - 4;
						b = player_shot_create(0, SHOT_NORTSPARKS, spark_x, spark_y, *mouseX_, *mouseY_, 671, playerNum_);
						shotRepeat[SHOT_NORTSPARKS] = abs(ship_banking) - 1;
					}
				}
			}
			else
			{
				blit_ship2x2(VGAScreen, this_player->x - 5, this_player->y - 7, *shipGrPtr_, ship_sprite);
			}
		}

		rl_current_id = 0;  // end ship tag

		/*Options Location*/
		if (shipGr_ == 0)  // The Dragonwing's wide hull sets sidekicks further out, either seat.
		{
			if (this_player->sidekick[LEFT_SIDEKICK].style == 0)
			{
				this_player->sidekick[LEFT_SIDEKICK].x = this_player->x - 14 + ship_banking * 2;
				this_player->sidekick[LEFT_SIDEKICK].y = this_player->y;
			}

			if (this_player->sidekick[RIGHT_SIDEKICK].style == 0)
			{
				this_player->sidekick[RIGHT_SIDEKICK].x = this_player->x + 17 + ship_banking * 2;
				this_player->sidekick[RIGHT_SIDEKICK].y = this_player->y;
			}
		}
	}  // !endLevel

	if (moveOk)
	{
		if (this_player->is_alive)
		{
			if (!endLevel)
			{
				if (vt)
				{
					// VT moves the ship between ticks, so (x - last_x_shot_move) reads ~0;
					// use the inter-tick delta so tracking shots (laser, main pulse) follow.
					vt_ship_shot_delta(playerNum_ - 1,
					                   &this_player->delta_x_shot_move,
					                   &this_player->delta_y_shot_move);

					// Weapons 98-100 inherit motion from the tick's starting position.
					// Reconstruct it because VT otherwise leaves a zero delta here.
					*mouseX_ = this_player->x - this_player->delta_x_shot_move;
					*mouseY_ = this_player->y - this_player->delta_y_shot_move;
				}
				else
				{
					this_player->delta_x_shot_move = this_player->x - this_player->last_x_shot_move;
					this_player->delta_y_shot_move = this_player->y - this_player->last_y_shot_move;
				}

				/* PLAYER SHOT Change */
				if (button[4-1])
				{
					portConfigChange = true;
					if (portConfigDone)
					{
						shotMultiPos[SHOT_REAR] = 0;

						// Super Arcade swaps this ship's paired special instead of cycling a rear
						// bay. Per ship: online the two players may be flying different Super
						// Arcade ships, each with its own A/B pair.
						const uint sa_ship = player_sa_ship(this_player);
						if (sa_ship != SA_NONE && sa_ship <= SA_LASTSHIP)
						{
							shotMultiPos[SHOT_SPECIAL] = 0;
							shotMultiPos[SHOT_SPECIAL2] = 0;
							if (this_player->items.special == SASpecialWeapon[sa_ship-1])
							{
								this_player->items.special = SASpecialWeaponB[sa_ship-1];
								this_player->weapon_mode = 2;
							}
							else
							{
								this_player->items.special = SASpecialWeapon[sa_ship-1];
								this_player->weapon_mode = 1;
							}
							if (dual_ship_mode())
							{
								this_player->shot_multi_pos[SHOT_SPECIAL] = 0;
								this_player->shot_multi_pos[SHOT_SPECIAL2] = 0;
							}
						}
						// This ship's own bay: the partner's toggle is simulated here too, and
						// wrapping it against the local gun kept the change off this screen.
						else if (++this_player->weapon_mode > JE_portConfigs(this_player))
							this_player->weapon_mode = 1;

						JE_drawPortConfigButtons();
						portConfigDone = false;
					}
				}

				/* PLAYER SHOT Creation */

				// Everything below is this ship's: the salvo it spends, the perks its shots read.
				// Re-assert the owner, since player_shot_create leaves the effect context on whoever
				// it fired for and the Nort banking sparks above run through it earlier in the tick.
				endlessSetFxPlayer((uint)(this_player - &player[0]));

				// Ahead of the special, so a special pressed with the volley belongs to it.
				endlessArmOpeningSalvoForTick(this_player, playerNum_);

				/*SpecialShot*/
				if (!galagaMode)
					JE_doSpecialShot(playerNum_, &this_player->armor, &this_player->shield);

				/*Normal Main Weapons*/
				if (!(twoPlayerLinked && playerNum_ == 2))
				{
					int min, max;

					if (!twoPlayerMode || dual_ship_mode())
						min = 1, max = 2;
					else
						min = max = playerNum_;

					for (temp = min - 1; temp < max; temp++)
					{
						// min/max are 1 or 2, so temp is a bay index. It is also the shared global
						// scratch byte, so its range isn't visible at the subscripts below.
						OT_ASSUME(temp < COUNTOF(this_player->items.weapon));
						const uint item = this_player->items.weapon[temp].id;

						if (item > 0)
						{
							if (shotRepeat[temp] > 0)
							{
								--shotRepeat[temp];
							}
							else if (button[1-1])
							{
								// Not the raw stored power: the arcade "Rear Gun Scale" row lets a rear bay
								// fire at the life count instead (arcade_weapon_power, player.c).  The
								// fire cursor needs no reset when that level moves; player_shot_create
								// wraps shotMultiPos on >= max, so a shorter pattern self-corrects.
								const uint item_power = galagaMode ? 0 : arcade_weapon_power(this_player, temp) - 1,
								           item_mode = (temp == REAR_WEAPON) ? this_player->weapon_mode - 1 : 0;

								// Zica Lv11: Long uses side beams; Buff adds the center beam. Extras share one power cost.
								const bool zica_l11 = (item == 5 && item_power == 10);
								JE_word l11_primary = weaponPort[item].op[item_mode][item_power];
								if (zica_l11 && zicaLaserLength == ZICA_LEN_LONG)
									l11_primary = ZICA_LONG_WEAP_LEFT;

								// Opening Salvo was armed for this tick's whole volley before the
								// special went out; see endlessArmOpeningSalvoForTick.
								b = player_shot_create(item, temp, this_player->x, this_player->y, *mouseX_, *mouseY_, l11_primary, playerNum_);

								// Free does not mean unconditional: zeroing poweruse also makes the
								// extras' own power check pass, which would let them keep firing on an
								// empty generator after the primary was refused. Gate them on the
								// primary having fired, so the whole weapon starves together.
								if (b < MAX_PWEAPON && zica_l11 && (zicaLaserLength == ZICA_LEN_LONG || zicaLaserBuff))
								{
									JE_word saved_poweruse = weaponPort[item].poweruse;
									weaponPort[item].poweruse = 0;
									if (zicaLaserLength == ZICA_LEN_LONG)
										player_shot_create(item, temp, this_player->x, this_player->y, *mouseX_, *mouseY_, ZICA_LONG_WEAP_RIGHT, playerNum_);
									if (zicaLaserBuff)
										player_shot_create(item, temp, this_player->x, this_player->y, *mouseX_, *mouseY_, weaponPort[item].op[item_mode][9], playerNum_);
									weaponPort[item].poweruse = saved_poweruse;
								}
							}
						}
					}
				}

				/*Super Charge Weapons*/
				// The charge meter is the Dragonwing's, so only the linked pair grows one.
				if (playerNum_ == 2 && !dual_ship_mode())
				{

					if (!twoPlayerLinked)
					{
						rl_current_id = RL_ID_SHIP_BASE + playerNum_;  // charge meter rides with the ship
						blit_ship_indicator2(VGAScreen, this_player->x + (shipGr_ == 0) + 1, this_player->y - 13, spriteSheet10, 77 + chargeLevel + chargeGr * 19);
						rl_current_id = 0;
					}

					if (chargeGrWait > 0)
					{
						chargeGrWait--;
					}
					else
					{
						chargeGr++;
						if (chargeGr == 4)
							chargeGr = 0;
						chargeGrWait = 3;
					}

					if (chargeLevel > 0)
					{
						fill_rectangle_xy(VGAScreenSeg, HUD_X(269), 107 + (chargeLevel - 1) * 3, HUD_X(275), 108 + (chargeLevel - 1) * 3, 193);
					}

					if (chargeWait > 0)
					{
						chargeWait--;
					}
					else
					{
						if (chargeLevel < chargeMax)
							chargeLevel++;

						chargeWait = 28 - this_player->items.weapon[REAR_WEAPON].power * 2;
						if (difficultyLevel > DIFFICULTY_HARD)
							chargeWait -= 5;
					}

					if (chargeLevel > 0)
						fill_rectangle_xy(VGAScreenSeg, HUD_X(269), 107 + (chargeLevel - 1) * 3, HUD_X(275), 108 + (chargeLevel - 1) * 3, 204);

					if (shotRepeat[SHOT_P2_CHARGE] > 0)
					{
						--shotRepeat[SHOT_P2_CHARGE];
					}
					else if (button[1-1] && (!twoPlayerLinked || chargeLevel > 0))
					{
						shotMultiPos[SHOT_P2_CHARGE] = 0;
						b = player_shot_create(16, SHOT_P2_CHARGE, this_player->x, this_player->y, *mouseX_, *mouseY_, chargeGunWeapons[player[1].items.weapon[REAR_WEAPON].id-1] + chargeLevel, playerNum_);

						if (chargeLevel > 0)
							fill_rectangle_xy(VGAScreenSeg, HUD_X(269), 107 + (chargeLevel - 1) * 3, HUD_X(275), 108 + (chargeLevel - 1) * 3, 193);

						chargeLevel = 0;
						chargeWait = 30 - this_player->items.weapon[REAR_WEAPON].power * 2;
					}
				}

				/* Super bomb. */
				temp = playerNum_;
				if (temp == 0)
					temp = 1;  /*Get whether player 1 or 2*/

				// temp is the shared global scratch byte, so its 1-or-2 range isn't visible at the
				// subscript. Index player[] through a local clamped to the array instead; still
				// assigns temp above, which the shotRepeat/shotMultiPos slots below key off.
				const uint bombPlayer = (temp >= 1 && temp <= COUNTOF(player)) ? temp - 1 : 0;

				if (player[bombPlayer].superbombs > 0)
				{
					if (shotRepeat[SHOT_P1_SUPERBOMB + temp-1] > 0)
					{
						--shotRepeat[SHOT_P1_SUPERBOMB + temp-1];
					}
					else if ((button[3-1] || button[2-1])
					         && !(endlessFxActive() && (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_DUD)))
					{  // Dud (gamble curse): the bombs are aboard but jammed; the fire press does nothing this sector
						--player[bombPlayer].superbombs;
						shotMultiPos[SHOT_P1_SUPERBOMB + temp-1] = 0;
						b = player_shot_create(16, SHOT_P1_SUPERBOMB + temp-1, this_player->x, this_player->y, *mouseX_, *mouseY_, 535, playerNum_);
					}
				}

				// sidekicks

				if (this_player->sidekick[LEFT_SIDEKICK].style == 4 && this_player->sidekick[RIGHT_SIDEKICK].style == 4)
					optionSatelliteRotate += 0.2f;
				else if (this_player->sidekick[LEFT_SIDEKICK].style == 4 || this_player->sidekick[RIGHT_SIDEKICK].style == 4)
					optionSatelliteRotate += 0.15f;

				switch (this_player->sidekick[LEFT_SIDEKICK].style)
				{
				case 1:  // trailing
				case 3:
					this_player->sidekick[LEFT_SIDEKICK].x = this_player->old_x[COUNTOF(player->old_x) / 2 - 1];
					this_player->sidekick[LEFT_SIDEKICK].y = this_player->old_y[COUNTOF(player->old_x) / 2 - 1];
					break;
				case 2:  // front-mounted (launchable)
					JE_frontOption(this_player, LEFT_SIDEKICK, front_option_home_x(this_player, LEFT_SIDEKICK), button[1 + LEFT_SIDEKICK]);
					break;
				case 4:  // orbiting
					this_player->sidekick[LEFT_SIDEKICK].x = this_player->x + roundf(sim_sinf(optionSatelliteRotate) * 20);
					this_player->sidekick[LEFT_SIDEKICK].y = this_player->y + roundf(sim_cosf(optionSatelliteRotate) * 20);
					break;
				}

				switch (this_player->sidekick[RIGHT_SIDEKICK].style)
				{
				case 4:  // orbiting
					this_player->sidekick[RIGHT_SIDEKICK].x = this_player->x - roundf(sim_sinf(optionSatelliteRotate) * 20);
					this_player->sidekick[RIGHT_SIDEKICK].y = this_player->y - roundf(sim_cosf(optionSatelliteRotate) * 20);
					break;
				case 1:  // trailing
				case 3:
					this_player->sidekick[RIGHT_SIDEKICK].x = this_player->old_x[0];
					this_player->sidekick[RIGHT_SIDEKICK].y = this_player->old_y[0];
					break;
				case 2:  // front-mounted (launchable)
					JE_frontOption(this_player, RIGHT_SIDEKICK, front_option_home_x(this_player, RIGHT_SIDEKICK), button[1 + RIGHT_SIDEKICK]);
					break;
				}

				if (playerNum_ == 2 || !twoPlayerMode || dual_ship_mode())
				{
					for (uint i = 0; i < COUNTOF(player->items.sidekick); ++i)
					{
						uint shot_i = (i == 0) ? SHOT_LEFT_SIDEKICK : SHOT_RIGHT_SIDEKICK;

						JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

						// A trailing large body draws downward from the pod position, so it fires
						// from lower down than the position itself.
						const int shot_y = this_player->sidekick[i].y
						                 + (this_player->sidekick[i].style == 1 ? SIDEKICK_TRAIL_SHOT_Y : 0);
						// Twin Pods: the own volley moves inboard by the twin's outboard offset, so the
						// pair stays centred on the pod (0 without the perk). The perk belongs to the
						// sidekick's owner, so name that ship instead of using the effect context.
						const int twin_dx = endlessPerkTwinPodOffset((uint)(this_player - &player[0]), i);
						const int shot_x = this_player->sidekick[i].x - twin_dx;

						// fire/refill sidekick
						if (this_option->wport > 0)
						{
							if (shotRepeat[shot_i] > 0)
							{
								--shotRepeat[shot_i];
							}
							else
							{
								const int ammo_max = cheatInfiniteSidekickAmmo ? 0 : this_player->sidekick[i].ammo_max;

								if (!cheatInfiniteSidekickAmmo && ammo_max > 0)  // sidekick has limited ammo
								{
									if (this_player->sidekick[i].ammo_refill_ticks > 0)
									{
										--this_player->sidekick[i].ammo_refill_ticks;
									}
									else  // refill one ammo
									{
										this_player->sidekick[i].ammo_refill_ticks = this_player->sidekick[i].ammo_refill_ticks_max;

										if (this_player->sidekick[i].ammo < ammo_max)
											++this_player->sidekick[i].ammo;

										JE_drawSidekickAmmoGauge(playerNum_, i, this_player->sidekick[i].ammo, ammo_max, false);
									}

									if (button[1 + i] && (cheatInfiniteSidekickAmmo || this_player->sidekick[i].ammo > 0))
									{
										b = player_shot_create(this_option->wport, shot_i, shot_x, shot_y, *mouseX_, *mouseY_, this_option->wpnum + this_player->sidekick[i].charge, playerNum_);

										if (!cheatInfiniteSidekickAmmo)
											--this_player->sidekick[i].ammo;

										// Twin Pods: the twin spends the next round, so the last
										// round in the magazine fires alone.
										if (cheatInfiniteSidekickAmmo || this_player->sidekick[i].ammo > 0)
										{
											const JE_integer twin = player_shot_create_twin(
												b, this_option->wport, i, twin_dx,
												this_player->sidekick[i].x, shot_y, *mouseX_, *mouseY_,
												this_option->wpnum + this_player->sidekick[i].charge, playerNum_);
											if (twin < MAX_PWEAPON && !cheatInfiniteSidekickAmmo)
												--this_player->sidekick[i].ammo;
										}

										if (this_player->sidekick[i].charge > 0)
										{
											shotMultiPos[shot_i] = 0;
											this_player->sidekick[i].charge = 0;
										}
										this_player->sidekick[i].charge_ticks = endlessPerkChargeTicks(20);
										this_player->sidekick[i].animation_enabled = true;

										if (!cheatInfiniteSidekickAmmo)
											JE_drawSidekickAmmoGauge(playerNum_, i, this_player->sidekick[i].ammo, ammo_max, true);
									}
								}
								else  // has infinite ammo
								{
									/*
									// Tyrian 2000: weapons with charge stages do not auto-fire
									if ((button[0] && !this_option->pwr) || button[1 + i])
									*/

									// Charge sidekicks (pwr > 0) autofire on the held main button per
									// this mode; non-charge sidekicks and the dedicated button always fire.
									const bool charge_autofire =
										chargeSidekickAutofire == CHARGE_AUTOFIRE_ON
										|| chargeSidekickAutofire == CHARGE_AUTOFIRE_FAST
										|| (chargeSidekickAutofire == CHARGE_AUTOFIRE_FULL
										    && this_player->sidekick[i].charge >= this_option->pwr);

									if ((button[0] && (charge_autofire || !this_option->pwr)) || button[1 + i])
									{
										b = player_shot_create(this_option->wport, shot_i, shot_x, shot_y, *mouseX_, *mouseY_, this_option->wpnum + this_player->sidekick[i].charge, playerNum_);
										// Twin Pods: no magazine here, so the twin costs generator power only.
										player_shot_create_twin(
											b, this_option->wport, i, twin_dx, this_player->sidekick[i].x, shot_y,
											*mouseX_, *mouseY_, this_option->wpnum + this_player->sidekick[i].charge,
											playerNum_);

										if (this_player->sidekick[i].charge > 0)
										{
											shotMultiPos[shot_i] = 0;
											this_player->sidekick[i].charge = 0;
										}
										this_player->sidekick[i].charge_ticks = endlessPerkChargeTicks(20);
										this_player->sidekick[i].animation_enabled = true;

										// Fast autofire uses the shortest repeat among all charge stages.
										if (chargeSidekickAutofire == CHARGE_AUTOFIRE_FAST && this_option->pwr > 0)
										{
											JE_byte fastest = weapons[this_option->wpnum].shotrepeat;
											for (uint s = 1; s <= this_option->pwr; ++s)
												if (weapons[this_option->wpnum + s].shotrepeat < fastest)
													fastest = weapons[this_option->wpnum + s].shotrepeat;
											shotRepeat[shot_i] = fastest;
										}
									}
								}
							}
						}
					}
				}  // end of if player has sidekicks
			}  // !endLevel
		} // this_player->is_alive
	} // moveOK

	// draw sidekicks
	if ((playerNum_ == 2 || !twoPlayerMode || dual_ship_mode()) && !endLevel)
	{
		// Sidekicks are dyed by the drive of the ship that flies them, so re-assert the owner:
		// the volleys fired above leave the effect context on whoever they last fired for.
		endlessSetFxPlayer((uint)(this_player - &player[0]));

		for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
		{
			JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

			if (this_option->option > 0)
			{
				if (this_player->sidekick[i].animation_enabled)
				{
					if (++this_player->sidekick[i].animation_frame >= this_option->ani)
					{
						this_player->sidekick[i].animation_frame = 0;
						this_player->sidekick[i].animation_enabled = (this_option->option == 1);
					}
				}

				const int x = this_player->sidekick[i].x,
				          y = this_player->sidekick[i].y;
				const uint sprite = this_option->gr[this_player->sidekick[i].animation_frame] + this_player->sidekick[i].charge;

				rl_current_id = RL_ID_SIDEKICK_BASE + playerNum_ * 2 + (int)i;
				// Pods and satellites are ship-relative, so attach both axes to the
				// render-rate ship. Their own offset or orbit still interpolates.
				rl_shot_attach = (this_player->sidekick[i].style == 0 || this_player->sidekick[i].style == 4)
				               ? (3 | ((playerNum_ - 1) << 2))
				               : 0;
				if (this_player->sidekick[i].style == 4)
				{
					// The orbit offset above is rounded to whole pixels, which makes the
					// recorded arc advance unevenly; the remainder puts the interpolated
					// path back on the exact circle. See doc/notes.md#render-list.
					const float ox = sim_sinf(optionSatelliteRotate) * 20,
					            oy = sim_cosf(optionSatelliteRotate) * 20;
					const float dir = (i == LEFT_SIDEKICK) ? 1.0f : -1.0f;  // right slot mirrors
					rl_current_sub_x = dir * (ox - roundf(ox));
					rl_current_sub_y = dir * (oy - roundf(oy));
				}
				if (this_player->sidekick[i].style == 1 || this_player->sidekick[i].style == 2)
					blit_ship2x2(VGAScreen, x - 6, y, spriteSheet10, sprite);
				else
					blit_ship2(VGAScreen, x, y, spriteSheet9, sprite);
				rl_current_id = 0;
				rl_shot_attach = 0;
				rl_current_sub_x = rl_current_sub_y = 0.0f;
			}

			if (cheatInstantCharge)
			{
				// debug: skip the timed ramp, hold the sidekick at full charge
				this_player->sidekick[i].charge = this_option->pwr;
			}
			else if (--this_player->sidekick[i].charge_ticks == 0)
			{
				if (this_player->sidekick[i].charge < this_option->pwr)
					++this_player->sidekick[i].charge;
				this_player->sidekick[i].charge_ticks = endlessPerkChargeTicks(20);
			}
		}
	}
}

static void coop_ship_runtime_load(Player *this_player)
{
	power = this_player->generator_power;
	powerAdd = this_player->generator_power_add;
	shieldWait = this_player->shield_wait;
	memcpy(shotRepeat, this_player->shot_repeat, sizeof(shotRepeat));
	memcpy(shotMultiPos, this_player->shot_multi_pos, sizeof(shotMultiPos));
	portConfigChange = false;
	portConfigDone = this_player->port_config_done;
	optionSatelliteRotate = this_player->option_satellite_rotate;
	memcpy(optionAttachmentMove, this_player->option_attachment_move, sizeof(optionAttachmentMove));
	memcpy(optionAttachmentLinked, this_player->option_attachment_linked, sizeof(optionAttachmentLinked));
	memcpy(optionAttachmentReturn, this_player->option_attachment_return, sizeof(optionAttachmentReturn));
	fireButtonHeld = this_player->special_fire_held;
	zinglonDuration = this_player->zinglon_duration;
	zinglonRamp = this_player->zinglon_ramp;
	astralDuration = this_player->astral_duration;
	flareDuration = this_player->flare_duration;
	flareStart = this_player->flare_start;
	flareColChg = this_player->flare_color_change;
	specialWait = this_player->special_wait;
	nextSpecialWait = this_player->next_special_wait;
	spraySpecial = this_player->spray_special;
	specialWeaponFilter = this_player->special_weapon_filter;
	specialWeaponFreq = this_player->special_weapon_freq;
	specialWeaponWpn = this_player->special_weapon_wpn;
	linkToPlayer = this_player->special_link_to_player;
}

static void coop_ship_runtime_save(Player *this_player)
{
	this_player->generator_power = (Uint16)power;
	this_player->generator_power_add = (Uint16)powerAdd;
	this_player->shield_wait = shieldWait;
	memcpy(this_player->shot_repeat, shotRepeat, sizeof(shotRepeat));
	memcpy(this_player->shot_multi_pos, shotMultiPos, sizeof(shotMultiPos));
	this_player->port_config_change = portConfigChange;
	this_player->port_config_done = portConfigDone;
	this_player->option_satellite_rotate = optionSatelliteRotate;
	memcpy(this_player->option_attachment_move, optionAttachmentMove, sizeof(optionAttachmentMove));
	memcpy(this_player->option_attachment_linked, optionAttachmentLinked, sizeof(optionAttachmentLinked));
	memcpy(this_player->option_attachment_return, optionAttachmentReturn, sizeof(optionAttachmentReturn));
	this_player->special_fire_held = fireButtonHeld;
	this_player->zinglon_duration = zinglonDuration;
	this_player->zinglon_ramp = zinglonRamp;
	this_player->astral_duration = astralDuration;
	this_player->flare_duration = flareDuration;
	this_player->flare_start = flareStart;
	this_player->flare_color_change = flareColChg;
	this_player->special_wait = specialWait;
	this_player->next_special_wait = nextSpecialWait;
	this_player->spray_special = spraySpecial;
	this_player->special_weapon_filter = specialWeaponFilter;
	this_player->special_weapon_freq = specialWeaponFreq;
	this_player->special_weapon_wpn = specialWeaponWpn;
	this_player->special_link_to_player = linkToPlayer;
}

void coop_ship_runtime_reset(void)
{
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		Player *const this_player = &player[p];
		this_player->generator_power = 0;
		this_player->generator_power_add = powerSys[this_player->items.generator].power;
		this_player->shield_wait = 15;
		memset(this_player->shot_repeat, 1, sizeof(this_player->shot_repeat));
		memset(this_player->shot_multi_pos, 0, sizeof(this_player->shot_multi_pos));
		this_player->port_config_change = false;
		this_player->port_config_done = false;
		this_player->option_satellite_rotate = 0.0f;
		memset(this_player->option_attachment_move, 0, sizeof(this_player->option_attachment_move));
		memset(this_player->option_attachment_linked, 1, sizeof(this_player->option_attachment_linked));
		memset(this_player->option_attachment_return, 0, sizeof(this_player->option_attachment_return));
		this_player->special_fire_held = false;
		this_player->zinglon_duration = 0;
		this_player->zinglon_ramp = 0;
		this_player->astral_duration = 0;
		this_player->flare_duration = 0;
		this_player->flare_start = false;
		this_player->flare_color_change = 0;
		this_player->special_wait = 0;
		this_player->next_special_wait = 0;
		this_player->spray_special = false;
		this_player->special_weapon_filter = -99;
		this_player->special_weapon_freq = 0;
		this_player->special_weapon_wpn = 0;
		this_player->special_link_to_player = false;
	}
}

/* Pool slots of the three linked-Dragonwing turret aim markers created this
 * tick (-1 = none).  Presentation-only: the shot draw maps these slots to the
 * stable RL_ID_LINKGUN ids so the aim indicator interpolates at render rate. */
int link_marker_slot[3] = { -1, -1, -1 };

void JE_mainGamePlayerFunctions(void)
{
	/* Player movement and input. */
	hud_special_light_tick_begin();

	// Last tick's aim markers were drawn (and their slots freed) by the shot
	// pass that just ran; forget them before this tick's movement re-creates
	// them, so a recycled slot can never mis-tag an unrelated shot.
	link_marker_slot[0] = link_marker_slot[1] = link_marker_slot[2] = -1;

	if (endLevel && levelEnd > 0)
	{
		levelEnd--;
		levelEndWarp++;
	}

	/*Reset Street-Fighter commands*/
	memset(SFExecuted, 0, sizeof(SFExecuted));

	portConfigChange = false;

	if (twoPlayerMode)
	{
		if (dual_ship_mode())
			coop_ship_runtime_load(&player[0]);
		JE_playerMovement(&player[0],
		                  !galagaMode ? inputDevice[0] : 0, 1, shipGr, shipGrPtr,
		                  &mouseX, &mouseY);
		if (dual_ship_mode())
		{
			coop_ship_runtime_save(&player[0]);
			if (!player[0].port_config_change)
				player[0].port_config_done = true;
			coop_ship_runtime_load(&player[1]);
		}
		JE_playerMovement(&player[1],
		                  !galagaMode ? inputDevice[1] : 0, 2, shipGr2, shipGr2ptr,
		                  &mouseXB, &mouseYB);
		if (dual_ship_mode())
		{
			coop_ship_runtime_save(&player[1]);
			if (!player[1].port_config_change)
				player[1].port_config_done = true;
			astralDuration = MAX(player[0].astral_duration, player[1].astral_duration);
			// The summary the shared globals carry has to describe one beam, so the longer-running
			// ship lends both halves of its pillar: a ramp read against the other's duration would
			// size the beam wrong.
			const uint zinglonLead = player[1].zinglon_duration > player[0].zinglon_duration ? 1 : 0;
			zinglonDuration = player[zinglonLead].zinglon_duration;
			zinglonRamp = player[zinglonLead].zinglon_ramp;
			shotAvail[MAX_PWEAPON - 1] =
				((player[0].zinglon_duration > 1 && player[0].zinglon_duration % 5 == 0) ||
				 (player[1].zinglon_duration > 1 && player[1].zinglon_duration % 5 == 0));
		}
	}
	else
	{
		JE_playerMovement(&player[0],
		                  0, 1, shipGr, shipGrPtr,
		                  &mouseX, &mouseY);
	}

	/* == Parallax Map Scrolling == */
	JE_word tempX;
	if (twoPlayerMode)
		tempX = (player[0].x + player[1].x) / 2;
	else
		tempX = player[0].x;

	// w_f is the shared float driver for all three layers: mapX3Ofs = w_f, mapX2Ofs = (w_f-17)*2/3,
	// mapXOfs = mapX2Ofs/2 (the original coupled 4:2:1 ratio); tempW is its floor. They move together.
	float w_f;
	bool bg2CrispLeft = false;  // Extra Parallax OFF only: render bg2 crisply at the far-left extreme (below)
	if (extraParallax)
	{
		// Pan the near layer edge-to-edge across the ship's actual travel.
		// Derive w_f so deeper layers keep the stock coupled ratio.
		const float travel = (float)((PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN) - SHIP_LEFT_MARGIN);
		float uu = (tempX - SHIP_LEFT_MARGIN) / travel;
		if (uu < 0.0f)
			uu = 0.0f;
		else if (uu > 1.0f)
			uu = 1.0f;
		const float near_flush_left = (float)(PLAYFIELD_LEFT - PLAYFIELD_X_SHIFT);  // 36
		const float near_slack      = (float)(14 * 24 - PLAYFIELD_WIDTH);           // 336 - 299 = 37
		w_f = 3.0f * (near_flush_left - near_slack * uu) + 17.0f;                   // 125 (far-left) .. 14 (far-right)
	}
	else
	{
		// Stock amplitude and normalization; the original parallax formula (the only OFF-mode
		// deviation is the deliberate far-left bg2 sub-pixel snap applied at the very end).
		const float left_bound = 40.0f;
		const float right_bound = PLAYFIELD_WIDTH + 64;
		float u = (tempX - left_bound) / (right_bound - left_bound);
		if (u < 0.0f)
			u = 0.0f;
		else if (u > 1.0f)
			u = 1.0f;
		w_f = (1.0f - u) * (float)(24 * 3);
		bg2CrispLeft = (u <= 0.0f);  // ship pushed fully to the left (parallax pinned at its leftmost)
	}

	tempW = floorf(w_f);
	mapX3Ofs = tempW;
	mapX3Pos = mapX3Ofs % 24;
	mapX3bpPos = 1 - (mapX3Ofs / 24);

	mapX2Ofs   = ((tempW-17) * 2) / 3;
	mapX2Pos   = mapX2Ofs % 24;
	mapX2bpPos = 1 - (mapX2Ofs / 24);

	oldMapXOfs = mapXOfs;
	oldMapXOfs_f = mapXOfs_f;  // both still hold the PREVIOUS tick's value here (updated below)
	mapXOfs    = mapX2Ofs / 2;  // near layer rides half the mid layer; original coupled ratio
	mapXPos    = mapXOfs % 24;
	mapXbpPos  = 1 - (mapXOfs / 24);

	if (background3x1)
	{
		mapX3Ofs = mapXOfs;
		mapX3Pos = mapXPos;
		mapX3bpPos = mapXbpPos - 1;
	}

	// Un-floored mirror of the same offsets (render-list sub-pixel interpolation of the pan),
	// derived from the same w_f so the fraction matches the integer offsets above.
	mapX3Ofs_f = w_f;
	mapX2Ofs_f = ((w_f - 17.0f) * 2.0f) / 3.0f;
	mapXOfs_f  = mapX2Ofs_f / 2.0f;
	if (background3x1)
		mapX3Ofs_f = mapXOfs_f;

	// At the stock left edge, snap bg2 interpolation to the integer anchor to prevent a one-pixel shift.
	if (bg2CrispLeft)
		mapX2Ofs_f = (float)mapX2Ofs;

	// Clamp bg2 to -1 so its 336-pixel strip covers the right edge in both integer and smooth paths.
	if (mapX2Ofs < -1)
	{
		mapX2Ofs = -1;
		mapX2Pos = mapX2Ofs % 24;
		mapX2bpPos = 1 - (mapX2Ofs / 24);
	}
	if (mapX2Ofs_f < -1.0f)
		mapX2Ofs_f = -1.0f;
}

// Per-slot lookup: an unset nickname must fall back to "Player N" for that slot, never to the
// other side's nickname, or the named player's nick shows on both slots for the unnamed player.
const char *JE_getName(JE_byte pnum)
{
	if (pnum == thisPlayerNum)
	{
		if (network_player_name[0] != '\0')
			return network_player_name;
	}
	else if (network_opponent_name[0] != '\0')
	{
		return network_opponent_name;
	}

	return miscText[47 + pnum];
}

// "Player N Score:" with its "Player N" prefix swapped for that player's nickname, so an online
// pair's totals are labelled by name wherever they are shown. Only the prefix is replaced, keeping
// the label's punctuation; a label that does not open with that name is left as authored.
void JE_playerScoreLabel(JE_byte pnum, char *out, size_t outSize)
{
	const char *const label = miscText[39 + pnum];   // "Player N Score:"
	const char *const who   = miscText[47 + pnum];   // "Player N"
	const size_t who_len = strlen(who);

	if (isNetworkGame && strncmp(label, who, who_len) == 0)
		snprintf(out, outSize, "%s%s", JE_getName(pnum), label + who_len);
	else
		snprintf(out, outSize, "%s", label);
}

// Find the next `]L` name at or after a section, including routing sections with no name of their
// own. The file-size bound protects the die-on-EOF reader from invalid section numbers.
static void JE_getLevelName(int levelNum, char *out, size_t outSize)
{
	if (outSize == 0)
		return;
	out[0] = '\0';

	FILE *f = dir_fopen(JE_episodeDir(), episode_file, "rb");
	if (f == NULL)
		return;

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	char s[256];

	// Seek past the first `levelNum` section markers ('*') to the target section.
	int x = 0;
	while (x < levelNum && ftell(f) < fsize)
	{
		read_encrypted_pascal_string(s, sizeof(s), f);
		if (s[0] == '*')
			x++;
	}

	// Find the next "]L" declaration (across sections, to resolve routing sections).
	while (ftell(f) < fsize)
	{
		s[0] = '\0';
		read_encrypted_pascal_string(s, sizeof(s), f);
		if (s[0] == ']' && s[1] == 'L' && strlen(s) >= 13)
		{
			char name[10];
			SDL_strlcpy(name, s + 13, sizeof(name));  // 9-char name field, as the game does
			for (int i = (int)strlen(name) - 1; i >= 0 && name[i] == ' '; --i)
				name[i] = '\0';  // trim the space padding
			SDL_strlcpy(out, name, outSize);
			break;
		}
	}

	fclose(f);
}

void JE_playerCollide(Player *this_player, JE_byte playerNum_)
{
	char tempStr[256];

	for (int z = 0; z < 100; z++)
	{
		if (enemyAvail[z] != 1)
		{
			int enemy_screen_x = enemy[z].ex + enemy[z].mapoffset;

			// Both refs sit ~equally left/up of their sprite centres (ship +7/+7,
			// enemy +6/+7), so the ref-to-ref test is effectively centre-to-centre.
			bool touching = abs(this_player->x - enemy_screen_x) < 12 && abs(this_player->y - enemy[z].ey) < 14;

			// Sampled before the branch below clears enemyAvail, which endlessSpecialPickup reads.
			const bool specialPickup = endlessSpecialPickup(z);

			// An Endless "?" draws one small glyph inside that footprint, so it keeps the vanilla
			// rule (ship centre inside the pickup's box) measured against the glyph.
			if (specialPickup)
			{
				const int shipCentreX = this_player->x + 7;
				const int shipCentreY = this_player->y + 7;
				touching = shipCentreX >= enemy_screen_x + ENDLESS_SPECIAL_GLYPH_X0
				        && shipCentreX <= enemy_screen_x + ENDLESS_SPECIAL_GLYPH_X1
				        && shipCentreY >= enemy[z].ey + ENDLESS_SPECIAL_GLYPH_Y0
				        && shipCentreY <= enemy[z].ey + ENDLESS_SPECIAL_GLYPH_Y1;
			}

			if (touching)
			{   /*Collide*/
				int evalue = enemy[z].evalue;
				if (evalue > 29999)
				{
					if (evalue == 30000)  // spawn dragonwing in galaga mode, otherwise just a purple ball
					{
						player_award_pickup_cash(this_player, 100);

						if (!galagaMode)
						{
							handle_got_purple_ball(this_player);
						}
						else
						{
							// spawn the dragonwing?
							if (twoPlayerMode)
								player_award_pickup_cash(this_player, 2400);
							// Co-op already flies two independent ships: the ball only pays,
							// and must not link the pair or clobber the second ship's state.
							if (!coop_mode_active())
							{
								twoPlayerMode = true;
								twoPlayerLinked = true;
								player[1].items.weapon[REAR_WEAPON].power = 1;
								arcade_rescale_to_lives(&player[1]);  // that power IS the spawned wing's life count
								player[1].armor = 10;
								player[1].is_alive = true;
							}
						}
						enemyAvail[z] = 1;
						soundQueue[7] = S_POWERUP;
					}
					else if (superArcadeMode != SA_NONE && evalue > 30000)
					{
						shotMultiPos[SHOT_FRONT] = 0;
						shotRepeat[SHOT_FRONT] = 10;

						/* A ball's color indexes the collecting ship's arsenal. This matters
						 * online, where the two Super Arcade ships may differ. */
						tempW = player_sa_ball_weapon(this_player, (uint)(evalue - 30000 - 1));

						// if picked up already-owned weapon, power weapon up
						if (tempW == this_player->items.weapon[FRONT_WEAPON].id)
						{
							player_award_pickup_cash(this_player, 1000);
							power_up_weapon(this_player, FRONT_WEAPON);
						}
						// else weapon also gives purple ball
						else
						{
							handle_got_purple_ball(this_player);
						}

						this_player->items.weapon[FRONT_WEAPON].id = tempW;
						if (dual_ship_mode())
							this_player->shot_multi_pos[SHOT_FRONT] = 0;
						player_award_pickup_cash(this_player, 200);
						soundQueue[7] = S_POWERUP;
						enemyAvail[z] = 1;
					}
					else if (evalue > 32100)
					{
						if (playerNum_ == 1 || dual_ship_mode())
						{
							player_award_pickup_cash(this_player, 250);
							this_player->items.special = evalue - 32100;
							shotMultiPos[SHOT_SPECIAL] = 0;
							shotRepeat[SHOT_SPECIAL] = 10;
							shotMultiPos[SHOT_SPECIAL2] = 0;
							shotRepeat[SHOT_SPECIAL2] = 0;
							if (dual_ship_mode())
							{
								this_player->shot_multi_pos[SHOT_SPECIAL] = 0;
								this_player->shot_repeat[SHOT_SPECIAL] = 10;
								this_player->shot_multi_pos[SHOT_SPECIAL2] = 0;
								this_player->shot_repeat[SHOT_SPECIAL2] = 0;
							}
							hud_special_light_rearm((uint)(this_player - player));

							if (isNetworkGame)
								snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(dual_ship_mode() ? playerNum_ : 1), miscTextB[4-1], JE_specialName((JE_byte)(evalue - 32100)));
							else if (twoPlayerMode)
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[43-1], JE_specialName((JE_byte)(evalue - 32100)));
							else
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[64-1], JE_specialName((JE_byte)(evalue - 32100)));
							JE_drawTextWindow(tempStr);
							soundQueue[7] = S_POWERUP;
							enemyAvail[z] = 1;
						}
					}
					else if (evalue > 32000)
					{
						if (dual_ship_mode())
						{
							enemyAvail[z] = 1;
							for (uint i = 0; i < COUNTOF(this_player->items.sidekick); ++i)
								this_player->items.sidekick[i] = evalue - 32000;
							this_player->shot_multi_pos[SHOT_LEFT_SIDEKICK] = 0;
							this_player->shot_multi_pos[SHOT_RIGHT_SIDEKICK] = 0;
							snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(playerNum_), miscTextB[4-1], options[evalue - 32000].name);
							JE_drawTextWindow(tempStr);
							JE_resetPlayerOptions(this_player);
							JE_drawOptionsHUD();
							soundQueue[7] = S_POWERUP;
						}
						else if (playerNum_ == 2)
						{
							enemyAvail[z] = 1;
							if (isNetworkGame)
								snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(2), miscTextB[4-1], options[evalue - 32000].name);
							else
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[44-1], options[evalue - 32000].name);
							JE_drawTextWindow(tempStr);

							// if picked up a different sidekick than player already has, then reset sidekicks to least powerful, else power them up
							if (evalue - 32000u != player[1].items.sidekick_series)
							{
								player[1].items.sidekick_series = evalue - 32000;
								player[1].items.sidekick_level = 101;
							}
							else if (player[1].items.sidekick_level < 103)
							{
								++player[1].items.sidekick_level;
							}

							uint temp = player[1].items.sidekick_level - 100 - 1;
							for (uint i = 0; i < COUNTOF(player[1].items.sidekick); ++i)
								player[1].items.sidekick[i] = optionSelect[player[1].items.sidekick_series][temp][i];

							shotMultiPos[SHOT_LEFT_SIDEKICK] = 0;
							shotMultiPos[SHOT_RIGHT_SIDEKICK] = 0;
							JE_drawOptions();
							soundQueue[7] = S_POWERUP;
						}
						else if (onePlayerAction)
						{
							enemyAvail[z] = 1;
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[64-1], options[evalue - 32000].name);
							JE_drawTextWindow(tempStr);

							for (uint i = 0; i < COUNTOF(player[0].items.sidekick); ++i)
								player[0].items.sidekick[i] = evalue - 32000;
							shotMultiPos[SHOT_LEFT_SIDEKICK] = 0;
							shotMultiPos[SHOT_RIGHT_SIDEKICK] = 0;

							JE_drawOptions();
							soundQueue[7] = S_POWERUP;
						}
						if (enemyAvail[z] == 1)
							player_award_pickup_cash(this_player, 250);
					}
					else if (evalue > 31000)
					{
						player_award_pickup_cash(this_player, 250);
						if (dual_ship_mode())
						{
							snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(playerNum_), miscTextB[4-1], weaponPort[evalue - 31000].name);
							JE_drawTextWindow(tempStr);
							this_player->items.weapon[REAR_WEAPON].id = evalue - 31000;
							this_player->shot_multi_pos[SHOT_REAR] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;
						}
						else if (playerNum_ == 2)
						{
							if (isNetworkGame)
								snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(2), miscTextB[4-1], weaponPort[evalue - 31000].name);
							else
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[44-1], weaponPort[evalue - 31000].name);
							JE_drawTextWindow(tempStr);
							player[1].items.weapon[REAR_WEAPON].id = evalue - 31000;
							shotMultiPos[SHOT_REAR] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;
						}
						else if (onePlayerAction)
						{
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[64-1], weaponPort[evalue - 31000].name);
							JE_drawTextWindow(tempStr);
							player[0].items.weapon[REAR_WEAPON].id = evalue - 31000;
							shotMultiPos[SHOT_REAR] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;

							if (player[0].items.weapon[REAR_WEAPON].power == 0)  // does this ever happen?
								player[0].items.weapon[REAR_WEAPON].power = 1;
						}
					}
					else if (evalue > 30000)
					{
						if (dual_ship_mode() || (playerNum_ == 1 && twoPlayerMode))
						{
							if (isNetworkGame)
								snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(dual_ship_mode() ? playerNum_ : 1), miscTextB[4-1], weaponPort[evalue - 30000].name);
							else
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[43-1], weaponPort[evalue - 30000].name);
							JE_drawTextWindow(tempStr);
							Player *const pickup_player = dual_ship_mode() ? this_player : &player[0];
							pickup_player->items.weapon[FRONT_WEAPON].id = evalue - 30000;
							shotMultiPos[SHOT_FRONT] = 0;
							if (dual_ship_mode())
								pickup_player->shot_multi_pos[SHOT_FRONT] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;
						}
						else if (onePlayerAction)
						{
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[64-1], weaponPort[evalue - 30000].name);
							JE_drawTextWindow(tempStr);
							player[0].items.weapon[FRONT_WEAPON].id = evalue - 30000;
							shotMultiPos[SHOT_FRONT] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;
						}

						if (enemyAvail[z] == 1 && !coop_mode_active())
						{
							// The arcade gun's paired special follows the same ship the gun went to.
							Player *const special_player = arcade_separate_mode() ? this_player : &player[0];
							special_player->items.special = specialArcadeWeapon[evalue - 30000-1];
							if (special_player->items.special > 0)
							{
								shotMultiPos[SHOT_SPECIAL] = 0;
								shotRepeat[SHOT_SPECIAL] = 0;
								shotMultiPos[SHOT_SPECIAL2] = 0;
								shotRepeat[SHOT_SPECIAL2] = 0;
								if (arcade_separate_mode())
								{
									special_player->shot_multi_pos[SHOT_SPECIAL] = 0;
									special_player->shot_repeat[SHOT_SPECIAL] = 0;
									special_player->shot_multi_pos[SHOT_SPECIAL2] = 0;
									special_player->shot_repeat[SHOT_SPECIAL2] = 0;
								}
								hud_special_light_rearm((uint)(special_player - player));
							}
							player_award_pickup_cash(this_player, 250);
						}

					}
				}
				else if (evalue > 20000)
				{
					if (twoPlayerLinked)
					{
						// share the armor evenly between linked players
						for (uint i = 0; i < COUNTOF(player); ++i)
						{
							player[i].armor += (evalue - 20000) / COUNTOF(player);
							const uint armorCap = arcade_life_scaling_active() ? player[i].initial_armor : 28;
							if (player[i].armor > armorCap)
								player[i].armor = armorCap;
						}
					}
					else
					{
						this_player->armor += evalue - 20000;
					// Endless and arcade use their raised hull ceiling; campaign effects never lower the classic 28.
					const uint armorCap = (endlessMode || arcade_life_scaling_active())
					                    ? this_player->initial_armor
					                    : (endlessCampaignMods && this_player->initial_armor > 28)
					                      ? this_player->initial_armor : 28;
						if (this_player->armor > armorCap)
							this_player->armor = armorCap;
					}
					enemyAvail[z] = 1;
					VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
					JE_drawArmor();
					VGAScreen = game_screen; /* side-effect of game_screen */
					soundQueue[7] = S_POWERUP;
				}
				else if (evalue > 10000 && enemyAvail[z] == 2)
				{
					if (endlessMode)
					{
						enemyAvail[z] = 1;
						soundQueue[7] = S_POWERUP;
						endlessGrantSpecial((uint)(this_player - &player[0]));
					}
					else if (!bonusLevel)
					{
						play_song(30);  /*Zanac*/
						bonusLevel = true;
						nextLevel = evalue - 10000;
						enemyAvail[z] = 1;

						// Show the secret destination in the temporary HUD message.
						char secretName[16];
						JE_getLevelName(nextLevel, secretName, sizeof(secretName));
						if (secretName[0] != '\0')
							snprintf(tempStr, sizeof(tempStr), "Secret Level: %s", secretName);
						else
							strcpy(tempStr, "Secret Level!");
						JE_drawTextWindow(tempStr);
					}
				}
				else if (enemy[z].scoreitem)
				{
					enemyAvail[z] = 1;
					soundQueue[7] = S_ITEM;
					if (evalue == 1)
					{
						cubeMax++;
						soundQueue[3] = V_DATA_CUBE;
										if (endlessMode)
										{
											cubeMax--;
											soundQueue[3] = S_POWERUP;
											endlessGrantSpecial((uint)(this_player - &player[0]));
										}
					}
					else if (evalue == -1)  // got front weapon powerup
					{
						if (isNetworkGame)
							snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(dual_ship_mode() ? playerNum_ : 1), miscTextB[4-1], miscText[45-1]);
						else if (twoPlayerMode)
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[43-1], miscText[45-1]);
						else
							strcpy(tempStr, miscText[45-1]);
						JE_drawTextWindow(tempStr);

						power_up_weapon(dual_ship_mode() ? this_player : &player[0], FRONT_WEAPON);
						soundQueue[7] = S_POWERUP;
					}
					else if (evalue == -2)  // got rear weapon powerup
					{
						if (isNetworkGame)
							snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(dual_ship_mode() ? playerNum_ : 2), miscTextB[4-1], miscText[46-1]);
						else if (twoPlayerMode)
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[44-1], miscText[46-1]);
						else
							strcpy(tempStr, miscText[46-1]);
						JE_drawTextWindow(tempStr);

						power_up_weapon(dual_ship_mode() ? this_player : (twoPlayerMode ? &player[1] : &player[0]), REAR_WEAPON);
						soundQueue[7] = S_POWERUP;
					}
					else if (evalue == -3)
					{
						// picked up orbiting asteroid killer
						shotMultiPos[SHOT_MISC] = 0;
						b = player_shot_create(0, SHOT_MISC, this_player->x, this_player->y, mouseX, mouseY, 104, playerNum_);
						shotAvail[z] = 0;
					}
					else if (evalue == -4)
					{
						if (player[playerNum_-1].superbombs < 10)
							++player[playerNum_-1].superbombs;
					}
					else if (evalue == -5)
					{
						Player *const hotdog_player = dual_ship_mode() ? this_player : &player[0];
						hotdog_player->items.weapon[FRONT_WEAPON].id = 25;  // HOT DOG!
						hotdog_player->items.weapon[REAR_WEAPON].id = 26;
						if (!dual_ship_mode())
							player[1].items.weapon[REAR_WEAPON].id = 26;

						hotdog_player->last_items = hotdog_player->items;

						if (dual_ship_mode())
						{
							hotdog_player->weapon_mode = 1;
							memset(hotdog_player->shot_multi_pos, 0, sizeof(hotdog_player->shot_multi_pos));
						}
						else
						{
							for (uint i = 0; i < COUNTOF(player); ++i)
								player[i].weapon_mode = 1;
						}

						memset(shotMultiPos, 0, sizeof(shotMultiPos));
					}
					else
					{
						// Bounty Hunter multiplies this where the Endless effect layer runs.
						const uint collector = (uint)(this_player - &player[0]);
						const long picked = endlessScorePickupValue(collector, evalue);
						if (twoPlayerLinked)
						{
							// players get equal share of pick-up cash when linked
							for (uint i = 0; i < COUNTOF(player); ++i)
								player_add_cash(&player[i], picked / (long)COUNTOF(player));
						}
						else
						{
							player_award_pickup_cash(this_player, picked);
						}
					}
					// The authored graphic names the original item; a data cube's spells out "DATA".
					if (!specialPickup)
						JE_setupExplosion(enemy_screen_x, enemy[z].ey, 0, enemyDat[enemy[z].enemytype].explosiontype, true, false);
				}
				// Endless Low Profile shrinks the damaging collision but keeps the full pickup range;
				// a boon must not make items harder to grab. endlessHitboxScale is the identity outside
				// the boon, so every other game tests exactly the 12x14 the outer branch did.
				else if ((this_player->invulnerable_ticks == 0
				          || endlessRamWhileInvulnerable(this_player->invulnerable_ticks)) &&
				         enemyAvail[z] == 0 && !noclipMode &&
				         (enemyDat[enemy[z].enemytype].explosiontype & 1) == 0 && // explosiontype & 1 == 0: not ground enemy
				         abs(this_player->x - enemy_screen_x) < endlessHitboxScale(12) &&
				         abs(this_player->y - enemy[z].ey) < endlessHitboxScale(14))
				{
					// The ram perks and the kill credit below belong to the ship doing the ramming.
					endlessSetFxPlayer((uint)(this_player - &player[0]));

					int armorleft = enemy[z].armorleft;
					if (armorleft > damageRate)
						armorleft = damageRate;

					// Reinforced Prow and Knife Fight raise what the enemy takes; the ship's own share
					// below stays on the stock figure, so a nearly dead enemy still hurts less. A ram
					// happens at nil range, so Knife Fight is always at its deepest here.
					const int knifePct = endlessFxActive() ? endlessPerkKnifeFightPercent((unsigned)z) : 0;
					int damage_to_enemy = endlessPerkProwRamDamage(damageRate);
					const int knifeRam = endlessPerkKnifeFightBonus(damage_to_enemy, knifePct);
					if (knifeRam > 0)
						endlessPerkKnifeFightBlood((unsigned)z, knifePct);
					// An open Opening Salvo window lifts the ram as it lifts a volley. Its lift and
					// Ramming uses Opening Salvo without spending it; Knife Fight adds separately.
					damage_to_enemy = endlessOpeningSalvoScale(damage_to_enemy) + knifeRam;
					if (damage_to_enemy > enemy[z].armorleft)
						damage_to_enemy = enemy[z].armorleft;

					int playerHit = armorleft;
					if (endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_RAMPAGE))
						playerHit = playerHit * 3 / 2;
					// Apply incoming ram modifiers in one rounded pass.
					if (endlessFxActive() && playerHit > 0)
					{
						const Sint64 pct = (Sint64)endlessContactDamagePercent()
						                 * endlessEliteContactPercent(enemy[z].eliteState)
						                 * endlessPerkProwContactPercent();
						const Sint64 scale = 100LL * 100 * 100;
						playerHit = (int)((playerHit * pct + scale / 2) / scale);
						if (playerHit < 1)
							playerHit = 1;   // a real hit still costs a point
					}
					if (playerHit > 255)
						playerHit = 255;
					// An invulnerable ship (Endless, on the cadence above) rams without being rammed.
					if (this_player->invulnerable_ticks == 0)
						JE_playerDamage((JE_byte)playerHit, this_player);

					// player ship gets push-back from collision
					if (enemy[z].armorleft > 0)
					{
						this_player->x_velocity += (enemy[z].exc * enemy[z].armorleft) / 2;
						this_player->y_velocity += (enemy[z].eyc * enemy[z].armorleft) / 2;
					}

					// Nx boss HP (expert mode and/or endless depth) and the endless tier, spent
					// through the hull's accumulator: 1 armor per N damage dealt.
					damage_to_enemy = enemy_spend_damage(z, damage_to_enemy);

					int armorleft2 = enemy[z].armorleft;
					if (armorleft2 == 255)
						armorleft2 = 30000;

					temp = enemy[z].linknum;
					if (temp == 0)
						temp = 255;

					b = z;

					if (armorleft2 > damage_to_enemy)
					{
						// damage enemy
						if (enemy[z].armorleft != 255)
						{
							if (!enemy[z].healthbar_seen)
							{
								enemy_note_full_armor(&enemy[z]);
								enemy[z].healthbar_seen = true;
							}
							enemy[z].armorleft -= damage_to_enemy;
						}
						soundQueue[5] = S_ENEMY_HIT;
					}
					else if (endlessFxActive())
					{
						// Endless: a ram kill is a kill, taken the way a killing shot takes it and
						// credited to the ship that rammed.
						enemy_kill_group((unsigned)z, (int)playerNum_ - 1, (int)playerNum_ - 1);
					}
					else
					{
						// A ram removes the enemy without awarding a kill: no payout, no bounty, no
						// death effects.
						for (temp2 = 0; temp2 < 100; temp2++)
						{
							if (enemyAvail[temp2] != 1)
							{
								temp3 = enemy[temp2].linknum;
								if (temp2 == b ||
									(temp != 255 &&
									 (temp == temp3 || temp - 100 == temp3 ||
									  (temp3 > 40 && temp3 / 20 == temp / 20 && temp3 <= temp))))
								{
									int enemy_screen_x = enemy[temp2].ex + enemy[temp2].mapoffset;

									enemy[temp2].linknum = 0;

									enemyAvail[temp2] = 1;

									explosionFilter = endlessEliteTint(enemy[temp2].eliteState);
									if (enemyDat[enemy[temp2].enemytype].esize == 1)
									{
										JE_setupExplosionLarge(enemy[temp2].enemyground, enemy[temp2].explonum, enemy_screen_x, enemy[temp2].ey);
										soundQueue[6] = S_EXPLOSION_9;
									}
									else
									{
										JE_setupExplosion(enemy_screen_x, enemy[temp2].ey, 0, 1, false, false);
										soundQueue[5] = S_EXPLOSION_4;
									}
									explosionFilter = 0;
								}
							}
						}
						enemyAvail[z] = 1;
					}
				}
			}

		}
	}
}
