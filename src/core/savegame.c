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
#include "savegame.h"

#include "config.h"
#include "config_file.h"
#include "custom_episode.h"
#include "debug_flags.h"
#include "endless.h"
#include "episodes.h"
#include "extra_ships.h"
#include "file.h"
#include "game_state.h"
#include "mtrand.h"
#include "net_style.h"
#include "network.h"
#include "opentyr.h"
#include "player.h"
#include "ship_skin.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _MSC_VER
#include <unistd.h>
#endif

/* Savegame files */
JE_SaveFilesType saveFiles; /*array[1..saveLevelnum] of savefiletype;*/

T2KHighScoreType t2kHighScores[20][3];

// Slots 1 through 11 are the one-player page, which no online session writes.
static bool save_slot_two_player(JE_byte slot)
{
	return slot >= 12 && slot <= SAVE_FILES_NUM;
}

uint save_slot_online_player(JE_byte slot)
{
	return (save_slot_two_player(slot) && saveFiles[slot - 1].onlinePlayerTwo) ? 2u : 1u;
}

void save_slot_set_online_player(JE_byte slot, uint playerNum)
{
	if (save_slot_two_player(slot))
		saveFiles[slot - 1].onlinePlayerTwo = playerNum == 2;
}

static void playeritems_to_pitems(JE_PItemsType pItems, PlayerItems *items, JE_byte initial_episode_num)
{
	pItems[0]  = items->weapon[FRONT_WEAPON].id;
	pItems[1]  = items->weapon[REAR_WEAPON].id;
	pItems[2]  = items->super_arcade_mode;
	pItems[3]  = items->sidekick[LEFT_SIDEKICK];
	pItems[4]  = items->sidekick[RIGHT_SIDEKICK];
	pItems[5]  = items->generator;
	pItems[6]  = items->sidekick_level;
	pItems[7]  = items->sidekick_series;
	pItems[8]  = initial_episode_num;
	pItems[9]  = items->shield;
	pItems[10] = items->special;
	pItems[11] = items->ship;
}

static void pitems_to_playeritems(PlayerItems *items, const JE_PItemsType pItems, JE_byte *initial_episode_num)
{
	items->weapon[FRONT_WEAPON].id  = pItems[0];
	items->weapon[REAR_WEAPON].id   = pItems[1];
	items->super_arcade_mode        = pItems[2];
	items->sidekick[LEFT_SIDEKICK]  = pItems[3];
	items->sidekick[RIGHT_SIDEKICK] = pItems[4];
	items->generator                = pItems[5];
	items->sidekick_level           = pItems[6];
	items->sidekick_series          = pItems[7];
	if (initial_episode_num != NULL)
		*initial_episode_num        = pItems[8];
	items->shield                   = pItems[9];
	items->special                  = pItems[10];
	items->ship                     = pItems[11];
}

void JE_saveGame(JE_byte slot, const char *name)
{
	// Enforce Hardcore below the menus. A mid-run call must leave both slot sections unchanged.
	if (endlessMode && endlessHardcore())
		return;

	saveFiles[slot-1].initialDifficulty = initialDifficulty;
	saveFiles[slot-1].gameHasRepeated = gameHasRepeated;
	saveFiles[slot-1].level = saveLevel;
	
	if (superTyrian)
		player[0].items.super_arcade_mode = SA_SUPERTYRIAN;
	else if (superArcadeMode == SA_NONE && onePlayerAction)
		player[0].items.super_arcade_mode = SA_ARCADE;
	else
		player[0].items.super_arcade_mode = superArcadeMode;
	
	playeritems_to_pitems(saveFiles[slot-1].items, &player[0].items, initial_episode_num);
	
	if (twoPlayerMode)
		playeritems_to_pitems(saveFiles[slot-1].lastItems, &player[1].items, 0);
	else
		playeritems_to_pitems(saveFiles[slot-1].lastItems, &player[0].last_items, 0);
	
	saveFiles[slot-1].score  = player[0].cash;
	saveFiles[slot-1].score2 = player[1].cash;
	
	memcpy(&saveFiles[slot-1].levelName, &lastLevelName, sizeof(lastLevelName));
	saveFiles[slot-1].cubes  = lastCubeMax;

	if (strcmp(lastLevelName, "Completed") == 0)
	{
		temp = episodeNum - 1;
		if (temp < 1)
		{
			temp = EPISODE_AVAILABLE; /* JE: {Episodemax is 4 for completion purposes} */
		}
		saveFiles[slot-1].episode = temp;
	}
	else
	{
		saveFiles[slot-1].episode = episodeNum;
	}

	// Stock saves leave this field empty.
	memset(saveFiles[slot-1].customEpFile, 0, sizeof(saveFiles[slot-1].customEpFile));
	if (customEpisodeActive())
		SDL_strlcpy(saveFiles[slot-1].customEpFile, customEpisodeActiveFile(),
		            sizeof(saveFiles[slot-1].customEpFile));

	memset(saveFiles[slot-1].customCollection, 0, sizeof(saveFiles[slot-1].customCollection));
	if (endlessMode &&
	    (customEpisodeSessionActive() || customEndlessEffectiveMode() != CUSTOM_ENDLESS_OFF))
		customEpisodeCollectionString(saveFiles[slot-1].customCollection,
		                              sizeof(saveFiles[slot-1].customCollection));

	saveFiles[slot-1].difficulty = difficultyLevel;
	saveFiles[slot-1].secretHint = secretHint;
	saveFiles[slot - 1].input1 = inputDevice[0];
	saveFiles[slot - 1].input2 = inputDevice[1];

	saveFiles[slot - 1].autoFireSpecial = autoFireSpecial;
	saveFiles[slot - 1].chargeSidekickAutofire = chargeSidekickAutofire;
	saveFiles[slot - 1].difficultyAdjust = difficultyAdjust;
	saveFiles[slot - 1].cheatInfiniteSidekickAmmo = cheatInfiniteSidekickAmmo;
	saveFiles[slot - 1].cheatInfiniteShields = cheatInfiniteShields;
	saveFiles[slot - 1].cheatInfiniteArmor = cheatInfiniteArmor;
	saveFiles[slot - 1].expertMode = expertMode;
	// Store dyes and views by seat so either player can host a resume.
	for (uint p = 0; p < COUNTOF(saveFiles[slot - 1].shipColor); ++p)
	{
		saveFiles[slot - 1].shipColor[p] = (JE_byte)netStyleSeatColor(p);
		saveFiles[slot - 1].shipSkin[p] = shipSkinGet(p);
		saveFiles[slot - 1].sidekickPlain[p] = netStyleSidekickDye(p) ? 0 : 1;

		const NetShipView view = netStyleView(p);
		saveFiles[slot - 1].viewOpacity[p] = view.opacity;
		saveFiles[slot - 1].viewShipOpacity[p] = view.shipOpacity ? 1 : 0;
		saveFiles[slot - 1].viewHpBars[p] = view.hpBars;

		const PlayerItems *const standard = extraShipStandard(p);
		if (standard != NULL)
		{
			PlayerItems copy = *standard;
			playeritems_to_pitems(saveFiles[slot - 1].standardItems[p], &copy, 0);
		}
		else
		{
			memset(saveFiles[slot - 1].standardItems[p], 0,
			       sizeof(saveFiles[slot - 1].standardItems[p]));
		}
		saveFiles[slot - 1].standardLent[p] = extraShipLentParts(p);
		for (uint w = 0; w < COUNTOF(saveFiles[slot - 1].standardPower[p]); ++w)
			saveFiles[slot - 1].standardPower[p][w] =
				(standard != NULL) ? standard->weapon[w].power : 0;
	}

	strcpy(saveFiles[slot-1].name, name);

	// Remember the seat this machine was flying, and forget it when a local game overwrites the
	// slot; a resume reads it back so nobody changes player number across the save.
	save_slot_set_online_player(slot, (isNetworkGame && twoPlayerMode) ? thisPlayerNum : 0);

	for (uint port = 0; port < 2; ++port)
	{
		// if two-player, use first player's front and second player's rear weapon
		saveFiles[slot-1].power[port] = player[twoPlayerMode ? port : 0].items.weapon[port].power;
	}

	/* Dual-ship records keep the two missing weapon powers and both modes. Distinct co-op and
	 * arcade kinds keep the shared slot page type-safe. */
	saveFiles[slot - 1].dualShips = SAVE_DUAL_NONE;
	memset(saveFiles[slot - 1].dualPower, 0, sizeof(saveFiles[slot - 1].dualPower));
	memset(saveFiles[slot - 1].dualMode, 0, sizeof(saveFiles[slot - 1].dualMode));
	if (dual_ship_mode())
	{
		saveFiles[slot - 1].dualShips = coop_mode_active() ? SAVE_DUAL_COOP : SAVE_DUAL_ARCADE;
		saveFiles[slot - 1].dualPower[0] = player[0].items.weapon[REAR_WEAPON].power;
		saveFiles[slot - 1].dualPower[1] = player[1].items.weapon[FRONT_WEAPON].power;
		saveFiles[slot - 1].dualMode[0] = player[0].weapon_mode;
		saveFiles[slot - 1].dualMode[1] = player[1].weapon_mode;
	}

	// The slot's Endless half rides the same write: captured from a run, cleared by any other game.
	endlessSaveCaptureSlot(slot);
	JE_saveConfiguration();
}

void JE_loadGame(JE_byte slot)
{
	JE_loadGameRecord(&saveFiles[slot-1], (slot-1) > 10);
}

void save_record_restore_stash(const JE_SaveFileType *rec)
{
	for (uint p = 0; p < COUNTOF(rec->standardItems); ++p)
	{
		PlayerItems standard;
		memset(&standard, 0, sizeof(standard));
		pitems_to_playeritems(&standard, rec->standardItems[p], NULL);
		for (uint w = 0; w < COUNTOF(standard.weapon); ++w)
			standard.weapon[w].power = (rec->standardPower[p][w] != 0)
			                         ? rec->standardPower[p][w] : player[p].items.weapon[w].power;
		extraShipSetStandard(p, &standard, rec->standardLent[p]);
	}
}

void JE_loadGameRecord(const JE_SaveFileType *rec, bool twoP)
{
	superTyrian = false;
	onePlayerAction = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	extraGame = false;
	galagaMode = false;
	timedBattleMode = false;
	endlessMode = false;  // saves are never endless (high-scores-only mode); always load as a normal game

	initialDifficulty = rec->initialDifficulty;
	gameHasRepeated   = rec->gameHasRepeated;
	twoPlayerMode     = twoP;
	// The tag only says the record carries two full loadouts; which co-op lobby is flying it is
	// the session's own business, so the network start path assigns the pair after this returns.
	coopCampaignMode  = isNetworkGame && twoP && save_record_is_coop(rec);
	difficultyLevel   = rec->difficulty;

	pitems_to_playeritems(&player[0].items, rec->items, &initial_episode_num);

	superArcadeMode = player[0].items.super_arcade_mode;

	if (superArcadeMode == SA_SUPERTYRIAN)
		superTyrian = true;
	if (superArcadeMode != SA_NONE)
		onePlayerAction = true;
	if (superArcadeMode > SA_LASTSHIP)
		superArcadeMode = SA_NONE;

	if (twoPlayerMode)
	{
		onePlayerAction = false;

		pitems_to_playeritems(&player[1].items, rec->lastItems, NULL);
		if (coop_mode_active())
		{
			player[1].is_dragonwing = false;
			player[0].last_items = player[0].items;
			player[1].last_items = player[1].items;
		}
	}
	else
	{
		pitems_to_playeritems(&player[0].last_items, rec->lastItems, NULL);
	}

	/* Compatibility with old version */
	if (player[1].items.sidekick_level < 101)
	{
		player[1].items.sidekick_level = 101;
		player[1].items.sidekick_series = player[1].items.sidekick[LEFT_SIDEKICK];
	}

	player_set_cash(&player[0], rec->score);
	player_set_cash(&player[1], rec->score2);

	mainLevel   = rec->level;
	cubeMax     = rec->cubes;
	lastCubeMax = cubeMax;

	secretHint = rec->secretHint;
	inputDevice[0] = rec->input1;
	inputDevice[1] = rec->input2;
#ifdef __SWITCH__
	// Older saves and other platforms seat players on the keyboard and mouse; here each gets a
	// controller instead.
	for (uint p = 0; p < 2; ++p)
	{
		if (inputDevice[p] != 1 && inputDevice[p] != 2)
			continue;
		JE_byte device = (JE_byte)(3 + p);
		if (device == inputDevice[1 - p])
			device = (JE_byte)(3 + (1 - p));
		inputDevice[p] = device;
	}
#endif

	for (uint p = 0; p < COUNTOF(rec->shipColor); ++p)
	{
		netStyleSetSeatColor(p, rec->shipColor[p]);
		shipSkinSet(p, rec->shipSkin[p]);
		netStyleSetSidekickDye(p, rec->sidekickPlain[p] == 0);
	}

	// Views are seat-indexed because thisPlayerNum may be assigned after loading.
	// One-player records have no online view and leave the current session alone.
	if (twoP)
	{
		for (uint p = 0; p < COUNTOF(rec->viewOpacity); ++p)
		{
			NetShipView view;
			view.opacity = rec->viewOpacity[p];
			view.shipOpacity = rec->viewShipOpacity[p] != 0;
			view.hpBars = rec->viewHpBars[p];
			netStyleSetView(p, view);
		}
	}

	autoFireSpecial = rec->autoFireSpecial;
	chargeSidekickAutofire = rec->chargeSidekickAutofire;
	difficultyAdjust = rec->difficultyAdjust;
	cheatInfiniteSidekickAmmo = rec->cheatInfiniteSidekickAmmo;
	cheatInfiniteShields = rec->cheatInfiniteShields;
	cheatInfiniteArmor = rec->cheatInfiniteArmor;
	expertMode = rec->expertMode;

	for (uint port = 0; port < 2; ++port)
	{
		// if two-player, use first player's front and second player's rear weapon
		player[twoPlayerMode ? port : 0].items.weapon[port].power = rec->power[port];
	}
	/* Restore the second loadout according to the record's mode. */
	if (save_record_is_coop(rec) || save_record_is_dual_arcade(rec))
	{
		player[0].items.weapon[REAR_WEAPON].power = rec->dualPower[0];
		player[1].items.weapon[FRONT_WEAPON].power = rec->dualPower[1];
		player[0].weapon_mode = rec->dualMode[0];
		player[1].weapon_mode = rec->dualMode[1];
		if (player[0].weapon_mode == 0) player[0].weapon_mode = 1;
		if (player[1].weapon_mode == 0) player[1].weapon_mode = 1;
	}
	save_record_restore_stash(rec);

	/* Which weapon bay each ship counts lives on depends on the shape of the session, and this is
	 * the one path that installs a loadout without going through newGame(). */
	player_bind_lives();

	int episode = rec->episode;

	memcpy(&levelName, &rec->levelName, sizeof(levelName));

	if (rec->customCollection[0] != '\0' && !isNetworkGame)
	{
		customEpisodeScan();
		char names[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
		const int count = customEpisodeCollectionNames(rec->customCollection, names,
		                                               CUSTOM_EPISODE_MAX);
		if (count > 0 && !customEpisodeCollectionMissing(rec->customCollection))
			customEpisodeSessionBegin(names, count,
				(customEndlessMode == CUSTOM_ENDLESS_MIXED ||
				 customEndlessMode == CUSTOM_ENDLESS_ONLY)
				? customEndlessMode : CUSTOM_ENDLESS_OFF);
	}

	// Custom saves resume before stock "Completed" rollover is handled.
	if (rec->customEpFile[0] != '\0')
	{
		customEpisodeScan();
		const int customIndex = customEpisodeFindByFile(rec->customEpFile);
		if (customIndex >= 0 && JE_initEpisodeCustom(customIndex))
		{
			saveLevel = mainLevel;
			memcpy(&lastLevelName, &levelName, sizeof(levelName));
			return;
		}
		fprintf(stderr, "custom episode: save wants missing '%s'\n", rec->customEpFile);
	}

	if (strcmp(levelName, "Completed") == 0)
	{
		if (episode == EPISODE_AVAILABLE)
			episode = 1;
		else if (episode < EPISODE_AVAILABLE)
			episode++;
		/* Increment episode.  Episode EPISODE_AVAILABLE goes to 1. */
	}

	JE_initEpisode(episode);
	saveLevel = mainLevel;
	memcpy(&lastLevelName, &levelName, sizeof(levelName));
}

/* The one-ship loadout block, and the second block a slot carries: player two's on the two-player
 * page, the last shop loadout on the one-player page. Named keys under a prefix. */
static void save_items_write(ConfigSection *section, const char *prefix, const JE_PItemsType items,
                             const JE_byte *frontPower, const JE_byte *rearPower)
{
	char key[48];
	#define ITEM(name, index) \
		snprintf(key, sizeof(key), "%s_%s", prefix, name); config_set_int_option(section, key, items[index])
	ITEM("ship", 11);
	ITEM("front_weapon", 0);
	if (frontPower != NULL)
	{
		snprintf(key, sizeof(key), "%s_front_power", prefix);
		config_set_int_option(section, key, *frontPower);
	}
	ITEM("rear_weapon", 1);
	if (rearPower != NULL)
	{
		snprintf(key, sizeof(key), "%s_rear_power", prefix);
		config_set_int_option(section, key, *rearPower);
	}
	ITEM("shield", 9);
	ITEM("generator", 5);
	ITEM("left_sidekick", 3);
	ITEM("right_sidekick", 4);
	ITEM("special", 10);
	ITEM("sidekick_level", 6);
	ITEM("sidekick_series", 7);
	ITEM("mode", 2);
	ITEM("start_episode", 8);
	#undef ITEM
}

static int save_get_int(const ConfigSection *section, const char *key, int fallback)
{
	int v = fallback;
	config_get_int_option(section, key, &v);
	return v;
}

static void save_items_read(const ConfigSection *section, const char *prefix, JE_PItemsType items,
                            JE_byte *frontPower, JE_byte *rearPower)
{
	char key[48];
	#define ITEM(name, index) \
		snprintf(key, sizeof(key), "%s_%s", prefix, name); \
		items[index] = (JE_byte)save_get_int(section, key, 0)
	ITEM("ship", 11);
	ITEM("front_weapon", 0);
	ITEM("rear_weapon", 1);
	ITEM("shield", 9);
	ITEM("generator", 5);
	ITEM("left_sidekick", 3);
	ITEM("right_sidekick", 4);
	ITEM("special", 10);
	ITEM("sidekick_level", 6);
	ITEM("sidekick_series", 7);
	ITEM("mode", 2);
	ITEM("start_episode", 8);
	#undef ITEM
	if (frontPower != NULL)
	{
		snprintf(key, sizeof(key), "%s_front_power", prefix);
		*frontPower = (JE_byte)save_get_int(section, key, 0);
	}
	if (rearPower != NULL)
	{
		snprintf(key, sizeof(key), "%s_rear_power", prefix);
		*rearPower = (JE_byte)save_get_int(section, key, 0);
	}
}

/* Save slots are named sections with optional p1_, p2_, and last_ keys. */
static void save_slot_write(ConfigSection *section, const JE_SaveFileType *rec, JE_byte slot)
{
	const bool twoP = slot > 11;

	config_set_string_option(section, "name", rec->name);
	config_set_int_option(section, "level", rec->level);
	config_set_string_option(section, "level_name", rec->levelName);
	config_set_int_option(section, "episode", rec->episode);
	// Slot sections are rebuilt, so stock saves omit this key.
	if (rec->customEpFile[0] != '\0')
		config_set_string_option(section, "custom_episode", rec->customEpFile);
	if (rec->customCollection[0] != '\0')
		config_set_string_option(section, "custom_collection", rec->customCollection);
	config_set_int_option(section, "difficulty", rec->difficulty);
	config_set_int_option(section, "initial_difficulty", rec->initialDifficulty);
	config_set_int_option(section, "game_has_repeated", rec->gameHasRepeated ? 1 : 0);
	config_set_int_option(section, "cubes", rec->cubes);
	config_set_int_option(section, "secret_hint", rec->secretHint);
	config_set_int64_option(section, "p1_cash", rec->score);
	if (twoP)
		config_set_int64_option(section, "p2_cash", rec->score2);

	// A one-ship record's rear power sits in power[1]; a two-ship record's rear power there is
	// player two's, and player one's rear and player two's front live in dualPower.
	const bool dual = save_record_is_coop(rec) || save_record_is_dual_arcade(rec);
	if (twoP)
	{
		save_items_write(section, "p1", rec->items, &rec->power[0],
		                 dual ? &rec->dualPower[0] : NULL);
		save_items_write(section, "p2", rec->lastItems, dual ? &rec->dualPower[1] : NULL,
		                 &rec->power[1]);
		if (dual)
		{
			config_set_string_option(section, "dual_ships", save_record_is_coop(rec) ? "coop" : "arcade");
			config_set_int_option(section, "p1_weapon_mode", rec->dualMode[0]);
			config_set_int_option(section, "p2_weapon_mode", rec->dualMode[1]);
		}
		config_set_int_option(section, "online_seat", rec->onlinePlayerTwo ? 2 : 1);
		for (uint p = 0; p < COUNTOF(rec->viewOpacity); ++p)
		{
			char key[32];
			snprintf(key, sizeof(key), "p%u_online_opacity", p + 1);
			config_set_int_option(section, key, rec->viewOpacity[p]);
			snprintf(key, sizeof(key), "p%u_online_ship_opacity", p + 1);
			config_set_int_option(section, key, rec->viewShipOpacity[p] ? 1 : 0);
			snprintf(key, sizeof(key), "p%u_online_hp_bars", p + 1);
			config_set_int_option(section, key, rec->viewHpBars[p]);
		}
	}
	else
	{
		save_items_write(section, "p1", rec->items, &rec->power[0], &rec->power[1]);
		save_items_write(section, "last", rec->lastItems, NULL, NULL);
	}

	const uint lookSeats = twoP ? 2u : 1u;
	for (uint p = 0; p < lookSeats; ++p)
	{
		char key[32];
		if (rec->shipColor[p] != NET_SHIP_COLOR_NONE)
		{
			snprintf(key, sizeof(key), "p%u_ship_color", p + 1);
			config_set_int_option(section, key, rec->shipColor[p]);
		}
		if (rec->shipSkin[p] != SHIP_SKIN_NONE)
		{
			snprintf(key, sizeof(key), "p%u_ship_skin", p + 1);
			config_set_int_option(section, key, rec->shipSkin[p]);
		}
		if (rec->sidekickPlain[p] != 0)
		{
			snprintf(key, sizeof(key), "p%u_sidekicks_plain", p + 1);
			config_set_int_option(section, key, 1);
		}
		if (rec->standardItems[p][11] != 0)
		{
			snprintf(key, sizeof(key), "p%u_standard", p + 1);
			save_items_write(section, key, rec->standardItems[p], &rec->standardPower[p][0],
			                 &rec->standardPower[p][1]);
			snprintf(key, sizeof(key), "p%u_standard_lent", p + 1);
			config_set_int_option(section, key, rec->standardLent[p]);
		}
	}

	config_set_int_option(section, "input_p1", rec->input1);
	config_set_int_option(section, "input_p2", rec->input2);
	config_set_int_option(section, "auto_fire_special", rec->autoFireSpecial ? 1 : 0);
	config_set_int_option(section, "charge_sidekick_autofire", rec->chargeSidekickAutofire);
	config_set_int_option(section, "difficulty_adjust", rec->difficultyAdjust ? 1 : 0);
	config_set_int_option(section, "cheat_infinite_sidekick_ammo", rec->cheatInfiniteSidekickAmmo ? 1 : 0);
	config_set_int_option(section, "cheat_infinite_shields", rec->cheatInfiniteShields ? 1 : 0);
	config_set_int_option(section, "cheat_infinite_armor", rec->cheatInfiniteArmor ? 1 : 0);
	config_set_int_option(section, "expert_mode", rec->expertMode ? 1 : 0);
}

static void save_get_string(const ConfigSection *section, const char *key, char *dst, size_t n)
{
	const char *text = NULL;
	if (config_get_string_option(section, key, &text) && text != NULL)
		SDL_strlcpy(dst, text, n);
	else
		dst[0] = '\0';
}

static void save_slot_read(JE_SaveFileType *rec, const ConfigSection *section, JE_byte slot)
{
	const bool twoP = slot > 11;
	memset(rec, 0, sizeof(*rec));

	save_get_string(section, "name", rec->name, sizeof(rec->name));
	rec->level = (JE_word)save_get_int(section, "level", 0);
	save_get_string(section, "level_name", rec->levelName, sizeof(rec->levelName));
	rec->episode = (JE_byte)save_get_int(section, "episode", 1);
	save_get_string(section, "custom_episode", rec->customEpFile, sizeof(rec->customEpFile));
	save_get_string(section, "custom_collection", rec->customCollection, sizeof(rec->customCollection));
	// The two difficulties index name tables; a hand edit past the last one reads as Normal.
	const int difficulty = save_get_int(section, "difficulty", DIFFICULTY_NORMAL);
	rec->difficulty = (JE_byte)((difficulty < DIFFICULTY_WIMP || difficulty > DIFFICULTY_10)
	                            ? DIFFICULTY_NORMAL : difficulty);
	const int initial = save_get_int(section, "initial_difficulty", rec->difficulty);
	rec->initialDifficulty = (JE_byte)((initial < DIFFICULTY_WIMP || initial > DIFFICULTY_10)
	                                   ? rec->difficulty : initial);
	rec->gameHasRepeated = save_get_int(section, "game_has_repeated", 0) != 0;
	rec->cubes = (JE_byte)save_get_int(section, "cubes", 0);
	rec->secretHint = (JE_byte)save_get_int(section, "secret_hint", 0);
	long long cash = 0;
	config_get_int64_option(section, "p1_cash", &cash);
	rec->score = cash_clamp((Sint64)cash);
	cash = 0;
	config_get_int64_option(section, "p2_cash", &cash);
	rec->score2 = cash_clamp((Sint64)cash);

	if (twoP)
	{
		JE_byte p1Rear = 0, p2Front = 0;
		save_items_read(section, "p1", rec->items, &rec->power[0], &p1Rear);
		save_items_read(section, "p2", rec->lastItems, &p2Front, &rec->power[1]);
		const char *dual = NULL;
		if (config_get_string_option(section, "dual_ships", &dual) && dual != NULL)
		{
			rec->dualShips = (strcmp(dual, "coop") == 0) ? SAVE_DUAL_COOP
			               : (strcmp(dual, "arcade") == 0) ? SAVE_DUAL_ARCADE : SAVE_DUAL_NONE;
			if (rec->dualShips != SAVE_DUAL_NONE)
			{
				rec->dualPower[0] = p1Rear;
				rec->dualPower[1] = p2Front;
				rec->dualMode[0] = (JE_byte)save_get_int(section, "p1_weapon_mode", 1);
				rec->dualMode[1] = (JE_byte)save_get_int(section, "p2_weapon_mode", 1);
			}
		}
		rec->onlinePlayerTwo = save_get_int(section, "online_seat", 1) == 2;
		for (uint p = 0; p < COUNTOF(rec->viewOpacity); ++p)
		{
			char key[32];
			// netStyleSetView snaps a hand-edited opacity to a picker step; keep the record raw.
			snprintf(key, sizeof(key), "p%u_online_opacity", p + 1);
			rec->viewOpacity[p] = (JE_byte)save_get_int(section, key, NET_OPACITY_FULL);
			snprintf(key, sizeof(key), "p%u_online_ship_opacity", p + 1);
			rec->viewShipOpacity[p] = (JE_byte)(save_get_int(section, key, 1) != 0);
			snprintf(key, sizeof(key), "p%u_online_hp_bars", p + 1);
			rec->viewHpBars[p] = (JE_byte)save_get_int(section, key, NET_HP_BARS_OFF);
		}
	}
	else
	{
		save_items_read(section, "p1", rec->items, &rec->power[0], &rec->power[1]);
		save_items_read(section, "last", rec->lastItems, NULL, NULL);
	}

	const uint lookSeats = twoP ? 2u : 1u;
	for (uint p = 0; p < lookSeats; ++p)
	{
		char key[32];
		snprintf(key, sizeof(key), "p%u_ship_color", p + 1);
		const int color = save_get_int(section, key, NET_SHIP_COLOR_NONE);
		rec->shipColor[p] = (JE_byte)((color < NET_SHIP_COLOR_NONE || color > NET_SHIP_COLORS)
		                              ? NET_SHIP_COLOR_NONE : color);
		snprintf(key, sizeof(key), "p%u_ship_skin", p + 1);
		const int skin = save_get_int(section, key, SHIP_SKIN_NONE);
		rec->shipSkin[p] = (JE_byte)((skin < 0 || skin > UINT8_MAX) ? SHIP_SKIN_NONE : skin);
		snprintf(key, sizeof(key), "p%u_sidekicks_plain", p + 1);
		rec->sidekickPlain[p] = save_get_int(section, key, 0) != 0;
		snprintf(key, sizeof(key), "p%u_standard", p + 1);
		save_items_read(section, key, rec->standardItems[p], &rec->standardPower[p][0],
		                &rec->standardPower[p][1]);
		snprintf(key, sizeof(key), "p%u_standard_lent", p + 1);
		const int lent = save_get_int(section, key, EXTRA_SHIP_LENT_ALL);
		const bool lentOk = lent >= 0 && lent <= EXTRA_SHIP_LENT_ALL;
		rec->standardLent[p] = (JE_byte)((rec->standardItems[p][11] == 0) ? 0
		                                 : lentOk ? lent : EXTRA_SHIP_LENT_ALL);
	}

	rec->input1 = (JE_byte)save_get_int(section, "input_p1", 1);
	rec->input2 = (JE_byte)save_get_int(section, "input_p2", 2);
	rec->autoFireSpecial = save_get_int(section, "auto_fire_special", 0) != 0;
	const int chargeAutofire = save_get_int(section, "charge_sidekick_autofire", CHARGE_AUTOFIRE_ON);
	rec->chargeSidekickAutofire = (JE_byte)((chargeAutofire < 0 || chargeAutofire >= CHARGE_AUTOFIRE_NUM)
	                                        ? CHARGE_AUTOFIRE_ON : chargeAutofire);
	rec->difficultyAdjust = save_get_int(section, "difficulty_adjust", 1) != 0;
	rec->cheatInfiniteSidekickAmmo = save_get_int(section, "cheat_infinite_sidekick_ammo", 0) != 0;
	rec->cheatInfiniteShields = save_get_int(section, "cheat_infinite_shields", 0) != 0;
	rec->cheatInfiniteArmor = save_get_int(section, "cheat_infinite_armor", 0) != 0;
	rec->expertMode = save_get_int(section, "expert_mode", 0) != 0;
}

// The name of high-score table 0..19: ten Timed Battle boards, then a 1P and 2P board per episode.
static const char *save_highscore_table_name(char *buf, size_t n, int table)
{
	if (table < 10)
		snprintf(buf, n, "timed battle %d", table + 1);
	else
		snprintf(buf, n, "episode %d %s", (table - 10) / 2 + 1, (table % 2 == 0) ? "1p" : "2p");
	return buf;
}

static void save_highscores_write(Config *config)
{
	for (int table = 0; table < 20; ++table)
	{
		char name[32];
		ConfigSection *section =
			config_add_section(config, "highscore", save_highscore_table_name(name, sizeof(name), table));
		if (section == NULL)
			exit(EXIT_FAILURE);  // out of memory
		for (int rank = 0; rank < 3; ++rank)
		{
			char key[16];
			snprintf(key, sizeof(key), "score_%d", rank + 1);
			config_set_int64_option(section, key, t2kHighScores[table][rank].score);
			snprintf(key, sizeof(key), "name_%d", rank + 1);
			config_set_string_option(section, key, t2kHighScores[table][rank].playerName);
			snprintf(key, sizeof(key), "difficulty_%d", rank + 1);
			config_set_int_option(section, key, t2kHighScores[table][rank].difficulty);
		}
	}
}

static void save_highscores_read(Config *config)
{
	for (int table = 0; table < 20; ++table)
	{
		char name[32];
		const ConfigSection *section =
			config_find_section(config, "highscore", save_highscore_table_name(name, sizeof(name), table));
		if (section == NULL)
			continue;   // the invented defaults stand
		for (int rank = 0; rank < 3; ++rank)
		{
			char key[16];
			long long score = 0;
			snprintf(key, sizeof(key), "score_%d", rank + 1);
			config_get_int64_option(section, key, &score);
			t2kHighScores[table][rank].score = cash_clamp((Sint64)score);
			snprintf(key, sizeof(key), "name_%d", rank + 1);
			save_get_string(section, key, t2kHighScores[table][rank].playerName,
			                sizeof(t2kHighScores[table][rank].playerName));
			snprintf(key, sizeof(key), "difficulty_%d", rank + 1);
			t2kHighScores[table][rank].difficulty = (JE_byte)save_get_int(section, key, 0);
		}
	}
}

// Empty slots and blank boards, the state a save file is read over.
static void save_reset(void)
{
	memset(t2kHighScores, 0, sizeof(t2kHighScores));

	memset(saveFiles, 0, sizeof(saveFiles));
	for (int z = 0; z < SAVE_FILES_NUM; z++)
	{
		memset(saveFiles[z].name, ' ', 14);
		saveFiles[z].name[14] = 0;
		saveFiles[z].chargeSidekickAutofire = CHARGE_AUTOFIRE_ON;
		saveFiles[z].difficultyAdjust = true;
	}
}

// Fresh-install state: empty slots and invented high-score boards.
void save_defaults(void)
{
	save_reset();

	for (int z = 0; z < 10; ++z)
	{
		for (int y = 0; y < 3; ++y)
		{
			// Timed Battle scores
			t2kHighScores[z][y].score = ((mt_rand() % 50) + 1) * 100;
			strcpy(t2kHighScores[z][y].playerName,
			       defaultHighScoreNames[mt_rand() % COUNTOF(defaultHighScoreNames)]);
			t2kHighScores[z][y].difficulty = 0;
		}
	}
	for (int z = 10; z < 20; ++z)
	{
		for (int y = 0; y < 3; ++y)
		{
			// Main Game scores
			t2kHighScores[z][y].score = ((mt_rand() % 20) + 1) * 1000;
			if (z & 1)
				strcpy(t2kHighScores[z][y].playerName,
				       defaultTeamNames[mt_rand() % COUNTOF(defaultTeamNames)]);
			else
				strcpy(t2kHighScores[z][y].playerName,
				       defaultHighScoreNames[mt_rand() % COUNTOF(defaultHighScoreNames)]);
			t2kHighScores[z][y].difficulty = 0;
		}
	}
}

/* Adopt parsed save data. Missing sections create empty slots; missing keys use defaults. */
static bool save_config_adopt(Config *config)
{
	// A file without its header is a broken write or a stray file, so the DOS-era files still stand in.
	const ConfigSection *header = config_find_section(config, "saves", NULL);
	if (header == NULL)
	{
		fprintf(stderr, "warning: %s has no 'saves' section and was not read\n", SAVE_FILE_NAME);
		return false;
	}

	save_reset();
	for (int z = 0; z < SAVE_FILES_NUM; z++)
	{
		char name[8];
		snprintf(name, sizeof(name), "%d", z + 1);
		const ConfigSection *section = config_find_section(config, "save", name);
		if (section != NULL)
			save_slot_read(&saveFiles[z], section, (JE_byte)(z + 1));
	}
	save_highscores_read(config);
	endlessSaveConfigRead(config);
	return true;
}

bool save_file_load(void)
{
	FILE *file = dir_fopen(get_user_directory(), SAVE_FILE_NAME, "r");
	if (file == NULL)
		return false;

	Config config;
	const bool parsed = config_parse(&config, file);
	fclose(file);
	if (!parsed)
	{
		config_deinit(&config);
		return false;
	}

	const bool adopted = save_config_adopt(&config);
	config_deinit(&config);
	return adopted;
}

/* The DOS-era tyrian.sav: XOR-chained, checksummed, fixed offsets. Read once when there is no
 * opentyrian.sav yet, so an existing installation keeps its slots and boards. */
#define LEGACY_SAVE_FILES_SIZE 2398
#define LEGACY_SAVE_TEMP_SIZE  (LEGACY_SAVE_FILES_SIZE + 4 + 100)
#define LEGACY_SAVE_FILE_SIZE  (LEGACY_SAVE_TEMP_SIZE - 4)

static bool legacy_save_decrypt(Uint8 *saveTemp)
{
	static const JE_byte cryptKey[10] = { 15, 50, 89, 240, 147, 34, 86, 9, 32, 208 };
	Uint8 s2[LEGACY_SAVE_TEMP_SIZE];

	for (int x = LEGACY_SAVE_FILE_SIZE - 1; x >= 0; x--)
	{
		const unsigned k = (unsigned)(x + 1) % 10;
		OT_ASSUME(k < 10);
		s2[x] = saveTemp[x] ^ cryptKey[k];
		if (x > 0)
			s2[x] ^= saveTemp[x - 1];
	}

	// The four one-byte checksums: additive, subtractive, multiplicative, xor.
	JE_byte sum = 0, sub = 0, mul = 1, xor = 0;
	for (int x = 0; x < LEGACY_SAVE_FILE_SIZE; x++)
	{
		sum += s2[x];
		sub -= s2[x];
		mul = (JE_byte)(mul * s2[x] + 1);
		xor ^= s2[x];
	}
	if (saveTemp[LEGACY_SAVE_FILE_SIZE] != sum || saveTemp[LEGACY_SAVE_FILE_SIZE + 1] != sub
	    || saveTemp[LEGACY_SAVE_FILE_SIZE + 2] != mul || saveTemp[LEGACY_SAVE_FILE_SIZE + 3] != xor)
	{
		fprintf(stderr, "warning: %s failed its checksums and was not imported\n", SAVE_FILE_LEGACY_NAME);
		return false;
	}

	memcpy(saveTemp, s2, LEGACY_SAVE_FILE_SIZE);
	return true;
}

// Read a legacy tyrian.sav image over the live tables; the caller owns the FILE.
static bool legacy_save_parse(FILE *fi)
{
	Uint8 saveTemp[LEGACY_SAVE_TEMP_SIZE];
	if (fread(saveTemp, 1, sizeof(saveTemp), fi) != sizeof(saveTemp) || !legacy_save_decrypt(saveTemp))
		return false;

	save_reset();

	const Uint8 *p = saveTemp;
	for (int z = 0; z < SAVE_FILES_NUM; z++)
	{
		JE_SaveFileType *rec = &saveFiles[z];
		Uint16 u16;
		Uint32 u32;

		p += 2;   // encode: a DOS field nothing reads
		memcpy(&u16, p, 2); p += 2;
		rec->level = SDL_SwapLE16(u16);
		memcpy(rec->items, p, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
		// Legacy wallets are unsigned 32-bit: read as signed, a record past 2^31 comes back negative.
		memcpy(&u32, p, 4); p += 4;
		rec->score = cash_clamp((Sint64)SDL_SwapLE32(u32));
		memcpy(&u32, p, 4); p += 4;
		rec->score2 = cash_clamp((Sint64)SDL_SwapLE32(u32));

		// Pascal string: a length byte, then the characters.
		memset(rec->levelName, 0, sizeof(rec->levelName));
		memcpy(rec->levelName, &p[1], MIN(*p, sizeof(rec->levelName) - 1));
		p += 10;
		memcpy(rec->name, p, 14);   // 14 bytes with no length prefix, unlike levelName
		p += 14;

		rec->cubes = *p++;
		rec->power[0] = *p++;
		rec->power[1] = *p++;
		rec->episode = *p++;
		memcpy(rec->lastItems, p, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
		rec->difficulty = *p++;
		rec->secretHint = *p++;
		rec->input1 = *p++;
		rec->input2 = *p++;
		rec->gameHasRepeated = *p++ != 0;
		rec->initialDifficulty = *p++;

		p += 8;   // highScore1 and highScore2: the DOS per-slot table, never shown
		p += 30;  // highScoreName
		p += 1;   // highScoreDiff
	}
	assert(p - saveTemp == LEGACY_SAVE_FILES_SIZE);   // the DOS ship editor's unlocks follow, unread

	// The T2K high-score boards follow the encrypted block unencrypted; the episode boards carry
	// an unused long between the score and the name.
	for (int z = 0; z < 20; ++z)
	{
		for (int y = 0; y < 3; ++y)
		{
			Sint32 score = 0;
			JE_byte len = 0;
			char name[30];
			JE_byte difficulty = 0;
			bool okay = fread(&score, 4, 1, fi) == 1;
			if (z >= 10)
				okay = okay && fseek(fi, 4, SEEK_CUR) == 0;
			okay = okay && fread(&len, 1, 1, fi) == 1 && fread(name, 1, 29, fi) == 29
			    && fread(&difficulty, 1, 1, fi) == 1;
			if (!okay)
			{
				printf("Imported the save slots from the old %s; its high-score boards were incomplete.\n",
				       SAVE_FILE_LEGACY_NAME);
				return true;
			}
			t2kHighScores[z][y].score = cash_clamp((Sint64)SDL_SwapLE32((Uint32)score));
			name[MIN(len, 29)] = '\0';
			memcpy(t2kHighScores[z][y].playerName, name, 30);
			t2kHighScores[z][y].difficulty = difficulty;
		}
	}

	printf("Imported the save slots and high scores from the old %s.\n", SAVE_FILE_LEGACY_NAME);
	return true;
}

bool legacy_save_load(void)
{
	FILE *fi = dir_fopen(get_user_directory(), SAVE_FILE_LEGACY_NAME, "rb");
	if (fi == NULL)
		return false;
	const bool okay = legacy_save_parse(fi);
	fclose(fi);
	return okay;
}

bool save_legacy_test_import(const char *path)
{
	FILE *fi = fopen(path, "rb");
	if (fi == NULL)
		return false;
	const bool okay = legacy_save_parse(fi);
	fclose(fi);
	return okay;
}

/* Prove the slot codec on records this build made: a one-ship and a two-ship record survive the
 * text round trip whole, and a slot with keys missing or malformed reads as its defaults. */
bool save_file_test_codec(char *detail, size_t detailSize)
{
	if (detail != NULL && detailSize != 0)
		detail[0] = '\0';

	JE_SaveFileType one, two, back;
	memset(&one, 0, sizeof(one));
	one.level = 7;
	for (unsigned i = 0; i < sizeof(one.items); ++i)
	{
		one.items[i] = (JE_byte)(i * 5 + 2);
		one.lastItems[i] = (JE_byte)(90 - i * 3);
	}
	one.score = 4000000000LL;   // past the old 32-bit wallet
	strcpy(one.levelName, "TYRIAN");
	strcpy(one.name, "SLOT ONE      ");
	one.cubes = 3;
	one.power[0] = 4;
	one.power[1] = 9;
	one.episode = 2;
	one.difficulty = DIFFICULTY_HARD;
	one.secretHint = 5;
	one.input1 = 1;
	one.input2 = 2;
	one.gameHasRepeated = true;
	one.initialDifficulty = DIFFICULTY_NORMAL;
	one.autoFireSpecial = true;
	one.chargeSidekickAutofire = 2;
	one.difficultyAdjust = false;
	one.cheatInfiniteSidekickAmmo = true;
	one.cheatInfiniteArmor = true;
	one.expertMode = true;
	one.shipSkin[0] = 3;
	one.shipColor[0] = 6;
	one.sidekickPlain[0] = 1;
	for (unsigned i = 0; i < sizeof(one.standardItems[0]); ++i)
		one.standardItems[0][i] = (JE_byte)(i * 7 + 1);
	one.standardLent[0] = 0x55;
	one.standardPower[0][0] = 7;
	one.standardPower[0][1] = 2;

	two = one;
	two.score2 = 123456789012LL;
	two.dualShips = SAVE_DUAL_COOP;
	two.dualPower[0] = 0x0a;
	two.dualPower[1] = 0x0b;
	two.dualMode[0] = 2;
	two.dualMode[1] = 3;
	two.onlinePlayerTwo = true;
	// Use distinct per-seat values to expose missing two-player keys.
	for (uint p = 0; p < COUNTOF(two.viewOpacity); ++p)
	{
		two.viewOpacity[p] = (JE_byte)(NET_OPACITY_MIN + p * NET_OPACITY_STEP);
		two.viewShipOpacity[p] = (JE_byte)(p == 0);
		two.viewHpBars[p] = (JE_byte)(p == 0 ? NET_HP_BARS_ON_HIT : NET_HP_BARS_ALWAYS);
		two.shipSkin[p] = (JE_byte)(9 + p * 12);
		two.shipColor[p] = (JE_byte)(2 + p * 9);
		two.sidekickPlain[p] = (JE_byte)(p == 1);
		for (unsigned i = 0; i < sizeof(two.standardItems[p]); ++i)
			two.standardItems[p][i] = (JE_byte)(i * 3 + 11 + p * 40);
		two.standardLent[p] = (JE_byte)(0x15 + p * 0x20);
		two.standardPower[p][0] = (JE_byte)(3 + p * 5);
		two.standardPower[p][1] = (JE_byte)(1 + p * 9);
	}

	const char *fault = NULL;
	Config config;
	config_init(&config);

	/* Adding a section can move the ones already there, so write each through the pointer its own
	 * add returned and look both up again once no more are coming. */
	ConfigSection *section = config_add_section(&config, "save", "3");
	if (section != NULL)
		save_slot_write(section, &one, 3);
	section = config_add_section(&config, "save", "15");
	if (section != NULL)
		save_slot_write(section, &two, 15);

	ConfigSection *const sOne = config_find_section(&config, "save", "3");
	ConfigSection *const sTwo = config_find_section(&config, "save", "15");
	if (sOne == NULL || sTwo == NULL)
		fault = "section allocation failed";
	else
	{
		save_slot_read(&back, sOne, 3);
		if (memcmp(&one, &back, sizeof(one)) != 0)
			fault = "a one-ship slot did not survive the round trip";
		save_slot_read(&back, sTwo, 15);
		if (fault == NULL && !back.onlinePlayerTwo)
			fault = "the online seat did not survive the round trip";
		if (fault == NULL && memcmp(&two, &back, sizeof(two)) != 0)
			fault = "a two-ship slot did not survive the round trip";

		// Report missing view keys separately from the full-record comparison.
		if (fault == NULL)
			for (uint p = 0; p < COUNTOF(two.viewOpacity); ++p)
				if (back.viewOpacity[p] != two.viewOpacity[p]
				    || back.viewShipOpacity[p] != two.viewShipOpacity[p]
				    || back.viewHpBars[p] != two.viewHpBars[p])
					fault = "the online look did not survive the round trip";

		config_remove_option(sOne, "p1_cash");
		config_set_string_option(sOne, "level", "junk");
		config_set_string_option(sOne, "p2_cash", "-5");
		config_set_string_option(sOne, "difficulty", "");
		save_slot_read(&back, sOne, 3);
		if (fault == NULL && (back.score != 0 || back.level != 0 || back.score2 != 0
		                      || back.difficulty != DIFFICULTY_NORMAL))
			fault = "a missing or malformed key did not read as its default";
	}
	config_deinit(&config);

	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

/* Build opentyrian.sav: the slots that hold a game, each Endless half beside its slot, and the
 * high-score boards. Everything is a named key in the same format as opentyrian.cfg. */
static void save_config_build(Config *config, bool includeHighScores)
{
	config_init(config);

	ConfigSection *section = config_add_section(config, "saves", NULL);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	config_set_int_option(section, "format", SAVE_FILE_FORMAT);

	for (int z = 0; z < SAVE_FILES_NUM; z++)
	{
		if (saveFiles[z].level == 0)
			continue;   // an empty slot is a slot with no section
		char name[8];
		snprintf(name, sizeof(name), "%d", z + 1);
		section = config_add_section(config, "save", name);
		if (section == NULL)
			exit(EXIT_FAILURE);  // out of memory
		save_slot_write(section, &saveFiles[z], (JE_byte)(z + 1));
	}
	endlessSaveConfigWrite(config);
	if (includeHighScores)
		save_highscores_write(config);
}

size_t save_file_serialize(Uint8 *buf, size_t cap)
{
	if (!configuration_loaded)
		return 0;

	Config config;
	save_config_build(&config, true);
	const size_t required = config_write_buffer(&config, (char *)buf, cap);
	config_deinit(&config);
	return required <= cap ? required : 0;
}

size_t save_slots_serialize(Uint8 *buf, size_t cap)
{
	if (!configuration_loaded)
		return 0;

	Config config;
	save_config_build(&config, false);
	const size_t required = config_write_buffer(&config, (char *)buf, cap);
	config_deinit(&config);
	return required <= cap ? required : 0;
}

bool save_file_write_current(void)
{
	if (!configuration_loaded)
		return false;

	Config config;
	save_config_build(&config, true);

	// Best-effort, as in save_opentyrian_config: dir_fopen_warn is what reports a broken directory.
	make_user_directory();

	bool ok = false;
	FILE *f = dir_fopen_warn(get_user_directory(), SAVE_FILE_NAME, "w");
	if (f != NULL)
	{
		config_write(&config, f);
		ok = ferror(f) == 0;
#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
		if (fsync(fileno(f)) != 0)
			ok = false;
#endif
		if (fclose(f) != 0)
			ok = false;
	}
	config_deinit(&config);
	return ok;
}

bool save_file_adopt(const Uint8 *buf, size_t len)
{
	if (buf == NULL || len == 0)
		return false;

	Config config;
	if (!config_parse_buffer(&config, (const char *)buf, len))
	{
		config_deinit(&config);
		return false;
	}

	const bool adopted = save_config_adopt(&config);
	config_deinit(&config);
	return adopted && save_file_write_current() && save_opentyrian_config();
}

bool save_slots_adopt(const Uint8 *buf, size_t len)
{
	if (buf == NULL || len == 0)
		return false;

	Config config;
	if (!config_parse_buffer(&config, (const char *)buf, len))
	{
		config_deinit(&config);
		return false;
	}

	T2KHighScoreType localHighScores[COUNTOF(t2kHighScores)][COUNTOF(t2kHighScores[0])];
	memcpy(localHighScores, t2kHighScores, sizeof(localHighScores));
	const bool adopted = save_config_adopt(&config);
	memcpy(t2kHighScores, localHighScores, sizeof(localHighScores));
	config_deinit(&config);
	return adopted && save_file_write_current();
}

// Packed save record used to give both peers identical resume state.

bool save_record_is_coop(const JE_SaveFileType *rec)
{
	return rec->dualShips == SAVE_DUAL_COOP;
}

// Dual arcade records contain two ships and scores, but no shared campaign purse.
bool save_record_is_dual_arcade(const JE_SaveFileType *rec)
{
	return rec->dualShips == SAVE_DUAL_ARCADE;
}

// Ruleset stored in each ship's loadout. Ship two is valid only for two-ship records.
uint save_record_sa_ship(const JE_SaveFileType *rec, uint p)
{
	return p == 0 ? rec->items[2] : rec->lastItems[2];
}

void save_record_pack(Uint8 *buf, const JE_SaveFileType *rec)
{
	Uint8 *p = buf;

	Uint16 u16 = SDL_SwapLE16(rec->level);
	memcpy(p, &u16, 2); p += 2;

	memcpy(p, rec->items, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
	memcpy(p, rec->lastItems, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);

	Uint64 u64 = SDL_SwapLE64((Uint64)rec->score);
	memcpy(p, &u64, 8); p += 8;
	u64 = SDL_SwapLE64((Uint64)rec->score2);
	memcpy(p, &u64, 8); p += 8;
	*p++ = rec->dualShips;
	*p++ = rec->dualPower[0];
	*p++ = rec->dualPower[1];
	*p++ = rec->dualMode[0];
	*p++ = rec->dualMode[1];

	memcpy(p, rec->levelName, sizeof(rec->levelName)); p += sizeof(rec->levelName);
	memcpy(p, rec->name, sizeof(rec->name)); p += sizeof(rec->name);

	*p++ = rec->cubes;
	*p++ = rec->power[0];
	*p++ = rec->power[1];
	*p++ = rec->episode;
	*p++ = rec->difficulty;
	*p++ = rec->secretHint;
	*p++ = rec->input1;
	*p++ = rec->input2;
	*p++ = rec->gameHasRepeated != false;
	*p++ = rec->initialDifficulty;
	*p++ = rec->autoFireSpecial != false;
	*p++ = rec->chargeSidekickAutofire;
	*p++ = rec->difficultyAdjust != false;
	*p++ = rec->cheatInfiniteSidekickAmmo != false;
	*p++ = rec->cheatInfiniteShields != false;
	*p++ = rec->cheatInfiniteArmor != false;
	*p++ = rec->expertMode != false;
	for (uint i = 0; i < COUNTOF(rec->shipColor); ++i)
	{
		*p++ = rec->shipColor[i];
		*p++ = rec->viewOpacity[i];
		*p++ = rec->viewShipOpacity[i];
		*p++ = rec->viewHpBars[i];
	}

	// All zero for a stock save.
	memcpy(p, rec->customEpFile, sizeof(rec->customEpFile));
	p += sizeof(rec->customEpFile);

	memcpy(p, rec->shipSkin, sizeof(rec->shipSkin));
	p += sizeof(rec->shipSkin);

	memcpy(p, rec->standardItems, sizeof(rec->standardItems));
	p += sizeof(rec->standardItems);

	memcpy(p, rec->standardLent, sizeof(rec->standardLent));
	p += sizeof(rec->standardLent);

	memcpy(p, rec->standardPower, sizeof(rec->standardPower));
	p += sizeof(rec->standardPower);

	memcpy(p, rec->sidekickPlain, sizeof(rec->sidekickPlain));
	p += sizeof(rec->sidekickPlain);

	assert(p - buf == SAVE_RECORD_PACKED_SIZE);
}

void save_record_unpack(JE_SaveFileType *rec, const Uint8 *buf)
{
	const Uint8 *p = buf;

	memset(rec, 0, sizeof(*rec));

	Uint16 u16;
	memcpy(&u16, p, 2); p += 2;
	rec->level = SDL_SwapLE16(u16);

	memcpy(rec->items, p, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);
	memcpy(rec->lastItems, p, sizeof(JE_PItemsType)); p += sizeof(JE_PItemsType);

	Uint64 u64;
	memcpy(&u64, p, 8); p += 8;
	rec->score = cash_clamp((Sint64)SDL_SwapLE64(u64));
	memcpy(&u64, p, 8); p += 8;
	rec->score2 = cash_clamp((Sint64)SDL_SwapLE64(u64));
	rec->dualShips = *p++;
	rec->dualPower[0] = *p++;
	rec->dualPower[1] = *p++;
	rec->dualMode[0] = *p++;
	rec->dualMode[1] = *p++;

	memcpy(rec->levelName, p, sizeof(rec->levelName)); p += sizeof(rec->levelName);
	rec->levelName[sizeof(rec->levelName) - 1] = '\0';
	memcpy(rec->name, p, sizeof(rec->name)); p += sizeof(rec->name);
	rec->name[sizeof(rec->name) - 1] = '\0';

	rec->cubes = *p++;
	rec->power[0] = *p++;
	rec->power[1] = *p++;
	rec->episode = *p++;
	rec->difficulty = *p++;
	rec->secretHint = *p++;
	rec->input1 = *p++;
	rec->input2 = *p++;
	rec->gameHasRepeated = *p++ != 0;
	rec->initialDifficulty = *p++;
	rec->autoFireSpecial = *p++ != 0;
	rec->chargeSidekickAutofire = *p++;
	rec->difficultyAdjust = *p++ != 0;
	rec->cheatInfiniteSidekickAmmo = *p++ != 0;
	rec->cheatInfiniteShields = *p++ != 0;
	rec->cheatInfiniteArmor = *p++ != 0;
	rec->expertMode = *p++ != 0;
	for (uint i = 0; i < COUNTOF(rec->shipColor); ++i)
	{
		rec->shipColor[i] = *p++;
		rec->viewOpacity[i] = *p++;
		rec->viewShipOpacity[i] = *p++;
		rec->viewHpBars[i] = *p++;
	}

	memcpy(rec->customEpFile, p, sizeof(rec->customEpFile));
	p += sizeof(rec->customEpFile);
	rec->customEpFile[sizeof(rec->customEpFile) - 1] = '\0';
	// Reject paths received over the network.
	if (rec->customEpFile[0] != '\0' && !customEpisodeFileNameValid(rec->customEpFile))
		rec->customEpFile[0] = '\0';

	memcpy(rec->shipSkin, p, sizeof(rec->shipSkin));
	p += sizeof(rec->shipSkin);

	memcpy(rec->standardItems, p, sizeof(rec->standardItems));
	p += sizeof(rec->standardItems);

	memcpy(rec->standardLent, p, sizeof(rec->standardLent));
	p += sizeof(rec->standardLent);

	memcpy(rec->standardPower, p, sizeof(rec->standardPower));
	p += sizeof(rec->standardPower);

	for (uint i = 0; i < COUNTOF(rec->sidekickPlain); ++i)
		rec->sidekickPlain[i] = *p++ != 0;

	assert(p - buf == SAVE_RECORD_PACKED_SIZE);
}
