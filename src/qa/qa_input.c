#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "debug_levels.h"
#include "debug_menu.h"
#include "destruct.h"
#include "endless.h"
#include "game_menu.h"
#include "help.h"
#include "helptext.h"
#include "ingame_menu.h"
#include "joystick.h"
#include "jukebox.h"
#include "keyboard.h"
#include "net_start.h"
#include "prompt.h"
#include "setup_menu.h"
#include "ship_editor.h"
#include "sprite_picker.h"
#include "sprite_viewer.h"
#include "weapon_editor.h"

#include "SDL.h"

#include <stdio.h>
#include <string.h>

#if !defined(__SWITCH__) && !defined(__vita__) && SDL_VERSION_ATLEAST(2, 24, 0)
static const char qaPadName[] = "OpenTyrian QA Pad";
static Uint16 qaPadRumbleLow, qaPadRumbleHigh;

static int SDLCALL qa_pad_rumble(void *userdata, Uint16 low, Uint16 high)
{
	(void)userdata;
	qaPadRumbleLow = low;
	qaPadRumbleHigh = high;
	return 0;
}

static int qa_pad_attach(const char *name, Uint16 vendor, Uint16 product)
{
	SDL_VirtualJoystickDesc desc;
	SDL_zero(desc);
	desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
	desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
	desc.naxes = SDL_CONTROLLER_AXIS_MAX;
	desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;  // numbered in SDL_GameControllerButton order
	desc.vendor_id = vendor;
	desc.product_id = product;
	desc.name = name;
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

static void qa_pad_hold(int slot, const SDL_GameControllerButton *buttons, size_t count, bool down)
{
	for (size_t i = 0; i < count; ++i)
		SDL_JoystickSetVirtualButton(joystick[slot].handle, buttons[i],
		                             down ? SDL_PRESSED : SDL_RELEASED);
	poll_joystick(slot);
}

// Hold one virtual button for a poll. `action` is a Joystick action index, or ~direction.
static bool qa_pad_button(int slot, SDL_GameControllerButton button, int action)
{
	qa_pad_hold(slot, &button, 1, true);
	const bool held = action >= 0 ? joystick[slot].action[action]
	                              : joystick[slot].direction[~action];
	qa_pad_hold(slot, &button, 1, false);
	return held;
}

static int qa_pad_page_step(int slot, const SDL_GameControllerButton *buttons, size_t count)
{
	qa_pad_hold(slot, buttons, count, true);
	bool was[2] = { false, false };
	const int step = joystick_page_step(was);
	qa_pad_hold(slot, buttons, count, false);
	return step;
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
	const bool joyconsBefore = joystick_joycons_connected();
	const unsigned changesBefore = joystick_change_count();

	if (qa_pad_attach(qaPadName, 0, 0) < 0)
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
	qa_check(joystick_change_count() != changesBefore,
	         "a controller connecting wakes menus that wait for input, so they redraw");
	qa_check(joystick_joycons_connected() == joyconsBefore,
	         "an ordinary controller leaves the Setup Joy-Cons row hidden");

	if (opened)
	{
		char line[64];
		prompt_expand(line, sizeof(line), "{confirm} {back} {north} {west} {pages}");
		qa_check(prompt_device() == PROMPT_PAD && prompt_pad_labels() == PAD_LABELS_XBOX &&
		         strcmp(line, "A B Y X LB/RB") == 0,
		         "plugging in a controller switches the prompts to its buttons");

		Joystick_assignment savedFire[2];
		memcpy(savedFire, joystick[slot].assignment[4], sizeof(savedFire));
		joystick[slot].assignment[4][0] =
			(Joystick_assignment){ .type = BUTTON, .num = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER };
		const bool rebound = strcmp(prompt_expand(line, sizeof(line), "{confirm}"), "RB") == 0;
		memcpy(joystick[slot].assignment[4], savedFire, sizeof(savedFire));
		qa_check(rebound, "a prompt names the button the player bound, not the default one");

		prompt_use_keyboard();
		const bool typed = prompt_device() == PROMPT_KEYBOARD;
		prompt_use_pad(slot);
		push_key(SDL_SCANCODE_RETURN);
		service_SDL_events(true);
		service_SDL_events(true);
		const bool synthesized = prompt_device() == PROMPT_PAD;
		prompt_use_keyboard();
		qa_pad_button(slot, SDL_CONTROLLER_BUTTON_A, 0);
		qa_check(typed && synthesized && prompt_device() == PROMPT_PAD,
		         "the keyboard takes the prompts back, its synthesized keys do not, "
		         "a pad press does");

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

		const SDL_GameControllerButton left = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
		const SDL_GameControllerButton right = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
		const SDL_GameControllerButton sidekick = SDL_CONTROLLER_BUTTON_X;
		const SDL_GameControllerButton confirm = SDL_CONTROLLER_BUTTON_A;
		const SDL_GameControllerButton shoulders[2] = { left, right };
		qa_check(qa_pad_page_step(slot, &left, 1) == -1,
		         "the left shoulder turns a picker back a tab");
		qa_check(qa_pad_page_step(slot, &right, 1) == 1,
		         "the right shoulder turns a picker forward a tab");
		qa_check(qa_pad_page_step(slot, &sidekick, 1) == -1,
		         "X, the other left sidekick button, turns a picker back a tab too");
		qa_check(qa_pad_page_step(slot, &confirm, 1) == 0, "A picks without turning the tab");
		qa_check(qa_pad_page_step(slot, shoulders, 2) == 0,
		         "both shoulders at once leave the tab where it is");

		const int *const menu = joystick[slot].menu_button;
		qa_check(menu[JOYSTICK_FACE_NORTH] == SDL_CONTROLLER_BUTTON_Y &&
		         menu[JOYSTICK_FACE_WEST] == SDL_CONTROLLER_BUTTON_X &&
		         menu[JOYSTICK_SHOULDER_LEFT] == SDL_CONTROLLER_BUTTON_LEFTSHOULDER &&
		         menu[JOYSTICK_SHOULDER_RIGHT] == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER &&
		         menu[JOYSTICK_FACE_CONFIRM] == SDL_CONTROLLER_BUTTON_A &&
		         menu[JOYSTICK_FACE_BACK] == SDL_CONTROLLER_BUTTON_B,
		         "a known controller's face and shoulder buttons are found by position");

		const SDL_GameControllerButton north = SDL_CONTROLLER_BUTTON_Y;
		const SDL_GameControllerButton west = SDL_CONTROLLER_BUTTON_X;
		const SDL_GameControllerButton cancel = SDL_CONTROLLER_BUTTON_B;
		joystick_claim_menu_buttons(jukebox_claimed_buttons());
		qa_pad_hold(slot, &north, 1, true);
		const bool northSeen = joystick_menu_button_down(JOYSTICK_FACE_NORTH);
		const bool northFree = !joystick[slot].action[3];
		qa_pad_hold(slot, &north, 1, false);
		qa_pad_hold(slot, &confirm, 1, true);
		const bool confirmSeen = joystick_menu_button_down(JOYSTICK_FACE_CONFIRM);
		qa_pad_hold(slot, &confirm, 1, false);
		qa_check(northSeen && northFree && confirmSeen && joystick_menu_actions_fixed(slot) &&
		         !qa_pad_button(slot, west, 2) && !qa_pad_button(slot, left, 2) &&
		         !qa_pad_button(slot, confirm, 0),
		         "the jukebox reads its own buttons by position and ignores the gameplay bindings");

		Joystick_assignment savedPause[2];
		memcpy(savedPause, joystick[slot].assignment[9], sizeof(savedPause));
		joystick[slot].assignment[9][0] = (Joystick_assignment){ .type = BUTTON, .num = north };
		joystick[slot].assignment[9][1].type = NONE;
		qa_pad_hold(slot, &north, 1, true);
		const bool claimedQuits = joystick[slot].cancel;
		qa_pad_hold(slot, &north, 1, false);
		qa_pad_hold(slot, &cancel, 1, true);
		const bool backSeen = joystick_menu_button_down(JOYSTICK_FACE_BACK);
		const bool backBinding = joystick[slot].cancel;
		qa_pad_hold(slot, &cancel, 1, false);
		qa_check(!claimedQuits && backSeen && !backBinding,
		         "a jukebox button bound to pause does not quit it; B quits as its back button");

		joystick_claim_menu_buttons(0);
		qa_pad_hold(slot, &north, 1, true);
		const bool releasedQuits = joystick[slot].cancel;
		qa_pad_hold(slot, &north, 1, false);
		memcpy(joystick[slot].assignment[9], savedPause, sizeof(savedPause));
		qa_check(releasedQuits, "outside the jukebox the same button pauses again");

		Joystick_assignment savedRows[3][2];
		memcpy(savedRows, &joystick[slot].assignment[4], sizeof(savedRows));
		const PadLabels savedLabels = joystick[slot].labels;
		joystick[slot].labels = PAD_LABELS_VITA;
		joystick[slot].assignment[5][0] = (Joystick_assignment){ .type = BUTTON, .num = north };
		joystick[slot].assignment[5][1].type = NONE;
		prompt_use_pad(slot);
		static const char jukeSongs[] =
			"{back} quits, {north} switches to sounds, {west} hides the text.";
		static const char vitaSongs[] =
			"Circle quits, Triangle switches to sounds, Square hides the text.";
		static const char vitaSongsUnclaimed[] =
			"Start quits, Triangle switches to sounds, Square hides the text.";
		char juke[3][96];
		joystick_claim_menu_buttons(jukebox_claimed_buttons());
		prompt_expand(juke[0], sizeof(juke[0]), jukeSongs);
		prompt_expand(juke[1], sizeof(juke[1]),
		              "{pageleft} mutes the music, {north} switches to songs, {back} quits.");
		joystick_claim_menu_buttons(0);
		prompt_expand(juke[2], sizeof(juke[2]), jukeSongs);
		static const char vitaSounds[] =
			"L mutes the music, Triangle switches to songs, Circle quits.";
		qa_check(strcmp(juke[0], vitaSongs) == 0 && strcmp(juke[1], vitaSounds) == 0,
		         "with change fire on Triangle, the Vita jukebox quits with Circle and says so");
		qa_check(strcmp(juke[2], vitaSongsUnclaimed) == 0,
		         "a line never names one button for two jobs; back moves to another binding");

		joystick[slot].labels = savedLabels;
		joystick[slot].assignment[4][0] = (Joystick_assignment){ .type = BUTTON, .num = confirm };
		joystick[slot].assignment[4][1].type = NONE;
		joystick[slot].assignment[5][0] = joystick[slot].assignment[4][0];
		static const PromptText shared = {
			"Enter picks. Esc cancels.", "{confirm} picks. {back} cancels.", NULL
		};
		char line2[64];
		prompt_text(line2, sizeof(line2), &shared);
		qa_check(strcmp(line2, "Enter picks. Esc cancels.") == 0,
		         "a button that both fires and cancels is never offered as confirm");
		memcpy(&joystick[slot].assignment[4], savedRows, sizeof(savedRows));
		qa_check(strcmp(prompt_expand(line2, sizeof(line2), "{rsidekick} {north}"), "RB Y") == 0,
		         "a token moves to its other binding when the first one is named for another job");

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
		const unsigned changesPlugged = joystick_change_count();
		qa_pad_detach(pad);
		joystick_rescan();
		qa_check(joystick[slot].handle == NULL && !joystick[slot].action[0],
		         "an unplugged controller closes without leaving fire held");
		qa_check(joystick_change_count() != changesPlugged,
		         "a controller leaving wakes menus that wait for input too");
		qa_check(joystick_nth_connected(connectedBefore) == -1,
		         "an unplugged controller no longer counts as connected");
		qa_check(connectedBefore > 0 || prompt_device() == PROMPT_KEYBOARD,
		         "unplugging the only controller switches the prompts back to the keyboard");

		const bool reattached = qa_pad_attach(qaPadName, 0, 0) >= 0;
		joystick_rescan();
		qa_check(reattached && joysticks == slotsBefore + 1 && joystick[slot].handle != NULL,
		         "a reconnected controller takes back its own slot");
		qa_check(!reattached || prompt_device() == PROMPT_PAD,
		         "a reconnected controller takes the prompts back too");
		if (reattached && joystick[slot].handle != NULL)
		{
			qa_check(qa_pad_button(slot, SDL_CONTROLLER_BUTTON_START, 4),
			         "a reconnected controller keeps its bindings");
			qa_pad_detach(joystick[slot].handle);
			joystick_rescan();
		}
	}

	const struct
	{
		Uint16 product;
		PadLabels labels;
		const char *menuButton;
	} joycons[] = {
		{ PAD_PRODUCT_JOYCON_LEFT,  PAD_LABELS_JOYCON_LEFT,  "Minus" },
		{ PAD_PRODUCT_JOYCON_RIGHT, PAD_LABELS_JOYCON_RIGHT, "Plus" },
	};
	for (size_t i = 0; i < COUNTOF(joycons); ++i)
	{
		const int slotsNow = joysticks;
		const bool attached = qa_pad_attach("Nintendo Switch Joy-Con", PAD_VENDOR_NINTENDO,
		                                    joycons[i].product) >= 0;
		joystick_rescan();
		const int joycon = joysticks - 1;
		const bool plugged = attached && joysticks == slotsNow + 1 &&
		                     joystick[joycon].handle != NULL;
		qa_check(plugged && joystick_joycons_connected(),
		         "a single Joy-Con connected after startup is opened and offers the Joy-Cons row");
		if (!plugged)
			continue;

		char line[64], want[64], row[32];
		prompt_expand(line, sizeof(line), "{confirm} {back} {north} {west} {pages} {menu} {dpad}");
		snprintf(want, sizeof(want), "(Right) (Down) (Up) (Left) SL/SR %s stick",
		         joycons[i].menuButton);
		prompt_assignments_label(row, sizeof(row), joycon, joystick[joycon].assignment[6], 76);
		qa_check(prompt_pad_labels() == joycons[i].labels && strcmp(line, want) == 0 &&
		         strcmp(row, "(Up), SL") == 0,
		         "a sideways Joy-Con names its buttons by position and its SL and SR");

		const int *const menu = joystick[joycon].menu_button;
		qa_check(qa_pad_button(joycon, SDL_CONTROLLER_BUTTON_A, 0) &&
		         menu[JOYSTICK_SHOULDER_LEFT] == SDL_CONTROLLER_BUTTON_LEFTSHOULDER &&
		         menu[JOYSTICK_SHOULDER_RIGHT] == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
		         "a sideways Joy-Con fires with its right button and turns pages with SL and SR");

		qa_pad_detach(joystick[joycon].handle);
		joystick_rescan();
		qa_check(joystick_joycons_connected() == joyconsBefore,
		         "unplugging the Joy-Con hides the Setup Joy-Cons row again");
	}

	static const Uint16 proControllerProduct = 0x2009;
	const int slotsNow = joysticks;
	if (qa_pad_attach("Nintendo Switch Pro Controller", PAD_VENDOR_NINTENDO,
	                  proControllerProduct) >= 0)
	{
		joystick_rescan();
		const bool pro = joysticks == slotsNow + 1 && joystick[joysticks - 1].handle != NULL;
		qa_check(pro && joystick_joycons_connected() == joyconsBefore,
		         "a Pro Controller is not a Joy-Con, so the Setup Joy-Cons row stays hidden");
		if (pro)
			qa_pad_detach(joystick[joysticks - 1].handle);
		joystick_rescan();
	}

	const bool savedSeparate = joycons_separate;
	prompt_use_keyboard();
	joystick_set_joycons_separate(true);
	const char *const split = SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS);
	const bool separated = split != NULL && strcmp(split, "0") == 0 &&
	                       prompt_device() == PROMPT_KEYBOARD;
	joystick_set_joycons_separate(false);
	const char *const paired = SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_COMBINE_JOY_CONS);
	joycons_separate = savedSeparate;
	qa_check(separated && paired != NULL && strcmp(paired, "1") == 0,
	         "the Joy-Cons setting reaches SDL, and reopening the controllers keeps the "
	         "keyboard's prompts");

	deinit_joysticks();
	for (int d = SDL_NumJoysticks() - 1; d >= 0; --d)
		if (SDL_JoystickIsVirtual(d))
			SDL_JoystickDetachVirtual(d);
	ignore_joystick = savedIgnore;
	rumble_strength = savedRumble;
#endif
}

static int qa_prompt_length(const char *line, const void *context)
{
	(void)context;
	return (int)strlen(line);
}

static bool qa_prompt_says(PromptDevice device, PadLabels labels, const char *tmpl,
                           const char *want)
{
	char line[96];
	prompt_force(device, labels);
	const bool same = strcmp(prompt_expand(line, sizeof(line), tmpl), want) == 0;
	prompt_unforce();
	return same;
}

void qa_test_prompt_labels(void)
{
	static const char menuKeys[] = "{confirm} {back} {north} {west} {pages} {menu} {pause}";
	static const struct
	{
		PadLabels labels;
		const char *want;
	} families[] = {
		{ PAD_LABELS_XBOX360,  "A B Y X LB/RB Start Back" },
		{ PAD_LABELS_XBOX,     "A B Y X LB/RB Menu View" },
		{ PAD_LABELS_PS3,      "Cross Circle Triangle Square L1/R1 Start Select" },
		{ PAD_LABELS_PS4,      "Cross Circle Triangle Square L1/R1 Options Share" },
		{ PAD_LABELS_PS5,      "Cross Circle Triangle Square L1/R1 Options Create" },
		{ PAD_LABELS_NINTENDO, "A B X Y L/R Plus Minus" },
		{ PAD_LABELS_VITA,     "Cross Circle Triangle Square L/R Start Select" },
	};
	bool every = true;
	for (size_t i = 0; i < COUNTOF(families); ++i)
		every = every && qa_prompt_says(PROMPT_PAD, families[i].labels, menuKeys, families[i].want);
	qa_check(every, "each controller family names its confirm, back, face and shoulder buttons");

	static const char sidekicks[] = "{lsidekick} {rsidekick}";
	qa_check(qa_prompt_says(PROMPT_PAD, PAD_LABELS_NINTENDO, sidekicks, "X Y") &&
	         qa_prompt_says(PROMPT_PAD, PAD_LABELS_VITA, sidekicks, "L R") &&
	         qa_prompt_says(PROMPT_PAD, PAD_LABELS_PS4, sidekicks, "Square Triangle"),
	         "the sidekick buttons follow each platform's default layout");
	qa_check(qa_prompt_says(PROMPT_PAD, PAD_LABELS_GENERIC, "{confirm} {back}",
	                        "Button 1 Button 2"),
	         "an unknown controller's buttons go by number");

	static const PromptText twice = {
		"Y fires, Y quits.", "{rsidekick} fires, {north} quits.", NULL
	};
	const char *const caught = prompt_measure_fault(&twice, 1, qa_prompt_length, NULL, 1000,
	                                                "probe");
	qa_check(caught != NULL && strstr(caught, "two jobs") != NULL,
	         "the screen checks catch a line that names one button for two jobs");

	static const char joyconKeys[] = "{confirm} {back} {north} {west} {pages} {menu} {dpad}";
	qa_check(qa_prompt_says(PROMPT_PAD, PAD_LABELS_JOYCON_LEFT, joyconKeys,
	                        "(Right) (Down) (Up) (Left) SL/SR Minus stick") &&
	         qa_prompt_says(PROMPT_PAD, PAD_LABELS_JOYCON_RIGHT, joyconKeys,
	                        "(Right) (Down) (Up) (Left) SL/SR Plus stick") &&
	         qa_prompt_says(PROMPT_PAD, PAD_LABELS_NINTENDO, "{dpad}", "D-pad"),
	         "a sideways Joy-Con names its face buttons by position and steers with its stick");

	const SDL_Scancode savedFire = keySettings[KEY_SETTING_FIRE];
	keySettings[KEY_SETTING_FIRE] = SDL_SCANCODE_LCTRL;
	const bool keys = qa_prompt_says(PROMPT_KEYBOARD, PAD_LABELS_XBOX, "{confirm} {back} {fire}",
	                                 "Enter Esc Ctrl");
	keySettings[KEY_SETTING_FIRE] = savedFire;
	qa_check(keys, "the keyboard names Enter, Esc and the key bound to fire");
	qa_check(qa_prompt_says(PROMPT_TOUCH, PAD_LABELS_XBOX, "{confirm} {back}", "Tap X"),
	         "a touch screen taps to confirm and backs out with its X button");

	static const PromptText fallback = { "Esc quits.", "{north} quits.", NULL };
	char line[64];
	prompt_force(PROMPT_PAD, PAD_LABELS_GENERIC);
	const bool fell = strcmp(prompt_text(line, sizeof(line), &fallback), "Esc quits.") == 0;
	prompt_force(PROMPT_PAD, PAD_LABELS_PS5);
	const bool kept = strcmp(prompt_text(line, sizeof(line), &fallback), "Triangle quits.") == 0;
	prompt_unforce();
	qa_check(fell && kept,
	         "a controller missing a prompted button reads the keyboard text instead");
	qa_check(qa_prompt_says(PROMPT_PAD, PAD_LABELS_XBOX, "{unknown} {", "{unknown} {"),
	         "unknown or unclosed tokens are left as written");

	Joystick *const savedPads = joystick;
	const int savedCount = joysticks;
	Joystick fake;
	memset(&fake, 0, sizeof(fake));
	fake.labels = PAD_LABELS_PS5;
	memset(fake.button_spot, PAD_SPOT_NONE, sizeof(fake.button_spot));
	memset(fake.axis_spot, PAD_SPOT_NONE, sizeof(fake.axis_spot));
	fake.button_spot[0] = PAD_SPOT_SOUTH;
	fake.axis_spot[2] = PAD_SPOT_LEFT_TRIGGER;
	joystick = &fake;
	joysticks = 1;
	const Joystick_assignment cross = { .type = BUTTON, .num = 0 };
	const Joystick_assignment unknown = { .type = BUTTON, .num = 7 };
	const Joystick_assignment trigger = { .type = AXIS, .num = 2 };
	const Joystick_assignment hat = {
		.type = HAT, .num = 0, .x_axis = true, .negative_axis = true
	};
	char a[24], b[24], c[24], d[24];
	const bool named = strcmp(prompt_binding_label(a, sizeof(a), 0, &cross), "Cross") == 0 &&
	                   strcmp(prompt_binding_label(b, sizeof(b), 0, &unknown), "Button 8") == 0 &&
	                   strcmp(prompt_binding_label(c, sizeof(c), 0, &trigger), "L2") == 0 &&
	                   strcmp(prompt_binding_label(d, sizeof(d), 0, &hat), "Left") == 0;
	const Joystick_assignment pair[2] = { cross, trigger };
	const Joystick_assignment mixed[2] = { cross, unknown };
	char row[3][32];
	prompt_assignments_label(row[0], sizeof(row[0]), 0, pair, 76);
	prompt_assignments_label(row[1], sizeof(row[1]), 0, mixed, 76);
	prompt_assignments_label(row[2], sizeof(row[2]), 0, pair, 20);
	joystick = savedPads;
	joysticks = savedCount;
	qa_check(named, "a binding is named by where it sits on its controller, or by number");
	qa_check(strcmp(row[0], "Cross, L2") == 0 && strcmp(row[1], "BTN 1, BTN 8") == 0 &&
	         strcmp(row[2], "BTN 1, AX 3+") == 0,
	         "the joystick setup names a pad's bindings, keeping the codes for unknown or "
	         "wide rows");

	const char *(*const screens[])(void) = {
		jukebox_qa_help_fault,
		spritePickerHintFault,
		debugLevelsPromptFault,
		debug_menu_prompt_fault,
		sprite_viewer_prompt_fault,
		JE_shipEditorPromptFault,
		weaponEditorPromptFault,
		networkReadyPromptFault,
		destruct_prompt_fault,
		game_menu_prompt_fault,
		helptext_prompt_fault,
		help_prompt_fault,
		ingame_menu_prompt_fault,
		setup_menu_prompt_fault,
		endless_seed_prompt_fault,
	};
	for (size_t i = 0; i < COUNTOF(screens); ++i)
	{
		const char *const fault = screens[i]();
		qa_check(fault == NULL, fault != NULL ? fault : "a screen's prompts fit on every device");
	}
}

void qa_test_wrapped_lines(void)
{
	const char *(*const checks[])(void) = {
		helptext_wrap_fault,
		game_menu_cube_wrap_fault,
		spritePickerWrapFault,
	};
	for (size_t i = 0; i < COUNTOF(checks); ++i)
	{
		const char *const fault = checks[i]();
		qa_check(fault == NULL, fault != NULL ? fault : "no wrapped line starts with a space");
	}
}
