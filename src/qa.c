/* Project-owned unit, property, serialization, and replay tests. */
#include "qa.h"

#include "config.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "endless.h"
#include "endless_internal.h"
#include "fonthand.h"
#include "mainint.h"
#include "mtrand.h"
#include "net_rollback.h"
#include "network.h"
#include "player.h"
#include "render_list.h"
#include "rollback.h"
#include "shots.h"
#include "tyrian2.h"
#include "varz.h"

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
bool qa_fast_forward = false;

static unsigned qa_checks;
static unsigned qa_failures;

static void qa_check(bool okay, const char *what)
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
	endlessPurchasedMods = 0;
	endlessCleanseChargeCount = 0;
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
	endlessReseed(0x5151);
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
	endlessReseed(0x5151);
	endlessGeneratePerkChoices(ENDLESS_PERK_OFFERS_MILESTONE);
	qa_check(memcmp(first, endlessPerkChoice, sizeof(first)) == 0,
	         "perk offer generation is deterministic on the structural RNG");
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
	int savedDiff[ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT];
	bool savedDiffCustom[ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT];
	int savedUntagged[ENDLESS_RUNMODE_COUNT];
	bool savedUntaggedCustom[ENDLESS_RUNMODE_COUNT];
	memcpy(savedDiff, endlessBestZoneDiff, sizeof(savedDiff));
	memcpy(savedDiffCustom, endlessBestZoneDiffCustom, sizeof(savedDiffCustom));
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		savedUntagged[m] = endlessBestZoneUntagged[m];
		savedUntaggedCustom[m] = endlessBestZoneUntaggedCustom[m];
	}
	memset(endlessBestZoneDiff, 0, sizeof(savedDiff));
	memset(endlessBestZoneDiffCustom, 0, sizeof(savedDiffCustom));
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		endlessBestZoneUntagged[m] = 0;
		endlessBestZoneUntaggedCustom[m] = false;
	}

	endlessBestZoneDiff[ENDLESS_RUNMODE_STANDARD][0] = 20;
	endlessBestZoneDiff[ENDLESS_RUNMODE_STANDARD][1] = 20;
	endlessBestZoneDiffCustom[ENDLESS_RUNMODE_STANDARD][1] = true;
	endlessBestZoneUntagged[ENDLESS_RUNMODE_STANDARD] = 15;
	endlessBestZoneUntaggedCustom[ENDLESS_RUNMODE_STANDARD] = true;
	qa_check(endlessBestZoneAny(ENDLESS_RUNMODE_STANDARD) == 20
	         && strcmp(endlessRecordAnyCustomMark(ENDLESS_RUNMODE_STANDARD), " C") == 0,
	         "deepest record derives its custom mark from any marked record tied at that depth");
	endlessBestZoneUntagged[ENDLESS_RUNMODE_STANDARD] = 25;
	endlessBestZoneUntaggedCustom[ENDLESS_RUNMODE_STANDARD] = false;
	qa_check(endlessBestZoneAny(ENDLESS_RUNMODE_STANDARD) == 25
	         && endlessRecordAnyCustomMark(ENDLESS_RUNMODE_STANDARD)[0] == '\0',
	         "legacy untagged record survives and owns the mode-wide mark when deepest");
	endlessBestZoneUntaggedCustom[ENDLESS_RUNMODE_STANDARD] = true;
	qa_check(strcmp(endlessRecordAnyCustomMark(ENDLESS_RUNMODE_STANDARD), " C") == 0,
	         "legacy untagged custom mark is retained");
	bool difficultyMap = true;
	for (int i = 0; i < ENDLESS_DIFFICULTY_COUNT; ++i)
		difficultyMap &= endlessDifficultySlot(endlessDifficultyLevel[i]) == i;
	qa_check(difficultyMap && endlessDifficultySlot(-999) == -1
	         && endlessBestZoneAny((EndlessRunMode)-1) == 0
	         && endlessBestZoneForDifficulty(ENDLESS_RUNMODE_STANDARD, -1) == 0,
	         "record readers preserve difficulty ordering and reject invalid indices");

	memcpy(endlessBestZoneDiff, savedDiff, sizeof(savedDiff));
	memcpy(endlessBestZoneDiffCustom, savedDiffCustom, sizeof(savedDiffCustom));
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		endlessBestZoneUntagged[m] = savedUntagged[m];
		endlessBestZoneUntaggedCustom[m] = savedUntaggedCustom[m];
	}
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
	qa_check(sizeof(PlayerItems) == 13 && SAVE_RECORD_PACKED_SIZE == 77
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
	src.highScore1 = 0x11111111; src.highScore2 = 0x22222222;
	strcpy(src.highScoreName, "NOT SENT"); src.highScoreDiff = 9;
	src.autoFireSpecial = true; src.chargeSidekickAutofire = 2;
	src.difficultyAdjust = true; src.cheatInfiniteSidekickAmmo = true;
	src.cheatInfiniteShields = false; src.cheatInfiniteArmor = true; src.expertMode = true;

	memset(guarded, 0xa5, sizeof(guarded));
	save_record_pack(packed, &src);
	qa_check(guarded[0] == 0xa5 && guarded[sizeof(guarded) - 1] == 0xa5,
	         "save-record packing writes exactly its fixed 77-byte frame");
	save_record_unpack(&dst, packed);
	save_record_pack(repacked, &dst);
	qa_check(memcmp(packed, repacked, sizeof(repacked)) == 0,
	         "network save record pack/unpack round-trips every serialized field");
	qa_check(dst.encode == 0 && dst.highScore1 == 0 && dst.highScore2 == 0
	         && dst.highScoreName[0] == '\0' && dst.highScoreDiff == 0,
	         "network save record leaves non-wire metadata cleared");
	qa_check(dst.gameHasRepeated && dst.autoFireSpecial && dst.difficultyAdjust
	         && dst.cheatInfiniteSidekickAmmo && !dst.cheatInfiniteShields
	         && dst.cheatInfiniteArmor && dst.expertMode,
	         "network save record preserves all boolean gameplay flags");

	/* Hostile fixed-width strings still have to become safe C strings on receipt. */
	memset(packed + 34, 'L', 11);
	memset(packed + 45, 'N', 15);
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
	qa_check(network_settings_adopt(packet) == NETWORK_SETTINGS_SIZE,
	         "network settings decoder consumes its exact fixed block");
	bool arraysMatch = true;
	for (int i = 0; i < SSW_COUNT; ++i) arraysMatch &= superSparkMode[i] == i % SUPER_SPARKS_COUNT;
	for (int i = 0; i < EDW_COUNT; ++i) arraysMatch &= epDiffMode[i] == i % EPDIFF_MODE_COUNT;
	qa_check(arraysMatch && zicaLaserBase == ZICA_BASE_EP4 && zicaLaserLength == ZICA_LEN_LONG
	         && zicaLaserLock && !zicaLaserBuff && wallopSecondBolt == SUPER_SPARKS_ON
	         && chargeLaserCannon && !restoreBaseDispensers && arcadeLifeBoost
	         && !arcadeRandomBalls && xmasMode == 1 && gameSpeed == 2
	         && nrb_session_mode() && nrb_session_vt() && nrb_session_recovery(),
	         "joiner adopts every host-authoritative simulation setting");
	network_settings_restore();
	arraysMatch = true;
	for (int i = 0; i < SSW_COUNT; ++i) arraysMatch &= superSparkMode[i] == SUPER_SPARKS_OFF;
	for (int i = 0; i < EDW_COUNT; ++i) arraysMatch &= epDiffMode[i] == EPDIFF_EP13;
	qa_check(arraysMatch && zicaLaserBase == ZICA_BASE_AUTO && zicaLaserLength == ZICA_LEN_SHORT
	         && !zicaLaserLock && zicaLaserBuff && wallopSecondBolt == SUPER_SPARKS_OFF
	         && !chargeLaserCannon && restoreBaseDispensers && !arcadeLifeBoost
	         && arcadeRandomBalls && xmasMode == 0 && gameSpeed == 5,
	         "leaving a network session restores every local simulation preference");

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
#else
	qa_check(true, "network settings round trip skipped without networking");
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
	qa_test_fixed_pool_layout();
	qa_test_save_record_wire();
	qa_test_cash_ledger();
	qa_test_arcade_scaling();
	qa_test_effect_gates();
	qa_test_network_settings();
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
