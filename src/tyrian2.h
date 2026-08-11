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

typedef struct
{
	Uint8 link_num;
	Uint8 armor;
	Uint8 color;
}
boss_bar_t;

extern boss_bar_t boss_bar[2];

// A boss must match an active, nonzero health-bar link.
bool enemy_has_boss_bar(JE_byte linknum);

// Route every kill through this function so tallies, bounties, and reactive effects agree.
// Despawns still clear enemyAvail directly.
typedef enum
{
	ENEMY_DEATH_FULL,   // an ordinary kill: the reactive boons / dangers / perks all get to fire
	ENEMY_DEATH_QUIET,  // bookkeeping and latches only, no reactive effects (the Chain Reaction drain)
}
enemy_death_kind;

void enemy_logical_death(unsigned int i, enemy_death_kind kind, int killer);

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
