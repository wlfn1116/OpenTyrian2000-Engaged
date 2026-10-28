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
#include "game_loop.h"

#include "audio.h"
#include "background.h"
#include "config.h"
#include "crashlog.h"
#include "debug_flags.h"
#include "debug_levels.h"
#include "debug_menu.h"
#include "demo.h"
#include "draw.h"
#include "endless.h"
#include "enemy_death.h"
#include "episodes.h"
#include "explosions.h"
#include "file.h"
#include "font.h"
#include "health_bars.h"
#include "high_scores.h"
#include "hud.h"
#include "ingame_menu.h"
#include "joystick.h"
#include "keyboard.h"
#include "lds_play.h"
#include "level_end.h"
#include "level_events.h"
#include "level_load.h"
#include "mouse.h"
#include "mtrand.h"
#include "musmast.h"
#include "net_start.h"
#include "network.h"
#include "net_style.h"
#include "opentyr.h"
#include "palette.h"
#include "params.h"
#include "picload.h"
#include "player.h"
#include "player_ship.h"
#include "qa.h"
#include "render_list.h"
#include "rollback.h"
#include "net_rollback.h"
#include "savegame.h"
#include "sfx.h"
#include "shots.h"
#include "sim_math.h"
#include "sparks.h"
#include "specials.h"
#include "sprite.h"
#include "timing.h"
#include "video.h"
#include "vt_ship.h"

#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Render-list verification harness: replay the captured list each clean frame
// and report any pixels that differ from the real frame. Gated off; kept for debugging.
#define RL_SELFTEST 0

// `outline` draws the frame as a flat silhouette in a dark shade of the colour the art would have
// used, for the "?" pickup's outline pass. Only that pickup passes it, and it always has a bank.
inline static void blit_enemy(SDL_Surface *surface, unsigned int i, signed int x_offset, signed int y_offset, signed int sprite_offset, bool outline);

static uint JE_makeEnemy(struct JE_SingleEnemyType *enemy, Uint16 eDatI, Sint16 uniqueShapeTableI);
// Arcade weapon-ball randomizer: re-scan enemyDat for the front/rear/sidekick/special ball
// pools. Must run after any load that rewrites enemyDat (JE_loadItemDat).
static void JE_buildArcadeBallPools(void);

/* Derive flip, spotlight, and inverted controls from level state and synchronized Endless
 * modifiers. The result affects simulation and must also run online. */
void JE_deriveStarShowSpecial(void)
{
	starShowVGASpecialCode = smoothies[9-1] + (smoothies[6-1] << 1);

	// Endless replaces a level's scripted spotlight with the zone's seeded roll. Keep flip codes
	// intact, and leave campaign spotlights authored by the level.
	if (endlessMode)
	{
		if (starShowVGASpecialCode == 2)
			starShowVGASpecialCode = 0;
		if (starShowVGASpecialCode == 0 && endlessLightConeActive())
			starShowVGASpecialCode = 2;
	}

	// Topsy Turvy uses the boss screen flip, including its matching control inversion.
	if (endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_TOPSY))
	{
		smoothies[9 - 1] = true;
		starShowVGASpecialCode = 1;
	}
}

// Apply one tier-limited course correction while preserving shot speed. The
// movement loop owns the delay and remaining-pass count.
#define ENDLESS_SEEKER_DELAY_TICKS 17     // ~0.5s at the 35Hz sim between corrections
static void endlessSeekerCorrect(EnemyShotType *s)
{
	float turnCos = 0.0f, turnSin = 0.0f;
	endlessSeekerTurn(&turnCos, &turnSin);
	const float vx = (float)s->sxm, vy = (float)s->sym;
	const float speed = sqrtf(vx * vx + vy * vy);
	if (speed < 0.5f)
		return;                          // a near-stationary shot has no heading to bend
	const Player *const target = &player[endlessDangerTargetPlayer(s->sx, s->sy)];
	const float dx = (float)((int)target->x - s->sx);
	const float dy = (float)((int)target->y - s->sy);
	const float dmag = sqrtf(dx * dx + dy * dy);
	if (dmag < 0.5f)
		return;
	const float ux = dx / dmag, uy = dy / dmag;      // unit vector toward the player
	const float cvx = vx / speed, cvy = vy / speed;  // current unit heading
	const float dot = cvx * ux + cvy * uy;           // cos(angle between heading and target)
	if (dot >= turnCos)
	{
		s->sxm = (JE_integer)roundf(ux * speed);     // within one turn: snap straight at the player, keep the speed
		s->sym = (JE_integer)roundf(uy * speed);
	}
	else
	{
		float sn = turnSin;
		// The cross-product sign selects the shorter rotation.
		if (cvx * uy - cvy * ux < 0.0f)
			sn = -sn;
		s->sxm = (JE_integer)roundf(vx * turnCos - vy * sn);
		s->sym = (JE_integer)roundf(vx * sn + vy * turnCos);
	}
	if (s->sxm == 0 && s->sym == 0)                  // rounding zeroed a tiny vector; keep it moving
	{
		s->sxm = (JE_integer)roundf(vx);
		s->sym = (JE_integer)roundf(vy);
	}
}

char tempStr[31];
JE_word armorShipDelay;
JE_boolean editShip1, editShip2;
JE_byte     doIced;
JE_boolean  infiniteShot;
static JE_boolean dispenserBasesActive = false;  /* this level wakes the dormant dispenser bases (set at level start) */
static const JE_byte randomEnemyLaunchSounds[3] /* [1..3] */ = {13,6,26};

// Fixed-timestep accumulator: each display frame presents at alpha = accumulator/period, the
// simulation ticks once per full period. Uses the performance counter, not SDL_GetTicks.
static float sim_accumulator = 0.0f;
static Uint64 sim_last_counter = 0;
static Uint64 sim_perf_freq = 0;
static bool sim_timing_init = false;

float debug_interp_alpha = 0.0f;  // last presented interpolation fraction (perf overlay)

// Feedback surfaces keep persistent plasma separate from the current composite.
static SDL_Surface *render_gs = NULL;
static SDL_Surface *smoothie_frame = NULL;
static SDL_Surface *smoothie_present_frame = NULL;  // separate foreground target for cached plasma
static SDL_Surface *feedback_work = NULL;

// Feedback filters rebuild content a resize discards from black.
static SDL_Surface *get_render_gs(int scale)
{
	return video_ensure_scaled_surface(&render_gs, scale);
}

static SDL_Surface *get_smoothie_frame(int scale)
{
	return video_ensure_scaled_surface(&smoothie_frame, scale);
}

static SDL_Surface *get_smoothie_present_frame(int scale)
{
	return video_ensure_scaled_surface(&smoothie_present_frame, scale);
}

static SDL_Surface *get_feedback_work(void)
{
	return video_ensure_scaled_surface(&feedback_work, 1);
}

// Supersampled present path: the interpolated playfield renders NxN into pf_hi, composites into
// vga_hi with the 1x HUD block-expanded on top, then presents via present_hi().
static SDL_Surface *pf_hi = NULL;   // NxN playfield replay target (normal levels)
static SDL_Surface *vga_hi = NULL;  // NxN final frame (playfield composite + HUD)
static SDL_Surface *vga_hi_ref = NULL;
static int vga_hi_ref_scale = 0;

void game_loop_deinit(void)
{
	SDL_FreeSurface(render_gs);
	render_gs = NULL;
	SDL_FreeSurface(smoothie_frame);
	smoothie_frame = NULL;
	SDL_FreeSurface(smoothie_present_frame);
	smoothie_present_frame = NULL;
	SDL_FreeSurface(feedback_work);
	feedback_work = NULL;
	SDL_FreeSurface(vga_hi_ref);
	vga_hi_ref = NULL;
	vga_hi_ref_scale = 0;
	SDL_FreeSurface(pf_hi);
	pf_hi = NULL;
	SDL_FreeSurface(vga_hi);
	vga_hi = NULL;
}

static bool ensure_hi_buffers(int scale)
{
	return video_ensure_scaled_surface(&pf_hi, scale) != NULL
	    && video_ensure_scaled_surface(&vga_hi, scale) != NULL;
}

static void note_hi_present(int scale)
{
	vga_hi_ref_scale = 0;
	if (video_ensure_scaled_surface(&vga_hi_ref, 1) == NULL)
		return;
	memcpy(vga_hi_ref->pixels, VGAScreenSeg->pixels, (size_t)VGAScreenSeg->h * VGAScreenSeg->pitch);
	vga_hi_ref_scale = scale;
}

bool present_overlay_on_last_hi_frame(void)
{
	const int scale = vga_hi_ref_scale;
	if (scale <= 1 || scale != effective_supersample() || vga_hi == NULL || pf_hi == NULL ||
	    vga_hi->w != vga_width * scale || pf_hi->w != vga_width * scale ||
	    vga_hi_ref->pitch != VGAScreenSeg->pitch)
		return false;

	memcpy(pf_hi->pixels, vga_hi->pixels, (size_t)vga_hi->h * vga_hi->pitch);
	for (int y = 0; y < vga_height; ++y)
	{
		const Uint8 *const live = (const Uint8 *)VGAScreenSeg->pixels + y * VGAScreenSeg->pitch;
		const Uint8 *const ref = (const Uint8 *)vga_hi_ref->pixels + y * vga_hi_ref->pitch;
		for (int x = 0; x < vga_width; ++x)
		{
			if (live[x] == ref[x])
				continue;
			Uint8 *d = (Uint8 *)pf_hi->pixels + (y * scale) * pf_hi->pitch + x * scale;
			for (int k = 0; k < scale; ++k, d += pf_hi->pitch)
				memset(d, live[x], (size_t)scale);
		}
	}
	if (mouse_cursor_wants_hi())
		JE_drawMouseToHiFrame(pf_hi, scale, video_get_menu_x_offset());
	present_hi(pf_hi);
	return true;
}

static int round_signed(float v)
{
	return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

/* Put the spotlight on the local ship's displayed position. */
static void spotlight_anchor(bool interpolate, float *out_x, float *out_y)
{
	const int p = (isNetworkGame && thisPlayerNum >= 2) ? 1 : 0;
	if (interpolate)
	{
		vt_ship_display_position(p, out_x, out_y);
	}
	else
	{
		*out_x = (float)player[p].x;
		*out_y = (float)player[p].y;
	}
}

// Composite the freshly drawn playfield, including flip and lighting effects.
static void composite_playfield(SDL_Surface *playfield, bool interpolate)
{
	JE_byte *src;
	Uint8 *s = VGAScreenSeg->pixels;

	int y, lightx, lighty;

	src = playfield->pixels;
	src += PLAYFIELD_LEFT;  // crop off the off-screen entry margin; see video.h

	if (starShowVGASpecialCode == 1)
	{
		src += playfield->pitch * 183;
		for (y = 0; y < 184; y++)
		{
			memmove(s, src, PLAYFIELD_WIDTH);
			s += VGAScreenSeg->pitch;
			src -= playfield->pitch;
		}
	}
	else if (starShowVGASpecialCode == 2 && processorType >= 2)
	{
		float litx, lity;
		spotlight_anchor(interpolate, &litx, &lity);
		lighty = 172 - round_signed(lity);
		lightx = (PLAYFIELD_WIDTH - PLAYFIELD_X_SHIFT + 5) - round_signed(litx);

		for (y = 184; y; y--)
		{
			spotlight_filter_row(s, src, PLAYFIELD_WIDTH, y, lightx, lighty, 5, 1);
			s += VGAScreenSeg->pitch;
			src += playfield->pitch;
		}
	}
	else
	{
		for (y = 0; y < 184; y++)
		{
			memmove(s, src, PLAYFIELD_WIDTH);
			s += VGAScreenSeg->pitch;
			src += playfield->pitch;
		}
	}
}

// composite_playfield at NxN: same three modes (normal copy, vertical flip, spotlight) on the
// supersampled playfield, writing into vga_hi's playfield region.
typedef struct
{
	const SDL_Surface *playfield;
	const SDL_Surface *hud;
	SDL_Surface *out;
	int scale;
	int lightx, lighty;
} HiCompose;

static void composite_playfield_hi(const HiCompose *job, int first, int last)
{
	const int scale = job->scale;
	const int width = PLAYFIELD_WIDTH * scale;
	const int rows = 184 * scale;
	const int src_pitch = job->playfield->pitch;
	const JE_byte *const src = (const JE_byte *)job->playfield->pixels + PLAYFIELD_LEFT * scale;
	Uint8 *s = (Uint8 *)job->out->pixels + (size_t)first * job->out->pitch;

	for (int y = first; y < last; ++y, s += job->out->pitch)
	{
		if (starShowVGASpecialCode == 1)
			memmove(s, src + (size_t)src_pitch * (rows - 1 - y), width);
		else if (starShowVGASpecialCode == 2 && processorType >= 2)
			spotlight_filter_row(s, src + (size_t)src_pitch * y, width, rows - y, job->lightx,
			                     job->lighty, 5 * scale, scale);
		else
			memmove(s, src + (size_t)src_pitch * y, width);
	}
}

static void expand_row_to_hi(Uint8 *d, const Uint8 *sp, int count, int scale)
{
	switch (scale)
	{
	case 1:
		memcpy(d, sp, (size_t)count);
		break;
	case 2:
		for (int x = 0; x < count; ++x)
		{
			const Uint8 v = *sp++;
			*d++ = v; *d++ = v;
		}
		break;
	case 3:
		for (int x = 0; x < count; ++x)
		{
			const Uint8 v = *sp++;
			*d++ = v; *d++ = v; *d++ = v;
		}
		break;
	case 4:
		for (int x = 0; x < count; ++x)
		{
			const Uint8 v = *sp++;
			*d++ = v; *d++ = v; *d++ = v; *d++ = v;
		}
		break;
	case 5:
		for (int x = 0; x < count; ++x)
		{
			const Uint8 v = *sp++;
			*d++ = v; *d++ = v; *d++ = v; *d++ = v; *d++ = v;
		}
		break;
	default:
		for (int x = 0; x < count; ++x)
		{
			const Uint8 v = *sp++;
			for (int k = 0; k < scale; ++k)
				*d++ = v;
		}
		break;
	}
}

// Block-expand the 1x HUD into the supersampled frame.
static void expand_hud_to_hi(const HiCompose *job, int first, int last)
{
	const int scale = job->scale;
	for (int y = first; y < last; ++y)
	{
		const int x_start = (y < 184) ? PLAYFIELD_WIDTH : 0;
		const Uint8 *sp = (const Uint8 *)job->hud->pixels + y * job->hud->pitch + x_start;
		Uint8 *const d0 = (Uint8 *)job->out->pixels + (y * scale) * job->out->pitch
		                  + x_start * scale;

		expand_row_to_hi(d0, sp, vga_width - x_start, scale);

		const int row_bytes = (vga_width - x_start) * scale;
		for (int k = 1; k < scale; ++k)
			memcpy(d0 + k * job->out->pitch, d0, row_bytes);
	}
}

static void compose_hi_rows(void *p, int row_begin, int row_end)
{
	const HiCompose *const job = p;
	composite_playfield_hi(job, row_begin * job->scale, MIN(row_end, 184) * job->scale);
	expand_hud_to_hi(job, row_begin, row_end);
}

static void compose_hi_frame(SDL_Surface *playfield, SDL_Surface *hud, SDL_Surface *out, int scale,
                             bool interpolate)
{
	HiCompose job = { playfield, hud, out, scale, 0, 0 };
	if (starShowVGASpecialCode == 2 && processorType >= 2)
	{
		// Scaled before rounding, so under supersampling the cone glides on the sub-pixel grid
		// with the ship.
		float litx, lity;
		spotlight_anchor(interpolate, &litx, &lity);
		const float edge = (float)(PLAYFIELD_WIDTH - PLAYFIELD_X_SHIFT + 5);
		job.lighty = round_signed((172.0f - lity) * (float)scale);
		job.lightx = round_signed((edge - litx) * (float)scale);
	}
	video_rows(VIDEO_ROWS_COMPOSE, compose_hi_rows, &job, vga_height, out->w * out->h, NULL);
}

typedef struct
{
	SDL_Surface *src, *hi;
	int scale;
} HiExpand;

static void expand_rows_to_hi(void *p, int row_begin, int row_end)
{
	const HiExpand *const job = p;
	const int scale = job->scale;
	for (int y = row_begin; y < row_end; ++y)
	{
		const Uint8 *sp = (const Uint8 *)job->src->pixels + y * job->src->pitch;
		Uint8 *const d0 = (Uint8 *)job->hi->pixels + (y * scale) * job->hi->pitch;

		expand_row_to_hi(d0, sp, job->src->w, scale);

		const int row_bytes = job->src->w * scale;
		for (int k = 1; k < scale; ++k)
			memcpy(d0 + k * job->hi->pitch, d0, row_bytes);
	}
}

static void expand_frame_to_hi_threaded(SDL_Surface *src, SDL_Surface *hi, int scale)
{
	assert(scale >= 1 && hi->w >= src->w * scale && hi->h >= src->h * scale);

	HiExpand job = { src, hi, scale };
	video_rows(VIDEO_ROWS_SMOOTHIE, expand_rows_to_hi, &job, src->h, hi->w * hi->h, NULL);
}

// Expand a lower-resolution feedback or preview surface into the high-resolution frame.
void expand_frame_to_hi(SDL_Surface *src, SDL_Surface *hi, int scale)
{
	assert(scale >= 1 && hi->w >= src->w * scale && hi->h >= src->h * scale);

	HiExpand job = { src, hi, scale };
	expand_rows_to_hi(&job, 0, src->h);
}

// Soul of Zinglon light pillar, drawn at display rate from the per-tick request (zinglonPillar*).
// cx is in HI units; temp is the 1x half-width.
static void draw_zinglon_pillar(SDL_Surface *surface, int cx, float temp, int scale)
{
	const int bottom = 184 * scale;
	const int half = round_signed(temp * scale);
	int x0 = cx - half;
	int x1 = cx + half + (scale - 1);
	if (x0 < 0) x0 = 0;
	JE_barBright(surface, x0, 0, x1, bottom);
	x0 = cx - half - 2 * scale;
	x1 = cx + half + 2 * scale + (scale - 1);
	if (x0 < 0) x0 = 0;
	JE_barBright(surface, x0, 0, x1, bottom);
}

static float zinglon_width_prev[2], zinglon_width_cur[2];

static void zinglon_pillar_tick(void)
{
	for (uint p = 0; p < COUNTOF(zinglonPillarActive); ++p)
	{
		zinglon_width_prev[p] = zinglon_width_cur[p];
		zinglon_width_cur[p] = zinglonPillarActive[p] ? (float)zinglonPillarTemp[p] : 0.0f;
	}
}

/* Both ships off the same per-tick request. */
static void draw_active_zinglon_pillars(SDL_Surface *surface, int scale, bool interpolate,
                                        float alpha)
{
	for (uint p = 0; p < COUNTOF(zinglonPillarActive); ++p)
	{
		if (!zinglonPillarActive[p])
			continue;
		const float offset = interpolate ? rl_get_ship_override_dx(p) : 0.0f;
		const int cx = round_signed(((float)zinglonPillarCX[p] + offset) * scale);
		const float grow = zinglon_width_cur[p] - zinglon_width_prev[p];
		const float width = interpolate ? zinglon_width_prev[p] + grow * alpha
		                                : (float)zinglonPillarTemp[p];
		draw_zinglon_pillar(surface, cx, width, scale);
	}
}

// Last PRESENTED twoPlayerLinked value, for the fuse/unfuse sound cue edge detector
// in the level loop.  Presentation state: unregistered, reset at level start.
static bool link_cue_state = false;

// Arm wreck-animation skipping only after all held input is released.
static bool deathSkipArmed = false;

// Ticks the endless death menu has let GAME OVER stand before opening itself. Reset at level
// start alongside deathSkipArmed.
static uint deathGameOverTicks = 0;

// Bound the game-over beat when music is absent, short, or does not finish.
#define DEATH_GAMEOVER_TICKS_MIN 70   // ~2s
#define DEATH_GAMEOVER_TICKS_MAX 700  // ~20s

// Generator power bar render state: a HUD overlay on VGAScreenSeg, redrawn every presented
// frame at an interpolated level with a sub-pixel anti-aliased top edge.
static bool power_gauge_active = false;
static int power_render_prev = 0, power_render_cur = 0;  // `power` (0..900) at the prev/cur tick
static int salvo_render_prev = 0, salvo_render_cur = 0;  // ...and the salvo green share (0..100)

// Opening Salvo share for the local ship's gauge, from 0 to 100.
static int local_salvo_gauge_percent(void)
{
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(gameplay_local_player_index());
	const int pct = endlessOpeningSalvoGaugePercent();
	endlessSetFxPlayer(fxSaved);
	return pct;
}

// Geometry shared by the generator and the arcade lives bar in the same HUD slot.
enum { PG_Y_BOTTOM = 104, PG_BAR_MAX = 93, PG_BASE = 113, PG_POWER_MAX = 900, PG_SEG_SHADE_MAX = 13 };

// A gauge rect given in 1x HUD coordinates, painted at the present pass's supersample factor.
static void gauge_bar_fill(SDL_Surface *dst, int scale, int x1, int y1, int x2, int y2, Uint8 color)
{
	fill_rectangle_xy(dst, x1 * scale, y1 * scale, (x2 + 1) * scale - 1, (y2 + 1) * scale - 1, color);
}

// The one row the bar's top edge only partly covers, blended from the bank floor up to the shade
// the bar ends on. Below 0.04 the row would only carry the floor colour, which reads as a stray dot.
static void gauge_bar_edge_row(SDL_Surface *dst, int x0, int x1, int top, float cover, int shade, int dark)
{
	if (cover <= 0.04f || top < 1)
		return;

	int edgeCol = dark + (int)(cover * (shade - dark) + 0.5f);
	if (edgeCol > shade)
		edgeCol = shade;
	fill_rectangle_xy(dst, x0, top - 1, x1, top - 1, (Uint8)edgeCol);
}

// level is the pixel height with a fractional top edge. salvo_frac tints that share green;
// segments divides counted resources into visible blocks.
static void draw_gauge_bar(SDL_Surface *dst, int scale, float level, float salvo_frac, int segments)
{
	// 9 pixels wide (x1..x2). The classic art drew this gauge 1px narrower than the
	// shield/armor bars; extend it right by one so all three gauges match at 9px.
	const int x1 = HUD_X(269), x2 = HUD_X(277);

	if (level < 0.0f)
		level = 0.0f;
	else if (level > PG_BAR_MAX)
		level = PG_BAR_MAX;

	const int full = (int)level;          // solid whole rows
	const int dir = gaugeGradGenerator;   // GaugeGradientDir

	// Split the top edge into solid sub-rows and one blended remainder.
	const float topf = (float)((PG_Y_BOTTOM + 1) * scale) - level * (float)scale;
	const int top = (int)ceilf(topf);
	const float cover = (float)top - topf;
	const int solidTop = (PG_Y_BOTTOM - full + 1) * scale;  // first row of the last whole row

	// Tint the local power gauge while kill-fire makes the main gun free.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(gameplay_local_player_index());
	int base = PG_BASE;
	if (endlessFxActive() && endlessTurbodriveActive() && !endlessKillFireIsEvil())
		base = ENDLESS_FREE_POWER_GAUGE_BASE;
	endlessSetFxPlayer(fxSaved);

	// Opening Salvo paints from the bottom and takes precedence over the kill-fire tint.
	int salvoRows = (salvo_frac > 0.0f) ? (int)(full * salvo_frac + 0.5f) : 0;
	if (salvoRows > full)
		salvoRows = full;
	const int salvoBase = ENDLESS_SALVO_GAUGE_BASE;
	// The AA row sits one above the solid bar, so it only reads green on a bar that is fully green.
	const int edgeBase = (salvoRows > 0 && salvoRows >= full) ? salvoBase : base;
	const int edgeDark = edgeBase & ~0x0F;  // bank floor: the AA top edge blends up from here

	// Clear the bar slot (its background is black, like the original shrink fill).
	gauge_bar_fill(dst, scale, x1, PG_Y_BOTTOM - PG_BAR_MAX, x2, PG_Y_BOTTOM, 0);

	if (dir == GAUGE_GRAD_LEFT || dir == GAUGE_GRAD_RIGHT)
	{
		// Horizontal gauges use one fixed shade per column.
		for (int j = 0; j <= x2 - x1; j++)
		{
			const int off = (dir == GAUGE_GRAD_RIGHT) ? j : (x2 - x1 - j);
			const int cx0 = (x1 + j) * scale, cx1 = (x1 + j + 1) * scale - 1;
			if (salvoRows >= 1)
				gauge_bar_fill(dst, scale, x1 + j, PG_Y_BOTTOM - salvoRows + 1, x1 + j, PG_Y_BOTTOM, (Uint8)(salvoBase + 2 + off));
			if (full > salvoRows)
				gauge_bar_fill(dst, scale, x1 + j, PG_Y_BOTTOM - full + 1, x1 + j, PG_Y_BOTTOM - salvoRows, (Uint8)(base + 2 + off));
			if (full < PG_BAR_MAX)
			{
				const int shade = edgeBase + 2 + off;
				if (top < solidTop)
					fill_rectangle_xy(dst, cx0, top, cx1, solidTop - 1, (Uint8)shade);
				gauge_bar_edge_row(dst, cx0, cx1, top, cover, shade, edgeDark);
			}
		}

		// Separate filled segments with one blank row without disturbing the bar's top edge.
		for (int i = 1; i < segments; ++i)
		{
			const int h = PG_BAR_MAX * i / segments;
			if (h >= 1 && h < full)
				gauge_bar_fill(dst, scale, x1, PG_Y_BOTTOM - h + 1, x2, PG_Y_BOTTOM - h + 1, 0);
		}
	}
	else if (segments > 1)
	{
		// Give each vertical segment a fixed bank shade and reserve its top row as a separator.
		for (int i = 1; i <= segments; ++i)
		{
			const int lo = PG_BAR_MAX * (i - 1) / segments;  // boundary under this block
			const int hi = PG_BAR_MAX * i / segments;        // boundary at its top

			if (lo + 1 > full)
				break;

			int blockTop = (i < segments) ? hi - 1 : hi;  // topmost block has no gap above it
			if (blockTop > full)
				blockTop = full;
			if (blockTop < lo + 1)
				continue;

			const int step = (dir == GAUGE_GRAD_DOWN) ? (segments - i) : (i - 1);
			const int shade = step * PG_SEG_SHADE_MAX / (segments - 1);
			gauge_bar_fill(dst, scale, x1, PG_Y_BOTTOM - blockTop + 1, x2, PG_Y_BOTTOM - lo, (Uint8)(base + shade));
		}
	}
	else
	{
		// Split vertical shade bands at the Opening Salvo boundary.
		for (int h = 1; h <= full; )
		{
			const int shade = (dir == GAUGE_GRAD_DOWN) ? (full - h) / 7 : h / 7;
			const int rowBase = (h <= salvoRows) ? salvoBase : base;
			int h2 = h;
			while (h2 + 1 <= full &&
			       ((dir == GAUGE_GRAD_DOWN) ? (full - (h2 + 1)) / 7 : (h2 + 1) / 7) == shade &&
			       (((h2 + 1) <= salvoRows) ? salvoBase : base) == rowBase)
				++h2;
			gauge_bar_fill(dst, scale, x1, PG_Y_BOTTOM - h2 + 1, x2, PG_Y_BOTTOM - h + 1, (Uint8)(rowBase + shade));
			h = h2 + 1;
		}

		// Blend one leading row for sub-pixel fill height.
		if (full < PG_BAR_MAX)
		{
			const int barCol = (dir == GAUGE_GRAD_DOWN) ? edgeBase : (edgeBase + full / 7);
			if (top < solidTop)
				fill_rectangle_xy(dst, x1 * scale, top, (x2 + 1) * scale - 1, solidTop - 1, (Uint8)barCol);
			gauge_bar_edge_row(dst, x1 * scale, (x2 + 1) * scale - 1, top, cover, barCol, edgeDark);
		}
	}
}

// Generator power (0..PG_POWER_MAX) -> a smooth, un-segmented bar.
static void draw_power_gauge(SDL_Surface *dst, int scale, float power_value, float salvo_frac)
{
	draw_gauge_bar(dst, scale, power_value * PG_BAR_MAX / (float)PG_POWER_MAX, salvo_frac, 0);
}

// The arcade modes have no generator, so its gauge slot sits empty; hand it to the life counter
// instead: one segment per life, full bar at ARCADE_LIVES_MAX.
static void draw_lives_gauge(int lives)
{
	if (lives < 0)
		lives = 0;
	else if (lives > ARCADE_LIVES_MAX)
		lives = ARCADE_LIVES_MAX;

	draw_gauge_bar(VGAScreenSeg, 1, (float)(PG_BAR_MAX * lives / ARCADE_LIVES_MAX), 0.0f, ARCADE_LIVES_MAX);
}

static struct
{
	Uint64 tick_start;
	Uint64 window_start;
	Uint64 last_present;
	Uint64 tick_sum, draw_sum, present_sum, worst;
	unsigned ticks, frames;
	FrameTimes shown;
} frame_times;

static void frame_times_tick_end(Uint64 now)
{
	const Uint64 spent = now - frame_times.tick_start;
	if (frame_times.tick_start != 0 && spent < SDL_GetPerformanceFrequency())
	{
		frame_times.tick_sum += spent;
		++frame_times.ticks;
	}
}

static void frame_times_present(Uint64 frame_start, Uint64 present_start)
{
	const Uint64 now = SDL_GetPerformanceCounter();
	const Uint64 freq = SDL_GetPerformanceFrequency();
	frame_times.draw_sum += present_start - frame_start;
	frame_times.present_sum += now - present_start;
	++frame_times.frames;

	const Uint64 interval = now - frame_times.last_present;
	if (frame_times.last_present != 0 && interval < freq && interval > frame_times.worst)
		frame_times.worst = interval;
	frame_times.last_present = now;

	if (frame_times.window_start == 0)
		frame_times.window_start = now;
	if (now - frame_times.window_start < freq)
		return;

	const float ms = 1000.0f / (float)freq;
	frame_times.shown.tick_ms = frame_times.ticks != 0
	                          ? (float)frame_times.tick_sum * ms / (float)frame_times.ticks : 0.0f;
	frame_times.shown.draw_ms = (float)frame_times.draw_sum * ms / (float)frame_times.frames;
	frame_times.shown.present_ms = (float)frame_times.present_sum * ms / (float)frame_times.frames;
	frame_times.shown.worst_ms = (float)frame_times.worst * ms;
	frame_times.tick_sum = frame_times.draw_sum = frame_times.present_sum = frame_times.worst = 0;
	frame_times.ticks = frame_times.frames = 0;
	frame_times.window_start = now;
}

void frame_times_get(FrameTimes *times)
{
	*times = frame_times.shown;
}

static void JE_starShowVGA(void)
{
	if (qa_fast_forward)
	{
		quitRequested = false;
		skipStarShowVGA = false;
		return;
	}

	const Uint64 entry = SDL_GetPerformanceCounter();
	frame_times_tick_end(entry);

	zinglon_pillar_tick();
	boss_bar_present_tick();

	if (!playerEndLevel && !skipStarShowVGA)
	{
		// Zinglon pillar at the tick position: baseline for the non-interpolated
		// present paths; the interp loop below redraws it shifted after replay.
		draw_active_zinglon_pillars(game_screen, 1, false, 1.0f);

		composite_playfield(game_screen, false);

		if (smoothScroll != 0)
		{
			// Smoothie levels stage backgrounds and foregrounds; normal levels use game_screen.
			const bool can_interp = frameCountMax > 0 && smoothMotion;

			// Fall back to the 1x path if supersampled buffers cannot be allocated.
			int rss = can_interp ? effective_supersample() : 1;
			const bool use_hi = rss > 1 && ensure_hi_buffers(rss);
			if (!use_hi)
				rss = 1;

			// Sub-pixel FX off filters feedback at 1x and expands only its consumed layers.
			const int pss = (anySmoothies && !smoothie_full_res) ? 1 : rss;
			const bool split_bg = (pss != rss);
			// Vita cannot lower the spatial factor below 1x. Its low-cost mode instead computes the
			// current tick's plasma once and reuses it while foreground-local movement stays smooth.
#ifdef __vita__
			const bool tick_plasma = anySmoothies && !smoothie_full_res && rss == 1;
#else
			const bool tick_plasma = false;
#endif

			SDL_Surface *const plasma_buf  = anySmoothies ? get_smoothie_frame(pss) : NULL;
			SDL_Surface *const interp_buf  = anySmoothies ? (tick_plasma ? get_smoothie_present_frame(rss)
			                                                        : (pss == rss ? plasma_buf : pf_hi))
			                               : (use_hi ? pf_hi : game_screen);
			SDL_Surface *const bg_feedback = anySmoothies ? get_render_gs(1) : NULL;
			SDL_Surface *const plasma_work = (anySmoothies && pss > 1) ? get_feedback_work()
			                                                           : plasma_buf;
			bool tick_plasma_ready = false;

			if (can_interp && interp_buf != NULL
			    && (!anySmoothies
			        || (plasma_buf != NULL && bg_feedback != NULL && plasma_work != NULL)))
			{
				// Carry leftover display time into the next fixed simulation tick.
				const float period = ((pentiumMode && !isNetworkGame) ? 2.5f : (float)frameCountMax)
				                   * get_delay_period();

				if (!sim_timing_init)
				{
					sim_perf_freq = SDL_GetPerformanceFrequency();
					sim_last_counter = SDL_GetPerformanceCounter();
					sim_accumulator = 0.0f;
					sim_timing_init = true;
				}

				const float counter_to_ms = 1000.0f / (float)sim_perf_freq;

				if (tick_plasma)
				{
					rl_feedback_trim(bg_feedback, 0);
					memcpy(plasma_buf->pixels, bg_feedback->pixels,
					       (size_t)bg_feedback->h * bg_feedback->pitch);
					rl_replay_bg_tick(plasma_buf, pss);
					tick_plasma_ready = true;
				}

				for (;;)
				{
					const Uint64 now = SDL_GetPerformanceCounter();
					float elapsed = (float)(now - sim_last_counter) * counter_to_ms;
					sim_last_counter = now;
					if (elapsed > period * 4.0f)
						elapsed = period;  // spiral guard (lag spike / resume from pause)
					sim_accumulator += elapsed;

					// Advance display-rate movement before the simulation-tick break.
					const bool vt_owns = vt_ship_owns();
					if (vt_owns)
						vt_ship_step(elapsed / period);

					if (sim_accumulator >= period)
					{
						sim_accumulator -= period;
						if (sim_accumulator > period)
							sim_accumulator = period;  // clamp backlog: at most one tick behind
						break;  // time for the next simulation tick
					}

					const float alpha = sim_accumulator / period;
					debug_interp_alpha = alpha;  // expose for the perf overlay
					// Interpolate ships whose positions remain owned by simulation or the wire.
					if (!vt_owns || isNetworkGame)
						vt_ship_update_override(alpha);

					if (anySmoothies)
					{
						float plasma_alpha = alpha;
						if (tick_plasma_ready)
						{
							// The cached plasma is the current tick endpoint. Keep it pristine while
							// foreground commands draw into their separate display buffer.
							memcpy(interp_buf->pixels, plasma_buf->pixels,
							       (size_t)plasma_buf->h * plasma_buf->pitch);
							plasma_alpha = 1.0f;
						}
						else
						{
							// Pass 1: derive this frame's background by filtering a copy of
							// the fixed plasma base with the interpolated backgrounds.
							memcpy(plasma_work->pixels, bg_feedback->pixels,
							       (size_t)bg_feedback->h * bg_feedback->pitch);
							rl_feedback_replay(plasma_work, alpha);
							if (plasma_work != plasma_buf)
								expand_frame_to_hi_threaded(plasma_work, plasma_buf, pss);
							rl_replay_bg(plasma_buf, alpha, pss, split_bg);
							if (interp_buf != plasma_buf)
							{
								expand_frame_to_hi_threaded(plasma_buf, interp_buf, rss / pss);
								rl_replay_bg_tail(interp_buf, alpha, rss);
							}
						}
						// Final stage: entities and overlays at foreground scale. Layer-bound
						// commands share the spatial/temporal phase of the plasma underneath.
						rl_replay_fg(interp_buf, alpha, rss, pss, plasma_alpha, split_bg);
					}
					else
					{
						rl_replay_interp(interp_buf, alpha, false, rss);
					}

					// Zinglon pillar onto the freshly-interpolated frame, centred on the
					// ship's render-rate position so it glides rather than snapping.
					draw_active_zinglon_pillars(interp_buf, rss, true, alpha);

					draw_boss_bar_present(interp_buf, rss, alpha);
					hud_killfire_bar_present(interp_buf, rss, alpha);
					hud_special_light_present(interp_buf, rss, alpha);

					if (use_hi)
					{
						// Expand the static HUD, then redraw its three moving gauges at
						// supersampled resolution for sub-pixel motion.
						compose_hi_frame(interp_buf, VGAScreenSeg, vga_hi, rss, true);
						if (power_gauge_active)
							draw_power_gauge(vga_hi, rss,
							                 (float)power_render_prev + (power_render_cur - power_render_prev) * alpha,
							                 (salvo_render_prev + (salvo_render_cur - salvo_render_prev) * alpha) / 100.0f);
						gauge_bars_present(vga_hi, rss, alpha);
						JE_drawPerfOverlay(vga_hi, rss);
						note_hi_present(rss);
						const Uint64 present_start = SDL_GetPerformanceCounter();
						present_hi(vga_hi);
						frame_times_present(now, present_start);
					}
					else
					{
						vga_hi_ref_scale = 0;
						composite_playfield(interp_buf, true);

						// Gauges at the interpolated level: they rise and fall smoothly
						// instead of stepping once per tick.
						if (power_gauge_active)
							draw_power_gauge(VGAScreenSeg, 1,
							                 (float)power_render_prev + (power_render_cur - power_render_prev) * alpha,
							                 (salvo_render_prev + (salvo_render_cur - salvo_render_prev) * alpha) / 100.0f);
						gauge_bars_present(NULL, 1, alpha);

						JE_drawPerfOverlay(VGAScreenSeg, 1);
						const Uint64 present_start = SDL_GetPerformanceCounter();
						JE_showVGA();
						frame_times_present(now, present_start);
					}

					if (!output_vsync)
						limit_render_fps();
					service_SDL_events(false);
				}
				setDelay(frameCountMax);  // keep `target` current for other timing readers
			}
			else
			{
				vga_hi_ref_scale = 0;
				JE_drawPerfOverlay(VGAScreenSeg, 1);
				const Uint64 present_start = SDL_GetPerformanceCounter();
				JE_showVGA();
				frame_times_present(entry, present_start);
				service_wait_delay();
				setDelay(frameCountMax);
			}

			// Advance persistent smoothie feedback once per tick. Store the full background because
			// the next filter reads the previous complete frame.
			if (anySmoothies && bg_feedback != NULL)
			{
				if (tick_plasma_ready)
					memcpy(bg_feedback->pixels, plasma_buf->pixels,
					       (size_t)plasma_buf->h * plasma_buf->pitch);
				else
					rl_feedback_commit(bg_feedback,
					                   (can_interp && !tick_plasma && rl_feedback_has_memory())
					                   ? RL_FEEDBACK_TICKS : 0);
			}
			else
			{
				rl_feedback_reset();
			}
		}
		else
		{
			JE_drawPerfOverlay(VGAScreenSeg, 1);
			const Uint64 present_start = SDL_GetPerformanceCounter();
			JE_showVGA();
			frame_times_present(entry, present_start);
		}
	}

	quitRequested = false;
	skipStarShowVGA = false;
	frame_times.tick_start = SDL_GetPerformanceCounter();
}

// Expand the bottom-right playfield edge to the HUD by copying a
// 16-pixel-tall column horizontally for the additional playfield width.
static void extend_playfield_right_column(SDL_Surface* surface)
{
	const int src_x = 262;  // last column of the original playfield
	const int src_y = 184;
	const int height = 16;
	const int copy_width = surface->w - HUD_WIDTH - 263;  // pixels to extend

	Uint8* row = (Uint8*)surface->pixels + src_y * surface->pitch + src_x;
	for (int y = 0; y < height; ++y)
	{
		const Uint8 pixel = row[0];
		memset(row + 1, pixel, copy_width);
		row += surface->pitch;
	}
}

// Sub-pixel fraction of tempMapXOfs, set beside every tempMapXOfs assignment so enemies float
// their parallax onto the background layer's sub-pixel offset.
static float tempMapXOfs_frac = 0.0f;
// Background layer whose horizontal anchor tempMapXOfs represents (1..3). The render list uses
// this to resolve legacy draw-order cases where the entity and layer straddle the parallax update.
static int tempMapXOfs_layer = 0;

// Vertical layer phase for enemy sprites and bars, which can straddle the integer ey advance.
static int   tempScrollYBase = 0, tempScrollYBaseBar = 0;
static float tempScrollYfrac = 0.0f, tempScrollYfracBar = 0.0f;
static int   tempScrollYLayer = 0;
// Normal layer step/delay behind the current batch. Full-speed fixedmovey scripts can be
// scaled from the layer's exact integer delta; delay-gated scripts use a percentage carry.
static int   tempScrollBaseStep = 0, tempScrollDelayMax = 1;

// Endless extra scroll for this enemy batch. Track the layer explicitly because layers can share
// the same temporary movement value.
static int tempScrollExtraPx = 0;

// Sky-bank enemies moving at the layer-2 step are attached scenery and inherit that layer's
// Endless and sub-pixel scroll.
static bool skyGlueThisEnemy = false;

// Preserve the previous curLoc interval so event-spawned enemies can catch up when boosted scroll
// crosses multiple event coordinates in one tick.
static bool eventScrollCatchupValid = false;
static int eventScrollFrom = 0, eventScrollTo = 0;
static int eventScrollLayerDelta[4] = { 0, 0, 0, 0 };
static int eventScrollBaseStep[4] = { 0, 0, 0, 0 };
static int eventScrollDelayMax[4] = { 1, 1, 1, 1 };
static int eventScrollBoost = 0;
// Sky-glue spawns use a separate layer clock; capture its carry phase for cross-layer anchoring.
static bool eventScrollSkyValid = false;
static int eventScrollSkyRatio100 = 0;
static int eventScrollSkyPhase100 = 0;

// Rollback-visible buffers for one-tick-lagged smooth-scroll publication.
static float bgSmoothRatePend[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static float bgSmoothFracPend[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static bool  bgSmoothActivePend  = false;

// Only the part of fixedmovey left after it cancels an opposing eyc is scroll-relative.
static int enemy_scalable_fixed_y(int fixed_move, int own_move)
{
	if (fixed_move < 0 && own_move > 0)
		return fixed_move + own_move < 0 ? fixed_move + own_move : 0;
	if (fixed_move > 0 && own_move < 0)
		return fixed_move + own_move > 0 ? fixed_move + own_move : 0;
	return fixed_move;
}

// Scale a layer-bound fixed-motion residual in the layer's integer phase so exact cancellation
// survives boosted scrolling. Delay-gated motion uses signed percentage carry instead.
static int enemy_fixed_move_y(unsigned int i)
{
	struct JE_SingleEnemyType *const e = &enemy[i];
	const int move = e->fixedmovey;
	const int scalable = enemy_scalable_fixed_y(move, e->eyc);
	const int boost = endlessScrollBoostPercent();

	if (scalable == 0 || tempScrollYLayer == 0 || boost == 0 || tempScrollBaseStep <= 0)
	{
		e->fixedmovey_carry = 0;
		e->fixedmovey_carry_base = 0;
		e->fixedmovey_carry_move = 0;
		return move;
	}

	int divisor, carry_base, numerator;
	if (tempScrollDelayMax == 1)
	{
		// Scale by the layer's ACTUAL base+extra delta this tick. For move == -baseStep,
		// integer division is exact and the sprite remains pixel-locked to the terrain.
		divisor = tempScrollBaseStep;
		carry_base = divisor;
		numerator = scalable * (tempBackMove + tempScrollExtraPx);
	}
	else
	{
		// Stock fixedmovey runs every tick even when the layer's delay gate is closed.
		// Preserve that behavior while scaling its average by the modifier exactly.
		divisor = 100;
		carry_base = -100;  // distinct from a full-speed layer whose step happens to be 100
		numerator = scalable * (100 + boost);
	}

	if (e->fixedmovey_carry_base != carry_base || e->fixedmovey_carry_move != scalable)
	{
		e->fixedmovey_carry = 0;
		e->fixedmovey_carry_base = carry_base;
		e->fixedmovey_carry_move = scalable;
	}

	numerator += e->fixedmovey_carry;
	const int scaled = numerator / divisor;  // signed truncation keeps carry bounded around zero
	e->fixedmovey_carry = numerator - scaled * divisor;
	return (move - scalable) + scaled;
}

enum { ENEMY_APPROACH_DEBT_MAX_PX100 = 25000 };

static int enemy_unscaled_own_y(const struct JE_SingleEnemyType *e)
{
	const int scalable = (tempScrollYLayer == 0 || tempScrollBaseStep <= 0)
	                     ? 0 : enemy_scalable_fixed_y(e->fixedmovey, e->eyc);
	return e->eyc + e->fixedmovey - scalable;
}

static void enemy_approach_debt_accrue(unsigned int i)
{
	struct JE_SingleEnemyType *const e = &enemy[i];
	const int boost = endlessScrollBoostPercent();

	if (boost == 0)
	{
		e->approach_debt_y100 = 0;
		return;
	}

	if (eventScrollBaseStep[1] <= 0 || skyGlueThisEnemy)
		return;

	int owed = e->approach_debt_y100 + enemy_unscaled_own_y(e) * boost;
	if (owed > ENEMY_APPROACH_DEBT_MAX_PX100)
		owed = ENEMY_APPROACH_DEBT_MAX_PX100;
	else if (owed < -ENEMY_APPROACH_DEBT_MAX_PX100)
		owed = -ENEMY_APPROACH_DEBT_MAX_PX100;

	e->approach_debt_y100 = (JE_integer)owed;
}

// Presentation-only enemy velocity fallback for blinking sprites. Recycled slots discard it.
static int    rl_enemy_hint_px[100], rl_enemy_hint_py[100];
static int    rl_enemy_hint_vx[100], rl_enemy_hint_vy[100];
static Uint32 rl_enemy_hint_gen[100];
static JE_word rl_enemy_hint_type[100];
static Uint32 rl_enemy_gen;   // bumped once per sim pass at the loop top
// Presented frames, for cosmetics that need a steady clock. Rollback re-simulation runs several sim
// passes against one presented frame, so a cadence keyed to rl_enemy_gen stutters.
static Uint32 rl_present_gen;

// Contract in game_loop.h.
Uint32 rl_presented_frames(void) { return rl_present_gen; }

/* Boss vulnerability cue. See doc/notes.md#health-and-tiers. */

// Presented-frame stamp plus one; 0 means idle.
static Uint32 enemyArmedFlashAt[100];

// Per-body shade lift passed to blit_enemy.
static Uint8 enemyFlashLift;

bool enemy_armed_flash_arms(JE_byte wasArmor, JE_byte nowArmor, JE_byte avail)
{
	return wasArmor >= 255 && nowArmor > 0 && nowArmor < 255 && avail == 0;
}

bool enemy_armed_flash_shows(JE_byte linknum)
{
	if (vulnerableCue == VULN_CUE_ALL)
		return true;
	return vulnerableCue == VULN_CUE_BOSSES && enemy_has_boss_bar(linknum);
}

Uint8 enemy_armed_flash_lift(Uint32 left)
{
	if (left == 0)
		return 0;
	if (left >= ENEMY_ARMED_FLASH_WHITE)
		return 0x0f;
	return (Uint8)(left * 3);
}

Uint32 enemy_armed_flash_left(unsigned int slot)
{
	if (enemyArmedFlashAt[slot] == 0)
		return 0;
	const Uint32 elapsed = rl_present_gen + 1 - enemyArmedFlashAt[slot];
	return (elapsed < ENEMY_ARMED_FLASH_FRAMES) ? ENEMY_ARMED_FLASH_FRAMES - elapsed : 0;
}

// Arm after direct armor writes on live passes; damage never calls this path.
void enemy_note_armed(unsigned int slot, JE_byte wasArmor)
{
	if (vulnerableCue == VULN_CUE_OFF || rollback_resim)
		return;
	if (!enemy_armed_flash_arms(wasArmor, enemy[slot].armorleft, enemyAvail[slot]))
		return;

	enemyArmedFlashAt[slot] = rl_present_gen + 1;
}

// A recycled slot must not inherit the last occupant's cue.
static void enemy_armed_flash_clear(unsigned int slot)
{
	enemyArmedFlashAt[slot] = 0;
}

// Banks the Endless "?" pickup cycles through. Level palettes vary, so the list keeps to hues that
// hold a readable ramp in all of them; bank 0 and the near-black banks are left out.
static const Uint8 endlessSpecialIconBanks[] = { 0x10, 0x20, 0x30, 0x50, 0x70, 0x90, 0xC0, 0xD0 };
#define ENDLESS_SPECIAL_ICON_TICKS 7

// Bank for this frame. Presentation only: rl_present_gen is outside the rollback registry.
static Uint8 endlessSpecialIconFilter(void)
{
	const Uint32 step = (rl_present_gen / ENDLESS_SPECIAL_ICON_TICKS) % COUNTOF(endlessSpecialIconBanks);
	return endlessSpecialIconBanks[step];
}

#define ENDLESS_SPECIAL_SPARK_TICKS 5  // one shower per pickup this often, staggered by enemy slot
#define ENDLESS_SPECIAL_SPARK_COUNT 3
// Reach is also the per-tick velocity, and a spark lives 15 ticks, so this bounds how far the
// shower carries. Matches the density of the superspark weapon trails.
#define ENDLESS_SPECIAL_SPARK_REACH 3

// Presentation-only glyph shower; silent replay must not spawn it.
static void endlessSpecialIconSparks(unsigned int i)
{
	if (rollback_resim_silent)
		return;

	// Publish the glyph occluder every tick while earlier sparks remain in flight.
	JE_addSPOccluder(enemy[i].ex + tempMapXOfs + ENDLESS_SPECIAL_GLYPH_INK_X0 - 1,
	                 enemy[i].ey + ENDLESS_SPECIAL_GLYPH_INK_Y0 - 1,
	                 enemy[i].ex + tempMapXOfs + ENDLESS_SPECIAL_GLYPH_INK_X1 + 1,
	                 enemy[i].ey + ENDLESS_SPECIAL_GLYPH_INK_Y1 + 1);

	if ((rl_present_gen % ENDLESS_SPECIAL_SPARK_TICKS) != (i % ENDLESS_SPECIAL_SPARK_TICKS))
		return;

	// Glyph centre, matching the cells the draw places at x_offset -6/+6 and y_offset 0.
	const int cx = enemy[i].ex + tempMapXOfs + 6;
	const int cy = enemy[i].ey + 7;
	if (cx < 0 || cy < 0)
		return;

	// Keep glyph showers outside the classic spark cap.
	JE_doSPSeeded((JE_word)cx, (JE_word)cy, ENDLESS_SPECIAL_SPARK_COUNT, ENDLESS_SPECIAL_SPARK_REACH,
	              endlessSpecialIconFilter(), false, ENDLESS_SPARK_BRIGHT, true,
	              rl_present_gen * 100u + i);
}

#define ENDLESS_ELITE_SPARK_TICKS 5  // one shower per elite this often, staggered by enemy slot
#define ENDLESS_ELITE_SPARK_COUNT 1  // champions shed twice this
#define ENDLESS_ELITE_SPARK_REACH 2  // per-tick velocity too, plus one for a 2x2 body

// Presentation-only tier aura; silent replay must not spawn it.
static void endlessEliteAuraSparks(unsigned int i)
{
	if (rollback_resim_silent || enemy[i].eliteState < 2 || enemy[i].iced || enemy[i].edamaged)
		return;
	if ((rl_present_gen % ENDLESS_ELITE_SPARK_TICKS) != (i % ENDLESS_ELITE_SPARK_TICKS))
		return;

	// Sprite centre, the same point for a 1x1 body and a 2x2 one. JE_drawSP clips the rest of the
	// way, so a body hanging off the bottom needs no bound of its own.
	const int cx = enemy[i].ex + tempMapXOfs + 6;
	const int cy = enemy[i].ey + 7;
	if (cx < 0 || cy < 0)
		return;

	const bool champion = (enemy[i].eliteState == 3);
	const int sparks = ENDLESS_ELITE_SPARK_COUNT * (champion ? 2 : 1);

	// Elite auras use a different seed stride from pickup sparks because one orb may emit both.
	// Auras on the special glyph share its occluder.
	JE_doSPSeeded((JE_word)cx, (JE_word)cy, sparks,
	              ENDLESS_ELITE_SPARK_REACH + (enemy[i].size == 1 ? 1 : 0),
	              champion ? ENDLESS_CHAMPION_FILTER : ENDLESS_ELITE_FILTER, false,
	              ENDLESS_SPARK_BRIGHT, endlessSpecialPickup((int)i), rl_present_gen * 137u + i);
}

inline static void blit_enemy(SDL_Surface *surface, unsigned int i, signed int x_offset, signed int y_offset, signed int sprite_offset, bool outline)
{
	if (enemy[i].sprite2s == NULL)
	{
		fprintf(stderr, "warning: enemy %d sprite missing\n", i);
		return;
	}

	const int x = enemy[i].ex + x_offset + tempMapXOfs,
	          y = enemy[i].ey + y_offset;

	// Derive one velocity hint per enemy slot and reuse it for linked cells.
	if (rl_enemy_hint_gen[i] != rl_enemy_gen)
	{
		const Uint32 gap = rl_enemy_gen - rl_enemy_hint_gen[i];
		int hvx = 0, hvy = 0;
		if (gap >= 1 && gap <= 3 && rl_enemy_hint_type[i] == enemy[i].enemytype)
		{
			hvx = (x - rl_enemy_hint_px[i]) / (int)gap;
			hvy = (y - rl_enemy_hint_py[i]) / (int)gap;
			if (hvx > 40 || hvx < -40 || hvy > 40 || hvy < -40)
			{
				hvx = 0;
				hvy = 0;
			}
		}
		rl_enemy_hint_vx[i] = hvx;
		rl_enemy_hint_vy[i] = hvy;
		rl_enemy_hint_px[i] = x;
		rl_enemy_hint_py[i] = y;
		rl_enemy_hint_gen[i] = rl_enemy_gen;
		rl_enemy_hint_type[i] = enemy[i].enemytype;
	}

	// Endless special pickups wear the cycling "?" from spriteSheet10 rather than their own art.
	const bool specialPickup = endlessSpecialPickup((int)i);
	Sprite2_array *const sheet = specialPickup ? &spriteSheet10 : enemy[i].sprite2s;

	// enemycycle indexes egr[] 1-based; skip anything that doesn't name a real in-sheet sprite
	// instead of underflowing into a wild read in blit_sprite2.
	const unsigned int cycle = enemy[i].enemycycle;
	if (cycle < 1 || cycle > 20)
		return;
	const unsigned int index = specialPickup
	                         ? (unsigned int)ENDLESS_SPECIAL_PICKUP_ICON + sprite_offset
	                         : enemy[i].egr[cycle - 1] + sprite_offset;
	if (index == 0 || (size_t)index * sizeof(Uint16) > sheet->size)
		return;

	rl_current_id = RL_ID_ENEMY_BASE + (int)i;  // tag for cross-frame interpolation
	rl_current_vel_x = rl_enemy_hint_vx[i];     // fallback velocity when pairing fails (blinkers)
	rl_current_vel_y = rl_enemy_hint_vy[i];
	rl_current_par_frac = tempMapXOfs_frac;     // float the parallax to match the background
	rl_current_par_layer = tempMapXOfs_layer;
	rl_current_par_anchor = (float)(tempMapXOfs - PLAYFIELD_X_SHIFT) + tempMapXOfs_frac;
	rl_current_par_ybase = tempScrollYBase;
	if (skyGlueThisEnemy)
	{
		// Attached sky scenery: bind to layer 2's lagged clock so replay places it with the
		// exact transform the glass rows use (pre-advance phase, batch base is already 0).
		rl_current_par_yfrac = bg_layer_yfrac[2];
		rl_current_par_ylayer = 2;
	}
	else
	{
		rl_current_par_yfrac = tempScrollYfrac;
		rl_current_par_ylayer = tempScrollYLayer;
	}
	// The special-pickup color cycle takes precedence over a hit flash.
	const Uint8 filter = specialPickup ? endlessSpecialIconFilter() : enemy[i].filter;
	if (outline)
		blit_sprite2_solid(surface, x, y, *sheet, index, filter + ENDLESS_SPECIAL_OUTLINE_SHADE);
	else if (enemyFlashLift != 0)
		blit_sprite2_filter_bright(surface, x, y, *sheet, index, ENEMY_FLASH_BANK | enemyFlashLift);
	else if (filter != 0)
		blit_sprite2_filter(surface, x, y, *sheet, index, filter);
	else
		blit_sprite2(surface, x, y, *sheet, index);
	rl_current_id = 0;
	rl_current_vel_x = 0;
	rl_current_vel_y = 0;
	rl_current_par_frac = 0.0f;
	rl_current_par_layer = 0;
	rl_current_par_anchor = 0.0f;
	rl_current_par_ybase = 0;
	rl_current_par_yfrac = 0.0f;
	rl_current_par_ylayer = 0;
}

// Extra off-screen margin for the enemy-draw visibility tests: the interpolated position lags
// the tick position by up to one tick of motion. Equals the interpolation snap threshold.
enum { ENEMY_DRAW_MARGIN = 40 };

// Test an opaque 12x14 cell with the same bounds convention as the sprite test.
static bool sprite_cell_in_window(int x, int y, int wx0, int wx1, int wy0, int wy1)
{
	return x + 11 >= wx0 && x <= wx1 && y + 13 >= wy0 && y < wy1;
}

// Fall back to nominal bounds when a map-drawn enemy frame has no opaque pixels.
static bool enemy_has_visible_pixel(unsigned int i)
{
	if (enemy[i].sprite2s == NULL)
		return false;

	const unsigned int cycle = enemy[i].enemycycle;
	if (cycle < 1 || cycle > 20)  // no real frame selected (matches blit_enemy's guard)
		return false;
	const JE_word gr = enemy[i].egr[cycle - 1];
	if (gr == 999)  // blank frame: JE_drawEnemy treats it as gone, nothing is drawn
		return false;

	const int baseX = enemy[i].ex + enemy[i].mapoffset;  // identical to the collision's X term
	const int baseY = enemy[i].ey;
	const int wx0 = PLAYFIELD_LEFT, wx1 = PLAYFIELD_RIGHT, wy0 = 0, wy1 = vga_height;

	if (enemy[i].size == 1)  // 2x2 enemy: four cells, top/bottom rows gated on ey as in the draw
	{
		const bool topRow = enemy[i].ey > -13 - ENEMY_DRAW_MARGIN;
		const bool botRow = enemy[i].ey > -26 - ENEMY_DRAW_MARGIN && enemy[i].ey < 182 + ENEMY_DRAW_MARGIN;
		const struct { int dx, dy, off; bool on; } cell[4] = {
			{ -6, -7,  0, topRow }, {  6, -7,  1, topRow },
			{ -6,  7, 19, botRow }, {  6,  7, 20, botRow },
		};
		bool anyArt = false, blankCellOnScreen = false;
		for (int c = 0; c < 4; c++)
		{
			if (!cell[c].on)
				continue;
			const int cx = baseX + cell[c].dx, cy = baseY + cell[c].dy;
			const unsigned int index = (unsigned int)gr + cell[c].off;
			if (index == 0 || (size_t)index * sizeof(Uint16) > enemy[i].sprite2s->size)
				continue;
			if (sprite2_has_pixel_in_window(cx, cy, *enemy[i].sprite2s, index, wx0, wx1, wy0, wy1))
				return true;
			if (sprite2_is_blank(*enemy[i].sprite2s, index))
				blankCellOnScreen |= sprite_cell_in_window(cx, cy, wx0, wx1, wy0, wy1);
			else
				anyArt = true;  // a drawn cell exists: its pixels alone decide
		}
		return !anyArt && blankCellOnScreen;
	}
	else  // normal enemy: a single cell, drawn only above the same lower ey bound
	{
		if (!(enemy[i].ey > -13 - ENEMY_DRAW_MARGIN))
			return false;
		const unsigned int index = gr;  // sprite_offset 0
		if (index == 0 || (size_t)index * sizeof(Uint16) > enemy[i].sprite2s->size)
			return false;
		if (sprite2_has_pixel_in_window(baseX, baseY, *enemy[i].sprite2s, index, wx0, wx1, wy0, wy1))
			return true;
		return sprite2_is_blank(*enemy[i].sprite2s, index) &&
		       sprite_cell_in_window(baseX, baseY, wx0, wx1, wy0, wy1);
	}
}

static bool enemy_approach_in_group(unsigned int g, unsigned int i, JE_byte link)
{
	if (enemyAvail[g] == 1)
		return false;

	return (link == 0) ? g == i : enemy[g].linknum == link;
}

void enemy_approach_debt_settle(unsigned int i)
{
	if (enemyAvail[i] == 1)
		return;

	const JE_byte link = enemy[i].linknum;

	for (unsigned int g = 0; g < i; g++)
		if (enemy_approach_in_group(g, i, link))
			return;

	int topY = 0, topDebt100 = 0;
	bool haveTop = false, owed = false;

	for (unsigned int g = 0; g < 100; g++)
	{
		if (!enemy_approach_in_group(g, i, link))
			continue;

		if (enemy[g].eyc > 0 || enemy[g].eycc > 0 || enemy[g].fixedmovey > 0)
			return;

		if (enemy[g].ey >= 0 || enemy_has_visible_pixel(g))
			return;

		owed = owed || enemy[g].approach_debt_y100 != 0;

		if (!haveTop || enemy[g].ey < topY)
		{
			topY = enemy[g].ey;
			topDebt100 = enemy[g].approach_debt_y100;
			haveTop = true;
		}
	}

	if (!owed)
		return;

	const int shift = topDebt100 / 100;

	for (unsigned int g = 0; g < 100; g++)
		if (enemy_approach_in_group(g, i, link))
		{
			enemy[g].ey += shift;
			enemy[g].approach_debt_y100 = 0;
		}
}

// True when a live enemy is frozen above shot reach. Horizontal movement does
// not count because HARVEST's anchor sways sideways.
static bool enemy_stuck_above_screen(unsigned int i)
{
	return enemy[i].ey   <= -58 &&
	       enemy[i].eyc  <= 0 &&
	       enemy[i].eycc <= 0 &&
	       enemy[i].fixedmovey <= 0;
}

// True when a linked enemy has a reachable partner. Link 0 means unlinked;
// each such enemy stands alone. HARVEST relies on its reachable linked pieces.
static bool enemy_link_group_reachable(unsigned int i)
{
	const JE_byte link = enemy[i].linknum;
	if (link == 0)
		return false;

	for (int e = 0; e < 100; e++)
		if (enemyAvail[e] != 1 && enemy[e].linknum == link && !enemy_stuck_above_screen(e))
			return true;

	return false;
}

// The watchdog targets enemies above shot reach with no reachable link partner.
static bool enemy_stuck_orphaned(unsigned int i)
{
	return enemy_stuck_above_screen(i) && !enemy_link_group_reachable(i);
}

// Count orphaned enemies with a full-pool scan, independent of drawing.
static unsigned int count_stuck_above_screen(void)
{
	unsigned int n = 0;
	for (int i = 0; i < 100; i++)
		if (enemyAvail[i] != 1 && enemy_stuck_orphaned(i))
			++n;
	return n;
}

// Wait about six seconds before culling a stuck-above enemy.
enum { MAP_STOP_STALL_LIMIT = 210 };

// Per-link scenery flags, rebuilt before each homing pass.
static bool endlessLinkHoldsScenery[256];

void endlessScanSceneryLinks(void)
{
	memset(endlessLinkHoldsScenery, 0, sizeof(endlessLinkHoldsScenery));

	for (int i = 0; i < 100; i++)
	{
		if (enemyAvail[i] != 1 && enemy[i].linknum != 0 && !enemy[i].scoreitem &&
		    !endlessEnemyDestructible(enemyAvail[i], enemy[i].linknum, enemy[i].armorleft))
			endlessLinkHoldsScenery[enemy[i].linknum] = true;
	}
}

// Loot keeps its scripted drift, so a homing sector never sends pickups after the ship.
// Linked bodies chase only if every member can eventually be shot.
bool endlessHomingChaser(unsigned int i)
{
	if (enemy[i].scoreitem)
		return false;
	if (!endlessEnemyDestructible(enemyAvail[i], enemy[i].linknum, enemy[i].armorleft))
		return false;
	return enemy[i].linknum == 0 || !endlessLinkHoldsScenery[enemy[i].linknum];
}

static void endlessGroupHoming(void)
{
	if (!endlessFxActive())
		return;

	endlessScanSceneryLinks();

	for (int i = 0; i < 100; i++)
		enemy[i].groupHomed = false;

	for (int i = 0; i < 100; i++)
	{
		if (enemyAvail[i] == 1 || enemy[i].groupHomed || enemy[i].linknum == 0 ||
		    enemy[i].scoreitem || (enemy[i].xaccel == 0 && enemy[i].yaccel == 0))
			continue;

		int members[100];
		int count = 0;
		int sumX = 0;
		int sumY = 0;

		for (int j = i; j < 100; j++)
		{
			if (enemyAvail[j] == 1 || enemy[j].scoreitem ||
			    enemy[j].linknum != enemy[i].linknum ||
			    enemy[j].exc    != enemy[i].exc    || enemy[j].eyc    != enemy[i].eyc ||
			    enemy[j].excc   != enemy[i].excc   || enemy[j].eycc   != enemy[i].eycc ||
			    enemy[j].exccw  != enemy[i].exccw  || enemy[j].eyccw  != enemy[i].eyccw ||
			    enemy[j].xaccel != enemy[i].xaccel || enemy[j].yaccel != enemy[i].yaccel)
				continue;
			members[count++] = j;
			sumX += enemy[j].ex;
			sumY += enemy[j].ey;
		}

		if (count < 2)
			continue;

		const int cx = sumX / count;
		const int cy = sumY / count;

		// The whole group chases the leader's ship, so a formation stays a formation.
		const Player *const prey = &player[endlessHomingTargetPlayer(enemy[i].homeTarget)];

		if (enemy[i].xaccel && enemy[i].xaccel - 89u > mt_rand() % 11)
		{
			if (prey->x - 25 > cx)
			{
				if (enemy[i].exc < enemy[i].xaccel - 89)
					for (int m = 0; m < count; m++)
						enemy[members[m]].exc++;
			}
			else
			{
				if (enemy[i].exc >= 0 || -enemy[i].exc < enemy[i].xaccel - 89)
					for (int m = 0; m < count; m++)
						enemy[members[m]].exc--;
			}
		}

		if (enemy[i].yaccel && enemy[i].yaccel - 89u > mt_rand() % 11)
		{
			if (prey->y > cy)
			{
				if (enemy[i].eyc < enemy[i].yaccel - 89)
					for (int m = 0; m < count; m++)
						enemy[members[m]].eyc++;
			}
			else
			{
				if (enemy[i].eyc >= 0 || -enemy[i].eyc < enemy[i].yaccel - 89)
					for (int m = 0; m < count; m++)
						enemy[members[m]].eyc--;
			}
		}

		for (int m = 0; m < count; m++)
			enemy[members[m]].groupHomed = true;
	}
}

static void enemy_shot_aim(int b, JE_byte aim, JE_integer fromX, JE_integer fromY, JE_byte *target)
{
	/*DIF*/
	if (difficultyLevel > DIFFICULTY_NORMAL)
		aim += difficultyLevel - 2;

	JE_word targetX = player[0].x;
	JE_word targetY = player[0].y;

	if (twoPlayerMode)
	{
		// fire at live player(s)
		if (player[0].is_alive && !player[1].is_alive)
			*target = 0;
		else if (player[1].is_alive && !player[0].is_alive)
			*target = 1;
		else
			*target = mt_rand() % 2;

		if (*target == 1)
		{
			targetX = player[1].x - 25;
			targetY = player[1].y;
		}
	}

	JE_integer aimX = (targetX + 25) - fromX - tempMapXOfs - 4;
	if (aimX == 0)
		aimX = 1;
	JE_integer aimY = targetY - fromY;
	if (aimY == 0)
		aimY = 1;
	const JE_integer maxMagAim = MAX(abs(aimX), abs(aimY));
	enemyShot[b].sxm = roundf((float)aimX / maxMagAim * aim);
	enemyShot[b].sym = roundf((float)aimY / maxMagAim * aim);
}

// Base Dispenser volley: one enemy-84 aimed shot plus a four-segment lightning column.
static void dispenser_fire(unsigned int i, JE_integer baseX, JE_integer baseY)
{
	if (cheatNoEnemyFire || endlessReviveGraceActive())
		return;

	const int shotCap = endlessFxActive() ? ENEMY_SHOT_MAX : ENEMY_SHOT_NORMAL;
	const Uint16 w = 59;  // enemy 84's aimed turret weapon

	int spct = 100, dpct = 100;
	if (endlessFxActive())
	{
		spct = endlessShotSpeedPercent();
		dpct = endlessShotDamagePercent();
		if (enemy[i].eliteState == 3)
			dpct = dpct * endlessChampionShotDamagePercent() / 100;
	}
	const Uint8 tint = endlessEliteTint(enemy[i].eliteState);

	// Align the 12x14 aimed shot with the eye and the lightning column with the lower orb.
	const JE_integer eyeX = baseX + 10, eyeY = baseY - 34;
	const JE_integer orbX = baseX + 10, orbY = baseY + 4;

	int b = -1;
	for (int slot = 0; slot < shotCap; ++slot)
		if (enemyShotAvail[slot] == 1)
		{
			b = slot;
			break;
		}
	if (b < 0)
		return;
	enemyShotAvail[b] = 0;

	{
		int sq;
		do
			sq = mt_rand() % 8;
		while (sq == 3);
		soundQueue[sq] = weapons[w].sound;
	}

	enemyShot[b].sx = eyeX + tempMapXOfs;
	enemyShot[b].sy = eyeY;
	enemyShot[b].sdmg = weapons[w].attack[0];
	enemyShot[b].tx = weapons[w].tx;
	enemyShot[b].ty = weapons[w].ty;
	enemyShot[b].duration = weapons[w].del[0];
	enemyShot[b].animate = 0;
	enemyShot[b].animax = weapons[w].weapani;
	enemyShot[b].sgr = weapons[w].sg[0];
	enemyShot[b].seekerArm = 0;
	enemyShot[b].seekerLeft = 0;
	enemyShot[b].filter = tint;
	enemyShot[b].syc = weapons[w].acceleration;
	enemyShot[b].sxc = weapons[w].accelerationx;
	enemyShot[b].sxm = weapons[w].sx[0];
	enemyShot[b].sym = weapons[w].sy[0];

	{
		JE_byte target;
		enemy_shot_aim(b, weapons[w].aim, eyeX, eyeY, &target);
	}

	if (endlessFxActive())
	{
		enemyShot[b].seekerLeft = endlessSeekerPasses();
		if (enemyShot[b].seekerLeft > 0)
			enemyShot[b].seekerArm = 1;
		enemyShot[b].sxm = (enemyShot[b].sxm * spct + (enemyShot[b].sxm >= 0 ? 50 : -50)) / 100;
		enemyShot[b].sym = (enemyShot[b].sym * spct + (enemyShot[b].sym >= 0 ? 50 : -50)) / 100;
		int dmg = (enemyShot[b].sdmg * dpct + 50) / 100;
		enemyShot[b].sdmg = (JE_byte)(dmg > 255 ? 255 : dmg);

		// Tide extra shots fan out from the aimed shot like any other turret volley.
		const int extra = endlessExtraEnemyShots();
		const int fanPhase = endlessFanPhaseNow();
		for (int k = 0; k < extra; ++k)
		{
			int c = -1;
			for (int slot = 0; slot < shotCap; ++slot)
				if (enemyShotAvail[slot] == 1)
				{
					c = slot;
					break;
				}
			if (c < 0)
				break;
			enemyShotAvail[c] = 0;
			enemyShot[c] = enemyShot[b];
			const int fanOrder = fanPhase + k;
			const float fanAng = ((fanOrder & 1) ? -1.0f : 1.0f) * (k / 2 + 1) * 0.20f;
			const float fc = sim_cosf(fanAng), fs = sim_sinf(fanAng);
			const int ox = enemyShot[c].sxm, oy = enemyShot[c].sym;
			enemyShot[c].sxm = roundf(ox * fc - oy * fs);
			enemyShot[c].sym = roundf(ox * fs + oy * fc);
			if (enemyShot[c].sxm == 0 && enemyShot[c].sym == 0)
			{
				enemyShot[c].sxm = ox;
				enemyShot[c].sym = oy;
			}
		}
	}

	// Spawn all four lightning segments together so their animation frames stay aligned.
	static const JE_word boltSegment[4] = { 210, 229, 248, 267 };
	int boltSpeed = 10;

	{
		// The bolt's own voice: weapons 238-242 fire this exact 4-tile sprite set and all
		// of them use S_WEAPON_15.
		int sq;
		do
			sq = mt_rand() % 8;
		while (sq == 3);
		soundQueue[sq] = S_WEAPON_15;
	}

	int boltDmg = weapons[w].attack[0];
	int bolts = 1;
	if (endlessFxActive())
	{
		boltSpeed = (boltSpeed * spct + 50) / 100;
		int dmg = (boltDmg * dpct + 50) / 100;
		boltDmg = dmg > 255 ? 255 : dmg;

		// Rising tide adds complete four-segment bolts, matching the aimed-shot multiplier.
		bolts += endlessExtraEnemyShots();
	}

	// Never emit a partial bolt: cap on the whole bolts the pool can still hold.
	{
		int freeShots = 0;
		for (int slot = 0; slot < shotCap; ++slot)
			if (enemyShotAvail[slot] == 1)
				++freeShots;
		if (bolts > freeShots / 4)
			bolts = freeShots / 4;
	}

	const int boltFanPhase = endlessFanPhaseNow();
	for (int volley = 0; volley < bolts; ++volley)
	{
		// Rotate both velocity and segment offsets so tide bolts remain straight.
		float dx = 0.0f, dy = 1.0f;
		if (volley > 0)
		{
			const int fanK = volley - 1;
			const int fanOrder = boltFanPhase + fanK;
			const float fanAng = ((fanOrder & 1) ? -1.0f : 1.0f) * (fanK / 2 + 1) * 0.20f;
			dx = -sim_sinf(fanAng);
			dy = sim_cosf(fanAng);
		}

		for (int s = 0; s < 4; ++s)
		{
			int c = -1;
			for (int slot = 0; slot < shotCap; ++slot)
				if (enemyShotAvail[slot] == 1)
				{
					c = slot;
					break;
				}
			if (c < 0)
				break;
			enemyShotAvail[c] = 0;
			enemyShot[c].sx = orbX + tempMapXOfs + (int)roundf(dx * 14.0f * s);
			enemyShot[c].sy = orbY + (int)roundf(dy * 14.0f * s);
			enemyShot[c].sxm = (int)roundf(dx * boltSpeed);
			enemyShot[c].sym = (int)roundf(dy * boltSpeed);
			enemyShot[c].sxc = 0;
			enemyShot[c].syc = 0;
			enemyShot[c].sdmg = (JE_byte)boltDmg;
			enemyShot[c].tx = 0;
			enemyShot[c].ty = 0;
			enemyShot[c].duration = 255;
			enemyShot[c].animate = 0;
			enemyShot[c].animax = 4;
			enemyShot[c].sgr = boltSegment[s];
			enemyShot[c].seekerArm = 0;
			enemyShot[c].seekerLeft = 0;
			enemyShot[c].filter = tint;
		}
	}
}

// Detect layer-2 sky scenery from its authored eyc/fixedmovey step. Exclude homing enemies and
// falling score items, whose movement can only coincide with the layer rate.
static bool enemy_rides_layer2(const struct JE_SingleEnemyType *e)
{
	return backMove2 > 0 && !e->scoreitem && e->yaccel == 0 &&
	       (int)e->fixedmovey + (e->eycc != 0 ? 0 : (int)e->eyc) == (int)backMove2;
}

static void JE_drawEnemy(int enemyOffset) // actually does a whole lot more than just drawing
{
	// Batch 25 is the sky bank and takes layer-2 movement from eyc.
	const bool skyBank = (enemyOffset == 25);

	player[0].x -= 25;

	for (int i = enemyOffset - 25; i < enemyOffset; i++)
	{
		if (enemyAvail[i] != 1)
		{
			// Settle tier and homing eligibility on the body's first processed frame.
			if (endlessFxActive() && enemy[i].eliteState == 0)
			{
				enemy[i].eliteState = (JE_byte)endlessEliteTierNow(
					enemy[i].linknum, enemy[i].armorleft, enemy[i].scoreitem);

				if (!endlessHomingChaser(i))
				{
					enemy[i].xaccel = enemyDat[enemy[i].enemytype].xaccel;
					enemy[i].yaccel = enemyDat[enemy[i].enemytype].yaccel;
				}
			}

			skyGlueThisEnemy = skyBank && enemy_rides_layer2(&enemy[i]);

			enemy[i].mapoffset = tempMapXOfs;
			enemy[i].mapoffset_frac = tempMapXOfs_frac;  // for the health bar's smooth-H match
			if (skyGlueThisEnemy)
			{
				// The bar records post-advance; pull back the scroll part of this tick's
				// advance (eyc == backMove2 plus the modifier's extra layer-2 pixels).
				enemy[i].scroll_ybase = -((int)backMove2 + endlessScrollExtraPx2);
				enemy[i].scroll_yfrac = bg_layer_yfrac[2];
				enemy[i].scroll_ylayer = 2;
			}
			else
			{
				enemy[i].scroll_ybase = tempScrollYBaseBar;
				enemy[i].scroll_yfrac = tempScrollYfracBar;
				enemy[i].scroll_ylayer = (JE_byte)tempScrollYLayer;
			}

			// The ship this one tracks: its own coin toss in co-op Endless, ship one everywhere else.
			const Player *const prey = &player[endlessHomingTargetPlayer(enemy[i].homeTarget)];

			if (!enemy[i].groupHomed && enemy[i].xaccel && enemy[i].xaccel - 89u > mt_rand() % 11)
			{
				if (prey->x > enemy[i].ex)
				{
					if (enemy[i].exc < enemy[i].xaccel - 89)
						enemy[i].exc++;
				}
				else
				{
					if (enemy[i].exc >= 0 || -enemy[i].exc < enemy[i].xaccel - 89)
						enemy[i].exc--;
				}
			}

			if (!enemy[i].groupHomed && enemy[i].yaccel && enemy[i].yaccel - 89u > mt_rand() % 11)
			{
				if (prey->y > enemy[i].ey)
				{
					if (enemy[i].eyc < enemy[i].yaccel - 89)
						enemy[i].eyc++;
				}
				else
				{
					if (enemy[i].eyc >= 0 || -enemy[i].eyc < enemy[i].yaccel - 89)
						enemy[i].eyc--;
				}
			}

			// Keep 2x2 sprites active until fully outside the visible window. The
			// asymmetric bounds also prevent unclipped left pixels from row-wrapping.
			if (enemy[i].ex + tempMapXOfs > -28 && enemy[i].ex + tempMapXOfs < PLAYFIELD_RIGHT + 38)
			{
				if (enemy[i].aniactive == 1)
				{
					enemy[i].enemycycle++;

					if (enemy[i].enemycycle == enemy[i].animax)
						enemy[i].aniactive = enemy[i].aniwhenfire;
					else if (enemy[i].enemycycle > enemy[i].ani)
						enemy[i].enemycycle = enemy[i].animin;

					// Fire the restored dispenser once, on its open-hatch frame.
					if (dispenserBasesActive && enemy[i].enemytype == 80 &&
					    enemy[i].enemycycle == 9 && !enemy[i].iced && !enemy[i].edamaged)
						dispenser_fire(i, enemy[i].ex, enemy[i].ey);
				}

				if (enemy[i].enemycycle >= 1 && enemy[i].enemycycle <= 20 &&
				    enemy[i].egr[enemy[i].enemycycle - 1] == 999)
					goto enemy_gone;

				// The flash overrides tint. Choose its scope here because the boss bar may
				// appear after the armor event in the same tick.
				enemyFlashLift = enemy_armed_flash_shows(enemy[i].linknum)
				               ? enemy_armed_flash_lift(enemy_armed_flash_left(i))
				               : 0;
				if (enemyFlashLift == 0 && enemy[i].filter == 0)
					enemy[i].filter = enemy_body_tint(i);

				endlessEliteAuraSparks(i);

				if (endlessSpecialPickup((int)i))
				{
					// Top half of the icon only, at y_offset 0 so the glyph lands on the centre a
					// full 2x2 pickup would have occupied.
					if (enemy[i].ey > -13 - ENEMY_DRAW_MARGIN && enemy[i].ey < 182 + ENEMY_DRAW_MARGIN)
					{
						// The glyph straddles both cells, so every outline pass must land before
						// either glyph cell; per-cell order lets one outline notch the other's art.
						static const struct { int dx, dy; } outline[] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
						for (unsigned int o = 0; o < COUNTOF(outline); ++o)
						{
							blit_enemy(VGAScreen, i, -6 + outline[o].dx, outline[o].dy, 0, true);
							blit_enemy(VGAScreen, i,  6 + outline[o].dx, outline[o].dy, 1, true);
						}

						blit_enemy(VGAScreen, i, -6, 0, 0, false);
						blit_enemy(VGAScreen, i,  6, 0, 1, false);
						endlessSpecialIconSparks(i);
					}
				}
				else if (enemy[i].size == 1) // 2x2 enemy
				{
					if (enemy[i].ey > -13 - ENEMY_DRAW_MARGIN)
					{
						blit_enemy(VGAScreen, i, -6, -7, 0, false);
						blit_enemy(VGAScreen, i,  6, -7, 1, false);
					}
					if (enemy[i].ey > -26 - ENEMY_DRAW_MARGIN && enemy[i].ey < 182 + ENEMY_DRAW_MARGIN)
					{
						blit_enemy(VGAScreen, i, -6,  7, 19, false);
						blit_enemy(VGAScreen, i,  6,  7, 20, false);
					}
				}
				else
				{
					if (enemy[i].ey > -13 - ENEMY_DRAW_MARGIN)
						blit_enemy(VGAScreen, i, 0, 0, 0, false);
				}

				enemy[i].filter = 0;
				enemyFlashLift = 0;
			}

			if (enemy[i].excc)
			{
				if (--enemy[i].exccw <= 0)
				{
					if (enemy[i].exc == enemy[i].exrev)
					{
						enemy[i].excc = -enemy[i].excc;
						enemy[i].exrev = -enemy[i].exrev;
						enemy[i].exccadd = -enemy[i].exccadd;
					}
					else
					{
						enemy[i].exc += enemy[i].exccadd;
						enemy[i].exccw = enemy[i].exccwmax;
						if (enemy[i].exc == enemy[i].exrev)
						{
							enemy[i].excc = -enemy[i].excc;
							enemy[i].exrev = -enemy[i].exrev;
							enemy[i].exccadd = -enemy[i].exccadd;
						}
					}
				}
			}

			if (enemy[i].eycc)
			{
				if (--enemy[i].eyccw <= 0)
				{
					if (enemy[i].eyc == enemy[i].eyrev)
					{
						enemy[i].eycc = -enemy[i].eycc;
						enemy[i].eyrev = -enemy[i].eyrev;
						enemy[i].eyccadd = -enemy[i].eyccadd;
					}
					else
					{
						enemy[i].eyc += enemy[i].eyccadd;
						enemy[i].eyccw = enemy[i].eyccwmax;
						if (enemy[i].eyc == enemy[i].eyrev)
						{
							enemy[i].eycc = -enemy[i].eycc;
							enemy[i].eyrev = -enemy[i].eyrev;
							enemy[i].eyccadd = -enemy[i].eyccadd;
						}
					}
				}
			}

			// Scale only layer-bound fixed movement from the batch's actual layer delta.
			enemy[i].ey += enemy_fixed_move_y(i);

			enemy[i].ex += enemy[i].exc;
			if (enemy[i].ex < -80 || enemy[i].ex > vga_width + 20)
				goto enemy_gone;

			enemy_approach_debt_accrue(i);

			enemy[i].ey += enemy[i].eyc;
			if (enemy[i].ey < -112 || enemy[i].ey > 190)
				goto enemy_gone;

			goto enemy_still_exists;

enemy_gone:
			/* enemy[i].egr[10] &= 0x00ff; <MXD> madness? */
			enemyAvail[i] = 1;
			goto draw_enemy_end;

enemy_still_exists:

			/*X bounce*/
			if (enemy[i].ex <= enemy[i].xminbounce || enemy[i].ex >= enemy[i].xmaxbounce)
				enemy[i].exc = -enemy[i].exc;

			/*Y bounce*/
			if (enemy[i].ey <= enemy[i].yminbounce || enemy[i].ey >= enemy[i].ymaxbounce)
				enemy[i].eyc = -enemy[i].eyc;

			/* Evalue != 0 - score item at boundary */
			// Clamp pickups to the visible widened playfield.
			if (enemy[i].scoreitem)
			{
				if (enemy[i].ex < PLAYFIELD_LEFT - 5)
					enemy[i].ex++;
				if (enemy[i].ex > PLAYFIELD_RIGHT - 17)
					enemy[i].ex--;
			}

			// Track the bound layer's smooth overclock rate.
			enemy[i].ey += tempBackMove + tempScrollExtraPx +
			               (skyGlueThisEnemy ? endlessScrollExtraPx2 : 0);

			if (enemy[i].ex <= -24 || enemy[i].ex >= vga_width - 24)
				goto draw_enemy_end;

			JE_integer tempX = enemy[i].ex;
			JE_integer tempY = enemy[i].ey;

			temp = enemy[i].enemytype;

			/* Enemy Shots */
			if (enemy[i].edamaged == 1)
				goto draw_enemy_end;

			enemyOnScreen++;

			if (enemy[i].iced)
			{
				enemy[i].iced--;
				if (enemy[i].enemyground != 0)
				{
					enemy[i].filter = 0x09;
				}
				goto draw_enemy_end;
			}

			for (int j = 3; j > 0; j--)
			{
				if (enemy[i].freq[j-1])
				{
					temp3 = enemy[i].tur[j-1];

					if (--enemy[i].eshotwait[j-1] == 0 && temp3)
					{
						enemy[i].eshotwait[j-1] = enemy[i].freq[j-1];
						if (difficultyLevel > DIFFICULTY_NORMAL)
						{
							enemy[i].eshotwait[j-1] = (enemy[i].eshotwait[j-1] / 2) + 1;
							if (difficultyLevel > DIFFICULTY_MANIACAL)
								enemy[i].eshotwait[j-1] = (enemy[i].eshotwait[j-1] / 2) + 1;
						}

						if (endlessFxActive())
						{
							// Endless: enemies fire faster with depth (lower cooldown = faster);
							// champions fire faster still.
							int fd = endlessFireDelayPercent();
							if (enemy[i].eliteState == 3)
								fd = fd * endlessChampionFireDelayPercent() / 100;
							enemy[i].eshotwait[j-1] = enemy[i].eshotwait[j-1] * fd / 100;
							if (enemy[i].eshotwait[j-1] < 1)
								enemy[i].eshotwait[j-1] = 1;
						}

						if (galagaMode && (enemy[i].eyc == 0 || (mt_rand() % 400) >= galagaShotFreq))
							goto draw_enemy_end;

						switch (temp3)
						{
						case 252: /* Savara Boss DualMissile */
							if (enemy[i].ey > 20)
							{
								JE_setupExplosion(tempX - 8 + tempMapXOfs, tempY - 20 - backMove * 8, -2, 6, false, false);
								JE_setupExplosion(tempX + 4 + tempMapXOfs, tempY - 20 - backMove * 8, -2, 6, false, false);
							}
							break;
						case 251:; /* Suck-O-Magnet */
							const JE_integer attraction = 4 - (abs(player[0].x - tempX) + abs(player[0].y - tempY)) / 100;
							if (attraction > 0)
								player[0].x_velocity += (player[0].x > tempX) ? -attraction : attraction;
							break;
						case 253: /* Left ShortRange Magnet */
							if (abs(player[0].x + 25 - 14 - tempX) < 24 && abs(player[0].y - tempY) < 28)
							{
								player[0].x_velocity += 2;
							}
							if (twoPlayerMode &&
							   (abs(player[1].x - 14 - tempX) < 24 && abs(player[1].y - tempY) < 28))
							{
								player[1].x_velocity += 2;
							}
							break;
						case 254: /* Left ShortRange Magnet */
							if (abs(player[0].x + 25 - 14 - tempX) < 24 && abs(player[0].y - tempY) < 28)
							{
								player[0].x_velocity -= 2;
							}
							if (twoPlayerMode &&
							   (abs(player[1].x - 14 - tempX) < 24 && abs(player[1].y - tempY) < 28))
							{
								player[1].x_velocity -= 2;
							}
							break;
						case 255: /* Magneto RePulse!! */
							if (difficultyLevel != DIFFICULTY_EASY) /*DIF*/
							{
								if (j == 3)
								{
									enemy[i].filter = 0x70;
								}
								else
								{
									const JE_integer repulsion = 4 - (abs(player[0].x - tempX) + abs(player[0].y - tempY)) / 20;
									if (repulsion > 0)
										player[0].x_velocity += (player[0].x > tempX) ? repulsion : -repulsion;
								}
							}
							break;
						default:
						/*Rot*/
							if (cheatNoEnemyFire)  // debug: enemies behave but don't shoot
								break;
							// Revive grace suppresses bullets, not turret cooldowns or non-firing
							// behavior such as magnets and launch puffs.
							if (endlessReviveGraceActive())
								break;
							// `multi` may describe one tiled projectile or beam. Carry tide
							// credit between shots and emit only complete authored volleys.
							const int endlessBaseMulti = weapons[temp3].multi;
							int endlessExtraVolleys = 0;
							if (endlessFxActive() && endlessBaseMulti > 0)
							{
								const int credit = enemy[i].eshotextracredit[j-1] + endlessExtraEnemyShots();
								endlessExtraVolleys = credit / endlessBaseMulti;
								enemy[i].eshotextracredit[j-1] = (JE_byte)(credit % endlessBaseMulti);
							}

							// Only endless draws on the enlarged enemy-shot pool; normal levels keep the
							// original 60-slot cap so they play exactly as before.
							const int enemyShotCap = endlessFxActive() ? ENEMY_SHOT_MAX : ENEMY_SHOT_NORMAL;
							if (endlessFxActive() && endlessBaseMulti > 0)
							{
								// Do not truncate a composite volley when the pool is nearly full.
								int freeShots = 0;
								for (int slot = 0; slot < enemyShotCap; ++slot)
									if (enemyShotAvail[slot] == 1)
										++freeShots;

								const int completeVolleys = freeShots / endlessBaseMulti;
								if (completeVolleys == 0)
									goto draw_enemy_end;
								if (endlessExtraVolleys > completeVolleys - 1)
									endlessExtraVolleys = completeVolleys - 1;
							}

							// Tide fan lean: held for a full second of game time instead of
							// alternating per volley, so a burst reads as one sweep.
							const int endlessFanPhase = endlessFanPhaseNow();

							const int endlessVolley = endlessBaseMulti * (1 + endlessExtraVolleys);
							int baseShotSlots[WEAPON_MULTI_MAX];
							for (int shotNum = 0; shotNum < endlessVolley; shotNum++)
							{
								for (b = 0; b < enemyShotCap; b++)
								{
									if (enemyShotAvail[b] == 1)
										break;
								}
								if (b == enemyShotCap)
									goto draw_enemy_end;

								enemyShotAvail[b] = !enemyShotAvail[b];

								if (shotNum >= endlessBaseMulti)
								{
									// Clone the current volley without advancing its weapon pattern.
									// One fan angle keeps all of a composite shot's parts together.
									const int component = shotNum % endlessBaseMulti;
									enemyShot[b] = enemyShot[baseShotSlots[component]];

									const int fanK = shotNum / endlessBaseMulti - 1;
									const int fanOrder = endlessFanPhase + fanK;
									const float fanAng = ((fanOrder & 1) ? -1.0f : 1.0f) * (fanK / 2 + 1) * 0.20f;
									const float fc = sim_cosf(fanAng), fs = sim_sinf(fanAng);
									const int ox = enemyShot[b].sxm, oy = enemyShot[b].sym;
									enemyShot[b].sxm = roundf(ox * fc - oy * fs);
									enemyShot[b].sym = roundf(ox * fs + oy * fc);
									if (enemyShot[b].sxm == 0 && enemyShot[b].sym == 0)
									{
										enemyShot[b].sxm = ox;
										enemyShot[b].sym = oy;
									}
									continue;
								}

								baseShotSlots[shotNum] = b;

								if (weapons[temp3].sound > 0)
								{
									do
									{
										temp = mt_rand() % 8;
									} while (temp == 3);
									soundQueue[temp] = weapons[temp3].sound;
								}

								if (enemy[i].aniactive == 2)
									enemy[i].aniactive = 1;

								if (++enemy[i].eshotmultipos[j-1] > weapons[temp3].max)
									enemy[i].eshotmultipos[j-1] = 1;

								int tempPos = enemy[i].eshotmultipos[j-1] - 1;

								if (j == 1)
									temp2 = 4;

								enemyShot[b].sx = tempX + weapons[temp3].bx[tempPos] + tempMapXOfs;
								enemyShot[b].sy = tempY + weapons[temp3].by[tempPos];
								enemyShot[b].sdmg = weapons[temp3].attack[tempPos];
								enemyShot[b].tx = weapons[temp3].tx;
								enemyShot[b].ty = weapons[temp3].ty;
								enemyShot[b].duration = weapons[temp3].del[tempPos];
								enemyShot[b].animate = 0;
								enemyShot[b].animax = weapons[temp3].weapani;

								enemyShot[b].sgr = weapons[temp3].sg[tempPos];
								enemyShot[b].seekerArm = 0;
								enemyShot[b].seekerLeft = 0;
								// An elite or champion fires in its own bank, so its bullets read as
								// dangerous as the body they came from. 0 outside an endless run.
								enemyShot[b].filter = endlessEliteTint(enemy[i].eliteState);
								switch (j)
								{
								case 1:
									enemyShot[b].syc = weapons[temp3].acceleration;
									enemyShot[b].sxc = weapons[temp3].accelerationx;

									enemyShot[b].sxm = weapons[temp3].sx[tempPos];
									enemyShot[b].sym = weapons[temp3].sy[tempPos];
									break;
								case 3:
									enemyShot[b].sxc = -weapons[temp3].acceleration;
									enemyShot[b].syc = weapons[temp3].accelerationx;

									enemyShot[b].sxm = -weapons[temp3].sy[tempPos];
									enemyShot[b].sym = -weapons[temp3].sx[tempPos];
									break;
								case 2:
									enemyShot[b].sxc = weapons[temp3].acceleration;
									enemyShot[b].syc = -weapons[temp3].acceleration;

									enemyShot[b].sxm = weapons[temp3].sy[tempPos];
									enemyShot[b].sym = -weapons[temp3].sx[tempPos];
									break;
								}

								if (weapons[temp3].aim > 0)
									enemy_shot_aim(b, weapons[temp3].aim, tempX, tempY, &temp);

								if (endlessFxActive())
								{
									enemyShot[b].seekerLeft = endlessSeekerPasses();
									if (enemyShot[b].seekerLeft > 0)
										enemyShot[b].seekerArm = 1;

									// Scale both projectile axes and round nonzero motion away from zero.
									int spct = endlessShotSpeedPercent();
									enemyShot[b].sxm = (enemyShot[b].sxm * spct + (enemyShot[b].sxm >= 0 ? 50 : -50)) / 100;
									enemyShot[b].sym = (enemyShot[b].sym * spct + (enemyShot[b].sym >= 0 ? 50 : -50)) / 100;

									// Round projectile damage to nearest and clamp it to one byte.
									int dpct = endlessShotDamagePercent();
									if (enemy[i].eliteState == 3)
										dpct = dpct * endlessChampionShotDamagePercent() / 100;
									int dmg = (enemyShot[b].sdmg * dpct + 50) / 100;
									enemyShot[b].sdmg = (JE_byte)(dmg > 255 ? 255 : dmg);

								}
							}
							break;
						}
					}
				}
			}

			/* Enemy Launch Routine */
			if (enemy[i].launchfreq)
			{
				if (--enemy[i].launchwait == 0)
				{
					enemy[i].launchwait = enemy[i].launchfreq;

					if (enemy[i].launchspecial != 0)
					{
						/*Type  1 : Must be inline with player*/
						if (abs(enemy[i].ey - player[0].y) > 5)
							goto draw_enemy_end;
					}

					if (enemy[i].aniactive == 2)
					{
						enemy[i].aniactive = 1;
					}

					if (enemy[i].launchtype == 0)
						goto draw_enemy_end;

					tempW = enemy[i].launchtype;
					b = JE_newEnemy(enemyOffset == 50 ? 75 : enemyOffset - 25, tempW, 0);

					/*Launch Enemy Placement*/
					if (b > 0)
					{
						struct JE_SingleEnemyType* e = &enemy[b-1];

						e->ex = tempX;
						e->ey = tempY + enemyDat[e->enemytype].startyc;
						if (e->size == 0)
							e->ey -= 7;

						if (e->launchtype > 0 && e->launchfreq == 0)
						{
							if (e->launchtype > 90)
							{
								e->ex += mt_rand() % ((e->launchtype - 90) * 4) - (e->launchtype - 90) * 2;
							}
							else
							{
								JE_integer aimX = (player[0].x + 25) - tempX - tempMapXOfs - 4;
								if (aimX == 0)
									aimX = 1;
								JE_integer aimY = player[0].y - tempY;
								if (aimY == 0)
									aimY = 1;
								const JE_integer maxMagAim = MAX(abs(aimX), abs(aimY));
								e->exc = roundf((float)aimX / maxMagAim * e->launchtype);
								e->eyc = roundf((float)aimY / maxMagAim * e->launchtype);
							}
						}

						do
						{
							temp = mt_rand() % 8;
						} while (temp == 3);
						soundQueue[temp] = randomEnemyLaunchSounds[(mt_rand() % 3)];

						if (enemy[i].launchspecial == 1 &&
						    enemy[i].linknum < 100)
						{
							e->linknum = enemy[i].linknum;
						}
					}
				}
			}
		}
draw_enemy_end:
		;
	}

	player[0].x += 25;
}

/* Choose the LAST LEVEL slot from session type, not transient twoPlayerMode. */
JE_byte backup_save_slot(void)
{
	return (twoPlayerMode || isNetworkGame) ? 22 : 11;
}

/* Draw and claim one opaque warning strip for residual replay. The lettering stays unclaimed
 * because its bounding box contains playfield pixels that must keep interpolating. */
static void warning_bar(int x0, int y0, int x1, int y1, Uint8 col)
{
	fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, col);
	rl_mark_overlay_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

static void draw_background_2_layer(void)
{
	if ((smoothies[2-1] && processorType < 4) || (smoothies[1-1] && processorType == 3))
		skip_background_2();
	else if (wild && !background2notTransparent)
		draw_background_2_blend(VGAScreen);
	else
		draw_background_2(VGAScreen);
}

void JE_main(void)
{
	char buffer[256];

	int lastEnemyOnScreen;

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: JE_main entered\n");
		fflush(stderr);
	}

	/* Initial Endless outpost. */
	if (endlessMode)
	{
		// Endless opens its first shop before loading zone one.
		endlessBetweenLevels();

		// Player chose Quit Game in the shop instead of charting a course: end the run and
		// return to the title. On quit the shop leaves mainLevel == 0 (and has already faded out).
		if (mainLevel == 0)
		{
			endlessEndRunToTitle();  // hardcore shows the Run Over summary first
			return;
		}
	}

	goto start_level_first;

	/* Level transition. */

	/* Startlevel is called after a previous level is over.  If the first level
	   is started for a gaming session, startlevelfirst is called instead and
	   this code is skipped.  The code here finishes the level and prepares for
	   the loadmap function. */

start_level:

	// Leaving the level loop: whatever rollback machinery was live is done.
	rollback_level_end();

	// A script's music fade (event 34) drops the MASTER volume and is normally undone by the event
	// 35 that follows it.
	musicFade = false;
	set_volume(tyrMusicVolume, fxVolume);

	mouseSetRelative(false);

	if (galagaMode && !coop_mode_active())
		twoPlayerMode = false;

	JE_clearKeyboard();

	free_enemy_sprite_sheets();

	/* Normal speed */
	if (fastPlay != 0)
	{
		smoothScroll = true;
		Uint16 speed = 0x4300;
		setDelaySpeed(speed);
	}

	if (play_demo || record_demo)
	{
		close_demo_file();

		if (play_demo)
		{
			moveTyrianLogoUp = true;
			stop_song();
			fade_black(10);

			wait_noinput(true, true, true);
			newkey = false;
			newmouse = false;
		}
	}

	if (difficulty_adjust_active())
		difficultyLevel = oldDifficultyLevel;   /*Return difficulty to normal*/

	if (!play_demo)
	{
		// Quit Level restores the launch snapshot without clearing or advancing the zone.
		if (endlessMode && endlessQuitToOutpost && endlessSortieValid())
		{
			endlessQuitToOutpost = false;
			fade_song();
			fade_black(10);
			endlessRestoreSortie();   // revert player loadout + endless state; arms the locked outpost
			endlessBetweenLevels();   // reopens the shop (locked); sets mainLevel on relaunch, 0 on quit-run
			if (mainLevel == 0)          // player chose Quit Game in the locked outpost: end the run and
			{                            // return to the title (hardcore shows the Run Over summary first,
				endlessEndRunToTitle();  // since a hardcore quit is as final as a death).
				return;
			}
			goto start_level_first;   // re-run the same level (endlessCaptureSortie re-snapshots + clears the lock)
		}

		// Was the level that just ran picked straight out of the debug level browser? Consume the
		// flag whatever the answer, so it can never carry over to a campaign-reached level.
		const bool fromDebugBrowser = debugLevelJumpTake();

		const bool cleared = (!all_players_dead() || normalBonusLevelCurrent || bonusLevelCurrent) && !playerEndLevel;

		// ENGAGE quits and deaths return to the originating outpost. Cleared TIME WAR
		// continues into SuperTyrian on both peers.
		if (fromDebugBrowser && engageMode && !(cleared && !galagaMode))
		{
			if (cleared)
				JE_endLevelAni();
			fade_song();
			if (!cleared)
				fade_black(10);

			debugLevelJumpReturn();
			// Another level reached without passing through the outpost; same handshake owed.
			network_level_rendezvous();
			goto start_level_first;
		}

		// Empty online ENGAGE rounds return through the quit path.
		const bool engageGalagaEnd = engageMode && galagaMode && coop_mode_active();

		if (cleared && !engageGalagaEnd)
		{
			if (qa_net_gameplay_ticks > 0)
			{
				++qa_net_zones_cleared;
				fprintf(stderr, "net gameplay: level cleared (%d of %d)\n",
				        qa_net_zones_cleared, qa_net_zones);
				fflush(stderr);
			}

			if (endlessMode)
			{
				endlessRunDepth++;
				endlessOnSectorCleared();  // bank the boons that pay out at the NEXT outpost (Star Charts / Breakthrough)
			}
			else
			{
				mainLevel = nextLevel;

				/* Use the episode's ]Q transition before the final scheduled campaign level. */
				if (qa_net_gameplay_ticks > 0 && qa_net_zones > 0 && episodeNum == 1
				    && network_game_type == NETWORK_GAME_CAMPAIGN
				    && qa_net_zones_cleared == qa_net_zones - 1)
				{
					mainLevel = 26;
					fprintf(stderr, "net gameplay: driving the episode transition\n");
					fflush(stderr);
				}
			}

			JE_endLevelAni();  // level-complete screen first...

			fade_song();

			if (endlessMode)
			{
				endlessBetweenLevels();  // ...then the between-level shop, whose Start Level submenu is the
				                         // course choice: it sets mainLevel + the mutators and launches.

				// Player chose Quit Game instead of a course: end the run, back to the title.
				if (mainLevel == 0)
				{
					endlessEndRunToTitle();  // hardcore shows the Run Over summary first
					return;
				}
			}
		}
		else
		{
			// Endless death in Relaxed: the frozen death frame gets a choice before the run summary.
			EndlessDeathChoice deathPick = ENDLESS_DEATH_END_RUN;

			// The blocking death menu fades and restores its own music. Endless keeps
			// the track for the run summary, which handles its own fade.
			if (endlessDeathMenuDue() && all_players_dead())
			{
				// One run, so one decision: the host chooses for the pair and the joiner adopts it.
				if (endlessCoop() && thisPlayerNum != networkHostPlayerNum)
				{
					// Draw the joiner's modal wait over the frozen death frame.
					SDL_Surface *const death_surface = VGAScreen;
					VGAScreen = VGAScreenSeg;   /* side-effect of game_screen; display space */
					JE_drawNetworkNotice("Waiting for the host to choose.");
					JE_showVGA();
					const int adopted = network_endless_death_sync(-1);
					VGAScreen = death_surface;
					deathPick = (adopted >= 0 && adopted <= ENDLESS_DEATH_END_RUN)
					          ? (EndlessDeathChoice)adopted : ENDLESS_DEATH_END_RUN;
				}
				else
				{
					deathPick = JE_endlessDeathMenu();
					if (endlessCoop())
						network_endless_death_sync((int)deathPick);
				}
			}
			else if (!endlessMode)
				fade_song();

			fade_black(10);

			if (endlessMode && deathPick == ENDLESS_DEATH_RESTART)
			{
				endlessRestartSortie();  // revert to the launch snapshot and re-arm the same zone

				// This path reaches a level without going through the outpost, so it owes the
				// level-start handshake the shop would otherwise have done.
				if (endlessCoop())
					network_level_rendezvous();

				// Force a same-song retry to reload after start_level_first fades the current track.
				clear_song_selection();
				goto start_level_first;
			}

			if (endlessMode)
			{
				// Return to Outpost reverts to the same launch-time snapshot, then reopens the shop
				// the way Quit Level does.
				if (deathPick == ENDLESS_DEATH_OUTPOST)
				{
					endlessRestoreSortie();
					endlessBetweenLevels();
					if (mainLevel == 0)  // player chose Quit Game in the reopened outpost
					{
						endlessEndRunToTitle();
						return;
					}
					goto start_level_first;
				}

				endlessOnRunEnd();
				endlessMode = false;
				mainLevel = 0;
				return;
			}

			if (timedBattleMode)
			{
				// Both racers went down before the clock did; the run still had a winner, and
				// this path never reaches the level-complete tally that would have shown it.
				if (isNetworkGame)
					JE_timedBattleResult();
				mainLevel = 0;
				return;
			}

			// A campaign death retries the level. This reload puts both wallets back to their
			// level-start values, so nothing is banked or recorded here.
			JE_loadGame(backup_save_slot());
			if (doNotSaveBackup)
			{
				superTyrian = false;
				onePlayerAction = false;
				player[0].items.super_arcade_mode = SA_NONE;
			}
			if (bonusLevelCurrent && !playerEndLevel)
			{
				mainLevel = nextLevel;
			}
		}
	}
	doNotSaveBackup = false;

	if (play_demo)
		return;

start_level_first:

	set_volume(tyrMusicVolume, fxVolume);

	endLevel = false;
	reallyEndLevel = false;
	playerEndLevel = false;
	extraGame = false;

	doNotSaveBackup = false;
	JE_loadMap();

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: JE_loadMap returned (mainLevel %d)\n", (int)mainLevel);
		fflush(stderr);
	}

	if (mainLevel == 0)  // if quit itemscreen
		return;          // back to titlescreen

	if (isNetworkGame)
		JE_loadItemDat();

	// A run loaded from the shop resumes at its saved outpost.
	if (endlessMode && endlessResumePending())
	{
		gameLoaded = false;      // consumed; don't let the shop see it and close instantly
		endlessBetweenLevels();  // reopens the SAVED outpost snapshot (no reroll)
		if (mainLevel == 0)      // player quit the outpost
		{
			endlessEndRunToTitle();  // hardcore shows Run Over first (a resumed run is never hardcore, but keep it uniform)
			return;
		}
		goto start_level_first;
	}

	if (endlessMode)
	{
		endlessRegenerateLevel();
		endlessCaptureSortie();  // snapshot the launch-time loadout + committed level for a possible Quit Level retry
		endlessNoteZoneReached(endlessRunDepth + 1);  // launching a zone IS reaching it: advance the all-time record
		if (qa_net_gameplay_ticks > 0)
		{
			fprintf(stderr, "net gameplay: endless level regenerated\n");
			fflush(stderr);
		}
	}
	else
	{
		if (isNetworkGame)
			mt_srand(32402394u + mainLevel);
		endlessCampaignLevelStart();  // debug campaign mods: the effect layer's per-level reset (no-op when off)
	}

	crashlog_set_phase("playing level");

	if (!play_demo)
		mouseSetRelative(true);

	fade_song();

	for (uint i = 0; i < COUNTOF(player); ++i)
		player[i].is_alive = true;

	oldDifficultyLevel = difficultyLevel;
	if (difficulty_adjust_active())
	{
		if (episodeNum == EPISODE_AVAILABLE)
			difficultyLevel--;
		if (difficultyLevel < DIFFICULTY_EASY)
			difficultyLevel = DIFFICULTY_EASY;
	}

	player[0].x = 100;
	player[0].y = 180;

	player[1].x = 190;
	player[1].y = 180;

	assert(COUNTOF(player->old_x) == COUNTOF(player->old_y));

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		for (uint j = 0; j < COUNTOF(player->old_x); ++j)
		{
			player[i].old_x[j] = player[i].x - (19 - j);
			player[i].old_y[j] = player[i].y - 18;
		}

		player[i].last_x_shot_move = player[i].x;
		player[i].last_y_shot_move = player[i].y;
		player[i].last_x_explosion_follow = player[i].x;
		player[i].last_y_explosion_follow = player[i].y;
		player[i].x_friction_ticks = 0;
		player[i].y_friction_ticks = 0;
	}
	
	JE_loadPic(VGAScreen, split_arcade_mode() ? 6 : 3, false);

	// Relocate the HUD to the new right edge when the playfield is wider
	const int hud_shift = vga_width - LEGACY_WIDTH;
	if (hud_shift > 0)
	{
		for (int y = 0; y < vga_height; ++y)
		{
			Uint8* row = (Uint8*)VGAScreen->pixels + y * VGAScreen->pitch;
			memmove(row + PLAYFIELD_WIDTH, row + LEGACY_WIDTH - HUD_WIDTH, HUD_WIDTH);
			memset(row + LEGACY_WIDTH - HUD_WIDTH, 0, hud_shift);
		}
	}

	JE_drawOptions();

	// Two-player HUD: left-align the name exactly as the one-player HUD does, only nudged off the
	// classic y76 so it sits vertically centred in its black readout instead of low against the frame.
	int nameX = HUD_X(268), nameY = 118;
	if (split_arcade_mode())
	{
		enum { BOX_Y = 71, BOX_H = 12, TEXT_H = 6 };
		nameY = BOX_Y + (BOX_H - TEXT_H) / 2;
	}
	JE_outText(VGAScreen, nameX, nameY, levelName, 12, 4);
	JE_drawPlayerTags();

	// Ensure the widened playfield blends into the HUD before the fade-in
	// so no remnants of the old HUD position appear during level start.
	extend_playfield_right_column(VGAScreen);

	JE_showVGA();
	JE_gammaCorrect(&colors, gammaCorrection);
	fade_palette(colors, 50, 0, 255);

	if (explosionSpriteSheet.data == NULL)
		JE_loadCompShapes(&explosionSpriteSheet, '6');

	/* MAPX will already be set correctly */
	mapY = 300 - 8;
	mapY2 = 600 - 8;
	mapY3 = 600 - 8;
	mapYPos = &megaData1.mainmap[mapY][0] - 1;
	mapY2Pos = &megaData2.mainmap[mapY2][0] - 1;
	mapY3Pos = &megaData3.mainmap[mapY3][0] - 1;
	mapXPos = 0;
	mapXOfs = 0;
	mapX2Pos = 0;
	mapX3Pos = 0;
	mapX3Ofs = 0;
	mapXbpPos = 0;
	mapX2bpPos = 0;
	mapX3bpPos = 0;

	map1YDelay = 1;
	map1YDelayMax = 1;
	map2YDelay = 1;
	map2YDelayMax = 1;

	musicFade = false;

	backPos = 0;
	backPos2 = 0;
	backPos3 = 0;
	power = 0;
	starfield_speed = 1;

	/* Setup player ship graphics */
	JE_getShipInfo();

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		player[i].x_velocity = 0;
		player[i].y_velocity = 0;

		player[i].invulnerable_ticks = 100;
	}

	newkey = newmouse = false;

	/* Initialize Level Data and Debug Mode */
	levelEnd = 255;
	levelEndWarp = -4;
	levelEndFxWait = 0;
	warningCol = 120;
	warningColChange = 1;
	warningSoundDelay = 0;
	armorShipDelay = 50;

	bonusLevel = false;
	readyToEndLevel = false;
	firstGameOver = true;
	eventLoc = 1;
	curLoc = 0;
	eventScrollCatchupValid = false;
	eventScrollSkyValid = false;
	backMove = 1;
	backMove2 = 2;
	backMove3 = 3;
	explodeMove = 2;
	enemiesActive = true;
	for (temp = 0; temp < 3; temp++)
	{
		button[temp] = false;
	}
	stopBackgrounds = false;
	stopBackgroundNum = 0;
	mapStopStallTicks = 0;
	background3x1   = false;
	background3x1b  = false;
	background3over = 0;
	background2over = 1;
	topEnemyOver = false;
	skyEnemyOverAll = false;
	smallEnemyAdjust = false;
	starActive = true;
	enemyContinualDamage = false;
	levelEnemyFrequency = 96;
	quitRequested = false;

	for (unsigned int i = 0; i < COUNTOF(boss_bar); i++)
		boss_bar[i].link_num = 0;

	memset(enemyArmedFlashAt, 0, sizeof(enemyArmedFlashAt));

	forceEvents = false;  /*Force events to continue if background movement = 0*/

	superEnemy254Jump = 0;   /*When Enemy with PL 254 dies*/

	/* Filter Status */
	filterActive = true;
	filterFade = true;
	filterFadeStart = false;
	levelFilter = -99;
	levelBrightness = -14;
	levelBrightnessChg = 1;
	flareOwnsFilter = false;  // this one is the level's fade-in, so Special Tint doesn't touch it

	background2notTransparent = false;

	uint old_weapon_bar[2] = { 0, 0 };  // only redrawn when they change

	/* Initially erase power bars */
	lastPower = power / 10;

	/* Initial Text */
	JE_drawTextWindow(miscText[20]);

	/* Setup Armor/Shield Data */
	shieldWait = 1;
	shieldT    = shields[player[0].items.shield].tpwr * 20;

	// Endless SHIELDLESS/DEADGEN sectors never recharge the shield, so hand it over fully charged:
	// you get the whole buffer up front, then fly on armor once it's spent (no way to earn it back).
	const bool startShieldFull = endlessShieldRegenOff();

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		// Start at half of the Arcade lives-scaled ceiling, which is
		// just the shield item's own mpwr*2 outside the arcade modes.
		player[i].shield_max = arcade_shield_max(&player[i]);
		player[i].shield     = startShieldFull ? player[i].shield_max : player[i].shield_max / 2;
		shieldGaugeFlash[i] = armorGaugeFlash[i] = 0;
	}
	JE_resetGaugeRender();  // nothing to interpolate away from on the level's first frames

	JE_drawShield();
	JE_drawArmor();

	// Endless keeps its bought bombs across levels (like cash/armor/perks); campaign resets each level.
	if (!endlessMode)
		for (uint i = 0; i < COUNTOF(player); ++i)
			player[i].superbombs = 0;

	/* Set cubes to 0 */
	cubeMax = 0;

	/* Secret Level Display */
	flash = 0;
	flashChange = 1;
	displayTime = 0;

	play_song(levelSong - 1);

	JE_drawPortConfigButtons();

	/* --- MAIN LOOP --- */

	newkey = false;

	if (isNetworkGame)
	{
		JE_clearSpecialRequests();
		mt_srand(32402394);
	}

	// Use a fixed RNG seed for demo recording and playback so the input stream is reproducible.
	if (play_demo || record_demo)
		mt_srand(32402394);

	initialize_starfield();

	JE_setNewGameSpeed();

	set_volume(tyrMusicVolume, fxVolume);

	/*Save backup game*/
	// Skip this mid-level autosave point for endless: its continue-slot autosave lives at the
	// outpost instead, the one coherent resume point.
	if (!play_demo && !doNotSaveBackup && !timedBattleMode && !endlessMode)
	{
		temp = backup_save_slot();
		JE_saveGame(temp, "LAST LEVEL    ");   // not in endless mode: drops any stale endless half of the slot

		// The backup just written is what the disconnect dialog offers to keep (network.c).
		if (isNetworkGame)
			network_session_saveable = true;
	}

	if (!play_demo && record_demo)
		begin_demo_recording();

	twoPlayerLinked = false;
	link_cue_state = false;
	deathSkipArmed = false;
	deathGameOverTicks = 0;
	linkGunDirec = M_PI;

	for (uint i = 0; i < COUNTOF(player); ++i)
		calc_purple_balls_needed(&player[i]);

	damageRate = 2;  /*Normal Rate for Collision Damage*/

	chargeWait   = 5;
	chargeLevel  = 0;
	chargeMax    = 5;
	chargeGr     = 0;
	chargeGrWait = 3;

	portConfigChange = false;

	/*Destruction Ratio*/
	totalEnemy = 0;
	enemyKilled = 0;

	// A chain wave spends a tick per hop, so one can still be in the air when a level ends. Drop it
	// here rather than let it fire into the next level's enemies from last level's coordinates.
	chain_reset_queue();

	superArcadePowerUp = 1;

	yourInGameMenuRequest = false;

	constantLastX = -1;

	for (uint i = 0; i < COUNTOF(player); ++i)
		player[i].exploding_ticks = 0;

	// After the last thing that can rewrite enemyDat, so the ball pools match this episode.
	JE_buildArcadeBallPools();

	memset(enemyAvail,       1, sizeof(enemyAvail));
	for (uint i = 0; i < COUNTOF(enemyShotAvail); i++)
		enemyShotAvail[i] = 1;

	/*Initialize Shots*/
	memset(playerShotData,   0, sizeof(playerShotData));
	memset(shotAvail,        0, sizeof(shotAvail));
	memset(shotMultiPos,     0, sizeof(shotMultiPos));
	memset(shotRepeat,       1, sizeof(shotRepeat));

	memset(button,           0, sizeof(button));

	memset(globalFlags,      0, sizeof(globalFlags));

	memset(explosions,       0, sizeof(explosions));
	memset(rep_explosions,   0, sizeof(rep_explosions));
	explosion_rumble_reset();

	/* --- Clear Sound Queue --- */
	memset(soundQueue,       0, sizeof(soundQueue));
	soundQueue[3] = V_GOOD_LUCK;

	memset(enemySpriteSheetIds, 0, sizeof(enemySpriteSheetIds));
	memset(enemy,               0, sizeof(enemy));

	if (endlessMode)
		endlessPreloadBanks();  // load starting sprite banks now so early spawns aren't invisible

	// Dormant dispenser bases: the campaign obeys the Gameplay menu toggle; Endless
	// ignores it and asks the zone instead.
	dispenserBasesActive = endlessMode ? endlessDispenserBaseRoll() : restoreBaseDispensers;

	memset(SFCurrentCode,    0, sizeof(SFCurrentCode));
	memset(SFExecuted,       0, sizeof(SFExecuted));

	JE_resetSpecialState();
	hud_special_light_reset();  // the meter starts this level fresh, not mid-recharge from the last
	if (dual_ship_mode())
		coop_ship_runtime_reset();
	for (uint i = 0; i < 2; i++)  /*Launch the Attachments!*/
	{
		optionAttachmentMove[i]   = 0;
		optionAttachmentLinked[i] = true;
		optionAttachmentReturn[i] = false;
	}

	editShip1 = false;
	editShip2 = false;

	memset(smoothies, 0, sizeof(smoothies));

	levelTimer = false;
	randomExplosions = false;

	JE_resetSP();

	returnActive = false;

	galagaShotFreq = 0;

	if (galagaMode)
	{
		difficultyLevel = DIFFICULTY_NORMAL;
	}
	galagaLife = 10000;

	JE_drawOptionLevel();

	// keeps map from scrolling past the top
	BKwrap1 = BKwrap1to = &megaData1.mainmap[1][0];
	BKwrap2 = BKwrap2to = &megaData2.mainmap[1][0];
	BKwrap3 = BKwrap3to = &megaData3.mainmap[1][0];

	// Rendezvous after loading so both peers begin the simulation-driven fade together.
	if (isNetworkGame)
	{
		if (qa_net_gameplay_ticks > 0)
		{
			fprintf(stderr, "net gameplay: level rendezvous\n");
			fflush(stderr);
		}

		SDL_Surface *const save_surface = VGAScreen;
		VGAScreen = VGAScreenSeg;
		network_level_loaded_rendezvous();
		VGAScreen = save_surface;
	}

	// Initialize rollback after finalizing ship spawn positions.
	rollback_level_start();
	// Reset per-tick ship motion at each level boundary.
	vt_ship_pred_reset();
	rl_feedback_reset();
	rl_forget_previous();
	if (isNetworkGame && nrb_active())
		nrb_level_reset();

level_loop:

	// Snapshot before the tick, then apply recorded requests, including during replay.
	nrb_frame_begin();
	rollback_selftest_frame_begin();

	// New sim pass: advance the enemy velocity-hint generation (blit_enemy). The presentation
	// clock skips the silent re-simulation passes, so it counts presented frames instead.
	++rl_enemy_gen;
	if (!rollback_resim_silent)
		++rl_present_gen;

	// Open the spark ring's pass here, ahead of the first spawn, so an abandoned pass can hand
	// its slots back.
	JE_beginSPPass();

	JE_deriveStarShowSpecial();
	boss_bar_fade_flash();

	/*Background Wrapping*/
	// Preserve whole-row overshoot when boosted scroll crosses a wrap; stop wraps remain pinned.
	if (mapYPos <= BKwrap1)
		mapYPos = (BKwrap1to > BKwrap1) ? BKwrap1to - (BKwrap1 - mapYPos) / 14 * 14 : BKwrap1to;
	if (mapY2Pos <= BKwrap2)
		mapY2Pos = (BKwrap2to > BKwrap2) ? BKwrap2to - (BKwrap2 - mapY2Pos) / 14 * 14 : BKwrap2to;
	if (mapY3Pos <= BKwrap3)
		mapY3Pos = (BKwrap3to > BKwrap3) ? BKwrap3to - (BKwrap3 - mapY3Pos) / 15 * 15 : BKwrap3to;

	allPlayersGone = all_players_dead() &&
	                 ((*player[0].lives == 1 && player[0].exploding_ticks == 0) || !arcade_rules_active()) &&
	                 ((*player[1].lives == 1 && player[1].exploding_ticks == 0) || !arcade_rules_active());

	/* Music fade. */
	if (musicFade)
	{
		if (tempVolume > 10)
		{
			tempVolume--;
			set_volume(tempVolume, fxVolume);
		}
		else
		{
			musicFade = false;
		}
	}

	if (!allPlayersGone && levelEnd > 0 && endLevel)
	{
		play_song(9);
		if (musicFade)  // cancelling the ramp isn't enough: the jingle needs the volume back too
		{
			musicFade = false;
			set_volume(tyrMusicVolume, fxVolume);
		}
	}
	else if (!playing && firstGameOver)
	{
		// Loop the selected Endless level track until a script event replaces it.
		if (endlessMode && song_playing == (unsigned int)(levelSong - 1))
			restart_song();
		else
			play_song(levelSong - 1);
	}

	if (!endLevel) // draw HUD
	{
		VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

		extend_playfield_right_column(VGAScreenSeg);

		/* Message bar. */
		// Advance message expiry only on live passes that can erase the old text.
		if (!rollback_resim && textErase > 0 && --textErase == 0)
			blit_sprite(VGAScreenSeg, 16, 189, OPTION_SHAPES, 36);  // in-game message area

		/* Shield generator. */
		if (galagaMode)
		{
			for (uint i = 0; i < COUNTOF(player); ++i)
				player[i].shield = 0;

			// Disable two-player mode if the spawned Dragonwing died. Co-op's second ship
			// is a player with the normal death handling, not a spawned wing.
			if (!coop_mode_active() && (*player[1].lives == 0 || player[1].armor == 0))
				twoPlayerMode = false;

			if (player[0].cash >= galagaLife)
			{
				soundQueue[6] = S_EXPLOSION_11;
				soundQueue[7] = S_SOUL_OF_ZINGLON;

				if (*player[0].lives < ARCADE_LIVES_MAX)
				{
					++(*player[0].lives);
					arcade_rescale_to_lives(&player[0]);
				}
				else
					player_add_cash(&player[0], 1000);

				if (galagaLife == 10000)
					galagaLife = 20000;
				else
					galagaLife += 25000;
			}
		}
		else // not galagaMode
		{
			if (dual_ship_mode())
			{
				// Recharge from each ship's generator and personal effects.
				const bool regenOff  = endlessShieldRegenOff();
				const bool regenFree = endlessShieldRegenFree();
				const uint fxSaved = endlessFxPlayer();
				bool shield_changed = false;
				for (uint i = 0; i < COUNTOF(player); ++i)
				{
					Player *const this_player = &player[i];
					endlessSetFxPlayer(i);
					const int regen_cost = regenFree ? 0 : shields[this_player->items.shield].tpwr * 20;
					const uint add = endlessGeneratorPowerAdd(this_player->generator_power_add);
					this_player->generator_power = (Uint16)MIN(900u, this_player->generator_power + add);

					if (!regenOff && this_player->is_alive && this_player->shield < this_player->shield_max &&
					    this_player->generator_power > regen_cost && --this_player->shield_wait == 0)
					{
						this_player->shield_wait = (Uint8)endlessPerkShieldWait(15);
						this_player->generator_power -= (Uint16)regen_cost;
						++this_player->shield;
						shield_changed = true;
					}
				}
				endlessSetFxPlayer(fxSaved);
				if (shield_changed)
					JE_drawShield();
			}
			else if (twoPlayerMode)
			{
				if (--shieldWait == 0)
				{
					shieldWait = endlessPerkShieldWait(15);  // Shield Matrix perk shortens this in endless (no-op otherwise)

					for (uint i = 0; i < COUNTOF(player); ++i)
					{
						if (player[i].shield < player[i].shield_max && player[i].is_alive)
							++player[i].shield;
					}

					JE_drawShield();
				}
			}
			// endless AUXILIARY REACTOR: the recharge is free this sector, so the generator no longer
			// gates it either; an empty reserve must not stall a recharge that costs nothing.
			else if (player[0].is_alive && player[0].shield < player[0].shield_max
			         && (power > shieldT || endlessShieldRegenFree())
			         && !endlessShieldRegenOff())  // endless SHIELDLESS / DEADGEN: shields never recharge
			{
				if (--shieldWait == 0)
				{
					shieldWait = endlessPerkShieldWait(15);  // Shield Matrix perk shortens this in endless (no-op otherwise)

					if (!endlessShieldRegenFree())
						power -= shieldT;

					++player[0].shield;
					if (player[1].shield < player[0].shield_max)
						++player[1].shield;

					JE_drawShield();
				}
			}
		}

		/* Weapon display. */
		// Eleven gradient slots show effective weapon power. In Arcade this also tracks lives
		// and any rear-gun scaling; the wider two-player bar keeps the same slot count.
		for (uint i = 0; i < 2; ++i)
		{
			const uint hud_player = gameplay_local_player_index();
			uint item_power = arcade_weapon_power(&player[dual_ship_mode() ? hud_player : (twoPlayerMode ? i : 0)], i);

			if (old_weapon_bar[i] != item_power)
			{
				old_weapon_bar[i] = item_power;

				enum { WEAPON_BAR_SLOTS = 11 };
				const int width  = split_arcade_mode() ? 28 : WEAPON_BAR_SLOTS * 2,
				          height = split_arcade_mode() ? 3 : 2;  // rows y .. y + height

				const int x = HUD_X(split_arcade_mode() ? 284 : 289),
				          y = (i == 0) ? (split_arcade_mode() ? 5 : 17) : (split_arcade_mode() ? 99 : 38);

				fill_rectangle_xy(VGAScreenSeg, x, y, x + width - 1, y + height, 0);

				if (item_power > WEAPON_BAR_SLOTS)
					item_power = WEAPON_BAR_SLOTS;

				// Fill the three-pixel slot; an outline would leave a black centre.
				for (uint j = 1; j <= item_power; ++j)
				{
					fill_rectangle_xy(VGAScreen, x + (int)(width * (j - 1)) / WEAPON_BAR_SLOTS, y,
					                             x + (int)(width * j) / WEAPON_BAR_SLOTS - 1, y + height, 115 + j); /* SEGa000 */
				}
			}
		}

		/* Power bar. */
		if (arcade_rules_active())
		{
			power = 900;
			power_gauge_active = false;

			// Preserve infinite power when generator state is stored per ship.
			if (dual_ship_mode())
				for (uint p = 0; p < COUNTOF(player); ++p)
					player[p].generator_power = 900;

			// One-player Arcade modes use the unused generator gauge for lives; two-ship
			// sessions show this machine's own ship.
			if (dual_ship_mode() || onePlayerAction)
			{
				const uint ship = dual_ship_mode() ? gameplay_local_player_index() : 0;
				draw_lives_gauge(player_is_out(ship) ? 0 : (int)*player[ship].lives);
			}
		}
		else if (coop_mode_active())
		{
			const uint hud_player = gameplay_local_player_index();
			power = player[hud_player].generator_power;
			powerAdd = player[hud_player].generator_power_add;
			power_render_prev = power_render_cur;
			power_render_cur = (int)power;
			salvo_render_prev = salvo_render_cur;
			salvo_render_cur = local_salvo_gauge_percent();
			power_gauge_active = true;
			lastPower = power / 10;
			draw_power_gauge(VGAScreenSeg, 1, (float)power, salvo_render_cur / 100.0f);
		}
		else
		{
			// Dead Generator throttles the normal charge rate.
			power += endlessGeneratorPowerAdd(powerAdd);
			if (power > 900)
				power = 900;

			// Track prev/cur tick levels for the present loop to interpolate between;
			// draw now so the non-interpolated path still updates each tick.
			power_render_prev = power_render_cur;
			power_render_cur = (int)power;
			salvo_render_prev = salvo_render_cur;
			salvo_render_cur = local_salvo_gauge_percent();
			power_gauge_active = true;
			lastPower = power / 10;  // keep the legacy counter consistent

			draw_power_gauge(VGAScreenSeg, 1, (float)power, salvo_render_cur / 100.0f);
		}

		oldMapX3Ofs = mapX3Ofs;
		oldMapX3Ofs_f = mapX3Ofs_f;  // matching un-floored mirror (see background.c)

		enemyOnScreen = 0;
	}

	/* use game_screen for all the generic drawing functions */
	VGAScreen = game_screen;

	// Record only passes that may be presented.
	if (!rollback_resim_silent)
		rl_begin_record();

	// Cleared each tick; JE_doSpecialShot re-sets a ship's while its Zinglon blast is live.
	zinglonPillarActive[0] = zinglonPillarActive[1] = false;

	/* Events. */
	while (eventRec[eventLoc-1].eventtime <= curLoc && eventLoc <= maxEvent)
		JE_eventSystem();

	// Rollback waits for the end-of-tick driver to confirm a predicted level end.
	if (isNetworkGame && reallyEndLevel && !nrb_active())
		goto start_level;

	/* SMOOTHIES! */
	JE_checkSmoothies();
	if (anySmoothies)
		VGAScreen = VGAScreen2;  // Smoothie effects use the secondary buffer.

	/* Backgrounds. */
	/* --- BACKGROUND 1 --- */

	// Widen the interpolation margin for scroll faster than one tile per tick.
	bgMarginRows = (endlessFxActive() && endlessScrollBoostActive()) ? 3 : 1;

	if (forceEvents && !backMove)
		curLoc++;
	// Any forceEvents-only increment is a script-timeline step, not terrain movement. Start the
	// catch-up interval after it so a later spawn is never shifted by a stationary background.
	const int eventScrollStartThisTick = (int)curLoc;

	if (map1YDelayMax > 1 && backMove < 2)
		backMove = (map1YDelay == 1) ? 1 : 0;

	/*Draw background*/
	if (astralDuration == 0)
	{
		draw_background_1(VGAScreen);
	}
	else
	{
		// Publish layer 1's pan phase even when Astral Zone blanks the layer.
		JE_clr256(VGAScreen);
		bg_publish_layer_1_phase();
	}

	/*Set Movement of background 1*/
	// Actual layer-1 scroll this tick; zero on delay-gated ticks.
	int base1ScrollPx = 0;
	if (--map1YDelay == 0)
	{
		map1YDelay = map1YDelayMax;

		curLoc += backMove;

		backPos += backMove;
		base1ScrollPx = backMove;

		if (backPos > 27)
		{
			backPos -= 28;
			mapY--;
			mapYPos -= 14;  /*Map Width*/
		}
	}

	// Publish the previous tick's layer rate to match the list's pre-advance position.
	for (int L = 1; L <= 3; ++L)
	{
		bg_layer_dy[L]    = bgSmoothRatePend[L];
		bg_layer_yfrac[L] = bgSmoothFracPend[L];
	}
	bg_smooth_y_active = bgSmoothActivePend;

	// Smooth every layer to its true average scroll rate so a delay-gated slow section (event 3:
	// layer 1 1px/3 ticks, layer 2 1px/2 ticks) slides sub-pixel instead of freezing then jumping.
	{
		int fire1 = (map1YDelayMax > 1 && backMove < 2) ? 1 : (int)backMove;
		endlessScrollExtraPx1 = endlessScrollExtraPx(0, fire1, map1YDelayMax, base1ScrollPx,
		                                             &bgSmoothRatePend[1], &bgSmoothFracPend[1]);
		eventScrollBaseStep[1] = fire1;
		eventScrollDelayMax[1] = map1YDelayMax;

		int fire2 = (map2YDelayMax > 1 && backMove2 < 2) ? 1 : (int)backMove2;
		int base2 = (map2YDelay == 1) ? fire2 : 0;
		endlessScrollExtraPx2 = endlessScrollExtraPx(1, fire2, map2YDelayMax, base2,
		                                             &bgSmoothRatePend[2], &bgSmoothFracPend[2]);

		// Capture the post-update sky-glue anchor in exact hundredths.
		eventScrollSkyValid = fire1 > 0 && fire2 > 0;
		if (eventScrollSkyValid)
		{
			eventScrollSkyRatio100 = 100 * fire2 * map1YDelayMax / (fire1 * map2YDelayMax);
			const int c1 = (int)lroundf(bgSmoothFracPend[1] * 100.0f);
			const int c2 = (int)lroundf(bgSmoothFracPend[2] * 100.0f);
			eventScrollSkyPhase100 = eventScrollSkyRatio100 * c1 / 100 - c2;
		}

		endlessScrollExtraPx3 = endlessScrollExtraPx(2, (int)backMove3, 1, (int)backMove3,
		                                             &bgSmoothRatePend[3], &bgSmoothFracPend[3]);
		eventScrollBaseStep[3] = (int)backMove3;
		eventScrollDelayMax[3] = 1;
	}
	bgSmoothActivePend = true;

	// Layer 3 publishes its current rate because it advances before recording rows.
	for (int L = 1; L <= 3; ++L)
	{
		bg_layer_yfrac_now[L] = bgSmoothFracPend[L];
		bg_layer_dy_now[L]    = bgSmoothRatePend[L];
	}

	// Layer 1 (+ the event pointer, which must ride the identical delta so scripted stops stay
	// aligned to the terrain). One combined advance; the wrap can cross more than one tile.
	curLoc += endlessScrollExtraPx1;
	eventScrollFrom = eventScrollStartThisTick;
	eventScrollTo = (int)curLoc;
	eventScrollLayerDelta[1] = eventScrollTo - eventScrollFrom;
	eventScrollLayerDelta[3] = (int)backMove3 + endlessScrollExtraPx3;
	eventScrollBoost = endlessScrollBoostPercent();
	eventScrollCatchupValid = eventScrollBoost > 0 && eventScrollTo > eventScrollFrom;
	backPos += endlessScrollExtraPx1;
	while (backPos > 27)
	{
		backPos -= 28;
		mapY--;
		mapYPos -= 14;
	}

	if (starActive || astralDuration > 0)
		update_and_draw_starfield(VGAScreen, starfield_speed);

	if (processorType > 1 && smoothies[5-1])
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_ICED_BLUR);
		iced_blur_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}

	/* Background layer 2. */
	if (background2over == 3)
	{
		draw_background_2(VGAScreen);
		background2 = true;
	}

	if (background2over == 0)
		draw_background_2_layer();

	if (smoothies[0] && processorType > 2 && smoothie_data[0] == 0)
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_LAVA_FILTER);
		lava_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}
	if (smoothies[2-1] && processorType > 2)
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_WATER_FILTER);
		water_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}

	endlessGroupHoming();

	/* Ground enemies. */
	lastEnemyOnScreen = enemyOnScreen;

	tempMapXOfs = mapXOfs + PLAYFIELD_X_SHIFT;
	tempMapXOfs_frac = mapXOfs_f - mapXOfs;
	tempMapXOfs_layer = 1;
	tempBackMove = backMove;
	tempScrollExtraPx  = endlessScrollExtraPx1;  // this batch rides layer 1
	tempScrollYLayer   = 1;
	tempScrollBaseStep = (map1YDelayMax > 1 && backMove < 2) ? 1 : (int)backMove;
	tempScrollDelayMax = map1YDelayMax;
	// Pull post-advance health bars back to layer 1's recorded phase.
	tempScrollYBase    = 0;
	tempScrollYfrac    = bg_layer_yfrac[1];
	tempScrollYBaseBar = -(tempBackMove + tempScrollExtraPx);
	tempScrollYfracBar = bg_layer_yfrac[1];
	JE_drawEnemy(50);
	JE_drawEnemy(100);

	if (enemyOnScreen == 0 || enemyOnScreen == lastEnemyOnScreen)
	{
		if (stopBackgroundNum == 1)
			stopBackgroundNum = 9;
	}

	if (smoothies[0] && processorType > 2 && smoothie_data[0] > 0)
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_LAVA_FILTER);
		lava_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}

	if (superWild)
	{
		neat += 3;
		JE_darkenBackground(neat);
	}

	/* Background layer 2. */
	if (background2over == 1)
		draw_background_2_layer();

	if (superWild)
	{
		neat++;
		JE_darkenBackground(neat);
	}

	if (background3over == 2)
		draw_background_3(VGAScreen);

	/* New Enemy */
	if (enemiesActive && levelEnemyMax > 0 && mt_rand() % 100 > levelEnemyFrequency)
	{
		tempW = levelEnemy[mt_rand() % levelEnemyMax];
		if (tempW == 2)
			soundQueue[3] = S_WEAPON_7;
		b = JE_newEnemy(0, tempW, 0);
	}

	if (processorType > 1 && smoothies[3-1])
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_ICED_BLUR);
		iced_blur_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}
	if (processorType > 1 && smoothies[4-1])
	{
		if (render_list_recording)
			rl_rec_smoothie_filter(RC_BLUR);
		blur_filter(game_screen, VGAScreen);
		VGAScreen = game_screen;
	}

	/* Draw Sky Enemy */
	if (!skyEnemyOverAll)
	{
		lastEnemyOnScreen = enemyOnScreen;

		tempMapXOfs = mapX2Ofs + PLAYFIELD_X_SHIFT;
		tempMapXOfs_frac = mapX2Ofs_f - mapX2Ofs;
		tempMapXOfs_layer = 2;
		tempBackMove = 0;
		tempScrollExtraPx  = 0;     // layer-2 anchor: any ride is per-enemy via skyGlueThisEnemy
		tempScrollYLayer   = 0;
		tempScrollBaseStep = 0;
		tempScrollDelayMax = 1;
		tempScrollYBase = tempScrollYBaseBar = 0;
		tempScrollYfrac = tempScrollYfracBar = 0.0f;
		JE_drawEnemy(25);

		if (enemyOnScreen == lastEnemyOnScreen)
		{
			if (stopBackgroundNum == 2)
				stopBackgroundNum = 9;
		}
	}

	if (background3over == 0)
		draw_background_3(VGAScreen);

	/* Draw Top Enemy */
	if (!topEnemyOver)
	{
		tempMapXOfs = ((background3x1 == 0) ? oldMapX3Ofs : mapXOfs) + PLAYFIELD_X_SHIFT;
		tempMapXOfs_frac = (background3x1 == 0) ? (oldMapX3Ofs_f - oldMapX3Ofs) : (mapXOfs_f - mapXOfs);
		tempMapXOfs_layer = 3;
		tempBackMove = backMove3;
		tempScrollExtraPx  = endlessScrollExtraPx3;  // this batch rides layer 3
		tempScrollYLayer   = 3;
		tempScrollBaseStep = (int)backMove3;
		tempScrollDelayMax = 1;
		// All enemies are authored and recorded at their pre-advance ey phase.
		tempScrollYBase    = 0;
		tempScrollYfrac    = bg_layer_yfrac[3];
		tempScrollYBaseBar = -(tempBackMove + tempScrollExtraPx);
		tempScrollYfracBar = bg_layer_yfrac[3];
		JE_drawEnemy(75);
	}

	// Cache whether each enemy has an opaque pixel in the visible playfield. This
	// uses collision state, not draw state, so the kill gate stays deterministic.
	bool enemyVisible[COUNTOF(enemy)];
	for (unsigned int ev = 0; ev < COUNTOF(enemy); ev++)
		enemyVisible[ev] = (enemyAvail[ev] == 0) && enemy_has_visible_pixel(ev);

	/* Player Shot Images */
	for (int z = 0; z < MAX_PWEAPON; z++)
	{
		if (shotAvail[z] != 0)
		{
			// The endless pierce lockout ages here; once per bullet per sim tick, immediately
			// before this bullet's own collision pass reads it.
			{
				PlayerShotDataType *ps = &playerShotData[z];
				if (ps->pierceLock > 0)
					--ps->pierceLock;

				// Apply last tick's pierce lock after the current hit pass. Locking during
				// the pass would stop one bullet from crossing overlapping hulls.
				if (ps->pierceLockPending > 0)
				{
					int ticks = ps->pierceLockPending / ENDLESS_PIERCE_LOCK_SCALE;
					int carry = ps->pierceLockCarry + ps->pierceLockPending % ENDLESS_PIERCE_LOCK_SCALE;
					if (carry >= ENDLESS_PIERCE_LOCK_SCALE)
					{
						carry -= ENDLESS_PIERCE_LOCK_SCALE;
						++ticks;
					}
					ps->pierceLockCarry = (JE_byte)carry;
					ps->pierceLockPending = 0;
					if (ticks > 0)
						ps->pierceLock = (JE_byte)ticks;
				}
			}

			// Zinglon uses the reserved last slot without a playerShotData entry. Its branch below
			// supplies these outputs, and safe defaults keep player indexing in range.
			bool is_special = false;
			int tempShotX = 0, tempShotY = 0;
			JE_byte chain = 0;
			JE_byte playerNum = 1;
			JE_word tempX2 = 0, tempY2 = 0;
			JE_integer damage = 0;
			int shotHitDx, shotHitDy;

			if (!player_shot_move_and_draw(z, &is_special, &tempShotX, &tempShotY, &damage, &temp2,
			                               &chain, &playerNum, &tempX2, &tempY2,
			                               &shotHitDx, &shotHitDy))
			{
				goto draw_player_shot_loop_end;
			}

			// Select the projectile collision anchor from the hitbox setting.
			const int shotHitX = tempShotX + shotHitDx;
			const int shotHitY = tempShotY + shotHitDy;

			// Scale real damage from every Endless source. Decode ice and piercing markers before
			// scaling, then restore the marker.
			if (endlessFxActive() && z != MAX_PWEAPON - 1)
			{
				endlessSetFxPlayer(playerShotData[z].playerNumber >= 1
				                   ? (uint)playerShotData[z].playerNumber - 1 : 0);
				int dmgPct = endlessPlayerDamagePercent();
				// Opening Salvo perk: shots tagged as part of a charged volley get an extra bump on top.
				if (playerShotData[z].salvoBoost)
					dmgPct += endlessOpeningSalvoDamagePercent();
				if (damage >= 250)
				{
					// Piercing rounds keep their marker and carry their own remainder, so the
					// run's levers reach a 1-damage round.
					damage = 250 + endlessPierceHitDamage(damage - 250, dmgPct,
					                                      &playerShotData[z].pierceDmgCarry);
				}
				else if (dmgPct != 100 && damage != 99)   // 99 is the ice marker: nothing to scale
				{
					int scaled = (damage * dmgPct + 50) / 100;   // round, don't truncate; see below
					if (damage > 0)
					{
						if (scaled < 1)
							scaled = 1;              // a shot that deals damage never rounds away to none
						// Nonzero percentage damage bonuses add at least one point.
						if (dmgPct > 100 && scaled <= damage)
							scaled = damage + 1;
					}
					if (scaled > 249)                // keep clear of the piercing marker...
						scaled = 249;
					else if (scaled == 99)           // ...and of the ice one
						scaled = 100;
					damage = scaled;
				}
			}

			for (b = 0; b < 100; b++)
			{
				if (enemyAvail[b] == 0)
				{
					bool collided;

					// Do not damage enemies before an opaque pixel enters the playfield.
					// enemyVisible[] is computed above from this tick's collision state.
					const bool enemyOnPlayfield = enemyVisible[b];

					// Select the enemy collision anchor from the hitbox setting.
					const int enemyHitX = enemy[b].ex + enemy[b].mapoffset + (centeredShotHitboxes ? 6 : 0);
					const int enemyHitYBig = enemy[b].ey + (centeredShotHitboxes ? 7 : -12);
					const int enemyHitYSmall = enemy[b].ey + (centeredShotHitboxes ? 7 : -6);

					if (z == MAX_PWEAPON - 1)
					{
						int width = 0, pillarDamage = 0;
						uint owner = 0;
						collided = zinglon_pillar_hit(enemy[b].ex + enemy[b].mapoffset,
						                              &width, &pillarDamage, &owner);
						temp = (JE_byte)width;
						temp2 = 9;
						chain = 0;
						damage = pillarDamage;
						// Damage perks and kill credit belong to the beam's owner.
						playerNum = (JE_byte)(owner + 1);
						endlessSetFxPlayer(owner);
					}
					else if (is_special)
					{
						collided = ((enemy[b].enemycycle == 0) &&
						            (abs(enemyHitX - shotHitX)      < (25 + tempX2)) &&
						            (abs(enemyHitYBig - shotHitY)   < (29 + tempY2))) ||
						           ((enemy[b].enemycycle > 0) &&
						            (abs(enemyHitX - shotHitX)      < (13 + tempX2)) &&
						            (abs(enemyHitYSmall - shotHitY) < (15 + tempY2)));
					}
					else
					{
						collided = ((enemy[b].enemycycle == 0) &&
						            (abs(enemyHitX - shotHitX) < 25) && (abs(enemyHitYBig - shotHitY) < 29)) ||
						           ((enemy[b].enemycycle > 0) &&
						            (abs(enemyHitX - shotHitX) < 13) && (abs(enemyHitYSmall - shotHitY) < 15));
					}

					if (collided && enemyOnPlayfield)
					{
						if (chain > 0)
						{
							// The Zinglon pseudo-shot clears chain above, so this slot always has
							// playerShotData behind it.
							shotMultiPos[SHOT_MISC] = 0;
							b = player_shot_create_chained(tempShotX, tempShotY, mouseX, mouseY, chain, playerNum,
							                               playerShotData[z].salvoBoost != 0);
							shotAvail[z] = 0;
							goto draw_player_shot_loop_end;
						}

						infiniteShot = false;

						if (damage == 99)
						{
							damage = 0;
							doIced = 40;
							enemy[b].iced = 40;
						}
						else
						{
							doIced = 0;
							if (damage >= 250)
							{
								damage = damage - 250;
								infiniteShot = true;
							}
						}

						// Preserve raw piercing damage while `damage` becomes this hull's result.
						const int bulletDamage = damage;

						int armorleft = enemy[b].armorleft;

						const bool has_boss_bar = enemy_has_boss_bar(enemy[b].linknum);
						const int hpMult = enemy_hp_multiplier(b);
						const int divisor100 = enemy_hp_divisor100(b);

						// Per-bullet lockout prevents a piercing shot from damaging the same
						// scaled hull on every overlapping tick.
						if (endlessFxActive() && infiniteShot)
						{
							// Compute this hull's tier before applying the tier-only repeat lock.
							const int lock100 = endlessPierceLock100(has_boss_bar, hpMult, enemy[b].eliteState);
							if (lock100 > 0)
							{
								PlayerShotDataType *pshot = &playerShotData[z];
								if (pshot->pierceLock > 0)
								{
									damage += 250;   // re-encode: the bullet flies on, dealing nothing here
									continue;
								}
							// Bank the toughest lockout crossed this tick; apply it next pass
							// so the current sweep can hit every aligned hull.
								if (lock100 > pshot->pierceLockPending)
									pshot->pierceLockPending = (JE_byte)lock100;
							}
						}

						// Apply Executioner and Knife Fight to raw damage before the health multiplier
						// accumulator; a post-divide percentage would vanish against heavily scaled targets.
						const int knifePct = endlessFxActive()
							? endlessPerkKnifeFightPercent((unsigned)b) : 0;
						const int knifeRaw = endlessPerkKnifeFightBonus(damage, knifePct);
						const int perkRaw = endlessFxActive()
							? endlessPerkExecutionerBonus(damage, enemy[b].armorleft,
							      enemy[b].healthbar_seen ? enemy[b].healthbar_max : 0, has_boss_bar)
							  + knifeRaw
							: 0;
						if (knifeRaw > 0)
							endlessPerkKnifeFightBlood((unsigned)b, knifePct);

						// Track bonus damage in post-divisor armour points.
						int perkBonus;

						if (divisor100 > 100)
						{
							// Compare perk damage from the same pre-hit accumulator state.
							const int plain = (enemy[b].damageAccum
							                   + damage * ENEMY_DAMAGE_ACCUM_SCALE) / divisor100;
							enemy[b].damageAccum += (damage + perkRaw) * ENEMY_DAMAGE_ACCUM_SCALE;
							damage = enemy[b].damageAccum / divisor100;
							enemy[b].damageAccum -= damage * divisor100;
							perkBonus = damage - plain;
						}
						else
						{
							damage += perkRaw;
							perkBonus = perkRaw;
						}

						temp = enemy[b].linknum;
						if (temp == 0)
							temp = 255;

						if (enemy[b].armorleft < 255)
						{
							boss_bar_note_hit((JE_byte)temp);

							if (enemy[b].enemyground)
								enemy[b].filter = temp2;

							for (unsigned int e = 0; e < COUNTOF(enemy); e++)
							{
								if (enemy[e].linknum == temp &&
								    enemyAvail[e] != 1 &&
								    enemy[e].enemyground != 0)
								{
									if (doIced)
										enemy[e].iced = doIced;
									enemy[e].filter = temp2;
								}
							}
						}

						if (armorleft > damage)
						{
							if (z != MAX_PWEAPON - 1)
							{
								if (enemy[b].armorleft != 255)
								{
									if (!enemy[b].healthbar_seen)
									{
										// pre-hit armor, if nothing established one earlier
										enemy_note_full_armor(&enemy[b]);
										enemy[b].healthbar_seen = true;
									}
									enemy[b].armorleft -= damage;
									JE_setupExplosion(tempShotX, tempShotY, 0, 0, false, false);
								}
								else
								{
									JE_doSP(tempShotX + 6, tempShotY + 6, damage / 2 + 3, damage / 4 + 2, temp2, false);
								}
							}

							soundQueue[5] = S_ENEMY_HIT;

							if ((armorleft - damage <= enemy[b].edlevel) &&
							    ((!enemy[b].edamaged) ^ (enemy[b].edani < 0)))
							{

								for (temp3 = 0; temp3 < 100; temp3++)
								{
									if (enemyAvail[temp3] != 1)
									{
										int linknum = enemy[temp3].linknum;
										if (
										     (temp3 == b) ||
										     (
										       (temp != 255) &&
										       (
										         ((enemy[temp3].edlevel > 0) && (linknum == temp)) ||
										         (
										           (enemyContinualDamage && (temp - 100 == linknum)) ||
										           ((linknum > 40) && (linknum / 20 == temp / 20) && (linknum <= temp))
										         )
										       )
										     )
										   )
										{
											enemy[temp3].enemycycle = 1;

											enemy[temp3].edamaged = !enemy[temp3].edamaged;

											if (enemy[temp3].edani != 0)
											{
												enemy[temp3].ani = abs(enemy[temp3].edani);
												enemy[temp3].aniactive = 1;
												enemy[temp3].animax = 0;
												enemy[temp3].animin = enemy[temp3].edgr;
												enemy[temp3].enemycycle = enemy[temp3].animin - 1;

											}
											else if (enemy[temp3].edgr > 0)
											{
												enemy[temp3].egr[1-1] = enemy[temp3].edgr;
												enemy[temp3].ani = 1;
												enemy[temp3].aniactive = 0;
												enemy[temp3].animax = 0;
												enemy[temp3].animin = 1;
											}
											else
											{
												// Tally, bounty, SHOCKWAVE, MARTYRDOM and the Chain Reaction pulse all
												// live in the one helper; never inline any of them here again (see
												// enemy_death.h).
												enemy_logical_death(temp3,
												                    playerNum >= 1 ? (int)playerNum - 1
												                                   : ENDLESS_KILLER_NONE);
											}

											enemy[temp3].aniwhenfire = 0;

											if (enemy[temp3].armorleft > (unsigned char)enemy[temp3].edlevel)
											{
												const JE_byte wasArmor = enemy[temp3].armorleft;
												enemy[temp3].armorleft = enemy[temp3].edlevel;
												enemy_note_full_armor(&enemy[temp3]);
												enemy_note_armed(temp3, wasArmor);
											}

											JE_integer tempX = enemy[temp3].ex + enemy[temp3].mapoffset;
											JE_integer tempY = enemy[temp3].ey;

											explosionFilter = endlessEliteTint(enemy[temp3].eliteState);
											if (enemyDat[enemy[temp3].enemytype].esize != 1)
												JE_setupExplosion(tempX, tempY - 6, 0, 1, false, false);
											else
												JE_setupExplosionLarge(enemy[temp3].enemyground, enemy[temp3].explonum / 2, tempX, tempY);
											explosionFilter = 0;
										}
									}
								}
							}
						}
						else
						{
							// in galaga mode player 2 is sidekick, so give cash to player 1
							enemy_kill_group(b, galagaMode ? 0 : (int)playerNum - 1,
							                 playerNum >= 1 ? (int)playerNum - 1 : ENDLESS_KILLER_NONE);
						}

						if (infiniteShot)
						{
							damage = bulletDamage + 250;
						}
						else
						{
							// Executioner and Knife Fight: undo this hull's bonus before an overkill
							// shot carries `damage` to the next enemy.
							damage -= perkBonus;

							if (z != MAX_PWEAPON - 1)
							{
								if (damage <= armorleft)
								{
									shotAvail[z] = 0;
									goto draw_player_shot_loop_end;
								}
								else
								{
									playerShotData[z].shotDmg -= armorleft;
								}
							}
						}
					}
				}
			}

draw_player_shot_loop_end:
			;
		}
	}

	// Chain Reaction perk: drain the queued death-pulses now that the player-shot loop is done, so a
	// chain kill can neither recurse nor disturb the loop's per-linkgroup kill bookkeeping.
	chain_reaction_process();
	endlessAttractorTick();

	/* Player movement indicators for shots that track your ship */
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		player[i].last_x_shot_move = player[i].x;
		player[i].last_y_shot_move = player[i].y;
	}
	
	/* Collision detection. */
	
	for (uint i = 0; i < (twoPlayerMode ? 2 : 1); ++i)
		if (player[i].is_alive && !endLevel)
			JE_playerCollide(&player[i], i + 1);
	
	if (firstGameOver)
		JE_mainGamePlayerFunctions();      /* Player draw and movement. */

	if (!endLevel)
	{    /* Draw the active level. */

		/* Draw enemy shots. */
		for (int z = 0; z < ENEMY_SHOT_MAX; z++)
		{
			if (enemyShotAvail[z] == 0)
			{
				if (enemyShot[z].seekerArm > 0 && ++enemyShot[z].seekerArm >= ENDLESS_SEEKER_DELAY_TICKS)
				{
					endlessSeekerCorrect(&enemyShot[z]);
					enemyShot[z].seekerLeft = (enemyShot[z].seekerLeft > 0) ? enemyShot[z].seekerLeft - 1 : 0;
					enemyShot[z].seekerArm = (enemyShot[z].seekerLeft > 0) ? 1 : 0;
				}

				// Homing shots chase the nearer ship still flying; solo play keeps player 1.
				const Player *const homeTo =
					&player[endlessFxActive() ? endlessDangerTargetPlayer(enemyShot[z].sx, enemyShot[z].sy) : 0];

				enemyShot[z].sxm += enemyShot[z].sxc;
				enemyShot[z].sx += enemyShot[z].sxm;

				if (enemyShot[z].tx != 0)
				{
					if (enemyShot[z].sx > homeTo->x)
					{
						if (enemyShot[z].sxm > -enemyShot[z].tx)
							enemyShot[z].sxm--;
					}
					else
					{
						if (enemyShot[z].sxm < enemyShot[z].tx)
							enemyShot[z].sxm++;
					}
				}

				enemyShot[z].sym += enemyShot[z].syc;
				enemyShot[z].sy += enemyShot[z].sym;

				if (enemyShot[z].ty != 0)
				{
					if (enemyShot[z].sy > homeTo->y)
					{
						if (enemyShot[z].sym > -enemyShot[z].ty)
							enemyShot[z].sym--;
					}
					else
					{
						if (enemyShot[z].sym < enemyShot[z].ty)
							enemyShot[z].sym++;
					}
				}

				// X cull is against the VISIBLE window: [PLAYFIELD_LEFT, PLAYFIELD_RIGHT] after the
				// composite crop (vanilla: 0..262, cull at >275/<=0).
				if (enemyShot[z].duration-- == 0 || enemyShot[z].sy > 190 || enemyShot[z].sy <= -14 || enemyShot[z].sx > PLAYFIELD_RIGHT + 13 || enemyShot[z].sx <= 0)
				{
					enemyShotAvail[z] = true;
				}
				else  // check if shot collided with player
				{
					// Select shot and ship collision anchors from the hitbox setting.
					int eShotHitDx, eShotHitDy;
					enemy_shot_hit_offset(enemyShot[z].sgr, enemyShot[z].animate, &eShotHitDx, &eShotHitDy);
					const int eShotHitX = enemyShot[z].sx + eShotHitDx;
					const int eShotHitY = enemyShot[z].sy + eShotHitDy;

					for (uint i = 0; i < (twoPlayerMode ? 2 : 1); ++i)
					{
						// endless LOW PROFILE boon shrinks the box ~25%; endlessHitboxScale returns the
						// stock extent otherwise, so this reads as the vanilla test in every other game.
						const int hitX = endlessHitboxScale((int)player[i].shot_hit_area_x);
						const int hitY = endlessHitboxScale((int)player[i].shot_hit_area_y);
						const int shipHitX = player[i].x + (centeredShotHitboxes ? 7 : 0);
						const int shipHitY = player[i].y + (centeredShotHitboxes ? 7 : 0);
						if (player[i].is_alive &&
						    eShotHitX > shipHitX - hitX &&
						    eShotHitX < shipHitX + hitX &&
						    eShotHitY > shipHitY - hitY &&
						    eShotHitY < shipHitY + hitY)
						{
							JE_integer tempX = enemyShot[z].sx;
							JE_integer tempY = enemyShot[z].sy;
							temp = enemyShot[z].sdmg;

							enemyShotAvail[z] = true;

							JE_setupExplosion(tempX, tempY, 0, 0, false, false);

							if (player[i].invulnerable_ticks == 0)
							{
								const uint shieldBefore = player[i].shield;
								const uint armorBefore = player[i].armor;
								if ((temp = JE_playerDamage(temp, &player[i])) > 0)
								{
									player[i].x_velocity += (enemyShot[z].sxm * temp) / 2;
									player[i].y_velocity += (enemyShot[z].sym * temp) / 2;
								}

								// Reflect shield-only hits through the owning ship's perk context.
								if (endlessFxActive() && player[i].armor == armorBefore
								    && player[i].shield < shieldBefore)
								{
									const int absorbed = (int)(shieldBefore - player[i].shield);
									const int returned = endlessPerkDeflectDamage(absorbed);
									if (returned > 0)
										player_shot_create_deflected(&enemyShot[z], returned,
										                             (JE_byte)(i + 1));
								}

								// Refund after damage resolution so armor overflow uses the full hit.
								if (endlessFxActive() && player[i].shield < shieldBefore)
								{
									const int stopped = (int)(shieldBefore - player[i].shield);
									const int spared = endlessPerkDeflectShieldSpared(stopped);
									if (spared > 0)
									{
										player[i].shield += (uint)spared;
										hud_bars_dirty = true;  // JE_playerDamage drew the unrefunded loss
									}
								}
							}

							break;
						}
					}

					if (enemyShotAvail[z] == false)
					{
						if (enemyShot[z].animax != 0)
						{
							if (++enemyShot[z].animate >= enemyShot[z].animax)
								enemyShot[z].animate = 0;
						}

						rl_current_id = RL_ID_ESHOT_BASE + z;
						// Record velocity and acceleration for display-rate extrapolation.
						rl_current_vel_x = enemyShot[z].sxm;
						rl_current_vel_y = enemyShot[z].sym;
						rl_current_acc_x = enemyShot[z].sxc;
						rl_current_acc_y = enemyShot[z].syc;
						// Graphics from 500 up live in the second sheet, indexed from its start.
						const bool highSheet = (enemyShot[z].sgr >= 500);
						Sprite2_array *const sheet = highSheet ? &spriteSheet12 : &spriteSheet8;
						const unsigned int frame =
							enemyShot[z].sgr + enemyShot[z].animate - (highSheet ? 500 : 0);
						if (enemyShot[z].filter != 0)
							blit_sprite2_filter_bright(VGAScreen, enemyShot[z].sx, enemyShot[z].sy, *sheet,
							                           frame, enemyShot[z].filter | ENDLESS_SHOT_BRIGHT);
						else
							blit_sprite2(VGAScreen, enemyShot[z].sx, enemyShot[z].sy, *sheet, frame);
						rl_current_id = 0;
						rl_current_vel_x = 0;
						rl_current_vel_y = 0;
						rl_current_acc_x = 0;
						rl_current_acc_y = 0;
					}
				}

			}
		}
	}

	if (background3over == 1)
		draw_background_3(VGAScreen);

	/* Draw Top Enemy */
	if (topEnemyOver)
	{
		// Bind this bank to layer 3's post-update parallax anchor.
		tempMapXOfs = ((background3x1 == 0) ? mapX3Ofs : mapXOfs) + PLAYFIELD_X_SHIFT;
		tempMapXOfs_frac = (background3x1 == 0) ? (mapX3Ofs_f - mapX3Ofs) : (mapXOfs_f - mapXOfs);
		tempMapXOfs_layer = 3;
		tempBackMove = backMove3;
		tempScrollExtraPx  = endlessScrollExtraPx3;  // this batch rides layer 3
		tempScrollYLayer   = 3;
		tempScrollBaseStep = (int)backMove3;
		tempScrollDelayMax = 1;
		// Keep every enemy bank at the common pre-advance entity phase; see the matching
		// !topEnemyOver path above. The bar is recorded after JE_drawEnemy advances ey.
		tempScrollYBase    = 0;
		tempScrollYfrac    = bg_layer_yfrac[3];
		tempScrollYBaseBar = -(tempBackMove + tempScrollExtraPx);
		tempScrollYfracBar = bg_layer_yfrac[3];
		JE_drawEnemy(75);
	}

	/* Draw Sky Enemy */
	if (skyEnemyOverAll)
	{
		lastEnemyOnScreen = enemyOnScreen;

		tempMapXOfs = mapX2Ofs + PLAYFIELD_X_SHIFT;
		tempMapXOfs_frac = mapX2Ofs_f - mapX2Ofs;
		tempMapXOfs_layer = 2;
		tempBackMove = 0;
		tempScrollExtraPx  = 0;     // layer-2 anchor: any ride is per-enemy via skyGlueThisEnemy
		tempScrollYLayer   = 0;
		tempScrollBaseStep = 0;
		tempScrollDelayMax = 1;
		tempScrollYBase = tempScrollYBaseBar = 0;
		tempScrollYfrac = tempScrollYfracBar = 0.0f;
		JE_drawEnemy(25);

		if (enemyOnScreen == lastEnemyOnScreen)
		{
			if (stopBackgroundNum == 2)
				stopBackgroundNum = 9;
		}
	}

	/* Sequenced explosions. */
	enemyStillExploding = false;
	for (int i = 0; i < MAX_REPEATING_EXPLOSIONS; i++)
	{
		if (rep_explosions[i].ttl != 0)
		{
			enemyStillExploding = true;

			if (rep_explosions[i].delay > 0)
			{
				rep_explosions[i].delay--;
				continue;
			}

			// Track layer 2 while keeping the explosion's one-pixel fall speed.
			rep_explosions[i].y += backMove2 + endlessScrollExtraPx2 + 1;
			JE_integer tempX = rep_explosions[i].x + (mt_rand() % 24) - 12;
			JE_integer tempY = rep_explosions[i].y + (mt_rand() % 27) - 24;

			// A big sequence keeps re-arming itself, so the tint has to travel with it.
			explosionFilter = rep_explosions[i].filter;

			if (rep_explosions[i].big)
			{
				JE_setupExplosionLarge(false, 2, tempX, tempY);

				if (rep_explosions[i].ttl == 1 || mt_rand() % 5 == 1)
					soundQueue[7] = S_EXPLOSION_11;
				else
					soundQueue[6] = S_EXPLOSION_9;

				rep_explosions[i].delay = 4 + (mt_rand() % 3);
			}
			else
			{
				JE_setupExplosion(tempX, tempY, 0, 1, false, false);
				explosion_rumble_add(EXPLOSION_RUMBLE_POP, false, tempX, tempY);

				soundQueue[5] = S_EXPLOSION_4;

				rep_explosions[i].delay = 3;
			}

			explosionFilter = 0;
			rep_explosions[i].ttl--;
		}
	}

	/* Draw explosions. */
	for (int j = 0; j < MAX_EXPLOSIONS; j++)
	{
		if (explosions[j].ttl != 0)
		{
			if (!explosions[j].fixedPosition)
			{
				explosions[j].sprite++;
				explosions[j].y += explodeMove;
			}
			else if (explosions[j].followPlayer)
			{
				explosions[j].x += explosionFollowAmountX;
				explosions[j].y += explosionFollowAmountY;
			}
			explosions[j].y += explosions[j].deltaY;

			if (explosions[j].y > vga_height - 14)
			{
				explosions[j].ttl = 0;
			}
			else
			{
				// Skip far-offscreen explosion drawing because these blitters do not clip X.
				// Lifetime still advances, and the bounds retain every visible pixel.
				if (explosions[j].x > -12 && explosions[j].x < 344)
				{
					// Combine slot and generation to avoid pairing recycled explosions.
					rl_current_id = RL_ID_EXPL_BASE + j * 4 + (explosions[j].id_gen & 3);
					rl_current_vel_x = (explosions[j].fixedPosition && explosions[j].followPlayer)
					                   ? explosionFollowAmountX : 0;
					rl_current_vel_y = explosions[j].deltaY
					                 + (!explosions[j].fixedPosition ? explodeMove
					                    : explosions[j].followPlayer ? explosionFollowAmountY : 0);
					const int ex = explosions[j].x, ey = explosions[j].y;
					const unsigned int frame = explosions[j].sprite + 1;
					const Uint8 tint = explosions[j].filter;  // endless elite / champion
					const Uint8 alpha = explosion_opacity(j);
					// Shield bubbles never combine partial opacity with an Endless tier tint.
					if (alpha < NET_STYLE_SOLID)
						blit_sprite2_alpha(VGAScreen, ex, ey, explosionSpriteSheet, frame, -1, alpha);
					else if (explosionTransparent && tint != 0)
						blit_sprite2_blend_filter(VGAScreen, ex, ey, explosionSpriteSheet, frame,
						                          tint | ENDLESS_EXPLOSION_BRIGHT);
					else if (explosionTransparent)
						blit_sprite2_blend(VGAScreen, ex, ey, explosionSpriteSheet, frame);
					else if (tint != 0)
						blit_sprite2_filter(VGAScreen, ex, ey, explosionSpriteSheet, frame, tint);
					else
						blit_sprite2(VGAScreen, ex, ey, explosionSpriteSheet, frame);
					rl_current_id = 0;
					rl_current_vel_x = 0;
					rl_current_vel_y = 0;
				}

				explosions[j].ttl--;
			}
		}
	}

	if (!portConfigChange)
		portConfigDone = true;

	/* Background layer 2. */
	if (background2over == 2)
		draw_background_2_layer();

	// Draw sparks before residual capture so replay interpolation owns them. Silent re-simulation
	// skips this presentation-only state.
	if (!rollback_resim_silent)
		JE_drawSP();

	// Record enemy bars before smoothie residual capture; normal levels draw them after filtering.
	if (anySmoothies)
	{
		draw_enemy_health_bars();
		hud_draw_ship_hp_bars();
	}

	// Capture smoothie residuals between recorded blits and non-blit overlays.
	if (anySmoothies && !rollback_resim_silent)
		memcpy(VGAScreen2->pixels, game_screen->pixels, (size_t)game_screen->h * game_screen->pitch);
	if (!anySmoothies)
		rl_mark_residual_base(game_screen);

	/* Warning indicator. */
	if ((player[0].is_alive && player[0].armor < 6) ||
	    (twoPlayerMode && !galagaMode && player[1].is_alive && player[1].armor < 6))
	{
		int armor_amount = (player[0].is_alive && player[0].armor < 6) ? player[0].armor : player[1].armor;

		if (armorShipDelay > 0)
		{
			armorShipDelay--;
		}
		else
		{
			tempW = 560;
			b = JE_newEnemy(50, tempW, 0);
			if (b > 0)
			{
				enemy[b-1].enemydie = 560 + (mt_rand() % 3) + 1;
				enemy[b-1].eyc -= backMove3;
				enemy[b-1].armorleft = 4;
				enemy_note_full_armor(&enemy[b-1]);
			}
			armorShipDelay = 500;
		}

		if ((player[0].is_alive && player[0].armor < 6 && (!isNetworkGame || thisPlayerNum == 1)) ||
		    (twoPlayerMode && player[1].is_alive && player[1].armor < 6 && (!isNetworkGame || thisPlayerNum == 2)))
		{

			tempW = armor_amount * 4 + 8;
			if (warningSoundDelay > tempW)
				warningSoundDelay = tempW;

			if (warningSoundDelay > 1)
			{
				warningSoundDelay--;
			}
			else
			{
				if (armorAlarm)
					soundQueue[7] = S_WARNING;
				warningSoundDelay = tempW;
			}

			warningCol += warningColChange;
			if (warningCol > 113 + (14 - (armor_amount * 2)))
			{
				warningColChange = -warningColChange;
				warningCol = 113 + (14 - (armor_amount * 2));
			}
			else if (warningCol < 113)
			{
				warningColChange = -warningColChange;
			}
			const int playfield_left = PLAYFIELD_LEFT;
			const int playfield_right = playfield_left + PLAYFIELD_WIDTH - 1;
			const char warning_text[] = "WARNING";
			const int warning_text_width = JE_textWidth(warning_text, TINY_FONT);
			const int warning_x = playfield_left + (PLAYFIELD_WIDTH - warning_text_width) / 2;
			const int gap_margin = 1;
			warning_bar(playfield_left, 181, warning_x - gap_margin - 1, 183, warningCol);
			warning_bar(warning_x + warning_text_width + gap_margin, 181, playfield_right, 183, warningCol);
			warning_bar(playfield_left, 0, playfield_right, 3, warningCol);

			JE_outText(VGAScreen, warning_x, 178, warning_text, 7, (warningCol % 16) / 2);

		}
	}

	/* Random explosions. */
	// Full visible playfield: 280 is the pre-widescreen width and stops short of the widened right
	// edge (see composite_playfield / video.h); 184 = full playfield height (vanilla stopped at 180).
	if (randomExplosions && mt_rand() % 10 == 1)
	{
		// Sequenced: as arguments the two draws were unordered, and the compilers disagree
		// about which coordinate gets which (see specials.c's special scatter).
		const int boom_x = PLAYFIELD_LEFT + mt_rand() % PLAYFIELD_WIDTH;
		const int boom_y = mt_rand() % 184;
		JE_setupExplosionLarge(false, 20, boom_x, boom_y);
	}

	// Silent re-simulation and loadout edits both leave the sidekick HUD dirty.
	if (hud_sidekicks_dirty && !rollback_resim_silent)
	{
		hud_sidekicks_dirty = false;
		JE_drawOptionsHUD();
		JE_drawPortConfigButtons();
	}

	// Repaint event-driven gauges after silent replay.
	if (hud_bars_dirty && !rollback_resim_silent)
	{
		hud_bars_dirty = false;
		JE_repaintShieldArmorBars();
	}

	// Redraw an unexpired message posted during silent replay.
	if (hud_message_dirty && !rollback_resim_silent)
	{
		hud_message_dirty = false;
		if (textErase > 0)
			JE_repaintTextWindow();
	}

	// Play link cues from presented state so rollback neither loses nor repeats them.
	if (!rollback_resim_silent && twoPlayerLinked != link_cue_state)
	{
		link_cue_state = twoPlayerLinked;
		if (linkSounds && !galagaMode)
		{
			const JE_byte cue = twoPlayerLinked ? S_CLINK : S_SPRING;
			multiSamplePlay(soundSamples[cue-1], soundSampleCount[cue-1], SFX_CUE_CHANNEL, fxPlayVol / 2);
		}
	}

	/* Sound queue. */
	if (firstGameOver)
	{
		temp = 0;
		for (temp2 = 0; temp2 < COUNTOF(soundQueue); temp2++)
		{
			if (soundQueue[temp2] != S_NONE)
			{
				temp = soundQueue[temp2];
				if (temp2 == 3)
					temp3 = fxPlayVol;
				else if (temp == 15)
					temp3 = fxPlayVol / 4;
				else   /*Lightning*/
					temp3 = fxPlayVol / 2;

				// Silent replay mutates scratch state but does not replay sounds.
				if (!rollback_resim)
					multiSamplePlayLimited(soundSamples[temp-1], soundSampleCount[temp-1],
					                       temp2, temp3);

				soundQueue[temp2] = S_NONE;
			}
		}
	}

	// The tick's blasts all feed one fading rumble, so a busier tick rumbles harder.
	explosion_rumble_tick(!rollback_resim && !play_demo);

	if (returnActive && enemyOnScreen == 0)
	{
		JE_eventJump(65535);
		returnActive = false;
	}

	/* Debug counters. */
	debugTime = SDL_GetTicks();
	tempW = lastmouse_but;

	if (debug)
	{
		for (size_t i = 0; i < 9; i++)
		{
			tempStr[i] = '0' + smoothies[i];
		}
		tempStr[9] = '\0';
		sprintf(buffer, "SM = %s", tempStr);
		JE_outText(VGAScreen, 30, 70, buffer, 4, 0);

		sprintf(buffer, "Memory left = %d", -1);
		JE_outText(VGAScreen, 30, 80, buffer, 4, 0);
		sprintf(buffer, "Enemies onscreen = %d", enemyOnScreen);
		JE_outText(VGAScreen, 30, 90, buffer, 6, 0);

		debugHist = debugHist + abs((JE_longint)debugTime - (JE_longint)lastDebugTime);
		debugHistCount++;
		sprintf(tempStr, "%2.3f", 1000.0f / roundf(debugHist / debugHistCount));
		sprintf(buffer, "X:%d Y:%-5d  %s FPS  %d %d %d %d", (mapX - 1) * 12 + player[0].x, curLoc, tempStr, player[0].x_velocity, player[0].y_velocity, player[0].x, player[0].y);
		JE_outText(VGAScreen, 45, 175, buffer, 15, 3);
		lastDebugTime = debugTime;
	}

	/*Pentium Speed Mode?*/
	if (pentiumMode)
	{
		frameCountMax = (frameCountMax == 2) ? 3 : 2;
	}

	/* Level timer. */
	if (levelTimer && levelTimerCountdown > 0)
	{
		levelTimerCountdown--;
		if (levelTimerCountdown == 0)
			JE_eventJump(levelTimerJumpTo);

		if (timedBattleMode)
		{
			// No-op; play no sound effects
		}
		else if (levelTimerCountdown > 200)
		{
			if (levelTimerCountdown % 100 == 0)
				soundQueue[7] = S_WARNING;

			if (levelTimerCountdown % 10 == 0)
				soundQueue[6] = S_CLICK;
		}
		else if (levelTimerCountdown % 20 == 0)
		{
			soundQueue[7] = S_WARNING;
		}

		// Don't use floats due to rounding.
		sprintf(buffer, "%d.%d", levelTimerCountdown / 100, (levelTimerCountdown / 10) % 10);

		// Lay the countdown number and its "TIME" label out as one group and centre
		// that group on the playfield.
		const int label_w = JE_textWidth(miscText[66], TINY_FONT);
		const int counter_w = JE_textWidth(buffer, SMALL_FONT_SHAPES);
		const int group_w = counter_w + 3 + label_w;
		const int counter_x = PLAYFIELD_CENTER_X(group_w);
		const int label_x = counter_x + counter_w + 3;

		JE_textShade(VGAScreen, label_x, 6, miscText[66], 7, (levelTimerCountdown % 20) / 3, FULL_SHADE);
		JE_dString(VGAScreen, counter_x, 2, buffer, SMALL_FONT_SHAPES);
	}

	/* Game over. */
	if (!constantPlay && !constantDie)
	{
		if (allPlayersGone)
		{
			if (player[0].exploding_ticks > 0 || player[1].exploding_ticks > 0)
			{
				if (galagaMode)
					player[1].exploding_ticks = 0;

				musicFade = true;

				// Fresh non-Esc input skips the Relaxed wreck animation. Held steering must be released first.
				if (endlessDeathMenuDue() && !play_demo && !rollback_resim && !isNetworkGame)
				{
					push_joysticks_as_keyboard();
					service_SDL_events(false);

					// Steering touches use mouse_pressed[0], not mousedown or newmouse.
					if (!keydown && !mousedown && !joydown && !mouse_pressed[0])
					{
						deathSkipArmed = true;
					}
					else if (deathSkipArmed && ((newkey && lastkey_scan != SDL_SCANCODE_ESCAPE) ||
					                            newmouse || mouse_pressed[0]))
					{
						reallyEndLevel = true;
						// A live-input one-shot outside the movement tuples; record it so a
						// self-test replay of this tick lands on the same state.
						if (rollback_selftest_active())
							rollback_st_event(RB_EV_DISMISS);
					}
				}
			}
			else
			{
				// Relaxed mode plays the normal GAME OVER beat before opening its death menu.
				if (play_demo || normalBonusLevelCurrent || bonusLevelCurrent)
					reallyEndLevel = true;
				else
				{
					const int playfield_left = PLAYFIELD_LEFT;
					const int game_over_width = JE_textWidth(miscText[21], FONT_SHAPES);
					const int game_over_x = playfield_left + (PLAYFIELD_WIDTH - game_over_width) / 2;
					JE_dString(VGAScreen, game_over_x, 60, miscText[21], FONT_SHAPES); // game over
				}

				if (firstGameOver)
				{
					if (!play_demo)
					{
						play_song(SONG_GAMEOVER);
						// Cancel the death fade before restoring volume for the game-over song.
						musicFade = false;
						set_volume(tyrMusicVolume, fxVolume);
					}
					// Require a fresh press for GAME OVER, including the held touch-fire latch.
					newkey = newmouse = false;
					mouse_pressed[0] = false;
					firstGameOver = false;
				}

				// Open the Relaxed death menu when the game-over song ends, bounded by min/max timers.
				if (endlessDeathMenuDue() && !play_demo && !rollback_resim)
				{
					++deathGameOverTicks;

					if ((!playing && deathGameOverTicks >= DEATH_GAMEOVER_TICKS_MIN) ||
					    deathGameOverTicks >= DEATH_GAMEOVER_TICKS_MAX)
					{
						reallyEndLevel = true;
						// Record the automatic dismissal for self-test replay.
						if (rollback_selftest_active())
							rollback_st_event(RB_EV_DISMISS);
					}
				}

				if (!play_demo && !rollback_resim)
				{
					push_joysticks_as_keyboard();
					// Preserve input edges collected by the display-rate loop.
					service_SDL_events(false);
					// Dead-player touch input still arrives through mouse_pressed[0].
					if ((newkey || button[0] || button[1] || button[2]) || newmouse || mouse_pressed[0])
					{
						reallyEndLevel = true;
						// A live-input one-shot outside the movement tuples; record it
						// so a self-test replay of this tick lands on the same state.
						if (rollback_selftest_active())
							rollback_st_event(RB_EV_DISMISS);
					}
				}

				if (isNetworkGame)
					reallyEndLevel = true;
			}
		}
	}

	if (rollback_resim)
	{
		// Replay uses recorded one-shot events and never samples live input.
		if (rollback_st_events() & RB_EV_DISMISS)
			reallyEndLevel = true;
	}
	else if (play_demo) // input kills demo
	{
		push_joysticks_as_keyboard();
		service_SDL_events(false);

		if (newkey || newmouse)
		{
			reallyEndLevel = true;
			if (rollback_selftest_active())
				rollback_st_event(RB_EV_DISMISS);

			stopped_demo = true;
		}
	}
	else // input handling for pausing, menu, cheats
	{
		service_SDL_events(false);

		if (newkey)
		{
			skipStarShowVGA = false;
			JE_mainKeyboardInput();
			newkey = false;
			if (skipStarShowVGA)
			{
				// Mid-tick restart (in-game help / cheat overlays): the tick is
				// not cleanly replayable, so the self-test skips verifying it.
				rollback_taint("skipStarShowVGA");
				goto level_loop;
			}
		}

		/* Online sessions ignore and consume pause input. */
		if (pause_pressed || !windowHasFocus)
		{
			pause_pressed = false;

			if (!isNetworkGame)
				JE_pauseGame();
		}

		// Endless Standard and Hardcore: the pause menu is off-limits from the moment the ship dies.
		if (ingamemenu_pressed && endlessDeathLocksMenu() && all_players_dead())
			ingamemenu_pressed = false;

		if (ingamemenu_pressed)
		{
			ingamemenu_pressed = false;

			if (isNetworkGame)
			{
				inGameMenuRequest = true;
			}
			else
			{
				yourInGameMenuRequest = true;
				JE_doInGameSetup();
				skipStarShowVGA = true;
			}
		}
	}

	// Snapshot ship velocity inside the simulation so replayed inherited-velocity shots agree.
	vt_ship_pred_tick();

	/* Restart backgrounds or end the level when no enemies remain. This is simulation state and
	 * must run before the rollback driver. */
	if (stopBackgroundNum == 9 && backMove == 0 && !enemyStillExploding)
	{
		backMove = 1;
		backMove2 = 2;
		backMove3 = 3;
		explodeMove = 2;
		stopBackgroundNum = 0;
		stopBackgrounds = false;
		if (waitToEndLevel)
		{
			endLevel = true;
			levelEnd = 40;
		}
		if (allPlayersGone)
		{
			reallyEndLevel = true;
		}
	}

	if (!endLevel && enemyOnScreen == 0)
	{
		if (readyToEndLevel && !enemyStillExploding)
		{
			if (levelTimerCountdown > 0)
			{
				levelTimer = false;
			}
			readyToEndLevel = false;
			endLevel = true;
			levelEnd = 40;
			if (allPlayersGone)
			{
				reallyEndLevel = true;
			}
		}
		if (stopBackgrounds)
		{
			stopBackgrounds = false;
			backMove = 1;
			backMove2 = 2;
			backMove3 = 3;
			explodeMove = 2;
		}
	}

	// Cull unreachable above-screen orphans after a prolonged map stop. Live linked boss anchors
	// remain untouched.
	enemyParkedAbove = count_stuck_above_screen();
	if (!endLevel && stopBackgrounds && !forceEvents && enemyParkedAbove != 0)
	{
		if (++mapStopStallTicks >= MAP_STOP_STALL_LIMIT)
		{
			for (int i = 0; i < 100; i++)
				if (enemyAvail[i] != 1 && enemy_stuck_orphaned(i))
					enemyAvail[i] = 1;
			mapStopStallTicks = 0;
		}
	}
	else
		mapStopStallTicks = 0;

	// Advance level grades with simulation so rollback replays them.
	JE_advanceLevelFade();
	boss_bar_tick();

	/* Wire-test scripted events, keyed to the frame counter so re-simulation passes replay them
	 * exactly. */
	if (qa_net_gameplay_ticks > 0 && isNetworkGame && nrb_active())
	{
		if (qa_net_lobby_settings && nrb_frame() >= 200 && nrb_frame() <= 320
		    && nrb_frame() % 40 == 0)
			player_award_pickup_cash(&player[(nrb_frame() / 40) & 1], 75);

		/* Exercise each Super Arcade color through the real pickup rule for both ships.
		 * Resolve against the collector's arsenal to avoid divergent loadouts. */
		if (network_game_type == NETWORK_GAME_SUPERARCADE && nrb_frame() >= 200
		    && nrb_frame() <= 400 && nrb_frame() % 50 == 0)
		{
			const uint slot = (uint)(nrb_frame() / 50) - 4;   // 0..4
			const uint w1 = player_sa_ball_weapon(&player[0], slot);
			const uint w2 = player_sa_ball_weapon(&player[1], slot);
			player[0].items.weapon[FRONT_WEAPON].id = (Uint8)w1;
			player[1].items.weapon[FRONT_WEAPON].id = (Uint8)w2;
			player[0].shot_multi_pos[SHOT_FRONT] = player[1].shot_multi_pos[SHOT_FRONT] = 0;
			if (!rollback_resim)
			{
				printf("NET SA BALL slot=%u p1=%u p2=%u\n", slot, w1, w2);
				fflush(stdout);
			}
		}

		if (qa_net_zones > 0 && nrb_frame() == QA_NET_ZONE_END_FRAME && !endLevel)
		{
			endLevel = true;
			levelEnd = 40;
		}
	}

	/*Network Update*/
	if (isNetworkGame && nrb_active())
	{
		// Rollback either presents this pass or restores and re-enters the loop.
		if (nrb_driver() == NRB_STEP_RESIM)
			goto level_loop;
	}
	else if (isNetworkGame)
	{
		if (!reallyEndLevel)
		{
			// Bit 0 was the pause request. Online cannot pause, so it stays clear.
			Uint16 requests = (inGameMenuRequest == true) << 1 |
			                  (skipLevelRequest == true) << 2 |
			                  (nortShipRequest == true) << 3;
			SDLNet_Write16(requests,        &packet_state_out[0]->data[14]);

			SDLNet_Write16(difficultyLevel, &packet_state_out[0]->data[16]);
			SDLNet_Write16(player[0].x,     &packet_state_out[0]->data[18]);
			SDLNet_Write16(player[1].x,     &packet_state_out[0]->data[20]);
			SDLNet_Write16(player[0].y,     &packet_state_out[0]->data[22]);
			SDLNet_Write16(player[1].y,     &packet_state_out[0]->data[24]);
			SDLNet_Write16(curLoc,          &packet_state_out[0]->data[26]);

			// Desync canary: a summary of the simulation as it stands at this point in the
			// tick, which both machines reach with identical state when all is well.
			{
				Uint32 rand_draws, player_hash, enemy_hash;
				network_sim_state(&rand_draws, &player_hash, &enemy_hash);
				SDLNet_Write32(rand_draws,  &packet_state_out[0]->data[NET_STATE_RAND]);
				SDLNet_Write32(player_hash, &packet_state_out[0]->data[NET_STATE_PHASH]);
				SDLNet_Write32(enemy_hash,  &packet_state_out[0]->data[NET_STATE_EHASH]);
			}

			network_state_send();

			if (network_state_update())
			{
				assert(SDLNet_Read16(&packet_state_in[0]->data[26]) == SDLNet_Read16(&packet_state_out[network_delay]->data[26]));

				// Compare packets from the same logical tick and report only the first mismatch per level.
				{
					static int reported_for_level = -1;

					const Uint32 their_rand = SDLNet_Read32(&packet_state_in[0]->data[NET_STATE_RAND]);
					const Uint32 our_rand   = SDLNet_Read32(&packet_state_out[network_delay]->data[NET_STATE_RAND]);
					const Uint32 their_ph   = SDLNet_Read32(&packet_state_in[0]->data[NET_STATE_PHASH]);
					const Uint32 our_ph     = SDLNet_Read32(&packet_state_out[network_delay]->data[NET_STATE_PHASH]);
					const Uint32 their_eh   = SDLNet_Read32(&packet_state_in[0]->data[NET_STATE_EHASH]);
					const Uint32 our_eh     = SDLNet_Read32(&packet_state_out[network_delay]->data[NET_STATE_EHASH]);

					if ((their_rand != our_rand || their_ph != our_ph || their_eh != our_eh) &&
					    reported_for_level != (int)mainLevel)
					{
						reported_for_level = (int)mainLevel;

						if (qa_net_scenario == 19)
						{
							Uint32 link_bits = 0;
							memcpy(&link_bits, &linkGunDirec,
							       MIN(sizeof(link_bits), sizeof(linkGunDirec)));
							fprintf(stderr,
							        "NET DELAY DESYNC player=%u scroll=%u ph=%08x/%08x "
							        "pos=%d,%d/%d,%d shield=%d/%d linked=%d dir=%08x input=%u,%04x/%u,%04x\n",
							        thisPlayerNum, (unsigned)curLoc, (unsigned)our_ph,
							        (unsigned)their_ph, player[0].x, player[0].y,
							        player[1].x, player[1].y, player[0].shield, player[1].shield,
							        twoPlayerLinked ? 1 : 0,
							        (unsigned)link_bits,
							        (unsigned)SDLNet_Read16(
							                &packet_state_out[network_delay]->data[NET_STATE_LINK_FLAGS]),
							        (unsigned)SDLNet_Read16(
							                &packet_state_out[network_delay]->data[NET_STATE_LINK_ANGLE]),
							        (unsigned)SDLNet_Read16(
							                &packet_state_in[0]->data[NET_STATE_LINK_FLAGS]),
							        (unsigned)SDLNet_Read16(
							                &packet_state_in[0]->data[NET_STATE_LINK_ANGLE]));
							fflush(stderr);
						}

						// Goes through the net log, not stderr: this is a Windows-subsystem
						// build with no console, so a printf would be thrown away.
						char detail[512];
						snprintf(detail, sizeof(detail),
						         "level %d (scroll clock %u), player %u, delay %d\n"
						         "  rand draws : local %lu  remote %lu  %s\n"
						         "  players    : local %08x  remote %08x  %s\n"
						         "  enemies    : local %08x  remote %08x  %s",
						         (int)mainLevel, (unsigned)curLoc, thisPlayerNum, network_delay,
						         (unsigned long)our_rand, (unsigned long)their_rand,
						         our_rand == their_rand ? "ok" : "DIFFERS",
						         (unsigned)our_ph, (unsigned)their_ph,
						         our_ph == their_ph ? "ok" : "DIFFERS",
						         (unsigned)our_eh, (unsigned)their_eh,
						         our_eh == their_eh ? "ok" : "DIFFERS");
						network_diag_note_desync((int)mainLevel);
						crashlog_note_net("NETWORK DESYNC", detail);

						if (networkDesyncHalt)
							network_tyrian_halt(7, false);
					}
				}

				requests = SDLNet_Read16(&packet_state_in[0]->data[14]) ^ SDLNet_Read16(&packet_state_out[network_delay]->data[14]);
				// Bit 0 was the pause request. Nothing online sets it now; the bit stays
				// reserved so the state packet keeps its layout.
				if (requests & 2)
				{
					yourInGameMenuRequest = SDLNet_Read16(&packet_state_out[network_delay]->data[14]) & 2;
					JE_doInGameSetup();
					yourInGameMenuRequest = false;
					if (haltGame)
						reallyEndLevel = true;
				}
				if (requests & 4)
				{
					levelTimer = true;
					levelTimerCountdown = 0;
					endLevel = true;
					levelEnd = 40;
				}
				if (requests & 8) // nortship
				{
					player[0].items.ship = 12;                     // Nort Ship
					player[0].items.special = 13;                  // Astral Zone
					player[0].items.weapon[FRONT_WEAPON].id = 36;  // NortShip Super Pulse
					player[0].items.weapon[REAR_WEAPON].id = 37;   // NortShip Spreader
					shipGr = 1;
				}

				for (int i = 0; i < 2; i++)
				{
					if (SDLNet_Read16(&packet_state_in[0]->data[18 + i * 2]) != SDLNet_Read16(&packet_state_out[network_delay]->data[18 + i * 2]) || SDLNet_Read16(&packet_state_in[0]->data[20 + i * 2]) != SDLNet_Read16(&packet_state_out[network_delay]->data[20 + i * 2]))
					{
						char temp[64];
						sprintf(temp, "Player %d is unsynchronized!", i + 1);

						JE_textShade(game_screen, 40, 110 + i * 10, temp, 9, 2, FULL_SHADE);
					}
				}

				if (qa_net_scenario == 19 && ++qa_net_delay_frames > qa_net_gameplay_ticks)
				{
					const int rc = network_desync_count() == 0 && qa_net_special_flashes > 0 ? 0 : 1;
					printf("NET DELAY %s player=%u frames=%lu desyncs=%lu special-flashes=%lu\n",
					       rc == 0 ? "PASS" : "FAIL", thisPlayerNum, qa_net_delay_frames,
					       (unsigned long)network_desync_count(), qa_net_special_flashes);
					fflush(stdout);

					const Uint32 drain_started = SDL_GetTicks();
					while (SDL_GetTicks() - drain_started < 1200)
					{
						watchdog_heartbeat();
						network_check();
						SDL_Delay(1);
					}
					JE_tyrianExitWithoutSaving(rc);
				}
			}
		}

		JE_clearSpecialRequests();
	}

	// Replay the tick from recorded input and compare every registered byte (rollback.h).
	if (rollback_selftest_tick())
	{
		rl_abort_record();
		JE_discardSPPass();  // separate from rl_abort_record, which returns early when not recording
		goto level_loop;
	}

	/*Filtration*/
	// Special Tint is a local paint choice; the flare grade remains simulation state.
	if (filterActive && !(flareOwnsFilter && !specialScreenTint))
	{
		if (render_list_recording)
			rl_rec_filter_screen(levelFilter, levelBrightness);

		// Apply full-screen grades before capturing feedback residuals.
		if (anySmoothies)
			JE_filterScreenApply(VGAScreen2, levelFilter, levelBrightness);

		JE_filterScreenApply(VGAScreen, levelFilter, levelBrightness);
	}

	// Smoothie levels already drew+recorded the enemy bars before the residual
	// snapshot (above) so they interpolate; here we draw them for normal levels only.
	if (!anySmoothies)
	{
		draw_enemy_health_bars();
		hud_draw_ship_hp_bars();
	}
	draw_boss_bar();
	JE_updateGaugeFlash();

	JE_inGameDisplays();

	// Render-list capture for this tick ends here (everything below composites
	// or presents; it does not draw into the playfield).
	rl_end_record();
	rl_finalize();  // match against previous frame -> per-command motion deltas
	if (!anySmoothies)
		rl_capture_residual(game_screen, VGAScreen2);  // non-blit pixels (superpixels, boss bar)
	else
		// Reapply overlay-only changes to the display frame without filtering.
		rl_capture_residual_delta(VGAScreen2, game_screen);
	vt_ship_tick();       // fold external forces / repositions into the variable-dt ship
	// Publish tick velocity for render interpolation of ship-attached shots.
	vt_ship_publish_velocity();
#if RL_SELFTEST
	{
		static int seen = 0;
		if (seen < 5)
		{
			++seen;
			fprintf(stderr, "RL: in-game frame %d, anySmoothies=%d filterActive=%d cmds=%zu\n",
			        seen, (int)anySmoothies, (int)filterActive, rl_count());
		}
	}
	// Without smoothie per-pixel effects the replayed list (including the recorded
	// full-screen colour filter) must reproduce game_screen exactly.
	if (!anySmoothies)
	{
		const size_t mism = rl_replay_and_compare(VGAScreen2, game_screen);
		static int clean_frames = 0, bad_frames = 0;
		++clean_frames;
		if (mism != 0)
		{
			++bad_frames;
			if (bad_frames <= 20)
				fprintf(stderr, "RL selftest: %zu mismatched bytes (clean frame %d, %zu cmds)\n",
				        mism, clean_frames, rl_count());
		}
		else if (clean_frames % 120 == 1)
		{
			fprintf(stderr, "RL selftest: clean frame %d OK (%zu cmds)\n", clean_frames, rl_count());
		}
	}
#endif

	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	JE_starShowVGA();

	if (reallyEndLevel)
	{
		goto start_level;
	}
	goto level_loop;
}

Sint16 JE_newEnemy(int enemyOffset, Uint16 eDatI, Sint16 uniqueShapeTableI)
{
	for (int i = enemyOffset; i < enemyOffset + 25; ++i)
	{
		if (enemyAvail[i] == 1)
		{
			enemyAvail[i] = JE_makeEnemy(&enemy[i], eDatI, uniqueShapeTableI);
			enemy_armed_flash_clear(i);
			return i + 1;
		}
	}
	
	return 0;
}

/* Reroll weapon pickups within their current episode class. */
enum
{
	BALL_CLASS_FRONT,     // value 30001..30999 -> front weapon
	BALL_CLASS_REAR,      // value 31000..31999 -> rear weapon
	BALL_CLASS_SIDEKICK,  // value 32000..32099 -> sidekick
	BALL_CLASS_SPECIAL,   // value 32100+       -> special weapon
	BALL_CLASS_COUNT
};

#define BALL_POOL_MAX 32

static Uint16 ballPool[BALL_CLASS_COUNT][BALL_POOL_MAX];
static int ballPoolLen[BALL_CLASS_COUNT];

// Ball enemies sit on the Power-ups sprite sheet (shape bank 26) and encode what they grant
// in `value`; JE_playerCollide (player_ship.c) decodes the very same ranges on pickup.
static int arcadeBallClass(Uint16 eDatI)
{
	if (eDatI > ENEMY_NUM || enemyDat[eDatI].shapebank != 26)
		return -1;

	const Sint16 value = enemyDat[eDatI].value;
	if (value <= 30000)  // 30000 is the purple ball; nothing below it on this sheet grants a weapon
		return -1;
	if (value < 31000)
		return BALL_CLASS_FRONT;
	if (value < 32000)
		return BALL_CLASS_REAR;
	if (value < 32100)
		return BALL_CLASS_SIDEKICK;
	return BALL_CLASS_SPECIAL;
}

// Rebuilt per level, after the episode's item data is in place (see the level init below).
static void JE_buildArcadeBallPools(void)
{
	memset(ballPoolLen, 0, sizeof(ballPoolLen));

	for (Uint16 i = 0; i <= ENEMY_NUM; ++i)
	{
		const int cls = arcadeBallClass(i);
		if (cls < 0 || ballPoolLen[cls] >= BALL_POOL_MAX)
			continue;
		ballPool[cls][ballPoolLen[cls]++] = i;
	}
}

// Applies to Arcade and Super Arcade, excluding scripted SuperTyrian/Galaga loadouts and Endless.
static bool arcadeRandomPickupsOn(void)
{
	return arcadeRandomBalls
	    && arcade_rules_active()
	    && !superTyrian
	    && !galagaMode
	    && !timedBattleMode
	    && !endlessMode;
}

// Super Arcade rerolls its five-color weapon index after spawn, not the ball id here.
static bool arcadeBallRandomActive(void)
{
	return arcadeRandomPickupsOn() && superArcadeMode == SA_NONE;
}

// A Super Arcade color index selects one of the current ship's five weapons.
bool arcadeSuperPickupRandomActive(void)
{
	return arcadeRandomPickupsOn() && superArcadeMode != SA_NONE;
}

// One simulation RNG draw per ball; the host also synchronizes the toggle.
static Uint16 arcadeRandomizeBall(Uint16 eDatI)
{
	const int cls = arcadeBallClass(eDatI);
	if (cls < 0 || ballPoolLen[cls] == 0)
		return eDatI;

	return ballPool[cls][mt_rand() % ballPoolLen[cls]];
}

static uint JE_makeEnemy(struct JE_SingleEnemyType *enemy, Uint16 eDatI, Sint16 uniqueShapeTableI)
{
	uint avail;

	JE_byte shapeTableI;

	if (superArcadeMode != SA_NONE && eDatI == 534)
		eDatI = 533;

	// Reroll at creation so every scripted and death-drop path is covered.
	if (arcadeBallRandomActive())
		eDatI = arcadeRandomizeBall(eDatI);

	// Endless: a weapon powerup whose gun is already maxed becomes the other gun's powerup, or the
	// 5000 gem when both are full. Here rather than at the drop site so every spawn path is covered.
	if (endlessMode)
		eDatI = endlessResolvePowerupDrop(eDatI);

	if (uniqueShapeTableI > 0)
	{
		shapeTableI = uniqueShapeTableI;
	}
	else
	{
		shapeTableI = enemyDat[eDatI].shapebank;
	}

	Sprite2_array *sprite2s = NULL;
	if (shapeTableI == 21)
	{
		sprite2s = &spriteSheet11;  // Coins&Gems
	}
	else if (shapeTableI == 26)
	{
		sprite2s = &spriteSheet10;  // Two-Player Stuff
	}
	else
	{
		for (size_t i = 0; i < COUNTOF(enemySpriteSheetIds); ++i)
			if (shapeTableI == enemySpriteSheetIds[i])
				sprite2s = &enemySpriteSheets[i];
	}
	
	if (sprite2s != NULL)
		enemy->sprite2s = sprite2s;
	else
		// Use shape table value from previous enemy that occupied the enemy slot. (Ex. APPROACH.)
		fprintf(stderr, "warning: ignoring sprite from unloaded shape table %d\n", shapeTableI);

	enemy->enemydatofs = &enemyDat[eDatI];

	enemy->mapoffset = 0;
	enemy->mapoffset_frac = 0.0f;
	enemy->scroll_ybase = 0;
	enemy->scroll_yfrac = 0.0f;
	enemy->scroll_ylayer = 0;

	for (uint i = 0; i < 3; ++i)
	{
		enemy->eshotmultipos[i] = 0;
		enemy->eshotextracredit[i] = 0;
	}

	enemy->enemyground = (enemyDat[eDatI].explosiontype & 1) == 0;
	enemy->explonum = enemyDat[eDatI].explosiontype >> 1;

	enemy->launchfreq = enemyDat[eDatI].elaunchfreq;
	enemy->launchwait = enemyDat[eDatI].elaunchfreq;

	// T2000 ... Account for the second enemy bank only if we're creating something from it
	if (eDatI > 1000)
	{
		enemy->launchtype = enemyDat[eDatI].elaunchtype;
		enemy->launchspecial = 0;
	}
	else
	{
		enemy->launchtype = enemyDat[eDatI].elaunchtype % 1000;
		enemy->launchspecial = enemyDat[eDatI].elaunchtype / 1000;
	}

	// Start all four restored dispenser pieces on the same animation tick.
	if (dispenserBasesActive && eDatI >= 80 && eDatI <= 83)
	{
		enemy->launchfreq = 40;
		enemy->launchwait = 40;
		enemy->launchspecial = 0;
	}

	enemy->xaccel = enemyDat[eDatI].xaccel;
	enemy->yaccel = enemyDat[eDatI].yaccel;

	// Homing tiers floor weak tracking at 90, 92, or 96. Stronger tracking survives;
	// Rampage also adds collision damage. Scenery is restored on its first processed frame.
	if (endlessFxActive())
	{
		const int trackFloor = (endlessActiveMods & ENDLESS_MOD_RAMPAGE)  ? 96
		                     : (endlessActiveMods & ENDLESS_MOD_KAMIKAZE) ? 92
		                     : (endlessActiveMods & ENDLESS_MOD_HOMING)   ? 90
		                     : 0;
		if (trackFloor)
		{
			if (enemy->xaccel < trackFloor) enemy->xaccel = trackFloor;
			if (enemy->yaccel < trackFloor) enemy->yaccel = trackFloor;
		}
	}

	enemy->xminbounce = -10000;
	enemy->xmaxbounce = 10000;
	enemy->yminbounce = -10000;
	enemy->ymaxbounce = 10000;
	/*Far enough away to be impossible to reach*/

	for (uint i = 0; i < 3; ++i)
	{
		enemy->tur[i] = enemyDat[eDatI].tur[i];
	}

	enemy->ani = enemyDat[eDatI].ani;
	enemy->animin = 1;

	switch (enemyDat[eDatI].animate)
	{
	case 0:
		enemy->enemycycle = 1;
		enemy->aniactive = 0;
		enemy->animax = 0;
		enemy->aniwhenfire = 0;
		break;
	case 1:
		enemy->enemycycle = 0;
		enemy->aniactive = 1;
		enemy->animax = 0;
		enemy->aniwhenfire = 0;
		break;
	case 2:
		enemy->enemycycle = 1;
		enemy->aniactive = 2;
		enemy->animax = enemy->ani;
		enemy->aniwhenfire = 2;
		break;
	}

	if (enemyDat[eDatI].startxc != 0)
		enemy->ex = enemyDat[eDatI].startx + (mt_rand() % (enemyDat[eDatI].startxc * 2)) - enemyDat[eDatI].startxc + 1;
	else
		enemy->ex = enemyDat[eDatI].startx + 1;

	if (enemyDat[eDatI].startyc != 0)
		enemy->ey = enemyDat[eDatI].starty + (mt_rand() % (enemyDat[eDatI].startyc * 2)) - enemyDat[eDatI].startyc + 1;
	else
		enemy->ey = enemyDat[eDatI].starty + 1;

	enemy->exc = enemyDat[eDatI].xmove;
	enemy->eyc = enemyDat[eDatI].ymove;
	enemy->excc = enemyDat[eDatI].xcaccel;
	enemy->eycc = enemyDat[eDatI].ycaccel;
	enemy->exccw = abs(enemy->excc);
	enemy->exccwmax = enemy->exccw;
	enemy->eyccw = abs(enemy->eycc);
	enemy->eyccwmax = enemy->eyccw;
	enemy->exccadd = (enemy->excc > 0) ? 1 : -1;
	enemy->eyccadd = (enemy->eycc > 0) ? 1 : -1;
	enemy->special = false;
	enemy->iced = 0;

	if (enemyDat[eDatI].xrev == 0)
		enemy->exrev = 100;
	else if (enemyDat[eDatI].xrev == -99)
		enemy->exrev = 0;
	else
		enemy->exrev = enemyDat[eDatI].xrev;

	if (enemyDat[eDatI].yrev == 0)
		enemy->eyrev = 100;
	else if (enemyDat[eDatI].yrev == -99)
		enemy->eyrev = 0;
	else
		enemy->eyrev = enemyDat[eDatI].yrev;

	enemy->exca = (enemy->xaccel > 0) ? 1 : -1;
	enemy->eyca = (enemy->yaccel > 0) ? 1 : -1;

	enemy->enemytype = eDatI;

	for (uint i = 0; i < 3; ++i)
	{
		if (enemy->tur[i] == 252)
			enemy->eshotwait[i] = 1;
		else if (enemy->tur[i] > 0)
			enemy->eshotwait[i] = 20;
		else
			enemy->eshotwait[i] = 255;
	}
	for (uint i = 0; i < 20; ++i)
		enemy->egr[i] = enemyDat[eDatI].egraphic[i];
	enemy->size = enemyDat[eDatI].esize;
	enemy->linknum = 0;
	enemy->edamaged = enemyDat[eDatI].dani < 0;
	enemy->enemydie = enemyDat[eDatI].eenemydie;

	enemy->freq[1-1] = enemyDat[eDatI].freq[1-1];
	enemy->freq[2-1] = enemyDat[eDatI].freq[2-1];
	enemy->freq[3-1] = enemyDat[eDatI].freq[3-1];

	enemy->edani   = enemyDat[eDatI].dani;
	enemy->edgr    = enemyDat[eDatI].dgr;
	enemy->edlevel = enemyDat[eDatI].dlevel;

	enemy->fixedmovey = 0;
	enemy->fixedmovey_carry = 0;
	enemy->fixedmovey_carry_base = 0;
	enemy->fixedmovey_carry_move = 0;
	enemy->approach_debt_y100 = 0;

	enemy->filter = 0x00;

	int tempValue = 0;
	if (enemyDat[eDatI].value > 1 && enemyDat[eDatI].value < 10000)
	{
		switch (difficultyLevel)
		{
		case -1:
		case DIFFICULTY_WIMP:
			tempValue = enemyDat[eDatI].value * 0.75f;
			break;
		case DIFFICULTY_EASY:
		case DIFFICULTY_NORMAL:
			tempValue = enemyDat[eDatI].value;
			break;
		case DIFFICULTY_HARD:
			tempValue = enemyDat[eDatI].value * 1.125f;
			break;
		case DIFFICULTY_IMPOSSIBLE:
			tempValue = enemyDat[eDatI].value * 1.5f;
			break;
		case DIFFICULTY_INSANITY:
			tempValue = enemyDat[eDatI].value * 2;
			break;
		case DIFFICULTY_SUICIDE:
			tempValue = enemyDat[eDatI].value * 2.5f;
			break;
		case DIFFICULTY_MANIACAL:
		case DIFFICULTY_LORD_OF_GAME:
			tempValue = enemyDat[eDatI].value * 4;
			break;
		case DIFFICULTY_NORTANEOUS:
		case DIFFICULTY_10:
			tempValue = enemyDat[eDatI].value * 8;
			break;
		}
		if (expertMode)  // expert-mode cash bonus to offset the harsher economy
			tempValue = tempValue * expertScorePct / 100;
		if (tempValue > 10000)
			tempValue = 10000;
		enemy->evalue = tempValue;
	}
	else
	{
		enemy->evalue = enemyDat[eDatI].value;
	}

	int tempArmor = 1;
	if (enemyDat[eDatI].armor > 0)
	{
		if (enemyDat[eDatI].armor != 255)
		{
			switch (difficultyLevel)
			{
			case -1:
			case DIFFICULTY_WIMP:
				tempArmor = enemyDat[eDatI].armor * 0.5f + 1;
				break;
			case DIFFICULTY_EASY:
				tempArmor = enemyDat[eDatI].armor * 0.75f + 1;
				break;
			case DIFFICULTY_NORMAL:
				tempArmor = enemyDat[eDatI].armor;
				break;
			case DIFFICULTY_HARD:
				tempArmor = enemyDat[eDatI].armor * 1.2f;
				break;
			case DIFFICULTY_IMPOSSIBLE:
				tempArmor = enemyDat[eDatI].armor * 1.5f;
				break;
			case DIFFICULTY_INSANITY:
				tempArmor = enemyDat[eDatI].armor * 1.8f;
				break;
			case DIFFICULTY_SUICIDE:
				tempArmor = enemyDat[eDatI].armor * 2;
				break;
			case DIFFICULTY_MANIACAL:
				tempArmor = enemyDat[eDatI].armor * 3;
				break;
			case DIFFICULTY_LORD_OF_GAME:
				tempArmor = enemyDat[eDatI].armor * 4;
				break;
			case DIFFICULTY_NORTANEOUS:
			case DIFFICULTY_10:
				tempArmor = enemyDat[eDatI].armor * 8;
				break;
			}

			if (endlessFxActive())
				tempArmor = tempArmor * endlessArmorPercent() / 100;

			// Expert mode toughens every enemy; bosses sit near the 254 cap already
			// and get their extra HP from expertBossHpMult instead.
			if (expertMode)
				tempArmor = tempArmor * expertEnemyArmorPct / 100;

			if (tempArmor > 254)
			{
				tempArmor = 254;
			}
		}
		else
		{
			tempArmor = 255;
		}

		enemy->armorleft = tempArmor;

		avail = 0;
		enemy->scoreitem = false;
	}
	else
	{
		avail = 2;
		enemy->armorleft = 255;
		if (enemy->evalue != 0)
			enemy->scoreitem = true;
	}

	enemy->damageAccum = 0;  // reset the scaled-HP damage accumulator on (re)spawn
	enemy->chainWave = 0;    // a wave still in the air must not skip the slot's new occupant
	enemy->healthbar_seen = false;  // no enemy HP bar until this slot takes damage
	enemy->healthbar_max = 0;       // an invincible spawn has no health value and keeps this 0
	enemy_note_full_armor(enemy);   // any other spawn takes its scaled armor as full
	enemy->eliteState = 0;  // endless: elite undecided until first processed (see JE_drawEnemy)
	enemy->groupHomed = false;
	enemy->attractedBy = 0;
	// Only an enemy that can actually track draws for a side, so the roll costs the shared RNG
	// stream nothing on the score pickups and straight-line traffic that make up most of a zone.
	enemy->homeTarget = (enemy->xaccel || enemy->yaccel) ? (JE_byte)endlessRollHomingTarget() : 0;

	if (!enemy->scoreitem)
	{
		totalEnemy++;  /*Destruction ratio*/
	}

	/* indicates what to set ENEMYAVAIL to */
	return avail;
}

// Signed round-to-nearest division. Spawn catch-up values are small, but keeping the negative
// path symmetric matters for fixedmovey/eyc combinations whose net motion is upward.
static int event_scroll_round_div(int numerator, int denominator)
{
	if (denominator <= 0)
		return 0;
	return numerator >= 0
	       ? (numerator + denominator / 2) / denominator
	       : -((-numerator + denominator / 2) / denominator);
}

// Catch a new layer-bound enemy up through the remainder of the previous scroll
// tick. Use actual layer motion so fractional carry and fixed movement stay correct.
int event_enemy_scroll_catchup(JE_word enemyOffset, const struct JE_SingleEnemyType *e)
{
	int layer = 0;
	if (enemyOffset == 25 || enemyOffset == 75)
		layer = 1;
	else if (enemyOffset == 50)
		layer = 3;
	else if (enemyOffset == 0 && enemy_rides_layer2(e))
		layer = 2;  // attached sky scenery rides layer 2 through eyc and/or fixedmovey (see JE_drawEnemy)
	else
		return 0;  // free-flying sky enemies are not vertically layer-bound

	const int eventTime = (int)eventRec[eventLoc - 1].eventtime;
	const int span = eventScrollTo - eventScrollFrom;
	if (!eventScrollCatchupValid || (int)curLoc != eventScrollTo || span <= 0 ||
	    eventTime <= eventScrollFrom || eventTime > eventScrollTo)
		return 0;  // exact-time event, level/event jump, or a non-scroll forceEvents interval

	const int late = eventScrollTo - eventTime;

	if (layer == 2)
	{
		// Anchor sky spawns to one ideal layer/event phase to avoid one-pixel seams.
		if (!eventScrollSkyValid || late < 0)
			return 0;
		int catchup = event_scroll_round_div(eventScrollSkyRatio100 * late +
		                                     eventScrollSkyPhase100, 100);
		const int surplus = ((int)e->fixedmovey + (e->eycc != 0 ? 0 : (int)e->eyc)) -
		                    (int)backMove2;
		if (surplus != 0)
			catchup += event_scroll_round_div(surplus * late, span);
		return catchup;
	}

	if (late <= 0)
		return 0;

	const int fixedMoveRaw = e->fixedmovey;
	const int scalable = enemy_scalable_fixed_y(fixedMoveRaw, e->eyc);
	int fixedMoveScaled = scalable;
	const int baseStep = eventScrollBaseStep[layer];
	if (scalable != 0 && eventScrollBoost > 0 && baseStep > 0)
	{
		if (eventScrollDelayMax[layer] == 1)
			fixedMoveScaled = scalable * eventScrollLayerDelta[layer] / baseStep;
		else
			fixedMoveScaled = scalable * (100 + eventScrollBoost) / 100;
	}
	const int fixedMove = (fixedMoveRaw - scalable) + fixedMoveScaled;

	// Apply the fraction of the previous tick after this event coordinate.
	const int fullMove = eventScrollLayerDelta[layer] + fixedMove + e->eyc;
	return event_scroll_round_div(fullMove * late, span);
}

void JE_createNewEventEnemy(JE_byte enemyTypeOfs, JE_word enemyOffset, Sint16 uniqueShapeTableI)
{
	int i;

	b = 0;

	for (i = enemyOffset; i < enemyOffset + 25; i++)
	{
		if (enemyAvail[i] == 1)
		{
			b = i + 1;
			break;
		}
	}

	if (b == 0)
		return;

	tempW = eventRec[eventLoc-1].eventdat + enemyTypeOfs;

	enemyAvail[b-1] = JE_makeEnemy(&enemy[b-1], tempW, uniqueShapeTableI);
	enemy_armed_flash_clear(b-1);

	// When T2000 gives an X position of -200, what it actually wants is a random X position...
	if (eventRec[eventLoc-1].eventdat2 == -200)
	{
		// Ranged 24 - 231
		eventRec[eventLoc-1].eventdat2 = (mt_rand() % 208) + 24;
	}

	if (eventRec[eventLoc-1].eventdat2 != -99)
	{
		switch (enemyOffset)
		{
		case 0:
			enemy[b - 1].ex = eventRec[eventLoc - 1].eventdat2 - (mapX - 3) * 24;
			enemy[b - 1].ey -= backMove2;
			break;
		case 25:
		case 75:
			enemy[b - 1].ex = eventRec[eventLoc - 1].eventdat2 - (mapX - 3) * 24 - 12;
			enemy[b - 1].ey -= backMove;
			break;
		case 50:
			if (background3x1)
				enemy[b - 1].ex = eventRec[eventLoc - 1].eventdat2 - (mapX - 3) * 24 - 12;
			else
				enemy[b - 1].ex = eventRec[eventLoc - 1].eventdat2 - mapX3 * 24 + 6;
			enemy[b - 1].ey -= backMove3;

			if (background3x1b)
				enemy[b-1].ex -= 6;
			break;
		}
		enemy[b-1].ey = -28;
		if (background3x1b && enemyOffset == 50)
			enemy[b-1].ey += 4;
	}

	if (smallEnemyAdjust && enemy[b-1].size == 0)
	{
		enemy[b-1].ex -= 10;
		enemy[b-1].ey -= 7;
	}

	enemy[b - 1].ey += eventRec[eventLoc - 1].eventdat5;
	enemy[b - 1].eyc += eventRec[eventLoc - 1].eventdat3;
	enemy[b - 1].linknum = eventRec[eventLoc - 1].eventdat4;
	enemy[b - 1].fixedmovey = eventRec[eventLoc - 1].eventdat6;
	enemy[b - 1].fixedmovey_carry = 0;
	enemy[b - 1].fixedmovey_carry_base = 0;
	enemy[b - 1].fixedmovey_carry_move = 0;
	enemy[b - 1].ey += event_enemy_scroll_catchup(enemyOffset, &enemy[b - 1]);
}

/* Register file-local simulation state only. */

void game_loop_register_rollback(void)
{
	rollback_register("t2.eventRec",         eventRec, sizeof(eventRec));
	enemy_death_register_rollback();
	vt_ship_register_rollback();
	rollback_register("t2.tempMapXOfsFrac",  &tempMapXOfs_frac, sizeof(tempMapXOfs_frac));
	rollback_register("t2.tempMapXOfsLayer", &tempMapXOfs_layer, sizeof(tempMapXOfs_layer));
	rollback_register("t2.tempScrollYBase",  &tempScrollYBase, sizeof(tempScrollYBase));
	rollback_register("t2.tempScrollYBaseB", &tempScrollYBaseBar, sizeof(tempScrollYBaseBar));
	rollback_register("t2.tempScrollYfrac",  &tempScrollYfrac, sizeof(tempScrollYfrac));
	rollback_register("t2.tempScrollYfracB", &tempScrollYfracBar, sizeof(tempScrollYfracBar));
	rollback_register("t2.tempScrollYLayer", &tempScrollYLayer, sizeof(tempScrollYLayer));
	rollback_register("t2.tempScrollBase",   &tempScrollBaseStep, sizeof(tempScrollBaseStep));
	rollback_register("t2.tempScrollDelay",  &tempScrollDelayMax, sizeof(tempScrollDelayMax));
	rollback_register("t2.tempScrollExtra",  &tempScrollExtraPx, sizeof(tempScrollExtraPx));
	rollback_register("t2.skyGlue",          &skyGlueThisEnemy, sizeof(skyGlueThisEnemy));
	rollback_register("t2.evScrollValid",    &eventScrollCatchupValid, sizeof(eventScrollCatchupValid));
	rollback_register("t2.evScrollFrom",     &eventScrollFrom, sizeof(eventScrollFrom));
	rollback_register("t2.evScrollTo",       &eventScrollTo, sizeof(eventScrollTo));
	rollback_register("t2.evScrollDelta",    eventScrollLayerDelta, sizeof(eventScrollLayerDelta));
	rollback_register("t2.evScrollBase",     eventScrollBaseStep, sizeof(eventScrollBaseStep));
	rollback_register("t2.evScrollDelayMax", eventScrollDelayMax, sizeof(eventScrollDelayMax));
	rollback_register("t2.evScrollBoost",    &eventScrollBoost, sizeof(eventScrollBoost));
	rollback_register("t2.evSkyValid",       &eventScrollSkyValid, sizeof(eventScrollSkyValid));
	rollback_register("t2.evSkyRatio",       &eventScrollSkyRatio100, sizeof(eventScrollSkyRatio100));
	rollback_register("t2.evSkyPhase",       &eventScrollSkyPhase100, sizeof(eventScrollSkyPhase100));
	rollback_register("t2.bgSmoothRate",     bgSmoothRatePend, sizeof(bgSmoothRatePend));
	rollback_register("t2.bgSmoothFrac",     bgSmoothFracPend, sizeof(bgSmoothFracPend));
	rollback_register("t2.bgSmoothActive",   &bgSmoothActivePend, sizeof(bgSmoothActivePend));
}
