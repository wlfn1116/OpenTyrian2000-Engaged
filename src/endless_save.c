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
#define ENDLESS_SAVE_VERSION 21
#define ENDLESS_SAVE_PERKS   32
#define ENDLESS_SAVE_PERKS_V10 16
#define ENDLESS_SAVE_PERK_CHARGER_V13 14
#define ENDLESS_SAVE_OFFERS     ENDLESS_PERK_OFFERS_MILESTONE
#define ENDLESS_SAVE_OFFERS_V12 3

// Spare cash-source/sink slots, so appending an EndlessCashSource or EndlessCashSink needs no
// version bump.
#define ENDLESS_SAVE_CASH_SOURCES 12
#define ENDLESS_SAVE_CASH_SINKS   12

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
	 * record loads into it, so a single-player run resumes exactly as it used to. */
	Uint8  coopHostCharts;   // Alternating course picks: is the host charting the next one?
	Uint8  courseChooser;    // EndlessCourseChooser the run was started under
	Sint32 armorBonus2;      // player 2's Reinforce tier
	Sint32 revivesUsed2, shopTax2, longCon2, buffKind2, buffCharge2, buffCooldownUntil2;
	Sint32 rerollCost2, hullCost2, bombCost2, extraPerkCost2, cleanseCost2, shopEntryCash2;
	Sint32 superbombs2, cleanseCharges2;
	Uint32 purchasedMods2;
	Uint8  reviveHeld2, gambleRigged2, downed[2];
	Uint8  perkTakenBy[2][ENDLESS_SAVE_PERKS];   // who picked what; the effective stacks are their sum
	Uint64 playerRng[2];                          // each player's own outpost draw stream
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
		endlessCourseMod[restoredCount] = r->courseMod[i];
		++restoredCount;
	}
	endlessCourseCnt = restoredCount;

	// Rebuild an invalid forced visit rather than turning another saved option into an Ambush.
	if (endlessCourseCnt == 0 || (endlessForced && dropped))
	{
		endlessReseed((Uint64)endlessRunDepth * 2);
		endlessGenerateCourses();
	}

	endlessNameCourseBaseLevels();  // populate the Radar perk's base-level cache for the restored chart
}

// Little-endian field I/O. A short read invalidates the optional Endless sidecar.
static void endlessPutU8(FILE *f, unsigned v)                 { Uint8 b = (Uint8)v; fwrite(&b, 1, 1, f); }
static void endlessPutU32(FILE *f, Uint32 v)                  { v = SDL_SwapLE32(v); fwrite(&v, 4, 1, f); }
static void endlessPutU64(FILE *f, Uint64 v)                  { v = SDL_SwapLE64(v); fwrite(&v, 8, 1, f); }
static void endlessPutBytes(FILE *f, const void *p, size_t n) { fwrite(p, 1, n, f); }
static bool endlessGetU8(FILE *f, Uint8 *v)                   { return fread(v, 1, 1, f) == 1; }
static bool endlessGetU32(FILE *f, Uint32 *v)                 { Uint32 b; if (fread(&b, 4, 1, f) != 1) return false; *v = SDL_SwapLE32(b); return true; }
static bool endlessGetU64(FILE *f, Uint64 *v)                 { Uint64 b; if (fread(&b, 8, 1, f) != 1) return false; *v = SDL_SwapLE64(b); return true; }
static bool endlessGetBytes(FILE *f, void *p, size_t n)       { return fread(p, 1, n, f) == n; }

static void endlessWriteRec(FILE *f, const EndlessSlotRec *r)
{
	endlessPutU8(f, r->used ? 1 : 0);

	const Sint32 s32[] = {
		r->runDepth, r->armorBonus, r->runKills, r->runBossKills, r->buffCharge, r->revivesUsed,
		r->shopTax, r->longCon, r->perkDepthDone, r->superbombs,
		r->rerollCost, r->hullCost, r->bombCost, r->extraPerkCost, r->cleanseCost, r->shopEntryCash,
		r->buffKind, r->cleanseCharges, r->perkChoiceN, r->courseCnt, r->lastEp,
	};
	for (unsigned i = 0; i < COUNTOF(s32); ++i)
		endlessPutU32(f, (Uint32)s32[i]);

	endlessPutU32(f, r->purchasedMods);
	endlessPutU8(f, r->reviveHeld);
	endlessPutU8(f, r->gambleRigged);
	endlessPutU8(f, r->gamblePerkWon);
	endlessPutU8(f, r->perkPending);
	endlessPutU8(f, r->lastSec);
	endlessPutU8(f, r->forced);

	endlessPutBytes(f, r->perkOwned, ENDLESS_SAVE_PERKS);
	endlessPutBytes(f, r->gambleMsg, sizeof(r->gambleMsg));
	endlessPutBytes(f, r->lastSpecialName, sizeof(r->lastSpecialName));

	for (unsigned i = 0; i < COUNTOF(r->perkChoice); ++i)
		endlessPutU32(f, (Uint32)r->perkChoice[i]);
	for (unsigned i = 0; i < ENDLESS_MAX_COURSES; ++i)
		endlessPutU32(f, (Uint32)r->courseEp[i]);
	for (unsigned i = 0; i < ENDLESS_MAX_COURSES; ++i)
		endlessPutU64(f, r->courseMod[i]);
	endlessPutBytes(f, r->courseSec, ENDLESS_MAX_COURSES);
	endlessPutBytes(f, r->courseFile, ENDLESS_MAX_COURSES);

	endlessPutBytes(f, r->itemAvail, sizeof(r->itemAvail));
	endlessPutBytes(f, r->itemAvailMax, sizeof(r->itemAvailMax));
	endlessPutBytes(f, r->seed, sizeof(r->seed));

	endlessPutU8(f, r->lockedSortie);        // v4 locked-sortie block
	endlessPutU64(f, r->sortieMods);         // v7: 64-bit (was U32 in v4-v6)
	endlessPutU8(f, r->sortieSec);
	endlessPutU32(f, (Uint32)r->sortieEp);
	endlessPutU8(f, r->sortieFile);

	endlessPutU32(f, (Uint32)r->buffCooldownUntil);  // v5 kill-fire recharge

	endlessPutU8(f, r->recentCount);                 // v6 anti-repeat recent-level ring
	for (unsigned i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
		endlessPutU32(f, (Uint32)r->recentEp[i]);
	endlessPutBytes(f, r->recentSec, ENDLESS_LEVEL_HISTORY);

	endlessPutU8(f, r->creditsShown);                // v9 zone-100 credits

	endlessPutU8(f, r->lastSong);                    // v10 per-zone music continuity
	endlessPutU32(f, (Uint32)r->lastSongDepth);

	endlessPutU8(f, r->starChartsOwed);              // v12 boons owed to a later outpost
	endlessPutU8(f, r->breakthroughOwed);

	endlessPutU8(f, r->runMode);                     // v15 Relaxed / Standard / Hardcore

	endlessPutU64(f, r->cashEarned);                 // v16 stored earnings only
	endlessPutU64(f, r->cashSpent);                  // v17 added spending and source detail
	for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SOURCES; ++i)
		endlessPutU64(f, r->cashBySource[i]);

	for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SINKS; ++i)  // v19 spending breakdown
		endlessPutU64(f, r->cashBySink[i]);

	endlessPutU8(f, r->usedCustom);                  // v20 custom-weapon record mark

	// v21 online co-op: the second player's own half of the outpost, plus the shared turn flag.
	endlessPutU8(f, r->coopHostCharts);
	endlessPutU8(f, r->courseChooser);
	const Sint32 coop32[] = {
		r->armorBonus2, r->revivesUsed2, r->shopTax2, r->longCon2, r->buffKind2, r->buffCharge2,
		r->buffCooldownUntil2, r->rerollCost2, r->hullCost2, r->bombCost2, r->extraPerkCost2,
		r->cleanseCost2, r->shopEntryCash2, r->superbombs2, r->cleanseCharges2,
	};
	for (unsigned i = 0; i < COUNTOF(coop32); ++i)
		endlessPutU32(f, (Uint32)coop32[i]);
	endlessPutU32(f, r->purchasedMods2);
	endlessPutU8(f, r->reviveHeld2);
	endlessPutU8(f, r->gambleRigged2);
	endlessPutU8(f, r->downed[0]);
	endlessPutU8(f, r->downed[1]);
	endlessPutBytes(f, r->perkTakenBy[0], ENDLESS_SAVE_PERKS);
	endlessPutBytes(f, r->perkTakenBy[1], ENDLESS_SAVE_PERKS);
	endlessPutU64(f, r->playerRng[0]);
	endlessPutU64(f, r->playerRng[1]);
}

static bool endlessReadRec(FILE *f, EndlessSlotRec *r, int version)
{
	memset(r, 0, sizeof(*r));

	Uint8 used;
	if (!endlessGetU8(f, &used))
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
		if (!endlessGetU32(f, &t))
			return false;
		*s32[i] = (Sint32)t;
	}

	if (!endlessGetU32(f, &r->purchasedMods)
	    || !endlessGetU8(f, &r->reviveHeld) || !endlessGetU8(f, &r->gambleRigged)
	    || !endlessGetU8(f, &r->gamblePerkWon) || !endlessGetU8(f, &r->perkPending)
	    || !endlessGetU8(f, &r->lastSec) || !endlessGetU8(f, &r->forced))
		return false;

	// v11 widened the perk block. Zero-filled newer slots remain unowned in older records.
	const size_t perkBytes = (version >= 11) ? ENDLESS_SAVE_PERKS : ENDLESS_SAVE_PERKS_V10;
	if (!endlessGetBytes(f, r->perkOwned, perkBytes)
	    || !endlessGetBytes(f, r->gambleMsg, sizeof(r->gambleMsg))
	    || !endlessGetBytes(f, r->lastSpecialName, sizeof(r->lastSpecialName)))
		return false;

	// v13 widened perk offers from three to five. Clamp counts to the version's stored width.
	const unsigned offerSlots = (version >= 13) ? ENDLESS_SAVE_OFFERS : ENDLESS_SAVE_OFFERS_V12;
	for (unsigned i = 0; i < offerSlots; ++i)
	{
		Uint32 t;
		if (!endlessGetU32(f, &t))
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
		if (!endlessGetU32(f, &t))
			return false;
		r->courseEp[i] = (Sint32)t;
	}
	for (unsigned i = 0; i < ENDLESS_MAX_COURSES; ++i)
	{
		if (version >= 7)
		{
			if (!endlessGetU64(f, &r->courseMod[i]))
				return false;
		}
		else
		{
			Uint32 t;   // v3-v6 stored the course mods 32-bit (high bits were unused back then)
			if (!endlessGetU32(f, &t))
				return false;
			r->courseMod[i] = t;
		}
	}
	if (!endlessGetBytes(f, r->courseSec, ENDLESS_MAX_COURSES))
		return false;
	if (version >= 8 && !endlessGetBytes(f, r->courseFile, ENDLESS_MAX_COURSES))
		return false;

	if (!endlessGetBytes(f, r->itemAvail, sizeof(r->itemAvail))
	    || !endlessGetBytes(f, r->itemAvailMax, sizeof(r->itemAvailMax))
	    || !endlessGetBytes(f, r->seed, sizeof(r->seed)))
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
		if (!endlessGetU8(f, &u8))
			return false;
		r->lockedSortie = u8;
		if (version >= 7)   // v7 widened sortieMods to 64-bit; v4-v6 stored it 32-bit
		{
			Uint64 u64;
			if (!endlessGetU64(f, &u64))
				return false;
			r->sortieMods = u64;
		}
		else
		{
			if (!endlessGetU32(f, &u32))
				return false;
			r->sortieMods = u32;
		}
		if (!endlessGetU8(f, &u8))
			return false;
		r->sortieSec = u8;
		if (!endlessGetU32(f, &u32))
			return false;
		r->sortieEp = (Sint32)u32;
		if (!endlessGetU8(f, &u8))
			return false;
		r->sortieFile = u8;
	}

	// Pre-v5 records leave the kill-fire purchase cooldown unlocked.
	if (version >= 5)
	{
		Uint32 u32;
		if (!endlessGetU32(f, &u32))
			return false;
		r->buffCooldownUntil = (Sint32)u32;
	}

	// Pre-v6 records resume with an empty recent-level window.
	if (version >= 6)
	{
		if (!endlessGetU8(f, &r->recentCount))
			return false;
		for (unsigned i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
		{
			Uint32 u32;
			if (!endlessGetU32(f, &u32))
				return false;
			r->recentEp[i] = (Sint32)u32;
		}
		if (!endlessGetBytes(f, r->recentSec, ENDLESS_LEVEL_HISTORY))
			return false;
		if (r->recentCount > ENDLESS_LEVEL_HISTORY)
			r->recentCount = ENDLESS_LEVEL_HISTORY;
	}

	// Pre-v9 records may show the zone-100 credits once after resuming.
	if (version >= 9 && !endlessGetU8(f, &r->creditsShown))
		return false;

	// Pre-v10 records derive the previous song when lastSong remains zero.
	if (version >= 10)
	{
		Uint32 u32;
		if (!endlessGetU8(f, &r->lastSong) || !endlessGetU32(f, &u32))
			return false;
		r->lastSongDepth = (Sint32)u32;
	}

	// Pre-v12 records resume without deferred Star Charts or Breakthrough rewards.
	if (version >= 12 && (!endlessGetU8(f, &r->starChartsOwed) || !endlessGetU8(f, &r->breakthroughOwed)))
		return false;

	// Pre-v15 records resume in Relaxed mode, matching their original behavior.
	if (version >= 15)
	{
		if (!endlessGetU8(f, &r->runMode))
			return false;
		if (r->runMode >= ENDLESS_RUNMODE_COUNT)
			r->runMode = ENDLESS_RUNMODE_RELAXED;
	}

	// v16 stores total earnings. v17 adds spending and per-source totals.
	if (version >= 16)
	{
		if (!endlessGetU64(f, &r->cashEarned))
			return false;
		if (version >= 17)
		{
			if (!endlessGetU64(f, &r->cashSpent))
				return false;
			for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SOURCES; ++i)
				if (!endlessGetU64(f, &r->cashBySource[i]))
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
		if (!endlessGetU64(f, &r->cashBySink[ENDLESS_SINK_GEAR]))
			return false;
	}
	else if (version >= 19)
	{
		for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SINKS; ++i)
			if (!endlessGetU64(f, &r->cashBySink[i]))
				return false;
	}

	// Pre-v20 records resume unmarked, so their record only gains a C if the rest of the run earns it.
	if (version >= 20 && !endlessGetU8(f, &r->usedCustom))
		return false;

	// Pre-v21 records are single-player: the zero-filled second slot leaves that player with
	// nothing bought, and the perk rows are rebuilt from the effective stacks below.
	if (version >= 21)
	{
		if (!endlessGetU8(f, &r->coopHostCharts) || !endlessGetU8(f, &r->courseChooser))
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
			if (!endlessGetU32(f, &t))
				return false;
			*coop32[i] = (Sint32)t;
		}
		if (!endlessGetU32(f, &r->purchasedMods2)
		    || !endlessGetU8(f, &r->reviveHeld2) || !endlessGetU8(f, &r->gambleRigged2)
		    || !endlessGetU8(f, &r->downed[0]) || !endlessGetU8(f, &r->downed[1])
		    || !endlessGetBytes(f, r->perkTakenBy[0], ENDLESS_SAVE_PERKS)
		    || !endlessGetBytes(f, r->perkTakenBy[1], ENDLESS_SAVE_PERKS)
		    || !endlessGetU64(f, &r->playerRng[0]) || !endlessGetU64(f, &r->playerRng[1]))
			return false;
		if (r->courseChooser >= ENDLESS_PICK_COUNT)
			r->courseChooser = ENDLESS_PICK_HOST;
	}
	else
	{
		r->coopHostCharts = 1;
		memcpy(r->perkTakenBy[0], r->perkOwned, ENDLESS_SAVE_PERKS);
	}

	return true;
}

int endlessSaveCurrentVersion(void)
{
	return ENDLESS_SAVE_VERSION;
}

static bool endlessTestDecode(const Uint8 *bytes, size_t size, EndlessSlotRec *rec, int *version)
{
	FILE *f = tmpfile();
	if (f == NULL)
		return false;
	if (size != 0 && fwrite(bytes, 1, size, f) != size)
	{
		fclose(f);
		return false;
	}
	rewind(f);

	Uint8 hdr[6];
	const bool okay = fread(hdr, 1, sizeof(hdr), f) == sizeof(hdr)
	               && memcmp(hdr, "OTES", 4) == 0
	               && hdr[4] >= 3 && hdr[4] <= ENDLESS_SAVE_VERSION
	               && hdr[5] >= 1 && hdr[5] <= SAVE_FILES_NUM
	               && endlessReadRec(f, rec, hdr[4]);
	if (okay && version != NULL)
		*version = hdr[4];
	fclose(f);
	return okay;
}

static bool endlessTestEncode(const EndlessSlotRec *rec, Uint8 **bytes, size_t *size)
{
	FILE *f = tmpfile();
	if (f == NULL)
		return false;
	const Uint8 hdr[6] = { 'O', 'T', 'E', 'S', ENDLESS_SAVE_VERSION, 1 };
	if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
	{
		fclose(f);
		return false;
	}
	endlessWriteRec(f, rec);
	const long end = ftell(f);
	if (end <= 0 || ferror(f))
	{
		fclose(f);
		return false;
	}
	rewind(f);

	*bytes = malloc((size_t)end);
	*size = (size_t)end;
	const bool okay = *bytes != NULL && fread(*bytes, 1, *size, f) == *size;
	if (!okay)
	{
		free(*bytes);
		*bytes = NULL;
		*size = 0;
	}
	fclose(f);
	return okay;
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
	    || (version >= 20 && first.usedCustom != 1))
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

// Load all slot records. Any sidecar-level error marks the optional cache unused.
static void endlessReadAllSlots(void)
{
	memset(endlessSlotCache, 0, sizeof(endlessSlotCache));

	FILE *f = dir_fopen(get_user_directory(), ENDLESS_SAVE_FILE, "rb");
	if (f == NULL)
		return;

	Uint8 hdr[6];
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)
	    || memcmp(hdr, "OTES", 4) != 0 || hdr[4] < 3 || hdr[4] > ENDLESS_SAVE_VERSION
	    || hdr[5] > SAVE_FILES_NUM)
	{
		fclose(f);  // accept v3 (pre-locked-sortie), v4 and v5; anything else is "no endless save"
		return;
	}

	const int count = hdr[5];
	for (int s = 0; s < count; ++s)
	{
		EndlessSlotRec rec;
		if (!endlessReadRec(f, &rec, hdr[4]))
			break;  // truncated: keep the full records already read
		if (s < SAVE_FILES_NUM)
			endlessSlotCache[s] = rec;
	}

	fclose(f);
}

// Write the fixed-layout slot cache.
static void endlessWriteAllSlots(void)
{
	FILE *f = dir_fopen_warn(get_user_directory(), ENDLESS_SAVE_FILE, "wb");
	if (f == NULL)
		return;

	const Uint8 hdr[6] = { 'O', 'T', 'E', 'S', ENDLESS_SAVE_VERSION, (Uint8)SAVE_FILES_NUM };
	fwrite(hdr, 1, sizeof(hdr), f);
	for (int s = 0; s < SAVE_FILES_NUM; ++s)
		endlessWriteRec(f, &endlessSlotCache[s]);

	fclose(f);
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
	endlessPerkRederive();
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

	// Store two hex digits per perk in the serialized PERK_* order.
	char perks[2 * PERK_COUNT + 1];
	int n = 0;
	for (int p = 0; p < PERK_COUNT && n + 2 < (int)sizeof(perks); ++p)
		n += snprintf(perks + n, sizeof(perks) - (size_t)n, "%02X", endlessPerkOwned[p] & 0xFF);
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

void endlessRecordConfigSave(ConfigSection *section)
{
	if (section == NULL)
		return;

	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		config_set_int_option(section, endlessBestZoneKey[m], endlessBestZoneUntagged[m]);
		config_set_int_option(section, endlessBestZoneCustomKey[m], endlessBestZoneUntaggedCustom[m] ? 1 : 0);

		char zones[ENDLESS_DIFFICULTY_COUNT * 8], marks[ENDLESS_DIFFICULTY_COUNT + 1];
		size_t len = 0;
		for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
		{
			len += snprintf(zones + len, sizeof(zones) - len, "%s%d",
			                (d > 0) ? "," : "", endlessBestZoneDiff[m][d]);
			marks[d] = endlessBestZoneDiffCustom[m][d] ? '1' : '0';
		}
		marks[ENDLESS_DIFFICULTY_COUNT] = '\0';

		config_set_string_option(section, endlessBestZoneDiffKey[m], zones);
		config_set_string_option(section, endlessBestZoneDiffCustomKey[m], marks);
	}
}

void endlessRecordConfigLoad(const ConfigSection *section)
{
	if (section == NULL)
		return;

	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		int best = 0;
		config_get_int_option(section, endlessBestZoneKey[m], &best);
		endlessBestZoneUntagged[m] = (best > 0) ? best : 0;  // a hand-edited negative reads as "no record"

		int custom = 0;
		config_get_int_option(section, endlessBestZoneCustomKey[m], &custom);
		endlessBestZoneUntaggedCustom[m] = (endlessBestZoneUntagged[m] > 0) && (custom != 0);

		for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
		{
			endlessBestZoneDiff[m][d] = 0;
			endlessBestZoneDiffCustom[m][d] = false;
		}

		const char *zones = NULL;
		if (config_get_string_option(section, endlessBestZoneDiffKey[m], &zones) && zones != NULL)
		{
			for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT && *zones != '\0'; ++d)
			{
				const int zone = (int)strtol(zones, NULL, 10);
				endlessBestZoneDiff[m][d] = (zone > 0) ? zone : 0;

				const char *const comma = strchr(zones, ',');
				zones = (comma != NULL) ? comma + 1 : "";
			}
		}

		const char *marks = NULL;
		if (config_get_string_option(section, endlessBestZoneDiffCustomKey[m], &marks) && marks != NULL)
		{
			for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT && marks[d] != '\0'; ++d)
				endlessBestZoneDiffCustom[m][d] = (endlessBestZoneDiff[m][d] > 0) && (marks[d] == '1');
		}
	}
	endlessRecordRunStart();   // nothing is running yet, so the baseline is the record
}
