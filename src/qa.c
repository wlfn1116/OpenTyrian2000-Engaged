/* Project-owned unit, property, serialization, and replay tests. */
#include "qa.h"

#include "config.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "destruct.h"
#include "destruct_rollback.h"
#include "editship.h"
#include "endless.h"
#include "episodes.h"
#include "endless_internal.h"
#include "fonthand.h"
#include "game_menu.h"   // JE_getLevelSections
#include "keyboard.h"
#include "mainint.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "net_style.h"
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
#ifdef WITH_MIDI
#include <midiproc.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

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
bool qa_net_guest_esc = false;

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
 * machines. */
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

static void qa_test_any_button_latch(void)
{
	newkey = true;
	newmouse = false;
	const bool keyEdge = JE_anyButton();

	newkey = false;
	newmouse = true;
	const bool mouseEdge = JE_anyButton();

	newkey = newmouse = false;
	qa_check(keyEdge && mouseEdge,
	         "an any-button wait preserves key and mouse edges seen by an earlier event pump");
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

static void qa_test_seeker_tiers(void)
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
static void qa_test_finale_mods(void)
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
	qa_check(sizeof(PlayerItems) == 13 && SAVE_RECORD_PACKED_SIZE == 97
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
	src.level = 0x1234;
	for (unsigned i = 0; i < sizeof(src.items); ++i)
	{
		src.items[i] = (JE_byte)(i * 7 + 1);
		src.lastItems[i] = (JE_byte)(255 - i * 9);
	}
	src.score = 4000000000LL;   // past the old 32-bit wallet
	src.score2 = 0x76543210;
	strcpy(src.levelName, "QA LEVEL");
	strcpy(src.name, "WIRE PLAYER");
	src.cubes = 17;
	src.power[0] = 4; src.power[1] = 11;
	src.episode = 5; src.difficulty = DIFFICULTY_LORD_OF_GAME;
	src.secretHint = 3; src.input1 = 1; src.input2 = 2;
	src.gameHasRepeated = true; src.initialDifficulty = DIFFICULTY_HARD;
	src.dualShipTag = 0xc74f4321u;
	src.autoFireSpecial = true; src.chargeSidekickAutofire = 2;
	src.difficultyAdjust = true; src.cheatInfiniteSidekickAmmo = true;
	src.cheatInfiniteShields = false; src.cheatInfiniteArmor = true; src.expertMode = true;
	src.shipColor[0] = 6; src.shipColor[1] = NET_SHIP_COLORS;
	src.viewOpacity[0] = NET_OPACITY_MIN; src.viewOpacity[1] = NET_OPACITY_FULL;
	src.viewShipOpacity[0] = 0; src.viewShipOpacity[1] = 1;
	src.viewHpBars[0] = NET_HP_BARS_ALWAYS; src.viewHpBars[1] = NET_HP_BARS_ON_HIT;

	memset(guarded, 0xa5, sizeof(guarded));
	save_record_pack(packed, &src);
	qa_check(guarded[0] == 0xa5 && guarded[sizeof(guarded) - 1] == 0xa5,
	         "save-record packing writes exactly its fixed 97-byte frame");
	save_record_unpack(&dst, packed);
	save_record_pack(repacked, &dst);
	qa_check(memcmp(packed, repacked, sizeof(repacked)) == 0,
	         "network save record pack/unpack round-trips every serialized field");
	qa_check(dst.score == src.score && dst.score2 == src.score2 && dst.dualShipTag == src.dualShipTag,
	         "network save record carries 64-bit wallets and the dual-ship tag");
	qa_check(save_record_is_coop(&dst),
	         "network save record preserves the Online Campaign type marker");
	qa_check(dst.shipColor[0] == src.shipColor[0] && dst.shipColor[1] == src.shipColor[1],
	         "network save record carries both ships' online dyes");
	qa_check(memcmp(dst.viewOpacity, src.viewOpacity, sizeof(src.viewOpacity)) == 0
	         && memcmp(dst.viewShipOpacity, src.viewShipOpacity, sizeof(src.viewShipOpacity)) == 0
	         && memcmp(dst.viewHpBars, src.viewHpBars, sizeof(src.viewHpBars)) == 0,
	         "...and both machines' views of the other ship, by seat");
	qa_check(dst.gameHasRepeated && dst.autoFireSpecial && dst.difficultyAdjust
	         && dst.cheatInfiniteSidekickAmmo && !dst.cheatInfiniteShields
	         && dst.cheatInfiniteArmor && dst.expertMode,
	         "network save record preserves all boolean gameplay flags");

	/* Hostile fixed-width strings still have to become safe C strings on receipt. */
	memset(packed + 46, 'L', 11);
	memset(packed + 57, 'N', 15);
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
	const Sint64 savedCash = player[0].cash;
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
	         && endlessRunCashEarned - endlessRunCashSpent == (Uint64)player[0].cash,
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
 * other combat payment. */
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
		const Sint64 base = champ ? endlessChampionBounty() : endlessEliteBounty();
		endlessSetFxPlayer(0);
		const Sint64 want = (doubled && !shared) ? base * 2 : base;  // Double Earnings covers bounties
		const bool okay = shared
		                ? (player[0].cash == want && player[1].cash == want)
		                : (player[1].cash == want && player[0].cash == 0);
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

	const Sint64 face = 250, boosted = face * ENDLESS_PERK_BOUNTY_PICKUP_MULT;
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

		const Sint64 scale = (doubled && !shared) ? 2 : 1;  // Double Earnings covers pickups
		const bool okay = shared
		                ? (player[0].cash == boosted + face && player[1].cash == boosted + face)
		                : (player[0].cash == boosted * scale && player[1].cash == face * scale);
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

	Sint64 out[2][2];
	for (uint machine = 1; machine <= 2; ++machine)
	{
		thisPlayerNum = machine;
		coop_set_session_shared_credit(false);
		coop_set_session_double_earnings(false);
		player[0].cash = 10000;
		player[1].cash = 40000;
		endlessCashResync();
		Sint64 interest = 0, bonus = 0;
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

// One shot from `bay` at a fixed spot, fired by player 1; MAX_PWEAPON when the gun refused.
static JE_integer qa_guidance_fire(uint bay, JE_word gun)
{
	return player_shot_create(0, bay, 100, 150, 0, 0, gun, 1);
}

static bool qa_guidance_marked(JE_integer id)
{
	return id < MAX_PWEAPON && (playerShotData[id].aimDelayMax & SHOT_AIM_GUIDANCE) != 0;
}

/* Guidance Package: which bays it steers at which stacks, what it will and will not aim at, and
 * the course correction itself. The stock weapon-table homing is checked beside it, since the two
 * share the move-side and the stock branch has to stay as shipped. */
static void qa_test_guidance_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const bool savedGuidedAim = guidedShotScreenAim;
	const uint savedPower = power;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	guidedShotScreenAim = false;   // the shipped homing is what the stock checks below pin
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);
	power = 10000;   // the guns below must not be refused for generator power
	memset(shotAvail, 0, sizeof(shotAvail));

	const JE_word plainGun = 1;     // Pulse-Cannon level 1: one straight, unguided shot
	const JE_word guidedGun = 587;  // Heavy Guided Bombs: one shot that homes on its own
	qa_check(weapons[plainGun].aim <= 5 && weapons[plainGun].circlesize == 0
	         && weapons[plainGun].multi == 1 && weapons[guidedGun].aim > 5
	         && weapons[guidedGun].multi == 1,
	         "the guidance test's stock guns are the ones it assumes");
	const int guidedOwn = weapons[guidedGun].aim - 5;

	/* The field: one shootable hull whose screen x (ex + mapoffset) is left of the shot while its
	 * map x is right of it, and three nearer things a steered shot must ignore: a pickup, an
	 * invulnerable part and scenery. */
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	enemyAvail[0] = 0;
	enemy[0].ex = 120;
	enemy[0].mapoffset = -40;
	enemy[0].ey = 60;
	enemy[0].armorleft = 20;
	enemyAvail[1] = 2;
	enemy[1].ex = 100;
	enemy[1].ey = 100;
	enemy[1].armorleft = 255;
	enemy[1].scoreitem = true;
	enemyAvail[2] = 0;
	enemy[2].ex = 100;
	enemy[2].ey = 120;   // the nearest thing on the field
	enemy[2].armorleft = 255;
	enemyAvail[3] = 2;
	enemy[3].ex = 100;
	enemy[3].ey = 110;
	enemy[3].armorleft = 255;

	// No stacks: nothing changes, and the stock branch aims where it always did.
	JE_integer id = qa_guidance_fire(SHOT_FRONT, plainGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 0 && !qa_guidance_marked(id),
	         "without Guidance Package an unguided gun's shot is not steered");
	id = qa_guidance_fire(SHOT_FRONT, guidedGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 3
	         && playerShotData[id].aimDelayMax == guidedOwn,
	         "...and a stock guided shot keeps its shipped aim: nearest map x, invulnerable part included");
	memset(shotAvail, 0, sizeof(shotAvail));

	// One stack: the main guns steer, the sidekicks and the specials do not.
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 1;
	endlessPerkRederive();
	const JE_integer steered = qa_guidance_fire(SHOT_FRONT, plainGun);
	qa_check(steered < MAX_PWEAPON && playerShotData[steered].aimAtEnemy == 1
	         && playerShotData[steered].aimDelayMax == (ENDLESS_PERK_GUIDANCE_DELAY | SHOT_AIM_GUIDANCE),
	         "one stack steers a front-gun shot at the nearest shootable hull by screen x");
	id = qa_guidance_fire(SHOT_REAR, plainGun);
	qa_check(qa_guidance_marked(id) && playerShotData[id].aimAtEnemy == 1, "...the rear gun's too");
	id = qa_guidance_fire(SHOT_LEFT_SIDEKICK, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id) && playerShotData[id].aimAtEnemy == 0,
	         "...but not a sidekick's at one stack");
	id = qa_guidance_fire(SHOT_SPECIAL, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id), "...nor a special's at any stack");
	id = qa_guidance_fire(SHOT_FRONT, guidedGun);
	int tightened = guidedOwn - ENDLESS_PERK_GUIDANCE_TIGHTEN;
	if (tightened < 1)
		tightened = 1;
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 1
	         && playerShotData[id].aimDelayMax == (tightened | SHOT_AIM_GUIDANCE),
	         "a gun that homes already turns tighter and is re-aimed at a shootable hull");

	// Two stacks: the sidekicks join and every interval shortens.
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 2;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_RIGHT_SIDEKICK, plainGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 1
	         && playerShotData[id].aimDelayMax
	            == ((ENDLESS_PERK_GUIDANCE_DELAY - ENDLESS_PERK_GUIDANCE_STEP) | SHOT_AIM_GUIDANCE),
	         "two stacks steer a sidekick's shot, on the shorter interval");
	id = qa_guidance_fire(SHOT_SPECIAL2, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id), "...and still leave the specials alone");

	// Three stacks: the specials join; the superbomb and chained bays never do.
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 3;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_SPECIAL, plainGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 1
	         && playerShotData[id].aimDelayMax
	            == ((ENDLESS_PERK_GUIDANCE_DELAY - 2 * ENDLESS_PERK_GUIDANCE_STEP) | SHOT_AIM_GUIDANCE),
	         "three stacks steer a special's shot, on a shorter interval");
	id = qa_guidance_fire(SHOT_SPECIAL2, plainGun);
	qa_check(qa_guidance_marked(id), "...from either special bay");
	id = qa_guidance_fire(SHOT_P1_SUPERBOMB, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id), "...but never a superbomb");
	id = qa_guidance_fire(SHOT_MISC, plainGun);
	qa_check(id < MAX_PWEAPON && !qa_guidance_marked(id), "...nor a chained child");

	// Four stacks: no bay gate reaches this far, and the interval bottoms out at every tick.
	qa_check(endlessPerkMaxStack(PERK_GUIDANCE) == 4, "the registry offers a fourth guidance stack");
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 4;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_FRONT, plainGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimDelayMax == (1 | SHOT_AIM_GUIDANCE),
	         "four stacks correct on every tick");

	// The correction, from one spot: a steered shot bends toward the screen x, the stock rule
	// toward the map x, and each reloads its own interval.
	PlayerShotDataType *s = &playerShotData[steered];
	s->shotX = 100;
	s->shotXM = 0;
	s->shotYM = -12;
	s->aimDelay = 1;
	player_shot_aim_step(s);
	qa_check(s->shotXM == -1 && s->shotYM == -13 && s->aimDelay == ENDLESS_PERK_GUIDANCE_DELAY,
	         "a steered shot bends toward the enemy's screen x and reloads its interval");
	PlayerShotDataType stock = *s;
	stock.aimDelayMax = (JE_byte)guidedOwn;
	stock.aimAtEnemy = 1;
	stock.shotXM = 0;
	player_shot_aim_step(&stock);
	qa_check(stock.shotXM == 1 && stock.aimDelay == guidedOwn,
	         "...where the stock rule bends toward its map x");

	// The enemy dies with a second hull standing to the right, then the field empties.
	enemyAvail[0] = 1;
	enemyAvail[5] = 0;
	enemy[5].ex = 200;
	enemy[5].mapoffset = 0;
	enemy[5].ey = 60;
	enemy[5].armorleft = 20;
	s->shotXM = 0;
	player_shot_aim_step(s);
	qa_check(s->aimAtEnemy == 6 && s->shotXM == 1,
	         "a steered shot whose enemy died moves on to the next shootable hull");
	stock.shotXM = 0;
	player_shot_aim_step(&stock);
	qa_check(stock.aimAtEnemy == 1 && stock.shotXM == -1,
	         "...while the stock rule veers off, as shipped");
	enemyAvail[5] = 1;
	s->shotXM = 0;
	player_shot_aim_step(s);
	qa_check(s->aimAtEnemy == 0 && s->shotXM == 0,
	         "...and flies straight once nothing shootable is left");

	/* A shot riding the ship is steered inside the ship's frame: a riding velocity (120 sits still
	 * beside the ship) takes the same nudge and never leaves its band, so the curve travels with the
	 * ship; a free velocity never crosses into the band either. */
	enemyAvail[5] = 0;   // the hull to the upper right stands again
	s->shotX = 100;
	s->shotY = 150;
	s->shotXM = 120;
	s->shotYM = -8;
	player_shot_aim_step(s);
	qa_check(s->aimAtEnemy == 6 && s->shotXM == 121 && s->shotYM == -9,
	         "a shot riding the ship curves toward the enemy inside the ship's frame");
	s->shotXM = SHOT_ATTACHED_VEL_MAX;
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_MAX, "...and its riding velocity is capped at the range's top");
	s->shotXM = SHOT_ATTACHED_VEL_MIN - 1;
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_MIN - 1,
	         "...while a free velocity never crosses into the riding range");
	enemy[5].ex = 0;   // now to the upper left: the nudges turn negative
	s->shotXM = SHOT_ATTACHED_VEL_MIN + 1;
	s->shotYM = SHOT_ATTACHED_VEL_MIN;
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_MIN + 1 && s->shotYM == SHOT_ATTACHED_VEL_MIN,
	         "...a riding velocity never falls out of the range on either axis");
	s->shotXM = SHOT_ATTACHED_VEL_MIN;
	player_shot_aim_step(s);
	qa_check(s->shotXM == SHOT_ATTACHED_VEL_MIN, "...and the value that pins both axes is left alone");
	enemy[5].ex = 200;

	const JE_word ridingGun = 203;  // Zica Laser level 5: every beam rides the ship on x
	qa_check(weapons[ridingGun].sx[0] > 100 && weapons[ridingGun].circlesize == 0,
	         "the guidance test's riding gun is the one it assumes");
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 1;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_FRONT, ridingGun);
	qa_check(qa_guidance_marked(id) && playerShotData[id].shotXM >= SHOT_ATTACHED_VEL_MIN
	         && playerShotData[id].aimAtEnemy == 6,
	         "a gun that rides the ship is steered and keeps riding it");

	// A recycled slot: the mark must not outlive the shot that carried it.
	memset(shotAvail, 0, sizeof(shotAvail));
	endlessPerkTakenBy[0][PERK_GUIDANCE] = 0;
	endlessPerkRederive();
	id = qa_guidance_fire(SHOT_FRONT, plainGun);
	qa_check(id == steered && !qa_guidance_marked(id) && playerShotData[id].aimAtEnemy == 0,
	         "an unsteered shot in a recycled slot does not inherit the guidance mark");
	memset(shotAvail, 0, sizeof(shotAvail));

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	power = savedPower;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
	guidedShotScreenAim = savedGuidedAim;
}

/* Twin Pods: where the two volleys land around each pod, that the perk stays personal in co-op, and
 * the two refusals (no first volley, no generator power for a second). */
static void qa_test_twin_pods_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const uint savedPower = power;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);
	power = 10000;
	memset(shotAvail, 0, sizeof(shotAvail));

	const JE_word podPort = 16;   // Miscellaneous Option Weapons: the port every stock sidekick fires through
	const JE_word podGun = 1;     // Pulse-Cannon level 1: one shot, one pattern position
	qa_check(weaponPort[podPort].poweruse > 0 && weapons[podGun].multi == 1 && weapons[podGun].max <= 1,
	         "the twin pods test's port and gun are the ones it assumes");
	const int off = ENDLESS_PERK_TWINPODS_SPREAD_PX / 2;

	// Without the perk there is no offset and nothing fires. The lone shot marks the pod's centre line.
	qa_check(endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == 0 && endlessPerkTwinPodOffset(0, RIGHT_SIDEKICK) == 0,
	         "without Twin Pods a sidekick has no twin offset");
	JE_integer first = player_shot_create(podPort, SHOT_LEFT_SIDEKICK, 100, 150, 0, 0, podGun, 1);
	qa_check(first < MAX_PWEAPON
	         && player_shot_create_twin(first, podPort, LEFT_SIDEKICK, 0, 100, 150, 0, 0, podGun, 1) == MAX_PWEAPON,
	         "...and no twin volley fires");
	const int centreX = (first < MAX_PWEAPON) ? playerShotData[first].shotX : 0;
	memset(shotAvail, 0, sizeof(shotAvail));

	// The perk: the twin half the spread outboard of each pod, so the pair mirrors across the ship.
	endlessPerkTakenBy[0][PERK_TWINPODS] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == -off && endlessPerkTwinPodOffset(0, RIGHT_SIDEKICK) == off,
	         "the twin leaves half the spread left of the left pod and right of the right pod");
	endlessMode = false;
	qa_check(endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == 0, "...but only in an endless run");
	endlessMode = true;
	qa_check(endlessPerkTwinPodOffset(1, LEFT_SIDEKICK) == -off,
	         "...and outside co-op a second ship flies off the only perk row there is");

	/* Personal in co-op, and read off the named ship rather than the effect context: the partner's
	 * pods stay single however the context is pointed, which is what keeps two machines simulating
	 * the same volley from the same perk rows. */
	coopEndlessMode = true;
	endlessSetFxPlayer(0);
	qa_check(endlessPerkTwinPodOffset(1, LEFT_SIDEKICK) == 0 && endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == -off,
	         "a partner without Twin Pods keeps single pods while the holder's twin");
	endlessSetFxPlayer(1);
	qa_check(endlessPerkTwinPodOffset(1, LEFT_SIDEKICK) == 0 && endlessPerkTwinPodOffset(0, LEFT_SIDEKICK) == -off,
	         "...whichever ship the effect context names");
	endlessPerkTakenBy[1][PERK_TWINPODS] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkTwinPodOffset(1, RIGHT_SIDEKICK) == off,
	         "...and a partner who takes it too gets the same spread");
	endlessPerkTakenBy[1][PERK_TWINPODS] = 0;
	endlessPerkRederive();
	coopEndlessMode = false;
	endlessSetFxPlayer(0);

	// As the fire sites do it: the own volley the same distance inboard, the twin from the pod's x.
	const int leftDx = endlessPerkTwinPodOffset(0, LEFT_SIDEKICK),
	          rightDx = endlessPerkTwinPodOffset(0, RIGHT_SIDEKICK);
	first = player_shot_create(podPort, SHOT_LEFT_SIDEKICK, 100 - leftDx, 150, 0, 0, podGun, 1);
	JE_integer twin = player_shot_create_twin(first, podPort, LEFT_SIDEKICK, leftDx,
	                                          100, 150, 0, 0, podGun, 1);
	qa_check(first < MAX_PWEAPON && twin < MAX_PWEAPON && twin != first
	         && playerShotData[first].shotX == centreX + off && playerShotData[twin].shotX == centreX - off
	         && playerShotData[twin].shotY == playerShotData[first].shotY
	         && playerShotData[twin].shotDmg == playerShotData[first].shotDmg,
	         "a left pod's two volleys are the same shot, centred on the pod, the twin outboard");
	first = player_shot_create(podPort, SHOT_RIGHT_SIDEKICK, 100 - rightDx, 150, 0, 0, podGun, 1);
	twin = player_shot_create_twin(first, podPort, RIGHT_SIDEKICK, rightDx, 100, 150, 0, 0, podGun, 1);
	qa_check(twin < MAX_PWEAPON && playerShotData[first].shotX == centreX - off
	         && playerShotData[twin].shotX == centreX + off,
	         "...and a right pod's mirror them");
	qa_check(player_shot_create_twin(MAX_PWEAPON, podPort, LEFT_SIDEKICK, -off,
	                                 100, 150, 0, 0, podGun, 1) == MAX_PWEAPON,
	         "a refused first volley fires no twin");
	memset(shotAvail, 0, sizeof(shotAvail));

	// The twin pays the generator itself: enough for one volley is enough for one.
	const uint volleyCost = weaponPort[podPort].poweruse;
	power = volleyCost + volleyCost / 2;
	first = player_shot_create(podPort, SHOT_LEFT_SIDEKICK, 100, 150, 0, 0, podGun, 1);
	twin = player_shot_create_twin(first, podPort, LEFT_SIDEKICK, -off, 100, 150, 0, 0, podGun, 1);
	qa_check(first < MAX_PWEAPON && twin == MAX_PWEAPON && power == volleyCost / 2,
	         "a generator that can pay for one volley fires the pod's own and refuses the twin");
	power = 2 * volleyCost;
	first = player_shot_create(podPort, SHOT_LEFT_SIDEKICK, 100, 150, 0, 0, podGun, 1);
	twin = player_shot_create_twin(first, podPort, LEFT_SIDEKICK, -off, 100, 150, 0, 0, podGun, 1);
	qa_check(first < MAX_PWEAPON && twin < MAX_PWEAPON && power == 0,
	         "...and one that can pay for two fires both, spending both");
	memset(shotAvail, 0, sizeof(shotAvail));

	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	power = savedPower;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

/* Reinforced Prow: the ram figures per stack, the invulnerable-ram cadence, and the Endless ram
 * kill going through the shot's own destruction walk (payout, tally, a linked hull down whole). */
static void qa_test_reinforced_prow_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	struct JE_SingleEnemyType savedEnemy[8];
	JE_byte savedAvail[COUNTOF(savedEnemy)];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	const JE_word savedKilled = enemyKilled;
	const Sint64 savedCash = player[0].cash;
	const Uint64 savedEarned = endlessRunCashEarned, savedSpent = endlessRunCashSpent;
	Uint64 savedSources[ENDLESS_CASH_SOURCES], savedSinks[ENDLESS_CASH_SINKS];
	memcpy(savedSources, endlessCashBySource, sizeof(savedSources));
	memcpy(savedSinks, endlessCashBySink, sizeof(savedSinks));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);
	endlessCashResync();

	qa_check(endlessPerkProwRamDamage(2) == 2 && endlessPerkProwContactPercent() == 100,
	         "without Reinforced Prow a ram deals and costs the stock figure");
	static const int dealt[] = { 4, 6, 8, 10 };
	static const int taken[] = { 78, 56, 34, 12 };
	bool ladder = true;
	for (int s = 1; s <= 4; ++s)
	{
		endlessPerkTakenBy[0][PERK_PROW] = (JE_byte)s;
		endlessPerkRederive();
		ladder &= endlessPerkProwRamDamage(2) == dealt[s - 1]
		       && endlessPerkProwContactPercent() == taken[s - 1];
	}
	qa_check(ladder, "the stacks deal x2 to x5 and take 78%, 56%, 34%, 12%");
	endlessMode = false;
	qa_check(endlessPerkProwRamDamage(2) == 2 && endlessPerkProwContactPercent() == 100,
	         "...only in an endless run");
	endlessMode = true;

	/* The ram site's own arithmetic: an open Opening Salvo window lifts the Reinforced Prow figure,
	 * and Knife Fight's bonus comes off that same unlifted figure, the two summed. Neither bonus
	 * reaches the ship's own share. */
	const int savedWindow = endlessSalvoWindow[0];
	endlessSalvoWindow[0] = 0;
	const int stockRam = endlessPerkProwRamDamage(2);
	const int knifeRam = endlessPerkKnifeFightBonus(stockRam, 50);
	qa_check(stockRam == 10 && knifeRam == 5
	         && endlessOpeningSalvoScale(stockRam) + knifeRam == 15,
	         "with no salvo window a four-stack ram is its stock x5 plus Knife Fight");
	endlessSalvoWindow[0] = ENDLESS_PERK_SALVO_WINDOW;
	qa_check(endlessOpeningSalvoScale(stockRam) == 25
	         && endlessOpeningSalvoScale(stockRam) + knifeRam == 30,
	         "...and an open Opening Salvo window lifts that ram 2.5x, Knife Fight added to it");
	qa_check(endlessPerkKnifeFightBonus(stockRam, 50) == knifeRam,
	         "...and Knife Fight's bonus reads the same inside the window as outside");
	qa_check(endlessPerkProwContactPercent() == 12,
	         "...and never touching what that ram costs the ship");
	endlessSalvoWindow[0] = savedWindow;

	// Personal: the partner rams on its own stacks.
	coopEndlessMode = true;
	endlessSetFxPlayer(1);
	qa_check(endlessPerkProwRamDamage(2) == 2 && endlessPerkProwContactPercent() == 100,
	         "a co-op partner without the perk rams at stock");
	endlessSetFxPlayer(0);
	qa_check(endlessPerkProwRamDamage(2) == 10, "...while the holder rams at its stacks");
	coopEndlessMode = false;

	// An invulnerable ship lands a ram every tenth tick of its window in Endless, never outside it.
	qa_check(endlessRamWhileInvulnerable(20) && endlessRamWhileInvulnerable(10)
	         && !endlessRamWhileInvulnerable(19) && !endlessRamWhileInvulnerable(1),
	         "an invulnerable ship rams on the tick cadence");
	endlessMode = false;
	qa_check(!endlessRamWhileInvulnerable(20), "...and outside Endless never");
	endlessMode = true;

	/* The Endless ram kill: enemy_kill_group takes a lone enemy and pays it out, and takes a linked
	 * hull whole. Both are what a killing shot does. */
	for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	enemyAvail[0] = 0;
	enemy[0].ex = 100;
	enemy[0].ey = 100;
	enemy[0].armorleft = 1;
	enemy[0].enemytype = 1;
	enemy[0].evalue = 300;
	const Sint64 loneCash = player[0].cash;
	const JE_word loneKilled = enemyKilled;
	enemy_kill_group(0, 0, 0);
	qa_check(enemyAvail[0] == 1 && player[0].cash == loneCash + 300 && enemyKilled == loneKilled + 1,
	         "a ram kill removes the enemy, pays its worth and counts");

	for (uint i = 0; i < 3; ++i)
	{
		enemyAvail[i] = 0;
		enemy[i].ex = (JE_integer)(100 + i * 8);
		enemy[i].ey = 100;
		enemy[i].linknum = 7;
		enemy[i].armorleft = 1;
		enemy[i].enemytype = 1;
		enemy[i].evalue = 100;
	}
	enemyAvail[3] = 0;   // a bystander with another link stays
	enemy[3].ex = 200;
	enemy[3].ey = 100;
	enemy[3].linknum = 9;
	enemy[3].armorleft = 5;
	enemy[3].enemytype = 1;
	const Sint64 hullCash = player[0].cash;
	enemy_kill_group(1, 0, 0);
	qa_check(enemyAvail[0] == 1 && enemyAvail[1] == 1 && enemyAvail[2] == 1 && enemyAvail[3] == 0
	         && player[0].cash == hullCash + 300,
	         "...and a linked hull goes down whole, every tile paid, its neighbour untouched");
	JE_resetSP();
	memset(explosions, 0, sizeof(explosions));   // the deaths above puffed; explosions[] is registered

	enemyKilled = savedKilled;
	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	player[0].cash = savedCash;
	endlessRunCashEarned = savedEarned;
	endlessRunCashSpent = savedSpent;
	memcpy(endlessCashBySource, savedSources, sizeof(savedSources));
	memcpy(endlessCashBySink, savedSinks, sizeof(savedSinks));
	endlessCashResync();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

/* Knife Fight: the hull-to-hull gap, the bonus ladder over it, a linked hull measured to its nearest
 * tile, and the perk staying with the ship that flies close in co-op. */
static void qa_test_knife_fight_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	struct JE_SingleEnemyType savedEnemy[8];
	JE_byte savedAvail[COUNTOF(savedEnemy)];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));
	memcpy(savedAvail, enemyAvail, sizeof(savedAvail));
	const Player savedPlayers[2] = { player[0], player[1] };

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);
	for (uint i = 0; i < COUNTOF(savedEnemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}

	// The ship's hull covers x-5..x+18, y-7..y+20; a lone 12x14 tile at (ex, ey) covers ex..ex+11,
	// ey..ey+13. With the ship at (100, 150) a tile at (100, ey) sits 129 - ey px above it.
	player[0].x = 100;
	player[0].y = 150;
	enemyAvail[0] = 0;
	enemy[0].ex = 100;
	enemy[0].ey = 100;
	enemy[0].armorleft = 50;
	enemy[0].enemytype = 1;
	qa_check(endlessShipHullGapPx(0, 0) == 29, "the gap is measured between the two hulls' edges");
	enemy[0].ey = 130;
	qa_check(endlessShipHullGapPx(0, 0) == 0, "...and is zero once they overlap");
	enemy[0].ey = 100;
	enemy[0].ex = 160;   // 41 px clear on x, 29 on y: the wider clearance is the gap
	qa_check(endlessShipHullGapPx(0, 0) == 41, "...and the larger per-axis clearance decides");
	enemy[0].ex = 100;
	enemy[0].size = 1;   // a 2x2 body reaches 7 px further down
	qa_check(endlessShipHullGapPx(0, 0) == 22, "a 2x2 body is measured to its outer edge");
	enemy[0].size = 0;

	qa_check(endlessPerkKnifeFightPercent(0) == 0
	         && endlessPerkKnifeFightBonus(20, endlessPerkKnifeFightPercent(0)) == 0,
	         "without Knife Fight there is no bonus");
	endlessPerkTakenBy[0][PERK_KNIFE] = 4;
	endlessPerkRederive();
	const int fullY = 129 - ENDLESS_PERK_KNIFE_FULL_PX;   // the tile that sits exactly the full gap away
	enemy[0].ey = (JE_integer)fullY;
	qa_check(endlessPerkKnifeFightPercent(0) == 4 * ENDLESS_PERK_KNIFE_PCT
	         && endlessPerkKnifeFightBonus(20, endlessPerkKnifeFightPercent(0))
	            == 20 * 4 * ENDLESS_PERK_KNIFE_PCT / 100,
	         "at the full gap four stacks give the whole bonus");
	enemy[0].ey = 130;
	qa_check(endlessPerkKnifeFightPercent(0) == 4 * ENDLESS_PERK_KNIFE_PCT,
	         "...touching gives the same");
	enemy[0].ey = (JE_integer)(fullY - ENDLESS_PERK_KNIFE_FADE_PX / 2);   // half way through the fade
	qa_check(endlessPerkKnifeFightPercent(0) == 4 * ENDLESS_PERK_KNIFE_PCT / 2,
	         "half way out it is half");
	enemy[0].ey = (JE_integer)(fullY - ENDLESS_PERK_KNIFE_FADE_PX);
	qa_check(endlessPerkKnifeFightPercent(0) == 0, "...and at the end of the fade nothing");
	enemy[0].ey = (JE_integer)fullY;
	endlessPerkTakenBy[0][PERK_KNIFE] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkKnifeFightPercent(0) == ENDLESS_PERK_KNIFE_PCT, "one stack is one step");
	endlessMode = false;
	qa_check(endlessPerkKnifeFightPercent(0) == 0, "...only in an endless run");
	endlessMode = true;

	// A linked hull is measured to whichever tile is nearest, though the hit landed elsewhere.
	enemy[0].ey = 100;
	enemy[0].linknum = 7;
	enemyAvail[1] = 0;
	enemy[1].ex = 100;
	enemy[1].ey = (JE_integer)fullY;
	enemy[1].linknum = 7;
	enemy[1].armorleft = 50;
	enemy[1].enemytype = 1;
	qa_check(endlessShipHullGapPx(0, 0) == ENDLESS_PERK_KNIFE_FULL_PX
	         && endlessPerkKnifeFightPercent(0) == ENDLESS_PERK_KNIFE_PCT,
	         "a hit on a far tile of a linked hull is measured to its nearest tile");
	enemyAvail[1] = 1;
	qa_check(endlessShipHullGapPx(0, 0) == 29, "...a dead tile no longer counts");
	enemy[0].linknum = 0;

	// Personal, and measured from the owner's own ship: the partner is far away, the holder close.
	coopEndlessMode = true;
	player[1].x = 100;
	player[1].y = 150;
	player[0].x = 20;
	player[0].y = 20;
	enemy[0].ey = (JE_integer)fullY;
	endlessPerkTakenBy[0][PERK_KNIFE] = 0;
	endlessPerkTakenBy[1][PERK_KNIFE] = 2;
	endlessPerkRederive();
	endlessSetFxPlayer(1);
	qa_check(endlessPerkKnifeFightPercent(0) == 2 * ENDLESS_PERK_KNIFE_PCT,
	         "in co-op the holder's shot is measured from the holder's ship");
	endlessSetFxPlayer(0);
	qa_check(endlessPerkKnifeFightPercent(0) == 0, "...and the partner without it gets nothing");
	endlessSetFxPlayer(1);
	player[1].y = 20;
	qa_check(endlessPerkKnifeFightPercent(0) == 0, "...nor the holder from far away");
	coopEndlessMode = false;
	endlessSetFxPlayer(0);

	player[0] = savedPlayers[0];
	player[1] = savedPlayers[1];
	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	memcpy(enemyAvail, savedAvail, sizeof(savedAvail));
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

// Deflector damage, ownership, shot reversal, and Opening Salvo inheritance.
static void qa_test_deflector_perk(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	int savedSalvo[2];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	memcpy(savedSalvo, endlessSalvoWindow, sizeof(savedSalvo));
	memset(endlessSalvoWindow, 0, sizeof(endlessSalvoWindow));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	qa_check(endlessPerkDeflectDamage(6) == 0, "without Deflector a shield hit returns nothing");
	endlessPerkTakenBy[0][PERK_DEFLECTOR] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkDeflectDamage(6) == 6 && endlessPerkDeflectDamage(0) == 0,
	         "one stack returns the absorbed damage, and nothing for nothing");
	endlessPerkTakenBy[0][PERK_DEFLECTOR] = 2;
	endlessPerkRederive();
	qa_check(endlessPerkDeflectDamage(6) == 12, "two stacks return it doubled");
	qa_check(endlessPerkDeflectDamage(200) == 249 && endlessPerkDeflectDamage(50) == 100,
	         "...clear of the piercing and ice markers");
	endlessMode = false;
	qa_check(endlessPerkDeflectDamage(6) == 0, "...only in an endless run");
	endlessMode = true;

	coopEndlessMode = true;
	endlessSetFxPlayer(1);
	qa_check(endlessPerkDeflectDamage(6) == 0, "a co-op partner without the perk returns nothing");
	endlessSetFxPlayer(0);
	coopEndlessMode = false;

	// The shield refund uses the same perk stacks and never reaches the absorbed amount.
	qa_check(endlessPerkDeflectShieldSpared(10) == 3 && endlessPerkDeflectShieldSpared(100) == 34,
	         "two stacks spare 34% of what the shield stopped");
	endlessPerkTakenBy[0][PERK_DEFLECTOR] = 1;
	endlessPerkRederive();
	qa_check(endlessPerkDeflectShieldSpared(10) == 2 && endlessPerkDeflectShieldSpared(2) == 0,
	         "one stack spares 17%, in whole points, and a hit too small to discount spares none");
	bool overSpared = false;
	for (int hit = 1; hit <= 255; ++hit)
		overSpared |= (endlessPerkDeflectShieldSpared(hit) >= hit);
	qa_check(!overSpared, "...and no hit ever leaves the shield more than it spent");
	endlessMode = false;
	qa_check(endlessPerkDeflectShieldSpared(10) == 0,
	         "...outside an endless run the shield spends it all");
	endlessMode = true;
	coopEndlessMode = true;
	endlessSetFxPlayer(1);
	qa_check(endlessPerkDeflectShieldSpared(10) == 0, "a co-op partner without the perk spares nothing");
	endlessSetFxPlayer(0);
	coopEndlessMode = false;
	endlessPerkTakenBy[0][PERK_DEFLECTOR] = 2;
	endlessPerkRederive();

	// The returned shot: player two's, at the bullet, velocity and acceleration reversed.
	memset(shotAvail, 0, sizeof(shotAvail));
	EnemyShotType incoming;
	memset(&incoming, 0, sizeof(incoming));
	incoming.sx = 120;
	incoming.sy = 90;
	incoming.sxm = 3;
	incoming.sym = 5;
	incoming.sxc = 1;
	incoming.syc = -1;
	incoming.sgr = 12;
	incoming.animate = 1;
	incoming.animax = 3;
	incoming.filter = ENDLESS_CHAMPION_FILTER;
	const JE_integer id = player_shot_create_deflected(&incoming, 12, 2);
	qa_check(id < MAX_PWEAPON && shotAvail[id] > 0, "a returned shot takes a pool slot");
	if (id < MAX_PWEAPON)
	{
		const PlayerShotDataType *s = &playerShotData[id];
		qa_check(s->shotX == 120 && s->shotY == 90 && s->shotXM == -3 && s->shotYM == -5
		         && s->shotXC == -1 && s->shotYC == 1,
		         "...leaving from the bullet along the reverse of its path");
		qa_check(s->playerNumber == 2 && s->shotDmg == 12 && s->shotGr == 12 && s->shotAni == 1
		         && s->shotAniMax == 3 && s->tint == ENDLESS_CHAMPION_FILTER,
		         "...as the hit ship's own shot, at the returned damage, in the bullet's look and tier");
		qa_check(s->aimAtEnemy == 0 && s->aimDelayMax == 0 && !s->shotComplicated && s->chainReaction == 0
		         && s->salvoBoost == 0 && s->pierceLock == 0,
		         "...unsteered, and untagged outside a salvo window");
	}

	/* A deflection inside a charged Opening Salvo window joins that volley, and the window it joins
	 * is the deflecting ship's own. */
	memset(shotAvail, 0, sizeof(shotAvail));
	endlessSalvoWindow[0] = ENDLESS_PERK_SALVO_WINDOW;
	endlessSalvoWindow[1] = 0;
	coopEndlessMode = true;
	const JE_integer inSalvo = player_shot_create_deflected(&incoming, 12, 1);
	qa_check(inSalvo < MAX_PWEAPON && playerShotData[inSalvo].salvoBoost == 1,
	         "a deflection inside a charged window carries the volley's tag");
	const JE_integer partnerShot = player_shot_create_deflected(&incoming, 12, 2);
	qa_check(partnerShot < MAX_PWEAPON && playerShotData[partnerShot].salvoBoost == 0,
	         "...and the partner, whose own window is not running, deflects untagged");
	coopEndlessMode = false;
	memset(endlessSalvoWindow, 0, sizeof(endlessSalvoWindow));
	endlessSetFxPlayer(0);
	// The slot recycled by a gun's shot sheds the tint; the ordinary create keeps its own colours.
	memset(shotAvail, 0, sizeof(shotAvail));
	const uint savedPower = power;
	power = 10000;
	const JE_integer plain = player_shot_create(1, SHOT_FRONT, 100, 150, 0, 0, 1, 1);
	qa_check(plain < MAX_PWEAPON && playerShotData[plain].tint == 0,
	         "a gun's shot in the same slot flies untinted");
	power = savedPower;
	memset(shotAvail, 0, sizeof(shotAvail));

	incoming.sxm = 0;
	incoming.sym = 0;
	incoming.animax = 0;
	const JE_integer still = player_shot_create_deflected(&incoming, 3, 1);
	qa_check(still < MAX_PWEAPON && playerShotData[still].shotYM < 0 && playerShotData[still].shotAniMax == 1,
	         "a bullet that had stopped leaves straight up, and a still sprite stays on its frame");
	incoming.sgr = 65535;
	qa_check(player_shot_create_deflected(&incoming, 3, 1) == MAX_PWEAPON
	         && player_shot_create_deflected(&incoming, 0, 1) == MAX_PWEAPON,
	         "no shot for a frame past the sheets or for no damage");
	memset(shotAvail, 0, sizeof(shotAvail));

	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	memcpy(endlessSalvoWindow, savedSalvo, sizeof(savedSalvo));
	endlessPerkRederive();
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

/* Countermeasures clears shots within hull-relative reach and has no cooldown. */
static void qa_test_countermeasure_burst(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	const Player savedPlayer = player[0];
	JE_byte savedPerks[2][PERK_COUNT];
	EnemyShotType savedShots[8];
	JE_boolean savedAvail[ENEMY_SHOT_MAX];
	Explosion savedExplosions[MAX_EXPLOSIONS];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	memcpy(savedShots, enemyShot, sizeof(savedShots));
	memcpy(savedAvail, enemyShotAvail, sizeof(savedAvail));
	memcpy(savedExplosions, explosions, sizeof(savedExplosions));

	endlessMode = true;
	endlessCampaignMods = false;
	coopEndlessMode = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	player[0].x = 140;
	player[0].y = 100;
	player[0].shot_hit_area_x = 12;
	player[0].shot_hit_area_y = 10;
	const int reachX = 12 + ENDLESS_PERK_CM_RADIUS1;
	const int reachY = 10 + ENDLESS_PERK_CM_RADIUS1;

	// Check both axes because the hitbox is not square.
	memset(enemyShotAvail, 1, sizeof(enemyShotAvail));   // an empty sky, so the counts below are exact
	for (uint i = 0; i < COUNTOF(savedShots); ++i)
	{
		memset(&enemyShot[i], 0, sizeof(enemyShot[i]));
		enemyShotAvail[i] = 0;
		enemyShot[i].sx = 140;
		enemyShot[i].sy = 100;
		enemyShot[i].sgr = 270;
	}
	enemyShot[4].sx = (JE_integer)(140 + reachX);
	enemyShot[5].sx = (JE_integer)(140 + reachX + 1);
	enemyShot[6].sy = (JE_integer)(100 + reachY);
	enemyShot[7].sy = (JE_integer)(100 + reachY + 1);

	qa_check(endlessCountermeasureBurst(&player[0]) == 0 && enemyShotAvail[0] == 0,
	         "without the perk a hull hit sweeps nothing");

	endlessPerkTakenBy[0][PERK_COUNTERMEASURE] = 1;
	endlessPerkRederive();
	qa_check(endlessCountermeasureBurst(&player[0]) == 6,
	         "the burst takes every bullet out to its reach, measured past the hitbox");
	qa_check(enemyShotAvail[0] && enemyShotAvail[4] && enemyShotAvail[6]
	         && !enemyShotAvail[5] && !enemyShotAvail[7],
	         "...and leaves the ones a pixel beyond it, on either axis, flying");

	endlessPerkTakenBy[0][PERK_COUNTERMEASURE] = 2;
	endlessPerkRederive();
	qa_check(endlessCountermeasureBurst(&player[0]) == 2
	         && enemyShotAvail[5] && enemyShotAvail[7],
	         "a second stack reaches the bullets one stack left flying");
	qa_check(endlessCountermeasureBurst(&player[0]) == 0,
	         "a burst over cleared air takes nothing");

	// Each consecutive hit must clear a fresh shot without a recharge tick.
	endlessPerkTakenBy[0][PERK_COUNTERMEASURE] = 1;
	endlessPerkRederive();
	bool everyHit = true;
	for (uint hit = 0; hit < 8; ++hit)
	{
		enemyShotAvail[0] = 0;
		enemyShot[0].sx = 140;
		enemyShot[0].sy = 100;
		everyHit = everyHit && endlessCountermeasureBurst(&player[0]) == 1 && enemyShotAvail[0];
	}
	qa_check(everyHit, "consecutive hull hits each fire: nothing has to recharge between them");

	endlessMode = false;
	enemyShotAvail[0] = 0;
	qa_check(endlessCountermeasureBurst(&player[0]) == 0 && enemyShotAvail[0] == 0,
	         "...and none of it happens outside an endless run");

	memcpy(enemyShot, savedShots, sizeof(savedShots));
	memcpy(enemyShotAvail, savedAvail, sizeof(savedAvail));
	memcpy(explosions, savedExplosions, sizeof(savedExplosions));
	JE_resetSP();
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	player[0] = savedPlayer;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
	coopEndlessMode = savedCoop;
}

/* Guided Aim (guidedShotScreenAim): the weapon-table homing picks and chases the enemy's screen x
 * instead of its map x. Nothing else about the stock rule moves: it still takes any non-free,
 * non-pickup slot and still veers off when its enemy dies. */
static void qa_test_guided_screen_aim(void)
{
	const JE_boolean savedEndless = endlessMode, savedCampaign = endlessCampaignMods;
	const bool savedGuidedAim = guidedShotScreenAim;
	const uint savedPower = power;

	endlessMode = false;   // no perk in the way: the weapon-table branch alone decides
	endlessCampaignMods = false;
	power = 10000;
	memset(shotAvail, 0, sizeof(shotAvail));

	const JE_word guidedGun = 587;  // Heavy Guided Bombs: one shot that homes on its own
	qa_check(weapons[guidedGun].aim > 5 && weapons[guidedGun].multi == 1,
	         "the Guided Aim test's gun is the one it assumes");
	const int guidedOwn = weapons[guidedGun].aim - 5;

	/* Two enemies level with each other, both right of the shot on the map: A is the nearer by
	 * map x, B by screen x. B is an invulnerable part, which the stock rule takes either way. */
	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	enemyAvail[0] = 0;
	enemy[0].ex = 110;
	enemy[0].mapoffset = 30;    // screen x 140
	enemy[0].ey = 100;
	enemy[0].armorleft = 20;
	enemyAvail[1] = 0;
	enemy[1].ex = 140;
	enemy[1].mapoffset = -40;   // screen x 100, level with the shot
	enemy[1].ey = 100;
	enemy[1].armorleft = 255;

	guidedShotScreenAim = false;
	JE_integer id = qa_guidance_fire(SHOT_FRONT, guidedGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 1
	         && playerShotData[id].aimDelayMax == guidedOwn,
	         "with Guided Aim off a guided shot picks the nearest enemy by map x");
	memset(shotAvail, 0, sizeof(shotAvail));

	guidedShotScreenAim = true;
	id = qa_guidance_fire(SHOT_FRONT, guidedGun);
	qa_check(id < MAX_PWEAPON && playerShotData[id].aimAtEnemy == 2
	         && playerShotData[id].aimDelayMax == guidedOwn,
	         "with Guided Aim on it picks by screen x, invulnerable part included, on its own interval");

	// The correction from one spot: B's map x is right of the shot, its screen x is on the shot.
	PlayerShotDataType *s = &playerShotData[id];
	s->shotX = 100;
	s->shotXM = 0;
	s->shotYM = -12;
	s->aimDelay = 1;
	player_shot_aim_step(s);
	qa_check(s->shotXM == -1 && s->shotYM == -13 && s->aimDelay == guidedOwn,
	         "a Guided Aim shot bends toward the enemy's screen x and reloads its interval");
	guidedShotScreenAim = false;
	s->shotXM = 0;
	player_shot_aim_step(s);
	qa_check(s->shotXM == 1, "...where the shipped rule bends toward its map x");

	// The enemy dies: the stock veer-off still applies, and no retarget happens.
	guidedShotScreenAim = true;
	enemyAvail[1] = 1;
	s->shotXM = -1;
	player_shot_aim_step(s);
	qa_check(s->aimAtEnemy == 2 && s->shotXM == -2,
	         "a Guided Aim shot whose enemy died veers off like the shipped rule");
	memset(shotAvail, 0, sizeof(shotAvail));

	for (uint i = 0; i < COUNTOF(enemy); ++i)
	{
		memset(&enemy[i], 0, sizeof(enemy[i]));
		enemyAvail[i] = 1;
	}
	guidedShotScreenAim = savedGuidedAim;
	power = savedPower;
	endlessMode = savedEndless;
	endlessCampaignMods = savedCampaign;
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

	// Wrecks clear both direct tier tint and borrowed shell tint.
	const JE_byte savedAvail = enemyAvail[0];
	const struct JE_SingleEnemyType savedEnemy = enemy[0];

	enemyAvail[0] = 0;
	enemy[0].edamaged = false;
	enemy[0].eliteState = 3;
	enemy[0].linknum = 64;
	enemy[0].armorleft = 255;
	qa_check(enemy_body_tint(0) == ENDLESS_CHAMPION_FILTER,
	         "a live champion paints in its own bank");
	enemy[0].eliteState = 1;
	qa_check(enemy_body_tint(0) == groupTint,
	         "...and its sealed plating in the one the group lends");

	enemyAvail[0] = 2;
	enemy[0].edamaged = true;
	qa_check(enemy_is_wreck(0) && enemy_body_tint(0) == 0,
	         "a wreck stops borrowing the group's bank");
	enemy[0].eliteState = 3;
	qa_check(enemy_body_tint(0) == 0, "...and a champion's wreck drops the bank it wore itself");

	enemy[0] = savedEnemy;
	enemyAvail[0] = savedAvail;

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

/* Homing eligibility for active, scripted, linked, and pickup bodies. */
static void qa_test_homing_chaser_eligibility(void)
{
	const bool savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	const JE_word savedMaxEvent = maxEvent;
	struct JE_EventRecType savedEvents[2];
	memcpy(savedEvents, eventRec, sizeof(savedEvents));

	endlessMode = true;
	endlessCampaignMods = false;

	// Link 64 opens through an armor event; link 7 never opens.
	memset(eventRec, 0, sizeof(savedEvents));
	eventRec[0].eventtype = 25;
	eventRec[0].eventdat = 200;
	eventRec[0].eventdat4 = 64;
	maxEvent = 1;
	endlessResetElites();

	qa_check(endlessEnemyDestructible(0, 0, 40), "a damageable body can be shot");
	qa_check(!endlessEnemyDestructible(2, 0, 40),
	         "...but avail 2 carries no health the shot loop will look at");
	qa_check(endlessEnemyDestructible(0, 64, 255) && !endlessEnemyDestructible(0, 7, 255),
	         "a sealed body waits on its link group's damage event");

	// A destructible weak point shares link 12 with permanent scenery.
	const JE_byte savedAvail[3] = { enemyAvail[0], enemyAvail[1], enemyAvail[2] };
	struct JE_SingleEnemyType savedEnemy[3];
	memcpy(savedEnemy, enemy, sizeof(savedEnemy));

	memset(enemy, 0, sizeof(savedEnemy));
	enemyAvail[0] = 0;
	enemy[0].linknum = 12;
	enemy[0].armorleft = 40;
	enemyAvail[1] = 2;
	enemy[1].linknum = 12;
	enemy[1].armorleft = 255;
	enemyAvail[2] = 0;
	enemy[2].linknum = 0;
	enemy[2].armorleft = 40;
	endlessScanSceneryLinks();

	qa_check(!endlessHomingChaser(0) && !endlessHomingChaser(1),
	         "a span keeps still even where a destructible weak point sits inside it");
	qa_check(endlessHomingChaser(2), "an unlinked damageable body still gives chase");

	enemy[1].scoreitem = true;
	endlessScanSceneryLinks();
	qa_check(endlessHomingChaser(0),
	         "loot riding a link number is not scenery and poisons nothing");
	qa_check(!endlessHomingChaser(1), "...and the pickup itself never gives chase");

	// The tiers that floor tracking are also the ones whose wreckage would trail the ship.
	const Uint64 savedMods = endlessActiveMods;
	const Uint64 tiers[] = { ENDLESS_MOD_HOMING, ENDLESS_MOD_KAMIKAZE, ENDLESS_MOD_RAMPAGE };
	bool everyTier = true;
	for (unsigned t = 0; t < COUNTOF(tiers); ++t)
	{
		endlessActiveMods = tiers[t];
		everyTier = everyTier && endlessHomingTierActive();
	}
	endlessActiveMods = 0;
	qa_check(!endlessHomingTierActive(), "a sector with no homing tier keeps its wreckage");
	qa_check(everyTier, "...and every tier that floors tracking drops the corpse instead");
	endlessActiveMods = savedMods;

	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	for (unsigned i = 0; i < COUNTOF(savedAvail); ++i)
		enemyAvail[i] = savedAvail[i];

	memcpy(eventRec, savedEvents, sizeof(savedEvents));
	maxEvent = savedMaxEvent;
	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
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

	// Local opacity stays outside registered snapshot bytes.
	explosionOpacity = 8;
	JE_setupExplosion(100, 100, 0, 14, false, false);
	explosionOpacity = NET_STYLE_SOLID;
	JE_setupExplosion(100, 100, 0, 14, false, false);
	qa_check(explosion_opacity(0) == 8 && explosion_opacity(1) == NET_STYLE_SOLID,
	         "an explosion keeps the opacity set when it was spawned");
	memset(explosions, 0, sizeof(explosions));
	qa_check(rollback_state_hash() == hashBefore,
	         "...and that opacity never reaches the registered state byte stream");

	// Partner shield bubbles follow hull opacity and Apply to Ship.
	{
		const bool savedNet = isNetworkGame;
		const JE_boolean savedTwo = twoPlayerMode;
		const uint savedPlayerNum = thisPlayerNum;
		const NetShipView savedView = netStyleView(0);
		const Player saved0 = player[0], saved1 = player[1];
		const JE_boolean savedEndless = endlessMode, savedMods = endlessCampaignMods;
		const int savedShieldFlash[2] = { shieldGaugeFlash[0], shieldGaugeFlash[1] };
		const int savedArmorFlash[2] = { armorGaugeFlash[0], armorGaugeFlash[1] };

		isNetworkGame = true;
		twoPlayerMode = true;
		endlessMode = false;          // avoid perk side effects
		endlessCampaignMods = false;
		thisPlayerNum = 1;             // local seat 0
		NetShipView view = savedView;
		view.opacity = 50;
		view.shipOpacity = true;
		netStyleSetView(0, view);

		for (uint seat = 0; seat < 2; ++seat)
		{
			player[seat].x = 100;
			player[seat].y = 100;
			player[seat].shield = 20;
			player[seat].shield_max = 20;
			player[seat].armor = 20;
			player[seat].is_alive = true;
		}

		memset(explosions, 0, sizeof(explosions));
		JE_playerDamage(1, &player[1]);
		qa_check(explosions[0].ttl != 0 && explosion_opacity(0) == 8,
		         "the partner's shield bubble fades with their hull");

		memset(explosions, 0, sizeof(explosions));
		JE_playerDamage(1, &player[0]);
		qa_check(explosions[0].ttl != 0 && explosion_opacity(0) == NET_STYLE_SOLID,
		         "...and this machine's own bubble stays solid");

		view.shipOpacity = false;   // Apply to Ship off
		netStyleSetView(0, view);
		memset(explosions, 0, sizeof(explosions));
		JE_playerDamage(1, &player[1]);
		qa_check(explosions[0].ttl != 0 && explosion_opacity(0) == NET_STYLE_SOLID,
		         "...and sparing their ship spares the bubble with it");

		player[1] = saved1;
		player[0] = saved0;
		shieldGaugeFlash[0] = savedShieldFlash[0]; shieldGaugeFlash[1] = savedShieldFlash[1];
		armorGaugeFlash[0] = savedArmorFlash[0];   armorGaugeFlash[1] = savedArmorFlash[1];
		endlessCampaignMods = savedMods;
		endlessMode = savedEndless;
		endlessSetFxPlayer(0);
		memset(explosions, 0, sizeof(explosions));
		netStyleSetView(0, savedView);
		thisPlayerNum = savedPlayerNum;
		twoPlayerMode = savedTwo;
		isNetworkGame = savedNet;
	}

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

/* A replacement pass must reuse discarded spark slots instead of advancing the ring twice.
 * See doc/notes.md#gauges-and-effects. */
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
   emission cadences. See doc/notes.md#gauges-and-effects. */
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

/* The blood a Knife Fight hit draws: drops that run down from the hull that was hit, in the reddest
 * bank of the palette in use, deeper bonus for more of them, silent under a re-simulation and
 * bounded over a presented frame. */
static void qa_test_knife_fight_blood(void)
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

/* Settings baked into the loaded item data do nothing on their own: something has to rewrite
 * the tables JE_loadItemDat filled, which is JE_applyItemDataSettings. */
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
 * sound, and both data sets set all eleven alike. */
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

#ifdef WITH_MIDI

/* LDS detune, envelope release, channel ownership, and truncation checks. */

#define QA_LDS_PATCH_BYTES 46
#define QA_LDS_TEMPO       7    // a row every 8 ticks, so one position spans 512 of them
#define QA_LDS_ROWS        64
#define QA_LDS_NOTE        60   // pattern note byte, in whole semitones
#define QA_LDS_VELOCITY    100
#define QA_LDS_WHEEL_RANGE 12   // semitones, as MIDIProcessorLDS.cpp announces over RPN
#define QA_LDS_BEND_CENTER 8192

/* Program identifies voice groups in this probe. Detune uses 1/16 semitone units, and channel 8
 * remains free for the stop command. */
static const struct
{
	JE_byte program;
	JE_byte level;    // pattern volume for this voice, 0 to 63
	JE_byte carMisc;  // carrier register 0x20; bit 5 makes the voice sustain
	JE_byte carAD;    // carrier attack and decay rates
	JE_byte carSR;    // carrier sustain level and release rate
	int finetune;     // as the patch record stores it
	int detune;       // what should survive: this patch's offset within its group
} qaLdsVoices[] = {
	{ 48, 10, 0x00, 0x00, 0x00,   0,   0 },  // three layers of one voice, already centred
	{ 48, 14, 0x00, 0x00, 0x00,  -2,  -2 },
	{ 48, 18, 0x00, 0x00, 0x00,   2,   2 },
	{ 49, 22, 0x00, 0x00, 0x00,  12,  12 },  // spread wide enough that the offset moves the key
	{ 49, 26, 0x00, 0x00, 0x00, -12, -12 },
	{ 50, 30, 0x00, 0x05, 0xF0, -12,   0 },  // standing alone: voicing stays out of the synth.
	{ 51, 34, 0x00, 0x02, 0xF0,  32,   0 },  // 5 decays inside the song, 6 outlasts it,
	{ 52, 38, 0x20, 0x05, 0xF0,   5,   0 },  // 7 sustains. None of the three may overrun.
};

typedef struct
{
	int key;     // -1 until the voice sounds
	int bend;    // wheel value in force when it sounded
	int notes;   // note-ons seen, so a repeat is caught
	int level;   // -1 until a volume controller reaches this channel
	int levels;  // volume controllers seen, so a stray one is caught
	int held;    // ticks between the note-on and its release, -1 while unreleased
	unsigned on; // tick the note sounded on
} QaLdsVoice;

static JE_byte *qa_lds_put16(JE_byte *p, unsigned value)
{
	*p++ = (JE_byte)(value & 0xff);
	*p++ = (JE_byte)((value >> 8) & 0xff);
	return p;
}

/* One position holding one note per voice and then a stop, so every patch sounds exactly once and
 * the converter's walk ends without needing a position jump. Returns the song's length. */
static size_t qa_lds_build(JE_byte *out)
{
	const unsigned voices = COUNTOF(qaLdsVoices);
	JE_byte *p = out;

	*p++ = 0;                // melodic mode
	p = qa_lds_put16(p, 0);  // speed, which the converter ignores
	*p++ = QA_LDS_TEMPO;     // ticks per row, less one
	*p++ = QA_LDS_ROWS;      // rows per position
	for (unsigned i = 0; i < 9; ++i)
		*p++ = 0;            // no channel delay
	*p++ = 0;                // percussion register

	p = qa_lds_put16(p, voices);
	for (unsigned v = 0; v < voices; ++v)
	{
		memset(p, 0, QA_LDS_PATCH_BYTES);
		p[14] = (JE_byte)(qaLdsVoices[v].finetune & 0xff);  // finetune, a signed byte on disk
		p[5] = qaLdsVoices[v].carMisc;
		p[7] = qaLdsVoices[v].carAD;
		p[8] = qaLdsVoices[v].carSR;
		p[40] = qaLdsVoices[v].program;
		p[41] = QA_LDS_VELOCITY;
		p += QA_LDS_PATCH_BYTES;
	}

	p = qa_lds_put16(p, 1);  // one position
	for (unsigned ch = 0; ch < 9; ++ch)
	{
		p = qa_lds_put16(p, ch * 6);  // byte offset into this channel's pattern words
		*p++ = 0;                     // transpose
	}

	p = qa_lds_put16(p, 0);  // digital sample count

	for (unsigned ch = 0; ch < 9; ++ch)
	{
		// Voices set volume, play once, then wait. Channel 8 stops on the final row.
		const bool voiced = ch < voices;

		p = qa_lds_put16(p, voiced ? 0xfd00u | qaLdsVoices[ch].level : 0x8000u | (QA_LDS_ROWS - 2));
		p = qa_lds_put16(p, voiced ? ((unsigned)QA_LDS_NOTE << 8) | ch : 0xfc00u);

		// Omit unread trailing words so truncation checks cover the parser's actual boundary.
		if (voiced)
			p = qa_lds_put16(p, 0x8000u | (QA_LDS_ROWS - 3));
	}

	return (size_t)(p - out);
}

static unsigned qa_smf_u32(const JE_byte *p)
{
	return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static const JE_byte *qa_smf_vlq(const JE_byte *p, const JE_byte *end, unsigned *out)
{
	unsigned value = 0;

	for (int i = 0; i < 4 && p < end; ++i)
	{
		value = (value << 7) | (*p & 0x7f);
		if ((*p++ & 0x80) == 0)
		{
			*out = value;
			return p;
		}
	}
	return NULL;
}

/* Walk every track, following running status, and record the key and the wheel value in force for
 * each channel's first note-on. */
static bool qa_smf_scan(const JE_byte *data, size_t size, QaLdsVoice *voice, unsigned voices)
{
	const JE_byte *p = data, *end = data + size;
	int bend[16];

	if (size < 14 || memcmp(p, "MThd", 4) != 0)
		return false;
	p += 8 + qa_smf_u32(p + 4);

	for (unsigned i = 0; i < COUNTOF(bend); ++i)
		bend[i] = QA_LDS_BEND_CENTER;

	while (end - p >= 8)
	{
		const unsigned length = qa_smf_u32(p + 4);
		const JE_byte *track = p + 8;
		const JE_byte *trackEnd = track + length;
		JE_byte status = 0;
		unsigned now = 0;

		if (memcmp(p, "MTrk", 4) != 0 || (size_t)(end - track) < length)
			return false;
		p = trackEnd;

		while (track < trackEnd)
		{
			unsigned delta, payload;

			track = qa_smf_vlq(track, trackEnd, &delta);
			now += delta;
			if (track == NULL || track >= trackEnd)
				return false;

			if (*track & 0x80)
				status = *track++;
			if (status < 0x80)
				return false;

			if (status == 0xff)  // meta: a type byte, then a counted payload
			{
				if (track >= trackEnd)
					return false;
				++track;
				track = qa_smf_vlq(track, trackEnd, &payload);
			}
			else if (status == 0xf0 || status == 0xf7)  // sysex: a counted payload
			{
				track = qa_smf_vlq(track, trackEnd, &payload);
			}
			else
			{
				const unsigned kind = status & 0xf0;
				const unsigned channel = status & 0x0f;

				payload = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
				if ((size_t)(trackEnd - track) < payload)
					return false;

				if (kind == 0xb0 && track[0] == 7 && channel < voices)
				{
					if (voice[channel].levels++ == 0)
						voice[channel].level = track[1];
				}
				else if (kind == 0xe0)
					bend[channel] = (int)track[0] | ((int)track[1] << 7);
				else if (kind == 0x90 && track[1] != 0 && channel < voices)
				{
					if (voice[channel].notes++ == 0)
					{
						voice[channel].key = track[0];
						voice[channel].bend = bend[channel];
						voice[channel].on = now;
					}
				}
				else if (kind == 0x80 && channel < voices && voice[channel].held < 0
				         && voice[channel].notes > 0)
				{
					voice[channel].held = (int) (now - voice[channel].on);
				}
			}

			if (track == NULL || (size_t)(trackEnd - track) < payload)
				return false;
			track += payload;
		}
	}
	return true;
}

static void qa_test_lds_midi_detune(void)
{
	const unsigned voices = COUNTOF(qaLdsVoices);
	JE_byte song[640];
	QaLdsVoice voice[16];
	JE_byte *smf = NULL;
	size_t smfSize = 0;

	qa_check(voices <= 8, "the detune probe leaves a channel free to stop the song");

	const size_t songSize = qa_lds_build(song);
	qa_check(songSize <= sizeof(song), "the probe song fits its buffer");

	HMIDIContainer container = MIDPROC_Container_Create();
	qa_check(MIDPROC_Process(song, songSize, "lds", container),
	         "the converter reads the probe song");

	MIDPROC_Container_SerializeAsSMF(container, &smf, &smfSize);
	qa_check(smf != NULL && smfSize > 0, "the converted song serializes to an SMF");

	for (unsigned i = 0; i < COUNTOF(voice); ++i)
	{
		voice[i].key = -1;
		voice[i].bend = QA_LDS_BEND_CENTER;
		voice[i].notes = 0;
		voice[i].level = -1;
		voice[i].levels = 0;
		voice[i].held = -1;
		voice[i].on = 0;
	}
	qa_check(smf != NULL && qa_smf_scan(smf, smfSize, voice, voices), "the SMF parses");

	for (unsigned v = 0; v < voices; ++v)
	{
		const int target = (QA_LDS_NOTE * 16) + qaLdsVoices[v].detune;
		const int wantKey = (target + 8) >> 4;
		const int wantBend = QA_LDS_BEND_CENTER
		                   + (((target - (wantKey * 16)) * 512) / QA_LDS_WHEEL_RANGE);
		const int wantLevel = (qaLdsVoices[v].level & 0x3f) * 127 / 63;

		if (voice[v].notes != 1 || voice[v].key != wantKey || voice[v].bend != wantBend
		    || voice[v].levels != 1 || voice[v].level != wantLevel)
			fprintf(stderr,
			        "# voice %u: %d note(s), key %d bend %d level %d (x%d), want %d / %d / %d\n",
			        v, voice[v].notes, voice[v].key, voice[v].bend, voice[v].level, voice[v].levels,
			        wantKey, wantBend, wantLevel);

		qa_check(voice[v].notes == 1, "a detuned patch sounds exactly once");
		qa_check(voice[v].key == wantKey, "a patch sounds the key its reduced detune lands on");
		qa_check(voice[v].bend == wantBend, "a patch leaves its detune remainder on the wheel");
		qa_check(voice[v].levels == 1, "a voice takes exactly one volume controller");
		qa_check(voice[v].level == wantLevel, "a volume controller reaches its own voice");
	}

	/* Voice 5 decays before the end, voice 6 outlasts it, and voice 7 sustains. */
	qa_check(voices == 8, "the probe still has the eight voices these checks name");
	if (voices == 8)
	{
		const int songEnd = voice[0].held;

		fprintf(stderr, "# held ticks: decays %d, outlasts %d, sustains %d, song end %d\n",
		        voice[5].held, voice[6].held, voice[7].held, songEnd);
		qa_check(voice[5].held > 0, "a decaying voice is released by its envelope");
		qa_check(voice[5].held < songEnd, "a decaying voice goes quiet before the song ends");
		qa_check(voice[6].held == songEnd, "an envelope past the song end releases at the end");
		qa_check(voice[7].held == songEnd, "an envelope never cuts a sustaining voice short");
	}

	MIDPROC_FreeSerialized(smf);
	MIDPROC_Container_Delete(container);

	/* Every proper prefix must be rejected. Sanitizers cover out-of-bounds reads. */
	size_t accepted = 0, firstAccepted = 0;
	for (size_t cut = 1; cut < songSize; ++cut)
	{
		HMIDIContainer truncated = MIDPROC_Container_Create();

		if (MIDPROC_Process(song, cut, "lds", truncated))
		{
			if (accepted++ == 0)
				firstAccepted = cut;
		}

		MIDPROC_Container_Delete(truncated);
	}

	if (accepted != 0)
		fprintf(stderr, "# %u of %u truncations accepted, first at %u byte(s)\n",
		        (unsigned)accepted, (unsigned)songSize - 1, (unsigned)firstAccepted);
	qa_check(accepted == 0, "the converter rejects every truncation of the probe song");
}

#endif /* WITH_MIDI */

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
		{ .intSetting = &vulnerableCue, .poke = VULN_CUE_ALL, .name = "Vulnerable Cue" },
		{ .intSetting = &gaugeGradGenerator, .poke = GAUGE_GRAD_DOWN, .name = "Gauges" },
		{ .boolSetting = &gaugeFlashArmor, .poke = false, .name = "Gauge flash" },
		{ .boolSetting = &chargeLaserCannon, .poke = false, .name = "Charge-Laser" },
		{ .intSetting = &superSparkMode[SSW_ICE], .poke = SUPER_SPARKS_OFF, .name = "Spark Trails" },
		{ .intSetting = &wallopSecondBolt, .poke = SUPER_SPARKS_OFF, .name = "Wallop 2nd Bolt" },
		{ .boolSetting = &superSparkClassicCap[SSW_ICE], .poke = false, .name = "Classic Spark Caps" },
		{ .boolSetting = &centeredShotHitboxes, .poke = false, .name = "Gameplay" },
		{ .boolSetting = &guidedShotScreenAim, .poke = true, .name = "Guided Aim" },
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

	// Enhancement presets must not change the Extra menu's Custom Weapons setting.
	customWeaponEnabled = false;
	enhancementApplyPreset(ENH_PRESET_ENGAGED);
	qa_check(!customWeaponEnabled, "the Engaged preset leaves the Extra custom-weapon toggle alone");
	customWeaponEnabled = true;
	enhancementApplyPreset(ENH_PRESET_VANILLA);
	qa_check(customWeaponEnabled, "the Vanilla preset leaves the Extra custom-weapon toggle alone");

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
	customWeaponEnabled = true;
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

	const Sint64 shooterBounty = player[1].cash;
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

/* The peer leaving a level under us. */
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
	qa_check(!nrb_peer_left_level(PACKET_GAME_MENU) && !reallyEndLevel,
	         "a rollback in-game menu release is not a departure: the peer is still in the level");

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

/* The idle rule that ends a level-end or menu-frame confirmation wait when the peer stops
 * producing frames. The case that matters is a peer that gained frames after the wait began and
 * then left; the rule has to fire on it, and every advance has to buy the peer a fresh window. */
static void qa_test_peer_idle_rule(void)
{
#ifdef WITH_NETWORK
	const Uint32 limit = NRB_PEER_IDLE_TIME_OUT;
	Uint32 seen = 540, tick = 1000;

	qa_check(!nrb_peer_idle(1000, 540, &seen, &tick), "a wait that just began is not idle");
	qa_check(!nrb_peer_idle(1000 + limit, 540, &seen, &tick), "...nor at exactly the limit");
	qa_check(nrb_peer_idle(1001 + limit, 540, &seen, &tick),
	         "a peer whose newest frame never moved is idle once the limit passes");

	seen = 537;
	tick = 1000;
	qa_check(!nrb_peer_idle(1300, 539, &seen, &tick) && seen == 539 && tick == 1300,
	         "an advance is recorded and re-arms the clock");
	qa_check(!nrb_peer_idle(1600, 540, &seen, &tick) && tick == 1600,
	         "...as does every later advance");
	qa_check(!nrb_peer_idle(1000 + limit, 540, &seen, &tick),
	         "the limit counts from the last advance, so the original start no longer matters");
	qa_check(nrb_peer_idle(1601 + limit, 540, &seen, &tick),
	         "a peer that advanced during the wait and then went quiet is idle from that advance");
#endif
}

/* Who owns the in-game menu a frame's request bits open. The property that matters is that the
 * two seats never disagree: a frame either opens no menu, or opens one with exactly one presser
 * and one waiter. */
static void qa_test_menu_claim(void)
{
#ifdef WITH_NETWORK
	const Uint16 none = RB_BTN_FIRE;               // traffic on the frame, no request
	const Uint16 menu = RB_BTN_FIRE | RB_REQ_MENU;

	for (int hostPressed = 0; hostPressed <= 1; ++hostPressed)
	{
		for (int joinerPressed = 0; joinerPressed <= 1; ++joinerPressed)
		{
			const Uint16 hostBits = hostPressed ? menu : none;
			const Uint16 joinerBits = joinerPressed ? menu : none;

			// The same frame read from each seat: local and remote swap, is_host does not.
			const NrbMenuClaim onHost = nrb_menu_claim(hostBits, joinerBits, true);
			const NrbMenuClaim onJoiner = nrb_menu_claim(joinerBits, hostBits, false);

			qa_check(onHost.open == onJoiner.open
			         && onHost.open == (hostPressed || joinerPressed),
			         "both seats agree on whether a frame opens the in-game menu");
			if (!onHost.open)
			{
				qa_check(!onHost.local && !onJoiner.local,
				         "...and a frame that opens none leaves neither seat holding one");
				continue;
			}
			qa_check(onHost.local != onJoiner.local,
			         "an opened menu has exactly one presser, so the other seat has someone to wait for");
			qa_check(onHost.local == (hostPressed != 0),
			         "the host presses for itself, and takes a tie from the joiner");
		}
	}

	/* Presses one frame apart are two frames, and each is claimed on its own: the earlier one is
	 * the frame the menu opens at, so a joiner that pressed first keeps it. */
	const NrbMenuClaim earlyOnJoiner = nrb_menu_claim(menu, none, false);
	const NrbMenuClaim earlyOnHost = nrb_menu_claim(none, menu, true);
	qa_check(earlyOnJoiner.open && earlyOnJoiner.local && earlyOnHost.open && !earlyOnHost.local,
	         "a joiner press on a frame the host did not press is the joiner's menu");

	/* Only RB_REQ_MENU opens one; the other request bits ride the same records. */
	qa_check(!nrb_menu_claim(RB_REQ_SKIPLEVEL | RB_REQ_NORTSHIP | RB_REQ_PAUSE,
	                         RB_REQ_SKIPLEVEL | RB_REQ_PAUSE, true).open,
	         "skip-level, Nort ship and the dead pause bit open no menu");
#endif
}

// Change one local-view field while preserving the others.
static void qa_set_local_opacity(int pct)
{
	NetShipView view = netStyleLocalView();
	view.opacity = (Uint8)pct;
	netStyleSetLocalView(view);
}

static void qa_set_local_ship_opacity(bool on)
{
	NetShipView view = netStyleLocalView();
	view.shipOpacity = on;
	netStyleSetLocalView(view);
}

static void qa_set_local_hp_bars(int mode)
{
	NetShipView view = netStyleLocalView();
	view.hpBars = (Uint8)mode;
	netStyleSetLocalView(view);
}

// Partner HP-bar bounds, placement, painting, and mode eligibility.
static void qa_test_partner_hp_bars(void)
{
	const Player saved0 = player[0], saved1 = player[1];
	const JE_word savedGr = shipGr, savedGr2 = shipGr2;
	const int savedLayout = enemyBarLayout, savedPos = enemyBarPosition, savedOp = enemyBarOpacity;

	player[0].x = 100; player[0].y = 100;
	player[1].x = 100; player[1].y = 100;

	int l, r, t, b;

	shipGr = 5;  // ordinary single hull
	hud_ship_hp_bar_box(0, &l, &r, &t, &b);
	qa_check(l == 95 && r == 118 && t == 93 && b == 121,
	         "an ordinary hull measures the 24x28 sprite it draws");

	shipGr2 = 0;  // Dragonwing sentinel
	hud_ship_hp_bar_box(1, &l, &r, &t, &b);
	qa_check(l == 83 && r == 130 && t == 93 && b == 121,
	         "a Dragonwing measures both halves it straddles its anchor with");

	int nl, nr, nt, nb;
	shipGr2 = 1;  // Nort Ship sentinel
	hud_ship_hp_bar_box(1, &nl, &nr, &nt, &nb);
	qa_check(nl == l && nr == r && nt == t && nb == b,
	         "...and so does the Nort Ship, the other two-piece hull");

	// A two-bar block must clear the hull while spanning it.
	enemyBarLayout = ENEMY_BAR_HORIZONTAL;
	enemyBarPosition = ENEMY_BAR_POS_BOTTOM;

	int x, y, along, y1;
	qa_check(enemy_bar_place(l, r, t, b, 2 * ENEMY_BAR_THICK, false, &x, &y, &along)
	         && x == 84 && along == 46 && y == 122,
	         "a two-bar block under a wide hull spans it and clears the sprite");
	qa_check(along <= ENEMY_BAR_MAX_LEN, "...within the length every small bar is capped at");

	enemyBarPosition = ENEMY_BAR_POS_TOP;
	qa_check(enemy_bar_place(l, r, t, b, 2 * ENEMY_BAR_THICK, false, &x, &y, &along)
	         && enemy_bar_place(l, r, t, b, ENEMY_BAR_THICK, false, &x, &y1, &along)
	         && y + 2 * ENEMY_BAR_THICK == t - 1 && y1 + ENEMY_BAR_THICK == t - 1,
	         "a block above the box hangs from its top edge whatever its thickness");

	// Paint at full opacity in a box away from the ship.
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8 && VGAScreen->h > 40)
	{
		enemyBarPosition = ENEMY_BAR_POS_BOTTOM;
		enemyBarOpacity = 100;

		const int bx = 4, by = 4, bw = 24, bh = 20;

		// Full shield, half armor.
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20,
		                         ARMOR_GAUGE_LAYER_UNITS / 2,
		                         ARMOR_GAUGE_LAYER_UNITS);

		const Uint8 *const pixels = (const Uint8 *)VGAScreen->pixels;
		const int pitch = VGAScreen->pitch;
		const int barY = by + bh + 1, barX = bx + 1;
		const int barLen = bw - 2;

		qa_check((pixels[barY * pitch + barX] & 0xf0) == (SHIELD_GAUGE_BASE & 0xf0),
		         "the partner's shield bar is drawn in the shield gauge's own bank");
		qa_check((pixels[(barY + ENEMY_BAR_THICK) * pitch + barX] & 0xf0)
		         == (armorGaugeLayerCol[0] & 0xf0),
		         "the armor bar sits right under it, in the armor gauge's bank");

		// Half a layer of armour leaves the far end of that bar empty.
		qa_check((pixels[(barY + ENEMY_BAR_THICK) * pitch + barX + barLen - 1] & 0xf0)
		         == (armorGaugeLayerCol[0] & 0xf0)
		         && pixels[(barY + ENEMY_BAR_THICK) * pitch + barX + barLen - 1]
		            < pixels[(barY + ENEMY_BAR_THICK) * pitch + barX],
		         "...half full, so its far end is track rather than fill");

		// Rollover uses the current layer for fill and the previous layer for track.
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20,
		                         ARMOR_GAUGE_LAYER_UNITS + ARMOR_GAUGE_LAYER_UNITS / 2,
		                         2 * ARMOR_GAUGE_LAYER_UNITS);

		qa_check((pixels[(barY + ENEMY_BAR_THICK) * pitch + barX] & 0xf0)
		         == (armorGaugeLayerCol[1] & 0xf0),
		         "a rolled-over armor bar fills in the layer it has reached");
		qa_check((pixels[(barY + ENEMY_BAR_THICK) * pitch + barX + barLen - 1] & 0xf0)
		         == (armorGaugeLayerCol[0] & 0xf0),
		         "...over a track showing the full layer underneath");

		// A Life Boost hull is full at its current 12-point ceiling.
		const int farEnd = (barY + ENEMY_BAR_THICK) * pitch + barX + barLen - 1;
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20, 12, 12);
		const Uint8 weakHull = pixels[farEnd];
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20,
		                         12, ARMOR_GAUGE_LAYER_UNITS);
		qa_check(weakHull > pixels[farEnd],
		         "a mini armor bar divides by the hull's own ceiling, not the strongest hull's");

		// Armor above one gauge layer still reads full at its current ceiling.
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 20, 20,
		                         2 * ARMOR_GAUGE_LAYER_UNITS, 2 * ARMOR_GAUGE_LAYER_UNITS);
		qa_check((pixels[farEnd] & 0xf0) == (armorGaugeLayerCol[1] & 0xf0),
		         "a fully repaired upgraded hull fills its top layer end to end");

		// Shield bars also use the ship's current ceiling.
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 8, 8, 12, 12);
		const Uint8 weakShield = pixels[barY * pitch + barX + barLen - 1];
		hud_draw_ship_hp_bars_at(0, bx, bx + bw - 1, by, by + bh, 8, 20, 12, 12);
		qa_check(weakShield > pixels[barY * pitch + barX + barLen - 1],
		         "...and the shield bar does the same with shield_max");
	}

	// Linked Arcade skips bars because its shared HUD already shows both ships.
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8 && VGAScreen->h > 70)
	{
		const bool savedNet = isNetworkGame;
		const JE_boolean savedTwo = twoPlayerMode, savedSep = arcadeSeparateMode;
		const JE_boolean savedCoop = coopCampaignMode, savedCoopE = coopEndlessMode;
		const uint savedPlayerNum = thisPlayerNum;
		const NetShipView savedView = netStyleView(0);

		isNetworkGame = true;
		twoPlayerMode = true;
		coopCampaignMode = false;
		coopEndlessMode = false;
		thisPlayerNum = 1;                 // this machine flies seat 0
		qa_set_local_hp_bars(NET_HP_BARS_ALWAYS);
		enemyBarPosition = ENEMY_BAR_POS_BOTTOM;
		enemyBarOpacity = 100;
		shipGr2 = 5;
		player[1].is_alive = true;
		player[1].x = 40;
		player[1].y = 40;
		player[1].shield_max = 20;
		player[1].shield = 20;
		player[1].armor = ARMOR_GAUGE_LAYER_UNITS;

		// The rows the bars would land on, under a 24x28 hull whose box ends at y = 61.
		const int probeY = 62, probeRows = 2 * ENEMY_BAR_THICK;
		Uint8 *const pixels = (Uint8 *)VGAScreen->pixels;
		const int pitch = VGAScreen->pitch;

		arcadeSeparateMode = true;   // independent HUDs
		for (int row = 0; row < probeRows; ++row)
			memset(pixels + (probeY + row) * pitch + 30, 0, 40);
		hud_draw_ship_hp_bars();
		int painted = 0;
		for (int row = 0; row < probeRows; ++row)
			for (int col = 30; col < 70; ++col)
				painted += (pixels[(probeY + row) * pitch + col] != 0);
		qa_check(painted > 0, "a session with two HUDs paints the partner's bars");

		arcadeSeparateMode = false;  // shared HUD
		for (int row = 0; row < probeRows; ++row)
			memset(pixels + (probeY + row) * pitch + 30, 0, 40);
		hud_draw_ship_hp_bars();
		painted = 0;
		for (int row = 0; row < probeRows; ++row)
			for (int col = 30; col < 70; ++col)
				painted += (pixels[(probeY + row) * pitch + col] != 0);
		qa_check(painted == 0, "Linked Arcade paints none, whatever the setting says");

		// 70% bar opacity scaled by 30% ship opacity is 21%.
		enemyBarOpacity = 70;
		NetShipView fade = netStyleView(0);
		fade.opacity = 30;
		fade.shipOpacity = true;
		netStyleSetView(0, fade);
		qa_check(hud_ship_hp_bar_opacity() == 53,
		         "the partner's opacity scales the enemy-bar setting instead of replacing it");
		fade.shipOpacity = false;
		netStyleSetView(0, fade);
		qa_check(hud_ship_hp_bar_opacity() == 178,
		         "...and sparing their ship leaves the bars at the enemy-bar setting alone");

		netStyleSetView(0, savedView);
		thisPlayerNum = savedPlayerNum;
		coopEndlessMode = savedCoopE;
		coopCampaignMode = savedCoop;
		arcadeSeparateMode = savedSep;
		twoPlayerMode = savedTwo;
		isNetworkGame = savedNet;
	}

	enemyBarOpacity = savedOp;
	enemyBarPosition = savedPos;
	enemyBarLayout = savedLayout;
	shipGr = savedGr;
	shipGr2 = savedGr2;
	player[0] = saved0;
	player[1] = saved1;
}

// Per-seat dyes, per-seat views, previews, and sprite blending.
static void qa_test_online_ship_style(void)
{
	const bool savedNet = isNetworkGame;
	const JE_boolean savedTwo = twoPlayerMode;
	const JE_boolean savedMode = endlessMode, savedCampaign = endlessCampaignMods;
	const uint savedPlayerNum = thisPlayerNum;
	const int savedColor[2] = { netStyleSeatColor(0), netStyleSeatColor(1) };
	const NetShipView savedView[2] = { netStyleView(0), netStyleView(1) };

	netStylePreviewClear();

	// Start outside Endless so all banks are available.
	endlessMode = false;
	endlessCampaignMods = false;

	// Offline rendering ignores online styles.
	isNetworkGame = false;
	twoPlayerMode = false;
	thisPlayerNum = 2;
	netStyleSetSeatColor(1, 5);
	qa_set_local_opacity(50);
	qa_check(netStyleIsPlain(netStyleForSeat(0)) && netStyleIsPlain(netStyleForSeat(1)),
	         "offline play ignores the online ship colours");

	isNetworkGame = true;
	twoPlayerMode = true;
	netStyleSetSeatColor(0, 3);

	qa_check(netStyleLocalSeat() == 1, "seat two is this machine's ship");
	qa_check(netStylePeerColor() == 3, "the partner's dye is the one the other seat holds");
	qa_check(netStyleForSeat(1).bank == 4 && netStyleForSeat(1).opacity == NET_STYLE_SOLID,
	         "this ship wears the dye its own seat holds, at full strength");
	qa_check(netStyleForSeat(0).bank == 2 && netStyleForSeat(0).opacity == 8,
	         "the partner wears the dye they announced, at the opacity this machine set");

	thisPlayerNum = 1;
	qa_set_local_opacity(50);
	qa_check(netStyleForSeat(0).opacity == NET_STYLE_SOLID && netStyleForSeat(1).opacity == 8,
	         "the opacity lands on the partner whichever seat this machine flies");

	// Each seat retains its own view; only the local seat affects this screen.
	qa_check(netStyleView(1).opacity == 50 && netStyleView(0).opacity == 50
	         && netStyleLocalView().opacity == 50,
	         "each seat holds its own view and this machine reads the seat it flies");
	thisPlayerNum = 2;
	qa_set_local_opacity(NET_OPACITY_FULL);
	thisPlayerNum = 1;
	qa_check(netStyleView(1).opacity == NET_OPACITY_FULL && netStyleLocalView().opacity == 50,
	         "...so the peer's own view never overwrites this one");

	// Picker steps map to a nonzero, strictly decreasing mix.
	bool laddered = true;
	int previous = NET_STYLE_SOLID + 1;
	for (int pct = NET_OPACITY_FULL; pct >= NET_OPACITY_MIN; pct -= NET_OPACITY_STEP)
	{
		qa_set_local_opacity(pct);
		const int opacity = netStyleForSeat(1).opacity;
		laddered = laddered && opacity < previous && opacity >= 1 && opacity <= NET_STYLE_SOLID;
		previous = opacity;
	}
	qa_check(laddered, "each opacity step is fainter than the last and none of them reaches nothing");

	netStyleSetSeatColor(0, NET_SHIP_COLORS + 7);
	qa_check(netStyleSeatColor(0) == NET_SHIP_COLOR_NONE,
	         "a dye off the end of the palette is read as no dye");
	netStyleSetSeatColor(0, 3);

	// Endless reserves the four kill-fire palette banks.
	{
		static const int driveFilter[] = {
			ENDLESS_TURBODRIVE_SHIP_FILTER, ENDLESS_OVERDRIVE_SHIP_FILTER,
			ENDLESS_OVERBLAST_SHIP_FILTER, ENDLESS_EVIL_SHIP_FILTER,
		};

		endlessMode = false;
		endlessCampaignMods = false;
		bool freeOutside = true;
		for (unsigned int i = 0; i < COUNTOF(driveFilter); ++i)
			freeOutside = freeOutside && !netStyleColorReserved((driveFilter[i] >> 4) + 1);
		qa_check(freeOutside, "outside Endless every bank is offered");

		endlessMode = true;
		bool heldBack = true;
		int reserved = 0;
		for (int color = 1; color <= NET_SHIP_COLORS; ++color)
			if (netStyleColorReserved(color))
				++reserved;
		for (unsigned int i = 0; i < COUNTOF(driveFilter); ++i)
			heldBack = heldBack && netStyleColorReserved((driveFilter[i] >> 4) + 1);
		qa_check(heldBack && reserved == (int)COUNTOF(driveFilter),
		         "Endless withholds the three drive banks and the evil one, and no others");

		netStyleSetSeatColor(0, (ENDLESS_TURBODRIVE_SHIP_FILTER >> 4) + 1);
		thisPlayerNum = 1;
		qa_check(netStyleForSeat(0).bank < 0, "a withheld dye is not worn in Endless either");
		netStyleSetSeatColor(0, 3);

		endlessMode = false;
	}

	// Apply to Ship affects bodies only; shots always use partner opacity.
	thisPlayerNum = 1;
	qa_set_local_opacity(50);
	qa_set_local_ship_opacity(true);
	qa_check(netStyleForSeat(1).opacity == 8 && netStyleForShot(1).opacity == 8,
	         "the partner's ship and its shots share one opacity by default");
	qa_set_local_ship_opacity(false);
	qa_check(netStyleForSeat(1).opacity == NET_STYLE_SOLID && netStyleForSeat(1).bank == 4,
	         "sparing the ship draws their hull solid and keeps its dye");
	qa_check(netStyleForShot(1).opacity == 8 && netStyleForShot(1).bank < 0,
	         "...and leaves their shots faded, and undyed either way");
	qa_check(netStyleForSeat(0).opacity == NET_STYLE_SOLID
	         && netStyleForShot(0).opacity == NET_STYLE_SOLID,
	         "neither reaches the ship this machine flies");
	qa_set_local_ship_opacity(true);

	// Fresh sessions start plain; saved sessions restore their styles later.
	netStyleSetSeatColor(0, 9);
	netStyleSetSeatColor(1, 4);
	qa_set_local_opacity(NET_OPACITY_MIN);
	qa_set_local_ship_opacity(false);
	qa_set_local_hp_bars(NET_HP_BARS_ALWAYS);
	netStyleSessionReset();
	bool forgotten = true;
	for (uint seat = 0; seat < 2; ++seat)
		forgotten = forgotten && netStyleSeatColor(seat) == NET_SHIP_COLOR_NONE
		            && netStyleView(seat).opacity == NET_OPACITY_FULL
		            && netStyleView(seat).shipOpacity
		            && netStyleView(seat).hpBars == NET_HP_BARS_OFF;
	qa_check(forgotten, "a session starting forgets the look the last one was flying");
	netStyleSessionReset();
	qa_check(netStylePeerColor() == NET_SHIP_COLOR_NONE, "a session ending forgets the peer's dye");

	// Menu previews override session styles.
	netStylePreviewSet(10, 50);
	qa_check(netStyleForSeat(0).bank == 9 && netStyleForSeat(1).opacity == 8,
	         "a menu preview dresses every ship it draws");
	netStylePreviewClear();

	// One-pixel Sprite2 frames for direct blend checks.
	if (VGAScreen != NULL && VGAScreen->format->BitsPerPixel == 8)
	{
		union {
			Uint16 align;  // the offset table is read as Uint16, which needs the storage aligned
			Uint8 bytes[10];
		} frames = { .bytes = { 4, 0, 7, 0,        // two frames, one pixel each, bank 7 shade 12
		                        0x10, 0x7c, 0x0f,
		                        0x10, 0x7c, 0x0f } };
		const Sprite2_array sheet = { sizeof(frames.bytes), frames.bytes };

		Uint8 *const row = (Uint8 *)VGAScreen->pixels;
		const Uint8 savedPixels[2] = { row[0], row[1] };
		const int mixed = (12 * 4 + 4 * (NET_STYLE_SOLID - 4) + 8) / NET_STYLE_SOLID;

		row[0] = 0x24;  // bank 2, shade 4
		blit_sprite2_alpha_clip(VGAScreen, 0, 0, sheet, 1, -1, 4);
		row[1] = 0x24;
		blit_sprite2_alpha_clip(VGAScreen, 1, 0, sheet, 2, 9, 4);
		qa_check(row[0] == (Uint8)(0x70 | mixed),
		         "a faded sprite keeps its own bank and mixes only brightness");
		qa_check(row[1] == (Uint8)(0x90 | mixed),
		         "...and a dyed one fades inside the bank it was dyed with");

		row[0] = 0x24;
		blit_sprite2_alpha_clip(VGAScreen, 0, 0, sheet, 1, -1, NET_STYLE_SOLID);
		row[1] = 0x24;
		blit_sprite2_alpha_clip(VGAScreen, 1, 0, sheet, 2, 9, NET_STYLE_SOLID);
		qa_check(row[0] == 0x7c && row[1] == 0x9c,
		         "nothing left to mix hands the sprite back to the plain and filtered blits");

		row[0] = savedPixels[0];
		row[1] = savedPixels[1];
	}

	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	endlessMode = savedMode;
	endlessCampaignMods = savedCampaign;
	thisPlayerNum = savedPlayerNum;
	for (uint seat = 0; seat < 2; ++seat)
	{
		netStyleSetSeatColor(seat, savedColor[seat]);
		netStyleSetView(seat, savedView[seat]);
	}
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
	const bool savedCenteredHitboxes = centeredShotHitboxes, savedGuidedAim = guidedShotScreenAim;
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
	centeredShotHitboxes = true; guidedShotScreenAim = true;
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
	centeredShotHitboxes = false; guidedShotScreenAim = false;
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
	         && !arcadeRandomBalls && arcadeRearGunScale && centeredShotHitboxes && guidedShotScreenAim
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
	         && arcadeRandomBalls && !arcadeRearGunScale && !centeredShotHitboxes && !guidedShotScreenAim
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

	/* The host runs on flags armed from its own config; the joiner adopts the block packed from
	 * that same config. */
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
	guidedShotScreenAim = savedGuidedAim;
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
	Uint8 regenOff, regenFree, seeker, seekTier, seekPasses, scrollActive;
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
	out->seekTier = (Uint8)endlessSeekerTier();
	out->seekPasses = endlessSeekerPasses();
	out->scrollActive = endlessScrollBoostActive() ? 1 : 0;
}

/* Every sector modifier's derived combat parameters, identical whichever machine computes them:
 * both thisPlayerNum values, both isNetworkGame values, both host roles, and both fx players. */
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

	/* Boss-bar surveys ignore wreckage while retaining active transformed parts. */
	enemyAvail[0] = 2;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor == 60 && full == 100,
	         "a hull that leaves wreckage beside a live part still reads the live one");

	enemyAvail[1] = 2;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor > 255, "wreckage a dead boss leaves behind is not a live part");

	enemyAvail[0] = 1;
	enemyAvail[1] = 1;
	boss_bar_survey(link, &armor, &full);
	qa_check(armor > 255, "a group with no live parts left reports the boss gone");

	memcpy(enemy, savedEnemy, sizeof(savedEnemy));
	for (uint i = 0; i < COUNTOF(parts); ++i)
		enemyAvail[i] = savedAvail[i];
}

/* Boss vulnerability transition and flash fade. */
static void qa_test_vulnerable_cue(void)
{
	qa_check(enemy_armed_flash_arms(255, 254, 0),
	         "a level arming an invulnerable boss lights the cue");
	qa_check(enemy_armed_flash_arms(255, 1, 0), "even one armor point is a target");
	qa_check(!enemy_armed_flash_arms(100, 50, 0),
	         "an enemy that could already be hurt has nothing to announce");
	qa_check(!enemy_armed_flash_arms(255, 255, 0), "and neither has one left invulnerable");
	qa_check(!enemy_armed_flash_arms(255, 0, 0), "armor landing on zero kills rather than arms");
	qa_check(!enemy_armed_flash_arms(255, 254, 1) && !enemy_armed_flash_arms(255, 254, 2),
	         "a freed or lingering slot is no target to announce");

	qa_check(enemy_armed_flash_lift(ENEMY_ARMED_FLASH_FRAMES) == 0x0f,
	         "the body opens solid white on the frame the cue is armed");
	qa_check(enemy_armed_flash_lift(ENEMY_ARMED_FLASH_WHITE) == 0x0f,
	         "and holds white for the frames the shield gauge holds it");
	qa_check(enemy_armed_flash_lift(ENEMY_ARMED_FLASH_WHITE - 1) < 0x0f,
	         "then drops into its own shading in greys");
	qa_check(enemy_armed_flash_lift(0) == 0, "and is back in its colours once the cue is spent");

	for (Uint32 left = ENEMY_ARMED_FLASH_FRAMES; left > 0; --left)
		if (enemy_armed_flash_lift(left - 1) > enemy_armed_flash_lift(left))
		{
			qa_check(false, "the flash only ever fades");
			break;
		}
	qa_check(enemy_armed_flash_lift(1) > 0, "without going dark before the cue ends");

	/* Cue scope when one armor event affects boss and non-boss bodies. */
	const int savedCue = vulnerableCue;
	const Uint8 savedLink[2] = { boss_bar[0].link_num, boss_bar[1].link_num };
	boss_bar[0].link_num = 42;
	boss_bar[1].link_num = 0;

	vulnerableCue = VULN_CUE_BOSSES;
	qa_check(enemy_armed_flash_shows(42), "a hull carrying a boss bar shows the cue");
	qa_check(!enemy_armed_flash_shows(7), "a linked hull with no bar of its own does not");
	qa_check(!enemy_armed_flash_shows(0), "and neither does unlinked traffic");

	vulnerableCue = VULN_CUE_ALL;
	qa_check(enemy_armed_flash_shows(0) && enemy_armed_flash_shows(7),
	         "All Enemies hands the cue to everything an armor event arms");

	vulnerableCue = VULN_CUE_OFF;
	qa_check(!enemy_armed_flash_shows(42), "Off silences it even on a boss");

	vulnerableCue = savedCue;
	boss_bar[0].link_num = savedLink[0];
	boss_bar[1].link_num = savedLink[1];
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

/* Check the synthesized Dragonwing row, including its sentinel graphic, stats, and id clamps. */
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
	const JE_boolean savedTwo = twoPlayerMode, savedSeparate = arcadeSeparateMode;
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

	// Linked Arcade identifies seat two by sentinel; items.ship is empty.
	coopCampaignMode = false;
	arcadeSeparateMode = false;
	player[1].items.ship = 0;
	JE_getShipInfo();
	qa_check(split_arcade_mode() && shipGr2 == 0 && shipGr2ptr == &spriteSheet9,
	         "the linked pair names seat two's Dragonwing by sentinel, with no ship item to read");

	shipGrPtr = savedGrPtr;
	shipGr2ptr = savedGr2Ptr;
	shipGr = savedGr;
	shipGr2 = savedGr2;
	powerAdd = savedPowerAdd;
	player[0] = saved0;
	player[1] = saved1;
	coopEndlessMode = savedCoopEndless;
	coopCampaignMode = savedCoop;
	arcadeSeparateMode = savedSeparate;
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

/* The Nort Ship and Dragonwing paint 48px wide against the item list's 24px icon column, so
 * each takes a shifted anchor and a label column of its own. */
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

/* A weapon row tags a two-mode port, or a gun Endless stocked for the other bay, after its
 * cost, in a column that has to clear both the cost text and the owned marker the same row can
 * carry. */
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
		snprintf(buf, sizeof(buf), "Cost: %lld", (long long)base * (long long)prices[i].mult);

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

// Check the editor codec against Tyrian 2000's stock newsh$.shp.
static void qa_test_ship_editor_file(void)
{
	JE_ShipsType backup, enc;
	memcpy(backup, extraShips, sizeof(backup));
	qa_check(strstr(ships[1].name, "Light Fighter") != NULL,
	         "the USP Talon keeps its full data-file name outside the ship editor");
	qa_check(strcmp(extraShipEditorGraphicName(1), "USP Talon") == 0,
	         "the ship editor shortens the USP Talon's graphic label");

	qa_check(extraAvail, "the stock compiled ship file loads");
	if (extraAvail)
	{
		qa_check(extraShips[0] == 8 && extraShips[1] == 6 && extraShips[7] == 16 && extraShips[8] == 5,
		         "slot one decodes to its known loadout");

		bool bays = true;
		for (int slot = 0; slot < 10; ++slot)
		{
			const JE_byte *record = &extraShips[slot * 15];
			bays = bays && shop_weapon_port_bay(record[1]) == SHOP_BAY_FRONT;
			bays = bays && (record[2] == 0 || shop_weapon_port_bay(record[2]) == SHOP_BAY_REAR);
		}
		qa_check(bays, "stock loadouts fit the editor's front/rear bay split");
	}

	JE_encryptShips(enc);
	memcpy(extraShips, enc, sizeof(extraShips));
	qa_check(JE_decryptShips(), "an encrypted table passes its own checksums");
	qa_check(memcmp(extraShips, backup, sizeof(JE_ShipsType) - 4) == 0,
	         "encrypt then decrypt returns the original table");

	memcpy(extraShips, enc, sizeof(extraShips));
	extraShips[5] ^= 0x40;
	qa_check(!JE_decryptShips(), "a corrupted table is rejected");

	memcpy(extraShips, backup, sizeof(backup));

	qa_check(JE_shapeCodecSelfTest(), "the sprite cell codec round-trips every cell");
	qa_check(JE_legacyUserShapeSelfTest(),
	         "a legacy User.shp imports every cell and becomes transferable custom ships");
	qa_check(JE_legacyUserShapeCaseSelfTest(),
	         "User.shp discovery is case-insensitive on case-sensitive filesystems");
	qa_check(JE_stockExtraShapesSelfTest(),
	         "the ship editor can reload the untouched stock newsh$.shp");
	qa_check(JE_shipEditorConfirmationSelfTest(),
	         "destructive ship-editor actions require two consecutive activations");
	qa_check(JE_captureHullListSelfTest(),
	         "the sprite editor offers each compatible hull graphic exactly once");
	qa_check(JE_shipEditorGraphicCycleSelfTest(),
	         "the ship editor cycles Nort Ship immediately after Dragonwing");
	{
		const unsigned int gr = ships[16].shipgraphic - 500;
		qa_check(ships[16].shipgraphic == 581 &&
		         sprite2_hflip_equal(spriteSheetT2000, gr - 4, gr + 5) &&
		         sprite2_hflip_equal(spriteSheetT2000, gr - 3, gr + 4) &&
		         sprite2_hflip_equal(spriteSheetT2000, gr + 15, gr + 24) &&
		         sprite2_hflip_equal(spriteSheetT2000, gr + 16, gr + 23),
		         "Gencore II replaces its malformed hard-left pose with a clean mirror");
	}
	{
		const unsigned int gr = ships[10].shipgraphic;
		qa_check(gr == 271 &&
		         !sprite2_has_color(spriteSheet9, gr + 5, 0xfe) &&
		         !sprite2_has_color(spriteSheet9, gr + 18, 0xfe) &&
		         !sprite2_has_color(spriteSheet9, gr + 22, 0xfe) &&
		         !sprite2_has_color(spriteSheet9, gr + 24, 0xfe),
		         "the stock U-Ship poses contain no stray white pixels");
	}

	{
		static const JE_word legacy[] = { 233, 157, 195, 271, 81, 0, 119 };
		static const JE_word custom[] = { 5, 43, 81, 119, 157, 195, 233, 271 };
		Sprite2_array *seenSheets[SHIP_DRAGONWING + 7];
		JE_word seenGraphics[SHIP_DRAGONWING + 7];
		const JE_byte savedGraphic = extraShips[0];
		const bool savedNet = isNetworkGame;
		const int maxGraphic = extraShipGraphicMax();
		int seenCount = 0;
		bool unique = true, named = true, nort = false, dragonwing = false, t2000 = false;
		bool legacyCompatible = true, customCompatible = true;

		isNetworkGame = false;
		for (int graphic = 1; graphic <= maxGraphic; ++graphic)
		{
			extraShips[0] = (JE_byte)graphic;
			Sprite2_array *sheet = NULL;
			const JE_word gr = JE_SGr(0, 1, &sheet);
			if (extraShipGraphicIsCustom(graphic))
				continue;

			for (int i = 0; i < seenCount; ++i)
				unique = unique && !(seenSheets[i] == sheet && seenGraphics[i] == gr);
			seenSheets[seenCount] = sheet;
			seenGraphics[seenCount++] = gr;
			named = named && extraShipEditorGraphicName(graphic) != NULL;
			nort = nort || (sheet == &spriteSheet9 && gr == 1);
			dragonwing = dragonwing || (sheet == &spriteSheet9 && gr == 0);
			t2000 = t2000 || sheet == &spriteSheetT2000;
		}

		for (uint i = 0; i < COUNTOF(legacy); ++i)
		{
			extraShips[0] = (JE_byte)(i + 1);
			Sprite2_array *sheet = NULL;
			legacyCompatible = legacyCompatible && JE_SGr(0, 1, &sheet) == legacy[i]
			                   && sheet == &spriteSheet9;
		}
		for (uint i = 0; i < COUNTOF(custom); ++i)
		{
			extraShips[0] = (JE_byte)(i + 8);
			Sprite2_array *sheet = NULL;
			customCompatible = customCompatible && JE_SGr(0, 1, &sheet) == custom[i]
			                   && sheet == &extraShapes;
		}

		extraShips[0] = savedGraphic;
		isNetworkGame = savedNet;
		qa_check(maxGraphic > 15 && maxGraphic <= 255,
		         "the loadout graphic row extends beyond the original ShipEdit choices");
		qa_check(unique, "the loadout graphic row contains no duplicate built-in sprite identity");
		qa_check(named, "every built-in graphic choice has an in-game ship name");
		qa_check(nort && dragonwing, "the loadout graphic row includes Nort Ship and Dragonwing");
		qa_check(t2000, "the loadout graphic row includes Tyrian 2000 hull art");
		qa_check(legacyCompatible && customCompatible,
		         "graphics 1 through 15 retain their original ShipEdit meanings");
		printf("# ship editor graphics: %d unique built-ins, 8 custom banks, codes 1..%d\n",
		       seenCount, maxGraphic);
	}

	{
		const bool savedEnabled = customWeaponEnabled;
		const int savedPorts[CUSTOM_WEAPON_OWNERS] = { customWeaponOwnerPort[0], customWeaponOwnerPort[1] };

		qa_check(extraShipResolvePort(0, 12) == 12, "an ordinary weapon byte resolves to itself");

		customWeaponEnabled = true;
		customWeaponOwnerPort[0] = 60;
		customWeaponOwnerPort[1] = 59;
		qa_check(extraShipResolvePort(0, EXTRA_SHIP_CUSTOM_PORT) == 60 &&
		         extraShipResolvePort(1, EXTRA_SHIP_CUSTOM_PORT) == 59,
		         "each seat resolves the sentinel to its own reserved port");

		customWeaponEnabled = false;
		qa_check(extraShipResolvePort(0, EXTRA_SHIP_CUSTOM_PORT) == 60 &&
		         extraShipResolvePort(1, EXTRA_SHIP_CUSTOM_PORT) == 59,
		         "the sentinel resolves the same with the local toggle off");

		customWeaponEnabled = true;
		customWeaponOwnerPort[0] = 0;
		qa_check(extraShipResolvePort(0, EXTRA_SHIP_CUSTOM_PORT) == 0,
		         "...and where no port was free for that seat");

		customWeaponOwnerPort[0] = savedPorts[0];
		customWeaponOwnerPort[1] = savedPorts[1];
		customWeaponEnabled = savedEnabled;
	}

	{
		Uint8 *const stream = malloc(EXTRA_SHIPS_WIRE_MAX);
		const size_t total = extraShipsSerialize(stream, EXTRA_SHIPS_WIRE_MAX);
		qa_check(total >= 6 + sizeof(JE_ShipsType), "the ship file serializes for the wire");
		qa_check(extraShipsAdopt(1, stream, total), "a serialized ship file adopts into a seat");
		qa_check(!extraShipsAdopt(1, stream, 10), "a truncated ship file is refused");

		const bool savedNet = isNetworkGame;
		isNetworkGame = true;
		qa_check(extraShipsFor(1) != extraShips &&
		         memcmp(extraShipsFor(1), extraShips, sizeof(JE_ShipsType)) == 0 &&
		         extraAvailFor(1) == extraAvail &&
		         extraShapesFor(1)->size == extraShapes.size,
		         "online seat accessors serve the adopted copy");
		isNetworkGame = false;
		qa_check(extraShipsFor(1) == extraShips, "offline both seats read the local file");
		isNetworkGame = savedNet;

		extraShipsNetReset();
		free(stream);
	}
}

/* Shield and armor glows are personal presentation state. They advance once per real tick,
 * outside rollback re-simulation. */
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

/* The two repair specials have to stay distinct where there are two hulls. */
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

/* What a twiddle charges. */
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
 * 1..4 are UP/DOWN/LEFT/RIGHT, 5..8 the same four with fire held, 9 everything released. */
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

	/* A whole combo over the wire on an upside-down screen. */
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

/* A twiddle keeps its own clock. */
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

// One tick of the live flare inside JE_doSpecialShot, with every fire gate shut.
static void qa_flare_tick(void)
{
	uint armor = 30, shield = 50;
	SFExecuted[0] = 0;
	JE_doSpecialShot(1, &armor, &shield);
}

/* A flare may release only the full-screen grade it acquired. See
 * doc/notes.md#twiddles-and-specials. */
static void qa_test_flare_grade_ownership(void)
{
	const JE_boolean savedActive = filterActive, savedFade = filterFade;
	const JE_boolean savedFadeStart = filterFadeStart, savedOwns = flareOwnsFilter;
	const JE_shortint savedFilter = levelFilter, savedBright = levelBrightness;
	const JE_shortint savedChg = levelBrightnessChg, savedColChg = flareColChg;
	const JE_shortint savedWpnFilter = specialWeaponFilter, savedFreq = specialWeaponFreq;
	const JE_byte savedWait = specialWait;
	const JE_byte savedNextWait = nextSpecialWait, savedRepeat = shotRepeat[SHOT_SPECIAL];
	const JE_byte savedZing = zinglonDuration, savedAstral = astralDuration;
	const JE_word savedFlare = flareDuration;
	const JE_boolean savedFlareStart = flareStart, savedLink = linkToPlayer;
	const JE_boolean savedAuto = autoFireSpecial, savedDbgAuto = debugAutofireTwiddle;
	const JE_boolean savedTrigger = debugTwiddleTrigger, savedEndless = endlessMode;
	const JE_byte savedDbgTwiddle = debugTwiddleSpecial, savedExec = SFExecuted[0];
	const Player saved0 = player[0];

	player[0].items.special = 0;
	autoFireSpecial = false;
	debugAutofireTwiddle = false;
	debugTwiddleTrigger = false;
	debugTwiddleSpecial = 0;
	endlessMode = false;
	endlessSetFxPlayer(0);
	JE_resetTwiddleClocks();
	specialWeaponFreq = 0;  // the flare spawns nothing
	specialWait = 0;
	nextSpecialWait = 0;
	linkToPlayer = false;
	zinglonDuration = 0;
	astralDuration = 0;

	// A level's white flash half way up, and a tint-less flare (MegaLaser, Missile Pod...) on its
	// last tick: the flash keeps ramping.
	filterActive = true; filterFade = true; filterFadeStart = true;
	levelFilter = -99; levelBrightness = 6; levelBrightnessChg = 1;
	flareOwnsFilter = false;
	specialWeaponFilter = -99;
	flareDuration = 1; flareStart = true;
	qa_flare_tick();
	qa_check(!flareStart && flareDuration == 0 && filterActive && filterFade
	         && levelFilter == -99 && levelBrightness == 6,
	         "a tint-less flare running out leaves a level's white flash to finish");

	// A level's own filter 7 (CORE's late red tint) with a Flare running: no pulse, no ownership,
	// and the tint outlives the flare.
	filterActive = true; filterFade = false;
	levelFilter = 7; levelBrightness = -99;
	flareOwnsFilter = false;
	specialWeaponFilter = 7;
	flareDuration = 5; flareStart = true;
	qa_flare_tick();
	const bool leftAlone = !flareOwnsFilter && filterActive && levelFilter == 7
	                    && levelBrightness == -99;
	flareDuration = 1;
	qa_flare_tick();
	qa_check(leftAlone && !flareStart && filterActive && levelFilter == 7 && levelBrightness == -99,
	         "a Flare over a level's own red tint neither pulses nor removes it");

	// An idle grade (the level's fade long finished): the Flare takes it, pulses it, and hands it
	// back when it runs out.
	filterActive = true; filterFade = false;
	levelFilter = -99; levelBrightness = -99;
	flareOwnsFilter = false;
	flareDuration = 5; flareStart = true;
	qa_flare_tick();
	const bool taken = flareOwnsFilter && filterActive && levelFilter == 7
	                && (levelBrightness == 1 || levelBrightness == -1);
	flareDuration = 1;
	qa_flare_tick();
	qa_check(taken && !flareStart && !flareOwnsFilter && !filterActive
	         && levelFilter == -99 && levelBrightness == -99,
	         "a Flare takes an idle grade, pulses it, and hands it back when it runs out");

	player[0] = saved0;
	SFExecuted[0] = savedExec;
	debugTwiddleSpecial = savedDbgTwiddle;
	endlessMode = savedEndless;
	debugTwiddleTrigger = savedTrigger;
	debugAutofireTwiddle = savedDbgAuto;
	autoFireSpecial = savedAuto;
	linkToPlayer = savedLink;
	flareStart = savedFlareStart;
	flareDuration = savedFlare;
	astralDuration = savedAstral;
	zinglonDuration = savedZing;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	nextSpecialWait = savedNextWait;
	specialWait = savedWait;
	specialWeaponFreq = savedFreq;
	specialWeaponFilter = savedWpnFilter;
	flareColChg = savedColChg;
	levelBrightnessChg = savedChg;
	levelBrightness = savedBright;
	levelFilter = savedFilter;
	flareOwnsFilter = savedOwns;
	filterFadeStart = savedFadeStart;
	filterFade = savedFade;
	filterActive = savedActive;
	JE_resetTwiddleClocks();
}

/* Zinglon width, refresh, damage scaling, and co-op ownership. */
static void qa_test_zinglon_pillar(void)
{
	const JE_byte savedZing = zinglonDuration, savedRamp = zinglonRamp;
	const JE_byte savedStype = special[SPECIAL_NUM].stype;
	const JE_byte savedRepeat = shotRepeat[SHOT_SPECIAL];
	const JE_boolean savedEndless = endlessMode, savedMods = endlessCampaignMods;
	const JE_boolean savedCoop = coopEndlessMode;
	JE_byte savedPerks[2][PERK_COUNT];
	memcpy(savedPerks, endlessPerkTakenBy, sizeof(savedPerks));
	Player savedShips[2];
	memcpy(savedShips, player, sizeof(savedShips));
	int savedSalvo[2];
	memcpy(savedSalvo, endlessSalvoWindow, sizeof(savedSalvo));

	// Upstream drew the beam as `25 - abs(duration - 25)`, which the ramp has to reproduce exactly.
	bool stockCurve = true;
	for (int d = 50, ramp = 0; d > 1; --d)
	{
		stockCurve = stockCurve && zinglon_pillar_width(ramp, d) == 25 - abs(d - 25);
		if (ramp < ZINGLON_PILLAR_HALF_W)
			++ramp;
	}
	qa_check(stockCurve, "the Zinglon pillar traces vanilla's width curve over a stock blast");

	// A stretched one opens on the same ramp, holds every tick the stretch bought, then closes.
	int held = 0, lastWidth = -1;
	bool bounded = true;
	for (int d = 110, ramp = 0; d > 1; --d)
	{
		const int w = zinglon_pillar_width(ramp, d);
		bounded = bounded && w >= 0 && w <= ZINGLON_PILLAR_HALF_W;
		held += (w == ZINGLON_PILLAR_HALF_W);
		lastWidth = w;
		if (ramp < ZINGLON_PILLAR_HALF_W)
			++ramp;
	}
	qa_check(bounded && held == 110 - 2 * ZINGLON_PILLAR_HALF_W + 1 && lastWidth == 2,
	         "a stretched Zinglon blast spends its extra ticks open and still closes at the end");

	// The scratch slot takes Soul of Zinglon's shape: the pillar with no flare beside it.
	special[SPECIAL_NUM].stype = 3;
	endlessMode = false;
	endlessCampaignMods = false;
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetFxPlayer(0);

	zinglonDuration = 0;
	zinglonRamp = 0;
	JE_specialComplete(1, SPECIAL_NUM);
	const JE_byte stockTicks = zinglonDuration;

	// A blast onto a live beam refreshes the window and leaves the beam at the width it reached.
	zinglonRamp = ZINGLON_PILLAR_HALF_W;
	zinglonDuration = 5;
	JE_specialComplete(1, SPECIAL_NUM);
	const bool refireHolds = zinglonRamp == ZINGLON_PILLAR_HALF_W && zinglonDuration == stockTicks;

	// One fired with the beam spent opens from closed again.
	zinglonDuration = 1;
	zinglonRamp = ZINGLON_PILLAR_HALF_W;
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(stockTicks == 50 && refireHolds && zinglonRamp == 0,
	         "a Zinglon blast on a live beam refreshes it, and one fired cold opens from nothing");

	// Ordnance Reserves stretches the blast, as it already stretched the flare beside it.
	endlessMode = true;
	endlessPerkSetOwned(PERK_ORDNANCE, endlessPerkMaxStack(PERK_ORDNANCE));
	zinglonDuration = 0;
	zinglonRamp = 0;
	JE_specialComplete(1, SPECIAL_NUM);
	qa_check(zinglonDuration == endlessPerkSpecialDuration(50, 255) && zinglonDuration > stockTicks,
	         "Ordnance Reserves stretches the Zinglon blast");

	/* The pillar helper supplies collision data for the beam's reserved shot slot. */
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	memset(endlessSalvoWindow, 0, sizeof(endlessSalvoWindow));
	coopEndlessMode = false;
	endlessMode = false;
	player[0].x = 100;
	zinglonRamp = ZINGLON_PILLAR_HALF_W;
	zinglonDuration = 30;

	int width = 0, dmg = 0;
	uint owner = 9;
	const bool onBeam = zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	qa_check(onBeam && dmg == ZINGLON_PILLAR_DAMAGE && width == ZINGLON_PILLAR_HALF_W && owner == 0,
	         "outside an endless run the beam deals its stock damage, on the one ship");
	qa_check(!zinglon_pillar_hit(player[0].x + 7 + ZINGLON_PILLAR_HALF_W, &width, &dmg, &owner)
	         && dmg == 0,
	         "...and a hull past its edge takes nothing");

	// The pillar uses the same damage modifiers as player shots.
	endlessMode = true;
	endlessPerkSetOwned(PERK_DAMAGE, endlessPerkMaxStack(PERK_DAMAGE));
	zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	const int buffed = dmg;
	qa_check(buffed == endlessScaleOwnDamage(ZINGLON_PILLAR_DAMAGE, false)
	         && buffed > ZINGLON_PILLAR_DAMAGE,
	         "Heavy Rounds lifts the beam the way it lifts a bullet");
	endlessSalvoWindow[0] = ENDLESS_PERK_SALVO_WINDOW;
	endlessPerkSetOwned(PERK_SALVO, endlessPerkMaxStack(PERK_SALVO));
	zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	qa_check(dmg == endlessScaleOwnDamage(ZINGLON_PILLAR_DAMAGE, true) && dmg > buffed,
	         "...and a charged Opening Salvo window bumps it further");
	memset(endlessSalvoWindow, 0, sizeof(endlessSalvoWindow));
	endlessPerkSetOwned(PERK_SALVO, 0);
	endlessPerkSetOwned(PERK_DAMAGE, 0);

	/* Overlapping co-op beams use the stronger hit and its owner. */
	coopEndlessMode = true;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].x = 100;
		player[p].zinglon_ramp = ZINGLON_PILLAR_HALF_W;
		player[p].zinglon_duration = 30;
	}
	endlessPerkTakenBy[1][PERK_DAMAGE] = endlessPerkMaxStack(PERK_DAMAGE);
	endlessPerkRederive();
	const bool pairHit = zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	qa_check(pairHit && owner == 1 && dmg == buffed,
	         "under two beams the hull takes the stronger one, billed to the ship behind it");
	player[1].zinglon_duration = 0;
	zinglon_pillar_hit(player[0].x + 7, &width, &dmg, &owner);
	qa_check(owner == 0 && dmg == ZINGLON_PILLAR_DAMAGE,
	         "...and with that beam spent the partner's own scale is all that is left");

	memcpy(endlessSalvoWindow, savedSalvo, sizeof(savedSalvo));
	memcpy(player, savedShips, sizeof(savedShips));
	coopEndlessMode = savedCoop;
	memcpy(endlessPerkTakenBy, savedPerks, sizeof(savedPerks));
	endlessPerkRederive();
	endlessCampaignMods = savedMods;
	endlessMode = savedEndless;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	special[SPECIAL_NUM].stype = savedStype;
	zinglonRamp = savedRamp;
	zinglonDuration = savedZing;
}

/* No special outlives the level that fired it, and the debug fire helpers stay out of a demo.
 * Either one left alone puts the previous run's special into the title screen's demo. */
static void qa_test_special_state_reset(void)
{
	if (VGAScreenSeg == NULL || game_screen == NULL)
		return;  // the live flare below spawns its shots

	const JE_byte savedStype = special[SPECIAL_NUM].stype, savedPwr = special[SPECIAL_NUM].pwr;
	const JE_byte savedZing = zinglonDuration, savedAstral = astralDuration;
	const JE_byte savedWait = specialWait, savedNextWait = nextSpecialWait;
	const JE_byte savedRepeat = shotRepeat[SHOT_SPECIAL], savedExec = SFExecuted[0];
	const JE_byte savedTemp2 = temp2, savedDbgTwiddle = debugTwiddleSpecial;
	const JE_word savedFlare = flareDuration;
	const JE_boolean savedFlareStart = flareStart, savedSpray = spraySpecial;
	const JE_boolean savedLink = linkToPlayer, savedTrigger = debugTwiddleTrigger;
	const JE_boolean savedAuto = autoFireSpecial, savedDbgAuto = debugAutofireTwiddle;
	const JE_boolean savedEndless = endlessMode, savedDemo = play_demo;
	const JE_shortint savedFilter = specialWeaponFilter, savedFreq = specialWeaponFreq;
	const JE_word savedWpn = specialWeaponWpn;
	const Player saved0 = player[0], saved1 = player[1];
	SDL_Surface *const savedVGA = VGAScreen;

	// A special of every shape is mid-flight: a flare spawning shots, a beam, a recharge, a
	// twiddle the menu armed but nobody consumed.
	zinglonDuration = 40;
	zinglonRamp = ZINGLON_PILLAR_HALF_W;
	astralDuration = 30;
	flareDuration = 200;
	flareStart = true;
	specialWait = 15;
	nextSpecialWait = 20;
	debugTwiddleTrigger = true;

	JE_resetSpecialState();
	qa_check(flareDuration == 0 && !flareStart && zinglonDuration == 0 && zinglonRamp == 0
	         && astralDuration == 0
	         && specialWait == 0 && nextSpecialWait == 0 && !debugTwiddleTrigger,
	         "level start clears every clock a fired special left running");

	// Two ships keep their own copy of those clocks and swap them in per ship, so the mirrors are
	// the other half of the barrier: only a dual-ship level start clears them.
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		player[p].flare_duration = 100;
		player[p].flare_start = true;
		player[p].zinglon_duration = 20;
		player[p].zinglon_ramp = ZINGLON_PILLAR_HALF_W;
		player[p].astral_duration = 20;
		player[p].special_wait = 10;
		player[p].next_special_wait = 10;
		player[p].spray_special = true;
	}
	coop_ship_runtime_reset();
	bool mirrorsIdle = true;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		mirrorsIdle = mirrorsIdle && player[p].flare_duration == 0 && !player[p].flare_start
		           && player[p].zinglon_duration == 0 && player[p].zinglon_ramp == 0
		           && player[p].astral_duration == 0
		           && player[p].special_wait == 0 && player[p].next_special_wait == 0
		           && !player[p].spray_special;
	}
	qa_check(mirrorsIdle, "a dual-ship level start clears both ships' special mirrors");

	// The scratch slot takes the Minefield shape: a flare whose shots follow the ship. Clearing
	// the equipped special and Autofire Special leaves the debug twiddle as the only fire gate.
	special[SPECIAL_NUM].stype = 16;
	special[SPECIAL_NUM].pwr = 2;
	player[0].items.special = 0;
	autoFireSpecial = false;
	endlessMode = false;
	endlessSetFxPlayer(0);
	debugAutofireTwiddle = true;
	debugTwiddleSpecial = SPECIAL_NUM;
	button[0] = true;
	SFExecuted[0] = 0;
	shotRepeat[SHOT_SPECIAL] = 0;

	uint armor = 30, shield = 50;
	play_demo = true;
	JE_doSpecialShot(1, &armor, &shield);
	const bool demoStaysClean = flareDuration == 0;

	play_demo = false;
	JE_doSpecialShot(1, &armor, &shield);
	qa_check(demoStaysClean && flareDuration > 0,
	         "the debug twiddle stays out of a demo and fires for a player at the controls");

	VGAScreen = savedVGA;
	player[0] = saved0;
	player[1] = saved1;
	play_demo = savedDemo;
	endlessMode = savedEndless;
	debugAutofireTwiddle = savedDbgAuto;
	autoFireSpecial = savedAuto;
	debugTwiddleTrigger = savedTrigger;
	debugTwiddleSpecial = savedDbgTwiddle;
	linkToPlayer = savedLink;
	spraySpecial = savedSpray;
	specialWeaponWpn = savedWpn;
	specialWeaponFreq = savedFreq;
	specialWeaponFilter = savedFilter;
	flareStart = savedFlareStart;
	flareDuration = savedFlare;
	temp2 = savedTemp2;
	SFExecuted[0] = savedExec;
	shotRepeat[SHOT_SPECIAL] = savedRepeat;
	nextSpecialWait = savedNextWait;
	specialWait = savedWait;
	astralDuration = savedAstral;
	zinglonDuration = savedZing;
	special[SPECIAL_NUM].pwr = savedPwr;
	special[SPECIAL_NUM].stype = savedStype;
	button[0] = false;
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

	// Every binary endless.sav an older build could have left must still import.
	for (int version = 3; version <= endlessSaveLegacyVersionMax(); ++version)
	{
		snprintf(path, sizeof(path), "%s/v%02d.sav", qa_fixture_dir, version);
		detail[0] = '\0';
		const bool okay = endlessSaveTestFixture(path, detail, sizeof(detail));
		char label[640];
		snprintf(label, sizeof(label), "legacy endless save v%02d imports and round-trips as text%s%s",
		         version, detail[0] ? ": " : "", detail);
		qa_check(okay, label);
	}

	// ...and the text record itself: every field survives, and a stray or missing key costs only itself.
	detail[0] = '\0';
	const bool codec = endlessSaveTestTextCodec(detail, sizeof(detail));
	char label[320];
	snprintf(label, sizeof(label), "endless text record codec%s%s", detail[0] ? ": " : "", detail);
	qa_check(codec, label);

	// The campaign slot codec, the same way.
	detail[0] = '\0';
	const bool slots = save_file_test_codec(detail, sizeof(detail));
	snprintf(label, sizeof(label), "campaign slot codec%s%s", detail[0] ? ": " : "", detail);
	qa_check(slots, label);

	// Customize is inserted without changing stock row mappings.
	detail[0] = '\0';
	const bool rows = game_menu_test_outpost_rows(detail, sizeof(detail));
	snprintf(label, sizeof(label), "online outpost rows%s%s", detail[0] ? ": " : "", detail);
	qa_check(rows, label);

	/* A real pair of files from a build before opentyrian.sav imports over the live tables the way
	 * first launch does. They sit in the fixture directory's `legacy` sibling. Restored afterwards,
	 * apart from the endless slot cache, which the suite's own saves overwrite slot by slot. */
	JE_SaveFilesType savedSlots;
	T2KHighScoreType savedBoards[20][3];
	memcpy(savedSlots, saveFiles, sizeof(savedSlots));
	memcpy(savedBoards, t2kHighScores, sizeof(savedBoards));
	const bool savedEndless = endlessMode;

	snprintf(path, sizeof(path), "%s/../legacy/tyrian.sav", qa_fixture_dir);
	qa_check(save_legacy_test_import(path), "a legacy tyrian.sav passes its checksums and imports");
	qa_check(saveFiles[1].level == 1 && saveFiles[1].score == 999999999
	         && strcmp(saveFiles[1].name, "TEST ENDLESS  ") == 0 && saveFiles[1].episode == 1
	         && saveFiles[9].level == 23 && saveFiles[9].score == 96422
	         && saveFiles[21].level == 4 && saveFiles[21].episode == 4
	         && strcmp(saveFiles[21].name, "LAST LEVEL    ") == 0 && saveFiles[2].level == 0,
	         "the imported legacy slots carry their names, levels, episodes and cash");
	snprintf(path, sizeof(path), "%s/../legacy/endless.sav", qa_fixture_dir);
	qa_check(endlessSaveLegacyTestImport(path), "a legacy endless.sav imports");
	// Slot 14 held a run in the sidecar but no campaign game: an orphan, dropped on import.
	qa_check(endlessSlotHasRun(10) && endlessSlotHasRun(2) && !endlessSlotHasRun(3) && !endlessSlotHasRun(14),
	         "the imported legacy endless slots pair with the campaign slots that hold runs");
	qa_check(endlessLoadSlot(10) && endlessRunDepth == 8 && endlessRunKills == 1652,
	         "an imported legacy endless run resumes at its zone with its kill tally");
	endlessResetRun();
	endlessMode = savedEndless;

	/* The repair pass: a save file whose Endless-named slot lost its half (an import that could
	 * not read the sidecar) gets it back from that sidecar, and only that slot. */
	endlessMode = false;
	endlessSaveCaptureSlot(10);   // not in endless mode: clears the half, as such an import left it
	endlessMode = savedEndless;
	SDL_strlcpy(saveFiles[9].levelName, "ZONE 9", sizeof(saveFiles[9].levelName));
	qa_check(!endlessSlotHasRun(10) && endlessSaveLegacyTestRepair(path) && endlessSlotHasRun(10)
	         && !endlessSlotHasRun(14),
	         "a slot named for a zone with no run behind it takes its run back from the old sidecar");
	qa_check(!endlessSaveLegacyTestRepair(path), "the repair pass is a no-op once every zone slot has its run");
	qa_check(endlessSaveLegacyWasRead(), "reading a sidecar through is what marks it taken in");

	// ...and a sidecar from a build past v27 still gives up the v27 prefix of every record.
	snprintf(path, sizeof(path), "%s/v27.sav", qa_fixture_dir);
	detail[0] = '\0';
	const bool newer = endlessSaveTestNewerLegacy(path, detail, sizeof(detail));
	snprintf(label, sizeof(label), "a legacy endless.sav newer than v27 imports its known fields%s%s",
	         detail[0] ? ": " : "", detail);
	qa_check(newer, label);

	memcpy(saveFiles, savedSlots, sizeof(savedSlots));
	memcpy(t2kHighScores, savedBoards, sizeof(savedBoards));
}

int qa_run_unit_suite(void)
{
	qa_checks = qa_failures = 0;
	printf("TAP version 13\n");

	// Test transfer before normal episode setup, matching a fresh title screen.
	qa_test_save_transfer_preinit();
	qa_test_any_button_latch();

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
	qa_test_save_transfer();
	qa_test_load_screen_help();
	qa_test_extra_ship_return();
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
	qa_test_ship_editor_file();
	qa_test_special_light_events();
	qa_test_partner_repair_special();
	qa_test_twiddle_ships();
	qa_test_twiddle_diagonals();
	qa_test_twiddle_wire();
	qa_test_twiddle_strictness();
	qa_test_twiddle_cooldown();
	qa_test_twiddle_charges();
	qa_test_flare_grade_ownership();
	qa_test_zinglon_pillar();
	qa_test_special_state_reset();
	qa_test_modifier_online_parity();
	qa_test_effect_gates();
	qa_test_shot_hitboxes();
	qa_test_guidance_perk();
	qa_test_twin_pods_perk();
	qa_test_reinforced_prow_perk();
	qa_test_knife_fight_perk();
	qa_test_deflector_perk();
	qa_test_countermeasure_burst();
	qa_test_guided_screen_aim();
	qa_test_health_bar_scale();
	qa_test_vulnerable_cue();
	qa_test_elite_tier_eligibility();
	qa_test_homing_chaser_eligibility();
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
	qa_test_knife_fight_blood();
	qa_test_online_ship_style();
	qa_test_partner_hp_bars();
	qa_test_network_settings();
	qa_test_network_endless_lobby();
	qa_test_seeker_tiers();
	qa_test_endless_coop();
	qa_test_kill_fire_drives();
	qa_test_kill_fire_wiring();
	qa_test_kinetic_converter();
	qa_test_coop_combo_and_pickups();
	qa_test_peer_left_level();
	qa_test_peer_idle_rule();
	qa_test_menu_claim();
	qa_test_online_suite();
	qa_test_endless_suite();
	qa_test_save_fixtures();
	qa_test_resync_serialization();
	qa_test_courses();
	qa_test_finale_mods();
	qa_test_course_base_rule();
	qa_test_course_shuffle_rule();
	qa_test_course_reroll();
	qa_test_course_seat_parity();
	qa_test_course_reroll_dodge();
	qa_test_item_data_settings();
	qa_test_firing_sound_levels();
#ifdef WITH_MIDI
	qa_test_lds_midi_detune();
#endif
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
