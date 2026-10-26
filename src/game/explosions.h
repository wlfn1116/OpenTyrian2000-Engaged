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
#ifndef EXPLOSIONS_H
#define EXPLOSIONS_H

#include "opentyr.h"

#define MAX_EXPLOSIONS           200
#define MAX_REPEATING_EXPLOSIONS 20
#define EXPLOSION_TYPES          54

typedef struct {
	JE_byte ttl;
	JE_integer x, y;
	JE_word sprite;
	bool followPlayer;
	bool fixedPosition;
	JE_integer deltaY;
	Uint8 id_gen;  // bumped each time this slot is reused, to disambiguate the render-
	               // list interpolation id so a recycled slot isn't mis-paired
	Uint8 filter;  // palette bank to recolour into, 0 for the sprite's own colours
} Explosion;

typedef struct {
	unsigned int delay;
	unsigned int ttl;
	unsigned int x, y;
	bool big;
	Uint8 filter;  // passed on to each explosion this sequence spawns
} rep_explosion_type;

extern Explosion explosions[MAX_EXPLOSIONS];
extern JE_integer explosionFollowAmountX, explosionFollowAmountY;
// Presentation tint for the next explosion call; clear it before the tick ends.
extern Uint8 explosionFilter;

/* Opacity for the next explosion, in sixteenths. JE_setupExplosion() stamps it into a local side
 * table; reset it after spawning. */
extern Uint8 explosionOpacity;
Uint8 explosion_opacity(uint slot);
extern rep_explosion_type rep_explosions[MAX_REPEATING_EXPLOSIONS];

void JE_setupExplosion(JE_integer x, JE_integer y, JE_integer deltaY, JE_integer type, bool fixedPosition, bool followPlayer);
void JE_setupExplosionLarge(JE_boolean enemyground, JE_byte explonum, JE_integer x, JE_integer y);

// Rumble energy per blast, as a share of full motor strength. Presentation only.
#define EXPLOSION_RUMBLE_SMALL 0.2f   // a small enemy's death
#define EXPLOSION_RUMBLE_POP   0.15f  // one pop of a small repeating sequence
#define EXPLOSION_RUMBLE_LARGE 0.35f  // every JE_setupExplosionLarge
#define EXPLOSION_RUMBLE_BIG   0.6f   // a large blast that starts a big sequence

// A blast at (x, y) for this tick's rumble. Heavy blasts lean on the low motor and fade slowly,
// light ones on the high motor and fade fast. A ship feels a distant blast less.
void explosion_rumble_add(float energy, bool heavy, int x, int y);
// Once per tick: fade each ship's blast rumble, add the tick's blasts and hold the result on
// its controllers. A tick that is not `live` (a rollback resim, a demo) only drops its blasts.
void explosion_rumble_tick(bool live);
// Silence the fading rumble, for a new level.
void explosion_rumble_reset(void);

#endif /* EXPLOSIONS_H */
