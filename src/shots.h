/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2013  The OpenTyrian Development Team
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
#ifndef SHOTS_H
#define SHOTS_H
#include "opentyr.h"

typedef struct {
	JE_integer shotX, shotY, shotXM, shotYM, shotXC, shotYC;
	JE_boolean shotComplicated;
	JE_integer shotDevX, shotDirX, shotDevY, shotDirY, shotCirSizeX, shotCirSizeY;
	JE_byte shotTrail;
	JE_word shotGr, shotAni, shotAniMax;
	Uint8 shotDmg;
	JE_byte shotBlastFilter, chainReaction, playerNumber, aimAtEnemy, aimDelay, aimDelayMax;
	JE_byte salvoBoost;  // endless Opening Salvo perk: 1 = this shot is part of a charged volley (extra damage at collision)
	// Per-bullet Endless pierce lock. An enemy-wide lock would discard the other
	// bullets in the same volley instead of preventing repeated overlap hits.
	JE_byte pierceLock;       // sim ticks before this bullet may deal damage again
	JE_byte pierceLockCarry;  // sub-tick remainder, so the lockout can ramp in fractions of a tick
	JE_byte pierceLockPending;// largest lockout (1/100 tick) charged during the CURRENT tick, banked
	                          // until the top of the next pass; see the hit site for why
} PlayerShotDataType;

// Large enough for sustained specials and maximum-width custom weapons. Keep
// RL_ID_PSHOT_BASE + MAX_PWEAPON below RL_ID_ESHOT_BASE.
#define MAX_PWEAPON     8000
extern PlayerShotDataType playerShotData[MAX_PWEAPON + 1];
extern JE_byte shotAvail[MAX_PWEAPON];

/** Used in the shop to show weapon previews. */
void simulate_player_shots(void);

/** Points shot movement in the specified direction. Used for the turret gun. */
void player_shot_set_direction(JE_integer shot_id, uint weapon_id, JE_real direction);

/** Return the hit-test offset for Classic or Centered Hitboxes. Decodes
 * \a sprite_frame here so every caller uses the same geometry. */
void player_shot_hit_offset(JE_word sprite_frame, int *out_dx, int *out_dy);
void enemy_shot_hit_offset(JE_word sgr, JE_word animate, int *out_dx, int *out_dy);

/** Moves and draws a shot. Does \b not collide it with enemies.
 * The hit offset is that of the frame drawn this tick.
 * \return False if the shot went off-screen, true otherwise.
 */
bool player_shot_move_and_draw(
	int shot_id, bool *out_is_special,
	int *out_shotx, int *out_shoty,
	JE_integer *out_shot_damage, JE_byte *out_blast_filter,
	JE_byte *out_chain, JE_byte *out_playerNum,
	JE_word *out_special_radiusw, JE_word *out_special_radiush,
	int *out_hit_dx, int *out_hit_dy);

/** Creates a player shot. */
JE_integer player_shot_create(
	JE_word portnum, uint shot_i, JE_word px, JE_word py,
	JE_word mousex, JE_word mousey,
	JE_word wpnum, JE_byte playernum);

/** Creates the chain-reaction child of a shot that just hit, at the impact point.
 * \a salvo_boost carries the parent's endless Opening Salvo tag onto every child bullet and
 * replaces the live salvo-window test.
 */
JE_integer player_shot_create_chained(
	JE_word px, JE_word py, JE_word mousex, JE_word mousey,
	JE_word wpnum, JE_byte playernum, bool salvo_boost);

#endif // SHOTS_H
