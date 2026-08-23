/* On-screen touch controls for the phone and tablet ports. */
#ifndef TOUCH_UI_H
#define TOUCH_UI_H

#include "SDL.h"

#include <stdbool.h>

/* Android and iOS use gestures for flight and on-screen buttons for other actions. */
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
	TOUCH_BTN_REAR_MODE,      // shop preview: cycle the rear weapon mode, the / key
	TOUCH_BTN_FULLSCREEN,     // jukebox: hide the text overlay, leaving only the starfield
	TOUCH_BTN_COUNT
} TouchButton;

typedef enum
{
	TOUCH_LAYOUT_NONE,      // transition state with no on-screen controls or hit targets
	TOUCH_LAYOUT_GAME,      // chosen automatically while a level is being flown
	TOUCH_LAYOUT_MENU,      // the default everywhere else
	TOUCH_LAYOUT_LIST,      // a scrolling list a tap cannot reach all of: the debug screens
	TOUCH_LAYOUT_PICK,      // a short keyboard-only menu: up, down, confirm
	TOUCH_LAYOUT_CONFIRM,   // a screen waiting for any key: confirm
	TOUCH_LAYOUT_SKIP,      // a skippable logo: back, plus optional navigation
	TOUCH_LAYOUT_JUKEBOX,
	TOUCH_LAYOUT_DESTRUCT
} TouchLayout;

// Request a nonautomatic layout. Reassert while active; requests expire quickly.
void touch_ui_set_layout(TouchLayout layout);

// Add one temporary button to the current layout. Reassert while its key is live.
void touch_ui_set_extra(TouchButton button);

// Clear requests immediately when their screen or condition ends without a fade.
void touch_ui_clear_layout(void);
void touch_ui_clear_extra(void);

// Hide controls immediately and discard their queued input at a screen transition.
void touch_ui_suppress(void);

// Discard the current press without hiding the layout, allowing it to fade with the screen.
void touch_ui_consume_input(void);

// Deliver queued keys beside controller synthesis, just before the screen's event pump.
void touch_ui_flush_keys(void);

// True while a finger is on the button. Destruct reads its held actions this way.
bool touch_ui_held(TouchButton button);

// Consume one press edge, for actions that must not repeat while held.
bool touch_ui_take_tap(TouchButton button);

// Claim a button press using SDL's normalized window coordinates.
// A claimed finger is kept out of ship steering.
bool touch_ui_finger_down(SDL_FingerID finger, float nx, float ny);

// Release a claimed finger. True when this finger was holding a button.
bool touch_ui_finger_up(SDL_FingerID finger);

// Drop every held button. Backgrounding an app can swallow the matching FINGERUP, which
// would otherwise leave a finger id claimed for the rest of the session.
void touch_ui_release_all(void);

// True while this finger is held on a button; its drag must not steer the ship.
bool touch_ui_owns_finger(SDL_FingerID finger);

// Draw over `frame`, using its pillarbox when available. Call before SDL_RenderPresent.
void touch_ui_render(SDL_Renderer *renderer, const SDL_Rect *frame);

// Drop cached texture handles after their renderer is destroyed.
void touch_ui_renderer_lost(void);

// Re-present a finished frame when an idle screen's buttons change.
// Does nothing unless the layout changed.
void touch_ui_idle_repaint(void);

#else

/* Keep touch calls inert on platforms without the touch UI. */
#define touch_ui_render(renderer, frame)  ((void)0)
#define touch_ui_renderer_lost()          ((void)0)
#define touch_ui_flush_keys()             ((void)0)
#define touch_ui_idle_repaint()           ((void)0)
#define touch_ui_set_layout(layout)       ((void)0)
#define touch_ui_set_extra(button)        ((void)0)
#define touch_ui_clear_layout()           ((void)0)
#define touch_ui_clear_extra()            ((void)0)
#define touch_ui_suppress()               ((void)0)
#define touch_ui_consume_input()          ((void)0)
#define touch_ui_held(button)             (false)
#define touch_ui_take_tap(button)         (false)

#endif // TOUCH_UI_BUTTONS

#endif // TOUCH_UI_H
