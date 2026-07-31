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
extern ulong upgradeCost;
extern ulong downgradeCost;
extern JE_boolean performSave;
extern JE_boolean jumpSection;
extern JE_boolean useLastBank;

extern bool pause_pressed, ingamemenu_pressed, changefire_pressed;

/*void JE_textMenuWait(JE_word waittime, JE_boolean dogamma);*/

void JE_drawTextWindow(const char *text);
void JE_drawTextWindowSplit(const char *left, const char *right, int right_x);
void JE_initPlayerData(void);
void JE_highScoreScreen(void);
void JE_gammaCorrect_func(JE_byte *col, JE_real r);
void JE_gammaCorrect(Palette *colorBuffer, JE_byte gamma);
JE_boolean JE_gammaCheck(void);
/* void JE_textMenuWait(JE_word *waitTime, JE_boolean doGamma); /!\ In setup.h */
void JE_nextEpisode(void);
void JE_helpSystem(JE_byte startTopic);
void JE_doInGameSetup(void);
JE_boolean JE_inGameSetup(void);
void JE_debugMenu(bool center);
bool JE_extraMenu(void);
void JE_inGameHelp(void);
void JE_sortHighScores(void);
void JE_highScoreCheck(void);
void adjust_difficulty(void);

bool load_next_demo(void);
bool replay_demo_keys(void);

void JE_SFCodes(JE_byte playerNum_, JE_integer PX_, JE_integer PY_, JE_integer mouseX_, JE_integer mouseY_);

long weapon_upgrade_cost(long base_cost, unsigned int power);
ulong JE_getCost(JE_byte itemType, JE_word itemNum);
JE_longint JE_getValue(JE_byte itemType, JE_word itemNum);
ulong JE_totalScore(const Player *);

void JE_drawPortConfigButtons(void);
void JE_outCharGlow(JE_word x, JE_word y, const char *s);

void JE_playCredits(void);
void JE_endLevelAni(void);
void JE_drawCube(SDL_Surface * screen, JE_word x, JE_word y, JE_byte filter, JE_byte brightness);
void JE_handleChat(void);
bool str_pop_int(char *str, int *val);
bool JE_loadScreen(void);
void JE_operation(JE_byte slot);
void JE_inGameDisplays(void);

/* Bottom-band HUD layout, in playfield (game_screen) space.
 *
 * Several readouts want the bottom corners, so they claim space in a fixed precedence and
 * everything below yields to everything above it -- which keeps the resolution acyclic:
 *   1. the scores          fixed anchors, never move
 *   2. the FPS counter     stacks above player 2's score
 *   3. the boss bars       clear 1 and 2 (these helpers)
 *   4. the endless readout clears 1, 2 and 3
 */
int hud_fps_row(void);            // text row the FPS counter occupies
int hud_bottom_band_top(void);    // topmost row the scores/FPS claim anywhere across the width
int hud_bottom_right_top(void);   // ...and in the bottom-right corner alone

// Horizontal extent of the top corner clusters (name label, lives row, special-weapon icon),
// so a centred TOP boss bar stops short of them instead of being clipped to a legacy constant.
int hud_top_left_right_edge(void);
int hud_top_right_left_edge(void);
void JE_mainKeyboardInput(void);
void JE_pauseGame(void);

void JE_playerMovement(Player *this_player, JE_byte inputDevice, JE_byte playerNum, JE_word shipGr, Sprite2_array *shipGrPtr_, JE_word *mouseX, JE_word *mouseY);
void JE_mainGamePlayerFunctions(void);

// Pool slots of this tick's linked-Dragonwing aim markers (-1 = none); the
// shot draw maps them to stable render-list ids so the indicator interpolates.
extern int link_marker_slot[3];
const char *JE_getName(JE_byte pnum);

void JE_playerCollide(Player *this_player, JE_byte playerNum);

#endif /* MAININT_H */
