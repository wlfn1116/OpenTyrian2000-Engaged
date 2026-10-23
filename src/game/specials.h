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
#ifndef SPECIALS_H
#define SPECIALS_H

#include "opentyr.h"

#define TWIDDLE_MIN_WAIT 14  // shortest gap between two twiddles by the same ship, in ticks

#define ZINGLON_PILLAR_HALF_W 25  // widest the light pillar reaches, either side of the ship
#define ZINGLON_PILLAR_DAMAGE 10  // what the beam deals to one hull per firing tick, before scaling
// Half-width of a pillar `ramp` ticks into its opening with `duration` ticks left to run. The drawn
// beam and the column that damages both read it, so the two can never disagree.
int zinglon_pillar_width(int ramp, int duration);
// Return whether a live beam covers this screen x and report its width, damage, and owner.
bool zinglon_pillar_hit(int hullScreenX, int *width_px, int *damage, uint *owner);

void JE_specialComplete(JE_byte playernum, JE_byte specialType);
void JE_doSpecialShot(JE_byte playernum, uint *armor, uint *shield);
void JE_resetTwiddleClocks(void);  // both ships' twiddle cooldowns and any twiddle-owned flare
void JE_resetSpecialState(void);   // every special clock and live flare, so none crosses a level

void specials_register_rollback(void);

extern JE_boolean fireButtonHeld;
extern JE_byte zinglonDuration;
extern JE_byte zinglonRamp;
extern bool zinglonPillarActive[2];
extern int zinglonPillarCX[2];
extern int zinglonPillarTemp[2];
extern JE_byte astralDuration;
extern JE_word flareDuration;
extern JE_boolean flareStart;
extern JE_shortint flareColChg;
extern bool flareOwnsFilter;  // the live screen grade is a flare's, not the level's (see specials.c)
extern JE_byte specialWait;
extern JE_byte nextSpecialWait;
extern JE_boolean spraySpecial;
extern JE_shortint specialWeaponFilter, specialWeaponFreq;
extern JE_word specialWeaponWpn;
extern JE_boolean linkToPlayer;

#endif /* SPECIALS_H */
