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
#ifndef MAININT_H
#define MAININT_H

#include "config.h"
#include "opentyr.h"
#include "palette.h"
#include "player.h"
#include "sprite.h"

extern bool button[4]; // fire, left fire, right fire, mode swap

extern JE_shortint constantLastX;
extern JE_word textErase;
extern Sint64 upgradeCost;
extern Sint64 downgradeCost;
extern JE_boolean performSave;
extern JE_boolean jumpSection;
extern JE_boolean useLastBank;

extern bool pause_pressed, ingamemenu_pressed, changefire_pressed;

void JE_initPlayerData(void);
void JE_gammaCorrect_func(JE_byte *col, JE_real r);
void JE_gammaCorrect(Palette *colorBuffer, JE_byte gamma);
JE_boolean JE_gammaCheck(void);
void JE_nextEpisode(void);

// One-line "the other machine is not with us yet" panel over a frozen gameplay frame. Draws into
// whatever VGAScreen points at, so callers must have pointed it at VGAScreenSeg first.
void JE_drawNetworkNotice(const char *text);

// Whether the score-based difficulty drift between levels runs; false for the whole of an Endless
// run, which stays on the rung it launched with. Public so the unit suite can pin that.
bool difficulty_adjust_active(void);
void adjust_difficulty(void);

bool load_next_demo(void);
bool replay_demo_keys(void);

Sint64 weapon_upgrade_cost(Sint64 base_cost, unsigned int power);
Sint64 JE_getCost(JE_byte itemType, JE_word itemNum);
Sint64 JE_getValue(JE_byte itemType, JE_word itemNum);
Sint64 JE_totalScore(const Player *);

void JE_outCharGlow(JE_word x, JE_word y, const char *s);

void JE_endLevelAni(void);
void JE_drawCube(SDL_Surface * screen, JE_word x, JE_word y, JE_byte filter, JE_byte brightness);
bool str_pop_int(char *str, int *val);
const char *JE_getName(JE_byte pnum);
void JE_playerScoreLabel(JE_byte pnum, char *out, size_t outSize);

#endif /* MAININT_H */
