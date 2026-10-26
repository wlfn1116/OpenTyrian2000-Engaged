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
#include "demo.h"

#include "config.h"
#include "episodes.h"
#include "file.h"
#include "game_loop.h"
#include "game_state.h"
#include "keyboard.h"
#include "opentyr.h"
#include "player.h"
#include "player_ship.h"

#include <stdio.h>

/* Demo Stuff */
bool play_demo = false, record_demo = false, stopped_demo = false;
Uint8 demo_num = 0;
static FILE *demo_file = NULL;

static Uint8 demo_keys;
static Uint16 demo_keys_wait;

static Uint8 *const demo_header_fields[] =
{
	&lvlFileNum,
	&player[0].items.weapon[FRONT_WEAPON].id,
	&player[0].items.weapon[REAR_WEAPON].id,
	&player[0].items.super_arcade_mode,
	&player[0].items.sidekick[LEFT_SIDEKICK],
	&player[0].items.sidekick[RIGHT_SIDEKICK],
	&player[0].items.generator,
	&player[0].items.sidekick_level,   // could probably ignore
	&player[0].items.sidekick_series,  // could probably ignore
	&initial_episode_num,              // could probably ignore
	&player[0].items.shield,
	&player[0].items.special,
	&player[0].items.ship,
	&player[0].items.weapon[0].power,
	&player[0].items.weapon[1].power,
};

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

	for (size_t i = 0; i < COUNTOF(demo_header_fields); ++i)
		fread_u8_die(demo_header_fields[i], 1, demo_file);

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

void begin_demo_recording(void)
{
	Uint8 new_demo_num = 0;

	do
	{
		sprintf(tempStr, "demorec.%d", new_demo_num++);
	} while (dir_file_exists(get_user_directory(), tempStr)); // until file doesn't exist

	demo_file = dir_fopen_warn(get_user_directory(), tempStr, "wb");
	if (!demo_file)
		JE_tyrianExitWithoutSaving(1);

	fwrite_u8_die(&episodeNum, 1, demo_file);

	// Pad string buffer with NULs.
	for (size_t i = 1; i < 10; ++i)
		if (levelName[i - 1] == '\0')
			levelName[i] = '\0';
	fwrite_u8_die((Uint8 *)levelName, 10, demo_file);

	for (size_t i = 0; i < COUNTOF(demo_header_fields); ++i)
		fwrite_u8_die(demo_header_fields[i], 1, demo_file);

	Uint8 unused[3] = { 0, 0, 0 };
	fwrite_u8_die(unused, 3, demo_file);

	fwrite_u8_die(&levelSong, 1, demo_file);

	demo_keys = 0;
	demo_keys_wait = 0;
}

void record_demo_keys(void)
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

void close_demo_file(void)
{
	if (demo_file != NULL)
	{
		fclose(demo_file);
		demo_file = NULL;
	}
}
