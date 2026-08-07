/* Project-owned unit, property, serialization, and replay tests. */
#include "qa.h"

#include "config.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "endless.h"
#include "episodes.h"
#include "endless_internal.h"
#include "fonthand.h"
#include "mainint.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "network.h"
#include "nortvars.h"
#include "player.h"
#include "render_list.h"
#include "rollback.h"
#include "shots.h"
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
bool qa_replay_expect_set = false;
Uint32 qa_replay_expect = 0;
int qa_net_rounds = 0;
int qa_net_scenario = 0;
int qa_net_version_skew = 0;
unsigned long qa_net_gameplay_ticks = 0;
unsigned long qa_net_corrupt_frame = 0;
bool qa_net_save_exit = false;
int qa_net_resume_slot = 0;
int qa_net_loadout = 0;
unsigned long qa_net_menu_frame = 0;
int qa_net_game_type = -1;
int qa_net_zones = 0;
int qa_net_zones_cleared = 0;
bool qa_net_lobby_settings = false;

/* The forced modifier slate per Endless wire zone. Ten depths cover every charted modifier bit
 * (the gamble-only and banked-boon bits included); depths past the table fly unmodified. Each
 * row respects the compatibility rules course generation enforces (see qa_mods_compatible).
 * Both machines derive the slate from the depth alone, so it can never diverge. */
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
		/* Ammo-limited and charge-up kicks against a custom design and a satellite: the
		 * counters all live in player[].sidekick and must cross rollback intact. The custom
		 * design is the identical startup default on both machines, adopted into owner 1's
		 * slots the way the outpost exchange would deliver it, so both simulations hold the
		 * same compiled sidekick. */
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
	if ((m & ENDLESS_MOD_NOELITE) && (m & (ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION))) return false;
	if ((m & ENDLESS_MOD_NOCHAMP) && (m & ENDLESS_MOD_LEGION)) return false;
	if ((m & ENDLESS_MOD_DEADGEN) && (m & ENDLESS_MOD_STATIC)) return false;
	if ((m & ENDLESS_MOD_NOELITE) && (m & ENDLESS_MOD_NOCHAMP)) return false;
	if ((m & ENDLESS_MOD_GRAVITY_OMNI) && !(m & ENDLESS_MOD_GRAVITY)) return false;
	if (qa_popcount64(m & ENDLESS_MOD_KILLFIRE_ANY) > 1) return false;
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
	int  savedDiff[ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT];
	bool savedDiffCustom[ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT];
	int  savedUntagged[ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT];
	bool savedUntaggedCustom[ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT];
	memcpy(savedDiff, endlessBestZoneDiff, sizeof(savedDiff));
	memcpy(savedDiffCustom, endlessBestZoneDiffCustom, sizeof(savedDiffCustom));
	memcpy(savedUntagged, endlessBestZoneUntagged, sizeof(savedUntagged));
	memcpy(savedUntaggedCustom, endlessBestZoneUntaggedCustom, sizeof(savedUntaggedCustom));
	memset(endlessBestZoneDiff, 0, sizeof(savedDiff));
	memset(endlessBestZoneDiffCustom, 0, sizeof(savedDiffCustom));
	memset(endlessBestZoneUntagged, 0, sizeof(savedUntagged));
	memset(endlessBestZoneUntaggedCustom, 0, sizeof(savedUntaggedCustom));

	endlessBestZoneDiff[0][ENDLESS_RUNMODE_STANDARD][0] = 20;
	endlessBestZoneDiff[0][ENDLESS_RUNMODE_STANDARD][1] = 20;
	endlessBestZoneDiffCustom[0][ENDLESS_RUNMODE_STANDARD][1] = true;
	endlessBestZoneUntagged[0][ENDLESS_RUNMODE_STANDARD] = 15;
	endlessBestZoneUntaggedCustom[0][ENDLESS_RUNMODE_STANDARD] = true;
	qa_check(endlessBestZoneAny(0, ENDLESS_RUNMODE_STANDARD) == 20
	         && strcmp(endlessRecordAnyCustomMark(0, ENDLESS_RUNMODE_STANDARD), " C") == 0,
	         "deepest record derives its custom mark from any marked record tied at that depth");
	endlessBestZoneUntagged[0][ENDLESS_RUNMODE_STANDARD] = 25;
	endlessBestZoneUntaggedCustom[0][ENDLESS_RUNMODE_STANDARD] = false;
	qa_check(endlessBestZoneAny(0, ENDLESS_RUNMODE_STANDARD) == 25
	         && endlessRecordAnyCustomMark(0, ENDLESS_RUNMODE_STANDARD)[0] == '\0',
	         "legacy untagged record survives and owns the mode-wide mark when deepest");
	endlessBestZoneUntaggedCustom[0][ENDLESS_RUNMODE_STANDARD] = true;
	qa_check(strcmp(endlessRecordAnyCustomMark(0, ENDLESS_RUNMODE_STANDARD), " C") == 0,
	         "legacy untagged custom mark is retained");

	/* The two crew sizes keep separate books: a solo record is invisible to the co-op table. */
	endlessBestZoneDiff[1][ENDLESS_RUNMODE_STANDARD][0] = 60;
	qa_check(endlessBestZoneAny(1, ENDLESS_RUNMODE_STANDARD) == 60
	         && endlessBestZoneAny(0, ENDLESS_RUNMODE_STANDARD) == 25,
	         "one-player and two-player zone records are kept apart");
	endlessClearDeepestRecord(1, ENDLESS_RUNMODE_STANDARD);
	qa_check(endlessBestZoneAny(1, ENDLESS_RUNMODE_STANDARD) == 0
	         && endlessBestZoneAny(0, ENDLESS_RUNMODE_STANDARD) == 25,
	         "erasing a co-op record leaves the solo one standing");

	bool difficultyMap = true;
	for (int i = 0; i < ENDLESS_DIFFICULTY_COUNT; ++i)
		difficultyMap &= endlessDifficultySlot(endlessDifficultyLevel[i]) == i;
	qa_check(difficultyMap && endlessDifficultySlot(-999) == -1
	         && endlessBestZoneAny(0, (EndlessRunMode)-1) == 0
	         && endlessBestZoneAny(-1, ENDLESS_RUNMODE_STANDARD) == 0
	         && endlessBestZoneForDifficulty(0, ENDLESS_RUNMODE_STANDARD, -1) == 0,
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

static void qa_test_weapon_editor(void)
{
	char encoded[32768], encodedAgain[32768], malformed[4096];
	Uint32 rng = 0xc0570e11u;

	customWeaponResetAllLevels();
	bool presetsValid = customBulletPresetCount > 0
	                 && customBulletPresetCount <= CUSTOM_BULLET_PRESET_MAX;
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
	         && NETWORK_SETTINGS_SIZE == 24
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
 * modes, the Bounty perk, and Double Pickups, which is pickup-only and must not touch kill
 * cash. The wallet outcomes have to be identical whichever machine simulates the kill. */
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
		coop_set_session_double_pickups(doubled != 0);
		memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
		if (perk)
			endlessPerkGrant(0, PERK_BOUNTY, 1);
		endlessPerkRederive();

		player[0].cash = player[1].cash = 0;
		endlessCashResync();
		endlessAwardEliteKill(++link, champ ? 3 : 2, 1);   // player 2's kill

		const long want = champ ? endlessChampionBounty() : endlessEliteBounty();
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
	coop_set_session_double_pickups(false);
	isNetworkGame = savedNet;
	twoPlayerMode = savedTwo;
	coopCampaignMode = savedCampaign;
	coopEndlessMode = savedCoop;
	endlessMode = savedEndless;
	thisPlayerNum = savedThis;
	endlessActiveMods = savedMods;
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
		coop_set_session_double_pickups(false);
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
	         && endlessPerkOwned[PERK_DAMAGE] == MIN(4, endlessPerkMaxStack(PERK_DAMAGE)),
	         "endless perks are the capped sum of both players' picks");

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
	 * shooting must not spend the other's charge. */
	endlessPerkTakenBy[0][PERK_SALVO] = 1;
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

	/* Double Pickups compensates a split take, and only a split take. */
	coopEndlessMode = false;
	coopCampaignMode = true;
	endlessMode = false;

	coop_set_session_shared_credit(false);
	coop_set_session_double_pickups(true);
	qa_check(coop_pickups_are_doubled(), "Double Pickups applies under Individual credit");

	player[0].cash = 0;
	player[1].cash = 0;
	player_award_pickup_cash(&player[0], 250);
	qa_check(player[0].cash == 500 && player[1].cash == 0,
	         "a doubled pickup pays its collector twice and nobody else");

	player[0].cash = 0;
	player_award_kill_cash(&player[0], 250);
	qa_check(player[0].cash == 250, "...and leaves kill cash alone");

	coop_set_session_shared_credit(true);
	qa_check(!coop_pickups_are_doubled(),
	         "Double Pickups stands down under Shared credit, where both already collect in full");
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
	coop_set_session_double_pickups(false);
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
	coop_set_session_double_pickups(false);
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
	const int savedXmas = xmasMode;
	const JE_byte savedSpeed = gameSpeed;
	const bool savedRollbackConfig = net_rollback, savedRecoveryConfig = net_desync_recovery;
	const bool savedVt = vt_ship, savedMotion = smoothMotion;
	const JE_boolean savedScroll = smoothScroll;
	const bool savedSessionMode = nrb_session_mode(), savedSessionVt = nrb_session_vt();
	const bool savedSessionRecovery = nrb_session_recovery();
	const bool savedSharedCredit = coopSharedCredit;
	const bool savedDoublePickups = coopDoublePickups;
	const JE_boolean savedCoopCampaign = coopCampaignMode;
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
	arcadeLifeBoost = true; arcadeRandomBalls = false;
	xmasMode = 1; gameSpeed = 2;
	net_rollback = true; net_desync_recovery = true;
	coopSharedCredit = true;
	coopDoublePickups = true;
	vt_ship = true; smoothMotion = true; smoothScroll = true;
	memset(guarded.bytes, 0x5a, sizeof(guarded.bytes));
	const int packed = network_settings_pack(packet);
	qa_check(packed == NETWORK_SETTINGS_SIZE && guarded.bytes[3] == 0x5a
	         && guarded.bytes[4 + NETWORK_SETTINGS_SIZE] == 0x5a,
	         "network settings packing writes exactly its fixed 24-byte block");

	/* A joiner has different local preferences before it adopts the host block. */
	for (int i = 0; i < SSW_COUNT; ++i) superSparkMode[i] = SUPER_SPARKS_OFF;
	for (int i = 0; i < EDW_COUNT; ++i) epDiffMode[i] = EPDIFF_EP13;
	zicaLaserBase = ZICA_BASE_AUTO; zicaLaserLength = ZICA_LEN_SHORT;
	zicaLaserLock = false; zicaLaserBuff = true; wallopSecondBolt = SUPER_SPARKS_OFF;
	chargeLaserCannon = false; restoreBaseDispensers = true;
	arcadeLifeBoost = false; arcadeRandomBalls = true;
	xmasMode = 0; gameSpeed = 5;
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
	         && !arcadeRandomBalls && xmasMode == 1 && gameSpeed == 2
	         && nrb_session_mode() && nrb_session_vt() && nrb_session_recovery()
	         && coop_credit_is_shared(),
	         "joiner adopts every host-authoritative simulation setting");
	// Doubling is carried in the same word but is inert under Shared, so check the flag itself
	// by flipping the credit mode the adopted value sits behind.
	coop_set_session_shared_credit(false);
	qa_check(coop_pickups_are_doubled(), "...including whether Individual pays pickups twice");
	coop_set_session_shared_credit(true);
	network_settings_restore();
	arraysMatch = true;
	for (int i = 0; i < SSW_COUNT; ++i) arraysMatch &= superSparkMode[i] == SUPER_SPARKS_OFF;
	for (int i = 0; i < EDW_COUNT; ++i) arraysMatch &= epDiffMode[i] == EPDIFF_EP13;
	qa_check(arraysMatch && zicaLaserBase == ZICA_BASE_AUTO && zicaLaserLength == ZICA_LEN_SHORT
	         && !zicaLaserLock && zicaLaserBuff && wallopSecondBolt == SUPER_SPARKS_OFF
	         && !chargeLaserCannon && restoreBaseDispensers && !arcadeLifeBoost
	         && arcadeRandomBalls && xmasMode == 0 && gameSpeed == 5,
	         "leaving a network session restores every local simulation preference");

	/* The host runs on flags armed from its own config; the joiner adopts the block packed
	 * from that same config. The two must land on identical session behavior, or the pair
	 * splits at the first payout: Double Pickups was armed on the joiner alone, and every
	 * pickup desynced the wallets by its own value. */
	coopSharedCredit = false;
	coopDoublePickups = true;
	net_rollback = true;
	net_desync_recovery = true;
	vt_ship = true; smoothMotion = true; smoothScroll = true;
	coopCampaignMode = true;
	coop_set_session_shared_credit(true);    // stale session values the arm must replace,
	coop_set_session_double_pickups(false);  // or a missed flag hides behind leftovers
	network_arm_local_session();
	const bool hostDoubled = coop_pickups_are_doubled();
	const bool hostShared = coop_credit_is_shared();
	network_settings_pack(packet);
	coop_set_session_shared_credit(true);     // a joiner arrives holding other values
	coop_set_session_double_pickups(false);
	network_settings_adopt(packet);
	qa_check(hostDoubled && !hostShared
	         && coop_pickups_are_doubled() == hostDoubled
	         && coop_credit_is_shared() == hostShared,
	         "host arming and joiner adoption produce the same credit session");
	network_settings_restore();

	memset(packet, 0xff, NETWORK_SETTINGS_SIZE);
	network_settings_adopt(packet);
	bool malformedClamped = true;
	for (int i = 0; i < SSW_COUNT; ++i) malformedClamped &= superSparkMode[i] == SUPER_SPARKS_AUTO;
	for (int i = 0; i < EDW_COUNT; ++i) malformedClamped &= epDiffMode[i] == EPDIFF_AUTO;
	qa_check(malformedClamped && zicaLaserBase == ZICA_BASE_AUTO
	         && zicaLaserLength == ZICA_LEN_SHORT && wallopSecondBolt == SUPER_SPARKS_AUTO
	         && xmasMode == -1 && gameSpeed == 4,
	         "hostile network settings are clamped before any array can be indexed");
	network_settings_restore();

	memcpy(superSparkMode, savedSpark, sizeof(savedSpark));
	memcpy(epDiffMode, savedEpDiff, sizeof(savedEpDiff));
	zicaLaserBase = savedZicaBase; zicaLaserLength = savedZicaLength;
	zicaLaserLock = savedZicaLock; zicaLaserBuff = savedZicaBuff; wallopSecondBolt = savedWallop;
	chargeLaserCannon = savedCharge; restoreBaseDispensers = savedDispensers;
	arcadeLifeBoost = savedLifeBoost; arcadeRandomBalls = savedRandomBalls;
	xmasMode = savedXmas; gameSpeed = savedSpeed;
	net_rollback = savedRollbackConfig; net_desync_recovery = savedRecoveryConfig;
	vt_ship = savedVt; smoothMotion = savedMotion; smoothScroll = savedScroll;
	nrb_set_session_mode(savedSessionMode);
	nrb_set_session_vt(savedSessionVt);
	nrb_set_session_recovery(savedSessionRecovery);
	coopSharedCredit = savedSharedCredit;
	coopDoublePickups = savedDoublePickups;
	coop_set_session_shared_credit(savedSharedCredit);
	coop_set_session_double_pickups(savedDoublePickups);
	coopCampaignMode = savedCoopCampaign;
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
	char savedSeed[NET_ENDLESS_SEED_MAX], savedHostSeed[NET_ENDLESS_SEED_MAX];
	memcpy(savedSeed, network_endless_session_seed, sizeof(savedSeed));
	memcpy(savedHostSeed, network_host_endless_seed, sizeof(savedHostSeed));

	Uint8 block[3 + NET_ENDLESS_SEED_MAX];
	memset(block, 0, sizeof(block));
	block[0] = (Uint8)ENDLESS_RUNMODE_HARDCORE;
	block[1] = (Uint8)(ENDLESS_PICK_COUNT - 1);
	block[2] = 1;
	memcpy(&block[3], "qa-seed-123", sizeof("qa-seed-123"));
	network_endless_adopt(block);
	qa_check(network_host_endless_run_mode == ENDLESS_RUNMODE_HARDCORE
	         && network_host_endless_chooser == ENDLESS_PICK_COUNT - 1
	         && network_host_endless_combo_shared
	         && strcmp(network_endless_session_seed, "qa-seed-123") == 0,
	         "joiner adopts every field of the host's Endless lobby block");

	memset(block, 0xEE, sizeof(block));
	network_endless_adopt(block);
	qa_check(network_host_endless_run_mode == ENDLESS_RUNMODE_STANDARD
	         && network_host_endless_chooser == ENDLESS_PICK_HOST,
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
	memcpy(network_endless_session_seed, savedSeed, sizeof(savedSeed));
	memcpy(network_host_endless_seed, savedHostSeed, sizeof(savedHostSeed));
#else
	qa_check(true, "Endless lobby block skipped without networking");
#endif
}

static void qa_test_resync_serialization(void)
{
#ifdef WITH_NETWORK
	Uint8 raw[4096], packed[8192], expanded[4096];
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
}

int qa_run_unit_suite(void)
{
	qa_checks = qa_failures = 0;
	printf("TAP version 13\n");

	/* Mirror normal episode setup before testing item, ship, weapon, and sidekick invariants. */
	JE_loadItemDat();
	JE_initPlayerData();
	qa_test_rollback();
	qa_test_course_tables();
	qa_test_structural_rng();
	qa_test_perk_registry();
	qa_test_record_readers();
	qa_test_weapon_editor();
	qa_test_custom_weapon_wire();
	qa_test_fixed_pool_layout();
	qa_test_save_record_wire();
	qa_test_cash_ledger();
	qa_test_bounty_matrix();
	qa_test_zone_payout();
	qa_test_arcade_scaling();
	qa_test_arcade_matrices();
	qa_test_sidekick_rollback_state();
	qa_test_modifier_online_parity();
	qa_test_effect_gates();
	qa_test_network_settings();
	qa_test_network_endless_lobby();
	qa_test_endless_coop();
	qa_test_kill_fire_drives();
	qa_test_kill_fire_wiring();
	qa_test_coop_combo_and_pickups();
	qa_test_peer_left_level();
	qa_test_online_suite();
	qa_test_endless_suite();
	qa_test_save_fixtures();
	qa_test_resync_serialization();
	qa_test_courses();

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
