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
#ifndef TIMING_H
#define TIMING_H

#include "opentyr.h"

#include "SDL.h"

extern JE_word frameCountMax;

extern int fps_cap;
void set_fps(int fps);

void setDelay(int delay);
void setDelay2(int delay);
float get_delay_period(void);
bool tick_schedule_due(double *next_ms, double period_ms, Uint32 now_ms);
Uint32 getDelayTicks(void);
Uint32 getDelayTicks2(void);

void wait_delay(void);
void limit_render_fps(void);
void service_wait_delay(void);
void wait_delayorinput(void);

void setDelaySpeed(Uint16 speed);

#endif /* TIMING_H */
