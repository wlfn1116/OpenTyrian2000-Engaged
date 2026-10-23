#define ENDLESS_INTERNAL_ACCESS
#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "endless.h"
#include "episodes.h"
#include "endless_internal.h"
#include "fnv1a.h"
#include "game_state.h"
#include "mtrand.h"
#include "palette.h"
#include "player.h"
#include "render_list.h"
#include "rollback.h"
#include "sparks.h"
#include "sprite.h"
#include "video.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Which slots a shower lands in, so the per-weapon classic cap can be checked without a screen.
 * Returns the number of live sparks and the lowest and highest slot used. */
static int qa_spark_span(int *out_lo, int *out_hi)
{
	int live = 0;
	*out_lo = MAX_SUPERPIXELS;
	*out_hi = -1;
	for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
	{
		if (superpixels[i].z == 0)
			continue;
		++live;
		if ((int)i < *out_lo)
			*out_lo = (int)i;
		if ((int)i > *out_hi)
			*out_hi = (int)i;
	}
	return live;
}

/* The per-weapon superspark cap, from the setting through to the slots a trail occupies. */
void qa_test_superspark_caps(void)
{
	const bool savedExtra = extraSparks;
	bool savedCap[SSW_COUNT];
	memcpy(savedCap, superSparkClassicCap, sizeof(savedCap));

	for (int w = 0; w < SSW_COUNT; ++w)
		superSparkClassicCap[w] = true;

	/* Coverage: a graphic above 1000 draws a trail, and superSparkCapForSprite has to recognise
	 * its base sprite or the trail runs uncapped whatever the setting says. */
	int tagged = 0, uncovered = 0, firstUncoveredWpn = -1, firstUncoveredSg = -1;
	for (int wpn = 0; wpn <= WEAP_NUM; ++wpn)
	{
		/* The fire loop reads sg[shotMultiPos - 1] with the cursor wrapping at max, or at 8 when
		 * max is 0. Everything past that is padding the loader never filled. */
		int used = weapons[wpn].max ? weapons[wpn].max : 8;
		if (weapons[wpn].multi > used)
			used = weapons[wpn].multi;
		if (used > WEAPON_MULTI_MAX)
			used = WEAPON_MULTI_MAX;

		for (int m = 0; m < used; ++m)
		{
			/* Graphics above 60000 are option shapes; trail graphics are limited to 1001..15999. */
			const JE_word sg = weapons[wpn].sg[m];
			if (sg <= 1000 || sg / 1000 > 15)
				continue;
			++tagged;
			if (superSparkCapForSprite(sg % 1000))
				continue;
			++uncovered;
			if (firstUncoveredWpn < 0)
			{
				firstUncoveredWpn = wpn;
				firstUncoveredSg = sg;
			}
		}
	}
	if (uncovered)
		fprintf(stderr, "# %d of %d spark-tagged shot graphics have no cap setting"
		                " (first: weapon %d, graphic %d)\n",
		        uncovered, tagged, firstUncoveredWpn, firstUncoveredSg);
	qa_check(tagged > 0, "the loaded weapon data has spark-tagged shot graphics");

	/* The four the mapping names, at the tagged values JE_applySuperSparks writes. */
	qa_check(superSparkCapForSprite(7035 % 1000), "Mega Pulse's tagged graphic maps to its setting");
	qa_check(superSparkCapForSprite(7030 % 1000) && superSparkCapForSprite(7029 % 1000),
	         "both Wallop Beam bolts map to theirs");
	qa_check(superSparkCapForSprite(9028 % 1000), "Protron System -B- maps to its setting");
	qa_check(superSparkCapForSprite(9634 % 1000), "Ice Beam maps to its setting");

	/* The lookup takes the base graphic. A shot draws shotGr + shotAni, and feeding that in
	 * walks off the entry on every animated frame, which silently uncaps the trail. */
	static const JE_word taggedGraphics[] = { 7035, 7030, 7029, 9028, 9634 };
	bool animatedFramesMiss = false;
	for (unsigned int w = 0; w < COUNTOF(taggedGraphics); ++w)
		for (JE_word ani = 1; ani < 8; ++ani)
			if (!superSparkCapForSprite((taggedGraphics[w] + ani) % 1000))
				animatedFramesMiss = true;
	qa_check(animatedFramesMiss, "the drawn frame is not a valid key, so call sites must pass shotGr");

	/* Slots: a capped trail stays inside the classic window, an uncapped one leaves it. */
	int lo, hi;
	extraSparks = true;

	JE_resetSP();
	for (int t = 0; t < 40; ++t)
		JE_doSP(100, 100, 5, 3, 7 << 4, superSparkCapForSprite(7035 % 1000));
	qa_spark_span(&lo, &hi);
	qa_check(lo >= 0 && hi < SUPERPIXELS_CLASSIC,
	         "Mega Pulse with its cap on keeps every spark in the classic window");

	superSparkClassicCap[SSW_MEGA_PULSE] = false;
	JE_resetSP();
	for (int t = 0; t < 40; ++t)
		JE_doSP(100, 100, 5, 3, 7 << 4, superSparkCapForSprite(7035 % 1000));
	qa_spark_span(&lo, &hi);
	qa_check(lo >= SUPERPIXELS_CLASSIC, "with its cap off the same trail spawns outside that window");

	/* And the cap is what bounds it: 200 sparks would otherwise all be live at once. */
	superSparkClassicCap[SSW_MEGA_PULSE] = true;
	JE_resetSP();
	for (int t = 0; t < 40; ++t)
		JE_doSP(100, 100, 5, 3, 7 << 4, superSparkCapForSprite(7035 % 1000));
	const int cappedLive = qa_spark_span(&lo, &hi);
	qa_check(cappedLive <= SUPERPIXELS_CLASSIC, "a capped trail never holds more than the classic count");

	superSparkClassicCap[SSW_MEGA_PULSE] = false;
	JE_resetSP();
	for (int t = 0; t < 40; ++t)
		JE_doSP(100, 100, 5, 3, 7 << 4, superSparkCapForSprite(7035 % 1000));
	const int uncappedLive = qa_spark_span(&lo, &hi);
	qa_check(uncappedLive > cappedLive, "an uncapped trail holds more than a capped one");

	JE_resetSP();
	extraSparks = savedExtra;
	memcpy(superSparkClassicCap, savedCap, sizeof(savedCap));
}

static Uint32 qa_spark_hash(void)
{
	return fnv1a32(superpixels, sizeof(superpixels));
}

/* One shower, spawned and drawn the way a level frame does it. The seeded spawn stands in for the
   simulation RNG a rollback restores, so a re-run of the same frame emits the same sparks. */
static void qa_spark_frame(Uint32 seed)
{
	JE_beginSPPass();
	JE_doSPSeeded(100, 100, 5, 3, 7 << 4, false, 0, false, seed);
	JE_drawSP();
}

/* A replacement pass must reuse discarded spark slots instead of advancing the ring twice.
 * See doc/notes.md#gauges-and-effects. */
void qa_test_superspark_discarded_pass(void)
{
	if (VGAScreen == NULL || VGAScreen->format->BitsPerPixel != 8)
		return;

	const bool savedExtra = extraSparks;
	extraSparks = true;

	JE_resetSP();
	for (Uint32 f = 0; f < 3; ++f)
		qa_spark_frame(1234u + f);
	const Uint32 cleanHash = qa_spark_hash();

	JE_resetSP();
	for (Uint32 f = 0; f < 2; ++f)
		qa_spark_frame(1234u + f);
	qa_spark_frame(1236u);
	JE_discardSPPass();
	qa_spark_frame(1236u);
	qa_check(qa_spark_hash() == cleanHash,
	         "a discarded drawing pass leaves the spark ring where a clean run leaves it");

	/* Control: with the discard dropped, the same two calls have to disagree. */
	JE_resetSP();
	for (Uint32 f = 0; f < 2; ++f)
		qa_spark_frame(1234u + f);
	qa_spark_frame(1236u);
	qa_spark_frame(1236u);
	qa_check(qa_spark_hash() != cleanHash,
	         "without it the replayed pass spends a second step and a second set of slots");

	JE_resetSP();
	extraSparks = savedExtra;
}

static void qa_reference_spark_pass(superpixel_type *sp, SDL_Surface *s)
{
	for (int i = MAX_SUPERPIXELS; i--; )
	{
		if (sp[i].z == 0)
			continue;
		sp[i].x += sp[i].delta_x;
		sp[i].y += sp[i].delta_y;
		if (sp[i].x < (unsigned)s->w && sp[i].y < (unsigned)s->h)
		{
			const Uint8 z = sp[i].z, color = sp[i].color, bright = sp[i].bright;
			Uint8 *const p = (Uint8 *)s->pixels + sp[i].y * s->pitch + sp[i].x;
			*p = rl_superpixel_value(*p, z, color, bright);
			if (sp[i].x > 0)
				p[-1] = rl_superpixel_value(p[-1], z >> 1, color, bright >> 1);
			if (sp[i].x < s->w - 1u)
				p[1] = rl_superpixel_value(p[1], z >> 1, color, bright >> 1);
			if (sp[i].y > 0)
				p[-s->pitch] = rl_superpixel_value(p[-s->pitch], z >> 1, color, bright >> 1);
			if (sp[i].y < s->h - 1u)
				p[s->pitch] = rl_superpixel_value(p[s->pitch], z >> 1, color, bright >> 1);
		}
		--sp[i].z;
	}
}

static void qa_reference_spark_rewind(superpixel_type *sp)
{
	for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
	{
		if (sp[i].z == 0 || sp[i].z >= SUPERPIXEL_SPAWN_Z)
			continue;
		sp[i].x -= sp[i].delta_x;
		sp[i].y -= sp[i].delta_y;
		++sp[i].z;
	}
}

void qa_test_spark_ring_bounds(void)
{
	if (VGAScreen == NULL || VGAScreen->format->BitsPerPixel != 8)
		return;

	const bool savedExtra = extraSparks, savedRecording = render_list_recording;
	const bool savedSilent = rollback_resim_silent, savedClip = superpixelClipActive;
	extraSparks = true;
	render_list_recording = false;
	rollback_resim_silent = false;
	superpixelClipActive = false;

	const size_t screenBytes = (size_t)VGAScreen->pitch * VGAScreen->h;
	Uint8 *const savedScreen = malloc(screenBytes);
	SDL_Surface *const refScreen =
	    SDL_CreateRGBSurface(0, VGAScreen->w, VGAScreen->h, 8, 0, 0, 0, 0);
	superpixel_type *const ref = malloc(sizeof(superpixels));
	bool same = savedScreen != NULL && refScreen != NULL && ref != NULL
	         && refScreen->pitch == VGAScreen->pitch;
	if (savedScreen != NULL)
		memcpy(savedScreen, VGAScreen->pixels, screenBytes);

	JE_resetSP();
	mt_srand(20261022u);
	Uint32 seed = 0x5b0d5u;
	for (int pass = 0; pass < 140 && same; ++pass)
	{
		seed = seed * 1664525u + 1013904223u;
		const int showers = 1 + (int)(seed >> 30);
		for (int k = 0; k < showers; ++k)
		{
			seed = seed * 1664525u + 1013904223u;
			const JE_word x = (JE_word)((seed >> 4) % (Uint32)(VGAScreen->w + 20));
			const JE_word y = (JE_word)((seed >> 14) % (Uint32)(VGAScreen->h + 20));
			const JE_word num = (JE_word)(200 + (seed >> 22) % 600);
			const JE_byte width = (JE_byte)(2 + seed % 12), color = (JE_byte)(seed & 0xF0);
			JE_doSP(x, y, num, width, color, (seed & 0x100) != 0);
		}

		memcpy(ref, superpixels, sizeof(superpixels));
		memcpy(refScreen->pixels, VGAScreen->pixels, screenBytes);
		JE_beginSPPass();
		JE_drawSP();
		qa_reference_spark_pass(ref, refScreen);
		same = memcmp(ref, superpixels, sizeof(superpixels)) == 0
		    && memcmp(refScreen->pixels, VGAScreen->pixels, screenBytes) == 0;

		if (same && pass % 5 == 2)
		{
			JE_discardSPPass();
			qa_reference_spark_rewind(ref);
			same = memcmp(ref, superpixels, sizeof(superpixels)) == 0;
		}
	}

	JE_resetSP();
	if (savedScreen != NULL)
		memcpy(VGAScreen->pixels, savedScreen, screenBytes);
	free(savedScreen);
	free(ref);
	SDL_FreeSurface(refScreen);
	extraSparks = savedExtra;
	render_list_recording = savedRecording;
	rollback_resim_silent = savedSilent;
	superpixelClipActive = savedClip;
	qa_check(same,
	         "spark passes over the live slots match a full-ring scan, through wraps and rewinds");
}

/* Generator draws one shower takes, counted from a fresh seed. */
static Uint32 qa_spark_rng_draws(bool silent, JE_word num)
{
	const bool saved = rollback_resim_silent;

	rollback_resim_silent = silent;
	mt_srand(20260813u);
	JE_doSP(100, 100, num, 3, 7 << 4, false);
	rollback_resim_silent = saved;
	JE_resetSP();
	return mt_rand_count;
}

/* Silent re-simulation must consume the same spark RNG draws as a presented frame. */
void qa_test_superspark_rng_cost(void)
{
	qa_check(qa_spark_rng_draws(true, 7) == qa_spark_rng_draws(false, 7),
	         "a silent re-simulation pass costs the RNG exactly what a drawn one costs");
	qa_check(qa_spark_rng_draws(false, 7) == 7 * 3,
	         "and that cost is an angle and two magnitudes a spark, which is what a stray guard moves");
}

/* First live spark's z and bright, spawned into a cleared ring by JE_doSPBrief. */
static bool qa_spark_brief_first(JE_byte life, JE_byte bright,
                                 unsigned int *out_z, Uint8 *out_bright)
{
	JE_resetSP();
	JE_doSPBrief(100, 100, 1, 3, 7 << 4, life, bright);
	for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
	{
		if (superpixels[i].z == 0)
			continue;
		*out_z = superpixels[i].z;
		*out_bright = superpixels[i].bright;
		return true;
	}
	return false;
}

/* Brief sparks keep JE_doSP's RNG cost while applying their shorter life and brighter shade. */
void qa_test_superspark_brief(void)
{
	const bool savedExtra = extraSparks;
	extraSparks = true;

	unsigned int spawnZ = 0;
	Uint8 spawnBright = 0;
	qa_check(qa_spark_brief_first(4, 5, &spawnZ, &spawnBright) && spawnZ == 4 && spawnBright == 5,
	         "a brief spark spawns at the requested life with the requested lift");
	qa_check(qa_spark_brief_first(0, 0, &spawnZ, &spawnBright) && spawnZ == 1,
	         "a zero life still spawns a spark for one tick");
	qa_check(qa_spark_brief_first(200, 0, &spawnZ, &spawnBright) && spawnZ == SUPERPIXEL_SPAWN_Z,
	         "a life past the classic spawn z clamps to it");

	int lo, hi;
	qa_spark_brief_first(4, 5, &spawnZ, &spawnBright);
	qa_spark_span(&lo, &hi);
	qa_check(lo >= SUPERPIXELS_CLASSIC,
	         "a brief shower is uncapped, so it spawns outside the classic window");

	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8)
	{
		qa_spark_brief_first(3, 5, &spawnZ, &spawnBright);
		JE_drawSP();
		JE_drawSP();
		const int liveBefore = qa_spark_span(&lo, &hi);
		JE_drawSP();
		const int liveAfter = qa_spark_span(&lo, &hi);
		qa_check(liveBefore == 1 && liveAfter == 0,
		         "a three-tick spark is gone after its third draw");
	}

	mt_srand(20260815u);
	JE_doSPBrief(100, 100, 6, 5, 7 << 4, 4, 5);
	const Uint32 briefDraws = mt_rand_count;

	const bool savedSilent = rollback_resim_silent;
	rollback_resim_silent = true;
	mt_srand(20260815u);
	JE_doSPBrief(100, 100, 6, 5, 7 << 4, 4, 5);
	const Uint32 silentDraws = mt_rand_count;
	rollback_resim_silent = savedSilent;

	qa_check(briefDraws == qa_spark_rng_draws(false, 6) && silentDraws == briefDraws,
	         "a brief shower costs the RNG what JE_doSP costs, drawn or silently re-simulated");

	JE_resetSP();
	extraSparks = savedExtra;
}

#define QA_SEEDED_SHOWERS 64u  /* showers sampled per stride */
#define QA_SEEDED_REACH   60   /* wide enough that rounding keeps an offset in its quadrant */

/* Offset stored by a one-spark seeded shower, spawned into a cleared ring. */
static void qa_spark_seeded_offset(Uint32 seed, int *dx, int *dy)
{
	JE_resetSP();
	JE_doSPSeeded(200, 200, 1, QA_SEEDED_REACH, 7 << 4, false, 0, false, seed);

	*dx = 0;
	*dy = 0;
	for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
	{
		if (superpixels[i].z == 0)
			continue;
		*dx = superpixels[i].delta_x;
		*dy = superpixels[i].delta_y - 1;  /* the stored velocity carries the fall as well */
		break;
	}
}

/* Successive showers use the caller strides documented in doc/notes.md#gauges-and-effects. */
void qa_test_superspark_seeded_spread(void)
{
	static const Uint32 strides[] = { 1u, 100u, 137u, 500u, 685u };

	/* Half the even share: a quarter of the showers belongs in each quadrant. */
	const unsigned int minPerQuadrant = QA_SEEDED_SHOWERS / 8;

	unsigned int worstCount = QA_SEEDED_SHOWERS;
	Uint32 worstStride = 0;

	for (unsigned int s = 0; s < COUNTOF(strides); ++s)
	{
		unsigned int quadrant[4] = { 0, 0, 0, 0 };
		for (unsigned int shower = 0; shower < QA_SEEDED_SHOWERS; ++shower)
		{
			int dx, dy;
			qa_spark_seeded_offset(4242u + shower * strides[s], &dx, &dy);
			++quadrant[(dx < 0 ? 1u : 0u) + (dy < 0 ? 2u : 0u)];
		}

		for (unsigned int q = 0; q < COUNTOF(quadrant); ++q)
		{
			if (quadrant[q] >= worstCount)
				continue;
			worstCount = quadrant[q];
			worstStride = strides[s];
		}
	}

	if (worstCount < minPerQuadrant)
		fprintf(stderr, "# stride %u sends only %u of %u showers into its thinnest quadrant\n",
		        worstStride, worstCount, QA_SEEDED_SHOWERS);
	qa_check(worstCount >= minPerQuadrant,
	         "seeded showers a fixed stride apart still reach every quadrant");

	JE_resetSP();
}

/* Chain Reaction rings and bolts must preserve their geometry at every supported size. */
void qa_test_superspark_shapes(void)
{
	const bool savedExtra = extraSparks;
	extraSparks = true;

	char label[160];

	/* A spark travels on an integer per-axis delta, so each axis can be half a pixel out on every
	 * one of the steps its life buys, and the two together land the ring this far off its radius. */
	const int ringLife = 5;
	const int ringSlack = (int)(0.5f * (ringLife + 1) * 1.41422f) + 1;

	for (int radius = 20; radius <= 120; radius += 20)
	{
		JE_resetSP();
		JE_doSPRingSeeded(200, 100, (JE_word)radius, 12, 15 << 4, (JE_byte)ringLife, 0, 9001u);

		int lo, hi;
		const int live = qa_spark_span(&lo, &hi);
		snprintf(label, sizeof(label), "a %d px ring spaces its sparks out rather than stretching a fixed few",
		         radius);
		qa_check(live >= 5 && live <= 24, label);

		/* Step them the way the level loop does, then measure where the ring finished. */
		for (int t = 0; t < ringLife; ++t)
			for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
			{
				if (superpixels[i].z == 0)
					continue;
				superpixels[i].x += superpixels[i].delta_x;
				superpixels[i].y += superpixels[i].delta_y;
				--superpixels[i].z;
			}

		int worst = 0;
		for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
		{
			if (superpixels[i].delta_x == 0 && superpixels[i].delta_y == 0)
				continue;
			const int dx = (int)superpixels[i].x - 200, dy = (int)superpixels[i].y - 100;
			const int reach = (int)(sqrtf((float)(dx * dx + dy * dy)) + 0.5f);
			if (abs(reach - radius) > worst)
				worst = abs(reach - radius);
		}
		snprintf(label, sizeof(label), "...and finishes on its %d px radius, %d px out at worst",
		         radius, worst);
		qa_check(worst <= ringSlack, label);
	}

	/* A bolt's sparks sit on the segment, bowed off it by no more than the wander it was given. */
	static const struct { int x1, y1; } ends[] = { { 260, 100 }, { 140, 160 }, { 200, 40 }, { 201, 101 } };
	for (unsigned int e = 0; e < COUNTOF(ends); ++e)
	{
		JE_resetSP();
		JE_doSPBoltSeeded(200, 100, (JE_word)ends[e].x1, (JE_word)ends[e].y1, 4, 3, 15 << 4, 5, 12, 4242u);

		const float dx = (float)(ends[e].x1 - 200), dy = (float)(ends[e].y1 - 100);
		const float len = sqrtf(dx * dx + dy * dy);

		int live = 0;
		float worst = 0.0f;
		for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
		{
			if (superpixels[i].z == 0)
				continue;
			++live;
			/* Distance from the infinite line through the two ends. */
			const float px = (float)superpixels[i].x - 200.0f, py = (float)superpixels[i].y - 100.0f;
			const float off = fabsf(px * dy - py * dx) / len;
			if (off > worst)
				worst = off;
		}

		snprintf(label, sizeof(label), "a %d px bolt draws %d sparks, one every 4 px up to the cap",
		         (int)(len + 0.5f), live);
		qa_check(live >= 1 && live <= 24 && live >= (int)(len / 4.0f) - 1, label);
		snprintf(label, sizeof(label), "...and none of them stray past its bow, %d px off at worst",
		         (int)(worst + 0.5f));
		qa_check(worst <= 4.0f, label);
	}

	JE_resetSP();
	extraSparks = savedExtra;
}

/* Lowest sprite in a sheet painted in a bank other than 0, so a colour taken from it cannot be
 * confused with the 0 an unpaintable index reads as. Returns 0 if the sheet holds none. */
static JE_word qa_painted_sprite(Sprite2_array sheet, Uint8 *out_bank)
{
	for (JE_word index = 1; index < 500; ++index)  /* 500 is the shot graphics' own sheet split */
	{
		*out_bank = sprite2_dominant_bank(sheet, index);
		if (*out_bank != 0)
			return index;
	}
	return 0;
}

/* Every live spark carries `want`, and at least one is live. */
static bool qa_sparks_all_coloured(Uint8 want)
{
	bool any = false;
	for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
	{
		if (superpixels[i].z == 0)
			continue;
		if (superpixels[i].color != want)
			return false;
		any = true;
	}
	return any;
}

/* Count, placement and colour of the pop a vaporised bullet leaves (Endless Shockwave and
 * Countermeasures), and the silence a rollback re-simulation owes it. */
void qa_test_vaporised_shot_sparks(void)
{
	const EnemyShotType savedShot = enemyShot[0];
	const bool savedSilent = rollback_resim_silent;
	int lo, hi;

	enemyShot[0].sx = 100;
	enemyShot[0].sy = 80;
	enemyShot[0].animate = 0;
	enemyShot[0].sgr = 270;
	enemyShot[0].filter = ENDLESS_ELITE_FILTER;

	JE_resetSP();
	enemy_shot_vaporise_sparks(0);
	const int live = qa_spark_span(&lo, &hi);
	qa_check(live >= 3 && live <= 5, "a vaporised bullet pops into 3 to 5 sparks");
	qa_check(qa_sparks_all_coloured(ENDLESS_ELITE_FILTER),
	         "an elite bullet's sparks wear its tier tint");

	/* Thrown from the centre of the bullet's cell, no further than the reach it spawns with. */
	bool centred = true;
	for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
	{
		if (superpixels[i].z == 0)
			continue;
		centred = centred && abs((int)superpixels[i].x - (enemyShot[0].sx + 6)) <= 3
		                  && abs((int)superpixels[i].y - (enemyShot[0].sy + 6)) <= 3;
	}
	qa_check(centred, "the pop is thrown from the bullet's centre");

	/* Without a tier tint the colour comes from the sprite, which sits on one of two sheets: a
	 * graphic from 500 up is drawn from the second one, indexed from its start. */
	Uint8 bank8 = 0, bank12 = 0;
	const JE_word sprite8 = qa_painted_sprite(spriteSheet8, &bank8);
	const JE_word sprite12 = qa_painted_sprite(spriteSheet12, &bank12);
	qa_check(sprite8 != 0 && sprite12 != 0,
	         "both shot sheets hold a painted sprite to colour a pop from");

	enemyShot[0].filter = 0;
	enemyShot[0].sgr = sprite8;
	JE_resetSP();
	enemy_shot_vaporise_sparks(0);
	qa_check(qa_sparks_all_coloured((Uint8)(bank8 << 4)),
	         "an untinted bullet's sparks take the bank its sprite is drawn in");

	enemyShot[0].sgr = (JE_word)(500 + sprite12);
	JE_resetSP();
	enemy_shot_vaporise_sparks(0);
	qa_check(qa_sparks_all_coloured((Uint8)(bank12 << 4)),
	         "a graphic from 500 up is read from the second sheet's start");

	/* Online the sweeps re-run inside rollback re-simulation; only the visible pass spawns. */
	rollback_resim_silent = true;
	JE_resetSP();
	enemy_shot_vaporise_sparks(0);
	qa_check(qa_spark_span(&lo, &hi) == 0, "a silent resim pass spawns no pop");
	rollback_resim_silent = savedSilent;

	enemyShot[0] = savedShot;
	JE_resetSP();
}

/* Knife Fight blood uses the hit hull, scales by bonus, and stays silent during re-simulation. */
void qa_test_knife_fight_blood(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const bool savedSilent = rollback_resim_silent;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	struct JE_SingleEnemyType savedEnemy = enemy[0];
	const JE_byte savedAvail = enemyAvail[0];
	const Player savedPlayer = player[0];
	int lo, hi;

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkTakenBy[0][PERK_KNIFE] = (JE_byte)endlessPerkMaxStack(PERK_KNIFE);
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	// The ship right above the enemy, which is the deepest the bonus goes.
	memset(&enemy[0], 0, sizeof(enemy[0]));
	enemyAvail[0] = 0;
	enemy[0].ex = 100;
	enemy[0].ey = 100;
	enemy[0].armorleft = 50;
	enemy[0].enemytype = 1;
	player[0].x = 100;
	player[0].y = 130;
	const int deepPct = endlessPerkKnifeFightPercent(0);
	qa_check(deepPct > 0, "the blood test flies close enough to raise a hit");

	JE_resetSP();
	endlessPerkKnifeFightBlood(0, 0);
	qa_check(qa_spark_span(&lo, &hi) == 0, "a hit Knife Fight did not raise draws no blood");
	rollback_resim_silent = true;
	endlessPerkKnifeFightBlood(0, deepPct);
	qa_check(qa_spark_span(&lo, &hi) == 0, "...and neither does a silent resim pass");
	rollback_resim_silent = savedSilent;

	JE_resetSP();
	endlessPerkKnifeFightBlood(0, deepPct);
	const int deepDrops = qa_spark_span(&lo, &hi);
	qa_check(deepDrops > 0, "a raised hit bleeds");

	bool falls = true, oneBank = true, atHull = true;
	Uint8 bank = 0;
	for (unsigned int i = 0; i < MAX_SUPERPIXELS; ++i)
	{
		if (superpixels[i].z == 0)
			continue;
		falls = falls && superpixels[i].delta_y > 0 && superpixels[i].delta_x == 0;
		if (bank == 0)
			bank = superpixels[i].color;
		oneBank = oneBank && superpixels[i].color == bank;
		atHull = atHull && abs((int)superpixels[i].x - (enemy[0].ex + enemy[0].mapoffset + 6)) <= 8
		                && abs((int)superpixels[i].y - (enemy[0].ey + 7)) <= 8;
	}
	qa_check(falls, "...in drops that run straight down");
	qa_check(atHull, "...from the hull that was hit");
	qa_check(oneBank && (bank & 0x0f) == 0, "...all in one palette bank");

	// Which bank that is follows the palette in use: nothing else in it reads redder.
	int chosenRed = INT_MIN, reddest = INT_MIN;
	for (unsigned b = 0; b < 16; ++b)
	{
		int score = 0;
		for (unsigned s = 9; s <= 14; ++s)
		{
			const SDL_Color *const c = &colors[b * 16 + s];
			score += (int)c->r - (int)((c->g > c->b) ? c->g : c->b);
		}
		if (score > reddest)
			reddest = score;
		if ((Uint8)(b << 4) == bank)
			chosenRed = score;
	}
	qa_check(chosenRed == reddest, "...and no bank of this palette reads redder");

	JE_resetSP();
	endlessPerkKnifeFightBlood(0, deepPct / 4);
	qa_check(qa_spark_span(&lo, &hi) < deepDrops, "a fading bonus bleeds less");

	// One presented frame's budget is shared by every hit in it, so a burst of them runs dry.
	JE_resetSP();
	for (int shower = 0; shower < 40; ++shower)
		endlessPerkKnifeFightBlood(0, deepPct);
	const int bled = qa_spark_span(&lo, &hi);
	qa_check(bled > 0 && bled < 40 * deepDrops, "a frame full of raised hits stops at its budget");

	JE_resetSP();
	player[0] = savedPlayer;
	enemy[0] = savedEnemy;
	enemyAvail[0] = savedAvail;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}
