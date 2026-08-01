/* 
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "opentyr.h"

#include "SDL.h"

#include <stdbool.h>

#define SDL_POLL_INTERVAL 10

extern JE_boolean ESCPressed;
extern JE_boolean newkey, newmouse, keydown, mousedown;
extern SDL_Scancode lastkey_scan;
extern SDL_Keymod lastkey_mod;
extern Uint8 lastmouse_but;
extern Sint32 lastmouse_x, lastmouse_y;
extern JE_boolean mouse_pressed[4];
extern Sint32 mouse_x, mouse_y;
extern Sint32 mouse_scroll;
extern Uint8 keysactive[SDL_NUM_SCANCODES];

extern bool windowHasFocus;

extern bool new_text;
extern char last_text[SDL_TEXTINPUTEVENT_TEXT_SIZE];

// Ship-control sensitivity: scales the consoles' touch-drag or the desktop relative mouse
// motion. Slider range [0, SHIP_SENS_MAX]; the bar's middle (SHIP_SENS_DEFAULT) reproduces
// the classic 1:1 feel, more bars = higher sensitivity, fewer = lower. One "Sensitivity"
// row per build; only the help text and config key are per-platform. Persisted in config.
#define SHIP_SENS_MAX 255
#define SHIP_SENS_DEFAULT 128
// Colour banks for the slider's middle-value marker bar: bank 9 is a dark->light blue ramp
// identical across the setup/shop palettes. The marker draws dark blue while the fill is
// below the neutral middle slot and bright blue at/above it, keyed to the drawn bar counts
// so it flips exactly on the middle bar. Tunable.
#define SHIP_SENS_MARK_COL     144  // bright blue: the fill has reached the middle slot
#define SHIP_SENS_MARK_COL_DIM 136  // dark blue:   the fill is still below the middle slot
#define SHIP_SENS_NAME "Sensitivity"
#if defined(__SWITCH__) || defined(__vita__)
#define SHIP_SENS_HELP "Touchscreen ship control sensitivity."
#define SHIP_SENS_CFG  "touch_sensitivity"
#else
#define SHIP_SENS_HELP "Mouse ship control sensitivity."
#define SHIP_SENS_CFG  "mouse_sensitivity"
#endif
extern int ship_sensitivity;

void wait_input(JE_boolean keyboard, JE_boolean mouse, JE_boolean joystick);
void wait_noinput(JE_boolean keyboard, JE_boolean mouse, JE_boolean joystick);
void init_keyboard(void);
void mouseSetRelative(bool enable);
bool mouseGetRelative(void);
JE_word JE_mousePosition(JE_word *mouseX, JE_word *mouseY);
void mouseGetRelativePosition(Sint32 *out_x, Sint32 *out_y);
void mouseGetRelativeMotionF(float *out_x, float *out_y);  // float-scaled, no per-call rounding

void service_SDL_events(JE_boolean clear_new);

void JE_clearKeyboard(void);

int scancode_digit(int sc);  // typed digit (0-9) from a scancode, or -1

#endif /* KEYBOARD_H */
