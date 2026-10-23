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
#include "touch_ui.h"
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
static Sint64 endlessCashMark = 0;   // fixed-width: registered rollback state

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
static Sint64 endlessWallet(void)
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
/* A peer quit returns both players to the launch snapshot and outpost. */
void endlessCoopPeerQuitLevel(void)
{
	if (endlessCoop() && endlessSortieValid())
		endlessQuitToOutpost = true;
}

Uint64 endlessPlayerMods[2] = { 0, 0 };
static uint endlessFxPlayerIdx = 0;

void endlessSetFxPlayer(uint p) { endlessFxPlayerIdx = (p < COUNTOF(player)) ? p : 0; }
uint endlessFxPlayer(void)      { return coopEndlessMode ? endlessFxPlayerIdx : 0; }

/* Fold personal purchases into each player and shared purchases into the sector. */
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

/* One ship's kill-fire buffs, contract in endless.h. The live mask is what combat reads and the
 * purchased mask is what a commit re-folds from, so the setter writes both. */
Uint64 endlessPersonalBuffMods(uint p)
{
	if (p >= COUNTOF(endlessPurchasedMods))
		return 0;
	return (endlessPlayerMods[p] | (Uint64)endlessPurchasedMods[p]) & ENDLESS_PERSONAL_MOD_MASK;
}

void endlessSetPersonalBuffMods(uint p, Uint64 bits)
{
	if (p >= COUNTOF(endlessPurchasedMods))
		return;
	bits &= ENDLESS_PERSONAL_MOD_MASK;
	endlessPurchasedMods[p] =
		(unsigned)((endlessPurchasedMods[p] & ~ENDLESS_PERSONAL_MOD_MASK) | bits);
	endlessPlayerMods[p] = endlessFoldPurchasedMods(endlessActiveMods, bits);
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

/* The shared seat whose perks shape the next course. Solo uses player one. */
uint endlessChartingPlayerIndex(void)
{
	if (!endlessCoop())
		return 0;

	bool hostCharts;
	switch (endlessCourseChooser)
	{
	case ENDLESS_PICK_GUEST:     hostCharts = false; break;
	case ENDLESS_PICK_ALTERNATE: hostCharts = endlessCoopHostCharts; break;
	case ENDLESS_PICK_COINFLIP:
		hostCharts = ((endlessSplitMixSeed((Uint64)endlessRunDepth * 4 + 1) >> 33) & 1) != 0;
		break;
	default:                     hostCharts = true; break;
	}
	return hostCharts ? (networkHostPlayerNum - 1u) : (2u - networkHostPlayerNum);
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
	const Sint64 now = endlessWallet();
	if (now == endlessCashMark)
		return;
	if (warn)
		fprintf(stderr, "warning: endless cash audit caught an undeclared %s of %lld\n",
		        (now > endlessCashMark) ? "rise" : "fall",
		        (long long)((now > endlessCashMark) ? now - endlessCashMark : endlessCashMark - now));
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

void endlessCashCredit(Sint64 amount, EndlessCashSource src)
{
	if (amount <= 0)
		return;
	Player *const wallet = &player[endlessEconomyIndex()];
	if (!endlessMode)
	{
		player_add_cash(wallet, amount);   // campaign with the effect layer on: pay out, nothing to tally
		return;
	}
	endlessCashReconcile(true);   // any undeclared drift surfaces before the mark moves
	const Sint64 before = wallet->cash;
	player_add_cash(wallet, amount);
	endlessCashBook((Uint64)(wallet->cash - before), src);   // the ceiling can shorten a credit
	endlessCashMark = endlessWallet();
}

void endlessCashDebit(Sint64 amount, EndlessCashSink sink)
{
	if (amount <= 0)
		return;
	Player *const wallet = &player[endlessEconomyIndex()];
	if (!endlessMode)
	{
		player_add_cash(wallet, -amount);   // campaign fallback: plain wallet math (no debit runs there today)
		return;
	}
	endlessCashReconcile(true);
	const Sint64 before = wallet->cash;
	player_add_cash(wallet, -amount);   // a debit can take at most the wallet
	const Uint64 take = (Uint64)(before - wallet->cash);
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
static Sint64 endlessTradeBefore = 0;

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
	const Sint64 now = endlessWallet();   // the exit assignment already committed JE_cashLeft()
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
 * difficulty it started on, or the untagged one if that difficulty is outside the six below. */
int  endlessBestZoneUntagged[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT] = { { { 0 } } };
bool endlessBestZoneUntaggedCustom[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT] = { { { false } } };
static int endlessBestZoneAtRunStart = 0;

const int endlessDifficultyLevel[ENDLESS_DIFFICULTY_COUNT] = {
	DIFFICULTY_EASY, DIFFICULTY_NORMAL, DIFFICULTY_HARD,
	DIFFICULTY_IMPOSSIBLE, DIFFICULTY_SUICIDE, DIFFICULTY_LORD_OF_GAME,
};

int  endlessBestZoneDiff[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT] = { { { { 0 } } } };
bool endlessBestZoneDiffCustom[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT] = { { { { false } } } };

int endlessRecordTable(void) { return coopEndlessMode ? 1 : 0; }

const char *endlessRecordTableName(int players)
{
	return (players == 1) ? "2 Players" : "1 Player";
}

// Picked at run start and fixed from there; the run writes the record set it names.
EndlessBaseRule endlessRunBaseRule = ENDLESS_BASE_VARIED;

static const char *const endlessBaseRuleName[ENDLESS_BASE_RULE_COUNT] = {
	"Varied", "Same", "Varied Shuffle", "Same Shuffle",
};

// One line per rule, shared by the seed screen and the Endless lobby.
static const char *const endlessBaseRuleHelp[ENDLESS_BASE_RULE_COUNT] = {
	"Varied: every charted route is its own level.",
	"Same: one base level per chart, modifiers differ.",
	"Varied Shuffle: routes come off a shuffled pool.",
	"Same Shuffle: one level, off that shuffled pool.",
};

/* Menu and record-page order, pairing each rule with its Shuffle twin. The enum values are the
 * record, save and wire order, so this is a separate list rather than a reordering of them. */
static const EndlessBaseRule endlessBaseRuleMenuOrder[ENDLESS_BASE_RULE_COUNT] = {
	ENDLESS_BASE_VARIED, ENDLESS_BASE_VARIED_SHUFFLE, ENDLESS_BASE_SAME, ENDLESS_BASE_SAME_SHUFFLE,
};

const char *endlessBaseLevelRuleName(int variant)
{
	if (variant < 0 || variant >= ENDLESS_BASE_RULE_COUNT)
		return endlessBaseRuleName[ENDLESS_BASE_VARIED];
	return endlessBaseRuleName[variant];
}

const char *endlessBaseLevelRuleHelp(int variant)
{
	if (variant < 0 || variant >= ENDLESS_BASE_RULE_COUNT)
		return endlessBaseRuleHelp[ENDLESS_BASE_VARIED];
	return endlessBaseRuleHelp[variant];
}

EndlessBaseRule endlessBaseRuleAtMenuIndex(int index)
{
	if (index < 0 || index >= ENDLESS_BASE_RULE_COUNT)
		return ENDLESS_BASE_VARIED;
	return endlessBaseRuleMenuOrder[index];
}

int endlessBaseRuleMenuIndex(EndlessBaseRule rule)
{
	for (int i = 0; i < ENDLESS_BASE_RULE_COUNT; ++i)
		if (endlessBaseRuleMenuOrder[i] == rule)
			return i;
	return 0;
}

static bool endlessRecordArgsOk(int variant, int players, EndlessRunMode mode)
{
	return variant >= 0 && variant < ENDLESS_BASE_TABLES
	    && players >= 0 && players < ENDLESS_PLAYER_TABLES
	    && mode >= 0 && mode < ENDLESS_RUNMODE_COUNT;
}

int endlessBestZoneForDifficulty(int variant, int players, EndlessRunMode mode, int slot)
{
	if (!endlessRecordArgsOk(variant, players, mode) || slot < 0 || slot >= ENDLESS_DIFFICULTY_COUNT)
		return 0;
	return endlessBestZoneDiff[variant][players][mode][slot];
}

int endlessDifficultySlot(int difficulty)
{
	for (int i = 0; i < ENDLESS_DIFFICULTY_COUNT; ++i)
		if (endlessDifficultyLevel[i] == difficulty)
			return i;
	return -1;
}

// Only equipment used during a running zone may mark the run as custom.
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

void endlessNoteCustomShip(void)
{
	if (endlessMode && endlessCustomZoneRunning)
		endlessCustomFiredZone = true;
}

// Check both seats because an extra ship may carry over from the previous zone.
static bool endlessFlyingCustomShip(void)
{
	for (uint i = 0; i < COUNTOF(player); ++i)
		if (player[i].items.ship > 90)
			return true;
	return false;
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
	const int base = (int)endlessRunBaseRule;
	const int slot = endlessDifficultySlot(initialDifficulty);
	if (slot >= 0)
	{
		*zone = &endlessBestZoneDiff[base][endlessRecordTable()][endlessRunMode][slot];
		*mark = &endlessBestZoneDiffCustom[base][endlessRecordTable()][endlessRunMode][slot];
	}
	else
	{
		*zone = &endlessBestZoneUntagged[base][endlessRecordTable()][endlessRunMode];
		*mark = &endlessBestZoneUntaggedCustom[base][endlessRecordTable()][endlessRunMode];
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

// Record ownership comes from the run's starting loadout, not its current depth.
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

// Promote zone usage on every exit path. This is idempotent.
void endlessCustomWeaponZoneEnd(void)
{
	if (!endlessMode)
		return;

	if (endlessCustomFiredZone || endlessFlyingCustomShip())
		endlessRunUsedCustom = true;
	endlessCustomFiredZone = false;
	endlessCustomZoneRunning = false;

	if (endlessRunUsedCustom)
		endlessMarkRecordCustom();
}

int endlessBestZoneAny(int variant, int players, EndlessRunMode mode)
{
	if (!endlessRecordArgsOk(variant, players, mode))
		return 0;

	int best = endlessBestZoneUntagged[variant][players][mode];
	for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
		if (endlessBestZoneDiff[variant][players][mode][d] > best)
			best = endlessBestZoneDiff[variant][players][mode][d];
	return best;
}

/* Return the deepest record across the four base-level rules. */
int endlessBestZoneAnyRule(int players, EndlessRunMode mode)
{
	int best = 0;
	for (int v = 0; v < ENDLESS_BASE_TABLES; ++v)
	{
		const int zone = endlessBestZoneAny(v, players, mode);
		if (zone > best)
			best = zone;
	}
	return best;
}

const char *endlessRecordAnyRuleCustomMark(int players, EndlessRunMode mode)
{
	const int best = endlessBestZoneAnyRule(players, mode);
	if (best <= 0)
		return "";

	// Whichever rule is the deepest owns the mark, and a tie takes the first marked one.
	for (int v = 0; v < ENDLESS_BASE_TABLES; ++v)
	{
		if (endlessBestZoneAny(v, players, mode) == best
		    && endlessRecordAnyCustomMark(v, players, mode)[0] != '\0')
		{
			return " C";
		}
	}
	return "";
}

const char *endlessRecordAnyCustomMark(int variant, int players, EndlessRunMode mode)
{
	// Whichever record is the deepest owns the mark, and a tie takes the first marked one.
	const int best = endlessBestZoneAny(variant, players, mode);
	if (best <= 0)
		return "";

	if (endlessBestZoneUntagged[variant][players][mode] == best
	    && endlessBestZoneUntaggedCustom[variant][players][mode])
		return " C";
	for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
		if (endlessBestZoneDiff[variant][players][mode][d] == best
		    && endlessBestZoneDiffCustom[variant][players][mode][d])
			return " C";
	return "";
}

const char *endlessRecordDiffCustomMark(int variant, int players, EndlessRunMode mode, int slot)
{
	if (!endlessRecordArgsOk(variant, players, mode) || slot < 0 || slot >= ENDLESS_DIFFICULTY_COUNT)
		return "";
	return endlessBestZoneDiffCustom[variant][players][mode][slot] ? " C" : "";
}

void endlessClearDeepestRecord(int variant, int players, EndlessRunMode mode)
{
	if (!endlessRecordArgsOk(variant, players, mode))
		return;
	const int best = endlessBestZoneAny(variant, players, mode);
	if (best <= 0)
		return;

	// Every record standing at that depth goes, so one confirmation always moves the figure and
	// what remains below it is what the mode now shows.
	if (endlessBestZoneUntagged[variant][players][mode] == best)
	{
		endlessBestZoneUntagged[variant][players][mode] = 0;
		endlessBestZoneUntaggedCustom[variant][players][mode] = false;
	}
	for (int d = 0; d < ENDLESS_DIFFICULTY_COUNT; ++d)
	{
		if (endlessBestZoneDiff[variant][players][mode][d] == best)
		{
			endlessBestZoneDiff[variant][players][mode][d] = 0;
			endlessBestZoneDiffCustom[variant][players][mode][d] = false;
		}
	}
	save_opentyrian_config();
}

void endlessClearRecordDifficulty(int variant, int players, EndlessRunMode mode, int slot)
{
	if (!endlessRecordArgsOk(variant, players, mode) || slot < 0 || slot >= ENDLESS_DIFFICULTY_COUNT)
		return;
	endlessBestZoneDiff[variant][players][mode][slot] = 0;
	endlessBestZoneDiffCustom[variant][players][mode][slot] = false;
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
	endlessPartnerOutpostClear();   // no visit yet, so no partner half to store with a save
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
	endlessWarnedZone = 0;
	endlessLastSong = 0;
	endlessLastSongDepth = -1;
	endlessRegenTick = 0;
	for (unsigned p = 0; p < COUNTOF(endlessSalvoIdle); ++p)
	{
		endlessSalvoIdle[p] = ENDLESS_PERK_SALVO_IDLE;
		endlessSalvoWindow[p] = 0;
		endlessRegenCalm[p] = 0;
	}
	memset(endlessPerkKineticAmmoAccum, 0, sizeof(endlessPerkKineticAmmoAccum));
	endlessLockedSortie = false;
	endlessQuitToOutpost = false;
	endlessSortieHave = false;
	memset(endlessSortiePrePurchased, 0, sizeof(endlessSortiePrePurchased));
	memset(endlessSortiePreCleanse, 0, sizeof(endlessSortiePreCleanse));
	memset(endlessSortiePreLongCon, 0, sizeof(endlessSortiePreLongCon));
	endlessSortieOutpostMods = 0;
	endlessSortieOutpostEp = 0;
	endlessCoopHostCharts = true;
	endlessChartRerolls = 0;
	endlessChartStarCharts = false;
	endlessChartSeat = 0;
	endlessShuffleNext = endlessShuffleHandStart = 0;
	endlessShuffleHandDepth = -1;
	// New runs override these after reset, and a loaded/reverted one restores the saved pair.
	endlessRunMode = ENDLESS_RUNMODE_RELAXED;
	endlessRunBaseRule = ENDLESS_BASE_VARIED;
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
		endlessExtraPerksBought[p] = 0;
		endlessExtraPerksVisit[p] = 0;
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

/* Multi-part enemies share a nonzero link number and count once; this is that dedup guard. */
static int endlessLastCountedLink = 0;

void endlessResetKillDedup(void)
{
	endlessLastCountedLink = 0;
}

void endlessCountKill(int linknum, int killer)
{
	if (!endlessFxActive())
		return;

	if (linknum != 0 && linknum == endlessLastCountedLink)
		return;
	endlessLastCountedLink = linknum;

	++endlessRunKills;
	/* Combo Feed decides whether a boss kill advances one streak or both. */
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

	// Roll personal Siphon effects in seat order to keep RNG deterministic.
	for (uint p = 0; p < endlessEffectPlayers(); ++p)
	{
		const JE_byte stacks = endlessPerkEffective(p, PERK_SIPHON);
		if (stacks > 0 && (int)(mt_rand() % 100) < stacks * ENDLESS_PERK_SIPHON_PCT
		    && !endlessPlayerDowned[p] && player[p].armor < player[p].initial_armor)
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

	// The Alternating turn counts sectors flown, so it passes here rather than at the course pick:
	// a sector the pair bails out of is re-charted by the player who charted it.
	endlessAdvanceCourseTurn();

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
	// Personal: one running clock, and each owning ship heals on its own stack's cadence.
	++endlessRegenTick;
	for (uint p = 0; p < endlessEffectPlayers(); ++p)
	{
		if (player[p].shield < player[p].shield_max)
			endlessRegenCalm[p] = 0;
		else if (endlessRegenCalm[p] < ENDLESS_PERK_REGEN_CALM_TICKS)
			++endlessRegenCalm[p];

		const JE_byte stacks = endlessPerkEffective(p, PERK_REGEN);
		if (stacks > 0 && endlessRegenCalm[p] >= ENDLESS_PERK_REGEN_CALM_TICKS
		    && endlessRegenTick % (ENDLESS_PERK_REGEN_TICKS / stacks) == 0
		    && !endlessPlayerDowned[p] && player[p].armor < player[p].initial_armor)
			++player[p].armor;
	}

	endlessOpeningSalvoTick();    // Opening Salvo perk: advance the main-gun idle timer
}

/* Run and zone state required by re-simulation and full-state recovery. */
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
	rollback_register("endless.regenCalm", endlessRegenCalm, sizeof(endlessRegenCalm));
	rollback_register("endless.salvoIdle", endlessSalvoIdle, sizeof(endlessSalvoIdle));
	rollback_register("endless.salvoWindow", endlessSalvoWindow, sizeof(endlessSalvoWindow));
	rollback_register("endless.buffCharge", endlessBuffCharge, sizeof(endlessBuffCharge));

	/* Roll the run ledger back with the wallet. Otherwise speculative income is
	 * booked again during re-simulation and peer summaries diverge. */
	rollback_register("endless.cashEarned", &endlessRunCashEarned, sizeof(endlessRunCashEarned));
	rollback_register("endless.cashSpent", &endlessRunCashSpent, sizeof(endlessRunCashSpent));
	rollback_register("endless.cashBySource", endlessCashBySource, sizeof(endlessCashBySource));
	rollback_register("endless.cashBySink", endlessCashBySink, sizeof(endlessCashBySink));
	rollback_register("endless.cashMark", &endlessCashMark, sizeof(endlessCashMark));
	rollback_register("endless.perkFireAccum", endlessPerkFireAccum, sizeof(endlessPerkFireAccum));
	rollback_register("endless.perkCdAccum", endlessPerkSpecialCdAccum, sizeof(endlessPerkSpecialCdAccum));
	rollback_register("endless.kineticAccum", endlessPerkKineticAmmoAccum, sizeof(endlessPerkKineticAmmoAccum));
	rollback_register("endless.killDedup", &endlessLastCountedLink, sizeof(endlessLastCountedLink));
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
static const char *endlessMilestoneLine(int zone)
{
	// The marker is the first zone of each band, so it stays put when a line is reworded.
	static const char *const lines[] = {
		/*   0 */ "The gate seals shut behind you.",
		/*   5 */ "The last friendly beacon fades.",
		/*  10 */ "Something is following your signal.",
		/*  15 */ "The wreckage ahead is still warm.",
		/*  20 */ "Command has stopped answering.",
		/*  25 */ "Enemy signals fill every channel.",
		/*  30 */ "The stars no longer match the charts.",
		/*  35 */ "Every route leads farther in.",
		/*  40 */ "The navigation computer refuses course.",
		/*  45 */ "The wrecks are starting to look familiar.",
		/*  50 */ "Something has learned how you fight.",
		/*  55 */ "The guns have not cooled in hours.",
		/*  60 */ "A dreadful hush falls between volleys.",
		/*  65 */ "The hull remembers every impact.",
		/*  70 */ "No human signal reaches this far.",
		/*  75 */ "Even the warning lights fall silent.",
		/*  80 */ "The charts end here.",
		/*  85 */ "Nothing living knows these coordinates.",
		/*  90 */ "Reality bends around the wreckage.",
		/*  95 */ "Your engines run on borrowed time.",
		/* 100 */ "Legends come this far to die.",
		/* 105 */ "The enemy no longer sees you as prey.",
		/* 110 */ "Their fleets gather beyond the static.",
		/* 115 */ "The stars flicker when you fire.",
		/* 120 */ "The distress calls are no longer yours.",
		/* 125 */ "The swarm goes on without end.",
		/* 130 */ "They tell stories about your ship.",
		/* 135 */ "Your signal has become a warning.",
		/* 140 */ "Time loses count between the gunfire.",
		/* 145 */ "The last known beacon has gone dark.",
		/* 150 */ "The end of the map was the beginning.",
		/* 155 */ "These zones should not exist.",
		/* 160 */ "The next sector is waiting for you.",
		/* 165 */ "Still it grows. Still you press on.",
		/* 170 */ "No rescue was ever coming.",
		/* 175 */ "There are no maps for what comes next.",
		/* 180 */ "Enemy fleets turn before you arrive.",
		/* 185 */ "They scatter when your signal appears.",
		/* 190 */ "The hunters have become the hunted.",
		/* 195 */ "Only the guns remember you now.",
		/* 200 */ "Two hundred zones burn behind you.",
		/* 205 */ "The guns glow white with wrath.",
		/* 210 */ "Entire fleets vanish in your wake.",
		/* 215 */ "Your name is now an evacuation order.",
		/* 220 */ "Even their warships flee your signal.",
		/* 225 */ "You are the anomaly on their charts.",
		/* 230 */ "The universe is running out of hiding places.",
		/* 235 */ "Creation grows thin around your ship.",
		/* 240 */ "There are no more stars ahead.",
		/* 245 */ "There is nothing left to chart.",
	};

	int i = zone / 5;
	if (i < 0)
		i = 0;
	if (i >= (int)COUNTOF(lines))
		i = (int)COUNTOF(lines) - 1;
	return lines[i];
}

// Sign-off shown after the final milestone line.
static const char *endlessMilestoneEpilogue(int zone)
{
	return (zone >= 250) ? "Thank you for playing." : NULL;
}

// Centered on vga_width, the full display width.
static void endlessGlowCentered(int y, unsigned int font, const char *text)
{
	textGlowFont = font;
	JE_outTextGlow(VGAScreen, (vga_width - JE_textWidth(text, font)) / 2, y, text);
}

// One stat row, drawn through a single shared glow so both columns arrive together.
static void endlessGlowRow(int left, int right, int y, unsigned int font,
                           const char *label, const char *value)
{
	textGlowFont = font;
	const int x[2] = { left, right - JE_textWidth(value, font) };
	const char *const text[2] = { label, value };
	JE_outTextGlowMulti(VGAScreen, x, y, text, 2);
}

// Dim the campaign-ending ship art behind the run summary.
#define ENDLESS_RUNEND_PIC   "tshp2.pcx"
#define ENDLESS_RUNEND_DIM   32   // retained background brightness, in percent

static void endlessDrawRunEndBackdrop(void)
{
	JE_loadPCX(ENDLESS_RUNEND_PIC);

	// Center the legacy-width image and extend its edge columns into the side strips.
	const int pad = (vga_width - LEGACY_WIDTH) / 2;   // left strip
	const int tail = vga_width - pad - LEGACY_WIDTH;  // right strip
	if (pad > 0 && tail >= 0 && vga_width <= VGAScreen->pitch)
	{
		for (int row = 0; row < vga_height; ++row)
		{
			Uint8 *const p = (Uint8 *)VGAScreen->pixels + row * VGAScreen->pitch;
			const Uint8 left = p[0], right = p[LEGACY_WIDTH - 1];
			memmove(p + pad, p, LEGACY_WIDTH);
			memset(p, left, pad);
			memset(p + pad + LEGACY_WIDTH, right, tail);
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

	// Bring up the dimmed ship illustration the summary is drawn over.
	VGAScreen = VGAScreenSeg;
	JE_clr256(VGAScreen);
	endlessDrawRunEndBackdrop();
	JE_showVGA();
	fade_palette(colors, 15, 0, 255);

	JE_wipeKey();
	frameCountMax = 4;
	SDL_Color white = { 255, 255, 255 };
	set_colors(white, 254, 254);

	// Tally rows use left labels, right values, and SMALL_FONT_SHAPES for its digit support.
	char fellLine[48];
	snprintf(fellLine, sizeof(fellLine), "You fell in Zone %d", endlessRunDepth + 1);

	// Zero-initialized for the analyzer (C6001): it cannot correlate the rowCount guard with which
	// entries the width loop below reads.
	struct { char label[28], value[40]; } rows[10] = { 0 };
	int rowCount = 0;
	#define RUNEND_ROW(lbl, ...) \
		do { \
			if (rowCount < (int)COUNTOF(rows)) \
			{ \
				SDL_strlcpy(rows[rowCount].label, (lbl), sizeof(rows[0].label)); \
				snprintf(rows[rowCount].value, sizeof(rows[0].value), __VA_ARGS__); \
				++rowCount; \
			} \
		} while (0)

	// Records are split by mode, difficulty, crew size and chart rule, so the summary names them.
	const int runDifficulty = (initialDifficulty >= 0
	                           && (size_t)initialDifficulty < COUNTOF(difficultyNameB))
	                        ? initialDifficulty : 0;
	// Two ships change what every other row means (the kills, the cash, the zones reached), so the
	// Mode row names the crew size as well.
	if (endlessCoop())
		RUNEND_ROW("Mode:", "%s, %s, Multiplayer", endlessRunModeName(endlessRunMode),
		           difficultyNameB[runDifficulty]);
	else
		RUNEND_ROW("Mode:", "%s, %s", endlessRunModeName(endlessRunMode),
		           difficultyNameB[runDifficulty]);
	RUNEND_ROW("Base Level:", "%s", endlessBaseLevelRuleName(endlessRunBaseRule));
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

	// Show the record selected by the Mode and Base Level rows.
	char recordLine[64];
	int *bestRecord;
	bool *bestMark;
	endlessRunRecord(&bestRecord, &bestMark);
	const int best = *bestRecord;
	const int recordGain = best - endlessBestZoneAtStart();
	const char *const customMark = *bestMark ? " C" : "";
	if (recordGain > 0)
		snprintf(recordLine, sizeof(recordLine), "New furthest zone: %d%s   up %d",
		         best, customMark, recordGain);
	else
		snprintf(recordLine, sizeof(recordLine), "Furthest zone: %d%s", best, customMark);

	// Size the block to its widest label and widest value, then center it as a unit so both columns
	// line up whatever the run produced.
	int labelW = 0, valueW = 0;
	for (int i = 0; i < rowCount; ++i)
	{
		const int lw = JE_textWidth(rows[i].label, SMALL_FONT_SHAPES);
		const int vw = JE_textWidth(rows[i].value, SMALL_FONT_SHAPES);
		if (lw > labelW)
			labelW = lw;
		if (vw > valueW)
			valueW = vw;
	}

	const int colGap = 30;                // enough that the two columns read as columns
	const int blockMax = vga_width - 40;  // ...but never past the margins
	int blockW = labelW + colGap + valueW;
	if (blockW > blockMax)
		blockW = blockMax;  // squeeze the gap first: the columns meet before anything clips
	const int blockLeft = (vga_width - blockW) / 2;
	const int blockRight = blockLeft + blockW;

	const char *const closingLine = endlessMilestoneLine(endlessRunDepth + 1);
	const char *const closingTail = endlessMilestoneEpilogue(endlessRunDepth + 1);

	// Glow rows need a 12-pixel pitch; descenders need 15.
	const int titleH    = 15;  // FONT_SHAPES cap height
	const int lineH     = 13;  // SMALL_FONT_SHAPES, descender included
	const int stepMin   = 11;  // hard floor, where the glow outlines of adjacent rows merge
	const int recordGap = 3;   // gap for the record, which hangs off the closing line
	int step     = 14;         // row pitch: two clear scanlines between the glyph bodies
	int titleGap = 9;          // breathing room under the title
	int fellGap  = 4;          // ...under fellLine, so the tally reads as a block of its own
	int tailGap  = 9;          // ...and above the closing milestone line

	// The milestone line, plus the sign-off at zone 250.
	const int closeLines = (closingTail != NULL) ? 2 : 1;

	int total;
	// Tighten block gaps before reducing row pitch.
	while ((total = titleH + titleGap + lineH + fellGap + (rowCount - 1) * step
	                + lineH + tailGap + lineH
	                + (closeLines - 1) * (recordGap + lineH)
	                + recordGap + lineH) > 190)
	{
		if (titleGap > 5)
			--titleGap;
		else if (tailGap > 5)
			--tailGap;
		else if (fellGap > 2)
			--fellGap;
		else if (step > stepMin)
			--step;
		else
			break;
	}

	int y = (vga_height - total) / 2;

	endlessGlowCentered(y, FONT_SHAPES, "RUN OVER");
	y += titleH + titleGap;

	endlessGlowCentered(y, SMALL_FONT_SHAPES, fellLine);
	y += lineH + fellGap;

	for (int i = 0; i < rowCount; ++i, y += step)
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

	// Fade the current track while the summary opens.
	MusicFadeOut songFade;
	music_fade_out_init(&songFade);

	do
	{
		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);
		touch_ui_idle_repaint();
		music_fade_out_tick(&songFade);
		// The tally has no time limit and an online run is still a session while it is read.
		NETWORK_KEEP_ALIVE();
		while (network_shop_pump())
			;
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
