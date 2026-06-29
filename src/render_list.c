/*
 * OpenTyrian2000: render-list capture & replay (see render_list.h).
 */
#include "render_list.h"

#include "backgrnd.h"
#include "sprite.h"
#include "video.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool render_list_recording = false;
int rl_current_id = 0;
int rl_shot_attach = 0;

// Double-buffered command lists: one for the current tick, one for the previous
// (used to derive per-command motion for interpolation).
static RenderCmd *bufs[2] = { NULL, NULL };
static size_t counts[2] = { 0, 0 };
static size_t caps[2] = { 0, 0 };
static int cur_buf = 0;

static RenderCmd *rl_push(void)
{
	size_t *cap = &caps[cur_buf];
	if (counts[cur_buf] == *cap)
	{
		size_t ncap = *cap ? *cap * 2 : 4096;
		RenderCmd *n = realloc(bufs[cur_buf], ncap * sizeof(*n));
		if (n == NULL)
			return NULL;  // out of memory: drop this command
		bufs[cur_buf] = n;
		*cap = ncap;
	}
	RenderCmd *c = &bufs[cur_buf][counts[cur_buf]++];
	c->id = rl_current_id;
	c->ship_attach = (Uint8)rl_shot_attach;
	c->dx = 0;
	c->dy = 0;
	// On smoothie levels the playfield draw ping-pongs between game_screen and
	// VGAScreen2; capture which buffer this draw targeted so replay can route it.
	c->surface = (VGAScreen == VGAScreen2) ? 1 : 0;
	return c;
}

void rl_begin_record(void)
{
	cur_buf ^= 1;             // previous current becomes prev; record into the other
	counts[cur_buf] = 0;
	rl_current_id = 0;
	render_list_recording = true;
}

void rl_end_record(void)
{
	render_list_recording = false;
}

size_t rl_count(void)
{
	return counts[cur_buf];
}

void rl_rec_sprite2(int x, int y, Sprite2_array sheet, unsigned int index, RenderCmdKind kind)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = kind;
	c->x = x;
	c->y = y;
	c->sheet = sheet;
	c->index = index;
}

void rl_rec_sprite2_filter(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter, bool clip)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = clip ? RC_SPRITE2_FILTER_CLIP : RC_SPRITE2_FILTER;
	c->x = x;
	c->y = y;
	c->sheet = sheet;
	c->index = index;
	c->filter = filter;
}

void rl_rec_sprite(int x, int y, unsigned int table, unsigned int index, RenderCmdKind kind, Uint8 hue, Sint8 value, bool black)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = kind;
	c->x = x;
	c->y = y;
	c->table = table;
	c->index = index;
	c->hue = hue;
	c->value = value;
	c->black = black;
}

void rl_rec_bg_row(int x, int y, Uint8 **map, bool blend)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = blend ? RC_BG_ROW_BLEND : RC_BG_ROW;
	c->x = x;
	c->y = y;
	c->map = map;
}

void rl_rec_star(int x, float y, float dy, Uint8 color)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = RC_STAR;
	c->star_x = x;
	c->star_y = y;
	c->star_dy = dy;
	c->star_color = color;
}

void rl_rec_filter_screen(int col, int brightness)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = RC_FILTER_SCREEN;
	c->filt_col = col;
	c->filt_bright = brightness;
}

void rl_rec_smoothie_filter(RenderCmdKind kind)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = kind;
	// rl_push already stamped c->surface = (VGAScreen == VGAScreen2), which is the
	// filter's SOURCE buffer; the destination is always the main buffer.
}

// Lazily-allocated background scratch (the "VGAScreen2" role) for replaying the
// smoothie two-buffer ping-pong without disturbing the live surfaces.
static SDL_Surface *rl_scratch_b = NULL;

static SDL_Surface *rl_get_scratch_b(void)
{
	if (rl_scratch_b == NULL)
		rl_scratch_b = SDL_CreateRGBSurface(0, vga_width, vga_height, 8, 0, 0, 0, 0);
	return rl_scratch_b;
}

// Wrap a delta into [-m/2, m/2) so background rows interpolate smoothly across
// the 24px/28px tile wrap instead of snapping a whole tile.
static int wrap_delta(int d, int m)
{
	int r = d % m;
	if (r < 0)
		r += m;
	if (r >= m / 2)
		r -= m;
	return r;
}

void rl_finalize(void)
{
	RenderCmd *const cur = bufs[cur_buf];
	const size_t ncur = counts[cur_buf];
	RenderCmd *const prev = bufs[cur_buf ^ 1];
	const size_t nprev = counts[cur_buf ^ 1];

	// Build per-id forward-linked lists over the previous frame.
	static int head[RL_ID_MAX];
	static int *link = NULL;
	static size_t link_cap = 0;

	for (int i = 0; i < RL_ID_MAX; ++i)
		head[i] = -1;

	if (link_cap < nprev)
	{
		int *n = realloc(link, nprev * sizeof(int));
		if (n == NULL)
			return;  // on OOM, skip matching: every command stays snapped (dx=dy=0)
		link = n;
		link_cap = nprev;
	}

	for (size_t i = nprev; i-- > 0; )
	{
		int id = prev[i].id;
		if (id <= 0 || id >= RL_ID_MAX)
			continue;
		link[i] = head[id];
		head[id] = (int)i;
	}

	for (size_t i = 0; i < ncur; ++i)
	{
		RenderCmd *const c = &cur[i];
		c->dx = 0;
		c->dy = 0;

		const int id = c->id;
		if (id <= 0 || id >= RL_ID_MAX)
			continue;  // static / untagged: never interpolate

		const int pi = head[id];
		if (pi < 0)
			continue;  // no match (newly spawned): snap
		head[id] = link[pi];

		int dx = c->x - prev[pi].x;
		int dy = c->y - prev[pi].y;

		if (c->kind == RC_BG_ROW || c->kind == RC_BG_ROW_BLEND)
		{
			dx = wrap_delta(dx, 24);
			dy = wrap_delta(dy, 28);
		}
		else if (dx > 40 || dx < -40 || dy > 40 || dy < -40)
		{
			// Large jump => recycled slot or teleport; snap rather than streak.
			dx = 0;
			dy = 0;
		}

		c->dx = dx;
		c->dy = dy;
	}
}

static void rl_draw_cmd(SDL_Surface *dst, const RenderCmd *c, int x, int y)
{
	switch (c->kind)
	{
	case RC_SPRITE2:             blit_sprite2(dst, x, y, c->sheet, c->index); break;
	case RC_SPRITE2_CLIP:        blit_sprite2_clip(dst, x, y, c->sheet, c->index); break;
	case RC_SPRITE2_BLEND:       blit_sprite2_blend(dst, x, y, c->sheet, c->index); break;
	case RC_SPRITE2_DARKEN:      blit_sprite2_darken(dst, x, y, c->sheet, c->index); break;
	case RC_SPRITE2_FILTER:      blit_sprite2_filter(dst, x, y, c->sheet, c->index, c->filter); break;
	case RC_SPRITE2_FILTER_CLIP: blit_sprite2_filter_clip(dst, x, y, c->sheet, c->index, c->filter); break;
	case RC_SPRITE:              blit_sprite(dst, x, y, c->table, c->index); break;
	case RC_SPRITE_BLEND:        blit_sprite_blend(dst, x, y, c->table, c->index); break;
	case RC_SPRITE_HV:           blit_sprite_hv(dst, x, y, c->table, c->index, c->hue, c->value); break;
	case RC_SPRITE_HV_BLEND:     blit_sprite_hv_blend(dst, x, y, c->table, c->index, c->hue, c->value); break;
	case RC_SPRITE_HV_UNSAFE:    blit_sprite_hv_unsafe(dst, x, y, c->table, c->index, c->hue, c->value); break;
	case RC_SPRITE_DARK:         blit_sprite_dark(dst, x, y, c->table, c->index, c->black); break;
	case RC_BG_ROW:              blit_background_row(dst, x, y, c->map); break;
	case RC_BG_ROW_BLEND:        blit_background_row_blend(dst, x, y, c->map); break;
	case RC_STAR:                draw_starfield_star(dst, c->star_x, (int)(c->star_y + 0.5f), c->star_color); break;
	case RC_FILTER_SCREEN:       JE_filterScreen((JE_shortint)c->filt_col, (JE_shortint)c->filt_bright); break;
	}
}

// Residual = pixels the captured blit list does NOT reproduce: non-blit
// playfield draws (superpixels, boss-health bars, etc). Captured each tick by
// diffing the authoritative frame against a blit-only replay, then re-applied
// on every interpolated frame so those effects don't vanish between ticks.
// (They snap rather than interpolate — fine for sparks and screen-fixed bars.)
static int *res_off = NULL;
static Uint8 *res_val = NULL;
static size_t res_count = 0, res_cap = 0;

// Ship render-rate override (Phase 4): per-player offset applied to that ship's
// hull/shadow/charge (id in [RL_ID_SHIP_BASE, RL_ID_SIDEKICK_BASE)).
static bool ship_override_active = false;
static int ship_override_dx[2] = { 0, 0 }, ship_override_dy[2] = { 0, 0 };

void rl_set_ship_override(int player, int dx, int dy)
{
	if (player < 0 || player > 1)
		return;
	ship_override_active = true;
	ship_override_dx[player] = dx;
	ship_override_dy[player] = dy;
}

void rl_clear_ship_override(void)
{
	ship_override_active = false;
}

// Frame-rate-independent feedback: blend cur toward old by `a` on the low nibble
// (brightness), keeping cur's hue. With a<1 the smoothie filter advances only
// part of a full 35Hz step, so the trail length stays constant across refresh
// rates (a is chosen so the per-tick net step matches the original).
static void rl_blend_feedback(SDL_Surface *cur, const SDL_Surface *old, float a)
{
	Uint8 *c = (Uint8 *)cur->pixels;
	const Uint8 *o = (const Uint8 *)old->pixels;
	const size_t n = (size_t)cur->h * cur->pitch;
	for (size_t i = 0; i < n; ++i)
	{
		int lo = (int)((o[i] & 0x0f) * (1.0f - a) + (c[i] & 0x0f) * a + 0.5f);
		if (lo > 15)
			lo = 15;
		c[i] = (Uint8)((c[i] & 0xf0) | lo);
	}
}

// Background scratch (B) and feedback-snapshot scratch (C) for the replay.
static SDL_Surface *rl_scratch_c = NULL;

static SDL_Surface *rl_get_scratch_c(void)
{
	if (rl_scratch_c == NULL)
		rl_scratch_c = SDL_CreateRGBSurface(0, vga_width, vga_height, 8, 0, 0, 0, 0);
	return rl_scratch_c;
}

// Which recorded ids are drawn at their *extrapolated* (render-rate, forward)
// position instead of interpolated (one tick behind). Used to put parts of the
// world on the same render-rate clock as the variable-timestep ship. Enemy
// bullets travel at near-constant velocity, so cur + dx*alpha is accurate and
// doesn't snap at tick boundaries.
static bool rl_id_extrapolates(int id)
{
	// Enemy shots ride the render-rate clock (forward extrapolation) in sync
	// with the VT ship for fair dodging.
	//
	// Player shots are NOT extrapolated: they're fired as continuous streams
	// from a fixed muzzle, so leading the stream while a freshly-spawned shot
	// (no prior-frame match) stays put opens a visible gap at the gun. They
	// interpolate instead — no gap, and consistent with the (interpolated)
	// enemies. Ship-tracking shots (laser, main pulse) instead follow the ship
	// per-axis via ship_attach (see the replay), which keeps them on the gun.
	return id >= RL_ID_ESHOT_BASE && id < RL_ID_EXPL_BASE;  // enemy shots only
}

static void rl_replay_common(SDL_Surface *dst, float inv, float alpha, bool apply_residual, bool use_override, bool feedback, float feedback_blend)
{
	const bool was_recording = render_list_recording;
	render_list_recording = false;  // re-issued blits must not record themselves

	// A = main playfield buffer; B = background scratch (smoothie ping-pong).
	SDL_Surface *const A = dst;
	SDL_Surface *const B = rl_get_scratch_b();

	// The leaf blitters step rows using the global VGAScreen's pitch; point it
	// at dst so they write coherently (all 8-bit surfaces share a pitch anyway).
	SDL_Surface *const saved = VGAScreen;
	VGAScreen = A;

	// B is a fresh background scratch every frame. A is cleared too, EXCEPT in
	// feedback mode, where its previous contents are the smoothie filters' input
	// (trails/plasma) and must persist.
	if (B != NULL)
		JE_clr256(B);
	if (!feedback)
		JE_clr256(A);

	RenderCmd *const cur = bufs[cur_buf];
	const size_t n = counts[cur_buf];
	for (size_t i = 0; i < n; ++i)
	{
		const RenderCmd *const c = &cur[i];
		SDL_Surface *const src = (c->surface && B != NULL) ? B : A;

		if (c->kind == RC_ICED_BLUR || c->kind == RC_LAVA_FILTER || c->kind == RC_WATER_FILTER)
		{
			// Apply the smoothie filter. When feedback_blend < 1 (high refresh),
			// only advance the trail part of a full step so it stays the same
			// length in wall-clock time regardless of frame rate.
			SDL_Surface *const C = rl_get_scratch_c();
			const bool partial = (feedback_blend < 0.999f && C != NULL);
			if (partial)
				memcpy(C->pixels, A->pixels, (size_t)A->h * A->pitch);  // A_old

			switch (c->kind)
			{
			case RC_ICED_BLUR:    iced_blur_filter(A, src); break;
			case RC_LAVA_FILTER:  lava_filter(A, src);      break;
			default:              water_filter(A, src);     break;
			}

			if (partial)
				rl_blend_feedback(A, C, feedback_blend);
			continue;
		}

		if (c->kind == RC_STAR)
		{
			// Interpolate only the row (x is fixed): the star slides from its
			// previous row to the recorded one across the tick. star_dy is 0 on a
			// wrap tick, so a wrapped star simply snaps to the top.
			const float sy = c->star_y - c->star_dy * inv;
			draw_starfield_star(src, c->star_x, (int)(sy + 0.5f), c->star_color);
			continue;
		}

		int x = c->x, y = c->y;
		const bool is_ship_id = c->id >= RL_ID_SHIP_BASE && c->id < RL_ID_SIDEKICK_BASE;
		if (use_override && ship_override_active && is_ship_id)
		{
			// Ship hull/shadow/charge: render-rate driven, not time-interpolated.
			// Sidekicks are EXCLUDED — trailing companions (e.g. Gerund) follow the
			// ship's past path, so their motion differs from the ship's velocity;
			// driving them with the ship offset jitters. They interpolate by their
			// own per-frame motion instead (the branch below).
			// id = RL_ID_SHIP_BASE + playerNum (1 or 2) => player index 0/1.
			int p = c->id - RL_ID_SHIP_BASE - 1;
			if (p < 0) p = 0; else if (p > 1) p = 1;
			x += ship_override_dx[p];
			y += ship_override_dy[p];
		}
		else
		{
			// Per-axis placement. An axis that tracks the ship (ship_attach) is
			// drawn at the ship's render-rate position, so attached shots (laser,
			// main pulse) stay on the gun during strafes; otherwise the axis
			// extrapolates (enemy shots) or interpolates (everything else).
			const bool ovr = use_override && ship_override_active;
			const int sp = (c->ship_attach >> 2) & 1;  // player index
			const bool extrap = rl_id_extrapolates(c->id);

			if ((c->ship_attach & 1) && ovr)
				x += ship_override_dx[sp];           // X tracks the render-rate ship
			else if (c->dx)
			{
				if (extrap)
					x += (int)(c->dx * alpha + (c->dx >= 0 ? 0.5f : -0.5f));
				else if (inv != 0.0f)
					x -= (int)(c->dx * inv + (c->dx >= 0 ? 0.5f : -0.5f));
			}

			if ((c->ship_attach & 2) && ovr)
				y += ship_override_dy[sp];           // Y tracks the render-rate ship
			else if (c->dy)
			{
				if (extrap)
					y += (int)(c->dy * alpha + (c->dy >= 0 ? 0.5f : -0.5f));
				else if (inv != 0.0f)
					y -= (int)(c->dy * inv + (c->dy >= 0 ? 0.5f : -0.5f));
			}
		}
		rl_draw_cmd(src, c, x, y);  // backgrounds -> B, entities/filters-out -> A
	}

	VGAScreen = saved;
	render_list_recording = was_recording;

	if (apply_residual)
	{
		Uint8 *const p = (Uint8 *)A->pixels;
		for (size_t i = 0; i < res_count; ++i)
			p[res_off[i]] = res_val[i];
	}
}

void rl_replay(SDL_Surface *dst)
{
	rl_replay_common(dst, 0.0f, 0.0f, false, false, false, 1.0f);  // exact positions (inv=0, alpha=0)
}

void rl_replay_interp(SDL_Surface *dst, float alpha, bool feedback, float dt)
{
	if (alpha < 0.0f)
		alpha = 0.0f;
	else if (alpha > 1.0f)
		alpha = 1.0f;

	// Per-frame smoothie-feedback strength: chosen so that the net effect over a
	// whole tick (sum of dt = 1) equals the original single 35Hz application,
	// keeping trail length constant across refresh rates. a = 1 at dt = 1.
	if (dt < 0.0f)
		dt = 0.0f;
	else if (dt > 1.0f)
		dt = 1.0f;
	float blend = 2.0f * (1.0f - powf(0.5f, dt));
	if (blend > 1.0f)
		blend = 1.0f;
	else if (blend < 0.0f)
		blend = 0.0f;

	// Residual (non-blit pixels) is always re-applied last, on top of everything
	// the replay drew. On non-feedback levels it's the full set of non-blit draws
	// (superpixels, boss bar, HUD); on feedback (smoothie) levels it's the
	// overlay-only delta (boss bar, in-game displays) so the replayed per-pixel
	// filter can't smear/recolor those overlays. Either way the caller must have
	// populated it this tick (rl_capture_residual[_delta]).
	rl_replay_common(dst, 1.0f - alpha, alpha, true, true, feedback, blend);
}

// Drop any captured residual so a subsequent rl_replay_interp applies none.
// For callers whose frame is fully reproduced by the recorded blits and which
// must not inherit gameplay's residual pixels.
void rl_clear_residual(void)
{
	res_count = 0;
}

// Append one residual pixel (offset + value). Returns false if growth failed
// (out of memory) so the caller can stop; the residual captured so far is kept.
static bool rl_res_push(int off, Uint8 val)
{
	if (res_count == res_cap)
	{
		size_t ncap = res_cap ? res_cap * 2 : 1024;
		int *no = realloc(res_off, ncap * sizeof(*no));
		Uint8 *nv = realloc(res_val, ncap * sizeof(*nv));
		if (no == NULL || nv == NULL)
		{
			if (no != NULL) res_off = no;
			if (nv != NULL) res_val = nv;
			return false;  // OOM: keep what we have
		}
		res_off = no;
		res_val = nv;
		res_cap = ncap;
	}

	res_off[res_count] = off;
	res_val[res_count] = val;
	++res_count;
	return true;
}

void rl_capture_residual(SDL_Surface *reference, SDL_Surface *scratch)
{
	JE_clr256(scratch);
	rl_replay(scratch);  // blit-only reproduction at recorded positions

	res_count = 0;

	const size_t n = (size_t)reference->h * reference->pitch;
	const Uint8 *const ref = (const Uint8 *)reference->pixels;
	const Uint8 *const sc = (const Uint8 *)scratch->pixels;
	for (size_t i = 0; i < n; ++i)
	{
		if (ref[i] == sc[i])
			continue;
		if (!rl_res_push((int)i, ref[i]))
			break;
	}
}

// Residual from a direct before/after diff: the pixels that changed between two
// snapshots of the authoritative frame. Used on feedback (smoothie) levels to
// capture just the overlays drawn AFTER the per-pixel filters (boss bar, in-game
// displays). `before` is the frame snapshotted right before those overlays were
// drawn; `after` is the finished frame. The filtered playfield is identical in
// both, so only the overlay pixels are captured — they then get re-applied on
// top of every interpolated frame instead of being smeared by the replayed
// filter (a blit-only replay can't reproduce the evolved plasma, so the full
// rl_capture_residual would wrongly flag the whole filtered area).
void rl_capture_residual_delta(SDL_Surface *before, SDL_Surface *after)
{
	res_count = 0;

	const size_t n = (size_t)after->h * after->pitch;
	const Uint8 *const b = (const Uint8 *)before->pixels;
	const Uint8 *const a = (const Uint8 *)after->pixels;
	for (size_t i = 0; i < n; ++i)
	{
		if (a[i] == b[i])
			continue;
		if (!rl_res_push((int)i, a[i]))
			break;
	}
}

size_t rl_replay_and_compare(SDL_Surface *scratch, SDL_Surface *reference)
{
	JE_clr256(scratch);  // the frame normally starts with a full clear to 0
	rl_replay(scratch);

	const size_t n = (size_t)reference->h * reference->pitch;
	const Uint8 *a = (const Uint8 *)scratch->pixels;
	const Uint8 *b = (const Uint8 *)reference->pixels;
	size_t mismatch = 0;
	for (size_t i = 0; i < n; ++i)
		if (a[i] != b[i])
			++mismatch;
	return mismatch;
}
