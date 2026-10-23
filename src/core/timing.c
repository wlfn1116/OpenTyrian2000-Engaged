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
#include "timing.h"

#include "joystick.h"
#include "keyboard.h"
#include "opentyr.h"
#include "touch_ui.h"

#include "SDL.h"

JE_word frameCountMax;

// Render cap: max frames *presented* per second; 0 = uncapped (or vsync-paced).
// Does not affect the simulation, which always advances at SIM_FPS below.
int fps_cap = 0;

// Canonical simulation rate: the original ~35Hz logic tick (subdivided by
// frameCountMax). Kept fixed so gameplay speed is independent of render rate.
#define SIM_FPS 35
static float delayPeriod = 1000.0f / (SIM_FPS * 2);

static Uint16 delaySpeed = 0x4300;

static Uint32 target = 0;
static Uint32 target2 = 0;

void set_fps(int fps)  // sets the render-rate cap; does not affect sim speed
{
	fps_cap = fps;
}

void setDelay(int delay)  // FKA NortSong.frameCount
{
	target = SDL_GetTicks() + delay * delayPeriod;
}

float get_delay_period(void)  // ms per delay unit; tick period = frameCountMax * this
{
	return delayPeriod;
}

void setDelay2(int delay)  // FKA NortSong.frameCount2
{
	target2 = SDL_GetTicks() + delay * delayPeriod;
}

Uint32 getDelayTicks(void)  // FKA NortSong.frameCount
{
	Sint32 delay = target - SDL_GetTicks();
	return MAX(0, delay);
}

Uint32 getDelayTicks2(void)  // FKA NortSong.frameCount2
{
	Sint32 delay = target2 - SDL_GetTicks();
	return MAX(0, delay);
}

void wait_delay(void)
{
	Sint32 delay = target - SDL_GetTicks();
	if (delay > 0)
		SDL_Delay(delay);
}

// With vsync off, space out presents to the fps_cap render cap (0 = uncapped);
// with vsync on this is unused; the display paces us.
void limit_render_fps(void)
{
	// Use the high-res performance counter, not ms: integer `1000 / fps_cap` truncates
	// (a 144 cap paces to 166fps) and SDL_Delay over-sleeps. So coarse-sleep most of the
	// wait, then spin the final ~1ms for even cadence.
	static Uint64 last_present = 0;
	static Uint64 freq = 0;

	if (freq == 0)
		freq = SDL_GetPerformanceFrequency();

	if (fps_cap <= 0)
	{
		last_present = SDL_GetPerformanceCounter();
		return;
	}

	const Uint64 frame_ticks = freq / (Uint64)fps_cap;  // counter ticks per frame
	const Uint64 ms_ticks = freq / 1000;                // counter ticks per millisecond
	const Uint64 target = last_present + frame_ticks;

	Uint64 now = SDL_GetPerformanceCounter();
	if (now < target)
	{
		const Uint64 remaining = target - now;
		if (remaining > ms_ticks)
			SDL_Delay((Uint32)((remaining - ms_ticks) * 1000 / freq));  // coarse sleep, leave ~1ms margin
		while ((now = SDL_GetPerformanceCounter()) < target)
			;  // brief spin to the exact target for even spacing
	}

	// Advance the baseline by exactly one frame to keep cadence even; resync to now
	// if more than a frame behind (e.g. after a stall) so we don't burst to catch up.
	last_present = (now - target > frame_ticks) ? now : target;
}

void service_wait_delay(void)
{
	for (; ; )
	{
		service_SDL_events(false);

		Sint32 delay = target - SDL_GetTicks();
		if (delay <= 0)
			return;

		SDL_Delay(MIN(delay, SDL_POLL_INTERVAL));
	}
}

void wait_delayorinput(void)
{
	for (; ; )
	{
		// Keep logo skip controls live during the wait.
		touch_ui_set_layout(TOUCH_LAYOUT_SKIP);
		push_joysticks_as_keyboard();
		service_SDL_events(false);

		if (newkey || newmouse || mousedown || joydown)
		{
			newkey = false;
			newmouse = false;
			// Discard the press while the controls fade with the logo.
			touch_ui_consume_input();
			return;
		}

		Sint32 delay = target - SDL_GetTicks();
		if (delay <= 0)
		{
			touch_ui_consume_input();
			return;
		}

		SDL_Delay(MIN(delay, SDL_POLL_INTERVAL));
	}
}

void setDelaySpeed(Uint16 speed)  // FKA NortSong.speed and NortSong.setTimerInt
{
	delaySpeed = speed;
	delayPeriod = ((float)speed / 0x4300) * (1000.0f / (SIM_FPS * 2));
}
