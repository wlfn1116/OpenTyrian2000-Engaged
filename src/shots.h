/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2013  The OpenTyrian Development Team
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
#ifndef SHOTS_H
#define SHOTS_H
#include "opentyr.h"
#include "varz.h"

typedef struct {
	JE_integer shotX, shotY, shotXM, shotYM, shotXC, shotYC;
	JE_boolean shotComplicated;
	// Palette bank the sprite is drawn in, 0 for its own colours: a returned elite or champion bullet
	// keeps its tier's. Sits in the alignment hole after shotComplicated, so the layout is unchanged.
	JE_byte tint;
	JE_integer shotDevX, shotDirX, shotDevY, shotDirY, shotCirSizeX, shotCirSizeY;
	JE_byte shotTrail;
	JE_word shotGr, shotAni, shotAniMax;
	Uint8 shotDmg;
	// aimDelayMax carries the steering interval in SHOT_AIM_DELAY_MASK; SHOT_AIM_GUIDANCE on top marks
	// a shot the endless Guidance Package steers, which aims at screen x and retargets (see shots.c).
	JE_byte shotBlastFilter, chainReaction, playerNumber, aimAtEnemy, aimDelay, aimDelayMax;
	JE_byte salvoBoost;  // Opening Salvo charged-volley tag.
	// Per-bullet Endless pierce lock. An enemy-wide lock would discard the other
	// bullets in the same volley instead of preventing repeated overlap hits.
	JE_byte pierceLock;       // sim ticks before this bullet may deal damage again
	JE_byte pierceLockCarry;  // sub-tick remainder, so the lockout can ramp in fractions of a tick
	JE_byte pierceLockPending;// largest lockout (1/100 tick) charged during the CURRENT tick, banked
	                          // until the top of the next pass; see the hit site for why
	JE_byte pierceDmgCarry;   // scaled-damage remainder in hundredths of a point (see endless.h)
} PlayerShotDataType;

// The bit rides in aimDelayMax because a new field would grow the struct, which moves the rollback
// layout fingerprint and the replay fixtures. Weapon-table intervals are 1..8, well under the mask.
#define SHOT_AIM_GUIDANCE   0x80
#define SHOT_AIM_DELAY_MASK 0x7f

// A shot velocity above 100 rides the ship: the move subtracts 120 and adds the ship's delta, so
// 120 rests beside the ship and the shipped tables use 111 to 124 for beams that drift either way.
// Steering clamps a riding velocity to this range, which keeps it riding.
#define SHOT_ATTACHED_VEL_MIN 101  // ...and on x this one value pins both axes; steering leaves it alone
#define SHOT_ATTACHED_VEL_MAX 199  // drift cap, well clear of the range the shipped tables use

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

/** One course correction of a homing shot, run when its steering interval elapses. Weapon-table
 * homing keeps the stock rule, aiming at screen x only under guidedShotScreenAim; a
 * SHOT_AIM_GUIDANCE shot aims at screen x and retargets. */
void player_shot_aim_step(PlayerShotDataType *shot);

/** Move and draw a shot without enemy collision. Returns false off-screen and reports the hit
 * offset of the frame drawn this tick. */
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

/** Fire the Twin Pods follow-up at \a x + \a twindx after a successful primary shot. Returns the
 * new slot or MAX_PWEAPON; the caller spends the round. */
JE_integer player_shot_create_twin(
	JE_integer first, JE_word portnum, uint sidekick, int twindx, int x, int y,
	JE_word mousex, JE_word mousey, JE_word wpnum, JE_byte playernum);

/** Return an absorbed enemy shot as player \a playernum's shot with \a damage. The shot has no
 * steering and inherits an active Opening Salvo; returns MAX_PWEAPON when no slot is available. */
JE_integer player_shot_create_deflected(const EnemyShotType *incoming, int damage, JE_byte playernum);

/** Creates the chain-reaction child of a shot that just hit, at the impact point.
 * \a salvo_boost carries the parent's endless Opening Salvo tag onto every child bullet and
 * replaces the live salvo-window test.
 */
JE_integer player_shot_create_chained(
	JE_word px, JE_word py, JE_word mousex, JE_word mousey,
	JE_word wpnum, JE_byte playernum, bool salvo_boost);

#endif // SHOTS_H
