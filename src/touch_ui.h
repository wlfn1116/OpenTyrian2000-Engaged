/* On-screen touch controls for the phone and tablet ports. */
#ifndef TOUCH_UI_H
#define TOUCH_UI_H

#include "SDL.h"

#include <stdbool.h>

/* Android and iOS have no buttons of their own, so the two actions a finger cannot
 * express -- open the pause menu, cycle the rear weapon mode -- get an on-screen button
 * each. Steering and firing stay gestures: a drag anywhere moves the ship and holding a
 * finger down fires. Handhelds with physical controls (Switch, Vita) never draw these. */
#if defined(__ANDROID__) || defined(TARGET_IOS)
#define TOUCH_UI_BUTTONS 1
#endif

#ifdef TOUCH_UI_BUTTONS

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

// Every present path calls this; a no-op keeps the platform test out of video.c.
#define touch_ui_render(renderer, frame)  ((void)0)

#endif // TOUCH_UI_BUTTONS

#endif // TOUCH_UI_H
