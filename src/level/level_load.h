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
#ifndef LEVEL_LOAD_H
#define LEVEL_LOAD_H

#include "opentyr.h"

#include "SDL.h"

extern JE_byte itemAvail[9][10], itemAvailMax[9];
extern JE_word levelEnemyFrequency;

void JE_loadMap(void);

extern JE_boolean levelWarningDisplay;
extern JE_byte levelWarningLines;
extern JE_boolean warningRed;
extern JE_byte warningSoundDelay;
extern JE_byte warningCol;
extern JE_shortint warningColChange;

void JE_updateWarning(SDL_Surface * screen);

extern JE_boolean jumpSection;

#endif /* LEVEL_LOAD_H */
