/* Endless run state, lifecycle, milestones, and run-over screen. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "fonthand.h"
#include "helptext.h"
#include "keyboard.h"
#include "loudness.h"
#include "mainint.h"
#include "mtrand.h"
#include "rollback.h"
#include "nortsong.h"
#include "nortvars.h"
#include "palette.h"
#include "pcxload.h"
#include "network.h"
#include "player.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

// Run state.

int      endlessRunDepth  = 0;
Uint64   endlessActiveMods = 0;
int      endlessArmorBonus[2] = { 0, 0 };
int      endlessRunKills = 0;
int      endlessRunBossKills = 0;

// Run cash ledger. The mark mirrors the wallet after each declared credit, debit, or trade;
// endlessCashAudit books and reports any undeclared drift.
Uint64 endlessRunCashEarned = 0;
Uint64 endlessRunCashSpent  = 0;
Uint64 endlessCashBySource[ENDLESS_CASH_SOURCES] = { 0 };
Uint64 endlessCashBySink[ENDLESS_CASH_SINKS] = { 0 };
static ulong endlessCashMark = 0;

/* Online co-op. The sector is shared; wallets, gear and personal upgrades are not. Each machine
 * outfits, spends for and tallies its OWN ship, and mirrors the other's state at the outpost. */

uint endlessEconomyIndex(void)
{
	return coopEndlessMode ? gameplay_local_player_index() : 0;
}

uint endlessPartnerIndex(void)
{
	return coopEndlessMode ? 1 - gameplay_local_player_index() : 0;
}

// This machine's wallet, which is the one the run ledger follows.
static ulong endlessWallet(void)
{
	return player[endlessEconomyIndex()].cash;
}

// Ships a per-player effect has to walk. One outside co-op, so solo behaviour is untouched.
uint endlessEffectPlayers(void)
{
	return coopEndlessMode ? (uint)COUNTOF(player) : 1u;
}

bool endlessCoopComboShared = false;
EndlessCourseChooser endlessCourseChooser = ENDLESS_PICK_HOST;
bool endlessCoopHostCharts = true;
bool endlessPlayerDowned[2] = { false, false };
/* The peer pressed Quit in the in-game menu. Endless answers it the way the local press is
 * answered: revert to the launch snapshot and reopen the outpost, together. Anything the sortie
 * snapshot cannot restore leaves the run where it was, so this is never a teardown. */
void endlessCoopPeerQuitLevel(void)
{
	if (endlessCoop() && endlessSortieValid())
		endlessQuitToOutpost = true;
}

Uint64 endlessPlayerMods[2] = { 0, 0 };
static uint endlessFxPlayerIdx = 0;

void endlessSetFxPlayer(uint p) { endlessFxPlayerIdx = (p < COUNTOF(player)) ? p : 0; }
uint endlessFxPlayer(void)      { return coopEndlessMode ? endlessFxPlayerIdx : 0; }

/* Split what each player bought: the personal half lands on their own mask, the rest (a shop
 * discount, a bulked-up boss, a rush of rammers) changes the sector for both. Called once, when
 * the course is committed. */
void endlessApplyPurchasedMods(void)
{
	for (uint p = 0; p < COUNTOF(player); ++p)
		endlessActiveMods |= endlessPurchasedMods[p] & ~ENDLESS_PERSONAL_MOD_MASK;

	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		const Uint64 mine = endlessPurchasedMods[p] & ENDLESS_PERSONAL_MOD_MASK;
		endlessPlayerMods[p] = endlessFoldPurchasedMods(endlessActiveMods, mine);
	}
}

const char *endlessCourseChooserName(EndlessCourseChooser mode)
{
	switch (mode)
	{
	case ENDLESS_PICK_GUEST:     return "Guest";
	case ENDLESS_PICK_ALTERNATE: return "Alternating";
	case ENDLESS_PICK_COINFLIP:  return "50-50";
	default:                     return "Host";
	}
}

bool endlessLocalPlayerCharts(void)
{
	if (!endlessCoop())
		return true;

	switch (endlessCourseChooser)
	{
	case ENDLESS_PICK_GUEST:     return !network_is_host;
	case ENDLESS_PICK_ALTERNATE: return endlessCoopHostCharts == network_is_host;
	case ENDLESS_PICK_COINFLIP:
		// Derived from the seed rather than drawn from a stream, so the coin never depends on how
		// much either player has shopped and both machines read the same side of it.
		return (((endlessSplitMixSeed((Uint64)endlessRunDepth * 4 + 1) >> 33) & 1) != 0) == network_is_host;
	default:                     return network_is_host;
	}
}

void endlessAdvanceCourseTurn(void)
{
	if (endlessCoop() && endlessCourseChooser == ENDLESS_PICK_ALTERNATE)
		endlessCoopHostCharts = !endlessCoopHostCharts;
}

bool endlessAnyPlayerFlying(void)
{
	for (uint p = 0; p < endlessEffectPlayers(); ++p)
		if (player[p].is_alive && !endlessPlayerDowned[p])
			return true;
	return false;
}

/* A partner who went down mid-zone comes back at the outpost the survivor reached: full hull,
 * no shield, everything they owned still theirs. */
void endlessReviveDownedAtOutpost(void)
{
	for (uint p = 0; p < endlessEffectPlayers(); ++p)
	{
		if (!endlessPlayerDowned[p])
			continue;
		endlessPlayerDowned[p] = false;
		player[p].is_alive = true;
		player[p].exploding_ticks = 0;
		player[p].armor = player[p].initial_armor;
		player[p].shield = 0;
	}
}

// 12 digits: high enough that no run reaches it, low enough that the run-over column can print it.
#define ENDLESS_CASH_TALLY_MAX  999999999999ULL

// Order tracks the append-only EndlessCashSource enum.
static const char *const endlessCashSourceNames[ENDLESS_CASH_SOURCES] = {
	"kills", "pickups", "bounties", "zone clears", "interest", "gambling", "declined perks", "untagged",
	"starting stake", "gear sold",
};

const char *endlessCashSourceName(EndlessCashSource src)
{
	return ((unsigned)src < ENDLESS_CASH_SOURCES) ? endlessCashSourceNames[src] : "";
}

static const char *const endlessCashSinkNames[ENDLESS_CASH_SINKS] = {
	"gear", "supplies", "buffs", "revives", "perk picks", "rerolls", "hull", "gambling",
};

const char *endlessCashSinkName(EndlessCashSink sink)
{
	return ((unsigned)sink < ENDLESS_CASH_SINKS) ? endlessCashSinkNames[sink] : "";
}

static void endlessCashAddSat(Uint64 *tally, Uint64 amount)
{
	*tally = (amount > ENDLESS_CASH_TALLY_MAX - *tally) ? ENDLESS_CASH_TALLY_MAX : *tally + amount;
}

// Book income in the ledger; the caller updates the wallet.
static void endlessCashBook(Uint64 amount, EndlessCashSource src)
{
	if ((unsigned)src >= ENDLESS_CASH_SOURCES)
		src = ENDLESS_CASH_OTHER;
	// Clamp once so the total and source breakdown cannot diverge.
	if (amount > ENDLESS_CASH_TALLY_MAX - endlessRunCashEarned)
		amount = ENDLESS_CASH_TALLY_MAX - endlessRunCashEarned;
	endlessRunCashEarned += amount;
	endlessCashBySource[src] += amount;
}

// Book undeclared wallet drift, then re-anchor the ledger mark.
static void endlessCashReconcile(bool warn)
{
	const ulong now = endlessWallet();
	if (now == endlessCashMark)
		return;
	if (warn)
		fprintf(stderr, "warning: endless cash audit caught an undeclared %s of %llu\n",
		        (now > endlessCashMark) ? "rise" : "fall",
		        (unsigned long long)((now > endlessCashMark) ? now - endlessCashMark : endlessCashMark - now));
	if (now > endlessCashMark)
		endlessCashBook((Uint64)(now - endlessCashMark), ENDLESS_CASH_OTHER);
	else
		endlessCashAddSat(&endlessRunCashSpent, (Uint64)(endlessCashMark - now));
	endlessCashMark = now;
}

void endlessCashAudit(void)
{
	if (!endlessMode)
		return;
	endlessCashReconcile(true);
}

// Treat debug wallet edits as declared but unclassified.
void endlessCashDebugOverwrite(void)
{
	if (!endlessMode)
		return;
	endlessCashReconcile(false);
}

void endlessCashCredit(long amount, EndlessCashSource src)
{
	if (amount <= 0)
		return;
	if (!endlessMode)
	{
		player[endlessEconomyIndex()].cash += (ulong)amount;   // campaign with the effect layer on: pay out, nothing to tally
		return;
	}
	endlessCashReconcile(true);   // any undeclared drift surfaces before the mark moves
	player[endlessEconomyIndex()].cash += (ulong)amount;
	endlessCashBook((Uint64)amount, src);
	endlessCashMark = endlessWallet();
}

void endlessCashDebit(Sint64 amount, EndlessCashSink sink)
{
	if (amount <= 0)
		return;
	if (!endlessMode)
	{
		player[endlessEconomyIndex()].cash -= (ulong)amount;   // campaign fallback: plain wallet math (no debit runs there today)
		return;
	}
	endlessCashReconcile(true);
	Uint64 take = (Uint64)amount;
	if (take > endlessWallet())   // a debit can take at most the wallet
		take = endlessWallet();
	player[endlessEconomyIndex()].cash -= (ulong)take;
	endlessCashAddSat(&endlessRunCashSpent, take);
	if ((unsigned)sink < ENDLESS_CASH_SINKS)
		endlessCashAddSat(&endlessCashBySink[sink], take);
	endlessCashMark = endlessWallet();
}

// Re-anchor after loading or restoring cash that was not earned in this run.
void endlessCashResync(void)
{
	endlessCashMark = endlessWallet();
}

// The upgrade menu shows a temporary balance; Begin/Commit must bracket one transaction.
static ulong endlessTradeBefore = 0;

void endlessShopTradeBegin(void)
{
	if (!endlessMode)
		return;
	endlessCashReconcile(true);   // settle drift while the wallet is still real
	endlessTradeBefore = endlessWallet();
}

// Full-refund trades cancel prior gear spending. Only excess from granted gear counts as income.
void endlessShopTradeCommit(void)
{
	if (!endlessMode)
		return;
	const ulong now = endlessWallet();   // the exit assignment already committed JE_cashLeft()
	if (now > endlessTradeBefore)
	{
		const Uint64 refund = (Uint64)(now - endlessTradeBefore);
		Uint64 cancel = (refund < endlessCashBySink[ENDLESS_SINK_GEAR]) ? refund : endlessCashBySink[ENDLESS_SINK_GEAR];
		if (cancel > endlessRunCashSpent)   // unreachable (a sink never exceeds the total), kept for the unsigned math
			cancel = endlessRunCashSpent;
		endlessCashBySink[ENDLESS_SINK_GEAR] -= cancel;
		endlessRunCashSpent -= cancel;
		if (refund > cancel)
			endlessCashBook(refund - cancel, ENDLESS_CASH_TRADEIN);
	}
	else if (now < endlessTradeBefore)
	{
		const Uint64 fall = (Uint64)(endlessTradeBefore - now);
		endlessCashAddSat(&endlessRunCashSpent, fall);
		endlessCashAddSat(&endlessCashBySink[ENDLESS_SINK_GEAR], fall);
	}
	endlessTradeBefore = now;   // idempotent: a stray second commit books nothing
	endlessCashMark = now;
}

// Per-zone timers, advanced by endlessGameplayTick.
int endlessZoneTicks      = 0;
int endlessTurbodriveTimer[2] = { 0, 0 };
int endlessRetaliationTimer = 0;

// Rewards banked on sector clear and paid at the next outpost.
bool endlessStarChartsOwed  = false;
int  endlessBreakthroughOwed = 0;
static bool endlessArmorHudDirty = false;

// Milestones use the real upcoming zone so their labels match the HUD.
#define ENDLESS_MILESTONE_EVERY 50
#define ENDLESS_MILESTONE_GRAND 100

// Kinds are tags: 0 ordinary, 1 plain, 2 grand, 3 minor.
int endlessMilestoneKindOfZone(int zone)
{
	if (zone <= 0)
		return 0;
	if (zone % ENDLESS_MILESTONE_GRAND == 0)
		return 2;
	if (zone % ENDLESS_MILESTONE_EVERY == 0)
		return 1;
	if (zone % ENDLESS_MILESTONE_EVERY == ENDLESS_MILESTONE_EVERY / 2)
		return 3;
	return 0;
}

// Run depth counts cleared zones, so charting looks one zone ahead.
int endlessMilestoneKind(void)
{
	return endlessMilestoneKindOfZone(endlessRunDepth + 1);
}

// All milestone classes grant a perk after clearing the zone.
static bool endlessPerkMilestoneAt(int depth)
{
	return endlessMilestoneKindOfZone(depth) != 0;
}

// Scheduled perk picks occur at depths 1, 5, 9, ...
#define ENDLESS_PERK_EVERY 4

// Song IDs are 1-based, like levelSong.
#define ENDLESS_MILESTONE_SONG_GRAND 35  // "One Mustn't Fall" ; every 100th zone
#define ENDLESS_MILESTONE_SONG_PLAIN 37  // "A Field for Mag"  ; the other 50th zones (50, 150, 250, ...)
#define ENDLESS_MILESTONE_SONG_MINOR 17  // "Tunneling Trolls" ; the minor milestone (25, 75, 125, ...)

// The pinned track for a milestone class, or 0 for an ordinary zone.
JE_byte endlessMilestoneSong(int kind)
{
	return (kind == 2) ? ENDLESS_MILESTONE_SONG_GRAND
	     : (kind == 1) ? ENDLESS_MILESTONE_SONG_PLAIN
	     : (kind == 3) ? ENDLESS_MILESTONE_SONG_MINOR
	     : 0;
}

// A cadence/milestone collision defers the second pick by one zone.
bool endlessPerkDueAtDepth(int depth)
{
	if (depth <= 0)
		return false;
	if (depth % ENDLESS_PERK_EVERY == 1 || endlessPerkMilestoneAt(depth))
		return true;
	const int prev = depth - 1;
	return endlessPerkMilestoneAt(prev) && prev % ENDLESS_PERK_EVERY == 1;
}

// Milestones use the larger offer count; deferred picks use the normal count.
int endlessPerkOffersAtDepth(int depth)
{
	return endlessMilestoneKindOfZone(depth) ? ENDLESS_PERK_OFFERS_MILESTONE : ENDLESS_PERK_OFFERS;
}

// Seed-screen run mode: Relaxed allows retries; Standard ends on death; Hardcore also disables saves.
EndlessRunMode endlessRunMode = ENDLESS_RUNMODE_RELAXED;

const char *endlessRunModeName(EndlessRunMode mode)
{
	switch (mode)
	{
	case ENDLESS_RUNMODE_STANDARD: return "Standard";
	case ENDLESS_RUNMODE_HARDCORE: return "Hardcore";
	default:                       return "Relaxed";
	}
}

/* All-time records, stored in opentyrian.cfg. A run writes one of these: the record for the
 * difficulty it started on, or the untagged one if that difficulty is outside the six below.
 * The outer index is crew size: two ships reach depths a solo run cannot, so the two sets of
 * records never meet. */
int  endlessBestZoneUntagged[ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT] = { { 0 } };
bool endlessBestZoneUntaggedCustom[ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT] = { { false } };
static int endlessBestZoneAtRunStart = 0;

const int endlessDifficultyLevel[ENDLESS_DIFFICULTY_COUNT] = {
	DIFFICULTY_EASY, DIFFICULTY_NORMAL, DIFFICULTY_HARD,
	DIFFICULTY_IMPOSSIBLE, DIFFICULTY_SUICIDE, DIFFICULTY_LORD_OF_GAME,
};

int  endlessBestZoneDiff[ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT] = { { { 0 } } };
bool endlessBestZoneDiffCustom[ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT] = { { { false } } };

int endlessRecordTable(void) { return coopEndlessMode ? 1 : 0; }

const char *endlessRecordTableName(int players)
{
	return (players == 1) ? "2 Players" : "1 Player";
}

static bool endlessRecordArgsOk(int players, EndlessRunMode mode)
{
	return players >= 0 && players < ENDLESS_PLAYER_TABLES
	    && mode >= 0 && mode < ENDLESS_RUNMODE_COUNT;
}

int endlessBestZoneForDifficulty(int players, EndlessRunMode mode, int slot)
{
	if (!endlessRecordArgsOk(players, mode) || slot < 0 || slot >= ENDLESS_DIFFICULTY_COUNT)
		return 0;
	return endlessBestZoneDiff[players][mode][slot];
}

int endlessDifficultySlot(int difficulty)
{
	for (int i = 0; i < ENDLESS_DIFFICULTY_COUNT; ++i)
		if (endlessDifficultyLevel[i] == difficulty)
			return i;
	return -1;
}

// Custom-weapon usage. A shot out of the custom port arms the zone flag, and the end of that zone
// promotes it to the run. Only a running zone can arm it, which is what keeps the outpost weapon
// editor and shop previews out of the record.
bool endlessRunUsedCustom = false;
static bool endlessCustomFiredZone = false;
static bool endlessCustomZoneRunning = false;

void endlessNoteCustomWeaponShot(void)
{
	// Only a shot fired inside a running zone counts. The outpost's weapon editor and the shop's
	// weapon preview fire through the same path, and neither is a zone.
	if (endlessMode && endlessCustomZoneRunning)
		endlessCustomFiredZone = true;
}

void endlessResetCustomWeaponZone(void)
{
	endlessCustomFiredZone = false;
	endlessCustomZoneRunning = true;
}

#define ENDLESS_BEST_ZONE_MAX 99999  // a sanity ceiling on what a hand-edited config can claim

// The one record the running run writes to, and its mark.
static void endlessRunRecord(int **zone, bool **mark)
{
	const int slot = endlessDifficultySlot(initialDifficulty);
	if (slot >= 0)
	{
		*zone = &endlessBestZoneDiff[endlessRecordTable()][endlessRunMode][slot];
		*mark = &endlessBestZoneDiffCustom[endlessRecordTable()][endlessRunMode][slot];
	}
	else
	{
		*zone = &endlessBestZoneUntagged[endlessRecordTable()][endlessRunMode];
		*mark = &endlessBestZoneUntaggedCustom[endlessRecordTable()][endlessRunMode];
	}
}

// Record a zone when it starts, not after it is cleared.
void endlessNoteZoneReached(int zone)
{
	if (!endlessMode || zone > ENDLESS_BEST_ZONE_MAX)
		return;

	int *best;
	bool *mark;
	endlessRunRecord(&best, &mark);
	if (zone <= *best)
		return;

	*best = zone;
	*mark = endlessRunUsedCustom;
	save_opentyrian_config();
}

// Mark the record this run set, after the fact: a record is stamped as its zone is entered, before
// that zone is flown. Ownership must come from the run-start baseline rather than the run depth;
// see the Save format section of doc/notes.md.
static void endlessMarkRecordCustom(void)
{
	int *best;
	bool *mark;
	endlessRunRecord(&best, &mark);
	if (*mark || *best <= endlessBestZoneAtRunStart)
		return;

	*mark = true;
	save_opentyrian_config();
}

// A zone the custom weapon fired in counts once that zone is over, however it ended: cleared, died
// in, or bailed out of. Idempotent, so every path out of a zone can call it.
void endlessCustomWeaponZoneEnd(void)
{
	if (!endlessMode)
		return;

	if (endlessCustomFiredZone)
		endlessRunUsedCustom = true;
	endlessCustomFiredZone = false;
	endlessCustomZoneRunning = false;

	if (endlessRunUsedCustom)
		endlessMarkRecordCustom();
}

int endlessBestZoneAny(int players, EndlessRunMode mode)
{
	if (!endlessRecordArgsOk(players, mode))
		return 0;

	int best = endlessBestZoneUntagged[players][mode];
	for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
		if (endlessBestZoneDiff[players][mode][d] > best)
			best = endlessBestZoneDiff[players][mode][d];
	return best;
}

const char *endlessRecordAnyCustomMark(int players, EndlessRunMode mode)
{
	// Whichever record is the deepest owns the mark, and a tie takes the first marked one.
	const int best = endlessBestZoneAny(players, mode);
	if (best <= 0)
		return "";

	if (endlessBestZoneUntagged[players][mode] == best && endlessBestZoneUntaggedCustom[players][mode])
		return " C";
	for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
		if (endlessBestZoneDiff[players][mode][d] == best && endlessBestZoneDiffCustom[players][mode][d])
			return " C";
	return "";
}

const char *endlessRecordDiffCustomMark(int players, EndlessRunMode mode, int slot)
{
	if (!endlessRecordArgsOk(players, mode) || slot < 0 || slot >= ENDLESS_DIFFICULTY_COUNT)
		return "";
	return endlessBestZoneDiffCustom[players][mode][slot] ? " C" : "";
}

void endlessClearDeepestRecord(int players, EndlessRunMode mode)
{
	if (!endlessRecordArgsOk(players, mode))
		return;
	const int best = endlessBestZoneAny(players, mode);
	if (best <= 0)
		return;

	// Every record standing at that depth goes, so one confirmation always moves the figure and
	// what remains below it is what the mode now shows.
	if (endlessBestZoneUntagged[players][mode] == best)
	{
		endlessBestZoneUntagged[players][mode] = 0;
		endlessBestZoneUntaggedCustom[players][mode] = false;
	}
	for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
	{
		if (endlessBestZoneDiff[players][mode][d] == best)
		{
			endlessBestZoneDiff[players][mode][d] = 0;
			endlessBestZoneDiffCustom[players][mode][d] = false;
		}
	}
	save_opentyrian_config();
}

void endlessClearRecordDifficulty(int players, EndlessRunMode mode, int slot)
{
	if (!endlessRecordArgsOk(players, mode) || slot < 0 || slot >= ENDLESS_DIFFICULTY_COUNT)
		return;
	endlessBestZoneDiff[players][mode][slot] = 0;
	endlessBestZoneDiffCustom[players][mode][slot] = false;
	save_opentyrian_config();
}

// Baseline the record this run will write to, so the run-over gain measures that record alone.
// Callers set the run's mode and difficulty first.
void endlessRecordRunStart(void)
{
	int *best;
	bool *mark;
	endlessRunRecord(&best, &mark);
	endlessBestZoneAtRunStart = *best;
}
int  endlessBestZoneAtStart(void) { return endlessBestZoneAtRunStart; }

void endlessResetRun(void)
{
	endlessRunDepth   = 0;
	endlessActiveMods = 0;
	endlessRunUsedCustom = false;
	endlessCustomFiredZone = false;
	endlessRunKills   = 0;
	endlessRunBossKills = 0;
	endlessRunCashEarned = 0;
	endlessRunCashSpent  = 0;
	memset(endlessCashBySource, 0, sizeof(endlessCashBySource));
	memset(endlessCashBySink, 0, sizeof(endlessCashBySink));
	endlessCashResync();   // whatever is in the wallet right now was not earned by the run starting here
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	memset(endlessPlayerMods, 0, sizeof(endlessPlayerMods));
	endlessPerkPending = false;
	endlessStarChartsOwed = false;
	endlessBreakthroughOwed = 0;
	endlessPerkChoiceN = 0;
	endlessPerkDepthDone = -1;
	endlessResumeVisit = false;
	endlessCreditsShown = false;
	endlessLastSong = 0;
	endlessLastSongDepth = -1;
	endlessRegenTick = 0;
	for (unsigned p = 0; p < COUNTOF(endlessSalvoIdle); ++p)
	{
		endlessSalvoIdle[p] = ENDLESS_PERK_SALVO_IDLE;
		endlessSalvoWindow[p] = 0;
	}
	endlessCmCooldown = 0;
	endlessLockedSortie = false;
	endlessQuitToOutpost = false;
	endlessSortieHave = false;
	memset(endlessSortiePrePurchased, 0, sizeof(endlessSortiePrePurchased));
	memset(endlessSortiePreCleanse, 0, sizeof(endlessSortiePreCleanse));
	memset(endlessSortiePreLongCon, 0, sizeof(endlessSortiePreLongCon));
	endlessSortieOutpostMods = 0;
	endlessSortieOutpostEp = 0;
	endlessCoopHostCharts = true;
	// New runs override this after reset, and a loaded/reverted one restores the saved mode.
	endlessRunMode = ENDLESS_RUNMODE_RELAXED;
	endlessBaseName[0] = endlessPrevBaseName[0] = '\0';
	endlessBaseEp = endlessBaseLvl = endlessPrevBaseEp = endlessPrevBaseLvl = 0;
	endlessRecentCount = 0;

	// Everything a player owns for themselves.
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		endlessArmorBonus[p] = 0;
		endlessPurchasedMods[p] = 0;
		endlessBuffKind[p] = 0;
		endlessBuffCooldownUntil[p] = 0;
		endlessBuffCharge[p] = 0;
		endlessReviveHeld[p] = false;
		endlessRevivesUsed[p] = 0;
		endlessCleanseChargeCount[p] = 0;
		endlessGamblePerkWon[p] = false;
		endlessShopTax[p] = 0;
		endlessGambleRigged[p] = false;
		endlessLongCon[p] = 0;
		endlessPlayerDowned[p] = false;
		player[p].superbombs = 0;
	}
	memset(endlessPerkTakenBy, 0, sizeof(endlessPerkTakenBy));
	endlessPerkRederive();
	endlessSetSeed("");
}

// Clear outpost-only state before enabling campaign debug effects.
void endlessCampaignModsArm(void)
{
	if (endlessMode)
		return;
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		endlessArmorBonus[p] = 0;
		endlessBuffCharge[p] = 0;
		endlessBuffKind[p] = 0;
		endlessPurchasedMods[p] = 0;
		endlessReviveHeld[p] = false;
		endlessCleanseChargeCount[p] = 0;
		endlessShopTax[p] = 0;
	}
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));
	endlessStarChartsOwed = false;
	endlessBreakthroughOwed = 0;
	endlessPerkPending = false;
}

void endlessCountKill(int linknum, int killer)
{
	if (!endlessFxActive())
		return;

	// Multi-part enemies share a nonzero link number and count once.
	static int lastCountedLink = 0;
	if (linknum != 0 && linknum == lastCountedLink)
		return;
	lastCountedLink = linknum;

	++endlessRunKills;
	/* Boss kills are counted when their health bar empties. Whose streak a kill feeds is the
	 * session's Combo Feed setting: Individual credits the ship that fired the shot, Shared feeds
	 * both. A kill nothing can be credited with feeds both either way, so neither ship's streak
	 * is punished for a death it could not have claimed. */
	for (uint p = 0; p < endlessEffectPlayers(); ++p)
	{
		if (endlessCoop() && !endlessCoopComboShared
		    && killer != ENDLESS_KILLER_NONE && (uint)killer != p)
			continue;

		if (endlessPlayerMods[p] & ENDLESS_MOD_KILLFIRE_ANY)
		{
			endlessTurbodriveTimer[p] = endlessBuffWindowTicksFor(p);
			++endlessComboKills[p];
		}
		if ((endlessPlayerMods[p] & ENDLESS_MOD_STACKED)
		    && endlessOverdriveStacks[p] < ENDLESS_OVERDRIVE_MAX_STACKS)
			++endlessOverdriveStacks[p];
	}

	// Retaliation refreshes its window but does not stack.
	if (endlessActiveMods & ENDLESS_MOD_RETALIATION)
		endlessRetaliationTimer = ENDLESS_RETALIATION_TICKS;

	// Siphon perk: a per-kill chance to restore 1 armor (up to the ship's max). One roll feeds
	// both ships, so the draw count stays the same whether the run is solo or co-op.
	if (endlessPerkOwned[PERK_SIPHON] > 0
	    && (int)(mt_rand() % 100) < endlessPerkOwned[PERK_SIPHON] * ENDLESS_PERK_SIPHON_PCT)
	{
		for (uint p = 0; p < endlessEffectPlayers(); ++p)
			if (!endlessPlayerDowned[p] && player[p].armor < player[p].initial_armor)
				++player[p].armor;
	}
}

// Bank post-clear boons before the next sector changes endlessActiveMods.
void endlessOnSectorCleared(void)
{
	if (!endlessMode)
		return;
	if (endlessActiveMods & ENDLESS_MOD_STARCHARTS)
		endlessStarChartsOwed = true;
	if (endlessActiveMods & ENDLESS_MOD_BREAKTHROUGH)
		++endlessBreakthroughOwed;

	endlessCustomWeaponZoneEnd();
}

// Time-based and player-side modifiers.

void endlessGameplayTick(void)
{
	if (!endlessFxActive())
		return;
	++endlessZoneTicks;

	// Every wallet movement is declared at source; this is the per-tick drift assertion.
	endlessCashAudit();

	// Overheat drains hull but cannot land the killing blow. It is a deal one player took, so
	// only that player's hull cooks.
	if ((endlessZoneTicks % 80) == 0)
	{
		for (uint p = 0; p < endlessEffectPlayers(); ++p)
			if ((endlessPlayerMods[p] & ENDLESS_MOD_OVERHEAT) && !endlessPlayerDowned[p]
			    && player[p].armor > 1)
			{
				--player[p].armor;
				endlessArmorHudDirty = true;
			}
	}

	for (uint p = 0; p < endlessEffectPlayers(); ++p)
	{
		if (endlessTurbodriveTimer[p] > 0 && --endlessTurbodriveTimer[p] == 0)
		{
			endlessOverdriveStacks[p] = 0;
			endlessComboKills[p] = 0;
		}
	}

	// RETALIATION: drain the quickened-enemy-fire window opened by the last kill.
	if (endlessRetaliationTimer > 0)
		--endlessRetaliationTimer;

	// STATIC DISCHARGE: drain the generator-regen lockout opened by the last hit taken.
	endlessStaticLockoutTick();

	// AEGIS GATE: recharge the shield gate after a block.
	endlessAegisTick();

	// REVIVE GRACE: drain the enemy-fire stun a spent revive token bought.
	endlessReviveGraceTick();

	// Nanorepair perk: regenerate 1 armor every so often (interval shortens with more stacks).
	if (endlessPerkOwned[PERK_REGEN] > 0)
	{
		if (++endlessRegenTick >= ENDLESS_PERK_REGEN_TICKS / endlessPerkOwned[PERK_REGEN])
		{
			endlessRegenTick = 0;
			for (uint p = 0; p < endlessEffectPlayers(); ++p)
				if (!endlessPlayerDowned[p] && player[p].armor < player[p].initial_armor)
					++player[p].armor;
		}
	}

	endlessOpeningSalvoTick();    // Opening Salvo perk: advance the main-gun idle timer
	endlessCountermeasureTick();  // Countermeasure Suite perk: advance the burst cooldown
}

/* The run and per-zone state a re-simulated tick has to see exactly as the live pass did. The
 * sector's own modifiers and the perk collection do not move inside a level, but a desync
 * recovery adopts the peer's whole snapshot, so they travel with it. */
void endless_register_rollback(void)
{
	rollback_register("endless.activeMods", &endlessActiveMods, sizeof(endlessActiveMods));
	rollback_register("endless.playerMods", endlessPlayerMods, sizeof(endlessPlayerMods));
	rollback_register("endless.zoneTicks", &endlessZoneTicks, sizeof(endlessZoneTicks));
	rollback_register("endless.turboTimer", endlessTurbodriveTimer, sizeof(endlessTurbodriveTimer));
	rollback_register("endless.retalTimer", &endlessRetaliationTimer, sizeof(endlessRetaliationTimer));
	rollback_register("endless.comboKills", endlessComboKills, sizeof(endlessComboKills));
	rollback_register("endless.odStacks", endlessOverdriveStacks, sizeof(endlessOverdriveStacks));
	rollback_register("endless.runKills", &endlessRunKills, sizeof(endlessRunKills));
	rollback_register("endless.runBossKills", &endlessRunBossKills, sizeof(endlessRunBossKills));
	rollback_register("endless.customFired", &endlessCustomFiredZone, sizeof(endlessCustomFiredZone));
	rollback_register("endless.eliteRng", &endlessEliteRngState, sizeof(endlessEliteRngState));
	rollback_register("endless.armorBonus", endlessArmorBonus, sizeof(endlessArmorBonus));
	rollback_register("endless.downed", endlessPlayerDowned, sizeof(endlessPlayerDowned));
	rollback_register("endless.reviveHeld", endlessReviveHeld, sizeof(endlessReviveHeld));
	rollback_register("endless.revivesUsed", endlessRevivesUsed, sizeof(endlessRevivesUsed));
	rollback_register("endless.perkOwned", endlessPerkOwned, sizeof(endlessPerkOwned));
	rollback_register("endless.perkTakenBy", endlessPerkTakenBy, sizeof(endlessPerkTakenBy));
	rollback_register("endless.regenTick", &endlessRegenTick, sizeof(endlessRegenTick));
	rollback_register("endless.salvoIdle", endlessSalvoIdle, sizeof(endlessSalvoIdle));
	rollback_register("endless.salvoWindow", endlessSalvoWindow, sizeof(endlessSalvoWindow));
	rollback_register("endless.cmCooldown", &endlessCmCooldown, sizeof(endlessCmCooldown));
	rollback_register("endless.buffCharge", endlessBuffCharge, sizeof(endlessBuffCharge));
}

// Consume the event-driven armor-bar repaint flag.
bool endlessConsumeArmorHudDirty(void)
{
	const bool dirty = endlessArmorHudDirty;
	endlessArmorHudDirty = false;
	return dirty;
}

bool endlessTurbodriveActive(void)
{
	return endlessFxActive() && endlessTurbodriveTimer[endlessFxPlayer()] > 0;
}

// Run-over flavor text, one line per five-zone band.
static const char *endlessMilestoneLine(int d)
{
	static const char* const lines[] = {
		"The gate seals shut behind you.",         //   0-4
		"The last friendly beacon fades.",         //   5
		"Something is following your signal.",     //  10
		"The wreckage ahead is still warm.",       //  15
		"Command has stopped answering.",          //  20
		"Enemy signals fill every channel.",       //  25
		"The stars no longer match the charts.",   //  30
		"Every route leads farther in.",            //  35
		"The navigation computer refuses course.", //  40
		"The wrecks are starting to look familiar.", // 45
		"Something has learned how you fight.",     //  50
		"The guns have not cooled in hours.",       //  55
		"A dreadful hush falls between volleys.",  //  60
		"The hull remembers every impact.",         //  65
		"No human signal reaches this far.",        //  70
		"Even the warning lights fall silent.",     //  75
		"The charts end here.",                     //  80
		"Nothing living knows these coordinates.", //  85
		"Reality bends around the wreckage.",       //  90
		"Your engines run on borrowed time.",       //  95
		"Legends come this far to die.",            // 100
		"The enemy no longer sees you as prey.",    // 105
		"Their fleets gather beyond the static.",   // 110
		"The stars flicker when you fire.",         // 115
		"The distress calls are no longer yours.",  // 120
		"The swarm goes on without end.",           // 125
		"They tell stories about your ship.",       // 130
		"Your signal has become a warning.",        // 135
		"Time loses count between the gunfire.",    // 140
		"The last known beacon has gone dark.",     // 145
		"The end of the map was the beginning.",    // 150
		"These zones should not exist.",            // 155
		"The next sector is waiting for you.",      // 160
		"Still it grows. Still you press on.",      // 165
		"No rescue was ever coming.",               // 170
		"There are no maps for what comes next.",   // 175
		"Enemy fleets turn before you arrive.",     // 180
		"They scatter when your signal appears.",   // 185
		"The hunters have become the hunted.",      // 190
		"Only the guns remember you now.",          // 195
		"Two hundred zones burn behind you.",       // 200
		"The guns glow white with wrath.",          // 205
		"Entire fleets vanish in your wake.",       // 210
		"Your name is now an evacuation order.",    // 215
		"Even their warships flee your signal.",    // 220
		"You are the anomaly on their charts.",     // 225
		"The universe is running out of hiding places.", // 230
		"Creation grows thin around your ship.",    // 235
		"There are no more stars ahead.",           // 240
		"There is nothing left to chart.",          // 245
	};

	int i = d / 5;
	if (i < 0)
		i = 0;
	if (i >= (int)COUNTOF(lines))
		i = (int)COUNTOF(lines) - 1;
	return lines[i];
}

// Sign-off shown after the final milestone line.
static const char *endlessMilestoneEpilogue(int d)
{
	return (d >= 250) ? "Thank you for playing." : NULL;
}

// Draw centered text on the full widescreen surface.
static void endlessGlowCentered(int y, unsigned int font, const char *s)
{
	textGlowFont = font;
	JE_outTextGlow(VGAScreen, (vga_width - JE_textWidth(s, font)) / 2, y, s);
}

// Draw one stat row with a single shared glow effect.
static void endlessGlowRow(int left, int right, int y, unsigned int font, const char *label, const char *value)
{
	textGlowFont = font;
	const int xs[2] = { left, right - JE_textWidth(value, font) };
	const char *const ss[2] = { label, value };
	JE_outTextGlowMulti(VGAScreen, xs, y, ss, 2);
}

// Dim the campaign-ending ship art behind the run summary.
#define ENDLESS_RUNEND_PIC   "tshp2.pcx"
#define ENDLESS_RUNEND_DIM   32   // retained background brightness, in percent

static void endlessDrawRunEndBackdrop(void)
{
	JE_loadPCX(ENDLESS_RUNEND_PIC);

	// Center the 320px image and extend its edge columns into the side strips.
	const int pad = (vga_width - 320) / 2;   // left strip
	const int tail = vga_width - pad - 320;  // right strip
	if (pad > 0 && tail >= 0 && vga_width <= VGAScreen->pitch)
	{
		for (int row = 0; row < vga_height; ++row)
		{
			Uint8 *const p = (Uint8 *)VGAScreen->pixels + row * VGAScreen->pitch;
			const Uint8 left = p[0], right = p[319];
			memmove(p + pad, p, 320);
			memset(p, left, pad);
			memset(p + pad + 320, right, tail);
		}
	}

	// Dim the image palette.
	for (int i = 0; i < 224; ++i)
	{
		colors[i].r = (Uint8)(colors[i].r * ENDLESS_RUNEND_DIM / 100);
		colors[i].g = (Uint8)(colors[i].g * ENDLESS_RUNEND_DIM / 100);
		colors[i].b = (Uint8)(colors[i].b * ENDLESS_RUNEND_DIM / 100);
	}

	// Restore the text glow ramp in bank 15.
	memcpy(&colors[240], &palettes[0][240], 16 * sizeof(colors[0]));
}

// Relaxed deaths open JE_endlessDeathMenu instead of going directly to the run summary.
bool endlessDeathMenuDue(void)
{
	return endlessMode && endlessRunMode == ENDLESS_RUNMODE_RELAXED && endlessSortieValid();
}

// Standard and Hardcore lock the pause menu after death to prevent a Quit Level escape.
bool endlessDeathLocksMenu(void)
{
	return endlessMode && endlessRunMode != ENDLESS_RUNMODE_RELAXED;
}

void endlessOnRunEnd(void)
{
	endlessCashAudit();  // last drift check before the tally is printed
	endlessCustomWeaponZoneEnd();  // a run that died mid-zone still flew whatever it was holding

	// Draw the run summary over the dimmed ship illustration.
	VGAScreen = VGAScreenSeg;
	JE_clr256(VGAScreen);
	endlessDrawRunEndBackdrop();
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	JE_wipeKey();
	frameCountMax = 4;
	SDL_Color white = { 255, 255, 255 };
	set_colors(white, 254, 254);

	// The tally uses left-aligned labels and right-aligned values. Other lines are centered.
	// SMALL_FONT_SHAPES lacks several punctuation glyphs, so the text uses words.
	char fellLine[48];
	snprintf(fellLine, sizeof(fellLine), "You fell in Zone %d", endlessRunDepth + 1);

	// Zero-initialized for the analyzer (C6001): it cannot correlate the n guard with which
	// entries the width loop below reads.
	struct { char label[28], value[40]; } rows[10] = { 0 };
	int n = 0;
	#define RUNEND_ROW(lbl, ...) \
		do { \
			if (n < (int)COUNTOF(rows)) \
			{ \
				SDL_strlcpy(rows[n].label, (lbl), sizeof(rows[0].label)); \
				snprintf(rows[n].value, sizeof(rows[0].value), __VA_ARGS__); \
				++n; \
			} \
		} while (0)

	// Records are kept per mode and difficulty, so the summary names both.
	const int runDifficulty = (initialDifficulty >= 0 && (size_t)initialDifficulty < COUNTOF(difficultyNameB))
	                        ? initialDifficulty : 0;
	RUNEND_ROW("Mode:", "%s, %s", endlessRunModeName(endlessRunMode), difficultyNameB[runDifficulty]);
	RUNEND_ROW("Zones cleared:", "%d", endlessRunDepth);
	RUNEND_ROW("Enemies destroyed:", "%d", endlessRunKills);
	RUNEND_ROW("Bosses slain:", "%d", endlessRunBossKills);
	// Report lifetime income and spending in addition to the final balance.
	RUNEND_ROW("Cash earned:", "$%llu", (unsigned long long)endlessRunCashEarned);
	RUNEND_ROW("Cash spent:", "$%llu", (unsigned long long)endlessRunCashSpent);

	if (endlessArmorBonus[endlessEconomyIndex()] > 0)
		RUNEND_ROW("Hull reinforced:", "%d", endlessArmorBonus[endlessEconomyIndex()]);

	RUNEND_ROW("Seed:", "%s", endlessSeedString());
	#undef RUNEND_ROW

	// The record shown is the one this run wrote to, its mode's on the difficulty it was played,
	// which the Mode row above names. Show a trailing C when a custom weapon set it, and any gain.
	char recordLine[64];
	int *bestRecord;
	bool *bestMark;
	endlessRunRecord(&bestRecord, &bestMark);
	const int best = *bestRecord;
	const int recordGain = best - endlessBestZoneAtStart();
	const char *const customMark = *bestMark ? " C" : "";
	if (recordGain > 0)
		snprintf(recordLine, sizeof(recordLine), "New furthest zone: %d%s   up %d", best, customMark, recordGain);
	else
		snprintf(recordLine, sizeof(recordLine), "Furthest zone: %d%s", best, customMark);

	// Size the block to its widest label and widest value, then center it as a unit so both columns
	// line up whatever the run produced.
	int labelW = 0, valueW = 0;
	for (int i = 0; i < n; ++i)
	{
		const int lw = JE_textWidth(rows[i].label, SMALL_FONT_SHAPES);
		const int vw = JE_textWidth(rows[i].value, SMALL_FONT_SHAPES);
		if (lw > labelW) labelW = lw;
		if (vw > valueW) valueW = vw;
	}

	const int colGap = 30;                    // enough that the two columns read as columns
	const int blockMax = vga_width - 40;      // ...but never past the margins
	int blockW = labelW + colGap + valueW;
	if (blockW > blockMax)
		blockW = blockMax;                    // squeeze the gap first: the columns meet before anything clips
	const int blockLeft = (vga_width - blockW) / 2;
	const int blockRight = blockLeft + blockW;

	// Fit the title, stats, and closing lines within the screen.
	const char *const closingLine = endlessMilestoneLine(endlessRunDepth + 1);
	const char *const closingTail = endlessMilestoneEpilogue(endlessRunDepth + 1);

	const int titleH  = 20;   // FONT_SHAPES
	const int lineH   = 13;   // SMALL_FONT_SHAPES
	const int recordGap = 3;  // the record hangs off the closing line as its own beat, not a new block
	int titleGap = 12;        // breathing room under the title
	int tailGap  = 10;        // ...and above the closing milestone line

	const int bodyLines = n + 1;                            // the epitaph, then one line per row
	const int closeLines = (closingTail != NULL) ? 2 : 1;   // the milestone line, plus the sign-off at 250
	int step = 18;
	int total;
	// Tighten row pitch and gaps to fit the deepest-run summary in 200 scanlines.
	while ((total = titleH + titleGap + (bodyLines - 1) * step + lineH
	                + tailGap + lineH
	                + (closeLines - 1) * (recordGap + lineH)
	                + recordGap + lineH) > 182)
	{
		if (step > lineH)
			--step;
		else if (titleGap > 6)
			--titleGap;
		else if (tailGap > 6)
			--tailGap;
		else
			break;
	}

	int y = (vga_height - total) / 2;

	endlessGlowCentered(y, FONT_SHAPES, "RUN OVER");
	y += titleH + titleGap;

	endlessGlowCentered(y, SMALL_FONT_SHAPES, fellLine);
	y += step;

	for (int i = 0; i < n; ++i, y += step)
		endlessGlowRow(blockLeft, blockRight, y, SMALL_FONT_SHAPES, rows[i].label, rows[i].value);

	int closingY = y - step + lineH + tailGap;
	endlessGlowCentered(closingY, SMALL_FONT_SHAPES, closingLine);
	if (closingTail != NULL)
	{
		closingY += lineH + recordGap;   // the sign-off stays with the line it follows from
		endlessGlowCentered(closingY, SMALL_FONT_SHAPES, closingTail);
	}
	endlessGlowCentered(closingY + lineH + recordGap, SMALL_FONT_SHAPES, recordLine);

	// Ignore held controls, then wait for fresh input.
	wait_noinput(true, true, true);

	// Ramp whatever is still playing away over the next half second, the same cue the Relaxed
	// death menu uses when its panel goes up. The callers leave the track running for it, and the
	// ramp starts here so the whole of it is ticked by the loop below.
	MusicFadeOut songFade;
	music_fade_out_init(&songFade);

	do
	{
		music_fade_out_tick(&songFade);
		setDelay(1);
		wait_delay();
	} while (!JE_anyButton());

	music_fade_out_finish(&songFade);   // dismissed mid-ramp must not strand the master volume
	wait_noinput(false, false, true);
	fade_black(15);
	JE_clr256(VGAScreen);
}

void endlessEndRunToTitle(void)
{
	// A Hardcore quit is final; a saveable run may still be resumed. The outpost track keeps
	// playing into the summary, which ramps it away itself.
	if (endlessHardcore())
		endlessOnRunEnd();
	endlessMode = false;
}
