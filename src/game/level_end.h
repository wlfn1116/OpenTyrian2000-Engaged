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
#ifndef LEVEL_END_H
#define LEVEL_END_H

#include "opentyr.h"

#include "SDL.h"

void JE_nextEpisode(void);

// Whether the score-based difficulty drift between levels runs; false for the whole of an Endless
// run, which stays on the rung it launched with. Public so the unit suite can pin that.
bool difficulty_adjust_active(void);

void JE_endLevelAni(void);
void JE_drawCube(SDL_Surface * screen, JE_word x, JE_word y, JE_byte filter, JE_byte brightness);

#endif /* LEVEL_END_H */
