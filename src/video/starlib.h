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
#ifndef STARLIB_H
#define STARLIB_H

#include "opentyr.h"

#include "SDL.h"

// Render the starfield at 1x or into a cleared supersampled target. The high-res
// path uses sub-pixel positions for smooth downscaling.
void JE_starlib_main(float step, SDL_Surface *target, int scale);
void JE_wackyCol(void);
void JE_starlib_init(void);
void JE_resetValues(void);
void JE_changeSetup(JE_byte setupType);
void JE_newStar(void);

#endif /* STARLIB_H */
