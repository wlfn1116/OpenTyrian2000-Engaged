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
#ifndef HEALTH_BARS_H
#define HEALTH_BARS_H

#include "opentyr.h"

#include "game_state.h"

#include "SDL.h"

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

/* How full a boss bar draws, from the most-damaged part's remaining armor and the armor that
 * part started with. */
JE_byte boss_bar_fill(unsigned int armorleft, unsigned int full);

/* Return remaining and full armor for a group's most-damaged active part. Wreckage is ignored;
 * `*out_armor` above 255 means the group has no live parts. */
void boss_bar_survey(JE_byte link_num, unsigned int *out_armor, unsigned int *out_full);

/* Update an enemy's full-armour baseline after direct armour writes, never damage. */
void enemy_note_full_armor(struct JE_SingleEnemyType *enemy);

void boss_bar_tick(void);
void boss_bar_fade_flash(void);
void draw_boss_bar(void);

// Offsets used to keep the Endless kill-fire HUD clear of a boss bar.
int  boss_bar_hud_left_shift(int hudRightX);  // px to shift LEFT for a right-side vertical bar
int  boss_bar_bottom_band_top(void);          // topmost row that bar covers, or INT_MAX if none

// Inclusive vertical boss-bar bounds after HUD and warning-strip clearance.
void boss_bar_vertical_span(bool onLeft, int *top, int *bot);

void draw_enemy_health_bars(void);
void draw_enemy_hp_bar_box(int id, int boxL, int boxR, int boxT, int boxB, float frac);
void boss_bar_present_tick(void);
void draw_boss_bar_present(SDL_Surface *dst, int scale, float alpha);

#endif /* HEALTH_BARS_H */
