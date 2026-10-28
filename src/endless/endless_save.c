/* Endless saves and Quit Level sortie snapshots. */

#define ENDLESS_INTERNAL_ACCESS

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "episodes.h"
#include "game_menu.h"
#include "game_state.h"
#include "level_load.h"
#include "player.h"
#include "savegame.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Launch-time state used by Quit Level.
bool endlessQuitToOutpost = false;
bool endlessLockedSortie  = false;
bool     endlessSortieHave  = false;
static Player   endlessSortiePlayer[2];
static Uint64 endlessSortieModsV = 0;
static Uint64 endlessSortiePlayerModsV[2] = { 0, 0 };
static JE_byte  endlessSortieSec   = 0;
static int      endlessSortieEp    = 0;
static JE_byte  endlessSortieFile  = 0;
// One-shot purchases are captured before course selection consumes them.
unsigned endlessSortiePrePurchased[2] = { 0, 0 };
int      endlessSortiePreCleanse[2]   = { 0, 0 };
int      endlessSortiePreLongCon[2]   = { 0, 0 };
// Mutators captured when the outpost opens. An unlocked bail must restore this previous-sector set.
Uint64   endlessSortieOutpostMods = 0;
// Episode captured with them. Shop stock is item ids, and each episode loads its own item tables,
// so a bail must restore this before the outpost redraws. See doc/notes.md#saves-and-retries.
JE_byte  endlessSortieOutpostEp = 0;

/* Every save slot's Endless half lives in opentyrian.sav as a `section 'endless' 'N'`, written
 * beside the campaign record so the pair cannot drift. See doc/notes.md#saves-and-retries. */

#define ENDLESS_SAVE_OFFERS     ENDLESS_PERK_OFFERS_MILESTONE

/* A larger co-op outpost block requires a NET_VERSION bump. */
COMPILE_TIME_ASSERT(endless_block_perks_fit, PERK_COUNT <= ENDLESS_PLAYER_BLOCK_PERKS);

/* Each player's own half. */
typedef struct {
	Sint32 armorBonus, revivesUsed, shopTax, longCon, buffKind, buffCharge;
	Sint32 buffCooldownUntil;  // run depth at which the E-Shop kill-fire buys unlock again (0 = no lock)
	Sint32 superbombs, cleanseCharges, extraPerksBought, extraPerksVisit;
	Sint64 rerollCost, hullCost, bombCost, cleanseCost, shopEntryCash;
	Uint32 purchasedMods;
	Uint8  reviveHeld, gambleRigged, downed, gamblePerkWon;
	Uint8  perks[PERK_COUNT];  // perks are personal, so this is each ship's own holding
	Uint64 rng;                // each player's own outpost draw stream
	char   gambleMsg[48];
	char   lastSpecialName[31];
} EndlessRecPlayer;

typedef struct {
	bool used;

	// Run state.
	Sint32 runDepth, runKills, runBossKills, perkDepthDone;
	Uint8  perkPending;

	// Perk offer.
	Sint32 perkChoiceN;
	Sint32 perkChoice[ENDLESS_SAVE_OFFERS];

	// Course offer.
	Sint32 courseCnt;
	Sint32 courseEp[ENDLESS_MAX_COURSES];
	Uint8  courseSec[ENDLESS_MAX_COURSES];
	Uint8  courseFile[ENDLESS_MAX_COURSES];
	Uint64 courseMod[ENDLESS_MAX_COURSES];
	Sint32 lastEp;
	Uint8  lastSec, forced;

	// Shop stock.
	Uint8  itemAvail[9][10];
	Uint8  itemAvailMax[9];

	char   seed[ENDLESS_SEED_MAXLEN];

	// Locked sortie.
	Uint8  lockedSortie;  // 1 = this save reopens the locked retry outpost (else a normal outpost)
	Uint64 sortieMods;    // endlessActiveMods of the committed level
	Uint8  sortieSec;     // committed level section
	Sint32 sortieEp;      // committed episode
	Uint8  sortieFile;    // committed lvl file number

	// Recent levels.
	Uint8  recentCount;
	Sint32 recentEp[ENDLESS_LEVEL_HISTORY];
	Uint8  recentSec[ENDLESS_LEVEL_HISTORY];

	Uint8  creditsShown;  // 1 = this run has already rolled the credits, so resuming won't replay them

	// Music continuity.
	Uint8  lastSong;       // the track the last-played zone really used (0 = none yet)
	Sint32 lastSongDepth;  // that zone's run depth (only meaningful when lastSong != 0)

	// Deferred boons.
	Uint8  starChartsOwed;    // STAR CHARTS: the next ordinary chart still owes its full route slate
	Uint8  breakthroughOwed;  // bonus perk picks still owed; two can queue

	Uint8  runMode;  // EndlessRunMode: the run's Relaxed/Standard/Hardcore choice (never Hardcore on disk)

	Uint64 cashEarned;  // running total of cash taken in, for the run-over tally
	Uint64 cashSpent;   // running total spent
	Uint64 cashBySource[ENDLESS_CASH_SOURCES];  // the earnings breakdown, indexed by EndlessCashSource
	Uint64 cashBySink[ENDLESS_CASH_SINKS];      // the spending breakdown, indexed by EndlessCashSink

	Uint8  usedCustom;  // 1 = the run has cleared a zone firing the custom weapon

	// Slot 0 is the run's only player outside co-op.
	Uint8  coopHostCharts;   // Alternating course picks: is the host charting the next one?
	Uint8  courseChooser;    // EndlessCourseChooser the run was started under
	EndlessRecPlayer player[2];

	// Fixed for the run.
	Uint8  baseLevelRule;

	Uint8  chartRerolls;     // Radar rerolls this outpost's chart has had; salts the visit's phases
	Uint8  chartStarCharts;  // Star Charts as the visit's chart found it, so a redeal replays it

	// Both are 0 under the two unshuffled rules.
	Uint32 shuffleNext;       // pieces a Shuffle run has drawn
	Uint32 shuffleHandStart;  // where the restored chart's hand came off, for the co-op re-anchor

	/* The partner's half of the outpost this save checkpointed, as their machine reported it over
	 * the save acknowledgement. Valid 0 means the checkpoint had no answer. */
	Uint8  partnerValid;
	Uint8  partnerSeat;              // player index the half belongs to
	Uint8  partnerAvailMax[9];
	Uint8  partnerAvail[9][10];
	Uint64 partnerRng;
} EndlessSlotRec;

// One record per save slot, read with and written beside saveFiles[] (savegame.c owns the file).
static EndlessSlotRec endlessSlotCache[SAVE_FILES_NUM];

// Restore a chart. Drop invalid entries; regenerate deterministically if none remain.
static void endlessRestoreSavedCourses(const EndlessSlotRec *r)
{
	int savedCount = r->courseCnt;
	if (savedCount < 0) savedCount = 0;
	if (savedCount > ENDLESS_MAX_COURSES) savedCount = ENDLESS_MAX_COURSES;

	memset(endlessCourseEp, 0, sizeof(endlessCourseEp));
	memset(endlessCourseSec, 0, sizeof(endlessCourseSec));
	memset(endlessCourseFile, 0, sizeof(endlessCourseFile));
	memset(endlessCourseMod, 0, sizeof(endlessCourseMod));

	int restoredCount = 0;
	bool dropped = false;
	for (int i = 0; i < savedCount; ++i)
	{
		if (!endlessCourseFileValid(r->courseEp[i], r->courseSec[i], r->courseFile[i]))
		{
			fprintf(stderr, "warning: dropping invalid saved course episode %d section %u\n",
			        r->courseEp[i], (unsigned int)r->courseSec[i]);
			dropped = true;
			continue;
		}
		endlessCourseEp[restoredCount] = r->courseEp[i];
		endlessCourseSec[restoredCount] = r->courseSec[i];
		endlessCourseFile[restoredCount] = r->courseFile[i];
		endlessCourseMod[restoredCount] = r->courseMod[i];
		++restoredCount;
	}
	endlessCourseCnt = restoredCount;

	// Rebuild an invalid forced visit rather than turning another saved option into an Ambush.
	// The redeal uses the restored reroll count, so it rebuilds the chart the save was on.
	if (endlessCourseCnt == 0 || (endlessForced && dropped))
		endlessChartRedeal();

	endlessNameCourseBaseLevels();  // populate the Radar perk's base-level cache for the restored chart
}

/* Text codec: one config section per record, every field under a name. */

// A player-prefixed key: p1_armor_bonus, p2_armor_bonus.
static const char *endlessPlayerKey(char *buf, size_t n, uint p, const char *key)
{
	snprintf(buf, n, "p%u_%s", p + 1, key);
	return buf;
}

static void endlessPutInt(ConfigSection *s, const char *key, Sint64 v)
{
	config_set_int64_option(s, key, (long long)v);
}

static void endlessPutHex(ConfigSection *s, const char *key, Uint64 v)
{
	char buf[24];
	snprintf(buf, sizeof(buf), "%016" PRIX64, v);
	config_set_string_option(s, key, buf);
}

// Space-separated numbers. Unsigned values print unsigned so a 64-bit mask survives the round trip.
static void endlessPutList(ConfigSection *s, const char *key, const void *vals, size_t count,
                           size_t width, bool isSigned)
{
	const size_t cap = count * 24 + 1;   // 20 digits, a sign and a space per number, at most
	char *buf = malloc(cap);
	if (buf == NULL)
		return;
	size_t n = 0;
	for (size_t i = 0; i < count; ++i)
	{
		const Uint8 *at = (const Uint8 *)vals + i * width;
		long long v = 0;
		switch (width)
		{
		case 1: v = isSigned ? *(const Sint8 *)at : *(const Uint8 *)at; break;
		case 4: v = isSigned ? *(const Sint32 *)at : (long long)*(const Uint32 *)at; break;
		default: v = *(const long long *)at; break;
		}
		char num[32];
		const int len = (isSigned || width < 8)
		              ? snprintf(num, sizeof(num), "%s%lld", i ? " " : "", v)
		              : snprintf(num, sizeof(num), "%s%llu", i ? " " : "", (unsigned long long)v);
		if (len < 0 || n + (size_t)len >= cap)
			break;
		memcpy(buf + n, num, (size_t)len);
		n += (size_t)len;
	}
	buf[n] = '\0';
	config_set_string_option(s, key, buf);
	free(buf);
}

// Read up to `count` numbers into vals; the rest are left as they were. Returns how many parsed.
static size_t endlessGetList(const ConfigSection *s, const char *key, void *vals, size_t count,
                             size_t width, bool isSigned)
{
	const char *text = NULL;
	if (!config_get_string_option(s, key, &text) || text == NULL)
		return 0;
	size_t got = 0;
	while (got < count)
	{
		while (*text == ' ')
			++text;
		if (*text == '\0')
			break;
		char *end = NULL;
		const long long v = isSigned ? strtoll(text, &end, 10) : (long long)strtoull(text, &end, 10);
		if (end == text)
			break;
		text = end;
		Uint8 *at = (Uint8 *)vals + got * width;
		switch (width)
		{
		case 1: *(Uint8 *)at = (Uint8)v; break;
		case 4: *(Uint32 *)at = (Uint32)v; break;
		default: *(long long *)at = v; break;
		}
		++got;
	}
	return got;
}

static Sint64 endlessGetInt(const ConfigSection *s, const char *key, Sint64 fallback)
{
	long long v;
	return config_get_int64_option(s, key, &v) ? (Sint64)v : fallback;
}

static Uint64 endlessGetHex(const ConfigSection *s, const char *key)
{
	const char *text = NULL;
	return (config_get_string_option(s, key, &text) && text != NULL) ? (Uint64)strtoull(text, NULL, 16) : 0;
}

static void endlessGetStr(const ConfigSection *s, const char *key, char *dst, size_t n)
{
	const char *text = NULL;
	if (config_get_string_option(s, key, &text) && text != NULL)
		SDL_strlcpy(dst, text, n);
	else
		dst[0] = '\0';
}

// The 9 shop rows: one key per row holding that row's item ids, so the count is the list length.
static void endlessPutStock(ConfigSection *s, const char *prefix, const Uint8 avail[9][10],
                            const Uint8 availMax[9])
{
	for (int row = 0; row < 9; ++row)
	{
		char key[32];
		snprintf(key, sizeof(key), "%s_%d", prefix, row + 1);
		const size_t n = (availMax[row] > 10) ? 10 : availMax[row];
		endlessPutList(s, key, avail[row], n, 1, false);
	}
}

static void endlessGetStock(const ConfigSection *s, const char *prefix, Uint8 avail[9][10],
                            Uint8 availMax[9])
{
	for (int row = 0; row < 9; ++row)
	{
		char key[32];
		snprintf(key, sizeof(key), "%s_%d", prefix, row + 1);
		memset(avail[row], 0, 10);
		availMax[row] = (Uint8)endlessGetList(s, key, avail[row], 10, 1, false);
	}
}

static void endlessRecToSection(ConfigSection *s, const EndlessSlotRec *r)
{
	// Run.
	endlessPutInt(s, "run_depth", r->runDepth);
	endlessPutInt(s, "run_kills", r->runKills);
	endlessPutInt(s, "run_boss_kills", r->runBossKills);
	endlessPutInt(s, "perk_depth_done", r->perkDepthDone);
	config_set_string_option(s, "seed", r->seed);
	endlessPutInt(s, "run_mode", r->runMode);
	endlessPutInt(s, "base_level_rule", r->baseLevelRule);
	endlessPutInt(s, "credits_shown", r->creditsShown);
	endlessPutInt(s, "used_custom", r->usedCustom);
	endlessPutInt(s, "last_song", r->lastSong);
	endlessPutInt(s, "last_song_depth", r->lastSongDepth);
	endlessPutInt(s, "star_charts_owed", r->starChartsOwed);
	endlessPutInt(s, "breakthrough_owed", r->breakthroughOwed);
	endlessPutInt(s, "cash_earned", (Sint64)r->cashEarned);
	endlessPutInt(s, "cash_spent", (Sint64)r->cashSpent);
	endlessPutList(s, "cash_by_source", r->cashBySource, ENDLESS_CASH_SOURCES, 8, false);
	endlessPutList(s, "cash_by_sink", r->cashBySink, ENDLESS_CASH_SINKS, 8, false);

	// The outpost this save reopens: its chart, perk offer, shop rows and recent-level ring.
	endlessPutInt(s, "course_count", r->courseCnt);
	endlessPutList(s, "course_episode", r->courseEp, ENDLESS_MAX_COURSES, 4, true);
	endlessPutList(s, "course_section", r->courseSec, ENDLESS_MAX_COURSES, 1, false);
	endlessPutList(s, "course_file", r->courseFile, ENDLESS_MAX_COURSES, 1, false);
	for (int i = 0; i < ENDLESS_MAX_COURSES; ++i)
	{
		char key[32];
		snprintf(key, sizeof(key), "course_mods_%d", i + 1);
		endlessPutHex(s, key, r->courseMod[i]);
	}
	endlessPutInt(s, "last_episode", r->lastEp);
	endlessPutInt(s, "last_section", r->lastSec);
	endlessPutInt(s, "forced", r->forced);
	endlessPutInt(s, "chart_rerolls", r->chartRerolls);
	endlessPutInt(s, "chart_star_charts", r->chartStarCharts);
	endlessPutInt(s, "shuffle_next", r->shuffleNext);
	endlessPutInt(s, "shuffle_hand_start", r->shuffleHandStart);
	endlessPutInt(s, "recent_count", r->recentCount);
	endlessPutList(s, "recent_episode", r->recentEp, ENDLESS_LEVEL_HISTORY, 4, true);
	endlessPutList(s, "recent_section", r->recentSec, ENDLESS_LEVEL_HISTORY, 1, false);
	endlessPutInt(s, "perk_choice_count", r->perkChoiceN);
	endlessPutList(s, "perk_choice", r->perkChoice, ENDLESS_SAVE_OFFERS, 4, true);
	endlessPutInt(s, "perk_pending", r->perkPending);
	endlessPutStock(s, "stock", r->itemAvail, r->itemAvailMax);

	// The locked retry outpost, when the save reopens one.
	endlessPutInt(s, "sortie_locked", r->lockedSortie);
	if (r->lockedSortie)
	{
		endlessPutHex(s, "sortie_mods", r->sortieMods);
		endlessPutInt(s, "sortie_section", r->sortieSec);
		endlessPutInt(s, "sortie_episode", r->sortieEp);
		endlessPutInt(s, "sortie_file", r->sortieFile);
	}

	// Co-op run-wide settings, then each ship's own half.
	endlessPutInt(s, "coop_host_charts", r->coopHostCharts);
	endlessPutInt(s, "course_chooser", r->courseChooser);
	for (uint p = 0; p < 2; ++p)
	{
		const EndlessRecPlayer *v = &r->player[p];
		char k[48];
		#define PK(name) endlessPlayerKey(k, sizeof(k), p, name)
		endlessPutInt(s, PK("armor_bonus"), v->armorBonus);
		endlessPutInt(s, PK("revives_used"), v->revivesUsed);
		endlessPutInt(s, PK("shop_tax"), v->shopTax);
		endlessPutInt(s, PK("long_con"), v->longCon);
		endlessPutInt(s, PK("buff_kind"), v->buffKind);
		endlessPutInt(s, PK("buff_charge"), v->buffCharge);
		endlessPutInt(s, PK("buff_cooldown_until"), v->buffCooldownUntil);
		endlessPutInt(s, PK("superbombs"), v->superbombs);
		endlessPutInt(s, PK("cleanse_charges"), v->cleanseCharges);
		endlessPutInt(s, PK("reroll_cost"), v->rerollCost);
		endlessPutInt(s, PK("hull_cost"), v->hullCost);
		endlessPutInt(s, PK("bomb_cost"), v->bombCost);
		endlessPutInt(s, PK("extra_perks_bought"), v->extraPerksBought);
		endlessPutInt(s, PK("extra_perks_visit"), v->extraPerksVisit);
		endlessPutInt(s, PK("cleanse_cost"), v->cleanseCost);
		endlessPutInt(s, PK("shop_entry_cash"), v->shopEntryCash);
		endlessPutHex(s, PK("purchased_mods"), v->purchasedMods);
		endlessPutInt(s, PK("revive_held"), v->reviveHeld);
		endlessPutInt(s, PK("gamble_rigged"), v->gambleRigged);
		endlessPutInt(s, PK("downed"), v->downed);
		endlessPutInt(s, PK("gamble_perk_won"), v->gamblePerkWon);
		endlessPutList(s, PK("perks"), v->perks, PERK_COUNT, 1, false);
		endlessPutList(s, PK("rng"), &v->rng, 1, 8, false);
		config_set_string_option(s, PK("gamble_message"), v->gambleMsg);
		config_set_string_option(s, PK("last_special"), v->lastSpecialName);
		#undef PK
	}

	// The partner's half of the outpost, when a co-op save checkpoint captured one.
	if (r->partnerValid)
	{
		endlessPutInt(s, "partner_seat", r->partnerSeat + 1);
		endlessPutStock(s, "partner_stock", r->partnerAvail, r->partnerAvailMax);
		endlessPutList(s, "partner_rng", &r->partnerRng, 1, 8, false);
	}
}

static void endlessRecFromSection(EndlessSlotRec *r, const ConfigSection *s)
{
	memset(r, 0, sizeof(*r));
	r->used = true;

	r->runDepth = (Sint32)endlessGetInt(s, "run_depth", 0);
	r->runKills = (Sint32)endlessGetInt(s, "run_kills", 0);
	r->runBossKills = (Sint32)endlessGetInt(s, "run_boss_kills", 0);
	r->perkDepthDone = (Sint32)endlessGetInt(s, "perk_depth_done", 0);
	endlessGetStr(s, "seed", r->seed, sizeof(r->seed));
	r->runMode = (Uint8)endlessGetInt(s, "run_mode", 0);
	r->baseLevelRule = (Uint8)endlessGetInt(s, "base_level_rule", 0);
	r->creditsShown = endlessGetInt(s, "credits_shown", 0) != 0;
	r->usedCustom = endlessGetInt(s, "used_custom", 0) != 0;
	r->lastSong = (Uint8)endlessGetInt(s, "last_song", 0);
	r->lastSongDepth = (Sint32)endlessGetInt(s, "last_song_depth", 0);
	r->starChartsOwed = endlessGetInt(s, "star_charts_owed", 0) != 0;
	r->breakthroughOwed = (Uint8)endlessGetInt(s, "breakthrough_owed", 0);
	r->cashEarned = (Uint64)endlessGetInt(s, "cash_earned", 0);
	r->cashSpent = (Uint64)endlessGetInt(s, "cash_spent", 0);
	endlessGetList(s, "cash_by_source", r->cashBySource, ENDLESS_CASH_SOURCES, 8, false);
	endlessGetList(s, "cash_by_sink", r->cashBySink, ENDLESS_CASH_SINKS, 8, false);

	r->courseCnt = (Sint32)endlessGetInt(s, "course_count", 0);
	endlessGetList(s, "course_episode", r->courseEp, ENDLESS_MAX_COURSES, 4, true);
	endlessGetList(s, "course_section", r->courseSec, ENDLESS_MAX_COURSES, 1, false);
	endlessGetList(s, "course_file", r->courseFile, ENDLESS_MAX_COURSES, 1, false);
	for (int i = 0; i < ENDLESS_MAX_COURSES; ++i)
	{
		char key[32];
		snprintf(key, sizeof(key), "course_mods_%d", i + 1);
		r->courseMod[i] = endlessGetHex(s, key);
	}
	r->lastEp = (Sint32)endlessGetInt(s, "last_episode", 0);
	r->lastSec = (Uint8)endlessGetInt(s, "last_section", 0);
	r->forced = endlessGetInt(s, "forced", 0) != 0;
	r->chartRerolls = (Uint8)endlessGetInt(s, "chart_rerolls", 0);
	r->chartStarCharts = endlessGetInt(s, "chart_star_charts", 0) != 0;
	r->shuffleNext = (Uint32)endlessGetInt(s, "shuffle_next", 0);
	r->shuffleHandStart = (Uint32)endlessGetInt(s, "shuffle_hand_start", 0);
	r->recentCount = (Uint8)endlessGetInt(s, "recent_count", 0);
	endlessGetList(s, "recent_episode", r->recentEp, ENDLESS_LEVEL_HISTORY, 4, true);
	endlessGetList(s, "recent_section", r->recentSec, ENDLESS_LEVEL_HISTORY, 1, false);
	r->perkChoiceN = (Sint32)endlessGetInt(s, "perk_choice_count", 0);
	endlessGetList(s, "perk_choice", r->perkChoice, ENDLESS_SAVE_OFFERS, 4, true);
	r->perkPending = endlessGetInt(s, "perk_pending", 0) != 0;
	endlessGetStock(s, "stock", r->itemAvail, r->itemAvailMax);

	r->lockedSortie = endlessGetInt(s, "sortie_locked", 0) != 0;
	r->sortieMods = endlessGetHex(s, "sortie_mods");
	r->sortieSec = (Uint8)endlessGetInt(s, "sortie_section", 0);
	r->sortieEp = (Sint32)endlessGetInt(s, "sortie_episode", 0);
	r->sortieFile = (Uint8)endlessGetInt(s, "sortie_file", 0);

	r->coopHostCharts = endlessGetInt(s, "coop_host_charts", 1) != 0;
	r->courseChooser = (Uint8)endlessGetInt(s, "course_chooser", 0);
	for (uint p = 0; p < 2; ++p)
	{
		EndlessRecPlayer *v = &r->player[p];
		char k[48];
		#define PK(name) endlessPlayerKey(k, sizeof(k), p, name)
		v->armorBonus = (Sint32)endlessGetInt(s, PK("armor_bonus"), 0);
		v->revivesUsed = (Sint32)endlessGetInt(s, PK("revives_used"), 0);
		v->shopTax = (Sint32)endlessGetInt(s, PK("shop_tax"), 0);
		v->longCon = (Sint32)endlessGetInt(s, PK("long_con"), 0);
		v->buffKind = (Sint32)endlessGetInt(s, PK("buff_kind"), 0);
		v->buffCharge = (Sint32)endlessGetInt(s, PK("buff_charge"), 0);
		v->buffCooldownUntil = (Sint32)endlessGetInt(s, PK("buff_cooldown_until"), 0);
		v->superbombs = (Sint32)endlessGetInt(s, PK("superbombs"), 0);
		v->cleanseCharges = (Sint32)endlessGetInt(s, PK("cleanse_charges"), 0);
		// Prices and the entry wallet are wallet-sized: a hand edit past either end is clamped.
		v->rerollCost = cash_clamp(endlessGetInt(s, PK("reroll_cost"), 0));
		v->hullCost = cash_clamp(endlessGetInt(s, PK("hull_cost"), 0));
		v->bombCost = cash_clamp(endlessGetInt(s, PK("bomb_cost"), 0));
		v->extraPerksBought = (Sint32)endlessGetInt(s, PK("extra_perks_bought"), 0);
		v->extraPerksVisit = (Sint32)endlessGetInt(s, PK("extra_perks_visit"), 0);
		v->cleanseCost = cash_clamp(endlessGetInt(s, PK("cleanse_cost"), 0));
		v->shopEntryCash = cash_clamp(endlessGetInt(s, PK("shop_entry_cash"), 0));
		v->purchasedMods = (Uint32)endlessGetHex(s, PK("purchased_mods"));
		v->reviveHeld = endlessGetInt(s, PK("revive_held"), 0) != 0;
		v->gambleRigged = endlessGetInt(s, PK("gamble_rigged"), 0) != 0;
		v->downed = endlessGetInt(s, PK("downed"), 0) != 0;
		v->gamblePerkWon = endlessGetInt(s, PK("gamble_perk_won"), 0) != 0;
		endlessGetList(s, PK("perks"), v->perks, PERK_COUNT, 1, false);
		endlessGetList(s, PK("rng"), &v->rng, 1, 8, false);
		endlessGetStr(s, PK("gamble_message"), v->gambleMsg, sizeof(v->gambleMsg));
		endlessGetStr(s, PK("last_special"), v->lastSpecialName, sizeof(v->lastSpecialName));
		#undef PK
	}

	// Enum and cursor values index tables; a hand edit past their range falls back. Counts and
	// stacks are clamped where the record is applied.
	if (r->courseChooser >= ENDLESS_PICK_COUNT)
		r->courseChooser = ENDLESS_PICK_HOST;
	if (r->baseLevelRule >= ENDLESS_BASE_RULE_COUNT)
		r->baseLevelRule = ENDLESS_BASE_VARIED;
	if (r->shuffleNext > ENDLESS_SHUFFLE_POSITION_MAX)
		r->shuffleNext = 0;
	if (r->shuffleHandStart > r->shuffleNext)
		r->shuffleHandStart = r->shuffleNext;

	const Sint64 partnerSeat = endlessGetInt(s, "partner_seat", 0);
	if (partnerSeat == 1 || partnerSeat == 2)
	{
		r->partnerValid = 1;
		r->partnerSeat = (Uint8)(partnerSeat - 1);
		endlessGetStock(s, "partner_stock", r->partnerAvail, r->partnerAvailMax);
		endlessGetList(s, "partner_rng", &r->partnerRng, 1, 8, false);
	}
}

/* Text encode/decode of a lone record, for the wire and the tests: a one-section config, its text
 * in a malloc'd buffer. */
static bool endlessTextEncode(EndlessSlotRec *rec, char **text, size_t *size)
{
	Config config;
	config_init(&config);
	ConfigSection *section = config_add_section(&config, "endless", "run");
	if (section == NULL)
	{
		config_deinit(&config);
		return false;
	}
	endlessRecToSection(section, rec);

	const size_t need = config_write_buffer(&config, NULL, 0);
	char *out = malloc(need + 1);
	if (out == NULL)
	{
		config_deinit(&config);
		return false;
	}
	config_write_buffer(&config, out, need);
	out[need] = '\0';
	config_deinit(&config);

	*text = out;
	*size = need;
	return true;
}

static bool endlessTextDecode(const char *text, size_t size, EndlessSlotRec *rec)
{
	if (text == NULL)
		return false;
	Config config;
	if (!config_parse_buffer(&config, text, size))
		return false;
	const ConfigSection *section = config_find_section(&config, "endless", "run");
	const bool okay = section != NULL;
	if (okay)
		endlessRecFromSection(rec, section);
	config_deinit(&config);
	return okay;
}

static void endlessTestDetail(char *detail, size_t detailSize, const char *message)
{
	if (detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", message);
}

/* Exercise round trips, unknown keys, defaults, and wallet clamping. */
bool endlessSaveTestTextCodec(char *detail, size_t detailSize)
{
	if (detail != NULL && detailSize != 0)
		detail[0] = '\0';

	EndlessSlotRec rec;
	memset(&rec, 0, sizeof(rec));
	rec.used = true;
	rec.runDepth = 91;
	rec.courseCnt = 2;
	rec.courseEp[0] = 3;
	rec.courseEp[1] = 5;
	rec.courseSec[1] = 7;
	rec.courseMod[1] = 0x8000000000000001ull;
	rec.shuffleNext = 12;
	rec.player[0].shopEntryCash = 4000000000LL;
	rec.player[1].rerollCost = 123456789012LL;
	rec.cashEarned = 5000000000ull;
	rec.cashBySink[ENDLESS_SINK_GEAR] = 77;
	rec.player[1].perks[PERK_SPECIALCD] = 2;
	rec.player[0].rng = 0xF00DF00DF00DF00Dull;
	rec.itemAvailMax[3] = 2;
	rec.itemAvail[3][0] = 40;
	rec.itemAvail[3][1] = 41;
	rec.partnerValid = 1;
	rec.partnerSeat = 1;
	rec.partnerAvailMax[0] = 1;
	rec.partnerAvail[0][0] = 9;
	rec.partnerRng = 0x1122334455667788ull;
	rec.lockedSortie = 1;
	rec.sortieMods = 0x4000000000000002ull;
	rec.sortieEp = 2;
	SDL_strlcpy(rec.seed, "text-codec", sizeof(rec.seed));
	SDL_strlcpy(rec.player[1].gambleMsg, "it's 'quoted'", sizeof(rec.player[1].gambleMsg));

	char *text = NULL;
	size_t size = 0;
	EndlessSlotRec back;
	const char *fault = NULL;

	if (!endlessTextEncode(&rec, &text, &size))
		fault = "encode failed";
	else if (!endlessTextDecode(text, size, &back))
		fault = "decode failed";
	else if (memcmp(&rec, &back, sizeof(rec)) != 0)
		fault = "a field did not survive the round trip";
	free(text);
	text = NULL;

	if (fault == NULL)
	{
		Config config;
		config_init(&config);
		ConfigSection *section = config_add_section(&config, "endless", "run");
		endlessRecToSection(section, &rec);
		config_set_string_option(section, "not_a_field", "ignored");
		config_remove_option(section, "run_kills");
		config_set_string_option(section, "p1_shop_entry_cash", "-5");
		config_set_string_option(section, "p2_reroll_cost", "junk");
		endlessRecFromSection(&back, section);
		config_deinit(&config);
		if (back.runDepth != 91 || back.runKills != 0 || back.player[0].shopEntryCash != 0
		    || back.player[1].rerollCost != 0)
			fault = "an unknown, missing or malformed key was not defaulted";
	}

	Uint32 rng = 0x45534f54u;
	for (unsigned pass = 0; pass < 128; ++pass)
	{
		rng = rng * 1664525u + 1013904223u;
		const size_t n = rng % 1024;
		Uint8 fuzz[1024];
		for (size_t i = 0; i < n; ++i)
		{
			rng = rng * 1664525u + 1013904223u;
			fuzz[i] = (Uint8)(rng >> 24);
		}
		EndlessSlotRec junk;
		(void)endlessTextDecode((const char *)fuzz, n, &junk);
	}

	if (fault != NULL)
		endlessTestDetail(detail, detailSize, fault);
	return fault == NULL;
}

/* opentyrian.sav carries one 'endless' section per slot with a run; savegame.c parses and writes
 * the file and hands the Config here for the Endless half. A slot without a section holds no
 * run. */
void endlessSaveConfigRead(Config *config)
{
	memset(endlessSlotCache, 0, sizeof(endlessSlotCache));
	if (config == NULL)
		return;
	for (int s = 0; s < SAVE_FILES_NUM; ++s)
	{
		char name[8];
		snprintf(name, sizeof(name), "%d", s + 1);
		const ConfigSection *section = config_find_section(config, "endless", name);
		if (section != NULL && saveFiles[s].level != 0)   // read after the campaign slots
			endlessRecFromSection(&endlessSlotCache[s], section);
	}
}

void endlessSaveConfigWrite(Config *config)
{
	for (int s = 0; s < SAVE_FILES_NUM; ++s)
	{
		// A run belongs to the campaign slot it was saved with; an empty slot carries none.
		if (!endlessSlotCache[s].used || saveFiles[s].level == 0)
			continue;
		char name[8];
		snprintf(name, sizeof(name), "%d", s + 1);
		ConfigSection *section = config_add_section(config, "endless", name);
		if (section == NULL)
			exit(EXIT_FAILURE);  // out of memory
		endlessRecToSection(section, &endlessSlotCache[s]);
	}
}

/* Partner outpost state received with the save acknowledgement. */
static struct
{
	bool   fresh;
	Uint8  seat;                 // player index the half belongs to
	Uint8  availMax[9];
	Uint8  avail[9][10];
	Uint64 rng;
}
endlessPartnerStash;

// This machine's own outpost, packed for the save acknowledgement. Fixed width and endian-safe.
int endlessPackOwnOutpost(Uint8 *buf)
{
	JE_byte avail[COUNTOF(itemAvail)][COUNTOF(itemAvail[0])];
	JE_byte availMax[COUNTOF(itemAvailMax)];
	shop_outpost_stock(avail, availMax);

	int n = 0;
	memcpy(&buf[n], availMax, sizeof(availMax));
	n += (int)sizeof(availMax);
	memcpy(&buf[n], avail, sizeof(avail));
	n += (int)sizeof(avail);

	const Uint64 rng = endlessPlayerRngState[endlessEconomyIndex()];
	for (int b = 7; b >= 0; --b)
		buf[n++] = (Uint8)(rng >> (8 * b));
	return n;
}

void endlessPartnerOutpostStash(uint seat, const Uint8 *block)
{
	if (seat > 1)
		return;

	endlessPartnerStash.fresh = true;
	endlessPartnerStash.seat = (Uint8)seat;

	int n = 0;
	memcpy(endlessPartnerStash.availMax, &block[n], sizeof(endlessPartnerStash.availMax));
	n += (int)sizeof(endlessPartnerStash.availMax);
	memcpy(endlessPartnerStash.avail, &block[n], sizeof(endlessPartnerStash.avail));
	n += (int)sizeof(endlessPartnerStash.avail);

	Uint64 rng = 0;
	for (int b = 0; b < 8; ++b)
		rng = (rng << 8) | block[n++];
	endlessPartnerStash.rng = rng;
}

void endlessPartnerOutpostClear(void)
{
	memset(&endlessPartnerStash, 0, sizeof(endlessPartnerStash));
}

// Snapshot the live run AND the current outpost into a record.
static void endlessCaptureCurrent(EndlessSlotRec *r)
{
	// Reconcile the ledger with the wallet before snapshotting; restore re-anchors this value.
	// Callers do not run while the upgrade menu displays its temporary trade-in balance.
	endlessCashAudit();

	memset(r, 0, sizeof(*r));
	r->used = true;

	r->runDepth      = endlessRunDepth;
	r->runKills      = endlessRunKills;
	r->runBossKills  = endlessRunBossKills;
	r->perkDepthDone = endlessPerkDepthDone;
	r->perkPending   = endlessPerkPending ? 1 : 0;

	r->coopHostCharts = endlessCoopHostCharts ? 1 : 0;
	r->courseChooser  = (Uint8)endlessCourseChooser;
	for (uint p = 0; p < 2; ++p)
	{
		EndlessRecPlayer *v = &r->player[p];
		v->armorBonus        = endlessArmorBonus[p];
		v->revivesUsed       = endlessRevivesUsed[p];
		v->shopTax           = endlessShopTax[p];
		v->longCon           = endlessLongCon[p];
		v->buffKind          = endlessBuffKind[p];
		v->buffCharge        = endlessBuffCharge[p];
		v->buffCooldownUntil = endlessBuffCooldownUntil[p];
		v->superbombs        = (Sint32)player[p].superbombs;
		v->cleanseCharges    = endlessCleanseChargeCount[p];
		v->extraPerksBought  = endlessExtraPerksBought[p];
		v->extraPerksVisit   = endlessExtraPerksVisit[p];
		v->rerollCost        = endlessRerollCost[p];
		v->hullCost          = endlessHullCost[p];
		v->bombCost          = endlessBombCost[p];
		v->cleanseCost       = endlessCleanseCost[p];
		v->shopEntryCash     = endlessShopEntryCash[p];
		v->purchasedMods     = endlessPurchasedMods[p];
		v->reviveHeld        = endlessReviveHeld[p] ? 1 : 0;
		v->gambleRigged      = endlessGambleRigged[p] ? 1 : 0;
		v->downed            = endlessPlayerDowned[p] ? 1 : 0;
		v->gamblePerkWon     = endlessGamblePerkWon[p] ? 1 : 0;
		memcpy(v->perks, endlessPerkTakenBy[p], sizeof(v->perks));
		v->rng               = endlessPlayerRngState[p];
		SDL_strlcpy(v->gambleMsg, endlessGambleMsg[p], sizeof(v->gambleMsg));
		SDL_strlcpy(v->lastSpecialName, endlessLastSpecialName[p], sizeof(v->lastSpecialName));
	}

	r->perkChoiceN = endlessPerkChoiceN;
	for (int i = 0; i < ENDLESS_SAVE_OFFERS; ++i)
		r->perkChoice[i] = endlessPerkChoice[i];

	r->courseCnt = endlessCourseCnt;
	for (int i = 0; i < ENDLESS_MAX_COURSES; ++i)
	{
		r->courseEp[i]  = endlessCourseEp[i];
		r->courseSec[i] = endlessCourseSec[i];
		r->courseFile[i] = endlessCourseFile[i];
		r->courseMod[i] = endlessCourseMod[i];
	}
	r->lastEp  = endlessLastEp;
	r->lastSec = endlessLastSec;
	r->forced  = endlessForced ? 1 : 0;

	shop_outpost_stock(r->itemAvail, r->itemAvailMax);

	SDL_strlcpy(r->seed, endlessRunSeed, sizeof(r->seed));

	// The locked-sortie fields are populated only when saving from the locked outpost.
	r->lockedSortie = endlessLockedSortie ? 1 : 0;
	r->sortieMods   = endlessSortieModsV;
	r->sortieSec    = endlessSortieSec;
	r->sortieEp     = endlessSortieEp;
	r->sortieFile   = endlessSortieFile;

	// Anti-repeat recent-level ring.
	r->recentCount = (Uint8)endlessRecentCount;
	for (int i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
	{
		r->recentEp[i]  = endlessRecentEp[i];
		r->recentSec[i] = endlessRecentSec[i];
	}

	r->creditsShown = endlessCreditsShown ? 1 : 0;  // zone-100 credits

	r->lastSong      = endlessLastSong;             // per-zone music continuity
	r->lastSongDepth = endlessLastSongDepth;

	r->starChartsOwed   = endlessStarChartsOwed ? 1 : 0;   // boons owed to a later outpost
	r->breakthroughOwed = (Uint8)((endlessBreakthroughOwed < 0) ? 0
	                             : (endlessBreakthroughOwed > 255 ? 255 : endlessBreakthroughOwed));

	// Disk saves use Relaxed or Standard; in-memory sortie snapshots can carry Hardcore.
	r->runMode = (Uint8)endlessRunMode;

	r->baseLevelRule = (Uint8)endlessRunBaseRule;         // chart rule, fixed for the run

	r->chartRerolls    = endlessChartRerolls;             // Radar chart reroll
	r->chartStarCharts = endlessChartStarCharts ? 1 : 0;

	r->shuffleNext      = (Uint32)endlessShuffleNext;     // Shuffle bag cursor and live hand
	r->shuffleHandStart = (Uint32)endlessShuffleHandStart;

	r->cashEarned = endlessRunCashEarned;
	r->cashSpent  = endlessRunCashSpent;
	for (int i = 0; i < ENDLESS_CASH_SOURCES; ++i)
		r->cashBySource[i] = endlessCashBySource[i];
	for (int i = 0; i < ENDLESS_CASH_SINKS; ++i)
		r->cashBySink[i] = endlessCashBySink[i];

	r->usedCustom = endlessRunUsedCustom ? 1 : 0;   // custom-weapon record mark

	// The partner's half, when a save checkpoint captured one this visit.
	if (endlessPartnerStash.fresh)
	{
		r->partnerValid = 1;
		r->partnerSeat = endlessPartnerStash.seat;
		memcpy(r->partnerAvailMax, endlessPartnerStash.availMax, sizeof(r->partnerAvailMax));
		memcpy(r->partnerAvail, endlessPartnerStash.avail, sizeof(r->partnerAvail));
		r->partnerRng = endlessPartnerStash.rng;
	}
}

// Reset transient state, restore the saved run and outpost, then reopen the saved visit without a reroll.
static void endlessApplyCurrent(const EndlessSlotRec *r)
{
	endlessResetRun();

	endlessRunDepth      = r->runDepth;
	endlessRunUsedCustom = r->usedCustom != 0;
	endlessRunKills      = r->runKills;
	endlessRunBossKills  = r->runBossKills;
	endlessPerkDepthDone = r->perkDepthDone;
	endlessPerkPending   = r->perkPending != 0;

	endlessCoopHostCharts = r->coopHostCharts != 0;
	endlessCourseChooser  = (EndlessCourseChooser)r->courseChooser;
	for (uint p = 0; p < 2; ++p)
	{
		const EndlessRecPlayer *v = &r->player[p];
		endlessArmorBonus[p]         = v->armorBonus;
		endlessRevivesUsed[p]        = v->revivesUsed;
		endlessShopTax[p]            = v->shopTax;
		endlessLongCon[p]            = v->longCon;
		endlessBuffKind[p]           = v->buffKind;
		endlessBuffCharge[p]         = v->buffCharge;
		endlessBuffCooldownUntil[p]  = v->buffCooldownUntil;
		player[p].superbombs         = (v->superbombs < 0) ? 0u
		                             : (v->superbombs > 10 ? 10u : (uint)v->superbombs);
		endlessCleanseChargeCount[p] = v->cleanseCharges;
		endlessExtraPerksBought[p]   = endlessClamp(v->extraPerksBought, 0, ENDLESS_PERK_PAID_MAX);
		endlessExtraPerksVisit[p]    = endlessClamp(v->extraPerksVisit, 0, ENDLESS_PERK_PAID_MAX);
		endlessRerollCost[p]         = v->rerollCost;
		endlessHullCost[p]           = v->hullCost;
		endlessBombCost[p]           = v->bombCost;
		endlessCleanseCost[p]        = v->cleanseCost;
		endlessShopEntryCash[p]      = v->shopEntryCash;
		endlessPurchasedMods[p]      = v->purchasedMods;
		endlessReviveHeld[p]         = v->reviveHeld != 0;
		endlessGambleRigged[p]       = v->gambleRigged != 0;
		endlessPlayerDowned[p]       = v->downed != 0;
		endlessGamblePerkWon[p]      = v->gamblePerkWon != 0;
		for (int i = 0; i < PERK_COUNT; ++i)
		{
			const int maxs = endlessPerkTable[i].maxStack;
			endlessPerkTakenBy[p][i] = (JE_byte)(v->perks[i] > maxs ? maxs : v->perks[i]);
		}
		SDL_strlcpy(endlessGambleMsg[p], v->gambleMsg, sizeof(endlessGambleMsg[p]));
		SDL_strlcpy(endlessLastSpecialName[p], v->lastSpecialName,
		            sizeof(endlessLastSpecialName[p]));
	}
	endlessPerkRederive();

	endlessSetSeed(r->seed);  // restore the run seed (endlessResetRun blanked it); rehashes + primes the stream
	// ...then put each player's own draw stream back where the save left it, so a resumed outpost
	// deals the same next hand.
	if (r->player[0].rng != 0 || r->player[1].rng != 0)
	{
		endlessPlayerRngState[0] = r->player[0].rng;
		endlessPlayerRngState[1] = r->player[1].rng;
	}

	endlessPerkChoiceN = endlessClamp(r->perkChoiceN, 0, ENDLESS_SAVE_OFFERS);
	for (int i = 0; i < ENDLESS_SAVE_OFFERS; ++i)
		endlessPerkChoice[i] = r->perkChoice[i];
	for (int i = 0; i < endlessPerkChoiceN; ++i)
		if (endlessPerkChoice[i] < 0 || endlessPerkChoice[i] >= PERK_COUNT)
			endlessPerkChoiceN = 0;

	endlessLastEp  = r->lastEp;
	endlessLastSec = r->lastSec;
	endlessForced  = r->forced != 0;

	endlessRecentCount = (r->recentCount > ENDLESS_LEVEL_HISTORY) ? ENDLESS_LEVEL_HISTORY : r->recentCount;
	for (int i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
	{
		endlessRecentEp[i]  = r->recentEp[i];
		endlessRecentSec[i] = r->recentSec[i];
	}

	endlessCreditsShown = r->creditsShown != 0;

	// A loaded run replays its approaches, so a milestone reached again warns again.
	endlessWarnedZone = 0;

	// A zero lastSong means no remembered depth.
	endlessLastSong      = r->lastSong;
	endlessLastSongDepth = (r->lastSong != 0) ? r->lastSongDepth : -1;

	endlessStarChartsOwed   = r->starChartsOwed != 0;
	endlessBreakthroughOwed = r->breakthroughOwed;

	// Restore the saved run mode.
	endlessRunMode = (r->runMode < ENDLESS_RUNMODE_COUNT) ? (EndlessRunMode)r->runMode
	                                                      : ENDLESS_RUNMODE_RELAXED;

	// ...and the chart rule the run was started under, whatever the toggle currently says.
	endlessRunBaseRule = (EndlessBaseRule)r->baseLevelRule;

	// The visit resumes its chart as saved: the reroll count rode in on the record, and the seat
	// that charted it follows from the co-op turn restored above.
	endlessChartRerolls    = r->chartRerolls;
	endlessChartStarCharts = r->chartStarCharts != 0;
	endlessChartSeat       = endlessChartingPlayerIndex();

	// The bag resumes where the saved chart left it, so the next hand is the one the run was owed.
	// The restored chart keeps the hand it was dealt from, which is what a peer re-anchors against.
	endlessShuffleSetNext((int)r->shuffleNext);
	endlessShuffleHandStart = (int)r->shuffleHandStart;
	endlessShuffleHandDepth = endlessRunDepth;

	endlessRestoreSavedCourses(r);

	memcpy(itemAvail, r->itemAvail, sizeof(itemAvail));
	memcpy(itemAvailMax, r->itemAvailMax, sizeof(itemAvailMax));

	// A locked-sortie save reopens the locked outpost and committed level.
	endlessLockedSortie = r->lockedSortie != 0;
	if (endlessLockedSortie)
	{
		endlessSortieModsV = r->sortieMods;
		endlessSortieSec   = (JE_byte)r->sortieSec;
		endlessSortieEp    = r->sortieEp;
		endlessSortieFile  = (JE_byte)r->sortieFile;
		endlessSortieHave  = true;
	}

	// Restore the ledger and anchor it to the current wallet. Sortie restores re-anchor after loadout copy.
	endlessRunCashEarned = r->cashEarned;
	endlessRunCashSpent  = r->cashSpent;
	for (int i = 0; i < ENDLESS_CASH_SOURCES; ++i)
		endlessCashBySource[i] = r->cashBySource[i];
	for (int i = 0; i < ENDLESS_CASH_SINKS; ++i)
		endlessCashBySink[i] = r->cashBySink[i];
	endlessCashResync();

	endlessResumeVisit = true;  // next outpost: restore this snapshot, do not reroll
}

/* Stage the Endless half for the same file write as its campaign record. */
void endlessSaveCaptureSlot(JE_byte slot)
{
	if (slot < 1 || slot > SAVE_FILES_NUM)
		return;
	if (endlessMode)
		endlessCaptureCurrent(&endlessSlotCache[slot - 1]);
	else
		endlessSlotCache[slot - 1].used = false;  // a normal save over an endless slot drops its stale record
}

/* One player's own half of the outpost, as the other machine needs to see it. Fixed width and
 * endian-safe: it travels on the shop sync packet (network.c). */
int endlessPackPlayerBlock(Uint8 *buf, uint p)
{
	if (p >= COUNTOF(player))
		return 0;

	int n = 0;
	buf[n++] = endlessReviveHeld[p] ? 1 : 0;
	buf[n++] = endlessGambleRigged[p] ? 1 : 0;
	buf[n++] = endlessPlayerDowned[p] ? 1 : 0;
	buf[n++] = (Uint8)MIN(player[p].superbombs, 10u);

	const Sint32 fields[11] = {
		endlessArmorBonus[p], (Sint32)endlessPurchasedMods[p], endlessBuffKind[p],
		endlessBuffCharge[p], endlessBuffCooldownUntil[p], endlessCleanseChargeCount[p],
		endlessLongCon[p], endlessShopTax[p], endlessRevivesUsed[p],
		endlessExtraPerksBought[p], endlessExtraPerksVisit[p],
	};
	for (unsigned i = 0; i < COUNTOF(fields); ++i, n += 4)
	{
		const Uint32 v = (Uint32)fields[i];
		buf[n]     = (Uint8)(v >> 24);
		buf[n + 1] = (Uint8)(v >> 16);
		buf[n + 2] = (Uint8)(v >> 8);
		buf[n + 3] = (Uint8)v;
	}
	// The three prices that scale with the wallet travel at the wallet's width.
	const Sint64 prices[3] = { endlessRerollCost[p], endlessHullCost[p], endlessShopEntryCash[p] };
	for (unsigned i = 0; i < COUNTOF(prices); ++i)
		for (int b = 7; b >= 0; --b)
			buf[n++] = (Uint8)((Uint64)prices[i] >> (8 * b));

	for (int i = 0; i < ENDLESS_PLAYER_BLOCK_PERKS; ++i)
		buf[n++] = (i < PERK_COUNT) ? endlessPerkTakenBy[p][i] : 0;

	/* The chart itself is derived on both machines; only the charting seat's reroll count and the
	 * bag position its hand came off travel, and the second only to repair a drift. */
	buf[n++] = endlessChartRerolls;
	const Uint32 hand = (Uint32)endlessShuffleHandStart;
	buf[n++] = (Uint8)(hand >> 24);
	buf[n++] = (Uint8)(hand >> 16);
	buf[n++] = (Uint8)(hand >> 8);
	buf[n++] = (Uint8)hand;
	return n;
}

void endlessUnpackPlayerBlock(const Uint8 *buf, uint p)
{
	if (p >= COUNTOF(player))
		return;

	int n = 0;
	endlessReviveHeld[p] = buf[n++] != 0;
	endlessGambleRigged[p] = buf[n++] != 0;
	endlessPlayerDowned[p] = buf[n++] != 0;
	const uint bombs = buf[n++];   // MIN evaluates twice, so the read has to happen first
	player[p].superbombs = MIN(bombs, 10u);

	Sint32 fields[11];
	for (unsigned i = 0; i < COUNTOF(fields); ++i, n += 4)
	{
		fields[i] = (Sint32)(((Uint32)buf[n] << 24) | ((Uint32)buf[n + 1] << 16)
		                     | ((Uint32)buf[n + 2] << 8) | (Uint32)buf[n + 3]);
	}
	Sint64 prices[3];
	for (unsigned i = 0; i < COUNTOF(prices); ++i)
	{
		Uint64 v = 0;
		for (int b = 0; b < 8; ++b)
			v = (v << 8) | buf[n++];
		prices[i] = (Sint64)v;
	}

	endlessArmorBonus[p]         = fields[0];
	endlessPurchasedMods[p]      = (unsigned)fields[1];
	endlessBuffKind[p]           = fields[2];
	endlessBuffCharge[p]         = fields[3];
	endlessBuffCooldownUntil[p]  = fields[4];
	endlessCleanseChargeCount[p] = fields[5];
	endlessLongCon[p]            = fields[6];
	endlessShopTax[p]            = fields[7];
	endlessRevivesUsed[p]        = fields[8];
	endlessExtraPerksBought[p]   = endlessClamp(fields[9], 0, ENDLESS_PERK_PAID_MAX);
	endlessExtraPerksVisit[p]    = endlessClamp(fields[10], 0, ENDLESS_PERK_PAID_MAX);
	endlessRerollCost[p]         = prices[0];
	endlessHullCost[p]           = prices[1];
	endlessShopEntryCash[p]      = cash_clamp(prices[2]);

	for (int i = 0; i < ENDLESS_PLAYER_BLOCK_PERKS && i < PERK_COUNT; ++i)
	{
		const int maxs = endlessPerkTable[i].maxStack;
		endlessPerkTakenBy[p][i] = (JE_byte)MIN((int)buf[n + i], maxs);
	}
	n += ENDLESS_PLAYER_BLOCK_PERKS;
	endlessPerkRederive();

	endlessChartSyncRerolls(p, buf[n]);
	++n;

	// After the reroll count, so a re-anchored redeal runs on the perks unpacked above.
	const Uint32 hand = ((Uint32)buf[n] << 24) | ((Uint32)buf[n + 1] << 16)
	                  | ((Uint32)buf[n + 2] << 8) | (Uint32)buf[n + 3];
	endlessShuffleSyncHand(p, (hand > ENDLESS_SHUFFLE_POSITION_MAX) ? 0 : (int)hand);
}

// The Endless debug panel's edits, contract in endless.h.
COMPILE_TIME_ASSERT(endless_debug_block_perks_fit, PERK_COUNT <= ENDLESS_DEBUG_BLOCK_PERKS);

void endlessPackDebugBlock(Uint8 *buf)
{
	int n = 0;
	for (int b = 7; b >= 0; --b)
		buf[n++] = (Uint8)(endlessActiveMods >> (8 * b));

	const Uint16 depth = (Uint16)((endlessRunDepth < 0) ? 0
	                            : (endlessRunDepth > 0xFFFF) ? 0xFFFF : endlessRunDepth);
	buf[n++] = (Uint8)(depth >> 8);
	buf[n++] = (Uint8)depth;

	for (uint p = 0; p < COUNTOF(endlessPerkTakenBy); ++p)
		for (int i = 0; i < ENDLESS_DEBUG_BLOCK_PERKS; ++i)
			buf[n++] = (i < PERK_COUNT) ? endlessPerkTakenBy[p][i] : 0;

	/* The live modifier mask cannot be rebuilt from consumed purchases. */
	for (uint p = 0; p < COUNTOF(endlessPlayerMods); ++p)
		for (int b = 7; b >= 0; --b)
			buf[n++] = (Uint8)(endlessPlayerMods[p] >> (8 * b));
	for (uint p = 0; p < COUNTOF(endlessPurchasedMods); ++p)
		for (int b = 3; b >= 0; --b)
			buf[n++] = (Uint8)(endlessPurchasedMods[p] >> (8 * b));
}

void endlessUnpackDebugBlock(const Uint8 *buf)
{
	int n = 0;
	Uint64 mods = 0;
	for (int b = 0; b < 8; ++b)
		mods = (mods << 8) | buf[n++];
	const bool modsMoved = (mods != endlessActiveMods);
	endlessActiveMods = mods;

	endlessRunDepth = ((int)buf[n] << 8) | buf[n + 1];
	n += 2;

	for (uint p = 0; p < COUNTOF(endlessPerkTakenBy); ++p, n += ENDLESS_DEBUG_BLOCK_PERKS)
		for (int i = 0; i < PERK_COUNT; ++i)
			endlessPerkTakenBy[p][i] = (JE_byte)MIN((int)buf[n + i], endlessPerkTable[i].maxStack);
	endlessPerkRederive();

	for (uint p = 0; p < COUNTOF(endlessPlayerMods); ++p)
	{
		Uint64 v = 0;
		for (int b = 0; b < 8; ++b)
			v = (v << 8) | buf[n++];
		endlessPlayerMods[p] = v;
	}
	for (uint p = 0; p < COUNTOF(endlessPurchasedMods); ++p)
	{
		unsigned v = 0;
		for (int b = 0; b < 4; ++b)
			v = (v << 8) | buf[n++];
		endlessPurchasedMods[p] = v;
	}

	/* Refresh only changed masks; gravity refresh consumes RNG. */
	if (modsMoved)
		endlessRefreshModDerivedState();
}

bool endlessSavedStockSelfTest(void)
{
	JE_byte avail[COUNTOF(itemAvail)][COUNTOF(itemAvail[0])];
	JE_byte availMax[COUNTOF(itemAvailMax)];
	shop_outpost_stock(avail, availMax);

	EndlessSlotRec rec;
	endlessCaptureCurrent(&rec);
	Uint8 packed[sizeof(availMax) + sizeof(avail) + sizeof(Uint64)];
	endlessPackOwnOutpost(packed);
	return memcmp(rec.itemAvail, avail, sizeof(avail)) == 0
	    && memcmp(rec.itemAvailMax, availMax, sizeof(availMax)) == 0
	    && memcmp(packed, availMax, sizeof(availMax)) == 0
	    && memcmp(&packed[sizeof(availMax)], avail, sizeof(avail)) == 0;
}

/* Online resume uses the save text codec; personal shop stock is regenerated locally. */
size_t endlessRunSerialize(Uint8 *out, size_t max)
{
	if (!endlessMode || out == NULL)
		return 0;

	EndlessSlotRec rec;
	endlessCaptureCurrent(&rec);

	char *text = NULL;
	size_t size = 0;
	if (!endlessTextEncode(&rec, &text, &size) || size > max)
	{
		free(text);
		return 0;
	}
	memcpy(out, text, size);
	free(text);
	return size;
}

bool endlessRunAdopt(const Uint8 *bytes, size_t len)
{
	EndlessSlotRec rec;
	if (bytes == NULL || !endlessTextDecode((const char *)bytes, len, &rec) || !rec.used)
		return false;

	endlessApplyCurrent(&rec);
	endlessMode = true;

	/* Use this seat's partner block when the checkpoint captured one; otherwise redeal its rows. */
	const uint p = endlessEconomyIndex();
	if (rec.partnerValid && rec.partnerSeat == p)
	{
		memcpy(itemAvailMax, rec.partnerAvailMax, sizeof(itemAvailMax));
		memcpy(itemAvail, rec.partnerAvail, sizeof(itemAvail));
		if (rec.partnerRng != 0)
			endlessPlayerRngState[p] = rec.partnerRng;
	}
	else
	{
		endlessShopRedrawStock();
	}

	/* This machine can now save a complete record of its own: the publisher's half is the
	 * record's own rows and stream, so it becomes the stash here. */
	endlessPartnerOutpostClear();
	if (coopEndlessMode)
	{
		endlessPartnerStash.fresh = true;
		endlessPartnerStash.seat = (Uint8)(1 - p);
		memcpy(endlessPartnerStash.availMax, rec.itemAvailMax, sizeof(endlessPartnerStash.availMax));
		memcpy(endlessPartnerStash.avail, rec.itemAvail, sizeof(endlessPartnerStash.avail));
		endlessPartnerStash.rng = rec.player[1 - p].rng;
	}

	return true;
}

/* These operate on the slot cache, not the live run. The serialized form matches opentyrian.sav. */
size_t endlessSlotSerialize(JE_byte slot, Uint8 *out, size_t max)
{
	if (slot < 1 || slot > SAVE_FILES_NUM || out == NULL || !endlessSlotCache[slot - 1].used)
		return 0;

	char *text = NULL;
	size_t size = 0;
	if (!endlessTextEncode(&endlessSlotCache[slot - 1], &text, &size) || size > max)
	{
		free(text);
		return 0;
	}
	memcpy(out, text, size);
	free(text);
	return size;
}

bool endlessSlotAdopt(JE_byte slot, const Uint8 *bytes, size_t len)
{
	if (slot < 1 || slot > SAVE_FILES_NUM || bytes == NULL)
		return false;

	EndlessSlotRec rec;
	if (!endlessTextDecode((const char *)bytes, len, &rec) || !rec.used)
		return false;

	endlessSlotCache[slot - 1] = rec;
	return true;
}

void endlessSlotClear(JE_byte slot)
{
	if (slot >= 1 && slot <= SAVE_FILES_NUM)
		endlessSlotCache[slot - 1].used = false;
}

// Does this save slot hold an Endless run? Used by the load screen to keep Endless and Campaign
// sessions from offering each other's saves.
bool endlessSlotHasRun(JE_byte slot)
{
	if (slot < 1 || slot > SAVE_FILES_NUM)
		return false;
	return endlessSlotCache[slot - 1].used;
}

bool endlessLoadSlot(JE_byte slot)
{
	if (slot < 1 || slot > SAVE_FILES_NUM)
		return false;

	if (!endlessSlotCache[slot - 1].used)
		return false;

	endlessApplyCurrent(&endlessSlotCache[slot - 1]);
	endlessMode = true;  // JE_loadGame cleared it for a normal load; this slot is an endless run
	endlessRecordRunStart();  // resumed record gains count from this point

	/* Preserve the partner half for the resumed entry checkpoint. */
	endlessPartnerOutpostClear();
	const EndlessSlotRec *const r = &endlessSlotCache[slot - 1];
	if (r->partnerValid && r->partnerSeat <= 1)
	{
		endlessPartnerStash.fresh = true;
		endlessPartnerStash.seat = r->partnerSeat;
		memcpy(endlessPartnerStash.availMax, r->partnerAvailMax, sizeof(endlessPartnerStash.availMax));
		memcpy(endlessPartnerStash.avail, r->partnerAvail, sizeof(endlessPartnerStash.avail));
		endlessPartnerStash.rng = r->partnerRng;
	}

	return true;
}

// Remains true until the restored outpost consumes its snapshot. JE_main handles in-shop loads.
bool endlessResumePending(void) { return endlessResumeVisit; }

// Quit Level snapshots reuse the save record for run and outpost state.
static EndlessSlotRec endlessSortieRec;

void endlessCaptureSortie(void)
{
	if (!endlessMode)
		return;

	// A new launch clears the locked-outpost flag before capture.
	endlessLockedSortie = false;

	endlessCaptureCurrent(&endlessSortieRec);                          // endless run + outpost state
	memcpy(endlessSortiePlayer, player, sizeof(endlessSortiePlayer));  // full loadout (cash / items / superbombs)
	endlessSortieModsV = endlessActiveMods;   // committed level modifiers
	// ...and each ship's own half of them. Course selection folded the buff its owner paid for into
	// this mask and then cleared the purchase it came from, so nothing else can re-derive it.
	memcpy(endlessSortiePlayerModsV, endlessPlayerMods, sizeof(endlessSortiePlayerModsV));
	endlessSortieSec   = mainLevel;           // committed section
	endlessSortieEp    = JE_currentEpisodeId();  // committed episode (extended id)
	endlessSortieFile  = lvlFileNum;          // committed level file
	endlessSortieHave  = true;
}

void endlessRestoreSortie(void)
{
	if (!endlessSortieHave)
		return;

	// Preserve one-shot state across endlessApplyCurrent's reset. Run mode is stored in the record.
	unsigned preBuff[COUNTOF(endlessSortiePrePurchased)];
	int      preCleanse[COUNTOF(endlessSortiePreCleanse)];
	int      preLongCon[COUNTOF(endlessSortiePreLongCon)];
	memcpy(preBuff, endlessSortiePrePurchased, sizeof(preBuff));
	memcpy(preCleanse, endlessSortiePreCleanse, sizeof(preCleanse));
	memcpy(preLongCon, endlessSortiePreLongCon, sizeof(preLongCon));
	const Uint64   outpostMods = endlessSortieOutpostMods;
	const JE_byte  outpostEp   = endlessSortieOutpostEp;

	// Restore the launch snapshot and keep its committed level data.
	endlessApplyCurrent(&endlessSortieRec);
	memcpy(player, endlessSortiePlayer, sizeof(endlessSortiePlayer));
	endlessActiveMods      = endlessSortieModsV;
	endlessSortieHave      = true;
	endlessSortieOutpostMods = outpostMods;
	endlessSortieOutpostEp   = outpostEp;

	// The restored stock is item ids, so reload the tables it was drawn against before the outpost
	// redraws. Both branches below reselect an episode when they relaunch.
	if (outpostEp != 0)
		JE_initEpisodeId(outpostEp);

	if (endlessHardcore())
	{
		// Hardcore reuses the committed course and its post-pick one-shot state, so the relaunch
		// skips course selection and has to be handed back the masks it folded (see below).
		endlessLockedSortie = true;
		memcpy(endlessPlayerMods, endlessSortiePlayerModsV, sizeof(endlessPlayerMods));
	}
	else
	{
		// Relaxed and Standard reopen before course selection, so restore pre-pick one-shots.
		endlessLockedSortie = false;
		memcpy(endlessPurchasedMods, preBuff, sizeof(endlessPurchasedMods));
		memcpy(endlessCleanseChargeCount, preCleanse, sizeof(endlessCleanseChargeCount));
		memcpy(endlessLongCon, preLongCon, sizeof(endlessLongCon));
		// Restore the outpost's modifiers; the next course selection replaces them.
		endlessActiveMods         = endlessSortieOutpostMods;
	}

	endlessCashResync();  // the reverted wallet is the new baseline; the tally rode in on the record
}

bool endlessSortieValid(void) { return endlessSortieHave; }

// Restart Zone restores the launch snapshot and re-arms the same level without an outpost visit.
// Consumed one-shot purchases remain consumed.
void endlessRestartSortie(void)
{
	if (!endlessSortieHave)
		return;

	const Uint64  outpostMods = endlessSortieOutpostMods;  // rescue these from the reset inside the apply
	const JE_byte outpostEp   = endlessSortieOutpostEp;
	// Preserve one-shot purchases because the restored wallet has already paid for them.
	unsigned prePurchased[COUNTOF(endlessSortiePrePurchased)];
	int      preCleanse[COUNTOF(endlessSortiePreCleanse)];
	int      preLongCon[COUNTOF(endlessSortiePreLongCon)];
	memcpy(prePurchased, endlessSortiePrePurchased, sizeof(prePurchased));
	memcpy(preCleanse, endlessSortiePreCleanse, sizeof(preCleanse));
	memcpy(preLongCon, endlessSortiePreLongCon, sizeof(preLongCon));

	endlessApplyCurrent(&endlessSortieRec);
	memcpy(player, endlessSortiePlayer, sizeof(endlessSortiePlayer));
	// Reapply personal modifiers cleared by endlessApplyCurrent's reset.
	memcpy(endlessPlayerMods, endlessSortiePlayerModsV, sizeof(endlessPlayerMods));
	endlessSortieHave   = true;
	endlessLockedSortie = false;
	// Preserve outpost modifiers and episode for a later bail from the restarted zone.
	endlessSortieOutpostMods = outpostMods;
	endlessSortieOutpostEp   = outpostEp;
	memcpy(endlessSortiePrePurchased, prePurchased, sizeof(endlessSortiePrePurchased));
	memcpy(endlessSortiePreCleanse, preCleanse, sizeof(endlessSortiePreCleanse));
	memcpy(endlessSortiePreLongCon, preLongCon, sizeof(endlessSortiePreLongCon));

	endlessCashResync();           // the reverted wallet is the new baseline (the tally rode in on the record)
	endlessResumeVisit = false;    // endlessBetweenLevels normally consumes this; nothing will here
	endlessArmLockedRelaunch();    // re-arm the committed level: episode, section, level file, mutators
}

// Return the committed level's payout adjustment, or zero when no sortie is active.
int endlessSortiePayoutMille(void)
{
	if (!endlessSortieHave)
		return 0;
	return endlessLevelPayoutMille(endlessSortieEp, endlessSortieFile, difficultyLevel);
}

void endlessArmLockedRelaunch(void)
{
	// Re-arm directly so course-selection one-shots do not run twice.
	JE_initEpisodeId(endlessSortieEp);  // set level fields after this reset
	endlessActiveMods = endlessSortieModsV;
	mainLevel = endlessSortieSec;
	if (endlessSortieFile != 0)
		lvlFileNum = endlessSortieFile;
	forcedLvlFileNum = endlessSortieFile;  // keep JE_loadMap's rescan from reverting to the section's first ']L'
	nextLevel = mainLevel;
	jumpSection = true;  // exits the shop loop; JE_loadMap then loads the committed level
}

// Campaign debug modifiers persist in opentyrian.cfg.
// Do not overwrite them with live Endless run state.

#define ENDLESS_CFG_PIN_PREFIX "pin_"
#define ENDLESS_CFG_PIN_OFF    (-1)   // no lever's valid range reaches below 0, so this can't collide

void endlessDebugConfigSave(ConfigSection *section)
{
	if (section == NULL || endlessMode)
		return;

	config_set_int_option(section, "campaign_mods", endlessCampaignMods ? 1 : 0);
	config_set_int_option(section, "virtual_zone", endlessRunDepth + 1);

	// Store the 64-bit modifier mask as hex text because config integers are narrower.
	char buf[32];
	snprintf(buf, sizeof(buf), "%016" PRIX64, (Uint64)endlessActiveMods);
	config_set_string_option(section, "mods", buf);

	// Store two hex digits per perk in the serialized PERK_* order. endlessPerkSetOwned reads this
	// back, so write the row it writes: this machine's own.
	char perks[2 * PERK_COUNT + 1];
	int n = 0;
	for (int p = 0; p < PERK_COUNT && n + 2 < (int)sizeof(perks); ++p)
		n += snprintf(perks + n, sizeof(perks) - (size_t)n, "%02X", endlessPerkGetOwned(p) & 0xFF);
	perks[n] = '\0';
	config_set_string_option(section, "perks", perks);

	for (int i = 0; i < ESO_COUNT; ++i)
	{
		char key[48];
		snprintf(key, sizeof(key), ENDLESS_CFG_PIN_PREFIX "%s", endlessScalingOverrideKey(i));
		config_set_int_option(section, key, endlessScalingOverride[i].active
		                                    ? endlessScalingOverride[i].value : ENDLESS_CFG_PIN_OFF);
	}
}

void endlessDebugConfigLoad(const ConfigSection *section)
{
	if (section == NULL)
		return;

	int campaign_mods = 0;
	config_get_int_option(section, "campaign_mods", &campaign_mods);
	endlessCampaignMods = (campaign_mods != 0);

	int zone = 1;
	config_get_int_option(section, "virtual_zone", &zone);
	if (zone < 1)
		zone = 1;
	if (zone > 9999)
		zone = 9999;
	endlessRunDepth = zone - 1;

	const char *mods = NULL;
	if (config_get_string_option(section, "mods", &mods) && mods != NULL)
		endlessActiveMods = (Uint64)strtoull(mods, NULL, 16);

	const char *perks = NULL;
	if (config_get_string_option(section, "perks", &perks) && perks != NULL)
	{
		const size_t len = strlen(perks);
		for (int p = 0; p < PERK_COUNT && (size_t)(p * 2 + 1) < len; ++p)
		{
			unsigned v = 0;
			if (sscanf(perks + p * 2, "%2x", &v) == 1)
				endlessPerkSetOwned(p, (int)v);   // clamps to the perk's own max
		}
	}

	for (int i = 0; i < ESO_COUNT; ++i)
	{
		char key[48];
		snprintf(key, sizeof(key), ENDLESS_CFG_PIN_PREFIX "%s", endlessScalingOverrideKey(i));
		int v = ENDLESS_CFG_PIN_OFF;
		config_get_int_option(section, key, &v);
		// Clamp rather than trust: a hand-edited or older config must not be able to hand a lever a
		// value outside the range the editor itself enforces.
		if (v == ENDLESS_CFG_PIN_OFF)
		{
			endlessScalingOverride[i].active = false;
			endlessScalingOverride[i].value = 0;
		}
		else
		{
			endlessScalingOverride[i].active = true;
			endlessScalingOverride[i].value = endlessClamp(v, endlessScalingOverrideMin(i),
			                                                  endlessScalingOverrideMax(i));
		}
	}
}

// Preserve the original Relaxed and pre-rename Standard config keys.
static const char *const endlessBestZoneKey[ENDLESS_RUNMODE_COUNT] = {
	"best_zone", "best_zone_normal", "best_zone_hardcore",
};

// Whether each record above was set with a custom weapon in use. Absent from a config written
// before the mark existed, which reads as an unassisted record.
static const char *const endlessBestZoneCustomKey[ENDLESS_RUNMODE_COUNT] = {
	"best_zone_custom", "best_zone_normal_custom", "best_zone_hardcore_custom",
};

// Short or missing per-difficulty lists leave later entries empty.
static const char *const endlessBestZoneDiffKey[ENDLESS_RUNMODE_COUNT] = {
	"best_zone_diff", "best_zone_normal_diff", "best_zone_hardcore_diff",
};
static const char *const endlessBestZoneDiffCustomKey[ENDLESS_RUNMODE_COUNT] = {
	"best_zone_diff_custom", "best_zone_normal_diff_custom", "best_zone_hardcore_diff_custom",
};

/* Co-op and non-Varied records use key suffixes; missing suffixes read as empty tables. */
static const char *const endlessRecordRuleTail[ENDLESS_BASE_TABLES] = {
	"", "_same", "_variedshuffle", "_sameshuffle",
};

static void endlessRecordKey(char *out, size_t n, const char *base, int players, int variant)
{
	const char *const tail = (variant >= 0 && variant < ENDLESS_BASE_TABLES)
	                       ? endlessRecordRuleTail[variant] : "";
	snprintf(out, n, "%s%s%s", base, (players == 1) ? "_2p" : "", tail);
}

void endlessRecordConfigSave(ConfigSection *section)
{
	if (section == NULL)
		return;

	for (int v = 0; v < ENDLESS_BASE_TABLES; ++v)
	for (int t = 0; t < ENDLESS_PLAYER_TABLES; ++t)
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		char key[64];
		endlessRecordKey(key, sizeof(key), endlessBestZoneKey[m], t, v);
		config_set_int_option(section, key, endlessBestZoneUntagged[v][t][m]);
		endlessRecordKey(key, sizeof(key), endlessBestZoneCustomKey[m], t, v);
		config_set_int_option(section, key, endlessBestZoneUntaggedCustom[v][t][m] ? 1 : 0);

		char zones[ENDLESS_DIFFICULTY_COUNT * 8], marks[ENDLESS_DIFFICULTY_COUNT + 1];
		size_t len = 0;
		for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
		{
			len += snprintf(zones + len, sizeof(zones) - len, "%s%d",
			                (d > 0) ? "," : "", endlessBestZoneDiff[v][t][m][d]);
			marks[d] = endlessBestZoneDiffCustom[v][t][m][d] ? '1' : '0';
		}
		marks[ENDLESS_DIFFICULTY_COUNT] = '\0';

		endlessRecordKey(key, sizeof(key), endlessBestZoneDiffKey[m], t, v);
		config_set_string_option(section, key, zones);
		endlessRecordKey(key, sizeof(key), endlessBestZoneDiffCustomKey[m], t, v);
		config_set_string_option(section, key, marks);
	}
}

void endlessRecordConfigLoad(const ConfigSection *section)
{
	if (section == NULL)
		return;

	for (int v = 0; v < ENDLESS_BASE_TABLES; ++v)
	for (int t = 0; t < ENDLESS_PLAYER_TABLES; ++t)
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		char key[64];
		int best = 0;
		endlessRecordKey(key, sizeof(key), endlessBestZoneKey[m], t, v);
		config_get_int_option(section, key, &best);
		endlessBestZoneUntagged[v][t][m] = (best > 0) ? best : 0;  // a hand-edited negative reads as "no record"

		int custom = 0;
		endlessRecordKey(key, sizeof(key), endlessBestZoneCustomKey[m], t, v);
		config_get_int_option(section, key, &custom);
		endlessBestZoneUntaggedCustom[v][t][m] = (endlessBestZoneUntagged[v][t][m] > 0) && (custom != 0);

		for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
		{
			endlessBestZoneDiff[v][t][m][d] = 0;
			endlessBestZoneDiffCustom[v][t][m][d] = false;
		}

		const char *zones = NULL;
		endlessRecordKey(key, sizeof(key), endlessBestZoneDiffKey[m], t, v);
		if (config_get_string_option(section, key, &zones) && zones != NULL)
		{
			for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT && *zones != '\0'; ++d)
			{
				const int zone = (int)strtol(zones, NULL, 10);
				endlessBestZoneDiff[v][t][m][d] = (zone > 0) ? zone : 0;

				const char *const comma = strchr(zones, ',');
				zones = (comma != NULL) ? comma + 1 : "";
			}
		}

		const char *marks = NULL;
		endlessRecordKey(key, sizeof(key), endlessBestZoneDiffCustomKey[m], t, v);
		if (config_get_string_option(section, key, &marks) && marks != NULL)
		{
			for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT && marks[d] != '\0'; ++d)
				endlessBestZoneDiffCustom[v][t][m][d] = (endlessBestZoneDiff[v][t][m][d] > 0) && (marks[d] == '1');
		}
	}
	endlessRecordRunStart();   // nothing is running yet, so the baseline is the record
}
