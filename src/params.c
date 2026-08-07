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
#include "params.h"

#include "arg_parse.h"
#include "file.h"
#include "joystick.h"
#include "loudness.h"
#include "network.h"
#include "opentyr.h"
#include "qa.h"
#include "varz.h"
#include "xmas.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

JE_boolean richMode = false, constantPlay = false, constantDie = false;

/* YKS: Note: LOOT cheat had non letters removed. */
const char pars[][9] = {
	"LOOT", "RECORD", "NOJOY", "CONSTANT", "DEATH", "NOSOUND", "NOXMAS", "YESXMAS"
};

void JE_paramCheck(int argc, char *argv[])
{
	const Options options[] =
	{
		{ 'h', 'h', "help",              false },
		
		{ 's', 's', "no-sound",          false },
		{ 'j', 'j', "no-joystick",       false },
		{ 'x', 'x', "no-xmas",           false },
		
		{ 't', 't', "data",              true },
		
		{ 'n', 'n', "net",               true },
		{ 256, 0,   "net-player-name",   true },
		{ 257, 0,   "net-player-number", true },
		{ 'p', 'p', "net-port",          true },
		{ 'd', 'd', "net-delay",         true },
		
		{ 'X', 'X', "xmas",              false },
		{ 'c', 'c', "constant",          false },
		{ 'k', 'k', "death",             false },
		{ 'r', 'r', "record",            false },
		{ 'l', 'l', "loot",              false },

		{ 300, 0,   "test-suite",        false },
		{ 301, 0,   "test-fixtures",     true },
		{ 302, 0,   "test-replay",       true },
		{ 303, 0,   "test-replay-ticks", true },
		{ 304, 0,   "test-replay-hash",  true },
		{ 305, 0,   "test-net-rounds",   true },
		{ 306, 0,   "test-net-scenario", true },
		{ 307, 0,   "test-net-version-skew", true },
		{ 308, 0,   "test-net-gameplay-ticks", true },
		{ 309, 0,   "test-net-corrupt-frame", true },
		{ 310, 0,   "test-net-save-exit", false },
		{ 311, 0,   "test-net-resume-slot", true },
		{ 312, 0,   "test-net-loadout", true },
		{ 313, 0,   "test-net-menu-frame", true },
		{ 314, 0,   "test-net-game-type", true },
		{ 315, 0,   "test-net-zones", true },
		{ 316, 0,   "test-net-lobby-settings", false },

		{ 0, 0, NULL, false}
	};
	
	Option option;
	
	for (; ; )
	{
		option = parse_args(argc, (const char **)argv, options);
		
		if (option.value == NOT_OPTION)
			break;
		
		switch (option.value)
		{
		case INVALID_OPTION:
		case AMBIGUOUS_OPTION:
		case OPTION_MISSING_ARG:
			fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
			exit(EXIT_FAILURE);
			break;
			
		case 'h':
			printf("Usage: %s [OPTION...]\n\n"
			       "Options:\n"
			       "  -h, --help                   Show help about options\n\n"
			       "  -s, --no-sound               Disable audio\n"
			       "  -j, --no-joystick            Disable joystick/gamepad input\n"
			       "  -x, --no-xmas                Disable Christmas mode\n\n"
			       "  -t, --data=DIR               Set Tyrian data directory\n\n"
			       "  -n, --net=HOST[:PORT]        Start a networked game\n"
			       "  --net-player-name=NAME       Sets local player name in a networked game\n"
			       "  --net-player-number=NUMBER   Sets local player number in a networked game\n"
			       "                               (1 or 2)\n"
			       "  -p, --net-port=PORT          Local port to bind (default is 1333)\n"
			       "  -d, --net-delay=FRAMES       Set lag-compensation delay (default is 1)\n", argv[0]);
			exit(0);
			break;
			
		case 's':
			// Disables sound/music usage
			audio_disabled = true;
			break;
			
		case 'j':
			// Disables joystick detection
			ignore_joystick = true;
			break;
			
		case 'x':
			override_xmas = true;
			xmas = false;
			break;
			
		// set custom Tyrian data directory
		case 't':
			custom_data_dir = option.arg;
			break;
			
		case 'n':
			isNetworkGame = true;
			
			intptr_t temp = (intptr_t)strchr(option.arg, ':');
			if (temp)
			{
				temp -= (intptr_t)option.arg;
				
				int temp_port = atoi(&option.arg[temp + 1]);
				if (temp_port > 0 && temp_port < 49152)
					network_opponent_port = temp_port;
				else
				{
					fprintf(stderr, "%s: error: invalid network port number\n", argv[0]);
					exit(EXIT_FAILURE);
				}
				
				network_opponent_host = malloc(temp + 1);
				SDL_strlcpy(network_opponent_host, option.arg, temp + 1);
			}
			else
			{
				network_opponent_host = malloc(strlen(option.arg) + 1);
				strcpy(network_opponent_host, option.arg);
			}
			break;
			
		case 256: // --net-player-name
			network_set_player_name(option.arg);
			break;
			
		case 257: // --net-player-number
		{
			int temp = atoi(option.arg);
			if (temp >= 1 && temp <= 2)
				thisPlayerNum = temp;
			else
			{
				fprintf(stderr, "%s: error: invalid network player number\n", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'p':
		{
			int temp = atoi(option.arg);
			if (temp > 0 && temp < 49152)
				network_player_port = temp;
			else
			{
				fprintf(stderr, "%s: error: invalid network port number\n", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'd':
		{
			int temp;
			if (sscanf(option.arg, "%d", &temp) == 1)
				network_delay = 1 + temp;
			else
			{
				fprintf(stderr, "%s: error: invalid network delay value\n", argv[0]);
				exit(EXIT_FAILURE);
			}
			break;
		}
		case 'X':
			override_xmas = true;
			xmas = true;
			break;
			
		case 'c':
			/* Constant play for testing purposes (C key activates invincibility)
			   This might be useful for publishers to see everything - especially
			   those who can't play it */
			constantPlay = true;
			break;
			
		case 'k':
			constantDie = true;
			break;
			
		case 'r':
			record_demo = true;
			break;
			
		case 'l':
			// Enable rich mode.
			richMode = true;
			break;

		case 300:
			qa_test_suite = true;
			break;
		case 301:
			qa_fixture_dir = option.arg;
			break;
		case 302:
			qa_replay_demo = atoi(option.arg);
			break;
		case 303:
			qa_replay_ticks = strtoul(option.arg, NULL, 10);
			break;
		case 304:
			qa_replay_expect = (Uint32)strtoul(option.arg, NULL, 16);
			qa_replay_expect_set = true;
			break;
		case 305:
			qa_net_rounds = atoi(option.arg);
			break;
		case 306:
			qa_net_scenario = atoi(option.arg);
			break;
		case 307:
			qa_net_version_skew = atoi(option.arg);
			break;
		case 308:
			qa_net_gameplay_ticks = strtoul(option.arg, NULL, 10);
			break;
		case 309:
			qa_net_corrupt_frame = strtoul(option.arg, NULL, 10);
			break;
		case 310:
			qa_net_save_exit = true;
			break;
		case 311:
			qa_net_resume_slot = atoi(option.arg);
			break;
		case 312:
			qa_net_loadout = atoi(option.arg);
			break;
		case 313:
			qa_net_menu_frame = strtoul(option.arg, NULL, 10);
			break;
		case 314:
			qa_net_game_type = atoi(option.arg);
			break;
		case 315:
			qa_net_zones = atoi(option.arg);
			break;
		case 316:
			qa_net_lobby_settings = true;
			break;

		default:
			assert(false);
			break;
		}
	}
	
	// legacy parameter support
	for (int i = option.argn; i < argc; ++i)
	{
		for (uint j = 0; j < strlen(argv[i]); ++j)
			argv[i][j] = toupper((unsigned char)argv[i][j]);
		
		for (uint j = 0; j < COUNTOF(pars); ++j)
		{
			if (strcmp(argv[i], pars[j]) == 0)
			{
				switch (j)
				{
				case 0:
					richMode = true;
					break;
				case 1:
					record_demo = true;
					break;
				case 2:
					ignore_joystick = true;
					break;
				case 3:
					constantPlay = true;
					break;
				case 4:
					constantDie = true;
					break;
				case 5:
					audio_disabled = true;
					break;
				case 6:
					override_xmas = true;
					xmas = false;
					break;
				case 7:
					override_xmas = true;
					xmas = true;
					break;
					
				default:
					assert(false);
					break;
				}
			}
		}
	}
}
