/* Endless course generation and selection. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "game_menu.h"
#include "joystick.h"
#include "player.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The shop's Start Level menu displays these generated courses.

int      endlessCourseCnt = 0;
int      endlessCourseEp[ENDLESS_MAX_COURSES];
JE_byte  endlessCourseSec[ENDLESS_MAX_COURSES];
JE_byte  endlessCourseFile[ENDLESS_MAX_COURSES];  // each course's specific lvlFileNum (see forcedLvlFileNum)
Uint64 endlessCourseMod[ENDLESS_MAX_COURSES];
static JE_byte  endlessCourseNameSalt[ENDLESS_MAX_COURSES];  // per-visit nudge so no two offered names read the same
static char     endlessCourseBaseName[ENDLESS_MAX_COURSES][10];  // Radar perk: authored level name behind each course (9 chars + NUL)
int      endlessLastEp = 0;
JE_byte  endlessLastSec = 0;
bool     endlessForced = false;  // this visit is a forced "Ambush" (single dangerous sector)

// The intrinsic-danger nudge for course i's shipped level at the current run difficulty
// (endless_levelprofile.h): folded into the course's shown danger tier/grade, its sort position and
// its payout so all three reflect the LEVEL, not just its modifiers. 0 for an out-of-range course.
static int endlessCourseBaseDanger(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		return 0;
	return endlessLevelBaseDanger(endlessCourseEp[i], endlessCourseFile[i], difficultyLevel);
}

// What course i would ACTUALLY launch with, replaying endlessSelectCourse's two fold passes: merge
// anything bought this visit (the purchase wins any kill-fire collision), then spend the queued
// Sabotage charges. Returns the FOLDED set, reporting through `cleansedOut` which bits the charges
// strip so the card can list them struck through. The Long Con's deferred APEX is NOT replayed --
// the player paid for that one to arrive unannounced.
static Uint64 endlessCourseLaunchMods(int i, Uint64 *cleansedOut)
{
	const Uint64 folded = endlessFoldPurchasedMods(endlessCourseMod[i], endlessPurchasedMods);
	Uint64 kept = folded;
	for (int c = 0; c < endlessCleanseChargeCount; ++c)
		kept = endlessStripWorstMod(kept);
	if (cleansedOut != NULL)
		*cleansedOut = folded & ~kept;
	return folded;
}

// The clear payout for course i at the current depth: the modifier table plus the LEVEL's fine
// payoutMille term, exactly as endlessClearBonus banks it, so shown == banked. Priced off what
// launch actually commits (endlessCourseLaunchMods), so a bought buff or a queued Sabotage charge
// can't leave the card quoting danger money the sector will no longer be dangerous enough to pay.
long endlessCoursePayout(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		return 0;
	Uint64 cleansed = 0;
	const Uint64 mods = endlessCourseLaunchMods(i, &cleansed);
	return endlessClearBonusForEx(mods & ~cleansed,
	                              endlessLevelPayoutMille(endlessCourseEp[i], endlessCourseFile[i], difficultyLevel));
}

// Pick a random BOON bit safe to weld onto a hostile course carrying `hostiles`, making it a MIXED
// "gambit" sector. Boons that would fight a threat on the SAME lever are held back, so the monitor's
// red/green rows never contradict. The kill-fire boons get a small roll of their own rather than a
// full share of the pool. The first three candidates are unconditional, so this never returns 0.
static Uint64 endlessPickMixBoon(Uint64 hostiles)
{
	// Split evenly between the two kill-fire boons allowed on mixed courses. Overdrive remains a
	// pure-boon/shop effect: a hostile course should not casually roll the strongest version.
	if (endlessRand() % 100 < 4)
		return (endlessRand() % 2) ? ENDLESS_MOD_TURBODRIVE : ENDLESS_MOD_OVERBLAST;

	// The no-elite-tier boons get their own small roll. NOELITE is held back when the course's danger
	// IS that tier (it would cancel, not gamble); NOCHAMP may ride an Elite Pack but not a Legion.
	// Weighted ~2:1 toward NOCHAMP. Gated on the 25%-share unlock; roll first so the stream is stable.
	if ((endlessRand() % 100 < 6) && endlessEliteBoonsUnlocked())
	{
		if ((endlessRand() % 3) == 0)  // ~1/3 of the roll aims for the stronger, rarer NOELITE
		{
			if (!(hostiles & (ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION)))
				return ENDLESS_MOD_NOELITE;
		}
		else if (!(hostiles & ENDLESS_MOD_LEGION))
			return ENDLESS_MOD_NOCHAMP;
	}

	Uint64 cand[14];   // 3 always-safe + 6 new unconditional + Flak + the 2 elite-stat + Fragile + Dilation
	int n = 0;
	cand[n++] = ENDLESS_MOD_OVERCHARGE;   // more player damage -- always safe
	cand[n++] = ENDLESS_MOD_BOUNTY;       // pure cash, no safety -- always safe
	cand[n++] = ENDLESS_MOD_FAVOR;        // cheaper next shop -- always safe
	// The later boons: each acts on a system no ordinary hostile bit touches, so none can cancel a
	// threat. Two are gated on the system being in play at all (see endlessBoonsLockedHere).
	cand[n++] = ENDLESS_MOD_AEGIS;        // the shield can't be punched through
	cand[n++] = ENDLESS_MOD_LOWPROFILE;   // a quarter off the hitbox
	cand[n++] = ENDLESS_MOD_AUXREACTOR;   // free shield recharge
	cand[n++] = ENDLESS_MOD_SOFTLANDING;  // ramming stops being lethal
	cand[n++] = ENDLESS_MOD_SHOCKWAVE;    // elite kills clear the air
	cand[n++] = ENDLESS_MOD_STARCHARTS;   // no combat effect at all -- a pure agency reward
	if (endlessTideBoonsUnlocked())
		cand[n++] = ENDLESS_MOD_FLAKSCREEN;
	if (endlessEliteBoonsUnlocked())
	{
		cand[n++] = ENDLESS_MOD_GIANTKILLER;
		cand[n++] = ENDLESS_MOD_CLEANSIGNALS;
	}
	if (!(hostiles & ENDLESS_MOD_FORTIFIED))                        // frail vs +HP would cancel
		cand[n++] = ENDLESS_MOD_FRAGILE;
	if (!(hostiles & (ENDLESS_MOD_SWIFT | ENDLESS_MOD_OVERCLOCK)))  // slow shots vs fast shots would cancel
		cand[n++] = ENDLESS_MOD_DILATION;
	return cand[endlessRand() % n];
}

// The boon table doubles as the canonical name dictionary, so its bitsets stay intact and the
// Turbodrive/Overblast rarity slots are swapped at generation time instead. An involution, so
// distinct Jackpot themes stay distinct. Reactor Redline keeps Turbodrive -- it promises fast guns.
static Uint64 endlessSwapTurbodriveOverblast(Uint64 mods)
{
	const bool hadTurbodrive = (mods & ENDLESS_MOD_TURBODRIVE) != 0;
	const bool hadOverblast  = (mods & ENDLESS_MOD_OVERBLAST) != 0;
	mods &= ~(ENDLESS_MOD_TURBODRIVE | ENDLESS_MOD_OVERBLAST);
	if (hadTurbodrive)
		mods |= ENDLESS_MOD_OVERBLAST;
	if (hadOverblast)
		mods |= ENDLESS_MOD_TURBODRIVE;
	return mods;
}

// Hide boons until the system they affect is active.
static Uint64 endlessLockedBoons(void)
{
	Uint64 locked = 0;
	if (!endlessEliteBoonsUnlocked())
		locked |= ENDLESS_MOD_NOCHAMP | ENDLESS_MOD_NOELITE | ENDLESS_MOD_GIANTKILLER | ENDLESS_MOD_CLEANSIGNALS;
	if (!endlessTideBoonsUnlocked())
		locked |= ENDLESS_MOD_FLAKSCREEN;
	return locked;
}

// Build a two- or three-bit boon combo from compatible effects.
static Uint64 endlessMakeBoonCombo(void)
{
	Uint64 pool[16];
	int poolN = 0;
	pool[poolN++] = ENDLESS_MOD_FRAGILE;
	pool[poolN++] = ENDLESS_MOD_BOUNTY;
	pool[poolN++] = ENDLESS_MOD_OVERCHARGE;
	pool[poolN++] = ENDLESS_MOD_DILATION;
	pool[poolN++] = ENDLESS_MOD_FAVOR;
	pool[poolN++] = ENDLESS_MOD_OVERBLAST;
	// The later boons, all on independent levers. GIANTKILLER and CLEANSIGNALS share the special
	// tier but touch different halves of it (hulls vs guns), so that pair still stacks.
	// BREAKTHROUGH is deliberately absent -- it has its own heavily-gated roll.
	pool[poolN++] = ENDLESS_MOD_AEGIS;
	pool[poolN++] = ENDLESS_MOD_LOWPROFILE;
	pool[poolN++] = ENDLESS_MOD_AUXREACTOR;
	pool[poolN++] = ENDLESS_MOD_SOFTLANDING;
	pool[poolN++] = ENDLESS_MOD_SHOCKWAVE;
	pool[poolN++] = ENDLESS_MOD_STARCHARTS;
	if (endlessTideBoonsUnlocked())         // FLAKSCREEN thins tide shots -- pointless before the tide runs
		pool[poolN++] = ENDLESS_MOD_FLAKSCREEN;
	if (endlessEliteBoonsUnlocked())        // NOCHAMP / the two elite-stat boons: only once elites are a real presence (>25% share)
	{
		pool[poolN++] = ENDLESS_MOD_NOCHAMP;
		pool[poolN++] = ENDLESS_MOD_GIANTKILLER;
		pool[poolN++] = ENDLESS_MOD_CLEANSIGNALS;
	}
	int ord[COUNTOF(pool)];
	for (int k = 0; k < poolN; ++k)
		ord[k] = k;
	for (int k = poolN - 1; k > 0; --k)
	{
		const int j = endlessRand() % (k + 1);
		const int t = ord[k]; ord[k] = ord[j]; ord[j] = t;
	}
	const int want = 2 + (endlessRand() % 100 < 40);   // 2, sometimes 3
	Uint64 combo = 0;
	for (int k = 0; k < want && k < poolN; ++k)
		combo |= pool[ord[k]];
	return combo;
}

// Deep-run danger rises in two stages; Gauntlet and Ambush remain capped.
#define ENDLESS_DANGER_RAMP_START 40   // zone the tilt begins (no escalation at/below)
#define ENDLESS_DANGER_RAMP_MID   100  // zone the tilt reaches its "~2x" tuning (scale == ENDLESS_DANGER_RAMP_MID_SCALE)
#define ENDLESS_DANGER_RAMP_FULL  250  // zone the tilt caps at its "~6x" tuning (scale == ENDLESS_DANGER_RAMP_FULL_SCALE)
#define ENDLESS_DANGER_RAMP_MID_SCALE  100  // scale at the mid-point -- the approved ~2x point; keep at 100 so zone<=MID is unchanged
#define ENDLESS_DANGER_RAMP_FULL_SCALE 500  // scale at the cap -- the deep-end "~6x" (only the uncapped levers read this far)
#define ENDLESS_DANGER_GAUNTLET_CAP_PCT 45  // ceiling on the all-hostile Gauntlet chance -- keeps a calm route always possible
#define ENDLESS_DANGER_AMBUSH_CAP_PCT   15  // ceiling on the one-forced-danger Ambush chance

// Ramp scale: 0 at START, 100 at MID, then up to FULL_SCALE.
static int endlessDangerRamp(void)
{
	if (!endlessMode)
		return 0;
	const int zone = endlessDifficultyZone();
	if (zone <= ENDLESS_DANGER_RAMP_START)
		return 0;
	if (zone <= ENDLESS_DANGER_RAMP_MID)  // first stage: 0 -> MID_SCALE across START..MID (unchanged)
		return ENDLESS_DANGER_RAMP_MID_SCALE * (zone - ENDLESS_DANGER_RAMP_START)
		         / (ENDLESS_DANGER_RAMP_MID - ENDLESS_DANGER_RAMP_START);
	// Second stage: MID_SCALE -> FULL_SCALE across MID..FULL, then held at the cap.
	const int s = ENDLESS_DANGER_RAMP_MID_SCALE
	                + (ENDLESS_DANGER_RAMP_FULL_SCALE - ENDLESS_DANGER_RAMP_MID_SCALE) * (zone - ENDLESS_DANGER_RAMP_MID)
	                    / (ENDLESS_DANGER_RAMP_FULL - ENDLESS_DANGER_RAMP_MID);
	return (s > ENDLESS_DANGER_RAMP_FULL_SCALE) ? ENDLESS_DANGER_RAMP_FULL_SCALE : s;
}

// Shrink rare-event divisors with depth; brutal rows stop at the midpoint's 2x rate.
static int endlessDangerRareDivEx(int base, bool brutal)
{
	int ramp = endlessDangerRamp();
	if (brutal && ramp > ENDLESS_DANGER_RAMP_MID_SCALE)
		ramp = ENDLESS_DANGER_RAMP_MID_SCALE;
	const int d = base * 100 / (100 + ramp);
	return (d < 1) ? 1 : d;
}

// Rare signatures overwrite random non-clean slots in table order.
// Rows draw from a filtered theme pool or use a fixed modifier set.
typedef struct {
	int                 oneInN;   // base rarity: 1 in this many visits, before the danger ramp
	const EndlessTheme *pool;     // pool to draw the sector from; NULL = deal `mods` instead
	unsigned            poolN;
	Uint64              must;     // pool entries must carry all of these bits...
	Uint64              forbid;   // ...and none of these
	Uint64              mods;     // the fixed bitset, when there is no pool
	bool                brutal;   // true = the deep ramp on this row is capped at 2x (never routine)
} EndlessRareInjection;

#define RARE_FROM(n, tbl, hard)               { (n), (tbl), COUNTOF(tbl), 0, 0, 0, (hard) }
#define RARE_PICK(n, tbl, must, forbid, hard) { (n), (tbl), COUNTOF(tbl), (must), (forbid), 0, (hard) }
#define RARE_FIXED(n, bits, hard)             { (n), NULL, 0, 0, 0, (bits), (hard) }
static const EndlessRareInjection endlessRareInjections[] = {
	// Homing: the GENTLEST homing tier -- enemies barely lean toward you, no ram. Capped: even the
	// mild homing tier changes how a whole sector has to be flown.
	RARE_FROM(26, endlessHomingThemes, true),
	// Kamikaze: the MODERATE homing tier (strength 3, no ram -- the brutal rammer moved to the
	// RAMPAGE gamble). After homing, so the harder tier wins a clash.
	RARE_FROM(55, endlessKamikazeThemes, true),
	// Overload: Overclock cranked way up.
	RARE_FROM(17, endlessOverloadThemes, true),
	// Warp Speed: a scroll THREAT -- the level hurtles at you.
	RARE_FIXED(15, ENDLESS_MOD_WARP, true),
	// Evil Turbodrive / Overdrive: your own kill streak turned against you -- jammed guns, and
	// for Evil Overdrive weaker shots too. One roll feeds all three mirrors, so the base rarity is
	// the frequency of "some evil sector"; each individual bit lands at about a third of it.
	RARE_FROM(12, endlessEvilThemes, true),
	// Reactor Redline: the gamble "Overheat" loose in the wild -- kills quicken your guns, but
	// the redlined core cooks your hull.
	RARE_FROM(50, endlessRedlineThemes, true),
	// Tar Pit (SLUGGISH + GRAVITY): the ship crawls WHILE dragged down. Brutal but always flyable
	// (endlessGravityDrift slows the pull with the ship), so this one keeps the full ramp. SLUGGISH
	// stays out of the combinable pool, so this is the sole source of the pairing.
	RARE_FROM(28, endlessSluggishThemes, false),
	// Apex Swarm (every enemy elite), from the Apex-tier rare themes: bare Apex, or Apex plus one
	// extra danger. Late enough to override a boon slot.
	RARE_PICK(26, endlessRareThemes, ENDLESS_MOD_APEX, ENDLESS_MOD_LEGION, true),
	// Legion (every enemy a CHAMPION) -- rarer still, among the deadliest sectors.
	RARE_PICK(53, endlessRareThemes, ENDLESS_MOD_LEGION, 0, true),
	// Cataclysm: an extreme multi-danger nightmare with no elite tier -- just everything at once.
	// The rare themes carrying neither Apex nor Legion (the 5+-danger pure combos).
	RARE_PICK(50, endlessRareThemes, 0, ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION, true),
	// Dead Generator (DEADGEN): no shield regen AND a power-starved main gun. The nastiest
	// handicap in the game, so it is the rarest and rolled LAST -- it claims the slot when it
	// fires. Rear guns / sidekicks / specials carry the fight.
	RARE_FROM(70, endlessDeadgenThemes, true),
};
#undef RARE_FROM
#undef RARE_PICK
#undef RARE_FIXED

// Replace Elite Pack once the natural elite share exceeds its 50% cap.
// Apex and Legion remain valid because they force 100%.
static void endlessFixRedundantElitePack(int c)
{
	const Uint64 mods = endlessCourseMod[c];
	if (!(mods & ENDLESS_MOD_ELITEPACK))
		return;
	if (mods & (ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION))
		return;                               // 100% share -- a real increase, keep it
	if (endlessNaturalEliteChancePercent() <= 50)
		return;                               // still raises the share -- ELITEPACK is genuine here

	const Uint64 base = mods & ~(Uint64)ENDLESS_MOD_ELITEPACK;
	// Ordered by rough danger so the stand-in is a fair replacement; the first bit not already on the
	// course (and, where possible, not colliding with another offered course) wins.
	static const Uint64 subs[] = {
		ENDLESS_MOD_DEVASTATING, ENDLESS_MOD_FORTIFIED, ENDLESS_MOD_ENRAGE,
		ENDLESS_MOD_FRENZY, ENDLESS_MOD_SWIFT, ENDLESS_MOD_GRAVITY, ENDLESS_MOD_OVERCLOCK,
	};
	Uint64 fallback = base;                   // if every candidate collides, at least drop ELITEPACK
	bool haveFallback = false;
	for (unsigned i = 0; i < COUNTOF(subs); ++i)
	{
		if (base & subs[i])
			continue;                         // already present -- adding it would be a no-op
		const Uint64 cand = base | subs[i];
		if (!haveFallback) { fallback = cand; haveFallback = true; }
		bool clash = false;
		for (int k = 0; k < endlessCourseCnt && !clash; ++k)
			if (k != c && endlessCourseMod[k] == cand)
				clash = true;
		if (!clash)
		{
			endlessCourseMod[c] = cand;
			return;
		}
	}
	endlessCourseMod[c] = fallback;           // every stand-in collided/was present -- best effort
}

// Milestone pool. Nonzero groups are mutually exclusive on a course.
// Elite Pack and rare signatures are excluded.
static const struct { Uint64 bit; int group; } endlessMilestonePool[] = {
	{ ENDLESS_MOD_FORTIFIED,   0 },
	{ ENDLESS_MOD_FRENZY,      0 },
	{ ENDLESS_MOD_SWIFT,       0 },
	{ ENDLESS_MOD_DEVASTATING, 0 },
	{ ENDLESS_MOD_ENRAGE,      0 },
	{ ENDLESS_MOD_GRAVITY,     0 },
	{ ENDLESS_MOD_TOPSY,       0 },
	{ ENDLESS_MOD_SLUGGISH,    0 },
	{ ENDLESS_MOD_MARTYRDOM,   0 },  // the four reactive dangers: independent levers, any mix may land
	{ ENDLESS_MOD_SEEKER,      0 },
	{ ENDLESS_MOD_STATIC,      0 },  // safe here: DEADGEN is out of the pool, so the incompatibility can't arise
	{ ENDLESS_MOD_RETALIATION, 0 },
	{ ENDLESS_MOD_SLIPSTREAM,  1 },  // scroll pace
	{ ENDLESS_MOD_OVERCLOCK,   1 },
	{ ENDLESS_MOD_OVERLOAD,    1 },
	{ ENDLESS_MOD_WARP,        1 },
	{ ENDLESS_MOD_HOMING,      2 },  // homing tier
	{ ENDLESS_MOD_KAMIKAZE,    2 },
	{ ENDLESS_MOD_APEX,        3 },  // elite tier
	{ ENDLESS_MOD_LEGION,      3 },
	{ ENDLESS_MOD_SHIELDLESS,  4 },  // shield handicap
};

// Bits held scarcer than their danger warrants: they tax how a sector must be FLOWN for its whole
// length, so they wear thin faster than a stat bump does. The weighted pools further down do this
// with a low weight; the milestone pool above is flat, so each bit sits out 1 in `oneInN` of the
// builder's attempts instead. Add a bit here AND to its pool weight -- the two work together.
static const struct { Uint64 bit; int oneInN; } endlessScarceMods[] = {
	{ ENDLESS_MOD_TOPSY,   3 },
	{ ENDLESS_MOD_GRAVITY, 3 },
};

// The danger weight one modifier bit contributes, read straight off endlessModTable so a milestone's
// target rank can never drift from the table the rank bands are computed from.
static int endlessModReward(Uint64 bit)
{
	for (unsigned i = 0; i < COUNTOF(endlessModTable); ++i)
		if (endlessModTable[i].bit == bit)
			return endlessModTable[i].reward;
	return 0;
}

// Build a distinct hostile combo whose displayed grade matches `rank`.
static Uint64 endlessMakeRankComboForLevel(int rank, int baseDanger, const Uint64 *used, int usedN)
{
	int lo = (rank <= 6) ? 34 : (rank == 7) ? 40 : (rank == 8) ? 50 : 60;
	int hi = (rank <= 6) ? 39 : (rank == 7) ? 49 : (rank == 8) ? 59 : 95;
	lo -= baseDanger;  // aim the modifier-only build low by the level's own contribution; the verify
	hi -= baseDanger;  // below still checks the TRUE displayed rank, so synergy overshoot is caught too
	if (hi < 1) hi = 1;
	if (lo > hi) lo = hi;
	if (lo < 1) lo = 1;

	Uint64 best = 0;   // first buildable combo, an absolute fallback if no attempt lands the exact rank
	Uint64 exact = 0;  // first combo whose DISPLAYED rank matches, kept if every such combo collides
	for (int attempt = 0; attempt < 60; ++attempt)
	{
		int ord[COUNTOF(endlessMilestonePool)];
		for (unsigned k = 0; k < COUNTOF(endlessMilestonePool); ++k)
			ord[k] = (int)k;
		for (int k = (int)COUNTOF(endlessMilestonePool) - 1; k > 0; --k)
		{
			const int j = endlessRand() % (k + 1);
			const int t = ord[k]; ord[k] = ord[j]; ord[j] = t;
		}

		// Which scarce bits sit this attempt out. Rolled unconditionally, so the stream stays aligned.
		Uint64 sitOut = 0;
		for (unsigned s = 0; s < COUNTOF(endlessScarceMods); ++s)
			if ((endlessRand() % endlessScarceMods[s].oneInN) == 0)
				sitOut |= endlessScarceMods[s].bit;

		// Randomised greedy: walk the shuffled pool taking every bit that doesn't overshoot the band,
		// and stop the moment the running (modifier-only) score is inside it.
		Uint64 combo = 0;
		int score = 0;
		unsigned groups = 0;
		for (unsigned k = 0; k < COUNTOF(endlessMilestonePool) && score < lo; ++k)
		{
			if (sitOut & endlessMilestonePool[ord[k]].bit)
				continue;
			const int g = endlessMilestonePool[ord[k]].group;
			if (g != 0 && (groups & (1u << g)))
				continue;
			const int w = endlessModReward(endlessMilestonePool[ord[k]].bit);
			if (score + w > hi)
				continue;
			combo |= endlessMilestonePool[ord[k]].bit;
			score += w;
			if (g != 0)
				groups |= 1u << g;
		}
		if (combo != 0 && best == 0)
			best = combo;
		if (endlessDangerRankLevelEx(combo, baseDanger) != rank)
			continue;  // synergy or the level's baseDanger pushed the displayed rank off the band -- reshuffle

		if (exact == 0)
			exact = combo;
		bool clash = false;
		for (int k = 0; k < usedN && !clash; ++k)
			if (used[k] == combo)
				clash = true;
		if (!clash)
			return combo;
	}
	return exact ? exact : best;
}

// Generation phase order is seed-visible; append phases unless reshuffling is intentional.

// Weighted ordinary dangers used by widening and gambit phases.
// Special signatures, redundant effects, and boons stay out of this pool.
typedef struct {
	Uint64 bit;
	int    weight;
} EndlessModWeight;
static const EndlessModWeight endlessCombinableMods[] = {
	// -- the four core bits: still the commonest dangers, no longer near-certain --
	{ ENDLESS_MOD_FORTIFIED,   3 },
	{ ENDLESS_MOD_FRENZY,      3 },
	{ ENDLESS_MOD_SWIFT,       3 },
	{ ENDLESS_MOD_DEVASTATING, 3 },
	// -- mid tier --
	{ ENDLESS_MOD_ENRAGE,      4 },
	{ ENDLESS_MOD_OVERCLOCK,   4 },
	{ ENDLESS_MOD_ELITEPACK,   4 },
	// -- under-seen flavour, weighted up --
	{ ENDLESS_MOD_STATIC,      6 },  // a generic damage-punish tax; never pairs the injected-only DEADGEN
	{ ENDLESS_MOD_SHIELDLESS,  5 },  // a pure defense debuff -- safe to stack onto any combo
	{ ENDLESS_MOD_RETALIATION, 5 },  // kill-tempo tax, promoted out of injection-only
	// -- promoted, deliberately kept the modest end of the pool --
	{ ENDLESS_MOD_MARTYRDOM,   4 },
	{ ENDLESS_MOD_SEEKER,      4 },
	// -- the scarce bits (endlessScarceMods): a whole sector flown crooked, or flown against a pull --
	{ ENDLESS_MOD_TOPSY,       3 },
	{ ENDLESS_MOD_GRAVITY,     3 },
};

// Every combinable bit OR'd together -- "is this course built only from ordinary dangers?".
static Uint64 endlessCombinableMask(void)
{
	Uint64 mask = 0;
	for (unsigned k = 0; k < COUNTOF(endlessCombinableMods); ++k)
		mask |= endlessCombinableMods[k].bit;
	return mask;
}

// Draw `want` DISTINCT bits from a weighted table, honouring the weights (a plain shuffle can't).
// `damp` bits already on another route of this slate are cut to a third weight, so one danger rarely
// covers three of five offered courses -- the per-slate half of the variety fix.
static Uint64 endlessWeightedModDraw(const EndlessModWeight *tbl, unsigned count, int want, Uint64 damp)
{
	Uint64 taken = 0, combo = 0;
	for (int n = 0; n < want; ++n)
	{
		int total = 0;
		for (unsigned k = 0; k < count; ++k)
		{
			if (taken & tbl[k].bit)
				continue;
			total += (damp & tbl[k].bit) ? (tbl[k].weight + 2) / 3 : tbl[k].weight;
		}
		if (total <= 0)
			break;                               // every entry already drawn
		int roll = (int)(endlessRand() % (unsigned)total);
		for (unsigned k = 0; k < count; ++k)
		{
			if (taken & tbl[k].bit)
				continue;
			const int w = (damp & tbl[k].bit) ? (tbl[k].weight + 2) / 3 : tbl[k].weight;
			if (roll < w)
			{
				taken |= tbl[k].bit;
				combo |= tbl[k].bit;
				break;
			}
			roll -= w;
		}
	}
	return combo;
}

// Every hostile bit already charted on a route OTHER than `forCourse` -- the damping set above.
static Uint64 endlessOtherCourseMods(int forCourse)
{
	Uint64 used = 0;
	for (int k = 0; k < endlessCourseCnt; ++k)
		if (k != forCourse)
			used |= endlessCourseMod[k] & ENDLESS_HOSTILE_MASK;
	return used;
}

// Pick a weighted signature first, then a curated theme carrying it.
// Martyrdom and Seeker already enter through the ordinary pool.
static const EndlessModWeight endlessThemeSignatures[] = {
	{ ENDLESS_MOD_FORTIFIED,    3 },
	{ ENDLESS_MOD_FRENZY,       3 },
	{ ENDLESS_MOD_SWIFT,        3 },
	{ ENDLESS_MOD_DEVASTATING,  3 },
	{ ENDLESS_MOD_ENRAGE,       4 },
	{ ENDLESS_MOD_ELITEPACK,    4 },
	{ ENDLESS_MOD_OVERCLOCK,    4 },
	// the scarce bits, held down here too -- though the dictionary carries them into other
	// signatures' rows, so this side moves far less than the widen pool
	{ ENDLESS_MOD_TOPSY,        5 },
	{ ENDLESS_MOD_GRAVITY,      3 },
	{ ENDLESS_MOD_STATIC,       9 },
	{ ENDLESS_MOD_RETALIATION,  9 },
	{ ENDLESS_MOD_SLIPSTREAM,   8 },  // its only route onto the chart: out of the widen pool by design
	{ ENDLESS_MOD_SLUGGISH,     7 },
	{ ENDLESS_MOD_SHIELDLESS,   6 },
};

// One curated theme drawn by signature, damping signatures already on the slate. Returns 0 only if
// the signature it picked has no row at all (impossible with the table above, but checked).
static Uint64 endlessPickSignatureTheme(int forCourse)
{
	const Uint64 damp = endlessOtherCourseMods(forCourse);
	const Uint64 sig = endlessWeightedModDraw(endlessThemeSignatures,
	                                          COUNTOF(endlessThemeSignatures), 1, damp);
	if (sig == 0)
		return 0;
	return endlessPickThemeMods(endlessHostileThemes, COUNTOF(endlessHostileThemes), sig, 0);
}

// Gather distinct episode/section pairs outside the recent-play window.
static void endlessGatherCourseLevels(int wantCourses)
{
	for (int guard = 0; guard < 40 && endlessCourseCnt < wantCourses; ++guard)
	{
		int ep;
		JE_byte sec, file;
		if (!endlessRandomSafeLevel(&ep, &sec, &file))
			break;

		bool dup = false;
		for (int k = 0; k < endlessCourseCnt && !dup; ++k)
			if (endlessCourseEp[k] == ep && endlessCourseSec[k] == sec)
				dup = true;
		if (dup)
			continue;

		endlessCourseEp[endlessCourseCnt] = ep;
		endlessCourseSec[endlessCourseCnt] = sec;
		endlessCourseFile[endlessCourseCnt] = file;
		endlessCourseMod[endlessCourseCnt] = 0;
		++endlessCourseCnt;
	}
	if (endlessCourseCnt == 0)  // fallback: guarantee at least one course
	{
		int ep = episodeNum;
		JE_byte sec = FIRST_LEVEL, file = 0;
		endlessRandomSafeLevel(&ep, &sec, &file);
		endlessCourseEp[0] = ep;
		endlessCourseSec[0] = sec;
		endlessCourseFile[0] = file;
		endlessCourseMod[0] = 0;
		endlessCourseCnt = 1;
	}
}

// Shuffle the hostile-theme table into `idx`. The signature draw (endlessPickSignatureTheme) is what
// normally deals the themes; this order is the exhaustive FALLBACK all three consumers share (the
// initial deal, the duplicate re-roll and the gauntlet), which is why it is drawn once here.
static void endlessShuffleThemeOrder(int *idx)
{
	for (unsigned i = 0; i < COUNTOF(endlessHostileThemes); ++i)
		idx[i] = (int)i;
	for (int i = (int)COUNTOF(endlessHostileThemes) - 1; i > 0; --i)
	{
		const int j = endlessRand() % (i + 1);
		const int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
	}
}

// Pick an unused hostile theme, preferring the weighted signature distribution.
static Uint64 endlessUnusedHostileTheme(const int *idx, int forCourse)
{
	for (int attempt = 0; attempt < 24; ++attempt)
	{
		const Uint64 m = endlessPickSignatureTheme(forCourse);
		if (m == 0)
			break;
		bool used = false;
		for (int k = 0; k < endlessCourseCnt && !used; ++k)
			if (k != forCourse && endlessCourseMod[k] == m)
				used = true;
		if (!used)
			return m;
	}
	for (unsigned t = 0; t < COUNTOF(endlessHostileThemes); ++t)
	{
		const Uint64 m = endlessHostileThemes[idx[t]].mods;
		bool used = false;
		for (int k = 0; k < endlessCourseCnt && !used; ++k)
			if (k != forCourse && endlessCourseMod[k] == m)
				used = true;
		if (!used)
			return m;
	}
	return 0;
}

// Course 0 is always clean; courses 1+ get distinct hostile themes drawn by signature, so a visit
// never offers the same danger twice and the spread reflects endlessThemeSignatures rather than how
// many combos each bit happens to be named in. The shuffled order stays the exhaustive fallback.
static void endlessDealHostileThemes(const int *idx)
{
	endlessCourseMod[0] = 0;
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		const Uint64 m = endlessPickSignatureTheme(c);
		endlessCourseMod[c] = (m != 0) ? m : endlessHostileThemes[idx[c - 1]].mods;
	}
}

// WIDEN VARIETY: about half the hostile courses instead get a random combination drawn from the
// WEIGHTED ordinary-danger pool (any un-named combo still gets a generated name/help), 1-4 bits.
static void endlessWidenHostileCombos(int dangerRamp)
{
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		// Deep runs push more courses off the (often single-danger) curated themes onto random
		// multi-danger combos: the ~50% base share climbs with the ramp, capped at 80% so a few
		// legible curated themes always survive (the diverse-choice guarantee leans on them).
		int comboShare = 50 + dangerRamp * 20 / 100;
		if (comboShare > 80)
			comboShare = 80;
		if (endlessRand() % 100 >= comboShare)
			continue;
		// Bit-count weights: ~2.2 bits typical, lifted to ~3.4 by the ramp, with a HARD CEILING of
		// four simultaneous dangers -- past that a sector reads as "every danger at once" rather
		// than as itself. The deep-end escalation lands on WHICH dangers, not on how many.
		int want = 1 + (endlessRand() % 100 < 70 + dangerRamp * 8 / 100)
		             + (endlessRand() % 100 < 40 + dangerRamp * 10 / 100)
		             + (endlessRand() % 100 < 12 + dangerRamp * 8 / 100);
		if (want > (int)COUNTOF(endlessCombinableMods))
			want = (int)COUNTOF(endlessCombinableMods);
		endlessCourseMod[c] = endlessWeightedModDraw(endlessCombinableMods,
		                                             COUNTOF(endlessCombinableMods), want,
		                                             endlessOtherCourseMods(c));
	}
}

// Some visits replace a hostile route with a named or generated boon.
// Breakthrough is a much rarer replacement because it grants a perk.
#define ENDLESS_BREAKTHROUGH_PCT   7
#define ENDLESS_BREAKTHROUGH_DEPTH 5   // never in the opening zones: perks are still arriving on their own cadence there

// Is a Breakthrough allowed to be charted for the zone about to be played? Kept off any zone whose
// CLEAR would already hand out a guaranteed perk -- the outpost can only open one perk pick at a time,
// so landing both on one visit would just defer half the reward (endlessBreakthroughOwed does carry it
// over, but the player should SEE the perk they charted for, not one zone later).
static bool endlessBreakthroughAllowed(void)
{
	return endlessRunDepth >= ENDLESS_BREAKTHROUGH_DEPTH
	    && !endlessPerkDueAtDepth(endlessRunDepth + 1);
}

static void endlessDealBoonCourse(int dangerRamp)
{
	if (endlessCourseCnt > 1 && (endlessRand() % (3 + dangerRamp * 2 / 100)) == 0)
	{
		const int slot = 1 + endlessRand() % (endlessCourseCnt - 1);
		// Roll unconditionally so the seed stream stays aligned whether or not the gate lets it through.
		const bool breakthroughRoll = ((int)(endlessRand() % 100) < ENDLESS_BREAKTHROUGH_PCT);
		if (breakthroughRoll && endlessBreakthroughAllowed())
		{
			endlessCourseMod[slot] = endlessPickThemeMods(endlessBreakthroughThemes,
			                                              COUNTOF(endlessBreakthroughThemes), 0, 0);
			return;
		}
		if (endlessRand() % 100 < 40)
			endlessCourseMod[slot] = endlessMakeBoonCombo();
		else
			// Forbid the still-locked boons, so a shallow boon course draws a different theme instead of a
			// near-empty one: the elite-tier boons until the 25%-share unlock, Flak Screen until the tide
			// is actually adding shots to remove.
			endlessCourseMod[slot] = endlessSwapTurbodriveOverblast(
				endlessPickThemeMods(endlessBoonThemes, COUNTOF(endlessBoonThemes), 0, endlessLockedBoons()));
	}
}

// Add one compatible boon to some ordinary hostile routes.
// Rare signatures and pure boon routes keep their identity.
static void endlessGraftGambits(int dangerRamp)
{
	// Slipstream isn't in the combinable pool (redundant beside Overclock's scroll), but its named
	// hostile sectors are ordinary enough to gamble on -- keep them boon-graft eligible so the
	// Blitz / Time Warp / Power Play / Payday / Smash and Grab gambits stay reachable.
	const Uint64 mixCommon = endlessCombinableMask() | ENDLESS_MOD_SLIPSTREAM;
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		const Uint64 h = endlessCourseMod[c] & ENDLESS_HOSTILE_MASK;
		if (h == 0 || (h & ~mixCommon) != 0)       // only ordinary hostile courses
			continue;
		if (endlessCourseMod[c] & ENDLESS_BOON_MASK) // already a gambit / carries a boon
			continue;
		int gambitPct = 35 - dangerRamp * 20 / 100;  // ~35% gain a boon early -> ~15% by the mid -> 5% floor deep
		if (gambitPct < 5)
			gambitPct = 5;
		if (endlessRand() % 100 >= gambitPct)  // fewer mitigations grafted onto hostiles as the run deepens
			continue;
		endlessCourseMod[c] |= endlessPickMixBoon(h);
	}
}

// Rare / super-rare danger injections, rolled IN TABLE ORDER (see endlessRareInjections): each
// gets one "1 in N" roll, and the one that fires last wins a contested slot.
static void endlessInjectRareSectors(void)
{
	for (unsigned k = 0; k < COUNTOF(endlessRareInjections); ++k)
	{
		const EndlessRareInjection *inj = &endlessRareInjections[k];
		if (endlessCourseCnt > 1 && (endlessRand() % endlessDangerRareDivEx(inj->oneInN, inj->brutal)) == 0)
		{
			const int slot = 1 + endlessRand() % (endlessCourseCnt - 1);
			endlessCourseMod[slot] = (inj->pool != NULL)
				? endlessPickThemeMods(inj->pool, inj->poolN, inj->must, inj->forbid)
				: inj->mods;
		}
	}
}

// Replace duplicate ordinary modifier sets with unused hostile themes.
static void endlessDedupeCourseMods(const int *idx)
{
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		bool duplicate = false;
		for (int k = 0; k < c && !duplicate; ++k)
			if (endlessCourseMod[k] == endlessCourseMod[c])
				duplicate = true;
		if (!duplicate)
			continue;

		const Uint64 m = endlessUnusedHostileTheme(idx, c);
		if (m != 0)
			endlessCourseMod[c] = m;
	}
}

// JACKPOT: every course a pure boon. Deal DISTINCT boon themes (shuffle the table, take one per
// course), skipping the Cursed entries -- those carry a catch (empty next shop), not clean boons -- so
// the jackpot is all upside. Below the 25%-share unlock, skip the no-elite-tier boons too (they'd be
// near-empty this shallow), so the jackpot deals only themes that actually help here.
static void endlessDealJackpot(void)
{
	// Also skip whatever is still locked at this depth (endlessLockedBoons), so the jackpot deals
	// only themes that actually help here. BREAKTHROUGH needs no entry -- it has no row in
	// endlessBoonThemes at all, which is what keeps a jackpot from handing out a free perk pick.
	const Uint64 jackpotSkip = ENDLESS_MOD_CURSED | endlessLockedBoons();
	int bidx[COUNTOF(endlessBoonThemes)];
	int bn = 0;
	for (unsigned i = 0; i < COUNTOF(endlessBoonThemes); ++i)
		if ((endlessBoonThemes[i].mods & jackpotSkip) == 0)
			bidx[bn++] = (int)i;
	for (int i = bn - 1; i > 0; --i)
	{
		const int j = endlessRand() % (i + 1);
		const int t = bidx[i]; bidx[i] = bidx[j]; bidx[j] = t;
	}
	for (int c = 0; c < endlessCourseCnt && c < bn; ++c)
		endlessCourseMod[c] = endlessSwapTurbodriveOverblast(endlessBoonThemes[bidx[c]].mods);
}

// GAUNTLET: no Calm route and no boon -- turn every non-hostile course (the clean course 0 and any
// boon slot) into a fresh, distinct hostile theme. Courses already carrying a hostile bit
// (including a rare injected Apex / Kamikaze / Overload / etc.) keep their theme, so the gauntlet
// still fans out into varied dangers. Consumes no RNG, so the stream stays aligned.
static void endlessDealGauntlet(const int *idx)
{
	for (int c = 0; c < endlessCourseCnt; ++c)
	{
		if (endlessCourseMod[c] & ENDLESS_HOSTILE_MASK)
			continue;  // already a danger -- leave it be
		const Uint64 m = endlessUnusedHostileTheme(idx, c);
		if (m != 0)
			endlessCourseMod[c] = m;
	}
}

// AMBUSH: collapse the visit to one forced dangerous sector (keeping course 0's level). Kept fair
// -- homing rather than full kamikaze -- because the player gets no choice about flying it.
static void endlessDealAmbush(void)
{
	static const unsigned ambushCombos[] = {
		ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY,
		ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,
		ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_SWIFT,
		ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT,
		ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,
		ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,
		ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_FRENZY | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,
		ENDLESS_MOD_HOMING | ENDLESS_MOD_DEVASTATING,   // homing, not full kamikaze: an ambush is unavoidable, so keep it fair
		ENDLESS_MOD_HOMING | ENDLESS_MOD_SWIFT | ENDLESS_MOD_DEVASTATING,
		ENDLESS_MOD_GRAVITY | ENDLESS_MOD_SWIFT,
		ENDLESS_MOD_ENRAGE | ENDLESS_MOD_DEVASTATING,
		ENDLESS_MOD_OVERCLOCK | ENDLESS_MOD_DEVASTATING,
		ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_FORTIFIED | ENDLESS_MOD_DEVASTATING,
		ENDLESS_MOD_HOMING | ENDLESS_MOD_GRAVITY | ENDLESS_MOD_DEVASTATING,
	};
	endlessCourseCnt = 1;  // collapse to a single sector (keeps course 0's level)
	endlessCourseMod[0] = ambushCombos[endlessRand() % COUNTOF(ambushCombos)];
}

// DIVERSE CHOICE: with a full slate, guarantee at least one hostile course is a SINGLE negative
// modifier, so the chart reads as a spread of risk rather than a wall of multi-danger sectors. If
// none is, thin the mildest ORDINARY course down to its least-nasty bit -- never a rare signature
// or a boon/clean route. Runs before the OMNI roll and sort, so the thinned course sorts left.
static void endlessEnsureLegibleChoice(void)
{
	if (endlessCourseCnt < 4)
		return;

	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		const Uint64 h = endlessCourseMod[c] & ENDLESS_HOSTILE_MASK;
		if (h != 0 && (h & (h - 1)) == 0)   // exactly one hostile bit -- nothing to do
			return;
	}

	const Uint64 commonMask = endlessCombinableMask();
	int best = -1, bestBits = 99;
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		const Uint64 h = endlessCourseMod[c] & ENDLESS_HOSTILE_MASK;
		if (h == 0 || (h & ~commonMask) != 0)   // skip boon/clean routes and rare signature sectors
			continue;
		if (endlessCourseMod[c] & ENDLESS_BOON_MASK)  // don't flatten a mixed gambit into a plain single
			continue;
		int bits = 0;
		for (Uint64 x = h; x != 0; x &= x - 1)
			++bits;
		if (bits >= 2 && bits < bestBits) { bestBits = bits; best = c; }
	}
	if (best < 0)
		return;

	// Keep only the least-dangerous hostile bit (lowest reward) so it reads as the easy route.
	const Uint64 h = endlessCourseMod[best] & ENDLESS_HOSTILE_MASK;
	Uint64 keep = 0;
	int keepReward = 0x7fffffff;
	for (unsigned t = 0; t < COUNTOF(endlessModTable); ++t)
		if ((h & endlessModTable[t].bit) && endlessModTable[t].reward < keepReward)
		{
			keepReward = endlessModTable[t].reward;
			keep = endlessModTable[t].bit;
		}
	if (keep != 0)
		endlessCourseMod[best] = keep;
}

// Milestones keep the selected base levels but redeal their modifiers at fixed grades.
static void endlessDealMilestoneSlate(int milestone)
{
	const int lowRank = (milestone == 2) ? 8 : (milestone == 3) ? 6 : 7;  // S++ / S / S+  (see endlessDangerRankLevel)
	int lowN = 2 + (int)(endlessRand() % 2);       // minor/plain: 2 or 3 of the lower rung, the rest one higher
	if (milestone == 2)
		lowN = 2;                                  // grand: exactly 2 S++ (the roll above is still consumed,
		                                           // so the seed stream stays aligned with a plain milestone)
	if (lowN > endlessCourseCnt - 1)
		lowN = endlessCourseCnt - 1;               // short slate (too few distinct levels): keep both rungs present
	if (lowN < 1)
		lowN = 1;

	// Grand milestones pin The End before dealing two S++ and two S+++ routes.
	int first = 0;
	if (milestone == 2)
	{
		endlessCourseMod[0] = endlessMakeTheEndMods();
		first = 1;
	}
	for (int c = first, lowLeft = lowN; c < endlessCourseCnt; ++c)
	{
		const int rank = (lowLeft > 0) ? lowRank : lowRank + 1;
		if (lowLeft > 0)
			--lowLeft;
		// Build to the rank the course will DISPLAY (its level's baseDanger folded in), not the bare
		// modifier rank, so the slate's guaranteed grades hold after the level is attached and sorted.
		endlessCourseMod[c] = endlessMakeRankComboForLevel(rank, endlessCourseBaseDanger(c), endlessCourseMod, c);
	}
}

// Gravity courses may become omnidirectional. The choice is seeded and saved.
static void endlessRollGravityVariants(void)
{
	for (int c = 0; c < endlessCourseCnt; ++c)
		if ((endlessCourseMod[c] & ENDLESS_MOD_GRAVITY) && (endlessRand() % 2))
			endlessCourseMod[c] |= ENDLESS_MOD_GRAVITY_OMNI;
}

// The elite-tier rules, applied to the finished chart. RNG-free, so where they sit among the
// phases doesn't disturb the seed stream -- only that they run after every mod-adding step and
// before the sort, so the danger ordering, tier word, name and reward all reflect the real sector.
static void endlessEnforceEliteRules(void)
{
	// A boon that would be empty at this depth must not appear at all: scrub every locked bit from every
	// course as the final guarantee, in case a generation path above leaked one this shallow. The pick
	// sites already avoid emitting them here, so this normally does nothing.
	const Uint64 locked = endlessLockedBoons();
	if (locked != 0)
		for (int c = 0; c < endlessCourseCnt; ++c)
			endlessCourseMod[c] &= ~locked;

	// NOELITE (no elites or champions) supersedes NOCHAMP (no champions) -- the two must never ride one
	// sector, so strip the redundant NOCHAMP wherever both landed. The generators above never pair them on
	// purpose, but this makes the "can't have both" guarantee hold no matter how the bits were assembled.
	for (int c = 0; c < endlessCourseCnt; ++c)
		if ((endlessCourseMod[c] & ENDLESS_MOD_NOELITE) && (endlessCourseMod[c] & ENDLESS_MOD_NOCHAMP))
			endlessCourseMod[c] &= ~(Uint64)ENDLESS_MOD_NOCHAMP;

	// Deep runs: the natural elite share can climb past the 50% that "half enemies elite" (ELITEPACK)
	// pins it to, at which point ELITEPACK would CAP elites below the natural rate -- a stealth boon on
	// a danger course. Course 0 is normally clean, but an Ambush collapses its combo onto slot 0, so
	// scan from 0. (See endlessFixRedundantElitePack.)
	for (int c = 0; c < endlessCourseCnt; ++c)
		endlessFixRedundantElitePack(c);
}

// Stable-sort courses by displayed danger, then payout, without consuming RNG.
// Calm, boons, Cursed, and combat routes occupy separate ordering bands.
static int endlessDangerSortKey(Uint64 mods, int baseDanger)
{
	const int rank = endlessDangerRankLevelEx(mods, baseDanger);
	if (rank > 0)
		return rank + 2;                     // real combat danger (E..END), sorts from 3 upward
	if (mods & ENDLESS_MOD_CURSED)
		return 2;                            // economic catch: between the safe routes and combat danger
	return (mods == 0) ? 0 : 1;              // pinned Calm, then the pure boons
}

static int endlessCourseSortKey(int i)
{
	return endlessDangerSortKey(endlessCourseMod[i], endlessCourseBaseDanger(i));
}

// The payout the sort ranks equal-rank courses by. Off the course's OWN bits, NOT endlessCoursePayout:
// that one re-prices as the player buys buffs or queues Sabotage charges, and the slate's order is
// fixed once, here at generation.
static long endlessDangerSortPayout(Uint64 mods, int ep, int file)
{
	return endlessClearBonusForEx(mods, endlessLevelPayoutMille(ep, file, difficultyLevel));
}

static long endlessCourseSortPayout(int i)
{
	return endlessDangerSortPayout(endlessCourseMod[i], endlessCourseEp[i], endlessCourseFile[i]);
}

static void endlessSortCoursesByDanger(void)
{
	for (int i = 1; i < endlessCourseCnt; ++i)
	{
		const int      ep   = endlessCourseEp[i];
		const JE_byte  sec  = endlessCourseSec[i];
		const JE_byte  file = endlessCourseFile[i];
		const Uint64   mod  = endlessCourseMod[i];
		// The insertion element is out of the arrays during the shift below, so key it from the
		// captured (mod, ep, file) rather than endlessCourseSortKey(i).
		const int      key  = endlessDangerSortKey(mod, endlessLevelBaseDanger(ep, file, difficultyLevel));
		const long     pay  = endlessDangerSortPayout(mod, ep, file);
		int j = i - 1;
		while (j >= 0 && (endlessCourseSortKey(j) > key
		                  || (endlessCourseSortKey(j) == key && endlessCourseSortPayout(j) > pay)))
		{
			endlessCourseEp[j + 1]   = endlessCourseEp[j];
			endlessCourseSec[j + 1]  = endlessCourseSec[j];
			endlessCourseFile[j + 1] = endlessCourseFile[j];
			endlessCourseMod[j + 1]  = endlessCourseMod[j];
			--j;
		}
		endlessCourseEp[j + 1]   = ep;
		endlessCourseSec[j + 1]  = sec;
		endlessCourseFile[j + 1] = file;
		endlessCourseMod[j + 1]  = mod;
	}
}

// Salt generated labels until every course name in the visit is unique.
static void endlessMakeCourseNamesUnique(void)
{
	for (int c = 0; c < endlessCourseCnt; ++c)
		endlessCourseNameSalt[c] = 0;
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		for (int guard = 0; guard < 64; ++guard)
		{
			const char *name = endlessComboNameSalted(endlessCourseMod[c], endlessCourseNameSalt[c]);
			bool clash = false;
			for (int k = 0; k < c && !clash; ++k)
				if (strcmp(name, endlessComboNameSalted(endlessCourseMod[k], endlessCourseNameSalt[k])) == 0)
					clash = true;
			if (!clash)
				break;
			++endlessCourseNameSalt[c];
		}
	}
}

// Cache the authored base-level name behind each finalized course, so the Radar perk's per-frame
// help line (endlessCourseHelp) reads a string instead of re-parsing levels*.dat every frame. Runs
// after the danger sort (so index i matches the displayed order) and after any course restore, off
// the final (episode, section, file) each course actually launches. Empty for unused slots.
void endlessNameCourseBaseLevels(void)
{
	for (int i = 0; i < endlessCourseCnt && i < ENDLESS_MAX_COURSES; ++i)
		JE_getLevelSectionName(endlessCourseEp[i], endlessCourseSec[i], endlessCourseFile[i],
		                       endlessCourseBaseName[i], sizeof endlessCourseBaseName[i]);
	for (int i = endlessCourseCnt; i < ENDLESS_MAX_COURSES; ++i)
		endlessCourseBaseName[i][0] = '\0';
}

void endlessGenerateCourses(void)
{
	endlessCourseCnt = 0;

	const int milestone = endlessMilestoneKind();  // 0 ordinary zone, 1 S+/S++ slate, 2 S++/S+++ slate

	// This visit offers a random 2..5 course choices (fewer only if there aren't enough
	// distinct safe levels to fill them); a milestone always asks for the full slate.
	int wantCourses = 2 + (int)(endlessRand() % (ENDLESS_MAX_COURSES - 1));  // 2..5
	if (milestone)
		wantCourses = ENDLESS_MAX_COURSES;
	// STAR CHARTS, earned by clearing a sector that carried it: this visit deals the full route slate.
	// A milestone already does, and its slate is the point of the zone, so spending the charts there
	// would waste them -- hold them for the next ORDINARY visit instead. (An Ambush is the other visit
	// that can't honour them; that one is only decided further down, so it un-spends them at the end.)
	const bool spendStarCharts = endlessStarChartsOwed && !milestone;
	if (spendStarCharts)
	{
		wantCourses = ENDLESS_MAX_COURSES;
		endlessStarChartsOwed = false;
	}
	// Surveyor perk: chart extra routes. Added AFTER the RNG roll so the seed stream stays aligned;
	// clamped to the slate maximum (a milestone already asks for the full slate, so this is a no-op there).
	wantCourses += endlessPerkSurveyorRoutes();
	if (wantCourses > ENDLESS_MAX_COURSES)
		wantCourses = ENDLESS_MAX_COURSES;
	endlessGatherCourseLevels(wantCourses);

	int idx[COUNTOF(endlessHostileThemes)];
	endlessShuffleThemeOrder(idx);

	const int dangerRamp = endlessDangerRamp();  // 0 (z40) -> 100 (z100) -> 500 (z250 cap) -- deep-run danger tilt

	endlessDealHostileThemes(idx);
	endlessWidenHostileCombos(dangerRamp);
	endlessDealBoonCourse(dangerRamp);
	endlessGraftGambits(dangerRamp);
	endlessInjectRareSectors();
	endlessDedupeCourseMods(idx);

	// Rare whole-visit flavors are mutually exclusive.
	// Jackpot (~1/22) all boons; Ambush (~1/20) one forced dangerous sector; Gauntlet (~1/7) all
	// hostile. All three dice roll up front UNCONDITIONALLY so the seed stream stays aligned;
	// precedence Jackpot > Ambush > Gauntlet; none fire at depth 0. The danger ramp tilts the odds
	// deep, but Gauntlet/Ambush are hard-capped below certainty (the CAP_PCT knobs).
	int gauntletPct = 14 + dangerRamp * 11 / 100;  // ~1/7 early -> 25% by the mid -> capped
	if (gauntletPct > ENDLESS_DANGER_GAUNTLET_CAP_PCT)
		gauntletPct = ENDLESS_DANGER_GAUNTLET_CAP_PCT;
	int ambushPct = 5 + dangerRamp * 4 / 100;      // ~1/20 early -> ~9% by the mid -> capped
	if (ambushPct > ENDLESS_DANGER_AMBUSH_CAP_PCT)
		ambushPct = ENDLESS_DANGER_AMBUSH_CAP_PCT;
	const bool jackpotRoll  = ((endlessRand() % (22 + dangerRamp * 22 / 100)) == 0);  // ~1/22 -> 1/44 (mid) -> ~1/99 (cap) -- small bump to offset the extra milestone danger zones
	const bool gauntletRoll = ((int)(endlessRand() % 100) < gauntletPct);
	const bool ambushRoll   = ((int)(endlessRand() % 100) < ambushPct);
	// A milestone zone deals its own fixed slate below, so none of the three may fire there (an
	// Ambush would collapse the visit to one course). The dice are still rolled above, so the seed
	// stream stays aligned whether or not this zone is a milestone.
	const bool doJackpot  = jackpotRoll && (endlessRunDepth > 0) && !milestone;
	const bool doAmbush   = !doJackpot && ambushRoll && (endlessRunDepth > 0) && !milestone;
	const bool doGauntlet = !doJackpot && !doAmbush && gauntletRoll && (endlessRunDepth > 0) && !milestone;

	if (doJackpot)
		endlessDealJackpot();
	else if (doGauntlet)
		endlessDealGauntlet(idx);

	endlessForced = doAmbush;
	if (endlessForced)
	{
		endlessDealAmbush();
		// An Ambush collapses the visit to one forced sector, so the full slate Star Charts bought
		// never appears. Hand them back rather than eating them -- the boon promises a real choice, and
		// this visit had none to give.
		if (spendStarCharts)
			endlessStarChartsOwed = true;
	}

	endlessEnsureLegibleChoice();
	if (milestone)
		endlessDealMilestoneSlate(milestone);

	endlessRollGravityVariants();
	endlessEnforceEliteRules();
	endlessSortCoursesByDanger();
	endlessMakeCourseNamesUnique();
	endlessNameCourseBaseLevels();  // cache each course's base-level name for the Radar perk (after the sort)
}

// Resolve a saved/chosen (episode, section) back to a real endless-safe level file. Prefer the
// exact persisted file when present; v7 and older saves only have the section, so use its first
// safe match. Returning false means the script entry has no corresponding binary level data.
bool endlessResolveCourseFile(int ep, JE_byte sec, JE_byte requestedFile, JE_byte *resolvedFile)
{
	JE_byte secs[64], files[64];
	const uint n = JE_getLevelSections(ep, secs, files, COUNTOF(secs));
	JE_byte firstMatch = 0;
	for (uint i = 0; i < n; ++i)
	{
		if (secs[i] != sec)
			continue;
		if (firstMatch == 0)
			firstMatch = files[i];
		if (requestedFile != 0 && files[i] == requestedFile)
		{
			*resolvedFile = files[i];
			return true;
		}
	}
	if (firstMatch == 0)
		return false;
	*resolvedFile = firstMatch;
	return true;
}

int endlessCourseCount(void)
{
	return endlessCourseCnt;
}

const char *endlessCourseName(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		return "";
	if (endlessForced && i == 0)
		return "Ambush!";
	return endlessComboNameSalted(endlessCourseMod[i], endlessCourseNameSalt[i]);
}

// The help line is a short RISK SUMMARY only -- the itemized threats/boons are drawn on the
// planet monitor itself (endlessCourseModRows + game_menu.c's overlay), so the old generated
// "A, B and more" listing would just duplicate them.
const char *endlessCourseHelp(int i)
{
	static char buf[80];
	if (i < 0 || i >= endlessCourseCnt)
		return "";
	if (endlessForced && i == 0)
	{
		snprintf(buf, sizeof buf, "Ambush! %s - no way around it",
		         endlessDangerTierEx(endlessCourseMod[0], endlessCourseBaseDanger(0)));
	}
	else
	{
		const Uint64 mods = endlessCourseMod[i];
		const int    bd   = endlessCourseBaseDanger(i);   // the shipped level's own danger (hostile courses only)
		// The help line shows the tier; the monitor renders the letter grade separately.
		if (endlessDangerScoreEx(mods, bd) == 0)
			snprintf(buf, sizeof buf, "%s", (mods == 0) ? "Calm: clear skies ahead" : "Boon: no danger here");
		else
			snprintf(buf, sizeof buf, "Danger: %s", endlessDangerTierEx(mods, bd));
	}
	// Radar perk: reveal the shipped level behind this sector, right after the tier read.
	if (endlessPerkRadarActive() && endlessCourseBaseName[i][0] != '\0')
	{
		const size_t len = strlen(buf);
		snprintf(buf + len, sizeof buf - len, " (%s)", endlessCourseBaseName[i]);
	}
	return buf;
}

// The highlighted course's letter danger grade (F easiest .. S+++ hardest) for the monitor's
// RANK field -- moved off the help line so it reads ON the planet monitor. Delegates to the
// same endlessDangerRank/score/thresholds as the "Danger:" word, so the two never disagree.
const char *endlessCourseRank(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		return "";
	return endlessDangerRankEx(endlessCourseMod[i], endlessCourseBaseDanger(i));
}

// Numeric danger level 0 (F) .. 9 (S+++) for course i, or -1 if out of range. The monitor uses
// it to pick the letter's green->red tint; it maps the same letter endlessCourseRank returns.
int endlessCourseRankLevel(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		return -1;
	return endlessDangerRankLevelEx(endlessCourseMod[i], endlessCourseBaseDanger(i));
}

// The highlighted course's individual modifiers for the monitor overlay, worst-first. The
// weight is the modifier's endlessModTable reward (the same tenths endlessDangerScore sums),
// so the overlay's darkest-red tints land on exactly the bits that drive the tier word.
// Cursed lists on the hostile side -- it's a trap, and deep red is the right warning.
int endlessCourseModRows(int i, EndlessCourseModRow *rows, int max)
{
	if (i < 0 || i >= endlessCourseCnt)
		return 0;
	// The purchase-folded set, so the monitor never lists a threat already paid to override. (The
	// course NAME and RANK stay charted -- that is the sector's seed-determined grade.) A Sabotage
	// charge is shown differently: its bits STAY listed, flagged `cleansed`, so the card reads as
	// "this threat was here and your charge takes it off" rather than quietly shrinking.
	Uint64 cleansed = 0;
	const Uint64 mods = endlessCourseLaunchMods(i, &cleansed);
	int n = 0;
	for (unsigned t = 0; t < COUNTOF(endlessModTable) && n < max; ++t)
	{
		if ((mods & endlessModTable[t].bit) == 0)
			continue;
		if (endlessModTable[t].word == NULL)
			continue;  // label-only bit (the finale marker): it pays and ranks, but lists no threat
		rows[n].word     = endlessModTable[t].word;
		rows[n].weight   = endlessModTable[t].reward;
		rows[n].hostile  = (endlessModTable[t].bit & (ENDLESS_HOSTILE_MASK | ENDLESS_MOD_CURSED)) != 0;
		rows[n].cleansed = (endlessModTable[t].bit & cleansed) != 0;
		// An omnidirectional well doesn't pull DOWN, so relabel the gravity row to say so (OMNI has no
		// row of its own -- it rides the gravity bit and only changes the pull's direction).
		if (endlessModTable[t].bit == ENDLESS_MOD_GRAVITY && (mods & ENDLESS_MOD_GRAVITY_OMNI))
			rows[n].word = "pull any direction";
		++n;
	}
	// Show Overclock/Overload scroll speed as a separate display-only row.
	if (n < max && (mods & ENDLESS_MOD_OVERLOAD) && !(mods & ENDLESS_MOD_WARP))
	{
		rows[n].word     = "much faster scrolling";
		rows[n].weight   = 14;  // deep-red tint band (>=14, same as Warp's 20); sorts below Overload's own 30 attack row
		rows[n].hostile  = true;
		rows[n].cleansed = (cleansed & ENDLESS_MOD_OVERLOAD) != 0;  // the scroll goes with the bit that caused it
		++n;
	}
	else if (n < max && (mods & ENDLESS_MOD_OVERCLOCK) && !(mods & (ENDLESS_MOD_SLIPSTREAM | ENDLESS_MOD_WARP)))
	{
		rows[n].word     = "faster scrolling";
		rows[n].weight   = 9;  // pale-red tint band (<10, same as Slipstream's 6); sorts below Overclock's own 16 attack row
		rows[n].hostile  = true;
		rows[n].cleansed = (cleansed & ENDLESS_MOD_OVERCLOCK) != 0;
		++n;
	}
	for (int a = 1; a < n; ++a)  // insertion sort, worst first; n is tiny
	{
		const EndlessCourseModRow key = rows[a];
		int b = a - 1;
		while (b >= 0 && rows[b].weight < key.weight)
		{
			rows[b + 1] = rows[b];
			--b;
		}
		rows[b + 1] = key;
	}
	return n;
}

JE_byte endlessCoursePlanet(int i)
{
	// Distinct valid star-map planets (1..21) so the monitor shows a different world per
	// course. Purely cosmetic.
	static const JE_byte planets[ENDLESS_MAX_COURSES] = { 4, 9, 13, 17, 21 };
	return planets[(i < 0 || i >= ENDLESS_MAX_COURSES) ? 0 : i];
}

JE_byte endlessCourseSection(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		i = 0;
	return endlessCourseSec[i];
}

JE_byte endlessSelectCourse(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		i = 0;

	// Revalidate at the launch boundary too. This is the last line of defense for legacy saves or
	// externally edited/corrupt sidecars: substitute a safe level instead of handing JE_loadMap an
	// invalid section and terminating the game.
	JE_byte resolvedFile;
	if (endlessResolveCourseFile(endlessCourseEp[i], endlessCourseSec[i], endlessCourseFile[i], &resolvedFile))
	{
		endlessCourseFile[i] = resolvedFile;
	}
	else
	{
		int fallbackEp;
		JE_byte fallbackSec, fallbackFile;
		if (!endlessRandomSafeLevel(&fallbackEp, &fallbackSec, &fallbackFile))
		{
			fprintf(stderr, "error: no valid Endless level is available\n");
			forcedLvlFileNum = 0;
			return FIRST_LEVEL;
		}
		fprintf(stderr, "warning: replacing invalid saved course episode %d section %u\n",
		        endlessCourseEp[i], (unsigned int)endlessCourseSec[i]);
		endlessCourseEp[i] = fallbackEp;
		endlessCourseSec[i] = fallbackSec;
		endlessCourseFile[i] = fallbackFile;
	}

	// Save one-shot purchases so a normal mid-zone bail can restore them.
	endlessSortiePrePurchased = endlessPurchasedMods;
	endlessSortiePreCleanse   = endlessCleanseChargeCount;
	endlessSortiePreLongCon   = endlessLongCon;

	if (endlessCourseEp[i] != episodeNum)
		JE_initEpisode(endlessCourseEp[i]);  // load that episode's data (arsenal is shared)
	forcedLvlFileNum = endlessCourseFile[i];  // load this course's exact level file (see JE_loadMap)
	// Fold in the E-Shop buffs. The purchase wins any kill-fire collision and NOELITE supersedes
	// NOCHAMP; both rules live in the shared helper so the course card priced exactly this set.
	// endlessCourseLaunchMods replays THIS pass and the Sabotage one below to price and colour the
	// card -- keep the two in step, or the monitor promises a sector the sortie doesn't fly.
	endlessActiveMods = endlessFoldPurchasedMods(endlessCourseMod[i], endlessPurchasedMods);
	endlessPurchasedMods = 0;                                        // consumed by this sector
	for (int c = 0; c < endlessCleanseChargeCount; ++c)  // Sabotage: strip the worst hostile bit per charge
		endlessActiveMods = endlessStripWorstMod(endlessActiveMods);
	endlessCleanseChargeCount = 0;
	// The Long Con: an APEX ambush the player gambled for and forgot, added after the cleanse pass so
	// no queued sabotage charge can strip it -- you paid to not see this coming, and you don't.
	if (endlessLongCon > 0 && --endlessLongCon == 0)
		endlessActiveMods |= ENDLESS_MOD_APEX;
	endlessLastEp = endlessCourseEp[i];
	endlessLastSec = endlessCourseSec[i];
	return endlessCourseSec[i];
}
