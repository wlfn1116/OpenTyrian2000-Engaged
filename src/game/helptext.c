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
#include "helptext.h"

#include "config.h"
#include "episodes.h"
#include "file.h"
#include "fonthand.h"
#include "menus.h"
#include "opentyr.h"

#include <string.h>

const JE_byte menuHelp[MENU_MAX][11] = /* [1..maxmenu, 1..11] */
{
	{  1, 34,  2,  3,  4,  5,                  0, 0, 0, 0, 0 },
	{  6,  7,  8,  9, 10, 11, 11, 12,                0, 0, 0 },
	// Zero rows use local help text.
	{ 13, 14, 15, 15,  0, 16, 17, 35, 12,          0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{  4, 30, 30,  3,  5,                   0, 0, 0, 0, 0, 0 },
	{  4, 37, 12,                     0, 0, 0, 0, 0, 0, 0, 0 },
	// Online Options: the page above without its Load Game row.
	{ 14, 15, 15,  0, 16, 17, 35, 12,              0, 0, 0 },
	{ 31, 31, 31, 31, 32, 12,                  0, 0, 0, 0, 0 },
	{  4, 34,  3,  5,                    0, 0, 0, 0, 0, 0, 0 },
	{ 35, 35, 35, 36, 12,                   0, 0, 0, 0, 0, 0 },
	// Debug Play Level, E-Shop and Perks draw custom help.
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	{                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

JE_byte verticalHeight = 7;
JE_byte helpBoxColor = 12;
JE_byte helpBoxBrightness = 1;
JE_byte helpBoxShadeType = FULL_SHADE;

char helpTxt[39][231];                                                   /* [1..39] of string [230] */
char pName[21][16];                                                      /* [1..21] of string [15] */
char miscText[HELPTEXT_MISCTEXT_COUNT][42];                              /* [1..68] of string [41] */
char miscTextB[HELPTEXT_MISCTEXTB_COUNT][HELPTEXT_MISCTEXTB_SIZE];       /* [1..5] of string [10] */
char menuText[7][HELPTEXT_MENUTEXT_SIZE];                                /* [1..7] of string [20] */
char outputs[9][31];                                                     /* [1..9] of string [30] */
char topicName[6][21];                                                   /* [1..6] of string [20] */
char mainMenuHelp[HELPTEXT_MAINMENUHELP_COUNT][66];                      /* [1..34] of string [65] */
char inGameText[6][21];                                                  /* [1..6] of string [20] */
char detailLevel[6][13];                                                 /* [1..6] of string [12] */
char gameSpeedText[5][13];                                               /* [1..5] of string [12] */
char inputDevices[3][13];                                                /* [1..3] of string [12] */
char networkText[HELPTEXT_NETWORKTEXT_COUNT][HELPTEXT_NETWORKTEXT_SIZE]; /* [1..4] of string [20] */
char difficultyNameB[11][21];                                            /* [0..9] of string [20] */
char joyButtonNames[5][21];                                              /* [1..5] of string [20] */
char superShips[HELPTEXT_SUPERSHIPS_COUNT][26];                          /* [0..10] of string [25] */
char specialName[HELPTEXT_SPECIALNAME_COUNT][10];                        /* [1..9] of string [9] */
char destructHelp[25][22];                                               /* [1..25] of string [21] */
char weaponNames[17][17];                                                /* [1..17] of string [16] */
char destructModeName[DESTRUCT_MODES][13];                               /* [1..destructmodes] of string [12] */
char shipInfo[HELPTEXT_SHIPINFO_COUNT][2][256];                          /* [1..13, 1..2] of string */
char licensingInfo[3][46];                                               /* [1..3] of string */
char orderingInfo[6][32];                                                /* [1..6] of string */
char superTyrianText[6][64];                                             /* [1..6] of string */
char menuInt[MENU_MAX+1][14][24];

static void decrypt_string(char *s, size_t len)
{
	static const unsigned char crypt_key[] = { 204, 129, 63, 255, 71, 19, 25, 62, 1, 99 };

	if (len == 0)
		return;

	for (size_t i = len - 1; ; --i)
	{
		s[i] ^= crypt_key[i % sizeof(crypt_key)];
		if (i == 0)
			break;
		s[i] ^= s[i - 1];
	}
}

void read_encrypted_pascal_string(char *s, size_t size, FILE *f)
{
	Uint8 len;
	char buffer[255];

	fread_u8_die(&len, 1, f);
	fread_die(buffer, 1, len, f);

	if (size == 0)
		return;

	decrypt_string(buffer, len);

	if (len >= size)
		len = size - 1;

	memcpy(s, buffer, len);
	s[len] = '\0';
}

// Drop trailing blanks from a string read out of the data file.
static void rtrim_string(char *s)
{
	size_t len = strlen(s);
	while (len > 0 && s[len - 1] == ' ')
		s[--len] = '\0';
}

static void skip_pascal_string(FILE *f)
{
	Uint8 len;
	char buffer[255];

	fread_u8_die(&len, 1, f);
	fread_die(buffer, 1, len, f);
}

static void read_section(FILE *f, void *strings, size_t count, size_t size)
{
	skip_pascal_string(f);
	for (size_t i = 0; i < count; ++i)
		read_encrypted_pascal_string((char *)strings + i * size, size, f);
	skip_pascal_string(f);
}

#define READ_SECTION(f, strings) read_section(f, strings, COUNTOF(strings), sizeof((strings)[0]))

void JE_helpBox(SDL_Surface *screen,  int x, int y, const char *message, unsigned int boxwidth)
{
	JE_byte startpos, endpos, pos;
	JE_boolean endstring;

	char substring[256];

	if (strlen(message) == 0)
	{
		return;
	}

	pos = 1;
	endpos = 0;
	endstring = false;

	do
	{
		startpos = endpos + 1;

		do
		{
			endpos = pos;
			do
			{
				pos++;
				if (pos == strlen(message))
				{
					endstring = true;
					if ((unsigned)(pos - startpos) < boxwidth)
					{
						endpos = pos + 1;
					}
				}

			} while (!(message[pos-1] == ' ' || endstring));

		} while (!((unsigned)(pos - startpos) > boxwidth || endstring));

		SDL_strlcpy(substring, message + startpos - 1, MIN((size_t)(endpos - startpos + 1), sizeof(substring)));
		JE_textShade(screen, x, y, substring, helpBoxColor, helpBoxBrightness, helpBoxShadeType);

		y += verticalHeight;

	} while (!endstring);

	if (endpos != pos + 1)
	{
		JE_textShade(screen, x, y, message + endpos, helpBoxColor, helpBoxBrightness, helpBoxShadeType);
	}

	helpBoxColor = 12;
	helpBoxShadeType = FULL_SHADE;
}

void JE_HBox(SDL_Surface *screen, int x, int y, unsigned int  messagenum, unsigned int boxwidth)
{
	JE_helpBox(screen, x, y, helpTxt[messagenum-1], boxwidth);
}

void JE_loadHelpText(void)
{
	// E-Shop, Perks, and Online pages are populated at runtime.
	const unsigned int menuInt_entries[MENU_MAX + 1] = { -1, 7, 9, 9, -1, -1, 11, -1, -1, -1, 6, 4, 7, 7, 5, 6, 0, 0, 0, 0, 0, 0, 0 };
	const unsigned int setup_entries[10] = {10, 5, 4, 4, 5, 7, 7, 21, 3, 3};
	
	FILE *f = dir_fopen_die(data_dir(), "tyrian.hdt", "rb");
	fread_s32_die(&episode1DataLoc, 1, f);

	/*Online Help*/
	READ_SECTION(f, helpTxt);

	/*Planet names*/
	READ_SECTION(f, pName);

	/*Miscellaneous text*/
	READ_SECTION(f, miscText);

	/*Little Miscellaneous text*/
	READ_SECTION(f, miscTextB);

	/*Key names*/
	read_section(f, menuInt[6], menuInt_entries[6], sizeof(menuInt[6][0]));

	/*Main Menu*/
	READ_SECTION(f, menuText);

	// OpenTyrian 2000 Engaged Override
	strcpy(menuText[6], menuText[5]);
	strcpy(menuText[5], menuText[4]);
	strcpy(menuText[4], "Setup");
	strcpy(menuText[0], "Start Game");

	/*Event text*/
	READ_SECTION(f, outputs);

	/*Help topics*/
	READ_SECTION(f, topicName);

	/*Main Menu Help*/
	READ_SECTION(f, mainMenuHelp);

	/*Menu 1 - Main*/
	read_section(f, menuInt[1], menuInt_entries[1], sizeof(menuInt[1][0]));

	/*Menu 2 - Items*/
	read_section(f, menuInt[2], menuInt_entries[2], sizeof(menuInt[2][0]));

	/*Menu 3 - Options*/
	read_section(f, menuInt[3], menuInt_entries[3], sizeof(menuInt[3][0]));

	/*InGame Menu*/
	READ_SECTION(f, inGameText);

	/*Detail Level*/
	READ_SECTION(f, detailLevel);

	/*Game speed text*/
	READ_SECTION(f, gameSpeedText);

	// episode names.  The data file pads some of them out to a fixed width (episode 2 most
	// visibly), which shifts every centred or right-aligned draw of the name by the padding.
	READ_SECTION(f, episode_name);
	for (unsigned int i = 0; i < COUNTOF(episode_name); ++i)
		rtrim_string(episode_name[i]);

	// difficulty names
	READ_SECTION(f, difficulty_name);

	// gameplay mode names
	READ_SECTION(f, gameplay_name);

	/*Menu 10 - 2Player Main*/
	read_section(f, menuInt[10], menuInt_entries[10], sizeof(menuInt[10][0]));

	/*Input Devices*/
	READ_SECTION(f, inputDevices);

	/*Network text*/
	READ_SECTION(f, networkText);

	/*Menu 11 - 2Player Network*/
	read_section(f, menuInt[11], menuInt_entries[11], sizeof(menuInt[11][0]));

	/*HighScore Difficulty Names*/
	READ_SECTION(f, difficultyNameB);

	// Use the difficulty-select names in score and debug screens.
	// Hidden ranks retain their separate names.
	SDL_strlcpy(difficultyNameB[DIFFICULTY_EASY],         difficulty_name[1], sizeof(difficultyNameB[0]));
	SDL_strlcpy(difficultyNameB[DIFFICULTY_NORMAL],       difficulty_name[2], sizeof(difficultyNameB[0]));
	SDL_strlcpy(difficultyNameB[DIFFICULTY_HARD],         difficulty_name[3], sizeof(difficultyNameB[0]));
	SDL_strlcpy(difficultyNameB[DIFFICULTY_IMPOSSIBLE],   difficulty_name[4], sizeof(difficultyNameB[0]));
	SDL_strlcpy(difficultyNameB[DIFFICULTY_SUICIDE],      difficulty_name[5], sizeof(difficultyNameB[0]));
	SDL_strlcpy(difficultyNameB[DIFFICULTY_LORD_OF_GAME], difficulty_name[6], sizeof(difficultyNameB[0]));

	/*Menu 12 - Network Options*/
	read_section(f, menuInt[12], menuInt_entries[12], sizeof(menuInt[12][0]));

	/*Menu 13 - Joystick*/
	read_section(f, menuInt[13], menuInt_entries[13], sizeof(menuInt[13][0]));

	/*Joystick Button Assignments*/
	READ_SECTION(f, joyButtonNames);

	/*SuperShips - For Super Arcade Mode*/
	READ_SECTION(f, superShips);

	/*SuperShips - For Super Arcade Mode*/
	READ_SECTION(f, specialName);

	/*Secret DESTRUCT game*/
	READ_SECTION(f, destructHelp);

	/*Secret DESTRUCT weapons*/
	READ_SECTION(f, weaponNames);

	/*Secret DESTRUCT modes*/
	READ_SECTION(f, destructModeName);

	/*NEW: Ship Info*/
	skip_pascal_string(f);
	for (unsigned int i = 0; i < COUNTOF(shipInfo); ++i)
	{
		read_encrypted_pascal_string(shipInfo[i][0], sizeof(shipInfo[i][0]), f);
		read_encrypted_pascal_string(shipInfo[i][1], sizeof(shipInfo[i][1]), f);
	}
	skip_pascal_string(f);

	/*Menu 14 - Super Tyrian*/
	read_section(f, menuInt[14], menuInt_entries[14], sizeof(menuInt[14][0]));

	/*Timed Battle Planets*/
	READ_SECTION(f, timed_battle_name);

	/*Setup entries, skipped since we don't use them*/
	for (unsigned int entry = 0; entry < COUNTOF(setup_entries); ++entry)
	{
		skip_pascal_string(f);
		for (unsigned int i = 0; i < setup_entries[entry]; ++i)
			skip_pascal_string(f);
		skip_pascal_string(f);
	}

	/*Menu 15 - Mouse Settings*/
	read_section(f, menuInt[15], menuInt_entries[15], sizeof(menuInt[15][0]));

	/*Tyrian Licensing Info*/
	READ_SECTION(f, licensingInfo);

	/*Default High Score Names*/
	READ_SECTION(f, defaultHighScoreNames);

	/*Default Team Names*/
	READ_SECTION(f, defaultTeamNames);

	/*Ordering Info?*/
	READ_SECTION(f, orderingInfo);

	/*Super Tyrian text*/
	skip_pascal_string(f);
	for (unsigned int i = 0; i < COUNTOF(superTyrianText); ++i)
		read_encrypted_pascal_string(superTyrianText[i], sizeof(superTyrianText[i]), f);

	fclose(f);
}
