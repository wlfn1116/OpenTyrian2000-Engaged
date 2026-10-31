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
#include "joystick.h"

#include "config.h"
#include "config_file.h"
#include "file.h"
#include "game_state.h"
#include "keyboard.h"
#include "network.h"
#include "opentyr.h"
#include "player_ship.h"
#include "prompt.h"
#include "rollback.h"
#include "rumble.h"
#include "timing.h"
#include "switch_platform.h"
#include "touch_ui.h"
#include "video.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int joystick_axis_threshold(int j, int value);
static int check_assigned(int j, const Joystick_assignment assignment[2]);

static const char *assignment_to_code(const Joystick_assignment *assignment);
static void code_to_assignment(Joystick_assignment *assignment, const char *buffer);

static int joystick_repeat_delay = 300; // milliseconds, repeat delay for buttons
bool joydown = false;            // any joystick buttons down, updated by poll_joysticks()
bool ignore_joystick = false;
bool joycons_separate = false;

int joysticks = 0;
Joystick *joystick = NULL;

// Desktop and mobile pads come and go. The consoles keep their startup list: switch-sdl2 always
// lists eight slots with only the first one real.
#if !defined(__SWITCH__) && !defined(__vita__)
#define JOYSTICK_HOTPLUG 1
#define JOYSTICK_SLOTS_MAX 16
static bool pad_database = false;  // SDL_INIT_GAMECONTROLLER is up, for default bindings
#endif

bool joysticks_ready = false;

static unsigned joystick_changes = 0;

static const int joystick_analog_max = 32767;

static bool load_joystick_assignments(Config *config, int j);

// Config section name for slot j. switch-sdl2 calls every player "Switch Controller", so later
// Switch slots add their player number; slot 0 keeps the plain name its saved bindings use.
static const char *joystick_config_name(int j, char *buf, size_t size)
{
	const char *const name = SDL_JoystickName(joystick[j].handle);
#ifdef __SWITCH__
	if (j > 0 && name != NULL)
	{
		snprintf(buf, size, "%s %d", name, j + 1);
		return buf;
	}
#endif
	(void)buf;
	(void)size;
	return name;
}

// eliminates axis movement below the threshold
static int joystick_axis_threshold(int j, int value)
{
	assert(j < joysticks);
	
	bool negative = value < 0;
	if (negative)
		value = -value;
	
	if (value <= joystick[j].threshold * 1000)
		return 0;
	
	value -= joystick[j].threshold * 1000;
	
	return negative ? -value : value;
}

// converts joystick axis to sane Tyrian-usable value (based on sensitivity)
int joystick_axis_reduce(int j, int value)
{
	assert(j < joysticks);
	
	value = joystick_axis_threshold(j, value);
	
	if (value == 0)
		return 0;
	
	return value / (3000 - 200 * joystick[j].sensitivity);
}

// converts analog joystick axes to an angle
// returns false if axes are centered (there is no angle)
bool joystick_analog_angle(int j, float *angle)
{
	assert(j < joysticks);
	
	float x = joystick_axis_threshold(j, joystick[j].x), y = joystick_axis_threshold(j, joystick[j].y);
	
	if (x != 0)
	{
		*angle += atanf(-y / x);
		*angle += (x < 0) ? -M_PI_2 : M_PI_2;
		return true;
	}
	else if (y != 0)
	{
		*angle += y < 0 ? M_PI : 0;
		return true;
	}
	
	return false;
}

#ifdef __SWITCH__
// switch-sdl2 reports buttons 0 to 27 and leaves a single Joy-Con's L/R and ZL/ZR out, so the
// game reads those through libnx as buttons 28 and 29.
#define SWITCH_JOYCON_LR_BUTTON   28
#define SWITCH_JOYCON_ZLZR_BUTTON 29

// In a local 2-player game a Pro Controller or Joy-Con pair takes a sideways Joy-Con's layout:
// L and R fire the sidekicks like its SL and SR, ZL and ZR change the rear weapon mode.
static bool joystick_2p_joycon_layout(int j)
{
	return twoPlayerMode && !galagaMode && !isNetworkGame && !switch_slot_single_joycon(j);
}

static int joystick_2p_raw(int j, int num)
{
	// switch-sdl2's 8 and 9 are SL and SR on a single Joy-Con but ZL and ZR here.
	return ((num == 8 || num == 9) && joystick_2p_joycon_layout(j)) ? num - 2 : num;  // L, R
}
#endif

static unsigned menu_buttons_claimed;

static bool joystick_raw_claimed(int j, int num)
{
	for (int which = 0; which < JOYSTICK_MENU_BUTTON_COUNT; ++which)
		if ((menu_buttons_claimed & (1u << which)) != 0 && joystick[j].menu_button[which] == num)
			return true;
	return false;
}

static bool joystick_raw_held(int j, int num)
{
	return !joystick_raw_claimed(j, num) && SDL_JoystickGetButton(joystick[j].handle, num) == 1;
}

static int joystick_binding_raw(int j, const Joystick_assignment *binding)
{
#ifdef __SWITCH__
	return joystick_2p_raw(j, binding->num);
#else
	(void)j;
	return binding->num;
#endif
}

static bool joystick_button_held(int j, int num)
{
#ifdef __SWITCH__
	if (num == SWITCH_JOYCON_ZLZR_BUTTON && joystick_2p_joycon_layout(j))
		return joystick_raw_held(j, 8) || joystick_raw_held(j, 9);
	if (num == SWITCH_JOYCON_LR_BUTTON)
		return (switch_slot_joycon_extra(j) & SWITCH_JOYCON_LR) != 0;
	if (num == SWITCH_JOYCON_ZLZR_BUTTON)
		return (switch_slot_joycon_extra(j) & SWITCH_JOYCON_ZLZR) != 0;
	num = joystick_2p_raw(j, num);
#endif
	return joystick_raw_held(j, num);
}

/* gives back value 0..joystick_analog_max indicating that one of the assigned
 * buttons has been pressed or that one of the assigned axes/hats has been moved
 * in the assigned direction
 */
static int check_assigned(int j, const Joystick_assignment assignment[2])
{
	SDL_Joystick *const joystick_handle = joystick[j].handle;
	int result = 0;
	
	for (int i = 0; i < 2; i++)
	{
		int temp = 0;
		
		switch (assignment[i].type)
		{
		case NONE:
			continue;
			
		case AXIS:
			temp = SDL_JoystickGetAxis(joystick_handle, assignment[i].num);
			
			if (assignment[i].negative_axis)
				temp = -temp;
			break;
		
		case BUTTON:
			temp = joystick_button_held(j, assignment[i].num) ? joystick_analog_max : 0;
			break;
		
		case HAT:
			temp = SDL_JoystickGetHat(joystick_handle, assignment[i].num);
			
			if (assignment[i].x_axis)
				temp &= SDL_HAT_LEFT | SDL_HAT_RIGHT;
			else
				temp &= SDL_HAT_UP | SDL_HAT_DOWN;
			
			if (assignment[i].negative_axis)
				temp &= SDL_HAT_LEFT | SDL_HAT_UP;
			else
				temp &= SDL_HAT_RIGHT | SDL_HAT_DOWN;
			
			temp = temp ? joystick_analog_max : 0;
			break;
		}
		
		if (temp > result)
			result = temp;
	}
	
	return result;
}

// updates joystick state
void poll_joystick(int j)
{
	assert(j < joysticks);
	
	if (joystick[j].handle == NULL)
		return;
	
	SDL_JoystickUpdate();
	
	// indicates that a direction/action was pressed since last poll
	joystick[j].input_pressed = false;
	
	// indicates that an direction/action has been held long enough to fake a repeat press
	bool repeat = joystick[j].joystick_delay < SDL_GetTicks();

#if defined(__SWITCH__) || defined(__vita__)
	// Fold the console right stick into analog_direction[] because normal direction
	// bindings are already occupied by the left stick and d-pad.
	const bool switch_right_stick = SDL_JoystickNumAxes(joystick[j].handle) >= 4;
#endif

	// update direction state
	for (uint d = 0; d < COUNTOF(joystick[j].direction); d++)
	{
		bool old = joystick[j].direction[d];

		joystick[j].analog_direction[d] = check_assigned(j, joystick[j].assignment[d]);

#if defined(__SWITCH__) || defined(__vita__)
		if (switch_right_stick)
		{
			// Directions are up, right, down, left; use whichever stick is pushed farther.
			int rs = SDL_JoystickGetAxis(joystick[j].handle, (d + 1) % 2 + 2);
			if (d == 0 || d == 3)
				rs = -rs;
			if (rs > joystick[j].analog_direction[d])
				joystick[j].analog_direction[d] = rs;
		}
#endif

		joystick[j].direction[d] = joystick[j].analog_direction[d] > (joystick_analog_max / 2);
		joydown |= joystick[j].direction[d];
		
		joystick[j].direction_pressed[d] = joystick[j].direction[d] && (!old || repeat);
		joystick[j].input_pressed |= joystick[j].direction_pressed[d];
	}
	
	joystick[j].x = -joystick[j].analog_direction[3] + joystick[j].analog_direction[1];
	joystick[j].y = -joystick[j].analog_direction[0] + joystick[j].analog_direction[2];
	
	// update action state
	for (uint d = 0; d < COUNTOF(joystick[j].action); d++)
	{
		bool old = joystick[j].action[d];
		
		joystick[j].action[d] = check_assigned(j, joystick[j].assignment[d + COUNTOF(joystick[j].direction)]) > (joystick_analog_max / 2);
		joydown |= joystick[j].action[d];
		
		joystick[j].action_pressed[d] = joystick[j].action[d] && (!old || repeat);
		joystick[j].input_pressed |= joystick[j].action_pressed[d];
	}
	
	// Fire confirms; change-fire, menu, and pause cancel.
	joystick[j].confirm = joystick[j].action[0];
	joystick[j].cancel = joystick[j].action[1] || joystick[j].action[4] || joystick[j].action[5];
	
	// if new input, reset press-repeat delay
	if (joystick[j].input_pressed)
	{
		joystick[j].joystick_delay = SDL_GetTicks() + joystick_repeat_delay;
		if (!rollback_resim)
			prompt_use_pad(j);
	}
}

// updates all joystick states
void poll_joysticks(void)
{
	joydown = false;
	
	for (int j = 0; j < joysticks; j++)
		poll_joystick(j);
}

// sends SDL KEYDOWN and KEYUP events for a key
void push_key(SDL_Scancode key)
{
	SDL_Event e;
	
	memset(&e, 0, sizeof(e));
	
	e.key.keysym.scancode = key;
	e.key.state = SDL_RELEASED;
	
	e.type = SDL_KEYDOWN;
	SDL_PushEvent(&e);
	
	e.type = SDL_KEYUP;
	SDL_PushEvent(&e);
}

// helps us be lazy by pretending joysticks are a keyboard (useful for menus)
void push_joysticks_as_keyboard(void)
{
	const SDL_Scancode confirm = SDL_SCANCODE_RETURN, cancel = SDL_SCANCODE_ESCAPE;
	const SDL_Scancode direction[4] = { SDL_SCANCODE_UP, SDL_SCANCODE_RIGHT, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT };

	// No live input during a rollback re-simulation (see service_SDL_events).
	if (rollback_resim)
		return;

	/* Deliver touch keys beside controller keys, immediately before the screen's event pump.
	 * Idle wait loops also use this point to repaint changed controls. */
	touch_ui_flush_keys();
	touch_ui_idle_repaint();

	poll_joysticks();
	
	for (int j = 0; j < joysticks; j++)
	{
		if (!joystick[j].input_pressed)
			continue;
		
		if (!joystick_menu_actions_fixed(j))
		{
			// If one button maps to confirm and cancel, emit only cancel. Sending Return
			// and Escape together can select and leave a menu in the same frame.
			if (joystick[j].cancel)
				push_key(cancel);
			else if (joystick[j].confirm)
				push_key(confirm);
		}
		
		for (uint d = 0; d < COUNTOF(joystick[j].direction_pressed); d++)
		{
			if (joystick[j].direction_pressed[d])
				push_key(direction[d]);
		}
	}
}

bool joystick_menu_button_down(JoystickMenuButton which)
{
	for (int j = 0; j < joysticks; j++)
	{
		const int num = joystick[j].menu_button[which];
		if (joystick[j].handle != NULL && num >= 0 &&
		    SDL_JoystickGetButton(joystick[j].handle, num) != 0)
		{
			prompt_use_pad(j);
			return true;
		}
	}
	return false;
}

void joystick_claim_menu_buttons(unsigned mask)
{
	menu_buttons_claimed = mask;
}

bool joystick_binding_claimed(int j, const Joystick_assignment *binding)
{
	return binding->type == BUTTON && joystick_raw_claimed(j, joystick_binding_raw(j, binding));
}

bool joystick_menu_actions_fixed(int j)
{
	const unsigned fixed = (1u << JOYSTICK_FACE_CONFIRM) | (1u << JOYSTICK_FACE_BACK);
	return (menu_buttons_claimed & fixed) == fixed && j >= 0 && j < joysticks &&
	       joystick[j].menu_button[JOYSTICK_FACE_CONFIRM] >= 0 &&
	       joystick[j].menu_button[JOYSTICK_FACE_BACK] >= 0;
}

int joystick_shoulder_step(bool was[2])
{
	static const JoystickMenuButton shoulder[2] = { JOYSTICK_SHOULDER_LEFT,
	                                                JOYSTICK_SHOULDER_RIGHT };

	int edge = 0;
	for (int side = 0; side < 2; ++side)
	{
		const bool down = joystick_menu_button_down(shoulder[side]);
		if (down && !was[side] && edge == 0)
			edge = (side == 0) ? -1 : 1;
		was[side] = down;
	}
	return edge;
}

int joystick_page_step(bool was[2])
{
	bool back = false, forward = false;
	for (int j = 0; j < joysticks; j++)
	{
		back = back || joystick[j].action_pressed[2];
		forward = forward || joystick[j].action_pressed[3];
	}
	const int shoulder = joystick_shoulder_step(was);
	back = back || shoulder < 0;
	forward = forward || shoulder > 0;
	return (int)forward - (int)back;
}

static PadLabels joycon_labels(Uint16 vendor, Uint16 product)
{
	if (vendor != PAD_VENDOR_NINTENDO)
		return PAD_LABELS_COUNT;

	switch (product)
	{
	case PAD_PRODUCT_JOYCON_LEFT:  return PAD_LABELS_JOYCON_LEFT;
	case PAD_PRODUCT_JOYCON_RIGHT: return PAD_LABELS_JOYCON_RIGHT;
	case PAD_PRODUCT_JOYCON_PAIR:  return PAD_LABELS_NINTENDO;
	default:                       return PAD_LABELS_COUNT;
	}
}

static bool joystick_nintendo_face(PadLabels labels)
{
	return labels == PAD_LABELS_NINTENDO || joystick_sideways_joycon(labels);
}

#ifdef JOYSTICK_HOTPLUG
static SDL_GameController *pad_open(int j)
{
	if (!pad_database)
		return NULL;

	int index = -1;
	const SDL_JoystickID id = SDL_JoystickInstanceID(joystick[j].handle);
	for (int d = 0; d < SDL_NumJoysticks() && index < 0; d++)
		if (SDL_JoystickGetDeviceInstanceID(d) == id)
			index = d;
	if (index < 0 || !SDL_IsGameController(index))
		return NULL;

	return SDL_GameControllerOpen(index);
}

static PadLabels pad_labels(SDL_GameController *pad)
{
	const PadLabels joycon = joycon_labels(SDL_GameControllerGetVendor(pad),
	                                       SDL_GameControllerGetProduct(pad));
#ifdef SDL_HINT_JOYSTICK_HIDAPI_VERTICAL_JOY_CONS
	if (SDL_GetHintBoolean(SDL_HINT_JOYSTICK_HIDAPI_VERTICAL_JOY_CONS, SDL_FALSE) &&
	    joycon != PAD_LABELS_COUNT)
		return PAD_LABELS_NINTENDO;
#endif
	if (joycon != PAD_LABELS_COUNT)
		return joycon;

	switch (SDL_GameControllerGetType(pad))
	{
	case SDL_CONTROLLER_TYPE_XBOX360:
		return PAD_LABELS_XBOX360;
	case SDL_CONTROLLER_TYPE_PS3:
		return PAD_LABELS_PS3;
	case SDL_CONTROLLER_TYPE_PS4:
		return PAD_LABELS_PS4;
#if SDL_VERSION_ATLEAST(2, 0, 14)
	case SDL_CONTROLLER_TYPE_PS5:
		return PAD_LABELS_PS5;
#endif
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
	case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
		return PAD_LABELS_NINTENDO;
	default:
		return PAD_LABELS_XBOX;
	}
}

static bool pad_names_by_label(PadLabels labels)
{
	return joystick_nintendo_face(labels) &&
	       SDL_GetHintBoolean(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, SDL_TRUE);
}

static PadSpot pad_button_spot(SDL_GameControllerButton button, bool byLabel)
{
	switch (button)
	{
	case SDL_CONTROLLER_BUTTON_A:             return byLabel ? PAD_SPOT_EAST : PAD_SPOT_SOUTH;
	case SDL_CONTROLLER_BUTTON_B:             return byLabel ? PAD_SPOT_SOUTH : PAD_SPOT_EAST;
	case SDL_CONTROLLER_BUTTON_X:             return byLabel ? PAD_SPOT_NORTH : PAD_SPOT_WEST;
	case SDL_CONTROLLER_BUTTON_Y:             return byLabel ? PAD_SPOT_WEST : PAD_SPOT_NORTH;
	case SDL_CONTROLLER_BUTTON_BACK:          return PAD_SPOT_BACK;
	case SDL_CONTROLLER_BUTTON_GUIDE:         return PAD_SPOT_GUIDE;
	case SDL_CONTROLLER_BUTTON_START:         return PAD_SPOT_START;
	case SDL_CONTROLLER_BUTTON_LEFTSTICK:     return PAD_SPOT_LEFT_STICK_CLICK;
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK:    return PAD_SPOT_RIGHT_STICK_CLICK;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return PAD_SPOT_LEFT_SHOULDER;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return PAD_SPOT_RIGHT_SHOULDER;
	case SDL_CONTROLLER_BUTTON_DPAD_UP:       return PAD_SPOT_DPAD_UP;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return PAD_SPOT_DPAD_DOWN;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return PAD_SPOT_DPAD_LEFT;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return PAD_SPOT_DPAD_RIGHT;
#if SDL_VERSION_ATLEAST(2, 0, 14)
	case SDL_CONTROLLER_BUTTON_MISC1:         return PAD_SPOT_MISC;
	case SDL_CONTROLLER_BUTTON_PADDLE1:       return PAD_SPOT_PADDLE1;
	case SDL_CONTROLLER_BUTTON_PADDLE2:       return PAD_SPOT_PADDLE2;
	case SDL_CONTROLLER_BUTTON_PADDLE3:       return PAD_SPOT_PADDLE3;
	case SDL_CONTROLLER_BUTTON_PADDLE4:       return PAD_SPOT_PADDLE4;
#endif
	default:                                  return PAD_SPOT_NONE;
	}
}

static PadSpot pad_axis_spot(SDL_GameControllerAxis axis)
{
	switch (axis)
	{
	case SDL_CONTROLLER_AXIS_LEFTX:
	case SDL_CONTROLLER_AXIS_LEFTY:        return PAD_SPOT_LEFT_STICK;
	case SDL_CONTROLLER_AXIS_RIGHTX:
	case SDL_CONTROLLER_AXIS_RIGHTY:       return PAD_SPOT_RIGHT_STICK;
	case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  return PAD_SPOT_LEFT_TRIGGER;
	case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return PAD_SPOT_RIGHT_TRIGGER;
	default:                               return PAD_SPOT_NONE;
	}
}

static void pad_note_spot(Joystick *js, SDL_GameControllerButtonBind bind, PadSpot spot)
{
	if (bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON &&
	    bind.value.button >= 0 && bind.value.button < JOYSTICK_SPOT_BUTTONS)
		js->button_spot[bind.value.button] = (Sint8)spot;
	else if (bind.bindType == SDL_CONTROLLER_BINDTYPE_AXIS &&
	         bind.value.axis >= 0 && bind.value.axis < JOYSTICK_SPOT_AXES)
		js->axis_spot[bind.value.axis] = (Sint8)spot;
}

static void pad_note_spots(Joystick *js, SDL_GameController *pad)
{
	js->labels = pad_labels(pad);
	const bool byLabel = pad_names_by_label(js->labels);
	for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b)
		pad_note_spot(js, SDL_GameControllerGetBindForButton(pad, (SDL_GameControllerButton)b),
		              pad_button_spot((SDL_GameControllerButton)b, byLabel));
	for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; ++a)
		pad_note_spot(js, SDL_GameControllerGetBindForAxis(pad, (SDL_GameControllerAxis)a),
		              pad_axis_spot((SDL_GameControllerAxis)a));
}
#endif

#if defined(__SWITCH__)
static const Sint8 console_button_spot[] = {
	PAD_SPOT_EAST, PAD_SPOT_SOUTH, PAD_SPOT_NORTH, PAD_SPOT_WEST,
	PAD_SPOT_LEFT_STICK_CLICK, PAD_SPOT_RIGHT_STICK_CLICK,
	PAD_SPOT_LEFT_SHOULDER, PAD_SPOT_RIGHT_SHOULDER, PAD_SPOT_LEFT_TRIGGER, PAD_SPOT_RIGHT_TRIGGER,
	PAD_SPOT_START, PAD_SPOT_BACK,
	PAD_SPOT_DPAD_LEFT, PAD_SPOT_DPAD_UP, PAD_SPOT_DPAD_RIGHT, PAD_SPOT_DPAD_DOWN,
};

static const Sint8 switch_joycon_button_spot[] = {
	PAD_SPOT_EAST, PAD_SPOT_SOUTH, PAD_SPOT_NORTH, PAD_SPOT_WEST,
	PAD_SPOT_NONE, PAD_SPOT_NONE, PAD_SPOT_NONE, PAD_SPOT_NONE,
	PAD_SPOT_LEFT_SHOULDER, PAD_SPOT_RIGHT_SHOULDER, PAD_SPOT_LEFT_STICK_CLICK, PAD_SPOT_START,
	PAD_SPOT_DPAD_LEFT, PAD_SPOT_DPAD_UP, PAD_SPOT_DPAD_RIGHT, PAD_SPOT_DPAD_DOWN,
};

static PadLabels switch_slot_labels(int j)
{
	if (!switch_slot_single_joycon(j))
		return PAD_LABELS_NINTENDO;
	return switch_slot_left_joycon(j) ? PAD_LABELS_JOYCON_LEFT : PAD_LABELS_JOYCON_RIGHT;
}
#elif defined(__vita__)
static const Sint8 console_button_spot[] = {
	PAD_SPOT_NORTH, PAD_SPOT_EAST, PAD_SPOT_SOUTH, PAD_SPOT_WEST,
	PAD_SPOT_LEFT_SHOULDER, PAD_SPOT_RIGHT_SHOULDER,
	PAD_SPOT_DPAD_DOWN, PAD_SPOT_DPAD_LEFT, PAD_SPOT_DPAD_UP, PAD_SPOT_DPAD_RIGHT,
	PAD_SPOT_BACK, PAD_SPOT_START,
};
#endif

static void joystick_learn_layout(int j)
{
	Joystick *const js = &joystick[j];
	memset(js->button_spot, PAD_SPOT_NONE, sizeof(js->button_spot));
	memset(js->axis_spot, PAD_SPOT_NONE, sizeof(js->axis_spot));

#if defined(__SWITCH__) || defined(__vita__)
	memcpy(js->button_spot, console_button_spot, sizeof(console_button_spot));
	js->axis_spot[0] = js->axis_spot[1] = PAD_SPOT_LEFT_STICK;
	js->axis_spot[2] = js->axis_spot[3] = PAD_SPOT_RIGHT_STICK;
#endif
#if defined(__SWITCH__)
	js->labels = switch_slot_labels(j);
	if (joystick_sideways_joycon(js->labels))
	{
		const bool left = js->labels == PAD_LABELS_JOYCON_LEFT;
		memcpy(js->button_spot, switch_joycon_button_spot, sizeof(switch_joycon_button_spot));
		js->button_spot[SWITCH_JOYCON_LR_BUTTON] = left ? PAD_SPOT_PADDLE2 : PAD_SPOT_PADDLE1;
		js->button_spot[SWITCH_JOYCON_ZLZR_BUTTON] = left ? PAD_SPOT_PADDLE4 : PAD_SPOT_PADDLE3;
	}
	else
	{
		js->button_spot[SWITCH_JOYCON_LR_BUTTON] = PAD_SPOT_LEFT_SHOULDER;
		js->button_spot[SWITCH_JOYCON_ZLZR_BUTTON] = PAD_SPOT_LEFT_TRIGGER;
	}
#elif defined(__vita__)
	js->labels = PAD_LABELS_VITA;
#else
	js->labels = PAD_LABELS_GENERIC;
	SDL_GameController *const pad = pad_open(j);
	if (pad != NULL)
	{
		pad_note_spots(js, pad);
		SDL_GameControllerClose(pad);
	}
#endif

	const bool nintendo = joystick_nintendo_face(js->labels);
	const PadSpot menuSpot[JOYSTICK_MENU_BUTTON_COUNT] = {
		PAD_SPOT_NORTH, PAD_SPOT_WEST, PAD_SPOT_LEFT_SHOULDER, PAD_SPOT_RIGHT_SHOULDER,
		nintendo ? PAD_SPOT_EAST : PAD_SPOT_SOUTH, nintendo ? PAD_SPOT_SOUTH : PAD_SPOT_EAST,
	};
	for (int which = 0; which < JOYSTICK_MENU_BUTTON_COUNT; ++which)
	{
		js->menu_button[which] = -1;
		for (int raw = 0; raw < JOYSTICK_SPOT_BUTTONS && js->menu_button[which] < 0; ++raw)
			if (js->button_spot[raw] == menuSpot[which])
				js->menu_button[which] = raw;
	}
}

// Open SDL device `index` into slot j with its saved bindings, or defaults for a new device.
static void joystick_open(int j, int index)
{
	memset(&joystick[j], 0, sizeof(*joystick));
	joystick[j].guid = SDL_JoystickGetDeviceGUID(index);

	joystick[j].handle = SDL_JoystickOpen(index);
	if (joystick[j].handle == NULL)
		return;
	++joystick_changes;

	printf("joystick detected: %s ", SDL_JoystickName(joystick[j].handle));
	printf("(%d axes, %d buttons, %d hats)\n",
	       SDL_JoystickNumAxes(joystick[j].handle),
	       SDL_JoystickNumButtons(joystick[j].handle),
	       SDL_JoystickNumHats(joystick[j].handle));

	joystick_learn_layout(j);

#if defined(__SWITCH__) || defined(__vita__)
	// There is no visible stdout on the consoles; mirror the controller capabilities
	// to a file in the user dir so mapping problems can be diagnosed. Harmless anywhere.
	{
		FILE *jl = dir_fopen(get_user_directory(), "joystick_info.txt", j == 0 ? "w" : "a");
		if (jl)
		{
			fprintf(jl, "joystick %d: %s (%d axes, %d buttons, %d hats)\n",
			        j, SDL_JoystickName(joystick[j].handle),
			        SDL_JoystickNumAxes(joystick[j].handle),
			        SDL_JoystickNumButtons(joystick[j].handle),
			        SDL_JoystickNumHats(joystick[j].handle));
			fclose(jl);
		}
	}
#endif

	if (!load_joystick_assignments(&opentyrian_config, j))
		reset_joystick_assignments(j);

	// A pad can still be running from a session that ended mid-pulse.
	if (joystick_rumble_engaged(j))
		joystick_motor(j, 0, 0, 0);
}

// initializes SDL joystick system and loads assignments for joysticks found
void init_joysticks(void)
{
	if (ignore_joystick)
		return;

#if defined(__ANDROID__) || defined(TARGET_IOS)
	// Ignore mobile accelerometers so the default axis mapping does not steer menus by tilt.
	SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
#endif
	// Bluetooth PS4 and PS5 pads rumble, and report that they can, only in an extended report
	// mode SDL leaves off by default. The pad keeps it until it is switched off.
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS
	SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS, joycons_separate ? "0" : "1");
#endif

	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK))
	{
		fprintf(stderr, "warning: failed to initialize joystick system: %s\n", SDL_GetError());
		ignore_joystick = true;
		return;
	}

#ifdef JOYSTICK_HOTPLUG
	// The controller database only supplies default bindings; input stays on the joystick API.
	pad_database = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0;
	if (pad_database)
		SDL_GameControllerEventState(SDL_IGNORE);
#endif

	SDL_JoystickEventState(SDL_IGNORE);

	joysticks = SDL_NumJoysticks();

#ifdef __SWITCH__
	// switch-sdl2 always lists eight players, connected or not; the game seats two at most.
	if (joysticks > 2)
		joysticks = 2;
#endif

	joystick = malloc_die(joysticks * sizeof(*joystick));

	for (int j = 0; j < joysticks; j++)
		joystick_open(j, j);

	joysticks_ready = true;

	const int first = joystick_nth_connected(0);
	if (first >= 0)
		prompt_use_pad(first);

	if (joysticks == 0)
		printf("no joysticks detected\n");
}

bool joystick_pause_opens_menu(int j)
{
#ifdef __SWITCH__
	return twoPlayerMode || switch_slot_single_joycon(j);
#else
	(void)j;
	return false;
#endif
}

bool joystick_connected(int j)
{
	if (j < 0 || j >= joysticks || joystick[j].handle == NULL)
		return false;
#ifdef __SWITCH__
	return switch_slot_connected(j);
#else
	return SDL_JoystickGetAttached(joystick[j].handle);
#endif
}

PadSpot joystick_binding_spot(int j, const Joystick_assignment *binding)
{
	if (j < 0 || j >= joysticks)
		return PAD_SPOT_NONE;

	switch (binding->type)
	{
	case BUTTON:
	{
		const int num = joystick_binding_raw(j, binding);
		return (num >= 0 && num < JOYSTICK_SPOT_BUTTONS) ? (PadSpot)joystick[j].button_spot[num]
		                                                 : PAD_SPOT_NONE;
	}
	case AXIS:
		return (binding->num >= 0 && binding->num < JOYSTICK_SPOT_AXES)
		       ? (PadSpot)joystick[j].axis_spot[binding->num] : PAD_SPOT_NONE;
	case HAT:
		if (binding->x_axis)
			return binding->negative_axis ? PAD_SPOT_DPAD_LEFT : PAD_SPOT_DPAD_RIGHT;
		return binding->negative_axis ? PAD_SPOT_DPAD_UP : PAD_SPOT_DPAD_DOWN;
	default:
		return PAD_SPOT_NONE;
	}
}

bool joystick_sideways_joycon(PadLabels labels)
{
	return labels == PAD_LABELS_JOYCON_LEFT || labels == PAD_LABELS_JOYCON_RIGHT;
}

bool joystick_joycons_connected(void)
{
	for (int j = 0; j < joysticks; j++)
		if (joystick_connected(j) &&
		    joycon_labels(SDL_JoystickGetVendor(joystick[j].handle),
		                  SDL_JoystickGetProduct(joystick[j].handle)) != PAD_LABELS_COUNT)
			return true;
	return false;
}

void joystick_set_joycons_separate(bool separate)
{
	joycons_separate = separate;
	joystick_reopen_all();
}

int joystick_nth_connected(int n)
{
	for (int j = 0; j < joysticks; j++)
		if (joystick_connected(j) && n-- == 0)
			return j;
	return -1;
}

#ifdef JOYSTICK_HOTPLUG
static bool joystick_instance_open(SDL_JoystickID id)
{
	for (int j = 0; j < joysticks; j++)
		if (joystick[j].handle != NULL && SDL_JoystickInstanceID(joystick[j].handle) == id)
			return true;
	return false;
}

// A device takes back the closed slot it last had, so its player and bindings survive a
// reconnect. Slots never move or shrink.
static int joystick_slot_for(SDL_JoystickGUID guid)
{
	for (int j = 0; j < joysticks; j++)
		if (joystick[j].handle == NULL && memcmp(&joystick[j].guid, &guid, sizeof(guid)) == 0)
			return j;

	if (joysticks >= JOYSTICK_SLOTS_MAX)
	{
		for (int j = 0; j < joysticks; j++)
			if (joystick[j].handle == NULL)
				return j;
		return -1;
	}

	Joystick *const grown = realloc(joystick, (size_t)(joysticks + 1) * sizeof(*joystick));
	if (grown == NULL)
		return -1;
	joystick = grown;
	memset(&joystick[joysticks], 0, sizeof(*joystick));
	return joysticks++;
}

static void joystick_hotplug(void)
{
	for (int j = 0; j < joysticks; j++)
	{
		if (joystick[j].handle == NULL || SDL_JoystickGetAttached(joystick[j].handle))
			continue;

		// Keep the bindings for a reconnect, and drop live input so nothing stays held.
		save_joystick_assignments(&opentyrian_config, j);
		SDL_JoystickClose(joystick[j].handle);

		Joystick *const js = &joystick[j];
		js->handle = NULL;
		++joystick_changes;
		js->x = js->y = 0;
		memset(js->analog_direction, 0, sizeof(js->analog_direction));
		memset(js->direction, 0, sizeof(js->direction));
		memset(js->direction_pressed, 0, sizeof(js->direction_pressed));
		memset(js->action, 0, sizeof(js->action));
		memset(js->action_pressed, 0, sizeof(js->action_pressed));
		js->confirm = js->cancel = js->input_pressed = false;
		memset(&js->rumble_event, 0, sizeof(js->rumble_event));
		memset(&js->rumble_flow, 0, sizeof(js->rumble_flow));
		js->rumble_lo = js->rumble_hi = 0;
		js->rumble_stops = 0;
	}

	for (int index = 0; index < SDL_NumJoysticks(); index++)
	{
		const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(index);
		if (id < 0 || joystick_instance_open(id))
			continue;

		const int j = joystick_slot_for(SDL_JoystickGetDeviceGUID(index));
		if (j < 0)
			continue;
		joystick_open(j, index);
		if (joystick[j].handle != NULL)
			prompt_use_pad(j);
	}
}
#endif

void joystick_rescan(void)
{
#ifdef JOYSTICK_HOTPLUG
	if (!joysticks_ready)
		return;

	// Joystick events are ignored, so nothing else runs SDL's device detection.
	SDL_JoystickUpdate();
	joystick_hotplug();
#endif
}

void joystick_service(void)
{
	if (!joysticks_ready)
		return;

	const Uint32 now = SDL_GetTicks();

	static Uint32 next_scan = 0;
	if (SDL_TICKS_PASSED(now, next_scan))
	{
		next_scan = now + 1000;
		joystick_rescan();
	}

#ifdef __SWITCH__
	for (int j = 0; j < joysticks; j++)
	{
		if (joystick[j].handle != NULL && joystick[j].labels != switch_slot_labels(j))
		{
			joystick_learn_layout(j);
			++joystick_changes;
		}
	}
#endif

	// The game ends each pulse itself. SDL would end one only inside SDL_JoystickUpdate, which
	// screens without a controller poll never call, and the Switch has no timer at all.
	for (int j = 0; j < joysticks; j++)
		if (joystick[j].handle != NULL)
			joystick_rumble_mix(j, now);
}

unsigned joystick_change_count(void)
{
	return joystick_changes;
}

void joystick_reopen_all(void)
{
	if (ignore_joystick || !joysticks_ready)
		return;

	const PromptDevice device = prompt_device();
	deinit_joysticks();
	init_joysticks();
	if (device == PROMPT_KEYBOARD)
		prompt_use_keyboard();
	else if (device == PROMPT_TOUCH)
		prompt_use_touch();
}

// deinitializes SDL joystick system and saves joystick assignments
void deinit_joysticks(void)
{
	if (ignore_joystick)
		return;

	for (int j = 0; j < joysticks; j++)
	{
		if (joystick[j].handle != NULL)
		{
			if (joystick_rumble_engaged(j))
				joystick_motor(j, 0, 0, 0);
			save_joystick_assignments(&opentyrian_config, j);
			SDL_JoystickClose(joystick[j].handle);
		}
	}

	free(joystick);
	joystick = NULL;
	joysticks = 0;
	joysticks_ready = false;
	++joystick_changes;

#ifdef JOYSTICK_HOTPLUG
	if (pad_database)
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
	pad_database = false;
#endif
	SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
}

#ifdef JOYSTICK_HOTPLUG
// One controller-database binding as an assignment. SDL2 does not say which half of an axis a
// binding uses, so directions read up and left as the negative half.
static void pad_assignment(SDL_GameControllerButtonBind bind, bool negative,
                           Joystick_assignment *out)
{
	switch (bind.bindType)
	{
	case SDL_CONTROLLER_BINDTYPE_BUTTON:
		out->type = BUTTON;
		out->num = bind.value.button;
		break;
	case SDL_CONTROLLER_BINDTYPE_AXIS:
		out->type = AXIS;
		out->num = bind.value.axis;
		out->negative_axis = negative;
		break;
	case SDL_CONTROLLER_BINDTYPE_HAT:
		out->type = HAT;
		out->num = bind.value.hat.hat;
		out->x_axis = (bind.value.hat.hat_mask & (SDL_HAT_LEFT | SDL_HAT_RIGHT)) != 0;
		out->negative_axis = (bind.value.hat.hat_mask & (SDL_HAT_LEFT | SDL_HAT_UP)) != 0;
		break;
	default:
		break;
	}
}

// Bind a pad SDL's controller database knows by its button labels; the left stick and d-pad
// steer. Returns false, changing nothing, for a pad the database does not know.
static bool pad_default_assignments(int j)
{
	static const SDL_GameControllerButton dpad[4] = {
		SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
		SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
	};
	static const SDL_GameControllerButton actions[6][2] = {
		{ SDL_CONTROLLER_BUTTON_A,     SDL_CONTROLLER_BUTTON_INVALID },        // fire
		{ SDL_CONTROLLER_BUTTON_B,     SDL_CONTROLLER_BUTTON_INVALID },        // change fire
		{ SDL_CONTROLLER_BUTTON_X,     SDL_CONTROLLER_BUTTON_LEFTSHOULDER },   // left sidekick
		{ SDL_CONTROLLER_BUTTON_Y,     SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },  // right sidekick
		{ SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_INVALID },        // menu
		{ SDL_CONTROLLER_BUTTON_BACK,  SDL_CONTROLLER_BUTTON_INVALID },        // pause
	};

	SDL_GameController *const pad = pad_open(j);
	if (pad == NULL)
		return false;

	Joystick_assignment (*const as)[2] = joystick[j].assignment;
	memset(joystick[j].assignment, 0, sizeof(joystick[j].assignment));  // NONE everywhere

	for (int d = 0; d < 4; d++)
	{
		const bool x_axis = d == 1 || d == 3;
		const bool negative = d == 0 || d == 3;
		const SDL_GameControllerAxis stick = x_axis ? SDL_CONTROLLER_AXIS_LEFTX
		                                            : SDL_CONTROLLER_AXIS_LEFTY;
		pad_assignment(SDL_GameControllerGetBindForAxis(pad, stick), negative, &as[d][0]);
		pad_assignment(SDL_GameControllerGetBindForButton(pad, dpad[d]), negative, &as[d][1]);
	}
	for (int a = 0; a < 6; a++)
	{
		for (int s = 0; s < 2; s++)
		{
			if (actions[a][s] == SDL_CONTROLLER_BUTTON_INVALID)
				continue;
			const SDL_GameControllerButtonBind bind =
				SDL_GameControllerGetBindForButton(pad, actions[a][s]);
			pad_assignment(bind, false, &as[4 + a][s]);
		}
	}

	SDL_GameControllerClose(pad);
	return true;
}
#endif

#if defined(__SWITCH__)
// switch-sdl2 button order: 12=Left, 13=Up, 14=Right, 15=Down.
static const int console_dpad_btn[4] = { 13, 14, 15, 12 };  // up, right, down, left

static int switch_second_button(size_t a)
{
	// ZL and ZR (8/9) fire the sidekicks too; switch-sdl2 puts a sideways Joy-Con's SL
	// and SR there, the shoulder buttons of a single Joy-Con.
	if (a == 6 || a == 7)
		return a == 6 ? 8 : 9;
	// A single Joy-Con's own ZL or ZR changes the rear weapon mode.
	if (a == 5)
		return SWITCH_JOYCON_ZLZR_BUTTON;
	return -1;
}
#elif defined(__vita__)
// Vita SDL button order: 6=Down, 7=Left, 8=Up, 9=Right.
static const int console_dpad_btn[4] = { 8, 9, 6, 7 };      // up, right, down, left
#endif

static int default_action_button(size_t a)
{
#if defined(__vita__)
	// Vita defaults: Cross fire, Circle change/cancel, shoulders sidekicks, Start menu.
	static const int vita_action_btn[6] = { 2, 1, 4, 5, 11, 10 };
	return vita_action_btn[a - 4];
#else
#if defined(__SWITCH__)
	// Buttons 4/5 are analog-stick clicks in switch-sdl2 and are unsuitable for
	// "menu" and "pause". Use Plus (10) and Minus (11), the natural system buttons.
	if (a == 8 || a == 9)  // assignment_names[8] = "menu", [9] = "pause"
		return (a == 8) ? 10 : 11;
#endif
	return (int)(a - 4);
#endif
}

void reset_joystick_assignments(int j)
{
	assert(j < joysticks);

	if (joystick[j].handle == NULL)
		return;  // a disconnected slot keeps its bindings for the reconnect

	joystick[j].analog = false;
	joystick[j].sensitivity = 5;
	joystick[j].threshold = 5;

#ifdef JOYSTICK_HOTPLUG
	if (pad_default_assignments(j))
		return;
#endif

	// defaults: first 2 axes, first hat, first 6 buttons
	for (uint a = 0; a < COUNTOF(joystick[j].assignment); a++)
	{
		// clear assignments
		for (uint i = 0; i < COUNTOF(joystick[j].assignment[a]); i++)
			joystick[j].assignment[a][i].type = NONE;
		
		if (a < 4)
		{
			if (SDL_JoystickNumAxes(joystick[j].handle) >= 2)
			{
				joystick[j].assignment[a][0].type = AXIS;
				joystick[j].assignment[a][0].num = (a + 1) % 2;
				joystick[j].assignment[a][0].negative_axis = (a == 0 || a == 3);
			}
			
			if (SDL_JoystickNumHats(joystick[j].handle) >= 1)
			{
				joystick[j].assignment[a][1].type = HAT;
				joystick[j].assignment[a][1].num = 0;
				joystick[j].assignment[a][1].x_axis = (a == 1 || a == 3);
				joystick[j].assignment[a][1].negative_axis = (a == 0 || a == 3);
			}
#if defined(__SWITCH__) || defined(__vita__)
			// Consoles expose the d-pad as buttons, so bind it in the second assignment slot.
			else
			{
				if (console_dpad_btn[a] < SDL_JoystickNumButtons(joystick[j].handle))
				{
					joystick[j].assignment[a][1].type = BUTTON;
					joystick[j].assignment[a][1].num = console_dpad_btn[a];
				}
			}
#endif
		}
		else
		{
			const int btn = default_action_button(a);
			if (btn < (int)SDL_JoystickNumButtons(joystick[j].handle))
			{
				joystick[j].assignment[a][0].type = BUTTON;
				joystick[j].assignment[a][0].num = btn;
			}
#if defined(__SWITCH__)
			if (switch_second_button(a) >= 0)
			{
				joystick[j].assignment[a][1].type = BUTTON;
				joystick[j].assignment[a][1].num = switch_second_button(a);
			}
#endif
		}
	}
}

static const char* const assignment_names[] =
{
	"up",
	"right",
	"down",
	"left",
	"fire",
	"change fire",
	"left sidekick",
	"right sidekick",
	"menu",
	"pause",
};

static bool load_joystick_assignments(Config *config, int j)
{
	char name[64];
	ConfigSection *section = config_find_section(config, "joystick",
	                                             joystick_config_name(j, name, sizeof(name)));
	if (section == NULL)
		return false;
	
	if (!config_get_bool_option(section, "analog", &joystick[j].analog))
		joystick[j].analog = false;
	
	joystick[j].sensitivity = config_get_or_set_int_option(section, "sensitivity", 5);

	joystick[j].threshold = config_get_or_set_int_option(section, "threshold", 5);
	
	for (size_t a = 0; a < COUNTOF(assignment_names); ++a)
	{
		for (unsigned int i = 0; i < COUNTOF(joystick[j].assignment[a]); ++i)
			joystick[j].assignment[a][i].type = NONE;
		
		ConfigOption *option = config_get_option(section, assignment_names[a]);
		if (option == NULL)
			continue;
		
		foreach_option_i_value(i, value, option)
		{
			if (i >= COUNTOF(joystick[j].assignment[a]))
				break;

			code_to_assignment(&joystick[j].assignment[a][i], value);
		}
	}

	// Fill missing menu and pause bindings in older controller configs.
	for (size_t a = 8; a <= 9; ++a)  // assignment_names: 8 = "menu", 9 = "pause"
	{
		bool bound = false;
		for (unsigned int i = 0; i < COUNTOF(joystick[j].assignment[a]); ++i)
			if (joystick[j].assignment[a][i].type != NONE)
				bound = true;

		const int btn = default_action_button(a);
		if (!bound && btn < SDL_JoystickNumButtons(joystick[j].handle))
		{
			joystick[j].assignment[a][0].type = BUTTON;
			joystick[j].assignment[a][0].num = btn;
		}
	}

#if defined(__SWITCH__) || defined(__vita__)
	// Backfill console d-pad buttons when an old configuration has only analog
	// direction bindings. Direction order is up, right, down, left.
	for (size_t a = 0; a < 4; ++a)
	{
		bool has_digital = false;
		for (unsigned int i = 0; i < COUNTOF(joystick[j].assignment[a]); ++i)
			if (joystick[j].assignment[a][i].type == BUTTON ||
			    joystick[j].assignment[a][i].type == HAT)
				has_digital = true;

		if (!has_digital && joystick[j].assignment[a][1].type == NONE
		    && console_dpad_btn[a] < SDL_JoystickNumButtons(joystick[j].handle))
		{
			joystick[j].assignment[a][1].type = BUTTON;
			joystick[j].assignment[a][1].num = console_dpad_btn[a];
		}
	}
#endif
#if defined(__SWITCH__)
	// Older configs leave the second slots of change fire (5) and the sidekicks (6, 7) empty:
	// give them their reset_joystick_assignments defaults.
	for (size_t a = 5; a <= 7; ++a)
	{
		if (joystick[j].assignment[a][1].type == NONE)
		{
			joystick[j].assignment[a][1].type = BUTTON;
			joystick[j].assignment[a][1].num = switch_second_button(a);
		}
	}
#endif

	return true;
}

bool save_joystick_assignments(Config *config, int j)
{
	if (joystick[j].handle == NULL)
		return false;  // disconnected; its bindings were saved when it went

	char name[64];
	const char *const section_name = joystick_config_name(j, name, sizeof(name));
	ConfigSection *section = config_find_or_add_section(config, "joystick", section_name);
	if (section == NULL)
		exit(EXIT_FAILURE);  // out of memory
	
	config_set_bool_option(section, "analog", joystick[j].analog, NO_YES);
	
	config_set_int_option(section, "sensitivity", joystick[j].sensitivity);
	
	config_set_int_option(section, "threshold", joystick[j].threshold);
	
	for (size_t a = 0; a < COUNTOF(assignment_names); ++a)
	{
		ConfigOption *option = config_set_option(section, assignment_names[a], NULL);
		if (option == NULL)
			exit(EXIT_FAILURE);  // out of memory
		
		option = config_set_value(option, NULL);
		if (option == NULL)
			exit(EXIT_FAILURE);  // out of memory

		for (size_t i = 0; i < COUNTOF(joystick[j].assignment[a]); ++i)
		{
			if (joystick[j].assignment[a][i].type == NONE)
				continue;
			
			option = config_add_value(option, assignment_to_code(&joystick[j].assignment[a][i]));
			if (option == NULL)
				exit(EXIT_FAILURE);  // out of memory
		}
	}
	
	return true;
}

// fills buffer with comma separated list of assigned joystick functions
void joystick_assignments_to_string(char *buffer, size_t buffer_len, const Joystick_assignment *assignments)
{
	strncpy(buffer, "", buffer_len);
	
	bool comma = false;
	for (uint i = 0; i < COUNTOF(*joystick->assignment); ++i)
	{
		if (assignments[i].type == NONE)
			continue;
		
		size_t len = snprintf(buffer, buffer_len, "%s%s",
		                      comma ? ", " : "",
		                      assignment_to_code(&assignments[i]));
		buffer += len;
		buffer_len -= len;
		
		comma = true;
	}
}

// reverse of assignment_to_code()
static void code_to_assignment(Joystick_assignment *assignment, const char *buffer)
{
	memset(assignment, 0, sizeof(*assignment));
	
	char axis = 0, direction = 0;
	
	if (sscanf(buffer, " AX %d%c", &assignment->num, &direction) == 2)
		assignment->type = AXIS;
	else if (sscanf(buffer, " BTN %d", &assignment->num) == 1)
		assignment->type = BUTTON;
	else if (sscanf(buffer, " H %d%c%c", &assignment->num, &axis, &direction) == 3)
		assignment->type = HAT;
	
	if (assignment->num == 0)
		assignment->type = NONE;
	else
		--assignment->num;
	
	assignment->x_axis = (toupper(axis) == 'X');
	assignment->negative_axis = (toupper(direction) == '-');
}

/* Returns a six-character code so two bindings fit beside one action. */
static const char *assignment_to_code(const Joystick_assignment *assignment)
{
	static char name[7];
	
	switch (assignment->type)
	{
	case NONE:
		strcpy(name, "");
		break;
		
	case AXIS:
		snprintf(name, sizeof(name), "AX %d%c",
		         assignment->num + 1,
		         assignment->negative_axis ? '-' : '+');
		break;
		
	case BUTTON:
		snprintf(name, sizeof(name), "BTN %d",
		         assignment->num + 1);
		break;
		
	case HAT:
		snprintf(name, sizeof(name), "H %d%c%c",
		         assignment->num + 1,
		         assignment->x_axis ? 'X' : 'Y',
		         assignment->negative_axis ? '-' : '+');
		break;
	}
	
	return name;
}

/* Captures joystick j; input from other joysticks is ignored. */
bool detect_joystick_assignment(int j, Joystick_assignment *assignment)
{
	if (joystick[j].handle == NULL)
		return false;

	/* Capture the initial state for edge detection. */
	const int axes = SDL_JoystickNumAxes(joystick[j].handle);
	Sint16 *axis = malloc_die(axes * sizeof(*axis));
	for (int i = 0; i < axes; i++)
		axis[i] = SDL_JoystickGetAxis(joystick[j].handle, i);
	
	const int buttons = SDL_JoystickNumButtons(joystick[j].handle);
	Uint8 *button = malloc_die(buttons * sizeof(*button));
	for (int i = 0; i < buttons; i++)
		button[i] = SDL_JoystickGetButton(joystick[j].handle, i);
	
	const int hats = SDL_JoystickNumHats(joystick[j].handle);
	Uint8 *hat = malloc_die(hats * sizeof(*hat));
	for (int i = 0; i < hats; i++)
		hat[i] = SDL_JoystickGetHat(joystick[j].handle, i);
	
#ifdef __SWITCH__
	unsigned joycon = switch_slot_joycon_extra(j);  // buttons 28 and 29, which SDL leaves out
#endif

	bool detected = false;

	// The controller "confirm" that opened this prompt pushed a synthetic RETURN
	// (push_joysticks_as_keyboard) that is still queued.
	service_SDL_events(true);
	newkey = newmouse = false;

	do
	{
		setDelay(1);

		// The capture blocks until a control arrives; online, the wait can outlast the
		// peer's timeout unless the connection is serviced through it.
		NETWORK_KEEP_ALIVE();
		while (network_shop_pump())
			;

		SDL_JoystickUpdate();
		if (joystick[j].handle == NULL)
			break;

		for (int i = 0; i < axes; ++i)
		{
			Sint16 temp = SDL_JoystickGetAxis(joystick[j].handle, i);
			
			if (abs(temp - axis[i]) > joystick_analog_max * 2 / 3)
			{
				assignment->type = AXIS;
				assignment->num = i;
				assignment->negative_axis = temp < axis[i];
				detected = true;
				break;
			}
		}
		
		for (int i = 0; i < buttons; ++i)
		{
			Uint8 new_button = SDL_JoystickGetButton(joystick[j].handle, i),
			      changed = button[i] ^ new_button;
			
			if (!changed)
				continue;
			
			if (new_button == 0) // button was released
			{
				button[i] = new_button;
			}
			else                 // button was pressed
			{
				assignment->type = BUTTON;
				assignment->num = i;
#ifdef __SWITCH__
				if ((i == 8 || i == 9) && joystick_2p_joycon_layout(j))
					assignment->num = SWITCH_JOYCON_ZLZR_BUTTON;
#endif
				detected = true;
				break;
			}
		}

#ifdef __SWITCH__
		const unsigned joycon_now = switch_slot_joycon_extra(j);
		const unsigned joycon_pressed = joycon_now & ~joycon;
		joycon = joycon_now;
		if (!detected && joycon_pressed != 0)
		{
			assignment->type = BUTTON;
			assignment->num = (joycon_pressed & SWITCH_JOYCON_LR) ? SWITCH_JOYCON_LR_BUTTON
			                                                    : SWITCH_JOYCON_ZLZR_BUTTON;
			detected = true;
		}
#endif
		
		for (int i = 0; i < hats; ++i)
		{
			Uint8 new_hat = SDL_JoystickGetHat(joystick[j].handle, i),
			      changed = hat[i] ^ new_hat;
			
			if (!changed)
				continue;
			
			if ((new_hat & changed) == SDL_HAT_CENTERED) // hat was centered
			{
				hat[i] = new_hat;
			}
			else
			{
				assignment->type = HAT;
				assignment->num = i;
				assignment->x_axis = changed & (SDL_HAT_LEFT | SDL_HAT_RIGHT);
				assignment->negative_axis = changed & (SDL_HAT_LEFT | SDL_HAT_UP);
				detected = true;
			}
		}
		
		service_SDL_events(true);
		JE_showVGA();
		
		wait_delay();
	} while (!detected && !newkey && !newmouse);
	
	free(axis);
	free(button);
	free(hat);
	
	return detected;
}

// compares relevant parts of joystick assignments for equality
bool joystick_assignment_cmp(const Joystick_assignment *a, const Joystick_assignment *b)
{
	if (a->type == b->type)
	{
		switch (a->type)
		{
		case NONE:
			return true;
		case AXIS:
			return (a->num == b->num) &&
			       (a->negative_axis == b->negative_axis);
		case BUTTON:
			return (a->num == b->num);
		case HAT:
			return (a->num == b->num) &&
			       (a->x_axis == b->x_axis) &&
			       (a->negative_axis == b->negative_axis);
		}
	}
	
	return false;
}
