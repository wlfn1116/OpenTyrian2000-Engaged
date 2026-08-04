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
unsigned endlessSortiePrePurchased = 0;
int      endlessSortiePreCleanse   = 0;
int      endlessSortiePreLongCon   = 0;
// The mutators in force at the outpost the sortie launched FROM (captured on opening it), which is
// the previous sector's set -- not the committed level's. An unlocked bail reopens that same outpost,
// so it must price and stock itself off these, or the level's own Merchant's Favor / Cursed Bounty
// would leak backwards into a shop the player already visited.
Uint64   endlessSortieOutpostMods = 0;

// tyrian.sav has a fixed checksummed layout, so Endless uses a per-slot sidecar.

#define ENDLESS_SAVE_FILE    "endless.sav"
#define ENDLESS_SAVE_VERSION 19
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
	Uint8  breakthroughOwed;  // BREAKTHROUGH: bonus perk picks still owed (a count -- two can queue)

	// Added in v15.
	Uint8  runMode;  // EndlessRunMode: the run's Relaxed/Standard/Hardcore choice (never Hardcore on disk)

	// Added in v16 (which stored this one field alone), widened in v17.
	Uint64 cashEarned;  // running total of cash taken in, for the run-over tally
	Uint64 cashSpent;   // ...and of everything that left the wallet
	Uint64 cashBySource[ENDLESS_SAVE_CASH_SOURCES];  // the earnings breakdown, indexed by EndlessCashSource

	// Added in v19 (v18 briefly stored only the gear sink as a single field).
	Uint64 cashBySink[ENDLESS_SAVE_CASH_SINKS];  // the spending breakdown, indexed by EndlessCashSink
} EndlessSlotRec;

// One record per save slot, mirrored to endless.sav. Read-modify-write on each save keeps the
// other slots' records intact.
static EndlessSlotRec endlessSlotCache[SAVE_FILES_NUM];

// Restore a chart from disk while migrating v7-and-older records that did not persist courseFile.
// Invalid legacy entries (notably Episode 1 section 44 / nonexistent file 20) are dropped and the
// remaining parallel arrays are compacted. If nothing usable remains, regenerate deterministically
// for this depth so the outpost always has a launchable course.
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

	// A forced visit represents one specific unavoidable course; if that entry was invalid, rebuild
	// the whole visit rather than silently turning a different saved option into an Ambush.
	if (endlessCourseCnt == 0 || (endlessForced && dropped))
	{
		endlessReseed((Uint64)endlessRunDepth * 2);
		endlessGenerateCourses();
	}

	endlessNameCourseBaseLevels();  // populate the Radar perk's base-level cache for the restored chart
}

// Little-endian field I/O over a FILE*. The write side is fire-and-forget; the read side never
// dies -- any short/failed read just aborts the load, so a missing or corrupt sidecar simply
// means "no endless save".
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

	endlessPutU64(f, r->cashEarned);                 // v16 cash ledger (earned alone)...
	endlessPutU64(f, r->cashSpent);                  // ...widened in v17: spent, then the breakdown
	for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SOURCES; ++i)
		endlessPutU64(f, r->cashBySource[i]);

	for (unsigned i = 0; i < ENDLESS_SAVE_CASH_SINKS; ++i)  // v19 spending breakdown
		endlessPutU64(f, r->cashBySink[i]);
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

	// v11 widened the perk block; v3..v10 wrote 16 bytes. memset(r,0) above zeroed the extra
	// slots, so reading the narrow legacy width just leaves the newer perks unowned.
	const size_t perkBytes = (version >= 11) ? ENDLESS_SAVE_PERKS : ENDLESS_SAVE_PERKS_V10;
	if (!endlessGetBytes(f, r->perkOwned, perkBytes)
	    || !endlessGetBytes(f, r->gambleMsg, sizeof(r->gambleMsg))
	    || !endlessGetBytes(f, r->lastSpecialName, sizeof(r->lastSpecialName)))
		return false;

	// v13 widened the offer list for the milestone 1-of-5 pick; v3..v12 wrote 3 entries. Clamping the
	// COUNT to what the file stored keeps the memset-zeroed tail slots from reading as real offers.
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

	// v4 locked-sortie block. Older (v3) records don't carry it -- the memset above already left
	// lockedSortie = 0, so they simply read as "not a locked outpost".
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

	// v5 kill-fire buff recharge. Older (v3/v4) records lack it -- the memset above left
	// buffCooldownUntil = 0 ("no lock"), so a resumed pre-v5 run can buy immediately.
	if (version >= 5)
	{
		Uint32 u32;
		if (!endlessGetU32(f, &u32))
			return false;
		r->buffCooldownUntil = (Sint32)u32;
	}

	// v6 anti-repeat recent-level ring. Older records lack it -- the memset above left recentCount = 0,
	// so a resumed pre-v6 run just starts with an empty window (it refills as zones are played).
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

	// v9 zone-100 credits. Older records lack it -- the memset above left creditsShown = 0, so a
	// pre-v9 run already past zone 100 gets one (harmless) showing at its next outpost.
	if (version >= 9 && !endlessGetU8(f, &r->creditsShown))
		return false;

	// v10 per-zone music continuity. Older records lack it -- lastSong stays 0 ("nothing remembered"),
	// and the picker falls back to deriving the previous zone's song approximately.
	if (version >= 10)
	{
		Uint32 u32;
		if (!endlessGetU8(f, &r->lastSong) || !endlessGetU32(f, &u32))
			return false;
		r->lastSongDepth = (Sint32)u32;
	}

	// v12 Star Charts / Breakthrough debts. Older records lack them -- the memset above left both at 0,
	// so a resumed pre-v12 run simply owes nothing (it can only ever have been charted in a v12 build).
	if (version >= 12 && (!endlessGetU8(f, &r->starChartsOwed) || !endlessGetU8(f, &r->breakthroughOwed)))
		return false;

	// v15 run mode. Older records predate Standard and were all written by a build whose only
	// saveable run behaved like Relaxed, so the memset above (runMode 0) already resumes them right.
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
			// A v16 run knew what it had earned but not how, nor what it had spent. Book the lot as
			// untagged rather than leaving a breakdown that doesn't sum to the total.
			r->cashBySource[ENDLESS_CASH_OTHER] = r->cashEarned;
		}
	}

	// Spending breakdown. v18 briefly stored only the gear sink (the slice trade-in refunds cancel
	// against); v19 keeps the full per-sink array. Pre-v18 resumes all-zero (the memset), so gear
	// bought before the save sells as "gear sold" income instead of cancelling -- older behaviour,
	// and still consistent with earned - spent == wallet.
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
	return true;
}

// Load every slot's record into the cache (all-unused on a missing / short / corrupt / wrong-
// version file -- this is optional data, so any problem just means "no endless save").
static void endlessReadAllSlots(void)
{
	memset(endlessSlotCache, 0, sizeof(endlessSlotCache));

	FILE *f = dir_fopen(get_user_directory(), ENDLESS_SAVE_FILE, "rb");
	if (f == NULL)
		return;

	Uint8 hdr[6];
	if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr)
	    || memcmp(hdr, "OTES", 4) != 0 || hdr[4] < 3 || hdr[4] > ENDLESS_SAVE_VERSION)
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

// Write the whole cache back to disk (fixed record layout, so a slot is simply overwritten).
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
	// Audit before copying: the ledger must agree with the wallet this record is snapshotted
	// ALONGSIDE (in tyrian.sav for a save, in endlessSortiePlayer for a sortie), and the restore
	// re-anchors the mark, so any drift not booked now would be dropped for good. Safe here:
	// neither caller runs while the upgrade sub-menu is showing its fake trade-in balance.
	endlessCashAudit();

	memset(r, 0, sizeof(*r));
	r->used = true;

	r->runDepth      = endlessRunDepth;
	r->armorBonus    = endlessArmorBonus;
	r->runKills      = endlessRunKills;
	r->runBossKills  = endlessRunBossKills;
	r->buffCharge    = endlessBuffCharge;
	r->buffCooldownUntil = endlessBuffCooldownUntil;
	r->revivesUsed   = endlessRevivesUsed;
	r->shopTax       = endlessShopTax;
	r->longCon       = endlessLongCon;
	r->perkDepthDone = endlessPerkDepthDone;
	r->superbombs    = player[0].superbombs;
	r->reviveHeld    = endlessReviveHeld ? 1 : 0;
	r->gambleRigged  = endlessGambleRigged ? 1 : 0;
	for (int i = 0; i < ENDLESS_SAVE_PERKS; ++i)
		r->perkOwned[i] = (i < PERK_COUNT) ? endlessPerkOwned[i] : 0;

	r->rerollCost    = (Sint32)endlessRerollCost;
	r->hullCost      = endlessHullCost;
	r->bombCost      = (Sint32)endlessBombCost;
	r->extraPerkCost = (Sint32)endlessExtraPerkCost;
	r->cleanseCost   = (Sint32)endlessCleanseCost;
	r->shopEntryCash = (Sint32)endlessShopEntryCash;
	r->purchasedMods = endlessPurchasedMods;
	r->buffKind      = endlessBuffKind;
	r->cleanseCharges= endlessCleanseChargeCount;
	r->gamblePerkWon = endlessGamblePerkWon ? 1 : 0;
	r->perkPending   = endlessPerkPending ? 1 : 0;
	SDL_strlcpy(r->gambleMsg, endlessGambleMsg, sizeof(r->gambleMsg));
	SDL_strlcpy(r->lastSpecialName, endlessLastSpecialName, sizeof(r->lastSpecialName));

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

	// Locked "gave up the level" outpost (v4): only meaningful when saving FROM the locked shop
	// (endlessLockedSortie). memset(r,0) at the top leaves these cleared for a normal save.
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

	// Run mode (v15). On disk this is only ever Relaxed or Standard (Hardcore never saves), but the
	// sortie snapshot shares this record, and there it carries a Hardcore run's mode across a bail.
	r->runMode = (Uint8)endlessRunMode;

	// v16 cash ledger + v19 sink breakdown. The spare on-disk slots past ENDLESS_CASH_SOURCES /
	// ENDLESS_CASH_SINKS stay zeroed by the memset.
	r->cashEarned = endlessRunCashEarned;
	r->cashSpent  = endlessRunCashSpent;
	for (int i = 0; i < ENDLESS_CASH_SOURCES; ++i)
		r->cashBySource[i] = endlessCashBySource[i];
	for (int i = 0; i < ENDLESS_CASH_SINKS; ++i)
		r->cashBySink[i] = endlessCashBySink[i];
}

// Lay a saved record back over the live state. endlessResetRun first, so per-zone/per-visit
// transients we DON'T persist (combat timers, elite rolls, ...) start clean; then restore both
// the run and the outpost snapshot, and arm endlessResumeVisit so the next outpost is the
// SAVED one rather than a fresh (free) reroll.
static void endlessApplyCurrent(const EndlessSlotRec *r)
{
	endlessResetRun();

	endlessRunDepth      = r->runDepth;
	endlessArmorBonus    = r->armorBonus;
	endlessRunKills      = r->runKills;
	endlessRunBossKills  = r->runBossKills;
	endlessBuffCharge    = r->buffCharge;
	endlessBuffCooldownUntil = r->buffCooldownUntil;
	endlessRevivesUsed   = r->revivesUsed;
	endlessShopTax       = r->shopTax;
	endlessLongCon       = r->longCon;
	endlessPerkDepthDone = r->perkDepthDone;
	player[0].superbombs = (r->superbombs < 0) ? 0 : (r->superbombs > 10 ? 10 : r->superbombs);
	endlessReviveHeld    = r->reviveHeld != 0;
	endlessGambleRigged  = r->gambleRigged != 0;
	for (int i = 0; i < PERK_COUNT && i < ENDLESS_SAVE_PERKS; ++i)
	{
		int v = r->perkOwned[i];
		const int maxs = endlessPerkTable[i].maxStack;
		endlessPerkOwned[i] = (JE_byte)(v < 0 ? 0 : (v > maxs ? maxs : v));
	}

	endlessRerollCost         = r->rerollCost;
	endlessHullCost           = r->hullCost;
	endlessBombCost           = r->bombCost;
	endlessExtraPerkCost      = r->extraPerkCost;
	endlessCleanseCost        = r->cleanseCost;
	endlessShopEntryCash      = r->shopEntryCash;
	endlessPurchasedMods      = r->purchasedMods;
	endlessBuffKind           = r->buffKind;
	endlessCleanseChargeCount = r->cleanseCharges;
	endlessGamblePerkWon      = r->gamblePerkWon != 0;
	endlessPerkPending        = r->perkPending != 0;
	SDL_strlcpy(endlessGambleMsg, r->gambleMsg, sizeof(endlessGambleMsg));
	SDL_strlcpy(endlessLastSpecialName, r->lastSpecialName, sizeof(endlessLastSpecialName));
	endlessSetSeed(r->seed);  // restore the run seed (endlessResetRun blanked it); rehashes + primes the stream

	endlessPerkChoiceN = endlessClamp(r->perkChoiceN, 0, ENDLESS_SAVE_OFFERS);
	for (int i = 0; i < ENDLESS_SAVE_OFFERS; ++i)
		endlessPerkChoice[i] = r->perkChoice[i];

	endlessLastEp  = r->lastEp;
	endlessLastSec = r->lastSec;
	endlessForced  = r->forced != 0;

	// Anti-repeat recent-level ring (v6). endlessResetRun (above) already cleared it, so a pre-v6
	// record (recentCount 0) simply resumes with an empty window.
	endlessRecentCount = (r->recentCount > ENDLESS_LEVEL_HISTORY) ? ENDLESS_LEVEL_HISTORY : r->recentCount;
	for (int i = 0; i < ENDLESS_LEVEL_HISTORY; ++i)
	{
		endlessRecentEp[i]  = r->recentEp[i];
		endlessRecentSec[i] = r->recentSec[i];
	}

	// Zone-100 credits (v9): endlessResetRun above cleared the flag, so a pre-v9 record simply reads
	// as "not shown yet".
	endlessCreditsShown = r->creditsShown != 0;

	// Per-zone music continuity (v10). A pre-v10 record has lastSong 0; force the depth back to "none"
	// with it, since a zeroed record would otherwise read as a real entry for depth 0.
	endlessLastSong      = r->lastSong;
	endlessLastSongDepth = (r->lastSong != 0) ? r->lastSongDepth : -1;

	// Boons owed to a later outpost (v12). endlessResetRun above cleared both, so a pre-v12 record just
	// resumes owing nothing.
	endlessStarChartsOwed   = r->starChartsOwed != 0;
	endlessBreakthroughOwed = r->breakthroughOwed;

	// Run mode (v15). endlessResetRun above reset it to Relaxed, which is also what a pre-v15 record
	// (runMode 0) resumes as. A bail/retry goes through here too, so this is what keeps a Hardcore
	// run Hardcore across the reset the sortie restore performs.
	endlessRunMode = (r->runMode < ENDLESS_RUNMODE_COUNT) ? (EndlessRunMode)r->runMode
	                                                      : ENDLESS_RUNMODE_RELAXED;

	endlessRestoreSavedCourses(r);

	memcpy(itemAvail, r->itemAvail, sizeof(itemAvail));
	memcpy(itemAvailMax, r->itemAvailMax, sizeof(itemAvailMax));

	// Locked-sortie retry (v4): a save made from the "gave up the level" outpost reopens locked and
	// relaunches the same committed level. endlessResetRun (above) already cleared these to unlocked.
	endlessLockedSortie = r->lockedSortie != 0;
	if (endlessLockedSortie)
	{
		endlessSortieModsV = r->sortieMods;
		endlessSortieSec   = (JE_byte)r->sortieSec;
		endlessSortieEp    = r->sortieEp;
		endlessSortieFile  = (JE_byte)r->sortieFile;
		endlessSortieHave  = true;
	}

	// Cash ledger (v16). endlessResetRun above zeroed it all, which is also what a pre-v16 record
	// restores as. The mark is re-anchored to whatever wallet this record is being laid over -- the
	// two sortie paths below re-anchor again after they memcpy the snapshotted loadout back.
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

bool endlessLoadSlot(JE_byte slot)
{
	if (slot < 1 || slot > SAVE_FILES_NUM)
		return false;

	endlessReadAllSlots();
	if (!endlessSlotCache[slot - 1].used)
		return false;

	endlessApplyCurrent(&endlessSlotCache[slot - 1]);
	endlessMode = true;  // JE_loadGame cleared it for a normal load; this slot is an endless run
	endlessRecordRunStart();  // a resumed run measures its "(+n)" from here -- the zones it already flew are banked
	return true;
}

// True from the moment a run is restored until its outpost reopens and consumes the snapshot.
// The title-load path runs the outpost at JE_main's entry (flag already cleared by the time a
// level starts); an in-shop load can't, so JE_main checks this after JE_loadMap and detours to
// the outpost when it's still set (see tyrian2.c). Mirrors the endlessBetweenLevels gate.
bool endlessResumePending(void) { return endlessResumeVisit; }

// Quit Level and locked-outpost retry.
// The endless run/outpost half of the launch-time snapshot (the loadout + committed-level half are
// the endlessSortie* primitives up top). Reusing the save record means depth, perks, prices,
// purchased mods, courses, shop stock and seed are all reverted by the tested capture/apply code.
static EndlessSlotRec endlessSortieRec;

void endlessCaptureSortie(void)
{
	if (!endlessMode)
		return;

	// We're about to run a level, so by definition we're no longer sitting in a locked "gave up"
	// outpost. Clear it before the capture so the snapshot itself reads as unlocked.
	endlessLockedSortie = false;

	endlessCaptureCurrent(&endlessSortieRec);                          // endless run + outpost state
	memcpy(endlessSortiePlayer, player, sizeof(endlessSortiePlayer));  // full loadout (cash / items / superbombs)
	endlessSortieModsV = endlessActiveMods;   // the committed level's mutators...
	endlessSortieSec   = mainLevel;           // ...its section (== the level being loaded)...
	endlessSortieEp    = episodeNum;          // ...its episode...
	endlessSortieFile  = lvlFileNum;          // ...and its level file
	endlessSortieHave  = true;
}

void endlessRestoreSortie(void)
{
	if (!endlessSortieHave)
		return;

	// Grab the one-shots before endlessApplyCurrent -> endlessResetRun clobbers them. (The run mode
	// needs no such rescue: it rides in the snapshot record itself, so the reset the apply performs
	// is undone by the apply -- a bail can never silently un-lock the outpost or re-enable saving.)
	const unsigned preBuff     = endlessSortiePrePurchased;
	const int      preCleanse  = endlessSortiePreCleanse;
	const int      preLongCon  = endlessSortiePreLongCon;
	const Uint64   outpostMods = endlessSortieOutpostMods;

	endlessApplyCurrent(&endlessSortieRec);                           // revert endless state (incl. run mode); arms endlessResumeVisit (also cleared endlessSortieHave via endlessResetRun)
	memcpy(player, endlessSortiePlayer, sizeof(endlessSortiePlayer)); // revert loadout (wins over the superbombs field applyCurrent touched)
	endlessActiveMods   = endlessSortieModsV;                         // the committed level's mutators (for the relaunch)
	endlessSortieHave   = true;                                       // the committed-level statics are still valid -- keep the invariant
	endlessSortieOutpostMods = outpostMods;                           // still the same outpost -- keep its mutators across the reset

	if (endlessHardcore())
	{
		// Hardcore reuses the committed course and its post-pick one-shot state.
		endlessLockedSortie = true;
	}
	else
	{
		// Relaxed and Standard reopen before course selection, so restore pre-pick one-shots.
		endlessLockedSortie       = false;
		endlessPurchasedMods      = preBuff;
		endlessCleanseChargeCount = preCleanse;
		endlessLongCon            = preLongCon;
		// Restore the outpost's modifiers; the next course selection replaces them.
		endlessActiveMods         = endlessSortieOutpostMods;
	}

	endlessCashResync();  // the reverted wallet is the new baseline; the tally rode in on the record
}

bool endlessSortieValid(void) { return endlessSortieHave; }

// Death menu "Restart Zone": revert to the launch-time snapshot and re-arm the same level with no
// outpost visit in between. The one-shots the course pick consumed stay consumed -- the relaunch
// replays that very pick, so refunding them (what the unlocked Quit Level path does, since there
// the player re-picks a course) would spend them twice.
void endlessRestartSortie(void)
{
	if (!endlessSortieHave)
		return;

	const Uint64 outpostMods = endlessSortieOutpostMods;  // rescue it from the reset inside the apply

	endlessApplyCurrent(&endlessSortieRec);                           // revert endless state (incl. run mode); also arms endlessResumeVisit
	memcpy(player, endlessSortiePlayer, sizeof(endlessSortiePlayer)); // revert loadout
	endlessSortieHave   = true;                                       // the committed-level statics are still valid
	endlessLockedSortie = false;   // no outpost is opened, so there is nothing to lock
	// The outpost this zone launched from is unchanged, and a later bail out of the retry reopens it
	// -- so its mutators have to survive the restart, or that bail would reopen the shop with none.
	endlessSortieOutpostMods = outpostMods;

	endlessCashResync();           // the reverted wallet is the new baseline (the tally rode in on the record)
	endlessResumeVisit = false;    // endlessBetweenLevels normally consumes this; nothing will here
	endlessArmLockedRelaunch();    // re-arm the committed level: episode, section, level file, mutators
}

// The committed level's fine PAYOUT term at the run difficulty, from the sortie snapshot (the
// authoritative "level being played" record, valid through the clear + outpost and across a reload).
// The clear payout reads this so the banked cash matches the course card the player picked; 0 when no
// sortie is live (e.g. a debug launch that set endlessActiveMods without committing a level).
int endlessSortiePayoutMille(void)
{
	if (!endlessSortieHave)
		return 0;
	return endlessLevelPayoutMille(endlessSortieEp, endlessSortieFile, difficultyLevel);
}

void endlessArmLockedRelaunch(void)
{
	// Re-arm the same level directly -- not via endlessSelectCourse, whose one-shot consumption
	// (Long Con decrement, Sabotage/cleanse charges, purchased-mod fold-in) must not fire twice.
	if (endlessSortieEp != episodeNum)
		JE_initEpisode((JE_byte)endlessSortieEp);  // may reset mainLevel/lvlFileNum -- so set them after
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

	// The mod mask is 64-bit and config options are int, so it goes as hex text rather than as a
	// pair of halves that could be reassembled wrongly.
	char buf[32];
	snprintf(buf, sizeof(buf), "%016" PRIX64, (Uint64)endlessActiveMods);
	config_set_string_option(section, "mods", buf);

	// Two hex digits per perk, in perk-id order. That order is already contracted as stable (it is
	// the endless.sav slot index -- see the PERK_* enum), so this needs no separate contract.
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

void endlessRecordConfigSave(ConfigSection *section)
{
	if (section == NULL)
		return;

	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
		config_set_int_option(section, endlessBestZoneKey[m], endlessBestZone[m]);
}

void endlessRecordConfigLoad(const ConfigSection *section)
{
	if (section == NULL)
		return;

	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
	{
		int best = 0;
		config_get_int_option(section, endlessBestZoneKey[m], &best);
		endlessBestZone[m] = (best > 0) ? best : 0;   // a hand-edited negative reads as "no record yet"
	}
	endlessRecordRunStart();   // nothing is running yet, so the baseline is the record
}
