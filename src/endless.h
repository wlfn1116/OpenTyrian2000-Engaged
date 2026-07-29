/*
 * Public Endless mode interface.
 * Implementation details are private to endless.c and endless_*.c.
 */
#ifndef ENDLESS_H
#define ENDLESS_H

#include "config.h"
#include "opentyr.h"

#include <stdbool.h>

// Combat effects can be enabled in a campaign without enabling Endless run flow.
static inline bool endlessFxActive(void) { return endlessMode || endlessCampaignMods; }

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

// Count one logical enemy. All kill paths go through enemy_logical_death.
void endlessCountKill(int linknum);

extern Uint64 endlessActiveMods;

// Run-persistent Reinforce bonus.
extern int endlessArmorBonus;

void endlessResetRun(void);

// All-time record. Stored in opentyrian.cfg so Hardcore runs can update it.
extern int endlessBestZone;
void endlessNoteZoneReached(int zone);
void endlessRecordRunStart(void);
int  endlessBestZoneAtStart(void);

void endlessRecordConfigSave(ConfigSection *section);
void endlessRecordConfigLoad(const ConfigSection *section);

// Run seed. Structural randomness is deterministic; combat randomness is not.
#define ENDLESS_SEED_MAXLEN 24

// Hardcore disables all run saves.
extern bool endlessHardcore;

// Returns false when the seed screen is cancelled.
bool endlessSeedSelect(char *outSeed, size_t outN, bool *outHardcore);

void endlessSetSeed(const char *s);
const char *endlessSeedString(void);

// Sidecar save keyed by the normal save slot.
void endlessSaveSlot(JE_byte slot);
bool endlessLoadSlot(JE_byte slot);
bool endlessResumePending(void);

// Quit Level restores the launch snapshot. Hardcore keeps the sortie locked.
extern bool endlessQuitToOutpost;
extern bool endlessLockedSortie;

void endlessCaptureSortie(void);
void endlessRestoreSortie(void);
bool endlessSortieValid(void);
void endlessArmLockedRelaunch(void);

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
long        endlessCoursePayout(int i);

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
long endlessRerollPrice(void);    // current reroll cost (for the menu label)
int  endlessHullPrice(void);      // current hull-upgrade cost (for the menu label)
bool endlessHullMaxed(void);      // true once the run's armor bonus is capped
bool endlessTryReroll(void);      // buy a shop reroll; false if unaffordable
bool endlessTryReinforce(void);   // buy a +armor hull upgrade; false if unaffordable/maxed

// Cash-fraction purchases use the balance recorded on shop entry.
long endlessTurbodrivePrice(void);         // Turbodrive cost (66% of entry cash), for the label
long endlessOverblastPrice(void);    // Overblast cost (75% of entry cash), for the label
long endlessOverdrivePrice(void);   // Overdrive cost (95% of entry cash), for the label
long endlessSpecialPrice(void);      // Buy-Special cost (80% of entry cash), for the label
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
long endlessBombPrice(void);
bool endlessBombFull(void);
bool endlessTryBuyBomb(void);
long endlessRevivePrice(void);
bool endlessReviveArmed(void);       // a revive token is currently held
bool endlessTryBuyRevive(void);
bool endlessConsumeRevive(void);     // spend a held revive on death; true = survived (caller clears the bullet field) -- also arms the grace window below
bool endlessReviveGraceActive(void); // ~3s after a spent revive: every enemy gun is stunned (tyrian2.c enemy-fire + Martyrdom burst)
long endlessExtraPerkPrice(void);
bool endlessTryBuyExtraPerk(void);   // charges + rolls the offers; the dispatch then opens MENU_PERKS
// Maximum queued Sabotage charges per visit.
#define ENDLESS_CLEANSE_MAX_CHARGES 3
long endlessCleansePrice(void);
int  endlessCleanseCharges(void);    // sabotage strips queued for the next course select
bool endlessCleanseMaxed(void);      // queue is at ENDLESS_CLEANSE_MAX_CHARGES -- no further buy will take
bool endlessTryBuyCleanse(void);
long endlessGamblePrice(void);
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

long endlessStartingCash(void);

// Apply fixed starting gear before any purchase can occur.
void endlessApplyStartingLoadout(void);

// Move the starting gun to the first row of the opening shop.
void endlessHoistStartWeapon(void);

void endlessOnRunEnd(void);

// Hardcore quits show the same final summary as death.
void endlessEndRunToTitle(void);

// Apply the level-clear payout and return its components.
void endlessApplyLevelPayout(long *interestOut, long *bonusOut);

// Replace Endless data cubes and secret orbs with a safe special.
void endlessGrantSpecial(void);

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
	ESO_PIERCELOCK,  // endlessPierceLock100 (pinned = a fixed figure at every tier and multiplier)
	ESO_COUNT
};

typedef struct {
	int  value;
	bool active;
} EndlessScalingOverride;

extern EndlessScalingOverride endlessScalingOverride[ESO_COUNT];

const char *endlessScalingOverrideName(int id);   // short row label
const char *endlessScalingOverrideKey(int id);    // config-file key -- ON DISK, never rename one
int         endlessScalingOverrideStock(int id);  // what the lever would read right now UNoverridden
int         endlessScalingOverrideMin(int id);    // sane editing bounds for the debug row
int         endlessScalingOverrideMax(int id);
void        endlessScalingOverridesClear(void);   // drop every pin
int         endlessScalingOverrideCount(void);    // how many are currently pinned (0 = all stock)

// Rising tide adds projectile count after the ordinary intensity ramps flatten.
int endlessTideLevel(void);        // the single tide knob (0 early, then +1 per effective depth)
int endlessExtraEnemyShots(void);  // extra enemy shots to add to each firing volley at this tide

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
int endlessKillBuffFireMultiplier(void);// fire-rate multiplier the buff is granting (1 = none; 2x..10x on the combo ramp -- the same schedule for Turbodrive and Overdrive)
int endlessKillBuffDamagePercent(void); // shot-damage bonus % the buff is granting (0 during Turbodrive)
int  endlessKillBuffFireDecrements(void); // extra shotRepeat decrements this tick (the combo ramp; Turbodrive and Overdrive alike)
int  endlessPerkSpecialCooldownDecrements(void); // Rapid Recharge perk: extra cooldown decrements/tick, applied by the caller to the special-weapon gate AND sidekick ammo refill
int   endlessGravityPullX(void);        // GRAVITY: per-tick horizontal nudge (classic non-VT ship path; nonzero only for an omni well)
int   endlessGravityPullY(void);        // GRAVITY: per-tick vertical nudge (classic non-VT ship path)
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
int      endlessMartyrdomBurstShots(int linknum, int eliteState); // MARTYRDOM: burst size for this kill -- 0 (no burst / off), else 4/6/8 by tier; dedups so a multi-tile enemy bursts once
JE_word  endlessMartyrShotSprite(void);           // MARTYRDOM: the burst's own fixed bullet sprite (never the level's fire, so it always looks the same)
bool     endlessSeekerActive(void);               // SEEKER: a newly-fired enemy shot should arm for one mid-flight course correction
unsigned endlessStaticDischargeDrain(unsigned actualDamage); // STATIC: generator power to bleed for a hit of this size (0 = modifier off / dead generator); caller caps at the current reserve

// Boons queried at collision or death sites.
int  endlessHitboxScale(int area);       // LOW PROFILE: shrink a player hit-area half-extent (returns `area` unchanged when the boon is off)
bool endlessAegisGateConsume(int shieldBefore, int spill); // AEGIS GATE: may this hit be stopped at the shield? `spill` is the damage about to reach armor (trivial spills aren't worth the gate). true ARMS the cooldown, so call once per hit and honour the answer (varz.c JE_playerDamage)
int  endlessEliteContactPercent(int eliteState); // CLEAN SIGNALS: the elite/champion RAM premium (100/125/150, all 100 under the boon), applied by mainint.c on top of endlessContactDamagePercent
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
void endlessResetElites(void);               // clear per-level decisions (each level start)
int  endlessRollEliteTier(JE_byte linknum);  // spawn tier: 1 normal, 2 elite, 3 champion (per linkgroup)
int  endlessEliteHpMult(void);               // elite & champion HP multiplier (boss-style divisor)
int  endlessEnemyHpMult(bool hasBossBar, int bossHpMult, int eliteState);  // combined per-hit HP divisor

// Repeat-hit delay for piercing bullets, in hundredths of a tick.
#define ENDLESS_PIERCE_LOCK_SCALE 100
int  endlessPierceLock100(bool hasBossBar, int hpMult, int eliteState);
long endlessEliteBounty(void);               // extra cash for destroying an elite
long endlessChampionBounty(void);            // extra cash for destroying a champion (more)
int  endlessChampionFireDelayPercent(void);  // champion extra fire-cooldown scale (lower = faster)
int  endlessChampionShotDamagePercent(void); // champion extra shot-damage scale (higher = harder)

// Call for every logical death so ordinary enemies break the link-group latch.
void endlessAwardEliteKill(int linknum, int eliteState);

// Run-persistent perks.
extern bool endlessPerkPending;      // a perk pick is queued for the next shop's front gate

void        endlessGeneratePerkChoices(int offers);  // roll this visit's offers (call before the shop)
int         endlessPerkChoiceCount(void);      // how many perks are offered (3; 4 if bought, 5 after a milestone)
const char *endlessPerkChoiceName(int i);      // menu label for offered perk i
const char *endlessPerkChoiceDesc(int i);      // help-line description for offer i
const char *endlessPerkChoiceOwnedText(int i); // ...and its "Owned n/max", drawn flush right of that
void        endlessTakePerk(int i);            // acquire offered perk i (increments its stack); the post-zone pick is free
long        endlessPerkDeclineBonus(void);     // "Take the Cash" buyout: scales with depth, slate width and perks owned
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
void endlessCountermeasureTick(void);        // Countermeasure Suite: advance the burst cooldown one tick (endlessGameplayTick)
int  endlessPerkCountermeasureRadius(void);  // Countermeasure Suite: projectile-clear radius if ready now (0 = not owned / on cooldown); varz.c JE_playerDamage
void endlessCountermeasureFired(void);       // Countermeasure Suite: re-arm the cooldown after a burst (varz.c)
bool endlessPerkChainReactionActive(void);   // Chain Reaction: perk owned (tyrian2.c kill-site pulse queue)
int  endlessPerkChainRadius(void);           // Chain Reaction: pulse radius in px
int  endlessPerkChainDamage(void);           // Chain Reaction: armor damage the pulse deals to nearby fodder
int  endlessPerkAmmoPercent(void);           // Ordnance Reserves: sidekick-magazine bonus % (0 = not applying); the shop label and the flown magazine both derive from this
int  endlessPerkSidekickAmmo(int base);      // Ordnance Reserves: a shipped option.ammo magazine, boosted + capped (0 stays 0: charge sidekicks have no magazine)
int  endlessPerkSidekickRefillTicks(int baseTicks, int stockAmmo); // Ordnance Reserves: the per-round refill interval, scaled so a boosted magazine still fills in the shipped time
int  endlessPerkSpecialDuration(int base, int cap); // Ordnance Reserves: a timed special's duration, stretched; `cap` clamps it for the byte-wide fields (0 = uncapped)
int  endlessPerkFailsafeTicks(void);         // Failsafe: i-frames a hit that reached the hull grants (0 = not owned); varz.c JE_playerDamage

// Perk registry accessors (for the endless debug screen: list / toggle / stack perks).
int         endlessPerkCount(void);          // number of perks (PERK_COUNT)
const char *endlessPerkName(int id);         // perk display name
const char *endlessPerkDesc(int id);         // perk one-line effect description (for the perk-list help)
int         endlessPerkMaxStack(int id);     // max stacks this perk allows
int         endlessPerkGetOwned(int id);     // current owned stacks
void        endlessPerkSetOwned(int id, int n); // set owned stacks (clamped 0..max)

#endif // ENDLESS_H
