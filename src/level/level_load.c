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
#include "level_load.h"

#include "animlib.h"
#include "audio.h"
#include "background.h"
#include "config.h"
#include "crashlog.h"
#include "custom_episode.h"
#include "demo.h"
#include "draw.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "game_loop.h"
#include "game_menu.h"
#include "game_state.h"
#include "helptext.h"
#include "high_scores.h"
#include "item_prices.h"
#include "keyboard.h"
#include "level_end.h"
#include "lvllib.h"
#include "lvlmast.h"
#include "mtrand.h"
#include "musmast.h"
#include "network.h"
#include "palette.h"
#include "params.h"
#include "pcxmast.h"
#include "picload.h"
#include "player.h"
#include "qa.h"
#include "savegame.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "str_util.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Level Event Data */
static JE_boolean loadLevelOk;
static FILE *ep_f = NULL;
JE_boolean jumpSection;

struct JE_EventRecType eventRec[EVENT_MAXIMUM]; /* [1..eventMaximum] */
JE_word levelEnemyMax;
JE_word levelEnemyFrequency;
JE_word levelEnemy[40]; /* [1..40] */

/* Data used for ItemScreen procedure to indicate items available */
JE_byte itemAvail[9][10]; /* [1..9, 1..10] */
JE_byte itemAvailMax[9]; /* [1..9] */

JE_boolean levelWarningDisplay;
JE_byte levelWarningLines;
static char levelWarningText[12][61]; /* [1..10] of string [60] */
JE_boolean warningRed;

JE_byte warningSoundDelay;
JE_byte warningCol;
JE_shortint warningColChange;

static const JE_byte SANextShip[SA + 2] /* [0..SA + 1] */ = { 3, 8, 6, 2, 5, 1, 4, 10, 9, 7, 3 };

static void JE_displayText(void);

static void copy_screen_to_buffer(Uint8* buffer)
{
	Uint8* src = VGAScreen->pixels;
	for (int y = 0; y < vga_height; ++y)
	{
		memcpy(buffer, src, VGAScreen->pitch);
		buffer += VGAScreen->pitch;
		src += VGAScreen->pitch;
	}
}

static void copy_buffer_to_screen(const Uint8* buffer)
{
	Uint8* dst = VGAScreen->pixels;
	for (int y = 0; y < vga_height; ++y)
	{
		memcpy(dst, buffer, VGAScreen->pitch);
		buffer += VGAScreen->pitch;
		dst += VGAScreen->pitch;
	}
}

// Full-screen picture-wipe styles used by the level-script commands U / V / R.
typedef enum
{
	WIPE_U,  // vertical wipe (new picture slides in over the old)
	WIPE_V,  // vertical wipe (new picture revealed from the opposite edge)
	WIPE_R,  // horizontal wipe
} WipeKind;

// Keep the shipped wipe pixels; animate_picture_wipe changes only their pacing.
static void compose_wipe_frame(WipeKind kind, int z, const Uint8 *pic_buffer)
{
	const int pitch = VGAScreen->pitch;
	Uint8 *vga = VGAScreen->pixels;
	Uint8 *vga2 = VGAScreen2->pixels;
	const Uint8 *pic;

	switch (kind)
	{
	case WIPE_U:
		pic = pic_buffer + (vga_height - 1 - z) * pitch;
		for (int y = 0; y < vga_height; y++)
		{
			if (y <= z)
			{
				memcpy(vga, pic, pitch);
				pic += pitch;
			}
			else
			{
				memcpy(vga, vga2, pitch);
				vga2 += pitch;
			}
			vga += pitch;
		}
		break;

	case WIPE_V:
		pic = pic_buffer;
		for (int y = 0; y < vga_height; y++)
		{
			if (y <= vga_height - 1 - z)
			{
				memcpy(vga, vga2, pitch);
				vga2 += pitch;
			}
			else
			{
				memcpy(vga, pic, pitch);
				pic += pitch;
			}
			vga += pitch;
		}
		break;

	case WIPE_R:
		pic = pic_buffer;
		for (int y = 0; y < vga_height; y++)
		{
			memcpy(vga, vga2 + z, pitch - 1 - z);
			vga += pitch - z;
			vga2 += VGAScreen2->pitch;
			memcpy(vga, pic, z + 1);
			vga += z;
			pic += pitch;
		}
		break;
	}
}

static void compose_wipe_frame_hi(WipeKind kind, int n, SDL_Surface *old_hi, SDL_Surface *new_hi,
                                  SDL_Surface *canvas, int scale)
{
	const int H = vga_height * scale, W = vga_width * scale;

	for (int y = 0; y < H; ++y)
	{
		Uint8 *const dst = (Uint8 *)canvas->pixels + y * canvas->pitch;
		const Uint8 *src;

		switch (kind)
		{
		case WIPE_U:
			src = (y < n) ? (const Uint8 *)new_hi->pixels + (H - n + y) * new_hi->pitch
			              : (const Uint8 *)old_hi->pixels + (y - n) * old_hi->pitch;
			memcpy(dst, src, (size_t)W);
			break;

		case WIPE_V:
			src = (y < H - n) ? (const Uint8 *)old_hi->pixels + y * old_hi->pitch
			                  : (const Uint8 *)new_hi->pixels + (y - (H - n)) * new_hi->pitch;
			memcpy(dst, src, (size_t)W);
			break;

		case WIPE_R:
			memcpy(dst, (const Uint8 *)old_hi->pixels + y * old_hi->pitch + n, (size_t)(W - n));
			memcpy(dst + W - n, (const Uint8 *)new_hi->pixels + y * new_hi->pitch, (size_t)n);
			break;
		}
	}
}

// Animate a full-screen wipe at tick rate or display rate, with the same duration
// and final image. Input skips to the end.
static void animate_picture_wipe(WipeKind kind, const Uint8 *pic_buffer)
{
	const int steps = (kind == WIPE_R) ? (VGAScreen->pitch - 1) : vga_height;

	if (smoothMotion)
	{
		const float duration = steps * get_delay_period();  // ms; == classic (steps x one delay unit)
		const Uint64 freq = SDL_GetPerformanceFrequency();
		const Uint64 begin = SDL_GetPerformanceCounter();
		const float counter_to_ms = 1000.0f / (float)freq;

		const int ss = effective_supersample();
		SDL_Surface *old_hi = NULL, *new_hi = NULL, *canvas = NULL, *frame = NULL;
		SDL_Surface *const pic_view = (ss > 1)
		    ? SDL_CreateRGBSurfaceFrom((void *)pic_buffer, vga_width, vga_height, 8,
		                               VGAScreen->pitch, 0, 0, 0, 0)
		    : NULL;
		const bool hi = pic_view != NULL && video_ensure_scaled_surface(&old_hi, ss) != NULL &&
		                video_ensure_scaled_surface(&new_hi, ss) != NULL &&
		                video_ensure_scaled_surface(&canvas, ss) != NULL &&
		                video_ensure_scaled_surface(&frame, ss) != NULL;
		if (hi)
		{
			expand_frame_to_hi(VGAScreen2, old_hi, ss);
			expand_frame_to_hi(pic_view, new_hi, ss);
		}

		for (;;)
		{
			if (newkey)
				break;

			const float t = (float)(SDL_GetPerformanceCounter() - begin) * counter_to_ms / duration;
			const bool done = t >= 1.0f;
			int z = done ? steps - 1 : (int)(t * steps);
			if (z > steps - 1)
				z = steps - 1;

			compose_wipe_frame(kind, z, pic_buffer);
			if (hi)
			{
				const int lead = (kind == WIPE_U) ? ss : 0;
				const int n_max = (steps - 1) * ss + lead;
				int n = done ? n_max : (int)(t * steps * ss) + lead;
				if (n > n_max)
					n = n_max;
				compose_wipe_frame_hi(kind, n, old_hi, new_hi, canvas, ss);

				expand_frame_to_hi(video_compose_frame(), frame, ss);
				const int x_offset = video_get_menu_x_offset();
				const int copy_w = (x_offset > 0 ? LEGACY_WIDTH : vga_width) * ss;
				for (int row = 0; row < vga_height * ss; ++row)
					memcpy((Uint8 *)frame->pixels + row * frame->pitch + x_offset * ss,
					       (const Uint8 *)canvas->pixels + row * canvas->pitch, (size_t)copy_w);
				present_hi(frame);
			}
			else
			{
				JE_showVGA();
			}

			if (done)
				break;

			if (!output_vsync)
				limit_render_fps();
			service_SDL_events(false);
		}

		SDL_FreeSurface(pic_view);
		SDL_FreeSurface(old_hi);
		SDL_FreeSurface(new_hi);
		SDL_FreeSurface(canvas);
		SDL_FreeSurface(frame);
	}
	else
	{
		for (int z = 0; z < steps; z++)
		{
			if (!newkey)
			{
				setDelay(1);
				compose_wipe_frame(kind, z, pic_buffer);
				JE_showVGA();
				service_wait_delay();
			}
		}
	}
}

// Backdate E5 SAVARA cannon groups so their low sprites scroll in together.
static void event_backdate_savara_cannons(void)
{
	if (episodeNum != 5 || lvlFileNum != 4)
		return;

	for (JE_word i = 0; i < maxEvent; ++i)
	{
		if (eventRec[i].eventtype != 6 || eventRec[i].eventdat5 <= 0)
			continue;

		const JE_word time = eventRec[i].eventtime;
		const JE_byte link = eventRec[i].eventdat4;

		int shift = 0;
		for (JE_word j = 0; j < maxEvent; ++j)
		{
			if (eventRec[j].eventtype == 6 && eventRec[j].eventtime == time &&
			    eventRec[j].eventdat4 == link && eventRec[j].eventdat5 > shift)
				shift = eventRec[j].eventdat5;
		}
		if (time <= (JE_word)shift)
			continue;

		for (JE_word j = 0; j < maxEvent; ++j)
		{
			if (eventRec[j].eventtype == 6 && eventRec[j].eventtime == time &&
			    eventRec[j].eventdat4 == link)
			{
				eventRec[j].eventtime -= shift;
				eventRec[j].eventdat5 -= shift;
			}
		}
	}

	for (JE_word i = 1; i < maxEvent; ++i)
	{
		const struct JE_EventRecType moved = eventRec[i];
		JE_word j = i;
		while (j > 0 && eventRec[j - 1].eventtime > moved.eventtime)
		{
			eventRec[j] = eventRec[j - 1];
			--j;
		}
		eventRec[j] = moved;
	}
}

// Tyrian 2000 moved every SURFACE arch nub (enemy 212) to y 116, where it pops in mid-screen 194px
// below its arch. Restore the Tyrian 2.1 spot: 18px left of the arch's enemy 213 and 6px lower.
static void event_reattach_surface_arch_nubs(void)
{
	if (episodeNum != 4 || lvlFileNum != 4 || customEpisodeActive())
		return;

	for (JE_word i = 0; i < maxEvent; ++i)
	{
		struct JE_EventRecType *const nub = &eventRec[i];
		if (nub->eventtype != 7 || nub->eventdat != 212 || nub->eventdat5 != 116)
			continue;

		for (JE_word j = 0; j < maxEvent; ++j)
		{
			const struct JE_EventRecType *const arch = &eventRec[j];
			if (arch->eventtype == 7 && arch->eventdat == 213 &&
			    arch->eventtime == nub->eventtime && arch->eventdat2 == nub->eventdat2)
			{
				nub->eventdat2 = arch->eventdat2 - 18;
				nub->eventdat5 = arch->eventdat5 + 6;
				break;
			}
		}
	}
}

/* Read the event script that follows a level's random-enemy list, then apply the shipped-data
 * corrections. Fails when the script would overflow eventRec. */
bool level_read_event_script(FILE *f)
{
	fread_u16_die(&maxEvent, 1, f);
	if (maxEvent >= COUNTOF(eventRec))
		return false;

	for (JE_word i = 0; i < maxEvent; ++i)
	{
		fread_u16_die(&eventRec[i].eventtime, 1, f);
		fread_u8_die( &eventRec[i].eventtype, 1, f);
		fread_s16_die(&eventRec[i].eventdat,  1, f);
		fread_s16_die(&eventRec[i].eventdat2, 1, f);
		fread_s8_die( &eventRec[i].eventdat3, 1, f);
		fread_s8_die( &eventRec[i].eventdat5, 1, f);
		fread_s8_die( &eventRec[i].eventdat6, 1, f);
		fread_u8_die( &eventRec[i].eventdat4, 1, f);
	}
	eventRec[maxEvent].eventtime = 65500;  /* Sentinel event. */

	event_backdate_savara_cannons();
	event_reattach_surface_arch_nubs();
	return true;
}

/* --- Load Level/Map Data --- */
void JE_updateWarning(SDL_Surface * screen)
{
	if (getDelayTicks2() == 0)
	{ /*Update Color Bars*/

		warningCol += warningColChange;
		if (warningCol > 14 * 16 + 10 || warningCol < 14 * 16 + 4)
		{
			warningColChange = -warningColChange;
		}
		fill_rectangle_xy(screen, 0, 0, 319, 5, warningCol);
		fill_rectangle_xy(screen, 0, 194, 319, 199, warningCol);
		JE_showVGA();

		setDelay2(6);

		if (warningSoundDelay > 0)
		{
			warningSoundDelay--;
		}
		else
		{
			warningSoundDelay = 14;
			JE_playSampleNum(S_WARNING);
		}
	}
}

void close_episode_file(void)
{
	if (ep_f != NULL)
	{
		fclose(ep_f);
		ep_f = NULL;
	}
}

void JE_loadMap(void)
{
	JE_DanCShape shape;

	JE_word x, y;
	JE_integer yy;
	JE_word mapSh[3][128]; /* [1..3, 0..127] */
	JE_byte *ref[3][128]; /* [1..3, 0..127] */
	char s[256];

	static JE_byte mapBuf[15 * 600]; /* [1..15 * 600] */
	JE_word bufLoc;

	char buffer[256];
	int i;
	static Uint8 pic_buffer[vga_width * vga_height]; /* reusable 8-bit wipe buffer */

	crashlog_set_phase("loading level map");

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: JE_loadMap mainLevel=%d gameLoaded=%d\n",
		        (int)mainLevel, (int)gameLoaded);
		fflush(stderr);
	}

	lastCubeMax = cubeMax;

	/*Defaults*/
	songBuy = DEFAULT_SONG_BUY;  /*Item Screen default song*/

	/* Load LEVELS.DAT - Section = MAINLEVEL */
	saveLevel = mainLevel;

new_game:
	set_menu_centered(true);
	galagaMode = false;
	useLastBank = false;
	extraGame = false;
	engageMode = false;
	haltGame = false;

	gameLoaded = false;

	if (!play_demo)
	{
		do
		{
			ep_f = dir_fopen_die(JE_episodeDir(), episode_file, "rb");
			const long ep_end = ftell_eof(ep_f);  // guard the scans below against reading past EOF

			jumpSection = false;
			loadLevelOk = false;

			/* Bound the mainLevel section scan by EOF and recover invalid saves to the title screen. */
			int x = 0;
			while (x < mainLevel)
			{
				if (ftell(ep_f) >= ep_end)
				{
					char detail[192];
					snprintf(detail, sizeof(detail),
					         "episode %d has no section %d (file holds only %d); out-of-range save/level -- returning to title",
					         (int)episodeNum, (int)mainLevel, x);
					fprintf(stderr, "error: %s\n", detail);
					crashlog_note("RECOVERED (JE_loadMap: level section out of range)", detail);
					close_episode_file();
					mainLevel = 0;   // caller (JE_main) sees mainLevel == 0 and returns to the title screen
					return;
				}
				read_encrypted_pascal_string(s, sizeof(s), ep_f);
				if (s[0] == '*')
				{
					x++;
					s[0] = ' ';
				}
			}

			ESCPressed = false;

			do
			{
				if (gameLoaded)
				{
					close_episode_file();

					if (mainLevel == 0)  // if quit itemscreen
						return;          // back to title screen
					else if (endlessMode)  // endless save loaded from within this shop: bail out so JE_main
						return;             // resumes at its outpost (endlessBetweenLevels), not a campaign reload
					else
						goto new_game;
				}

				// Same EOF guard as the section seek above: a section that ends without a ']L' or a
				// jump (truncated/garbled data) would read past EOF into fread_die -> exit(). Recover.
				if (ftell(ep_f) >= ep_end)
				{
					char detail[192];
					snprintf(detail, sizeof(detail),
					         "episode %d section %d ended with no level to load (truncated/garbled data) -- returning to title",
					         (int)episodeNum, (int)mainLevel);
					fprintf(stderr, "error: %s\n", detail);
					crashlog_note("RECOVERED (JE_loadMap: section had no loadable level)", detail);
					close_episode_file();
					mainLevel = 0;
					return;
				}

				strcpy(s, " ");
				read_encrypted_pascal_string(s, sizeof(s), ep_f);

				if (qa_net_gameplay_ticks > 0 && s[0] == ']')
				{
					fprintf(stderr, "net gameplay: script ]%c\n", s[1]);
					fflush(stderr);
				}

				if (s[0] == ']')
				{
					switch (s[1])
					{
					case 'A':
						JE_playAnim("tyrend.anm", 0, 7);
						break;

					case 'G':
						mapOrigin = atoi(s + 4);
						mapPNum   = atoi(s + 7);
						for (i = 0; i < mapPNum; i++)
						{
							mapPlanet[i] = atoi(s + 1 + (i + 1) * 8);
							mapSection[i] = atoi(s + 4 + (i + 1) * 8);
						}
						break;

					case '?':
						temp = atoi(s + 4);
						for (i = 0; i < temp; i++)
						{
							cubeList[i] = atoi(s + 3 + (i + 1) * 4);
						}
						if (cubeMax > temp)
							cubeMax = temp;
						break;

					case '!':
						cubeMax = atoi(s + 4);    /*Auto set CubeMax*/
						break;

					case '+':
						temp = atoi(s + 4);
						cubeMax += temp;
						if (cubeMax > 4)
							cubeMax = 4;
						break;

					case 'g':
						galagaMode = true;   /*GALAGA mode*/

						// Solo arms the spawnable wing here. Co-op equipped both ships in ']e'
						// already and flies them like the solo ship: front gun only, no Vulcan.
						if (!coop_mode_active())
						{
							player[1].items = player[0].items;
							player[1].items.weapon[REAR_WEAPON].id = 15;  // Vulcan Cannon
							for (uint i = 0; i < COUNTOF(player[1].items.sidekick); ++i)
								player[1].items.sidekick[i] = 0;          // None
						}
						break;

					case 'x':
						extraGame = true;
						break;

					case 'e': // ENGAGE mode, used for mini-games
						engageMode = true;
						doNotSaveBackup = true;
						constantDie = false;
						onePlayerAction = true;
						superTyrian = true;
						// Online co-op flies the mini-game with both ships; solo drops to the
						// single ship exactly as shipped.
						if (!coop_mode_active())
							twoPlayerMode = false;

						// Co-op issues the mini-game loadout to both players as equals; TIME WAR
						// has no ']g' to copy it onto ship two, so it happens here.
						for (uint p = 0; p < (coop_mode_active() ? 2u : 1u); ++p)
						{
							player[p].cash = 0;

							player[p].items.ship = 13;                     // The Stalker 21.126
							player[p].items.weapon[FRONT_WEAPON].id = 39;  // Atomic RailGun
							player[p].items.weapon[REAR_WEAPON].id = 0;    // None
							for (uint i = 0; i < COUNTOF(player[p].items.sidekick); ++i)
								player[p].items.sidekick[i] = 0;           // None
							player[p].items.generator = 2;                 // Advanced MR-12
							player[p].items.shield = 4;                    // Advanced Integrity Field
							player[p].items.special = 0;                   // None

							player[p].items.weapon[FRONT_WEAPON].power = 3;
							player[p].items.weapon[REAR_WEAPON].power = 1;
						}
						break;

					case 'J':  // section jump
						temp = atoi(s + 3);
						mainLevel = temp;
						jumpSection = true;
						break;

					case '2':  // two-player section jump
						temp = atoi(s + 3);
						if (arcade_rules_active())
						{
							mainLevel = temp;
							jumpSection = true;
						}
						break;

					case 'w':  // Stalker 21.126 section jump
						temp = atoi(s + 3);   /*Allowed to go to Time War?*/
						// A session with two full ships needs two tickets: the Time War door
						// only opens when both are flying the Stalker 21.126.
						if (player[0].items.ship == 13
						    && (!dual_ship_mode() || player[1].items.ship == 13))
						{
							mainLevel = temp;
							jumpSection = true;
						}
						break;

					case 't':
						temp = atoi(s + 3);
						if (levelTimer && levelTimerCountdown == 0)
						{
							mainLevel = temp;
							jumpSection = true;
						}
						break;

					case 'l':
						temp = atoi(s + 3);
						if (!all_players_alive())
						{
							mainLevel = temp;
							jumpSection = true;
						}
						break;

					case 's':
						saveLevel = mainLevel;
						break; /*store savepoint*/

					case 'b':
						// Online rides the 2-player LAST LEVEL slot; solo keeps the original slot 11.
						temp = backup_save_slot();
						// Endless saves at outposts; its mid-level state is not stable.
						if (!endlessMode)
						{
							JE_saveGame(temp, "LAST LEVEL    ");   // drops any stale endless half of the slot
						}
						break;

					case 'i':
						temp = atoi(s + 3);
						songBuy = temp - 1;
						break;

					case 'I': /*Load Items Available Information*/
						// Parse campaign shop rows in Endless without installing them.
						if (endlessMode)
						{
							for (int i = 0; i < 9; ++i)
								read_encrypted_pascal_string(s, sizeof(s), ep_f);
							break;
						}

						memset(&itemAvail, 0, sizeof(itemAvail));

						for (int i = 0; i < 9; ++i)
						{
							read_encrypted_pascal_string(s, sizeof(s), ep_f);

							char buf[256];
							strncpy(buf, (strlen(s) > 8) ? s + 8 : "", sizeof(buf));

							int j = 0, temp;
							while (j < (int)COUNTOF(itemAvail[i]) && str_pop_int(buf, &temp))
								itemAvail[i][j++] = temp;
							itemAvailMax[i] = j;
						}

						// Inject the restored Charge-Laser into its original DOS shop lists.
						if (chargeLaserSlot > 0 &&
						    ((episodeNum == 2 && (mainLevel == 3 || mainLevel == 11 || mainLevel == 16)) ||
						     (episodeNum == 3 && mainLevel == 16)))
						{
							if (itemAvailMax[5] < 10) itemAvail[5][itemAvailMax[5]++] = chargeLaserSlot;
							if (itemAvailMax[6] < 10) itemAvail[6][itemAvailMax[6]++] = chargeLaserSlot;
						}

						JE_itemScreen();
						break;

					case 'L':
						if (qa_net_gameplay_ticks > 0)
						{
							fprintf(stderr, "net gameplay: level record read\n");
							fflush(stderr);
						}
						nextLevel = atoi(s + 9);
						SDL_strlcpy(levelName, s + 13, 10);
						levelSong = atoi(s + 22);
						if (nextLevel == 0)
						{
							nextLevel = mainLevel + 1;
						}
						lvlFileNum = atoi(s + 25);
						loadLevelOk = true;
						bonusLevelCurrent = (strlen(s) > 28) & (s[28] == '$');
						normalBonusLevelCurrent = (strlen(s) > 27) & (s[27] == '$');
						gameJustLoaded = false;
						break;

					case '@':
						useLastBank = !useLastBank;
						break;

					case 'Q':
						ESCPressed = false;
						temp = secretHint + (mt_rand() % 3) * 3;

						if (twoPlayerMode)
						{
							for (uint i = 0; i < 2; ++i)
							{
								char label[80];
								JE_playerScoreLabel((JE_byte)(i + 1), label, sizeof(label));
								snprintf(levelWarningText[i], sizeof(*levelWarningText), "%s %lld",
								         label, (long long)player[i].cash);
							}
							strcpy(levelWarningText[2], "");
							levelWarningLines = 3;
						}
						else
						{
							sprintf(levelWarningText[0], "%s %lld", miscText[37], (long long)JE_totalScore(&player[0]));
							strcpy(levelWarningText[1], "");
							levelWarningLines = 2;
						}

						for (x = 0; x < temp - 1; x++)
						{
							do
							{
								read_encrypted_pascal_string(s, sizeof(s), ep_f);
							} while (s[0] != '#');
						}

						do
						{
							read_encrypted_pascal_string(s, sizeof(s), ep_f);
							strcpy(levelWarningText[levelWarningLines], s);
							levelWarningLines++;
						} while (s[0] != '#');
						levelWarningLines--;

						frameCountMax = 4;
						if (!constantPlay)
							JE_displayText();

						fade_black(15);

						JE_nextEpisode();

						if (jumpBackToEpisode1 && !arcade_rules_active())
						{
							JE_loadPic(VGAScreen, 1, false); // huh?
							JE_clr256(VGAScreen);

							if (superTyrian)
							{
								// if completed Zinglon's Revenge, show SuperTyrian and Destruct codes
								// if completed SuperTyrian, show Nort-Ship Z code
								superArcadeMode = (initialDifficulty == DIFFICULTY_LORD_OF_GAME) ? 8 : 1;
							}

							if (superArcadeMode < SA_ENGAGE)
							{
								if (SANextShip[superArcadeMode] == SA_ENGAGE)
								{
									sprintf(buffer, "%s %s", miscTextB[4], pName[0]);
									JE_dString(VGAScreen, JE_fontCenter(buffer, FONT_SHAPES), 100, buffer, FONT_SHAPES);

									sprintf(buffer, "Or play... %s", specialName[SA_DESTRUCT - 1]);
									JE_dString(VGAScreen, 80, 180, buffer, SMALL_FONT_SHAPES);
								}
								else
								{
									JE_dString(VGAScreen, JE_fontCenter(superShips[0], FONT_SHAPES), 30, superShips[0], FONT_SHAPES);
									JE_dString(VGAScreen, JE_fontCenter(superShips[SANextShip[superArcadeMode]], SMALL_FONT_SHAPES), 100, superShips[SANextShip[superArcadeMode]], SMALL_FONT_SHAPES);
								}

								if (SANextShip[superArcadeMode] < SA_NORTSHIPZ)
									blit_sprite2x2(VGAScreen, 148, 70, spriteSheet9, ships[SAShip[SANextShip[superArcadeMode]-1]].shipgraphic);
								else if (SANextShip[superArcadeMode] == SA_NORTSHIPZ)
									trentWin = true;

								sprintf(buffer, "Type %s at Title", specialName[SANextShip[superArcadeMode]-1]);
								JE_dString(VGAScreen, JE_fontCenter(buffer, SMALL_FONT_SHAPES), 160, buffer, SMALL_FONT_SHAPES);
								JE_showVGA();

								fade_palette(colors, 50, 0, 255);

								if (!constantPlay)
									wait_input(true, true, true);

								/* trentWin leaves the outer game loop and closes the network session. Keep
								 * the final unlock visible until both players dismiss it. */
								if (isNetworkGame && trentWin)
									network_end_screen_rendezvous(true);
							}

							jumpSection = true;

							if (superTyrian)
							{
								fade_black(10);

								// back to titlescreen
								mainLevel = 0;
								close_episode_file();
								return;
							}
						}
						break;

					case 'P':
						if (!constantPlay)
						{
							JE_word tempX = atoi(s + 3);
							if (tempX > 900)
							{
								memcpy(colors, palettes[pcxpal[tempX-1 - 900]], sizeof(colors));
								JE_clr256(VGAScreen);
								JE_showVGA();
								fade_palette(colors, 1, 0, 255);
							}
							else
							{
								if (tempX == 0)
									JE_loadPCX("tshp2.pcx");
								else
									JE_loadPic(VGAScreen, tempX, false);

								JE_showVGA();
								fade_palette(colors, 10, 0, 255);
							}
						}
						break;

					case 'U':
						if (!constantPlay)
						{
							memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

							JE_word tempX = atoi(s + 3);
							JE_loadPic(VGAScreen, tempX, false);
							copy_screen_to_buffer(pic_buffer);

							service_SDL_events(true);

							animate_picture_wipe(WIPE_U, pic_buffer);

							copy_buffer_to_screen(pic_buffer);
						}
						break;

					case 'V':
						if (!constantPlay)
						{
							/* Picture wipes run outside synchronized simulation. */
							memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

							JE_word tempX = atoi(s + 3);
							JE_loadPic(VGAScreen, tempX, false);
							copy_screen_to_buffer(pic_buffer);

							service_SDL_events(true);

							animate_picture_wipe(WIPE_V, pic_buffer);

							copy_buffer_to_screen(pic_buffer);
						}
						break;

					case 'R':
						if (!constantPlay)
						{
							/* Picture wipes run outside synchronized simulation. */
							memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

							JE_word tempX = atoi(s + 3);
							JE_loadPic(VGAScreen, tempX, false);
							copy_screen_to_buffer(pic_buffer);

							service_SDL_events(true);

							animate_picture_wipe(WIPE_R, pic_buffer);

							copy_buffer_to_screen(pic_buffer);
						}
						break;

					case 'C':
						if (!isNetworkGame)
						{
							fade_black(10);
						}
						JE_clr256(VGAScreen);
						JE_showVGA();
						memcpy(colors, palettes[7], sizeof(colors));
						set_palette(colors, 0, 255);
						break;

					case 'B':
						if (!isNetworkGame)
						{
							fade_black(10);
						}
						break;
					case 'F':
						if (!isNetworkGame)
						{
							fade_white(100);
							fade_black(30);
						}
						JE_clr256(VGAScreen);
						JE_showVGA();
						break;

					case 'W':
						if (!constantPlay)
						{
							if (!ESCPressed)
							{
								warningCol = 14 * 16 + 5;
								warningColChange = 1;
								warningSoundDelay = 0;
								levelWarningDisplay = (s[2] == 'y');
								levelWarningLines = 0;
								frameCountMax = atoi(s + 4);
								setDelay2(6);
								warningRed = frameCountMax / 10;
								frameCountMax = frameCountMax % 10;

								do
								{
									read_encrypted_pascal_string(s, sizeof(s), ep_f);

									if (s[0] != '#')
									{
										strcpy(levelWarningText[levelWarningLines], s);
										levelWarningLines++;
									}
								} while (!(s[0] == '#'));

								JE_displayText();
								newkey = false;
							}
						}
						break;

					case 'H':
						if (initialDifficulty < DIFFICULTY_HARD)
						{
							mainLevel = atoi(s + 4);
							jumpSection = true;
						}
						break;

					case 'h':
						if (initialDifficulty > DIFFICULTY_NORMAL)
						{
							read_encrypted_pascal_string(s, sizeof(s), ep_f);
						}
						break;

					case 'n':
						ESCPressed = false;
						break;

					case 'M':
						temp = atoi(s + 3);
						play_song(temp - 1);
						break;

					case 'T':
						if (timedBattleMode)
						{
							// ]T[ 43 44 45 46 47; Episode 1
							// ]T[ 03 03 04 05 06; Episode 5
							mainLevel = atoi(s + (timeBattleSelection * 3));
							jumpSection = true;
						}
						break;

					case 'q':
						if (timedBattleMode)
						{
							if (isNetworkGame)
								JE_timedBattleResult();
							else
								JE_highScoreCheck();
							mainLevel = 0;
							close_episode_file();
							return;
						}
						break;
					}
				}

			} while (!(loadLevelOk || jumpSection));

			close_episode_file();

		} while (!loadLevelOk);
	}

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: level script done\n");
		fflush(stderr);
	}

	if (play_demo)
		load_next_demo();
	else
		fade_black(50);

	/* Return the display to the normal gameplay offset after the fade */
	set_menu_centered(false);

	// A nonzero file number selects a later ]L cut within the same section.
	if (forcedLvlFileNum != 0)
	{
		if (JE_levelFileNumValid(forcedLvlFileNum))
			lvlFileNum = forcedLvlFileNum;
		else
			fprintf(stderr, "warning: ignoring missing level file %u in episode %u\n",
			        (unsigned int)forcedLvlFileNum, (unsigned int)episodeNum);
		forcedLvlFileNum = 0;
	}

	if (!JE_levelFileNumValid(lvlFileNum))
	{
		fprintf(stderr, "error: episode %u references missing level file %u (available: 1-%u)\n",
		        (unsigned int)episodeNum, (unsigned int)lvlFileNum, (unsigned int)(lvlNum / 2));
		JE_tyrianHalt(1);
	}

	FILE* level_f = dir_fopen_die(JE_episodeDir(), levelFile, "rb");
	if (fseek(level_f, lvlPos[(lvlFileNum - 1) * 2], SEEK_SET) != 0)
	{
		fprintf(stderr, "error: failed to seek to episode %u level file %u\n",
		        (unsigned int)episodeNum, (unsigned int)lvlFileNum);
		fclose(level_f);
		JE_tyrianHalt(1);
	}

	JE_char char_mapFile;
	JE_char char_shapeFile;
	fread_die(&char_mapFile,   1, 1, level_f);
	fread_die(&char_shapeFile, 1, 1, level_f);
	fread_u16_die(&mapX,  1, level_f);
	fread_u16_die(&mapX2, 1, level_f);
	fread_u16_die(&mapX3, 1, level_f);

	fread_u16_die(&levelEnemyMax, 1, level_f);
	if (levelEnemyMax > COUNTOF(levelEnemy))
	{
		fprintf(stderr, "error: episode %u level file %u has too many random enemies (%u)\n",
		        (unsigned int)episodeNum, (unsigned int)lvlFileNum, (unsigned int)levelEnemyMax);
		fclose(level_f);
		JE_tyrianHalt(1);
	}
	fread_u16_die(levelEnemy, levelEnemyMax, level_f);

	if (!level_read_event_script(level_f))
	{
		fprintf(stderr, "error: episode %u level file %u has too many events (%u)\n",
		        (unsigned int)episodeNum, (unsigned int)lvlFileNum, (unsigned int)maxEvent);
		fclose(level_f);
		JE_tyrianHalt(1);
	}

	/* Map shape lookup table; each map follows its level data. */
	for (temp = 0; temp < 3; temp++)
	{
		fread_u16_die(mapSh[temp], sizeof(*mapSh) / sizeof(JE_word), level_f);
		for (temp2 = 0; temp2 < 128; temp2++)
		{
			mapSh[temp][temp2] = SDL_Swap16(mapSh[temp][temp2]);
		}
	}

	/* Read Shapes.DAT */
	sprintf(tempStr, "shapes%c.dat", tolower((unsigned char)char_shapeFile));
	FILE *shpFile = dir_fopen_die(data_dir(), tempStr, "rb");

	for (int z = 0; z < 600; z++)
	{
		JE_boolean shapeBlank;
		fread_bool_die(&shapeBlank, shpFile);

		if (shapeBlank)
			memset(shape, 0, sizeof(shape));
		else
			fread_u8_die(shape, sizeof(shape), shpFile);

		/* Match 1 */
		for (int x = 0; x <= 71; ++x)
		{
			if (mapSh[0][x] == z+1)
			{
				memcpy(megaData1.shapes[x].sh, shape, sizeof(JE_DanCShape));

				ref[0][x] = megaData1.shapes[x].sh;
			}
		}

		/* Match 2 */
		for (int x = 0; x <= 71; ++x)
		{
			if (mapSh[1][x] == z+1)
			{
				if (x != 71 && !shapeBlank)
				{
					memcpy(megaData2.shapes[x].sh, shape, sizeof(JE_DanCShape));

					y = 1;
					for (yy = 0; yy < (24 * 28) >> 1; yy++)
						if (shape[yy] == 0)
							y = 0;

					megaData2.shapes[x].fill = y;
					ref[1][x] = megaData2.shapes[x].sh;
				}
				else
				{
					ref[1][x] = NULL;
				}
			}
		}

		/*Match 3*/
		for (int x = 0; x <= 71; ++x)
		{
			if (mapSh[2][x] == z+1)
			{
				if (x < 70 && !shapeBlank)
				{
					memcpy(megaData3.shapes[x].sh, shape, sizeof(JE_DanCShape));

					y = 1;
					for (yy = 0; yy < (24 * 28) >> 1; yy++)
						if (shape[yy] == 0)
							y = 0;

					megaData3.shapes[x].fill = y;
					ref[2][x] = megaData3.shapes[x].sh;
				}
				else
				{
					ref[2][x] = NULL;
				}
			}
		}
	}

	fclose(shpFile);

	fread_u8_die(mapBuf, 14 * 300, level_f);
	bufLoc = 0;              /* Map 1. */
	for (y = 0; y < 300; y++)
	{
		for (x = 0; x < 14; x++)
		{
			megaData1.mainmap[y][x] = ref[0][mapBuf[bufLoc]];
			bufLoc++;
		}
	}

	fread_u8_die(mapBuf, 14 * 600, level_f);
	bufLoc = 0;              /* Map 2. */
	for (y = 0; y < 600; y++)
	{
		for (x = 0; x < 14; x++)
		{
			megaData2.mainmap[y][x] = ref[1][mapBuf[bufLoc]];
			bufLoc++;
		}
	}

	fread_u8_die(mapBuf, 15 * 600, level_f);
	bufLoc = 0;              /* Map 3. */
	for (y = 0; y < 600; y++)
	{
		for (x = 0; x < 15; x++)
		{
			megaData3.mainmap[y][x] = ref[2][mapBuf[bufLoc]];
			bufLoc++;
		}
	}

	fclose(level_f);

	/* Map shape pointers use (MAPSH - 1) * 168 plus S2Ofs. */
	/* End of find loop for LEVEL??.DAT */
}

static void JE_displayText(void)
{
	/* Display Warning Text */
	JE_word tempY = 55;
	if (warningRed)
	{
		tempY = 2;
	}
	for (temp = 0; temp < levelWarningLines; temp++)
	{
		if (!ESCPressed)
		{
			JE_outCharGlow(10, tempY, levelWarningText[temp]);

			if (haltGame)
			{
				JE_tyrianHalt(5);
			}

			tempY += 10;
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
	textGlowFont = TINY_FONT;
	tempW = 184;
	if (warningRed)
		tempW = 7 * 16 + 6;

	JE_outCharGlow(JE_fontCenter(miscText[4], TINY_FONT), tempW, miscText[4]);

	do
	{
		// A gameplay wire test has no player to press past the briefing.
		if (qa_net_gameplay_ticks > 0)
			break;

		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
		touch_ui_idle_repaint();
		if (levelWarningDisplay)
			JE_updateWarning(VGAScreen);

		setDelay(1);

		NETWORK_KEEP_ALIVE();

		wait_delay();

	} while (!(JE_anyButton() || (frameCountMax == 0 && temp == 1) || ESCPressed));
	levelWarningDisplay = false;
}
