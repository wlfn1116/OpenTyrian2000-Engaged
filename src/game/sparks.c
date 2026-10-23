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
#include "sparks.h"

#include "config.h"
#include "endless.h"
#include "game_state.h"
#include "mtrand.h"
#include "render_list.h"
#include "rollback.h"
#include "sprite.h"
#include "video.h"

#include <math.h>
#include <string.h>

/*SuperPixels*/
superpixel_type superpixels[MAX_SUPERPIXELS]; /* [0..MaxSP] */
static unsigned int last_superpixel;           // shared cursor, exactly as when one window existed
static unsigned int last_uncapped_superpixel;  // write cursor for the high window
static unsigned int sp_live_lo = MAX_SUPERPIXELS, sp_live_hi;

bool superpixelClipActive = false;
int superpixelClipX0, superpixelClipY0, superpixelClipX1, superpixelClipY1;

void JE_setSPClip(int x0, int y0, int x1, int y1)
{
	superpixelClipActive = true;
	superpixelClipX0 = x0;
	superpixelClipY0 = y0;
	superpixelClipX1 = x1;
	superpixelClipY1 = y1;
}

void JE_clearSPClip(void)
{
	superpixelClipActive = false;
}

// On-screen Endless mystery pickups publish boxes that occlude later spark draws.
#define MAX_SP_OCCLUDERS 24  // additional sprites do not occlude sparks

static struct { int x0, y0, x1, y1; } sp_occluders[MAX_SP_OCCLUDERS];
static unsigned int sp_occluder_count;

void JE_addSPOccluder(int x0, int y0, int x1, int y1)
{
	if (sp_occluder_count >= COUNTOF(sp_occluders))
		return;

	sp_occluders[sp_occluder_count].x0 = x0;
	sp_occluders[sp_occluder_count].y0 = y0;
	sp_occluders[sp_occluder_count].x1 = x1;
	sp_occluders[sp_occluder_count].y1 = y1;
	++sp_occluder_count;
}

// Only an on-screen spark spawned with `occluded` reaches this, and the list is empty or one box
// long in nearly every frame.
static bool sp_hidden(int x, int y)
{
	for (unsigned int i = 0; i < sp_occluder_count; ++i)
	{
		if (x >= sp_occluders[i].x0 && x <= sp_occluders[i].x1 &&
		    y >= sp_occluders[i].y0 && y <= sp_occluders[i].y1)
			return true;
	}
	return false;
}

// Advance the shared cursor so all spark traffic still thins classic weapon trails. With Extra
// Sparks, uncapped effects retire that classic slot and write into the upper window instead.
static unsigned int next_superpixel(bool classic_cap)
{
	const unsigned int cap = (extraSparks && !classic_cap) ? MAX_SUPERPIXELS : SUPERPIXELS_CLASSIC;

	if (++last_superpixel >= cap)
		last_superpixel = 0;

	if (classic_cap || !extraSparks)
		return last_superpixel;

	if (last_superpixel < SUPERPIXELS_CLASSIC)
		superpixels[last_superpixel].z = 0;

	if (++last_uncapped_superpixel >= MAX_SUPERPIXELS || last_uncapped_superpixel < SUPERPIXELS_CLASSIC)
		last_uncapped_superpixel = SUPERPIXELS_CLASSIC;
	if (last_uncapped_superpixel < sp_live_lo)
		sp_live_lo = last_uncapped_superpixel;
	if (last_uncapped_superpixel > sp_live_hi)
		sp_live_hi = last_uncapped_superpixel;
	return last_uncapped_superpixel;
}

/* Spark-ring checkpoint for a drawing pass that rollback may discard and replay. */
static unsigned int sp_pass_superpixel, sp_pass_uncapped;
static bool sp_pass_advanced;   // JE_drawSP stepped the ring during this pass

void JE_beginSPPass(void)
{
	sp_pass_superpixel = last_superpixel;
	sp_pass_uncapped = last_uncapped_superpixel;
	sp_pass_advanced = false;
}

static void sp_rewind(unsigned int i)
{
	if (superpixels[i].z == 0 || superpixels[i].z >= SUPERPIXEL_SPAWN_Z)
		return;
	superpixels[i].x -= superpixels[i].delta_x;
	superpixels[i].y -= superpixels[i].delta_y;
	++superpixels[i].z;
}

void JE_discardSPPass(void)
{
	last_superpixel = sp_pass_superpixel;
	last_uncapped_superpixel = sp_pass_uncapped;

	if (sp_pass_advanced)
	{
		/* Rewind JE_drawSP's step; retired and newly spawned sparks did not take it. */
		for (unsigned int i = 0; i < SUPERPIXELS_CLASSIC; ++i)
			sp_rewind(i);
		for (unsigned int i = sp_live_lo; i <= sp_live_hi; ++i)
			sp_rewind(i);
	}
	sp_pass_advanced = false;
}

void JE_resetSP(void)
{
	last_superpixel = 0;
	last_uncapped_superpixel = SUPERPIXELS_CLASSIC;
	sp_occluder_count = 0;
	sp_pass_superpixel = 0;
	sp_pass_uncapped = SUPERPIXELS_CLASSIC;
	sp_pass_advanced = false;
	memset(superpixels, 0, sizeof(superpixels));
	sp_live_lo = MAX_SUPERPIXELS;
	sp_live_hi = 0;
}

// The simulation-RNG shower behind JE_doSP and JE_doSPBrief; `life` is the spawn z.
static void sp_scatter(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color,
                       bool classic_cap, JE_byte life, JE_byte bright)
{
	// `num` can exceed 255; an 8-bit loop counter would wrap and never terminate.
	for (int sp = 0; sp < num; sp++)
	{
		JE_real tempr = mt_rand_lt1() * (2 * M_PI);
		signed int tempy = roundf(cosf(tempr) * mt_rand_1() * explowidth);
		signed int tempx = roundf(sinf(tempr) * mt_rand_1() * explowidth);

		// Silent replay keeps RNG draws but does not emit duplicate spark pixels.
		if (rollback_resim_silent)
			continue;

		const unsigned int slot = next_superpixel(classic_cap);
		superpixels[slot].x = tempx + x;
		superpixels[slot].y = tempy + y;
		superpixels[slot].delta_x = tempx;
		superpixels[slot].delta_y = tempy + 1;
		superpixels[slot].color = color;
		superpixels[slot].bright = bright;
		superpixels[slot].occluded = false;
		superpixels[slot].z = life;
	}
}

void JE_doSP(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color, bool classic_cap) /* superpixels */
{
	sp_scatter(x, y, num, explowidth, color, classic_cap, SUPERPIXEL_SPAWN_Z, 0);
}

void JE_doSPBrief(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color,
                  JE_byte life, JE_byte bright)
{
	// z == 0 is a free slot; SUPERPIXEL_SPAWN_Z is the longest life the ring is written around.
	if (life == 0)
		life = 1;
	else if (life > SUPERPIXEL_SPAWN_Z)
		life = SUPERPIXEL_SPAWN_Z;

	sp_scatter(x, y, num, explowidth, color, false, life, bright);
}

// Each draw avalanches its own point on a fixed walk. See doc/notes.md#gauges-and-effects.
#define SP_SEED_STRIDE 0x9E3779B9u  // any odd stride works; the mixing is what spreads the angles

static Uint32 sp_mix32(Uint32 h)  // MurmurHash3 finalizer
{
	h ^= h >> 16;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	h ^= h >> 16;
	return h;
}

// JE_doSP driven by `seed` instead of the simulation RNG. Superpixels are not rollback state, so a
// presentation-only effect can spawn them this way without touching the deterministic stream.
void JE_doSPSeeded(JE_word x, JE_word y, JE_word num, JE_byte explowidth, JE_byte color,
                   bool classic_cap, JE_byte bright, bool occluded, Uint32 seed)
{
	for (int sp = 0; sp < num; sp++)
	{
		seed += SP_SEED_STRIDE;
		const JE_real angle = (JE_real)(sp_mix32(seed) >> 16) / 65536.0 * (2 * M_PI);
		seed += SP_SEED_STRIDE;
		const JE_real reach = (JE_real)(sp_mix32(seed) >> 16) / 65536.0;

		const signed int tempy = roundf(cosf(angle) * reach * explowidth);
		const signed int tempx = roundf(sinf(angle) * reach * explowidth);

		const unsigned int slot = next_superpixel(classic_cap);
		superpixels[slot].x = tempx + x;
		superpixels[slot].y = tempy + y;
		superpixels[slot].delta_x = tempx;
		superpixels[slot].delta_y = tempy + 1;
		superpixels[slot].color = color;
		superpixels[slot].bright = bright;
		superpixels[slot].occluded = occluded;
		superpixels[slot].z = SUPERPIXEL_SPAWN_Z;
	}
}

// Space sparks by distance, capped to the classic pool when Extra Sparks is off.
#define SP_SHAPE_SPARKS_MAX 24

static JE_word sp_shape_count(JE_real span, JE_byte spacing)
{
	if (spacing == 0)
		return 1;
	const int n = (int)(span / (JE_real)spacing);
	if (n < 1)
		return 1;
	return (JE_word)(n > SP_SHAPE_SPARKS_MAX ? SP_SHAPE_SPARKS_MAX : n);
}

void JE_doSPRingSeeded(JE_word x, JE_word y, JE_word radius, JE_byte spacing, JE_byte color,
                       JE_byte life, JE_byte bright, Uint32 seed)
{
	if (radius == 0 || life == 0)
		return;
	if (life > SUPERPIXEL_SPAWN_Z)
		life = SUPERPIXEL_SPAWN_Z;

	const JE_word num = sp_shape_count(2 * M_PI * (JE_real)radius, spacing);

	// A spark is spawned one step out and stepped again before each of its `life` draws, so this
	// reach puts its last drawn position on the ring.
	const JE_real step = (JE_real)radius / (JE_real)(life + 1);
	const JE_real gap = (2 * M_PI) / num;

	for (JE_word sp = 0; sp < num; sp++)
	{
		// Even spacing keeps the circle legible; the jitter stays inside one gap, so successive
		// rings at the same spot do not plot the same points.
		seed += SP_SEED_STRIDE;
		const JE_real angle = gap * sp + ((JE_real)(sp_mix32(seed) >> 16) / 65536.0 - 0.5) * gap;
		const signed int tempx = roundf(sinf(angle) * step);
		const signed int tempy = roundf(cosf(angle) * step);

		const unsigned int slot = next_superpixel(false);
		superpixels[slot].x = tempx + x;
		superpixels[slot].y = tempy + y;
		superpixels[slot].delta_x = tempx;
		superpixels[slot].delta_y = tempy;
		superpixels[slot].color = color;
		superpixels[slot].bright = bright;
		superpixels[slot].occluded = false;
		superpixels[slot].z = life;
	}
}

void JE_doSPBoltSeeded(JE_word x0, JE_word y0, JE_word x1, JE_word y1, JE_byte spacing, JE_byte wander,
                       JE_byte color, JE_byte life, JE_byte bright, Uint32 seed)
{
	if (life == 0)
		return;
	if (life > SUPERPIXEL_SPAWN_Z)
		life = SUPERPIXEL_SPAWN_Z;

	const JE_real dx = (JE_real)x1 - (JE_real)x0;
	const JE_real dy = (JE_real)y1 - (JE_real)y0;
	const JE_real len = sqrtf(dx * dx + dy * dy);
	if (len < 1.0)
		return;

	// Unit normal, along which each point is pushed off the straight line.
	const JE_real nx = -dy / len;
	const JE_real ny = dx / len;
	const JE_word num = sp_shape_count(len, spacing);

	for (JE_word sp = 0; sp < num; sp++)
	{
		// Points run from one step off the source to the victim itself. The bow follows a sine
		// over the whole segment, so it is widest in the middle and vanishes onto the victim.
		const JE_real t = (JE_real)(sp + 1) / (JE_real)num;
		seed += SP_SEED_STRIDE;
		const JE_real swing = ((JE_real)(sp_mix32(seed) >> 16) / 32768.0 - 1.0) * wander * sinf(M_PI * t);

		const unsigned int slot = next_superpixel(false);
		superpixels[slot].x = (unsigned int)(signed int)roundf(x0 + dx * t + nx * swing);
		superpixels[slot].y = (unsigned int)(signed int)roundf(y0 + dy * t + ny * swing);
		superpixels[slot].delta_x = 0;   // a bolt holds its shape and fades in place
		superpixels[slot].delta_y = 0;
		superpixels[slot].color = color;
		superpixels[slot].bright = bright;
		superpixels[slot].occluded = false;
		superpixels[slot].z = life;
	}
}

// Fall rates a drop is dealt, in px per tick. The slowest still clears its own trail over a short
// life, and the range is what makes a shower spread into separate runs instead of one bar.
#define SP_DRIP_FALL_MIN 1
#define SP_DRIP_FALL_MAX 3

void JE_doSPDripSeeded(JE_word x, JE_word y, JE_word num, JE_byte spread, JE_byte color,
                       JE_byte life, JE_byte bright, Uint32 seed)
{
	if (life == 0)
		return;
	if (life > SUPERPIXEL_SPAWN_Z)
		life = SUPERPIXEL_SPAWN_Z;

	for (JE_word sp = 0; sp < num; sp++)
	{
		seed += SP_SEED_STRIDE;
		const int offX = (int)(sp_mix32(seed) % (2u * spread + 1u)) - spread;
		seed += SP_SEED_STRIDE;
		const int offY = (int)(sp_mix32(seed) % (spread + 1u));
		seed += SP_SEED_STRIDE;
		const int fall = SP_DRIP_FALL_MIN
		               + (int)(sp_mix32(seed) % (SP_DRIP_FALL_MAX - SP_DRIP_FALL_MIN + 1));

		const unsigned int slot = next_superpixel(false);
		superpixels[slot].x = (unsigned int)((int)x + offX);
		superpixels[slot].y = (unsigned int)((int)y + offY);
		superpixels[slot].delta_x = 0;   // a drop runs straight down
		superpixels[slot].delta_y = fall;
		superpixels[slot].color = color;
		superpixels[slot].bright = bright;
		superpixels[slot].occluded = false;
		superpixels[slot].z = life;
	}
}

// Shape of the pop; the reach is also each spark's per-tick velocity, so it bounds the spread.
#define VAPORISE_SPARK_MIN   3
#define VAPORISE_SPARK_MAX   5
#define VAPORISE_SPARK_REACH 3

// The bullet's colour is its shooter's tier tint when it carries one, else the palette bank its
// sprite is mostly drawn in. Contract in sparks.h.
void enemy_shot_vaporise_sparks(unsigned int slot)
{
	if (rollback_resim_silent)
		return;

	const EnemyShotType *const es = &enemyShot[slot];

	// Sheet split and frame arithmetic as the in-game bullet draw does them (game_loop.c).
	const bool highSheet = (es->sgr >= 500);
	const unsigned int frame = es->sgr + es->animate - (highSheet ? 500 : 0);
	const Uint8 color = (es->filter != 0)
		? es->filter
		: (Uint8)(sprite2_dominant_bank(highSheet ? spriteSheet12 : spriteSheet8, frame) << 4);

	// Slot and position in separate bit fields, so bullets swept in the same frame scatter
	// differently. sp_mix32 spreads them; the count below reads the mixed low bits.
	const Uint32 seed = sp_mix32(((Uint32)slot << 24)
	                             ^ (((Uint32)es->sx & 0x1FF) << 12) ^ ((Uint32)es->sy & 0xFFF));
	const JE_word sparks = (JE_word)(VAPORISE_SPARK_MIN
	                                 + seed % (VAPORISE_SPARK_MAX - VAPORISE_SPARK_MIN + 1));

	// Thrown from the centre of the bullet's 12px cell.
	JE_doSPSeeded((JE_word)(es->sx + 6), (JE_word)(es->sy + 6), sparks, VAPORISE_SPARK_REACH,
	              color, false, ENDLESS_SPARK_BRIGHT, false, seed);
}

static void sp_step(unsigned int i)
{
	if (superpixels[i].z)
	{
		superpixels[i].x += superpixels[i].delta_x;
		superpixels[i].y += superpixels[i].delta_y;

		if (superpixels[i].x < (unsigned)VGAScreen->w && superpixels[i].y < (unsigned)VGAScreen->h
		    && !(superpixels[i].occluded && sp_hidden((int)superpixels[i].x, (int)superpixels[i].y))
		    && (!superpixelClipActive
		        || (superpixels[i].x >= (unsigned)superpixelClipX0
		            && superpixels[i].x < (unsigned)superpixelClipX1
		            && superpixels[i].y >= (unsigned)superpixelClipY0
		            && superpixels[i].y < (unsigned)superpixelClipY1)))
		{
			const Uint8 z = superpixels[i].z;
			const Uint8 color = superpixels[i].color;
			const Uint8 bright = superpixels[i].bright;

			// Record for the render list so the spark interpolates smoothly at the
			// display rate (constant velocity -> the per-tick delta is self-contained).
			if (render_list_recording)
				rl_rec_superpixel(superpixels[i].x, superpixels[i].y, superpixels[i].delta_x,
				                  superpixels[i].delta_y, z, color, bright);

			Uint8 *s = (Uint8 *)VGAScreen->pixels; /* screen pointer, 8-bit specific */
			s += superpixels[i].y * VGAScreen->pitch;
			s += superpixels[i].x;

			*s = rl_superpixel_value(*s, z, color, bright);
			if (superpixels[i].x > 0)
				*(s - 1) = rl_superpixel_value(*(s - 1), z >> 1, color, bright >> 1);
			if (superpixels[i].x < VGAScreen->w - 1u)
				*(s + 1) = rl_superpixel_value(*(s + 1), z >> 1, color, bright >> 1);
			if (superpixels[i].y > 0)
				*(s - VGAScreen->pitch) = rl_superpixel_value(*(s - VGAScreen->pitch), z >> 1, color, bright >> 1);
			if (superpixels[i].y < VGAScreen->h - 1u)
				*(s + VGAScreen->pitch) = rl_superpixel_value(*(s + VGAScreen->pitch), z >> 1, color, bright >> 1);
		}

		superpixels[i].z--;
	}
}

void JE_drawSP(void)
{
	sp_pass_advanced = true;  // a rollback discarding this pass has a step to rewind

	unsigned int live_lo = MAX_SUPERPIXELS, live_hi = 0;
	for (unsigned int i = sp_live_hi + 1; i-- > sp_live_lo; )
	{
		sp_step(i);
		if (superpixels[i].z != 0)
		{
			if (live_hi == 0)
				live_hi = i;
			live_lo = i;
		}
	}
	sp_live_lo = live_lo;
	sp_live_hi = live_hi;

	for (unsigned int i = SUPERPIXELS_CLASSIC; i-- > 0; )
		sp_step(i);

	sp_occluder_count = 0;  // the boxes describe the frame just drawn; the next one republishes
}
