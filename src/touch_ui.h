/* On-screen touch controls for the phone and tablet ports. */
#ifndef TOUCH_UI_H
#define TOUCH_UI_H

#include "SDL.h"

#include <stdbool.h>

/* Android and iOS have no buttons of their own, so anything a finger cannot express gets
 * an on-screen button. Steering and firing in a level stay gestures: a drag anywhere moves
 * the ship and holding a finger down fires. Handhelds with physical controls (Switch,
 * Vita) never draw any of this. */
#if defined(__ANDROID__) || defined(TARGET_IOS)
#define TOUCH_UI_BUTTONS 1
#endif

#ifdef TOUCH_UI_BUTTONS

typedef enum
{
	TOUCH_BTN_PAUSE,    // in a level: open the pause menu
	TOUCH_BTN_WEAPON,   // in a level: cycle the rear weapon mode
	TOUCH_BTN_ESC,      // back out of the current screen
	TOUCH_BTN_SELECT,   // confirm the highlighted row
	TOUCH_BTN_UP,
	TOUCH_BTN_DOWN,
	TOUCH_BTN_LEFT,
	TOUCH_BTN_RIGHT,
	TOUCH_BTN_FIRE,     // Destruct
	TOUCH_BTN_CHANGE,   // Destruct: next unit
	TOUCH_BTN_CYCLE,    // Destruct: next weapon
	TOUCH_BTN_SIDEKICK_L,     // in a level, optional: fire the left sidekick
	TOUCH_BTN_SIDEKICK_R,
	TOUCH_BTN_SIDEKICK_BOTH,
	TOUCH_BTN_COUNT
} TouchButton;

typedef enum
{
	TOUCH_LAYOUT_GAME,      // chosen automatically while a level is being flown
	TOUCH_LAYOUT_MENU,      // the default everywhere else
	TOUCH_LAYOUT_JUKEBOX,
	TOUCH_LAYOUT_DESTRUCT
} TouchLayout;

/* Ask for a layout other than the automatic one. Screens re-assert this every frame or
 * tick; the request goes stale in a fraction of a second, so a screen that exits without
 * clearing it -- including the longjmp out of an online Destruct teardown -- cannot strand
 * the wrong buttons on a later screen. */
void touch_ui_set_layout(TouchLayout layout);

/* Report that the current screen reads the menu keys. push_joysticks_as_keyboard() calls
 * this, so every screen already wired for a controller gets the navigation buttons without
 * knowing they exist, and a screen that reads nothing shows only Esc. */
void touch_ui_menu_navigable(void);

// True while a finger is on the button. Destruct reads its held actions this way.
bool touch_ui_held(TouchButton button);

// Consume one press edge, for actions that must not repeat while held.
bool touch_ui_take_tap(TouchButton button);

// Claim a finger that landed on a button and fire its action. Coordinates are
// window-normalized, as SDL reports them. A true result keeps the touch out of ship
// steering, so the buttons can be used while the other thumb is flying.
bool touch_ui_finger_down(SDL_FingerID finger, float nx, float ny);

// Release a claimed finger. True when this finger was holding a button.
bool touch_ui_finger_up(SDL_FingerID finger);

// Drop every held button. Backgrounding an app can swallow the matching FINGERUP, which
// would otherwise leave a finger id claimed for the rest of the session.
void touch_ui_release_all(void);

// True while this finger is held on a button; its drag must not steer the ship.
bool touch_ui_owns_finger(SDL_FingerID finger);

// Draw the buttons over the presented frame. `frame` is the on-screen rectangle the
// game's output occupies; the buttons sit in the pillarbox beside it wherever the device
// is wider than 16:9. Call after the frame copy and before SDL_RenderPresent.
void touch_ui_render(SDL_Renderer *renderer, const SDL_Rect *frame);

#else

/* Callers are ordinary cross-platform code, so everything it uses collapses to nothing off
 * the touch ports. Each unused macro argument disappears with the expansion, which is why
 * the button and layout names need no definition here. */
#define touch_ui_render(renderer, frame)  ((void)0)
#define touch_ui_menu_navigable()         ((void)0)
#define touch_ui_set_layout(layout)       ((void)0)
#define touch_ui_held(button)             (false)
#define touch_ui_take_tap(button)         (false)

#endif // TOUCH_UI_BUTTONS

#endif // TOUCH_UI_H
