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
#ifndef PLAYER_SHIP_H
#define PLAYER_SHIP_H

#include "opentyr.h"
#include "player.h"
#include "sprite.h"

// One ship's Endless per-tick work; see the definition in player_ship.c.
void endlessPerShipTick(Player *this_player);
// Spend a charged Opening Salvo on the volley this ship fires this tick, before the special does.
bool endlessArmOpeningSalvoForTick(Player *this_player, JE_byte playerNum_);
void JE_SFCodes(JE_byte playerNum_, JE_integer PX_, JE_integer PY_, JE_integer mouseX_, JE_integer mouseY_);
// Resolves a movement intent (positive right and down) to the one-pixel target JE_SFCodes
// reads: the dominant axis inside the 2:1 cone, both axes (a neutral tick) outside it.
void SF_twiddleTarget(int px, int py, int dx, int dy, int *out_x, int *out_y);
// The wire form of that intent (RB_MOVE_* in rollback.h) and the direction rebuilt from it.
Uint16 rb_move_bits(int dx, int dy);
void rb_move_dir(Uint16 bits, int *out_dx, int *out_dy);

void JE_mainKeyboardInput(void);
void JE_pauseGame(void);

void JE_playerMovement(Player *this_player, JE_byte inputDevice, JE_byte playerNum, JE_word shipGr, Sprite2_array *shipGrPtr_, JE_word *mouseX, JE_word *mouseY);
void JE_mainGamePlayerFunctions(void);

// Pool slots of this tick's linked-Dragonwing aim markers (-1 = none); the
// shot draw maps them to stable render-list ids so the indicator interpolates.
extern int link_marker_slot[3];

void JE_playerCollide(Player *this_player, JE_byte playerNum);

#endif /* PLAYER_SHIP_H */
