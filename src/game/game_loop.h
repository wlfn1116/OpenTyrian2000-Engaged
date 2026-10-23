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
#ifndef GAME_LOOP_H
#define GAME_LOOP_H

#include "opentyr.h"

#include "game_state.h"
#include "helptext.h"

// Block-expand an 8-bit frame onto a supersampled one, every pixel a scale x scale block.
void expand_frame_to_hi(SDL_Surface *src, SDL_Surface *hi, int scale);
bool present_overlay_on_last_hi_frame(void);

typedef struct
{
	float tick_ms;
	float draw_ms;
	float present_ms;
	float worst_ms;
} FrameTimes;

void frame_times_get(FrameTimes *times);

#define ENEMY_ARMED_FLASH_FRAMES 6  // presented frames; matches GAUGE_FLASH_START
#define ENEMY_ARMED_FLASH_WHITE  5  // full-white threshold; matches GAUGE_FLASH_WHITE

/* Return whether a live slot changed from invulnerable to damageable armor. */
bool enemy_armed_flash_arms(JE_byte wasArmor, JE_byte nowArmor, JE_byte avail);
/* True if this body belongs to the configured cue scope. */
bool enemy_armed_flash_shows(JE_byte linknum);
/* Return the body-flash shade lift for the remaining presented frames. */
Uint8 enemy_armed_flash_lift(Uint32 left);
Uint32 enemy_armed_flash_left(unsigned int slot);
void enemy_note_armed(unsigned int slot, JE_byte wasArmor);

// In-game palette's greyscale ramp.
#define ENEMY_FLASH_BANK 0x00

/* Rebuild link-group scenery flags before the homing pass. */
void endlessScanSceneryLinks(void);

/* True if Endless homing may steer this body. Call endlessScanSceneryLinks first. */
bool endlessHomingChaser(unsigned int slot);

/* Presented-frame clock for cosmetics; never simulation or rollback state. */
Uint32 rl_presented_frames(void);

extern float debug_interp_alpha;  // last presented interpolation fraction

extern char tempStr[31];

void JE_createNewEventEnemy(JE_byte enemytypeofs, JE_word enemyoffset, Sint16 uniqueShapeTableI);
int event_enemy_scroll_catchup(JE_word enemyOffset, const struct JE_SingleEnemyType *e);
void enemy_approach_debt_settle(unsigned int i);

Sint16 JE_newEnemy(int enemyOffset, Uint16 eDatI, Sint16 uniqueShapeTableI);

void JE_main(void);
JE_byte backup_save_slot(void);
void game_loop_deinit(void);
// The flip/spotlight special code and inverted-control flag, from level and Endless state.
void JE_deriveStarShowSpecial(void);

// Arcade weapon-ball randomizer: re-scan enemyDat for the front/rear/sidekick/special ball
// pools. Must run after any load that rewrites enemyDat (JE_loadItemDat).
void JE_buildArcadeBallPools(void);
bool arcadeSuperPickupRandomActive(void);

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
void vt_ship_twiddle_dir(int player_index, int *out_dx, int *out_dy);  // mouse steering dir for twiddle codes

#endif /* GAME_LOOP_H */
