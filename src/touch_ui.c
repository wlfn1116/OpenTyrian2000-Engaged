/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * On-screen touch controls; see touch_ui.h.
 */
#include "touch_ui.h"

#ifdef TOUCH_UI_BUTTONS

#include "config.h"
#include "joystick.h"
#include "keyboard.h"
#include "mainint.h"
#include "palette.h"
#include "video.h"

#include <math.h>

/* Point sizes are scaled to output pixels, then clamped for phones and tablets. */
#define TOUCH_BTN_MIN_PT  44
#define TOUCH_BTN_MAX_PT  140

// Plates are fainter when they overlap the playfield.
#define TOUCH_BTN_ALPHA_CLEAR    195
#define TOUCH_BTN_ALPHA_OVERLAP  105

// Scale the outline with the drawable instead of leaving a high-DPI hairline.
#define TOUCH_BTN_EDGE_DIVISOR   36

// Expire stale screen and layout reports automatically.
#define TOUCH_ASSERT_TTL_MS  250

// Hide buttons and their hit targets near the dark end of a fade.
#define TOUCH_VISIBLE_PEAK_MIN  40

// Menu-arrow repeat timing.
#define TOUCH_REPEAT_DELAY_MS   350
#define TOUCH_REPEAT_PERIOD_MS   90

typedef enum
{
	ICON_PAUSE, ICON_CYCLE, ICON_CLOSE, ICON_SELECT,
	ICON_UP, ICON_DOWN, ICON_LEFT, ICON_RIGHT,
	ICON_FIRE, ICON_CHANGE,
	ICON_POD_L, ICON_POD_R, ICON_POD_BOTH,
	ICON_EXPAND
} TouchIcon;

typedef enum
{
	GATE_ALWAYS,
	GATE_SIDEKICKS   // the player asked for the sidekick buttons (Enhancements > HUD)
} TouchGate;

typedef struct
{
	Uint8 id;
	Uint8 icon;
	Sint8 side;          // -1 left bar, +1 right bar
	Sint8 row;           // 0.. counted down from the top, -1.. up from the bottom
	SDL_Scancode emit;   // pressing it pushes this key; UNKNOWN means the screen queries instead
	bool repeat;         // hold to repeat `emit`
	Uint8 gate;          // TouchGate
} TouchButtonDef;

/* Keep controls in the same rows across layouts. Buttons use the full bar width so their
 * touch targets stay usable on high-DPI phones. */
static const TouchButtonDef LAYOUT_GAME[] =
{
	{ TOUCH_BTN_PAUSE,         ICON_PAUSE,    -1,  0, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_WEAPON,        ICON_CYCLE,     1,  0, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_SIDEKICK_L,    ICON_POD_L,    -1, -1, SDL_SCANCODE_UNKNOWN, false, GATE_SIDEKICKS },
	{ TOUCH_BTN_SIDEKICK_BOTH, ICON_POD_BOTH,  1, -2, SDL_SCANCODE_UNKNOWN, false, GATE_SIDEKICKS },
	{ TOUCH_BTN_SIDEKICK_R,    ICON_POD_R,     1, -1, SDL_SCANCODE_UNKNOWN, false, GATE_SIDEKICKS },
};

/* Ordinary menus accept taps directly; only Back needs a separate button. */
static const TouchButtonDef LAYOUT_MENU[] =
{
	{ TOUCH_BTN_ESC, ICON_CLOSE, -1, 0, SDL_SCANCODE_ESCAPE, false, GATE_ALWAYS },
};

/* Debug lists need arrows to reach hidden rows and reverse values, plus Select to confirm. */
static const TouchButtonDef LAYOUT_LIST[] =
{
	{ TOUCH_BTN_ESC,    ICON_CLOSE,  -1,  0, SDL_SCANCODE_ESCAPE, false, GATE_ALWAYS },
	{ TOUCH_BTN_LEFT,   ICON_LEFT,   -1, -3, SDL_SCANCODE_LEFT,   true,  GATE_ALWAYS },
	{ TOUCH_BTN_RIGHT,  ICON_RIGHT,  -1, -2, SDL_SCANCODE_RIGHT,  true,  GATE_ALWAYS },
	{ TOUCH_BTN_UP,     ICON_UP,      1, -3, SDL_SCANCODE_UP,     true,  GATE_ALWAYS },
	{ TOUCH_BTN_DOWN,   ICON_DOWN,    1, -2, SDL_SCANCODE_DOWN,   true,  GATE_ALWAYS },
	{ TOUCH_BTN_SELECT, ICON_SELECT,  1, -1, SDL_SCANCODE_RETURN, false, GATE_ALWAYS },
};

/* Short keyboard-only menus need vertical movement and confirmation. */
static const TouchButtonDef LAYOUT_PICK[] =
{
	{ TOUCH_BTN_ESC,    ICON_CLOSE,  -1,  0, SDL_SCANCODE_ESCAPE, false, GATE_ALWAYS },
	{ TOUCH_BTN_UP,     ICON_UP,      1, -3, SDL_SCANCODE_UP,     true,  GATE_ALWAYS },
	{ TOUCH_BTN_DOWN,   ICON_DOWN,    1, -2, SDL_SCANCODE_DOWN,   true,  GATE_ALWAYS },
	{ TOUCH_BTN_SELECT, ICON_SELECT,  1, -1, SDL_SCANCODE_RETURN, false, GATE_ALWAYS },
};

/* Any-key screens expose Select while keeping Back in its usual position. */
static const TouchButtonDef LAYOUT_CONFIRM[] =
{
	{ TOUCH_BTN_ESC,    ICON_CLOSE,  -1,  0, SDL_SCANCODE_ESCAPE, false, GATE_ALWAYS },
	{ TOUCH_BTN_SELECT, ICON_SELECT,  1, -1, SDL_SCANCODE_RETURN, false, GATE_ALWAYS },
};

/* Jukebox adds a text-overlay toggle in the top-right slot. */
static const TouchButtonDef LAYOUT_JUKEBOX[] =
{
	{ TOUCH_BTN_ESC,        ICON_CLOSE,  -1,  0, SDL_SCANCODE_ESCAPE, false, GATE_ALWAYS },
	{ TOUCH_BTN_FULLSCREEN, ICON_EXPAND,  1,  0, SDL_SCANCODE_F,      false, GATE_ALWAYS },
	{ TOUCH_BTN_LEFT,       ICON_LEFT,   -1, -1, SDL_SCANCODE_LEFT,   false, GATE_ALWAYS },
	{ TOUCH_BTN_RIGHT,      ICON_RIGHT,   1, -1, SDL_SCANCODE_RIGHT,  false, GATE_ALWAYS },
};

/* Destruct keeps held actions below the thumbs and one-shot actions along the top. */
static const TouchButtonDef LAYOUT_DESTRUCT[] =
{
	// Destruct polls Esc directly because its mid-tick pump can swallow a pushed key.
	{ TOUCH_BTN_ESC,    ICON_CLOSE,  -1,  0, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_LEFT,   ICON_LEFT,   -1, -3, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_RIGHT,  ICON_RIGHT,  -1, -2, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_CHANGE, ICON_CHANGE, -1, -1, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_CYCLE,  ICON_CYCLE,   1,  0, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_UP,     ICON_UP,      1, -3, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_DOWN,   ICON_DOWN,    1, -2, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
	{ TOUCH_BTN_FIRE,   ICON_FIRE,    1, -1, SDL_SCANCODE_UNKNOWN, false, GATE_ALWAYS },
};

/* Screen-specific extras use the top-right slot. */
static const TouchButtonDef LAYOUT_EXTRA[] =
{
	{ TOUCH_BTN_REAR_MODE, ICON_CYCLE, 1, 0, SDL_SCANCODE_SLASH, false, GATE_ALWAYS },
};

#define LAYOUT_MAX_BUTTONS  9

// Live layout, rebuilt every present.
static const TouchButtonDef *shown[LAYOUT_MAX_BUTTONS];
static SDL_Rect shown_rect[LAYOUT_MAX_BUTTONS];
static int shown_count;
static int layout_out_w, layout_out_h;
static bool layout_valid;

static TouchLayout requested_layout;
static Uint32 requested_at_ms;
static Uint8 requested_extra;
static Uint32 extra_at_ms;
static Uint32 presented_signature;
static Uint8 last_peak = 255;

// Cache each composited button until its shape, size, or alpha changes.
typedef struct
{
	SDL_Texture *tex;
	int size;
	Uint8 icon;
	bool held;
	Uint8 plate_base;
	Uint8 opacity;
} ButtonCache;
static ButtonCache btn_cache[LAYOUT_MAX_BUTTONS];

static SDL_FingerID btn_finger[TOUCH_BTN_COUNT];
static bool btn_held[TOUCH_BTN_COUNT];
static bool btn_tapped[TOUCH_BTN_COUNT];
static Uint32 btn_pressed_ms[TOUCH_BTN_COUNT];
static Uint32 btn_repeat_ms[TOUCH_BTN_COUNT];

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// Apply the opacity setting to each button's authored alpha.
static Uint8 scaled_alpha(int base)
{
	return (Uint8)clampi(base * touchButtonOpacity / TOUCH_OPACITY_MAX, 0, 255);
}

static bool fresh(Uint32 stamp_ms, Uint32 now_ms)
{
	return stamp_ms != 0 && now_ms - stamp_ms < TOUCH_ASSERT_TTL_MS;
}

/* Rebuild the sidekick mouse-button state from held touch buttons. This preserves another
 * finger's hold and leaves a physical mouse alone. */
static void refresh_sidekick_fire(void)
{
	const bool both = btn_held[TOUCH_BTN_SIDEKICK_BOTH];
	mouse_pressed[1] = both || btn_held[TOUCH_BTN_SIDEKICK_L];
	mouse_pressed[2] = both || btn_held[TOUCH_BTN_SIDEKICK_R];
}

static bool is_sidekick(int id)
{
	return id == TOUCH_BTN_SIDEKICK_L || id == TOUCH_BTN_SIDEKICK_R ||
	       id == TOUCH_BTN_SIDEKICK_BOTH;
}

/* Signature of the next layout, built from live inputs independently of the last frame. */
static Uint32 desired_signature(Uint32 now_ms)
{
	TouchLayout layout = mouseGetRelative() ? TOUCH_LAYOUT_GAME : TOUCH_LAYOUT_MENU;
	if (fresh(requested_at_ms, now_ms))
		layout = requested_layout;

	Uint32 sig = (Uint32)layout;
	sig = sig * 31u + (touchSidekickButtons ? 1u : 0u);
	sig = sig * 31u + (Uint32)touchButtonOpacity;
	sig = sig * 31u + (fresh(extra_at_ms, now_ms) ? (Uint32)requested_extra + 1u : 0u);

	for (int i = 0; i < TOUCH_BTN_COUNT; ++i)
		sig = sig * 31u + (btn_held[i] ? 1u : 0u);

	return sig;
}

void touch_ui_idle_repaint(void)
{
	// Active levels present on their own; repainting mid-tick could expose a partial frame.
	if (mouseGetRelative())
		return;

	// Fades present every step; replaying their previous frame would flash stale brightness.
	if (palette_fading() || palette_peak() < TOUCH_VISIBLE_PEAK_MIN)
		return;

	if (desired_signature(SDL_GetTicks()) == presented_signature)
		return;

	/* Redraw over the last presented texture. Rebuilding from VGAScreen would lose alternate
	 * presentation paths such as the jukebox's supersampled starfield. */
	video_repeat_last_present();
}

void touch_ui_set_layout(TouchLayout layout)
{
	requested_layout = layout;
	requested_at_ms = SDL_GetTicks();
}

void touch_ui_set_extra(TouchButton button)
{
	requested_extra = (Uint8)button;
	extra_at_ms = SDL_GetTicks();
}

void touch_ui_clear_layout(void)
{
	requested_at_ms = 0;
}

void touch_ui_clear_extra(void)
{
	extra_at_ms = 0;
}

/* Prefer pillarbox space, then letterbox space, then the frame edge. Keep bar layouts close
 * to the frame and use proportional insets because SDL2 exposes no safe area. */
typedef struct
{
	int x_left, x_right;   // left edge of a full-width button in each bar
	int size, gap, pad_y;
} TouchGeometry;

static TouchGeometry measure(const SDL_Rect *frame, int out_w, int out_h)
{
	TouchGeometry g;

	// Convert point limits to drawable pixels on high-DPI screens.
	float px_per_pt = 1.f;
	video_output_pixel_scale(NULL, &px_per_pt);
	const int btn_min_px = (int)(TOUCH_BTN_MIN_PT * px_per_pt);
	const int btn_max_px = (int)(TOUCH_BTN_MAX_PT * px_per_pt);

	const int pad_x = clampi(out_h / 48, (int)(4.f * px_per_pt), (int)(20.f * px_per_pt));
	const int bar_left = frame->x - 2 * pad_x;
	const int bar_right = out_w - (frame->x + frame->w) - 2 * pad_x;
	const int bar = bar_left < bar_right ? bar_left : bar_right;

	g.pad_y = clampi(out_h / 14, (int)(10.f * px_per_pt), (int)(80.f * px_per_pt));
	g.size = clampi(out_h / 7, btn_min_px, btn_max_px);
	if (bar >= btn_min_px && bar < g.size)
		g.size = bar;
	g.gap = g.size / 6;

	if (bar >= btn_min_px)
	{
		g.x_left = frame->x - pad_x - g.size;
		g.x_right = frame->x + frame->w + pad_x;
	}
	else
	{
		// No bar to take: sit on the frame's own edges, drawn faint (see touch_ui_render).
		g.x_left = pad_x;
		g.x_right = out_w - pad_x - g.size;

		const int band = frame->y - 2 * pad_x;
		if (band >= btn_min_px && band < g.size)
			g.size = band;
	}

	return g;
}

static SDL_Rect place(const TouchButtonDef *def, const TouchGeometry *g, int out_h)
{
	const int pitch = g->size + g->gap;

	SDL_Rect r;
	r.x = def->side < 0 ? g->x_left : g->x_right;
	r.y = def->row >= 0 ? g->pad_y + def->row * pitch
	                    : out_h - g->pad_y - g->size + (def->row + 1) * pitch;
	r.w = g->size;
	r.h = g->size;

	return r;
}

static void build_layout(const SDL_Rect *frame, int out_w, int out_h, Uint32 now_ms)
{
	const TouchButtonDef *defs;
	int count;

	TouchLayout layout = mouseGetRelative() ? TOUCH_LAYOUT_GAME : TOUCH_LAYOUT_MENU;
	if (fresh(requested_at_ms, now_ms))
		layout = requested_layout;

	switch (layout)
	{
	case TOUCH_LAYOUT_LIST:     defs = LAYOUT_LIST;     count = (int)COUNTOF(LAYOUT_LIST);     break;
	case TOUCH_LAYOUT_PICK:     defs = LAYOUT_PICK;     count = (int)COUNTOF(LAYOUT_PICK);     break;
	case TOUCH_LAYOUT_CONFIRM:  defs = LAYOUT_CONFIRM;  count = (int)COUNTOF(LAYOUT_CONFIRM);  break;
	case TOUCH_LAYOUT_JUKEBOX:  defs = LAYOUT_JUKEBOX;  count = (int)COUNTOF(LAYOUT_JUKEBOX);  break;
	case TOUCH_LAYOUT_DESTRUCT: defs = LAYOUT_DESTRUCT; count = (int)COUNTOF(LAYOUT_DESTRUCT); break;
	case TOUCH_LAYOUT_GAME:     defs = LAYOUT_GAME;     count = (int)COUNTOF(LAYOUT_GAME);     break;
	case TOUCH_LAYOUT_MENU:
	default:                    defs = LAYOUT_MENU;     count = (int)COUNTOF(LAYOUT_MENU);     break;
	}

	const TouchGeometry g = measure(frame, out_w, out_h);

	shown_count = 0;
	for (int i = 0; i < count && shown_count < LAYOUT_MAX_BUTTONS; ++i)
	{
		if (defs[i].gate == GATE_SIDEKICKS && !touchSidekickButtons)
			continue;

		shown[shown_count] = &defs[i];
		shown_rect[shown_count] = place(&defs[i], &g, out_h);
		++shown_count;
	}

	if (fresh(extra_at_ms, now_ms))
	{
		for (size_t i = 0; i < COUNTOF(LAYOUT_EXTRA) && shown_count < LAYOUT_MAX_BUTTONS; ++i)
		{
			if (LAYOUT_EXTRA[i].id != requested_extra)
				continue;

			shown[shown_count] = &LAYOUT_EXTRA[i];
			shown_rect[shown_count] = place(&LAYOUT_EXTRA[i], &g, out_h);
			++shown_count;
		}
	}

	// Drop taps from buttons that have left the current layout.
	bool live[TOUCH_BTN_COUNT] = { false };
	for (int i = 0; i < shown_count; ++i)
		live[shown[i]->id] = true;
	for (int i = 0; i < TOUCH_BTN_COUNT; ++i)
	{
		if (!live[i])
			btn_tapped[i] = false;
	}
}

static bool overlaps_frame(const SDL_Rect *button, const SDL_Rect *frame)
{
	return button->x < frame->x + frame->w && button->x + button->w > frame->x &&
	       button->y < frame->y + frame->h && button->y + button->h > frame->y;
}

// Scale a colour by the live palette's brightness; see the note in touch_ui_render.
static Uint8 dim(int component, Uint8 peak)
{
	return (Uint8)(component * (int)peak / 255);
}

static void draw_plate(SDL_Renderer *renderer, const SDL_Rect *r, Uint8 alpha, bool held, Uint8 peak)
{
	SDL_SetRenderDrawColor(renderer, dim(held ? 60 : 14, peak), dim(held ? 66 : 16, peak),
	                       dim(held ? 88 : 26, peak), alpha);
	SDL_RenderFillRect(renderer, r);

	// Scale nested-ring outlines with the button so they remain visible on high-DPI screens.
	SDL_SetRenderDrawColor(renderer, dim(165, peak), dim(176, peak), dim(205, peak),
	                       scaled_alpha(235));

	const int edge = clampi((r->w + TOUCH_BTN_EDGE_DIVISOR / 2) / TOUCH_BTN_EDGE_DIVISOR, 2, 6);
	for (int i = 0; i < edge; ++i)
	{
		const SDL_Rect ring = { r->x + i, r->y + i, r->w - 2 * i, r->h - 2 * i };
		SDL_RenderDrawRect(renderer, &ring);
	}
}

// Scanline fill for SDL versions without a triangle primitive.
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

// A thick line segment, as the two triangles of its quad.
static void fill_bar(SDL_Renderer *renderer, float x1, float y1, float x2, float y2, float thick)
{
	const float dx = x2 - x1, dy = y2 - y1;
	const float len = sqrtf(dx * dx + dy * dy);
	if (len < 0.001f)
		return;

	const float nx = -dy / len * thick * 0.5f, ny = dx / len * thick * 0.5f;
	const float ax[3] = { x1 + nx, x2 + nx, x2 - nx };
	const float ay[3] = { y1 + ny, y2 + ny, y2 - ny };
	const float bx[3] = { x1 + nx, x2 - nx, x1 - nx };
	const float by[3] = { y1 + ny, y2 - ny, y1 - ny };
	fill_triangle(renderer, ax, ay);
	fill_triangle(renderer, bx, by);
}

static void fill_disc(SDL_Renderer *renderer, float cx, float cy, float radius)
{
	for (int y = (int)(cy - radius); y <= (int)(cy + radius); ++y)
	{
		const float dy = (float)y + 0.5f - cy;
		const float span = radius * radius - dy * dy;
		if (span <= 0.f)
			continue;

		const float half = sqrtf(span);
		SDL_RenderDrawLine(renderer, (int)(cx - half), y, (int)(cx + half), y);
	}
}

// One sidekick pod: full size when firing, a dot when idle.
static void draw_pod(SDL_Renderer *renderer, float cx, float cy, float rad, float thick, bool firing)
{
	fill_disc(renderer, cx, cy + rad * 0.4f, rad * (firing ? 0.5f : 0.3f));
	if (firing)
		fill_bar(renderer, cx, cy - rad * 0.15f, cx, cy - rad, thick);
}

// A solid triangle pointing along (dx, dy), which covers all four arrow icons.
static void draw_arrow(SDL_Renderer *renderer, float cx, float cy, float r, float dx, float dy)
{
	const float px[3] = { cx + dx * r,          cx - dx * r - dy * r * 0.85f, cx - dx * r + dy * r * 0.85f };
	const float py[3] = { cy + dy * r,          cy - dy * r + dx * r * 0.85f, cy - dy * r - dx * r * 0.85f };
	fill_triangle(renderer, px, py);
}

static void draw_icon(SDL_Renderer *renderer, TouchIcon icon, const SDL_Rect *r)
{
	const float cx = (float)r->x + (float)r->w / 2.f;
	const float cy = (float)r->y + (float)r->h / 2.f;
	const float unit = (float)(r->w < r->h ? r->w : r->h);
	const float rad = unit * 0.26f;
	const float thick = unit / 9.f > 2.f ? unit / 9.f : 2.f;

	switch (icon)
	{
	case ICON_PAUSE:
	{
		const int bar_w = r->w / 8 > 2 ? r->w / 8 : 2;
		const int bar_h = r->h / 2;
		const int left = r->x + (r->w - (3 * bar_w)) / 2;
		const SDL_Rect bars[2] = {
			{ left, r->y + (r->h - bar_h) / 2, bar_w, bar_h },
			{ left + 2 * bar_w, r->y + (r->h - bar_h) / 2, bar_w, bar_h },
		};
		SDL_RenderFillRects(renderer, bars, 2);
		break;
	}

	case ICON_CYCLE:
	{
		// Circular arrow for cycling modes.
		const float start = 0.55f, sweep = 4.9f;  // radians; the gap holds the arrowhead
		const int steps = clampi((int)(sweep * rad / thick) * 2, 24, 512);
		for (int i = 0; i <= steps; ++i)
		{
			const float a = start + sweep * (float)i / (float)steps;
			const SDL_Rect dot = {
				(int)(cx + rad * cosf(a)) - (int)thick / 2,
				(int)(cy + rad * sinf(a)) - (int)thick / 2,
				(int)thick, (int)thick
			};
			SDL_RenderFillRect(renderer, &dot);
		}

		const float a_end = start + sweep;
		const float head = thick * 1.9f;
		const float tx = cx + rad * cosf(a_end), ty = cy + rad * sinf(a_end);
		draw_arrow(renderer, tx, ty, head, -sinf(a_end), cosf(a_end));
		break;
	}

	case ICON_CLOSE:
		fill_bar(renderer, cx - rad, cy - rad, cx + rad, cy + rad, thick);
		fill_bar(renderer, cx + rad, cy - rad, cx - rad, cy + rad, thick);
		break;

	case ICON_SELECT:
	{
		// Cover the square-ended stroke join with a disc.
		const float jx = cx - rad * 0.25f, jy = cy + rad * 0.7f;
		fill_bar(renderer, cx - rad, cy, jx, jy, thick);
		fill_bar(renderer, jx, jy, cx + rad, cy - rad * 0.7f, thick);
		fill_disc(renderer, jx, jy, thick * 0.5f);
		break;
	}

	case ICON_UP:     draw_arrow(renderer, cx, cy, rad,  0.f, -1.f); break;
	case ICON_DOWN:   draw_arrow(renderer, cx, cy, rad,  0.f,  1.f); break;
	case ICON_LEFT:   draw_arrow(renderer, cx, cy, rad, -1.f,  0.f); break;
	case ICON_RIGHT:  draw_arrow(renderer, cx, cy, rad,  1.f,  0.f); break;

	case ICON_FIRE:
		fill_disc(renderer, cx, cy, rad);
		break;

	// Show both pod slots so left, right, and both remain distinct at thumbnail size.
	case ICON_POD_L:
	case ICON_POD_R:
	case ICON_POD_BOTH:
	{
		const float off = rad * 0.55f;
		draw_pod(renderer, cx - off, cy, rad, thick, icon != ICON_POD_R);
		draw_pod(renderer, cx + off, cy, rad, thick, icon != ICON_POD_L);
		break;
	}

	case ICON_EXPAND:
	{
		// Four corner brackets, the usual "fill the screen" glyph.
		const float arm = rad * 0.6f;
		for (int i = 0; i < 4; ++i)
		{
			const float sx = (i & 1) ? 1.f : -1.f;
			const float sy = (i & 2) ? 1.f : -1.f;
			const float x = cx + sx * rad, y = cy + sy * rad;
			fill_bar(renderer, x, y, x - sx * arm, y, thick);
			fill_bar(renderer, x, y, x, y - sy * arm, thick);
		}
		break;
	}

	case ICON_CHANGE:
	{
		// Two opposed arrows: this swaps which unit you are commanding.
		const float off = rad * 0.5f;
		fill_bar(renderer, cx - rad, cy - off, cx + rad * 0.4f, cy - off, thick * 0.8f);
		draw_arrow(renderer, cx + rad * 0.7f, cy - off, thick * 1.5f, 1.f, 0.f);
		fill_bar(renderer, cx + rad, cy + off, cx - rad * 0.4f, cy + off, thick * 0.8f);
		draw_arrow(renderer, cx - rad * 0.7f, cy + off, thick * 1.5f, -1.f, 0.f);
		break;
	}
	}
}

/* Queue touch keys until controller synthesis, immediately before the event pump whose
 * result the screen reads. Earlier pumps may clear keys delivered directly from SDL. */
#define PENDING_KEY_MAX  4
static SDL_Scancode pending_key[PENDING_KEY_MAX];
static int pending_key_count;

static void queue_key(SDL_Scancode scan)
{
	if (pending_key_count < PENDING_KEY_MAX)
		pending_key[pending_key_count++] = scan;
}

// Repeat held menu arrows; Destruct reads held state directly.
static void service_repeat(Uint32 now_ms)
{
	for (int i = 0; i < shown_count; ++i)
	{
		const TouchButtonDef *def = shown[i];
		if (!def->repeat || def->emit == SDL_SCANCODE_UNKNOWN || !btn_held[def->id])
			continue;

		if (now_ms - btn_pressed_ms[def->id] < TOUCH_REPEAT_DELAY_MS)
			continue;
		if (now_ms - btn_repeat_ms[def->id] < TOUCH_REPEAT_PERIOD_MS)
			continue;

		btn_repeat_ms[def->id] = now_ms;
		queue_key(def->emit);
	}
}

void touch_ui_flush_keys(void)
{
	service_repeat(SDL_GetTicks());

	for (int i = 0; i < pending_key_count; ++i)
		push_key(pending_key[i]);

	pending_key_count = 0;
}

/* Composite each button before applying opacity so overlapping glyph strokes blend once. */
static SDL_Texture *button_texture(SDL_Renderer *renderer, int slot, const TouchButtonDef *def,
                                   int size, bool held, Uint8 plate_base)
{
	ButtonCache *const c = &btn_cache[slot];
	const Uint8 opacity = (Uint8)clampi(touchButtonOpacity, 0, TOUCH_OPACITY_MAX);

	if (c->tex != NULL && c->size == size && c->icon == def->icon && c->held == held &&
	    c->plate_base == plate_base && c->opacity == opacity)
		return c->tex;

	if (c->tex == NULL || c->size != size)
	{
		if (c->tex != NULL)
			SDL_DestroyTexture(c->tex);

		c->tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		                           SDL_TEXTUREACCESS_TARGET, size, size);
		c->size = size;
		if (c->tex == NULL)
			return NULL;

		SDL_SetTextureBlendMode(c->tex, SDL_BLENDMODE_BLEND);
	}

	SDL_Texture *const prev_target = SDL_GetRenderTarget(renderer);
	if (SDL_SetRenderTarget(renderer, c->tex) != 0)
	{
		SDL_DestroyTexture(c->tex);
		c->tex = NULL;
		return NULL;
	}

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
	SDL_RenderClear(renderer);

	// Full brightness: the palette fade is applied as a colour mod when this is copied out.
	const SDL_Rect local = { 0, 0, size, size };
	draw_plate(renderer, &local, scaled_alpha(plate_base), held, 255);
	SDL_SetRenderDrawColor(renderer, 226, 232, 248, scaled_alpha(255));
	draw_icon(renderer, (TouchIcon)def->icon, &local);

	SDL_SetRenderTarget(renderer, prev_target);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	c->icon = def->icon;
	c->held = held;
	c->plate_base = plate_base;
	c->opacity = opacity;
	return c->tex;
}

void touch_ui_renderer_lost(void)
{
	// The renderer owned these, so they are already gone; only the handles are left.
	for (int i = 0; i < LAYOUT_MAX_BUTTONS; ++i)
		btn_cache[i] = (ButtonCache){ 0 };
}

void touch_ui_render(SDL_Renderer *renderer, const SDL_Rect *frame)
{
	if (renderer == NULL)
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

	const Uint32 now_ms = SDL_GetTicks();

	/* Follow palette brightness because touch controls are rendered after palette conversion.
	 * Near black, hide both the controls and their hit targets. */
	const Uint8 peak = palette_peak();

	// Zero opacity also removes hit targets.
	const bool visible = peak >= TOUCH_VISIBLE_PEAK_MIN && touchButtonOpacity > 0;

	/* Clear requests only while brightness is strictly falling near black. This removes the
	 * departing layout without erasing a layout asserted at the start of a fade-in. */
	if (!visible && peak < last_peak)
	{
		requested_at_ms = 0;
		extra_at_ms = 0;
	}
	last_peak = peak;

	// Keep live requests fresh through blocking fades. See "Touch and mobile UI" in doc/notes.md.
	if (palette_fading())
	{
		if (fresh(requested_at_ms, now_ms))
			requested_at_ms = now_ms;
		if (fresh(extra_at_ms, now_ms))
			extra_at_ms = now_ms;
	}

	build_layout(frame, out_w, out_h, now_ms);
	layout_out_w = out_w;
	layout_out_h = out_h;
	layout_valid = visible;

	// The idle repaint signature now matches this frame.
	presented_signature = desired_signature(now_ms);

	if (!layout_valid)
		return;

	SDL_BlendMode prev_blend;
	SDL_GetRenderDrawBlendMode(renderer, &prev_blend);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	for (int i = 0; i < shown_count; ++i)
	{
		const SDL_Rect *r = &shown_rect[i];
		const bool held = btn_held[shown[i]->id];
		const Uint8 base = overlaps_frame(r, frame) ? TOUCH_BTN_ALPHA_OVERLAP : TOUCH_BTN_ALPHA_CLEAR;

		SDL_Texture *tex = button_texture(renderer, i, shown[i], r->w, held, base);
		if (tex != NULL)
		{
			// Apply fade brightness as a colour mod without rebuilding the texture.
			SDL_SetTextureColorMod(tex, peak, peak, peak);
			SDL_RenderCopy(renderer, tex, NULL, r);
			continue;
		}

		// Without render targets, keep the glyph opaque so overlapping strokes remain legible.
		draw_plate(renderer, r, scaled_alpha(base), held, peak);
		SDL_SetRenderDrawColor(renderer, dim(226, peak), dim(232, peak), dim(248, peak), 255);
		draw_icon(renderer, (TouchIcon)shown[i]->icon, r);
	}

	SDL_SetRenderDrawBlendMode(renderer, prev_blend);
}

bool touch_ui_finger_down(SDL_FingerID finger, float nx, float ny)
{
	if (!layout_valid)
		return false;

	const int x = (int)(nx * (float)layout_out_w);
	const int y = (int)(ny * (float)layout_out_h);

	for (int i = 0; i < shown_count; ++i)
	{
		const SDL_Rect *r = &shown_rect[i];
		if (x < r->x || x >= r->x + r->w || y < r->y || y >= r->y + r->h)
			continue;

		const TouchButtonDef *def = shown[i];
		const Uint32 now_ms = SDL_GetTicks();

		btn_finger[def->id] = finger;
		btn_held[def->id] = true;
		btn_pressed_ms[def->id] = now_ms;
		btn_repeat_ms[def->id] = now_ms;

		if (def->emit != SDL_SCANCODE_UNKNOWN)
		{
			queue_key(def->emit);
		}
		else
		{
			// Record tap flags only for buttons polled directly; pushed keys are already delivered.
			btn_tapped[def->id] = true;

			if (def->id == TOUCH_BTN_PAUSE)
				ingamemenu_pressed = true;   // the same latch a pad's pause button sets
			else if (def->id == TOUCH_BTN_WEAPON)
				changefire_pressed = true;
			else if (is_sidekick(def->id))
				refresh_sidekick_fire();
		}

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
		if (is_sidekick(i))
			refresh_sidekick_fire();
		return true;
	}

	return false;
}

void touch_ui_release_all(void)
{
	bool had_sidekick = false;
	for (int i = 0; i < TOUCH_BTN_COUNT; ++i)
	{
		had_sidekick = had_sidekick || (btn_held[i] && is_sidekick(i));
		btn_held[i] = false;
	}

	if (had_sidekick)
		refresh_sidekick_fire();
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

bool touch_ui_held(TouchButton button)
{
	return btn_held[button];
}

bool touch_ui_take_tap(TouchButton button)
{
	const bool tapped = btn_tapped[button];
	btn_tapped[button] = false;
	return tapped;
}

#endif // TOUCH_UI_BUTTONS
