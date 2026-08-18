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
#ifndef TYRIAN2_H
#define TYRIAN2_H

#include "opentyr.h"

#include "varz.h"
#include "helptext.h"

void intro_logos(void);

// Block-expand an 8-bit frame onto a supersampled one, every pixel a scale x scale block.
void expand_frame_to_hi(SDL_Surface *src, SDL_Surface *hi, int scale);

#define BOSS_BAR_FULL 254  // the fill value of a boss bar at full health

typedef struct
{
	Uint8 link_num;
	Uint8 fill;   // bar fill, 0..BOSS_BAR_FULL, from boss_bar_fill
	Uint8 color;
}
boss_bar_t;

extern boss_bar_t boss_bar[2];

// A boss must match an active, nonzero health-bar link.
bool enemy_has_boss_bar(JE_byte linknum);
// Light this group's boss bar for the duration of the hit flash; a group without one is untouched.
void boss_bar_note_hit(JE_byte linknum);
// The whole-x tier multiplier this hull carries: Nx boss HP and the endless tier, 1 for a plain
// enemy. The live figure is fractional; this is its floor, for the pierce delay it calibrates.
int enemy_hp_multiplier(unsigned int slot);
// The divisor its damage is actually spent through, in ENEMY_DAMAGE_ACCUM_SCALE units: the tier
// multiplier times the endless ordinary-HP overflow. 100 means it takes damage point for point.
int enemy_hp_divisor100(unsigned int slot);
// Armor points `damage` buys against it, banking the remainder in the hull's accumulator.
int enemy_spend_damage(unsigned int slot, int damage);

/* How full a boss bar draws, from the most-damaged part's remaining armor and the armor that part
 * started with. Boss armor varies: the difficulty curve scales it at spawn and level scripts arm
 * and re-arm boss groups at their own values, so the two have to be measured against each other. */
JE_byte boss_bar_fill(unsigned int armorleft, unsigned int full);

/* The armor of a boss group's most-damaged live part, and the armor that same part started with.
 * `*out_armor` comes back above 255 when the group has no live parts left, which is how
 * draw_boss_bar tells a dead boss from an invincible one. */
void boss_bar_survey(JE_byte link_num, unsigned int *out_armor, unsigned int *out_full);

/* Re-latch the armor an enemy counts as starting with: its health bar's denominator, and the
 * full-HP figure the Executioner perk measures a wound against. Call after every direct write to
 * armorleft; damage must not call it. */
void enemy_note_full_armor(struct JE_SingleEnemyType *enemy);

// Route every kill through this function so tallies, bounties, and reactive effects agree.
// Despawns still clear enemyAvail directly.
void enemy_logical_death(unsigned int i, int killer);

/* Take the hull `slot` belongs to down as a killing shot does: every linked part pays out to
 * `payee`, dies credited to `killer` and explodes, a part with edlevel -1 transforms instead, and a
 * link-254 kill fires the level's jump. The player-shot loop and the Endless ram site share it. */
void enemy_kill_group(unsigned int slot, int payee, int killer);

/* True once a part has taken that edlevel -1 transformation. Nothing can shoot it, ram it or fire
 * from it after that, so what is left is wreckage. */
bool enemy_is_wreck(unsigned int slot);

/* The endless tier colour this body paints in, 0 for none: its own tier, or the bank its link group
 * lends sealed plating. Wreckage has stopped being an enemy and drops both. */
Uint8 enemy_body_tint(unsigned int slot);

/* Frames presented since the game started, the clock a cosmetic paces itself on. Rollback runs
 * several simulation passes against one presented frame, so a cadence counted in sim ticks stutters.
 * Outside the rollback registry, and never a simulation input. */
Uint32 rl_presented_frames(void);

extern float debug_interp_alpha;  // last presented interpolation fraction

extern char tempStr[31];
extern JE_byte itemAvail[9][10], itemAvailMax[9];
extern JE_word levelEnemyFrequency;

void JE_createNewEventEnemy(JE_byte enemytypeofs, JE_word enemyoffset, Sint16 uniqueShapeTableI);

uint JE_makeEnemy(struct JE_SingleEnemyType *enemy, Uint16 eDatI, Sint16 uniqueShapeTableI);

void JE_eventJump(JE_word jump);

void JE_whoa(void);

Sint16 JE_newEnemy(int enemyOffset, Uint16 eDatI, Sint16 uniqueShapeTableI);
void JE_drawEnemy(int enemyOffset);
void JE_starShowVGA(void);

void JE_main(void);
void JE_loadMap(void);
void tyrian2_deinit(void);
// The flip/spotlight special code and inverted-control flag, from level and Endless state.
void JE_deriveStarShowSpecial(void);
#ifdef WITH_NETWORK
void networkStartScreen(void);
/* Steps the host adds to the lobby's difficulty and the joiner subtracts again, so both land on
 * the same initialDifficulty. Public so the unit suite can pin the two halves against each other
 * for every game type; a mismatch would run the two machines on different rules. */
int networkDifficultyBump(void);
/* Equip one ship for the Super Arcade run it chose (1..SA). Both machines call it for both
 * ships, each from the pair of picks the announcement protocol settled. */
void networkSuperArcadeEquip(Player *this_player, int ship);
/* Equip one ship for an online SuperTyrian run. Its own function, and public, for the same reason
 * as the one above: the unit suite pins the loadout it issues to both ships. */
void networkSuperTyrianEquip(Player *this_player);
#endif
bool titleScreen(void);
bool newGame(void);
bool newSuperArcadeGame(unsigned int i);
bool newSuperTyrianGame(void);

/* Online Super Arcade's ship picker (networkStartScreen). The nine names go in two columns,
 * hit-tested for the mouse and stepped by the arrow keys, so the layout is declared here rather
 * than buried in the draw: the unit suite measures the real names against it and fails if a
 * column would clip or a row would collide with the hull below. */
#define SA_PICK_HEADER_Y   16
#define SA_PICK_ROWS        5   // rows in the left column; the right one takes the remainder
#define SA_PICK_TOP_Y      44
#define SA_PICK_ROW_H      16
#define SA_PICK_COL_X      40
#define SA_PICK_COL_DX    136
#define SA_PICK_SHIP_Y    128   // the highlighted hull, blitted 2x2 (28px tall)
#define SA_PICK_STATUS_Y  168
#define SA_PICK_PEER_Y    180
// Shown on the partner line while this player waits: the pick can still be taken back.
#define SA_PICK_UNPICK_HINT "Esc to pick again."
static inline int sa_pick_name_x(int i) { return SA_PICK_COL_X + (i / SA_PICK_ROWS) * SA_PICK_COL_DX; }
static inline int sa_pick_name_y(int i) { return SA_PICK_TOP_Y + (i % SA_PICK_ROWS) * SA_PICK_ROW_H; }
bool newEndlessGame(void);
void JE_readTextSync(void);
void JE_displayText(void);

bool JE_searchFor(JE_byte PLType, JE_byte* out_index);
void JE_eventSystem(void);

// Arcade weapon-ball randomizer: re-scan enemyDat for the front/rear/sidekick/special ball
// pools. Must run after any load that rewrites enemyDat (JE_loadItemDat).
void JE_buildArcadeBallPools(void);

void draw_boss_bar(void);

// Offsets used to keep the Endless kill-fire HUD clear of a boss bar.
int  boss_bar_hud_left_shift(int hudRightX);  // px to shift LEFT for a right-side vertical bar
bool boss_bar_hud_needs_up_shift(void);       // true while a BOTTOM horizontal bar is shown
int  boss_bar_bottom_band_top(void);          // topmost row that bar covers, or INT_MAX if none

// Inclusive first and last row a side-hugging vertical boss bar covers, measured from the corner
// HUD drawn on that side this tick: three blank rows above the frame and one below, clamped to
// the WARNING strips. Public for the unit suite's clearance checks.
void boss_bar_vertical_span(bool onLeft, int *top, int *bot);

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

#endif /* TYRIAN2_H */
