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
#include "mainint.h"

#include "config.h"
#include "credits.h"
#include "custom_episode.h"
#include "endless.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "game_menu.h"
#include "helptext.h"
#include "high_scores.h"
#include "ingame_menu.h"
#include "keyboard.h"
#include "loudness.h"
#include "lvlmast.h"
#include "menus.h"
#include "mtrand.h"
#include "network.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyr.h"
#include "palette.h"
#include "params.h"
#include "pcxmast.h"
#include "player.h"
#include "qa.h"
#include "sndmast.h"
#include "sprite.h"
#include "touch_ui.h"
#include "tyrian2.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backgrnd.h"
#include "config.h"
#include "crashlog.h"
#include "debug_menu.h"
#include "editship.h"
#include "endless.h"
#include "custom_episode.h"
#include "enemy_death.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "game_menu.h"
#include "health_bars.h"
#include "helptext.h"
#include "hud.h"
#include "joystick.h"
#include "keyboard.h"
#include "lds_play.h"
#include "loudness.h"
#include "menus.h"
#include "mouse.h"
#include "mtrand.h"
#include "musmast.h"
#include "net_savexfer.h"
#include "net_style.h"
#include "network.h"
#include "nortsong.h"
#include "nortvars.h"
#include "opentyr.h"
#include "palette.h"
#include "params.h"
#include "pcxmast.h"
#include "picload.h"
#include "player.h"
#include "qa.h"
#include "render_list.h"
#include "rollback.h"
#include "specials.h"
#include "touch_ui.h"
#include "net_rollback.h"
#include "shots.h"
#include "sndmast.h"
#include "sim_math.h"
#include "sprite.h"
#include "console_platform.h"
#include "tyrian2.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"
#include "lvlmast.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>

bool button[4];

JE_shortint constantLastX;
JE_word textErase;
Sint64 upgradeCost;
Sint64 downgradeCost;
JE_boolean performSave;
JE_boolean jumpSection;
JE_boolean useLastBank; /* Use the last 16 colors for DisplayText. */

bool pause_pressed = false, ingamemenu_pressed = false, changefire_pressed = false;

void JE_outCharGlow(JE_word x, JE_word y, const char *s)
{
	JE_integer maxloc, loc, z;
	JE_shortint glowcol[60]; /* [1..60] */
	JE_shortint glowcolc[60]; /* [1..60] */
	JE_word textloc[60]; /* [1..60] */
	JE_byte bank;

	setDelay2(1);

	bank = (warningRed) ? 7 : ((useLastBank) ? 15 : 14);

	if (s[0] == '\0')
		return;

	if (frameCountMax == 0)
	{
		JE_textShade(VGAScreen, x, y, s, bank, 0, PART_SHADE);
		JE_showVGA();
	}
	else
	{
		maxloc = strlen(s);
		for (z = 0; z < 60; z++)
		{
			glowcol[z] = -8;
			glowcolc[z] = 1;
		}

		loc = x;
		for (z = 0; z < maxloc; z++)
		{
			textloc[z] = loc;

			int sprite_id = font_ascii[(unsigned char)s[z]];

			if (s[z] == ' ')
				loc += 6;
			else if (sprite_id != -1)
				loc += sprite(TINY_FONT, sprite_id)->width + 1;
		}

		for (loc = 0; (unsigned)loc < strlen(s) + 28; loc++)
		{
			if (!ESCPressed)
			{
				setDelay(frameCountMax);

				NETWORK_KEEP_ALIVE();

				int sprite_id = -1;

				for (z = loc - 28; z <= loc; z++)
				{
					if (z >= 0 && z < maxloc)
					{
						sprite_id = font_ascii[(unsigned char)s[z]];

						if (sprite_id != -1)
						{
							blit_sprite_hv(VGAScreen, textloc[z], y, TINY_FONT, sprite_id, bank, glowcol[z]);

							glowcol[z] += glowcolc[z];
							if (glowcol[z] > 9)
								glowcolc[z] = -1;
						}
					}
				}
				if (sprite_id != -1 && --z < maxloc)
					blit_sprite_dark(VGAScreen, textloc[z] + 1, y + 1, TINY_FONT, sprite_id, true);

				if (JE_anyButton())
					frameCountMax = 0;

				do
				{
					if (levelWarningDisplay)
						JE_updateWarning(VGAScreen);

					SDL_Delay(1);
				} while (!(getDelayTicks() == 0 || ESCPressed));

				JE_showVGA();
			}
		}
	}
}

// cost to upgrade a weapon power from power-1 (where power == 0 indicates an unupgraded weapon)
Sint64 weapon_upgrade_cost(Sint64 base_cost, unsigned int power)
{
	assert(power <= 11);

	unsigned int temp = 0;

	// 0 1 3 6 10 15 21 29 ...
	for (; power > 0; power--)
		temp += power;

	return base_cost * temp;
}

/* Prices are Sint64 like the wallets they are checked against; the widest product here (a 16-bit
 * base cost, the power ladder, the expert and endless multipliers) stays far below 2^63. */
Sint64 JE_getCost(JE_byte itemType, JE_word itemNum)
{
	Sint64 cost = 0;

	switch (itemType)
	{
	case 2:
		// ships[] stops at SHIP_DRAGONWING, but only ids above 90 are extra ships. Test the
		// array's own bound, not the extra-ship one, or an id in between reads past the end.
		cost = (itemNum > SHIP_DRAGONWING) ? 100 : ships[itemNum].cost;
		break;
	case 3:
	case 4:
		cost = weaponPort[itemNum].cost;

		const uint port = itemType - 3,
			item_power = player[JE_shopPlayerIndex()].items.weapon[port].power - 1;

		downgradeCost = weapon_upgrade_cost(cost, item_power);
		upgradeCost = weapon_upgrade_cost(cost, item_power + 1);
		break;
	case 5:
		cost = shields[itemNum].cost;
		break;
	case 6:
		cost = powerSys[itemNum].cost;
		break;
	case 7:
	case 8:
		cost = options[itemNum].cost;
		break;
	}

	if (expertMode)
	{
		// purchase price scales with the shop knob; power upgrades with their own knob
		cost = cost * expertShopCostMult;

		if (itemType == 3 || itemType == 4)
		{
			downgradeCost = downgradeCost * expertUpgradeCostMult;
			upgradeCost = upgradeCost * expertUpgradeCostMult;
		}
	}

	if (endlessMode)
	{
		// Endless: shop prices inflate with depth (+19%/level, capped 100x) since income also
		// scales; the first five zones ramp at half slope.
		int pct;
		if (endlessRunDepth < 5)
			pct = 100 + endlessRunDepth * 19 / 2;                 // first 5 zones: gentle half-slope
		else
			pct = 100 + 4 * 19 / 2 + (endlessRunDepth - 4) * 19;  // then full +19%/depth (continuous at zone 5)
		if (pct > 10000)
			pct = 10000;                            // cap at 100x
		pct += endlessShopTaxPercent();             // Loan Shark: a permanent debt tax on top of the depth cap
		if (endlessActiveMods & ENDLESS_MOD_FAVOR)  // Merchant's Favor: the outpost slashes prices
			pct = pct * 65 / 100;
		// Financier perk: better terms, in basis points because the per-stack cut isn't a whole
		// percent. Multiplies like Favor does, so the two compound instead of one overriding the other.
		pct = pct * endlessPerkShopCostBp() / 10000;

		cost = cost * pct / 100;

		if (itemType == 3 || itemType == 4)
		{
			downgradeCost = downgradeCost * pct / 100;
			upgradeCost   = upgradeCost * pct / 100;
		}
	}

	return cost;
}

Sint64 JE_totalScore(const Player *this_player)
{
	Sint64 temp = this_player->cash;

	temp += JE_getValue(2, this_player->items.ship);
	temp += JE_getValue(3, this_player->items.weapon[FRONT_WEAPON].id);
	temp += JE_getValue(4, this_player->items.weapon[REAR_WEAPON].id);
	temp += JE_getValue(5, this_player->items.shield);
	temp += JE_getValue(6, this_player->items.generator);
	temp += JE_getValue(7, this_player->items.sidekick[LEFT_SIDEKICK]);
	temp += JE_getValue(8, this_player->items.sidekick[RIGHT_SIDEKICK]);

	return temp;
}

Sint64 JE_getValue(JE_byte itemType, JE_word itemNum)
{
	Sint64 value = 0;

	switch (itemType)
	{
	case 2:
		// Extra ships (id above 90) have no shop row and sell for nothing.
		if (itemNum <= SHIP_DRAGONWING)
			value = ships[itemNum].cost;
		break;
	case 3:
	case 4:;
		const Sint64 base_value = weaponPort[itemNum].cost;

		const uint port = itemType - 3;
		const uint item_power = player[JE_shopPlayerIndex()].items.weapon[port].power - 1;

		value = base_value;
		for (unsigned int i = 1; i <= item_power; ++i)
			value += weapon_upgrade_cost(base_value, i);
		break;
	case 5:
		value = shields[itemNum].cost;
		break;
	case 6:
		value = powerSys[itemNum].cost;
		break;
	case 7:
	case 8:
		value = options[itemNum].cost;
		break;
	}

	return value;
}

void JE_nextEpisode(void)
{
	strcpy(lastLevelName, "Completed");

	// The base episode does not own a custom run's high score.
	if (episodeNum == initial_episode_num && !gameHasRepeated && !isNetworkGame && !constantPlay &&
	    !endlessMode && !customEpisodeActive())
	{
		JE_highScoreCheck();
	}

	// Online co-op Campaign has its own board and no name-entry dialog; it owns the conditions
	// for a record, so this is the one place that offers it an episode.
	coopCampaignScoreNote();

	/* Verify the co-op Campaign record inputs at the episode boundary. */
	if (qa_net_gameplay_ticks > 0 && coopCampaignMode)
	{
		const int e = initial_episode_num - 1;
		printf("NET CAMPAIGN RECORD player=%u start=%u episode=%u cash=%lld recorded=%lld\n",
		       thisPlayerNum, (unsigned)initial_episode_num, (unsigned)episodeNum,
		       (long long)(player[0].cash + player[1].cash),
		       (e >= 0 && e < COOP_CAMPAIGN_SCORE_EPISODES) ? (long long)coopCampaignScores[e].score : -1LL);
		fflush(stdout);
	}

	unsigned int newEpisode = JE_findNextEpisode();

	if (jumpBackToEpisode1)
	{
		// Roll before the credits. Their animation consumes a variable number of random values, but
		// both peers are still on the same stream here.
		const bool superCarrot = (mt_rand() % 6) == 0;

		if (episodeNum > 2 &&
			!constantPlay && !endlessMode)
		{
			JE_playCredits();
		}

		// randomly give player the SuperCarrot
		if (superCarrot)
		{
			// Every ship flying its own arsenal gets the whole joke. Vanilla's lone player-two
			// line below is the linked pair's shared one, where the rear bay IS ship two.
			for (uint p = 0; p < (dual_ship_mode() ? COUNTOF(player) : 1u); ++p)
			{
				player[p].items.ship = 2;                      // SuperCarrot
				player[p].items.weapon[FRONT_WEAPON].id = 23;  // Banana Blast
				player[p].items.weapon[REAR_WEAPON].id = 24;   // Banana Blast Rear

				for (uint i = 0; i < COUNTOF(player[p].items.weapon); ++i)
					player[p].items.weapon[i].power = 1;

				player[p].last_items = player[p].items;
			}

			if (!dual_ship_mode())
				player[1].items.weapon[REAR_WEAPON].id = 24;   // Banana Blast Rear
		}
	}

	if (newEpisode != episodeNum)
		JE_initEpisode(newEpisode);

	gameLoaded = true;
	mainLevel = FIRST_LEVEL;
	saveLevel = FIRST_LEVEL;

	play_song(26);

	JE_clr256(VGAScreen);
	memcpy(colors, palettes[6-1], sizeof(colors));

	const char *episodeBanner = customEpisodeActive()
		? customEpisodeActiveTitle() : episode_name[episodeNum];
	JE_dString(VGAScreen, JE_fontCenter(episodeBanner, SMALL_FONT_SHAPES), 130, episodeBanner, SMALL_FONT_SHAPES);
	JE_dString(VGAScreen, JE_fontCenter(miscText[5-1], SMALL_FONT_SHAPES), 185, miscText[5-1], SMALL_FONT_SHAPES);

	const bool waitForEpisodeBanner = !constantPlay && qa_net_gameplay_ticks == 0;
	if (waitForEpisodeBanner)
		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	// A press surviving the fades would drop the banner on its first JE_anyButton.
	wait_noinput(true, true, true);
	newkey = newmouse = false;
	// A gameplay wire test has no player to press past the episode banner.
	if (waitForEpisodeBanner)
	{
		do
		{
			touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
			touch_ui_idle_repaint();
			NETWORK_KEEP_ALIVE();

			SDL_Delay(16);
		} while (!JE_anyButton());
	}

	fade_black(15);
}

void JE_initPlayerData(void)
{
	/* JE: New Game Items/Data */
	extraShipReturnReset();

	player[0].items.ship = 1;                     // USP Talon
	player[0].items.weapon[FRONT_WEAPON].id = 1;  // Pulse Cannon
	player[0].items.weapon[REAR_WEAPON].id = 0;   // None
	player[0].items.shield = 4;                   // Gencore High Energy Shield
	player[0].items.generator = 2;                // Advanced MR-12
	for (uint i = 0; i < COUNTOF(player[0].items.sidekick); ++i)
		player[0].items.sidekick[i] = 0;          // None
	player[0].items.special = 0;                  // None
	// Which Super Arcade ruleset a ship flies now lives on the ship (player_sa_ship), because
	// online Super Arcade gives the two players different ones.
	player[0].items.super_arcade_mode = SA_NONE;

	player[0].last_items = player[0].items;

	player[1].items = player[0].items;
	player[1].items.weapon[REAR_WEAPON].id = 15;  // Vulcan Cannon
	player[1].items.sidekick_level = 101;         // 101, 102, 103
	player[1].items.sidekick_series = 0;          // None

	gameHasRepeated = false;
	onePlayerAction = false;
	superArcadeMode = SA_NONE;
	superTyrian = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	arcadeSeparateMode = false;
	timedBattleMode = false;
	endlessMode = false;

	secretHint = (mt_rand() % 3) + 1;

	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		for (uint i = 0; i < COUNTOF(player->items.weapon); ++i)
		{
			player[p].items.weapon[i].power = 1;
		}

		player[p].weapon_mode = 1;
		player[p].armor = player[p].hull_armor = ships[player[p].items.ship].dmg;

		player[p].is_dragonwing = (p == 1);
		player[p].lives = &player[p].items.weapon[player_lives_port(p)].power;

	}

	mainLevel = FIRST_LEVEL;
	saveLevel = FIRST_LEVEL;

	strcpy(lastLevelName, miscText[19]);
}

void JE_gammaCorrect_func(JE_byte *col, JE_real r)
{
	int temp = roundf(*col * r);
	if (temp > 255)
	{
		temp = 255;
	}
	*col = temp;
}

void JE_gammaCorrect(Palette *colorBuffer, JE_byte gamma)
{
	int x;
	JE_real r = 1 + (JE_real)gamma / 10;

	for (x = 0; x < 256; x++)
	{
		JE_gammaCorrect_func(&(*colorBuffer)[x].r, r);
		JE_gammaCorrect_func(&(*colorBuffer)[x].g, r);
		JE_gammaCorrect_func(&(*colorBuffer)[x].b, r);
	}
}

JE_boolean JE_gammaCheck(void)
{
	bool temp = keysactive[SDL_SCANCODE_F11] != 0;
	if (temp)
	{
		keysactive[SDL_SCANCODE_F11] = false;
		newkey = false;
		gammaCorrection = (gammaCorrection + 1) % 4;
		memcpy(colors, palettes[pcxpal[3-1]], sizeof(colors));
		JE_gammaCorrect(&colors, gammaCorrection);
		set_palette(colors, 0, 255);
	}
	return temp;
}

void JE_drawNetworkNotice(const char *text)
{
	// Drawn into VGAScreenSeg, so this is display space: the playfield is x in [0,
	// PLAYFIELD_WIDTH) and y in [0, 184), with the HUD to the right of it.
	const int playfieldRows = 184;
	const int panelW = MIN(JE_textWidth(text, normal_font) + 28, PLAYFIELD_WIDTH - 16);
	const int panelH = 26;

	const int px0 = (PLAYFIELD_WIDTH - panelW) / 2, px1 = px0 + panelW;
	const int py0 = (playfieldRows - panelH) / 2, py1 = py0 + panelH;

	JE_barShade(VGAScreen, px0, py0, px1, py1);
	JE_barShade(VGAScreen, px0 + 2, py0 + 2, px1 - 2, py1 - 2);
	JE_dStringOutlined(VGAScreen, (px0 + px1) / 2 - JE_textWidth(text, normal_font) / 2, py0 + 7,
	                   text, SMALL_FONT_SHAPES);
}

// increases game difficulty based on player's total score / total of players' scores
/* Endless keeps its starting difficulty for the whole run; depth supplies the
 * difficulty curve. Restore temporary script or episode changes after each level. */
bool difficulty_adjust_active(void)
{
	return difficultyAdjust && !endlessFxActive();
}

void adjust_difficulty(void)
{
	const float score_multiplier[10] =
	{
		0,     // Wimp  (doesn't exist)
		0.4f,  // Easy
		0.8f,  // Normal
		1.3f,  // Hard
		1.6f,  // Impossible
		2,     // Insanity
		2,     // Suicide
		3,     // Maniacal
		3,     // Lord of Game
		3,     // Nortaneous
	};

	assert(initialDifficulty > 0 && initialDifficulty < 10);

	// Co-op Campaign scores each loadout plus cash, then averages the pair.
	const bool linkedScoring = twoPlayerMode && !coop_mode_active();

	const Sint64 score = linkedScoring ? player[0].cash + player[1].cash
	                   : twoPlayerMode ? (JE_totalScore(&player[0]) + JE_totalScore(&player[1])) / 2
	                                   : JE_totalScore(&player[0]),
	             adjusted_score = (Sint64)roundf(score * score_multiplier[initialDifficulty]);

	uint new_difficulty = 0;

	if (linkedScoring)
	{
		if (adjusted_score < 10000)
			new_difficulty = DIFFICULTY_EASY;
		else if (adjusted_score < 20000)
			new_difficulty = DIFFICULTY_NORMAL;
		else if (adjusted_score < 50000)
			new_difficulty = DIFFICULTY_HARD;
		else if (adjusted_score < 80000)
			new_difficulty = DIFFICULTY_IMPOSSIBLE;
		else if (adjusted_score < 125000)
			new_difficulty = DIFFICULTY_INSANITY;
		else if (adjusted_score < 200000)
			new_difficulty = DIFFICULTY_SUICIDE;
		else if (adjusted_score < 400000)
			new_difficulty = DIFFICULTY_MANIACAL;
		else if (adjusted_score < 600000)
			new_difficulty = DIFFICULTY_LORD_OF_GAME;
		else
			new_difficulty = DIFFICULTY_NORTANEOUS;
	}
	else
	{
		if (adjusted_score < 40000)
			new_difficulty = DIFFICULTY_EASY;
		else if (adjusted_score < 70000)
			new_difficulty = DIFFICULTY_NORMAL;
		else if (adjusted_score < 150000)
			new_difficulty = DIFFICULTY_HARD;
		else if (adjusted_score < 300000)
			new_difficulty = DIFFICULTY_IMPOSSIBLE;
		else if (adjusted_score < 600000)
			new_difficulty = DIFFICULTY_INSANITY;
		else if (adjusted_score < 1000000)
			new_difficulty = DIFFICULTY_SUICIDE;
		else if (adjusted_score < 2000000)
			new_difficulty = DIFFICULTY_MANIACAL;
		else if (adjusted_score < 3000000)
			new_difficulty = DIFFICULTY_LORD_OF_GAME;
		else
			new_difficulty = DIFFICULTY_NORTANEOUS;
	}

	difficultyLevel = MAX((unsigned)difficultyLevel, new_difficulty);
}

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

	fread_u8_die(&lvlFileNum, 1, demo_file);

	fread_u8_die(&player[0].items.weapon[FRONT_WEAPON].id,  1, demo_file);
	fread_u8_die(&player[0].items.weapon[REAR_WEAPON].id,   1, demo_file);
	fread_u8_die(&player[0].items.super_arcade_mode,        1, demo_file);
	fread_u8_die(&player[0].items.sidekick[LEFT_SIDEKICK],  1, demo_file);
	fread_u8_die(&player[0].items.sidekick[RIGHT_SIDEKICK], 1, demo_file);
	fread_u8_die(&player[0].items.generator,                1, demo_file);

	fread_u8_die(&player[0].items.sidekick_level,           1, demo_file); // could probably ignore
	fread_u8_die(&player[0].items.sidekick_series,          1, demo_file); // could probably ignore

	fread_u8_die(&initial_episode_num,                      1, demo_file); // could probably ignore

	fread_u8_die(&player[0].items.shield,                   1, demo_file);
	fread_u8_die(&player[0].items.special,                  1, demo_file);
	fread_u8_die(&player[0].items.ship,                     1, demo_file);

	for (uint i = 0; i < 2; ++i)
		fread_u8_die(&player[0].items.weapon[i].power,      1, demo_file);

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

void JE_endLevelAni(void)
{
	JE_word x, y;
	JE_byte temp;
	char tempStr[256];

	Sint8 i;

	Sint64 endlessInterest = 0, endlessBonus = 0;  // endless: the level-clear payout, shown below

	if (difficulty_adjust_active())
		adjust_difficulty();

	for (uint p = 0; p < (dual_ship_mode() ? COUNTOF(player) : 1u); ++p)
		player[p].last_items = player[p].items;
	strcpy(lastLevelName, levelName);

	JE_wipeKey();
	frameCountMax = 4;
	textGlowFont = SMALL_FONT_SHAPES;

	SDL_Color white = { 255, 255, 255 };
	set_colors(white, 254, 254);

	if (!levelTimer || levelTimerCountdown > 0 || !(episodeNum == 4))
		JE_playSampleNum(V_LEVEL_END);
	else
		play_song(21);

	if (bonusLevel)
	{
		JE_outTextGlow(VGAScreenSeg, 20, 20, miscText[17-1]);
	}
	else if (all_players_alive())
	{
		sprintf(tempStr, "%s %s", miscText[27-1], levelName); // "Completed"
		JE_outTextGlow(VGAScreenSeg, 20, 20, tempStr);
	}
	else
	{
		sprintf(tempStr, "%s %s", miscText[62-1], levelName); // "Exiting"
		JE_outTextGlow(VGAScreenSeg, 20, 20, tempStr);
	}

	// Endless banks the level-clear payout now, so the cash total printed just below already
	// includes it (the breakdown is shown further down, where data cubes would normally be).
	if (endlessMode)
		endlessApplyLevelPayout(&endlessInterest, &endlessBonus);

	if (twoPlayerMode)
	{
		for (uint i = 0; i < 2; ++i)
		{
			char label[80];
			JE_playerScoreLabel((JE_byte)(i + 1), label, sizeof(label));
			snprintf(tempStr, sizeof(tempStr), "%s %lld", label, (long long)player[i].cash);
			JE_outTextGlow(VGAScreenSeg, 30, 50 + 20 * i, tempStr);
		}
	}
	else
	{
		sprintf(tempStr, "%s %lld", miscText[28-1], (long long)player[0].cash);
		JE_outTextGlow(VGAScreenSeg, 30, 50, tempStr);
	}

	// Time left is one clock for the whole level, so an online pair that beat it beat it together
	// and both purses take the same bonus.
	const int timeBonus = timedBattleMode ? (levelTimerCountdown / 10) * 100 : 0;
	if (timedBattleMode)
	{
		for (uint p = 0; p < (dual_ship_mode() ? COUNTOF(player) : 1u); ++p)
			player_add_cash(&player[p], timeBonus);

		if (!dual_ship_mode())
		{
			sprintf(tempStr, "%s %d", miscTextB[6], timeBonus);
			JE_outTextGlow(VGAScreenSeg, 40, 75, tempStr);
		}
	}

	temp = (totalEnemy == 0) ? 0 : roundf(enemyKilled * 100 / totalEnemy);
	sprintf(tempStr, "%s %d%%", miscText[63-1], temp);
	JE_outTextGlow(VGAScreenSeg, 40, 90, tempStr);

	if (timedBattleMode && dual_ship_mode())
	{
		sprintf(tempStr, "%s %d", miscTextB[6], timeBonus);
		JE_outTextGlow(VGAScreenSeg, 40, 108, tempStr);
	}

	if (timedBattleMode && dual_ship_mode())
	{
		// Timed Battle shows one named life count per racer.
		for (uint p = 0; p < COUNTOF(player); ++p)
		{
			JE_playSampleNum(S_ITEM);

			x = *player[p].lives * 1000;
			snprintf(tempStr, sizeof(tempStr), "%s %s %d",
			         JE_getName((JE_byte)(p + 1)), miscTextB[7], x);
			JE_outTextGlow(VGAScreenSeg, 30, 128 + 18 * (int)p, tempStr);
			player_add_cash(&player[p], x);
		}
	}
	else if (timedBattleMode)
	{
		// The ships parade across a row of their own: at eleven lives they run past x=200, which
		// is where the total used to sit beside them, and the two ended up drawn through each other.
		for (temp = 1; temp <= *player[0].lives; temp++)
		{
			JE_playSampleNum(S_ITEM);
			x = 20 + 15 * temp;
			y = 112;

			for (i = -15; i <= 10; i++)
			{
				setDelay(frameCountMax);

				blit_sprite_hv(VGAScreenSeg, x, y, OPTION_SHAPES, 46, 0x9, i);

				if (JE_anyButton())
					frameCountMax = 0;

				JE_showVGA();

				wait_delay();
			}
			for (i = 10; i >= 0; i--)
			{
				setDelay(frameCountMax);

				blit_sprite_hv(VGAScreenSeg, x, y, OPTION_SHAPES, 46, 0x9, i);

				if (JE_anyButton())
					frameCountMax = 0;

				JE_showVGA();

				wait_delay();
			}
		}
		x = *player[0].lives * 1000;
		sprintf(tempStr, "%s %d", miscTextB[7], x);
		JE_outTextGlow(VGAScreenSeg, 40, 143, tempStr);
		player_add_cash(&player[0], x);
	}
	else if (endlessMode)
	{
		// Endless earns cash, not data cubes; show the clear payout just banked above. No '+' or
		// parentheses: SMALL_FONT_SHAPES silently drops those glyphs.
		char payStr[64];
		snprintf(payStr, sizeof(payStr), "Zone Bonus:  %lld", (long long)endlessBonus);
		JE_outTextGlow(VGAScreenSeg, 30, 120, payStr);
		if (endlessInterest > 0)
		{
			snprintf(payStr, sizeof(payStr), "Bank Interest:  %lld", (long long)endlessInterest);
			JE_outTextGlow(VGAScreenSeg, 30, 138, payStr);
		}
	}
	else if (!arcade_rules_active())
	{
		JE_outTextGlow(VGAScreenSeg, 30, 120, miscText[4-1]);   /*Cubes*/

		if (cubeMax > 0)
		{
			if (cubeMax > 4)
				cubeMax = 4;

			if (frameCountMax != 0)
				frameCountMax = 1;

			for (temp = 1; temp <= cubeMax; temp++)
			{
				NETWORK_KEEP_ALIVE();

				JE_playSampleNum(S_ITEM);
				x = 20 + 30 * temp;
				y = 135;
				JE_drawCube(VGAScreenSeg, x, y, 9, 0);
				JE_showVGA();

				for (i = -15; i <= 10; i++)
				{
					setDelay(frameCountMax);

					blit_sprite_hv(VGAScreenSeg, x, y, OPTION_SHAPES, 25, 0x9, i);

					if (JE_anyButton())
						frameCountMax = 0;

					JE_showVGA();

					wait_delay();
				}
				for (i = 10; i >= 0; i--)
				{
					setDelay(frameCountMax);

					blit_sprite_hv(VGAScreenSeg, x, y, OPTION_SHAPES, 25, 0x9, i);

					if (JE_anyButton())
						frameCountMax = 0;

					JE_showVGA();

					wait_delay();
				}
			}
		}
		else
		{
			JE_outTextGlow(VGAScreenSeg, 50, 135, miscText[15-1]);
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
	temp2 = twoPlayerMode ? 150 : 160;
	// An online battle stacks a time bonus and two ship-bonus rows under the two scores, so the
	// prompt drops to where Endless keeps its own, below its payout block.
	if (timedBattleMode && dual_ship_mode())
		temp2 = 168;
	// Leave room for two scores and the Endless payout block.
	if (endlessMode)
	{
		temp2 = 168;
		JE_outTextGlow(VGAScreenSeg, JE_fontCenter(miscText[5-1], SMALL_FONT_SHAPES), temp2,
		               miscText[5-1]);
	}
	else
		JE_outTextGlow(VGAScreenSeg, 90, temp2, miscText[5-1]);

	// A gameplay wire test has no player to press past the level-complete screen.
	if (!constantPlay && qa_net_gameplay_ticks == 0)
	{
		do
		{
			touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
			touch_ui_idle_repaint();
			setDelay(1);

			NETWORK_KEEP_ALIVE();

			wait_delay();
		} while (!(JE_anyButton() || (frameCountMax == 0 && temp == 1)));
	}

	wait_noinput(false, false, true); // debounce the dismissing input

	fade_black(15);
	JE_clr256(VGAScreen);
}

void JE_drawCube(SDL_Surface * screen, JE_word x, JE_word y, JE_byte filter, JE_byte brightness)
{
	blit_sprite_dark(screen, x + 4, y + 4, OPTION_SHAPES, 25, false);
	blit_sprite_dark(screen, x + 3, y + 3, OPTION_SHAPES, 25, false);
	blit_sprite_hv(screen, x, y, OPTION_SHAPES, 25, filter, brightness);
}

bool str_pop_int(char *str, int *val)
{
	bool success = false;

	char buf[256];
	assert(strlen(str) < sizeof(buf));

	// grab the value from str
	char *end;
	*val = strtol(str, &end, 10);

	if (end != str)
	{
		success = true;

		// shift the rest to the beginning
		strcpy(buf, end);
		strcpy(str, buf);
	}

	return success;
}

// Per-slot lookup: an unset nickname must fall back to "Player N" for that slot, never to the
// other side's nickname, or the named player's nick shows on both slots for the unnamed player.
const char *JE_getName(JE_byte pnum)
{
	if (pnum == thisPlayerNum)
	{
		if (network_player_name[0] != '\0')
			return network_player_name;
	}
	else if (network_opponent_name[0] != '\0')
	{
		return network_opponent_name;
	}

	return miscText[47 + pnum];
}

// Replace a leading "Player N" score label with that player's nickname.
void JE_playerScoreLabel(JE_byte pnum, char *out, size_t outSize)
{
	const char *const label = miscText[39 + pnum];   // "Player N Score:"
	const char *const who   = miscText[47 + pnum];   // "Player N"
	const size_t who_len = strlen(who);

	if (isNetworkGame && strncmp(label, who, who_len) == 0)
		snprintf(out, outSize, "%s%s", JE_getName(pnum), label + who_len);
	else
		snprintf(out, outSize, "%s", label);
}
