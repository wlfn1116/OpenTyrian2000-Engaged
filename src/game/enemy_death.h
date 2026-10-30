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
#ifndef ENEMY_DEATH_H
#define ENEMY_DEATH_H

#include "opentyr.h"

// Route every kill through this function so tallies, bounties, and reactive effects agree.
// Despawns still clear enemyAvail directly.
void enemy_logical_death(unsigned int i, int killer);

/* Where loot dropped by the body `i` belongs to should sit: the dying cell's anchor, or a cell
 * centered on the visible hull for a linked or 2x2 body. In `i`'s bank ex space. */
void enemy_loot_anchor(unsigned int i, JE_integer *x, JE_integer *y);

/* Kill a linked hull through the normal payout, transform, explosion, and jump paths. */
void enemy_kill_group(unsigned int slot, int payee, int killer);

/* True for an edlevel -1 transformed part that no longer participates in combat. */
bool enemy_is_wreck(unsigned int slot);

/* Return a live body's tier tint or its linked group's sealed-shell tint. Wrecks return 0. */
Uint8 enemy_body_tint(unsigned int slot);

// The whole-x tier multiplier this hull carries: Nx boss HP and the endless tier, 1 for a plain
// enemy. The live figure is fractional; this is its floor, for the pierce delay it calibrates.
int enemy_hp_multiplier(unsigned int slot);
// The divisor its damage is actually spent through, in ENEMY_DAMAGE_ACCUM_SCALE units: the tier
// multiplier times the endless ordinary-HP overflow. 100 means it takes damage point for point.
int enemy_hp_divisor100(unsigned int slot);
// Armor points `damage` buys against it, banking the remainder in the hull's accumulator.
int enemy_spend_damage(unsigned int slot, int damage);

void endlessShockwaveClear(JE_integer sx, JE_integer sy, int radius);
void execution_reflect_capture(unsigned int slot, JE_byte playerNum);
void execution_reflect_process(void);

void chain_reset_queue(void);
void chain_reaction_process(void);

void enemy_death_register_rollback(void);

#endif /* ENEMY_DEATH_H */
