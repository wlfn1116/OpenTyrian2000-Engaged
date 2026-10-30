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
#ifndef JOYSTICK_H
#define JOYSTICK_H

#include "opentyr.h"
#include "config_file.h"

#include "SDL.h"

typedef enum
{
	NONE,
	AXIS,
	BUTTON,
	HAT
}
Joystick_assignment_types;

typedef struct
{
	Joystick_assignment_types type;
	int num;
	
	// if hat
	bool x_axis; // else y_axis
	
	// if hat or axis
	bool negative_axis; // else positive
}
Joystick_assignment;

// One source of rumble on a controller; silent once `until` (SDL_GetTicks) has passed.
typedef struct
{
	Uint16 lo, hi;
	Uint32 until;
}
RumbleVoice;

typedef enum
{
	JOYSTICK_FACE_NORTH,
	JOYSTICK_FACE_WEST,
	JOYSTICK_SHOULDER_LEFT,
	JOYSTICK_SHOULDER_RIGHT,
	JOYSTICK_MENU_BUTTON_COUNT
}
JoystickMenuButton;

typedef struct
{
	SDL_Joystick *handle;  // NULL while the device is disconnected; the slot keeps its index
	SDL_JoystickGUID guid; // the device this slot belongs to, so a reconnect finds it again
	int menu_button[JOYSTICK_MENU_BUTTON_COUNT];

	Joystick_assignment assignment[10][2]; // 0-3: directions, 4-9: actions
	
	bool analog;
	int sensitivity, threshold;
	
	signed int x, y;
	int analog_direction[4];
	bool direction[4], direction_pressed[4]; // up, right, down, left  (_pressed, for emulating key presses)
	
	bool confirm, cancel;
	bool action[6], action_pressed[6]; // fire, mode swap, left fire, right fire, menu, pause
	
	Uint32 joystick_delay;
	bool input_pressed;

	// The motors run at the louder of the two voices (joystick_rumble_mix).
	RumbleVoice rumble_event;     // a hit or similar pulse
	RumbleVoice rumble_flow;      // a level its caller keeps refreshing, such as blast rumble
	Uint16 rumble_lo, rumble_hi;  // what the motors were last told
	Uint32 rumble_until;
	Uint8 rumble_stops;           // stop commands still to repeat
	Uint32 rumble_stop_at;
}
Joystick;

extern bool joydown;
extern bool ignore_joystick;
extern int joysticks;
extern Joystick *joystick;

typedef enum
{
	RUMBLE_OFF = 0,
	RUMBLE_LIGHT,
	RUMBLE_STRONG,
	RUMBLE_COUNT
} RumbleStrength;

extern int rumble_strength;  // RumbleStrength, Setup > Rumble

// Pulse the controllers behind an input device (0 all, 1 keyboard, 2 mouse, 3+ joystick n-3).
// Motor levels run 0..1 and are scaled by rumble_strength. A weaker pulse never cuts a stronger
// one short. Presentation only.
void joystick_rumble(int device, float low, float high, Uint32 duration_ms);
// Set the refreshed rumble level of those controllers, held for hold_ms unless set again. It may
// fall below its last value; event pulses still play over it.
void joystick_rumble_level(int device, float low, float high, Uint32 hold_ms);
// Silence every controller now, for screens where gameplay rumble should not continue.
void joystick_rumble_stop_all(void);
// Whether any connected controller has a rumble motor.
bool joystick_can_rumble(void);
// On Switch the pause button of joystick j opens the in-game menu in a 2-player game or on a single
// sideways Joy-Con, whose own menu button is the awkward stick press. Plus and Minus then both do.
bool joystick_pause_opens_menu(int j);
// Whether joystick j has a controller connected right now.
bool joystick_connected(int j);
int joystick_nth_connected(int n);

// Called from the event pump: opens controllers connected after startup, closes ones that left,
// and stops finished rumble.
void joystick_service(void);
// The connect and disconnect pass joystick_service runs once a second, run now. No-op on the
// consoles, which keep their startup list.
void joystick_rescan(void);
// Close and reopen every controller, keeping bindings, so the driver rereads how controllers are
// assigned. On Switch this follows the controller screen; switch-sdl2 would open its own otherwise.
void joystick_reopen_all(void);

int joystick_axis_reduce(int j, int value);
bool joystick_analog_angle(int j, float *angle);

void poll_joystick(int j);
void poll_joysticks(void);

void push_key(SDL_Scancode key);
void push_joysticks_as_keyboard(void);

bool joystick_menu_button_down(JoystickMenuButton which);
void joystick_claim_menu_buttons(unsigned mask);
int joystick_shoulder_step(bool was[2]);
int joystick_page_step(bool was[2]);

void init_joysticks(void);
void deinit_joysticks(void);

void reset_joystick_assignments(int j);
bool save_joystick_assignments(Config* config, int j);

void joystick_assignments_to_string(char *buffer, size_t buffer_len, const Joystick_assignment *assignments);

bool detect_joystick_assignment(int j, Joystick_assignment *assignment);
bool joystick_assignment_cmp(const Joystick_assignment *, const Joystick_assignment *);

#endif /* JOYSTICK_H */
