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
#include "rumble.h"

#include "joystick.h"
#include "opentyr.h"
#include "switch_platform.h"
#include "vita_platform.h"

#include "SDL.h"

int rumble_strength = RUMBLE_OFF;

// A motor level in 32 steps, so a slowly changing rumble sends fewer reports.
static Uint16 rumble_motor(float level, float scale)
{
	const float v = MIN(MAX(level * scale, 0.0f), 1.0f);
	return (Uint16)((Uint32)(v * 32.0f + 0.5f) * 65535u / 32u);
}

// The Rumble setting as a motor scale, 0 while it is off.
static float rumble_scale(void)
{
	switch (rumble_strength)
	{
	case RUMBLE_LIGHT:  return 0.5f;
	case RUMBLE_STRONG: return 1.0f;
	default:            return 0.0f;
	}
}

// Whether joystick j may need silencing: rumble is on, or its motors ran lately.
bool joystick_rumble_engaged(int j)
{
	const Joystick *const js = &joystick[j];
	return rumble_scale() > 0.0f || js->rumble_lo != 0 || js->rumble_hi != 0
	    || js->rumble_stops > 0;
}

// Tell joystick j's motors (lo, hi). SDL drops a request equal to its last one, so a stop is
// preceded by a faint nudge and always goes out, even when SDL believes the motors are idle.
void joystick_motor(int j, Uint16 lo, Uint16 hi, Uint32 duration_ms)
{
	Joystick *const js = &joystick[j];
#ifdef __SWITCH__
	(void)duration_ms;
	switch_rumble(j, lo / 65535.0f, hi / 65535.0f);
#elif SDL_VERSION_ATLEAST(2, 0, 9)
	if (lo == 0 && hi == 0)
		SDL_JoystickRumble(js->handle, 1, 1, 0);
	SDL_JoystickRumble(js->handle, lo, hi, duration_ms);
#else
	(void)duration_ms;
#endif
	js->rumble_lo = lo;
	js->rumble_hi = hi;
}

static bool rumble_voice_live(const RumbleVoice *v, Uint32 now)
{
	return (v->lo != 0 || v->hi != 0) && !SDL_TICKS_PASSED(now, v->until);
}

// Stop joystick j's motors, then twice more over the next 750 ms. SDL sends a stop once and
// never again, and a DualSense whose stop report is lost over Bluetooth runs until told otherwise.
static void joystick_motor_stop(int j, Uint32 now)
{
	joystick_motor(j, 0, 0, 0);
	joystick[j].rumble_stops = 2;
	joystick[j].rumble_stop_at = now + 250;
}

// Run joystick j's motors at the louder of its voices, and stop them once both have ended.
// The game stops every pulse itself; SDL's own timer, given a little longer, is a backstop.
void joystick_rumble_mix(int j, Uint32 now)
{
	Joystick *const js = &joystick[j];
	const RumbleVoice *const voices[2] = { &js->rumble_event, &js->rumble_flow };

	Uint16 lo = 0, hi = 0;
	Uint32 until = now;
	for (int v = 0; v < 2; ++v)
	{
		if (!rumble_voice_live(voices[v], now))
			continue;
		lo = MAX(lo, voices[v]->lo);
		hi = MAX(hi, voices[v]->hi);
		if (SDL_TICKS_PASSED(voices[v]->until, until))
			until = voices[v]->until;
	}

	if (lo != 0 || hi != 0)
	{
		js->rumble_stops = 0;
#ifdef __SWITCH__
		if (lo == js->rumble_lo && hi == js->rumble_hi)
			return;
#else
		if (lo == js->rumble_lo && hi == js->rumble_hi &&
		    !SDL_TICKS_PASSED(until, js->rumble_until))
			return;
#endif
		joystick_motor(j, lo, hi, until - now + 50);  // SDL sends a report only on a change
		js->rumble_until = until;
	}
	else if (js->rumble_lo != 0 || js->rumble_hi != 0)
	{
		joystick_motor_stop(j, now);
	}
	else if (js->rumble_stops > 0 && SDL_TICKS_PASSED(now, js->rumble_stop_at))
	{
		joystick_motor(j, 0, 0, 0);
		js->rumble_stops--;
		js->rumble_stop_at = now + 500;
	}
}

// The joystick slots behind an input device, as [*first, *last); empty for keyboard and mouse.
static void rumble_slots(int device, int *first, int *last)
{
	*first = device == 0 ? 0 : device - 3;
	*last = device == 0 ? joysticks : MIN(device - 2, joysticks);
	if (device == 1 || device == 2)
		*first = *last = 0;
}

void joystick_rumble(int device, float low, float high, Uint32 duration_ms)
{
	const float scale = rumble_scale();
	if (scale <= 0.0f || !joysticks_ready)
		return;

	const Uint16 lo = rumble_motor(low, scale);
	const Uint16 hi = rumble_motor(high, scale);
	const Uint32 now = SDL_GetTicks();

	int first, last;
	rumble_slots(device, &first, &last);
	for (int j = first; j < last; ++j)
	{
		Joystick *const js = &joystick[j];
		if (js->handle == NULL)
			continue;

		RumbleVoice *const ev = &js->rumble_event;
		if (rumble_voice_live(ev, now) && MAX(lo, hi) < MAX(ev->lo, ev->hi))
			continue;
		ev->lo = lo;
		ev->hi = hi;
		ev->until = now + duration_ms;
		joystick_rumble_mix(j, now);
	}
}

void joystick_rumble_level(int device, float low, float high, Uint32 hold_ms)
{
	const float scale = rumble_scale();
	if (scale <= 0.0f || !joysticks_ready)
		return;

	const Uint32 now = SDL_GetTicks();
	const RumbleVoice flow = { rumble_motor(low, scale), rumble_motor(high, scale), now + hold_ms };

	int first, last;
	rumble_slots(device, &first, &last);
	for (int j = first; j < last; ++j)
	{
		if (joystick[j].handle == NULL)
			continue;

		joystick[j].rumble_flow = flow;
		joystick_rumble_mix(j, now);
	}
}

void joystick_rumble_stop_all(void)
{
	if (!joysticks_ready)
		return;

	const Uint32 now = SDL_GetTicks();
	for (int j = 0; j < joysticks; ++j)
	{
		Joystick *const js = &joystick[j];
		if (js->handle == NULL)
			continue;

		js->rumble_event.lo = js->rumble_event.hi = 0;
		js->rumble_flow.lo = js->rumble_flow.hi = 0;
		if (joystick_rumble_engaged(j))
			joystick_motor_stop(j, now);
	}
}

bool joystick_can_rumble(void)
{
	for (int j = 0; j < joysticks; j++)
	{
		if (joystick[j].handle == NULL)
			continue;
#if defined(__SWITCH__)
		return true;  // every controller the system hands the game has HD rumble
#elif defined(__vita__)
		// SDL claims rumble for every Vita pad. Only the console's own controls are known to have
		// no motors.
		if (vita_pad_is_paired(SDL_JoystickInstanceID(joystick[j].handle)))
			return true;
#elif SDL_VERSION_ATLEAST(2, 0, 18)
		if (SDL_JoystickHasRumble(joystick[j].handle))
			return true;
#else
		return true;  // no capability query before SDL 2.0.18
#endif
	}
	return false;
}
