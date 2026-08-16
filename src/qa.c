/* Project-owned unit, property, serialization, and replay tests. */
#include "qa.h"

#include "config.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "destruct.h"
#include "destruct_rollback.h"
#include "endless.h"
#include "episodes.h"
#include "endless_internal.h"
#include "fonthand.h"
#include "game_menu.h"   // JE_getLevelSections
#include "mainint.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "network.h"
#include "nortvars.h"
#include "params.h"
#include "player.h"
#include "render_list.h"
#include "rollback.h"
#include "shots.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"
#include "video.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool qa_test_suite = false;
const char *qa_fixture_dir = "testing/fixtures/endless";
int qa_replay_demo = 0;
unsigned long qa_replay_ticks = 0;
unsigned long qa_destruct_selftest_ticks = 0;
bool qa_replay_expect_set = false;
Uint32 qa_replay_expect = 0;
int qa_replay_chain = 0;
int qa_net_rounds = 0;
int qa_net_scenario = 0;
int qa_net_version_skew = 0;
unsigned long qa_net_gameplay_ticks = 0;
unsigned long qa_net_delay_frames = 0;
unsigned long qa_net_special_flashes = 0;
unsigned long qa_net_corrupt_frame = 0;
bool qa_net_save_exit = false;
int qa_net_resume_slot = 0;
int qa_net_loadout = 0;
unsigned long qa_net_menu_frame = 0;
int qa_net_game_type = -1;
int qa_net_zones = 0;
int qa_net_zones_cleared = 0;
bool qa_net_lobby_settings = false;
bool qa_net_arcade_separate = false;
bool qa_net_scrollock = false;

/* Deterministic modifier slates for the first ten Endless wire-test zones.
 * Later zones fly unmodified. */
Uint64 qa_net_zone_mods(int depth)
{
	static const Uint64 slate[] = {
		ENDLESS_MOD_TOPSY | ENDLESS_MOD_SLUGGISH | ENDLESS_MOD_GRAVITY,
		ENDLESS_MOD_APEX | ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_BOUNTY,
		ENDLESS_MOD_LEGION | ENDLESS_MOD_SEEKER | ENDLESS_MOD_RETALIATION,
		ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT
			| ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING | ENDLESS_MOD_OVERCHARGE,
		ENDLESS_MOD_NOELITE | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_GRAVITY_OMNI
			| ENDLESS_MOD_STATIC | ENDLESS_MOD_SHIELDLESS,
		ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_CLEANSIGNALS | ENDLESS_MOD_DILATION
			| ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_ENRAGE,
		ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_SLIPSTREAM
			| ENDLESS_MOD_AEGIS | ENDLESS_MOD_LOWPROFILE,
		ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_WARP
			| ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_DEADGEN,
		ENDLESS_MOD_MISFIRE | ENDLESS_MOD_HOMING | (Uint64)ENDLESS_MOD_RAMPAGE
			| ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_FAVOR,
		ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_CURSED | ENDLESS_MOD_MARKED | ENDLESS_MOD_NITRO
			| ENDLESS_MOD_OVERHEAT | ENDLESS_MOD_DUD
			| ENDLESS_MOD_STARCHARTS | ENDLESS_MOD_BREAKTHROUGH,
	};

	if (depth < 0 || depth >= (int)COUNTOF(slate))
		return 0;
	return slate[depth];
}

// First sidekick whose mount style matches, for the wire tests' loadout profiles. Styles:
// 0 side pod, 1/3 trailing companion, 2 front pod, 4 orbiting satellite.
static JE_byte qa_first_sidekick_with_tr(JE_byte tr)
{
	for (uint i = 1; i <= OPTION_NUM; ++i)
		if (options[i].tr == tr && options[i].name[0] != '\0')
			return (JE_byte)i;
	return 0;
}

static JE_byte qa_first_sidekick_with_ammo(void)
{
	for (uint i = 1; i <= OPTION_NUM; ++i)
		if (options[i].ammo > 0 && options[i].name[0] != '\0')
			return (JE_byte)i;
	return 0;
}

static JE_byte qa_first_sidekick_with_charge(void)
{
	for (uint i = 1; i <= OPTION_NUM; ++i)
		if (options[i].pwr > 0 && options[i].name[0] != '\0')
			return (JE_byte)i;
	return 0;
}

/* Give the linked-arcade wire test an instant special. Holding fire releases it once, then the
 * ready light must retain that fired edge across player two's shared-special pass. */
void qa_net_apply_linked_special(void)
{
	for (uint i = 1; i <= SPECIAL_NUM; ++i)
	{
		if (special[i].stype == 2)  // Repulsor
		{
			player[0].items.special = (JE_byte)i;
			return;
		}
	}
}

/* Sidekick mount combinations for the gameplay wire tests, applied identically on both
 * machines. The profiles cross the styles the mounts differ most in: front pods fire from the
 * nose, side pods ride fixed offsets, trailing companions integrate motion history, and
 * satellites orbit on a shared angle. */
void qa_net_apply_loadout(int profile)
{
	const JE_byte side  = qa_first_sidekick_with_tr(0);
	const JE_byte trail = qa_first_sidekick_with_tr(1);
	const JE_byte front = qa_first_sidekick_with_tr(2);
	const JE_byte chase = qa_first_sidekick_with_tr(3);
	const JE_byte sat   = qa_first_sidekick_with_tr(4);

	switch (profile)
	{
	case 1:
		player[0].items.sidekick[0] = front;
		player[0].items.sidekick[1] = side;
		player[1].items.sidekick[0] = trail;
		player[1].items.sidekick[1] = trail;
		break;
	case 2:
		player[0].items.sidekick[0] = front;
		player[0].items.sidekick[1] = front;
		player[1].items.sidekick[0] = sat;
		player[1].items.sidekick[1] = chase ? chase : trail;
		break;
	case 3:
		player[0].items.sidekick[0] = sat;
		player[0].items.sidekick[1] = sat;
		player[1].items.sidekick[0] = chase ? chase : trail;
		player[1].items.sidekick[1] = front;
		break;
	case 4:
	{
		/* Mix ammo, charge, custom, and satellite sidekicks to exercise rollback of
		 * player[].sidekick counters and compiled owner slots. */
		const JE_byte ammoKick = qa_first_sidekick_with_ammo();
		const JE_byte chargeKick = qa_first_sidekick_with_charge();
		JE_byte customKick = 0;

		customWeaponEnabled = true;
		customWeaponNetPrepare();
		Uint8 *const stream = malloc(CUSTOM_WEAPON_WIRE_MAX);
		if (stream != NULL)
		{
			const size_t total = customWeaponSerializeDesign(stream, CUSTOM_WEAPON_WIRE_MAX);
			if (total > 0 && customWeaponAdoptDesign(1, stream, total)
			    && customSidekickOwnerSlot[1] > 0)
				customKick = (JE_byte)customSidekickOwnerSlot[1];
			free(stream);
		}

		player[0].items.sidekick[0] = ammoKick ? ammoKick : side;
		player[0].items.sidekick[1] = chargeKick ? chargeKick : trail;
		player[1].items.sidekick[0] = customKick ? customKick : sat;
		player[1].items.sidekick[1] = sat;
		break;
	}
	default:
		return;
	}

	fprintf(stderr, "net gameplay: loadout %d (sidekicks %u+%u vs %u+%u)\n", profile,
	        player[0].items.sidekick[0], player[0].items.sidekick[1],
	        player[1].items.sidekick[0], player[1].items.sidekick[1]);
	fflush(stderr);
}
bool qa_fast_forward = false;

static unsigned qa_checks;
static unsigned qa_failures;

void qa_check(bool okay, const char *what)
{
	++qa_checks;
	if (!okay)
	{
		++qa_failures;
		fprintf(stderr, "not ok %u - %s\n", qa_checks, what);
	}
}

static unsigned qa_popcount64(Uint64 v)
{
	unsigned n = 0;
	while (v != 0)
	{
		n += (unsigned)(v & 1);
		v >>= 1;
	}
	return n;
}

static bool qa_display_name_valid(const char *name)
{
	if (name == NULL || name[0] == '\0' || strlen(name) >= 24)
		return false;
	for (const unsigned char *p = (const unsigned char *)name; *p != '\0'; ++p)
		if (*p != ' ' && font_ascii[*p] < 0)
			return false;
	return true;
}

static int qa_course_sort_key(int i)
{
	const int rank = endlessCourseRankLevel(i);
	if (rank > 0)
		return rank + 2;
	if (endlessCourseMod[i] & ENDLESS_MOD_CURSED)
		return 2;
	return endlessCourseMod[i] == 0 ? 0 : 1;
}

static Uint32 qa_slate_hash(void)
{
	Uint32 h = 2166136261u;
	#define QA_HASH_BYTE(v) do { h ^= (Uint8)(v); h *= 16777619u; } while (0)
	#define QA_HASH_WORD(v) do { \
		const Uint64 w_ = (Uint64)(v); \
		for (unsigned b_ = 0; b_ < 8; ++b_) QA_HASH_BYTE(w_ >> (b_ * 8)); \
	} while (0)

	QA_HASH_WORD(endlessCourseCnt);
	for (int i = 0; i < endlessCourseCnt; ++i)
	{
		QA_HASH_WORD(endlessCourseEp[i]);
		QA_HASH_WORD(endlessCourseSec[i]);
		QA_HASH_WORD(endlessCourseFile[i]);
		QA_HASH_WORD(endlessCourseMod[i]);
	}

	#undef QA_HASH_WORD
	#undef QA_HASH_BYTE
	return h;
}

static void qa_reset_course_inputs(const char *seed, int depth, int difficulty)
{
	endlessMode = true;
	endlessCampaignMods = false;
	endlessRunMode = ENDLESS_RUNMODE_STANDARD;
	// The shipped chart rule and a full bag; the cases for the other three set their own.
	endlessRunBaseRule = ENDLESS_BASE_VARIED;
	endlessShuffleNext = endlessShuffleHandStart = 0;
	endlessShuffleHandDepth = -1;
	endlessRunDepth = depth;
	difficultyLevel = (JE_shortint)difficulty;
	endlessSetSeed(seed);
	endlessRecentCount = 0;
	memset(endlessRecentEp, 0, sizeof(endlessRecentEp));
	memset(endlessRecentSec, 0, sizeof(endlessRecentSec));
	endlessLastEp = 0;
	endlessLastSec = 0;
	endlessStarChartsOwed = false;
	endlessBreakthroughOwed = 0;
	endlessChartRerolls = 0;      // an unrerolled visit, so the phase salts are the seed's own
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		endlessPurchasedMods[p] = 0;
		endlessCleanseChargeCount[p] = 0;
	}
	for (int p = 0; p < endlessPerkCount(); ++p)
		endlessPerkSetOwned(p, 0);
	endlessReseed((Uint64)depth * 2);
}

static bool qa_mods_compatible(Uint64 m)
{
	/* These modifier pairs semantically cancel each other. */
	if ((m & ENDLESS_MOD_FRAGILE) && (m & ENDLESS_MOD_FORTIFIED)) return false;
	if ((m & ENDLESS_MOD_DILATION) && (m & (ENDLESS_MOD_SWIFT | ENDLESS_MOD_OVERCLOCK))) return false;
	if ((m & ENDLESS_MOD_DEADGEN) && (m & ENDLESS_MOD_STATIC)) return false;
	if ((m & ENDLESS_MOD_GRAVITY_OMNI) && !(m & ENDLESS_MOD_GRAVITY)) return false;
	if (qa_popcount64(m & ENDLESS_MOD_KILLFIRE_ANY) > 1) return false;
	/* Covers every special-enemy pair where the weaker bit changes nothing. */
	if (endlessCanonicalMods(m) != m) return false;
	return true;
}

static void qa_test_course_tables(void)
{
	char detail[256];
	const bool okay = endlessValidateModifierTables(detail, sizeof(detail));
	if (!okay)
		fprintf(stderr, "# modifier table validation: %s\n", detail);
	qa_check(okay, "modifier and course-name registries satisfy their documented invariants");
}

// The special-enemy ladder, checked against the shares endlessEliteChancePercent actually reads.
static void qa_test_canonical_mods(void)
{
	static const struct { Uint64 in, want; } cases[] = {
		{ ENDLESS_MOD_APEX | ENDLESS_MOD_ELITEPACK,   ENDLESS_MOD_APEX },
		{ ENDLESS_MOD_LEGION | ENDLESS_MOD_ELITEPACK, ENDLESS_MOD_LEGION },
		{ ENDLESS_MOD_LEGION | ENDLESS_MOD_APEX,      ENDLESS_MOD_LEGION },
		{ ENDLESS_MOD_LEGION | ENDLESS_MOD_APEX | ENDLESS_MOD_ELITEPACK, ENDLESS_MOD_LEGION },
		{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_LEGION,   ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_APEX },
		{ ENDLESS_MOD_NOELITE | ENDLESS_MOD_NOCHAMP,  ENDLESS_MOD_NOELITE },
		{ ENDLESS_MOD_NOELITE | ENDLESS_MOD_LEGION,   ENDLESS_MOD_NOELITE },
		// Bits on other systems, and the one meaningful pair, must survive untouched.
		{ ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_ELITEPACK, ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_ELITEPACK },
		{ ENDLESS_MOD_APEX | ENDLESS_MOD_FORTIFIED,    ENDLESS_MOD_APEX | ENDLESS_MOD_FORTIFIED },
		{ 0, 0 },
	};
	for (unsigned i = 0; i < COUNTOF(cases); ++i)
	{
		const Uint64 got = endlessCanonicalMods(cases[i].in);
		qa_check(got == cases[i].want, "redundant special-enemy bits are dropped");
		qa_check(endlessCanonicalMods(got) == got, "settling a modifier set twice changes nothing");
	}
}

static void qa_test_structural_rng(void)
{
	endlessSetSeed("qa-rng-isolation");
	endlessReseed(0x1234);
	mt_srand(0x4567);
	for (int i = 0; i < 256; ++i)
		(void)endlessRand();
	qa_check(mt_rand_count == 0,
	         "structural random draws do not consume gameplay RNG");

	endlessReseed(0x89ab);
	const Uint32 expected = endlessRand();
	endlessReseed(0x89ab);
	endlessResetZoneEffects();
	qa_check(endlessRand() == expected,
	         "zone-effect reset does not consume structural RNG");

	qa_reset_course_inputs("qa-gameplay-rng", 73, DIFFICULTY_HARD);
	mt_srand(1);
	endlessGenerateCourses();
	const Uint32 first = qa_slate_hash();
	qa_reset_course_inputs("qa-gameplay-rng", 73, DIFFICULTY_HARD);
	mt_srand(1);
	for (int i = 0; i < 4096; ++i)
		(void)mt_rand();
	endlessGenerateCourses();
	qa_check(qa_slate_hash() == first,
	         "course generation is independent of gameplay RNG position");
}

static void qa_test_courses(void)
{
	static const int depths[] = { 0, 1, 24, 25, 49, 50, 99, 100, 249, 250, 500 };
	char seed[ENDLESS_SEED_MAXLEN];
	unsigned routes = 0;

	for (unsigned sample = 0; sample < 768; ++sample)
	{
		watchdog_heartbeat();
		const int depth = depths[sample % COUNTOF(depths)];
		const int diff = DIFFICULTY_EASY + (int)(sample % ENDLESS_DIFFICULTY_COUNT);
		snprintf(seed, sizeof(seed), "qa-%08x", (unsigned)(sample * 2654435761u));

		qa_reset_course_inputs(seed, depth, diff);
		endlessGenerateCourses();
		const Uint32 first_hash = qa_slate_hash();
		const int first_count = endlessCourseCnt;

		qa_check(first_count >= 1 && first_count <= ENDLESS_MAX_COURSES,
		         "generated course count is bounded");
		for (int i = 0; i < first_count; ++i)
		{
			JE_byte resolved = 0;
			const long payout = endlessCoursePayout(i);
			const long floor = endlessClearBase() / 4;
			const Uint64 mods = endlessCourseMod[i];

			qa_check(endlessResolveCourseFile(endlessCourseEp[i], endlessCourseSec[i],
			                                   endlessCourseFile[i], &resolved)
			         && resolved == endlessCourseFile[i],
			         "generated route resolves to a launchable level");
			const bool compatible = qa_mods_compatible(mods);
			if (!compatible)
				fprintf(stderr, "# incompatible seed=%s depth=%d route=%d mods=%016llx\n",
				        seed, depth, i, (unsigned long long)mods);
			qa_check(compatible, "generated modifiers are compatible");
			qa_check(payout >= floor && payout <= 10000000L,
			         "generated payout remains positive and bounded");
			qa_check(qa_display_name_valid(endlessCourseName(i)),
			         "generated course name fits its menu field and uses visible glyphs");
			for (int k = 0; k < i; ++k)
				qa_check(strcmp(endlessCourseName(i), endlessCourseName(k)) != 0,
				         "generated course names are unique within a slate");
			if (i > 0)
			{
				const int prevKey = qa_course_sort_key(i - 1);
				const int key = qa_course_sort_key(i);
				qa_check(prevKey <= key,
				         "courses are sorted by displayed danger band");
				if (prevKey == key)
					qa_check(endlessCoursePayout(i - 1) <= payout,
					         "courses with equal danger are sorted by payout");
			}
			++routes;
		}

		const int milestone = endlessMilestoneKindOfZone(depth + 1);
		if (milestone != 0)
		{
			int ranks[11] = { 0 };
			for (int i = 0; i < first_count; ++i)
			{
				const int rank = endlessCourseRankLevel(i);
				if (rank >= 0 && rank < (int)COUNTOF(ranks))
					++ranks[rank];
			}
			qa_check(first_count == ENDLESS_MAX_COURSES,
			         "milestone always offers a full course slate");
			if (milestone == 3)
				qa_check((ranks[6] == 2 || ranks[6] == 3) && ranks[6] + ranks[7] == first_count,
				         "minor milestone contains only the documented S and S+ split");
			else if (milestone == 1)
				qa_check((ranks[7] == 2 || ranks[7] == 3) && ranks[7] + ranks[8] == first_count,
				         "plain milestone contains only the documented S+ and S++ split");
			else
				qa_check(ranks[8] == 2 && ranks[9] == 2 && ranks[10] == 1,
				         "grand milestone contains two S++, two S+++, and one END route");
		}

		qa_reset_course_inputs(seed, depth, diff);
		endlessGenerateCourses();
		qa_check(endlessCourseCnt == first_count && qa_slate_hash() == first_hash,
		         "seeded course generation is deterministic");
	}

	printf("# course properties: 768 seeds, %u launchable routes\n", routes);
}

/* The Base Level rule. Same puts every route of a slate onto one level, leaving the modifiers as
 * the whole choice; Varied deals each route its own. Both must stay seed-deterministic. */
static void qa_test_course_base_rule(void)
{
	static const int depths[] = { 0, 7, 25, 50, 100 };
	char seed[ENDLESS_SEED_MAXLEN];
	unsigned multiRouteSlates = 0;

	for (unsigned sample = 0; sample < 160; ++sample)
	{
		watchdog_heartbeat();
		const int depth = depths[sample % COUNTOF(depths)];
		snprintf(seed, sizeof(seed), "qa-base-%08x", (unsigned)(sample * 2654435761u));

		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessRunBaseRule = ENDLESS_BASE_SAME;
		endlessGenerateCourses();
		const int sameCount = endlessCourseCnt;
		const Uint32 sameHash = qa_slate_hash();

		bool oneLevel = sameCount >= 1;
		for (int i = 1; i < sameCount; ++i)
			oneLevel &= endlessCourseEp[i] == endlessCourseEp[0]
			         && endlessCourseSec[i] == endlessCourseSec[0]
			         && endlessCourseFile[i] == endlessCourseFile[0];
		qa_check(oneLevel, "Same base level puts every charted route on one level");

		for (int i = 0; i < sameCount; ++i)
		{
			JE_byte resolved = 0;
			qa_check(endlessResolveCourseFile(endlessCourseEp[i], endlessCourseSec[i],
			                                  endlessCourseFile[i], &resolved)
			         && resolved == endlessCourseFile[i],
			         "Same-base route resolves to a launchable level");
			for (int k = 0; k < i; ++k)
				qa_check(strcmp(endlessCourseName(i), endlessCourseName(k)) != 0,
				         "Same-base course names stay unique within a slate");
		}

		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessRunBaseRule = ENDLESS_BASE_SAME;
		endlessGenerateCourses();
		qa_check(endlessCourseCnt == sameCount && qa_slate_hash() == sameHash,
		         "Same-base course generation is deterministic");

		// The rule has to be what decides the slate, so the same seed under Varied must still
		// hand every route its own level.
		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessGenerateCourses();
		if (endlessCourseCnt > 1)
		{
			bool distinct = true;
			for (int i = 1; i < endlessCourseCnt; ++i)
				for (int k = 0; k < i; ++k)
					distinct &= endlessCourseEp[i] != endlessCourseEp[k]
					         || endlessCourseSec[i] != endlessCourseSec[k];
			qa_check(distinct, "Varied base level gives each charted route its own level");
			++multiRouteSlates;
		}
	}

	qa_check(multiRouteSlates > 0, "the Varied comparison saw slates with more than one route");

	printf("# base level rule: 160 seeds, %u multi-route Varied slates\n", multiRouteSlates);
}

/* ---- the Shuffle rules' bag ---------------------------------------------------------------- */

#define QA_POOL_MAX (EPISODE_MAX * 64)

/* The eligible pool, derived here rather than read from endless_level.c, so the bag is checked
 * against an independent expectation of what belongs in it. A section the level scripts load twice
 * counts once, the way a chart counts it. */
typedef struct { int ep; JE_byte sec; } QaPoolEntry;

static int qa_level_pool(QaPoolEntry *pool)
{
	int npool = 0;
	for (int e = 1; e <= EPISODE_MAX && npool < QA_POOL_MAX; ++e)
	{
		if (!episodeAvail[e - 1])
			continue;
		JE_byte secs[64], files[64];
		const uint n = JE_getLevelSections(e, secs, files, COUNTOF(secs));
		for (uint i = 0; i < n && npool < QA_POOL_MAX; ++i)
		{
			bool seen = false;
			for (int k = 0; k < npool && !seen; ++k)
				seen = pool[k].ep == e && pool[k].sec == secs[i];
			if (seen)
				continue;
			pool[npool].ep = e;
			pool[npool].sec = secs[i];
			++npool;
		}
	}
	return npool;
}

static int qa_pool_index(const QaPoolEntry *pool, int npool, int ep, JE_byte sec)
{
	for (int i = 0; i < npool; ++i)
		if (pool[i].ep == ep && pool[i].sec == sec)
			return i;
	return -1;
}

// One bagful as pool indices, read straight off the draw.
static bool qa_shuffle_bagful(const QaPoolEntry *pool, int npool, int refill, int *out)
{
	for (int i = 0; i < npool; ++i)
	{
		int ep;
		JE_byte sec, file;
		if (!endlessShuffleSafeLevel(refill * npool + i, &ep, &sec, &file))
			return false;
		out[i] = qa_pool_index(pool, npool, ep, sec);
		if (out[i] < 0)
			return false;
	}
	return true;
}

// Every bagful has to be a permutation of the pool, so a run meets each level once per refill.
static void qa_test_shuffle_permutation(const QaPoolEntry *pool, int npool)
{
	static const char *const seeds[] = { "qa-shuffle-a", "qa-shuffle-b", "qa-shuffle-c" };
	int first[COUNTOF(seeds)][QA_POOL_MAX];
	bool allPermutations = true, refillsDiffer = false, seedsDiffer = false;

	for (unsigned s = 0; s < COUNTOF(seeds); ++s)
	{
		qa_reset_course_inputs(seeds[s], 0, DIFFICULTY_NORMAL);
		for (int refill = 0; refill < 3; ++refill)
		{
			int bag[QA_POOL_MAX];
			int seen[QA_POOL_MAX] = { 0 };
			if (!qa_shuffle_bagful(pool, npool, refill, bag))
			{
				allPermutations = false;
				break;
			}
			for (int i = 0; i < npool; ++i)
				++seen[bag[i]];
			for (int i = 0; i < npool; ++i)
				allPermutations &= seen[i] == 1;

			if (refill == 0)
				memcpy(first[s], bag, sizeof(int) * (size_t)npool);
			else
				refillsDiffer |= memcmp(first[s], bag, sizeof(int) * (size_t)npool) != 0;
		}
	}

	for (unsigned s = 1; s < COUNTOF(seeds); ++s)
		seedsDiffer |= memcmp(first[0], first[s], sizeof(int) * (size_t)npool) != 0;

	qa_check(allPermutations, "every bagful holds each eligible level exactly once");
	qa_check(refillsDiffer, "a refill reshuffles rather than repeating the emptied bag");
	qa_check(seedsDiffer, "different seeds shuffle the bag differently");

	// Re-seeding the same run has to reproduce its bag order exactly.
	qa_reset_course_inputs(seeds[0], 0, DIFFICULTY_NORMAL);
	int again[QA_POOL_MAX];
	qa_check(qa_shuffle_bagful(pool, npool, 0, again)
	         && memcmp(first[0], again, sizeof(int) * (size_t)npool) == 0,
	         "one seed always deals the same bag order");

	// The two windows the refill correction keeps disjoint, measured off the bags themselves.
	bool seamClean = true;
	for (unsigned s = 0; s < COUNTOF(seeds) && npool >= 3 * ENDLESS_MAX_COURSES; ++s)
	{
		qa_reset_course_inputs(seeds[s], 0, DIFFICULTY_NORMAL);
		int bag0[QA_POOL_MAX], bag1[QA_POOL_MAX];
		if (!qa_shuffle_bagful(pool, npool, 0, bag0) || !qa_shuffle_bagful(pool, npool, 1, bag1))
		{
			seamClean = false;
			break;
		}
		for (int i = 0; i < 2 * ENDLESS_MAX_COURSES - 1; ++i)
			for (int k = npool - ENDLESS_MAX_COURSES; k < npool; ++k)
				seamClean &= bag1[i] != bag0[k];
	}
	qa_check(seamClean, "a refill opens clear of the pieces the previous bag closed with");
}

// What one chart spends, and what it deals. Same Shuffle spends one piece however many routes it
// shows; Varied Shuffle spends one per route.
static void qa_test_shuffle_spend(int npool)
{
	char seed[ENDLESS_SEED_MAXLEN];
	unsigned variedSlates = 0, seamHands = 0;

	for (unsigned sample = 0; sample < 96; ++sample)
	{
		watchdog_heartbeat();
		const int depth = (int)(sample % 37);
		snprintf(seed, sizeof(seed), "qa-shuffle-%08x", (unsigned)(sample * 2654435761u));

		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessRunBaseRule = ENDLESS_BASE_VARIED_SHUFFLE;
		const int before = endlessShuffleNext;
		endlessGenerateCourses();
		const int spent = endlessShuffleNext - before;

		bool distinct = true;
		for (int i = 1; i < endlessCourseCnt; ++i)
			for (int k = 0; k < i; ++k)
				distinct &= endlessCourseEp[i] != endlessCourseEp[k]
				         || endlessCourseSec[i] != endlessCourseSec[k];
		qa_check(distinct, "no level appears twice on one Varied Shuffle chart");

		// An Ambush collapses the chart it was dealt, so only an ordinary visit still shows
		// everything the bag paid for.
		if (!endlessForced)
		{
			qa_check(spent == endlessCourseCnt,
			         "a Varied Shuffle chart spends exactly one piece per route");
			++variedSlates;
		}
		else
		{
			qa_check(spent >= endlessCourseCnt,
			         "an Ambush still paid for the routes it collapsed");
		}

		// Reading the chart back is not a deal: only a fresh visit or a reroll moves the bag.
		const int settled = endlessShuffleNext;
		for (int i = 0; i < endlessCourseCount(); ++i)
			(void)endlessCourseName(i);
		qa_check(endlessShuffleNext == settled, "looking at a chart again draws nothing");

		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessRunBaseRule = ENDLESS_BASE_SAME_SHUFFLE;
		endlessGenerateCourses();
		qa_check(endlessShuffleNext == 1, "a Same Shuffle chart spends one piece however wide");
		bool oneLevel = endlessCourseCnt >= 1;
		for (int i = 1; i < endlessCourseCnt; ++i)
			oneLevel &= endlessCourseEp[i] == endlessCourseEp[0]
			         && endlessCourseSec[i] == endlessCourseSec[0];
		qa_check(oneLevel, "Same Shuffle puts every charted route on its one drawn level");
	}

	/* A hand that straddles a refill still has to come out clean, and the hand after one must not
	 * repeat the hand before it. Surveyor widens every chart to the slate maximum so each offset
	 * really does reach the seam. */
	for (unsigned sample = 0; sample < 24; ++sample)
	for (int offset = 1; offset < ENDLESS_MAX_COURSES; ++offset)
	{
		snprintf(seed, sizeof(seed), "qa-shuffle-seam-%02u", sample);
		qa_reset_course_inputs(seed, 12, DIFFICULTY_NORMAL);
		endlessRunBaseRule = ENDLESS_BASE_VARIED_SHUFFLE;
		endlessPerkSetOwned(PERK_SURVEYOR, endlessPerkMaxStack(PERK_SURVEYOR));
		endlessShuffleSetNext(npool - offset);

		// The hand the bag closes on, then the one that opens the refilled bag.
		endlessGenerateCourses();
		int lastEp[ENDLESS_MAX_COURSES];
		JE_byte lastSec[ENDLESS_MAX_COURSES];
		const int lastCnt = endlessCourseCnt;
		for (int i = 0; i < lastCnt; ++i)
		{
			lastEp[i] = endlessCourseEp[i];
			lastSec[i] = endlessCourseSec[i];
		}
		if (endlessShuffleNext <= npool)
		{
			endlessPerkSetOwned(PERK_SURVEYOR, 0);
			continue;   // this chart stopped short of the seam
		}
		++seamHands;

		bool distinct = true;
		for (int i = 1; i < lastCnt; ++i)
			for (int k = 0; k < i; ++k)
				distinct &= lastEp[i] != lastEp[k] || lastSec[i] != lastSec[k];
		qa_check(distinct, "a chart drawn across a refill still holds no duplicate");

		endlessGenerateCourses();
		bool fresh = true;
		for (int i = 0; i < endlessCourseCnt; ++i)
			for (int k = 0; k < lastCnt; ++k)
				fresh &= endlessCourseEp[i] != lastEp[k] || endlessCourseSec[i] != lastSec[k];
		qa_check(fresh, "the first chart off a refilled bag repeats nothing from the last");
		endlessPerkSetOwned(PERK_SURVEYOR, 0);
	}
	qa_check(seamHands > 0, "the seam case saw charts drawn across a refill");

	printf("# level shuffle: %u Varied charts measured, %u drawn across a refill\n",
	       variedSlates, seamHands);
}

// A Radar reroll spends the hand it threw away, and both a peer and a reloaded save land on the
// same next piece.
static void qa_test_shuffle_reroll_and_resume(void)
{
	char seed[ENDLESS_SEED_MAXLEN];
	unsigned rerolls = 0;

	for (unsigned sample = 0; sample < 48; ++sample)
	{
		watchdog_heartbeat();
		const int depth = 3 + (int)(sample % 29);
		snprintf(seed, sizeof(seed), "qa-shuffle-r-%08x", (unsigned)(sample * 2654435761u));

		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessRunBaseRule = ENDLESS_BASE_VARIED_SHUFFLE;
		endlessPerkSetOwned(PERK_RADAR, 1);
		endlessChartVisit();
		const int afterFirst = endlessShuffleNext;
		const Uint32 firstHash = qa_slate_hash();

		endlessCourseReroll();
		const Uint32 rerolledHash = qa_slate_hash();
		qa_check(endlessShuffleNext > afterFirst,
		         "a Radar reroll spends the hand it discarded rather than dealing it again");
		if (rerolledHash != firstHash)
			++rerolls;

		// Surveyor widens the chart, so the reroll has to spend the wider hand.
		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessRunBaseRule = ENDLESS_BASE_VARIED_SHUFFLE;
		endlessPerkSetOwned(PERK_RADAR, 1);
		endlessPerkSetOwned(PERK_SURVEYOR, endlessPerkMaxStack(PERK_SURVEYOR));
		endlessChartVisit();
		const int wideFirst = endlessShuffleNext;
		const int wideCount = endlessCourseCnt;
		const bool wideAmbush = endlessForced;   // the first deal's, not the rerolled deal's
		endlessCourseReroll();
		qa_check(wideAmbush || wideFirst == wideCount,
		         "Surveyor's extra routes are paid for out of the bag");
		qa_check(endlessForced || endlessShuffleNext - wideFirst == endlessCourseCnt,
		         "a rerolled chart pays for every route it deals");
		const int afterReroll = endlessShuffleNext;
		const Uint32 wideHash = qa_slate_hash();

		/* The peer derives the chart from the reroll count alone, so it has to land on the same
		 * piece. Player two hosting the resumed run changes neither. */
		Uint8 wire[ENDLESS_RUN_WIRE_MAX];
		const size_t wireLen = endlessRunSerialize(wire, sizeof(wire));
		qa_check(wireLen > 0, "a Shuffle run serializes for a resuming peer");

		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessRunBaseRule = ENDLESS_BASE_VARIED_SHUFFLE;
		endlessPerkSetOwned(PERK_RADAR, 1);
		endlessPerkSetOwned(PERK_SURVEYOR, endlessPerkMaxStack(PERK_SURVEYOR));
		endlessChartVisit();
		endlessChartSyncRerolls(endlessChartSeat, ENDLESS_CHART_REROLLS);
		qa_check(endlessShuffleNext == afterReroll && qa_slate_hash() == wideHash,
		         "a peer rebuilding the rerolled chart lands on the same next piece");

		// ...and player two hosting the resumed session moves the charting seat without moving the
		// bag, so the adopted record still opens on the piece the run was owed.
		const uint savedSeat = thisPlayerNum;
		const bool savedHost = network_is_host;
		const JE_boolean savedCoop = coopEndlessMode;
		const bool savedNet = isNetworkGame;
		thisPlayerNum = 2;
		network_is_host = true;
		coopEndlessMode = true;
		isNetworkGame = true;
		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessMode = true;
		qa_check(endlessRunAdopt(wire, wireLen) && endlessShuffleNext == afterReroll,
		         "a resumed run draws the piece it was owed, whichever seat is hosting");
		thisPlayerNum = savedSeat;
		network_is_host = savedHost;
		coopEndlessMode = savedCoop;
		isNetworkGame = savedNet;

		endlessPerkSetOwned(PERK_RADAR, 0);
		endlessPerkSetOwned(PERK_SURVEYOR, 0);
	}

	qa_check(rerolls > 0, "the reroll case saw charts change");

	/* The cursor is the one chart input that accumulates instead of being recomputed, so a machine
	 * that drifts has to be pulled back by the charting seat's published hand. */
	qa_reset_course_inputs("qa-shuffle-anchor", 9, DIFFICULTY_NORMAL);
	endlessRunBaseRule = ENDLESS_BASE_VARIED_SHUFFLE;
	endlessChartVisit();
	const int trueHand = endlessShuffleHandStart;
	const Uint32 trueChart = qa_slate_hash();

	endlessShuffleSetNext(trueHand + 3);   // pretend this machine dealt a hand the peer did not
	endlessChartRedeal();
	qa_check(endlessShuffleHandStart != trueHand && qa_slate_hash() != trueChart,
	         "the drift case really moved this machine off the charting seat's hand");
	endlessShuffleSyncHand(endlessChartSeat, trueHand);
	qa_check(endlessShuffleHandStart == trueHand && qa_slate_hash() == trueChart,
	         "a drifted machine re-anchors onto the charting seat's published hand");

	// The other seat's copy of the field says nothing, and neither does a stamp from another visit.
	endlessShuffleSyncHand(endlessChartSeat ^ 1, trueHand + 3);
	qa_check(endlessShuffleHandStart == trueHand, "only the charting seat's hand re-anchors it");
	endlessShuffleHandDepth = endlessRunDepth + 1;
	endlessShuffleSyncHand(endlessChartSeat, trueHand + 3);
	qa_check(endlessShuffleHandStart == trueHand,
	         "a hand dealt for another visit cannot re-anchor this one");
	endlessShuffleHandDepth = endlessRunDepth;

	// Unshuffled rules never publish a hand, so a stray one must not disturb their chart.
	qa_reset_course_inputs("qa-shuffle-anchor", 9, DIFFICULTY_NORMAL);
	endlessChartVisit();
	const Uint32 variedChart = qa_slate_hash();
	endlessShuffleSyncHand(endlessChartSeat, 500);
	qa_check(qa_slate_hash() == variedChart && endlessShuffleNext == 0,
	         "a Varied run ignores a published hand");

	// A hand-edited or corrupt cursor must not index off the end of the bag.
	int ep = 0;
	JE_byte sec = 0, file = 0;
	endlessShuffleSetNext(-5);
	qa_check(endlessShuffleNext == 0, "a negative saved cursor restarts the bag");
	endlessShuffleSetNext(ENDLESS_SHUFFLE_POSITION_MAX + 1000);
	qa_check(endlessShuffleNext == ENDLESS_SHUFFLE_POSITION_MAX, "a runaway cursor is capped");
	qa_check(endlessShuffleSafeLevel(endlessShuffleNext, &ep, &sec, &file),
	         "the capped cursor still draws a real level");
	qa_check(!endlessShuffleSafeLevel(-1, &ep, &sec, &file), "a negative position draws nothing");
	endlessShuffleSetNext(0);
}

static void qa_test_course_shuffle_rule(void)
{
	QaPoolEntry pool[QA_POOL_MAX];
	const int npool = qa_level_pool(pool);
	qa_check(npool > 0, "the endless-safe level pool is nonempty");
	if (npool <= 0)
		return;

	qa_test_shuffle_permutation(pool, npool);
	qa_test_shuffle_spend(npool);
	qa_test_shuffle_reroll_and_resume();
}

/* Radar's chart reroll. It has to deal a genuinely different visit, leave an unrerolled visit on
 * the salt every existing seed was played on, stay reproducible from the reroll count alone (which
 * is all that travels between two machines), and hand out exactly one reroll per outpost. */
static void qa_test_course_reroll(void)
{
	char seed[ENDLESS_SEED_MAXLEN];
	const unsigned samples = 96;
	unsigned changed = 0, widened = 0;

	for (unsigned sample = 0; sample < samples; ++sample)
	{
		watchdog_heartbeat();
		const int depth = 3 + (int)(sample % 40);
		snprintf(seed, sizeof(seed), "qa-reroll-%08x", (unsigned)(sample * 2654435761u));

		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessChartVisit();
		const Uint32 firstHash = qa_slate_hash();
		const Uint64 firstSalt = endlessZonePhaseSalt(1);

		qa_check(firstSalt == (Uint64)depth * 2 + 1,
		         "an unrerolled visit keeps the zone phase salt its seed has always used");
		qa_check(!endlessCourseRerollOffered(),
		         "a chart offers no reroll without the Radar perk");
		endlessCourseReroll();
		qa_check(qa_slate_hash() == firstHash && endlessChartRerolls == 0,
		         "a reroll without the perk leaves the chart alone");

		endlessPerkSetOwned(PERK_RADAR, 1);
		qa_check(endlessCourseRerollOffered() && !endlessCourseRerollSpent(),
		         "the Radar perk offers this visit's chart one reroll");

		endlessCourseReroll();
		const Uint32 rerolledHash = qa_slate_hash();
		qa_check(endlessCourseRerollSpent(), "a used reroll reads as spent");
		qa_check(endlessZonePhaseSalt(1) != firstSalt,
		         "a reroll moves the charted zone's own phases with it");
		if (rerolledHash != firstHash)
			++changed;

		endlessCourseReroll();
		qa_check(qa_slate_hash() == rerolledHash && endlessChartRerolls == ENDLESS_CHART_REROLLS,
		         "a spent reroll cannot be used again");

		// The count is all the other machine receives, so replaying it must rebuild the chart.
		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessPerkSetOwned(PERK_RADAR, 1);
		endlessChartVisit();
		endlessChartSyncRerolls(endlessChartSeat, ENDLESS_CHART_REROLLS);
		qa_check(qa_slate_hash() == rerolledHash,
		         "a peer rebuilds the rerolled chart from the reroll count alone");

		// Star Charts widens a chart, and a redeal replays the visit from the same inputs rather
		// than swallowing the banked boon. Ambush is the case that hands it back instead.
		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessPerkSetOwned(PERK_RADAR, 1);
		endlessStarChartsOwed = true;
		endlessChartVisit();
		if (endlessCourseCnt == ENDLESS_MAX_COURSES)
			++widened;
		endlessCourseReroll();
		qa_check(endlessCourseCnt == ENDLESS_MAX_COURSES || endlessStarChartsOwed,
		         "a rerolled chart either still spends Star Charts or hands the boon back");
		endlessPerkSetOwned(PERK_RADAR, 0);
	}

	endlessChartRerolls = 0;  // leave the phase salts where the tests after this one expect them

	qa_check(changed * 4 >= samples * 3, "rerolling deals a different chart on most seeds");
	qa_check(widened > 0, "the Star Charts case saw charts widened by the boon");

	printf("# chart reroll: %u seeds, %u dealt a different slate, %u widened by Star Charts\n",
	       samples, changed, widened);
}

static bool qa_slate_carries(Uint64 bits)
{
	for (int i = 0; i < endlessCourseCnt; ++i)
		if (endlessCourseMod[i] & bits)
			return true;
	return false;
}

/* The chart is derived on each machine rather than sent, so both must build the same slate from the
 * same run state. Anything seat-local leaking into generation shows up here as a hash mismatch. */
static void qa_test_course_seat_parity(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedHost = network_is_host;
	const uint savedThis = thisPlayerNum;
	const uint savedHostNum = networkHostPlayerNum;
	const bool savedCoop = coopEndlessMode, savedTwo = twoPlayerMode;
	const EndlessCourseChooser savedChooser = endlessCourseChooser;

	static const EndlessCourseChooser choosers[] = {
		ENDLESS_PICK_HOST, ENDLESS_PICK_GUEST, ENDLESS_PICK_COINFLIP,
	};
	unsigned compared = 0;
	bool agree = true;

	for (unsigned c = 0; c < COUNTOF(choosers); ++c)
	{
		for (unsigned s = 0; s < 24; ++s)
		{
			watchdog_heartbeat();
			char seed[ENDLESS_SEED_MAXLEN];
			snprintf(seed, sizeof(seed), "qa-seat-%08x", (unsigned)((c * 24 + s) * 2654435761u));
			const int depth = 1 + (int)(s * 6);   // spans the flat surface, the ramp, and the cap

			Uint32 want = 0;
			for (uint machine = 1; machine <= 2; ++machine)
			{
				isNetworkGame = true;
				coopEndlessMode = true;
				twoPlayerMode = true;
				networkHostPlayerNum = 1;
				thisPlayerNum = machine;
				network_is_host = (machine == 1);
				endlessCourseChooser = choosers[c];

				qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
				endlessChartVisit();
				if (machine == 1)
					want = qa_slate_hash();
				else if (qa_slate_hash() != want)
					agree = false;
			}
			++compared;
		}
	}

	isNetworkGame = savedNet;
	network_is_host = savedHost;
	thisPlayerNum = savedThis;
	networkHostPlayerNum = savedHostNum;
	coopEndlessMode = savedCoop;
	twoPlayerMode = savedTwo;
	endlessCourseChooser = savedChooser;

	qa_check(agree, "both machines derive the same chart from the same run state");
	printf("# chart parity: %u seed and chooser cases compared across both seats\n", compared);
}

/* A reroll re-places the scheduled signature sectors along with everything else, so it can be spent
 * to leave a zone that offered one. */
static void qa_test_course_reroll_dodge(void)
{
	// Bits only the scheduled rows deal. SLUGGISH is excluded: it is also a theme signature.
	const Uint64 scheduled = ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION | ENDLESS_MOD_WARP
	                       | ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_HOMING | ENDLESS_MOD_KAMIKAZE
	                       | ENDLESS_MOD_DEADGEN | ENDLESS_MOD_OVERHEAT;
	unsigned offered = 0, dodged = 0;

	for (unsigned s = 0; s < 400; ++s)
	{
		watchdog_heartbeat();
		const int depth = 3 + (int)(s % 90);
		if (endlessMilestoneKindOfZone(depth + 1))
			continue;   // a milestone slate deals its own modifiers over the injected ones

		char seed[ENDLESS_SEED_MAXLEN];
		snprintf(seed, sizeof(seed), "qa-dodge-%08x", (unsigned)(s * 2654435761u));
		qa_reset_course_inputs(seed, depth, DIFFICULTY_NORMAL);
		endlessPerkSetOwned(PERK_RADAR, 1);
		endlessChartVisit();
		if (!qa_slate_carries(scheduled))
			continue;

		++offered;
		endlessCourseReroll();
		if (!qa_slate_carries(scheduled))
			++dodged;
	}

	endlessPerkSetOwned(PERK_RADAR, 0);
	endlessChartRerolls = 0;  // leave the phase salts where the tests after this one expect them

	qa_check(offered > 40, "the dodge sample saw enough charts carrying a scheduled sector");
	qa_check(dodged * 2 > offered,
	         "a reroll clears the scheduled sector from most charts that carried one");
	printf("# reroll dodge: %u charts carried a scheduled sector, %u cleared it\n",
	       offered, dodged);
}

static void qa_test_perk_registry(void)
{
	const int count = endlessPerkCount();
	qa_check(count > 0, "perk registry is nonempty");
	for (int i = 0; i < count; ++i)
	{
		qa_check(qa_display_name_valid(endlessPerkName(i)),
		         "perk name fits its menu field and uses visible glyphs");
		qa_check(endlessPerkDesc(i)[0] != '\0' && endlessPerkMaxStack(i) > 0,
		         "perk registry row has a description and positive stack limit");
		for (int j = 0; j < i; ++j)
			qa_check(strcmp(endlessPerkName(i), endlessPerkName(j)) != 0,
			         "perk names are unique");
		endlessPerkSetOwned(i, -999);
		qa_check(endlessPerkGetOwned(i) == 0, "perk ownership clamps below zero");
		endlessPerkSetOwned(i, endlessPerkMaxStack(i) + 999);
		qa_check(endlessPerkGetOwned(i) == endlessPerkMaxStack(i),
		         "perk ownership clamps to the registry maximum");
		endlessPerkSetOwned(i, 0);
	}
	qa_check(endlessPerkName(-1)[0] == '\0' && endlessPerkDesc(count)[0] == '\0'
	         && endlessPerkMaxStack(-1) == 0 && endlessPerkGetOwned(count) == 0,
	         "perk accessors safely reject invalid identifiers");

	endlessSetSeed("qa-perk-offers");
	endlessReseedPlayers(0x5151);   // the slate is dealt from the offered player's own stream
	endlessGeneratePerkChoices(999);
	int first[ENDLESS_PERK_OFFERS_MILESTONE];
	memcpy(first, endlessPerkChoice, sizeof(first));
	bool offersValid = endlessPerkChoiceCount() == ENDLESS_PERK_OFFERS_MILESTONE;
	for (int i = 0; i < endlessPerkChoiceCount(); ++i)
	{
		offersValid &= endlessPerkChoice[i] >= 0 && endlessPerkChoice[i] < count;
		for (int j = 0; j < i; ++j)
			offersValid &= endlessPerkChoice[i] != endlessPerkChoice[j];
	}
	qa_check(offersValid, "perk offers clamp to their persisted array and contain no duplicates");
	endlessReseedPlayers(0x5151);
	endlessGeneratePerkChoices(ENDLESS_PERK_OFFERS_MILESTONE);
	qa_check(memcmp(first, endlessPerkChoice, sizeof(first)) == 0,
	         "perk offer generation is deterministic on the per-player RNG");
	endlessGeneratePerkChoices(-999);
	qa_check(endlessPerkChoiceCount() == 0 && endlessPerkChoiceName(0)[0] == '\0'
	         && endlessPerkChoiceDesc(-1)[0] == '\0',
	         "perk offer access safely handles an empty or invalid slate");

	for (int i = 0; i < count; ++i)
		endlessPerkSetOwned(i, endlessPerkMaxStack(i));
	endlessPerkSetOwned(PERK_ORDNANCE, 0);
	endlessGeneratePerkChoices(ENDLESS_PERK_OFFERS_MILESTONE);
	qa_check(endlessPerkChoiceCount() == 1 && endlessPerkChoice[0] == PERK_ORDNANCE,
	         "perk offers exclude maxed perks and shrink when only one choice remains");

	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	endlessMode = true; endlessCampaignMods = false;
	endlessPerkSetOwned(PERK_ORDNANCE, endlessPerkMaxStack(PERK_ORDNANCE));
	bool ammoBounded = endlessPerkSidekickAmmo(0) == 0;
	for (int base = 1; base <= 250; ++base)
	{
		const int boosted = endlessPerkSidekickAmmo(base);
		const int refill = endlessPerkSidekickRefillTicks(35, base);
		ammoBounded &= boosted >= base && boosted <= 250 && refill >= 1 && refill <= 35;
	}
	qa_check(ammoBounded,
	         "Ordnance Reserves never shrinks stock ammo, overflows its byte-safe cap, or stalls refill");
	qa_check(endlessPerkSpecialDuration(200, 255) == 255
	         && endlessPerkSpecialDuration(10, 0) > 10,
	         "Ordnance Reserves respects byte caps while allowing uncapped durations");
	endlessMode = savedMode; endlessCampaignMods = savedCampaign;
	for (int i = 0; i < count; ++i)
		endlessPerkSetOwned(i, 0);
}

static void qa_test_record_readers(void)
{
	int  savedDiff[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT];
	bool savedDiffCustom[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT];
	int  savedUntagged[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT];
	bool savedUntaggedCustom[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT];
	memcpy(savedDiff, endlessBestZoneDiff, sizeof(savedDiff));
	memcpy(savedDiffCustom, endlessBestZoneDiffCustom, sizeof(savedDiffCustom));
	memcpy(savedUntagged, endlessBestZoneUntagged, sizeof(savedUntagged));
	memcpy(savedUntaggedCustom, endlessBestZoneUntaggedCustom, sizeof(savedUntaggedCustom));
	memset(endlessBestZoneDiff, 0, sizeof(savedDiff));
	memset(endlessBestZoneDiffCustom, 0, sizeof(savedDiffCustom));
	memset(endlessBestZoneUntagged, 0, sizeof(savedUntagged));
	memset(endlessBestZoneUntaggedCustom, 0, sizeof(savedUntaggedCustom));

	endlessBestZoneDiff[0][0][ENDLESS_RUNMODE_STANDARD][0] = 20;
	endlessBestZoneDiff[0][0][ENDLESS_RUNMODE_STANDARD][1] = 20;
	endlessBestZoneDiffCustom[0][0][ENDLESS_RUNMODE_STANDARD][1] = true;
	endlessBestZoneUntagged[0][0][ENDLESS_RUNMODE_STANDARD] = 15;
	endlessBestZoneUntaggedCustom[0][0][ENDLESS_RUNMODE_STANDARD] = true;
	qa_check(endlessBestZoneAny(0, 0, ENDLESS_RUNMODE_STANDARD) == 20
	         && strcmp(endlessRecordAnyCustomMark(0, 0, ENDLESS_RUNMODE_STANDARD), " C") == 0,
	         "deepest record derives its custom mark from any marked record tied at that depth");
	endlessBestZoneUntagged[0][0][ENDLESS_RUNMODE_STANDARD] = 25;
	endlessBestZoneUntaggedCustom[0][0][ENDLESS_RUNMODE_STANDARD] = false;
	qa_check(endlessBestZoneAny(0, 0, ENDLESS_RUNMODE_STANDARD) == 25
	         && endlessRecordAnyCustomMark(0, 0, ENDLESS_RUNMODE_STANDARD)[0] == '\0',
	         "legacy untagged record survives and owns the mode-wide mark when deepest");
	endlessBestZoneUntaggedCustom[0][0][ENDLESS_RUNMODE_STANDARD] = true;
	qa_check(strcmp(endlessRecordAnyCustomMark(0, 0, ENDLESS_RUNMODE_STANDARD), " C") == 0,
	         "legacy untagged custom mark is retained");

	/* The two crew sizes keep separate books: a solo record is invisible to the co-op table. */
	endlessBestZoneDiff[0][1][ENDLESS_RUNMODE_STANDARD][0] = 60;
	qa_check(endlessBestZoneAny(0, 1, ENDLESS_RUNMODE_STANDARD) == 60
	         && endlessBestZoneAny(0, 0, ENDLESS_RUNMODE_STANDARD) == 25,
	         "one-player and two-player zone records are kept apart");
	endlessClearDeepestRecord(0, 1, ENDLESS_RUNMODE_STANDARD);
	qa_check(endlessBestZoneAny(0, 1, ENDLESS_RUNMODE_STANDARD) == 0
	         && endlessBestZoneAny(0, 0, ENDLESS_RUNMODE_STANDARD) == 25,
	         "erasing a co-op record leaves the solo one standing");

	/* ...and so does every chart rule: no run can reach another rule's record. */
	for (int rule = 1; rule < ENDLESS_BASE_TABLES; ++rule)
	{
		endlessBestZoneDiff[rule][0][ENDLESS_RUNMODE_STANDARD][0] = 80;
		qa_check(endlessBestZoneAny(rule, 0, ENDLESS_RUNMODE_STANDARD) == 80
		         && endlessBestZoneAny(0, 0, ENDLESS_RUNMODE_STANDARD) == 25,
		         "each Base Level rule keeps its zone records apart from Varied's");
		endlessClearRecordDifficulty(rule, 0, ENDLESS_RUNMODE_STANDARD, 0);
		qa_check(endlessBestZoneAny(rule, 0, ENDLESS_RUNMODE_STANDARD) == 0
		         && endlessBestZoneAny(0, 0, ENDLESS_RUNMODE_STANDARD) == 25,
		         "erasing one rule's record leaves the varied-base one standing");
	}

	/* The record page opens on one figure per mode, so that figure has to be the deepest of the
	 * four rules and carry whichever of them owns it, without ever reaching across the crew sizes
	 * the rules sit inside. */
	endlessBestZoneDiff[ENDLESS_BASE_SAME_SHUFFLE][0][ENDLESS_RUNMODE_STANDARD][2] = 41;
	endlessBestZoneDiffCustom[ENDLESS_BASE_SAME_SHUFFLE][0][ENDLESS_RUNMODE_STANDARD][2] = true;
	qa_check(endlessBestZoneAnyRule(0, ENDLESS_RUNMODE_STANDARD) == 41
	         && strcmp(endlessRecordAnyRuleCustomMark(0, ENDLESS_RUNMODE_STANDARD), " C") == 0
	         && endlessBestZoneAnyRule(1, ENDLESS_RUNMODE_STANDARD) == 0,
	         "a mode's figure is the deepest rule's, with that rule's mark and its own crew size");
	endlessBestZoneDiff[ENDLESS_BASE_VARIED][0][ENDLESS_RUNMODE_STANDARD][2] = 55;
	qa_check(endlessBestZoneAnyRule(0, ENDLESS_RUNMODE_STANDARD) == 55
	         && endlessRecordAnyRuleCustomMark(0, ENDLESS_RUNMODE_STANDARD)[0] == '\0',
	         "a deeper unmarked rule takes the figure and drops the mark with it");
	endlessBestZoneDiff[ENDLESS_BASE_VARIED][0][ENDLESS_RUNMODE_STANDARD][2] = 0;
	endlessClearRecordDifficulty(ENDLESS_BASE_SAME_SHUFFLE, 0, ENDLESS_RUNMODE_STANDARD, 2);

	// Each rule names a distinct board, and the menu order lists each exactly once.
	bool namesDistinct = true, orderComplete = true;
	for (int rule = 0; rule < ENDLESS_BASE_TABLES; ++rule)
	{
		for (int other = 0; other < rule; ++other)
			namesDistinct &= strcmp(endlessBaseLevelRuleName(rule),
			                        endlessBaseLevelRuleName(other)) != 0;
		orderComplete &= endlessBaseRuleMenuIndex(endlessBaseRuleAtMenuIndex(rule)) == rule
		              && qa_display_name_valid(endlessBaseLevelRuleName(rule));
	}
	qa_check(namesDistinct && orderComplete,
	         "the Base Level rules have distinct names and one menu place each");

	bool difficultyMap = true;
	for (int i = 0; i < ENDLESS_DIFFICULTY_COUNT; ++i)
		difficultyMap &= endlessDifficultySlot(endlessDifficultyLevel[i]) == i;
	qa_check(difficultyMap && endlessDifficultySlot(-999) == -1
	         && endlessBestZoneAny(0, 0, (EndlessRunMode)-1) == 0
	         && endlessBestZoneAny(0, -1, ENDLESS_RUNMODE_STANDARD) == 0
	         && endlessBestZoneAny(-1, 0, ENDLESS_RUNMODE_STANDARD) == 0
	         && endlessBestZoneAny(ENDLESS_BASE_TABLES, 0, ENDLESS_RUNMODE_STANDARD) == 0
	         && endlessBestZoneForDifficulty(0, 0, ENDLESS_RUNMODE_STANDARD, -1) == 0,
	         "record readers preserve difficulty ordering and reject invalid indices");

	memcpy(endlessBestZoneDiff, savedDiff, sizeof(savedDiff));
	memcpy(endlessBestZoneDiffCustom, savedDiffCustom, sizeof(savedDiffCustom));
	memcpy(endlessBestZoneUntagged, savedUntagged, sizeof(savedUntagged));
	memcpy(endlessBestZoneUntaggedCustom, savedUntaggedCustom, sizeof(savedUntaggedCustom));
}

static Uint32 qa_prng(Uint32 *state)
{
	Uint32 x = *state;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*state = x;
	return x;
}

static void qa_test_config_option_removal(void)
{
	Config config = {0};
	ConfigSection *section = config_find_or_add_section(&config, "test", NULL);
	qa_check(section != NULL, "config option removal creates its test section");
	if (section != NULL)
	{
		config_set_option(section, "first", "1");
		config_set_option(section, "retired", "2");
		config_set_option(section, "last", "3");

		qa_check(config_remove_option(section, "retired") && section->options_count == 2 &&
		             config_get_option(section, "first") != NULL && config_get_option(section, "retired") == NULL &&
		             config_get_option(section, "last") != NULL,
		         "config option removal compacts a middle entry");
		qa_check(!config_remove_option(section, "missing") && section->options_count == 2,
		         "config option removal leaves a missing key unchanged");
		qa_check(config_remove_option(section, "first") && config_remove_option(section, "last") &&
		             section->options_count == 0 && section->options == NULL,
		         "config option removal releases an empty option array");
	}

	config_deinit(&config);
	qa_check(config.sections_count == 0 && config.sections == NULL, "config teardown resets section ownership");
}

static void qa_test_weapon_editor(void)
{
	static char encoded[32768], encodedAgain[32768], malformed[4096];
	Uint32 rng = 0xc0570e11u;

	customWeaponResetAllLevels();
	bool presetsValid = customBulletPresetCount > 0 && customBulletPresetCount <= CUSTOM_BULLET_PRESET_MAX;
	for (int i = 0; i < customBulletPresetCount; ++i)
	{
		const CustomBulletPreset *const p = &customBulletPreset[i];
		presetsValid &= p->name[0] != '\0' && memchr(p->name, '\0', sizeof(p->name)) != NULL
		             && customBulletMaxPower(i) >= 1
		             && customBulletMaxPower(i) <= CUSTOM_POWER_LEVELS
		             && ((p->sourcePort >= 1 && p->sourcePort <= PORT_NUM)
		                 || (p->sourcePort == 0 && p->sourceWeapon >= 1 && p->sourceWeapon <= WEAP_NUM));
		for (int j = 0; j < i; ++j)
			presetsValid &= SDL_strcasecmp(p->name, customBulletPreset[j].name) != 0;
	}
	qa_check(presetsValid,
	         "weapon import registry is bounded, valid, terminated, and deduplicated");
	qa_check(customBulletMaxPower(-1) == 1
	         && customBulletMaxPower(customBulletPresetCount) == 1,
	         "weapon import registry safely handles invalid source indices");
	customWeaponSelectMode(-1000);
	customWeaponSelectLevel(-1000);
	qa_check(customWeaponEditMode == 0 && customWeaponEditLevel == 0,
	         "weapon editor clamps negative selections");
	customWeaponSelectMode(1000);
	customWeaponSelectLevel(1000);
	qa_check(customWeaponEditMode == CUSTOM_WEAPON_MODES - 1
	         && customWeaponEditLevel == CUSTOM_POWER_LEVELS - 1,
	         "weapon editor clamps oversized selections");

	customWeaponSelectMode(0);
	customWeaponSelectLevel(0);
	customWeaponSerializeLevel(0, 0, encoded, sizeof(encoded));
	customWeaponDeserializeLevel(0, 0, encoded);
	customWeaponSerializeLevel(0, 0, encodedAgain, sizeof(encodedAgain));
	qa_check(strcmp(encoded, encodedAgain) == 0,
	         "weapon editor serialization round-trips exactly");

	/* Exercise the config/editor parser with deterministic junk and huge integers. */
	static const char alphabet[] = "0123456789,+- xyz";
	for (unsigned pass = 0; pass < 256; ++pass)
	{
		const size_t n = qa_prng(&rng) % (sizeof(malformed) - 1);
		for (size_t i = 0; i < n; ++i)
			malformed[i] = alphabet[qa_prng(&rng) % (sizeof(alphabet) - 1)];
		malformed[n] = '\0';
		const int mode = (int)(pass % CUSTOM_WEAPON_MODES);
		const int level = (int)(pass % CUSTOM_POWER_LEVELS);
		customWeaponDeserializeLevel(mode, level, malformed);
		const JE_WeaponType *w = &customWeaponRaw[mode][level];
		qa_check(w->multi >= 1 && w->multi <= CUSTOM_BULLETS_MAX
		         && w->max >= 1 && w->max <= CUSTOM_BULLETS_MAX
		         && w->sound <= CUSTOM_SOUND_MAX,
		         "weapon editor rejects malformed values outside engine bounds");
	}

	customWeaponReset();
	int selected = 0;
	while ((selected = customWeaponAddBullet(selected)) >= 0)
		;
	qa_check(customWeaponRaw[0][0].multi == CUSTOM_BULLETS_MAX
	         && customWeaponAddBullet(0) == -1,
	         "weapon editor bullet pool stops at its exact capacity");
	while ((selected = customWeaponRemoveBullet(0)) >= 0)
		;
	qa_check(customWeaponRaw[0][0].multi == 1 && customWeaponRemoveBullet(0) == -1,
	         "weapon editor bullet pool keeps one valid segment");

	customWeaponChargeStages = 1;
	while (customWeaponAddChargeState() >= 0)
		;
	qa_check(customWeaponChargeStages == CUSTOM_POWER_LEVELS,
	         "weapon editor charge pool stops at its exact capacity");
	while (customWeaponRemoveChargeState() >= 0)
		;
	qa_check(customWeaponChargeStages == 1,
	         "weapon editor charge pool keeps one valid stage");

	customWeaponModes = 999;
	customWeaponCost = 1000000;
	customWeaponPowerUse = 1000000;
	customWeaponItemGraphic = 1000000;
	customWeaponChargeStages = 999;
	customSidekickMount = 999;
	customSidekickSprite = 1000000;
	customSidekickFrames = 999;
	customSidekickFrameStep = 999;
	customSidekickAnimate = 999;
	customWeaponMaterialize();
	if (customWeaponPort > 0 && customWeaponPort <= PORT_NUM
	 && customSidekickSlot > 0 && customSidekickSlot <= OPTION_NUM)
	{
		const JE_OptionType *const o = &options[customSidekickSlot];
		const int spriteCount = customSidekickSpriteCount(CUSTOM_SIDEKICK_MOUNTS - 1);
		const int spriteHi = spriteCount > CUSTOM_POWER_LEVELS - 1
		                   ? spriteCount - (CUSTOM_POWER_LEVELS - 1) : 1;
		bool framesSafe = true;
		for (int i = 0; i < 20; ++i)
			framesSafe &= o->gr[i] >= 1 && o->gr[i] <= spriteHi;
		qa_check(customWeaponModes == CUSTOM_WEAPON_MODES
		         && weaponPort[customWeaponPort].cost == 64000
		         && weaponPort[customWeaponPort].poweruse == 255
		         && weaponPort[customWeaponPort].itemgraphic == 237,
		         "weapon materialization clamps hostile port metadata to engine field bounds");
		qa_check(o->pwr == CUSTOM_POWER_LEVELS - 1 && o->tr == CUSTOM_SIDEKICK_MOUNTS - 1
		         && o->option == 2 && o->ani == 20 && o->cost == 64000 && framesSafe,
		         "sidekick materialization keeps charge and every sprite frame in its sheet");
	}
	else
	{
		qa_check(false, "custom weapon test slots were reserved during startup");
	}

	/* The in-memory library is another fixed pool. Do not involve the user's config file. */
	customWeaponLibCount = 1;
	customWeaponCurrentSlot = 0;
	customWeaponSelectSlot(-999);
	qa_check(customWeaponCurrentSlot == 0,
	         "weapon library clamps a negative slot selection");
	while (customWeaponLibraryNew() >= 0)
		;
	qa_check(customWeaponLibCount == CUSTOM_WEAPON_LIB_MAX
	         && customWeaponLibraryNew() == -1 && customWeaponLibraryDuplicate() == -1,
	         "weapon library stops exactly at its fixed capacity");
	customWeaponSelectSlot(999);
	qa_check(customWeaponCurrentSlot == CUSTOM_WEAPON_LIB_MAX - 1,
	         "weapon library clamps an oversized slot selection");
	while (customWeaponLibraryDelete() >= 0)
		;
	qa_check(customWeaponLibCount == 1 && customWeaponLibraryDelete() == -1,
	         "weapon library refuses to delete its final valid entry");

	/* Materialize every fuzzed level so sanitizer runs cover engine-array writes too. */
	customWeaponMaterialize();
}

/* The Online Campaign design exchange. Both machines fly both ships, so a design that decodes
 * to anything other than what was encoded is a desync; a short or hostile stream must be
 * refused rather than compiled. */
static void qa_test_custom_weapon_wire(void)
{
	const int owner = CUSTOM_WEAPON_OWNERS - 1;
	const int port = customWeaponOwnerPort[owner];
	if (port <= 0 || port > PORT_NUM)
	{
		qa_check(false, "custom weapon owner slots were reserved during startup");
		return;
	}

	customWeaponResetAllLevels();
	SDL_strlcpy(customWeaponName, "QA Wire Gun", sizeof(customWeaponName));
	customWeaponCost = 12500;
	customWeaponPowerUse = 7;
	customWeaponModes = CUSTOM_WEAPON_MODES;
	customWeaponChargeStages = 4;
	customWeaponRaw[0][0].multi = 3;
	customWeaponRaw[0][0].max = 3;
	customWeaponRaw[0][0].sy[2] = -9;   /* signed field: the wire carries bytes */
	customWeaponRaw[0][0].sg[2] = 1234;
	customWeaponRaw[1][10].attack[0] = 99;
	customWeaponMaterialize();

	Uint8 *const stream = malloc(CUSTOM_WEAPON_WIRE_MAX);
	if (stream == NULL)
	{
		qa_check(false, "custom weapon wire buffer allocation");
		return;
	}

	const size_t total = customWeaponSerializeDesign(stream, CUSTOM_WEAPON_WIRE_MAX);
	qa_check(total > 0 && total <= CUSTOM_WEAPON_WIRE_MAX,
	         "custom weapon design encodes inside its declared wire bound");

	qa_check(customWeaponSerializeDesign(stream, 8) == 0,
	         "custom weapon encoder reports failure instead of running past a short buffer");

	const size_t reencoded = customWeaponSerializeDesign(stream, CUSTOM_WEAPON_WIRE_MAX);
	qa_check(reencoded == total && customWeaponAdoptDesign(owner, stream, total),
	         "custom weapon design decodes into the other player's reserved slots");

	const int scratch = CUSTOM_WEAP_BASE + owner * CUSTOM_WEAPON_MODES * CUSTOM_POWER_LEVELS;
	qa_check(weaponPort[port].opnum == CUSTOM_WEAPON_MODES
	         && weaponPort[port].cost == 12500 && weaponPort[port].poweruse == 7
	         && weapons[scratch].multi == 3 && weapons[scratch].sy[2] == -9
	         && weapons[scratch].sg[2] == 1234
	         && weapons[scratch + CUSTOM_POWER_LEVELS + 10].attack[0] == 99,
	         "adopted design reaches the port and every scratch weapon slot intact");

	// A peer's weapon fires through its own reserved port, and the record mark must still see it.
	qa_check(customWeaponPortIsCustom((JE_word)customWeaponOwnerPort[0])
	         && customWeaponPortIsCustom((JE_word)port)
	         && !customWeaponPortIsCustom(1),
	         "the fired-a-custom-weapon test covers every owner's port, not just the local one");

	qa_check(!customWeaponAdoptDesign(owner, stream, total / 2)
	         && !customWeaponAdoptDesign(owner, stream, 2)
	         && !customWeaponAdoptDesign(-1, stream, total)
	         && !customWeaponAdoptDesign(CUSTOM_WEAPON_OWNERS, stream, total),
	         "custom weapon decoder refuses a truncated stream and an out-of-range owner");

	stream[0] = CUSTOM_WEAPON_WIRE_VERSION + 1;
	qa_check(!customWeaponAdoptDesign(owner, stream, total),
	         "custom weapon decoder refuses an unknown wire version");

	free(stream);

	// Leave the reserved port as the item data had it; nothing else in this run owns it.
	customWeaponResetAllLevels();
	customWeaponMaterialize();
}

static void qa_test_fixed_pool_layout(void)
{
	qa_check(RL_ID_ENEMY_BASE + (int)COUNTOF(enemy) <= RL_ID_ENEMYBAR_BASE
	         && RL_ID_ENEMYBAR_BASE + (int)COUNTOF(enemy) <= RL_ID_PSHOT_BASE
	         && RL_ID_PSHOT_BASE + MAX_PWEAPON <= RL_ID_ESHOT_BASE
	         && RL_ID_ESHOT_BASE + ENEMY_SHOT_MAX <= RL_ID_EXPL_BASE
	         && RL_ID_EXPL_BASE + MAX_EXPLOSIONS <= RL_ID_SHIP_BASE,
	         "render identities leave non-overlapping headroom for every fixed object pool");
	qa_check(RL_ID_SHIP_BASE + 2 <= RL_ID_SHIP_TRIM_BASE
	         && RL_ID_SHIP_TRIM_BASE + 2 <= RL_ID_SIDEKICK_BASE
	         && RL_ID_SIDEKICK_BASE + 4 <= RL_ID_LINKGUN_BASE
	         && RL_ID_LINKGUN_BASE + 3 < RL_ID_MAX,
	         "render identities for ships, sidekicks, and link guns remain disjoint");
	qa_check(sizeof(PlayerItems) == 13 && SAVE_RECORD_PACKED_SIZE == 81
#ifdef WITH_NETWORK
	         && NETWORK_SETTINGS_SIZE == 48
#endif
	         , "network wire-layout constants retain their protocol widths");
}

static void qa_test_save_record_wire(void)
{
	JE_SaveFileType src, dst;
	Uint8 guarded[SAVE_RECORD_PACKED_SIZE + 2];
	Uint8 repacked[SAVE_RECORD_PACKED_SIZE];
	Uint8 *const packed = guarded + 1;

	memset(&src, 0, sizeof(src));
	src.encode = 0xaaaa;
	src.level = 0x1234;
	for (unsigned i = 0; i < sizeof(src.items); ++i)
	{
		src.items[i] = (JE_byte)(i * 7 + 1);
		src.lastItems[i] = (JE_byte)(255 - i * 9);
	}
	src.score = -1234567;
	src.score2 = 0x76543210;
	strcpy(src.levelName, "QA LEVEL");
	strcpy(src.name, "WIRE PLAYER");
	src.cubes = 17;
	src.power[0] = 4; src.power[1] = 11;
	src.episode = 5; src.difficulty = DIFFICULTY_LORD_OF_GAME;
	src.secretHint = 3; src.input1 = 1; src.input2 = 2;
	src.gameHasRepeated = true; src.initialDifficulty = DIFFICULTY_HARD;
	src.highScore1 = 0x11111111; src.highScore2 = (JE_longint)0xc74f4321u;
	strcpy(src.highScoreName, "NOT SENT"); src.highScoreDiff = 9;
	src.autoFireSpecial = true; src.chargeSidekickAutofire = 2;
	src.difficultyAdjust = true; src.cheatInfiniteSidekickAmmo = true;
	src.cheatInfiniteShields = false; src.cheatInfiniteArmor = true; src.expertMode = true;

	memset(guarded, 0xa5, sizeof(guarded));
	save_record_pack(packed, &src);
	qa_check(guarded[0] == 0xa5 && guarded[sizeof(guarded) - 1] == 0xa5,
	         "save-record packing writes exactly its fixed 81-byte frame");
	save_record_unpack(&dst, packed);
	save_record_pack(repacked, &dst);
	qa_check(memcmp(packed, repacked, sizeof(repacked)) == 0,
	         "network save record pack/unpack round-trips every serialized field");
	qa_check(dst.encode == 0 && dst.highScore1 == 0 && dst.highScore2 == src.highScore2
	         && dst.highScoreName[0] == '\0' && dst.highScoreDiff == 0,
	         "network save record preserves campaign data and clears non-wire metadata");
	qa_check(save_record_is_coop(&dst),
	         "network save record preserves the Online Campaign type marker");
	qa_check(dst.gameHasRepeated && dst.autoFireSpecial && dst.difficultyAdjust
	         && dst.cheatInfiniteSidekickAmmo && !dst.cheatInfiniteShields
	         && dst.cheatInfiniteArmor && dst.expertMode,
	         "network save record preserves all boolean gameplay flags");

	/* Hostile fixed-width strings still have to become safe C strings on receipt. */
	memset(packed + 38, 'L', 11);
	memset(packed + 49, 'N', 15);
	save_record_unpack(&dst, packed);
	qa_check(dst.levelName[sizeof(dst.levelName) - 1] == '\0'
	         && dst.name[sizeof(dst.name) - 1] == '\0',
	         "network save record terminates unterminated peer strings");
}

// The seat a resume hands back. Restored afterwards: these ride the player's own configuration.
static void qa_test_save_slot_seats(void)
{
	uint saved[SAVE_FILES_NUM];
	for (uint i = 0; i < SAVE_FILES_NUM; ++i)
		saved[i] = save_slot_online_player((JE_byte)(i + 1));

	save_slot_set_online_player(22, 2);
	save_slot_set_online_player(12, 1);
	qa_check(save_slot_online_player(22) == 2 && save_slot_online_player(12) == 1,
	         "an online save slot remembers which player number wrote it");

	save_slot_set_online_player(22, 0);
	qa_check(save_slot_online_player(22) == 1,
	         "a local save over an online slot forgets its player number");

	save_slot_set_online_player(11, 2);
	save_slot_set_online_player(0, 2);
	save_slot_set_online_player(SAVE_FILES_NUM + 1, 2);
	qa_check(save_slot_online_player(11) == 1 && save_slot_online_player(0) == 1
	         && save_slot_online_player(SAVE_FILES_NUM + 1) == 1,
	         "seats are kept only for the two-player page an online session writes");

	for (uint i = 0; i < SAVE_FILES_NUM; ++i)
		save_slot_set_online_player((JE_byte)(i + 1), saved[i]);
}

static void qa_test_cash_ledger(void)
{
	const bool savedMode = endlessMode;
	const Uint32 savedCash = player[0].cash;
	const Uint64 savedEarned = endlessRunCashEarned, savedSpent = endlessRunCashSpent;
	Uint64 savedSources[ENDLESS_CASH_SOURCES], savedSinks[ENDLESS_CASH_SINKS];
	memcpy(savedSources, endlessCashBySource, sizeof(savedSources));
	memcpy(savedSinks, endlessCashBySink, sizeof(savedSinks));

	endlessMode = true;
	player[0].cash = 0;
	endlessRunCashEarned = endlessRunCashSpent = 0;
	memset(endlessCashBySource, 0, sizeof(endlessCashBySource));
	memset(endlessCashBySink, 0, sizeof(endlessCashBySink));
	endlessCashResync();

	endlessCashCredit(1000, ENDLESS_CASH_START);
	endlessCashDebit(250, ENDLESS_SINK_SUPPLIES);
	qa_check(player[0].cash == 750 && endlessRunCashEarned == 1000
	         && endlessRunCashSpent == 250
	         && endlessCashBySource[ENDLESS_CASH_START] == 1000
	         && endlessCashBySink[ENDLESS_SINK_SUPPLIES] == 250,
	         "cash ledger books ordinary credits and debits by category");

	endlessShopTradeBegin();
	player[0].cash = 600;
	endlessShopTradeCommit();
	qa_check(endlessRunCashSpent == 400 && endlessCashBySink[ENDLESS_SINK_GEAR] == 150,
	         "upgrade trade commits book temporary-wallet spending once");
	endlessShopTradeBegin();
	player[0].cash = 650;
	endlessShopTradeCommit();
	qa_check(endlessRunCashSpent == 350 && endlessCashBySink[ENDLESS_SINK_GEAR] == 100,
	         "full-refund trades cancel prior gear spending instead of creating income");
	endlessShopTradeBegin();
	player[0].cash = 800;
	endlessShopTradeCommit();
	qa_check(endlessRunCashSpent == 250 && endlessRunCashEarned == 1050
	         && endlessCashBySink[ENDLESS_SINK_GEAR] == 0
	         && endlessCashBySource[ENDLESS_CASH_TRADEIN] == 50,
	         "trade refunds beyond booked gear become trade-in income only for the excess");
	endlessShopTradeCommit();
	qa_check(endlessRunCashSpent == 250 && endlessRunCashEarned == 1050,
	         "duplicate trade commit is idempotent");
	endlessCashDebit(2000, ENDLESS_SINK_SUPPLIES);
	qa_check(player[0].cash == 0 && endlessRunCashSpent == 1050
	         && endlessCashBySink[ENDLESS_SINK_SUPPLIES] == 1050
	         && endlessRunCashEarned - endlessRunCashSpent == player[0].cash,
	         "oversized cash debit stops at the wallet and preserves ledger conservation");

	endlessMode = savedMode;
	player[0].cash = savedCash;
	endlessRunCashEarned = savedEarned;
	endlessRunCashSpent = savedSpent;
	memcpy(endlessCashBySource, savedSources, sizeof(savedSources));
	memcpy(endlessCashBySink, savedSinks, sizeof(savedSinks));
	endlessCashResync();
}

/* Elite and champion bounties across the whole session surface: both machines, both credit
 * modes, the Bounty perk, and Double Earnings, which covers bounties the way it covers every
 * other combat payment. The wallet outcomes have to be identical whichever machine simulates
 * the kill. */
static void qa_test_bounty_matrix(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedTwo = twoPlayerMode, savedCampaign = coopCampaignMode;
	const bool savedCoop = coopEndlessMode, savedEndless = endlessMode;
	const uint savedThis = thisPlayerNum;
	const Uint64 savedMods = endlessActiveMods;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	isNetworkGame = true;
	twoPlayerMode = true;
	coopCampaignMode = false;
	coopEndlessMode = true;
	endlessMode = true;
	endlessActiveMods = 0;

	char label[160];
	int link = 50;
	for (uint machine = 1; machine <= 2; ++machine)
	for (int shared = 0; shared <= 1; ++shared)
	for (int doubled = 0; doubled <= 1; ++doubled)
	for (int perk = 0; perk <= 1; ++perk)
	for (int champ = 0; champ <= 1; ++champ)
	{
		thisPlayerNum = machine;
		coop_set_session_shared_credit(shared != 0);
		coop_set_session_double_earnings(doubled != 0);
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		if (perk)
			endlessPerkGrant(1, PERK_BOUNTY, 1);   // personal: the KILLER's own perk sizes it
		endlessPerkRederive();

		player[0].cash = player[1].cash = 0;
		endlessCashResync();
		endlessAwardEliteKill(++link, champ ? 3 : 2, 1);   // player 2's kill

		endlessSetFxPlayer(1);   // read the figure the killer's row produced
		const long base = champ ? endlessChampionBounty() : endlessEliteBounty();
		endlessSetFxPlayer(0);
		const long want = (doubled && !shared) ? base * 2 : base;  // Double Earnings covers bounties
		const bool okay = shared
		                ? (player[0].cash == (ulong)want && player[1].cash == (ulong)want)
		                : (player[1].cash == (ulong)want && player[0].cash == 0);
		snprintf(label, sizeof(label),
		         "%s bounty (machine %u, %s credit, double %d, perk %d) pays the right wallets",
		         champ ? "champion" : "elite", machine,
		         shared ? "Shared" : "Individual", doubled, perk);
		qa_check(okay && want > 0, label);
	}

	player[0].cash = player[1].cash = 0;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessCashResync();
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	thisPlayerNum = savedThis;
	endlessActiveMods = savedMods;
}

/* Bounty Hunter's other half: the score-pickup multiplier. The cash belongs to whichever ship
 * flew over the pickup, so that ship's own row sizes it, and both machines collect for both
 * ships and must end on the same two wallets. */
static void qa_test_score_pickup_multiplier(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedTwo = twoPlayerMode, savedCampaign = coopCampaignMode;
	const bool savedCoop = coopEndlessMode, savedEndless = endlessMode;
	const JE_boolean savedMods = endlessCampaignMods;
	const uint savedThis = thisPlayerNum;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	isNetworkGame = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	endlessMode = false;
	endlessCampaignMods = false;   // the perk only reaches a campaign through the effect layer
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkGrant(0, PERK_BOUNTY, 1);

	const long face = 250, boosted = face * ENDLESS_PERK_BOUNTY_PICKUP_MULT;
	qa_check(endlessScorePickupValue(0, face) == face,
	         "a campaign score pickup pays its authored value whatever perks are stored");

	endlessMode = true;
	qa_check(endlessScorePickupValue(0, face) == boosted
	         && endlessScorePickupValue(1, face) == face,
	         "Bounty Hunter multiplies its holder's score pickups and nobody else's");
	qa_check(endlessScorePickupValue(0, 0) == 0 && endlessScorePickupValue(0, -1) == -1
	         && endlessScorePickupValue((uint)COUNTOF(player), face) == face,
	         "the score-pickup multiplier leaves non-cash values and out-of-range ships alone");

	isNetworkGame = true;
	twoPlayerMode = true;
	coopEndlessMode = true;

	char label[160];
	for (uint machine = 1; machine <= 2; ++machine)
	for (int shared = 0; shared <= 1; ++shared)
	for (int doubled = 0; doubled <= 1; ++doubled)
	{
		thisPlayerNum = machine;
		coop_set_session_shared_credit(shared != 0);
		coop_set_session_double_earnings(doubled != 0);
		player[0].cash = player[1].cash = 0;
		endlessCashResync();

		// Player 1 holds the perk; each ship then collects one pickup of the same authored value.
		for (uint p = 0; p < COUNTOF(player); ++p)
			player_award_pickup_cash(&player[p], endlessScorePickupValue(p, face));

		const long scale = (doubled && !shared) ? 2 : 1;  // Double Earnings covers pickups
		const bool okay = shared
		                ? (player[0].cash == (ulong)(boosted + face)
		                   && player[1].cash == (ulong)(boosted + face))
		                : (player[0].cash == (ulong)(boosted * scale)
		                   && player[1].cash == (ulong)(face * scale));
		snprintf(label, sizeof(label),
		         "score pickups (machine %u, %s credit, double %d) pay the right wallets",
		         machine, shared ? "Shared" : "Individual", doubled);
		qa_check(okay, label);
	}

	player[0].cash = player[1].cash = 0;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessCashResync();
	coop_set_session_shared_credit(true);
	coop_set_session_double_earnings(false);
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	endlessCampaignMods = savedMods;
	thisPlayerNum = savedThis;
}

/* The level-clear payout, which each machine derives for BOTH ships: interest on each ship's
 * own bank plus each ship's clear bonus, identical whichever machine runs it. Paying only the
 * local wallet left each machine's view of the partner short and skipped Shared entirely. */
static void qa_test_zone_payout(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedTwo = twoPlayerMode, savedCampaign = coopCampaignMode;
	const bool savedCoop = coopEndlessMode, savedEndless = endlessMode;
	const uint savedThis = thisPlayerNum;
	const int savedDepth = endlessRunDepth;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	isNetworkGame = true;
	twoPlayerMode = true;
	coopCampaignMode = false;
	coopEndlessMode = true;
	endlessMode = true;
	endlessRunDepth = 5;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();

	ulong out[2][2];
	for (uint machine = 1; machine <= 2; ++machine)
	{
		thisPlayerNum = machine;
		coop_set_session_shared_credit(false);
		coop_set_session_double_earnings(false);
		player[0].cash = 10000;
		player[1].cash = 40000;
		endlessCashResync();
		long interest = 0, bonus = 0;
		endlessApplyLevelPayout(&interest, &bonus);
		out[machine - 1][0] = player[0].cash;
		out[machine - 1][1] = player[1].cash;
		qa_check(interest > 0 && bonus > 0,
		         "the zone tally reports the local ship's own interest and bonus");
	}
	qa_check(out[0][0] == out[1][0] && out[0][1] == out[1][1],
	         "both machines derive the same wallets from the zone payout");
	qa_check(out[0][1] - 40000 > out[0][0] - 10000,
	         "zone interest follows each ship's own bank");

	thisPlayerNum = 1;
	coop_set_session_shared_credit(true);
	player[0].cash = player[1].cash = 20000;
	endlessCashResync();
	endlessApplyLevelPayout(NULL, NULL);
	qa_check(player[0].cash == player[1].cash && player[0].cash > 20000,
	         "Shared credit's equal wallets stay equal through the zone payout");

	isNetworkGame = false;
	twoPlayerMode = false;
	coopEndlessMode = false;
	player[0].cash = 10000;
	player[1].cash = 777;
	endlessCashResync();
	endlessApplyLevelPayout(NULL, NULL);
	qa_check(player[1].cash == 777 && player[0].cash > 10000,
	         "a solo zone payout touches one wallet");

	player[0].cash = player[1].cash = 0;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessCashResync();
	coop_set_session_shared_credit(true);
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	thisPlayerNum = savedThis;
	endlessRunDepth = savedDepth;
}

static void qa_test_arcade_scaling(void)
{
	const bool savedBoost = arcadeLifeBoost, savedRear = arcadeRearGunScale;
	const JE_boolean savedOne = onePlayerAction, savedTwo = twoPlayerMode, savedSuper = superTyrian;
	const bool savedHudDirty = hud_bars_dirty;
	Player p = player[0];
	Uint8 lives = 1;
	bool gaugeSafe = AMMO_GAUGE_STEP(0) == 1 && AMMO_GAUGE_STEP(26) == 3;
	for (int ammo = 1; ammo <= 65535; ++ammo)
	{
		const uint step = AMMO_GAUGE_STEP(ammo);
		gaugeSafe &= step >= 1 && ((uint)ammo + step - 1) / step <= 10;
	}
	qa_check(gaugeSafe,
	         "sidekick ammo gauge rounding never draws more than ten segments");
	p.lives = &lives;
	p.hull_armor = 10;

	arcadeLifeBoost = true;
	onePlayerAction = true;
	twoPlayerMode = false;
	superTyrian = false;
	qa_check(arcade_life_scaling_active(), "arcade life scaling activates only in an eligible mode");
	lives = 0;
	qa_check(arcade_armor_max(&p) == 10, "zero arcade lives clamps to the one-life hull");
	uint previous = arcade_armor_max(&p);
	for (lives = 1; lives <= ARCADE_LIVES_MAX; ++lives)
	{
		const uint scaled = arcade_armor_max(&p);
		qa_check(scaled >= previous && scaled >= 10 && scaled <= ARCADE_FULL_BAR,
		         "arcade hull scaling is monotonic and bounded");
		previous = scaled;
	}
	qa_check(previous == ARCADE_FULL_BAR, "maximum arcade lives reach one full armor bar");
	lives = ARCADE_LIVES_MAX + 1;
	qa_check(arcade_armor_max(&p) == ARCADE_FULL_BAR,
	         "arcade hull scaling clamps lives above the gameplay cap");
	p.hull_armor = 0;
	qa_check(arcade_armor_max(&p) == 0, "an absent arcade hull remains absent");
	p.hull_armor = ARCADE_FULL_BAR;
	qa_check(arcade_armor_max(&p) == ARCADE_FULL_BAR, "a full stock hull is never inflated");

	p.hull_armor = 10; p.initial_armor = 10; p.armor = 5;
	p.shield_max = p.shield = 0; lives = ARCADE_LIVES_MAX;
	arcade_rescale_to_lives(&p);
	qa_check(p.initial_armor == 28 && p.armor == 14,
	         "arcade rescaling preserves the live hull damage ratio while growing");
	lives = 1;
	arcade_rescale_to_lives(&p);
	qa_check(p.initial_armor == 10 && p.armor == 5,
	         "arcade rescaling preserves the live hull damage ratio while shrinking");

	arcadeRearGunScale = true;
	p.items.weapon[REAR_WEAPON].power = 3; lives = 5; p.lives = &lives;
	qa_check(arcade_weapon_power(&p, REAR_WEAPON) == 7,
	         "one-player arcade rear gun combines pickups with the life counter");
	p.lives = &p.items.weapon[REAR_WEAPON].power;
	qa_check(arcade_weapon_power(&p, REAR_WEAPON) == 3,
	         "arcade rear scaling avoids the two-player life-counter alias");
	p.items.weapon[REAR_WEAPON].power = 10; lives = 11; p.lives = &lives;
	qa_check(arcade_weapon_power(&p, REAR_WEAPON) == 11,
	         "arcade rear scaling respects the engine power ceiling");

	arcadeLifeBoost = false;
	qa_check(!arcade_life_scaling_active() && arcade_armor_max(&p) == p.hull_armor,
	         "disabling arcade life scaling restores the stock hull ceiling");

	arcadeLifeBoost = savedBoost; arcadeRearGunScale = savedRear;
	onePlayerAction = savedOne; twoPlayerMode = savedTwo; superTyrian = savedSuper;
	hud_bars_dirty = savedHudDirty;
}

static void qa_test_effect_gates(void)
{
	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;
	endlessMode = false;
	endlessCampaignMods = false;
	endlessActiveMods = ~(Uint64)0;
	endlessStaticLockoutReset();
	qa_check(endlessMoveScale() == 1.0f && !endlessShieldRegenOff()
	         && !endlessShieldRegenFree() && endlessGeneratorPowerAdd(7) == 7
	         && endlessHitboxScale(13) == 13,
	         "Endless movement, shield, generator, and hitbox effects cannot leak into normal play");
	qa_check(!endlessAegisGateConsume(10, 10) && !endlessShockwaveActive()
	         && endlessMartyrdomBurstShots(1, 3) == 0 && !endlessSeekerActive()
	         && endlessStaticDischargeDrain(10) == 0,
	         "Endless reactive combat effects cannot leak into normal play");
	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
	endlessActiveMods = savedMods;
}

/* Where a projectile is collided from. The geometry comes from sprite data alone, so a synthetic
 * frame pins it exactly; the shot loops in tyrian2.c add the target's own middle to the answer. */
static void qa_test_shot_hitboxes(void)
{
	/* One frame of a 12-wide cell: an empty row, then a four-pixel run at columns 2 to 5 on each
	 * of the next two rows. A control byte carries the opaque run in its high nibble and the
	 * transparent skip in its low one, a zero run advances to the next row, and 0x0f ends it. */
	static const Uint8 body[] = {
		0x0c,                                // row 0: 12 transparent columns, then the next row
		0x42, 0x11, 0x11, 0x11, 0x11, 0x06,  // row 1: skip 2, four pixels, skip the rest
		0x42, 0x11, 0x11, 0x11, 0x11, 0x06,  // row 2: the same
		0x0f,
	};
	union {
		Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
		Uint8 bytes[2 + sizeof(body)];
	} frame;
	frame.bytes[0] = 2;  // one-entry offset table, little-endian, pointing just past itself
	frame.bytes[1] = 0;
	memcpy(frame.bytes + 2, body, sizeof(body));

	const Sprite2_array sheet = { sizeof(frame.bytes), frame.bytes };

	int dx = -1, dy = -1;
	sprite2_center_offset(sheet, 1, &dx, &dy);
	qa_check(dx == 3 && dy == 1, "a frame's middle is the middle of the box its opaque pixels fill");
	sprite2_center_offset(sheet, 2, &dx, &dy);
	qa_check(dx == 0 && dy == 0, "a frame the sheet does not have leaves the point on the blit position");

	const bool savedCentered = centeredShotHitboxes;
	const Sprite2_array savedSheet = spriteSheet8;
	spriteSheet8 = sheet;

	centeredShotHitboxes = false;
	player_shot_hit_offset(1, &dx, &dy);
	qa_check(dx == 0 && dy == 0, "Classic collides a shot from the corner of its sprite cell");
	enemy_shot_hit_offset(1, 0, &dx, &dy);
	qa_check(dx == 0 && dy == 0, "...an enemy shot included");

	centeredShotHitboxes = true;
	player_shot_hit_offset(1, &dx, &dy);
	qa_check(dx == 4 && dy == 1, "Centered collides it from the frame's middle, plus the blit's own pixel");
	enemy_shot_hit_offset(1, 0, &dx, &dy);
	qa_check(dx == 3 && dy == 1, "...and an enemy shot from the frame's middle, which is blitted where it sits");

	spriteSheet8 = savedSheet;
	centeredShotHitboxes = savedCentered;
}

/* Which enemies a tier roll may land on. Every body settles on its first frame, so the roll reads
 * the level script to tell a boss flying in armored from scenery that can never be hurt. */
static void qa_test_elite_tier_eligibility(void)
{
	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;
	const int savedDepth = endlessRunDepth;
	const JE_word savedMaxEvent = maxEvent;
	struct JE_EventRecType savedEvents[4];
	memcpy(savedEvents, eventRec, sizeof(savedEvents));

	endlessMode = true;
	endlessCampaignMods = false;
	endlessRunDepth = 20;
	endlessActiveMods = ENDLESS_MOD_APEX;  // every roll returns a tier

	// A level that opens link 64 with a damage event, seals link 12 with one, and renumbers
	// link 9 into 64. Link 3 is named nowhere, so nothing can ever hurt it.
	memset(eventRec, 0, sizeof(savedEvents));
	eventRec[0].eventtype = 25;
	eventRec[0].eventdat = 200;
	eventRec[0].eventdat4 = 64;
	eventRec[1].eventtype = 25;
	eventRec[1].eventdat = 255;
	eventRec[1].eventdat4 = 12;
	eventRec[2].eventtype = 39;
	eventRec[2].eventdat = 9;
	eventRec[2].eventdat2 = 64;
	maxEvent = 3;

	endlessResetElites();
	qa_check(endlessEliteTierNow(0, 40, false) >= 2, "a damageable enemy takes a tier");
	qa_check(endlessEliteTierNow(0, 255, true) == 1, "a score pickup is never a tier");
	qa_check(endlessEliteTierNow(3, 255, false) == 1, "scenery no armor event can reach is never promoted");
	qa_check(endlessEliteTierNow(12, 255, false) == 1, "...nor is a body an armor event only seals");

	endlessResetElites();
	const int boss = endlessEliteTierNow(64, 255, false);
	qa_check(boss >= 2, "a boss flying in armored takes its tier while it is still invulnerable");
	qa_check(endlessEliteTierNow(64, 254, false) == boss,
	         "...and keeps it when the level's damage event opens it up");
	qa_check(endlessEliteTierNow(9, 255, false) >= 2,
	         "a group renumbered into an opened one rolls too, under its own number");

	endlessResetElites();
	qa_check(endlessEliteTierNow(64, 254, false) == endlessEliteTierNow(64, 255, false),
	         "an invulnerable part wears the tier its link group already holds");

	/* A part the roll never reached still paints in its group's bank, so a hull that mixes a
	 * damageable core with sealed plating tints as one body. */
	const Uint8 groupTint = endlessEliteTint(endlessEliteTierNow(64, 254, false));
	qa_check(groupTint != 0 && endlessEliteShellTint(64, 255) == groupTint,
	         "sealed plating borrows the colour its link group holds");
	qa_check(endlessEliteShellTint(64, 100) == 0,
	         "...while a damageable part is left to paint from its own tier");
	qa_check(endlessEliteShellTint(0, 255) == 0 && endlessEliteShellTint(3, 255) == 0,
	         "an unlinked part, and one whose group never rolls, stay untinted");

	// A damage event with no link number reaches every body on the field.
	memset(eventRec, 0, sizeof(savedEvents));
	eventRec[0].eventtype = 47;
	eventRec[0].eventdat = 30;
	maxEvent = 1;
	endlessResetElites();
	qa_check(endlessEliteTierNow(0, 255, false) >= 2, "a level-wide damage event opens even unlinked bodies");

	endlessActiveMods = ENDLESS_MOD_NOELITE;
	endlessResetElites();
	qa_check(endlessEliteTierNow(7, 40, false) == 1 && endlessEliteTierNow(7, 255, false) == 1,
	         "No Elites decides normal, and the group's parts still agree with it");

	memcpy(eventRec, savedEvents, sizeof(savedEvents));
	maxEvent = savedMaxEvent;
	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
	endlessActiveMods = savedMods;
	endlessRunDepth = savedDepth;
	endlessResetElites();
}

/* The tier tint an elite wears, and its route from a kill site into every explosion the death
 * spawns. Colour is presentation, so nothing here may reach the state hash. */
static void qa_test_elite_explosion_tint(void)
{
	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;

	endlessMode = false;
	endlessCampaignMods = false;
	qa_check(endlessEliteTint(2) == 0 && endlessEliteTint(3) == 0
	         && endlessEliteShellTint(64, 255) == 0,
	         "elite tints cannot leak into normal play, the one sealed plating borrows included");

	endlessMode = true;
	qa_check(endlessEliteTint(0) == 0 && endlessEliteTint(1) == 0
	         && endlessEliteTint(2) == ENDLESS_ELITE_FILTER
	         && endlessEliteTint(3) == ENDLESS_CHAMPION_FILTER,
	         "only elites and champions carry a tint, each its own bank");

	const Uint32 hashBefore = rollback_state_hash();
	memset(explosions, 0, sizeof(explosions));
	memset(rep_explosions, 0, sizeof(rep_explosions));

	explosionFilter = endlessEliteTint(3);
	JE_setupExplosion(100, 100, 0, 1, false, false);
	explosionFilter = 0;
	JE_setupExplosion(100, 100, 0, 1, false, false);
	qa_check(explosions[0].filter == ENDLESS_CHAMPION_FILTER && explosions[1].filter == 0,
	         "an explosion keeps the tint set when it was spawned");

	/* explonum 12 is over ten, so the death both puffs and queues a big repeating sequence. */
	memset(explosions, 0, sizeof(explosions));
	explosionFilter = endlessEliteTint(2);
	JE_setupExplosionLarge(false, 12, 100, 100);
	explosionFilter = 0;
	int tinted = 0;
	for (unsigned int i = 0; i < COUNTOF(explosions); ++i)
		if (explosions[i].ttl != 0 && explosions[i].filter == ENDLESS_ELITE_FILTER)
			++tinted;
	qa_check(tinted == 4, "all four puffs of one large explosion carry the tint");
	qa_check(rep_explosions[0].ttl != 0 && rep_explosions[0].filter == ENDLESS_ELITE_FILTER,
	         "...and the repeating sequence it arms carries it into its own explosions");

	memset(explosions, 0, sizeof(explosions));
	memset(rep_explosions, 0, sizeof(rep_explosions));
	qa_check(rollback_state_hash() == hashBefore,
	         "explosion colour leaves the registered state byte stream untouched");

	/* Both colour bytes sit in padding the structs already had. Widening either pool moves the
	 * registry layout and every replay fixture hash with it. */
	qa_check(sizeof(Explosion) == 14 && sizeof(rep_explosion_type) == 20,
	         "the explosion pools keep the widths the replay fixtures were recorded at");

	/* The packed bank-and-lift argument, through the blitter replay draws with. One opaque pixel:
	 * a control byte carrying an opaque run of one in its high nibble and no skip in its low one,
	 * the pixel, then the end marker. */
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8)
	{
		union {
			Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
			Uint8 bytes[10];
		} frames = { .bytes = { 4, 0, 7, 0,      // two frames, one pixel each, both bank 7
		                        0x10, 0x78, 0x0f,   // shade 8
		                        0x10, 0x7f, 0x0f } };  // shade 15
		const Sprite2_array sheet = { sizeof(frames.bytes), frames.bytes };

		Uint8 *const row = (Uint8 *)VGAScreen->pixels;
		const Uint8 savedPixels[2] = { row[0], row[1] };
		const int blended = (8 + 2) / 2 + ENDLESS_EXPLOSION_BRIGHT;
		const int lifted = blended > 15 ? 15 : blended;

		row[0] = 0x02;  // shade 2, so the average is five before the lift
		blit_sprite2_blend_filter_clip(VGAScreen, 0, 0, sheet, 1,
		                              ENDLESS_ELITE_FILTER | ENDLESS_EXPLOSION_BRIGHT);
		row[1] = 0x0f;  // full shade over full shade: the lift can only overshoot the bank
		blit_sprite2_blend_filter_clip(VGAScreen, 1, 0, sheet, 2,
		                              ENDLESS_CHAMPION_FILTER | ENDLESS_EXPLOSION_BRIGHT);

		qa_check(row[0] == (ENDLESS_ELITE_FILTER | lifted)
		         && row[1] == (ENDLESS_CHAMPION_FILTER | 15),
		         "a tinted explosion pixel blends into its own bank, lifted and clamped there");

		row[0] = savedPixels[0];
		row[1] = savedPixels[1];
	}

	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
}

/* An elite's bullets wear the tier bank its body does, stamped into the shot at spawn. The byte
 * carrying it is presentation, so it may not reach either hash a peer compares. */
static void qa_test_elite_shot_tint(void)
{
	/* Like both explosion pools, the colour byte sits in padding the struct already had. Widening
	 * the pool moves the registry layout and every replay fixture hash with it. */
	qa_check(sizeof(EnemyShotType) == 32,
	         "the enemy-shot pool keeps the width the replay fixtures were recorded at");

	/* The pool hash walks live slots only, so freeing the rest leaves one bullet to compare. */
	JE_boolean savedAvail[ENEMY_SHOT_MAX];
	const EnemyShotType savedShot = enemyShot[0];
	memcpy(savedAvail, enemyShotAvail, sizeof(savedAvail));
	for (unsigned int i = 0; i < COUNTOF(enemyShotAvail); ++i)
		enemyShotAvail[i] = 1;

	enemyShotAvail[0] = 0;
	enemyShot[0].sx = 90;
	enemyShot[0].sy = 120;
	enemyShot[0].duration = 20;
	enemyShot[0].filter = 0;
	const Uint32 plain = network_sim_pools(NULL);
	enemyShot[0].filter = ENDLESS_CHAMPION_FILTER;
	qa_check(network_sim_pools(NULL) == plain,
	         "bullet colour leaves the pool hash a peer compares untouched");

	memcpy(enemyShotAvail, savedAvail, sizeof(savedAvail));
	enemyShot[0] = savedShot;

	/* The lift the bullets are drawn with, through the blitter replay draws them with. Same
	 * one-opaque-pixel frames the explosion tint check above builds. */
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8)
	{
		union {
			Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
			Uint8 bytes[10];
		} frames = { .bytes = { 4, 0, 7, 0,         // two frames, one pixel each, both bank 7
		                        0x10, 0x74, 0x0f,      // shade 4
		                        0x10, 0x7e, 0x0f } };  // shade 14
		const Sprite2_array sheet = { sizeof(frames.bytes), frames.bytes };

		Uint8 *const row = (Uint8 *)VGAScreen->pixels;
		const Uint8 savedPixels[2] = { row[0], row[1] };
		const int lifted = (4 + ENDLESS_SHOT_BRIGHT > 15) ? 15 : 4 + ENDLESS_SHOT_BRIGHT;

		blit_sprite2_filter_bright_clip(VGAScreen, 0, 0, sheet, 1,
		                                ENDLESS_ELITE_FILTER | ENDLESS_SHOT_BRIGHT);
		blit_sprite2_filter_bright_clip(VGAScreen, 1, 0, sheet, 2,
		                                ENDLESS_CHAMPION_FILTER | ENDLESS_SHOT_BRIGHT);

		qa_check(row[0] == (ENDLESS_ELITE_FILTER | lifted)
		         && row[1] == (ENDLESS_CHAMPION_FILTER | 15),
		         "a tinted bullet pixel keeps its own shade, lifted and clamped in its bank");

		row[0] = savedPixels[0];
		row[1] = savedPixels[1];
	}
}

/* The band JE_repaintTextWindow paints: the bar's left edge out to the bounty column. */
enum { QA_MSGBAR_X = 16, QA_MSGBAR_W = 244 - 16, QA_MSGBAR_H = 11 };

static void qa_copy_message_bar(Uint8 *out)
{
	const int y0 = vga_height - QA_MSGBAR_H;
	for (int y = 0; y < QA_MSGBAR_H; ++y)
		memcpy(&out[y * QA_MSGBAR_W],
		       (Uint8 *)VGAScreenSeg->pixels + (y0 + y) * VGAScreenSeg->pitch + QA_MSGBAR_X,
		       QA_MSGBAR_W);
}

static bool qa_message_bar_differs(const Uint8 *was)
{
	Uint8 now[QA_MSGBAR_H * QA_MSGBAR_W];
	qa_copy_message_bar(now);
	return memcmp(now, was, sizeof(now)) != 0;
}

/* The bounty line names the tier in the tier's own bank. The name is drawn as a string of its own,
 * so its width has to advance the cursor exactly as one string would: the words after it must land
 * on the columns they had, and nothing but the tier name may change bank. */
static void qa_test_elite_message_tint(void)
{
	if (VGAScreenSeg == NULL || VGAScreenSeg->format->BitsPerPixel != 8)
		return;  // the draws below paint the real message bar

	const bool savedSilent = rollback_resim_silent, savedDirty = hud_message_dirty;
	const JE_word savedErase = textErase;
	rollback_resim_silent = false;  // a silent re-simulation pass blits nothing

	Uint8 plain[QA_MSGBAR_H * QA_MSGBAR_W], tinted[QA_MSGBAR_H * QA_MSGBAR_W];

	JE_drawTextWindow("Elite Enemy destroyed!");
	qa_copy_message_bar(plain);

	JE_drawTextWindowSplit("Elite Enemy", ENDLESS_ELITE_FILTER >> 4, " destroyed!", "", 244);
	qa_copy_message_bar(tinted);

	int moved = 0, recoloured = 0;
	for (int i = 0; i < QA_MSGBAR_H * QA_MSGBAR_W; ++i)
	{
		if (tinted[i] == plain[i])
			continue;
		if ((plain[i] & 0xf0) == 0 && tinted[i] == (ENDLESS_ELITE_FILTER | plain[i]))
			++recoloured;
		else
			++moved;
	}

	qa_check(moved == 0, "colouring the tier name leaves the rest of the bounty line where it was");
	qa_check(recoloured > 0, "the tier name is painted in the elite bank at the shades it had");

	/* Online a kill can land in a rollback pass that draws nothing, so the line is held and
	 * repainted on the next pass that reaches the screen. It has to come back with its tint. */
	JE_drawTextWindow("");  // blank the bar first, so anything the silent post paints shows up
	rollback_resim_silent = true;
	JE_drawTextWindowSplit("Elite Enemy", ENDLESS_ELITE_FILTER >> 4, " destroyed!", "", 244);
	rollback_resim_silent = false;

	qa_check(hud_message_dirty && qa_message_bar_differs(tinted),
	         "a bounty line posted in a silent pass paints nothing and is held for a visible one");

	JE_repaintTextWindow();
	qa_check(!qa_message_bar_differs(tinted),
	         "the held bounty line comes back with its tier colour on the next visible pass");

	textErase = savedErase;
	hud_message_dirty = savedDirty;
	rollback_resim_silent = savedSilent;
}

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
static void qa_test_superspark_caps(void)
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
			/* Above 60000 is an option-shape shot, which takes the blended draw instead of the
			 * trail branch. The trail's colour is the thousands digit shifted into a palette
			 * bank, so only 1001..15999 is a graphic the data can have meant. */
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
	Uint32 hash = 2166136261u;
	const Uint8 *const bytes = (const Uint8 *)superpixels;
	for (size_t i = 0; i < sizeof(superpixels); ++i)
		hash = (hash ^ bytes[i]) * 16777619u;
	return hash;
}

/* One shower, spawned and drawn the way a level frame does it. The seeded spawn stands in for the
   simulation RNG a rollback restores, so a re-run of the same frame emits the same sparks. */
static void qa_spark_frame(Uint32 seed)
{
	JE_beginSPPass();
	JE_doSPSeeded(100, 100, 5, 3, 7 << 4, false, 0, false, seed);
	JE_drawSP();
}

/* The ring is presentation state and is not restored with the simulation, so a rollback that
   discards a pass which has already drawn has to leave it where a clean run of the same frames
   would: the replacement pass reuses the discarded slots and repeats its step rather than adding
   one. See doc/notes.md, "Superspark ring buffer". */
static void qa_test_superspark_discarded_pass(void)
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

/* JE_doSP is reached from simulation code, so its draws belong to the deterministic stream even
   though the sparks themselves do not. Dropping the slot write on a silent re-simulation pass must
   drop none of them, or the peers' generators separate and the run desyncs. */
static void qa_test_superspark_rng_cost(void)
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

/* The Opening Salvo cue spawns through JE_doSPBrief, which cuts a spark's life to `life` ticks and
   lifts its shade by `bright` so the shorter life does not spawn it dark. It is reached from the
   shot draw, so its RNG cost has to match JE_doSP's spark for spark. */
static void qa_test_superspark_brief(void)
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

/* A seeded source's successive showers sit a fixed stride apart, and each has to get a fresh
   direction out of it. The strides below are what the callers in tyrian2.c produce at their
   emission cadences. See doc/notes.md, "Superspark ring buffer". */
static void qa_test_superspark_seeded_spread(void)
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

/* The two shapes the Chain Reaction pulse draws with. Both space their sparks by distance, so the
 * geometry has to hold at every size the perk reaches: a ring has to land on the radius it was asked
 * for, and a bolt has to stay on the line between the two things it connects. */
static void qa_test_superspark_shapes(void)
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
static void qa_test_vaporised_shot_sparks(void)
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

/* Settings baked into the loaded item data do nothing on their own: something has to rewrite the
 * tables JE_loadItemDat filled, which is JE_applyItemDataSettings. Each setting below is flipped
 * between two values with that call in between, and the tables have to come out different. One
 * that stops reaching them fails here instead of silently doing nothing in the running game. */
static Uint32 qa_item_data_hash(void)
{
	const struct { const void *data; size_t size; } tables[] = {
		{ weapons,    sizeof(weapons) },
		{ options,    sizeof(options) },
		{ weaponPort, sizeof(weaponPort) },
		{ shields,    sizeof(shields) },
		{ ships,      sizeof(ships) },
		{ special,    sizeof(special) },
	};

	Uint32 hash = 2166136261u;
	for (size_t t = 0; t < COUNTOF(tables); ++t)
	{
		const Uint8 *const bytes = tables[t].data;
		for (size_t i = 0; i < tables[t].size; ++i)
			hash = (hash ^ bytes[i]) * 16777619u;
	}

	return hash;
}

static void qa_test_item_data_settings(void)
{
	static const struct
	{
		int *intSetting;    // exactly one of the two pointers is set
		bool *boolSetting;
		int a, b;
		const char *name;
	} baked[] = {
		{ .boolSetting = &chargeLaserCannon, .a = false, .b = true, .name = "Charge-Laser" },
		{ .boolSetting = &unusedShopSprites, .a = false, .b = true, .name = "Unused Sprites" },
		{ .intSetting = &superSparkMode[SSW_MEGA_PULSE], .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Mega Pulse trail" },
		{ .intSetting = &superSparkMode[SSW_WALLOP_BEAM], .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Wallop Beam trail" },
		{ .intSetting = &superSparkMode[SSW_PROTRON_B], .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Protron -B- trail" },
		{ .intSetting = &superSparkMode[SSW_ICE], .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Ice Beam trail" },
		{ .intSetting = &wallopSecondBolt, .a = SUPER_SPARKS_OFF, .b = SUPER_SPARKS_ON,
		  .name = "Wallop 2nd Bolt" },
		{ .intSetting = &zicaLaserBase, .a = ZICA_BASE_EP13, .b = ZICA_BASE_EP4,
		  .name = "Zica Lv11 pattern" },
		{ .intSetting = &epDiffMode[EDW_XEGA_BALL], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Xega Ball" },
		{ .intSetting = &epDiffMode[EDW_MICROSOL_OPT5], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "MicroSol Opt 5" },
		{ .intSetting = &epDiffMode[EDW_FLARE], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Flare Blast" },
		{ .intSetting = &epDiffMode[EDW_NEEDLE_LASER], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Needle Laser sound" },
		{ .intSetting = &epDiffMode[EDW_BUBBLE_GUM], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Bubble Gum-Gun sound" },
		{ .intSetting = &epDiffMode[EDW_FLYING_PUNCH], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Flying Punch sound" },
		{ .intSetting = &epDiffMode[EDW_PRETZEL_MISSILE], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Pretzel Missile sound" },
		{ .intSetting = &epDiffMode[EDW_DRAGON_FROST], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Dragon Frost sound" },
		{ .intSetting = &epDiffMode[EDW_SOLAR_SHIELD], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Solar Shield icon" },
		{ .intSetting = &epDiffMode[EDW_USHIP_PIC], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "U-Ship picture" },
		{ .intSetting = &epDiffMode[EDW_NORTSHIP_PIC], .a = EPDIFF_EP13, .b = EPDIFF_EP45,
		  .name = "Nort Ship picture" },
	};

	for (size_t i = 0; i < COUNTOF(baked); ++i)
	{
		const int saved = baked[i].intSetting != NULL ? *baked[i].intSetting
		                                              : (*baked[i].boolSetting ? 1 : 0);

		Uint32 hash[2];
		for (int side = 0; side < 2; ++side)
		{
			const int value = side == 0 ? baked[i].a : baked[i].b;
			if (baked[i].intSetting != NULL)
				*baked[i].intSetting = value;
			else
				*baked[i].boolSetting = (value != 0);

			JE_applyItemDataSettings();
			hash[side] = qa_item_data_hash();
		}

		if (hash[0] == hash[1])
			fprintf(stderr, "# %s does not reach the item data\n", baked[i].name);
		qa_check(hash[0] != hash[1], "an item-data setting changes the item data when applied");

		if (baked[i].intSetting != NULL)
			*baked[i].intSetting = saved;
		else
			*baked[i].boolSetting = (saved != 0);
	}

	/* Reapplying the settings that were there all along has to put the tables back exactly, or
	 * a toggled row would leave the item data subtly different from a fresh load. */
	const Uint32 restored = (JE_applyItemDataSettings(), qa_item_data_hash());
	JE_applyItemDataSettings();
	qa_check(qa_item_data_hash() == restored, "reapplying the same settings is idempotent");
}

/* A gun is a port: each of its eleven power levels is a separate weapon record with its own
 * sound, and both data sets set all eleven alike. An epdiff row therefore has to move every
 * level, and moving only the first leaves every upgraded shot on the other episode's sound while
 * still changing the item data enough to satisfy the hash check above. */
static void qa_test_firing_sound_levels(void)
{
	static const struct
	{
		int item;
		JE_byte port;    // a gun, whose every power level has to match...
		JE_word weapon;  // ...or a sidekick's single record
		const char *name;
	} sounds[] = {
		{ EDW_NEEDLE_LASER,    .port = 43, .name = "Needle Laser" },
		{ EDW_PRETZEL_MISSILE, .port = 44, .name = "Pretzel Missile" },
		{ EDW_DRAGON_FROST,    .port = 45, .name = "Dragon Frost" },
		{ EDW_BUBBLE_GUM,    .weapon = 792, .name = "Bubble Gum-Gun" },
		{ EDW_FLYING_PUNCH,  .weapon = 794, .name = "Flying Punch" },
	};

	int savedMode[EDW_COUNT];
	memcpy(savedMode, epDiffMode, sizeof(savedMode));

	const int modes[] = { EPDIFF_EP13, EPDIFF_EP45 };
	for (size_t i = 0; i < COUNTOF(sounds); ++i)
	{
		for (size_t m = 0; m < COUNTOF(modes); ++m)
		{
			epDiffMode[sounds[i].item] = modes[m];
			JE_applyItemDataSettings();

			const JE_byte want = JE_epDiffFiringSound(sounds[i].item, modes[m]);
			qa_check(want != 0, "a firing-sound row resolves to a sound");

			int levels = 0, wrong = 0;
			if (sounds[i].port != 0)
			{
				for (unsigned int mode = 0; mode < COUNTOF(weaponPort[0].op); ++mode)
					for (unsigned int lvl = 0; lvl < COUNTOF(weaponPort[0].op[0]); ++lvl)
					{
						const JE_word wpn = weaponPort[sounds[i].port].op[mode][lvl];
						if (wpn == 0 || wpn > WEAP_NUM)
							continue;
						++levels;
						if (weapons[wpn].sound != want)
							++wrong;
					}
			}
			else
			{
				levels = 1;
				wrong = weapons[sounds[i].weapon].sound != want;
			}

			if (wrong != 0)
				fprintf(stderr, "# %s: %d of %d power levels kept the other sound\n",
				        sounds[i].name, wrong, levels);
			qa_check(levels > 0, "a firing-sound row names weapon records that exist");
			qa_check(wrong == 0, "every power level of a firing-sound row takes the chosen sound");
		}
	}

	memcpy(epDiffMode, savedMode, sizeof(savedMode));
	JE_applyItemDataSettings();
}

/* Enhancement presets (config.c): one probe per menu screen, proving that screen's settings reach
 * the preset table, plus the Custom set's round trip. Runs last in the suite, because it moves
 * the real settings and cannot restore what it cannot list. */
static void qa_test_enhancement_presets(void)
{
	/* One setting per Enhancements screen and group. Each is poked away from the Engaged set the
	 * probe starts from, which leaves the table matching neither preset if the setting is listed. */
	static const struct
	{
		int *intSetting;    // exactly one of the two pointers is set, as in the preset table
		bool *boolSetting;
		int poke;
		const char *name;
	} probes[] = {
		{ .boolSetting = &extraParallax, .poke = true, .name = "Visuals" },
		{ .intSetting = &enemyBarOpacity, .poke = 40, .name = "Enemy Bars" },
		{ .intSetting = &bossBarLayout, .poke = BOSS_BAR_BOTTOM, .name = "Boss Bars" },
		{ .intSetting = &gaugeGradGenerator, .poke = GAUGE_GRAD_DOWN, .name = "Gauges" },
		{ .boolSetting = &gaugeFlashArmor, .poke = false, .name = "Gauge flash" },
		{ .boolSetting = &customWeaponEnabled, .poke = false, .name = "Weapons" },
		{ .boolSetting = &chargeLaserCannon, .poke = false, .name = "Charge-Laser" },
		{ .intSetting = &superSparkMode[SSW_ICE], .poke = SUPER_SPARKS_OFF, .name = "Spark Trails" },
		{ .intSetting = &wallopSecondBolt, .poke = SUPER_SPARKS_OFF, .name = "Wallop 2nd Bolt" },
		{ .boolSetting = &superSparkClassicCap[SSW_ICE], .poke = false, .name = "Classic Spark Caps" },
		{ .boolSetting = &centeredShotHitboxes, .poke = false, .name = "Gameplay" },
		{ .boolSetting = &arcadeLifeBoost, .poke = false, .name = "Arcade Modes" },
		{ .intSetting = &epDiffMode[EDW_FLARE], .poke = EPDIFF_EP13, .name = "Item Data" },
		{ .intSetting = &zicaLaserLength, .poke = ZICA_LEN_LONG, .name = "Zica Laser" },
		{ .intSetting = &epDiffMode[EDW_NEEDLE_LASER], .poke = EPDIFF_EP13, .name = "Firing Sounds" },
		{ .intSetting = &epDiffMode[EDW_SOLAR_SHIELD], .poke = EPDIFF_EP13, .name = "Solar Shield icon" },
		{ .intSetting = &epDiffMode[EDW_USHIP_PIC], .poke = EPDIFF_EP45, .name = "Shop Pictures" },
	};

	/* Both presets have to land on themselves, or the Preset row would read Custom the moment
	 * one was applied. */
	const EnhancementPreset presets[] = { ENH_PRESET_VANILLA, ENH_PRESET_ENGAGED };
	for (size_t p = 0; p < COUNTOF(presets); ++p)
	{
		enhancementApplyPreset(presets[p]);
		qa_check(enhancementPresetState() == presets[p], "an applied preset reads back as itself");
	}

	for (size_t i = 0; i < COUNTOF(probes); ++i)
	{
		enhancementApplyPreset(ENH_PRESET_ENGAGED);

		if (probes[i].intSetting != NULL)
			*probes[i].intSetting = probes[i].poke;
		else
			*probes[i].boolSetting = (probes[i].poke != 0);

		if (enhancementPresetState() != ENH_PRESET_CUSTOM)
			fprintf(stderr, "# %s does not reach the preset table\n", probes[i].name);
		qa_check(enhancementPresetState() == ENH_PRESET_CUSTOM,
		         "a changed setting turns the Preset row Custom");
	}

	/* Custom is a set the player keeps: it survives a trip through the other presets and comes
	 * back with the values that were changed by hand. */
	enhancementApplyPreset(ENH_PRESET_ENGAGED);
	*probes[0].boolSetting = (probes[0].poke != 0);
	const int handMade = *probes[1].intSetting = probes[1].poke;
	enhancementNoteCustom();
	qa_check(enhancementCustomAvailable(), "a hand-edited set is remembered as Custom");

	enhancementApplyPreset(ENH_PRESET_VANILLA);
	qa_check(enhancementPresetState() == ENH_PRESET_VANILLA, "a preset still overwrites that set");

	enhancementApplyPreset(ENH_PRESET_CUSTOM);
	qa_check(enhancementPresetState() == ENH_PRESET_CUSTOM
	         && *probes[0].boolSetting == (probes[0].poke != 0) && *probes[1].intSetting == handMade,
	         "...and Custom hands the whole set back");

	/* Editing from a preset replaces the remembered set rather than adding to it. */
	enhancementApplyPreset(ENH_PRESET_VANILLA);
	*probes[2].intSetting = probes[2].poke;
	enhancementNoteCustom();
	enhancementApplyPreset(ENH_PRESET_ENGAGED);
	enhancementApplyPreset(ENH_PRESET_CUSTOM);
	qa_check(*probes[2].intSetting == probes[2].poke && *probes[1].intSetting != handMade,
	         "the newest hand-edited set is the one Custom holds");

	enhancementApplyPreset(ENH_PRESET_ENGAGED);
	qa_check(enhancementPresetState() == ENH_PRESET_ENGAGED, "re-applying a preset clears Custom");
}

/* Online Endless: the block each machine publishes for its own player, the way two players'
 * purchases fold into one sector, and who charts the next course. */
static void qa_test_endless_coop(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const bool savedHost = network_is_host;
	const EndlessCourseChooser savedChooser = endlessCourseChooser;
	const bool savedHostCharts = endlessCoopHostCharts;
	const int savedDepth = endlessRunDepth;

	endlessMode = true;
	coopEndlessMode = true;

	/* The wire block round trips through the peer's slot without touching the sender's. */
	endlessArmorBonus[0] = 24;
	endlessPurchasedMods[0] = ENDLESS_MOD_OVERDRIVE;
	endlessBuffKind[0] = ENDLESS_BUFF_KIND_OVERDRIVE;
	endlessBuffCharge[0] = 7;
	endlessBuffCooldownUntil[0] = 12;
	endlessCleanseChargeCount[0] = 2;
	endlessLongCon[0] = 3;
	endlessShopTax[0] = 50;
	endlessRevivesUsed[0] = 4;
	endlessRerollCost[0] = 91000;
	endlessHullCost[0] = 45000;
	endlessShopEntryCash[0] = 1234567;
	endlessReviveHeld[0] = true;
	endlessGambleRigged[0] = true;
	endlessPlayerDowned[0] = true;
	player[0].superbombs = 6;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkTakenBy[0][PERK_DAMAGE] = 2;
	endlessPerkRederive();

	union {
		Uint32 align;
		Uint8 bytes[ENDLESS_PLAYER_BLOCK_SIZE + 8];
	} guarded;
	memset(guarded.bytes, 0x5a, sizeof(guarded.bytes));
	const int packed = endlessPackPlayerBlock(guarded.bytes + 4, 0);
	qa_check(packed == ENDLESS_PLAYER_BLOCK_SIZE,
	         "endless co-op player block packs its declared width");
	qa_check(guarded.bytes[3] == 0x5a && guarded.bytes[4 + ENDLESS_PLAYER_BLOCK_SIZE] == 0x5a,
	         "endless co-op player block stays inside its buffer");

	endlessUnpackPlayerBlock(guarded.bytes + 4, 1);
	qa_check(endlessArmorBonus[1] == 24 && endlessPurchasedMods[1] == ENDLESS_MOD_OVERDRIVE
	         && endlessBuffKind[1] == ENDLESS_BUFF_KIND_OVERDRIVE && endlessBuffCharge[1] == 7
	         && endlessBuffCooldownUntil[1] == 12 && endlessCleanseChargeCount[1] == 2
	         && endlessLongCon[1] == 3 && endlessShopTax[1] == 50 && endlessRevivesUsed[1] == 4
	         && endlessRerollCost[1] == 91000 && endlessHullCost[1] == 45000
	         && endlessShopEntryCash[1] == 1234567,
	         "endless co-op player block restores every numeric field");
	qa_check(endlessReviveHeld[1] && endlessGambleRigged[1] && endlessPlayerDowned[1]
	         && player[1].superbombs == 6,
	         "endless co-op player block restores the one-shot latches and bombs");
	qa_check(endlessPerkTakenBy[1][PERK_DAMAGE] == 2
	         && endlessPerkEffective(1, PERK_DAMAGE) == 2
	         && endlessPerkEffective(0, PERK_DAMAGE) == endlessPerkTakenBy[0][PERK_DAMAGE],
	         "endless perks stay their owner's row through the wire block");

	/* A drive belongs to the ship that bought it; the sector-changing half of a purchase does not. */
	endlessActiveMods = ENDLESS_MOD_FORTIFIED;
	endlessPurchasedMods[0] = ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_FAVOR;
	endlessPurchasedMods[1] = ENDLESS_MOD_OVERBLAST;
	endlessApplyPurchasedMods();
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_TURBODRIVE)
	         && !(endlessPlayerMods[0] & ENDLESS_MOD_OVERBLAST),
	         "a bought drive reaches only the ship that bought it");
	qa_check((endlessPlayerMods[1] & ENDLESS_MOD_OVERBLAST)
	         && !(endlessPlayerMods[1] & ENDLESS_MOD_TURBODRIVE),
	         "...and the other ship keeps its own");
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_FORTIFIED)
	         && (endlessPlayerMods[1] & ENDLESS_MOD_FORTIFIED)
	         && (endlessActiveMods & ENDLESS_MOD_FAVOR),
	         "the sector's own modifiers and the shop-side buys stay shared");

	/* A drive the SECTOR deals is not a purchase: it reaches both ships. One that a player then
	 * buys for themselves replaces it on their own mask alone. */
	endlessActiveMods = ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[0] = 0;
	endlessPurchasedMods[1] = 0;
	endlessApplyPurchasedMods();
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_TURBODRIVE)
	         && (endlessPlayerMods[1] & ENDLESS_MOD_TURBODRIVE),
	         "a charted drive reaches both ships");
	endlessPurchasedMods[1] = ENDLESS_MOD_OVERBLAST;
	endlessApplyPurchasedMods();
	qa_check((endlessPlayerMods[0] & ENDLESS_MOD_TURBODRIVE)
	         && !(endlessPlayerMods[0] & ENDLESS_MOD_OVERBLAST),
	         "...and a partner who bought nothing keeps flying it");
	qa_check((endlessPlayerMods[1] & ENDLESS_MOD_OVERBLAST)
	         && !(endlessPlayerMods[1] & ENDLESS_MOD_TURBODRIVE),
	         "...while the buyer's own drive replaces it for them alone");
	endlessActiveMods = 0;
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;

	endlessBuffCharge[0] = 4;
	endlessBuffCharge[1] = 15;
	endlessSetFxPlayer(1);
	qa_check(endlessBuffChargePaid() == 15, "the kill-fire window follows the ship being computed");
	endlessSetFxPlayer(0);
	qa_check(endlessBuffChargePaid() == 4, "...and the other ship reads its own charge");
	endlessActiveMods = 0;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	endlessBuffCharge[0] = endlessBuffCharge[1] = 0;

	/* Course picking. Every mode has to answer the same way on both machines. */
	endlessCourseChooser = ENDLESS_PICK_HOST;
	network_is_host = true;
	qa_check(endlessLocalPlayerCharts(), "Host picking charts on the host");
	network_is_host = false;
	qa_check(!endlessLocalPlayerCharts(), "Host picking does not chart on the joiner");

	endlessCourseChooser = ENDLESS_PICK_GUEST;
	qa_check(endlessLocalPlayerCharts(), "Guest picking charts on the joiner");
	network_is_host = true;
	qa_check(!endlessLocalPlayerCharts(), "Guest picking does not chart on the host");

	endlessCourseChooser = ENDLESS_PICK_ALTERNATE;
	endlessCoopHostCharts = true;
	qa_check(endlessLocalPlayerCharts(), "Alternating starts on the host");
	endlessAdvanceCourseTurn();
	qa_check(!endlessLocalPlayerCharts(), "Alternating hands the next course to the joiner");
	network_is_host = false;
	qa_check(endlessLocalPlayerCharts(), "...and the joiner sees the same turn");

	/* The coin flip is derived from the seed, so it never depends on how much either player
	 * shopped, and exactly one machine charts each zone. */
	endlessCourseChooser = ENDLESS_PICK_COINFLIP;
	endlessSetSeed("qa-coinflip");
	bool split = false, alwaysAgree = true;
	for (int zone = 0; zone < 24; ++zone)
	{
		endlessRunDepth = zone;
		network_is_host = true;
		const bool hostCharts = endlessLocalPlayerCharts();
		network_is_host = false;
		const bool guestCharts = endlessLocalPlayerCharts();
		if (hostCharts == guestCharts)
			alwaysAgree = false;
		if (hostCharts)
			split = true;
	}
	qa_check(alwaysAgree, "the 50-50 coin gives exactly one machine each course");
	qa_check(split, "the 50-50 coin lands on the host at least once in 24 zones");

	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		endlessArmorBonus[p] = 0; endlessPurchasedMods[p] = 0; endlessBuffKind[p] = 0;
		endlessBuffCharge[p] = 0; endlessBuffCooldownUntil[p] = 0; endlessCleanseChargeCount[p] = 0;
		endlessLongCon[p] = 0; endlessShopTax[p] = 0; endlessRevivesUsed[p] = 0;
		endlessReviveHeld[p] = false; endlessGambleRigged[p] = false; endlessPlayerDowned[p] = false;
		player[p].superbombs = 0;
	}
	endlessRunDepth = savedDepth;
	endlessCoopHostCharts = savedHostCharts;
	endlessCourseChooser = savedChooser;
	network_is_host = savedHost;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

/* Every kill-fire drive, driven through the real kill path: the window has to open, the boon has
 * to grant what it advertises and nothing it does not, and a curse has to bite. */
static void qa_test_kill_fire_drives(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const Uint64 savedActive = endlessActiveMods;
	const int savedKills = endlessRunKills;

	endlessMode = true;
	coopEndlessMode = true;
	memset(endlessPerkOwned, 0, sizeof(endlessPerkOwned));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));

	struct { Uint64 bit; const char *name; bool fire; bool damage; bool jam; const char *evil; } drives[] = {
		{ ENDLESS_MOD_TURBODRIVE, "Turbodrive", true,  false, false, "" },
		{ ENDLESS_MOD_OVERBLAST,  "Overblast",  false, true,  false, "" },
		{ ENDLESS_MOD_OVERDRIVE,  "Overdrive",  true,  true,  false, "" },
		{ ENDLESS_MOD_BACKFIRE,   "Backfire",   false, false, true,  "JAMMED" },
		{ ENDLESS_MOD_BURNOUT,    "Burnout",    false, false, true,  "BURNOUT" },
		{ ENDLESS_MOD_MISFIRE,    "Misfire",    false, false, false, "MISFIRE" },
	};

	for (unsigned d = 0; d < COUNTOF(drives); ++d)
	{
		// Player 0 buys the drive; player 1 buys nothing and must come away with nothing.
		endlessActiveMods = 0;
		endlessPurchasedMods[0] = (unsigned)drives[d].bit;
		endlessPurchasedMods[1] = 0;
		endlessBuffCharge[0] = 10;
		endlessBuffCharge[1] = 0;
		endlessApplyPurchasedMods();
		memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
		memset(endlessComboKills, 0, sizeof(endlessComboKills));
		memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));

		// Enough kills to climb a step of the combo ramp and stack the damage terms.
		for (int k = 0; k < 60; ++k)
			endlessCountKill(0, ENDLESS_KILLER_NONE);

		endlessSetFxPlayer(0);
		char detail[96];
		snprintf(detail, sizeof(detail), "%s opens its kill-fire window", drives[d].name);
		qa_check(endlessTurbodriveActive() && endlessComboKills[0] == 60, detail);

		snprintf(detail, sizeof(detail), "%s window length follows the charge its buyer paid",
		         drives[d].name);
		qa_check(endlessTurbodriveTimer[0] == endlessBuffWindowTicksFor(0)
		         && endlessBuffWindowTicksFor(0) > endlessBuffWindowTicksFor(1), detail);

		snprintf(detail, sizeof(detail), "%s %s the guns", drives[d].name,
		         drives[d].fire ? "quickens" : "leaves the cadence alone");
		qa_check((endlessKillBuffFireDecrements() > 0) == drives[d].fire, detail);

		snprintf(detail, sizeof(detail), "%s %s shot damage", drives[d].name,
		         drives[d].damage ? "stacks" : "does not stack");
		qa_check((endlessKillBuffDamagePercent() > 0) == drives[d].damage, detail);

		snprintf(detail, sizeof(detail), "%s %s", drives[d].name,
		         drives[d].jam ? "jams the guns" : "does not jam the guns");
		qa_check((endlessKillFireJamTicks() > 0) == drives[d].jam, detail);

		const bool evil = drives[d].evil[0] != '\0';
		snprintf(detail, sizeof(detail), "%s reads as %s", drives[d].name, evil ? "a curse" : "a boon");
		qa_check(endlessKillFireIsEvil() == evil
		         && strcmp(endlessKillFireEvilName(), drives[d].evil) == 0, detail);

		snprintf(detail, sizeof(detail), "%s moves the buyer's shot damage the right way",
		         drives[d].name);
		const int dmg = endlessPlayerDamagePercent();
		qa_check(drives[d].damage ? (dmg > 100) : (evil ? (dmg <= 100) : (dmg == 100)), detail);

		// The partner bought nothing, so none of it reaches them.
		endlessSetFxPlayer(1);
		snprintf(detail, sizeof(detail), "%s leaves the other ship untouched", drives[d].name);
		qa_check(!endlessTurbodriveActive() && endlessKillBuffFireDecrements() == 0
		         && endlessKillBuffDamagePercent() == 0 && endlessKillFireJamTicks() == 0
		         && endlessPlayerDamagePercent() == 100 && endlessShipTintFilter() == 0, detail);

		endlessSetFxPlayer(0);
		snprintf(detail, sizeof(detail), "%s tints the buyer's hull", drives[d].name);
		qa_check(endlessShipTintFilter() != 0, detail);
	}

	/* Two ships, two different drives bought at two different prices. Each has to come away with
	 * its own window length: the kill loop reads the charge of the ship it is opening the window
	 * for, not whichever ship the effect context happened to be pointing at. */
	endlessActiveMods = 0;
	endlessPurchasedMods[0] = ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[1] = ENDLESS_MOD_OVERBLAST;
	endlessBuffCharge[0] = 0;
	endlessBuffCharge[1] = 20;
	endlessApplyPurchasedMods();
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	endlessSetFxPlayer(0);   // the context stays on player 0 across the whole burst
	for (int k = 0; k < 30; ++k)
		endlessCountKill(0, ENDLESS_KILLER_NONE);

	qa_check(endlessTurbodriveTimer[0] == endlessBuffWindowTicksFor(0)
	         && endlessTurbodriveTimer[1] == endlessBuffWindowTicksFor(1)
	         && endlessTurbodriveTimer[1] > endlessTurbodriveTimer[0],
	         "each ship's window is as long as its own drive was paid for");

	endlessSetFxPlayer(0);
	const bool p0 = endlessKillBuffFireDecrements() > 0 && endlessKillBuffDamagePercent() == 0;
	endlessSetFxPlayer(1);
	const bool p1 = endlessKillBuffFireDecrements() == 0 && endlessKillBuffDamagePercent() > 0;
	qa_check(p0 && p1, "two ships fly different drives at once, each getting only its own");

	endlessSetFxPlayer(0);
	endlessActiveMods = savedActive;
	endlessRunKills = savedKills;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;
	endlessBuffCharge[0] = endlessBuffCharge[1] = 0;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

/* The wiring, not the rules: the per-tick block in JE_playerMovement is what turns a drive into
 * a faster gun, and it used to run for player 1 alone. Re-create exactly what it does for each
 * ship in turn and check the cooldowns actually move for both. */
static void qa_test_kill_fire_wiring(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const Uint64 savedActive = endlessActiveMods;
	const int savedKills = endlessRunKills;

	endlessMode = true;
	coopEndlessMode = true;
	memset(endlessPerkOwned, 0, sizeof(endlessPerkOwned));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));

	// Both ships buy Turbodrive; both should have their guns quickened.
	endlessActiveMods = 0;
	endlessPurchasedMods[0] = ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[1] = ENDLESS_MOD_TURBODRIVE;
	endlessBuffCharge[0] = endlessBuffCharge[1] = 0;
	endlessApplyPurchasedMods();
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	for (int k = 0; k < 30; ++k)
		endlessCountKill(0, ENDLESS_KILLER_NONE);

	int dropped[2] = { 0, 0 };
	for (uint p = 0; p < 2; ++p)
	{
		endlessSetFxPlayer(p);              // JE_playerMovement sets this from this_player
		shotRepeat[SHOT_FRONT] = 100;
		endlessPerShipTick(&player[p]);     // the real per-tick block, gate included
		dropped[p] = 100 - shotRepeat[SHOT_FRONT];
	}
	qa_check(dropped[0] > 0 && dropped[1] > 0,
	         "a drive quickens the guns of every ship that bought one, not just player 1");

	// Only player 2 buys one: player 1's guns must stay at their stock cadence.
	endlessActiveMods = 0;
	endlessPurchasedMods[0] = 0;
	endlessPurchasedMods[1] = ENDLESS_MOD_TURBODRIVE;
	endlessApplyPurchasedMods();
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	for (int k = 0; k < 30; ++k)
		endlessCountKill(0, ENDLESS_KILLER_NONE);

	for (uint p = 0; p < 2; ++p)
	{
		endlessSetFxPlayer(p);
		shotRepeat[SHOT_FRONT] = 100;
		endlessPerShipTick(&player[p]);
		dropped[p] = 100 - shotRepeat[SHOT_FRONT];
	}
	qa_check(dropped[0] == 0 && dropped[1] > 0,
	         "a drive the second ship bought quickens the second ship alone");

	/* Opening Salvo charges on an idle gun and is spent by the gun that fires, so one ship
	 * shooting must not spend the other's charge; and the perk is personal, so a ship that
	 * never picked it has no salvo to spend at all. */
	endlessPerkTakenBy[0][PERK_SALVO] = 1;
	endlessPerkTakenBy[1][PERK_SALVO] = 1;
	endlessPerkRederive();
	endlessResetZonePerkTimers();      // both ships start a zone charged
	endlessSetFxPlayer(0);
	qa_check(endlessOpeningSalvoConsume() && endlessOpeningSalvoVolleyActive(),
	         "the first ship spends its own Opening Salvo");
	endlessSetFxPlayer(1);
	qa_check(!endlessOpeningSalvoVolleyActive(),
	         "...and the second ship's salvo is still banked");
	qa_check(endlessOpeningSalvoConsume() && endlessOpeningSalvoVolleyActive(),
	         "...for the second ship to spend itself");

	/* The generator gauge the HUD paints green reads the same per-ship state: full while a charge
	 * is banked, receding while the spent window burns down, and untouched on the ship that did
	 * not fire. Co-op draws it for whichever ship the machine flies, so both rows must answer. */
	endlessResetZonePerkTimers();
	endlessSetFxPlayer(0);
	qa_check(endlessOpeningSalvoGaugePercent() == 100, "a banked Opening Salvo fills its owner's gauge");
	endlessOpeningSalvoConsume();
	int salvoPct = endlessOpeningSalvoGaugePercent();
	bool salvoRecedes = (salvoPct > 0);
	for (int t = 0; t < ENDLESS_PERK_SALVO_WINDOW; ++t)
	{
		endlessOpeningSalvoTick();          // the run-wide walk, exactly as the sim tick calls it
		const int now = endlessOpeningSalvoGaugePercent();
		if (now > salvoPct)
			salvoRecedes = false;
		salvoPct = now;
	}
	qa_check(salvoRecedes && salvoPct == 0, "firing drains the gauge as the salvo window runs out");
	endlessSetFxPlayer(1);
	qa_check(endlessOpeningSalvoGaugePercent() == 100,
	         "...and the ship that held its fire still reads a full charge");

	endlessPerkTakenBy[1][PERK_SALVO] = 0;
	endlessPerkRederive();
	qa_check(endlessOpeningSalvoGaugePercent() == 0,
	         "a ship without the perk has no green on its gauge at all");
	endlessResetZonePerkTimers();
	qa_check(!endlessOpeningSalvoConsume(),
	         "a ship that never picked Opening Salvo has none to spend");

	/* Where in the tick the window opens. The special fires before the weapon loop, so a salvo
	 * armed down at the gun left the special that pressed the same button outside its own volley.
	 * Drive the exported gate the shot section reaches ahead of JE_doSpecialShot. */
	endlessPerkTakenBy[0][PERK_SALVO] = 1;
	endlessPerkTakenBy[1][PERK_SALVO] = 1;
	endlessPerkRederive();
	endlessResetZonePerkTimers();

	const JE_boolean savedTwo = twoPlayerMode, savedLinked = twoPlayerLinked;
	const bool savedFire = button[0];
	const JE_byte savedRepeat = shotRepeat[SHOT_FRONT];
	const Uint8 savedGun[2] = { player[0].items.weapon[SHOT_FRONT].id,
	                            player[1].items.weapon[SHOT_FRONT].id };

	twoPlayerMode = true;
	twoPlayerLinked = false;
	player[0].items.weapon[SHOT_FRONT].id = player[1].items.weapon[SHOT_FRONT].id = 1;
	shotRepeat[SHOT_FRONT] = 0;
	button[0] = true;

	endlessSetFxPlayer(0);
	qa_check(endlessArmOpeningSalvoForTick(&player[0], 1) && endlessOpeningSalvoVolleyActive(),
	         "the volley's window is open before the tick's special goes out");
	endlessSetFxPlayer(1);
	qa_check(!endlessOpeningSalvoVolleyActive(),
	         "...and open for the ship that pulled the trigger alone");
	qa_check(endlessArmOpeningSalvoForTick(&player[1], 2) && endlessOpeningSalvoVolleyActive(),
	         "...leaving the second ship its own to open from its own bank");

	// Nothing fires on a recharging gun or a released trigger, so neither may spend the charge.
	endlessResetZonePerkTimers();
	endlessSetFxPlayer(0);
	shotRepeat[SHOT_FRONT] = 5;
	qa_check(!endlessArmOpeningSalvoForTick(&player[0], 1) && !endlessOpeningSalvoVolleyActive()
	         && endlessOpeningSalvoGaugePercent() == 100,
	         "a gun still recharging opens no window and keeps the charge banked");
	shotRepeat[SHOT_FRONT] = 0;
	button[0] = false;
	qa_check(!endlessArmOpeningSalvoForTick(&player[0], 1) && endlessOpeningSalvoGaugePercent() == 100,
	         "...and so does a released trigger");
	button[0] = true;
	player[0].items.weapon[SHOT_FRONT].id = 0;
	qa_check(!endlessArmOpeningSalvoForTick(&player[0], 1) && endlessOpeningSalvoGaugePercent() == 100,
	         "...and an empty front bay, which has no volley to open one for");

	twoPlayerMode = savedTwo;
	twoPlayerLinked = savedLinked;
	button[0] = savedFire;
	shotRepeat[SHOT_FRONT] = savedRepeat;
	player[0].items.weapon[SHOT_FRONT].id = savedGun[0];
	player[1].items.weapon[SHOT_FRONT].id = savedGun[1];

	endlessSetFxPlayer(0);
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessResetZonePerkTimers();
	endlessActiveMods = savedActive;
	endlessRunKills = savedKills;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

/* Kinetic Converter's recharge, magazine and charge-ramp payouts each scale with the stacks, stay
 * bounded, and stay personal: in co-op only the ship that took the hit may be paid for it. */
static void qa_test_kinetic_converter(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	coopEndlessMode = true;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	const int maxStack = endlessPerkMaxStack(PERK_KINETIC);

	qa_check(endlessPerkKineticCooldownCut(200) == 0 && endlessPerkKineticAmmoRounds() == 0
	         && endlessPerkKineticChargeStages() == 0,
	         "a ship without Kinetic Converter is paid nothing for taking a hit");

	// The cut must be monotonic in the stacks, bounded by the time left, and at least a tick.
	bool cutGrows = true, cutBounded = true;
	int prevCut = 0;
	for (int s = 1; s <= maxStack; ++s)
	{
		endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)s;
		endlessPerkRederive();
		const int cut = endlessPerkKineticCooldownCut(200);
		cutGrows &= cut > prevCut;
		prevCut = cut;
		cutBounded &= cut <= 200 && endlessPerkKineticCooldownCut(1) == 1
		           && endlessPerkKineticCooldownCut(0) == 0;
	}
	qa_check(cutGrows, "each Kinetic Converter stack takes more off the special recharge");
	qa_check(cutBounded, "...but never more of the clock than is left on it");

	// Four hits is one whole round per stack at the 25%-per-stack rate, carry included.
	for (int s = 1; s <= maxStack; ++s)
	{
		endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)s;
		endlessPerkRederive();
		memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
		int rounds = 0;
		for (int hit = 0; hit < 4; ++hit)
			rounds += endlessPerkKineticAmmoRounds();
		qa_check(rounds == s, "four absorbed hits give back one sidekick round per Kinetic stack");
		qa_check(endlessPerkKineticChargeStages() == s,
		         "...and every hit walks a charge sidekick one stage per stack");
	}

	// Twiddle charges: cheaper with every stack, never free, and never dearer than the list price.
	endlessSetFxPlayer(0);
	endlessPerkTakenBy[0][PERK_KINETIC] = 0;
	endlessPerkRederive();
	qa_check(endlessPerkKineticTwiddleCost(20) == 20 && endlessPerkKineticTwiddleCost(0) == 0,
	         "a ship without Kinetic Converter pays a twiddle's list price");

	bool costFalls = true, costBounded = true;
	int prevCost = endlessPerkKineticTwiddleCost(20);
	for (int s = 1; s <= maxStack; ++s)
	{
		endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)s;
		endlessPerkRederive();
		const int paid = endlessPerkKineticTwiddleCost(20);
		costFalls &= paid < prevCost;
		prevCost = paid;
		costBounded &= paid > 0 && endlessPerkKineticTwiddleCost(1) == 1;
	}
	qa_check(costFalls, "each Kinetic Converter stack takes more off a twiddle's charge");
	qa_check(costBounded && prevCost == 7, "...down to a third of a 20-point charge, never free");

	// Only the first ship picks it, so the partner's hits pay nothing and each carry stays its own.
	memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
	endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)maxStack;
	endlessPerkTakenBy[1][PERK_KINETIC] = 0;
	endlessPerkRederive();
	int paid[2] = { 0, 0 };
	for (int hit = 0; hit < 4; ++hit)
		for (uint p = 0; p < 2; ++p)
		{
			endlessSetFxPlayer(p);
			paid[p] += endlessPerkKineticAmmoRounds();
		}
	endlessSetFxPlayer(1);
	qa_check(paid[0] == maxStack && paid[1] == 0 && endlessPerkKineticCooldownCut(200) == 0
	         && endlessPerkKineticChargeStages() == 0,
	         "only the ship that bought Kinetic Converter is paid for a hit");

	endlessSetFxPlayer(0);
	memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
}

/* Whose combo a kill feeds, and what Individual credit does to a pickup. */
static void qa_test_coop_combo_and_pickups(void)
{
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const JE_boolean savedCampaign = coopCampaignMode;
	const Uint64 savedActive = endlessActiveMods;
	const int savedKills = endlessRunKills;
	const bool savedShared = endlessCoopComboShared;

	endlessMode = true;
	coopEndlessMode = true;
	memset(endlessPerkOwned, 0, sizeof(endlessPerkOwned));
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));

	// Both ships fly a drive, so either one's streak could climb.
	endlessActiveMods = ENDLESS_MOD_TURBODRIVE;
	endlessPurchasedMods[0] = endlessPurchasedMods[1] = 0;
	endlessApplyPurchasedMods();

	endlessCoopComboShared = false;
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	for (int k = 0; k < 12; ++k)
		endlessCountKill(0, 0);          // player 1 does all the shooting
	qa_check(endlessComboKills[0] == 12 && endlessComboKills[1] == 0,
	         "Individual combo feed keeps one ship's kills out of the other's streak");
	qa_check(endlessTurbodriveTimer[0] > 0 && endlessTurbodriveTimer[1] == 0,
	         "...and only the shooter's drive window opens");

	for (int k = 0; k < 5; ++k)
		endlessCountKill(0, 1);          // now player 2 takes some
	qa_check(endlessComboKills[0] == 12 && endlessComboKills[1] == 5,
	         "...each ship counting its own");

	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	endlessCountKill(0, ENDLESS_KILLER_NONE);
	qa_check(endlessComboKills[0] == 1 && endlessComboKills[1] == 1,
	         "a kill nothing can claim feeds both, so neither streak is punished for it");

	endlessCoopComboShared = true;
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	for (int k = 0; k < 7; ++k)
		endlessCountKill(0, 0);
	qa_check(endlessComboKills[0] == 7 && endlessComboKills[1] == 7,
	         "Shared combo feed has every kill feed both streaks");

	/* Double Earnings compensates a split take, and only a split take. */
	coopEndlessMode = false;
	coopCampaignMode = true;
	endlessMode = false;

	coop_set_session_shared_credit(false);
	coop_set_session_double_earnings(true);
	qa_check(coop_earnings_are_doubled(), "Double Earnings applies under Individual credit");

	player[0].cash = 0;
	player[1].cash = 0;
	player_award_pickup_cash(&player[0], 250);
	qa_check(player[0].cash == 500 && player[1].cash == 0,
	         "a doubled pickup pays its collector twice and nobody else");

	player[0].cash = 0;
	player_award_kill_cash(&player[0], 250);
	qa_check(player[0].cash == 500, "...and covers kill cash the same way");

	coop_set_session_shared_credit(true);
	qa_check(!coop_earnings_are_doubled(),
	         "Double Earnings stands down under Shared credit, where both already collect in full");
	player[0].cash = player[1].cash = 0;
	player_award_pickup_cash(&player[0], 250);
	qa_check(player[0].cash == 250 && player[1].cash == 250,
	         "...and Shared pays the plain amount to both");

	/* An elite bounty is kill cash and follows the shooter, and one nothing can claim has to pay
	 * the same ship on both machines: paying "the local player" put it in a different wallet on
	 * each side of the session. Run this as the joiner, where the two answers differ. */
	const JE_boolean savedNetGame = isNetworkGame;
	const JE_byte savedPlayerNum = thisPlayerNum;
	isNetworkGame = true;
	thisPlayerNum = 2;
	coopCampaignMode = false;
	coopEndlessMode = true;
	endlessMode = true;
	endlessActiveMods = 0;
	coop_set_session_shared_credit(false);
	coop_set_session_double_earnings(false);
	qa_check(endlessEconomyIndex() == 1, "the joiner's own wallet is player 2's");

	player[0].cash = player[1].cash = 0;
	endlessCashResync();  // the run ledger books player 1's income; re-anchor after every reset
	endlessAwardEliteKill(41, 2, 1);
	qa_check(player[1].cash > 0 && player[0].cash == 0, "an elite bounty pays the ship that killed it");

	const Uint32 shooterBounty = player[1].cash;
	player[0].cash = player[1].cash = 0;
	endlessCashResync();
	endlessAwardEliteKill(42, 2, ENDLESS_KILLER_NONE);
	qa_check(player[0].cash == shooterBounty && player[1].cash == 0,
	         "...and an unclaimable one pays player 1, the same ship on both machines");

	player[0].cash = player[1].cash = 0;
	endlessCashResync();
	thisPlayerNum = savedPlayerNum;
	isNetworkGame = savedNetGame;
	coop_set_session_double_earnings(false);
	coop_set_session_shared_credit(true);
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	endlessCoopComboShared = savedShared;
	endlessActiveMods = savedActive;
	endlessRunKills = savedKills;
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
}

/* The peer leaving a level under us. Drives the real departure rule the rollback stall uses,
 * because the bug it covers was not in what any of these functions return: a quit was ending the
 * level without saying it was a quit, so the machine that stayed banked the zone and deepened
 * while the one that quit reopened the same outpost. */
static void qa_test_peer_left_level(void)
{
#ifdef WITH_NETWORK
	const JE_boolean savedEndless = endlessMode, savedCoop = coopEndlessMode;
	const JE_boolean savedEnd = reallyEndLevel, savedPlayerEnd = playerEndLevel;
	const bool savedQuit = endlessQuitToOutpost;

	endlessMode = true;
	coopEndlessMode = true;
	endlessCaptureSortie();  // a peer quit only reopens an outpost there is a launch snapshot for
	qa_check(endlessSortieValid(), "the quit cases below have a sortie to fall back to");

	reallyEndLevel = false;
	playerEndLevel = false;
	endlessQuitToOutpost = false;
	qa_check(!nrb_peer_left_level(0), "an empty queue does not end the level");
	qa_check(!reallyEndLevel && !playerEndLevel, "...and touches nothing");

	qa_check(nrb_peer_left_level(PACKET_WAITING), "a peer at the level handshake ends the level");
	qa_check(reallyEndLevel && !playerEndLevel && !endlessQuitToOutpost,
	         "...as a cleared zone, which is what reaching that handshake means");

	reallyEndLevel = false;
	qa_check(nrb_peer_left_level(PACKET_GAME_QUIT), "a peer quit ends the level too");
	qa_check(reallyEndLevel && playerEndLevel,
	         "...but not as a clear: the zone was given up, not finished");
	qa_check(endlessQuitToOutpost,
	         "...and Endless reopens the same outpost, so neither player deepens alone");

	// Campaign has no outpost to fall back to, so a quit there stays a quit.
	coopEndlessMode = false;
	endlessQuitToOutpost = false;
	reallyEndLevel = playerEndLevel = false;
	qa_check(nrb_peer_left_level(PACKET_GAME_QUIT) && playerEndLevel && !endlessQuitToOutpost,
	         "outside Endless a peer quit leaves the run-reopening flag alone");

	endlessQuitToOutpost = savedQuit;
	playerEndLevel = savedPlayerEnd;
	reallyEndLevel = savedEnd;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
#endif
}

static void qa_test_network_settings(void)
{
#ifdef WITH_NETWORK
	int savedSpark[SSW_COUNT], savedEpDiff[EDW_COUNT];
	memcpy(savedSpark, superSparkMode, sizeof(savedSpark));
	memcpy(savedEpDiff, epDiffMode, sizeof(savedEpDiff));
	const int savedZicaBase = zicaLaserBase, savedZicaLength = zicaLaserLength;
	const bool savedZicaLock = zicaLaserLock, savedZicaBuff = zicaLaserBuff;
	const int savedWallop = wallopSecondBolt;
	const bool savedCharge = chargeLaserCannon, savedDispensers = restoreBaseDispensers;
	const bool savedLifeBoost = arcadeLifeBoost, savedRandomBalls = arcadeRandomBalls;
	const bool savedRearScale = arcadeRearGunScale;
	const bool savedCenteredHitboxes = centeredShotHitboxes;
	const int savedXmas = xmasMode;
	const JE_byte savedSpeed = gameSpeed;
	const bool savedRollbackConfig = net_rollback, savedRecoveryConfig = net_desync_recovery;
	const bool savedVt = vt_ship, savedMotion = smoothMotion;
	const JE_boolean savedScroll = smoothScroll;
	const bool savedSessionMode = nrb_session_mode(), savedSessionVt = nrb_session_vt();
	const bool savedSessionRecovery = nrb_session_recovery();
	const bool savedSharedCredit = coopSharedCredit;
	const bool savedDoublePickups = coopDoubleEarnings;
	const JE_boolean savedCoopCampaign = coopCampaignMode;
	const JE_boolean savedExpertMode = expertMode;
	const bool savedInfShields = cheatInfiniteShields;
	const bool savedInfArmor = cheatInfiniteArmor;
	const bool savedInfGenerator = cheatInfiniteGenerator;
	const bool savedNoEnemyFire = cheatNoEnemyFire;
	const bool savedInstantCharge = cheatInstantCharge;
	const bool savedInfSidekick = cheatInfiniteSidekickAmmo;
	const bool savedAutoSpecial = autoFireSpecial;
	const bool savedAutoTwiddle = debugAutofireTwiddle;
	const bool savedToggleFire = debugToggleFire;
	const bool savedDifficultyAdjust = difficultyAdjust;
	const bool savedTwiddleTrigger = debugTwiddleTrigger;
	const bool savedConstantPlay = constantPlay;
	const bool savedConstantDie = constantDie;
	const JE_byte savedNoclip = noclipMode;
	const JE_byte savedChargeAF = chargeSidekickAutofire;
	const JE_byte savedTwiddle = debugTwiddleSpecial;
	int savedExpert[NETWORK_EXPERT_SLOTS];
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		savedExpert[i] = *expertSettings[i].value;
	/* SDLNet_Read/Write16/32 require naturally aligned storage. Keep guard bytes around an
	 * aligned payload instead of making the alignment itself part of this bounds test. */
	union {
		Uint32 align;
		Uint8 bytes[NETWORK_SETTINGS_SIZE + 8];
	} guarded;
	Uint8 *const packet = guarded.bytes + 4;

	for (int i = 0; i < SSW_COUNT; ++i)
		superSparkMode[i] = i % SUPER_SPARKS_COUNT;
	for (int i = 0; i < EDW_COUNT; ++i)
		epDiffMode[i] = i % EPDIFF_MODE_COUNT;
	zicaLaserBase = ZICA_BASE_EP4; zicaLaserLength = ZICA_LEN_LONG;
	zicaLaserLock = true; zicaLaserBuff = false; wallopSecondBolt = SUPER_SPARKS_ON;
	chargeLaserCannon = true; restoreBaseDispensers = false;
	arcadeLifeBoost = true; arcadeRandomBalls = false; arcadeRearGunScale = true;
	centeredShotHitboxes = true;
	xmasMode = 1; gameSpeed = 2;
	net_rollback = true; net_desync_recovery = true;
	coopSharedCredit = true;
	coopDoubleEarnings = true;
	vt_ship = true; smoothMotion = true; smoothScroll = true;
	// Expert Mode multiplies enemy health, weapon energy and prices, so the pair has to agree on
	// it before the first boss. Each tunable takes a distinct in-range value, so a slot wired to
	// the wrong offset lands somewhere visible instead of on its neighbour's identical number.
	expertMode = true;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = expertSettings[i].lo + expertSettings[i].step * (i + 1);
	cheatInfiniteShields = true;
	cheatInfiniteArmor = false;
	cheatInfiniteGenerator = true;
	cheatNoEnemyFire = false;
	cheatInstantCharge = true;
	cheatInfiniteSidekickAmmo = false;
	autoFireSpecial = true;
	debugAutofireTwiddle = false;
	debugToggleFire = true;
	difficultyAdjust = false;
	debugTwiddleTrigger = true;
	noclipMode = NOCLIP_TRANSPARENT;
	constantPlay = true;
	constantDie = false;
	chargeSidekickAutofire = CHARGE_AUTOFIRE_FULL;
	debugTwiddleSpecial = SPECIAL_NUM > 0 ? 1 : 0;
	memset(guarded.bytes, 0x5a, sizeof(guarded.bytes));
	const int packed = network_settings_pack(packet);
	qa_check(packed == NETWORK_SETTINGS_SIZE && guarded.bytes[3] == 0x5a
	         && guarded.bytes[4 + NETWORK_SETTINGS_SIZE] == 0x5a,
	         "network settings packing writes exactly its fixed block and no byte more");

	/* A joiner has different local preferences before it adopts the host block. */
	for (int i = 0; i < SSW_COUNT; ++i) superSparkMode[i] = SUPER_SPARKS_OFF;
	for (int i = 0; i < EDW_COUNT; ++i) epDiffMode[i] = EPDIFF_EP13;
	zicaLaserBase = ZICA_BASE_AUTO; zicaLaserLength = ZICA_LEN_SHORT;
	zicaLaserLock = false; zicaLaserBuff = true; wallopSecondBolt = SUPER_SPARKS_OFF;
	chargeLaserCannon = false; restoreBaseDispensers = true;
	arcadeLifeBoost = false; arcadeRandomBalls = true; arcadeRearGunScale = false;
	centeredShotHitboxes = false;
	xmasMode = 0; gameSpeed = 5;
	expertMode = false;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = expertSettings[i].def;
	cheatInfiniteShields = false;
	cheatInfiniteArmor = true;
	cheatInfiniteGenerator = false;
	cheatNoEnemyFire = true;
	cheatInstantCharge = false;
	cheatInfiniteSidekickAmmo = true;
	autoFireSpecial = false;
	debugAutofireTwiddle = true;
	debugToggleFire = false;
	difficultyAdjust = true;
	debugTwiddleTrigger = false;
	noclipMode = NOCLIP_OFF;
	constantPlay = false;
	constantDie = true;
	chargeSidekickAutofire = CHARGE_AUTOFIRE_OFF;
	debugTwiddleSpecial = 0;
	coop_set_session_shared_credit(false);
	coopCampaignMode = true;
	qa_check(network_settings_adopt(packet) == NETWORK_SETTINGS_SIZE,
	         "network settings decoder consumes its exact fixed block");
	bool arraysMatch = true;
	for (int i = 0; i < SSW_COUNT; ++i) arraysMatch &= superSparkMode[i] == i % SUPER_SPARKS_COUNT;
	for (int i = 0; i < EDW_COUNT; ++i) arraysMatch &= epDiffMode[i] == i % EPDIFF_MODE_COUNT;
	qa_check(arraysMatch && zicaLaserBase == ZICA_BASE_EP4 && zicaLaserLength == ZICA_LEN_LONG
	         && zicaLaserLock && !zicaLaserBuff && wallopSecondBolt == SUPER_SPARKS_ON
	         && chargeLaserCannon && !restoreBaseDispensers && arcadeLifeBoost
	         && !arcadeRandomBalls && arcadeRearGunScale && centeredShotHitboxes
	         && xmasMode == 1 && gameSpeed == 2
	         && nrb_session_mode() && nrb_session_vt() && nrb_session_recovery()
	         && coop_credit_is_shared(),
	         "joiner adopts every host-authoritative simulation setting");
	bool expertMatch = expertMode;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		expertMatch &= *expertSettings[i].value == expertSettings[i].lo + expertSettings[i].step * (i + 1);
	qa_check(expertMatch, "...including Expert Mode and every tunable behind it");
	qa_check(cheatInfiniteShields && !cheatInfiniteArmor && cheatInfiniteGenerator
	         && !cheatNoEnemyFire && cheatInstantCharge && !cheatInfiniteSidekickAmmo
	         && autoFireSpecial && !debugAutofireTwiddle && debugToggleFire
	         && !difficultyAdjust && debugTwiddleTrigger && noclipMode == NOCLIP_TRANSPARENT
	         && constantPlay && !constantDie
	         && chargeSidekickAutofire == CHARGE_AUTOFIRE_FULL
	         && debugTwiddleSpecial == (SPECIAL_NUM > 0 ? 1 : 0),
	         "...including initial autofire and every simulation-affecting debug control");
	// Doubling is carried in the same word but is inert under Shared, so check the flag itself
	// by flipping the credit mode the adopted value sits behind.
	coop_set_session_shared_credit(false);
	qa_check(coop_earnings_are_doubled(), "...including whether Individual pays pickups twice");
	coop_set_session_shared_credit(true);
	network_settings_restore();
	arraysMatch = true;
	for (int i = 0; i < SSW_COUNT; ++i) arraysMatch &= superSparkMode[i] == SUPER_SPARKS_OFF;
	for (int i = 0; i < EDW_COUNT; ++i) arraysMatch &= epDiffMode[i] == EPDIFF_EP13;
	qa_check(arraysMatch && zicaLaserBase == ZICA_BASE_AUTO && zicaLaserLength == ZICA_LEN_SHORT
	         && !zicaLaserLock && zicaLaserBuff && wallopSecondBolt == SUPER_SPARKS_OFF
	         && !chargeLaserCannon && restoreBaseDispensers && !arcadeLifeBoost
	         && arcadeRandomBalls && !arcadeRearGunScale && !centeredShotHitboxes
	         && xmasMode == 0 && gameSpeed == 5,
	         "leaving a network session restores every local simulation preference");
	expertMatch = !expertMode;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		expertMatch &= *expertSettings[i].value == expertSettings[i].def;
	qa_check(expertMatch, "...Expert Mode and its tunables included, so the next solo game is ours");
	qa_check(!cheatInfiniteShields && cheatInfiniteArmor && !cheatInfiniteGenerator
	         && cheatNoEnemyFire && !cheatInstantCharge && cheatInfiniteSidekickAmmo
	         && !autoFireSpecial && debugAutofireTwiddle && !debugToggleFire
	         && difficultyAdjust && !debugTwiddleTrigger && noclipMode == NOCLIP_OFF
	         && !constantPlay && constantDie
	         && chargeSidekickAutofire == CHARGE_AUTOFIRE_OFF && debugTwiddleSpecial == 0,
	         "...and restores the joiner's local autofire/debug controls afterward");

	/* The host runs on flags armed from its own config; the joiner adopts the block packed
	 * from that same config. The two must land on identical session behavior, or the pair
	 * splits at the first payout: Double Earnings was armed on the joiner alone, and every
	 * pickup desynced the wallets by its own value. */
	coopSharedCredit = false;
	coopDoubleEarnings = true;
	net_rollback = true;
	net_desync_recovery = true;
	vt_ship = true; smoothMotion = true; smoothScroll = true;
	coopCampaignMode = true;
	coop_set_session_shared_credit(true);    // stale session values the arm must replace,
	coop_set_session_double_earnings(false);  // or a missed flag hides behind leftovers
	network_arm_local_session();
	const bool hostDoubled = coop_earnings_are_doubled();
	const bool hostShared = coop_credit_is_shared();
	network_settings_pack(packet);
	coop_set_session_shared_credit(true);     // a joiner arrives holding other values
	coop_set_session_double_earnings(false);
	network_settings_adopt(packet);
	qa_check(hostDoubled && !hostShared
	         && coop_earnings_are_doubled() == hostDoubled
	         && coop_credit_is_shared() == hostShared,
	         "host arming and joiner adoption produce the same credit session");
	network_settings_restore();

	memset(packet, 0xff, NETWORK_SETTINGS_SIZE);
	network_settings_adopt(packet);
	bool malformedClamped = true;
	for (int i = 0; i < SSW_COUNT; ++i) malformedClamped &= superSparkMode[i] == SUPER_SPARKS_AUTO;
	for (int i = 0; i < EDW_COUNT; ++i) malformedClamped &= epDiffMode[i] == EPDIFF_AUTO;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		malformedClamped &= *expertSettings[i].value == expertSettings[i].hi;
	qa_check(malformedClamped && zicaLaserBase == ZICA_BASE_AUTO
	         && zicaLaserLength == ZICA_LEN_SHORT && wallopSecondBolt == SUPER_SPARKS_AUTO
	         && xmasMode == -1 && gameSpeed == 4 && noclipMode < NOCLIP_NUM
	         && chargeSidekickAutofire < CHARGE_AUTOFIRE_NUM
	         && debugTwiddleSpecial <= SPECIAL_NUM,
	         "hostile network settings are clamped before any array can be indexed"
	         " or any multiplier applied");
	network_settings_restore();

	memcpy(superSparkMode, savedSpark, sizeof(savedSpark));
	memcpy(epDiffMode, savedEpDiff, sizeof(savedEpDiff));
	zicaLaserBase = savedZicaBase; zicaLaserLength = savedZicaLength;
	zicaLaserLock = savedZicaLock; zicaLaserBuff = savedZicaBuff; wallopSecondBolt = savedWallop;
	chargeLaserCannon = savedCharge; restoreBaseDispensers = savedDispensers;
	arcadeLifeBoost = savedLifeBoost; arcadeRandomBalls = savedRandomBalls;
	arcadeRearGunScale = savedRearScale; centeredShotHitboxes = savedCenteredHitboxes;
	xmasMode = savedXmas; gameSpeed = savedSpeed;
	net_rollback = savedRollbackConfig; net_desync_recovery = savedRecoveryConfig;
	vt_ship = savedVt; smoothMotion = savedMotion; smoothScroll = savedScroll;
	nrb_set_session_mode(savedSessionMode);
	nrb_set_session_vt(savedSessionVt);
	nrb_set_session_recovery(savedSessionRecovery);
	coopSharedCredit = savedSharedCredit;
	coopDoubleEarnings = savedDoublePickups;
	coop_set_session_shared_credit(savedSharedCredit);
	coop_set_session_double_earnings(savedDoublePickups);
	coopCampaignMode = savedCoopCampaign;
	expertMode = savedExpertMode;
	for (int i = 0; i < expertSettingsCount && i < NETWORK_EXPERT_SLOTS; ++i)
		*expertSettings[i].value = savedExpert[i];
	cheatInfiniteShields = savedInfShields;
	cheatInfiniteArmor = savedInfArmor;
	cheatInfiniteGenerator = savedInfGenerator;
	cheatNoEnemyFire = savedNoEnemyFire;
	cheatInstantCharge = savedInstantCharge;
	cheatInfiniteSidekickAmmo = savedInfSidekick;
	autoFireSpecial = savedAutoSpecial;
	debugAutofireTwiddle = savedAutoTwiddle;
	debugToggleFire = savedToggleFire;
	difficultyAdjust = savedDifficultyAdjust;
	debugTwiddleTrigger = savedTwiddleTrigger;
	noclipMode = savedNoclip;
	constantPlay = savedConstantPlay;
	constantDie = savedConstantDie;
	chargeSidekickAutofire = savedChargeAF;
	debugTwiddleSpecial = savedTwiddle;
#else
	qa_check(true, "network settings round trip skipped without networking");
#endif
}

/* The Endless lobby block rides the connect packet beside the settings block. Every field the
 * joiner adopts has to arrive, and every out-of-range byte has to be clamped, not indexed with. */
static void qa_test_network_endless_lobby(void)
{
#ifdef WITH_NETWORK
	const int savedMode = network_host_endless_run_mode;
	const int savedChooser = network_host_endless_chooser;
	const bool savedCombo = network_host_endless_combo_shared;
	const int savedBaseRule = network_host_endless_base_rule;
	char savedSeed[NET_ENDLESS_SEED_MAX], savedHostSeed[NET_ENDLESS_SEED_MAX];
	memcpy(savedSeed, network_endless_session_seed, sizeof(savedSeed));
	memcpy(savedHostSeed, network_host_endless_seed, sizeof(savedHostSeed));

	Uint8 block[4 + NET_ENDLESS_SEED_MAX];
	memset(block, 0, sizeof(block));
	block[0] = (Uint8)ENDLESS_RUNMODE_HARDCORE;
	block[1] = (Uint8)(ENDLESS_PICK_COUNT - 1);
	block[2] = 1;
	block[3] = (Uint8)(ENDLESS_BASE_RULE_COUNT - 1);
	memcpy(&block[4], "qa-seed-123", sizeof("qa-seed-123"));
	network_endless_adopt(block);
	qa_check(network_host_endless_run_mode == ENDLESS_RUNMODE_HARDCORE
	         && network_host_endless_chooser == ENDLESS_PICK_COUNT - 1
	         && network_host_endless_combo_shared
	         && network_host_endless_base_rule == ENDLESS_BASE_RULE_COUNT - 1
	         && strcmp(network_endless_session_seed, "qa-seed-123") == 0,
	         "joiner adopts every field of the host's Endless lobby block");

	memset(block, 0xEE, sizeof(block));
	network_endless_adopt(block);
	qa_check(network_host_endless_run_mode == ENDLESS_RUNMODE_STANDARD
	         && network_host_endless_chooser == ENDLESS_PICK_HOST
	         && network_host_endless_base_rule == ENDLESS_BASE_VARIED,
	         "out-of-range Endless lobby bytes are clamped to their defaults");

	bool seedScrubbed = network_endless_session_seed[NET_ENDLESS_SEED_MAX - 1] == '\0'
	                 && network_endless_session_seed[0] != '\0';
	for (int i = 0; network_endless_session_seed[i] != '\0'; ++i)
		seedScrubbed = seedScrubbed && network_endless_session_seed[i] == '?';
	qa_check(seedScrubbed,
	         "a hostile Endless seed is scrubbed to printable characters and terminated");

	network_host_endless_seed[0] = '\0';
	network_endless_session_begin();
	qa_check(network_endless_session_seed[0] != '\0',
	         "a blank lobby seed rolls a session seed instead of hashing the empty string");

	SDL_strlcpy(network_host_endless_seed, "fixed", sizeof(network_host_endless_seed));
	network_endless_session_begin();
	qa_check(strcmp(network_endless_session_seed, "fixed") == 0
	         && strcmp(network_host_endless_seed, "fixed") == 0,
	         "a named lobby seed is carried into the session verbatim");

	network_host_endless_run_mode = savedMode;
	network_host_endless_chooser = savedChooser;
	network_host_endless_combo_shared = savedCombo;
	network_host_endless_base_rule = savedBaseRule;
	memcpy(network_endless_session_seed, savedSeed, sizeof(savedSeed));
	memcpy(network_host_endless_seed, savedHostSeed, sizeof(savedHostSeed));
#else
	qa_check(true, "Endless lobby block skipped without networking");
#endif
}

static void qa_test_resync_serialization(void)
{
#ifdef WITH_NETWORK
	static Uint8 raw[4096], packed[8192], expanded[4096];
	Uint32 rng = 0x51a7e123u;

	for (unsigned pass = 0; pass < 256; ++pass)
	{
		watchdog_heartbeat();
		const size_t n = 1 + qa_prng(&rng) % sizeof(raw);
		for (size_t i = 0; i < n; ++i)
		{
			const Uint32 r = qa_prng(&rng);
			raw[i] = (pass % 3 == 0 && (r & 7) != 0) ? 0 : (Uint8)r;
		}

		const size_t compressed = nrb_resync_compress(raw, n, packed, sizeof(packed));
		const size_t restored = compressed == 0 ? 0
		                      : nrb_resync_expand(packed, compressed, expanded, sizeof(expanded));
		qa_check(compressed != 0 && restored == n && memcmp(raw, expanded, n) == 0,
		         "resync compression round-trips arbitrary state");

		if (compressed > 1)
			qa_check(nrb_resync_expand(packed, compressed - 1, expanded, n) != n,
			         "truncated resync stream is rejected");
		qa_check(nrb_resync_expand(packed, compressed, expanded, n - 1) == 0,
		         "oversized resync expansion is rejected");
	}

	{
		const Uint8 zero_run[] = { 0, 0, 0 };
		const Uint8 huge_run[] = { 0, 0xff, 0xff };
		qa_check(nrb_resync_expand(zero_run, sizeof(zero_run), expanded, sizeof(expanded)) == 0,
		         "zero-length resync run is rejected");
		qa_check(nrb_resync_expand(huge_run, sizeof(huge_run), expanded, sizeof(expanded)) == 0,
		         "oversized resync run is rejected");
	}

	/* The packet parser must safely refuse every short header and randomized malformed body. */
	for (int len = 0; len < 48; ++len)
	{
		for (int i = 0; i < NET_PACKET_SIZE; ++i)
			packed[i] = (Uint8)qa_prng(&rng);
		nrb_handle_packet(packed, len);
	}
	for (unsigned pass = 0; pass < 512; ++pass)
	{
		const int len = (int)(qa_prng(&rng) % (NET_PACKET_SIZE + 1));
		for (int i = 0; i < NET_PACKET_SIZE; ++i)
			packed[i] = (Uint8)qa_prng(&rng);
		nrb_handle_packet(packed, len);
	}
	qa_check(true, "malformed rollback input packets are safely consumed");
#else
	qa_check(true, "resync serialization skipped without networking");
#endif
}

/* One sample of every modifier-derived combat parameter, memcmp-comparable (zeroed first, so
 * padding cannot differ). Floats are compared by bit pattern: parity means bit-identical. */
typedef struct
{
	EndlessScaling s;
	Uint32 gravity, gravityX, gravityY, moveScale;
	int scrollPct, genAdd, hitbox, contactElite, contactChamp;
	int shockElite, shockChamp, martyrElite, martyrChamp;
	int champFire, champDmg;
	unsigned staticDrain;
	Uint8 regenOff, regenFree, seeker, scrollActive;
} QaModParity;

static Uint32 qa_float_bits(float v)
{
	Uint32 bits;
	memcpy(&bits, &v, sizeof(bits));
	return bits;
}

static void qa_mod_parity_sample(int zone, Uint64 mods, uint fx, QaModParity *out)
{
	memset(out, 0, sizeof(*out));

	endlessActiveMods = mods;
	endlessPlayerMods[0] = endlessPlayerMods[1] = mods;
	endlessRunDepth = zone;
	difficultyLevel = DIFFICULTY_HARD;

	// Identical structural draws on every machine: the omni-gravity heading and the per-level
	// special-tier latches are seeded state, so reseed and reset before each sample.
	endlessReseed((Uint64)zone * 2);
	endlessResetZoneEffects();
	endlessResetElites();
	endlessSetFxPlayer(fx);

	endlessScalingSnapshot(zone, DIFFICULTY_HARD, mods, &out->s);
	out->gravity = qa_float_bits(endlessGravityDrift());
	out->gravityX = qa_float_bits(endlessGravityDriftX());
	out->gravityY = qa_float_bits(endlessGravityDriftY());
	out->moveScale = qa_float_bits(endlessMoveScale());
	out->scrollPct = endlessScrollBoostPercent();
	out->genAdd = (int)endlessGeneratorPowerAdd(4);
	out->hitbox = endlessHitboxScale(12);
	out->contactElite = endlessEliteContactPercent(2);
	out->contactChamp = endlessEliteContactPercent(3);
	out->shockElite = endlessShockwaveRadius(5, 2);
	out->shockChamp = endlessShockwaveRadius(6, 3);
	out->martyrElite = endlessMartyrdomBurstShots(7, 2);
	out->martyrChamp = endlessMartyrdomBurstShots(8, 3);
	out->champFire = endlessChampionFireDelayPercent();
	out->champDmg = endlessChampionShotDamagePercent();
	out->staticDrain = endlessStaticDischargeDrain(10);
	out->regenOff = endlessShieldRegenOff() ? 1 : 0;
	out->regenFree = endlessShieldRegenFree() ? 1 : 0;
	out->seeker = endlessSeekerActive() ? 1 : 0;
	out->scrollActive = endlessScrollBoostActive() ? 1 : 0;
}

/* Every sector modifier's derived combat parameters, identical whichever machine computes them:
 * both thisPlayerNum values, both isNetworkGame values, both host roles, and both fx players. A
 * lever that read local-only state here would tilt one machine's simulation and desync the pair
 * a frame later. */
static void qa_test_modifier_online_parity(void)
{
	const JE_boolean savedNet = isNetworkGame;
	const bool savedHost = network_is_host;
	const uint savedThis = thisPlayerNum;
	const bool savedEndless = endlessMode, savedCoop = coopEndlessMode, savedTwo = twoPlayerMode;
	const Uint64 savedActive = endlessActiveMods;
	Uint64 savedPlayerMods[2];
	const int savedDepth = endlessRunDepth;
	const JE_shortint savedDifficulty = difficultyLevel;
	memcpy(savedPlayerMods, endlessPlayerMods, sizeof(savedPlayerMods));

	endlessMode = true;
	unsigned rows = 0;

	for (unsigned m = 0; m < COUNTOF(endlessModTable); ++m)
	{
		Uint64 mods = endlessModTable[m].bit;
		if (mods == ENDLESS_MOD_GRAVITY_OMNI)
			mods |= ENDLESS_MOD_GRAVITY;   // omni is a heading for GRAVITY, never alone

		static const int zones[] = { 6, 64 };
		for (unsigned z = 0; z < COUNTOF(zones); ++z)
		for (uint fx = 0; fx < 2; ++fx)
		{
			QaModParity want;
			bool haveWant = false;
			bool same = true;

			for (int net = 0; net <= 1; ++net)
			for (uint machine = 1; machine <= 2; ++machine)
			{
				isNetworkGame = net != 0;
				coopEndlessMode = net != 0;
				twoPlayerMode = net != 0;
				thisPlayerNum = machine;
				network_is_host = machine == 1;

				QaModParity got;
				qa_mod_parity_sample(zones[z], mods, fx, &got);
				if (!haveWant)
				{
					want = got;
					haveWant = true;
				}
				else
				{
					same &= memcmp(&want, &got, sizeof(got)) == 0;
				}
			}

			if (!same || !haveWant)
			{
				char label[160];
				snprintf(label, sizeof(label),
				         "modifier %016llx (zone %d, ship %u) derives identically on "
				         "every machine and mode",
				         (unsigned long long)mods, zones[z], fx);
				qa_check(false, label);
			}
			++rows;
		}
	}
	qa_check(rows > 0, "modifier online-parity matrix covered the registry");
	printf("# modifier online parity: %u modifier/zone/ship rows\n", rows);

	memcpy(endlessPlayerMods, savedPlayerMods, sizeof(savedPlayerMods));
	endlessActiveMods = savedActive;
	endlessRunDepth = savedDepth;
	difficultyLevel = savedDifficulty;
	twoPlayerMode = savedTwo;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	thisPlayerNum = savedThis;
	network_is_host = savedHost;
	isNetworkGame = savedNet;
	endlessSetFxPlayer(0);
}

/* Sidekick simulation counters across rollback: ammo, refill, charge, the satellite angle, the
 * attachment latches, and the linked-pair state all live in registered state and must survive a
 * snapshot restore exactly. A counter outside the registry replays wrong after a correction. */
static void qa_test_sidekick_rollback_state(void)
{
	const Player savedPlayer = player[0];
	const JE_boolean savedLinked = twoPlayerLinked;
	const JE_real savedDirec = linkGunDirec;

	player[0].sidekick[0].ammo = 7;
	player[0].sidekick[0].ammo_refill_ticks = 13;
	player[0].sidekick[1].charge = 4;
	player[0].sidekick[1].charge_ticks = 9;
	player[0].option_satellite_rotate = 1.25f;
	player[0].option_attachment_move[0] = -3;
	player[0].option_attachment_linked[0] = true;
	player[0].option_attachment_return[1] = true;
	twoPlayerLinked = true;
	linkGunDirec = 2.5;

	rollback_snapshot(0x51DEu);
	player[0].sidekick[0].ammo = 1;
	player[0].sidekick[0].ammo_refill_ticks = 0;
	player[0].sidekick[1].charge = 0;
	player[0].sidekick[1].charge_ticks = 0;
	player[0].option_satellite_rotate = 0.0f;
	player[0].option_attachment_move[0] = 5;
	player[0].option_attachment_linked[0] = false;
	player[0].option_attachment_return[1] = false;
	twoPlayerLinked = false;
	linkGunDirec = 0.0;

	qa_check(rollback_restore(0x51DEu)
	         && player[0].sidekick[0].ammo == 7
	         && player[0].sidekick[0].ammo_refill_ticks == 13
	         && player[0].sidekick[1].charge == 4
	         && player[0].sidekick[1].charge_ticks == 9
	         && player[0].option_satellite_rotate == 1.25f
	         && player[0].option_attachment_move[0] == -3
	         && player[0].option_attachment_linked[0]
	         && player[0].option_attachment_return[1]
	         && twoPlayerLinked
	         && linkGunDirec == 2.5,
	         "sidekick ammo, charge, satellite, attachment, and link state survive a rollback");

	player[0] = savedPlayer;
	twoPlayerLinked = savedLinked;
	linkGunDirec = savedDirec;
}

/* Arcade specifics: the purple-ball economy bounds, the link-gun weapon table, and the split
 * two-player gauge geometry against the wipe region it must stay inside. */
static void qa_test_arcade_matrices(void)
{
	const Player savedPlayer = player[0];
	char label[128];

	// The purple-ball counter follows the life table for every reachable life count and the
	// power ceiling holds; a maxed gun pays cash instead of an out-of-range power step.
	Player p = player[0];
	Uint8 lives;
	p.lives = &lives;
	bool ballBounds = true;
	for (lives = 0; lives <= ARCADE_LIVES_MAX + 1; ++lives)
	{
		calc_purple_balls_needed(&p);
		ballBounds &= p.purple_balls_needed >= 1 && p.purple_balls_needed <= 50;
	}
	qa_check(ballBounds, "purple-ball pricing stays inside its table for every life count");

	lives = 5;
	p.items.weapon[FRONT_WEAPON].id = 1;
	p.items.weapon[FRONT_WEAPON].power = 11;
	p.cash = 0;
	p.is_dragonwing = false;
	p.purple_balls_needed = 1;
	handle_got_purple_ball(&p);
	qa_check(p.items.weapon[FRONT_WEAPON].power == 11 && p.cash == 1000,
	         "a purple ball at maximum power pays cash instead of a power step");
	p.purple_balls_needed = 3;
	handle_got_purple_ball(&p);
	qa_check(p.purple_balls_needed == 2 && p.cash == 1000,
	         "an unearned purple ball decrements the counter and pays nothing");

	bool linkTable = true;
	for (unsigned i = 0; i < COUNTOF(linkGunWeapons); ++i)
		linkTable &= linkGunWeapons[i] <= WEAP_NUM;
	qa_check(linkTable, "every link-gun rear weapon maps to a real weapon entry");

	// Split-HUD gauge geometry: a full two-player gauge (21 units, one row of top pad) must
	// stay inside the 45-row band the wipe clears; painted rows outside it would never be
	// erased. Painted directly at the two player strides and measured off the pixels.
	if (VGAScreen != NULL && VGAScreen->format->BytesPerPixel == 1)
	{
		for (int stride = 0; stride < 2; ++stride)
		{
			const int x = 100, y = 60 + 134 * stride;
			Uint8 *const pixels = (Uint8 *)VGAScreen->pixels;

			for (int row = y - 60; row <= y + 2; ++row)
				memset(pixels + (size_t)row * VGAScreen->pitch + x - 2, 0, 14);

			JE_dBar3(VGAScreen, x, y, 21, 144, 0, 0, 1);

			int top = -1, bottom = -1;
			for (int row = y - 60; row <= y + 2; ++row)
			{
				bool any = false;
				for (int col = x; col <= x + 8; ++col)
					any |= pixels[(size_t)row * VGAScreen->pitch + col] != 0;
				if (any)
				{
					if (top < 0)
						top = row;
					bottom = row;
				}
			}

			snprintf(label, sizeof(label),
			         "player %d split gauge paints exactly the 45-row band the wipe clears",
			         stride + 1);
			qa_check(top == y - 44 && bottom == y, label);
		}
	}

	player[0] = savedPlayer;
}

/* Every replacement icon top has to exist in its sheet and fit the space the ship half leaves. */
static void qa_test_special_icon_tops(void)
{
	if (spriteSheet8.data == NULL || spriteSheet12.data == NULL)
		return;  // shape tables not loaded

	const bool saved = unusedShopSprites;
	unusedShopSprites = true;

	int replaced = 0;
	for (uint id = 1; id <= SPECIAL_NUM; ++id)
	{
		JE_word top = 0;
		const Sprite2_array *const sheet = JE_specialIconTop((JE_byte)id, &top);
		if (sheet == NULL)
			continue;
		++replaced;

		int x0, y0, x1, y1;
		char label[128];
		snprintf(label, sizeof(label), "special %u icon top fits the ship half's %dx%d",
		         id, HUD_SPECIAL_ICON_W, HUD_SPECIAL_ICON_H / 2);
		qa_check(sprite2_ink_bounds(*sheet, top, &x0, &y0, &x1, &y1)
		         && x1 - x0 < HUD_SPECIAL_ICON_W && y1 - y0 < HUD_SPECIAL_ICON_H / 2, label);
	}

	qa_check(replaced == 11, "eleven specials take a replacement icon top");
	printf("# special icon tops: %d replacements\n", replaced);

	/* Dragon Lightning has a whole spare icon to take, so it swaps itemgraphic instead of taking a
	 * rebuilt top, and only it moves: Lightning Zone keeps the icon the two shipped sharing. */
	JE_applyUnusedShopSprites();
	const JE_word onDragon = special[48].itemgraphic;
	const JE_word onZone = special[40].itemgraphic;

	unusedShopSprites = false;
	JE_applyUnusedShopSprites();
	qa_check(special[48].itemgraphic == special[40].itemgraphic,
	         "Dragon Lightning ships sharing Lightning Zone's HUD icon");
	qa_check(onDragon != onZone && onZone == special[40].itemgraphic,
	         "Unused Sprites gives Dragon Lightning its own icon and leaves Lightning Zone's");

	/* blit_sprite2x2 draws gr, gr+1, gr+19 and gr+20. sprite2_is_blank also reports an index the
	 * sheet does not hold, the out-of-range icon endlessGrantSpecial and the debug menu exclude. */
	static const unsigned int blockOffsets[4] = { 0, 1, 19, 20 };
	bool painted = true;
	for (unsigned int i = 0; i < COUNTOF(blockOffsets); ++i)
		painted = painted && !sprite2_is_blank(spriteSheet10, onDragon + blockOffsets[i]);
	qa_check(painted, "the spare icon it takes is four sprites the sheet holds and paints");

	JE_word unusedTop = 0;
	qa_check(JE_specialIconTop(41, &unusedTop) == NULL,
	         "Unused Sprites off leaves every special drawing its shipped icon");

	unusedShopSprites = saved;
	JE_applyUnusedShopSprites();
}

/* What the health bars divide by. Boss armor varies: the difficulty curve scales it at spawn and
 * level scripts arm boss groups at their own values, so both bars have to measure a wound against
 * the armor that part actually started with. */
static void qa_test_health_bar_scale(void)
{
	struct JE_SingleEnemyType part = { 0 };

	part.armorleft = 100;
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 100, "an undamaged enemy takes its armor as full health");

	part.armorleft = 20;  /* a script event may arm a group well below its spawn armor */
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 20, "a rewrite before the first wound replaces the full value");

	part.healthbar_seen = true;
	part.armorleft = 8;
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 20, "a wounded enemy losing armor keeps its full value");

	part.armorleft = 254;
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 254, "a script healing a wounded enemy raises the full value");

	part.armorleft = 255;
	enemy_note_full_armor(&part);
	qa_check(part.healthbar_max == 254, "the invincible sentinel is not a health value");

	qa_check(boss_bar_fill(254, 254) == BOSS_BAR_FULL && boss_bar_fill(100, 100) == BOSS_BAR_FULL,
	         "a boss bar starts full whatever armor its boss was given");
	qa_check(boss_bar_fill(50, 100) == 127, "half a 100-armor boss draws half a bar");
	qa_check(boss_bar_fill(1, 254) == 1, "one armor point left still draws a sliver");
	qa_check(boss_bar_fill(255, 0) == BOSS_BAR_FULL && boss_bar_fill(60, 0) == BOSS_BAR_FULL,
	         "an invincible phase and an unestablished full value both draw full");

	/* The bar reads the most-damaged part, so it has to divide by that part's own full value. */
	static const struct { JE_byte avail, armor, full; } parts[] = {
		{ 0, 200, 254 },  /* an undamaged hull */
		{ 0,  60, 100 },  /* the most-damaged part: 60 of the 100 it flew in with */
		{ 1,   1, 254 },  /* destroyed, so the survey has to skip it */
	};
	const JE_byte link = 42;
	JE_byte savedAvail[COUNTOF(parts)];
	struct JE_SingleEnemyType savedEnemy[COUNTOF(parts)];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));

	for (uint i = 0; i < COUNTOF(parts); ++i)
	{
		savedAvail[i] = enemyAvail[i];
		enemyAvail[i] = parts[i].avail;
		enemy[i].linknum = link;
		enemy[i].armorleft = parts[i].armor;
		enemy[i].healthbar_max = parts[i].full;
	}

	unsigned int armor = 0, full = 0;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor == 60 && full == 100,
	         "the survey pairs the most-damaged part with its own full value");
	qa_check(boss_bar_fill(armor, full) == 152, "which fills 60% of the bar");

	enemyAvail[0] = 1;
	enemyAvail[1] = 1;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor > 255, "a group with no live parts left reports the boss gone");

	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	for (uint i = 0; i < COUNTOF(parts); ++i)
		enemyAvail[i] = savedAvail[i];
}

/* Item data points one of the Flying Punch's bolts (weapon 794, `sg[0]`) at People Pretzels'
 * sprite, so the load pass redirects it. Every frame that bolt draws has to be blank. */
static void qa_test_flying_punch_bolt(void)
{
	const JE_word sg = weapons[794].sg[0];
	if (spriteSheet12.data == NULL || sg == 0)
		return;  // shape tables or item data not loaded

	qa_check(sg > 500 && sg <= 1000, "the Flying Punch's center bolt draws from spriteSheet12");

	for (JE_word ani = 0; ani <= weapons[794].weapani; ++ani)
	{
		int x0, y0, x1, y1;
		char label[96];
		snprintf(label, sizeof(label), "Flying Punch center bolt frame %u paints nothing", ani + 1u);
		qa_check(!sprite2_ink_bounds(spriteSheet12, sg - 500 + ani, &x0, &y0, &x1, &y1), label);
	}
}

/* The Dragonwing's ships[] row is synthesized (no episode table carries one), so its invariants
 * live here: the graphic-0 sentinel, a hull and price between the Gencore Maelstrom and the
 * MicroCorp Stalker, the id clamps resolving the row instead of the entry-0 fallback, and the
 * co-op seat that must not be taken for the linked pair's rear bay. */
static void qa_test_dragonwing_row(void)
{
	const JE_byte hull = ships[SHIP_DRAGONWING].dmg;
	const JE_word cost = ships[SHIP_DRAGONWING].cost;

	qa_check(ships[SHIP_DRAGONWING].name[0] != '\0' && ships[SHIP_DRAGONWING].shipgraphic == 0,
	         "the Dragonwing row exists and keeps the two-piece hull sentinel");

	qa_check(hull > ships[4].dmg && hull < ships[5].dmg
	         && cost > ships[4].cost && cost < ships[5].cost,
	         "the Dragonwing's hull and price sit between the Gencores and the Stalkers");

	// Earlier tests may leave a price or hull scaling mode on; these want the raw table values.
	const JE_boolean savedEndless = endlessMode, savedExpert = expertMode;
	const JE_boolean savedTwo = twoPlayerMode;
	const bool savedCoop = coopCampaignMode, savedCoopEndless = coopEndlessMode;
	endlessMode = false;
	expertMode = false;

	qa_check(JE_getCost(2, SHIP_DRAGONWING) == cost,
	         "the shop prices the Dragonwing row, not the out-of-table fallback");

	const Player saved0 = player[0], saved1 = player[1];
	const JE_word savedGr = shipGr, savedGr2 = shipGr2;
	const uint savedPowerAdd = powerAdd;
	Sprite2_array *const savedGrPtr = shipGrPtr, *const savedGr2Ptr = shipGr2ptr;

	player[0].items.ship = SHIP_DRAGONWING;
	JE_getShipInfo();
	qa_check(player[0].armor == hull && shipGr == 0 && shipGrPtr == &spriteSheet9,
	         "a seat flying the bought Dragonwing gets its hull and the sentinel graphic");

	/* Graphic 0 is also how the linked pair marks its rear bay, which owns a fixed hull and no ship
	 * of its own. A second seat that bought the Dragonwing has to resolve through the two-ship path
	 * instead, or the two machines would fly different armor. */
	twoPlayerMode = true;
	coopCampaignMode = true;
	coopEndlessMode = false;
	player[1].items.ship = SHIP_DRAGONWING;
	JE_getShipInfo();
	qa_check(shipGr2 == 0 && player[1].hull_armor == hull,
	         "a co-op seat two flying the Dragonwing keeps its own hull, not the linked bay's");

	shipGrPtr = savedGrPtr;
	shipGr2ptr = savedGr2Ptr;
	shipGr = savedGr;
	shipGr2 = savedGr2;
	powerAdd = savedPowerAdd;
	player[0] = saved0;
	player[1] = saved1;
	coopEndlessMode = savedCoopEndless;
	coopCampaignMode = savedCoop;
	twoPlayerMode = savedTwo;
	expertMode = savedExpert;
	endlessMode = savedEndless;
}

/* Painted x-extent of one 2x2 hull piece (cells at +0 and +12 within it) shifted by xOff, folded
 * into an accumulating min/max that `painted` marks as already holding a value. */
static bool qa_fold_2x2_ink(Sprite2_array sheet, unsigned int index, int xOff, bool painted,
                            int *left, int *right)
{
	static const struct { unsigned int add; int dx; } cells[] = {
		{0, 0}, {1, 12}, {19, 0}, {20, 12},
	};

	for (uint c = 0; c < COUNTOF(cells); ++c)
	{
		int x0, y0, x1, y1;
		if (!sprite2_ink_bounds(sheet, index + cells[c].add, &x0, &y0, &x1, &y1))
			continue;

		const int l = xOff + cells[c].dx + x0, r = xOff + cells[c].dx + x1;
		if (!painted || l < *left)
			*left = l;
		if (!painted || r > *right)
			*right = r;
		painted = true;
	}
	return painted;
}

/* The Nort Ship and Dragonwing paint 48px wide against the item list's 24px icon column, so each
 * takes a shifted anchor and a label column of its own. Both follow from the sprite ink, so
 * recompute them: the hull must sit inside the column, and its label must leave the same gap the
 * tightest single-2x2 hull leaves at the fixed label column. */
static void qa_test_wide_hull_columns(void)
{
	if (spriteSheet9.data == NULL)
		return;  // shape tables not loaded

	// The tightest clearance a normal hull leaves is the gap the wide ones have to match.
	int gap = -1;
	for (uint ship = 1; ship <= SHIP_NUM; ++ship)
	{
		const JE_word gr = ships[ship].shipgraphic;
		if (gr <= 1)
			continue;  // the two-piece hulls, measured below

		const bool t2000 = gr > 500;
		int left = 0, right = 0;
		if (!qa_fold_2x2_ink(t2000 ? spriteSheetT2000 : spriteSheet9, t2000 ? gr - 500 : gr,
		                     0, false, &left, &right))
			continue;

		const int shipGap = SHOP_ITEM_NAME_X - (SHOP_ITEM_ICON_X + right) - 1;
		if (gap < 0 || shipGap < gap)
			gap = shipGap;
	}
	qa_check(gap >= 0, "the item list's normal ship hulls clear its label column");
	if (gap < 0)
		return;

	// Left and right half indices JE_drawItem blits, by ship.
	static const struct { const char *name; JE_word ship; unsigned int left, right; } hulls[] = {
		{ "Dragonwing", SHIP_DRAGONWING, 13, 51 },
		{ "Nort Ship",  12,             220, 222 },
	};

	for (uint i = 0; i < COUNTOF(hulls); ++i)
	{
		char label[128];
		int inkLeft = 0, inkRight = 0;
		bool painted = qa_fold_2x2_ink(spriteSheet9, hulls[i].left, 0, false, &inkLeft, &inkRight);
		painted = qa_fold_2x2_ink(spriteSheet9, hulls[i].right, 24, painted, &inkLeft, &inkRight);
		if (!painted)
		{
			snprintf(label, sizeof(label), "the %s hull paints something", hulls[i].name);
			qa_check(false, label);
			continue;
		}

		// JE_drawItem straddles its anchor, so the left half lands a half-width before it.
		const ShopItemColumns cols = shop_ship_item_columns(hulls[i].ship);
		const int boxLeft = cols.iconX - SHOP_WIDE_HULL_HALF;

		snprintf(label, sizeof(label), "the %s hull starts at the icon column", hulls[i].name);
		qa_check(boxLeft == SHOP_ITEM_ICON_X && boxLeft + inkLeft >= SHOP_ITEM_ICON_X, label);

		snprintf(label, sizeof(label), "the %s label clears its own hull", hulls[i].name);
		qa_check(cols.nameX == boxLeft + inkRight + 1 + gap, label);

		snprintf(label, sizeof(label), "the %s cost row keeps the label offset", hulls[i].name);
		qa_check(cols.costX - cols.nameX == SHOP_ITEM_COST_X - SHOP_ITEM_NAME_X, label);

		printf("# %s hull: ink %d..%d, label x %d\n", hulls[i].name, inkLeft, inkRight, cols.nameX);
	}
	printf("# wide shop hulls clear their labels by %dpx\n", gap);
}

/* A weapon row tags a two-mode port, or a gun Endless stocked for the other bay, after its cost,
 * in a column that has to clear both the cost text and the owned marker the same row can carry.
 * Endless scales prices, so it is measured at both multiplier caps as well as at the shipped
 * price. Dual-Mode is the widest tag, so it is the one the column has to fit. */
static void qa_test_dual_mode_tag(void)
{
	const int tagW = JE_textWidth(SHOP_DUAL_MODE_TAG, TINY_FONT);
	if (tagW <= 0)
		return;  // font bank not loaded

	qa_check(JE_textWidth(SHOP_FRONT_GUN_TAG, TINY_FONT) <= tagW
	         && JE_textWidth(SHOP_REAR_GUN_TAG, TINY_FONT) <= tagW,
	         "the bay tags fit the column the Dual-Mode tag sizes");

	// The ports the tag marks, and the priciest of them.
	uint dual = 0;
	JE_word base = 0;
	for (uint port = 1; port <= PORT_NUM; ++port)
	{
		if (weaponPort[port].opnum != 2)
			continue;
		++dual;
		base = MAX(base, weaponPort[port].cost);
	}
	qa_check(dual > 0 && base > 0, "the item data has two-mode ports for the rear list to tag");
	if (dual == 0)
		return;

	// Multipliers JE_getCost can stack on a shipped price: Endless depth caps at 100x and the
	// expert Shop Cost knob at 20x.
	static const struct { const char *what; ulong mult; } prices[] = {
		{ "campaign",       1 },
		{ "deep Endless",   100 },
		{ "the price cap",  100 * 20 },
	};

	for (uint i = 0; i < COUNTOF(prices); ++i)
	{
		char buf[32], label[128];
		snprintf(buf, sizeof(buf), "Cost: %lu", (ulong)base * prices[i].mult);

		// The row that carries the marker is the tighter case, so measure against that column.
		const int markerX = SHOP_ITEM_MARKER_X(true);
		const int costRight = SHOP_ITEM_COST_X + JE_textWidth(buf, TINY_FONT);
		const int tagX = shop_row_tag_x(costRight, tagW, markerX);

		snprintf(label, sizeof(label), "the Dual-Mode tag clears a %s price", prices[i].what);
		qa_check(tagX >= costRight, label);

		snprintf(label, sizeof(label), "the Dual-Mode tag stays in the row at a %s price",
		         prices[i].what);
		qa_check(tagX + tagW <= SHOP_ITEM_LIST_RIGHT, label);

		printf("# Dual-Mode tag, %s price \"%s\": cost ends %d, tag %d..%d\n",
		       prices[i].what, buf, costRight, tagX, tagX + tagW);
	}
	printf("# Dual-Mode tag: %u two-mode ports, %dpx wide, column ends %d\n",
	       dual, tagW, SHOP_ITEM_MARKER_X(true) - SHOP_ROW_TAG_MARKER_GAP);

	// The same row can carry the owned marker, whose icon paints from the marker column.
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // the shop loads this bank lazily

	int inkX0, inkY0, inkX1, inkY1;
	if (sprite2_ink_bounds(shopSpriteSheet, SHOP_OWNED_MARKER_SPRITE,
	                       &inkX0, &inkY0, &inkX1, &inkY1))
	{
		const int markerX = SHOP_ITEM_MARKER_X(true);
		const int tagRight = markerX - SHOP_ROW_TAG_MARKER_GAP;

		qa_check(tagRight < markerX + inkX0, "a row tag clears the owned marker icon");
		printf("# owned marker: icon ink %d..%d, tag column ends %d\n",
		       markerX + inkX0, markerX + inkX1, tagRight);
	}
}

// Tags are compared by text: each translation unit gets its own copy of the macro's literal.
static bool qa_tag_is(const char *tag, const char *want)
{
	if (tag == NULL || want == NULL)
		return tag == want;
	return strcmp(tag, want) == 0;
}

/* Endless deals both weapon lists from one id pool, so each list marks the guns the shipped game
 * issues for the other bay. The mark only means something if every real port names one bay. A
 * campaign shop fills its two lists from separate data, so it must never carry the mark. */
static void qa_test_weapon_bay_tags(void)
{
	uint front = 0, rear = 0, unclassified = 0;

	for (uint port = 1; port <= SHOP_REAL_WEAPON_PORTS; ++port)
	{
		char label[96];
		const ShopWeaponBay bay = shop_weapon_port_bay(port);
		const bool dualMode = weaponPort[port].opnum == 2;

		switch (bay)
		{
		case SHOP_BAY_FRONT:
			++front;
			break;
		case SHOP_BAY_REAR:
			++rear;
			break;
		default:
			++unclassified;
			break;
		}

		// A two-mode port has to be a rear gun, or its tag and a bay tag want the same column.
		snprintf(label, sizeof(label), "port %u (%.16s) has a second mode only as a rear gun",
		         port, weaponPort[port].name);
		qa_check(!dualMode || bay == SHOP_BAY_REAR, label);

		const char *const wantRear = dualMode ? SHOP_DUAL_MODE_TAG
		                           : bay == SHOP_BAY_FRONT ? SHOP_FRONT_GUN_TAG : NULL;

		snprintf(label, sizeof(label), "port %u is tagged only where it is out of place", port);
		qa_check(qa_tag_is(shop_weapon_row_tag(port, false, true),
		                   bay == SHOP_BAY_REAR ? SHOP_REAR_GUN_TAG : NULL)
		         && qa_tag_is(shop_weapon_row_tag(port, true, true), wantRear), label);

		snprintf(label, sizeof(label), "port %u carries no bay tag in a campaign shop", port);
		qa_check(qa_tag_is(shop_weapon_row_tag(port, false, false), NULL)
		         && qa_tag_is(shop_weapon_row_tag(port, true, false),
		                      dualMode ? SHOP_DUAL_MODE_TAG : NULL), label);
	}

	// Port 16 holds the sidekick weapon table; every other real port belongs to a bay.
	qa_check(front > 0 && rear > 0 && unclassified == 1, "the bay table covers the weapon ports");
	printf("# weapon bays: %u front, %u rear, %u unclassified of %d ports; tags %dpx / %dpx\n",
	       front, rear, unclassified, SHOP_REAL_WEAPON_PORTS,
	       JE_textWidth(SHOP_FRONT_GUN_TAG, TINY_FONT), JE_textWidth(SHOP_REAR_GUN_TAG, TINY_FONT));
}

/* The shield/armor damage glow is presentation state held out of the rollback registry, so it has
 * to step once per REAL tick and stay each ship's own. Both halves are easy to get wrong online:
 * a replay pass that steps it again spends a whole glow inside one displayed frame, and a shared
 * counter would have one ship's hit cutting the partner's glow short. */
static void qa_test_gauge_flash_lifetime(void)
{
	if (VGAScreenSeg == NULL)
		return;  // the repaint below paints the real HUD surface

	const bool savedResim = rollback_resim, savedDirty = hud_bars_dirty;
	const int savedShield[2] = { shieldGaugeFlash[0], shieldGaugeFlash[1] };
	const int savedArmor[2]  = { armorGaugeFlash[0],  armorGaugeFlash[1]  };

	shieldGaugeFlash[0] = shieldGaugeFlash[1] = 6;
	armorGaugeFlash[0]  = armorGaugeFlash[1]  = 0;

	rollback_resim = true;
	for (int i = 0; i < 4; ++i)
		JE_updateGaugeFlash();
	qa_check(shieldGaugeFlash[0] == 6 && shieldGaugeFlash[1] == 6,
	         "a rollback replay pass never spends a gauge flash");

	rollback_resim = false;
	JE_updateGaugeFlash();
	qa_check(shieldGaugeFlash[0] == 5 && shieldGaugeFlash[1] == 5,
	         "a live tick steps every ship's gauge flash exactly once");

	// Both ships hit at once, then only one of them: neither run touches the other's counter.
	shieldGaugeFlash[0] = 6;
	shieldGaugeFlash[1] = 0;
	JE_updateGaugeFlash();
	qa_check(shieldGaugeFlash[0] == 5 && shieldGaugeFlash[1] == 0,
	         "one ship's damage flash neither starts nor shortens the other's");

	rollback_resim = savedResim;
	shieldGaugeFlash[0] = savedShield[0]; shieldGaugeFlash[1] = savedShield[1];
	armorGaugeFlash[0]  = savedArmor[0];  armorGaugeFlash[1]  = savedArmor[1];
	hud_bars_dirty = savedDirty;
}

/* The two repair specials have to stay distinct where there are two hulls. stype 13 mends the ship
 * that fired; stype 14 is vanilla's repair-the-OTHER-hull special, so in co-op it mends the partner
 * and in the linked pair it stays on hull two. Driven through a scratch special slot so the test
 * does not depend on the shipped table being loaded. */
static void qa_test_partner_repair_special(void)
{
	const bool savedTwo = twoPlayerMode, savedCoop = coopCampaignMode, savedSep = arcadeSeparateMode;
	const Player saved0 = player[0], saved1 = player[1];
	const JE_byte savedStype = special[SPECIAL_NUM].stype, savedTemp2 = temp2;

	temp2 = 0;                      // heal is temp2 / 4 + 1, so one point
	special[SPECIAL_NUM].stype = 14;
	twoPlayerMode = true; coopCampaignMode = true; arcadeSeparateMode = false;

	for (uint p = 0; p < 2; ++p)
	{
		player[p].armor = 10;
		player[p].initial_armor = 50;
	}
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(player[1].armor == 11 && player[0].armor == 10,
	         "co-op: the partner-repair special mends the OTHER ship, not the firer");
	player[0].armor = player[1].armor = 10;   // reset, so the second seat is its own check
	JE_specialComplete(2, SPECIAL_NUM);
	qa_check(player[0].armor == 11 && player[1].armor == 10,
	         "...and mends ship one when ship two fires it");

	// stype 13 is the self-repair, and must not have become the same special.
	special[SPECIAL_NUM].stype = 13;
	player[0].armor = player[1].armor = 10;
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(player[0].armor == 11 && player[1].armor == 10,
	         "co-op: the self-repair special still mends the firer");

	// The linked pair shares one arsenal and has no partner ship, so hull two keeps it.
	coopCampaignMode = false;
	special[SPECIAL_NUM].stype = 14;
	player[0].armor = player[1].armor = 10;
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(player[1].armor == 11 && player[0].armor == 10,
	         "linked arcade: the partner-repair special stays on hull two");

	special[SPECIAL_NUM].stype = savedStype; temp2 = savedTemp2;
	twoPlayerMode = savedTwo; coopCampaignMode = savedCoop; arcadeSeparateMode = savedSep;
	player[0] = saved0; player[1] = saved1;
}

// Fire one twiddle through the real path, with the clocks and durations that gate it cleared.
static void qa_fire_twiddle(JE_byte pwr, uint *armor, uint *shield)
{
	special[SPECIAL_NUM].pwr = pwr;
	SFExecuted[0] = SPECIAL_NUM;
	shotRepeat[SHOT_SPECIAL] = 0;
	specialWait = 0;
	flareDuration = 0;
	flareStart = false;
	zinglonDuration = 0;
	astralDuration = 0;
	JE_resetTwiddleClocks();
	JE_doSpecialShot(1, armor, shield);
}

/* What a twiddle charges. Without Kinetic Converter every kind of charge has to deduct exactly what
 * it always did, the odd half-shield bar included; with the perk each deducts less, while temp2,
 * the magnitude JE_specialComplete reads, keeps the list price. Driven through a scratch special
 * slot, as the repair test above is. */
static void qa_test_twiddle_charges(void)
{
	if (VGAScreenSeg == NULL || game_screen == NULL)
		return;  // the charge path repaints the real shield and armor gauges

	const JE_boolean savedEndless = endlessMode;
	const JE_boolean savedMods = endlessCampaignMods;
	const JE_boolean savedAuto = autoFireSpecial;
	const JE_boolean savedDbgAuto = debugAutofireTwiddle;
	const JE_boolean savedTrigger = debugTwiddleTrigger;
	const JE_boolean savedFlareStart = flareStart;
	const JE_byte savedDbgTwiddle = debugTwiddleSpecial;
	const JE_byte savedSF = SFExecuted[0];
	const JE_byte savedStype = special[SPECIAL_NUM].stype;
	const JE_byte savedPwr = special[SPECIAL_NUM].pwr;
	const JE_byte savedTemp = temp;
	const JE_byte savedTemp2 = temp2;
	const JE_byte savedWait = specialWait;
	const JE_byte savedRepeat = shotRepeat[SHOT_SPECIAL];
	const JE_byte savedZing = zinglonDuration;
	const JE_byte savedAstral = astralDuration;
	const JE_word savedFlare = flareDuration;
	const Player saved0 = player[0];
	SDL_Surface *const savedVGA = VGAScreen;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	// The scratch slot takes an inert type: the effect switch has no case 0, so only the charge
	// runs. Clearing the equipped special keeps the other fire gates shut.
	special[SPECIAL_NUM].stype = 0;
	player[0].items.special = 0;
	autoFireSpecial = false;
	debugAutofireTwiddle = false;
	debugTwiddleTrigger = false;
	debugTwiddleSpecial = 0;
	endlessMode = false;
	endlessCampaignMods = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	// Each charge kind: the bars it starts with, the bars vanilla leaves behind, and the magnitude
	// JE_specialComplete has to be handed either way.
	static const struct {
		JE_byte pwr;
		uint startShield, startArmor;
		uint leftShield, leftArmor;
		JE_byte magnitude;
	} charges[] = {
		{  20, 50, 30, 30, 30, 20 },  // a fixed shield charge
		{  98, 41, 30,  0, 30, 41 },  // the whole bar, which is also the effect's size
		{  99, 41, 30, 20, 30, 20 },  // half of it, rounded as vanilla rounds an odd bar
		{ 104, 50, 30, 50, 26,  4 },  // a fixed armor charge
	};

	bool stock = true, cheaper = true, sameEffect = true;
	for (unsigned c = 0; c < COUNTOF(charges); ++c)
	{
		uint shield = charges[c].startShield, armor = charges[c].startArmor;
		qa_fire_twiddle(charges[c].pwr, &armor, &shield);
		stock &= shield == charges[c].leftShield && armor == charges[c].leftArmor
		      && temp2 == charges[c].magnitude;
	}
	qa_check(stock, "a twiddle charges its stock shield or armor without Kinetic Converter");

	endlessMode = true;
	endlessPerkTakenBy[0][PERK_KINETIC] = (JE_byte)endlessPerkMaxStack(PERK_KINETIC);
	endlessPerkRederive();

	for (unsigned c = 0; c < COUNTOF(charges); ++c)
	{
		uint shield = charges[c].startShield, armor = charges[c].startArmor;
		qa_fire_twiddle(charges[c].pwr, &armor, &shield);
		const uint spent[2] = { charges[c].startShield - shield, charges[c].startArmor - armor };
		const uint list[2] = { charges[c].startShield - charges[c].leftShield,
		                       charges[c].startArmor - charges[c].leftArmor };
		for (unsigned b = 0; b < COUNTOF(spent); ++b)
			cheaper &= (list[b] == 0) ? spent[b] == 0 : (spent[b] > 0 && spent[b] < list[b]);
		sameEffect &= temp2 == charges[c].magnitude;
	}
	qa_check(cheaper, "Kinetic Converter charges less for every twiddle, proportional ones too");
	qa_check(sameEffect, "...and the special still reads the magnitude its list price bought");

	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	special[SPECIAL_NUM].stype = savedStype;
	special[SPECIAL_NUM].pwr = savedPwr;
	SFExecuted[0] = savedSF;
	temp = savedTemp;
	temp2 = savedTemp2;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	specialWait = savedWait;
	flareDuration = savedFlare;
	flareStart = savedFlareStart;
	zinglonDuration = savedZing;
	astralDuration = savedAstral;
	autoFireSpecial = savedAuto;
	debugAutofireTwiddle = savedDbgAuto;
	debugTwiddleTrigger = savedTrigger;
	debugTwiddleSpecial = savedDbgTwiddle;
	endlessMode = savedEndless;
	endlessCampaignMods = savedMods;
	player[0] = saved0;
	VGAScreen = savedVGA;
}

/* One twiddle keystroke through the real detector. The codes are the keyboardCombos alphabet:
 * 1..4 are UP/DOWN/LEFT/RIGHT, 5..8 the same four with fire held, 9 everything released. The
 * detector reads a direction as the gap between where the ship is and where it came from, so each
 * code becomes a one-pixel offset. */
static void qa_twiddle_step(JE_byte playerNum, JE_byte code)
{
	enum { PX = 100, PY = 100 };
	const bool withFire = (code >= 5 && code <= 8);
	const JE_byte dir = withFire ? (JE_byte)(code - 4) : code;
	int mx = PX, my = PY;

	button[0] = withFire;

	if (dir == 1)
		my = PY + 1;  // up
	else if (dir == 2)
		my = PY - 1;  // down
	else if (dir == 3)
		mx = PX + 1;  // left
	else if (dir == 4)
		mx = PX - 1;  // right
	// any other code is 9, which holds nothing at all

	JE_SFCodes(playerNum, PX, PY, mx, my);
}

// Nothing entered on either seat.
static void qa_twiddle_clear(void)
{
	memset(SFCurrentCode, 0, sizeof(SFCurrentCode));
	memset(SFExecuted, 0, sizeof(SFExecuted));
}

// Perform one whole combo row and report the special it executed (0 if the ship refused it).
static JE_byte qa_perform_twiddle(JE_byte playerNum, JE_byte comboRow)
{
	qa_twiddle_clear();

	for (unsigned k = 0; k < COUNTOF(keyboardCombos[0]); ++k)
	{
		const JE_byte code = keyboardCombos[comboRow][k];
		if (code == 0 || code > 9)  // 0 pads the row, >100 is the terminator the detector consumes
			break;
		qa_twiddle_step(playerNum, code);
	}

	return SFExecuted[playerNum - 1];
}

/* Every ship reaches the twiddles its combo row lists, for either seat, and a ship with no row of
 * its own reaches none without reading past the table. */
static void qa_test_twiddle_ships(void)
{
	const JE_boolean savedSuper = superTyrian;
	const JE_boolean savedTwo = twoPlayerMode;
	const JE_boolean savedCoop = coopCampaignMode;
	const JE_boolean savedCoopEndless = coopEndlessMode;
	const JE_boolean savedSep = arcadeSeparateMode;
	const Player saved0 = player[0], saved1 = player[1];
	JE_byte savedCode[2][21];
	JE_byte savedExec[2];
	bool savedButton[4];
	memcpy(savedCode, SFCurrentCode, sizeof(savedCode));
	memcpy(savedExec, SFExecuted, sizeof(savedExec));
	memcpy(savedButton, button, sizeof(savedButton));

	superTyrian = false;
	twoPlayerMode = false;
	coopCampaignMode = false;
	coopEndlessMode = false;
	arcadeSeparateMode = false;

	bool everyRow = true, everySpecial = true;
	unsigned rowsChecked = 0;
	for (uint ship = 1; ship <= SHIP_NUM; ++ship)
	{
		player[0].items.ship = (Uint8)ship;
		for (unsigned slot = 0; slot < COUNTOF(shipCombos[0]); ++slot)
		{
			const JE_byte row = shipCombos[ship][slot];
			if (row == 0)
				continue;  // that slot is empty for this ship

			++rowsChecked;
			const JE_byte fired = qa_perform_twiddle(1, (JE_byte)(row - 1));
			everyRow &= fired != 0;
			everySpecial &= fired <= SPECIAL_NUM;
		}
	}
	// The floor guards against a loop that visits nothing; the table lists 34 rows today.
	qa_check(rowsChecked >= 30 && everyRow && everySpecial,
	         "every ship performs each twiddle its combo row lists");

	// The last two rows the table carries, checked by name so a bound that shortens names itself.
	player[0].items.ship = 15;  // Red Dragon
	const JE_byte dragon = qa_perform_twiddle(1, (JE_byte)(shipCombos[15][0] - 1));
	player[0].items.ship = 16;  // Gencore II
	const JE_byte gencore = qa_perform_twiddle(1, (JE_byte)(shipCombos[16][0] - 1));
	qa_check(dragon != 0 && gencore != 0,
	         "the last two ships in the table twiddle like the rest");

	// A shipedit "extra" ship has no row of its own, so it reaches no twiddle at all.
	player[0].items.ship = 91;
	qa_check(qa_perform_twiddle(1, 0) == 0 && qa_perform_twiddle(1, 25) == 0,
	         "a ship outside the combo table has no twiddles");

	// The Endless-sold Dragonwing collapses to the shared "2nd Player ship" row on any seat.
	player[0].items.ship = SHIP_DRAGONWING;
	const JE_byte boughtWing = qa_perform_twiddle(1, (JE_byte)(shipCombos[0][0] - 1));
	qa_check(boughtWing != 0, "a bought Dragonwing twiddles off the shared row");

	/* Seat two. The linked pair's rear half has no ship of its own and twiddles off row 0; every
	 * mode where it flies its own ship uses that ship's row. */
	twoPlayerMode = true;
	player[1].items.ship = 12;  // Nort Ship: seeker bombs, protron field, post-it
	const JE_byte linked = qa_perform_twiddle(2, (JE_byte)(shipCombos[0][0] - 1));
	const JE_byte linkedOwn = qa_perform_twiddle(2, (JE_byte)(shipCombos[12][0] - 1));
	coopCampaignMode = true;
	const JE_byte coopOwn = qa_perform_twiddle(2, (JE_byte)(shipCombos[12][0] - 1));
	qa_check(linked != 0 && linkedOwn == 0 && coopOwn != 0,
	         "player two twiddles off the shared row when linked and off its own ship in co-op");

	memcpy(button, savedButton, sizeof(button));
	memcpy(SFExecuted, savedExec, sizeof(SFExecuted));
	memcpy(SFCurrentCode, savedCode, sizeof(savedCode));
	player[0] = saved0;
	player[1] = saved1;
	arcadeSeparateMode = savedSep;
	coopEndlessMode = savedCoopEndless;
	coopCampaignMode = savedCoop;
	twoPlayerMode = savedTwo;
	superTyrian = savedSuper;
}

/* Every input path resolves a flick the same way. The detector ignores a tick offering it two
 * directions, so SF_twiddleTarget collapses a flick inside the 2:1 cone to its dominant axis and
 * keeps both axes for anything shallower, which the detector reads as a neutral tick. */
static void qa_test_twiddle_diagonals(void)
{
	enum { PX = 100, PY = 100 };

	// The detector mirrors the horizontal half on an upside-down screen, so pin the flag off for
	// every case that expects an unmirrored target.
	const JE_boolean savedInvert = smoothies[9-1];
	smoothies[9-1] = false;

	// One entry per shape a flick can take, with the target the cone has to produce.
	static const struct {
		int dx, dy;
		int wantX, wantY;
	} flicks[] = {
		{  0,  0, PX,     PY     },  // standing still
		{  3,  0, PX - 1, PY     },  // right
		{ -3,  0, PX + 1, PY     },  // left
		{  0,  3, PX,     PY - 1 },  // down
		{  0, -3, PX,     PY + 1 },  // up
		{  3,  1, PX - 1, PY     },  // mostly right
		{  1,  3, PX,     PY - 1 },  // mostly down
		{  5, -2, PX - 1, PY     },  // mostly right, drifting up
		{ -1, -4, PX,     PY + 1 },  // mostly up, drifting left
		{  3,  2, PX - 1, PY - 1 },  // inside the cone on neither axis: both survive
		{  2, -3, PX - 1, PY + 1 },
		{  2,  2, PX - 1, PY - 1 },  // an exact diagonal
		{ -2, -2, PX + 1, PY + 1 },
	};

	bool resolved = true;
	for (unsigned f = 0; f < COUNTOF(flicks); ++f)
	{
		int tx = 0, ty = 0;
		SF_twiddleTarget(PX, PY, flicks[f].dx, flicks[f].dy, &tx, &ty);
		resolved &= tx == flicks[f].wantX && ty == flicks[f].wantY;
	}
	qa_check(resolved, "a flick collapses inside the 2:1 cone and keeps both axes outside it");

	// An upside-down screen mirrors the horizontal half of every flick. The vertical half reaches
	// the helper already inverted, so only the expected x moves.
	smoothies[9-1] = true;
	bool mirrored = true;
	for (unsigned f = 0; f < COUNTOF(flicks); ++f)
	{
		int tx = 0, ty = 0;
		SF_twiddleTarget(PX, PY, flicks[f].dx, flicks[f].dy, &tx, &ty);
		mirrored &= tx == 2 * PX - flicks[f].wantX && ty == flicks[f].wantY;
	}
	qa_check(mirrored, "an upside-down screen mirrors a flick's horizontal half only");
	smoothies[9-1] = false;

	// A twiddle performed while the ship drifts sideways still registers.
	const Player saved0 = player[0];
	JE_byte savedCode[2][21];
	JE_byte savedExec[2];
	bool savedButton[4];
	memcpy(savedCode, SFCurrentCode, sizeof(savedCode));
	memcpy(savedExec, SFExecuted, sizeof(savedExec));
	memcpy(savedButton, button, sizeof(savedButton));

	// Gencore Phoenix's first combo row is Ice Blast: DOWN, then UP with fire held.
	enum { ICE_BLAST_SPECIAL = 42 };
	player[0].items.ship = 3;
	memset(SFCurrentCode, 0, sizeof(SFCurrentCode));
	SFExecuted[0] = 0;

	static const struct {
		int dx, dy;
		bool fire;
	} iceBlastAdrift[] = {
		{ -1,  3, false },  // down, drifting left
		{  1, -3, true  },  // up and fire, drifting right
	};
	for (unsigned s = 0; s < COUNTOF(iceBlastAdrift); ++s)
	{
		int tx = 0, ty = 0;
		button[0] = iceBlastAdrift[s].fire;
		SF_twiddleTarget(PX, PY, iceBlastAdrift[s].dx, iceBlastAdrift[s].dy, &tx, &ty);
		JE_SFCodes(1, PX, PY, tx, ty);
	}
	qa_check(SFExecuted[0] == ICE_BLAST_SPECIAL,
	         "a twiddle performed while drifting sideways still fires");

	/* A flick shallower than the cone is neutral. Ice Blast sits in ship 3's first combo slot, so
	 * SFCurrentCode[0][0] exposes its progress directly. */
	memset(SFCurrentCode, 0, sizeof(SFCurrentCode));
	SFExecuted[0] = 0;
	int tx = 0, ty = 0;
	button[0] = false;
	SF_twiddleTarget(PX, PY, 0, 3, &tx, &ty);  // down, the first Ice Blast step
	JE_SFCodes(1, PX, PY, tx, ty);
	const JE_byte afterStep = SFCurrentCode[0][0];
	SF_twiddleTarget(PX, PY, 3, 2, &tx, &ty);  // a wander outside the cone
	JE_SFCodes(1, PX, PY, tx, ty);
	qa_check(afterStep == 1 && SFCurrentCode[0][0] == 1 && SFExecuted[0] == 0,
	         "a flick outside the cone neither advances nor cancels a combo");
	button[0] = true;
	SF_twiddleTarget(PX, PY, 1, -3, &tx, &ty);  // up with fire, the second step
	JE_SFCodes(1, PX, PY, tx, ty);
	qa_check(SFExecuted[0] == ICE_BLAST_SPECIAL, "...and the combo still completes around it");

	memcpy(button, savedButton, sizeof(button));
	memcpy(SFExecuted, savedExec, sizeof(SFExecuted));
	memcpy(SFCurrentCode, savedCode, sizeof(savedCode));
	player[0] = saved0;
	smoothies[9-1] = savedInvert;
}

/* The direction a target carries, decoded as the top of JE_SFCodes decodes it: 1..4 for
 * UP/DOWN/LEFT/RIGHT, 0 where there is no single direction (the detector turns that into code 9
 * or a neutral return). Keep it in step with that decode. */
static int qa_twiddle_code(int px, int py, int tx, int ty)
{
	const int count = (ty > py) + (ty < py) + (px < tx) + (px > tx);
	if (count != 1)
		return 0;
	return (ty > py) * 1 + (ty < py) * 2 + (px < tx) * 3 + (px > tx) * 4;
}

/* The wire round trip. A peer never sees the displacement, only rb_move_bits, and rebuilds a
 * direction from them; the detector has to read that the way it reads the displacement itself,
 * upside down or not, or the two machines resolve one flick as two different codes. */
static void qa_test_twiddle_wire(void)
{
	enum { PX = 100, PY = 100 };
	const JE_boolean savedInvert = smoothies[9-1];
	const JE_boolean savedSuper = superTyrian;
	const Player saved0 = player[0];
	JE_byte savedCode[2][21];
	JE_byte savedExec[2];
	bool savedButton[4];
	memcpy(savedCode, SFCurrentCode, sizeof(savedCode));
	memcpy(savedExec, SFExecuted, sizeof(savedExec));
	memcpy(savedButton, button, sizeof(savedButton));

	// Every shape a tick's displacement can take, as rb_fill_tuple measures it.
	static const struct {
		int dx, dy;
	} flicks[] = {
		{  0,  0 }, {  1,  0 }, {  0, -1 }, { -1,  1 },
		{  3,  0 }, { -3,  0 }, {  0,  3 }, {  0, -3 },
		{  3,  1 }, {  1,  3 }, {  5, -2 }, { -1, -4 },
		{  3,  2 }, {  2, -3 }, {  2,  2 }, { -2, -2 },
	};

	bool agree = true;
	for (int inverted = 0; inverted <= 1; ++inverted)
	{
		smoothies[9-1] = inverted != 0;
		for (unsigned f = 0; f < COUNTOF(flicks); ++f)
		{
			int lx = 0, ly = 0, wx = 0, wy = 0, dx = 0, dy = 0;
			SF_twiddleTarget(PX, PY, flicks[f].dx, flicks[f].dy, &lx, &ly);
			rb_move_dir(rb_move_bits(flicks[f].dx, flicks[f].dy), &dx, &dy);
			SF_twiddleTarget(PX, PY, dx, dy, &wx, &wy);
			agree &= qa_twiddle_code(PX, PY, lx, ly) == qa_twiddle_code(PX, PY, wx, wy);
		}
	}
	qa_check(agree, "a flick reads as the same code from the wire as from its displacement, "
	                "upside down or not");

	/* A whole combo over the wire on an upside-down screen. The Nort ship's first row is Seeker
	 * Bombs (LEFT, RIGHT, DOWN with fire); the displacements below are the ones the classic reads
	 * hand the wire for the rotated keys, RIGHT, LEFT, UP with fire, since the vertical half is
	 * inverted before the tuple is filled and the horizontal one at the detector. */
	enum { SEEKER_BOMBS_SPECIAL = 39 };
	static const struct {
		int dx, dy;
		bool fire;
	} rotated[] = {
		{  1,  0, false },
		{ -1,  0, false },
		{  0,  1, true  },
	};
	superTyrian = false;
	player[0].items.ship = 12;

	for (int inverted = 1; inverted >= 0; --inverted)
	{
		smoothies[9-1] = inverted != 0;
		memset(SFCurrentCode, 0, sizeof(SFCurrentCode));
		SFExecuted[0] = 0;
		for (unsigned s = 0; s < COUNTOF(rotated); ++s)
		{
			int dx = 0, dy = 0, tx = 0, ty = 0;
			button[0] = rotated[s].fire;
			rb_move_dir(rb_move_bits(rotated[s].dx, rotated[s].dy), &dx, &dy);
			SF_twiddleTarget(PX, PY, dx, dy, &tx, &ty);
			JE_SFCodes(1, PX, PY, tx, ty);
		}
		if (inverted)
			qa_check(SFExecuted[0] == SEEKER_BOMBS_SPECIAL,
			         "a rotated combo entered over the wire fires on an upside-down screen");
		else
			qa_check(SFExecuted[0] == 0,
			         "...and the same wire input on an upright screen is not that combo");
	}

	memcpy(button, savedButton, sizeof(button));
	memcpy(SFExecuted, savedExec, sizeof(SFExecuted));
	memcpy(SFCurrentCode, savedCode, sizeof(savedCode));
	player[0] = saved0;
	superTyrian = savedSuper;
	smoothies[9-1] = savedInvert;
}

/* Any tick that is not the combo's next code throws it away, except the code just consumed and a
 * tick with everything released. The expected direction with the fire button in the wrong state
 * goes too, which is what keeps ordinary flying from finishing a combo. */
static void qa_test_twiddle_strictness(void)
{
	const Player saved0 = player[0];
	const JE_boolean savedSuper = superTyrian;
	JE_byte savedCode[2][21];
	JE_byte savedExec[2];
	bool savedButton[4];
	memcpy(savedCode, SFCurrentCode, sizeof(savedCode));
	memcpy(savedExec, SFExecuted, sizeof(savedExec));
	memcpy(savedButton, button, sizeof(savedButton));

	// USP Talon's first combo slot is Invulnerability: DOWN, UP, DOWN, then UP with fire held. Its
	// progress is SFCurrentCode[0][0], and the row's terminator names the special it sets off.
	superTyrian = false;  // SuperTyrian replaces the ship's rows with its own
	player[0].items.ship = 1;
	const JE_byte comboRow = (JE_byte)(shipCombos[1][0] - 1);
	const JE_byte want = (JE_byte)(keyboardCombos[comboRow][4] - 100);

	// A direction stays pressed across ticks, and the controls pass through neutral between two of
	// them. Neither costs the combo anything.
	qa_twiddle_clear();
	static const JE_byte held[] = { 2, 2, 2, 9, 1, 9, 9, 2, 9, 5 };
	for (unsigned k = 0; k < COUNTOF(held); ++k)
		qa_twiddle_step(1, held[k]);
	qa_check(SFExecuted[0] == want && want != 0,
	         "a twiddle completes through held directions and releases between steps");

	// Fire pressed one step early, on a step the combo wants bare.
	qa_twiddle_clear();
	qa_twiddle_step(1, 2);
	const JE_byte afterFirst = SFCurrentCode[0][0];
	qa_twiddle_step(1, 5);  // up, the next step, but with fire held
	qa_check(afterFirst == 1 && SFCurrentCode[0][0] == 0,
	         "starting to fire on a step that wants no fire loses the twiddle");

	// ...and fire let go on the last step, which is the only one that wants it.
	qa_twiddle_clear();
	static const JE_byte released[] = { 2, 1, 2 };
	for (unsigned k = 0; k < COUNTOF(released); ++k)
		qa_twiddle_step(1, released[k]);
	const JE_byte atLastStep = SFCurrentCode[0][0];
	qa_twiddle_step(1, 1);  // up, but no longer firing
	qa_check(atLastStep == 3 && SFCurrentCode[0][0] == 0 && SFExecuted[0] == 0,
	         "stopping fire on the step that wants it loses the twiddle");

	// Another direction loses it.
	qa_twiddle_clear();
	qa_twiddle_step(1, 2);
	qa_twiddle_step(1, 3);  // left, which the row never asks for
	qa_check(SFCurrentCode[0][0] == 0, "a direction the combo did not ask for loses it");

	memcpy(button, savedButton, sizeof(button));
	memcpy(SFExecuted, savedExec, sizeof(SFExecuted));
	memcpy(SFCurrentCode, savedCode, sizeof(savedCode));
	player[0] = saved0;
	superTyrian = savedSuper;
}

/* A twiddle keeps its own clock. SFExecuted is cleared at the top of every tick, so a twiddle the
 * equipped special's recharge turns away is discarded outright, and with an autofiring special
 * that recharge is running nearly every tick. Firing a twiddle must likewise leave the equipped
 * special's own recharge where it found it. */
static void qa_test_twiddle_cooldown(void)
{
	if (VGAScreenSeg == NULL || game_screen == NULL)
		return;  // the charge path repaints the real shield and armor gauges

	const JE_byte savedStype = special[SPECIAL_NUM].stype;
	const JE_byte savedPwr = special[SPECIAL_NUM].pwr;
	const JE_byte savedExec = SFExecuted[0];
	const JE_byte savedRepeat = shotRepeat[SHOT_SPECIAL];
	const JE_byte savedTemp2 = temp2;
	const JE_boolean savedAuto = autoFireSpecial;
	const JE_boolean savedDbgAuto = debugAutofireTwiddle;
	const JE_boolean savedTrigger = debugTwiddleTrigger;
	const JE_byte savedDbgTwiddle = debugTwiddleSpecial;
	const JE_boolean savedEndless = endlessMode;
	const Player saved0 = player[0];
	SDL_Surface *const savedVGA = VGAScreen;

	// The scratch slot takes an inert type: the effect switch has no case 0, so only the charge
	// runs. Clearing the equipped special and the debug twiddle keeps every other fire gate shut,
	// and stock (non-endless) charges keep the deducted amounts exact.
	special[SPECIAL_NUM].stype = 0;
	special[SPECIAL_NUM].pwr = 20;  // a fixed shield charge
	player[0].items.special = 0;
	autoFireSpecial = false;
	debugAutofireTwiddle = false;
	debugTwiddleTrigger = false;
	debugTwiddleSpecial = 0;
	endlessMode = false;
	endlessSetFxPlayer(0);

	// The equipped special is deep in a recharge. The twiddle must still go off.
	uint armor = 30, shield = 50;
	SFExecuted[0] = SPECIAL_NUM;
	specialWait = 0;
	flareDuration = 0;
	flareStart = false;
	zinglonDuration = 0;
	astralDuration = 0;
	JE_resetTwiddleClocks();
	shotRepeat[SHOT_SPECIAL] = 90;
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(shield == 30, "a twiddle fires while the equipped special is recharging");
	qa_check(shotRepeat[SHOT_SPECIAL] == 89,
	         "...and leaves the equipped special's recharge alone, bar the tick it just spent");

	// Its own clock then paces it: an immediate repeat is refused, and it comes back after.
	shield = 50;
	SFExecuted[0] = SPECIAL_NUM;
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(shield == 50, "a second twiddle inside the cooldown is refused");

	for (int tick = 0; tick < TWIDDLE_MIN_WAIT; ++tick)
	{
		SFExecuted[0] = 0;
		JE_doSpecialShot(1, &armor, &shield);
	}
	SFExecuted[0] = SPECIAL_NUM;
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(shield == 30, "...and fires again once that cooldown runs out");

	// An unaffordable charge takes nothing and leaves the bar it could not pay from intact.
	shield = 4;
	special[SPECIAL_NUM].pwr = 98;  // the whole shield bar
	SFExecuted[0] = SPECIAL_NUM;
	JE_resetTwiddleClocks();
	JE_doSpecialShot(1, &armor, &shield);
	const bool spentWhenPaid = shield == 0;
	shield = 3;
	SFExecuted[0] = SPECIAL_NUM;
	JE_resetTwiddleClocks();
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(spentWhenPaid && shield == 3,
	         "a twiddle nobody can pay for charges nothing at all");

	VGAScreen = savedVGA;
	player[0] = saved0;
	endlessMode = savedEndless;
	debugTwiddleSpecial = savedDbgTwiddle;
	debugTwiddleTrigger = savedTrigger;
	debugAutofireTwiddle = savedDbgAuto;
	autoFireSpecial = savedAuto;
	temp2 = savedTemp2;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	SFExecuted[0] = savedExec;
	special[SPECIAL_NUM].pwr = savedPwr;
	special[SPECIAL_NUM].stype = savedStype;
	JE_resetTwiddleClocks();
}

static void qa_test_rollback(void)
{
	rollback_register_all();
	const Uint32 before = rollback_state_hash();
	const int saved_x = player[0].x;

	rollback_snapshot(0x5141u);
	player[0].x ^= 0x55;
	qa_check(rollback_restore(0x5141u) && player[0].x == saved_x
	         && rollback_state_hash() == before,
	         "rollback snapshot restores the registered state exactly");

	const size_t state_size = rollback_state_size();
	Uint8 *wire = malloc(state_size);
	qa_check(wire != NULL, "rollback wire buffer allocation succeeds");
	if (wire != NULL)
	{
		const bool exported = rollback_wire_export(wire);
		player[0].x ^= 0x33;
		qa_check(exported && rollback_wire_adopt(wire) && player[0].x == saved_x
		         && rollback_state_hash() == before,
		         "wire snapshot export/adopt restores canonical state");
		free(wire);
	}

	printf("# rollback registry: %lu bytes, layout %08x\n",
	       (unsigned long)state_size, (unsigned)rollback_layout_fingerprint());
}

static void qa_test_save_fixtures(void)
{
	char path[512];
	char detail[256];

	for (int version = 3; version <= endlessSaveCurrentVersion(); ++version)
	{
		snprintf(path, sizeof(path), "%s/v%02d.sav", qa_fixture_dir, version);
		detail[0] = '\0';
		const bool okay = endlessSaveTestFixture(path, detail, sizeof(detail));
		char label[640];
		snprintf(label, sizeof(label), "save v%02d load/save/load migration%s%s",
		         version, detail[0] ? ": " : "", detail);
		qa_check(okay, label);
	}

	// ...and the header field that keeps a record whose width moved from taking every later slot
	// with it, which is the failure the version number alone did not catch.
	detail[0] = '\0';
	const bool guarded = endlessSaveTestWidthGuard(detail, sizeof(detail));
	char label[320];
	snprintf(label, sizeof(label), "save record width guard%s%s", detail[0] ? ": " : "", detail);
	qa_check(guarded, label);
}

int qa_run_unit_suite(void)
{
	qa_checks = qa_failures = 0;
	printf("TAP version 13\n");

	/* Mirror normal episode setup before testing item, ship, weapon, and sidekick invariants. */
	JE_loadItemDat();
	JE_initPlayerData();
	qa_test_config_option_removal();
	qa_test_rollback();
	qa_test_course_tables();
	qa_test_canonical_mods();
	qa_test_structural_rng();
	qa_test_perk_registry();
	qa_test_record_readers();
	qa_test_weapon_editor();
	qa_test_custom_weapon_wire();
	qa_test_fixed_pool_layout();
	qa_test_save_record_wire();
	qa_test_save_slot_seats();
	qa_test_cash_ledger();
	qa_test_bounty_matrix();
	qa_test_score_pickup_multiplier();
	qa_test_zone_payout();
	qa_test_arcade_scaling();
	qa_test_arcade_matrices();
	qa_test_sidekick_rollback_state();
	qa_test_gauge_flash_lifetime();
	qa_test_special_icon_tops();
	qa_test_flying_punch_bolt();
	qa_test_dragonwing_row();
	qa_test_wide_hull_columns();
	qa_test_dual_mode_tag();
	qa_test_weapon_bay_tags();
	qa_test_special_light_events();
	qa_test_partner_repair_special();
	qa_test_twiddle_ships();
	qa_test_twiddle_diagonals();
	qa_test_twiddle_wire();
	qa_test_twiddle_strictness();
	qa_test_twiddle_cooldown();
	qa_test_twiddle_charges();
	qa_test_modifier_online_parity();
	qa_test_effect_gates();
	qa_test_shot_hitboxes();
	qa_test_health_bar_scale();
	qa_test_elite_tier_eligibility();
	qa_test_elite_explosion_tint();
	qa_test_elite_shot_tint();
	qa_test_elite_message_tint();
	qa_test_superspark_caps();
	qa_test_superspark_discarded_pass();
	qa_test_superspark_rng_cost();
	qa_test_superspark_brief();
	qa_test_superspark_seeded_spread();
	qa_test_superspark_shapes();
	qa_test_vaporised_shot_sparks();
	qa_test_network_settings();
	qa_test_network_endless_lobby();
	qa_test_endless_coop();
	qa_test_kill_fire_drives();
	qa_test_kill_fire_wiring();
	qa_test_kinetic_converter();
	qa_test_coop_combo_and_pickups();
	qa_test_peer_left_level();
	qa_test_online_suite();
	qa_test_endless_suite();
	qa_test_save_fixtures();
	qa_test_resync_serialization();
	qa_test_courses();
	qa_test_course_base_rule();
	qa_test_course_shuffle_rule();
	qa_test_course_reroll();
	qa_test_course_seat_parity();
	qa_test_course_reroll_dodge();
	qa_test_item_data_settings();
	qa_test_firing_sound_levels();
	qa_test_enhancement_presets();  // keep last: it leaves the enhancement settings where it put them

	printf("1..%u\n", qa_checks);
	printf("# %u checks, %u failures\n", qa_checks, qa_failures);
	return qa_failures == 0 ? 0 : 1;
}

int qa_run_replay_fixture(void)
{
	if (qa_replay_demo < 1 || qa_replay_demo > 5 || qa_replay_ticks == 0)
	{
		fprintf(stderr, "replay test requires demo 1..5 and a positive tick bound\n");
		return 2;
	}

	JE_initPlayerData();
	gameLoaded = false;
	jumpSection = false;
	stopped_demo = false;
	play_demo = true;
	demo_num = (Uint8)(qa_replay_demo - 1);
	qa_fast_forward = true;

	/* Chain Reaction is Endless-only, so the shipped demos never fire a pulse and the self-test
	 * never sees the queue. Arming the effects over a campaign demo puts a wave in the air during
	 * real play, so the frame-by-frame comparison reaches a queue with pulses standing in it. */
	if (qa_replay_chain != 0)
	{
		endlessCampaignMods = true;
		rollback_selftest_allow_endless(true);
		/* 2 arms the effects without the perk, as the control for whatever 1 reports. */
		if (qa_replay_chain == 1)
		{
			endlessPerkGrant(0, PERK_CHAINRXN, endlessPerkTable[PERK_CHAINRXN].maxStack);

			/* A pulse is scaled by its owner's damage, so give that scale something to say and
			 * something that moves: Heavy Rounds is a constant lift, while a kill-fire drive opens
			 * and lapses as the demo kills, changing the figure the drain reads tick to tick. */
			endlessPerkGrant(0, PERK_DAMAGE, endlessPerkTable[PERK_DAMAGE].maxStack);
			endlessPlayerMods[0] |= ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_DMGUP;
		}
	}

	rollback_selftest_ticks = 0;
	rollback_selftest_failures = 0;
	rollback_selftest_set_limit(qa_replay_ticks);
	rollback_selftest_set(true);
	JE_main();
	rollback_selftest_set(false);

	const Uint32 hash = rollback_selftest_bounded_hash();
	printf("REPLAY demo=%d ticks=%lu failures=%lu hash=%08x layout=%08x\n",
	       qa_replay_demo, rollback_selftest_ticks, rollback_selftest_failures,
	       (unsigned)hash, (unsigned)rollback_layout_fingerprint());

	if (rollback_selftest_ticks != qa_replay_ticks || rollback_selftest_failures != 0)
		return 1;
	if (qa_replay_expect_set && hash != qa_replay_expect)
	{
		fprintf(stderr, "replay hash mismatch: expected %08x, got %08x\n",
		        (unsigned)qa_replay_expect, (unsigned)hash);
		return 1;
	}
	return 0;
}

/* Headless Destruct with every frame replayed from its own snapshot.  It runs the production
 * minigame through JE_destructGame, so a field the rollback state walk fails to cover shows up
 * here rather than as an online desync nobody can reproduce. */
int qa_run_destruct_selftest(void)
{
	if (qa_destruct_selftest_ticks == 0)
	{
		fprintf(stderr, "destruct self-test requires a positive tick bound\n");
		return 2;
	}

	JE_initPlayerData();
	JE_destructGame();

	size_t probeRaw = 0, probeComp = 0;
	drb_selftest_resync_bytes(&probeRaw, &probeComp);

	printf("DESTRUCT ticks=%lu failures=%lu resync=%s raw=%lu compressed=%lu chunks=%lu\n",
	       drb_selftest_ticks_run(), drb_selftest_failures(),
	       drb_selftest_resync_ok() ? "ok" : "FAILED",
	       (unsigned long)probeRaw, (unsigned long)probeComp,
	       probeComp == 0 ? 0UL : (unsigned long)((probeComp + 12 + 307) / 308));

	if (drb_selftest_ticks_run() != qa_destruct_selftest_ticks)
	{
		fprintf(stderr, "destruct self-test ran %lu of %lu ticks\n",
		        drb_selftest_ticks_run(), qa_destruct_selftest_ticks);
		return 1;
	}
	if (!drb_selftest_resync_ok())
	{
		fprintf(stderr, "destruct desync recovery: the battle did not survive its own "
		                "compress/expand round trip\n");
		return 1;
	}
	return drb_selftest_failures() == 0 ? 0 : 1;
}
