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
#include "title.h"

#include "audio.h"
#include "config.h"
#include "custom_episode.h"
#include "demo.h"
#include "endless.h"
#include "episodes.h"
#include "font.h"
#include "game_loop.h"
#include "game_state.h"
#include "helptext.h"
#include "high_scores.h"
#include "joystick.h"
#include "keyboard.h"
#include "level_load.h"
#include "menus.h"
#include "mouse.h"
#include "musmast.h"
#include "net_start.h"
#include "network.h"
#include "palette.h"
#include "params.h"
#include "picload.h"
#include "player.h"
#include "save_menu.h"
#include "setup_menu.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static bool newEndlessGame(void);
static bool newGame(void);

// Present one supersampled title-slide frame: the composed background with the two logo
// sprites at fractional positions.
static void title_logo_present_hi(SDL_Surface *hi, const SDL_Surface *bg, int scale,
                                  float yLogo, float y2K)
{
	memcpy(hi->pixels, bg->pixels, (size_t)hi->pitch * hi->h);

	const int x_off = video_get_menu_x_offset();
	blit_sprite_table_scaled(hi, (11 + x_off) * scale, (int)(yLogo * scale + 0.5f),
	                         PLANET_SHAPES, 146, scale, BLITT_COPY, 0, 0, false);
	blit_sprite_table_scaled(hi, (155 + x_off) * scale, (int)(y2K * scale + 0.5f),
	                         PLANET_SHAPES, 151, scale, BLITT_COPY, 0, 0, false);
	present_hi(hi);
}

static void JE_whoa(void)
{
	unsigned int i, j, color, offset, timer;
	unsigned int screenSize, topBorder, bottomBorder;
	Uint8 * TempScreen1, * TempScreen2, * TempScreenSwap;

	/* Fade used by the "engage" code. Existing secondary screens provide the
	 * two temporary buffers. */

	TempScreen1  = game_screen->pixels;
	TempScreen2  = VGAScreen2->pixels;

	screenSize   = VGAScreenSeg->h * VGAScreenSeg->pitch;
	topBorder    = VGAScreenSeg->pitch * 4; /* Four excluded rows. */
	bottomBorder = VGAScreenSeg->pitch * 7;

	/* Both temporary surfaces must cover the active surface. */
	assert((unsigned)VGAScreen2->h  * VGAScreen2->pitch >= screenSize &&
	       (unsigned)game_screen->h * game_screen->pitch >= screenSize);

	/* Exclude the top and bottom borders from processing and drawing. */
	memset((Uint8 *)VGAScreenSeg->pixels, 0, topBorder);
	memset((Uint8 *)VGAScreenSeg->pixels + screenSize - bottomBorder, 0, bottomBorder);

	/* Seed one temporary buffer with the screen and clear the other. */
	memset(TempScreen1, 0, screenSize);
	memcpy(TempScreen2, VGAScreenSeg->pixels, VGAScreenSeg->h * VGAScreenSeg->pitch);

	service_SDL_events(true);
	timer = 300; /* Enough diffusion rounds to leave the screen nearly black. */

	do
	{
		setDelay(1);

		/* Diffuse neighboring pixels into the fading trail. */
		for (i = screenSize - bottomBorder, j = topBorder / 2; i > 0; i--, j++)
		{
			offset = j + i/8192 - 4;
			color = (TempScreen2[offset                    ] * 12 +
			         TempScreen1[offset-VGAScreenSeg->pitch]      +
			         TempScreen1[offset-1                  ]      +
			         TempScreen1[offset+1                  ]      +
			         TempScreen1[offset+VGAScreenSeg->pitch]) / 16;

			TempScreen1[j] = color;
		}

		/* Copy the next diffusion frame to the display buffer. */
		memcpy((Uint8 *)VGAScreenSeg->pixels + topBorder, TempScreen1 + topBorder, screenSize - bottomBorder);

		JE_showVGA();

		timer--;
		wait_delay();

		/* Flip the buffer. */
		TempScreenSwap = TempScreen1;
		TempScreen1    = TempScreen2;
		TempScreen2    = TempScreenSwap;

	} while (!(timer == 0 || JE_anyButton()));

	levelWarningLines = 4;
}

bool titleScreen(void)
{
	enum MenuItemIndex
	{
		MENU_ITEM_NEW_GAME = 0,
		MENU_ITEM_LOAD_GAME,
		MENU_ITEM_HIGH_SCORES,
		MENU_ITEM_SETUP,
		MENU_ITEM_EXTRA,
		MENU_ITEM_DEMO,
		MENU_ITEM_QUIT,
	};

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	size_t selectedIndex = MENU_ITEM_NEW_GAME;
	size_t specialNameProgress[SA_ENGAGE] = { 0 };

	// Title menu labels: the data-file entries (menuText[]) with "Extra" inserted after
	// Setup. Instructions lives in the Extra menu. Keep in sync with MenuItemIndex above.
	const char *const titleLabels[] =
	{
		menuText[0],  // Start Game
		menuText[1],  // Load Game
		menuText[2],  // High Scores
		menuText[4],  // Setup
		"Extra",
		menuText[5],  // Demo
		menuText[6],  // Quit
	};

	const int xCenter = LEGACY_WIDTH / 2;
	const int yMenuItems = 104;
	const int hMenuItem = 13;
	int wMenuItem[COUNTOF(titleLabels)] = { 0 };

	for (; ; )
	{
		if (restart)
		{
			// Clear stale input before showing logo controls.
			touch_ui_suppress();
			touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
			play_song(SONG_TITLE);

			JE_loadPic(VGAScreen, 4, false);

			if (*opentyrian_commit)
				draw_font_hv_shadow(VGAScreen, 2, 183, opentyrian_commit, small_font, left_aligned, 15, 0, false, 1);
			draw_font_hv_shadow(VGAScreen, 2, 192, opentyrian_version, small_font, left_aligned, 15, 0, false, 1);

			if (moveTyrianLogoUp)
			{
				memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

				blit_sprite(VGAScreenSeg, 11, 62, PLANET_SHAPES, 146); // tyrian logo
				blit_sprite(VGAScreenSeg, 155, 41, PLANET_SHAPES, 151); // 2000(tm)
				fade_palette(colors, 10, 0, 255 - 16);

				if (smoothMotion)
				{
					// Slide the logo from elapsed time at the display rate.
					const Uint32 slideStart = SDL_GetTicks();
					const Uint32 slideMs = 800;  // ~matches the original stepped slide

					// The background is composed and expanded once: the logo never
					// reaches the edge columns the pillarbox gradient samples.
					SDL_Surface *logo_hi = NULL, *logo_hi_bg = NULL;
					const int ss = effective_supersample();
					if (ss > 1)
					{
						const int hiW = vga_width * ss;
						const int hiH = vga_height * ss;
						logo_hi = SDL_CreateRGBSurface(0, hiW, hiH, 8, 0, 0, 0, 0);
						logo_hi_bg = SDL_CreateRGBSurface(0, hiW, hiH, 8, 0, 0, 0, 0);
						if (logo_hi != NULL && logo_hi_bg != NULL)
						{
							memcpy(VGAScreen->pixels, VGAScreen2->pixels,
							       VGAScreen->pitch * VGAScreen->h);
							expand_frame_to_hi(video_compose_frame(), logo_hi_bg, ss);
						}
						else
						{
							if (logo_hi != NULL)
								SDL_FreeSurface(logo_hi);
							if (logo_hi_bg != NULL)
								SDL_FreeSurface(logo_hi_bg);
							logo_hi = NULL;
							logo_hi_bg = NULL;
						}
					}

					for (;;)
					{
						touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
						float t = (float)(SDL_GetTicks() - slideStart) / (float)slideMs;
						if (t > 1.0f)
							t = 1.0f;

						const float yLogo = 60.0f - 56.0f * t;
						const float y2K   = 45.0f + 28.0f * t;

						// The 1x frame stays current either way, so the resize-repaint
						// poll in service_SDL_events never presents a logo-less title.
						memcpy(VGAScreen->pixels, VGAScreen2->pixels,
						       VGAScreen->pitch * VGAScreen->h);
						blit_sprite(VGAScreenSeg, 11, (int)(yLogo + 0.5f),
						            PLANET_SHAPES, 146); // tyrian logo
						blit_sprite(VGAScreenSeg, 155, (int)(y2K + 0.5f),
						            PLANET_SHAPES, 151); // 2000(tm)

						if (logo_hi != NULL)
							title_logo_present_hi(logo_hi, logo_hi_bg, ss, yLogo, y2K);
						else
							JE_showVGA();

						if (JE_anyButton())
							break;
						if (t >= 1.0f)
							break;

						if (!output_vsync)
							limit_render_fps();
					}

					if (logo_hi != NULL)
					{
						SDL_FreeSurface(logo_hi);
						SDL_FreeSurface(logo_hi_bg);
					}
				}
				else
				{
					for (int yLogo = 60, y2K = 45; yLogo >= 4; yLogo -= 2, ++y2K)
					{
						touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
						setDelay(2);

						memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
						blit_sprite(VGAScreenSeg, 11, yLogo, PLANET_SHAPES, 146); // tyrian logo
						blit_sprite(VGAScreenSeg, 155, y2K, PLANET_SHAPES, 151); // 2000(tm)
						JE_showVGA();

						service_wait_delay();
						if (JE_anyButton())
							break;
					}
				}

				// Finish at the normal logo position after a skip.
				memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
				blit_sprite(VGAScreenSeg, 11, 4, PLANET_SHAPES, 146); // tyrian logo
				blit_sprite(VGAScreenSeg, 155, 73, PLANET_SHAPES, 151); // 2000(tm)
				moveTyrianLogoUp = false;
			}
			else
			{
				blit_sprite(VGAScreenSeg, 11, 4, PLANET_SHAPES, 146); // tyrian logo
				blit_sprite(VGAScreenSeg, 155, 73, PLANET_SHAPES, 151); // 2000(tm)
				fade_palette(colors, 10, 0, 255 - 16);
			}

			// Do not pass skip input to the title menu.
			touch_ui_suppress();
			newkey = false;
			newmouse = false;
			touch_ui_clear_layout();

			// Draw menu items.
			for (size_t i = 0; i < COUNTOF(titleLabels); ++i)
			{
				const char *const text = titleLabels[i];

				wMenuItem[i] = JE_textWidth(text, normal_font);
				const int x = xCenter - wMenuItem[i] / 2;
				const int y = yMenuItems + hMenuItem * i;

				draw_font_hv(VGAScreen, x - 1, y - 1, text, normal_font, left_aligned, 15, -10);
				draw_font_hv(VGAScreen, x + 1, y + 1, text, normal_font, left_aligned, 15, -10);
				draw_font_hv(VGAScreen, x + 1, y - 1, text, normal_font, left_aligned, 15, -10);
				draw_font_hv(VGAScreen, x - 1, y + 1, text, normal_font, left_aligned, 15, -10);
				draw_font_hv(VGAScreen, x,     y,     text, normal_font, left_aligned, 15, -3);
			}

			memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

			mouseCursor = MOUSE_POINTER_NORMAL;

			// Fade in menu items.
			fade_palette(colors, 20, 255 - 16 + 1, 255);

			restart = false;
		}

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);

		// Highlight selected menu item.
		draw_font_hv(VGAScreen, xCenter, yMenuItems + hMenuItem * selectedIndex, titleLabels[selectedIndex], normal_font, centered, 15, -1);

		service_SDL_events(true);

		JE_mouseStartFilter(0xF0);
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();  // pace the cursor redraw to the render-fps cap

		const Uint32 idleStartTick = SDL_GetTicks();

		// Poll finely instead of sleeping 16 ms so the outer loop (and mouse cursor)
		// redraws at the display's refresh rate; a still cursor yields the CPU.
		const float startMouseX = mouse_xf;
		const float startMouseY = mouse_yf;
		bool mouseMoved = false;
		for (;;)
		{
			// Play demo after idle for 30 seconds.
			if (SDL_GetTicks() - idleStartTick > 30000)
			{
				fade_black(15);

				play_demo = true;
				return true;
			}

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			mouseMoved = mouse_xf != startMouseX || mouse_yf != startMouseY;
			if (newkey || new_text || newmouse || mouseMoved)
				break;

			SDL_Delay(1);  // brief idle poll; a still cursor doesn't need redrawing
		}

		// Handle interaction.

		bool action = false;
		bool done = false;

		if (mouseMoved || newmouse)
			action = menuPointerSelect(&selectedIndex, COUNTOF(titleLabels), xCenter, yMenuItems,
			                           hMenuItem, hMenuItem, wMenuItem);

		menuKeySelect(&selectedIndex, COUNTOF(titleLabels), &action, &done);

		if (new_text)
		{
			for (size_t ti = 0U; last_text[ti] != '\0'; ++ti)
			{
				const char c = toupper(last_text[ti]);

				for (size_t i = 0; i < SA_ENGAGE; i++)
				{
					if (specialNameProgress[i] >= COUNTOF(specialName[i]) - 1 ||
						c != specialName[i][specialNameProgress[i]])
					{
						specialNameProgress[i] = 0;
						continue;
					}

					specialNameProgress[i]++;

					if (specialName[i][specialNameProgress[i]] == '\0')
					{
						if (i + 1 == SA_DESTRUCT)
						{
							fade_black(10);

							loadDestruct = true;
							return true;
						}
						else if (i + 1 == SA_ENGAGE)
						{
							JE_playSampleNum(V_DANGER);

							JE_whoa();
							set_colors((SDL_Color) { 0, 0, 0 }, 0, 255);

							if (newSuperTyrianGame())
								return true;

							restart = true;
						}
						else
						{
							fade_black(10);

							if (newSuperArcadeGame(i))
								return true;

							restart = true;
						}
					}
				}
			}
		}

		if (action)
		{
			JE_playSampleNum(S_SELECT);

			switch (selectedIndex)
			{
			case MENU_ITEM_NEW_GAME:
			{
				fade_black(15);

				if (newGame())
					return true;

				restart = true;
				break;
			}
			case MENU_ITEM_LOAD_GAME:
			{
				fade_black(15);

				if (JE_loadScreen(false, false) > 0)
					return true;

				restart = true;
				break;
			}
			case MENU_ITEM_HIGH_SCORES:
			{
				fade_black(15);

				JE_highScoreScreen();

				restart = true;
				break;
			}
			case MENU_ITEM_SETUP:
			{
				fade_black(15);

				setupMenu();

				restart = true;
				break;
			}
			case MENU_ITEM_EXTRA:
			{
				fade_black(15);

				if (extraMenu())  // launched a game (SuperTyrian / Super Arcade)?
					return true;

				restart = true;
				break;
			}
			case MENU_ITEM_DEMO:
			{
				fade_black(15);

				play_demo = true;
				return true;
			}
			case MENU_ITEM_QUIT:
			{
				fade_black(15);

				return false;
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

static bool newGame(void)
{
	if (gameplaySelect())
	{
		// Finish the online start handshake before initializing ships and wallets.
		if (isNetworkGame)
		{
			networkStartScreen();

			gameLoaded = true;
			return true;
		}

		// Endless was picked in the mode menu: newEndlessGame does its own difficulty select
		// and full setup (episode 1, starting cash, flags) and resets endlessMode on cancel.
		if (endlessMode)
			return newEndlessGame();

		if (timedBattleMode)
		{
			onePlayerAction = true;
			if (timedBattleSelect() && difficultySelect())
				gameLoaded = true;
		}
		else if (episodeSelect() && difficultySelect())
			gameLoaded = true;

		initialDifficulty = difficultyLevel;

		// Commit the selected loadout only after the start is confirmed.
		if (gameLoaded)
		{
			if (onePlayerAction)
			{
				player[0].cash = 0;

				player[0].items.ship = 8;  // Stalker
			}
			else if (twoPlayerMode)
			{
				for (uint i = 0; i < COUNTOF(player); ++i)
					player[i].cash = 0;

				player[0].items.ship = 11;  // Silver Ship

				difficultyLevel++;

				inputDevice[0] = 1;
				inputDevice[1] = 2;
			}
			else if (richMode)
			{
				player[0].cash = 1000000;
			}
			else
			{
				// keeps whatever ship the session already carries

				const ulong initial_cash[] = { 10000, 15000, 20000, 30000, 20000 };

				assert(episodeNum >= 1 && episodeNum <= EPISODE_AVAILABLE);
				player[0].cash = initial_cash[episodeNum - 1];
			}
		}
	}

	return gameLoaded;
}

bool newSuperArcadeGame(unsigned int i)
{
	if (episodeSelect() && difficultySelect())
	{
		// Claimed only once the picks are confirmed, so backing out leaves the ship alone.
		player[0].items.ship = SAShip[i];

		/* Start special mode! */
		JE_loadPic(VGAScreen, 1, false);
		JE_clr256(VGAScreen);
		JE_dString(VGAScreen, JE_fontCenter(superShips[0], FONT_SHAPES), 30, superShips[0], FONT_SHAPES);
		JE_dString(VGAScreen, JE_fontCenter(superShips[i + 1], SMALL_FONT_SHAPES), 100, superShips[i + 1], SMALL_FONT_SHAPES);
		tempW = ships[player[0].items.ship].shipgraphic;
		if (tempW > 500)
			blit_sprite2x2(VGAScreen, 148, 70, spriteSheetT2000, tempW - 500);
		else if (tempW == 1)
		{
			// Nort Ship: shipgraphic 1 is a sentinel (see JE_playerMovement / JE_drawItem), so draw its
			// two-piece hull here rather than treating 1 as a sprite index.
			blit_sprite2x2(VGAScreen, 148 - 12, 70, spriteSheet9, 220);
			blit_sprite2x2(VGAScreen, 148 + 12, 70, spriteSheet9, 222);
		}
		else
			blit_sprite2x2(VGAScreen, 148, 70, spriteSheet9, tempW);

		JE_showVGA();
		fade_palette(colors, 50, 0, 255);

		wait_input(true, true, true);

		twoPlayerMode = false;
		onePlayerAction = true;
		superArcadeMode = i + 1;
		timedBattleMode = false;
		gameLoaded = true;
		initialDifficulty = ++difficultyLevel;

		player[0].cash = 0;

		// Online Super Arcade stores each player's ship for per-ship weapon lookup.
		player[0].items.super_arcade_mode = (Uint8)(i + 1);
		player[0].items.weapon[FRONT_WEAPON].id = SAWeapon[i][0];
		player[0].items.special = SASpecialWeapon[i];
		if (superArcadeMode == SA_NORTSHIPZ)
		{
			for (uint i = 0; i < COUNTOF(player[0].items.sidekick); ++i)
				player[0].items.sidekick[i] = 24;  // Companion Ship Quicksilver
		}

		fade_black(10);
	}

	return gameLoaded;
}

bool newSuperTyrianGame(void)
{
	/* SuperTyrian */

	initialDifficulty = keysactive[SDL_SCANCODE_SCROLLLOCK] ? DIFFICULTY_SUICIDE : DIFFICULTY_LORD_OF_GAME;

	JE_clr256(VGAScreen);
	JE_outText(VGAScreen, 10, 10, superTyrianText[0], 15, 4);
	if (initialDifficulty == DIFFICULTY_LORD_OF_GAME)
		JE_outText(VGAScreen, 10, 20, superTyrianText[1], 15, 4);
	else
		JE_outText(VGAScreen, 10, 20, superTyrianText[2], 15, 4);
	JE_outText(VGAScreen, 10, 30, superTyrianText[3], 15, 4);
	if (initialDifficulty == DIFFICULTY_LORD_OF_GAME)
		JE_outText(VGAScreen, 10, 40, superTyrianText[4], 15, 4);
	JE_outText(VGAScreen, 10, 60, superTyrianText[5], 15, 4);

	char buf[10 + 1 + 15 + 1];
	snprintf(buf, sizeof(buf), "%s %s", miscTextB[4], pName[0]);
	JE_dString(VGAScreen, JE_fontCenter(buf, FONT_SHAPES), 110, buf, FONT_SHAPES);

	play_song(16);
	JE_playSampleNum(V_GOOD_LUCK);

	JE_showVGA();
	fade_palette(colors, 10, 0, 255);

	wait_noinput(true, true, true);
	wait_input(true, true, true);

	fade_black(1);
	if (episodeSelect()) // T2000 let you choose the starting episode
	{
		constantDie = false;
		superTyrian = true;
		onePlayerAction = true;
		timedBattleMode = false;
		gameLoaded = true;
		difficultyLevel = initialDifficulty;

		player[0].cash = 0;

		player[0].items.ship = 13;                     // The Stalker 21.126
		player[0].items.weapon[FRONT_WEAPON].id = 39;  // Atomic RailGun

		fade_black(10);
		return true;
	}
	else
	{
		play_song(SONG_TITLE);
		return false;
	}

}

static bool newEndlessGame(void)
{
	/* Endless roguelite mode. */

	// Absorb the menu-selection keypress/click so it doesn't fall straight through
	// into the difficulty picker (which would auto-select the default and skip it).
	wait_noinput(true, true, true);

	// Endless always starts at episode 1; the run traverses episodes as it deepens.
	if (!isNetworkGame)
		customEpisodeSessionEnd();
	JE_initEpisode(1);
	initial_episode_num = episodeNum;

	// Collect Endless run options before the difficulty picker.
	char seedbuf[ENDLESS_SEED_MAXLEN];
	EndlessRunMode runMode = ENDLESS_RUNMODE_STANDARD;
	EndlessBaseRule baseRule = ENDLESS_BASE_VARIED;
	if (!endlessSeedSelect(seedbuf, sizeof(seedbuf), &runMode, &baseRule))
	{
		endlessMode = false;
		play_song(SONG_TITLE);
		return false;
	}

	if (!difficultySelect())
	{
		endlessMode = false;  // cancelled: don't leave the mode flag set for the next new game
		play_song(SONG_TITLE);
		return false;
	}
	initialDifficulty = difficultyLevel;

	endlessResetRun();
	endlessSetSeed(seedbuf);  // establish the run's seeded structural RNG (endlessResetRun blanked it)
	endlessRunMode = runMode;  // apply the seed screen's mode choice (endlessResetRun reset it)
	endlessRunBaseRule = baseRule;   // ...and its chart rule, pinned for the whole run
	endlessRecordRunStart();  // baseline the all-time record so this run's "(+n)" measures only what IT gained

	endlessMode = true;
	onePlayerAction = false;  // full game: cash economy + between-level shops, NOT arcade orb drops
	timedBattleMode = false;
	twoPlayerMode = false;
	superTyrian = false;
	superArcadeMode = SA_NONE;
	gameLoaded = true;
	difficultyLevel = initialDifficulty;

	// Credit starting cash through the ledger so its invariant holds from zone one.
	player[0].cash = 0;
	endlessCashResync();
	endlessCashCredit(endlessStartingCash(), ENDLESS_CASH_START);

	endlessApplyStartingLoadout();  // Atomic RailGun front gun (the depth-0 outpost re-applies it too)

	fade_black(10);
	return true;
}

void intro_logos(void)
{
	moveTyrianLogoUp = true;
	touch_ui_suppress();

	SDL_FillRect(VGAScreen, NULL, 0);

	fade_white(25);

	JE_loadPic(VGAScreen, 10, false);
	touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
	JE_showVGA();

	fade_palette(colors, 25, 0, 255);

	setDelay(200);
	wait_delayorinput();

	fade_black(10);

	JE_loadPic(VGAScreen, 12, false);
	touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
	JE_showVGA();

	fade_palette(colors, 10, 0, 255);

	setDelay(200);
	wait_delayorinput();

	fade_black(10);
}
