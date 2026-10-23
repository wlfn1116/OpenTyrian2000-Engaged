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
#ifndef NET_START_H
#define NET_START_H

#include "opentyr.h"
#include "player.h"

void networkStartScreen(void);
/* Encode and decode the host's lobby difficulty offset. */
int networkDifficultyBump(void);
/* Equip one ship for the Super Arcade run it chose (1..SA). Both machines call it for both
 * ships, each from the pair of picks the announcement protocol settled. */
void networkSuperArcadeEquip(Player *this_player, int ship);
/* Equip one ship for an online SuperTyrian run. Its own function, and public, for the same reason
 * as the one above: the unit suite pins the loadout it issues to both ships. */
void networkSuperTyrianEquip(Player *this_player);

/* Joiner wait-screen layout limits. */
#define GUEST_WAIT_ROWS_CAP 14
#define GUEST_WAIT_TOP      42
#define GUEST_WAIT_BOTTOM  196
#define GUEST_WAIT_GAP      14   // between the last row and the waiting line
#define GUEST_WAIT_LINE_H   12   // the waiting line (SMALL_FONT_SHAPES)
#define GUEST_WAIT_HINT_H   16   // the small_font Esc hint, dropped clear of the waiting line
#define GUEST_WAIT_HINT "Esc to cancel"
static inline int guest_wait_row_h(int rows) { return rows >= 12 ? 9 : rows >= 11 ? 10 : 11; }
static inline int guest_wait_gap(int rows) { return rows >= 13 ? 9 : GUEST_WAIT_GAP; }
int networkGuestWaitRows(const char **label, const char **value);

/* Online Super Arcade's ship picker (networkStartScreen). */
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

// One-line "the other machine is not with us yet" panel over a frozen gameplay frame. Draws into
// whatever VGAScreen points at, so callers must have pointed it at VGAScreenSeg first.
void JE_drawNetworkNotice(const char *text);

#endif /* NET_START_H */
