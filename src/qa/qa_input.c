#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "joystick.h"

#include "SDL.h"

#include <stdio.h>

#if !defined(__SWITCH__) && !defined(__vita__) && SDL_VERSION_ATLEAST(2, 24, 0)
static Uint16 qaPadRumbleLow, qaPadRumbleHigh;

static int SDLCALL qa_pad_rumble(void *userdata, Uint16 low, Uint16 high)
{
	(void)userdata;
	qaPadRumbleLow = low;
	qaPadRumbleHigh = high;
	return 0;
}

static int qa_pad_attach(void)
{
	SDL_VirtualJoystickDesc desc;
	SDL_zero(desc);
	desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
	desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
	desc.naxes = SDL_CONTROLLER_AXIS_MAX;
	desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;  // numbered in SDL_GameControllerButton order
	desc.name = "OpenTyrian QA Pad";
	desc.Rumble = qa_pad_rumble;
	return SDL_JoystickAttachVirtualEx(&desc);
}

static void qa_pad_detach(SDL_Joystick *pad)
{
	const SDL_JoystickID id = SDL_JoystickInstanceID(pad);
	for (int d = 0; d < SDL_NumJoysticks(); d++)
		if (SDL_JoystickGetDeviceInstanceID(d) == id)
			SDL_JoystickDetachVirtual(d);
}

// Hold one virtual button for a poll. `action` is a Joystick action index, or ~direction.
static bool qa_pad_button(int slot, SDL_GameControllerButton button, int action)
{
	SDL_JoystickSetVirtualButton(joystick[slot].handle, button, SDL_PRESSED);
	poll_joystick(slot);
	const bool held = action >= 0 ? joystick[slot].action[action]
	                              : joystick[slot].direction[~action];
	SDL_JoystickSetVirtualButton(joystick[slot].handle, button, SDL_RELEASED);
	poll_joystick(slot);
	return held;
}
#endif

/* Controllers that connect after startup, with bindings from SDL's controller database. */
void qa_test_controller_hotplug(void)
{
#if !defined(__SWITCH__) && !defined(__vita__) && SDL_VERSION_ATLEAST(2, 24, 0)
	const bool savedIgnore = ignore_joystick;
	const int savedRumble = rumble_strength;

	ignore_joystick = false;
	init_joysticks();
	const int slotsBefore = joysticks;
	int connectedBefore = 0;
	while (joystick_nth_connected(connectedBefore) >= 0)
		++connectedBefore;

	if (qa_pad_attach() < 0)
	{
		fprintf(stderr, "# controller hotplug skipped: no virtual joystick (%s)\n", SDL_GetError());
		deinit_joysticks();
		ignore_joystick = savedIgnore;
		return;
	}

	joystick_rescan();
	const int slot = joysticks - 1;
	const bool opened = joysticks == slotsBefore + 1 && joystick[slot].handle != NULL;
	qa_check(opened, "a controller connected after startup is opened");

	if (opened)
	{
		qa_check(joystick_nth_connected(connectedBefore) == slot &&
		         joystick_nth_connected(connectedBefore + 1) == -1,
		         "a new controller is the next connected one, the way Destruct counts pads");

		const struct
		{
			SDL_GameControllerButton button;
			int action;
			const char *what;
		} presses[] = {
			{ SDL_CONTROLLER_BUTTON_A,             0, "A fires" },
			{ SDL_CONTROLLER_BUTTON_B,             1, "B changes the rear weapon mode" },
			{ SDL_CONTROLLER_BUTTON_X,             2, "X fires the left sidekick" },
			{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  2, "left shoulder fires the left sidekick" },
			{ SDL_CONTROLLER_BUTTON_Y,             3, "Y fires the right sidekick" },
			{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 3, "right shoulder fires the right sidekick" },
			{ SDL_CONTROLLER_BUTTON_START,         4, "Start opens the menu" },
			{ SDL_CONTROLLER_BUTTON_BACK,          5, "Back pauses" },
			{ SDL_CONTROLLER_BUTTON_DPAD_UP,      ~0, "the d-pad steers up" },
			{ SDL_CONTROLLER_BUTTON_DPAD_RIGHT,   ~1, "the d-pad steers right" },
			{ SDL_CONTROLLER_BUTTON_DPAD_DOWN,    ~2, "the d-pad steers down" },
			{ SDL_CONTROLLER_BUTTON_DPAD_LEFT,    ~3, "the d-pad steers left" },
		};
		for (size_t i = 0; i < COUNTOF(presses); ++i)
			qa_check(qa_pad_button(slot, presses[i].button, presses[i].action), presses[i].what);

		SDL_Joystick *const pad = joystick[slot].handle;
		SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, -32768);
		SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTX, 32767);
		poll_joystick(slot);
		qa_check(joystick[slot].direction[0] && joystick[slot].direction[1],
		         "the left stick steers up and right");
		SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, 0);
		SDL_JoystickSetVirtualAxis(pad, SDL_CONTROLLER_AXIS_LEFTX, 0);
		poll_joystick(slot);

		qa_check(joystick_can_rumble(), "a controller with a motor offers the Rumble setting");
		rumble_strength = RUMBLE_LIGHT;
		joystick_rumble(0, 1.0f, 0.5f, 50);
		qa_check(qaPadRumbleLow == 0x7FFF && qaPadRumbleHigh == 0x3FFF,
		         "Light rumble runs the motors at half strength");
		rumble_strength = RUMBLE_STRONG;
		joystick_rumble(0, 1.0f, 0.5f, 50);
		qa_check(qaPadRumbleLow == 0xFFFF && qaPadRumbleHigh == 0x7FFF,
		         "Strong rumble runs the motors at full strength");
		joystick_rumble_level(0, 0.5f, 0.75f, 50);
		qa_check(qaPadRumbleLow == 0xFFFF && qaPadRumbleHigh == 0xBFFF,
		         "a pulse and a held level run each motor at the louder of the two");
		joystick_rumble_stop_all();
		qa_check(qaPadRumbleLow == 0 && qaPadRumbleHigh == 0,
		         "stopping all rumble silences the motors");
		qaPadRumbleLow = qaPadRumbleHigh = 1;
		joystick_rumble_stop_all();
		qa_check(qaPadRumbleLow == 0 && qaPadRumbleHigh == 0,
		         "a stop still reaches a controller SDL believes is idle");

		joystick_rumble(0, 1.0f, 0.5f, 1);
		const Uint32 pulseEnd = SDL_GetTicks() + 3;
		while (!SDL_TICKS_PASSED(SDL_GetTicks(), pulseEnd))
			SDL_Delay(1);
		joystick_service();
		qa_check(qaPadRumbleLow == 0 && qaPadRumbleHigh == 0,
		         "the game stops a pulse once it ends");
		qaPadRumbleLow = qaPadRumbleHigh = 1;
		rumble_strength = RUMBLE_OFF;
		joystick_rumble(0, 1.0f, 0.5f, 50);
		qa_check(qaPadRumbleLow == 1 && qaPadRumbleHigh == 1, "Rumble Off leaves the motors alone");

		// Unplug it mid-press: the slot closes and nothing stays held.
		SDL_JoystickSetVirtualButton(pad, SDL_CONTROLLER_BUTTON_A, SDL_PRESSED);
		poll_joystick(slot);
		qa_pad_detach(pad);
		joystick_rescan();
		qa_check(joystick[slot].handle == NULL && !joystick[slot].action[0],
		         "an unplugged controller closes without leaving fire held");
		qa_check(joystick_nth_connected(connectedBefore) == -1,
		         "an unplugged controller no longer counts as connected");

		const bool reattached = qa_pad_attach() >= 0;
		joystick_rescan();
		qa_check(reattached && joysticks == slotsBefore + 1 && joystick[slot].handle != NULL,
		         "a reconnected controller takes back its own slot");
		if (reattached && joystick[slot].handle != NULL)
		{
			qa_check(qa_pad_button(slot, SDL_CONTROLLER_BUTTON_START, 4),
			         "a reconnected controller keeps its bindings");
			qa_pad_detach(joystick[slot].handle);
			joystick_rescan();
		}
	}

	deinit_joysticks();
	for (int d = SDL_NumJoysticks() - 1; d >= 0; --d)
		if (SDL_JoystickIsVirtual(d))
			SDL_JoystickDetachVirtual(d);
	ignore_joystick = savedIgnore;
	rumble_strength = savedRumble;
#endif
}
