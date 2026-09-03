/*
 * OpenTyrian 2000 Engaged: render-list capture & replay (see render_list.h).
 */
#include "render_list.h"

#include "backgrnd.h"
#include "rollback.h"
#include "sprite.h"
#include "vga256d.h"
#include "video.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

bool render_list_recording = false;
int rl_current_id = 0;
int rl_shot_attach = 0;
float rl_current_par_frac = 0.0f;
int rl_current_par_layer = 0;
float rl_current_par_anchor = 0.0f;
float rl_current_par_yfrac = 0.0f;
int rl_current_par_ybase = 0;
int rl_current_par_ylayer = 0;
int rl_current_vel_x = 0, rl_current_vel_y = 0;
int rl_current_acc_x = 0, rl_current_acc_y = 0;
float rl_current_sub_x = 0.0f, rl_current_sub_y = 0.0f;

// Forward decl: rl_finalize must preserve extrapolating ids' recorded dx/dy.
static bool rl_id_extrapolates(int id);

// Double-buffered command lists: one for the current tick, one for the previous
// (used to derive per-command motion for interpolation).
static RenderCmd *bufs[2] = { NULL, NULL };
static size_t counts[2] = { 0, 0 };
static size_t caps[2] = { 0, 0 };
static int cur_buf = 0;

// Scratch used while matching command identities across adjacent frames.
static int match_head[RL_ID_MAX];
static int match_prev_count[RL_ID_MAX], match_cur_count[RL_ID_MAX];
static int *match_link;
static size_t match_link_cap;

// Commands after the last smoothie filter are not filter input. Replay them in the
// high-resolution tail pass. See doc/notes.md#render-list.
static size_t bg_filter_end[2];
// Layers recorded after the final filter replay at foreground resolution.
static Uint8 bg_tail_layers[2];

static inline bool rl_cmd_is_filter(int kind)
{
	return kind == RC_ICED_BLUR || kind == RC_LAVA_FILTER || kind == RC_WATER_FILTER || kind == RC_BLUR;
}

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
	// Parallax sub-pixel fraction (enemies); finalize fills par_frac_dx from the prev match.
	c->par_frac = rl_current_par_frac;
	c->par_frac_dx = 0.0f;
	c->par_layer = (Uint8)rl_current_par_layer;
	c->par_anchor = rl_current_par_anchor;
	// Vertical background binding; finalize fills only the entity-local displacement.
	c->par_ybase = rl_current_par_ybase;
	c->par_yfrac = rl_current_par_yfrac;
	c->par_yown100 = 0;
	c->par_ylayer = (Uint8)rl_current_par_ylayer;
	// Seed dx/dy from the recorded velocity: rl_finalize keeps it for extrapolating
	// ids (shots) and overwrites it with the prev/cur diff for the rest.
	c->dx = rl_current_vel_x;
	c->dy = rl_current_vel_y;
	// Acceleration (shots only); finalize never touches it, so it survives for the
	// extrapolating ids that read it and stays 0 (unused) for everything else.
	c->acc_x = rl_current_acc_x;
	c->acc_y = rl_current_acc_y;
	// Sub-pixel remainder of a rounded offset; finalize fills its per-tick change.
	c->sub_x = rl_current_sub_x;
	c->sub_y = rl_current_sub_y;
	c->sub_dx = 0.0f;
	c->sub_dy = 0.0f;
	// On smoothie levels the playfield draw ping-pongs between game_screen and
	// VGAScreen2; capture which buffer this draw targeted so replay can route it.
	c->surface = (VGAScreen == VGAScreen2) ? 1 : 0;
	return c;
}

/* Opaque overlay rectangles excluded from residual comparisons. */
#define RL_OVERLAY_RECTS_MAX 12
static struct { int x, y, w, h; } overlay_rect[RL_OVERLAY_RECTS_MAX];
static int overlay_rect_count = 0;

void rl_mark_overlay_rect(int x, int y, int w, int h)
{
	if (rollback_resim_silent || w <= 0 || h <= 0)
		return;
	if (overlay_rect_count >= RL_OVERLAY_RECTS_MAX)
		return;

	overlay_rect[overlay_rect_count].x = x;
	overlay_rect[overlay_rect_count].y = y;
	overlay_rect[overlay_rect_count].w = w;
	overlay_rect[overlay_rect_count].h = h;
	++overlay_rect_count;
}

void rl_begin_record(void)
{
	cur_buf ^= 1;             // previous current becomes prev; record into the other
	counts[cur_buf] = 0;
	bg_filter_end[cur_buf] = 0;
	bg_tail_layers[cur_buf] = 0;
	rl_current_id = 0;
	rl_current_par_frac = 0.0f;
	rl_current_par_layer = 0;
	rl_current_par_anchor = 0.0f;
	rl_current_par_yfrac = 0.0f;
	rl_current_par_ybase = 0;
	rl_current_par_ylayer = 0;
	rl_current_vel_x = 0;
	rl_current_vel_y = 0;
	rl_current_acc_x = 0;
	rl_current_acc_y = 0;
	rl_current_sub_x = 0.0f;
	rl_current_sub_y = 0.0f;
	for (int layer = 1; layer <= 3; ++layer)
		bg_layer_xofs_valid[layer] = false;
	overlay_rect_count = 0;
	render_list_recording = true;
}

void rl_end_record(void)
{
	render_list_recording = false;

	const RenderCmd *const cur = bufs[cur_buf];
	bg_filter_end[cur_buf] = 0;
	for (size_t i = counts[cur_buf]; i > 0; --i)
	{
		if (rl_cmd_is_filter(cur[i - 1].kind))
		{
			bg_filter_end[cur_buf] = i;
			break;
		}
	}

	bg_tail_layers[cur_buf] = 0;
	for (size_t i = bg_filter_end[cur_buf]; i < counts[cur_buf]; ++i)
	{
		const RenderCmd *const c = &cur[i];
		if (!c->surface && (c->kind == RC_BG_ROW || c->kind == RC_BG_ROW_BLEND) &&
		    c->id >= RL_ID_BG_BASE + 1 && c->id <= RL_ID_BG_BASE + 3)
		{
			bg_tail_layers[cur_buf] |= (Uint8)(1u << (c->id - RL_ID_BG_BASE));
		}
	}
}

// Drop a partial recording and restore the last complete frame as current.
void rl_abort_record(void)
{
	if (!render_list_recording)
		return;
	render_list_recording = false;
	counts[cur_buf] = 0;
	cur_buf ^= 1;
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

void rl_rec_sprite2_filter_bright(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter, bool clip)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = clip ? RC_SPRITE2_FILTER_BRIGHT_CLIP : RC_SPRITE2_FILTER_BRIGHT;
	c->x = x;
	c->y = y;
	c->sheet = sheet;
	c->index = index;
	c->filter = filter;
}

void rl_rec_sprite2_blend_filter(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = RC_SPRITE2_BLEND_FILTER;
	c->x = x;
	c->y = y;
	c->sheet = sheet;
	c->index = index;
	c->filter = filter;
}

void rl_rec_sprite2_alpha(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter, bool dye, bool clip)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = dye ? (clip ? RC_SPRITE2_ALPHA_DYE_CLIP : RC_SPRITE2_ALPHA_DYE)
	              : (clip ? RC_SPRITE2_ALPHA_CLIP : RC_SPRITE2_ALPHA);
	c->x = x;
	c->y = y;
	c->sheet = sheet;
	c->index = index;
	c->filter = filter;
}

void rl_rec_sprite2_solid(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 color)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = RC_SPRITE2_SOLID;
	c->x = x;
	c->y = y;
	c->sheet = sheet;
	c->index = index;
	c->filter = color;
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

void rl_rec_bg_row(int x, int y, Uint8 **map, bool blend, int mirror_w, int col0)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = blend ? RC_BG_ROW_BLEND : RC_BG_ROW;
	c->x = x;
	c->y = y;
	c->map = map;
	c->bg_mirror_w = (Sint8)mirror_w;
	c->bg_col0 = (Sint8)col0;
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

void rl_rec_superpixel(int x, int y, int dx, int dy, Uint8 z, Uint8 color, Uint8 bright)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = RC_SUPERPIXEL;
	c->x = x;
	c->y = y;
	c->sp_dx = dx;
	c->sp_dy = dy;
	c->sp_z = z;
	c->sp_color = color;
	c->sp_bright = bright;
}

void rl_rec_hp_bar(int x, int y, int along, int fill, Uint8 col, bool vertical, Uint8 opacity, Uint8 groove)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = RC_HP_BAR;
	c->x = x;
	c->y = y;
	c->bar_w = along;
	c->bar_fill = fill;
	c->bar_col = col;
	c->bar_vertical = vertical ? 1 : 0;
	c->bar_opacity = opacity;
	c->bar_groove = groove;
}

void rl_rec_filter_screen(int col, int brightness)
{
	RenderCmd *c = rl_push();
	if (c == NULL)
		return;
	c->kind = RC_FILTER_SCREEN;
	c->id = RL_ID_FILTER;   // one per tick; lets rl_finalize match it for brightness interpolation
	c->x = 0;
	c->y = 0;
	c->filt_col = col;
	c->filt_bright = brightness;
	c->filt_dbright = 0;
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

// Background scratch for smoothie replay. Cache 1x and supersampled sizes because the shop
// preview can use both in one frame.
static SDL_Surface *rl_scratch_b[2] = { NULL, NULL };  // [0] = 1x, [1] = supersampled

static SDL_Surface *rl_get_scratch_b(int scale)
{
	SDL_Surface **const slot = &rl_scratch_b[scale == 1 ? 0 : 1];
	const int w = vga_width * scale, h = vga_height * scale;

	if (*slot != NULL && ((*slot)->w != w || (*slot)->h != h))
	{
		SDL_FreeSurface(*slot);
		*slot = NULL;
	}
	if (*slot == NULL)
		*slot = SDL_CreateRGBSurface(0, w, h, 8, 0, 0, 0, 0);
	return *slot;
}

// Round-half-away-from-zero, the rounding the 1x replay always used; shared by every
// scaled position computation so scale==1 reproduces the old integer path exactly.
static inline int rl_iround(float v)
{
	return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

// Round a fractional POSITION offset to the nearest pixel, with exact integer-translation
// invariance.
static inline int rl_round_offset(double v)
{
	return (int)floor(v + 0.5);
}

// Restore the sub-pixel remainder removed by the simulation's rounded position.
static inline float rl_sub_disp(int own, float sub, float sub_d, float inv)
{
	if (inv != 0.0f && own <= 40 && own >= -40)
		return sub - ((float)own + sub_d) * inv;
	return sub;
}

// Canonical vertical transform shared by background rows and bound entities.
// Layer and entity motion are rounded together to avoid one-pixel phase splits.
static inline int rl_layer_y_offset(int layer, bool now, float inv, int scale, int own100)
{
	const float rate = now ? bg_layer_dy_now[layer] : bg_layer_dy[layer];
	const float frac = now ? bg_layer_yfrac_now[layer] : bg_layer_yfrac[layer];
	// Subtract exact hundredths to keep half-pixel endpoints rounding consistently.
	const int rate100 = rl_iround(rate * 100.0f);
	const int frac100 = rl_iround(frac * 100.0f);
	const double offset = ((double)frac100 - (double)(rate100 + own100) * (double)inv) *
	                      (double)scale / 100.0;
	return rl_round_offset(offset);
}

// Filtered layers may use a lower scale or hold at the tick endpoint.
static inline int rl_bound_x_offset(const RenderCmd *c, int layer, float inv, int scale,
                                    int bg_scale, float bg_inv)
{
	const float total = c->par_frac - (c->dx + c->par_frac_dx) * inv;
	if (bg_scale == scale && bg_inv == inv)
		return rl_iround(total * scale);

	const float shared_at_entity = bg_layer_frac[layer] - bg_layer_dx[layer] * inv;
	const float shared_at_bg = bg_layer_frac[layer] - bg_layer_dx[layer] * bg_inv;
	const float own = total - shared_at_entity;
	const int expand = scale / bg_scale;
	return rl_iround(shared_at_bg * bg_scale) * expand + rl_iround(own * scale);
}

static inline int rl_bound_y_offset(int layer, float inv, int scale, int own100,
                                    int bg_scale, float bg_inv)
{
	if (bg_scale == scale && bg_inv == inv)
		return rl_layer_y_offset(layer, false, inv, scale, own100);

	const int rate100 = rl_iround(bg_layer_dy[layer] * 100.0f);
	const int frac100 = rl_iround(bg_layer_yfrac[layer] * 100.0f);
	const double shared = ((double)frac100 - (double)rate100 * (double)bg_inv) *
	                      (double)bg_scale / 100.0;
	const double own = -(double)own100 * (double)inv * (double)scale / 100.0;
	const int expand = scale / bg_scale;
	return rl_round_offset(shared) * expand + rl_round_offset(own);
}

// Wrap a delta into [-m/2, m/2) so background rows interpolate smoothly across the
// 24px/28px tile wrap instead of snapping. For either-way axes (horizontal scroll).
static int wrap_delta(int d, int m)
{
	int r = d % m;
	if (r < 0)
		r += m;
	if (r >= m / 2)
		r -= m;
	return r;
}

// Resolve vertical tile wraps downward; fast scroll can exceed half a tile per tick.
static int wrap_delta_down(int d, int m)
{
	int r = d % m;
	if (r < 0)
		r += m;
	return r;
}

void rl_finalize(void)
{
	RenderCmd *const cur = bufs[cur_buf];
	const size_t ncur = counts[cur_buf];
	RenderCmd *const prev = bufs[cur_buf ^ 1];
	const size_t nprev = counts[cur_buf ^ 1];

	// Per-id forward-linked lists over the previous frame, plus per-id blit counts
	// for both frames (to detect a changed sub-blit set; see the snap below).
	for (int i = 0; i < RL_ID_MAX; ++i)
	{
		match_head[i] = -1;
		match_prev_count[i] = 0;
		match_cur_count[i] = 0;
	}

	if (match_link_cap < nprev)
	{
		size_t new_cap = match_link_cap != 0 ? match_link_cap : 4096;
		while (new_cap < nprev)
		{
			const size_t next_cap = new_cap * 2;
			if (next_cap <= new_cap)
			{
				new_cap = nprev;
				break;
			}
			new_cap = next_cap;
		}
		int *n = realloc(match_link, new_cap * sizeof(*n));
		if (n == NULL)
			return;  // on OOM, skip matching: every command stays snapped (dx=dy=0)
		match_link = n;
		match_link_cap = new_cap;
	}

	for (size_t i = nprev; i-- > 0; )
	{
		int id = prev[i].id;
		if (id <= 0 || id >= RL_ID_MAX)
			continue;
		match_link[i] = match_head[id];
		match_head[id] = (int)i;
		++match_prev_count[id];
	}

	// Count this frame's blits per id (a pre-pass, since the matching loop below
	// needs each id's full current count before it decides the first blit).
	for (size_t i = 0; i < ncur; ++i)
	{
		int id = cur[i].id;
		if (id > 0 && id < RL_ID_MAX)
			++match_cur_count[id];
	}

	for (size_t i = 0; i < ncur; ++i)
	{
		RenderCmd *const c = &cur[i];
		// Normalize bound entities to the anchor their layer recorded.
		if (c->par_layer >= 1 && c->par_layer <= 3 && bg_layer_xofs_valid[c->par_layer])
			c->par_frac += bg_layer_xofs[c->par_layer] - c->par_anchor;

		const int id = c->id;

		// Extrapolating ids already carry their own velocity in dx/dy; keep it (see
		// rl_id_extrapolates); no large-jump snap, no recycled-slot streak.
		if (rl_id_extrapolates(id))
			continue;

		// Use the recorded velocity when blinking sprites cannot be paired by position.
		const int hint_dx = c->dx, hint_dy = c->dy;

		c->dx = 0;
		c->dy = 0;
		c->par_yown100 = 0;

		if (id <= 0 || id >= RL_ID_MAX)
			continue;  // static / untagged: never interpolate

		// A changed blit count makes positional pairing unsafe; snap for one tick
		// (or glide on the recorded velocity when the recorder supplied one).
		if (match_prev_count[id] != match_cur_count[id])
		{
			c->dx = hint_dx;
			c->dy = hint_dy;
			continue;
		}

		const int pi = match_head[id];
		if (pi < 0)
		{
			c->dx = hint_dx;  // newly (re)appeared mid-motion: glide, don't snap
			c->dy = hint_dy;
			continue;
		}
		match_head[id] = match_link[pi];

		int dx = c->x - prev[pi].x;
		int dy = c->y - prev[pi].y;

		// Add the layer anchor's fractional change to its integer displacement.
		c->par_frac_dx = c->par_frac - prev[pi].par_frac;

		// Sub-pixel remainder: its change this tick completes dx/dy into the exact
		// displacement, so replay can interpolate the unrounded path.
		c->sub_dx = c->sub_x - prev[pi].sub_x;
		c->sub_dy = c->sub_y - prev[pi].sub_y;

		// Recover entity-local displacement. Replay applies the layer rate separately
		// so unmatched or clipped sprites still follow their layer.
		int par_yown100 = 0;
		bool par_ybound_match = false;
		if (c->par_ylayer >= 1 && c->par_ylayer <= 3 &&
		    c->par_ylayer == prev[pi].par_ylayer)
		{
			par_ybound_match = true;
			const int endpoint100 =
			    ((c->y + c->par_ybase) -
			     (prev[pi].y + prev[pi].par_ybase)) * 100 +
			    rl_iround(c->par_yfrac * 100.0f) -
			    rl_iround(prev[pi].par_yfrac * 100.0f);
			const int L = c->par_ylayer;
			const float layer_rate = bg_layer_dy[L];
			par_yown100 = endpoint100 - rl_iround(layer_rate * 100.0f);
		}

		if (c->kind == RC_BG_ROW || c->kind == RC_BG_ROW_BLEND)
		{
			dx = wrap_delta(dx, 24);       // horizontal scroll: either direction
			// Use the full layer delta; screen coordinates lose whole-tile motion.
			const int layer = id - RL_ID_BG_BASE;
			dy = (layer >= 1 && layer <= 3) ? bgScrollDeltaY[layer] : wrap_delta_down(dy, 28);
		}
		else if (c->kind == RC_FILTER_SCREEN)
		{
			// Interpolate brightness ramps; snap sentinels and palette-bank changes.
			int db = c->filt_bright - prev[pi].filt_bright;
			if (c->filt_bright == -99 || prev[pi].filt_bright == -99 ||
			    c->filt_col != prev[pi].filt_col || db > 14 || db < -14)
				db = 0;
			c->filt_dbright = db;
		}
		else if (dx > 40 || dx < -40 ||
		         (par_ybound_match
		              ? (par_yown100 > 4000 || par_yown100 < -4000)
		              : (dy > 40 || dy < -40)))
		{
			// Snap recycled or teleported enemies, excluding their layer's own movement.
			dx = 0;
			dy = 0;
			par_yown100 = 0;
			c->sub_dx = 0.0f;  // no motion to complete
			c->sub_dy = 0.0f;
		}

		c->dx = dx;
		c->dy = dy;
		c->par_yown100 = par_yown100;
	}
}

// Draw one explosion spark (superpixel): a 5-pixel additive blend, matching
// JE_drawSP (varz.c) so an exact (alpha=0) replay reproduces it pixel-for-pixel.
static void rl_draw_superpixel(SDL_Surface *dst, int x, int y, Uint8 z, Uint8 color, Uint8 bright)
{
	if (x < 0 || y < 0 || x >= dst->w || y >= dst->h)
		return;
	const int pitch = dst->pitch;
	Uint8 *const s = (Uint8 *)dst->pixels + y * pitch + x;
	*s = rl_superpixel_value(*s, z, color, bright);
	if (x > 0)            *(s - 1)     = rl_superpixel_value(*(s - 1),     z >> 1, color, bright >> 1);
	if (x < dst->w - 1)   *(s + 1)     = rl_superpixel_value(*(s + 1),     z >> 1, color, bright >> 1);
	if (y > 0)            *(s - pitch) = rl_superpixel_value(*(s - pitch), z >> 1, color, bright >> 1);
	if (y < dst->h - 1)   *(s + pitch) = rl_superpixel_value(*(s + pitch), z >> 1, color, bright >> 1);
}

// One scale x scale block of superpixel light (additive-ish blend matching
// rl_draw_superpixel's per-pixel math), clipped.
static void rl_superpixel_block(SDL_Surface *dst, int x, int y, int scale, Uint8 z, Uint8 color, Uint8 bright)
{
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + scale, y1 = y + scale;
	if (x1 > dst->w)
		x1 = dst->w;
	if (y1 > dst->h)
		y1 = dst->h;

	for (int yy = y0; yy < y1; ++yy)
	{
		Uint8 *p = (Uint8 *)dst->pixels + yy * dst->pitch + x0;
		for (int xx = x0; xx < x1; ++xx, ++p)
			*p = rl_superpixel_value(*p, z, color, bright);
	}
}

// Supersampled explosion spark: the same 5-tap pattern as rl_draw_superpixel with
// each tap a scale x scale block (halo taps one whole 1x pixel = `scale` away).
static void rl_draw_superpixel_scaled(SDL_Surface *dst, int x, int y, Uint8 z, Uint8 color, Uint8 bright, int scale)
{
	if (x < -(scale - 1) || y < -(scale - 1) || x >= dst->w || y >= dst->h)
		return;
	rl_superpixel_block(dst, x, y, scale, z, color, bright);
	rl_superpixel_block(dst, x - scale, y, scale, z >> 1, color, bright >> 1);
	rl_superpixel_block(dst, x + scale, y, scale, z >> 1, color, bright >> 1);
	rl_superpixel_block(dst, x, y - scale, scale, z >> 1, color, bright >> 1);
	rl_superpixel_block(dst, x, y + scale, scale, z >> 1, color, bright >> 1);
}

// Plot one clipped bar pixel. Alpha blending retains the bar's palette bank and
// mixes only brightness over the reconstructed background.
static inline void rl_hp_plot(SDL_Surface *dst, int x, int y, Uint8 col, Uint8 opacity)
{
	if (x < 0 || y < 0 || x >= dst->w || y >= dst->h)
		return;
	Uint8 *const p = &((Uint8 *)dst->pixels)[y * dst->pitch + x];
	if (opacity >= 255)
	{
		*p = col;
		return;
	}
	// Mix brightness: fg at `opacity`, background at the remainder. Keep the bar's
	// bank (col & 0xf0) so the fade stays inside the health-bar colour ramp.
	const int fg = col & 0x0f, bg = *p & 0x0f;
	int lo = (fg * opacity + bg * (255 - opacity) + 127) / 255;
	if (lo > 15)
		lo = 15;
	*p = (Uint8)((col & 0xf0) | lo);
}

// Draw the same clipped health bar for the simulation frame and interpolated replay.
// Horizontal bars fill rightward; vertical bars fill upward.
void rl_draw_hp_bar(SDL_Surface *dst, int x, int y, int along, int fill, Uint8 col, bool vertical, Uint8 opacity, Uint8 grooveCol)
{
	if (along < 1 || opacity == 0)
		return;
	if (fill > along) fill = along;
	if (fill < 0)     fill = 0;

	// Keep the groove and shadow in the fill's palette bank.
	const int   bank   = col & 0xf0;
	const Uint8 groove = (grooveCol != 0) ? grooveCol : (Uint8)(bank + 2);
	const Uint8 shadow = (Uint8)((grooveCol != 0 ? (grooveCol & 0xf0) : bank) + 0);
	const Uint8 edge   = ((col & 0x0f) < 15) ? (Uint8)(col + 1) : col;

	if (!vertical)
	{
		for (int i = 0; i < along; ++i)          // empty groove
			rl_hp_plot(dst, x + i, y, groove, opacity);
		for (int i = 0; i < along; ++i)          // dark base shadow
			rl_hp_plot(dst, x + i, y + 1, shadow, opacity);
		for (int i = 0; i < fill; ++i)           // remaining health
			rl_hp_plot(dst, x + i, y, col, opacity);
		if (fill > 0)                            // glossy leading edge
			rl_hp_plot(dst, x + fill - 1, y, edge, opacity);
	}
	else
	{
		for (int i = 0; i < along; ++i)          // empty groove
			rl_hp_plot(dst, x, y + i, groove, opacity);
		for (int i = 0; i < along; ++i)          // dark base shadow
			rl_hp_plot(dst, x + 1, y + i, shadow, opacity);
		for (int i = 0; i < fill; ++i)           // remaining health, from the bottom up
			rl_hp_plot(dst, x, y + along - 1 - i, col, opacity);
		if (fill > 0)                            // glossy leading edge
			rl_hp_plot(dst, x, y + along - fill, edge, opacity);
	}
}

// One scale x scale block of health-bar pixel, clipped (see rl_hp_plot).
static void rl_hp_plot_block(SDL_Surface *dst, int x, int y, Uint8 col, Uint8 opacity, int scale)
{
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + scale, y1 = y + scale;
	if (x1 > dst->w)
		x1 = dst->w;
	if (y1 > dst->h)
		y1 = dst->h;

	for (int yy = y0; yy < y1; ++yy)
		for (int xx = x0; xx < x1; ++xx)
			rl_hp_plot(dst, xx, yy, col, opacity);
}

// Supersampled health bars use interpolated high-resolution coordinates.
static void rl_draw_hp_bar_scaled(SDL_Surface *dst, int x, int y, int along, int fill, Uint8 col,
                                  bool vertical, Uint8 opacity, Uint8 grooveCol, int scale)
{
	if (along < 1 || opacity == 0)
		return;
	if (fill > along) fill = along;
	if (fill < 0)     fill = 0;

	// Match rl_draw_hp_bar's bank-derived track, shadow, and edge.
	const int   bank   = col & 0xf0;
	const Uint8 groove = (grooveCol != 0) ? grooveCol : (Uint8)(bank + 2);
	const Uint8 shadow = (Uint8)((grooveCol != 0 ? (grooveCol & 0xf0) : bank) + 0);
	const Uint8 edge   = ((col & 0x0f) < 15) ? (Uint8)(col + 1) : col;

	if (!vertical)
	{
		for (int i = 0; i < along; ++i)          // empty groove
			rl_hp_plot_block(dst, x + i * scale, y, groove, opacity, scale);
		for (int i = 0; i < along; ++i)          // dark base shadow
			rl_hp_plot_block(dst, x + i * scale, y + scale, shadow, opacity, scale);
		for (int i = 0; i < fill; ++i)           // remaining health
			rl_hp_plot_block(dst, x + i * scale, y, col, opacity, scale);
		if (fill > 0)                            // glossy leading edge
			rl_hp_plot_block(dst, x + (fill - 1) * scale, y, edge, opacity, scale);
	}
	else
	{
		for (int i = 0; i < along; ++i)          // empty groove
			rl_hp_plot_block(dst, x, y + i * scale, groove, opacity, scale);
		for (int i = 0; i < along; ++i)          // dark base shadow
			rl_hp_plot_block(dst, x + scale, y + i * scale, shadow, opacity, scale);
		for (int i = 0; i < fill; ++i)           // remaining health, from the bottom up
			rl_hp_plot_block(dst, x, y + (along - 1 - i) * scale, col, opacity, scale);
		if (fill > 0)                            // glossy leading edge
			rl_hp_plot_block(dst, x, y + (along - fill) * scale, edge, opacity, scale);
	}
}

static void rl_draw_cmd(SDL_Surface *dst, const RenderCmd *c, int x, int y)
{
	switch (c->kind)
	{
	case RC_HP_BAR:              rl_draw_hp_bar(dst, x, y, c->bar_w, c->bar_fill, c->bar_col, c->bar_vertical, c->bar_opacity, c->bar_groove); break;
	// Extrapolation can move an otherwise unclipped sprite past an x edge. Clip these commands to
	// prevent row wrapping; in-bounds and exact replays are unchanged.
	case RC_SPRITE2:             blit_sprite2_clip(dst, x, y, c->sheet, c->index); break;
	case RC_SPRITE2_CLIP:        blit_sprite2_clip(dst, x, y, c->sheet, c->index); break;
	case RC_SPRITE2_BLEND:       blit_sprite2_blend_clip(dst, x, y, c->sheet, c->index); break;
	case RC_SPRITE2_DARKEN:      blit_sprite2_darken_clip(dst, x, y, c->sheet, c->index); break;
	case RC_SPRITE2_SOLID:       blit_sprite2_solid_clip(dst, x, y, c->sheet, c->index, c->filter); break;
	case RC_SPRITE2_FILTER:      blit_sprite2_filter(dst, x, y, c->sheet, c->index, c->filter); break;
	case RC_SPRITE2_FILTER_CLIP: blit_sprite2_filter_clip(dst, x, y, c->sheet, c->index, c->filter); break;
	case RC_SPRITE2_FILTER_BRIGHT:
	case RC_SPRITE2_FILTER_BRIGHT_CLIP:
		blit_sprite2_filter_bright_clip(dst, x, y, c->sheet, c->index, c->filter); break;
	case RC_SPRITE2_BLEND_FILTER: blit_sprite2_blend_filter_clip(dst, x, y, c->sheet, c->index, c->filter); break;
	// Alpha commands replay through the clipping form for the same reason the plain sprite ones do.
	case RC_SPRITE2_ALPHA:
	case RC_SPRITE2_ALPHA_CLIP:
		blit_sprite2_alpha_clip(dst, x, y, c->sheet, c->index, -1, (Uint8)(c->filter & 0x0f)); break;
	case RC_SPRITE2_ALPHA_DYE:
	case RC_SPRITE2_ALPHA_DYE_CLIP:
		blit_sprite2_alpha_clip(dst, x, y, c->sheet, c->index, (c->filter >> 4) & 0x0f,
		                        (Uint8)(c->filter & 0x0f)); break;
	case RC_SPRITE:              blit_sprite(dst, x, y, c->table, c->index); break;
	case RC_SPRITE_BLEND:        blit_sprite_blend(dst, x, y, c->table, c->index); break;
	case RC_SPRITE_HV:           blit_sprite_hv(dst, x, y, c->table, c->index, c->hue, c->value); break;
	case RC_SPRITE_HV_BLEND:     blit_sprite_hv_blend(dst, x, y, c->table, c->index, c->hue, c->value); break;
	case RC_SPRITE_HV_UNSAFE:    blit_sprite_hv_unsafe(dst, x, y, c->table, c->index, c->hue, c->value); break;
	case RC_SPRITE_DARK:         blit_sprite_dark(dst, x, y, c->table, c->index, c->black); break;
	case RC_SPRITE_ALPHA:
		blit_sprite_alpha(dst, x, y, c->table, c->index,
		                  c->hue == BLIT_ALPHA_KEEP_BANK ? -1 : (int)c->hue, (Uint8)c->value); break;
	case RC_BG_ROW:              blit_background_row(dst, x, y, c->map, c->bg_mirror_w, c->bg_col0); break;
	case RC_BG_ROW_BLEND:        blit_background_row_blend(dst, x, y, c->map, c->bg_mirror_w, c->bg_col0); break;
	case RC_STAR:                draw_starfield_star(dst, c->star_x, (int)(c->star_y + 0.5f), c->star_color); break;
	case RC_FILTER_SCREEN:       JE_filterScreenApply(dst, (JE_shortint)c->filt_col, (JE_shortint)c->filt_bright); break;
	}
}

// Supersampled dispatch: x,y are HI coordinates. Every kind routes to its scaled drawer; the
// clip-variant sprite kinds share the scaled blitter (it always clips).
static void rl_draw_cmd_scaled(SDL_Surface *dst, const RenderCmd *c, int x, int y, int scale)
{
	switch (c->kind)
	{
	case RC_HP_BAR:              rl_draw_hp_bar_scaled(dst, x, y, c->bar_w, c->bar_fill, c->bar_col, c->bar_vertical, c->bar_opacity, c->bar_groove, scale); break;
	case RC_SPRITE2:             blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_COPY, 0); break;
	case RC_SPRITE2_CLIP:        blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_COPY, 0); break;
	case RC_SPRITE2_BLEND:       blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_BLEND, 0); break;
	case RC_SPRITE2_DARKEN:      blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_DARKEN, 0); break;
	case RC_SPRITE2_SOLID:       blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_SOLID, c->filter); break;
	case RC_SPRITE2_FILTER:      blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_FILTER, c->filter); break;
	case RC_SPRITE2_FILTER_CLIP: blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_FILTER, c->filter); break;
	case RC_SPRITE2_FILTER_BRIGHT:
	case RC_SPRITE2_FILTER_BRIGHT_CLIP:
		blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_FILTER_BRIGHT, c->filter); break;
	case RC_SPRITE2_BLEND_FILTER: blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_BLEND_FILTER, c->filter); break;
	case RC_SPRITE2_ALPHA:
	case RC_SPRITE2_ALPHA_CLIP:
		blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_ALPHA, c->filter); break;
	case RC_SPRITE2_ALPHA_DYE:
	case RC_SPRITE2_ALPHA_DYE_CLIP:
		blit_sprite2_scaled(dst, x, y, c->sheet, c->index, scale, BLIT2_ALPHA_DYE, c->filter); break;
	case RC_SPRITE:              blit_sprite_table_scaled(dst, x, y, c->table, c->index, scale, BLITT_COPY, 0, 0, false); break;
	case RC_SPRITE_BLEND:        blit_sprite_table_scaled(dst, x, y, c->table, c->index, scale, BLITT_BLEND, 0, 0, false); break;
	case RC_SPRITE_HV:           blit_sprite_table_scaled(dst, x, y, c->table, c->index, scale, BLITT_HV, c->hue, c->value, false); break;
	case RC_SPRITE_HV_BLEND:     blit_sprite_table_scaled(dst, x, y, c->table, c->index, scale, BLITT_HV_BLEND, c->hue, c->value, false); break;
	case RC_SPRITE_HV_UNSAFE:    blit_sprite_table_scaled(dst, x, y, c->table, c->index, scale, BLITT_HV_UNSAFE, c->hue, c->value, false); break;
	case RC_SPRITE_DARK:         blit_sprite_table_scaled(dst, x, y, c->table, c->index, scale, BLITT_DARK, 0, 0, c->black); break;
	case RC_SPRITE_ALPHA:        blit_sprite_table_scaled(dst, x, y, c->table, c->index, scale, BLITT_ALPHA, c->hue, c->value, false); break;
	case RC_BG_ROW:              blit_background_row_scaled(dst, x, y, c->map, scale, false, c->bg_mirror_w, c->bg_col0); break;
	case RC_BG_ROW_BLEND:        blit_background_row_scaled(dst, x, y, c->map, scale, true, c->bg_mirror_w, c->bg_col0); break;
	default:                     break;
	}
}

// Residual pixels come from playfield effects the blit list cannot reproduce. Reapply them on
// interpolated frames; they may snap between ticks, but they must not disappear.
static int *res_off = NULL;
static Uint8 *res_val = NULL;
static size_t res_count = 0, res_cap = 0;
// Geometry of the 1x reference the residual was captured against, so a supersampled
// replay can decode each offset back to (x,y) and re-apply it as a scale x scale block.
static int res_ref_pitch = 0;

// Per-player display-rate offset for the hull, shadow, and charge effect.
static bool ship_override_active = false;
static float ship_override_dx[2] = { 0, 0 }, ship_override_dy[2] = { 0, 0 };

// The ship's authoritative per-tick velocity: lets the replay separate a ship-
// attached shot's own motion (orbit) from its ship-tracking component (see below).
static int ship_tick_vel_x[2] = { 0, 0 }, ship_tick_vel_y[2] = { 0, 0 };

void rl_set_ship_override(int player, float dx, float dy)
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

// Current render-rate x offset applied to a ship this frame (0 if inactive); overlays
// that track the smooth ship (Soul of Zinglon pillar) add it to their tick position.
float rl_get_ship_override_dx(int player)
{
	if (player < 0 || player > 1 || !ship_override_active)
		return 0.0f;
	return ship_override_dx[player];
}

// Y counterpart. Needed by the docked Dragonwing, which rides player 1 rigidly and so has to
// be drawn with player 1's sub-tick offset on both axes rather than its own.
float rl_get_ship_override_dy(int player)
{
	if (player < 0 || player > 1 || !ship_override_active)
		return 0.0f;
	return ship_override_dy[player];
}

void rl_set_ship_vel(int player, int vx, int vy)
{
	if (player < 0 || player > 1)
		return;
	ship_tick_vel_x[player] = vx;
	ship_tick_vel_y[player] = vy;
}

// Ids drawn extrapolated (forward, at the render rate) instead of interpolated (a tick behind),
// so they share the render-rate ship's clock.
static bool rl_id_extrapolates(int id)
{
	// Extrapolate shots from the velocity recorded around their blit.
	return id >= RL_ID_PSHOT_BASE && id < RL_ID_EXPL_BASE;  // player + enemy shots
}

// Smoothie replay splits filtered backgrounds, their tail, and the foreground.
typedef enum
{
	RL_PHASE_ALL = 0,  // backgrounds + filters + entities + grade (normal levels)
	RL_PHASE_BG,       // backgrounds + smoothie filters only (the persistent plasma)
	RL_PHASE_FG,       // entities + full-screen grade + residual only (onto a plasma copy)
	RL_PHASE_BG_HEAD,  // BG, stopping at the last filter (the rest is not filter input)
	RL_PHASE_BG_TAIL,  // backgrounds recorded after the last filter, at foreground scale
}
rl_phase;

static void rl_replay_common(SDL_Surface *dst, float inv, float alpha, bool apply_residual,
                             bool use_override, bool feedback, rl_phase phase, int scale,
                             int bg_scale, float bg_inv, bool split_bg)
{
	const bool was_recording = render_list_recording;
	render_list_recording = false;  // re-issued blits must not record themselves

	const bool bg_phase = (phase == RL_PHASE_BG || phase == RL_PHASE_BG_HEAD);
	const bool fg_phase = (phase == RL_PHASE_FG);
	const bool tail_phase = (phase == RL_PHASE_BG_TAIL);

	// Where the two split passes divide the list. The unsplit phases keep the whole background,
	// so their boundary sits past the end.
	const bool split = (phase == RL_PHASE_BG_HEAD || phase == RL_PHASE_BG_TAIL);
	const size_t filter_end = split ? bg_filter_end[cur_buf] : counts[cur_buf];

	// A is the playfield; B is the feedback scratch surface used by background phases.
	SDL_Surface *const A = dst;
	SDL_Surface *const B = (fg_phase || tail_phase) ? NULL : rl_get_scratch_b(scale);

	// The leaf blitters step rows using the global VGAScreen's pitch; point it
	// at dst so they write coherently (all 8-bit surfaces share a pitch anyway).
	SDL_Surface *const saved = VGAScreen;
	VGAScreen = A;

	// B is rebuilt each frame. Foreground and visible tail phases draw straight onto A, so they
	// skip B.
	if (B != NULL)
		JE_clr256(B);
	if (!feedback && phase == RL_PHASE_ALL)
		JE_clr256(A);

	RenderCmd *const cur = bufs[cur_buf];
	const size_t n = counts[cur_buf];
	for (size_t i = 0; i < n; ++i)
	{
		const RenderCmd *const c = &cur[i];

		const bool is_filter = rl_cmd_is_filter(c->kind);
		const bool is_bg = (c->kind == RC_BG_ROW || c->kind == RC_BG_ROW_BLEND || c->kind == RC_STAR);
		const bool post_filter_bg = is_bg && i >= filter_end;
		// Only main-surface commands form the visible tail. Scratch rows after the last filter have
		// no consumer, so the split path can omit them altogether.
		const bool bg_tail = post_filter_bg && !c->surface;
		if (bg_phase && (!(is_bg || is_filter) || post_filter_bg))
			continue;  // entities, the grade, and the tail backgrounds belong to later passes
		if (fg_phase && (is_filter || (is_bg && !bg_tail)))
			continue;  // every background is already baked in or replayed by the tail pass
		if (tail_phase && !bg_tail)
			continue;  // this pass draws only the unfiltered background tail

		// In the FG pass, entities draw straight onto the display buffer (the plasma
		// copy); the B/A ping-pong source only matters while evolving the plasma.
		SDL_Surface *const src = (c->surface && B != NULL) ? B : A;

		if (is_filter)
		{
			if (scale == 1)
			{
				switch (c->kind)
				{
				case RC_ICED_BLUR:    iced_blur_filter(A, src); break;
				case RC_LAVA_FILTER:  lava_filter(A, src);      break;
				case RC_BLUR:         blur_filter(A, src);      break;
				default:              water_filter(A, src);     break;  // RC_WATER_FILTER
				}
			}
			else
			{
				switch (c->kind)
				{
				case RC_ICED_BLUR:    iced_blur_filter_scaled(A, src, scale); break;
				case RC_LAVA_FILTER:  lava_filter_scaled(A, src, scale);      break;
				case RC_BLUR:         blur_filter_scaled(A, src, scale);      break;
				default:              water_filter_scaled(A, src, scale);     break;
				}
			}
			continue;
		}

		if (c->kind == RC_FILTER_SCREEN)
		{
			// Apply the interpolated full-screen grade after compositing entities.
			int bright = c->filt_bright;
			if (inv != 0.0f && c->filt_dbright != 0)
				bright -= rl_iround(c->filt_dbright * inv);
			if (scale == 1)
				JE_filterScreenApply(A, (JE_shortint)c->filt_col, (JE_shortint)bright);
			else
				filter_screen_apply_scaled(A, (JE_shortint)c->filt_col, (JE_shortint)bright, scale);
			continue;
		}

		if (c->kind == RC_STAR)
		{
			// Interpolate the fixed-x star row. Wraps set star_dy to 0 and snap to the top;
			// supersampling places slow drift on the finer pixel grid.
			const float sy = c->star_y - c->star_dy * inv;
			if (scale == 1)
				draw_starfield_star(src, c->star_x, (int)(sy + 0.5f), c->star_color);
			else
				draw_starfield_star_scaled(src, c->star_x * scale, (int)(sy * scale + 0.5f), c->star_color, scale);
			continue;
		}

		if (c->kind == RC_SUPERPIXEL)
		{
			// Explosion spark at its interpolated position (constant velocity, so the
			// recorded per-tick delta is self-contained; no cross-frame matching).
			const int sx = c->x * scale - rl_iround(c->sp_dx * inv * scale);
			const int sy = c->y * scale - rl_iround(c->sp_dy * inv * scale);
			if (scale == 1)
				rl_draw_superpixel(src, sx, sy, c->sp_z, c->sp_color, c->sp_bright);
			else
				rl_draw_superpixel_scaled(src, sx, sy, c->sp_z, c->sp_color, c->sp_bright, scale);
			continue;
		}

		// Keep background phase correction split into integer and fractional pieces.
		int x = c->x * scale, y = c->y * scale;
		const bool is_ship_id = c->id >= RL_ID_SHIP_BASE && c->id < RL_ID_SIDEKICK_BASE;
		if (use_override && ship_override_active && is_ship_id)
		{
			// Hull, shadow, charge, and trim use the render-rate ship offset.
			// Sidekicks interpolate independently.
			int p = (c->id - RL_ID_SHIP_BASE - 1) % 2;
			if (p < 0) p = 0; else if (p > 1) p = 1;
			x += rl_iround(ship_override_dx[p] * scale);
			y += rl_iround(ship_override_dy[p] * scale);
		}
		else
		{
			// Ship-bound axes follow display-rate movement; shots extrapolate, others interpolate.
			const bool ovr = use_override && ship_override_active;
			const int sp = (c->ship_attach >> 2) & 1;  // player index
			const bool extrap = rl_id_extrapolates(c->id);

			// Display replay uses fractional parallax; exact replay keeps recorded integer offsets.
			const bool bg_row = (c->kind == RC_BG_ROW || c->kind == RC_BG_ROW_BLEND)
			    && c->id >= RL_ID_BG_BASE + 1 && c->id <= RL_ID_BG_BASE + 3;

			if (bg_row)
			{
				const int L = c->id - RL_ID_BG_BASE;
				if (use_override)
					x = c->x * scale + rl_iround((bg_layer_frac[L] - bg_layer_dx[L] * inv) * scale);
				else if (c->dx && inv != 0.0f)
					x -= rl_iround(c->dx * inv * scale);  // exact / smoothie: classic whole-pixel
			}
			else if ((c->ship_attach & 1) && ovr)
			{
				x += rl_iround(ship_override_dx[sp] * scale);  // X tracks the render-rate ship
				// Remove ship velocity so an attached shot can interpolate its own motion.
				const int own = c->dx - ship_tick_vel_x[sp];
				if (c->sub_x != 0.0f || c->sub_dx != 0.0f)
					x += rl_iround(rl_sub_disp(own, c->sub_x, c->sub_dx, inv) * scale);
				else if (inv != 0.0f && own && own <= 40 && own >= -40)
					x -= rl_iround(own * inv * scale);
			}
			else if (extrap)
			{
				// Include acceleration so decelerating shots meet the next tick position.
				const int vext = c->dx + c->acc_x;
				if (vext)
					x += rl_iround(vext * alpha * scale);
			}
			else if (use_override && c->par_layer >= 1 && c->par_layer <= 3)
			{
				const int L = c->par_layer;
				const bool tail_layer = split_bg && (bg_tail_layers[cur_buf] & (1u << L));
				const int layer_scale = tail_layer ? scale : bg_scale;
				const float layer_inv = tail_layer ? inv : bg_inv;
				x = c->x * scale +
				    rl_bound_x_offset(c, L, inv, scale, layer_scale, layer_inv);
			}
			else if (use_override && (c->sub_x != 0.0f || c->sub_dx != 0.0f))
			{
				// Unrounded placement with no ship anchor (the shop preview's stationary
				// ship): dx is already the entity's whole-pixel motion on its own.
				x += rl_iround(rl_sub_disp(c->dx, c->sub_x, c->sub_dx, inv) * scale);
			}
			else if (c->dx && inv != 0.0f)
			{
				x -= rl_iround(c->dx * inv * scale);
			}

			if ((c->ship_attach & 2) && ovr)
			{
				y += rl_iround(ship_override_dy[sp] * scale);  // Y tracks the render-rate ship
				const int own = c->dy - ship_tick_vel_y[sp];  // own (orbit) motion; see X
				if (c->sub_y != 0.0f || c->sub_dy != 0.0f)
					y += rl_iround(rl_sub_disp(own, c->sub_y, c->sub_dy, inv) * scale);
				else if (inv != 0.0f && own && own <= 40 && own >= -40)
					y -= rl_iround(own * inv * scale);
			}
			else if (extrap)
			{
				const int vext = c->dy + c->acc_y;  // velocity + acceleration; see X
				if (vext)
					y += rl_iround(vext * alpha * scale);
			}
			else if (bg_row && bg_smooth_y_active && use_override)
			{
				// Smooth vertical scroll at its fractional rate.
				// Layer 3 is recorded after its base step, so remove only modifier motion.
				const int L = c->id - RL_ID_BG_BASE;
				const int phase_base = (L == 3) ? -endlessScrollExtraPx3 : 0;
				y = (c->y + phase_base) * scale +
				    rl_layer_y_offset(L, false, inv, scale, 0);
			}
			else if (use_override && c->par_ylayer != 0)
			{
				const int L = c->par_ylayer;
				const bool tail_layer = split_bg && (bg_tail_layers[cur_buf] & (1u << L));
				const int layer_scale = tail_layer ? scale : bg_scale;
				const float layer_inv = tail_layer ? inv : bg_inv;
				y = (c->y + c->par_ybase) * scale +
				    rl_bound_y_offset(L, inv, scale, c->par_yown100,
				                      layer_scale, layer_inv);
			}
			else if (use_override && (c->sub_y != 0.0f || c->sub_dy != 0.0f))
			{
				y += rl_iround(rl_sub_disp(c->dy, c->sub_y, c->sub_dy, inv) * scale);  // see X
			}
			else if (c->dy && inv != 0.0f)
			{
				y -= rl_iround(c->dy * inv * scale);
			}
		}
		if (scale == 1)
			rl_draw_cmd(src, c, x, y);  // backgrounds -> B, entities/filters-out -> A
		else
			rl_draw_cmd_scaled(src, c, x, y, scale);
	}

	VGAScreen = saved;
	render_list_recording = was_recording;

	if (apply_residual)
	{
		if (scale == 1)
		{
			Uint8 *const p = (Uint8 *)A->pixels;
			for (size_t i = 0; i < res_count; ++i)
				p[res_off[i]] = res_val[i];
		}
		else if (res_ref_pitch > 0)
		{
			// Residuals use the 1x reference, so reapply each pixel as a scale-square block.
			for (size_t i = 0; i < res_count; ++i)
			{
				const int rx = res_off[i] % res_ref_pitch;
				const int ry = res_off[i] / res_ref_pitch;
				if (rx >= vga_width || ry >= vga_height)
					continue;  // offset landed in the 1x pitch padding
				const Uint8 v = res_val[i];
				Uint8 *row = (Uint8 *)A->pixels + (ry * scale) * A->pitch + rx * scale;
				for (int yy = 0; yy < scale; ++yy)
				{
					memset(row, v, scale);
					row += A->pitch;
				}
			}
		}
	}
}

void rl_replay(SDL_Surface *dst)
{
	rl_replay_common(dst, 0.0f, 0.0f, false, false, false, RL_PHASE_ALL,
	                 1, 1, 0.0f, false);  // exact positions (inv=0, alpha=0)
}

void rl_replay_interp(SDL_Surface *dst, float alpha, bool feedback, int scale)
{
	if (alpha < 0.0f)
		alpha = 0.0f;
	else if (alpha > 1.0f)
		alpha = 1.0f;

	// Non-feedback levels replay once, then add residual overlays.
	rl_replay_common(dst, 1.0f - alpha, alpha, true, true, feedback, RL_PHASE_ALL,
	                 scale, scale, 1.0f - alpha, false);
}

// Feedback pass 1 stops at the final filter; a split tail may use a higher scale.
void rl_replay_bg(SDL_Surface *dst, float alpha, int scale, bool split)
{
	if (alpha < 0.0f)
		alpha = 0.0f;
	else if (alpha > 1.0f)
		alpha = 1.0f;
	rl_replay_common(dst, 1.0f - alpha, alpha, false, true, true,
	                 split ? RL_PHASE_BG_HEAD : RL_PHASE_BG,
	                 scale, scale, 1.0f - alpha, split);
}

// Replay the post-filter background tail at the foreground factor before any entities. Keeping it
// separate makes reduced- and full-resolution composition obey the same backgrounds-first order.
void rl_replay_bg_tail(SDL_Surface *dst, float alpha, int scale)
{
	if (alpha < 0.0f)
		alpha = 0.0f;
	else if (alpha > 1.0f)
		alpha = 1.0f;
	rl_replay_common(dst, 1.0f - alpha, alpha, false, true, false, RL_PHASE_BG_TAIL,
	                 scale, scale, 1.0f - alpha, true);
}

// Draw foreground over the completed feedback surface, then grade and add overlays.
void rl_replay_fg(SDL_Surface *dst, float alpha, int scale,
                  int bg_scale, float bg_alpha, bool split)
{
	assert(scale >= 1 && bg_scale >= 1 && bg_scale <= scale && scale % bg_scale == 0);
	if (alpha < 0.0f)
		alpha = 0.0f;
	else if (alpha > 1.0f)
		alpha = 1.0f;
	if (bg_alpha < 0.0f)
		bg_alpha = 0.0f;
	else if (bg_alpha > 1.0f)
		bg_alpha = 1.0f;
	rl_replay_common(dst, 1.0f - alpha, alpha, true, true, false, RL_PHASE_FG,
	                 scale, bg_scale, 1.0f - bg_alpha, split);
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

void rl_deinit(void)
{
	for (int i = 0; i < 2; ++i)
	{
		free(bufs[i]);
		bufs[i] = NULL;
		counts[i] = 0;
		caps[i] = 0;
		bg_filter_end[i] = 0;
		bg_tail_layers[i] = 0;
	}
	free(match_link);
	match_link = NULL;
	match_link_cap = 0;
	free(res_off);
	res_off = NULL;
	free(res_val);
	res_val = NULL;
	res_count = 0;
	res_cap = 0;
	for (unsigned i = 0; i < COUNTOF(rl_scratch_b); ++i)
	{
		if (rl_scratch_b[i] != NULL)
		{
			SDL_FreeSurface(rl_scratch_b[i]);
			rl_scratch_b[i] = NULL;
		}
	}
	render_list_recording = false;
}

/* Capture every marked overlay pixel. Diff-only capture leaves holes when a matching
 * background pixel moves during interpolation; duplicate offsets are harmless. */
static void rl_capture_overlay_rects(SDL_Surface *reference)
{
	const Uint8 *const ref = (const Uint8 *)reference->pixels;

	for (int r = 0; r < overlay_rect_count; ++r)
	{
		const int x0 = MAX(overlay_rect[r].x, 0);
		const int y0 = MAX(overlay_rect[r].y, 0);
		const int x1 = MIN(overlay_rect[r].x + overlay_rect[r].w, reference->w);
		const int y1 = MIN(overlay_rect[r].y + overlay_rect[r].h, reference->h);

		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x)
			{
				const int off = y * reference->pitch + x;
				if (!rl_res_push(off, ref[off]))
					return;
			}
	}
}

void rl_capture_residual(SDL_Surface *reference, SDL_Surface *scratch)
{
	JE_clr256(scratch);
	rl_replay(scratch);  // blit-only reproduction at recorded positions

	res_count = 0;
	res_ref_pitch = reference->pitch;

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

	rl_capture_overlay_rects(reference);
}

// Compare pre-overlay and final frames to isolate residual overlays.
void rl_capture_residual_delta(SDL_Surface *before, SDL_Surface *after)
{
	res_count = 0;
	res_ref_pitch = after->pitch;

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

	rl_capture_overlay_rects(after);
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
