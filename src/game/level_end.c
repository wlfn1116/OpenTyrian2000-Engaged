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
#include "level_end.h"

#include "audio.h"
#include "config.h"
#include "credits.h"
#include "custom_episode.h"
#include "debug_flags.h"
#include "endless.h"
#include "episodes.h"
#include "font.h"
#include "game_loop.h"
#include "game_state.h"
#include "helptext.h"
#include "high_scores.h"
#include "item_prices.h"
#include "keyboard.h"
#include "menus.h"
#include "mtrand.h"
#include "network.h"
#include "opentyr.h"
#include "palette.h"
#include "params.h"
#include "player.h"
#include "qa.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

// increases game difficulty based on player's total score / total of players' scores
/* Endless keeps its starting difficulty for the whole run; depth supplies the
 * difficulty curve. Restore temporary script or episode changes after each level. */
bool difficulty_adjust_active(void)
{
	return difficultyAdjust && !endlessFxActive();
}

static void adjust_difficulty(void)
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
