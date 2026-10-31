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
#include "prompt.h"

#include "config.h"
#include "font.h"
#include "sprite.h"
#include "touch_ui.h"

#include <stdio.h>
#include <string.h>

#if defined(__SWITCH__) || defined(__vita__)
#define PROMPT_HOME PROMPT_PAD
#elif defined(TOUCH_UI_BUTTONS)
#define PROMPT_HOME PROMPT_TOUCH
#else
#define PROMPT_HOME PROMPT_KEYBOARD
#endif

typedef enum
{
	PROMPT_KEY_CONFIRM,
	PROMPT_KEY_BACK,
	PROMPT_KEY_FIRE,
	PROMPT_KEY_CHANGE_FIRE,
	PROMPT_KEY_LEFT_SIDEKICK,
	PROMPT_KEY_RIGHT_SIDEKICK,
	PROMPT_KEY_MENU,
	PROMPT_KEY_PAUSE,
	PROMPT_KEY_NORTH,
	PROMPT_KEY_WEST,
	PROMPT_KEY_PAGE_LEFT,
	PROMPT_KEY_PAGE_RIGHT,
	PROMPT_KEY_PAGES,
	PROMPT_KEY_DPAD,
	PROMPT_KEY_COUNT
}
PromptKey;

#define PROMPT_BOUND_KEYS PROMPT_KEY_PAGES

static const char *const promptTokens[PROMPT_KEY_COUNT] = {
	"confirm", "back", "fire", "change", "lsidekick", "rsidekick", "menu", "pause",
	"north", "west", "pageleft", "pageright", "pages", "dpad",
};

static const Sint8 promptRows[PROMPT_KEY_PAUSE + 1][3] = {
	[PROMPT_KEY_CONFIRM]        = { 4, -1, -1 },
	[PROMPT_KEY_BACK]           = { 5, 8, 9 },
	[PROMPT_KEY_FIRE]           = { 4, -1, -1 },
	[PROMPT_KEY_CHANGE_FIRE]    = { 5, -1, -1 },
	[PROMPT_KEY_LEFT_SIDEKICK]  = { 6, -1, -1 },
	[PROMPT_KEY_RIGHT_SIDEKICK] = { 7, -1, -1 },
	[PROMPT_KEY_MENU]           = { 8, -1, -1 },
	[PROMPT_KEY_PAUSE]          = { 9, -1, -1 },
};

static const Sint8 promptRole[PROMPT_KEY_COUNT] = {
	[PROMPT_KEY_CONFIRM] = 0, [PROMPT_KEY_FIRE] = 0, [PROMPT_KEY_BACK] = 1,
	[PROMPT_KEY_CHANGE_FIRE] = 1, [PROMPT_KEY_LEFT_SIDEKICK] = 2, [PROMPT_KEY_RIGHT_SIDEKICK] = 3,
	[PROMPT_KEY_MENU] = 4, [PROMPT_KEY_PAUSE] = 5, [PROMPT_KEY_NORTH] = 6, [PROMPT_KEY_WEST] = 7,
	[PROMPT_KEY_PAGE_LEFT] = 8, [PROMPT_KEY_PAGE_RIGHT] = 8, [PROMPT_KEY_PAGES] = 8,
	[PROMPT_KEY_DPAD] = -1,
};

static const JoystickMenuButton promptMenuButton[] = {
	JOYSTICK_FACE_NORTH, JOYSTICK_FACE_WEST, JOYSTICK_SHOULDER_LEFT, JOYSTICK_SHOULDER_RIGHT,
};

typedef struct
{
	int identity[16];
	Sint8 role[16];
	int count;
}
PromptUsed;

static const char *const padSpotNames[PAD_LABELS_COUNT][PAD_SPOT_COUNT] = {
	[PAD_LABELS_XBOX360] = {
		"A", "B", "X", "Y", "Back", "Guide", "Start", "LS", "RS", "LB", "RB", "LT", "RT",
		"Up", "Down", "Left", "Right", "Left stick", "Right stick", "Share",
	},
	[PAD_LABELS_XBOX] = {
		"A", "B", "X", "Y", "View", "Xbox", "Menu", "LS", "RS", "LB", "RB", "LT", "RT",
		"Up", "Down", "Left", "Right", "Left stick", "Right stick", "Share",
	},
	[PAD_LABELS_PS3] = {
		"Cross", "Circle", "Square", "Triangle", "Select", "PS", "Start", "L3", "R3", "L1", "R1",
		"L2", "R2", "Up", "Down", "Left", "Right", "Left stick", "Right stick", "Mute",
	},
	[PAD_LABELS_PS4] = {
		"Cross", "Circle", "Square", "Triangle", "Share", "PS", "Options", "L3", "R3", "L1", "R1",
		"L2", "R2", "Up", "Down", "Left", "Right", "Left stick", "Right stick", "Mute",
	},
	[PAD_LABELS_PS5] = {
		"Cross", "Circle", "Square", "Triangle", "Create", "PS", "Options", "L3", "R3", "L1", "R1",
		"L2", "R2", "Up", "Down", "Left", "Right", "Left stick", "Right stick", "Mute",
	},
	[PAD_LABELS_NINTENDO] = {
		"B", "A", "Y", "X", "Minus", "Home", "Plus", "LS", "RS", "L", "R", "ZL", "ZR",
		"Up", "Down", "Left", "Right", "Left stick", "Right stick", "Capture",
	},
	[PAD_LABELS_JOYCON_LEFT] = {
		"(Down)", "(Right)", "(Left)", "(Up)", NULL, "Capture", "Minus", "LS", NULL, "SL", "SR",
		NULL, NULL, "Up", "Down", "Left", "Right", "Stick", NULL, NULL, NULL, "L", NULL, "ZL",
	},
	[PAD_LABELS_JOYCON_RIGHT] = {
		"(Down)", "(Right)", "(Left)", "(Up)", NULL, "Home", "Plus", "RS", NULL, "SL", "SR",
		NULL, NULL, "Up", "Down", "Left", "Right", "Stick", NULL, NULL, "R", NULL, "ZR", NULL,
	},
	[PAD_LABELS_VITA] = {
		"Cross", "Circle", "Square", "Triangle", "Select", "PS", "Start", "L3", "R3", "L", "R",
		"L2", "R2", "Up", "Down", "Left", "Right", "Left stick", "Right stick", "Mute",
	},
};

static const Sint8 padDefaultSpot[PAD_LABELS_COUNT][PROMPT_BOUND_KEYS] = {
#define PROMPT_DEFAULTS(fire, change, left, right) \
	{ fire, change, fire, change, left, right, PAD_SPOT_START, PAD_SPOT_BACK, PAD_SPOT_NORTH, \
	  PAD_SPOT_WEST, PAD_SPOT_LEFT_SHOULDER, PAD_SPOT_RIGHT_SHOULDER }
	[PAD_LABELS_XBOX360]      = PROMPT_DEFAULTS(PAD_SPOT_SOUTH, PAD_SPOT_EAST, PAD_SPOT_WEST, PAD_SPOT_NORTH),
	[PAD_LABELS_XBOX]         = PROMPT_DEFAULTS(PAD_SPOT_SOUTH, PAD_SPOT_EAST, PAD_SPOT_WEST, PAD_SPOT_NORTH),
	[PAD_LABELS_PS3]          = PROMPT_DEFAULTS(PAD_SPOT_SOUTH, PAD_SPOT_EAST, PAD_SPOT_WEST, PAD_SPOT_NORTH),
	[PAD_LABELS_PS4]          = PROMPT_DEFAULTS(PAD_SPOT_SOUTH, PAD_SPOT_EAST, PAD_SPOT_WEST, PAD_SPOT_NORTH),
	[PAD_LABELS_PS5]          = PROMPT_DEFAULTS(PAD_SPOT_SOUTH, PAD_SPOT_EAST, PAD_SPOT_WEST, PAD_SPOT_NORTH),
	[PAD_LABELS_NINTENDO]     = PROMPT_DEFAULTS(PAD_SPOT_EAST, PAD_SPOT_SOUTH, PAD_SPOT_NORTH, PAD_SPOT_WEST),
	[PAD_LABELS_JOYCON_LEFT]  = PROMPT_DEFAULTS(PAD_SPOT_EAST, PAD_SPOT_SOUTH, PAD_SPOT_NORTH, PAD_SPOT_WEST),
	[PAD_LABELS_JOYCON_RIGHT] = PROMPT_DEFAULTS(PAD_SPOT_EAST, PAD_SPOT_SOUTH, PAD_SPOT_NORTH, PAD_SPOT_WEST),
	[PAD_LABELS_VITA]         = PROMPT_DEFAULTS(PAD_SPOT_SOUTH, PAD_SPOT_EAST, PAD_SPOT_LEFT_SHOULDER,
	                                            PAD_SPOT_RIGHT_SHOULDER),
#undef PROMPT_DEFAULTS
};

static PromptDevice promptLast = PROMPT_HOME;
static int promptPad = -1;
static bool promptForced = false;
static PromptDevice promptForcedDevice = PROMPT_HOME;
static PadLabels promptForcedLabels = PAD_LABELS_XBOX;
static bool promptUnresolved = false;
static bool promptConflict = false;

void prompt_use_keyboard(void)
{
	promptLast = PROMPT_KEYBOARD;
}

void prompt_use_touch(void)
{
	promptLast = PROMPT_TOUCH;
}

void prompt_use_pad(int j)
{
	promptLast = PROMPT_PAD;
	promptPad = j;
}

static int prompt_slot(void)
{
	if (promptForced)
		return -1;
	if (joystick_connected(promptPad))
		return promptPad;
	return joystick_nth_connected(0);
}

PromptDevice prompt_device(void)
{
	if (promptForced)
		return promptForcedDevice;
#if defined(__SWITCH__) || defined(__vita__)
	return PROMPT_PAD;
#else
	if (promptLast == PROMPT_PAD && prompt_slot() < 0)
		return PROMPT_HOME;
	return promptLast;
#endif
}

PadLabels prompt_pad_labels(void)
{
	if (promptForced)
		return promptForcedLabels;
	const int j = prompt_slot();
	if (j >= 0)
		return joystick[j].labels;
#if defined(__SWITCH__)
	return PAD_LABELS_NINTENDO;
#elif defined(__vita__)
	return PAD_LABELS_VITA;
#else
	return PAD_LABELS_XBOX;
#endif
}

static const char *prompt_spot_name(PadLabels labels, PadSpot spot)
{
	if (spot <= PAD_SPOT_NONE || spot >= PAD_SPOT_COUNT)
		return NULL;
	return padSpotNames[labels][spot];
}

static const char *prompt_binding_name(int j, const Joystick_assignment *binding)
{
	const PadLabels labels = (j >= 0 && j < joysticks) ? joystick[j].labels : prompt_pad_labels();
	const PadSpot spot = joystick_binding_spot(j, binding);
	const char *name = prompt_spot_name(labels, spot);
	if (name == NULL && binding->type == HAT)
		name = prompt_spot_name(PAD_LABELS_XBOX, spot);
	return name;
}

const char *prompt_binding_label(char *buf, size_t size, int j, const Joystick_assignment *binding)
{
	const char *const name = prompt_binding_name(j, binding);
	if (name != NULL)
		snprintf(buf, size, "%s", name);
	else if (binding->type == BUTTON)
		snprintf(buf, size, "Button %d", binding->num + 1);
	else if (binding->type == AXIS)
		snprintf(buf, size, "Axis %d", binding->num + 1);
	else
		snprintf(buf, size, "-");
	return buf;
}

const char *prompt_assignments_label(char *buf, size_t size, int j,
                                     const Joystick_assignment *assignments, int width)
{
	char line[64] = "";
	bool named = true;
	for (size_t i = 0; i < COUNTOF(*joystick->assignment) && named; ++i)
	{
		if (assignments[i].type == NONE)
			continue;
		const char *const name = prompt_binding_name(j, &assignments[i]);
		named = name != NULL;
		if (named)
		{
			const size_t used = strlen(line);
			snprintf(line + used, sizeof(line) - used, "%s%s", used > 0 ? ", " : "", name);
		}
	}

	if (named && line[0] != '\0' && JE_textWidth(line, TINY_FONT) <= width)
		snprintf(buf, size, "%s", line);
	else
		joystick_assignments_to_string(buf, size, assignments);
	return buf;
}

static const char *prompt_scancode_name(SDL_Scancode scancode)
{
	switch (scancode)
	{
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:  return "Enter";
	case SDL_SCANCODE_ESCAPE:    return "Esc";
	case SDL_SCANCODE_LCTRL:
	case SDL_SCANCODE_RCTRL:     return "Ctrl";
	case SDL_SCANCODE_LSHIFT:
	case SDL_SCANCODE_RSHIFT:    return "Shift";
	case SDL_SCANCODE_LALT:
	case SDL_SCANCODE_RALT:      return "Alt";
	case SDL_SCANCODE_PAGEUP:    return "PgUp";
	case SDL_SCANCODE_PAGEDOWN:  return "PgDn";
	default:                     return SDL_GetScancodeName(scancode);
	}
}

static const char *prompt_keyboard_label(PromptKey key)
{
	static const int bound[] = {
		KEY_SETTING_FIRE, KEY_SETTING_CHANGE_FIRE, KEY_SETTING_LEFT_SIDEKICK,
		KEY_SETTING_RIGHT_SIDEKICK,
	};

	switch (key)
	{
	case PROMPT_KEY_CONFIRM:    return "Enter";
	case PROMPT_KEY_BACK:
	case PROMPT_KEY_MENU:       return "Esc";
	case PROMPT_KEY_PAUSE:      return "P";
	case PROMPT_KEY_PAGE_LEFT:  return "PgUp";
	case PROMPT_KEY_PAGE_RIGHT: return "PgDn";
	case PROMPT_KEY_PAGES:      return "PgUp/PgDn";
	case PROMPT_KEY_DPAD:       return "Arrows";
	case PROMPT_KEY_FIRE:
	case PROMPT_KEY_CHANGE_FIRE:
	case PROMPT_KEY_LEFT_SIDEKICK:
	case PROMPT_KEY_RIGHT_SIDEKICK:
	{
		const char *const name = prompt_scancode_name(keySettings[bound[key - PROMPT_KEY_FIRE]]);
		return (name[0] != '\0') ? name : NULL;
	}
	default:                    return NULL;
	}
}

static const char *prompt_touch_label(PromptKey key)
{
	switch (key)
	{
	case PROMPT_KEY_CONFIRM:
	case PROMPT_KEY_FIRE:        return "Tap";
	case PROMPT_KEY_BACK:        return "X";
	case PROMPT_KEY_MENU:
	case PROMPT_KEY_PAUSE:       return "Pause";
	case PROMPT_KEY_CHANGE_FIRE: return "Cycle";
	case PROMPT_KEY_DPAD:        return "Arrows";
	default:                     return NULL;
	}
}

static int prompt_identity(int j, const Joystick_assignment *binding)
{
	const PadSpot spot = joystick_binding_spot(j, binding);
	if (spot != PAD_SPOT_NONE)
		return spot;
	return PAD_SPOT_COUNT + ((binding->type * 256 + binding->num) * 2 + binding->x_axis) * 2 +
	       binding->negative_axis;
}

static bool prompt_conflicts(const PromptUsed *used, int role, int identity)
{
	for (int i = 0; i < used->count; ++i)
		if (used->identity[i] == identity && used->role[i] != role)
			return true;
	return false;
}

static void prompt_note_used(PromptUsed *used, int role, int identity)
{
	if (used->count < (int)COUNTOF(used->identity))
	{
		used->identity[used->count] = identity;
		used->role[used->count] = (Sint8)role;
		++used->count;
	}
}

static bool prompt_fixed_binding(int j, PromptKey key, Joystick_assignment *binding)
{
	JoystickMenuButton which;
	if (key >= PROMPT_KEY_NORTH && key <= PROMPT_KEY_PAGE_RIGHT)
		which = promptMenuButton[key - PROMPT_KEY_NORTH];
	else if ((key == PROMPT_KEY_CONFIRM || key == PROMPT_KEY_BACK) &&
	         joystick_menu_actions_fixed(j))
		which = (key == PROMPT_KEY_CONFIRM) ? JOYSTICK_FACE_CONFIRM : JOYSTICK_FACE_BACK;
	else
		return false;

	*binding = (Joystick_assignment){ .type = BUTTON, .num = joystick[j].menu_button[which] };
	return true;
}

static bool prompt_cancels(int j, int identity)
{
	const Sint8 *const rows = promptRows[PROMPT_KEY_BACK];
	for (size_t r = 0; r < COUNTOF(promptRows[PROMPT_KEY_BACK]); ++r)
	{
		const Joystick_assignment *const pair = joystick[j].assignment[rows[r]];
		for (int i = 0; i < 2; ++i)
			if (pair[i].type != NONE && prompt_identity(j, &pair[i]) == identity)
				return true;
	}
	return false;
}

static bool prompt_pick_binding(int j, PromptKey key, const PromptUsed *used,
                                Joystick_assignment *picked)
{
	if (prompt_fixed_binding(j, key, picked))
		return picked->num >= 0;

	for (size_t r = 0; r < COUNTOF(promptRows[key]) && promptRows[key][r] >= 0; ++r)
	{
		const Joystick_assignment *const pair = joystick[j].assignment[promptRows[key][r]];
		for (int i = 0; i < 2; ++i)
		{
			const Joystick_assignment *const binding = &pair[i];
			if (binding->type == NONE || (binding->type == BUTTON && binding->num < 0) ||
			    joystick_binding_claimed(j, binding))
				continue;
			const int identity = prompt_identity(j, binding);
			if ((key == PROMPT_KEY_CONFIRM && prompt_cancels(j, identity)) ||
			    prompt_conflicts(used, promptRole[key], identity))
				continue;
			*picked = *binding;
			return true;
		}
	}
	return false;
}

static void prompt_note_fixed_key(PromptKey key, int j, PadLabels labels, PromptUsed *used)
{
	Joystick_assignment binding;
	if (j >= 0)
	{
		if (prompt_fixed_binding(j, key, &binding) && binding.num >= 0)
			prompt_note_used(used, promptRole[key], prompt_identity(j, &binding));
	}
	else if (key >= PROMPT_KEY_NORTH && key <= PROMPT_KEY_PAGE_RIGHT &&
	         labels != PAD_LABELS_GENERIC)
	{
		prompt_note_used(used, promptRole[key], padDefaultSpot[labels][key]);
	}
}

static int prompt_token_at(const char *p, const char **close)
{
	*close = (*p == '{') ? strchr(p, '}') : NULL;
	for (int k = 0; *close != NULL && k < PROMPT_KEY_COUNT; ++k)
		if (strlen(promptTokens[k]) == (size_t)(*close - p - 1) &&
		    strncmp(p + 1, promptTokens[k], (size_t)(*close - p - 1)) == 0)
			return k;
	return -1;
}

static void prompt_note_fixed(const char *tmpl, PromptUsed *used)
{
	const int j = prompt_slot();
	const PadLabels labels = prompt_pad_labels();
	for (const char *p = tmpl; *p != '\0'; ++p)
	{
		const char *close;
		const int key = prompt_token_at(p, &close);
		if (key < 0)
			continue;
		if (key == PROMPT_KEY_PAGES)
		{
			prompt_note_fixed_key(PROMPT_KEY_PAGE_LEFT, j, labels, used);
			prompt_note_fixed_key(PROMPT_KEY_PAGE_RIGHT, j, labels, used);
		}
		else
		{
			prompt_note_fixed_key((PromptKey)key, j, labels, used);
		}
		p = close;
	}
}

static const char *prompt_pad_label(char *buf, size_t size, PromptKey key, PromptUsed *used)
{
	if (key == PROMPT_KEY_DPAD)
		return joystick_sideways_joycon(prompt_pad_labels()) ? "stick" : "D-pad";
	if (key == PROMPT_KEY_PAGES)
	{
		char left[24], right[24];
		const char *const l = prompt_pad_label(left, sizeof(left), PROMPT_KEY_PAGE_LEFT, used);
		const char *const r = prompt_pad_label(right, sizeof(right), PROMPT_KEY_PAGE_RIGHT, used);
		if (l == NULL || r == NULL)
			return NULL;
		snprintf(buf, size, "%s/%s", l, r);
		return buf;
	}

	const int role = promptRole[key];
	const int j = prompt_slot();
	if (j >= 0)
	{
		Joystick_assignment binding;
		if (!prompt_pick_binding(j, key, used, &binding))
			return NULL;
		prompt_note_used(used, role, prompt_identity(j, &binding));
		return prompt_binding_label(buf, size, j, &binding);
	}

	const PadLabels labels = prompt_pad_labels();
	if (labels == PAD_LABELS_GENERIC)
	{
		if (key > PROMPT_KEY_PAUSE)
			return NULL;
		snprintf(buf, size, "Button %d", promptRows[key][0] - 3);
		return buf;
	}
	const PadSpot spot = (PadSpot)padDefaultSpot[labels][key];
	if (prompt_conflicts(used, role, spot))
	{
		promptConflict = true;
		return NULL;
	}
	prompt_note_used(used, role, spot);
	return prompt_spot_name(labels, spot);
}

static const char *prompt_key_label(char *buf, size_t size, PromptKey key, PromptDevice device,
                                    PromptUsed *used)
{
	switch (device)
	{
	case PROMPT_KEYBOARD:
		return prompt_keyboard_label(key);
	case PROMPT_TOUCH:
		return prompt_touch_label(key);
	case PROMPT_PAD:
	default:
		return prompt_pad_label(buf, size, key, used);
	}
}

static const char *prompt_expand_for(char *buf, size_t size, const char *tmpl, PromptDevice device)
{
	if (size == 0)
		return buf;

	PromptUsed used = { .count = 0 };
	if (device == PROMPT_PAD)
		prompt_note_fixed(tmpl, &used);

	size_t n = 0;
	const char *p = tmpl;
	while (*p != '\0' && n + 1 < size)
	{
		const char *close;
		const int key = prompt_token_at(p, &close);
		if (key < 0)
		{
			buf[n++] = *p++;
			continue;
		}

		char label[48];
		const char *text = prompt_key_label(label, sizeof(label), (PromptKey)key, device, &used);
		if (text == NULL)
		{
			promptUnresolved = true;
			text = "?";
		}
		for (; *text != '\0' && n + 1 < size; ++text)
			buf[n++] = *text;
		p = close + 1;
	}
	buf[n] = '\0';
	return buf;
}

const char *prompt_expand(char *buf, size_t size, const char *tmpl)
{
	return prompt_expand_for(buf, size, tmpl, prompt_device());
}

const char *prompt_text(char *buf, size_t size, const PromptText *text)
{
	const PromptDevice device = prompt_device();
	const char *tmpl = text->keys;
	if (device == PROMPT_TOUCH && text->touch != NULL)
		tmpl = text->touch;
	else if (device != PROMPT_KEYBOARD && text->pad != NULL)
		tmpl = text->pad;

	promptUnresolved = false;
	promptConflict = false;
	prompt_expand_for(buf, size, tmpl, device);
#if defined(__SWITCH__) || defined(__vita__)
	if (promptUnresolved && !promptForced)
	{
		prompt_force(PROMPT_PAD, prompt_pad_labels());
		promptUnresolved = false;
		prompt_expand_for(buf, size, tmpl, PROMPT_PAD);
		prompt_unforce();
	}
#endif
	if (promptUnresolved && tmpl != text->keys)
	{
		promptUnresolved = false;
		prompt_expand_for(buf, size, text->keys, device);
	}
	if (promptUnresolved && device != PROMPT_KEYBOARD)
	{
		promptUnresolved = false;
		prompt_expand_for(buf, size, text->keys, PROMPT_KEYBOARD);
	}
	return buf;
}

void prompt_force(PromptDevice device, PadLabels labels)
{
	promptForced = true;
	promptForcedDevice = device;
	promptForcedLabels = labels;
}

void prompt_unforce(void)
{
	promptForced = false;
}

const char *prompt_measure_fault(const PromptText *texts, size_t count, PromptMeasure measure,
                                 const void *context, int limit, const char *where)
{
	static char fault[320];

	const bool wasForced = promptForced;
	const PromptDevice wasDevice = promptForcedDevice;
	const PadLabels wasLabels = promptForcedLabels;

	const char *result = NULL;
	for (size_t i = 0; i < count && result == NULL; ++i)
	{
		for (int d = PROMPT_KEYBOARD; d <= PROMPT_PAD && result == NULL; ++d)
		{
			const int styles = (d == PROMPT_PAD) ? PAD_LABELS_COUNT : 1;
			for (int l = 0; l < styles && result == NULL; ++l)
			{
				prompt_force((PromptDevice)d, (PadLabels)l);
				char line[256];
				prompt_text(line, sizeof(line), &texts[i]);
				const int m = measure(line, context);
				if (promptConflict)
				{
					snprintf(fault, sizeof(fault),
					         "%s: \"%s\" names one button for two jobs (family %d)", where,
					         texts[i].pad != NULL ? texts[i].pad : texts[i].keys, l);
					result = fault;
				}
				else if (m > limit || promptUnresolved || strchr(line, '{') != NULL)
				{
					snprintf(fault, sizeof(fault), "%s: \"%s\" measures %d, %d fit", where, line,
					         m, limit);
					result = fault;
				}
			}
		}
	}

	promptForced = wasForced;
	promptForcedDevice = wasDevice;
	promptForcedLabels = wasLabels;
	return result;
}

static int prompt_line_width(const char *line, const void *font)
{
	return JE_textWidth(line, *(const unsigned int *)font);
}

const char *prompt_fit_fault(const PromptText *texts, size_t count, unsigned int font, int width,
                             const char *where)
{
	return prompt_measure_fault(texts, count, prompt_line_width, &font, width, where);
}
