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
#ifndef SAVEGAME_H
#define SAVEGAME_H

#include "config.h"
#include "opentyr.h"

#include "SDL.h"

#define SAVE_FILES_NUM (11 * 2)

/* The plain-text save file beside opentyrian.cfg. The DOS-era tyrian.sav is imported once; see
 * doc/notes.md#saves-and-retries. */
#define SAVE_FILE_NAME        "opentyrian.sav"
#define SAVE_FILE_LEGACY_NAME "tyrian.sav"
#define SAVE_FILE_FORMAT      1   // bumped when a key changes meaning; a missing key is a default

enum
{
	SAVE_DUAL_NONE = 0,
	SAVE_DUAL_COOP,
	SAVE_DUAL_ARCADE,
};

/* One save slot. A slot with level 0 is empty. Slots 12-22 hold two full loadouts: lastItems is
 * then player two's, and dualShips says which two-ship session wrote it. */
typedef struct
{
	JE_word       level;
	JE_PItemsType items;
	Sint64        score;   // player 1 cash
	Sint64        score2;  // player 2 cash
	char          levelName[11]; /* string [9]; */ /* SYN: Added one more byte to match lastLevelName below */
	JE_char       name[15]; /* [1..14] */ /* SYN: Added extra byte for null */
	JE_byte       cubes;
	JE_byte       power[2]; /* [1..2] */
	JE_byte       episode;
	JE_PItemsType lastItems;
	JE_byte       difficulty;
	JE_byte       secretHint;
	JE_byte       input1;
	JE_byte       input2;
	JE_boolean    gameHasRepeated; /*See if you went from one episode to another*/
	JE_byte       initialDifficulty;

	/* SAVE_DUAL_NONE for a one-ship record. A two-complete-ships record says which session wrote
	 * it and carries the powers and modes the two loadout blocks cannot hold. */
	JE_byte       dualShips;
	JE_byte       dualPower[2];
	JE_byte       dualMode[2];
	JE_boolean    onlinePlayerTwo;
	JE_boolean    autoFireSpecial;
	JE_byte       chargeSidekickAutofire;
	JE_boolean    difficultyAdjust;
	JE_boolean    cheatInfiniteSidekickAmmo;
	JE_boolean    cheatInfiniteShields;
	JE_boolean    cheatInfiniteArmor;
	JE_boolean    expertMode;
	// Per-seat dyes included in the online resume record.
	JE_byte       shipColor[2];
	// Per-seat partner views included in two-player online resume records.
	JE_byte       viewOpacity[2];
	JE_byte       viewShipOpacity[2];
	JE_byte       viewHpBars[2];
	/* Custom-episode file name; empty for stock games. */
	char          customEpFile[64];  /* CUSTOM_EPISODE_FILE_LEN */
	/* Colon-separated Custom Endless dependencies; empty for other saves. */
	char          customCollection[4096];  /* CUSTOM_EPISODE_COLLECTION_LEN */
} JE_SaveFileType;

typedef JE_SaveFileType JE_SaveFilesType[SAVE_FILES_NUM]; /* [1..savefilesnum] */

typedef struct
{
	Sint64        score;
	char          playerName[30];
	JE_byte       difficulty;
} T2KHighScoreType;

// First 10 are timed battle, next 10 are episodes
extern T2KHighScoreType t2kHighScores[20][3];

extern JE_SaveFilesType saveFiles;

bool save_file_load(void);
bool legacy_save_load(void);
void save_defaults(void);
bool save_file_write_current(void);

// Serialize or replace every save slot, high-score board, online seat, and Endless run.
size_t save_file_serialize(Uint8 *buf, size_t cap);
bool save_file_adopt(const Uint8 *buf, size_t len);
// Serialize or replace slots, online seats, and Endless runs without high scores.
size_t save_slots_serialize(Uint8 *buf, size_t cap);
bool save_slots_adopt(const Uint8 *buf, size_t len);

void JE_saveGame(JE_byte slot, const char *name);
void JE_loadGame(JE_byte slot);

// Apply a save record without going through a slot; the network resume path feeds the joiner a
// record received from the host.  twoP tells it which loadout layout the record uses.
void JE_loadGameRecord(const JE_SaveFileType *rec, bool twoP);

// Fixed little-endian save record. NET_VERSION 87 added the 64-byte container name.
#define SAVE_RECORD_PACKED_SIZE 162
void save_record_pack(Uint8 *buf, const JE_SaveFileType *rec);
void save_record_unpack(JE_SaveFileType *rec, const Uint8 *buf);
bool save_record_is_coop(const JE_SaveFileType *rec);
// ...and the arcade shapes that also fly two complete ships (Separate, Super Arcade, SuperTyrian).
bool save_record_is_dual_arcade(const JE_SaveFileType *rec);
// The Super Arcade ship (1..SA), SA_SUPERTYRIAN, or SA_NONE each of the record's two ships flies.
uint save_record_sa_ship(const JE_SaveFileType *rec, uint p);

/* Saved two-player seat; unset and invalid values read as player one. */
uint save_slot_online_player(JE_byte slot);
void save_slot_set_online_player(JE_byte slot, uint playerNum);

/* Save-codec regression hooks (qa.c): the slot codec's round trip and defaults, and a legacy
 * tyrian.sav imported from an explicit path over the live tables. */
bool save_file_test_codec(char *detail, size_t detailSize);
bool save_legacy_test_import(const char *path);

#endif /* SAVEGAME_H */
