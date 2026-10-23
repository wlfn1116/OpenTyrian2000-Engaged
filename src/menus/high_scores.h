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
#ifndef HIGH_SCORES_H
#define HIGH_SCORES_H

#include "opentyr.h"
#include "config.h"

void JE_highScoreScreen(void);
// The co-op Campaign board's second line: who was flying, then the terms their figure was earned
// on, shortened to widthPx. The Px call is the width the board itself gives it.
void coopCampaignRecordLine(char *out, size_t outSize, const CoopCampaignScore *record,
                            int widthPx);
int coopCampaignRecordLineWidthPx(void);
void JE_sortHighScores(void);
void JE_highScoreCheck(void);
// How an online Timed Battle ends: the two purses side by side and who took the race. Replaces
// the solo mode's name-entry dialog, which blocks on a keyboard the other machine cannot see.
void JE_timedBattleResult(void);

#endif /* HIGH_SCORES_H */
