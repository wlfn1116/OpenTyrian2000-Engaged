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
#include "opentyr.h"

#include "audio.h"
#include "config.h"
#include "crashlog.h"
#include "custom_episode.h"
#include "debug_flags.h"
#include "demo.h"
#include "destruct.h"
#include "endless.h"
#include "episodes.h"
#include "extra_ships.h"
#include "file.h"
#include "game_loop.h"
#include "game_menu.h"
#include "game_state.h"
#include "helptext.h"
#include "high_scores.h"
#include "joystick.h"
#include "keyboard.h"
#include "level_load.h"
#include "mtrand.h"
#include "net_start.h"
#include "network.h"
#include "net_rollback.h"
#include "opentyrian_version.h"
#include "palette.h"
#include "params.h"
#include "player.h"
#include "qa.h"
#include "render_list.h"
#include "rollback.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "console_platform.h"
#include "title.h"
#include "video.h"
#include "weapon_editor.h"
#include "xmas.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Every other toolchain is built with -fsigned-char. MSVC has no equivalent switch, so the
// assumption is checked here instead of being left to a target's default.
typedef char assert_char_is_signed[(char)-1 < 0 ? 1 : -1];

const char *opentyrian_str = "OpenTyrian 2000 Engaged";
const char *opentyrian_version = OPENTYRIAN_VERSION;
const char *opentyrian_commit = OPENTYRIAN_COMMIT;

static void JE_tyrianShutdown(bool saveConfiguration)
{
	network_shutdown();
	deinit_audio();
	destruct_deinit();
	game_loop_deinit();
	game_menu_deinit();
	weaponEditorRelease();
	rl_deinit();
	deinit_video();
	deinit_joysticks();

	free_main_shape_tables();
	free_enemy_sprite_sheets();
	free_sprite2s(&shopSpriteSheet);
	free_sprite2s(&explosionSpriteSheet);

	for (int i = 0; i < SOUND_COUNT; ++i)
	{
		free(soundSamples[i]);
		soundSamples[i] = NULL;
	}

	close_demo_file();
	close_episode_file();

	if (saveConfiguration)
		JE_saveConfiguration();

	music_deinit();
	JE_freeExtraShapes();
	rollback_deinit();
	config_deinit(&opentyrian_config);
	network_deinit();
}

static void JE_tyrianQuitSDL(void)
{
	SDL_Quit();
#if SDL_VERSION_ATLEAST(2, 0, 16)
	SDL_TLSCleanup();
#endif
}

static int JE_tyrianFinish(int result)
{
	JE_tyrianShutdown(false);
	JE_tyrianQuitSDL();
	return result;
}

static OT_NORETURN void JE_tyrianExitProcess(int code)
{
	JE_tyrianQuitSDL();

#ifdef __SWITCH__
	// Switch: libc exit()'s atexit/stdio teardown NULL-derefs in newlib once romfs is
	// gone; everything is already flushed, so _Exit and skip it. That skips atexit too,
	// so the console services are released here.
	switch_platform_exit();
	_Exit(code);
#else
	exit(code);
#endif
}

void JE_tyrianExitWithoutSaving(int code)
{
	JE_tyrianShutdown(false);
	JE_tyrianExitProcess(code);
}

void JE_tyrianHalt(JE_byte code)
{
	// Code 1 is an unrecoverable data error. Write its report before exit()
	// bypasses the crash handlers; duplicate reports are ignored.
	if (code == 1)
		crashlog_report_fatal("FATAL (JE_tyrianHalt error exit)",
		                      "JE_tyrianHalt(1) -- unrecoverable data/level error; see phase + stack");

	JE_tyrianShutdown(code != 9);

	/* endkeyboard; */

	if (code == 9)
	{
	}

	if (code == 5)
	{
		code = 0;
	}

	if (trentWin)
	{
		printf("\n"
		       "\n"
		       "\n"
		       "\n"
		       "Sleep well, Trent, you deserve the rest.\n"
		       "You now have permission to borrow my ship on your next mission.\n"
		       "\n"
		       "Also, you might want to try out the YESXMAS parameter in Dos.\n"
		       "  Type: File0001 YESXMAS\n"
		       "\n"
		       " Press a Key to Quit\n"
		       "\n");
	}

	JE_tyrianExitProcess(code);
}

#ifdef _MSC_VER
// Some compilers do not infer that JE_tyrianHalt exits; suppress C4702 around main's end.
#pragma warning(push)
#pragma warning(disable: 4702)
#endif
int main(int argc, char *argv[])
{
#ifdef PLATFORM_HANDHELD
	// Mount data and prepare the user directory before file access.
	console_platform_init();
#endif

	// Crash logs sit beside the executable.
	install_crash_handler();
	watchdog_init();          // ...and on a hard main-thread hang (infinite loop), which throws no exception

	mt_srand(time(NULL));

	// opentyrian_version already includes "Engaged", so pair it with the base game name.
	printf("\nWelcome to... >> OpenTyrian 2000 %s <<\n\n", opentyrian_version);

	printf("Copyright (C) 2022 The OpenTyrian Development Team\n");
	printf("Copyright (C) 2022 Kaito Sinclaire\n");
	printf("Copyright (C) 2026 wlfn\n\n");

	printf("This program comes with ABSOLUTELY NO WARRANTY.\n");
	printf("This is free software, and you are welcome to redistribute it\n");
	printf("under certain conditions.  See the file COPYING for details.\n\n");

	if (SDL_Init(0))
	{
		printf("Failed to initialize SDL: %s\n", SDL_GetError());
		return -1;
	}

	// Load help text first because it supplies the default high-score names.

	JE_paramCheck(argc, argv);

	if (!override_xmas) // arg handler may override
		xmas = xmas_time();

	JE_loadHelpText();

	/* The debug entries in the buy/sell menu are inserted at runtime by
	 * JE_itemScreen, only when Debug Mode is enabled; off keeps the stock layout. */

	JE_loadConfiguration();

	// Command-line Christmas mode beats the saved choice, which beats date detection.
	if (!override_xmas && xmasMode >= 0)
	{
		xmas = (xmasMode != 0);
		override_xmas = true;
	}

	JE_scanForEpisodes();
	customEpisodeScan();   // Migrate loose containers and build the index.

	init_video();
	init_keyboard();
	init_joysticks();
	printf("assuming mouse detected\n"); // SDL can't tell us if there isn't one

	if (xmas && (!dir_file_exists(data_dir(), "tyrianc.shp") || !dir_file_exists(data_dir(), "voicesc.snd")))
	{
		xmas = false;

		fprintf(stderr, "warning: Christmas is missing.\n");
	}

	JE_loadPals();
	JE_loadMainShapeTables(xmas ? "tyrianc.shp" : "tyrian.shp");

	if (xmas && !override_xmas)
	{
		// xmas_prompt() draws across the full vga_width buffer (like jukebox()
		// and JE_destructGame()), so it needs the menu-centering pillarbox off.
		set_menu_centered(false);
		bool xmas_accepted = xmas_prompt();
		set_menu_centered(true);

		if (!xmas_accepted)
		{
			xmas = false;

			free_main_shape_tables();
			JE_loadMainShapeTables("tyrian.shp");
		}
	}

	/* Default Options */
	youAreCheating = false;
	smoothScroll = true;
	loadDestruct = false;

	if (!audio_disabled)
	{
		printf("initializing SDL audio...\n");

		init_audio();

		load_music();

		loadSndFile(xmas);
	}
	else
	{
		printf("audio disabled\n");
	}

	if (record_demo)
		printf("demo recording enabled (input limited to keyboard)\n");

	JE_loadExtraShapes();  /*Editship*/

	if (qa_test_suite)
	{
		return JE_tyrianFinish(qa_run_unit_suite());
	}

	if (qa_xfer_send != NULL || qa_xfer_recv != NULL)
	{
		return JE_tyrianFinish(qa_run_xfer());
	}

	if (qa_replay_demo != 0)
	{
		return JE_tyrianFinish(qa_run_replay_fixture());
	}

	if (qa_destruct_selftest_ticks > 0)
	{
		return JE_tyrianFinish(qa_run_destruct_selftest());
	}

	if (qa_net_rounds > 0)
	{
		/* Exercise the production lobby roles: player 1 listens, player 2 joins. */
		network_from_lobby = true;
		network_is_host = thisPlayerNum == 1;
		networkHostPlayerNum = 1;
	}

	/* The harness names non-Arcade wire modes directly, so both peers receive the same fixed
	 * settings here. */
	if (qa_net_gameplay_ticks > 0 && qa_net_game_type >= 0
	    && qa_net_game_type < NETWORK_GAME_TYPE_COUNT)
	{
		network_game_type = (NetworkGameType)qa_net_game_type;
		// Pin SuperTyrian's variant because it has no difficulty ladder.
		if (network_game_type == NETWORK_GAME_SUPERTYRIAN)
			network_host_difficulty = qa_net_scrollock ? DIFFICULTY_SUICIDE : DIFFICULTY_LORD_OF_GAME;
		if (network_game_type == NETWORK_GAME_ENDLESS)
		{
			network_host_endless_run_mode = (int)ENDLESS_RUNMODE_STANDARD;
			network_host_endless_chooser = (int)ENDLESS_PICK_HOST;
			network_host_endless_combo_shared = true;
			network_host_endless_base_rule = (int)ENDLESS_BASE_VARIED;
			SDL_strlcpy(network_endless_session_seed, "qa-wire-zones",
			            sizeof(network_endless_session_seed));
		}
	}

	/* The linked gameplay scenarios must not inherit a saved Separate preference. Scenario 19 also
	 * exercises the legacy Delay-Based state stream, set before the connect block is packed. */
	if (qa_net_gameplay_ticks > 0 && (qa_net_scenario == 5 || qa_net_scenario == 19))
		arcadeSeparateShips = false;
	if (qa_net_gameplay_ticks > 0 && qa_net_scenario == 19)
		net_rollback = false;
	if (qa_net_gameplay_ticks > 0 && qa_net_scenario == 20)
	{
		network_game_type = NETWORK_GAME_ARCADE;
		network_host_timed_battle = true;
		network_host_battle_level = 1;
	}

	/* Multi-zone runs must not lose a ship to the scripted wiggle: a death reroutes the run into
	 * the death menus, which these scenarios do not model. */
	if (qa_net_gameplay_ticks > 0
	    && (qa_net_zones > 0 || network_game_type_is_super(network_game_type)))
		cheatInfiniteArmor = true;

	/* Start the joiner with opposite values so the settings packet must replace them. */
	if (qa_net_gameplay_ticks > 0 && qa_net_lobby_settings)
	{
		network_from_lobby = true;
		if (thisPlayerNum == 1)
		{
			coopSharedCredit = false;
			coopDoubleEarnings = true;
		}
		else
		{
			coopSharedCredit = true;
			coopDoubleEarnings = false;
		}
	}

	/* The Separate-arcade scenario. Command-line peers adopt nothing, so both arm the host's
	 * ships setting from their own config, exactly as the two sides of a real lobby end up. */
	if (qa_net_gameplay_ticks > 0 && qa_net_arcade_separate)
		arcadeSeparateShips = true;

	/* Command-line peers have no lobby to assign roles. Give player 1 the host
	 * role expected by menu arbitration and desync recovery. */
	if (isNetworkGame)
	{
		network_is_host = thisPlayerNum == 1;
		networkHostPlayerNum = 1;
	}

	/* Mirror the lobby's custom-content settings for wire tests. */
	if (isNetworkGame && qa_net_custom_endless >= 0 &&
	    qa_net_custom_endless < CUSTOM_ENDLESS_MODES)
		customEndlessMode = qa_net_custom_endless;
	if (isNetworkGame && qa_net_custom_episode != NULL && network_is_host)
	{
		customEpisodeScan();
		const int idx = customEpisodeFindByFile(qa_net_custom_episode);
		Uint32 size, hash;
		if (idx < 0 || !customEpisodeIdentity(idx, &size, &hash))
		{
			fprintf(stderr, "network test: custom episode '%s' is not installed here\n",
			        qa_net_custom_episode);
			JE_tyrianExitWithoutSaving(1);
		}
		SDL_strlcpy(network_host_custom_file, customEpisodeFile(idx),
		            sizeof(network_host_custom_file));
		network_host_custom_size = size;
		network_host_custom_hash = hash;
		network_host_episode = customEpisodeBase(idx);
	}

	if (isNetworkGame)
	{
		if (network_init())
		{
			network_tyrian_halt(3, false);
		}
	}

	if (qa_net_rounds > 0)
	{
		return JE_tyrianFinish(network_test_peer(qa_net_rounds, qa_net_scenario));
	}

#ifdef NDEBUG
	if (!isNetworkGame)
		intro_logos();
#endif

	for (; ; )
	{
		// Mid-game network teardown returns here after cleaning the session.
		setjmp(network_bailout_env);
		network_bailout_armed = true;

		// Teardown bypasses the level exit, so clear rollback mode flags here.
		rollback_level_end();

		crashlog_set_phase("title / main menu");

		// Restore local settings after closing a completed lobby session.
		if (isNetworkGame && network_from_lobby && !qa_net_lobby_run())
		{
			network_shutdown();

			isNetworkGame = false;
			network_from_lobby = false;
			network_is_host = false;
			twoPlayerMode = false;
			arcadeSeparateMode = false;
		}

		JE_initPlayerData();
		JE_sortHighScores();

		play_demo = false;
		stopped_demo = false;

		gameLoaded = false;
		jumpSection = false;

		// A command-line network game has no title screen: it connects straight away, every
		// time round the loop.  A lobby game reaches the same handshake from the menu below.
		if (isNetworkGame && (!network_from_lobby || qa_net_lobby_run()))
		{
			networkStartScreen();
		}
		else
		{
			if (!titleScreen())
			{
				// Player quit from title screen.
				break;
			}
		}

		if (loadDestruct)
		{
			JE_destructGame();

			loadDestruct = false;
		}
		else
		{
			set_menu_centered(false);
			JE_main();
			set_menu_centered(true);

			/* The headless terminal-screen regression has reached its real completion
			 * condition. It has no title-screen driver, so end the two test processes here. */
			if (qa_net_scenario == 20 && qa_net_gameplay_ticks > 0 && timedBattleMode
			    && mainLevel == 0)
				JE_tyrianExitWithoutSaving(0);

			if (trentWin)
			{
				// Player beat SuperTyrian.
				break;
			}
		}
	}

	JE_tyrianHalt(0);

	return 0;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
