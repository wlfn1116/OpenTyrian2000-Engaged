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
#include "lvllib.h"

#include "file.h"
#include "opentyr.h"
#include "varz.h"

JE_LvlPosType lvlPos;

char levelFile[13]; /* string [12] */
JE_word lvlNum;

void JE_analyzeLevel(void)
{
	FILE *f = dir_fopen_die(data_dir(), levelFile, "rb");
	
	fread_u16_die(&lvlNum, 1, f);
	if (lvlNum < 3 || lvlNum >= COUNTOF(lvlPos) || lvlNum % 2 == 0)
	{
		fprintf(stderr, "error: '%s' has an invalid level-offset count (%u)\n",
		        levelFile, (unsigned int)lvlNum);
		fclose(f);
		JE_tyrianHalt(1);
		return;
	}

	fread_s32_die(lvlPos, lvlNum, f);
	
	lvlPos[lvlNum] = ftell_eof(f);

	fclose(f);
}

unsigned int JE_levelFileCount(int episode)
{
	if (episode < 1 || episode > 9)
		return 0;

	char filename[13];
	snprintf(filename, sizeof(filename), "tyrian%d.lvl", episode);
	FILE *f = dir_fopen_warn(data_dir(), filename, "rb");
	if (f == NULL)
		return 0;

	Uint16 offsetCount;
	const bool readOk = fread(&offsetCount, sizeof(offsetCount), 1, f) == 1;
	fclose(f);
	if (!readOk)
		return 0;

	/* Each playable level owns two offsets (level data, then map data), and
	 * the final offset is the end-of-file sentinel. */
	offsetCount = SDL_SwapLE16(offsetCount);
	if (offsetCount < 3 || offsetCount >= COUNTOF(lvlPos) || offsetCount % 2 == 0)
		return 0;
	return offsetCount / 2;
}

bool JE_levelFileNumValid(JE_word fileNum)
{
	return fileNum >= 1 && fileNum <= lvlNum / 2;
}
