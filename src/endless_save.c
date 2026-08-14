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

// tyrian.sav has a fixed checksummed layout, so Endless uses a per-slot sidecar.

#define ENDLESS_SAVE_FILE    "endless.sav"
#define ENDLESS_SAVE_VERSION 26   // v26: items may carry the Dragonwing ship id (SHIP_DRAGONWING)
#define ENDLESS_SAVE_PERKS   32
#define ENDLESS_SAVE_PERKS_V10 16
#define ENDLESS_SAVE_PERK_CHARGER_V13 14
#define ENDLESS_SAVE_OFFERS     ENDLESS_PERK_OFFERS_MILESTONE
#define ENDLESS_SAVE_OFFERS_V12 3

// Spare cash-source/sink slots, so appending an EndlessCashSource or EndlessCashSink needs no
// version bump.
#define ENDLESS_SAVE_CASH_SOURCES 12
#define ENDLESS_SAVE_CASH_SINKS   12

/* File header: the tag, the format version, how many slot records follow, and from v25 how many
 * bytes each of those records is. Without that last field a record that changed width without the
 * version changing with it puts every slot but the first at the wrong offset, silently. */
#define ENDLESS_SAVE_WIDTH_VERSION 25   // first version whose header carries the record width
#define ENDLESS_HEADER_BYTES       8

typedef struct {
	int version;   // format version the file was written by
	int slots;     // records that follow
	int width;     // bytes per record, or 0 when the version alone fixes it (pre-v25)
} EndlessSaveHeader;

// Perk IDs are on-disk slots; widen the block and bump the version together.
COMPILE_TIME_ASSERT(endless_save_perks_fit, PERK_COUNT <= ENDLESS_SAVE_PERKS);
COMPILE_TIME_ASSERT(endless_save_cash_sources_fit, ENDLESS_CASH_SOURCES <= ENDLESS_SAVE_CASH_SOURCES);
COMPILE_TIME_ASSERT(endless_save_cash_sinks_fit, ENDLESS_CASH_SINKS <= ENDLESS_SAVE_CASH_SINKS);

typedef struct {
	bool used;

	// Run state.
	Sint32 runDepth, armorBonus, runKills, runBossKills;
	Sint32 buffCharge, revivesUsed, shopTax, longCon, perkDepthDone, superbombs;
	Uint8  reviveHeld, gambleRigged;
	Uint8  perkOwned[ENDLESS_SAVE_PERKS];

	// Outpost prices and pending buys.
	Sint32 rerollCost, hullCost, bombCost, extraPerkCost, cleanseCost, shopEntryCash;
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
	Sint32 rerollCost2, hullCost2, bombCost2, extraPerkCost2, cleanseCost2, shopEntryCash2;
	Sint32 superbombs2, cleanseCharges2;
	Uint32 purchasedMods2;
	Uint8  reviveHeld2, gambleRigged2, downed[2];
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
} EndlessSlotRec;

// One record per save slot, mirrored to endless.sav. Read-modify-write on each save keeps the
// other slots' records intact.
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

/* Records are laid out as bytes rather than at file positions, so one whose stored width differs
 * from this build's can be padded or trimmed on its own. A writer with a null buffer measures
 * rather than stores. See "Save format" in doc/notes.md. */
#define ENDLESS_REC_MAX  4096            // ceiling for one record; the width itself is measured
#define ENDLESS_FILE_MAX (1024 * 1024)   // ...and for a whole sidecar file

typedef struct { Uint8 *p; const Uint8 *end; size_t n; } EndlessWriter;
typedef struct { const Uint8 *p, *end; } EndlessReader;

// Little-endian field I/O. A short read invalidates the optional Endless sidecar.
static void endlessPutU8(EndlessWriter *w, unsigned v)                 { if (w->p != NULL && w->p < w->end) *w->p++ = (Uint8)v; ++w->n; }
static void endlessPutBytes(EndlessWriter *w, const void *p, size_t n) { for (size_t i = 0; i < n; ++i) endlessPutU8(w, ((const Uint8 *)p)[i]); }
static void endlessPutU32(EndlessWriter *w, Uint32 v)                  { v = SDL_SwapLE32(v); endlessPutBytes(w, &v, 4); }
static void endlessPutU64(EndlessWriter *w, Uint64 v)                  { v = SDL_SwapLE64(v); endlessPutBytes(w, &v, 8); }
static bool endlessGetU8(EndlessReader *rd, Uint8 *v)                  { if (rd->p >= rd->end) return false; *v = *rd->p++; return true; }
static bool endlessGetBytes(EndlessReader *rd, void *p, size_t n)      { if ((size_t)(rd->end - rd->p) < n) return false; memcpy(p, rd->p, n); rd->p += n; return true; }
static bool endlessGetU32(EndlessReader *rd, Uint32 *v)                { Uint32 b; if (!endlessGetBytes(rd, &b, 4)) return false; *v = SDL_SwapLE32(b); return true; }
static bool endlessGetU64(EndlessReader *rd, Uint64 *v)                { Uint64 b; if (!endlessGetBytes(rd, &b, 8)) return false; *v = SDL_SwapLE64(b); return true; }

static void endlessWriteRec(EndlessWriter *w, const EndlessSlotRec *r)
{
	endlessPutU8(w, r->used ? 1 : 0);

	const Sint32 s32[] = {
		r->runDepth, r->armorBonus, r->runKills, r->runBossKills, r->buffCharge, r->revivesUsed,
		r->shopTax, r->longCon, r->perkDepthDone, r->superbombs,
		r->rerollCost, r->hullCost, r->bombCost, r->extraPerkCost, r->cleanseCost, r->shopEntryCash,
		r->buffKind, r->cleanseCharges, r->perkChoiceN, r->courseCnt, r->lastEp,
	};
	for (unsigned i = 0; i < COUNTOF(s32); ++i)
		endlessPutU32(w, (Uint32)s32[i]);

	endlessPutU32(w, r->purchasedMods);
	endlessPutU8(w, r->reviveHeld);
	endlessPutU8(w, r->gambleRigged);
	endlessPutU8(w, r->gamblePerkWon);
	endlessPutU8(w, r->perkPending);
	endlessPutU8(w, r->lastSec);
	endlessPutU8(w, r->forced);

	endlessPutBytes(w, r->perkOwned, ENDLESS_SAVE_PERKS);
	endlessPutBytes(w, r->gambleMsg, sizeof(r->gambleMsg));
	endlessPutBytes(w, r->lastSpecialName, sizeof(r->lastSpecialName));

	for (unsigned i = 0; i < COUNTOF(r->perkChoice); ++i)
		endlessPutU32(w, (Uint32)r->perkChoice[i]);
	for (unsigned i = 0; i < ENDLESS_MAX_COURSES; ++i)
		endlessPutU32(w, (Uint32)r->courseEp[i]);
	for (unsigned i = 0; i < ENDLESS_MAX_COURSES; ++i)
		endlessPutU64(w, r->courseMod[i]);
	endlessPutBytes(w, r->courseSec, ENDLESS_MAX_COURSES);
	endlessPutBytes(w, r->courseFile, ENDLESS_MAX_COURSES);

	endlessPutBytes(w, r->itemAvail, sizeof(r->itemAvail));
	endlessPutBytes(w, r->itemAvailMax, sizeof(r->itemAvailMax));
	endlessPutBytes(w, r->seed, sizeof(r->seed));

	endlessPutU8(w, r->lockedSortie);        // v4 locked-sortie block
	endlessPutU64(w, r->sortieMods);         // v7: 64-bit (was U32 in v4-v6)
	endlessPutU8(w, r->sortieSec);
	endlessPutU32(w, (Uint32)r->sortieEp);
	endlessPutU8(w, r->sortieFile);

	endlessPutU32(w, (Uint32)r->buffCooldownUntil);  // v5 kill-fire recharge

	endlessPutU8(w, r->recentCount);                 // v6 anti-repeat recent-level ring
	for (unsigned i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
		endlessPutU32(w, (Uint32)r->recentEp[i]);
	endlessPutBytes(w, r->recentSec, ENDLESS_LEVEL_HISTORY);

	endlessPutU8(w, r->creditsShown);                // v9 zone-100 credits

	endlessPutU8(w, r->lastSong);                    // v10 per-zone music continuity
	endlessPutU32(w, (Uint32)r->lastSongDepth);

	endlessPutU8(w, r->starChartsOwed);              // v12 boons owed to a later outpost
	endlessPutU8(w, r->breakthroughOwed);

	endlessPutU8(w, r->runMode);                     // v15 Relaxed / Standard / Hardcore

	endlessPutU64(w, r->cashEarned);                 // v16 stored earnings only
	endlessPutU64(w, r->cashSpent);                  // v17 added spending and source detail
	for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SOURCES; ++i)
		endlessPutU64(w, r->cashBySource[i]);

	for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SINKS; ++i)  // v19 spending breakdown
		endlessPutU64(w, r->cashBySink[i]);

	endlessPutU8(w, r->usedCustom);                  // v20 custom-weapon record mark

	// v21 online co-op: the second player's own half of the outpost, plus the shared turn flag.
	endlessPutU8(w, r->coopHostCharts);
	endlessPutU8(w, r->courseChooser);
	const Sint32 coop32[] = {
		r->armorBonus2, r->revivesUsed2, r->shopTax2, r->longCon2, r->buffKind2, r->buffCharge2,
		r->buffCooldownUntil2, r->rerollCost2, r->hullCost2, r->bombCost2, r->extraPerkCost2,
		r->cleanseCost2, r->shopEntryCash2, r->superbombs2, r->cleanseCharges2,
	};
	for (unsigned i = 0; i < COUNTOF(coop32); ++i)
		endlessPutU32(w, (Uint32)coop32[i]);
	endlessPutU32(w, r->purchasedMods2);
	endlessPutU8(w, r->reviveHeld2);
	endlessPutU8(w, r->gambleRigged2);
	endlessPutU8(w, r->downed[0]);
	endlessPutU8(w, r->downed[1]);
	endlessPutBytes(w, r->perkTakenBy[0], ENDLESS_SAVE_PERKS);
	endlessPutBytes(w, r->perkTakenBy[1], ENDLESS_SAVE_PERKS);
	endlessPutU64(w, r->playerRng[0]);
	endlessPutU64(w, r->playerRng[1]);

	endlessPutU8(w, r->baseLevelRule);               // v22 base-level rule, widened in v24

	endlessPutU8(w, r->chartRerolls);                // v23 Radar chart reroll
	endlessPutU8(w, r->chartStarCharts);

	endlessPutU32(w, r->shuffleNext);                // v24 Shuffle bag cursor and live hand
	endlessPutU32(w, r->shuffleHandStart);
}

static bool endlessReadRec(EndlessReader *rd, EndlessSlotRec *r, int version)
{
	memset(r, 0, sizeof(*r));

	Uint8 used;
	if (!endlessGetU8(rd, &used))
		return false;
	r->used = used != 0;

	Sint32 *const s32[] = {
		&r->runDepth, &r->armorBonus, &r->runKills, &r->runBossKills, &r->buffCharge, &r->revivesUsed,
		&r->shopTax, &r->longCon, &r->perkDepthDone, &r->superbombs,
		&r->rerollCost, &r->hullCost, &r->bombCost, &r->extraPerkCost, &r->cleanseCost, &r->shopEntryCash,
		&r->buffKind, &r->cleanseCharges, &r->perkChoiceN, &r->courseCnt, &r->lastEp,
	};
	for (unsigned i = 0; i < COUNTOF(s32); ++i)
	{
		Uint32 t;
		if (!endlessGetU32(rd, &t))
			return false;
		*s32[i] = (Sint32)t;
	}

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
		Sint32 *const coop32[] = {
			&r->armorBonus2, &r->revivesUsed2, &r->shopTax2, &r->longCon2, &r->buffKind2,
			&r->buffCharge2, &r->buffCooldownUntil2, &r->rerollCost2, &r->hullCost2, &r->bombCost2,
			&r->extraPerkCost2, &r->cleanseCost2, &r->shopEntryCash2, &r->superbombs2,
			&r->cleanseCharges2,
		};
		for (unsigned i = 0; i < COUNTOF(coop32); ++i)
		{
			Uint32 t;
			if (!endlessGetU32(rd, &t))
				return false;
			*coop32[i] = (Sint32)t;
		}
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

	return true;
}

int endlessSaveCurrentVersion(void)
{
	return ENDLESS_SAVE_VERSION;
}

/* The bytes one record occupies in this build, taken from the writer itself, so appending a field
 * updates what the header advertises with nothing else to keep in step. */
static int endlessRecordWidth(void)
{
	static int width = 0;
	if (width == 0)
	{
		EndlessSlotRec probe;
		memset(&probe, 0, sizeof(probe));
		EndlessWriter measure = { NULL, NULL, 0 };
		endlessWriteRec(&measure, &probe);
		width = (int)measure.n;
	}
	return width;
}

static void endlessWriteHeader(EndlessWriter *w, int slots)
{
	endlessPutBytes(w, "OTES", 4);
	endlessPutU8(w, ENDLESS_SAVE_VERSION);
	endlessPutU8(w, (unsigned)slots);
	endlessPutU8(w, (unsigned)(endlessRecordWidth() & 0xFF));   // v25 and up: the record width
	endlessPutU8(w, (unsigned)((endlessRecordWidth() >> 8) & 0xFF));
}

static bool endlessReadHeader(EndlessReader *rd, EndlessSaveHeader *h)
{
	Uint8 tag[6];
	if (!endlessGetBytes(rd, tag, sizeof(tag)) || memcmp(tag, "OTES", 4) != 0
	    || tag[4] < 3 || tag[4] > ENDLESS_SAVE_VERSION
	    || tag[5] < 1 || tag[5] > SAVE_FILES_NUM)
	{
		return false;
	}
	h->version = tag[4];
	h->slots   = tag[5];
	h->width   = 0;   // before v25 a record is exactly what its own version parses

	if (h->version >= ENDLESS_SAVE_WIDTH_VERSION)
	{
		Uint8 lo, hi;
		if (!endlessGetU8(rd, &lo) || !endlessGetU8(rd, &hi))
			return false;
		h->width = lo | (hi << 8);
		if (h->width < 1 || h->width > ENDLESS_REC_MAX)
			return false;
	}
	return true;
}

/* Take one record off the cursor. A stored width narrower than this build's is padded, so fields
 * added since read as the zero their version gate would have left them; a wider one has its tail
 * skipped. Either way the next slot still starts where the file says it does. */
static bool endlessReadOneRec(EndlessReader *rd, const EndlessSaveHeader *h, EndlessSlotRec *rec)
{
	const int want = endlessRecordWidth();
	if (h->width == 0 || h->width == want)
		return endlessReadRec(rd, rec, h->version);
	if (want > ENDLESS_REC_MAX || (size_t)(rd->end - rd->p) < (size_t)h->width)
		return false;

	Uint8 buf[ENDLESS_REC_MAX];
	const size_t take = MIN((size_t)h->width, (size_t)want);
	memcpy(buf, rd->p, take);
	memset(buf + take, 0, (size_t)want - take);
	rd->p += h->width;

	EndlessReader one = { buf, buf + want };
	return endlessReadRec(&one, rec, h->version);
}

static bool endlessTestDecode(const Uint8 *bytes, size_t size, EndlessSlotRec *rec, int *version)
{
	EndlessReader rd = { bytes, bytes + size };
	EndlessSaveHeader h;
	if (bytes == NULL || !endlessReadHeader(&rd, &h) || !endlessReadOneRec(&rd, &h, rec))
		return false;
	if (version != NULL)
		*version = h.version;
	return true;
}

static bool endlessTestEncode(const EndlessSlotRec *rec, Uint8 **bytes, size_t *size)
{
	const size_t total = (size_t)ENDLESS_HEADER_BYTES + (size_t)endlessRecordWidth();
	Uint8 *out = malloc(total);
	if (out == NULL)
		return false;

	EndlessWriter w = { out, out + total, 0 };
	endlessWriteHeader(&w, 1);
	endlessWriteRec(&w, rec);
	if (w.n != total)
	{
		free(out);
		return false;
	}
	*bytes = out;
	*size = total;
	return true;
}

static void endlessTestDetail(char *detail, size_t detailSize, const char *message)
{
	if (detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", message);
}

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
	if (!endlessTestDecode(bytes, (size_t)fileSize, &first, &version))
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
	    || (version >= 24 && (first.shuffleNext != 37 || first.shuffleHandStart != 33)))
	{
		free(bytes);
		endlessTestDetail(detail, detailSize, "version defaults differ");
		return false;
	}

	Uint8 *encoded1 = NULL, *encoded2 = NULL;
	size_t encoded1Size = 0, encoded2Size = 0;
	if (!endlessTestEncode(&first, &encoded1, &encoded1Size)
	    || !endlessTestDecode(encoded1, encoded1Size, &second, NULL)
	    || !endlessTestEncode(&second, &encoded2, &encoded2Size)
	    || encoded1Size != encoded2Size || memcmp(encoded1, encoded2, encoded1Size) != 0)
	{
		free(bytes); free(encoded1); free(encoded2);
		endlessTestDetail(detail, detailSize, "current-format round trip is unstable");
		return false;
	}
	free(encoded1);
	free(encoded2);

	/* Every strict prefix must fail cleanly. Sanitizers enforce the memory-safety half. */
	for (size_t cut = 0; cut < (size_t)fileSize; ++cut)
	{
		EndlessSlotRec junk;
		if (endlessTestDecode(bytes, cut, &junk, NULL))
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
	if (!endlessTestDecode(oversized, oversizedSize, &oversizedRec, NULL))
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
		(void)endlessTestDecode(fuzz, n, &junk, NULL);
	}

	free(bytes);
	return true;
}

/* Prove both directions on a record this build wrote: a narrower file keeps everything it did
 * carry, a wider one is read without its unknown tail, and a file shorter than it claims still
 * fails. */
bool endlessSaveTestWidthGuard(char *detail, size_t detailSize)
{
	if (detail != NULL && detailSize != 0)
		detail[0] = '\0';

	EndlessSlotRec rec;
	memset(&rec, 0, sizeof(rec));
	rec.used = true;
	rec.runDepth = 91;
	rec.courseCnt = 1;
	rec.courseEp[0] = 1;
	rec.shuffleNext = 12;
	SDL_strlcpy(rec.seed, "width-guard", sizeof(rec.seed));

	Uint8 *whole = NULL;
	size_t wholeSize = 0;
	if (!endlessTestEncode(&rec, &whole, &wholeSize))
	{
		endlessTestDetail(detail, detailSize, "encode failed");
		return false;
	}

	const size_t width = wholeSize - ENDLESS_HEADER_BYTES;
	const size_t step = 4;   // stands in for a field one of the two builds has and the other does not
	Uint8 *narrow = malloc(wholeSize - step);
	Uint8 *wide = malloc(wholeSize + step);
	Uint8 *again = NULL;
	size_t againSize = 0;
	EndlessSlotRec back;
	const char *fault = NULL;

	if (width <= step || narrow == NULL || wide == NULL)
	{
		fault = "fixture allocation failed";
	}
	else
	{
		memcpy(narrow, whole, wholeSize - step);
		narrow[6] = (Uint8)((width - step) & 0xFF);
		narrow[7] = (Uint8)(((width - step) >> 8) & 0xFF);

		memcpy(wide, whole, wholeSize);
		memset(wide + wholeSize, 0xa5, step);   // the unknown tail, which must not be read
		wide[6] = (Uint8)((width + step) & 0xFF);
		wide[7] = (Uint8)(((width + step) >> 8) & 0xFF);
	}

	if (fault == NULL && !(endlessTestDecode(narrow, wholeSize - step, &back, NULL)
	                       && endlessTestEncode(&back, &again, &againSize)
	                       && againSize == wholeSize
	                       && memcmp(again + ENDLESS_HEADER_BYTES, whole + ENDLESS_HEADER_BYTES,
	                                 width - step) == 0))
	{
		fault = "a narrower record did not pad";
	}
	free(again);
	again = NULL;

	if (fault == NULL && !(endlessTestDecode(wide, wholeSize + step, &back, NULL)
	                       && endlessTestEncode(&back, &again, &againSize)
	                       && againSize == wholeSize
	                       && memcmp(again, whole, wholeSize) == 0))
	{
		fault = "a wider record was not trimmed";
	}
	free(again);

	if (fault == NULL && endlessTestDecode(whole, wholeSize - 1, &back, NULL))
		fault = "a record shorter than the header claims was accepted";

	free(narrow);
	free(wide);
	free(whole);

	if (fault != NULL)
		endlessTestDetail(detail, detailSize, fault);
	return fault == NULL;
}

// Read the whole sidecar in. Any file-level problem leaves it as "no endless save".
static Uint8 *endlessReadSaveFile(size_t *size)
{
	FILE *f = dir_fopen(get_user_directory(), ENDLESS_SAVE_FILE, "rb");
	if (f == NULL)
		return NULL;

	fseek(f, 0, SEEK_END);
	const long end = ftell(f);
	rewind(f);

	Uint8 *bytes = (end > 0 && end <= ENDLESS_FILE_MAX) ? malloc((size_t)end) : NULL;
	if (bytes != NULL && fread(bytes, 1, (size_t)end, f) != (size_t)end)
	{
		free(bytes);
		bytes = NULL;
	}
	fclose(f);

	*size = (bytes != NULL) ? (size_t)end : 0;
	return bytes;
}

// Load all slot records. Any sidecar-level error marks the optional cache unused.
static void endlessReadAllSlots(void)
{
	memset(endlessSlotCache, 0, sizeof(endlessSlotCache));

	size_t size = 0;
	Uint8 *const bytes = endlessReadSaveFile(&size);
	if (bytes == NULL)
		return;

	EndlessReader rd = { bytes, bytes + size };
	EndlessSaveHeader h;
	if (endlessReadHeader(&rd, &h))   // accepts v3 and up; anything else is "no endless save"
	{
		// Say so rather than quietly loading half a run: a width this build does not share means
		// the records carry fields it does not know about, or lack ones it expects.
		if (h.width != 0 && h.width != endlessRecordWidth())
		{
			fprintf(stderr, "warning: endless save holds %d-byte records, this build writes %d\n",
			        h.width, endlessRecordWidth());
		}

		for (int s = 0; s < h.slots; ++s)
		{
			EndlessSlotRec rec;
			if (!endlessReadOneRec(&rd, &h, &rec))
				break;  // truncated: keep the full records already read
			if (s < SAVE_FILES_NUM)
				endlessSlotCache[s] = rec;
		}
	}

	free(bytes);
}

// Write the fixed-layout slot cache.
static void endlessWriteAllSlots(void)
{
	const size_t total = (size_t)ENDLESS_HEADER_BYTES
	                   + (size_t)SAVE_FILES_NUM * (size_t)endlessRecordWidth();
	Uint8 *const bytes = malloc(total);
	if (bytes == NULL)
		return;

	EndlessWriter w = { bytes, bytes + total, 0 };
	endlessWriteHeader(&w, SAVE_FILES_NUM);
	for (int s = 0; s < SAVE_FILES_NUM; ++s)
		endlessWriteRec(&w, &endlessSlotCache[s]);

	FILE *f = dir_fopen_warn(get_user_directory(), ENDLESS_SAVE_FILE, "wb");
	if (f != NULL)
	{
		fwrite(bytes, 1, w.n, f);
		fclose(f);
	}
	free(bytes);
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

	r->rerollCost    = (Sint32)endlessRerollCost[0];
	r->hullCost      = endlessHullCost[0];
	r->bombCost      = (Sint32)endlessBombCost[0];
	r->extraPerkCost = (Sint32)endlessExtraPerkCost[0];
	r->cleanseCost   = (Sint32)endlessCleanseCost[0];
	r->shopEntryCash = (Sint32)endlessShopEntryCash[0];
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
	r->rerollCost2    = (Sint32)endlessRerollCost[1];
	r->hullCost2      = endlessHullCost[1];
	r->bombCost2      = (Sint32)endlessBombCost[1];
	r->extraPerkCost2 = (Sint32)endlessExtraPerkCost[1];
	r->cleanseCost2   = (Sint32)endlessCleanseCost[1];
	r->shopEntryCash2 = (Sint32)endlessShopEntryCash[1];
	r->superbombs2    = (Sint32)player[1].superbombs;
	r->cleanseCharges2= endlessCleanseChargeCount[1];
	r->purchasedMods2 = endlessPurchasedMods[1];
	r->reviveHeld2    = endlessReviveHeld[1] ? 1 : 0;
	r->gambleRigged2  = endlessGambleRigged[1] ? 1 : 0;
	r->downed[0]      = endlessPlayerDowned[0] ? 1 : 0;
	r->downed[1]      = endlessPlayerDowned[1] ? 1 : 0;
	r->playerRng[0]   = endlessPlayerRngState[0];
	r->playerRng[1]   = endlessPlayerRngState[1];

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
	endlessExtraPerkCost[0]      = r->extraPerkCost;
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
	endlessExtraPerkCost[1]      = r->extraPerkCost2;
	endlessCleanseCost[1]        = r->cleanseCost2;
	endlessShopEntryCash[1]      = r->shopEntryCash2;
	player[1].superbombs         = (r->superbombs2 < 0) ? 0u : (r->superbombs2 > 10 ? 10u : (uint)r->superbombs2);
	endlessCleanseChargeCount[1] = r->cleanseCharges2;
	endlessPurchasedMods[1]      = r->purchasedMods2;
	endlessReviveHeld[1]         = r->reviveHeld2 != 0;
	endlessGambleRigged[1]       = r->gambleRigged2 != 0;
	endlessPlayerDowned[0]       = r->downed[0] != 0;
	endlessPlayerDowned[1]       = r->downed[1] != 0;
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

void endlessSaveSlot(JE_byte slot)
{
	if (slot < 1 || slot > SAVE_FILES_NUM)
		return;

	// The data-level half of the Hardcore no-save rule (the other half is JE_saveGame).
	// A plain no-op, not the clear branch: JE_saveGame refused too, so whatever record
	// the slot held before the attempt is still there and still owns its sidecar.
	if (endlessMode && endlessHardcore())
		return;

	endlessReadAllSlots();
	if (endlessMode)
		endlessCaptureCurrent(&endlessSlotCache[slot - 1]);
	else if (endlessSlotCache[slot - 1].used)
		endlessSlotCache[slot - 1].used = false;  // a normal save over an endless slot drops its stale record
	else
		return;  // campaign save over a non-endless slot: nothing to store or clear
	endlessWriteAllSlots();
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

	const Sint32 fields[12] = {
		endlessArmorBonus[p], (Sint32)endlessPurchasedMods[p], endlessBuffKind[p],
		endlessBuffCharge[p], endlessBuffCooldownUntil[p], endlessCleanseChargeCount[p],
		endlessLongCon[p], endlessShopTax[p], endlessRevivesUsed[p],
		(Sint32)endlessRerollCost[p], endlessHullCost[p], (Sint32)endlessShopEntryCash[p],
	};
	for (unsigned i = 0; i < COUNTOF(fields); ++i, n += 4)
	{
		const Uint32 v = (Uint32)fields[i];
		buf[n]     = (Uint8)(v >> 24);
		buf[n + 1] = (Uint8)(v >> 16);
		buf[n + 2] = (Uint8)(v >> 8);
		buf[n + 3] = (Uint8)v;
	}

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

	Sint32 fields[12];
	for (unsigned i = 0; i < COUNTOF(fields); ++i, n += 4)
	{
		fields[i] = (Sint32)(((Uint32)buf[n] << 24) | ((Uint32)buf[n + 1] << 16)
		                     | ((Uint32)buf[n + 2] << 8) | (Uint32)buf[n + 3]);
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
	endlessRerollCost[p]         = fields[9];
	endlessHullCost[p]           = fields[10];
	endlessShopEntryCash[p]      = fields[11];

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

/* Online co-op resume: the host serializes the live run through the same versioned codec the
 * sidecar uses and the joiner adopts it, so both machines resume from byte-identical state.
 * Each machine's own shop stock is redrawn from the seed rather than sent (see "Endless online"
 * in doc/notes.md). */
size_t endlessRunSerialize(Uint8 *out, size_t max)
{
	if (!endlessMode || out == NULL)
		return 0;

	EndlessSlotRec rec;
	endlessCaptureCurrent(&rec);

	Uint8 *bytes = NULL;
	size_t size = 0;
	if (!endlessTestEncode(&rec, &bytes, &size) || size > max)
	{
		free(bytes);
		return 0;
	}
	memcpy(out, bytes, size);
	free(bytes);
	return size;
}

bool endlessRunAdopt(const Uint8 *bytes, size_t len)
{
	EndlessSlotRec rec;
	if (bytes == NULL || !endlessTestDecode(bytes, len, &rec, NULL) || !rec.used)
		return false;

	endlessApplyCurrent(&rec);
	endlessMode = true;
	return true;
}

// Does this save slot hold an Endless run? Used by the load screen to keep Endless and Campaign
// sessions from offering each other's saves.
bool endlessSlotHasRun(JE_byte slot)
{
	if (slot < 1 || slot > SAVE_FILES_NUM)
		return false;
	endlessReadAllSlots();
	return endlessSlotCache[slot - 1].used;
}

bool endlessLoadSlot(JE_byte slot)
{
	if (slot < 1 || slot > SAVE_FILES_NUM)
		return false;

	endlessReadAllSlots();
	if (!endlessSlotCache[slot - 1].used)
		return false;

	endlessApplyCurrent(&endlessSlotCache[slot - 1]);
	endlessMode = true;  // JE_loadGame cleared it for a normal load; this slot is an endless run
	endlessRecordRunStart();  // resumed record gains count from this point
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
		// Hardcore reuses the committed course and its post-pick one-shot state.
		endlessLockedSortie = true;
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

	endlessApplyCurrent(&endlessSortieRec);                           // revert endless state (incl. run mode); also arms endlessResumeVisit
	memcpy(player, endlessSortiePlayer, sizeof(endlessSortiePlayer)); // revert loadout
	endlessSortieHave   = true;                                       // the committed-level statics are still valid
	endlessLockedSortie = false;   // no outpost is opened, so there is nothing to lock
	// Preserve outpost modifiers and episode for a later bail from the restarted zone.
	endlessSortieOutpostMods = outpostMods;
	endlessSortieOutpostEp   = outpostEp;

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
