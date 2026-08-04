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

// Fold the shipped level's intrinsic danger into its grade, sorting, and payout.
static int endlessCourseBaseDanger(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		return 0;
	return endlessLevelBaseDanger(endlessCourseEp[i], endlessCourseFile[i], difficultyLevel);
}

// Reproduce the purchase and Sabotage passes used at launch. Report stripped bits through
// `cleansedOut`; The Long Con remains hidden until launch.
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

// Price the launch-time modifier set plus the level profile adjustment, matching the banked payout.
long endlessCoursePayout(int i)
{
	if (i < 0 || i >= endlessCourseCnt)
		return 0;
	Uint64 cleansed = 0;
	const Uint64 mods = endlessCourseLaunchMods(i, &cleansed);
	return endlessClearBonusForEx(mods & ~cleansed,
	                              endlessLevelPayoutMille(endlessCourseEp[i], endlessCourseFile[i], difficultyLevel));
}

// Pick a boon that does not cancel a hostile effect. At least three candidates are unconditional.
static Uint64 endlessPickMixBoon(Uint64 hostiles)
{
	// Mixed courses may receive Turbodrive or Overblast; Overdrive stays a pure boon or shop effect.
	if (endlessRand() % 100 < 4)
		return (endlessRand() % 2) ? ENDLESS_MOD_TURBODRIVE : ENDLESS_MOD_OVERBLAST;

	// Elite-tier boons use a gated roll and cannot cancel the course's hostile tier.
	if ((endlessRand() % 100 < 6) && endlessEliteBoonsUnlocked())
	{
		if ((endlessRand() % 3) == 0)  // one-third targets the stronger NOELITE boon
		{
			if (!(hostiles & (ENDLESS_MOD_ELITEPACK | ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION)))
				return ENDLESS_MOD_NOELITE;
		}
		else if (!(hostiles & ENDLESS_MOD_LEGION))
			return ENDLESS_MOD_NOCHAMP;
	}

	Uint64 cand[14];   // 3 always-safe + 6 new unconditional + Flak + the 2 elite-stat + Fragile + Dilation
	int n = 0;
	cand[n++] = ENDLESS_MOD_OVERCHARGE;   // more player damage
	cand[n++] = ENDLESS_MOD_BOUNTY;       // cash only
	cand[n++] = ENDLESS_MOD_FAVOR;        // cheaper next shop
	// These boons act on systems untouched by ordinary hostile bits.
	cand[n++] = ENDLESS_MOD_AEGIS;        // the shield can't be punched through
	cand[n++] = ENDLESS_MOD_LOWPROFILE;   // a quarter off the hitbox
	cand[n++] = ENDLESS_MOD_AUXREACTOR;   // free shield recharge
	cand[n++] = ENDLESS_MOD_SOFTLANDING;  // ramming stops being lethal
	cand[n++] = ENDLESS_MOD_SHOCKWAVE;    // elite kills clear the air
	cand[n++] = ENDLESS_MOD_STARCHARTS;   // no combat effect
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

// Preserve canonical boon bitsets and swap Turbodrive/Overblast only during generation.
// Applying the swap twice restores the original set.
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
	// Giant Killer and Clean Signals both affect special enemies but modify independent stats.
	// Breakthrough has a separate gated roll.
	pool[poolN++] = ENDLESS_MOD_AEGIS;
	pool[poolN++] = ENDLESS_MOD_LOWPROFILE;
	pool[poolN++] = ENDLESS_MOD_AUXREACTOR;
	pool[poolN++] = ENDLESS_MOD_SOFTLANDING;
	pool[poolN++] = ENDLESS_MOD_SHOCKWAVE;
	pool[poolN++] = ENDLESS_MOD_STARCHARTS;
	if (endlessTideBoonsUnlocked())         // Flak Screen applies only after tides begin
		pool[poolN++] = ENDLESS_MOD_FLAKSCREEN;
	if (endlessEliteBoonsUnlocked())        // elite boons require a meaningful elite share
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
#define ENDLESS_DANGER_RAMP_MID_SCALE  100  // preserves the existing ramp through the midpoint
#define ENDLESS_DANGER_RAMP_FULL_SCALE 500  // cap for uncapped deep-run adjustments
#define ENDLESS_DANGER_GAUNTLET_CAP_PCT 45  // leaves a non-Gauntlet route possible
#define ENDLESS_DANGER_AMBUSH_CAP_PCT   15  // ceiling on the one-forced-danger Ambush chance

// Ramp from zero at START through MID_SCALE to FULL_SCALE.
static int endlessDangerRamp(void)
{
	if (!endlessMode)
		return 0;
	const int zone = endlessDifficultyZone();
	if (zone <= ENDLESS_DANGER_RAMP_START)
		return 0;
	if (zone <= ENDLESS_DANGER_RAMP_MID)  // first stage reaches MID_SCALE
		return ENDLESS_DANGER_RAMP_MID_SCALE * (zone - ENDLESS_DANGER_RAMP_START)
		         / (ENDLESS_DANGER_RAMP_MID - ENDLESS_DANGER_RAMP_START);
	// The second stage reaches FULL_SCALE and then remains capped.
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
	// Homing is the mild tier and does not add ram damage. Its chance is capped.
	RARE_FROM(26, endlessHomingThemes, true),
	// Kamikaze is the moderate homing tier; the stronger rammer is a Rampage gamble outcome.
	// This row follows Homing so the harder tier wins a slot collision.
	RARE_FROM(55, endlessKamikazeThemes, true),
	// Overload: Overclock cranked way up.
	RARE_FROM(17, endlessOverloadThemes, true),
	// Warp Speed is a separate high-scroll threat.
	RARE_FIXED(15, ENDLESS_MOD_WARP, true),
	// Hostile Turbodrive and Overdrive turn kill streaks into jammed guns and
	// for Evil Overdrive weaker shots too. One roll feeds all three mirrors, so the base rarity is
	// the frequency of "some evil sector"; each individual bit lands at about a third of it.
	RARE_FROM(12, endlessEvilThemes, true),
	// Reactor Redline quickens guns on kills while applying Overheat damage.
	RARE_FROM(50, endlessRedlineThemes, true),
	// Tar Pit (SLUGGISH + GRAVITY): the ship crawls WHILE dragged down. Brutal but always flyable
	// (endlessGravityDrift slows the pull with the ship), so this one keeps the full ramp. SLUGGISH
	// stays out of the combinable pool, so this is the sole source of the pairing.
	RARE_FROM(28, endlessSluggishThemes, false),
	// Apex Swarm (every enemy elite), from the Apex-tier rare themes: bare Apex, or Apex plus one
	// extra danger. Late enough to override a boon slot.
	RARE_PICK(26, endlessRareThemes, ENDLESS_MOD_APEX, ENDLESS_MOD_LEGION, true),
	// Legion makes every enemy a champion and remains one of the rarest sectors.
	RARE_PICK(53, endlessRareThemes, ENDLESS_MOD_LEGION, 0, true),
	// Cataclysm combines several dangers without an elite tier.
	// The rare themes carrying neither Apex nor Legion (the 5+-danger pure combos).
	RARE_PICK(50, endlessRareThemes, 0, ENDLESS_MOD_APEX | ENDLESS_MOD_LEGION, true),
	// Dead Generator (DEADGEN): no shield regen AND a power-starved main gun. The nastiest
	// handicap in the game, so it is the rarest and is rolled last. It claims the slot when it
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
		return;                               // 100 percent is still an increase
	if (endlessNaturalEliteChancePercent() <= 50)
		return;                               // Elite Pack still increases the share

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
			continue;                         // already present
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
	endlessCourseMod[c] = fallback;           // use the final valid fallback
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

// Movement-wide hazards are suppressed here because the milestone pool is otherwise flat.
// Add each bit here and assign it a low weight in the ordinary pool.
static const struct { Uint64 bit; int oneInN; } endlessScarceMods[] = {
	{ ENDLESS_MOD_TOPSY,   3 },
	{ ENDLESS_MOD_GRAVITY, 3 },
};

// Read a bit's danger weight from the same table used by the rank bands.
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
	lo -= baseDanger;  // offset the level contribution while building modifier-only scores
	hi -= baseDanger;  // final rank validation still includes level danger and synergy
	if (hi < 1) hi = 1;
	if (lo > hi) lo = hi;
	if (lo < 1) lo = 1;

	Uint64 best = 0;   // first buildable combo, an absolute fallback if no attempt lands the exact rank
	Uint64 exact = 0;  // first combo whose displayed rank matches
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
			continue;  // final displayed rank missed the requested band

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
	// Core dangers.
	{ ENDLESS_MOD_FORTIFIED,   3 },
	{ ENDLESS_MOD_FRENZY,      3 },
	{ ENDLESS_MOD_SWIFT,       3 },
	{ ENDLESS_MOD_DEVASTATING, 3 },
	// Mid-frequency dangers.
	{ ENDLESS_MOD_ENRAGE,      4 },
	{ ENDLESS_MOD_OVERCLOCK,   4 },
	{ ENDLESS_MOD_ELITEPACK,   4 },
	// Lower-frequency variety.
	{ ENDLESS_MOD_STATIC,      6 },  // a generic damage-punish tax; never pairs the injected-only DEADGEN
	{ ENDLESS_MOD_SHIELDLESS,  5 },  // defense-only debuff
	{ ENDLESS_MOD_RETALIATION, 5 },  // kill-tempo tax, promoted out of injection-only
	// Promoted rare modifiers retain modest weights.
	{ ENDLESS_MOD_MARTYRDOM,   4 },
	{ ENDLESS_MOD_SEEKER,      4 },
	// Scarce movement-wide hazards.
	{ ENDLESS_MOD_TOPSY,       3 },
	{ ENDLESS_MOD_GRAVITY,     3 },
};

// Mask of dangers eligible for ordinary combinations.
static Uint64 endlessCombinableMask(void)
{
	Uint64 mask = 0;
	for (unsigned k = 0; k < COUNTOF(endlessCombinableMods); ++k)
		mask |= endlessCombinableMods[k].bit;
	return mask;
}

// Draw `want` distinct bits with weights. Bits already used by another route receive one-third weight.
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

// Return hostile bits already used by routes other than `forCourse`.
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
	// Scarce signatures remain limited even though curated themes can contain them.
	{ ENDLESS_MOD_TOPSY,        5 },
	{ ENDLESS_MOD_GRAVITY,      3 },
	{ ENDLESS_MOD_STATIC,       9 },
	{ ENDLESS_MOD_RETALIATION,  9 },
	{ ENDLESS_MOD_SLIPSTREAM,   8 },  // omitted from the ordinary combination pool
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

// Shuffle the exhaustive fallback order shared by initial deals, duplicate replacement, and Gauntlet.
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

// Keep course zero clean and draw distinct hostile signatures for the remaining routes.
static void endlessDealHostileThemes(const int *idx)
{
	endlessCourseMod[0] = 0;
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		const Uint64 m = endlessPickSignatureTheme(c);
		endlessCourseMod[c] = (m != 0) ? m : endlessHostileThemes[idx[c - 1]].mods;
	}
}

// Replace some hostile routes with weighted one-to-four-bit combinations.
static void endlessWidenHostileCombos(int dangerRamp)
{
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		// Increase random combinations with depth while retaining some curated themes.
		int comboShare = 50 + dangerRamp * 20 / 100;
		if (comboShare > 80)
			comboShare = 80;
		if (endlessRand() % 100 >= comboShare)
			continue;
		// The ramp raises the typical bit count from about 2.2 to 3.4, capped at four.
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

// Do not chart Breakthrough when the same clear already awards a guaranteed perk.
// The outpost can present one perk pick at a time.
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
			// Exclude boons whose affected systems are not active at this depth.
			endlessCourseMod[slot] = endlessSwapTurbodriveOverblast(
				endlessPickThemeMods(endlessBoonThemes, COUNTOF(endlessBoonThemes), 0, endlessLockedBoons()));
	}
}

// Add one compatible boon to some ordinary hostile routes.
// Rare signatures and pure boon routes keep their identity.
static void endlessGraftGambits(int dangerRamp)
{
	// Named Slipstream sectors remain boon-graft eligible despite its pool exclusion.
	const Uint64 mixCommon = endlessCombinableMask() | ENDLESS_MOD_SLIPSTREAM;
	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		const Uint64 h = endlessCourseMod[c] & ENDLESS_HOSTILE_MASK;
		if (h == 0 || (h & ~mixCommon) != 0)       // ordinary hostile courses only
			continue;
		if (endlessCourseMod[c] & ENDLESS_BOON_MASK) // already a gambit / carries a boon
			continue;
		int gambitPct = 35 - dangerRamp * 20 / 100;  // falls from about 35 percent to a 5 percent floor
		if (gambitPct < 5)
			gambitPct = 5;
		if (endlessRand() % 100 >= gambitPct)  // fewer mitigations grafted onto hostiles as the run deepens
			continue;
		endlessCourseMod[c] |= endlessPickMixBoon(h);
	}
}

// Roll rare injections in table order; the last successful row wins a contested slot.
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

// Jackpot deals distinct pure-boon themes and excludes Cursed or depth-locked entries.
static void endlessDealJackpot(void)
{
	// Breakthrough is absent from endlessBoonThemes, so Jackpot cannot award a perk pick.
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

// Gauntlet replaces non-hostile routes with distinct hostiles and preserves existing hostile themes.
// It consumes no RNG.
static void endlessDealGauntlet(const int *idx)
{
	for (int c = 0; c < endlessCourseCnt; ++c)
	{
		if (endlessCourseMod[c] & ENDLESS_HOSTILE_MASK)
			continue;  // preserve existing danger
		const Uint64 m = endlessUnusedHostileTheme(idx, c);
		if (m != 0)
			endlessCourseMod[c] = m;
	}
}

// Ambush collapses the visit to one forced Homing sector using course zero's level.
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
		ENDLESS_MOD_HOMING | ENDLESS_MOD_DEVASTATING,   // moderate homing without ram damage
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

// A full slate retains one single-danger hostile route. Thin the mildest ordinary route if needed.
static void endlessEnsureLegibleChoice(void)
{
	if (endlessCourseCnt < 4)
		return;

	for (int c = 1; c < endlessCourseCnt; ++c)
	{
		const Uint64 h = endlessCourseMod[c] & ENDLESS_HOSTILE_MASK;
		if (h != 0 && (h & (h - 1)) == 0)   // already a single hostile bit
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

	// Keep the lowest-weight hostile bit.
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

	// Grand milestones pin The End before dealing the remaining fixed-grade routes.
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
		// Include the level's baseDanger so the displayed grade matches the requested rank.
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

// Apply elite-tier normalization after modifier generation and before sorting, without RNG.
static void endlessEnforceEliteRules(void)
{
	// Remove any depth-locked boon that escaped the generation gates.
	const Uint64 locked = endlessLockedBoons();
	if (locked != 0)
		for (int c = 0; c < endlessCourseCnt; ++c)
			endlessCourseMod[c] &= ~locked;

	// NOELITE supersedes NOCHAMP.
	for (int c = 0; c < endlessCourseCnt; ++c)
		if ((endlessCourseMod[c] & ENDLESS_MOD_NOELITE) && (endlessCourseMod[c] & ENDLESS_MOD_NOCHAMP))
			endlessCourseMod[c] &= ~(Uint64)ENDLESS_MOD_NOCHAMP;

	// Remove Elite Pack when it would cap elites below the natural deep-run rate.
	// Scan slot zero because Ambush can place a hostile course there.
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

// Sort against generated course bits. Purchases and Sabotage must not reorder the saved slate.
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
	// Star Charts expands an ordinary visit. Milestones preserve the charge for the next visit.
	const bool spendStarCharts = endlessStarChartsOwed && !milestone;
	if (spendStarCharts)
	{
		wantCourses = ENDLESS_MAX_COURSES;
		endlessStarChartsOwed = false;
	}
	// Add Surveyor routes after the RNG roll and clamp to the slate maximum.
	wantCourses += endlessPerkSurveyorRoutes();
	if (wantCourses > ENDLESS_MAX_COURSES)
		wantCourses = ENDLESS_MAX_COURSES;
	endlessGatherCourseLevels(wantCourses);

	int idx[COUNTOF(endlessHostileThemes)];
	endlessShuffleThemeOrder(idx);

	const int dangerRamp = endlessDangerRamp();  // 0 at zone 40, 100 at zone 100, 500 at zone 250

	endlessDealHostileThemes(idx);
	endlessWidenHostileCombos(dangerRamp);
	endlessDealBoonCourse(dangerRamp);
	endlessGraftGambits(dangerRamp);
	endlessInjectRareSectors();
	endlessDedupeCourseMods(idx);

	// Rare whole-visit flavors are mutually exclusive.
	// Roll all visit flavors up front to preserve the seed stream. Precedence is Jackpot, Ambush,
	// then Gauntlet; none apply at depth zero, and danger flavors remain capped below certainty.
	int gauntletPct = 14 + dangerRamp * 11 / 100;  // about 14 percent early, 25 percent at midpoint
	if (gauntletPct > ENDLESS_DANGER_GAUNTLET_CAP_PCT)
		gauntletPct = ENDLESS_DANGER_GAUNTLET_CAP_PCT;
	int ambushPct = 5 + dangerRamp * 4 / 100;      // about 5 percent early, 9 percent at midpoint
	if (ambushPct > ENDLESS_DANGER_AMBUSH_CAP_PCT)
		ambushPct = ENDLESS_DANGER_AMBUSH_CAP_PCT;
	const bool jackpotRoll  = ((endlessRand() % (22 + dangerRamp * 22 / 100)) == 0);  // falls from about 1/22 to 1/99
	const bool gauntletRoll = ((int)(endlessRand() % 100) < gauntletPct);
	const bool ambushRoll   = ((int)(endlessRand() % 100) < ambushPct);
	// Milestones ignore visit flavors after rolling them so the seed stream remains aligned.
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
		// Ambush preserves Star Charts because it cannot present the expanded slate.
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

// The help line gives only a risk summary; the planet monitor lists individual modifiers.
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

// Return the highlighted course grade for the monitor, using the shared danger thresholds.
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

// Return modifier rows in descending danger weight. Cursed appears on the hostile side.
int endlessCourseModRows(int i, EndlessCourseModRow *rows, int max)
{
	if (i < 0 || i >= endlessCourseCnt)
		return 0;
	// Purchases hide overridden threats. Sabotage keeps rows visible and marks them cleansed.
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
		// OMNI relabels the gravity row because it changes only pull direction.
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

	// Substitute a safe level if a legacy or corrupt sidecar names an invalid section.
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
	// Apply the same purchase and Sabotage passes used to price and color the course card.
	endlessActiveMods = endlessFoldPurchasedMods(endlessCourseMod[i], endlessPurchasedMods);
	endlessPurchasedMods = 0;                                        // consumed by this sector
	for (int c = 0; c < endlessCleanseChargeCount; ++c)  // Sabotage: strip the worst hostile bit per charge
		endlessActiveMods = endlessStripWorstMod(endlessActiveMods);
	endlessCleanseChargeCount = 0;
	// Add The Long Con's Apex ambush after cleansing so Sabotage cannot remove it.
	if (endlessLongCon > 0 && --endlessLongCon == 0)
		endlessActiveMods |= ENDLESS_MOD_APEX;
	endlessLastEp = endlessCourseEp[i];
	endlessLastSec = endlessCourseSec[i];
	return endlessCourseSec[i];
}
