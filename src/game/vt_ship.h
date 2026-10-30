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
#ifndef VT_SHIP_H
#define VT_SHIP_H

#include "opentyr.h"

// Experimental variable-timestep ship. Its mouse scale matches classic movement.
#define VT_MOUSE_SENS 0.25f
extern bool vt_ship;       // runtime toggle for render-rate ship simulation
bool vt_ship_owns(void);   // true when VT currently controls the player ship
void vt_ship_step(float dt);  // advance the ship one displayed frame (dt in ticks)
void vt_ship_tick(void);   // per-35Hz-tick reconcile (external forces / reposition)
// Netplay only: commit this tick's accumulated VT motion to the ship as whole pixels, so the
// netcode transmits it as its per-tick delta. No-op outside a network game.
void vt_ship_commit_net(int player_index);
void vt_ship_shot_delta(int player_index, int *out_dx, int *out_dy);  // inter-tick ship move for tracking shots
void vt_ship_take_pull(int player_index, int *out_dx, int *out_dy);
void vt_ship_twiddle_dir(int player_index, int *out_dx, int *out_dy);  // mouse steering dir for twiddle codes

void vt_ship_pred_reset(void);
void vt_ship_pred_tick(void);
void vt_ship_update_override(float alpha);
void vt_ship_display_position(int p, float *out_x, float *out_y);
void vt_ship_publish_velocity(void);
void vt_ship_register_rollback(void);

#endif /* VT_SHIP_H */
