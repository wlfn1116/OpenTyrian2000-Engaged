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
#include "lvllib.h"

#include "custom_episode.h"
#include "episodes.h"
#include "file.h"
#include "helptext.h"
#include "opentyr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

JE_LvlPosType lvlPos;

char levelFile[13]; /* string [12] */
JE_word lvlNum;

void JE_analyzeLevel(void)
{
	FILE *f = dir_fopen_die(JE_episodeDir(), levelFile, "rb");
	
	fread_u16_die(&lvlNum, 1, f);
	if (lvlNum < 3 || lvlNum >= COUNTOF(lvlPos) || lvlNum % 2 == 0)
	{
		fprintf(stderr, "error: '%s' has an invalid level-offset count (%u)\n",
		        levelFile, (unsigned int)lvlNum);
		fclose(f);
		JE_tyrianHalt(1);
	}

	fread_s32_die(lvlPos, lvlNum, f);
	
	lvlPos[lvlNum] = ftell_eof(f);

	fclose(f);
}

unsigned int JE_levelFileCount(int episode)
{
	// Count an extended ID without activating its container.
	if (episode >= CUSTOM_EPISODE_ID_BASE)
		return customEpisodeLevelCount(customEpisodeIdToLocal(episode - CUSTOM_EPISODE_ID_BASE));

	if (episode < 1 || episode > 9)
		return 0;

	FILE *f;
	if (customEpisodeActive() && episode == episodeNum)
	{
		// The active container owns this base episode's levels.
		f = dir_fopen_warn(custom_episode_dir(), CUSTOM_EP_LVL_NAME, "rb");
	}
	else
	{
		char filename[13];
		snprintf(filename, sizeof(filename), "tyrian%d.lvl", episode);
		f = dir_fopen_warn(data_dir(), filename, "rb");
	}
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

static FILE *open_level_script(int episode, long *end)
{
	FILE *f;
	*end = 0;
	if (episode >= CUSTOM_EPISODE_ID_BASE)
	{
		// Extended IDs can read an inactive container's script.
		f = customEpisodeOpenScript(customEpisodeIdToLocal(episode - CUSTOM_EPISODE_ID_BASE), end);
	}
	else if (customEpisodeActive() && episode == episodeNum)
	{
		// The active container owns this base episode's script.
		f = dir_fopen_warn(custom_episode_dir(), CUSTOM_EP_SCRIPT_NAME, "rb");
	}
	else
	{
		char fname[16];
		snprintf(fname, sizeof(fname), "levels%d.dat", episode);
		f = dir_fopen_warn(data_dir(), fname, "rb");
	}
	if (f != NULL && *end == 0)
		*end = ftell_eof(f);
	return f;
}

// The name is a fixed 9-char field (offset 13), space-padded, with the file number following at
// offset 25. Copy exactly nine characters, not the full tail.
static void level_entry_name(const char *entry, char *name, size_t size)
{
	SDL_strlcpy(name, entry + 13, size);
	size_t len = strlen(name);
	while (len > 0 && name[len - 1] == ' ')
		name[--len] = '\0';
}

uint JE_getLevelSections(int episode, JE_byte *out, JE_byte *fileOut, uint maxOut)
{
	static bool missingLevelWarned[EPISODE_MAX][256];
	const unsigned int levelFileCount = JE_levelFileCount(episode);
	if (levelFileCount == 0)
		return 0;

	long end;
	FILE *f = open_level_script(episode, &end);
	if (f == NULL)
		return 0;

	JE_word section = 0;
	bool sectionUnsafe = false;  // a mode-switch command seen since the last '*'
	char s[256];
	uint n = 0;

	while (ftell(f) < end && n < maxOut)
	{
		read_encrypted_pascal_string(s, sizeof(s), f);

		if (s[0] == '*')
		{
			section++;
			sectionUnsafe = false;
		}
		else if (s[0] == ']')
		{
			// Exclude levels that enter ENGAGE or other special multiplayer modes.
			if (s[1] == 'e' || s[1] == 'g' || s[1] == '2')
				sectionUnsafe = true;
			else if (s[1] == 'L')
			{
				const int fileNum = atoi(s + 25);
				if (fileNum < 1 || (unsigned int)fileNum > levelFileCount)
				{
					const bool inWarnTable = episode >= 1 && episode <= EPISODE_MAX
					                      && fileNum >= 0 && fileNum < 256;
					if (!inWarnTable || !missingLevelWarned[episode - 1][fileNum])
					{
						fprintf(stderr, "warning: episode %d section %u references missing level file %d\n",
						        episode, (unsigned int)section, fileNum);
						if (inWarnTable)
							missingLevelWarned[episode - 1][fileNum] = true;
					}
					continue;
				}

				// Strip the level name (padded with spaces) for a name blocklist too.
				char name[10];
				level_entry_name(s, name, sizeof(name));

				const bool special = sectionUnsafe
					|| SDL_strcasecmp(name, "ALE") == 0
					|| SDL_strcasecmp(name, "TIME WAR") == 0
					|| SDL_strcasecmp(name, "SQUADRON") == 0
					// Episode 5 levels 5-7 are an unfinished copy of a SAVARA level with no
					// proper ending, so they can never be cleared; keep them out too.
					|| (episode == 5 && section >= 5 && section <= 7);

				if (!special)
				{
					// Each ']L' is a distinct pool entry (section, lvlFileNum); a section can
					// carry two level files; see JE_getLevelSections's doc in lvllib.h.
					if (fileOut != NULL)
						fileOut[n] = (JE_byte)fileNum;
					out[n++] = (JE_byte)section;
				}
			}
		}
	}

	fclose(f);
	return n;
}

// Read a section's nine-character authored level name without loading it. fileNum 0 selects the
// first ]L entry; no match returns an empty string.
void JE_getLevelSectionName(int episode, JE_byte section, JE_byte fileNum, char *out, size_t outSize)
{
	if (out == NULL || outSize == 0)
		return;
	out[0] = '\0';

	const unsigned int levelFileCount = JE_levelFileCount(episode);
	if (levelFileCount == 0)
		return;

	long end;
	FILE *f = open_level_script(episode, &end);
	if (f == NULL)
		return;

	JE_word sec = 0;
	char s[256];

	while (ftell(f) < end)
	{
		read_encrypted_pascal_string(s, sizeof(s), f);

		if (s[0] == '*')
		{
			sec++;
		}
		else if (s[0] == ']' && s[1] == 'L')
		{
			const int fn = atoi(s + 25);
			if (fn < 1 || (unsigned int)fn > levelFileCount)
				continue;
			if (sec != section || (fileNum != 0 && fn != (int)fileNum))
				continue;

			char name[10];
			level_entry_name(s, name, sizeof(name));
			SDL_strlcpy(out, name, outSize);
			break;
		}
	}

	fclose(f);
}
