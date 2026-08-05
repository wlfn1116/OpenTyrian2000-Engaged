/* Project-owned unit, property, serialization, and replay tests. */
#include "qa.h"

#include "config.h"
#include "crashlog.h"
#include "custom_weapon.h"
#include "endless.h"
#include "endless_internal.h"
#include "mainint.h"
#include "net_rollback.h"
#include "network.h"
#include "rollback.h"
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
			++routes;
		}

		qa_reset_course_inputs(seed, depth, diff);
		endlessGenerateCourses();
		qa_check(endlessCourseCnt == first_count && qa_slate_hash() == first_hash,
		         "seeded course generation is deterministic");
	}

	printf("# course properties: 768 seeds, %u launchable routes\n", routes);
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

	/* Materialize every fuzzed level so sanitizer runs cover engine-array writes too. */
	customWeaponMaterialize();
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

	JE_initPlayerData();
	qa_test_rollback();
	qa_test_weapon_editor();
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
