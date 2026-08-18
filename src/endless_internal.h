/*
 * Private interface shared by the endless_*.c files.
 * Define state in its owning source file and expose it here only when necessary.
 */
#ifndef ENDLESS_INTERNAL_H
#define ENDLESS_INTERNAL_H

#include "endless.h"

// Clamp without evaluating the value twice.
static inline int endlessClamp(int v, int lo, int hi)
{
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

// Structural RNG.
extern char   endlessRunSeed[ENDLESS_SEED_MAXLEN];
extern Uint64 endlessEliteRngState;

Uint32 endlessRand(void);
void   endlessReseed(Uint64 salt);
Uint64 endlessSplitMixSeed(Uint64 salt);
Uint32 endlessSplitMixStep(Uint64 *state);   // advance a stream the caller owns
Uint32 endlessEliteRand(void);

// Per-player structural RNG: a reroll or gamble on one machine must not shift the other's draws.
Uint32 endlessRandFor(uint p);
void   endlessReseedPlayers(Uint64 salt);
extern Uint64 endlessPlayerRngState[2];

// Per-zone timers.
#define ENDLESS_TURBODRIVE_TICKS 70
#define ENDLESS_RETALIATION_TICKS 35
extern int endlessZoneTicks;
extern int endlessTurbodriveTimer[2];   // kill-fire window, per player
extern int endlessRetaliationTimer;
// Fractional fire-rate and special-cooldown carries, per ship (endless_perks.c). Rollback state:
// which tick they cross a whole step decides which tick a gun fires.
extern int endlessPerkFireAccum[2];
extern int endlessPerkSpecialCdAccum[2];
// Kinetic Converter's fractional sidekick-round carry, per ship. Rollback state for the same
// reason: which hit crosses a whole round decides when a pod can fire again.
extern int endlessPerkKineticAmmoAccum[2];

// Rewards banked on clear and spent at a later outpost.
extern bool endlessStarChartsOwed;
extern int  endlessBreakthroughOwed;

// Shop playback is zero-based; levelSong is one-based. Keep these paired.
#define ENDLESS_MILESTONE_SHOP_SONG     26
#define ENDLESS_MILESTONE_SHOP_SONG_LVL 27
#define ENDLESS_FINALE_SHOP_SONG        24  // "The final edge"; only the outpost charting the credits zone, pre-credits
#define ENDLESS_FINALE_SHOP_SONG_LVL    25

// Credits roll at the next outpost after this many cleared zones.
#define ENDLESS_CREDITS_ZONE 100

int     endlessMilestoneKindOfZone(int zone);
int     endlessMilestoneKind(void);
JE_byte endlessMilestoneSong(int kind);
bool    endlessPerkDueAtDepth(int depth);
int     endlessPerkOffersAtDepth(int depth);

// Authored level behind each zone.
#define ENDLESS_LEVEL_HISTORY 5

extern JE_byte endlessLastSong;       // track the last-played zone used; 0 = nothing played yet this run
extern int     endlessLastSongDepth;  // the run depth it was picked for; -1 = none

// Current and previous authored level, retained for diagnostics.
extern char endlessBaseName[11];
extern int  endlessBaseEp;
extern int  endlessBaseLvl;
extern char endlessPrevBaseName[11];
extern int  endlessPrevBaseEp;
extern int  endlessPrevBaseLvl;

extern int     endlessRecentEp[ENDLESS_LEVEL_HISTORY];
extern JE_byte endlessRecentSec[ENDLESS_LEVEL_HISTORY];
extern int     endlessRecentCount;

// Random safe level not present in the recent-level ring.
bool endlessRandomSafeLevel(int *epOut, JE_byte *secOut, JE_byte *fileOut);

/* The Shuffle rules' draw: the whole safe pool shuffled into a bag, taken in order, refilled with
 * a fresh shuffle once empty. Uses a stream of its own, so a shuffled chart consumes no structural
 * RNG and cannot shift the draws around it. See "Level shuffle" in doc/notes.md. */
bool endlessShuffleSafeLevel(int position, int *epOut, JE_byte *secOut, JE_byte *fileOut);

extern int endlessShuffleNext;        // pieces the run has drawn; the next hand starts here
extern int endlessShuffleHandStart;   // where the live chart's hand came off, for the re-anchor
extern int endlessShuffleHandDepth;   // ...and the visit it was dealt for; -1 = none dealt yet

// Positions are clamped into this range, which is far past any real run's draw count.
#define ENDLESS_SHUFFLE_POSITION_MAX 1000000000
void endlessShuffleSetNext(int position);
void endlessShuffleSyncHand(uint p, int handStart);

// Shared effect reset. It must not consume structural RNG.
void endlessResetZoneEffects(void);
void endlessResetKillDedup(void);   // clear the multi-part kill dedup guard at zone start

// Per-zone half of the custom-weapon tracking behind endlessRunUsedCustom.
void endlessResetCustomWeaponZone(void);

// Record storage. The per-difficulty pair is indexed [chart rule][crew size][EndlessRunMode]
// [difficulty slot]; readers outside the module go through endlessBestZoneForDifficulty and
// endlessRecordDiffCustomMark.
extern int  endlessBestZoneDiff[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT];
extern bool endlessBestZoneDiffCustom[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT][ENDLESS_DIFFICULTY_COUNT];

// The untagged pair is a mode's record belonging to no difficulty, which is only what a config
// written before the breakdown existed carries in. It keeps the original `best_zone` config keys
// and counts towards endlessBestZoneAny, so those records survive without inventing a difficulty
// for them. Nothing writes it unless a run starts on a difficulty outside the six.
extern int  endlessBestZoneUntagged[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT];
extern bool endlessBestZoneUntaggedCustom[ENDLESS_BASE_TABLES][ENDLESS_PLAYER_TABLES][ENDLESS_RUNMODE_COUNT];

// Combat state.
extern int  endlessComboKills[2];       // +1 per kill while a kill-fire window is up, reset when it lapses
extern char endlessLastSpecialName[2][31]; // name of the last special weapon each player was handed

bool endlessStaticLockoutActive(void);
void endlessStaticLockoutTick(void);
void endlessStaticLockoutReset(void);

int  endlessDifficultyZone(void);
int  endlessNaturalEliteChancePercent(void);

// Course generation unlocks these boons only once their targets can appear.
bool endlessEliteBoonsUnlocked(void);
bool endlessTideBoonsUnlocked(void);

void endlessAegisTick(void);
void endlessAegisReset(void);
void endlessRollGravityDir(void);

void endlessReviveGraceArm(void);
void endlessReviveGraceTick(void);
void endlessReviveGraceReset(void);

// Perk tuning. Values are per stack unless a comment says otherwise.
#define ENDLESS_PERK_DAMAGE_PCT    12  // Heavy Rounds damage (%).
#define ENDLESS_PERK_FIRE_PCT      20  // Rapid Cyclers fire accumulator (%).
#define ENDLESS_PERK_ARMOR_STEP     8  // Ablative Plating maximum armor.
#define ENDLESS_PERK_CASH_PCT      15  // Scavenger cash (%).
#define ENDLESS_PERK_REGEN_TICKS  140  // Nanorepair ticks per armor point at one stack.
#define ENDLESS_PERK_SIPHON_PCT    12  // Siphon heal chance (%).
#define ENDLESS_PERK_BOUNTY_PICKUP_MULT 4
#define ENDLESS_PERK_BULWARK        1

// Adrenaline and Glass Cannon.
#define ENDLESS_PERK_ADRENALINE_PCT 45
#define ENDLESS_PERK_ADRENALINE_DMG 25
#define ENDLESS_PERK_ADRENALINE_HP   3  // Active below one third armor.
#define ENDLESS_PERK_GLASS_DMG      40
#define ENDLESS_PERK_GLASS_ARMOR     8

// Cooldowns, power, shields and shot speed.
#define ENDLESS_PERK_SPECIALCD_PCT  25
#define ENDLESS_PERK_POWERUSE_PCT   15
#define ENDLESS_PERK_POWERUSE_MIN   20  // Minimum power cost (% of stock).
#define ENDLESS_PERK_SHIELDRGN_STEP  3
#define ENDLESS_PERK_SHIELDRGN_MIN   3
#define ENDLESS_PERK_CHARGE_STEP     4
#define ENDLESS_PERK_CHARGE_MIN      4
#define ENDLESS_PERK_SHOTSPEED_PCT  25

// Course choice and Executioner.
#define ENDLESS_PERK_SURVEYOR_ROUTES 1
#define ENDLESS_PERK_EXEC_DMG_PCT   15
#define ENDLESS_PERK_EXEC_HP_PCT    25
#define ENDLESS_PERK_EXEC_BOSS_PCT  15

// Opening Salvo, in simulation ticks.
#define ENDLESS_PERK_SALVO_IDLE     70
#define ENDLESS_PERK_SALVO_DMG_PCT 150
#define ENDLESS_PERK_SALVO_WINDOW   35

// Kinetic Converter.
#define ENDLESS_PERK_KINETIC_PCT         20
#define ENDLESS_PERK_KINETIC_CD_PCT       8
#define ENDLESS_PERK_KINETIC_AMMO_PCT    25  // Hundredths of a round.
#define ENDLESS_PERK_KINETIC_STAGES       1
#define ENDLESS_PERK_KINETIC_TWIDDLE_PCT 22

// Countermeasure Suite.
#define ENDLESS_PERK_CM_RADIUS1    80
#define ENDLESS_PERK_CM_RADIUS2   120
#define ENDLESS_PERK_CM_COOLDOWN   70

// Chain Reaction. Base damage matches the median shipped enemy's armor.
#define ENDLESS_PERK_CHAIN_RADIUS  59
#define ENDLESS_PERK_CHAIN_REACH   16
#define ENDLESS_PERK_CHAIN_DMG     20

// Financier and Ordnance Reserves.
#define ENDLESS_INTEREST_BASE_PCT 10
#define ENDLESS_PERK_INTEREST_PCT  5
#define ENDLESS_PERK_DISCOUNT_BP 825
#define ENDLESS_PERK_AMMO_PCT     30
#define ENDLESS_PERK_AMMO_CAP    250  // Fits the byte-wide item field.
#define ENDLESS_PERK_SPECDUR_PCT  30

// Defensive and guidance perks.
#define ENDLESS_PERK_FAILSAFE_TICKS   9
#define ENDLESS_PERK_GUIDANCE_DELAY   6
#define ENDLESS_PERK_GUIDANCE_STEP    2
#define ENDLESS_PERK_GUIDANCE_TIGHTEN 4
#define ENDLESS_PERK_GUIDANCE_SIDEKICK_STACKS 2
#define ENDLESS_PERK_GUIDANCE_SPECIAL_STACKS  3

// Close-range and sidekick perks.
#define ENDLESS_PERK_TWINPODS_SPREAD_PX 12
#define ENDLESS_PERK_PROW_DMG_PCT      100
#define ENDLESS_PERK_PROW_TAKEN_PCT     25
#define ENDLESS_PERK_KNIFE_PCT          15
#define ENDLESS_PERK_KNIFE_FULL_PX       7
#define ENDLESS_PERK_KNIFE_FADE_PX      48
#define ENDLESS_PERK_DEFLECT_MULT2      200

// Offer-array width is fixed by the widest persisted slate.
#define ENDLESS_PERK_OFFERS           3
#define ENDLESS_PERK_OFFERS_BOUGHT    4
#define ENDLESS_PERK_OFFERS_MILESTONE 5

// Extra Perk price ladder. STEP and GROWTH use the run purchase count; REPEAT and VISIT_MAX
// govern repeat purchases at one outpost. See doc/notes.md#economy-and-perks.
#define ENDLESS_PERK_PAID_STEP_PCT     20
#define ENDLESS_PERK_PAID_GROWTH_PCT    5
#define ENDLESS_PERK_VISIT_REPEAT_PCT 250
#define ENDLESS_PERK_VISIT_MAX          2
// Far past any reachable count, and low enough that the quadratic cannot overflow a loaded price.
#define ENDLESS_PERK_PAID_MAX        1000

// Perk decline payout.
#define ENDLESS_PERK_DECLINE_MULT      25
#define ENDLESS_PERK_DECLINE_OWNED_PCT  6
#define ENDLESS_PERK_DECLINE_OWNED_CAP 150

// Save v14 removed Rapid Charger and migrates every persisted ID after it.
enum {
	PERK_DAMAGE, PERK_FIRERATE, PERK_ARMOR, PERK_CASH,
	PERK_REGEN, PERK_SIPHON, PERK_BOUNTY,
	PERK_BULWARK, PERK_ADRENALINE, PERK_GLASSCANNON,  // relic-like
	PERK_SPECIALCD,
	PERK_AUTOSPECIAL,
	PERK_POWERUSE,
	PERK_SHIELDREGEN,
	PERK_SHOTSPEED,
	PERK_RADAR,
	PERK_SURVEYOR,        // persisted IDs: append only
	PERK_EXECUTIONER,
	PERK_SALVO,
	PERK_KINETIC,
	PERK_COUNTERMEASURE,
	PERK_CHAINRXN,
	PERK_FINANCIER,
	PERK_ORDNANCE,
	PERK_FAILSAFE,
	PERK_GUIDANCE,
	PERK_TWINPODS,
	PERK_PROW,
	PERK_KNIFE,
	PERK_DEFLECTOR,
	PERK_COUNT
};

typedef struct {
	const char *name;      // menu label (<= 23 chars, menuInt width)
	const char *desc;      // help-line description
	JE_byte     maxStack;  // how many times it can be taken
} EndlessPerk;

extern const EndlessPerk endlessPerkTable[PERK_COUNT];
/* Perks are personal: a stack affects only the ship that picked it. endlessPerkTakenBy is the
 * storage each machine owns a row of; endlessPerkEffective(p, id) is what effects read (through
 * the fx-ship context or an explicit seat), and endlessPerkOwned keeps the capped combined view
 * for diagnostics only. Route writes through endlessPerkGrant / endlessPerkRederive. */
extern JE_byte endlessPerkOwned[PERK_COUNT];
extern JE_byte endlessPerkTakenBy[2][PERK_COUNT];
JE_byte endlessPerkEffective(uint p, int id);
void endlessPerkRederive(void);
void endlessPerkGrant(uint p, int id, int delta);
/* The offered slate, the pending gate and the resolved depth all describe THIS machine's player:
 * both sides roll their own slate from their own stream at the same outpost. */
extern int endlessPerkChoice[ENDLESS_PERK_OFFERS_MILESTONE];
extern int endlessPerkChoiceN;
extern int endlessRegenTick;
extern int endlessSalvoIdle[2];
extern int endlessSalvoWindow[2];
extern int endlessCmCooldown[2];

void endlessResetZonePerkTimers(void);
extern int endlessPerkDepthDone;

int endlessPerkCashPercent(void);             // Scavenger cash multiplier (100 = unchanged)
int endlessPerkInterestPercent(void);         // bank-interest rate, % of unspent cash (10 = stock)
int endlessPerkTotalOwned(void);              // perk stacks held, summed across every perk
bool endlessAdrenalineActive(void);           // Adrenaline owned and armor below its hurt threshold

/* Outpost state. Everything indexed [2] is one player's own; solo runs use slot 0 alone. Each
 * machine owns its local player's slot and mirrors the peer's from the shop packet. */
extern Sint64 endlessRerollCost[2];   // escalating outpost prices, reset each visit
extern Sint64 endlessHullCost[2];
extern Sint64 endlessShopEntryCash[2];  // cash on entering the shop; the E-Shop cash-fraction buys price off this

// Purchased kill-fire modifiers are folded in after course selection; both players' are.
extern unsigned endlessPurchasedMods[2];
extern int endlessBuffKind[2];           // which buff was bought: 0 none, 1 Turbodrive, 2 Overdrive
extern int endlessOverdriveStacks[2];    // +1 per kill while the window is up (capped), reset when it lapses
extern int endlessBuffCooldownUntil[2];  // run depth at which the kill-fire buys unlock again (0 = no lock)
extern int endlessBuffCharge[2];         // cash-paid tier that scales the window/damage (0..20)

int endlessBuffWindowTicks(void);     // base kill-fire window for the ship being computed
int endlessBuffWindowTicksFor(uint p);// ...and for one named ship, for the kill loop
int endlessBuffChargePaid(void);      // the current ship's own charge, which also scales its damage

extern bool endlessReviveHeld[2];          // a held revive token survives one lethal hit
extern int  endlessRevivesUsed[2];         // revives spent this run (the price doubles per use)
extern int  endlessCleanseChargeCount[2];  // pre-bought strips of the worst mutator off the next course
extern Sint64 endlessBombCost[2], endlessCleanseCost[2];
extern int  endlessExtraPerksBought[2];    // perks bought from outposts this run; sets the permanent surcharge
extern int  endlessExtraPerksVisit[2];     // ...and how many of them at this outpost, capped at ENDLESS_PERK_VISIT_MAX
extern char endlessGambleMsg[2][48];       // last gamble outcome, for the E-Shop help line
extern bool endlessGamblePerkWon[2];       // a gamble handed out a free perk pick; the dispatch opens MENU_PERKS
extern int  endlessShopTax[2];             // Loan Shark: permanent +% on every shop price for the rest of the run
extern bool endlessGambleRigged[2];        // Rigged: the NEXT gamble secretly rolls twice and keeps the worse result
extern int  endlessLongCon[2];             // The Long Con: sectors until a paid-and-forgotten APEX ambush comes due
extern bool endlessResumeVisit;            // a save was just loaded: the next outpost restores its snapshot
extern bool endlessCreditsShown;           // the zone-100 credits roll already played this run (rides the save)

Sint64 endlessClearBase(void);              // the depth-scaled unit every endless payout is built from
Sint64 endlessClearBonusFor(Uint64 mods);   // clear payout for an ARBITRARY modifier set at the current depth
Sint64 endlessClearBonusForEx(Uint64 mods, int payoutMille);
int    endlessSortiePayoutMille(void);
Uint64 endlessStripWorstMod(Uint64 mods);   // strip the single most-dangerous hostile bit (one per cleanse charge)

// Modifier registry.
typedef struct {
	Uint64      bit;
	short       reward;    // clear-cash reward in TENTHS of the base (10 = 1.0x base; may be < 0)
	const char *word;      // short phrase for the generated help line
} EndlessMod;

// Theme rows supply names only; modifier tables drive behaviour and payout.
typedef struct { Uint64 mods; const char *name; } EndlessTheme;

// Counts are part of the cross-file array type and must match each definition.
extern const EndlessMod   endlessModTable[50];
extern const EndlessTheme endlessHostileThemes[256];
extern const EndlessTheme endlessKamikazeThemes[12];
extern const EndlessTheme endlessHomingThemes[8];
extern const EndlessTheme endlessBoonThemes[122];
extern const EndlessTheme endlessOverloadThemes[20];
extern const EndlessTheme endlessRareThemes[44];
extern const EndlessTheme endlessEvilThemes[30];
extern const EndlessTheme endlessRedlineThemes[2];
extern const EndlessTheme endlessSluggishThemes[5];
extern const EndlessTheme endlessDeadgenThemes[5];
extern const EndlessTheme endlessMartyrdomThemes[5];
extern const EndlessTheme endlessSeekerThemes[5];
extern const EndlessTheme endlessBreakthroughThemes[5];

// Bits included in combat danger and hostile naming.
#define ENDLESS_HOSTILE_MASK ( \
	ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING | \
	ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION | ENDLESS_MOD_ENRAGE | \
	ENDLESS_MOD_KAMIKAZE | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_OVERLOAD | \
	ENDLESS_MOD_BACKFIRE | ENDLESS_MOD_BURNOUT | ENDLESS_MOD_MISFIRE | ENDLESS_MOD_OVERHEAT | \
	ENDLESS_MOD_HOMING | ENDLESS_MOD_RAMPAGE | ENDLESS_MOD_TOPSY | ENDLESS_MOD_SLUGGISH | \
	ENDLESS_MOD_SHIELDLESS | ENDLESS_MOD_DEADGEN | ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_WARP | \
	ENDLESS_MOD_MARTYRDOM | ENDLESS_MOD_SEEKER | ENDLESS_MOD_STATIC | ENDLESS_MOD_RETALIATION | \
	ENDLESS_MOD_THEEND )

// Every bit that quickens the level scroll. Overclock and Overload quicken enemy fire as well, so
// the whole group is excluded where faster scrolling is unwanted.
#define ENDLESS_SCROLL_PACE_MASK ( \
	ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_OVERLOAD | ENDLESS_MOD_WARP )

// Boon bits; a course with both masks is a gambit.
#define ENDLESS_BOON_MASK ( \
	ENDLESS_MOD_FRAGILE | ENDLESS_MOD_BOUNTY | ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERCHARGE | \
	ENDLESS_MOD_DILATION | ENDLESS_MOD_FAVOR | ENDLESS_MOD_OVERDRIVE | \
	ENDLESS_MOD_OVERBLAST | ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_NOELITE | \
	ENDLESS_MOD_AEGIS | ENDLESS_MOD_FLAKSCREEN | ENDLESS_MOD_AUXREACTOR | ENDLESS_MOD_LOWPROFILE | \
	ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_SHOCKWAVE | ENDLESS_MOD_STARCHARTS | \
	ENDLESS_MOD_BREAKTHROUGH | ENDLESS_MOD_SOFTLANDING | ENDLESS_MOD_CLEANSIGNALS )

Uint64      endlessMakeTheEndMods(void);   // "The End"; the sector every GRAND milestone deals
// Drop special-enemy bits that a stronger one in the same set already covers. See endless_mods.c.
Uint64      endlessCanonicalMods(Uint64 mods);
Uint64      endlessPickThemeMods(const EndlessTheme *tbl, unsigned count, Uint64 must, Uint64 forbid);
const char *endlessComboNameSalted(Uint64 mods, unsigned salt);  // salt steps a GENERATED pick to the next word
// Test the hand-maintained modifier/name registries whose invariants are otherwise only comments.
bool        endlessValidateModifierTables(char *detail, size_t detailSize);
int         endlessSynergyBonus(Uint64 mods);
int         endlessDangerScore(Uint64 mods);      // net danger: summed hostile reward, minus boon credits, plus synergy
const char *endlessDangerTier(Uint64 mods);       // tier word shown before a course's description
int         endlessDangerRankLevel(Uint64 mods);  // 0 (F) .. 10 (END)
const char *endlessDangerRank(Uint64 mods);       // the letter grade for that level
// ...the same four, but with a shipped-level baseDanger nudge folded in (endless_levelprofile.h).
// The plain versions above are these with baseDanger 0.
int         endlessDangerScoreEx(Uint64 mods, int baseDanger);
const char *endlessDangerTierEx(Uint64 mods, int baseDanger);
int         endlessDangerRankLevelEx(Uint64 mods, int baseDanger);
const char *endlessDangerRankEx(Uint64 mods, int baseDanger);

// Generated level-specific danger and payout adjustments.
typedef struct {
	JE_byte     ep;               // episode number 1..5
	JE_byte     file;             // lvlFileNum (endlessCourseFile / forcedLvlFileNum)
	JE_byte     lengthClass;      // 0 short, 1 normal, 2 long
	JE_shortint baseDanger[11];   // COARSE grade/tier/sort nudge per difficulty (DIFFICULTY_WIMP..DIFFICULTY_10)
	Sint16      payoutMille[11];  // FINE payout term per difficulty, in thousandths of the base clear reward
} EndlessLevelProfile;

int endlessLevelBaseDanger(int ep, int file, int difficulty);   // coarse grade nudge (-2..+5); 0 if level unknown
int endlessLevelPayoutMille(int ep, int file, int difficulty);  // fine payout term (thousandths of base); 0 if unknown
int endlessLevelLengthClass(int ep, int file);                  // 0/1/2; 1 (normal) if level unknown

// Current course slate.
#define ENDLESS_MAX_COURSES 5

extern int      endlessCourseCnt;
extern int      endlessCourseEp[ENDLESS_MAX_COURSES];
extern JE_byte  endlessCourseSec[ENDLESS_MAX_COURSES];
extern JE_byte  endlessCourseFile[ENDLESS_MAX_COURSES];  // each course's specific lvlFileNum (see forcedLvlFileNum)
extern Uint64   endlessCourseMod[ENDLESS_MAX_COURSES];
extern int      endlessLastEp;
extern JE_byte  endlessLastSec;
extern bool     endlessForced;   // this visit is a forced "Ambush" (single dangerous sector)

// Radar's chart reroll: how many this visit's chart has had, and the inputs a redeal replays.
#define ENDLESS_CHART_REROLLS 1   // rerolls the perk grants per outpost
extern JE_byte endlessChartRerolls;
extern bool    endlessChartStarCharts;
extern uint    endlessChartSeat;

Uint64 endlessZonePhaseSalt(Uint64 phase);  // depth-keyed phase salt, shifted by the reroll count
void   endlessChartVisit(void);             // fresh outpost: latch the redeal inputs and chart
void   endlessChartRedeal(void);            // chart again at the current reroll count
void   endlessChartSyncRerolls(uint p, JE_byte rerolls);  // adopt a peer's reroll (shop packet)

// Resolve a saved/chosen (episode, section) back to a real endless-safe level file.
bool endlessResolveCourseFile(int ep, JE_byte sec, JE_byte requestedFile, JE_byte *resolvedFile);

// Cache authored names after generation or restore.
void endlessNameCourseBaseLevels(void);

// Quit Level launch snapshot.
extern bool     endlessSortieHave;         // a launch-time snapshot exists
extern unsigned endlessSortiePrePurchased[2]; // one-shots snapshotted pre-consumption at the course pick,
extern int      endlessSortiePreCleanse[2];   // so a non-hardcore bail can restore them
extern int      endlessSortiePreLongCon[2];
extern Uint64   endlessSortieOutpostMods;  // mutators in force at the outpost this sortie launched from
extern JE_byte  endlessSortieOutpostEp;    // episode whose item data that outpost was stocked against

#endif // ENDLESS_INTERNAL_H
