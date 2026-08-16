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

#define ENDLESS_CREDITS_ZONE 100  // zones cleared before the run rolls the credits (once per run, at the outpost that follows)

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

int  endlessDifficultyZone(void);              // the current zone as the difficulty ramp sees it
int  endlessNaturalEliteChancePercent(void);   // the depth-driven SPECIAL-enemy share, before mutators
bool endlessEliteBoonsUnlocked(void);          // are NOCHAMP / NOELITE / GIANTKILLER / CLEANSIGNALS eligible to be charted yet?
bool endlessTideBoonsUnlocked(void);           // is FLAKSCREEN worth charting yet (i.e. is the tide adding shots at all)?

void endlessAegisTick(void);
void endlessAegisReset(void);
void endlessRollGravityDir(void);

void endlessReviveGraceArm(void);
void endlessReviveGraceTick(void);
void endlessReviveGraceReset(void);

// Perk tuning.
#define ENDLESS_PERK_DAMAGE_PCT    12  // +% shot damage per Heavy Rounds stack
#define ENDLESS_PERK_FIRE_PCT      20  // fire-decrement accumulator % per Rapid Cyclers stack
#define ENDLESS_PERK_ARMOR_STEP     8  // +max armor per Ablative Plating stack
#define ENDLESS_PERK_CASH_PCT      15  // +% cash (clears + bounties) per Scavenger stack
#define ENDLESS_PERK_REGEN_TICKS  140  // ticks per +1 armor at 1 Nanorepair stack (faster with more)
#define ENDLESS_PERK_SIPHON_PCT    12  // heal-on-kill chance % per Siphon stack
#define ENDLESS_PERK_BOUNTY_PICKUP_MULT 4 // Bounty Hunter: score-pickup cash multiplier
#define ENDLESS_PERK_BULWARK        1  // incoming damage reduced by this per Bulwark stack (min 1 dmg kept)
#define ENDLESS_PERK_ADRENALINE_PCT 45 // extra fire-accumulator % per Adrenaline stack while badly hurt
#define ENDLESS_PERK_ADRENALINE_DMG 25 // +% shot damage per Adrenaline stack while badly hurt
#define ENDLESS_PERK_ADRENALINE_HP  3  // Adrenaline triggers when armor < 1/this of max
#define ENDLESS_PERK_GLASS_DMG     40  // Glass Cannon: +% shot damage
#define ENDLESS_PERK_GLASS_ARMOR    8  // Glass Cannon: -max armor (the drawback)
#define ENDLESS_PERK_SPECIALCD_PCT 25  // extra special-cooldown-decrement accumulator % per stack
#define ENDLESS_PERK_POWERUSE_PCT  15  // -% generator power drawn per main-weapon shot, per Efficient Coils stack
#define ENDLESS_PERK_POWERUSE_MIN  20  // ...but firing never costs less than this % of stock power
#define ENDLESS_PERK_SHIELDRGN_STEP 3  // shield-regen interval cut by this many ticks per Shield Matrix stack (base 15)
#define ENDLESS_PERK_SHIELDRGN_MIN  3  // ...but never quicker than +1 shield per this many ticks (floor)
#define ENDLESS_PERK_CHARGE_STEP    4  // ticks cut from the charge-sidekick charge interval per Rapid Recharge stack (base 20)
#define ENDLESS_PERK_CHARGE_MIN     4  // ...but a charge level never builds quicker than this many ticks (floor)
#define ENDLESS_PERK_SHOTSPEED_PCT 25  // +% shot travel speed per High-Velocity Rounds stack
#define ENDLESS_PERK_SURVEYOR_ROUTES 1 // +Chart-a-Course routes per Surveyor stack (capped at ENDLESS_MAX_COURSES)
#define ENDLESS_PERK_EXEC_DMG_PCT  15  // +% shot damage per Executioner stack, vs a wounded target
#define ENDLESS_PERK_EXEC_HP_PCT   25  // Executioner "wounded" threshold: target below this % of full HP
#define ENDLESS_PERK_EXEC_BOSS_PCT 15  // ...a tighter threshold for boss-bar enemies (harder to execute)
#define ENDLESS_PERK_SALVO_IDLE    70  // ticks the main gun must sit idle to charge an Opening Salvo (2s at the 35Hz sim tick)
#define ENDLESS_PERK_SALVO_DMG_PCT 150 // percentage points; also scales special effects
#define ENDLESS_PERK_SALVO_WINDOW  35  // ticks a consumed salvo lasts (~1s at the 35Hz sim tick), trigger held or not
#define ENDLESS_PERK_KINETIC_PCT   20  // Kinetic Converter: % of an absorbed shield hit's generator-cost refunded as power, per stack
#define ENDLESS_PERK_KINETIC_CD_PCT 8  // ...and % of the remaining special recharge that a hit takes off, per stack
#define ENDLESS_PERK_KINETIC_AMMO_PCT 25 // ...and hundredths of a sidekick round a hit gives back, per stack (carried in an accumulator)
#define ENDLESS_PERK_KINETIC_STAGES 1  // ...and charge stages a hit walks a charge sidekick up, per stack
#define ENDLESS_PERK_KINETIC_TWIDDLE_PCT 22 // ...and % off a twiddle's shield or armor charge, per stack
#define ENDLESS_PERK_CM_RADIUS1    80  // Countermeasure Suite: projectile-clear radius (px) at 1 stack
#define ENDLESS_PERK_CM_RADIUS2   120  // ...widened radius at 2 stacks
#define ENDLESS_PERK_CM_COOLDOWN   70  // ...ticks between countermeasure bursts (~2s at 35Hz)
#define ENDLESS_PERK_CHAIN_RADIUS  44  // Chain Reaction: pulse radius (px) around a destroyed enemy at one stack
#define ENDLESS_PERK_CHAIN_REACH   12  // Chain Reaction: px added to that radius by each stack past the first
// Chain Reaction: armor damage per stack, before the owner's damage scale. 20 is both the median
// armor of the shipped enemies and the commonest single value among them, so one stack kills that
// enemy on an unscaled hit; depth raises what it is worth against, a damage build raises the hit.
#define ENDLESS_PERK_CHAIN_DMG     20
#define ENDLESS_INTEREST_BASE_PCT  10  // stock bank interest: % of unspent cash paid on each level clear
#define ENDLESS_PERK_INTEREST_PCT   5  // ...+this many points per Financier stack (the cap scales with the rate)
#define ENDLESS_PERK_DISCOUNT_BP  825  // basis points; 4 stacks = 33% off
#define ENDLESS_PERK_AMMO_PCT      30  // Ordnance Reserves: +% sidekick magazine per stack (always at least +1 round)
#define ENDLESS_PERK_AMMO_CAP     250  // ...magazine ceiling, so the shop label and the byte-wide item field stay in range
#define ENDLESS_PERK_SPECDUR_PCT   30  // ...and +% duration per stack on the timed special weapons
#define ENDLESS_PERK_FAILSAFE_TICKS  9 // Failsafe: i-frames granted per stack by a hit that reaches the hull (~0.25s at the 35Hz sim tick, so ~0.5s at 2 stacks)

// Offer-array width is fixed by the widest persisted slate.
#define ENDLESS_PERK_OFFERS           3
#define ENDLESS_PERK_OFFERS_BOUGHT    4
#define ENDLESS_PERK_OFFERS_MILESTONE 5

// Extra-perk surcharge by total owned stacks.
#define ENDLESS_EXTRA_PERK_OWNED_PCT  40
#define ENDLESS_EXTRA_PERK_OWNED_CAP 1000

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
extern long endlessRerollCost[2];     // escalating outpost prices, reset each visit
extern int  endlessHullCost[2];
extern long endlessShopEntryCash[2];  // cash on entering the shop; the E-Shop cash-fraction buys price off this

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
extern long endlessBombCost[2], endlessExtraPerkCost[2], endlessCleanseCost[2];
extern char endlessGambleMsg[2][48];       // last gamble outcome, for the E-Shop help line
extern bool endlessGamblePerkWon[2];       // a gamble handed out a free perk pick; the dispatch opens MENU_PERKS
extern int  endlessShopTax[2];             // Loan Shark: permanent +% on every shop price for the rest of the run
extern bool endlessGambleRigged[2];        // Rigged: the NEXT gamble secretly rolls twice and keeps the worse result
extern int  endlessLongCon[2];             // The Long Con: sectors until a paid-and-forgotten APEX ambush comes due
extern bool endlessResumeVisit;            // a save was just loaded: the next outpost restores its snapshot
extern bool endlessCreditsShown;           // the zone-100 credits roll already played this run (rides the save)

long   endlessClearBase(void);              // the depth-scaled unit every endless payout is built from
long   endlessClearBonusFor(Uint64 mods);   // clear payout for an ARBITRARY modifier set at the current depth
long   endlessClearBonusForEx(Uint64 mods, int payoutMille);
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
extern const EndlessTheme endlessMartyrdomThemes[5];  // MARTYRDOM: rare-injected death-burst sectors (its own pool)
extern const EndlessTheme endlessSeekerThemes[5];     // SEEKER: rare-injected course-correcting-shot sectors (its own pool)
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
