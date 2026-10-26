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
#ifndef RUMBLE_H
#define RUMBLE_H

#include "opentyr.h"

#include "SDL.h"

extern bool joysticks_ready;

bool joystick_rumble_engaged(int j);
void joystick_motor(int j, Uint16 lo, Uint16 hi, Uint32 duration_ms);
void joystick_rumble_mix(int j, Uint32 now);

#endif // RUMBLE_H
