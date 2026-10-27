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
#include "video.h"

#include "SDL.h"

#include <stdio.h>

JE_word frameCountMax;

// Render cap: max frames *presented* per second; 0 = uncapped (or vsync-paced).
// Does not affect the simulation, which always advances at SIM_FPS below.
int fps_cap = 0;

// Canonical simulation rate: the original ~35Hz logic tick (subdivided by
// frameCountMax). Kept fixed so gameplay speed is independent of render rate.
#define SIM_FPS 35
static float delayPeriod = 1000.0f / (SIM_FPS * 2);

#define WAIT_FRAME_PACING_MS 100

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

bool tick_schedule_due(double *next_ms, double period_ms, Uint32 now_ms)
{
	if ((double)now_ms < *next_ms)
		return false;
	*next_ms = ((double)now_ms - *next_ms > 4.0 * period_ms) ? now_ms + period_ms
	                                                          : *next_ms + period_ms;
	return true;
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

static Uint32 wait_frame_due(bool new_frame, Sint32 entry_ms, Sint32 delay_ms)
{
	if (!new_frame)
		return VIDEO_NO_FRAME_DUE;
	return entry_ms < WAIT_FRAME_PACING_MS ? 0 : (Uint32)delay_ms;
}

void wait_delay(void)
{
	const bool new_frame = video_take_new_frame();
	const Sint32 entry = target - SDL_GetTicks();
	for (Sint32 delay = entry; delay > 0; delay = target - SDL_GetTicks())
	{
		const Uint32 frame_due = wait_frame_due(new_frame, entry, delay);
		const Uint32 nap = video_idle_refresh((Uint32)delay, frame_due);
		SDL_Delay(nap);
		if (nap == (Uint32)delay)
			return;
	}
}

void idle_delay(Uint32 ms)
{
	SDL_Delay(video_idle_refresh(ms, VIDEO_NO_FRAME_DUE));
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
	const bool new_frame = video_take_new_frame();
	const Sint32 entry = target - SDL_GetTicks();
	for (; ; )
	{
		service_SDL_events(false);

		Sint32 delay = target - SDL_GetTicks();
		if (delay <= 0)
			return;

		SDL_Delay(video_idle_refresh((Uint32)MIN(delay, SDL_POLL_INTERVAL),
		                             wait_frame_due(new_frame, entry, delay)));
	}
}

void wait_delayorinput(void)
{
	const bool new_frame = video_take_new_frame();
	const Sint32 entry = target - SDL_GetTicks();
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

		SDL_Delay(video_idle_refresh((Uint32)MIN(delay, SDL_POLL_INTERVAL),
		                             wait_frame_due(new_frame, entry, delay)));
	}
}

void setDelaySpeed(Uint16 speed)  // FKA NortSong.speed and NortSong.setTimerInt
{
	delaySpeed = speed;
	delayPeriod = ((float)speed / 0x4300) * (1000.0f / (SIM_FPS * 2));
}

bool timing_test_wait_frames(char *detail, size_t detailSize)
{
	static const struct
	{
		bool new_frame;
		Sint32 entry_ms, delay_ms;
		Uint32 frame_due_ms;
	} cases[] = {
		{ false, 28, 28, VIDEO_NO_FRAME_DUE }, { false, 3000, 5, VIDEO_NO_FRAME_DUE },
		{ true, 28, 28, 0 }, { true, 71, 40, 0 }, { true, 99, 99, 0 },
		{ true, 100, 100, 100 }, { true, 3000, 25, 25 },
	};
	char fault[96] = "";

	for (size_t k = 0; k < COUNTOF(cases) && fault[0] == '\0'; ++k)
	{
		const Uint32 got = wait_frame_due(cases[k].new_frame, cases[k].entry_ms, cases[k].delay_ms);
		if (got != cases[k].frame_due_ms)
			snprintf(fault, sizeof(fault), "a %d ms wait %s a new frame sees its frame due at %u",
			         (int)cases[k].entry_ms, cases[k].new_frame ? "after" : "without",
			         (unsigned)got);
	}

	if (fault[0] != '\0' && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault[0] == '\0';
}
