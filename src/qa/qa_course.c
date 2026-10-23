#define ENDLESS_INTERNAL_ACCESS
#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "config.h"
#include "crashlog.h"
#include "custom_episode.h"
#include "endless.h"
#include "episodes.h"
#include "endless_internal.h"
#include "font.h"
#include "game_menu.h"
#include "lvllib.h"
#include "mtrand.h"
#include "network.h"
#include "player.h"

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

void qa_test_course_tables(void)
{
	char detail[256];
	const bool okay = endlessValidateModifierTables(detail, sizeof(detail));
	if (!okay)
		fprintf(stderr, "# modifier table validation: %s\n", detail);
	qa_check(okay, "modifier and course-name registries satisfy their documented invariants");
}

// The special-enemy ladder, checked against the shares endlessEliteChancePercent actually reads.
void qa_test_canonical_mods(void)
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
		{ ENDLESS_MOD_SEEKER | ENDLESS_MOD_TWINSEEK,   ENDLESS_MOD_TWINSEEK },
		{ ENDLESS_MOD_TWINSEEK | ENDLESS_MOD_HUNTER,   ENDLESS_MOD_HUNTER },
		{ ENDLESS_MOD_HUNTER | ENDLESS_MOD_TRUEAIM,    ENDLESS_MOD_TRUEAIM },
		{ ENDLESS_MOD_SEEKER_ANY,                      ENDLESS_MOD_KILLSHOT },
		{ ENDLESS_MOD_KILLSHOT | ENDLESS_MOD_SWIFT,    ENDLESS_MOD_KILLSHOT | ENDLESS_MOD_SWIFT },
		{ 0, 0 },
	};
	for (unsigned i = 0; i < COUNTOF(cases); ++i)
	{
		const Uint64 got = endlessCanonicalMods(cases[i].in);
		qa_check(got == cases[i].want, "redundant special-enemy bits are dropped");
		qa_check(endlessCanonicalMods(got) == got, "settling a modifier set twice changes nothing");
	}
}

void qa_test_seeker_tiers(void)
{
	const bool savedMode = endlessMode;
	const bool savedCampaign = endlessCampaignMods;
	const Uint64 savedMods = endlessActiveMods;

	static const struct { Uint64 mods; int tier; int passes; float turnCos; } cases[] = {
		{ 0,                       ENDLESS_SEEK_NONE,  0, 1.0000f },
		{ ENDLESS_MOD_SEEKER,      ENDLESS_SEEK_CURVE, 1, 0.9205f },
		{ ENDLESS_MOD_TWINSEEK,    ENDLESS_SEEK_TWIN,  2, 0.9205f },
		{ ENDLESS_MOD_HUNTER,      ENDLESS_SEEK_WIDE,  1, 0.5736f },
		{ ENDLESS_MOD_TRUEAIM,     ENDLESS_SEEK_TRUE,  1, -1.0000f },
		{ ENDLESS_MOD_KILLSHOT,    ENDLESS_SEEK_KILL,  2, -1.0000f },
		{ ENDLESS_MOD_SEEKER_ANY,  ENDLESS_SEEK_KILL,  2, -1.0000f },
	};

	endlessMode = false;
	endlessCampaignMods = false;
	endlessActiveMods = ENDLESS_MOD_KILLSHOT;
	qa_check(endlessSeekerTier() == ENDLESS_SEEK_NONE && !endlessSeekerActive()
	         && endlessSeekerPasses() == 0,
	         "no shot corrects course outside an endless run");

	endlessMode = true;
	for (unsigned i = 0; i < COUNTOF(cases); ++i)
	{
		float turnCos = 0.0f, turnSin = 0.0f;
		endlessActiveMods = cases[i].mods;
		endlessSeekerTurn(&turnCos, &turnSin);
		qa_check(endlessSeekerTier() == cases[i].tier, "each sector runs its strongest correction tier");
		qa_check(endlessSeekerPasses() == cases[i].passes, "a tier corrects the number of times it declares");
		qa_check(turnCos == cases[i].turnCos, "a tier bends through the angle it declares");
		qa_check(endlessSeekerActive() == (cases[i].passes > 0),
		         "shots arm exactly when the sector has a correction tier");
	}

	static const Uint64 ladder[] = {
		ENDLESS_MOD_SEEKER, ENDLESS_MOD_TWINSEEK, ENDLESS_MOD_HUNTER,
		ENDLESS_MOD_TRUEAIM, ENDLESS_MOD_KILLSHOT,
	};
	for (unsigned i = 0; i < COUNTOF(ladder); ++i)
	{
		qa_check(endlessModWord(ladder[i])[0] != '\0', "every correction tier lists a monitor row");
		qa_check((ladder[i] & ENDLESS_HOSTILE_MASK) != 0, "every correction tier counts as a danger");
		if (i > 0)
			qa_check(endlessDangerScore(ladder[i]) > endlessDangerScore(ladder[i - 1]),
			         "the correction ladder gains danger at every rung");
	}

	endlessActiveMods = savedMods;
	endlessCampaignMods = savedCampaign;
	endlessMode = savedMode;
}

void qa_test_structural_rng(void)
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

void qa_test_courses(void)
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
			const long payout = endlessCoursePayout(i);
			const long floor = endlessClearBase() / 4;
			const Uint64 mods = endlessCourseMod[i];

			qa_check(endlessCourseFileValid(endlessCourseEp[i], endlessCourseSec[i],
			                                endlessCourseFile[i]),
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
			{
				qa_check(ranks[8] == 2 && ranks[9] == 2 && ranks[10] == 1,
				         "grand milestone contains two S++, two S+++, and one END route");
				for (int i = 0; i < first_count; ++i)
				{
					qa_check((endlessCourseMod[i] & ENDLESS_SCROLL_PACE_MASK) == 0,
					         "no grand milestone route quickens the scroll");
					qa_check((endlessCourseMod[i] & ENDLESS_MOD_DEADGEN) == 0,
					         "no grand milestone route runs the generator dead");
				}
			}
		}

		qa_reset_course_inputs(seed, depth, diff);
		endlessGenerateCourses();
		qa_check(endlessCourseCnt == first_count && qa_slate_hash() == first_hash,
		         "seeded course generation is deterministic");
	}

	printf("# course properties: 768 seeds, %u launchable routes\n", routes);
}

// Check The End's exclusions and probability bands across seeded runs.
void qa_test_finale_mods(void)
{
	static const int depths[] = { 99, 199, 299, 399 };   // the depth a zone-100 finale is charted at
	const unsigned samples = 2048;
	char seed[ENDLESS_SEED_MAXLEN];
	unsigned statics = 0, topsy = 0, homing = 0, kamikaze = 0;
	bool deadgen = false, built = true, bothTiers = false;

	for (unsigned sample = 0; sample < samples; ++sample)
	{
		watchdog_heartbeat();
		snprintf(seed, sizeof(seed), "qa-end-%08x", (unsigned)(sample * 2654435761u));
		qa_reset_course_inputs(seed, depths[sample % COUNTOF(depths)], DIFFICULTY_NORMAL);

		const Uint64 mods = endlessMakeTheEndMods();
		if (!(mods & ENDLESS_MOD_THEEND) || !(mods & (ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION)))
			built = false;
		if (mods & ENDLESS_MOD_DEADGEN)
			deadgen = true;
		if (mods & ENDLESS_MOD_STATIC)
			++statics;
		if (mods & ENDLESS_MOD_TOPSY)
			++topsy;
		if (mods & ENDLESS_MOD_HOMING)
			++homing;
		if (mods & ENDLESS_MOD_KAMIKAZE)
			++kamikaze;
		if ((mods & ENDLESS_MOD_HOMING) && (mods & ENDLESS_MOD_KAMIKAZE))
			bothTiers = true;
	}

	qa_check(built, "every finale carries its marker and an all-elite tier");
	qa_check(!deadgen, "no finale runs the generator dead");
	qa_check(statics * 20 < samples, "Static lands on fewer than one finale in twenty");
	qa_check(statics > 0, "...and a finale can still roll it");
	qa_check(topsy * 10 < samples && topsy * 34 > samples,
	         "the upside-down view lands on about one finale in seventeen");
	qa_check(!bothTiers, "no finale carries two homing tiers at once");
	// Wide bands check the distribution without pinning exact counts.
	qa_check(homing * 3 > samples && homing * 3 < samples * 2,
	         "Light Homing lands on about one finale in two");
	qa_check(kamikaze * 66 > samples && kamikaze * 16 < samples,
	         "Kamikaze lands on about one finale in thirty-three");

	printf("# finale modifiers: %u seeds, Static %u, Topsy %u, Homing %u, Kamikaze %u\n",
	       samples, statics, topsy, homing, kamikaze);
}

/* The Base Level rule. Same puts every route of a slate onto one level, leaving the modifiers as
 * the whole choice; Varied deals each route its own. Both must stay seed-deterministic. */
void qa_test_course_base_rule(void)
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
			qa_check(endlessCourseFileValid(endlessCourseEp[i], endlessCourseSec[i],
			                                endlessCourseFile[i]),
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

/* Keep this expected pool independent of endless_level.c; duplicate script sections count once. */
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

	/* A draw spanning a refill must stay unique, and the next draw must not repeat its tail. */
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
		Uint8 *const wire = malloc(ENDLESS_RUN_WIRE_MAX);   // off the stack: the text record is kilobytes
		const size_t wireLen = wire ? endlessRunSerialize(wire, ENDLESS_RUN_WIRE_MAX) : 0;
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
		qa_check(wire != NULL && endlessRunAdopt(wire, wireLen) && endlessShuffleNext == afterReroll,
		         "a resumed run draws the piece it was owed, whichever seat is hosting");
		free(wire);
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

void qa_test_course_shuffle_rule(void)
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

/* Radar rerolls must change the visit, preserve old seeds, and reproduce from the reroll count. */
void qa_test_course_reroll(void)
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
void qa_test_course_seat_parity(void)
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
void qa_test_course_reroll_dodge(void)
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

void qa_test_perk_registry(void)
{
	const int count = endlessPerkCount();
	qa_check(count > 0, "perk registry is nonempty");
	for (int i = 0; i < count; ++i)
	{
		qa_check(qa_display_name_valid(endlessPerkName(i)),
		         "perk name fits its menu field and uses visible glyphs");
		qa_check(endlessPerkDesc(i)[0] != '\0' && endlessPerkMaxStack(i) > 0,
		         "perk registry row has a description and positive stack limit");
		// The pick screen draws "Owned n/max" flush right of the description on one help line.
		qa_check(help_bar_right_x(endlessPerkDesc(i), "Owned 9/9") == help_bar_right_x("", "Owned 9/9"),
		         "perk description leaves the stack count flush right on the help bar");
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

void qa_test_record_readers(void)
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

	/* The records page shows the deepest run for the selected mode and crew size. */
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

/* Exercises each Custom Endless pool through the public shuffle. */
void qa_test_custom_endless_pool(void)
{
	const int savedMode = customEndlessMode;
	int ep;
	JE_byte sec, file;

	customEpisodeScan();
	if (customEpisodeCount() == 0)
	{
		customEndlessMode = CUSTOM_ENDLESS_ONLY;
		bool ok = true, sawCustom = false;
		for (int p = 0; p < 40 && ok; ++p)
		{
			ok = endlessShuffleSafeLevel(p, &ep, &sec, &file);
			sawCustom |= ep >= CUSTOM_EPISODE_ID_BASE;
		}
		qa_check(ok && !sawCustom,
		         "without containers the Endless pool is stock whatever the setting says");
		customEndlessMode = savedMode;
		return;
	}

	customEndlessMode = CUSTOM_ENDLESS_OFF;
	bool ok = true, sawCustom = false;
	for (int p = 0; p < 40 && ok; ++p)
	{
		ok = endlessShuffleSafeLevel(p, &ep, &sec, &file);
		sawCustom |= ep >= CUSTOM_EPISODE_ID_BASE;
	}
	qa_check(ok && !sawCustom, "Custom Endless Off keeps the pool stock");

	customEndlessMode = CUSTOM_ENDLESS_MIXED;
	bool sawStock = false;
	sawCustom = false;
	for (int p = 0; p < 200 && ok; ++p)
	{
		ok = endlessShuffleSafeLevel(p, &ep, &sec, &file);
		if (ep >= CUSTOM_EPISODE_ID_BASE)
			sawCustom = true;
		else
			sawStock = true;
	}
	qa_check(ok && sawStock && sawCustom,
	         "Custom Endless Mixed pools stock and custom levels together");

	customEndlessMode = CUSTOM_ENDLESS_ONLY;
	sawStock = false;
	sawCustom = false;
	for (int p = 0; p < 40 && ok; ++p)
	{
		ok = endlessShuffleSafeLevel(p, &ep, &sec, &file);
		if (ep >= CUSTOM_EPISODE_ID_BASE)
			sawCustom = true;
		else
			sawStock = true;
	}
	qa_check(ok && sawCustom && !sawStock,
	         "Custom Endless Custom Only draws the containers' levels alone");

	// A host session list overrides the local setting and collection.
	{
		char sessionName[1][CUSTOM_EPISODE_FILE_LEN];
		SDL_strlcpy(sessionName[0], customEpisodeFile(0), CUSTOM_EPISODE_FILE_LEN);
		customEndlessMode = CUSTOM_ENDLESS_OFF;
		customEpisodeSessionBegin(sessionName, 1, CUSTOM_ENDLESS_ONLY);
		bool sOk = true, sStock = false, sCustom = false;
		for (int p = 0; p < 40 && sOk; ++p)
		{
			sOk = endlessShuffleSafeLevel(p, &ep, &sec, &file);
			if (ep >= CUSTOM_EPISODE_ID_BASE)
				sCustom = true;
			else
				sStock = true;
		}
		customEpisodeSessionEnd();
		qa_check(sOk && sCustom && !sStock,
		         "a session list overrides the local setting and answers the pool's ids");
	}

	customEndlessMode = savedMode;
}
