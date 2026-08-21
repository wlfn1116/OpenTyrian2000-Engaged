/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * On-screen touch controls; see touch_ui.h.
 */
#include "touch_ui.h"

#ifdef TOUCH_UI_BUTTONS

#include "keyboard.h"
#include "mainint.h"

#include <math.h>

enum
{
	TOUCH_BTN_PAUSE,   // top left: opens the in-game pause menu
	TOUCH_BTN_WEAPON,  // top right: cycles the rear weapon mode
	TOUCH_BTN_COUNT
};

// Smallest comfortable finger target, and a ceiling so a tablet does not get a button
// the size of a fist. Both in output pixels.
#define TOUCH_BTN_MIN_PX  44
#define TOUCH_BTN_MAX_PX  140

// Opacity of the button plate: solid where it sits in the pillarbox, faint where the
// device is 16:9 or narrower and it has to float over the playfield instead.
#define TOUCH_BTN_ALPHA_CLEAR    195
#define TOUCH_BTN_ALPHA_OVERLAP  105

// Layout from the most recent touch_ui_render, which is also what makes the buttons live:
// every screen that is not a level in progress leaves it invalid, so taps fall through.
static SDL_Rect btn_rect[TOUCH_BTN_COUNT];
static int layout_out_w, layout_out_h;
static bool layout_valid;

static SDL_FingerID btn_finger[TOUCH_BTN_COUNT];
static bool btn_held[TOUCH_BTN_COUNT];

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* Where one button goes, in preference order: the pillarbox bar beside the frame, which is
 * what a phone gives us; the letterbox band above it, which is what a 4:3 tablet gives us;
 * and the frame's own corner when the display is 16:9 and there is no margin to take.
 *
 * In the bar the button hugs the frame rather than the screen edge, keeping it away from
 * the notch a phone puts on a short edge in landscape. The vertical inset is the larger of
 * the two because it also has to clear the display's rounded corner. SDL2 reports no safe
 * area, so both insets are proportions of the screen. */
static SDL_Rect place_button(const SDL_Rect *frame, int out_w, int out_h, bool right_side)
{
	const int pad_x = clampi(out_h / 48, 4, 20);
	const int pad_y = clampi(out_h / 14, 10, 80);
	const int base = clampi(out_h / 7, TOUCH_BTN_MIN_PX, TOUCH_BTN_MAX_PX);

	const int bar_room = (right_side ? out_w - (frame->x + frame->w) : frame->x) - 2 * pad_x;
	if (bar_room >= TOUCH_BTN_MIN_PX)
	{
		const int size = bar_room < base ? bar_room : base;
		const int x = right_side ? frame->x + frame->w + pad_x : frame->x - pad_x - size;
		return (SDL_Rect){ x, pad_y, size, size };
	}

	const int band_room = frame->y - 2 * pad_x;
	if (band_room >= TOUCH_BTN_MIN_PX)
	{
		const int size = band_room < base ? band_room : base;
		const int x = right_side ? out_w - pad_x - size : pad_x;
		return (SDL_Rect){ x, (frame->y - size) / 2, size, size };
	}

	return (SDL_Rect){ right_side ? out_w - pad_x - base : pad_x, pad_y, base, base };
}

static void layout_buttons(const SDL_Rect *frame, int out_w, int out_h)
{
	btn_rect[TOUCH_BTN_PAUSE] = place_button(frame, out_w, out_h, false);
	btn_rect[TOUCH_BTN_WEAPON] = place_button(frame, out_w, out_h, true);
}

static bool overlaps_frame(const SDL_Rect *button, const SDL_Rect *frame)
{
	return button->x < frame->x + frame->w && button->x + button->w > frame->x &&
	       button->y < frame->y + frame->h && button->y + button->h > frame->y;
}

static void draw_plate(SDL_Renderer *renderer, const SDL_Rect *r, Uint8 alpha, bool held)
{
	SDL_SetRenderDrawColor(renderer, held ? 60 : 14, held ? 66 : 16, held ? 88 : 26, alpha);
	SDL_RenderFillRect(renderer, r);

	SDL_SetRenderDrawColor(renderer, 165, 176, 205, 235);
	SDL_RenderDrawRect(renderer, r);

	const SDL_Rect inner = { r->x + 1, r->y + 1, r->w - 2, r->h - 2 };
	SDL_RenderDrawRect(renderer, &inner);
}

static void draw_pause_icon(SDL_Renderer *renderer, const SDL_Rect *r)
{
	const int bar_w = r->w / 8 > 2 ? r->w / 8 : 2;
	const int bar_h = r->h / 2;
	const int gap = bar_w;
	const int top = r->y + (r->h - bar_h) / 2;
	const int left = r->x + (r->w - (2 * bar_w + gap)) / 2;

	const SDL_Rect bars[2] = {
		{ left, top, bar_w, bar_h },
		{ left + bar_w + gap, top, bar_w, bar_h },
	};
	SDL_RenderFillRects(renderer, bars, 2);
}

// Scanline fill; SDL's renderer has no triangle primitive before 2.0.18 and the arrowhead
// is the only place that needs one.
static void fill_triangle(SDL_Renderer *renderer, const float px[3], const float py[3])
{
	float y_min = py[0], y_max = py[0];
	for (int i = 1; i < 3; ++i)
	{
		if (py[i] < y_min)
			y_min = py[i];
		if (py[i] > y_max)
			y_max = py[i];
	}

	for (int y = (int)floorf(y_min); y <= (int)ceilf(y_max); ++y)
	{
		const float scan = (float)y + 0.5f;
		float x_min = 0.f, x_max = 0.f;
		int crossings = 0;

		for (int i = 0, j = 2; i < 3; j = i++)
		{
			if ((py[i] > scan) == (py[j] > scan))
				continue;

			const float x = px[i] + (scan - py[i]) * (px[j] - px[i]) / (py[j] - py[i]);
			if (crossings++ == 0)
				x_min = x_max = x;
			else if (x < x_min)
				x_min = x;
			else if (x > x_max)
				x_max = x;
		}

		if (crossings >= 2)
			SDL_RenderDrawLine(renderer, (int)x_min, y, (int)x_max, y);
	}
}

// A circular arrow: the mode cycles, and the glyph has to read at thumbnail size without
// any text, since the button lives outside the palettized frame the game's fonts draw to.
static void draw_weapon_icon(SDL_Renderer *renderer, const SDL_Rect *r)
{
	const float cx = (float)r->x + (float)r->w / 2.f;
	const float cy = (float)r->y + (float)r->h / 2.f;
	const float radius = (float)r->w * 0.26f;
	const int thickness = r->w / 12 > 2 ? r->w / 12 : 2;

	const float start = 0.55f, sweep = 4.9f;  // radians; the gap holds the arrowhead
	const int steps = clampi((int)(sweep * radius / (float)thickness) * 2, 24, 512);

	for (int i = 0; i <= steps; ++i)
	{
		const float a = start + sweep * (float)i / (float)steps;
		const SDL_Rect dot = {
			(int)(cx + radius * cosf(a)) - thickness / 2,
			(int)(cy + radius * sinf(a)) - thickness / 2,
			thickness, thickness
		};
		SDL_RenderFillRect(renderer, &dot);
	}

	// Arrowhead on the tangent at the swept end.
	const float a_end = start + sweep;
	const float head = (float)thickness * 2.2f;
	const float tx = cx + radius * cosf(a_end), ty = cy + radius * sinf(a_end);
	const float dx = -sinf(a_end), dy = cosf(a_end);

	const float px[3] = { tx + dx * head, tx - dy * head * 0.8f, tx + dy * head * 0.8f };
	const float py[3] = { ty + dy * head, ty + dx * head * 0.8f, ty - dx * head * 0.8f };
	fill_triangle(renderer, px, py);
}

void touch_ui_render(SDL_Renderer *renderer, const SDL_Rect *frame)
{
	// The buttons only mean anything while a ship is flying, which is exactly when the
	// pointer is in relative (steering) mode.
	if (renderer == NULL || !mouseGetRelative())
	{
		layout_valid = false;
		return;
	}

	int out_w = 0, out_h = 0;
	if (SDL_GetRendererOutputSize(renderer, &out_w, &out_h) != 0 || out_w <= 0 || out_h <= 0)
	{
		layout_valid = false;
		return;
	}

	layout_buttons(frame, out_w, out_h);
	layout_out_w = out_w;
	layout_out_h = out_h;
	layout_valid = true;

	SDL_BlendMode prev_blend;
	SDL_GetRenderDrawBlendMode(renderer, &prev_blend);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	for (int i = 0; i < TOUCH_BTN_COUNT; ++i)
	{
		const SDL_Rect *r = &btn_rect[i];
		const Uint8 alpha = overlaps_frame(r, frame) ? TOUCH_BTN_ALPHA_OVERLAP : TOUCH_BTN_ALPHA_CLEAR;

		draw_plate(renderer, r, alpha, btn_held[i]);

		SDL_SetRenderDrawColor(renderer, 226, 232, 248, btn_held[i] ? 255 : 225);
		if (i == TOUCH_BTN_PAUSE)
			draw_pause_icon(renderer, r);
		else
			draw_weapon_icon(renderer, r);
	}

	SDL_SetRenderDrawBlendMode(renderer, prev_blend);
}

bool touch_ui_finger_down(SDL_FingerID finger, float nx, float ny)
{
	if (!layout_valid)
		return false;

	const int x = (int)(nx * (float)layout_out_w);
	const int y = (int)(ny * (float)layout_out_h);

	for (int i = 0; i < TOUCH_BTN_COUNT; ++i)
	{
		const SDL_Rect *r = &btn_rect[i];
		if (x < r->x || x >= r->x + r->w || y < r->y || y >= r->y + r->h)
			continue;

		btn_finger[i] = finger;
		btn_held[i] = true;

		// Both actions are latches the tick loop drains, the same ones a gamepad sets.
		if (i == TOUCH_BTN_PAUSE)
			ingamemenu_pressed = true;
		else
			changefire_pressed = true;

		return true;
	}

	return false;
}

bool touch_ui_finger_up(SDL_FingerID finger)
{
	for (int i = 0; i < TOUCH_BTN_COUNT; ++i)
	{
		if (!btn_held[i] || btn_finger[i] != finger)
			continue;

		btn_held[i] = false;
		return true;
	}

	return false;
}

void touch_ui_release_all(void)
{
	for (int i = 0; i < TOUCH_BTN_COUNT; ++i)
		btn_held[i] = false;
}

bool touch_ui_owns_finger(SDL_FingerID finger)
{
	for (int i = 0; i < TOUCH_BTN_COUNT; ++i)
	{
		if (btn_held[i] && btn_finger[i] == finger)
			return true;
	}

	return false;
}

#endif // TOUCH_UI_BUTTONS
