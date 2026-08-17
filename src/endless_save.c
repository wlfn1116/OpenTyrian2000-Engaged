/* Endless saves and Quit Level sortie snapshots. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "episodes.h"
#include "file.h"
#include "mainint.h"
#include "player.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"

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
// so a bail must restore this before the outpost redraws. See "Death, retries" in doc/notes.md.
JE_byte  endlessSortieOutpostEp = 0;

/* Every save slot's Endless half lives in opentyrian.sav as a `section 'endless' 'N'`, written
 * beside the campaign record so the pair cannot drift (the codec is below). The DOS-era binary
 * sidecar endless.sav is read only to migrate, through the frozen reader further down. See
 * "Saves and records" in doc/notes.md. */

#define ENDLESS_LEGACY_SAVE_FILE      "endless.sav"
#define ENDLESS_LEGACY_VERSION_MAX    27   // the last binary format; the reader knows v3..v27
#define ENDLESS_SAVE_PERKS   32
#define ENDLESS_SAVE_PERKS_V10 16
#define ENDLESS_SAVE_PERK_CHARGER_V13 14
#define ENDLESS_SAVE_OFFERS     ENDLESS_PERK_OFFERS_MILESTONE
#define ENDLESS_SAVE_OFFERS_V12 3

// The legacy record kept spare cash-source/sink slots past the live enums.
#define ENDLESS_SAVE_CASH_SOURCES 12
#define ENDLESS_SAVE_CASH_SINKS   12

// Legacy file header: the tag, the format version, how many slot records follow, and from v25 how
// many bytes each of those records is.
#define ENDLESS_LEGACY_WIDTH_VERSION 25   // first version whose header carries the record width
#define ENDLESS_LEGACY_HEADER_BYTES  8

typedef struct {
	int version;   // format version the file was written by
	int slots;     // records that follow
	int width;     // bytes per record, or 0 when the version alone fixes it (pre-v25)
} EndlessSaveHeader;

// Perk IDs are on-disk slots in the legacy record and in the co-op outpost block alike.
COMPILE_TIME_ASSERT(endless_save_perks_fit, PERK_COUNT <= ENDLESS_SAVE_PERKS);
/* The co-op outpost block's loops truncate rather than overrun, so an overflow would silently stop
 * syncing the perks past the width and desync the two ships instead of failing here. Widen it with
 * NET_VERSION. */
COMPILE_TIME_ASSERT(endless_block_perks_fit, PERK_COUNT <= ENDLESS_PLAYER_BLOCK_PERKS);
COMPILE_TIME_ASSERT(endless_save_cash_sources_fit, ENDLESS_CASH_SOURCES <= ENDLESS_SAVE_CASH_SOURCES);
COMPILE_TIME_ASSERT(endless_save_cash_sinks_fit, ENDLESS_CASH_SINKS <= ENDLESS_SAVE_CASH_SINKS);

typedef struct {
	bool used;

	// Run state.
	Sint32 runDepth, armorBonus, runKills, runBossKills;
	Sint32 buffCharge, revivesUsed, shopTax, longCon, perkDepthDone, superbombs;
	Uint8  reviveHeld, gambleRigged;
	Uint8  perkOwned[ENDLESS_SAVE_PERKS];   // legacy: the summed stacks; perkTakenBy is authoritative

	// Outpost prices and pending buys. extraPerkCost is legacy: nothing prices off it now, and it
	// is carried only so the frozen binary reader and older text saves still round-trip.
	Sint64 rerollCost, hullCost, bombCost, extraPerkCost, cleanseCost, shopEntryCash;
	Sint32 extraPerksBought, extraPerksVisit;
	Uint32 purchasedMods;
	Sint32 buffKind, cleanseCharges;
	Uint8  gamblePerkWon, perkPending;
	char   gambleMsg[48];
	char   lastSpecialName[31];

	// Perk offer.
	Sint32 perkChoiceN;
	Sint32 perkChoice[ENDLESS_SAVE_OFFERS];  // v13: was 3 (read narrow from v3-v12 files)

	// Course offer.
	Sint32 courseCnt;
	Sint32 courseEp[ENDLESS_MAX_COURSES];
	Uint8  courseSec[ENDLESS_MAX_COURSES];
	Uint8  courseFile[ENDLESS_MAX_COURSES];  // v8: exact binary level file for each saved course
	Uint64 courseMod[ENDLESS_MAX_COURSES];   // v7: was Uint32 (read narrow from v3-v6 files)
	Sint32 lastEp;
	Uint8  lastSec, forced;

	// Shop stock.
	Uint8  itemAvail[9][10];
	Uint8  itemAvailMax[9];

	// Added in v3.
	char   seed[ENDLESS_SEED_MAXLEN];

	// Locked sortie, added in v4.
	Uint8  lockedSortie;  // 1 = this save reopens the locked retry outpost (else a normal outpost)
	Uint64 sortieMods;    // endlessActiveMods of the committed level (v7: was Uint32)
	Uint8  sortieSec;     // committed level section
	Sint32 sortieEp;      // committed episode
	Uint8  sortieFile;    // committed lvl file number

	// Added in v5.
	Sint32 buffCooldownUntil;  // run depth at which the E-Shop kill-fire buys unlock again (0 = no lock)

	// Recent levels, added in v6.
	Uint8  recentCount;
	Sint32 recentEp[ENDLESS_LEVEL_HISTORY];
	Uint8  recentSec[ENDLESS_LEVEL_HISTORY];

	// Added in v9.
	Uint8  creditsShown;  // 1 = this run has already rolled the credits, so resuming won't replay them

	// Music continuity, added in v10.
	Uint8  lastSong;       // the track the last-played zone really used (0 = none yet)
	Sint32 lastSongDepth;  // that zone's run depth (only meaningful when lastSong != 0)

	// Deferred boons, added in v12.
	Uint8  starChartsOwed;    // STAR CHARTS: the next ordinary chart still owes its full route slate
	Uint8  breakthroughOwed;  // bonus perk picks still owed; two can queue

	// Added in v15.
	Uint8  runMode;  // EndlessRunMode: the run's Relaxed/Standard/Hardcore choice (never Hardcore on disk)

	// Added in v16 (which stored this one field alone), widened in v17.
	Uint64 cashEarned;  // running total of cash taken in, for the run-over tally
	Uint64 cashSpent;   // running total spent
	Uint64 cashBySource[ENDLESS_SAVE_CASH_SOURCES];  // the earnings breakdown, indexed by EndlessCashSource

	// Added in v19 (v18 briefly stored only the gear sink as a single field).
	Uint64 cashBySink[ENDLESS_SAVE_CASH_SINKS];  // the spending breakdown, indexed by EndlessCashSink

	// Added in v20.
	Uint8  usedCustom;  // 1 = the run has cleared a zone firing the custom weapon

	/* Added in v21 for online co-op. Slot 0 is the run's only player outside co-op, and a v20
	 * record loads into it, so a single-player run resumes exactly just as it used to. */
	Uint8  coopHostCharts;   // Alternating course picks: is the host charting the next one?
	Uint8  courseChooser;    // EndlessCourseChooser the run was started under
	Sint32 armorBonus2;      // player 2's Reinforce tier
	Sint32 revivesUsed2, shopTax2, longCon2, buffKind2, buffCharge2, buffCooldownUntil2;
	Sint64 rerollCost2, hullCost2, bombCost2, extraPerkCost2, cleanseCost2, shopEntryCash2;
	Sint32 extraPerksBought2, extraPerksVisit2;
	Sint32 superbombs2, cleanseCharges2;
	Uint32 purchasedMods2;
	Uint8  reviveHeld2, gambleRigged2, downed[2];
	Uint8  gamblePerkWon2;
	char   gambleMsg2[48];
	char   lastSpecialName2[31];
	Uint8  perkTakenBy[2][ENDLESS_SAVE_PERKS];   // who picked what; perks are personal, so this IS
	                                             // each ship's holding (perkOwned is the legacy sum)
	Uint64 playerRng[2];                          // each player's own outpost draw stream

	/* Added in v22 as a Same/Varied flag; v24 widened it to an EndlessBaseRule. Values 0 and 1 kept
	 * their meaning, so a v22 or v23 record needs no migration. Fixed for the run. */
	Uint8  baseLevelRule;

	// Added in v23.
	Uint8  chartRerolls;     // Radar rerolls this outpost's chart has had; salts the visit's phases
	Uint8  chartStarCharts;  // Star Charts as the visit's chart found it, so a redeal replays it

	// Added in v24. Both are 0 under the two unshuffled rules.
	Uint32 shuffleNext;       // pieces a Shuffle run has drawn
	Uint32 shuffleHandStart;  // where the restored chart's hand came off, for the co-op re-anchor

	/* Added in v27: the partner's half of the outpost this save checkpointed, as their machine
	 * reported it over the save acknowledgement. Valid 0 means the checkpoint had no answer. */
	Uint8  partnerValid;
	Uint8  partnerSeat;              // player index the half belongs to
	Uint8  partnerAvailMax[9];
	Uint8  partnerAvail[9][10];
	Uint64 partnerRng;
} EndlessSlotRec;

// One record per save slot, read with and written beside saveFiles[] (config.c owns the file).
static EndlessSlotRec endlessSlotCache[SAVE_FILES_NUM];

// Restore a chart and migrate records that predate persisted courseFile values.
// Drop invalid entries; regenerate deterministically if none remain.
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
		JE_byte file;
		if (!endlessResolveCourseFile(r->courseEp[i], r->courseSec[i], r->courseFile[i], &file))
		{
			fprintf(stderr, "warning: dropping invalid saved course episode %d section %u\n",
			        r->courseEp[i], (unsigned int)r->courseSec[i]);
			dropped = true;
			continue;
		}
		endlessCourseEp[restoredCount] = r->courseEp[i];
		endlessCourseSec[restoredCount] = r->courseSec[i];
		endlessCourseFile[restoredCount] = file;
		// Charts dealt by an older build can carry a tier bit a stronger one already covers.
		endlessCourseMod[restoredCount] = endlessCanonicalMods(r->courseMod[i]);
		++restoredCount;
	}
	endlessCourseCnt = restoredCount;

	// Rebuild an invalid forced visit rather than turning another saved option into an Ambush.
	// The redeal uses the restored reroll count, so it rebuilds the chart the save was on.
	if (endlessCourseCnt == 0 || (endlessForced && dropped))
		endlessChartRedeal();

	endlessNameCourseBaseLevels();  // populate the Radar perk's base-level cache for the restored chart
}

/* Text codec: one config section per record, every field under a name. Absent keys read as zero,
 * unknown keys are ignored, and lists are space-separated numbers, so a hand edit or a build that
 * knows one field more or less parses the rest of the record unharmed. The wire
 * (endlessRunSerialize) carries this same text. */

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

/* Each player's own half. The record keeps player two's fields under separate names for the frozen
 * legacy reader's sake; the text codec addresses both halves the same way. */
typedef struct {
	Sint32 *armorBonus, *revivesUsed, *shopTax, *longCon, *buffKind, *buffCharge, *buffCooldownUntil;
	Sint32 *superbombs, *cleanseCharges, *extraPerksBought, *extraPerksVisit;
	Sint64 *rerollCost, *hullCost, *bombCost, *extraPerkCost, *cleanseCost, *shopEntryCash;
	Uint32 *purchasedMods;
	Uint8  *reviveHeld, *gambleRigged, *downed, *gamblePerkWon;
	Uint8  *perks;
	Uint64 *rng;
	char   *gambleMsg, *lastSpecialName;
} EndlessRecPlayer;

static EndlessRecPlayer endlessRecPlayerView(EndlessSlotRec *r, uint p)
{
	EndlessRecPlayer v;
	if (p == 0)
	{
		v.armorBonus = &r->armorBonus; v.revivesUsed = &r->revivesUsed; v.shopTax = &r->shopTax;
		v.longCon = &r->longCon; v.buffKind = &r->buffKind; v.buffCharge = &r->buffCharge;
		v.buffCooldownUntil = &r->buffCooldownUntil; v.superbombs = &r->superbombs;
		v.cleanseCharges = &r->cleanseCharges; v.rerollCost = &r->rerollCost; v.hullCost = &r->hullCost;
		v.bombCost = &r->bombCost; v.extraPerkCost = &r->extraPerkCost; v.cleanseCost = &r->cleanseCost;
		v.shopEntryCash = &r->shopEntryCash; v.purchasedMods = &r->purchasedMods;
		v.reviveHeld = &r->reviveHeld; v.gambleRigged = &r->gambleRigged; v.downed = &r->downed[0];
		v.gamblePerkWon = &r->gamblePerkWon; v.perks = r->perkTakenBy[0]; v.rng = &r->playerRng[0];
		v.extraPerksBought = &r->extraPerksBought; v.extraPerksVisit = &r->extraPerksVisit;
		v.gambleMsg = r->gambleMsg; v.lastSpecialName = r->lastSpecialName;
	}
	else
	{
		v.armorBonus = &r->armorBonus2; v.revivesUsed = &r->revivesUsed2; v.shopTax = &r->shopTax2;
		v.longCon = &r->longCon2; v.buffKind = &r->buffKind2; v.buffCharge = &r->buffCharge2;
		v.buffCooldownUntil = &r->buffCooldownUntil2; v.superbombs = &r->superbombs2;
		v.cleanseCharges = &r->cleanseCharges2; v.rerollCost = &r->rerollCost2; v.hullCost = &r->hullCost2;
		v.bombCost = &r->bombCost2; v.extraPerkCost = &r->extraPerkCost2; v.cleanseCost = &r->cleanseCost2;
		v.shopEntryCash = &r->shopEntryCash2; v.purchasedMods = &r->purchasedMods2;
		v.reviveHeld = &r->reviveHeld2; v.gambleRigged = &r->gambleRigged2; v.downed = &r->downed[1];
		v.gamblePerkWon = &r->gamblePerkWon2; v.perks = r->perkTakenBy[1]; v.rng = &r->playerRng[1];
		v.extraPerksBought = &r->extraPerksBought2; v.extraPerksVisit = &r->extraPerksVisit2;
		v.gambleMsg = r->gambleMsg2; v.lastSpecialName = r->lastSpecialName2;
	}
	return v;
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
		const EndlessRecPlayer v = endlessRecPlayerView((EndlessSlotRec *)r, p);
		char k[48];
		#define PK(name) endlessPlayerKey(k, sizeof(k), p, name)
		endlessPutInt(s, PK("armor_bonus"), *v.armorBonus);
		endlessPutInt(s, PK("revives_used"), *v.revivesUsed);
		endlessPutInt(s, PK("shop_tax"), *v.shopTax);
		endlessPutInt(s, PK("long_con"), *v.longCon);
		endlessPutInt(s, PK("buff_kind"), *v.buffKind);
		endlessPutInt(s, PK("buff_charge"), *v.buffCharge);
		endlessPutInt(s, PK("buff_cooldown_until"), *v.buffCooldownUntil);
		endlessPutInt(s, PK("superbombs"), *v.superbombs);
		endlessPutInt(s, PK("cleanse_charges"), *v.cleanseCharges);
		endlessPutInt(s, PK("reroll_cost"), *v.rerollCost);
		endlessPutInt(s, PK("hull_cost"), *v.hullCost);
		endlessPutInt(s, PK("bomb_cost"), *v.bombCost);
		endlessPutInt(s, PK("extra_perk_cost"), *v.extraPerkCost);
		endlessPutInt(s, PK("extra_perks_bought"), *v.extraPerksBought);
		endlessPutInt(s, PK("extra_perks_visit"), *v.extraPerksVisit);
		endlessPutInt(s, PK("cleanse_cost"), *v.cleanseCost);
		endlessPutInt(s, PK("shop_entry_cash"), *v.shopEntryCash);
		endlessPutHex(s, PK("purchased_mods"), *v.purchasedMods);
		endlessPutInt(s, PK("revive_held"), *v.reviveHeld);
		endlessPutInt(s, PK("gamble_rigged"), *v.gambleRigged);
		endlessPutInt(s, PK("downed"), *v.downed);
		endlessPutInt(s, PK("gamble_perk_won"), *v.gamblePerkWon);
		endlessPutList(s, PK("perks"), v.perks, PERK_COUNT, 1, false);
		endlessPutList(s, PK("rng"), v.rng, 1, 8, false);
		config_set_string_option(s, PK("gamble_message"), v.gambleMsg);
		config_set_string_option(s, PK("last_special"), v.lastSpecialName);
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
		const EndlessRecPlayer v = endlessRecPlayerView(r, p);
		char k[48];
		#define PK(name) endlessPlayerKey(k, sizeof(k), p, name)
		*v.armorBonus = (Sint32)endlessGetInt(s, PK("armor_bonus"), 0);
		*v.revivesUsed = (Sint32)endlessGetInt(s, PK("revives_used"), 0);
		*v.shopTax = (Sint32)endlessGetInt(s, PK("shop_tax"), 0);
		*v.longCon = (Sint32)endlessGetInt(s, PK("long_con"), 0);
		*v.buffKind = (Sint32)endlessGetInt(s, PK("buff_kind"), 0);
		*v.buffCharge = (Sint32)endlessGetInt(s, PK("buff_charge"), 0);
		*v.buffCooldownUntil = (Sint32)endlessGetInt(s, PK("buff_cooldown_until"), 0);
		*v.superbombs = (Sint32)endlessGetInt(s, PK("superbombs"), 0);
		*v.cleanseCharges = (Sint32)endlessGetInt(s, PK("cleanse_charges"), 0);
		// Prices and the entry wallet are wallet-sized: a hand edit past either end is clamped.
		*v.rerollCost = cash_clamp(endlessGetInt(s, PK("reroll_cost"), 0));
		*v.hullCost = cash_clamp(endlessGetInt(s, PK("hull_cost"), 0));
		*v.bombCost = cash_clamp(endlessGetInt(s, PK("bomb_cost"), 0));
		*v.extraPerkCost = cash_clamp(endlessGetInt(s, PK("extra_perk_cost"), 0));
		// A save written before the extra perk was priced by count carries neither key and resumes
		// at zero bought, which is the forgiving direction.
		*v.extraPerksBought = (Sint32)endlessGetInt(s, PK("extra_perks_bought"), 0);
		*v.extraPerksVisit = (Sint32)endlessGetInt(s, PK("extra_perks_visit"), 0);
		*v.cleanseCost = cash_clamp(endlessGetInt(s, PK("cleanse_cost"), 0));
		*v.shopEntryCash = cash_clamp(endlessGetInt(s, PK("shop_entry_cash"), 0));
		*v.purchasedMods = (Uint32)endlessGetHex(s, PK("purchased_mods"));
		*v.reviveHeld = endlessGetInt(s, PK("revive_held"), 0) != 0;
		*v.gambleRigged = endlessGetInt(s, PK("gamble_rigged"), 0) != 0;
		*v.downed = endlessGetInt(s, PK("downed"), 0) != 0;
		*v.gamblePerkWon = endlessGetInt(s, PK("gamble_perk_won"), 0) != 0;
		endlessGetList(s, PK("perks"), v.perks, PERK_COUNT, 1, false);
		endlessGetList(s, PK("rng"), v.rng, 1, 8, false);
		endlessGetStr(s, PK("gamble_message"), v.gambleMsg, sizeof(r->gambleMsg));
		endlessGetStr(s, PK("last_special"), v.lastSpecialName, sizeof(r->lastSpecialName));
		#undef PK
	}
	memcpy(r->perkOwned, r->perkTakenBy[0], ENDLESS_SAVE_PERKS);

	// Enum and cursor values index tables; a hand edit past their range falls back like the legacy
	// reader's did. Counts and stacks are clamped where the record is applied.
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

/* The frozen legacy binary reader for endless.sav (v3..v27), kept only to migrate a file an older
 * build left behind. Little-endian fields; a short read invalidates the record. */
#define ENDLESS_LEGACY_REC_MAX  4096            // ceiling for one record
#define ENDLESS_LEGACY_FILE_MAX (1024 * 1024)   // ...and for a whole sidecar file

typedef struct { const Uint8 *p, *end; } EndlessReader;

static bool endlessGetU8(EndlessReader *rd, Uint8 *v)                  { if (rd->p >= rd->end) return false; *v = *rd->p++; return true; }
static bool endlessGetBytes(EndlessReader *rd, void *p, size_t n)      { if ((size_t)(rd->end - rd->p) < n) return false; memcpy(p, rd->p, n); rd->p += n; return true; }
static bool endlessGetU32(EndlessReader *rd, Uint32 *v)                { Uint32 b; if (!endlessGetBytes(rd, &b, 4)) return false; *v = SDL_SwapLE32(b); return true; }
static bool endlessGetU64(EndlessReader *rd, Uint64 *v)                { Uint64 b; if (!endlessGetBytes(rd, &b, 8)) return false; *v = SDL_SwapLE64(b); return true; }

static bool endlessLegacyReadRec(EndlessReader *rd, EndlessSlotRec *r, int version)
{
	memset(r, 0, sizeof(*r));

	Uint8 used;
	if (!endlessGetU8(rd, &used))
		return false;
	r->used = used != 0;

	// The 21 leading Sint32 fields, in the order the binary record laid them out.
	Sint32 v[21];
	for (unsigned i = 0; i < COUNTOF(v); ++i)
	{
		Uint32 t;
		if (!endlessGetU32(rd, &t))
			return false;
		v[i] = (Sint32)t;
	}
	r->runDepth = v[0];      r->armorBonus = v[1];     r->runKills = v[2];        r->runBossKills = v[3];
	r->buffCharge = v[4];    r->revivesUsed = v[5];    r->shopTax = v[6];         r->longCon = v[7];
	r->perkDepthDone = v[8]; r->superbombs = v[9];
	// Prices and the entry wallet are unsigned 32-bit amounts in the record's Sint32 slots.
	r->rerollCost = (Uint32)v[10];    r->hullCost = (Uint32)v[11];      r->bombCost = (Uint32)v[12];
	r->extraPerkCost = (Uint32)v[13]; r->cleanseCost = (Uint32)v[14];   r->shopEntryCash = (Uint32)v[15];
	r->buffKind = v[16];     r->cleanseCharges = v[17]; r->perkChoiceN = v[18];   r->courseCnt = v[19];
	r->lastEp = v[20];

	if (!endlessGetU32(rd, &r->purchasedMods)
	    || !endlessGetU8(rd, &r->reviveHeld) || !endlessGetU8(rd, &r->gambleRigged)
	    || !endlessGetU8(rd, &r->gamblePerkWon) || !endlessGetU8(rd, &r->perkPending)
	    || !endlessGetU8(rd, &r->lastSec) || !endlessGetU8(rd, &r->forced))
		return false;

	// v11 widened the perk block. Zero-filled newer slots remain unowned in older records.
	const size_t perkBytes = (version >= 11) ? ENDLESS_SAVE_PERKS : ENDLESS_SAVE_PERKS_V10;
	if (!endlessGetBytes(rd, r->perkOwned, perkBytes)
	    || !endlessGetBytes(rd, r->gambleMsg, sizeof(r->gambleMsg))
	    || !endlessGetBytes(rd, r->lastSpecialName, sizeof(r->lastSpecialName)))
		return false;

	// v13 widened perk offers from three to five. Clamp counts to the version's stored width.
	const unsigned offerSlots = (version >= 13) ? ENDLESS_SAVE_OFFERS : ENDLESS_SAVE_OFFERS_V12;
	for (unsigned i = 0; i < offerSlots; ++i)
	{
		Uint32 t;
		if (!endlessGetU32(rd, &t))
			return false;
		r->perkChoice[i] = (Sint32)t;
	}
	if (r->perkChoiceN > (Sint32)offerSlots)
		r->perkChoiceN = (Sint32)offerSlots;

	// v14 removed Rapid Charger. Merge its stacks into Rapid Recharge and close
	// the resulting ID gap in owned perks and saved offers.
	if (version < 14)
	{
		const int merged = r->perkOwned[PERK_SPECIALCD] + r->perkOwned[ENDLESS_SAVE_PERK_CHARGER_V13];
		r->perkOwned[PERK_SPECIALCD] = (Uint8)(merged > 255 ? 255 : merged);
		memmove(&r->perkOwned[ENDLESS_SAVE_PERK_CHARGER_V13],
		        &r->perkOwned[ENDLESS_SAVE_PERK_CHARGER_V13 + 1],
		        ENDLESS_SAVE_PERKS - ENDLESS_SAVE_PERK_CHARGER_V13 - 1);
		r->perkOwned[ENDLESS_SAVE_PERKS - 1] = 0;

		Sint32 kept = 0;
		for (Sint32 i = 0; i < r->perkChoiceN; ++i)
		{
			const Sint32 id = r->perkChoice[i];
			if (id == ENDLESS_SAVE_PERK_CHARGER_V13)
				continue;
			r->perkChoice[kept++] = (id > ENDLESS_SAVE_PERK_CHARGER_V13) ? id - 1 : id;
		}
		r->perkChoiceN = kept;   // 0 is fine: the pick menu then opens on "Take the Cash" alone
	}

	for (unsigned i = 0; i < ENDLESS_MAX_COURSES; ++i)
	{
		Uint32 t;
		if (!endlessGetU32(rd, &t))
			return false;
		r->courseEp[i] = (Sint32)t;
	}
	for (unsigned i = 0; i < ENDLESS_MAX_COURSES; ++i)
	{
		if (version >= 7)
		{
			if (!endlessGetU64(rd, &r->courseMod[i]))
				return false;
		}
		else
		{
			Uint32 t;   // v3-v6 stored the course mods 32-bit (high bits were unused back then)
			if (!endlessGetU32(rd, &t))
				return false;
			r->courseMod[i] = t;
		}
	}
	if (!endlessGetBytes(rd, r->courseSec, ENDLESS_MAX_COURSES))
		return false;
	if (version >= 8 && !endlessGetBytes(rd, r->courseFile, ENDLESS_MAX_COURSES))
		return false;

	if (!endlessGetBytes(rd, r->itemAvail, sizeof(r->itemAvail))
	    || !endlessGetBytes(rd, r->itemAvailMax, sizeof(r->itemAvailMax))
	    || !endlessGetBytes(rd, r->seed, sizeof(r->seed)))
		return false;

	// Never trust a terminator off disk.
	r->gambleMsg[sizeof(r->gambleMsg) - 1] = '\0';
	r->lastSpecialName[sizeof(r->lastSpecialName) - 1] = '\0';
	r->seed[sizeof(r->seed) - 1] = '\0';

	// v3 records have no locked-sortie block and remain unlocked after zero-initialization.
	if (version >= 4)
	{
		Uint8  u8;
		Uint32 u32;
		if (!endlessGetU8(rd, &u8))
			return false;
		r->lockedSortie = u8;
		if (version >= 7)   // v7 widened sortieMods to 64-bit; v4-v6 stored it 32-bit
		{
			Uint64 u64;
			if (!endlessGetU64(rd, &u64))
				return false;
			r->sortieMods = u64;
		}
		else
		{
			if (!endlessGetU32(rd, &u32))
				return false;
			r->sortieMods = u32;
		}
		if (!endlessGetU8(rd, &u8))
			return false;
		r->sortieSec = u8;
		if (!endlessGetU32(rd, &u32))
			return false;
		r->sortieEp = (Sint32)u32;
		if (!endlessGetU8(rd, &u8))
			return false;
		r->sortieFile = u8;
	}

	// Pre-v5 records leave the kill-fire purchase cooldown unlocked.
	if (version >= 5)
	{
		Uint32 u32;
		if (!endlessGetU32(rd, &u32))
			return false;
		r->buffCooldownUntil = (Sint32)u32;
	}

	// Pre-v6 records resume with an empty recent-level window.
	if (version >= 6)
	{
		if (!endlessGetU8(rd, &r->recentCount))
			return false;
		for (unsigned i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
		{
			Uint32 u32;
			if (!endlessGetU32(rd, &u32))
				return false;
			r->recentEp[i] = (Sint32)u32;
		}
		if (!endlessGetBytes(rd, r->recentSec, ENDLESS_LEVEL_HISTORY))
			return false;
		if (r->recentCount > ENDLESS_LEVEL_HISTORY)
			r->recentCount = ENDLESS_LEVEL_HISTORY;
	}

	// Pre-v9 records may show the zone-100 credits once after resuming.
	if (version >= 9 && !endlessGetU8(rd, &r->creditsShown))
		return false;

	// Pre-v10 records derive the previous song when lastSong remains zero.
	if (version >= 10)
	{
		Uint32 u32;
		if (!endlessGetU8(rd, &r->lastSong) || !endlessGetU32(rd, &u32))
			return false;
		r->lastSongDepth = (Sint32)u32;
	}

	// Pre-v12 records resume without deferred Star Charts or Breakthrough rewards.
	if (version >= 12
	    && (!endlessGetU8(rd, &r->starChartsOwed) || !endlessGetU8(rd, &r->breakthroughOwed)))
	{
		return false;
	}

	// Pre-v15 records resume in Relaxed mode, matching their original behavior.
	if (version >= 15)
	{
		if (!endlessGetU8(rd, &r->runMode))
			return false;
		if (r->runMode >= ENDLESS_RUNMODE_COUNT)
			r->runMode = ENDLESS_RUNMODE_RELAXED;
	}

	// v16 stores total earnings. v17 adds spending and per-source totals.
	if (version >= 16)
	{
		if (!endlessGetU64(rd, &r->cashEarned))
			return false;
		if (version >= 17)
		{
			if (!endlessGetU64(rd, &r->cashSpent))
				return false;
			for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SOURCES; ++i)
				if (!endlessGetU64(rd, &r->cashBySource[i]))
					return false;
		}
		else
		{
			// v16 stored total earnings only, so assign them to the untagged source.
			r->cashBySource[ENDLESS_CASH_OTHER] = r->cashEarned;
		}
	}

	// v18 stored only the gear sink; v19 stores every sink. Earlier versions remain zero-filled.
	if (version == 18)
	{
		if (!endlessGetU64(rd, &r->cashBySink[ENDLESS_SINK_GEAR]))
			return false;
	}
	else if (version >= 19)
	{
		for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SINKS; ++i)
			if (!endlessGetU64(rd, &r->cashBySink[i]))
				return false;
	}

	// Pre-v20 records resume unmarked, so their record only gains a C if the rest of the run earns it.
	if (version >= 20 && !endlessGetU8(rd, &r->usedCustom))
		return false;

	// Pre-v21 records are single-player: the zero-filled second slot leaves that player with
	// nothing bought, and the perk rows are rebuilt from the effective stacks below.
	if (version >= 21)
	{
		if (!endlessGetU8(rd, &r->coopHostCharts) || !endlessGetU8(rd, &r->courseChooser))
			return false;
		Sint32 c[15];   // the 15 co-op Sint32 fields, in record order
		for (unsigned i = 0; i < COUNTOF(c); ++i)
		{
			Uint32 t;
			if (!endlessGetU32(rd, &t))
				return false;
			c[i] = (Sint32)t;
		}
		r->armorBonus2 = c[0];    r->revivesUsed2 = c[1];   r->shopTax2 = c[2];      r->longCon2 = c[3];
		r->buffKind2 = c[4];      r->buffCharge2 = c[5];    r->buffCooldownUntil2 = c[6];
		r->rerollCost2 = (Uint32)c[7];     r->hullCost2 = (Uint32)c[8];     r->bombCost2 = (Uint32)c[9];
		r->extraPerkCost2 = (Uint32)c[10]; r->cleanseCost2 = (Uint32)c[11]; r->shopEntryCash2 = (Uint32)c[12];
		r->superbombs2 = c[13];   r->cleanseCharges2 = c[14];
		if (!endlessGetU32(rd, &r->purchasedMods2)
		    || !endlessGetU8(rd, &r->reviveHeld2) || !endlessGetU8(rd, &r->gambleRigged2)
		    || !endlessGetU8(rd, &r->downed[0]) || !endlessGetU8(rd, &r->downed[1])
		    || !endlessGetBytes(rd, r->perkTakenBy[0], ENDLESS_SAVE_PERKS)
		    || !endlessGetBytes(rd, r->perkTakenBy[1], ENDLESS_SAVE_PERKS)
		    || !endlessGetU64(rd, &r->playerRng[0]) || !endlessGetU64(rd, &r->playerRng[1]))
			return false;
		if (r->courseChooser >= ENDLESS_PICK_COUNT)
			r->courseChooser = ENDLESS_PICK_HOST;
	}
	else
	{
		r->coopHostCharts = 1;
		memcpy(r->perkTakenBy[0], r->perkOwned, ENDLESS_SAVE_PERKS);
	}

	// Pre-v22 records predate the chart rule, so they resume as Varied and keep the records they set.
	if (version >= 22 && !endlessGetU8(rd, &r->baseLevelRule))
		return false;
	if (r->baseLevelRule >= ENDLESS_BASE_RULE_COUNT)
		r->baseLevelRule = ENDLESS_BASE_VARIED;

	// Pre-v23 records predate the Radar chart reroll and resume with this visit's reroll unspent.
	if (version >= 23)
	{
		if (!endlessGetU8(rd, &r->chartRerolls) || !endlessGetU8(rd, &r->chartStarCharts))
			return false;
	}
	else
	{
		// What the visit's chart opened with is not recorded, so take what it still owes: a redeal
		// of an older save then spends the boon again rather than dropping it.
		r->chartStarCharts = r->starChartsOwed;
	}

	// Pre-v24 records predate the Shuffle rules, so their bag is genuinely at the start.
	if (version >= 24
	    && (!endlessGetU32(rd, &r->shuffleNext) || !endlessGetU32(rd, &r->shuffleHandStart)))
	{
		return false;
	}
	// A corrupt cursor restarts the bag rather than indexing off the end, and a hand cannot have
	// come off a position the cursor has not reached.
	if (r->shuffleNext > ENDLESS_SHUFFLE_POSITION_MAX)
		r->shuffleNext = 0;
	if (r->shuffleHandStart > r->shuffleNext)
		r->shuffleHandStart = r->shuffleNext;

	// Pre-v27 records checkpointed no partner half.
	if (version >= 27)
	{
		if (!endlessGetU8(rd, &r->partnerValid) || !endlessGetU8(rd, &r->partnerSeat)
		    || !endlessGetBytes(rd, r->partnerAvailMax, sizeof(r->partnerAvailMax))
		    || !endlessGetBytes(rd, r->partnerAvail, sizeof(r->partnerAvail))
		    || !endlessGetU64(rd, &r->partnerRng))
		{
			return false;
		}
		if (r->partnerSeat > 1)
			r->partnerValid = 0;
	}

	return true;
}

int endlessSaveLegacyVersionMax(void)
{
	return ENDLESS_LEGACY_VERSION_MAX;
}

/* A file past v27 came from a build that appended fields the frozen reader does not know. Its
 * header still states the record width, so the v27 prefix of every record is where it always was:
 * read that and skip the tail (endlessLegacyReadOneRec). Only a file with no width is refused. */
static bool endlessLegacyReadHeader(EndlessReader *rd, EndlessSaveHeader *h)
{
	Uint8 tag[6];
	if (!endlessGetBytes(rd, tag, sizeof(tag)) || memcmp(tag, "OTES", 4) != 0
	    || tag[4] < 3 || tag[5] < 1 || tag[5] > SAVE_FILES_NUM)
	{
		return false;
	}
	h->version = tag[4];
	h->slots   = tag[5];
	h->width   = 0;   // before v25 a record is exactly what its own version parses

	if (h->version >= ENDLESS_LEGACY_WIDTH_VERSION)
	{
		Uint8 lo, hi;
		if (!endlessGetU8(rd, &lo) || !endlessGetU8(rd, &hi))
			return false;
		h->width = lo | (hi << 8);
		if (h->width < 1 || h->width > ENDLESS_LEGACY_REC_MAX)
			return false;
	}
	if (h->version > ENDLESS_LEGACY_VERSION_MAX)
	{
		if (h->width == 0)
			return false;
		fprintf(stderr, "warning: %s is format %d, newer than the v%d importer; reading the fields it knows\n",
		        ENDLESS_LEGACY_SAVE_FILE, h->version, ENDLESS_LEGACY_VERSION_MAX);
		h->version = ENDLESS_LEGACY_VERSION_MAX;
	}
	return true;
}

/* Take one record off the cursor. A file that states its record width is parsed from a zero-padded
 * copy of exactly that many bytes, so a record written narrower or wider than this reader expects
 * still leaves the next slot where the file says it starts. */
static bool endlessLegacyReadOneRec(EndlessReader *rd, const EndlessSaveHeader *h, EndlessSlotRec *rec)
{
	if (h->width == 0)
		return endlessLegacyReadRec(rd, rec, h->version);
	if ((size_t)(rd->end - rd->p) < (size_t)h->width)
		return false;

	Uint8 buf[ENDLESS_LEGACY_REC_MAX];
	memcpy(buf, rd->p, (size_t)h->width);
	memset(buf + h->width, 0, sizeof(buf) - (size_t)h->width);
	rd->p += h->width;

	EndlessReader one = { buf, buf + sizeof(buf) };
	return endlessLegacyReadRec(&one, rec, h->version);
}

// Decode the first record of a legacy file image; the fixture test and the migration share it.
static bool endlessLegacyDecode(const Uint8 *bytes, size_t size, EndlessSlotRec *rec, int *version)
{
	EndlessReader rd = { bytes, bytes + size };
	EndlessSaveHeader h;
	if (bytes == NULL || !endlessLegacyReadHeader(&rd, &h) || !endlessLegacyReadOneRec(&rd, &h, rec))
		return false;
	if (version != NULL)
		*version = h.version;
	return true;
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

/* Prove a legacy fixture still migrates: the sentinels the generator baked in come out, the record
 * then survives the text codec byte-for-byte, and no truncated or random legacy image is accepted. */
bool endlessSaveTestFixture(const char *path, char *detail, size_t detailSize)
{
	if (detail != NULL && detailSize != 0)
		detail[0] = '\0';

	FILE *f = fopen(path, "rb");
	if (f == NULL)
	{
		endlessTestDetail(detail, detailSize, "fixture missing");
		return false;
	}
	fseek(f, 0, SEEK_END);
	const long fileSize = ftell(f);
	rewind(f);
	if (fileSize <= 0 || fileSize > 1024 * 1024)
	{
		fclose(f);
		endlessTestDetail(detail, detailSize, "fixture size invalid");
		return false;
	}
	Uint8 *bytes = malloc((size_t)fileSize);
	if (bytes == NULL || fread(bytes, 1, (size_t)fileSize, f) != (size_t)fileSize)
	{
		free(bytes);
		fclose(f);
		endlessTestDetail(detail, detailSize, "fixture read failed");
		return false;
	}
	fclose(f);

	EndlessSlotRec first, second;
	int version = 0;
	if (!endlessLegacyDecode(bytes, (size_t)fileSize, &first, &version))
	{
		free(bytes);
		endlessTestDetail(detail, detailSize, "fixture did not decode");
		return false;
	}

	/* Independent fixture sentinels, including the v14 removed-perk migration. */
	if (!first.used || first.runDepth != 42 || first.armorBonus != 7
	    || strncmp(first.seed, "fixture-v", 9) != 0
	    || first.perkOwned[PERK_SPECIALCD] != 5 || first.perkOwned[14] != 4
	    || first.perkChoiceN != 2 || first.perkChoice[0] != 14 || first.perkChoice[1] != 2
	    || first.courseCnt != 1 || first.courseEp[0] != 1 || first.courseSec[0] != 1)
	{
		free(bytes);
		endlessTestDetail(detail, detailSize, "migration sentinels differ");
		return false;
	}
	if ((version < 8 && first.courseFile[0] != 0)
	    || (version >= 8 && first.courseFile[0] != 1)
	    || (version < 15 && first.runMode != ENDLESS_RUNMODE_RELAXED)
	    || (version >= 15 && first.runMode != ENDLESS_RUNMODE_STANDARD)
	    || (version < 20 && first.usedCustom != 0)
	    || (version >= 20 && first.usedCustom != 1)
	    || (version < 22 && first.baseLevelRule != 0)
	    || (version >= 22 && first.baseLevelRule != 1)
	    || (version < 23 && first.chartRerolls != 0)
	    || (version >= 23 && first.chartRerolls != 1)
	    || (version < 24 && (first.shuffleNext != 0 || first.shuffleHandStart != 0))
	    || (version >= 24 && (first.shuffleNext != 37 || first.shuffleHandStart != 33))
	    || (version < 27 && first.partnerValid != 0)
	    || (version >= 27 && (first.partnerValid != 1 || first.partnerSeat != 1
	        || first.partnerAvailMax[0] != 2 || first.partnerAvail[0][0] != 90
	        || first.partnerRng != 0x1122334455667788ull)))
	{
		free(bytes);
		endlessTestDetail(detail, detailSize, "version defaults differ");
		return false;
	}

	char *encoded1 = NULL, *encoded2 = NULL;
	size_t encoded1Size = 0, encoded2Size = 0;
	if (!endlessTextEncode(&first, &encoded1, &encoded1Size)
	    || !endlessTextDecode(encoded1, encoded1Size, &second)
	    || !endlessTextEncode(&second, &encoded2, &encoded2Size)
	    || encoded1Size != encoded2Size || memcmp(encoded1, encoded2, encoded1Size) != 0)
	{
		free(bytes); free(encoded1); free(encoded2);
		endlessTestDetail(detail, detailSize, "text round trip is unstable");
		return false;
	}
	free(encoded1);
	free(encoded2);

	/* Every strict prefix of the legacy image must fail cleanly. */
	for (size_t cut = 0; cut < (size_t)fileSize; ++cut)
	{
		EndlessSlotRec junk;
		if (endlessLegacyDecode(bytes, cut, &junk, NULL))
		{
			free(bytes);
			endlessTestDetail(detail, detailSize, "truncated fixture was accepted");
			return false;
		}
	}
	/* Oversized and randomized inputs exercise all length guards without trusting their contents. */
	const size_t oversizedSize = (size_t)fileSize + 65536;
	Uint8 *oversized = malloc(oversizedSize);
	if (oversized == NULL)
	{
		free(bytes);
		endlessTestDetail(detail, detailSize, "fuzz allocation failed");
		return false;
	}
	memcpy(oversized, bytes, (size_t)fileSize);
	memset(oversized + fileSize, 0xa5, oversizedSize - (size_t)fileSize);
	EndlessSlotRec oversizedRec;
	if (!endlessLegacyDecode(oversized, oversizedSize, &oversizedRec, NULL))
	{
		free(oversized); free(bytes);
		endlessTestDetail(detail, detailSize, "oversized input damaged the valid prefix");
		return false;
	}
	free(oversized);

	Uint32 rng = 0x45534f54u ^ (Uint32)version;
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
		(void)endlessLegacyDecode(fuzz, n, &junk, NULL);
		(void)endlessTextDecode((const char *)fuzz, n, &junk);
	}

	free(bytes);
	return true;
}

/* Prove the text codec on a record this build made: every field survives a round trip, a key the
 * reader does not know is ignored, a key it does not find reads as its zero, and a hand-edited
 * value outside the wallet range is clamped. */
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
	rec.shopEntryCash = 4000000000LL;
	rec.rerollCost2 = 123456789012LL;
	rec.cashEarned = 5000000000ull;
	rec.cashBySink[ENDLESS_SINK_GEAR] = 77;
	rec.perkTakenBy[1][PERK_SPECIALCD] = 2;
	rec.playerRng[0] = 0xF00DF00DF00DF00Dull;
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
	SDL_strlcpy(rec.gambleMsg2, "it's 'quoted'", sizeof(rec.gambleMsg2));

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
		if (back.runDepth != 91 || back.runKills != 0 || back.shopEntryCash != 0 || back.rerollCost2 != 0)
			fault = "an unknown, missing or malformed key was not defaulted";
	}

	if (fault != NULL)
		endlessTestDetail(detail, detailSize, fault);
	return fault == NULL;
}

/* opentyrian.sav carries one 'endless' section per slot with a run; config.c parses and writes the
 * file and hands the Config here for the Endless half. A slot without a section holds no run. */
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

/* Whether this session read the sidecar through. config.c records it in opentyrian.sav, so a save
 * file that has taken the sidecar in never consults it again, while one whose import failed
 * tries once more. */
static bool endlessLegacySidecarRead = false;

bool endlessSaveLegacyWasRead(void) { return endlessLegacySidecarRead; }

bool endlessSaveLegacyExists(void)
{
	return dir_file_exists(get_user_directory(), ENDLESS_LEGACY_SAVE_FILE);
}

/* Parse the binary sidecar an older build left behind into `out` (SAVE_FILES_NUM records, only the
 * slots whose campaign half holds a game). The file itself is left in place. */
static int endlessSaveLegacyParse(FILE *f, EndlessSlotRec *out)
{
	memset(out, 0, sizeof(*out) * SAVE_FILES_NUM);

	fseek(f, 0, SEEK_END);
	const long end = ftell(f);
	rewind(f);

	Uint8 *bytes = (end > 0 && end <= ENDLESS_LEGACY_FILE_MAX) ? malloc((size_t)end) : NULL;
	if (bytes != NULL && fread(bytes, 1, (size_t)end, f) != (size_t)end)
	{
		free(bytes);
		bytes = NULL;
	}
	if (bytes == NULL)
		return 0;

	EndlessReader rd = { bytes, bytes + end };
	EndlessSaveHeader h;
	int loaded = 0;
	if (endlessLegacyReadHeader(&rd, &h))
	{
		endlessLegacySidecarRead = true;
		for (int s = 0; s < h.slots; ++s)
		{
			EndlessSlotRec rec;
			if (!endlessLegacyReadOneRec(&rd, &h, &rec))
				break;  // truncated: keep the full records already read
			if (s < SAVE_FILES_NUM && saveFiles[s].level != 0)   // read after the campaign slots
				out[s] = rec;
			++loaded;
		}
	}
	free(bytes);
	return loaded;
}

static int endlessSaveLegacyParsePath(const char *dir, const char *path, EndlessSlotRec *out)
{
	FILE *f = (dir != NULL) ? dir_fopen(dir, path, "rb") : fopen(path, "rb");
	if (f == NULL)
		return 0;
	const int loaded = endlessSaveLegacyParse(f, out);
	fclose(f);
	return loaded;
}

// The first launch without opentyrian.sav: every slot's half comes from the sidecar.
bool endlessSaveLegacyLoad(void)
{
	const int loaded = endlessSaveLegacyParsePath(get_user_directory(), ENDLESS_LEGACY_SAVE_FILE, endlessSlotCache);
	if (loaded > 0)
		printf("Imported %d Endless slot records from the old %s.\n", loaded, ENDLESS_LEGACY_SAVE_FILE);
	return loaded > 0;
}

/* opentyrian.sav exists but a slot named for an Endless zone has no run behind it, and the old
 * sidecar still does: take that slot's half from there. This is the state an import that could not
 * read the sidecar leaves, and a run without its half replays one level. Returns whether any slot
 * was repaired. */
static bool endlessSaveRepairFrom(const char *dir, const char *path)
{
	int wanted = 0;
	for (int s = 0; s < SAVE_FILES_NUM; ++s)
		if (saveFiles[s].level != 0 && !endlessSlotCache[s].used
		    && strncmp(saveFiles[s].levelName, "ZONE ", 5) == 0)
			++wanted;
	if (wanted == 0)
		return false;

	EndlessSlotRec *legacy = calloc(SAVE_FILES_NUM, sizeof(*legacy));
	if (legacy == NULL)
		return false;
	int repaired = 0;
	if (endlessSaveLegacyParsePath(dir, path, legacy) > 0)
	{
		for (int s = 0; s < SAVE_FILES_NUM; ++s)
		{
			if (saveFiles[s].level == 0 || endlessSlotCache[s].used || !legacy[s].used
			    || strncmp(saveFiles[s].levelName, "ZONE ", 5) != 0)
				continue;
			endlessSlotCache[s] = legacy[s];
			++repaired;
		}
	}
	free(legacy);
	if (repaired > 0)
		printf("Restored %d Endless run(s) that %s was missing from the old %s.\n",
		       repaired, SAVE_FILE_NAME, ENDLESS_LEGACY_SAVE_FILE);
	return repaired > 0;
}

bool endlessSaveRepairFromLegacy(void)
{
	return endlessSaveRepairFrom(get_user_directory(), ENDLESS_LEGACY_SAVE_FILE);
}

bool endlessSaveLegacyTestImport(const char *path)
{
	return endlessSaveLegacyParsePath(NULL, path, endlessSlotCache) > 0;
}

bool endlessSaveLegacyTestRepair(const char *path)
{
	return endlessSaveRepairFrom(NULL, path);
}

/* A sidecar from a build past v27: the same record with fields appended and the header saying so.
 * Built here from the v27 fixture, since no such build is kept. */
bool endlessSaveTestNewerLegacy(const char *v27Path, char *detail, size_t detailSize)
{
	if (detail != NULL && detailSize != 0)
		detail[0] = '\0';

	FILE *f = fopen(v27Path, "rb");
	if (f == NULL)
	{
		endlessTestDetail(detail, detailSize, "v27 fixture missing");
		return false;
	}
	fseek(f, 0, SEEK_END);
	const long fileSize = ftell(f);
	rewind(f);
	const size_t extra = 112;   // fourteen 64-bit fields, as the v28 files seen in the wild carried
	Uint8 *bytes = (fileSize > ENDLESS_LEGACY_HEADER_BYTES && fileSize < 65536)
	             ? malloc((size_t)fileSize + extra) : NULL;
	if (bytes == NULL || fread(bytes, 1, (size_t)fileSize, f) != (size_t)fileSize)
	{
		free(bytes);
		fclose(f);
		endlessTestDetail(detail, detailSize, "v27 fixture read failed");
		return false;
	}
	fclose(f);

	const size_t width = (size_t)fileSize - ENDLESS_LEGACY_HEADER_BYTES + extra;
	bytes[4] = ENDLESS_LEGACY_VERSION_MAX + 1;
	bytes[6] = (Uint8)(width & 0xFF);
	bytes[7] = (Uint8)((width >> 8) & 0xFF);
	memset(bytes + fileSize, 0xa5, extra);

	EndlessSlotRec rec;
	const bool okay = endlessLegacyDecode(bytes, (size_t)fileSize + extra, &rec, NULL)
	               && rec.used && rec.runDepth == 42 && rec.armorBonus == 7
	               && rec.partnerValid == 1 && rec.partnerRng == 0x1122334455667788ull;
	free(bytes);
	if (!okay)
		endlessTestDetail(detail, detailSize, "the v27 prefix of a newer record did not import");
	return okay;
}

/* The partner's half of a save, as their machine reported it over the save acknowledgement:
 * their stock rows and the stream position they came off. Cleared when a new visit deals, so
 * a stale half cannot ride a later save; see "Online saves" in doc/notes.md. */
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
	int n = 0;
	memcpy(&buf[n], itemAvailMax, sizeof(itemAvailMax));
	n += (int)sizeof(itemAvailMax);
	memcpy(&buf[n], itemAvail, sizeof(itemAvail));
	n += (int)sizeof(itemAvail);

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
	r->armorBonus    = endlessArmorBonus[0];
	r->runKills      = endlessRunKills;
	r->runBossKills  = endlessRunBossKills;
	r->buffCharge    = endlessBuffCharge[0];
	r->buffCooldownUntil = endlessBuffCooldownUntil[0];
	r->revivesUsed   = endlessRevivesUsed[0];
	r->shopTax       = endlessShopTax[0];
	r->longCon       = endlessLongCon[0];
	r->perkDepthDone = endlessPerkDepthDone;
	r->superbombs    = player[0].superbombs;
	r->reviveHeld    = endlessReviveHeld[0] ? 1 : 0;
	r->gambleRigged  = endlessGambleRigged[0] ? 1 : 0;
	for (int i = 0; i < ENDLESS_SAVE_PERKS; ++i)
	{
		r->perkOwned[i] = (i < PERK_COUNT) ? endlessPerkOwned[i] : 0;
		r->perkTakenBy[0][i] = (i < PERK_COUNT) ? endlessPerkTakenBy[0][i] : 0;
		r->perkTakenBy[1][i] = (i < PERK_COUNT) ? endlessPerkTakenBy[1][i] : 0;
	}

	r->rerollCost    = endlessRerollCost[0];
	r->hullCost      = endlessHullCost[0];
	r->bombCost      = endlessBombCost[0];
	r->extraPerkCost = 0;   // legacy field: the extra perk is priced off the counts below
	r->extraPerksBought = endlessExtraPerksBought[0];
	r->extraPerksVisit  = endlessExtraPerksVisit[0];
	r->cleanseCost   = endlessCleanseCost[0];
	r->shopEntryCash = endlessShopEntryCash[0];
	r->purchasedMods = endlessPurchasedMods[0];
	r->buffKind      = endlessBuffKind[0];
	r->cleanseCharges= endlessCleanseChargeCount[0];
	r->gamblePerkWon = endlessGamblePerkWon[0] ? 1 : 0;
	r->perkPending   = endlessPerkPending ? 1 : 0;
	SDL_strlcpy(r->gambleMsg, endlessGambleMsg[0], sizeof(r->gambleMsg));
	SDL_strlcpy(r->lastSpecialName, endlessLastSpecialName[0], sizeof(r->lastSpecialName));

	// v21: the other player's own half, and the run-wide co-op settings.
	r->coopHostCharts = endlessCoopHostCharts ? 1 : 0;
	r->courseChooser  = (Uint8)endlessCourseChooser;
	r->armorBonus2    = endlessArmorBonus[1];
	r->revivesUsed2   = endlessRevivesUsed[1];
	r->shopTax2       = endlessShopTax[1];
	r->longCon2       = endlessLongCon[1];
	r->buffKind2      = endlessBuffKind[1];
	r->buffCharge2    = endlessBuffCharge[1];
	r->buffCooldownUntil2 = endlessBuffCooldownUntil[1];
	r->rerollCost2    = endlessRerollCost[1];
	r->hullCost2      = endlessHullCost[1];
	r->bombCost2      = endlessBombCost[1];
	r->extraPerkCost2 = 0;   // as above
	r->extraPerksBought2 = endlessExtraPerksBought[1];
	r->extraPerksVisit2  = endlessExtraPerksVisit[1];
	r->cleanseCost2   = endlessCleanseCost[1];
	r->shopEntryCash2 = endlessShopEntryCash[1];
	r->superbombs2    = (Sint32)player[1].superbombs;
	r->cleanseCharges2= endlessCleanseChargeCount[1];
	r->purchasedMods2 = endlessPurchasedMods[1];
	r->reviveHeld2    = endlessReviveHeld[1] ? 1 : 0;
	r->gambleRigged2  = endlessGambleRigged[1] ? 1 : 0;
	r->downed[0]      = endlessPlayerDowned[0] ? 1 : 0;
	r->downed[1]      = endlessPlayerDowned[1] ? 1 : 0;
	r->playerRng[0]   = endlessPlayerRngState[0];
	r->playerRng[1]   = endlessPlayerRngState[1];
	r->gamblePerkWon2 = endlessGamblePerkWon[1] ? 1 : 0;
	SDL_strlcpy(r->gambleMsg2, endlessGambleMsg[1], sizeof(r->gambleMsg2));
	SDL_strlcpy(r->lastSpecialName2, endlessLastSpecialName[1], sizeof(r->lastSpecialName2));

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

	memcpy(r->itemAvail, itemAvail, sizeof(r->itemAvail));
	memcpy(r->itemAvailMax, itemAvailMax, sizeof(r->itemAvailMax));

	SDL_strlcpy(r->seed, endlessRunSeed, sizeof(r->seed));

	// The v4 locked-sortie fields are populated only when saving from the locked outpost.
	r->lockedSortie = endlessLockedSortie ? 1 : 0;
	r->sortieMods   = endlessSortieModsV;
	r->sortieSec    = endlessSortieSec;
	r->sortieEp     = endlessSortieEp;
	r->sortieFile   = endlessSortieFile;

	// Anti-repeat recent-level ring (v6).
	r->recentCount = (Uint8)endlessRecentCount;
	for (int i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
	{
		r->recentEp[i]  = endlessRecentEp[i];
		r->recentSec[i] = endlessRecentSec[i];
	}

	r->creditsShown = endlessCreditsShown ? 1 : 0;  // v9 zone-100 credits

	r->lastSong      = endlessLastSong;             // v10 per-zone music continuity
	r->lastSongDepth = endlessLastSongDepth;

	r->starChartsOwed   = endlessStarChartsOwed ? 1 : 0;   // v12 boons owed to a later outpost
	r->breakthroughOwed = (Uint8)((endlessBreakthroughOwed < 0) ? 0
	                             : (endlessBreakthroughOwed > 255 ? 255 : endlessBreakthroughOwed));

	// Disk saves use Relaxed or Standard; in-memory sortie snapshots can carry Hardcore.
	r->runMode = (Uint8)endlessRunMode;

	r->baseLevelRule = (Uint8)endlessRunBaseRule;         // v22 chart rule, fixed for the run

	r->chartRerolls    = endlessChartRerolls;             // v23 Radar chart reroll
	r->chartStarCharts = endlessChartStarCharts ? 1 : 0;

	r->shuffleNext      = (Uint32)endlessShuffleNext;     // v24 Shuffle bag cursor and live hand
	r->shuffleHandStart = (Uint32)endlessShuffleHandStart;

	// v16 cash ledger + v19 sink breakdown. The spare on-disk slots past ENDLESS_CASH_SOURCES /
	// ENDLESS_CASH_SINKS stay zeroed by the memset.
	r->cashEarned = endlessRunCashEarned;
	r->cashSpent  = endlessRunCashSpent;
	for (int i = 0; i < ENDLESS_CASH_SOURCES; ++i)
		r->cashBySource[i] = endlessCashBySource[i];
	for (int i = 0; i < ENDLESS_CASH_SINKS; ++i)
		r->cashBySink[i] = endlessCashBySink[i];

	r->usedCustom = endlessRunUsedCustom ? 1 : 0;   // v20 custom-weapon record mark

	// The partner's half, when a save checkpoint captured one this visit (v27).
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
	endlessArmorBonus[0] = r->armorBonus;
	endlessRunUsedCustom = r->usedCustom != 0;
	endlessRunKills      = r->runKills;
	endlessRunBossKills  = r->runBossKills;
	endlessBuffCharge[0] = r->buffCharge;
	endlessBuffCooldownUntil[0] = r->buffCooldownUntil;
	endlessRevivesUsed[0] = r->revivesUsed;
	endlessShopTax[0]    = r->shopTax;
	endlessLongCon[0]    = r->longCon;
	endlessPerkDepthDone = r->perkDepthDone;
	player[0].superbombs = (r->superbombs < 0) ? 0 : (r->superbombs > 10 ? 10 : r->superbombs);
	endlessReviveHeld[0] = r->reviveHeld != 0;
	endlessGambleRigged[0] = r->gambleRigged != 0;
	for (int i = 0; i < PERK_COUNT && i < ENDLESS_SAVE_PERKS; ++i)
	{
		const int maxs = endlessPerkTable[i].maxStack;
		for (int p = 0; p < 2; ++p)
		{
			const int v = r->perkTakenBy[p][i];
			endlessPerkTakenBy[p][i] = (JE_byte)(v < 0 ? 0 : (v > maxs ? maxs : v));
		}
	}
	endlessPerkRederive();

	endlessRerollCost[0]         = r->rerollCost;
	endlessHullCost[0]           = r->hullCost;
	endlessBombCost[0]           = r->bombCost;
	endlessExtraPerksBought[0]   = endlessClamp(r->extraPerksBought, 0, ENDLESS_PERK_PAID_MAX);
	endlessExtraPerksVisit[0]    = endlessClamp(r->extraPerksVisit, 0, ENDLESS_PERK_VISIT_MAX);
	endlessCleanseCost[0]        = r->cleanseCost;
	endlessShopEntryCash[0]      = r->shopEntryCash;
	endlessPurchasedMods[0]      = r->purchasedMods;
	endlessBuffKind[0]           = r->buffKind;
	endlessCleanseChargeCount[0] = r->cleanseCharges;
	endlessGamblePerkWon[0]      = r->gamblePerkWon != 0;
	endlessPerkPending           = r->perkPending != 0;
	SDL_strlcpy(endlessGambleMsg[0], r->gambleMsg, sizeof(endlessGambleMsg[0]));
	SDL_strlcpy(endlessLastSpecialName[0], r->lastSpecialName, sizeof(endlessLastSpecialName[0]));

	// v21 co-op half. A pre-v21 record leaves it zeroed, which is a player who bought nothing.
	endlessCoopHostCharts        = r->coopHostCharts != 0;
	endlessCourseChooser         = (EndlessCourseChooser)r->courseChooser;
	endlessArmorBonus[1]         = r->armorBonus2;
	endlessRevivesUsed[1]        = r->revivesUsed2;
	endlessShopTax[1]            = r->shopTax2;
	endlessLongCon[1]            = r->longCon2;
	endlessBuffKind[1]           = r->buffKind2;
	endlessBuffCharge[1]         = r->buffCharge2;
	endlessBuffCooldownUntil[1]  = r->buffCooldownUntil2;
	endlessRerollCost[1]         = r->rerollCost2;
	endlessHullCost[1]           = r->hullCost2;
	endlessBombCost[1]           = r->bombCost2;
	endlessExtraPerksBought[1]   = endlessClamp(r->extraPerksBought2, 0, ENDLESS_PERK_PAID_MAX);
	endlessExtraPerksVisit[1]    = endlessClamp(r->extraPerksVisit2, 0, ENDLESS_PERK_VISIT_MAX);
	endlessCleanseCost[1]        = r->cleanseCost2;
	endlessShopEntryCash[1]      = r->shopEntryCash2;
	player[1].superbombs         = (r->superbombs2 < 0) ? 0u : (r->superbombs2 > 10 ? 10u : (uint)r->superbombs2);
	endlessCleanseChargeCount[1] = r->cleanseCharges2;
	endlessPurchasedMods[1]      = r->purchasedMods2;
	endlessReviveHeld[1]         = r->reviveHeld2 != 0;
	endlessGambleRigged[1]       = r->gambleRigged2 != 0;
	endlessPlayerDowned[0]       = r->downed[0] != 0;
	endlessPlayerDowned[1]       = r->downed[1] != 0;
	endlessGamblePerkWon[1]      = r->gamblePerkWon2 != 0;
	SDL_strlcpy(endlessGambleMsg[1], r->gambleMsg2, sizeof(endlessGambleMsg[1]));
	SDL_strlcpy(endlessLastSpecialName[1], r->lastSpecialName2, sizeof(endlessLastSpecialName[1]));
	endlessSetSeed(r->seed);  // restore the run seed (endlessResetRun blanked it); rehashes + primes the stream
	// ...then put each player's own draw stream back where the save left it, so a resumed outpost
	// deals the same next hand. A pre-v21 record has none: endlessSetSeed already primed a pair.
	if (r->playerRng[0] != 0 || r->playerRng[1] != 0)
	{
		endlessPlayerRngState[0] = r->playerRng[0];
		endlessPlayerRngState[1] = r->playerRng[1];
	}

	endlessPerkChoiceN = endlessClamp(r->perkChoiceN, 0, ENDLESS_SAVE_OFFERS);
	for (int i = 0; i < ENDLESS_SAVE_OFFERS; ++i)
		endlessPerkChoice[i] = r->perkChoice[i];

	endlessLastEp  = r->lastEp;
	endlessLastSec = r->lastSec;
	endlessForced  = r->forced != 0;

	// Pre-v6 records retain the reset empty recent-level window.
	endlessRecentCount = (r->recentCount > ENDLESS_LEVEL_HISTORY) ? ENDLESS_LEVEL_HISTORY : r->recentCount;
	for (int i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
	{
		endlessRecentEp[i]  = r->recentEp[i];
		endlessRecentSec[i] = r->recentSec[i];
	}

	// Pre-v9 records resume with zone-100 credits marked unshown.
	endlessCreditsShown = r->creditsShown != 0;

	// A zero lastSong means no remembered depth for pre-v10 records.
	endlessLastSong      = r->lastSong;
	endlessLastSongDepth = (r->lastSong != 0) ? r->lastSongDepth : -1;

	// Pre-v12 records owe no deferred boons.
	endlessStarChartsOwed   = r->starChartsOwed != 0;
	endlessBreakthroughOwed = r->breakthroughOwed;

	// Restore the saved run mode; pre-v15 records use Relaxed.
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

	// A v4 locked-sortie save reopens the locked outpost and committed level.
	endlessLockedSortie = r->lockedSortie != 0;
	if (endlessLockedSortie)
	{
		endlessSortieModsV = r->sortieMods;
		endlessSortieSec   = (JE_byte)r->sortieSec;
		endlessSortieEp    = r->sortieEp;
		endlessSortieFile  = (JE_byte)r->sortieFile;
		endlessSortieHave  = true;
	}

	// Restore the v16 ledger and anchor it to the current wallet. Sortie restores re-anchor after loadout copy.
	endlessRunCashEarned = r->cashEarned;
	endlessRunCashSpent  = r->cashSpent;
	for (int i = 0; i < ENDLESS_CASH_SOURCES; ++i)
		endlessCashBySource[i] = r->cashBySource[i];
	for (int i = 0; i < ENDLESS_CASH_SINKS; ++i)
		endlessCashBySink[i] = r->cashBySink[i];
	endlessCashResync();

	endlessResumeVisit = true;  // next outpost: restore this snapshot, do not reroll
}

/* JE_saveGame calls this for the slot it is about to write, so the Endless half of the slot leaves
 * with the campaign half in the same file write. Hardcore is refused in JE_saveGame before this
 * runs, which keeps whatever record the slot held. */
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
	endlessExtraPerksVisit[p]    = endlessClamp(fields[10], 0, ENDLESS_PERK_VISIT_MAX);
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

	/* Both halves of each ship's personal buffs, verbatim. The live mask cannot be re-derived from
	 * the purchased one: a sector consumes the purchase and zeroes it while the mask it folded
	 * stays up for the rest of the zone. */
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

	/* Only on a mask that actually moved. The refresh re-rolls the gravity heading off the endless
	 * stream, and the editing machine draws it exactly once for the same edit; rolling here on an
	 * unrelated debug block would put the two streams a draw apart. */
	if (modsMoved)
		endlessRefreshModDerivedState();
}

/* Online co-op resume: the host serializes the live run as the same text a save slot holds and the
 * joiner adopts it, so both machines resume from identical state. Each machine's own shop stock is
 * redrawn from the seed rather than sent (see "Endless online" in doc/notes.md). */
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

	/* The record's own rows belong to the machine that captured them, its equipped gear seeded
	 * in. This seat's half is the record's partner block when the save checkpointed one; without
	 * it the rows are redealt from the restored stream, which reproduces the deal this seat was
	 * originally shown (the capturing machine never draws from its peer's stream). */
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
		endlessPartnerStash.rng = rec.playerRng[1 - p];
	}

	return true;
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

	/* Put the record's partner half back in the stash: the resume re-captures this run for the
	 * wire and the entry checkpoint, and losing the half there would strand the joiner on a
	 * redeal. It still belongs to the partner, so the semantics hold on this machine. */
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
	endlessSortieEp    = episodeNum;          // committed episode
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

	endlessApplyCurrent(&endlessSortieRec);                           // revert endless state (incl. run mode); arms endlessResumeVisit (also cleared endlessSortieHave via endlessResetRun)
	memcpy(player, endlessSortiePlayer, sizeof(endlessSortiePlayer)); // revert loadout (wins over the superbombs field applyCurrent touched)
	endlessActiveMods   = endlessSortieModsV;                         // the committed level's mutators (for the relaunch)
	endlessSortieHave   = true;                                       // committed-level state remains valid
	endlessSortieOutpostMods = outpostMods;                           // preserve the same outpost modifiers
	endlessSortieOutpostEp   = outpostEp;                             // and its item data

	// The restored stock is item ids, so reload the tables it was drawn against before the outpost
	// redraws. Both branches below reselect an episode when they relaunch.
	if (outpostEp != 0 && outpostEp != episodeNum)
		JE_initEpisode(outpostEp);

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
	// The pre-pick one-shots go with them. This retry opens no outpost, but a later bail out of
	// the restarted zone does, and it restores the pending purchases from these; the wallet
	// reverts to a launch value that has already paid for them.
	unsigned prePurchased[COUNTOF(endlessSortiePrePurchased)];
	int      preCleanse[COUNTOF(endlessSortiePreCleanse)];
	int      preLongCon[COUNTOF(endlessSortiePreLongCon)];
	memcpy(prePurchased, endlessSortiePrePurchased, sizeof(prePurchased));
	memcpy(preCleanse, endlessSortiePreCleanse, sizeof(preCleanse));
	memcpy(preLongCon, endlessSortiePreLongCon, sizeof(preLongCon));

	endlessApplyCurrent(&endlessSortieRec);                           // revert endless state (incl. run mode); also arms endlessResumeVisit
	memcpy(player, endlessSortiePlayer, sizeof(endlessSortiePlayer)); // revert loadout
	// Each ship's personal effect mask, which the reset inside the apply blanked. This retry
	// re-arms the committed level directly, so nothing downstream re-folds it: without this a
	// buff bought for the zone (Turbodrive and the rest) dies with the zone it was bought for.
	memcpy(endlessPlayerMods, endlessSortiePlayerModsV, sizeof(endlessPlayerMods));
	endlessSortieHave   = true;                                       // the committed-level statics are still valid
	endlessLockedSortie = false;   // no outpost is opened, so there is nothing to lock
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
	if (endlessSortieEp != episodeNum)
		JE_initEpisode((JE_byte)endlessSortieEp);  // set level fields after this reset
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

// The per-difficulty breakdown, one key per mode: the zones as a comma-separated list in
// endlessDifficultyLevel order, and their custom marks as a string of 0 and 1 in the same order.
// A shorter or absent value leaves the remaining slots empty, so the list can grow.
static const char *const endlessBestZoneDiffKey[ENDLESS_RUNMODE_COUNT] = {
	"best_zone_diff", "best_zone_normal_diff", "best_zone_hardcore_diff",
};
static const char *const endlessBestZoneDiffCustomKey[ENDLESS_RUNMODE_COUNT] = {
	"best_zone_diff_custom", "best_zone_normal_diff_custom", "best_zone_hardcore_diff_custom",
};

/* The co-op table lives under the same key with a "_2p" tail, and every base-level rule past Varied
 * under a further tail of its own. A config written before either split carries neither tail, which
 * reads as an empty set of records for the tables that gained one. */
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
