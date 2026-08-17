/*
 * Public Endless mode interface.
 * Implementation details are private to endless.c and endless_*.c.
 */
#ifndef ENDLESS_H
#define ENDLESS_H

#include "config.h"
#include "opentyr.h"

#include <stdbool.h>
#include <stddef.h>

// Combat effects can be enabled in a campaign without enabling Endless run flow.
static inline bool endlessFxActive(void) { return endlessMode || endlessCampaignMods; }

/* Online co-op. Both ships fly a shared run: the sector, its modifiers and the course slate are
 * run-wide, while wallets, stock, gear and the personal upgrades below belong to one player each.
 * See "Endless online" in doc/notes.md. */

// The ship this machine outfits and spends for: player 1 solo, the local ship in co-op.
uint    endlessEconomyIndex(void);
// ...and the other one, which is the same ship outside co-op.
uint    endlessPartnerIndex(void);
// Ships a per-player Endless effect walks: two in co-op, one otherwise.
uint    endlessEffectPlayers(void);

/* Sector modifiers as ONE ship sees them: the charted set, plus whatever that player bought for
 * themselves. A drive one player paid for boosts that player alone, on both machines.
 * endlessFxPlayer is the ship whose effects are being computed; every caller that works through
 * the players in turn sets it, and it is 0 outside co-op. */
extern Uint64 endlessPlayerMods[2];
void endlessSetFxPlayer(uint p);
uint endlessFxPlayer(void);

// Bits a purchase applies to the buyer alone. Everything else it can buy changes the sector.
#define ENDLESS_PERSONAL_MOD_MASK ((Uint64)( \
	ENDLESS_MOD_KILLFIRE_ANY | ENDLESS_MOD_OVERCHARGE | ENDLESS_MOD_NITRO | \
	ENDLESS_MOD_OVERHEAT | ENDLESS_MOD_DUD))

// Fold each player's purchases into their own mask; the shared bits go into endlessActiveMods.
void endlessApplyPurchasedMods(void);
static inline bool endlessCoop(void) { return endlessMode && coopEndlessMode; }

// Who charts the next course. Persisted in the run save and the lobby block, so append only.
typedef enum {
	ENDLESS_PICK_HOST = 0,
	ENDLESS_PICK_GUEST,
	ENDLESS_PICK_ALTERNATE,   // takes turns; the turn flag rides the run save
	ENDLESS_PICK_COINFLIP,    // seeded from the run RNG, so both machines agree
	ENDLESS_PICK_COUNT
}
EndlessCourseChooser;

/* Whose combo a kill feeds. Individual keeps each ship's kill-fire streak its own, which is what
 * a drive one player paid for is worth; Shared has every kill feed both. Host-authoritative. */
extern bool endlessCoopComboShared;
// A kill nobody can be credited with, so neither player's streak is punished for it.
#define ENDLESS_KILLER_NONE (-1)

extern EndlessCourseChooser endlessCourseChooser;  // host-authoritative session setting
extern bool endlessCoopHostCharts;                 // Alternating: is the host charting this one?
const char *endlessCourseChooserName(EndlessCourseChooser mode);
bool endlessLocalPlayerCharts(void);   // does this machine pick the next course?
uint endlessChartingPlayerIndex(void); // which SEAT picks it, identical on both machines
void endlessAdvanceCourseTurn(void);   // call once a course has been committed

// A downed player spectates until the zone ends, then revives at the outpost.
extern bool endlessPlayerDowned[2];
/* The other player used the in-game menu's Quit. In Endless that means "back to the outpost",
 * the same as pressing it here, so the peer joins them there rather than being torn down; the
 * level ends with playerEndLevel set, which on its own would read as this player's death. */
void endlessCoopPeerQuitLevel(void);
bool endlessAnyPlayerFlying(void);     // at least one ship still alive and not downed
// Which ship a homing or course-correcting shot at (fromX, fromY) goes for: the nearer one
// still flying, and player 1 outside co-op.
uint endlessDangerTargetPlayer(int fromX, int fromY);
// A homing enemy picks its ship by coin toss when it is created (0 outside co-op, and no roll is
// spent there), and keeps it for life unless that ship goes down.
uint endlessRollHomingTarget(void);
uint endlessHomingTargetPlayer(uint stored);
void endlessReviveDownedAtOutpost(void);

// Number of active modifier bits.
static inline int endlessPopCount64(Uint64 v)
{
	int n = 0;
	for (; v != 0; v &= v - 1)
		++n;
	return n;
}

// Sector modifiers. Bits 0-30 fit in the enum; later bits use Uint64 defines.
enum {
	ENDLESS_MOD_FORTIFIED   = 1u << 0,  // enemy and boss HP
	ENDLESS_MOD_FRENZY      = 1u << 1,  // enemy fire rate
	ENDLESS_MOD_SWIFT       = 1u << 2,  // enemy shot speed
	ENDLESS_MOD_FRAGILE     = 1u << 3,  // lower enemy HP
	ENDLESS_MOD_BOUNTY      = 1u << 4,  // larger clear reward
	ENDLESS_MOD_DEVASTATING = 1u << 5,  // enemy shot damage
	ENDLESS_MOD_ELITEPACK   = 1u << 6,  // 50% special enemies
	ENDLESS_MOD_APEX        = 1u << 7,  // 100% special enemies
	ENDLESS_MOD_LEGION      = 1u << 8,  // 100% champions
	ENDLESS_MOD_ENRAGE      = 1u << 9,  // time-based enemy fire ramp
	ENDLESS_MOD_KAMIKAZE    = 1u << 10, // moderate homing
	ENDLESS_MOD_GRAVITY     = 1u << 11, // downward pull
	ENDLESS_MOD_TURBODRIVE  = 1u << 12, // kill-fed fire boost
	ENDLESS_MOD_OVERCHARGE  = 1u << 13, // player damage boost
	ENDLESS_MOD_DILATION    = 1u << 14, // slower enemy shots
	ENDLESS_MOD_FAVOR       = 1u << 15, // next-shop discount
	ENDLESS_MOD_CURSED      = 1u << 16, // cash now, empty next shop
	ENDLESS_MOD_OVERCLOCK   = 1u << 17, // enemy fire, shots, and scroll
	ENDLESS_MOD_SLIPSTREAM  = 1u << 18, // faster scroll
	ENDLESS_MOD_OVERLOAD    = 1u << 19, // stronger Overclock
	ENDLESS_MOD_WARP        = 1u << 20, // stronger Slipstream
	ENDLESS_MOD_OVERDRIVE   = 1u << 21, // Turbodrive and Overblast
	ENDLESS_MOD_BACKFIRE    = 1u << 22, // kill-fed gun jam
	ENDLESS_MOD_BURNOUT     = 1u << 23, // jam and damage penalty
	ENDLESS_MOD_OVERBLAST   = 1u << 24, // kill-fed damage boost
	ENDLESS_MOD_MISFIRE     = 1u << 25, // kill-fed damage penalty

	// Gamble-only next-sector effects.
	ENDLESS_MOD_MARKED    = 1u << 26,
	ENDLESS_MOD_NITRO     = 1u << 27,
	ENDLESS_MOD_OVERHEAT  = 1u << 28,
	ENDLESS_MOD_DUD       = 1u << 29,

	ENDLESS_MOD_HOMING    = 1u << 30, // weak homing
};

// Bit 31 cannot be a C enum constant because it may not fit in int.
#define ENDLESS_MOD_RAMPAGE (1u << 31)

// Bits 32 and above require Uint64 storage throughout courses and saves.
#define ENDLESS_MOD_TOPSY    ((Uint64)1 << 32)  // flipped playfield and controls
#define ENDLESS_MOD_SLUGGISH ((Uint64)1 << 33)  // slower ship movement
#define ENDLESS_MOD_GRAVITY_OMNI ((Uint64)1 << 34) // random fixed gravity heading
#define ENDLESS_MOD_SHIELDLESS ((Uint64)1 << 35)
#define ENDLESS_MOD_DEADGEN    ((Uint64)1 << 36)
#define ENDLESS_MOD_NOCHAMP    ((Uint64)1 << 37)
#define ENDLESS_MOD_NOELITE    ((Uint64)1 << 38)

// Finale marker; mechanics remain in the other modifier bits.
#define ENDLESS_MOD_THEEND     ((Uint64)1 << 39)
#define ENDLESS_MOD_MARTYRDOM   ((Uint64)1 << 40)
#define ENDLESS_MOD_SEEKER      ((Uint64)1 << 41)
#define ENDLESS_MOD_STATIC      ((Uint64)1 << 42)
#define ENDLESS_MOD_RETALIATION ((Uint64)1 << 43)
#define ENDLESS_MOD_AEGIS        ((Uint64)1 << 44)
#define ENDLESS_MOD_FLAKSCREEN   ((Uint64)1 << 45)
#define ENDLESS_MOD_AUXREACTOR   ((Uint64)1 << 46)
#define ENDLESS_MOD_LOWPROFILE   ((Uint64)1 << 47)
#define ENDLESS_MOD_GIANTKILLER  ((Uint64)1 << 48)
#define ENDLESS_MOD_SHOCKWAVE    ((Uint64)1 << 49)
#define ENDLESS_MOD_STARCHARTS   ((Uint64)1 << 50)
#define ENDLESS_MOD_BREAKTHROUGH ((Uint64)1 << 51)
#define ENDLESS_MOD_SOFTLANDING  ((Uint64)1 << 52)
#define ENDLESS_MOD_CLEANSIGNALS ((Uint64)1 << 53)

// A course carries at most one kill-fire modifier.
#define ENDLESS_MOD_FIREBOOST      (ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERDRIVE)
#define ENDLESS_MOD_FIREJAM        (ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_BURNOUT)
#define ENDLESS_MOD_DMGUP          (ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_OVERBLAST)
#define ENDLESS_MOD_DMGDOWN        (ENDLESS_MOD_BURNOUT | ENDLESS_MOD_MISFIRE)
#define ENDLESS_MOD_STACKED        (ENDLESS_MOD_DMGUP | ENDLESS_MOD_DMGDOWN)
#define ENDLESS_MOD_KILLFIRE_GOOD  (ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERDRIVE | ENDLESS_MOD_OVERBLAST)
#define ENDLESS_MOD_KILLFIRE_EVIL  (ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_BURNOUT | ENDLESS_MOD_MISFIRE)
#define ENDLESS_MOD_KILLFIRE_ANY   (ENDLESS_MOD_KILLFIRE_GOOD | ENDLESS_MOD_KILLFIRE_EVIL)

// Sprite-filter banks shared by special enemies and their health bars.
#define ENDLESS_ELITE_FILTER    0xD0
#define ENDLESS_CHAMPION_FILTER 0x50  // bank 5 = purple (an "epic" aura; 0xB0 read as brown)

// Shade lift for the endless aura and icon spark showers (see rl_superpixel_value). Over a dark
// background it runs them from shade 13 down to 6 rather than 7 down to 0, so they stay in the
// bright half of the bank for their whole 15 ticks.
#define ENDLESS_SPARK_BRIGHT 6

// Shade lift for a tinted explosion (see blit_sprite2_blend_filter). Its sprites are mostly bank 7
// around shade 7, which the blend halves to about 4: legible in their own yellow ramp, near-black
// in the elite banks. Well under the aura's lift, so the sprite keeps its own gradient.
#define ENDLESS_EXPLOSION_BRIGHT 3

// Shade lift for a tinted enemy bullet (see blit_sprite2_filter_bright). Shot art spans the whole
// ramp, and the champion bank's bottom third is nearly black, so its darker pixels would disappear
// at the sprite's own shade. Smaller than the explosion lift, which is undoing a blend as well.
#define ENDLESS_SHOT_BRIGHT 2

// Player-side kill-fire tint banks.
#define ENDLESS_TURBODRIVE_SHIP_FILTER 0xC0
#define ENDLESS_OVERDRIVE_SHIP_FILTER  0x70
#define ENDLESS_OVERBLAST_SHIP_FILTER  0x90
#define ENDLESS_EVIL_SHIP_FILTER       0x40

// Gauge bases must leave room for all 14 shades inside one palette bank.
#define ENDLESS_FREE_POWER_GAUGE_BASE 1
#define ENDLESS_SALVO_GAUGE_BASE  (12 * 16 + 1)
#define ENDLESS_SALVO_SPARK_COLOR (12 << 4)

// Kill-fire damage reaches this bonus at the stack cap.
#define ENDLESS_OVERDRIVE_MAX_STACKS 200
#define ENDLESS_OVERDRIVE_DMG_MAX    150

// Global effective-HP multiplier ceiling.
#define ENDLESS_HP_MULT_MAX 24

// Authored level identity retained after levelName becomes "ZONE n".
const char *endlessBaseLevelName(void);
int         endlessBaseLevelEpisode(void);
int         endlessBaseLevelSection(void);
const char *endlessPrevLevelName(void);
int         endlessPrevLevelEpisode(void);
int         endlessPrevLevelSection(void);

// Recent authored levels, newest first. Out-of-range accessors return 0.
int endlessRecentLevelCount(void);
int endlessRecentLevelEpisode(int i);
int endlessRecentLevelSection(int i);

// Cleared zones; drives progression and score.
extern int endlessRunDepth;

// Per-run kill totals. Boss kills count only enemies with a boss bar.
extern int endlessRunKills;
extern int endlessRunBossKills;

// Run income sources. Append only; the save block reserves ENDLESS_SAVE_CASH_SOURCES slots.
typedef enum {
	ENDLESS_CASH_KILL = 0,   // enemy value, paid the instant a ship is destroyed
	ENDLESS_CASH_PICKUP,     // cash and gems collected off the playfield
	ENDLESS_CASH_BOUNTY,     // Elite / Champion kill bounties
	ENDLESS_CASH_CLEAR,      // the zone-clear bonus
	ENDLESS_CASH_INTEREST,   // bank interest on unspent cash
	ENDLESS_CASH_GAMBLE,     // E-Shop gamble winnings
	ENDLESS_CASH_PERK,       // the cash taken instead of a perk
	ENDLESS_CASH_OTHER,      // undeclared: the audit saw a rise no endlessCashCredit announced
	ENDLESS_CASH_START,      // the difficulty-based stake the run is handed at zone 1
	ENDLESS_CASH_TRADEIN,    // gear sold beyond what buying it booked as spent (granted gear, e.g. the starting gun)
	ENDLESS_CASH_SOURCES
} EndlessCashSource;

// Run spending sinks. Append only; the save block reserves ENDLESS_SAVE_CASH_SINKS slots.
typedef enum {
	ENDLESS_SINK_GEAR = 0,   // upgrade-shop trades: guns, power steps, shields, generators, sidekicks
	ENDLESS_SINK_SUPPLIES,   // E-Shop kit: specials, superbombs, sabotage charges
	ENDLESS_SINK_BUFF,       // kill-fire buffs: Turbodrive / Overdrive / Overblast
	ENDLESS_SINK_REVIVE,     // revive tokens
	ENDLESS_SINK_PERK,       // bought extra perk picks
	ENDLESS_SINK_REROLL,     // shop-stock rerolls
	ENDLESS_SINK_HULL,       // Reinforce hull tiers
	ENDLESS_SINK_GAMBLE,     // wagers and the machine's crueller outcomes
	ENDLESS_CASH_SINKS
} EndlessCashSink;

// Route every Endless wallet change through this interface. Credit and Debit book ordinary
// movement; Begin/Commit brackets the upgrade menu's temporary balance and full-refund trades.
void endlessCashCredit(Sint64 amount, EndlessCashSource src);
void endlessCashDebit(Sint64 amount, EndlessCashSink sink);
void endlessShopTradeBegin(void);
void endlessShopTradeCommit(void);

// Saturating run totals and their source/sink breakdowns.
extern Uint64 endlessRunCashEarned;
extern Uint64 endlessRunCashSpent;
extern Uint64 endlessCashBySource[ENDLESS_CASH_SOURCES];
extern Uint64 endlessCashBySink[ENDLESS_CASH_SINKS];
const char *endlessCashSourceName(EndlessCashSource src);
const char *endlessCashSinkName(EndlessCashSink sink);

// Report and book wallet drift caused by bypassing the economy interface. Expected to no-op.
void endlessCashAudit(void);
void endlessCashResync(void);          // re-anchor without booking either way (run start, load, sortie revert)
void endlessCashDebugOverwrite(void);  // the debug screen overwrote the wallet: book the delta, no warning

// Count one logical enemy, credited to `killer` (0/1, or ENDLESS_KILLER_NONE when nothing can
// claim it). All kill paths go through enemy_logical_death.
void endlessCountKill(int linknum, int killer);

extern Uint64 endlessActiveMods;

// Run-persistent Reinforce bonus, per player.
extern int endlessArmorBonus[2];

void endlessResetRun(void);

// All-time records, stored in opentyrian.cfg so Hardcore runs can update theirs. A run writes the
// record for the difficulty it was started on; the figure for a mode as a whole is derived from
// those, so it can never disagree with the breakdown behind it.
void endlessNoteZoneReached(int zone);
void endlessRecordRunStart(void);
int  endlessBestZoneAtStart(void);

// The six difficulties an Endless run can start on, in difficulty-select order. That order is the
// on-disk order of the per-difficulty records too, so append to it rather than reordering it.
#define ENDLESS_DIFFICULTY_COUNT 6
extern const int endlessDifficultyLevel[ENDLESS_DIFFICULTY_COUNT];

/* Records are kept apart by crew size: two ships clear zones a solo run cannot, so the two are
 * never compared. Index 0 is a one-player run, 1 an online co-op one. */
#define ENDLESS_PLAYER_TABLES 2
int endlessRecordTable(void);   // which table the run in progress writes
const char *endlessRecordTableName(int players);

/* How a visit's chart picks the levels behind its routes. Appended to rather than reordered: the
 * value indexes the record tables, rides the save, and travels on the connect packet. Menu order
 * pairs each rule with its Shuffle twin and is a separate list, endlessBaseRuleAtMenuIndex. */
typedef enum {
	ENDLESS_BASE_VARIED = 0,       // every charted route is its own level
	ENDLESS_BASE_SAME,             // one level fills the chart, leaving the modifiers as the choice
	ENDLESS_BASE_VARIED_SHUFFLE,   // Varied, drawing from the level bag in doc/notes.md
	ENDLESS_BASE_SAME_SHUFFLE,     // Same, drawing its one level from that bag
	ENDLESS_BASE_RULE_COUNT
}
EndlessBaseRule;

/* ...and by the chart rule above, for the same reason: a slate of one repeated base level is a
 * different run from a slate of five, and a Shuffle run meets every level in the pool on a
 * schedule an unshuffled one does not. Each rule has a High Scores page of its own. */
#define ENDLESS_BASE_TABLES ENDLESS_BASE_RULE_COUNT
const char *endlessBaseLevelRuleName(int variant);   // the Base Level row's value for that rule
const char *endlessBaseLevelRuleHelp(int variant);   // the one-line explanation both pickers show

// Menu and record-page order, which is not the enum order. Both directions clamp to Varied.
EndlessBaseRule endlessBaseRuleAtMenuIndex(int index);
int             endlessBaseRuleMenuIndex(EndlessBaseRule rule);

/* The rule the run in progress is flying, picked on the seed screen (or by the host in the Endless
 * lobby) and fixed from there: nothing can move a run between the four sets of records. */
extern EndlessBaseRule endlessRunBaseRule;

// Both Same rules fill every route from one draw; both Shuffle rules take their draws in bag order.
static inline bool endlessBaseRuleShared(EndlessBaseRule rule)
{
	return rule == ENDLESS_BASE_SAME || rule == ENDLESS_BASE_SAME_SHUFFLE;
}

static inline bool endlessBaseRuleShuffled(EndlessBaseRule rule)
{
	return rule == ENDLESS_BASE_VARIED_SHUFFLE || rule == ENDLESS_BASE_SAME_SHUFFLE;
}

// Slot a difficulty level occupies, or -1 when runs on it are not broken out.
int endlessDifficultySlot(int difficulty);

// A custom weapon only counts once the run has fought with it: a shot has to leave the custom port
// inside a running zone (shots.c calls this for every one), and that zone has to end.
void endlessNoteCustomWeaponShot(void);
void endlessCustomWeaponZoneEnd(void);
extern bool endlessRunUsedCustom;

void endlessRecordConfigSave(ConfigSection *section);
void endlessRecordConfigLoad(const ConfigSection *section);

// Run seed. Structural randomness is deterministic; combat randomness is not.
#define ENDLESS_SEED_MAXLEN 24

// How forgiving the run is. Picked on the seed screen, fixed for the whole run.
typedef enum {
	ENDLESS_RUNMODE_RELAXED = 0,  // a fatal hit opens the death menu: retry the zone, or back to the outpost
	ENDLESS_RUNMODE_STANDARD,     // a fatal hit ends the run (the pause menu's Quit Level is still a bail)
	ENDLESS_RUNMODE_HARDCORE,     // no saving, and no bail once the ship is hit
	ENDLESS_RUNMODE_COUNT
}
EndlessRunMode;

extern EndlessRunMode endlessRunMode;

// Hardcore disables all run saves.
static inline bool endlessHardcore(void) { return endlessRunMode == ENDLESS_RUNMODE_HARDCORE; }

// "Relaxed" / "Standard" / "Hardcore", as shown on the seed, run-over and Zone Records screens.
const char *endlessRunModeName(EndlessRunMode mode);

// The mode's record on one difficulty slot, and its deepest on any of them. The latter also counts
// the untagged record a config written before the breakdown existed still carries.
int endlessBestZoneForDifficulty(int variant, int players, EndlessRunMode mode, int slot);
int endlessBestZoneAny(int variant, int players, EndlessRunMode mode);

// ...and the deepest under any base-level rule, the figure the record page opens on.
int endlessBestZoneAnyRule(int players, EndlessRunMode mode);

// " C" when a custom weapon set that record, otherwise an empty string. Every record is shown
// against a named mode and difficulty, so the zone number carries this mark alone.
const char *endlessRecordAnyCustomMark(int variant, int players, EndlessRunMode mode);
const char *endlessRecordDiffCustomMark(int variant, int players, EndlessRunMode mode, int slot);
const char *endlessRecordAnyRuleCustomMark(int players, EndlessRunMode mode);

// Erase records. Destructive, so only call these behind a confirmation. Clearing the deepest peels
// a mode back one record at a time, which keeps its any-difficulty figure equal to what is left.
void endlessClearDeepestRecord(int variant, int players, EndlessRunMode mode);
void endlessClearRecordDifficulty(int variant, int players, EndlessRunMode mode, int slot);

// Returns false when the seed screen is cancelled.
bool endlessSeedSelect(char *outSeed, size_t outN, EndlessRunMode *outMode,
                       EndlessBaseRule *outBaseRule);

void endlessSetSeed(const char *s);
const char *endlessSeedString(void);

// Re-fork both players' outpost draw streams from the run seed.
void endlessReseedPlayers(Uint64 salt);

/* The Endless half of every save slot, kept in memory beside saveFiles[] and read and written with
 * them as 'endless' sections of opentyrian.sav (config.c owns the file). JE_saveGame captures or
 * clears the slot's half through endlessSaveCaptureSlot before the file is written. */
bool endlessSlotHasRun(JE_byte slot);
void endlessSaveCaptureSlot(JE_byte slot);
void endlessSaveConfigRead(Config *config);
void endlessSaveConfigWrite(Config *config);
bool endlessSaveLegacyLoad(void);         // one-time import of the binary endless.sav an older build wrote
bool endlessSaveRepairFromLegacy(void);   // ...and the halves an Endless-named slot lacks, if it still has them
bool endlessSaveLegacyExists(void);       // is that file there at all?
bool endlessSaveLegacyWasRead(void);      // ...and did this session read it through?

/* Everything one Endless co-op player owns for themselves that the other machine also has to
 * know: the run-wide sector effects are derived identically on both sides, but these are bought.
 * Rides every outpost sync packet; see "Endless online" in doc/notes.md. */
#define ENDLESS_PLAYER_BLOCK_PERKS 32
#define ENDLESS_PLAYER_BLOCK_SIZE  (4 + 4 * 9 + 8 * 3 + ENDLESS_PLAYER_BLOCK_PERKS + 1 + 4)
int  endlessPackPlayerBlock(Uint8 *buf, uint p);
void endlessUnpackPlayerBlock(const Uint8 *buf, uint p);

/* What the Endless debug panel rewrites behind the outpost's back: the run's modifier set and
 * depth, and both ships' perk rows and personal buffs. The panel edits either ship straight into
 * simulation state, so the block is whole-state and the adopter takes all of it; the debug-sync
 * generation and the jump's host-wins rule settle two panels open at once. Rides the debug-sync
 * block and the Endless zone jump; see "Endless debug panel online" in doc/notes.md. */
#define ENDLESS_DEBUG_BLOCK_PERKS 32
#define ENDLESS_DEBUG_BLOCK_SIZE  (8 + 2 + 2 * ENDLESS_DEBUG_BLOCK_PERKS + 2 * 8 + 2 * 4)
void endlessPackDebugBlock(Uint8 *buf);
void endlessUnpackDebugBlock(const Uint8 *buf);

/* Online co-op resume. The host serializes the live run as the text a save slot holds and the
 * joiner adopts it. Returns 0 / false when there is nothing usable. */
#define ENDLESS_RUN_WIRE_MAX 16384
size_t endlessRunSerialize(Uint8 *out, size_t max);
bool   endlessRunAdopt(const Uint8 *bytes, size_t len);
// Redeal this seat's shop rows: the fallback when an adopted record carries no partner half.
void endlessShopRedrawStock(void);
/* The partner's outpost half a save stores next to this machine's own (nothing is written on
 * the partner's disk). Their machine packs its half onto the save acknowledgement; the stash
 * holds it until the visit ends. */
#define ENDLESS_OUTPOST_BLOCK_SIZE (9 + 90 + 8)
int  endlessPackOwnOutpost(Uint8 *buf);
void endlessPartnerOutpostStash(uint seat, const Uint8 *block);
void endlessPartnerOutpostClear(void);
bool endlessLoadSlot(JE_byte slot);
bool endlessResumePending(void);

// Quit Level restores the launch snapshot. Hardcore keeps the sortie locked.
extern bool endlessQuitToOutpost;
extern bool endlessLockedSortie;

void endlessCaptureSortie(void);
void endlessRestoreSortie(void);
bool endlessSortieValid(void);
void endlessArmLockedRelaunch(void);

// Death menu "Restart Zone": restore + re-arm the same level, skipping the outpost.
void endlessRestartSortie(void);

// Fallback picker; normal runs use the course path.
JE_byte endlessPickNextLevel(void);

// Course list for the current outpost.
void        endlessGenerateCourses(void);
int         endlessCourseCount(void);
const char *endlessCourseName(int i);
const char *endlessCourseHelp(int i);
const char *endlessCourseRank(int i);
int         endlessCourseRankLevel(int i);
JE_byte     endlessCoursePlanet(int i);
JE_byte     endlessCourseSection(int i);
JE_byte     endlessSelectCourse(int i);
Sint64      endlessCoursePayout(int i);

// Radar perk: one reroll of the whole chart per outpost (the menu row in game_menu.c).
bool        endlessCourseRerollOffered(void);
bool        endlessCourseRerollSpent(void);
const char *endlessCourseRerollHelp(void);
void        endlessCourseReroll(void);

// One modifier row on a course card.
typedef struct {
	const char *word;
	int         weight;
	bool        hostile;
	bool        cleansed;
} EndlessCourseModRow;

// Returns rows in descending danger order.
int endlessCourseModRows(int i, EndlessCourseModRow *rows, int max);

// Registry description for one modifier bit.
const char *endlessModWord(Uint64 bit);

// Pay the clear reward and open the outpost.
void endlessBetweenLevels(void);

// Outpost actions; prices escalate within one visit.
void endlessResetShopPrices(void);
Sint64 endlessRerollPrice(void);    // current reroll cost (for the menu label)
Sint64 endlessHullPrice(void);      // current hull-upgrade cost (for the menu label)
bool endlessHullMaxed(void);      // true once the run's armor bonus is capped
bool endlessTryReroll(void);      // buy a shop reroll; false if unaffordable
bool endlessTryReinforce(void);   // buy a +armor hull upgrade; false if unaffordable/maxed

// Cash-fraction purchases use the balance recorded on shop entry.
Sint64 endlessTurbodrivePrice(void);         // Turbodrive cost (66% of entry cash), for the label
Sint64 endlessOverblastPrice(void);    // Overblast cost (75% of entry cash), for the label
Sint64 endlessOverdrivePrice(void);   // Overdrive cost (95% of entry cash), for the label
Sint64 endlessSpecialPrice(void);      // Buy-Special cost (80% of entry cash), for the label
// Persisted IDs: append only.
enum {
	ENDLESS_BUFF_KIND_NONE = 0,
	ENDLESS_BUFF_KIND_TURBODRIVE,
	ENDLESS_BUFF_KIND_OVERDRIVE,
	ENDLESS_BUFF_KIND_OVERBLAST,
};
int  endlessBuffKindBought(void);
bool endlessBuffOnCooldown(void);    // a kill-fire buy is on recharge (a prior buy locked all three); no buy this visit
int  endlessBuffCooldownLeft(void);  // sectors until the kill-fire buys unlock again (0 = ready now)
bool endlessTryBuyTurbodrive(void);        // buy Turbodrive; false if unaffordable / a buff already owned / on recharge
bool endlessTryBuyOverblast(void);   // buy Overblast; false if unaffordable / a buff already owned / on recharge
bool endlessTryBuyOverdrive(void);// buy Overdrive; false if unaffordable / a buff already owned / on recharge
bool endlessTryBuySpecial(void);     // buy a random special weapon; false if unaffordable

// Remaining E-Shop purchases.
Sint64 endlessBombPrice(void);
bool endlessBombFull(void);
bool endlessTryBuyBomb(void);
Sint64 endlessRevivePrice(void);
bool endlessReviveArmed(void);       // a revive token is currently held by the shopping player
bool endlessTryBuyRevive(void);
bool endlessConsumeRevive(uint p);   // spend player p's held revive on death; true = survived (caller clears the bullet field); also arms the grace window below
bool endlessReviveGraceActive(void); // ~3s after a spent revive: every enemy gun is stunned (tyrian2.c enemy-fire + Martyrdom burst)
Sint64 endlessExtraPerkPrice(void);
bool endlessTryBuyExtraPerk(void);   // charges + rolls the offers; the dispatch then opens MENU_PERKS
// Maximum queued Sabotage charges per visit, counted across both players: the charges buy off the
// shared sector, so co-op spends one queue between the two of them.
#define ENDLESS_CLEANSE_MAX_CHARGES 3
Sint64 endlessCleansePrice(void);      // the local player's own escalating price
int  endlessCleanseCharges(void);    // strips queued for the next course select, the pair's, capped
bool endlessCleanseMaxed(void);      // queue is at ENDLESS_CLEANSE_MAX_CHARGES; no further buy will take
bool endlessTryBuyCleanse(void);
Sint64 endlessGamblePrice(void);
bool endlessTryGamble(void);
const char *endlessGambleResult(void);  // last gamble outcome text (for the E-Shop help line)
bool endlessGambleWonPerk(void);        // last gamble handed out a free perk pick (dispatch opens MENU_PERKS)
void endlessClearGamblePerk(void);      // consume that flag once the perk menu has opened, so later E-Shop buys don't re-open it
int  endlessShopTaxPercent(void);       // Loan Shark: permanent +% added to every shop price this run (0 = none)
int  endlessPerkShopCostBp(void);       // Financier perk: what the outpost charges, in basis points (10000 = unchanged); multiplies the depth-scaled percent in JE_getCost
int  endlessGambleOutcomeCount(void);   // number of distinct gamble outcomes (for the debug "Gamble Outcomes" page)
const char *endlessGambleOutcomeName(int id);  // display name of gamble outcome `id`
void endlessForceGambleOutcome(int id); // debug: fire outcome `id`'s effect directly (no fee), for testing
const char *endlessLastGrantedSpecial(void);  // name of the last special granted this shop visit ("" if none)
unsigned endlessPendingMods(void);   // kill-fire buff bits bought this visit, not yet applied (for the debug jump)
// Purchased kill-fire modifiers win conflicts.
Uint64 endlessFoldPurchasedMods(Uint64 sectorMods, Uint64 purchased);

// Initialize fork state after an Endless level is loaded.
void endlessRegenerateLevel(void);

// Reset only effect state for campaign debug mode.
void endlessCampaignLevelStart(void);

// Seed-repeatable per-zone coin: does this zone wake the dormant dispenser bases?
bool endlessDispenserBaseRoll(void);

// Recompute state chosen once per sector, such as gravity heading.
void endlessRefreshModDerivedState(void);

// Campaign-effect debug state in opentyrian.cfg.
void endlessDebugConfigSave(ConfigSection *section);
void endlessDebugConfigLoad(const ConfigSection *section);

// Prevent run purchases from leaking into campaign debug mode.
void endlessCampaignModsArm(void);

// Milestone zones ignore script music changes.
bool endlessMilestoneZone(void);

// 0 ordinary, 1 half-century, 2 century, 3 odd multiple of 25.
int endlessMilestoneKindOfZone(int zone);

// Seeded replacement for level-script spotlight state.
bool endlessLightConeActive(void);

// Preload sprite banks needed before their script event.
void endlessPreloadBanks(void);

Sint64 endlessStartingCash(void);

// Apply fixed starting gear before any purchase can occur.
void endlessApplyStartingLoadout(void);

// Move the starting gun to the first row of the opening shop.
void endlessHoistStartWeapon(void);

// A destroyed ship gets the death menu instead of GAME OVER and the run summary.
bool endlessDeathMenuDue(void);

// True after a fatal Standard or Hardcore death; closes the Quit Level escape.
bool endlessDeathLocksMenu(void);

void endlessOnRunEnd(void);

// Hardcore quits show the same final summary as death.
void endlessEndRunToTitle(void);

// Apply the level-clear payout and return its components.
void endlessApplyLevelPayout(Sint64 *interestOut, Sint64 *bonusOut);

// Replace Endless data cubes and secret orbs with a safe special for player p.
void endlessGrantSpecial(uint p);

// Pickups that hand out a random special. Drives the "?" art (tyrian2.c) and the pickup box
// (mainint.c). Reads enemy state, writes none.
bool endlessSpecialPickup(int slot);

// Top half of the shop's unknown-item icon; its bottom half is a ship body. INK is the opaque
// glyph's measured extent from the enemy reference point, grown by GRAB on each side for the pickup
// box. The outline pass puts one more pixel on each side, so the solid shape is INK grown by 1.
#define ENDLESS_SPECIAL_PICKUP_ICON 125
#define ENDLESS_SPECIAL_GLYPH_INK_X0 2
#define ENDLESS_SPECIAL_GLYPH_INK_X1 9
#define ENDLESS_SPECIAL_GLYPH_INK_Y0 2
#define ENDLESS_SPECIAL_GLYPH_INK_Y1 12
#define ENDLESS_SPECIAL_GLYPH_GRAB   5
#define ENDLESS_SPECIAL_GLYPH_X0    (ENDLESS_SPECIAL_GLYPH_INK_X0 - ENDLESS_SPECIAL_GLYPH_GRAB)
#define ENDLESS_SPECIAL_GLYPH_X1    (ENDLESS_SPECIAL_GLYPH_INK_X1 + ENDLESS_SPECIAL_GLYPH_GRAB)
#define ENDLESS_SPECIAL_GLYPH_Y0    (ENDLESS_SPECIAL_GLYPH_INK_Y0 - ENDLESS_SPECIAL_GLYPH_GRAB)
#define ENDLESS_SPECIAL_GLYPH_Y1    (ENDLESS_SPECIAL_GLYPH_INK_Y1 + ENDLESS_SPECIAL_GLYPH_GRAB)

// Rim shade for the glyph's outline pass, within whichever bank it is cycling through: dark enough
// to read as an outline, tinted enough to belong to the icon. Below the glyph's own darkest pixel
// (shade 3), and above the bank floor, which is flat black in some palettes.
#define ENDLESS_SPECIAL_OUTLINE_SHADE 2

// Replace an embedded data cube with a gem at the enemy slot.
void endlessDropCubeGem(int slot);

// Replace the random-special event drop with a weapon power-up.
JE_word endlessPowerupDropEnemy(void);

// Redirect a maxed-port power-up to the other port, then to a gem.
JE_word endlessResolvePowerupDrop(JE_word eDatI);

// Depth and modifier scaling. Boss HP uses a divisor because armor is byte-sized.
int endlessArmorPercent(void);      // ordinary-enemy HP scale (100 = unchanged); 254 cap applies
int endlessBossHpMult(void);        // boss HP divisor (1 = unchanged); N = N times boss HP
int endlessFireDelayPercent(void);  // enemy shot-cooldown scale (100 = unchanged; lower = fires faster)
int endlessShotSpeedPercent(void);  // enemy projectile-speed scale (100 = unchanged; higher = faster)
int endlessShotDamagePercent(void); // enemy shot-damage scale (100 = unchanged; higher = hits harder)
int endlessContactDamagePercent(void); // scales player damage only

// Debug snapshot of the pure depth/modifier levers.

// The ramp as computed for one (zone, difficulty, mods) triple.
typedef struct {
	int  effDepth;      // endlessEffectiveDepth(): run depth x ramp% x 1.25, the levers' own clock
	int  diffZone;      // endlessDifficultyZone(): the zone the player-facing thresholds see
	int  rampPercent;   // the difficulty tilt itself: 50 (Wimp) .. 160 (Insanity and beyond)
	int  armorPct;      // ordinary-enemy HP, % of stock
	int  bossMult;      // boss HP multiplier
	int  fireDelayPct;  // enemy shot cooldown, % of stock (LOWER = faster fire)
	int  shotSpeedPct;  // enemy projectile speed, % of stock
	int  shotDmgPct;    // enemy shot damage, % of stock
	int  tide;          // the rising-tide coefficient
	int  extraShots;    // extra enemy shots added per firing volley
	int  contactPct;    // contact/ram damage the PLAYER receives, % of stock
	int  elitePct;      // natural elite/champion share, % of eligible enemies
	int  eliteHpMult;   // elite/champion HP multiplier
	int  playerDmgPct;  // YOUR shot damage, % of stock (sector mods + perks)
	int  piercePct;     // your piercing damage, % of the weapon-table value, before playerDmgPct
	int  pierceLock100; // HUNDREDTHS of a sim tick a boss shrugs off repeat piercing hits for, at this zone's boss multiplier
	long eliteBounty;   // cash per elite kill
	long champBounty;   // cash per champion kill
} EndlessScaling;

// Temporarily swaps the three input globals; not reentrant.
void endlessScalingSnapshot(int zone, int difficulty, Uint64 mods, EndlessScaling *out);

// An active override bypasses both depth and modifiers for one lever.
enum {
	ESO_ARMOR,       // endlessArmorPercent
	ESO_BOSSHP,      // endlessBossHpMult
	ESO_FIREDELAY,   // endlessFireDelayPercent
	ESO_SHOTSPEED,   // endlessShotSpeedPercent
	ESO_SHOTDMG,     // endlessShotDamagePercent
	ESO_CONTACT,     // endlessContactDamagePercent
	ESO_TIDE,        // endlessTideLevel
	ESO_EXTRASHOTS,  // endlessExtraEnemyShots (bypasses FLAK SCREEN too)
	ESO_ELITECHANCE, // endlessNaturalEliteChancePercent (Elite Pack / Apex / NOELITE still win)
	ESO_ELITEHP,     // endlessEliteHpMult
	ESO_PLAYERDMG,   // endlessPlayerDamagePercent
	ESO_PIERCEDMG,   // endlessPiercePotencyPercent
	ESO_PIERCELOCK,  // endlessPierceLock100 (pinned = a fixed figure at every tier and multiplier)
	ESO_COUNT
};

typedef struct {
	int  value;
	bool active;
} EndlessScalingOverride;

extern EndlessScalingOverride endlessScalingOverride[ESO_COUNT];

const char *endlessScalingOverrideName(int id);   // short row label
const char *endlessScalingOverrideKey(int id);    // config-file key; ON DISK, never rename one
int         endlessScalingOverrideStock(int id);  // what the lever would read right now UNoverridden
int         endlessScalingOverrideMin(int id);    // sane editing bounds for the debug row
int         endlessScalingOverrideMax(int id);
void        endlessScalingOverridesClear(void);   // drop every pin
int         endlessScalingOverrideCount(void);    // how many are currently pinned (0 = all stock)

// Rising tide adds projectile count after the ordinary intensity ramps flatten.
int endlessTideLevel(void);        // the single tide knob (0 early, then +1 per effective depth)
int endlessExtraEnemyShots(void);  // extra enemy shots to add to each firing volley at this tide
int endlessFanPhaseNow(void);      // tide fan lean; flips every second of zone time

// Player-side + time-based modifier hooks (see endless.c).
int  endlessPlayerDamagePercent(void);  // OVERCHARGE / Overdrive stacks + Heavy Rounds perk: your shot-damage scale (100 = normal)
void endlessGameplayTick(void);         // once per game tick (main player): zone timer + turbodrive/Overdrive decay
bool endlessConsumeArmorHudDirty(void); // true once after the Overheat DoT shaves hull -> the game loop repaints the armor bar
bool endlessTurbodriveActive(void);      // TURBODRIVE kill-streak fire boost currently active?

// Live kill-fire values for the HUD.
int endlessKillBuffTicksLeft(void);    // window ticks remaining (drains ~2s after the last kill)
int endlessKillBuffTicksMax(void);     // full window length, for the timer bar proportion
int endlessKillBuffComboCount(void);   // combo kill count driving the escalation, shown as "xN"
int endlessKillBuffColorBank(void);    // themed palette bank (red Turbodrive / yellow Overdrive / blue Overblast)
int endlessKillBuffFireMultiplier(void);// fire-rate multiplier the buff is granting (1 = none; 2x..10x on the combo ramp; the same schedule for Turbodrive and Overdrive)
int endlessKillBuffDamagePercent(void); // shot-damage bonus % the buff is granting (0 during Turbodrive)
int  endlessKillBuffFireDecrements(void); // extra shotRepeat decrements this tick (the combo ramp; Turbodrive and Overdrive alike)
int  endlessPerkSpecialCooldownDecrements(void); // Rapid Recharge perk: extra cooldown decrements/tick, applied by the caller to the special-weapon gate AND sidekick ammo refill
int   endlessGravityPullX(uint p);      // GRAVITY: ship p's per-tick horizontal nudge (classic non-VT ship path; nonzero only for an omni well)
int   endlessGravityPullY(uint p);      // GRAVITY: ship p's per-tick vertical nudge (classic non-VT ship path)
float endlessGravityDrift(void);        // GRAVITY: pull magnitude in px per 35Hz tick (direction-agnostic)
float endlessGravityDriftX(void);       // GRAVITY: horizontal drag component in px/tick (VT ship path; nonzero only for an omni well)
float endlessGravityDriftY(void);       // GRAVITY: vertical drag component in px/tick (VT ship path)
float endlessMoveScale(void);           // Sluggish input scale; 1.0 is normal
bool  endlessShieldRegenOff(void);      // SHIELDLESS or DEADGEN: true when the shield must not recharge (gate the shield-regen step in tyrian2.c)
bool  endlessShieldRegenFree(void);     // AUXREACTOR: shield regen draws no generator power this sector (tyrian2.c: skip the `power -= shieldT` AND its power>shieldT gate)
unsigned endlessGeneratorPowerAdd(unsigned normalAdd); // DEADGEN: generator charge per tick, throttled to a trickle (else the passed-in normal rate)
int  endlessScrollBoostPercent(void); // 0/70/220 for the active scroll modifier; single source for layers and bound scripted motion
bool endlessScrollBoostActive(void);    // true while any scroll-speed modifier is active (stable across the tick, unlike the fractional step count)
// Call once per layer (channels 0-2) per tick.
int  endlessScrollExtraPx(int channel, int fireStep, int delayMax, int baseThisTick, float *rateOut, float *fracOut);
int  endlessShipTintFilter(void);       // player-ship blit filter: electric yellow while the TURBODRIVE buff is active (0 = none)

// Combat hooks for modifiers that need engine-owned object pools.
int      endlessMartyrdomBurstShots(int linknum, int eliteState); // MARTYRDOM: burst size for this kill; 0 (no burst / off), else 4/6/8 by tier; dedups so a multi-tile enemy bursts once
JE_word  endlessMartyrShotSprite(void);           // MARTYRDOM: the burst's own fixed bullet sprite (never the level's fire, so it always looks the same)
bool     endlessSeekerActive(void);               // SEEKER: a newly-fired enemy shot should arm for one mid-flight course correction
unsigned endlessStaticDischargeDrain(unsigned actualDamage); // STATIC: generator power to bleed for a hit of this size (0 = modifier off / dead generator); caller caps at the current reserve

// Boons queried at collision or death sites.
int  endlessHitboxScale(int area);       // LOW PROFILE: shrink a player hit-area half-extent (returns `area` unchanged when the boon is off)
bool endlessAegisGateConsume(int shieldBefore, int spill); // AEGIS GATE: may this hit be stopped at the shield? `spill` is the damage about to reach armor (trivial spills aren't worth the gate). true ARMS the cooldown, so call once per hit and honour the answer (varz.c JE_playerDamage)
int  endlessEliteContactPercent(int eliteState); // CLEAN SIGNALS: the elite/champion RAM premium (100/125/150, all 100 under the boon), applied by mainint.c on top of endlessContactDamagePercent
bool endlessRamWhileInvulnerable(uint invulnerableTicks); // Endless: an invulnerable ship lands a ram on this tick? (one per ENDLESS_RAM_INVULN_CADENCE ticks; mainint.c)
int  endlessShockwaveRadius(int linknum, int eliteState); // 0, 80 (elite), or 120 (champion)
bool endlessShockwaveActive(void);       // SHOCKWAVE: on? (tyrian2.c clears the whole field when a boss bar empties)

// Bank Star Charts and Breakthrough after a clear.
void endlessOnSectorCleared(void);

// Hostile kill-fire effects share the normal combo machinery.
bool endlessKillFireIsEvil(void);            // is the active kill-fire window an evil curse (not a boon)?
int  endlessKillFireJamTicks(void);          // extra shotRepeat cooldown per shot while an evil curse is up (0 otherwise)
int  endlessKillBuffEvilDamagePenalty(void); // Evil Overdrive: shot-damage REDUCTION % currently applied (0 otherwise), for the HUD
const char *endlessKillFireEvilName(void);   // one-word HUD label for the active curse: JAMMED (Backfire) / BURNOUT / MISFIRE ("" if none)

// Tier rolls are cached by link group.
void endlessResetElites(void);               // clear per-level decisions and rescan the level script (each level start)
int  endlessEliteTierNow(JE_byte linknum, JE_byte armorleft, bool scoreitem);  // this body's tier, settled on its first frame: 1 normal, 2 elite, 3 champion
int  endlessEliteHpMult(void);               // elite & champion HP multiplier (boss-style divisor)
int  endlessEnemyHpMult(bool hasBossBar, int bossHpMult, int eliteState);  // combined per-hit HP divisor

// Repeat-hit delay for piercing bullets, in hundredths of a tick.
#define ENDLESS_PIERCE_LOCK_SCALE 100
int  endlessPierceLock100(bool hasBossBar, int hpMult, int eliteState);

// Piercing damage, carried between ticks in hundredths of a point.
#define ENDLESS_PIERCE_DMG_SCALE 100
int  endlessPiercePotencyPercent(void);      // piercing damage, % of the weapon-table value
// Whole points one piercing bullet lands this tick. `carry100` is that bullet's remainder in
// ENDLESS_PIERCE_DMG_SCALE units, read and advanced here.
int  endlessPierceHitDamage(int rawDamage, int dmgPct, JE_byte *carry100);

long endlessEliteBounty(void);               // extra cash for destroying an elite
long endlessChampionBounty(void);            // extra cash for destroying a champion (more)
// Bounty Hunter also multiplies score pickups; `p` is the ship collecting one (JE_playerCollide).
long endlessScorePickupValue(uint p, long value);
int  endlessChampionFireDelayPercent(void);  // champion extra fire-cooldown scale (lower = faster)
int  endlessChampionShotDamagePercent(void); // champion extra shot-damage scale (higher = harder)
Uint8 endlessEliteTint(int eliteState);      // tier's palette-bank filter, 0 for an ordinary enemy
// Colour an invulnerable part borrows from its link group, 0 when that group holds no tier.
Uint8 endlessEliteShellTint(JE_byte linknum, JE_byte armorleft);

// Call for every logical death so ordinary enemies break the link-group latch.
void endlessAwardEliteKill(int linknum, int eliteState, int killer);

// Run-persistent perks.
extern bool endlessPerkPending;      // a perk pick is queued for the next shop's front gate

void        endlessGeneratePerkChoices(int offers);  // roll this visit's offers (call before the shop)
int         endlessPerkChoiceCount(void);      // how many perks are offered (3; 4 if bought, 5 after a milestone)
const char *endlessPerkChoiceName(int i);      // menu label for offered perk i
const char *endlessPerkChoiceDesc(int i);      // help-line description for offer i
const char *endlessPerkChoiceOwnedText(int i); // ...and its "Owned n/max", drawn flush right of that
void        endlessTakePerk(int i);            // acquire offered perk i (increments its stack); the post-zone pick is free
Sint64      endlessPerkDeclineBonus(void);     // "Take the Cash" buyout: scales with depth, slate width and perks owned
void        endlessDeclinePerk(void);          // take the cash instead of a perk

int endlessPerkArmorBonus(void);     // +max armor from Ablative Plating (added at ship-info, varz.c); may be negative (Glass Cannon)
int endlessPerkFireDecrements(void); // extra shotRepeat decrements/tick from Rapid Cyclers (+ Adrenaline when hurt)
int endlessPerkPreviewFireDecrements(void); // ...the same for the shop weapon preview, minus Adrenaline (the preview shows the full-hull cadence)
int endlessPlayerDamageReduce(void); // flat reduction on each hit taken (Bulwark relic); applied in JE_playerDamage
bool endlessPerkAutoFireSpecial(void); // Autofire Special perk: auto-fire the equipped special while fire is held (varz.c)
int endlessPerkPowerUsePercent(void);  // Efficient Coils perk: generator power-use scale per main-weapon shot (100 = normal, lower = cheaper); applied in shots.c
int endlessPerkShieldWait(int base);   // Shield Matrix perk: shield-regen interval (ticks between +1 shield) reduced from `base`, floored; applied at the shield-regen reset in tyrian2.c
int endlessPerkChargeTicks(int base);  // Rapid Recharge perk: charge-base sidekick charge interval (ticks per +1 charge level) reduced from `base`, floored; applied at the sidekick charge loop in mainint.c
int endlessPerkShotSpeedPercent(void); // High-Velocity Rounds perk: shot travel-speed scale (100 = normal); applied to genuine shot velocities in shots.c player_shot_create
bool endlessPerkRadarActive(void);     // Radar perk: Chart-a-Course help line names each sector's base level (endless_course.c endlessCourseHelp)
int  endlessPerkSurveyorRoutes(void);  // Surveyor perk: extra Chart-a-Course routes this visit (endless_course.c, added after the RNG roll)
int  endlessPerkExecutionerBonus(int damage, int armorleft, int fullHp, bool boss); // Executioner: bonus damage vs a wounded enemy (tyrian2.c shot collision)
void endlessOpeningSalvoTick(void);        // Opening Salvo: advance the main-gun idle timer one tick (endlessGameplayTick)
bool endlessOpeningSalvoConsume(void);     // Opening Salvo: main gun fired -> reset idle, arm the charged-volley flag for the rest of this tick (mainint.c)
bool endlessOpeningSalvoVolleyActive(void);// Opening Salvo: is a consumed salvo window running? (shots.c: power-free + tag every shot; varz.c specials)
int  endlessOpeningSalvoGaugePercent(void); // Opening Salvo: share of the gauge that reads green, 0..100 (tyrian2.c draw_power_gauge)
int  endlessOpeningSalvoScale(int value);  // Opening Salvo: scale a non-damage special magnitude x2.5 while the window runs (varz.c repulsor/heal/invuln)
int  endlessOpeningSalvoDamagePercent(void); // Opening Salvo: +% damage the charged volley deals (tyrian2.c collision)
int  endlessPerkKineticPower(int shieldAbsorbed, int tpwr); // Kinetic Converter: generator power refunded for an absorbed shield hit (varz.c JE_playerDamage)
int  endlessPerkKineticCooldownCut(int remaining);  // Kinetic Converter: ticks a hit takes off a special-recharge clock holding `remaining` (varz.c JE_playerDamage)
int  endlessPerkKineticAmmoRounds(void);            // Kinetic Converter: whole sidekick rounds a hit gives back; stateful, call once per hit (varz.c JE_playerDamage)
int  endlessPerkKineticChargeStages(void);          // Kinetic Converter: charge stages a hit walks a charge sidekick up (varz.c JE_playerDamage)
int  endlessPerkKineticTwiddleCost(int listCost);   // Kinetic Converter: what a twiddle's shield/armor charge deducts, discounted (varz.c JE_doSpecialShot)
void endlessCountermeasureTick(void);        // Countermeasure Suite: advance the burst cooldown one tick (endlessGameplayTick)
int  endlessPerkCountermeasureRadius(void);  // Countermeasure Suite: projectile-clear radius if ready now (0 = not owned / on cooldown); varz.c JE_playerDamage
void endlessCountermeasureFired(void);       // Countermeasure Suite: re-arm the cooldown after a burst (varz.c)
bool endlessPerkChainReactionActive(void);   // Chain Reaction: perk owned (tyrian2.c kill-site pulse queue)
uint endlessPerkChainOwner(int killer);      // Chain Reaction: ship whose stacks a kill's pulse uses (0 outside coop)
int  endlessPerkChainRadius(void);           // Chain Reaction: pulse radius in px, widening with each stack
int  endlessPerkChainDamage(bool salvoBoosted); // Chain Reaction: pulse damage, stacks scaled by the owner's damage
int  endlessPerkAmmoPercent(void);           // Ordnance Reserves: sidekick-magazine bonus % (0 = not applying); the shop label and the flown magazine both derive from this
int  endlessPerkSidekickAmmo(int base);      // Ordnance Reserves: a shipped option.ammo magazine, boosted + capped (0 stays 0: charge sidekicks have no magazine)
int  endlessPerkSidekickRefillTicks(int baseTicks, int stockAmmo); // Ordnance Reserves: the per-round refill interval, scaled so a boosted magazine still fills in the shipped time
int  endlessPerkSpecialDuration(int base, int cap); // Ordnance Reserves: a timed special's duration, stretched; `cap` clamps it for the byte-wide fields (0 = uncapped)
int  endlessPerkFailsafeTicks(void);         // Failsafe: i-frames a hit that reached the hull grants (0 = not owned); varz.c JE_playerDamage
int  endlessPerkGuidanceDelay(uint bay, int ownDelay); // Guidance Package: ticks between course corrections for a shot from `bay` (0 = not steered); shots.c player_shot_create
int  endlessPerkTwinPodOffset(uint p, uint sidekick); // Twin Pods: x offset in px of ship p's twin volley, outboard of the pod; the fire site moves its own volley the same inboard (0 = no twin)
int  endlessPerkProwRamDamage(int damage);   // Reinforced Prow: what a contact tick deals the enemy, from the stock figure (mainint.c)
int  endlessPerkProwContactPercent(void);    // Reinforced Prow: share of contact damage the ship still takes, 100 = all (mainint.c)
int  endlessShipHullGapPx(uint p, unsigned slot); // px from ship p's hull to the nearest live tile of `slot`'s hull; 0 while touching
int  endlessPerkKnifeFightPercent(unsigned slot); // Knife Fight: bonus % for the fx ship's hit on `slot` at the current gap (0 = none)
int  endlessPerkKnifeFightBonus(int damage, int pct); // Knife Fight: that bonus in armor points, from raw damage (tyrian2.c, mainint.c)
void endlessPerkKnifeFightBlood(unsigned slot, int pct);  // Knife Fight: bleed the hull for a raised hit; presentation only, budgeted per frame
int  endlessPerkDeflectDamage(int absorbed); // Deflector: damage of the shot a shield absorption returns, 0 = none (tyrian2.c)

// Perk registry accessors (for the endless debug screen: list / toggle / stack perks).
int         endlessPerkCount(void);          // number of perks (PERK_COUNT)
const char *endlessPerkName(int id);         // perk display name
const char *endlessPerkDesc(int id);         // perk one-line effect description (for the perk-list help)
int         endlessPerkMaxStack(int id);     // max stacks this perk allows
// Perks are personal, so both sides of this pair name THIS machine's own player's stacks.
int         endlessPerkGetOwned(int id);     // current owned stacks
void        endlessPerkSetOwned(int id, int n); // set owned stacks (clamped 0..max)
// ...and this pair names the ship outright, for the debug panel, which edits either one.
int         endlessPerkGetOwnedFor(uint p, int id);
void        endlessPerkSetOwnedFor(uint p, int id, int n);

/* The kill-fire buffs one ship is flying or has bought (ENDLESS_PERSONAL_MOD_MASK). The setter
 * re-derives that ship's live mask, so the debug panel can hand a buff to either ship. */
Uint64 endlessPersonalBuffMods(uint p);
void   endlessSetPersonalBuffMods(uint p, Uint64 bits);

/* Save-codec regression hooks used by the project-owned migration/fuzz suite. */
int  endlessSaveLegacyVersionMax(void);
bool endlessSaveTestFixture(const char *path, char *detail, size_t detailSize);
bool endlessSaveTestTextCodec(char *detail, size_t detailSize);
bool endlessSaveLegacyTestImport(const char *path);   // a whole legacy endless.sav over the slot cache
bool endlessSaveLegacyTestRepair(const char *path);   // the repair pass against a named legacy file
bool endlessSaveTestNewerLegacy(const char *v27Path, char *detail, size_t detailSize);

#endif // ENDLESS_H
