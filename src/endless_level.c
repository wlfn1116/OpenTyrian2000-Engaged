/* Endless base-level selection, music, and per-zone setup. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "game_menu.h"
#include "joystick.h"
#include "loudness.h"
#include "lvlmast.h"
#include "mtrand.h"
#include "player.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Saved for exact music anti-repeat behavior after a resume.
JE_byte endlessLastSong      = 0;
int     endlessLastSongDepth = -1;

// Current and previous shipped levels, exposed to the crash log.
char endlessBaseName[11]     = "";
int  endlessBaseEp           = 0;
int  endlessBaseLvl          = 0;
char endlessPrevBaseName[11] = "";
int  endlessPrevBaseEp       = 0;
int  endlessPrevBaseLvl      = 0;

// Recent base levels, newest first, used to avoid repeats.
int     endlessRecentEp[ENDLESS_LEVEL_HISTORY];
JE_byte endlessRecentSec[ENDLESS_LEVEL_HISTORY];
int     endlessRecentCount;

// Do not add the same level twice during a locked-sortie relaunch.
static void endlessRecordRecentLevel(int ep, int sec)
{
	if (endlessRecentCount > 0 && endlessRecentEp[0] == ep && endlessRecentSec[0] == (JE_byte)sec)
		return;
	for (int i = ENDLESS_LEVEL_HISTORY - 1; i > 0; --i)
	{
		endlessRecentEp[i]  = endlessRecentEp[i - 1];
		endlessRecentSec[i] = endlessRecentSec[i - 1];
	}
	endlessRecentEp[0]  = ep;
	endlessRecentSec[0] = (JE_byte)sec;
	if (endlessRecentCount < ENDLESS_LEVEL_HISTORY)
		++endlessRecentCount;
}

// Search the newest `window` entries.
static bool endlessLevelInRecent(int ep, JE_byte sec, int window)
{
	if (window > endlessRecentCount)
		window = endlessRecentCount;
	for (int i = 0; i < window; ++i)
		if (endlessRecentEp[i] == ep && endlessRecentSec[i] == sec)
			return true;
	return false;
}

void endlessPreloadBanks(void)
{
	// Load the initial enemy sprite banks before the first spawn.
	for (int i = 0; i < maxEvent; ++i)
	{
		if (eventRec[i].eventtype != 5)
			continue;

		const int banks[4] = {
			eventRec[i].eventdat,  eventRec[i].eventdat2,
			eventRec[i].eventdat3, eventRec[i].eventdat4,
		};
		for (int s = 0; s < 4; ++s)
		{
			const int b = banks[s];
			if (b > 0 && b <= 36 && enemySpriteSheetIds[s] != (Uint8)b)
			{
				JE_loadCompShapes(&enemySpriteSheets[s], shapeFile[b - 1]);
				enemySpriteSheetIds[s] = (Uint8)b;
			}
		}
		break;
	}
}

// The Endless-safe levels of every installed episode, in episode and section order. That order is
// what the bag shuffles, so it has to depend on the install alone.
typedef struct { int ep; JE_byte sec, file; } EndlessLevelPoolEntry;
#define ENDLESS_LEVEL_POOL_MAX (EPISODE_MAX * 64)

static int endlessBuildLevelPool(EndlessLevelPoolEntry *pool, int max)
{
	int npool = 0;
	for (int e = 1; e <= EPISODE_MAX && npool < max; ++e)
	{
		if (!episodeAvail[e - 1])
			continue;
		JE_byte secs[64], files[64];
		const uint n = JE_getLevelSections(e, secs, files, COUNTOF(secs));
		for (uint i = 0; i < n && npool < max; ++i)
		{
			pool[npool].ep   = e;
			pool[npool].sec  = secs[i];
			pool[npool].file = files[i];
			++npool;
		}
	}
	return npool;
}

/* Salt of the shuffle's own stream. Above every depth-keyed phase salt (endlessZonePhaseSalt), so
 * a bag's shuffle cannot collide with a zone's draws. */
#define ENDLESS_SHUFFLE_SALT 0x2000000000000000ULL

// One bagful: the pool permuted from the run seed and the refill count alone.
static void endlessShuffleOrder(int *order, int npool, Uint64 refill)
{
	Uint64 state = endlessSplitMixSeed(ENDLESS_SHUFFLE_SALT + refill);
	for (int i = 0; i < npool; ++i)
		order[i] = i;
	for (int i = npool - 1; i > 0; --i)
	{
		const int j = (int)(endlessSplitMixStep(&state) % (Uint32)(i + 1));
		const int t = order[i];
		order[i] = order[j];
		order[j] = t;
	}
}

static bool endlessShuffleInTail(const int *order, int npool, int window, int entry)
{
	for (int i = npool - window; i < npool; ++i)
		if (order[i] == entry)
			return true;
	return false;
}

/* Drop the second cut of a section the pool lists twice, so the bag holds each level once. A chart
 * tells routes apart by (episode, section), and a bag that promises every level before a repeat has
 * to count them the same way. Episode 1 section 3 is the only such pair. */
static int endlessDedupeLevelPool(EndlessLevelPoolEntry *pool, int npool)
{
	int kept = 0;
	for (int i = 0; i < npool; ++i)
	{
		bool seen = false;
		for (int k = 0; k < kept && !seen; ++k)
			seen = pool[k].ep == pool[i].ep && pool[k].sec == pool[i].sec;
		if (!seen)
			pool[kept++] = pool[i];
	}
	return kept;
}

bool endlessShuffleSafeLevel(int position, int *epOut, JE_byte *secOut, JE_byte *fileOut)
{
	EndlessLevelPoolEntry pool[ENDLESS_LEVEL_POOL_MAX];
	const int npool = endlessDedupeLevelPool(pool, endlessBuildLevelPool(pool, (int)COUNTOF(pool)));
	if (npool == 0 || position < 0)
		return false;

	const Uint64 refill = (Uint64)(position / npool);
	int order[ENDLESS_LEVEL_POOL_MAX];
	endlessShuffleOrder(order, npool, refill);

	// A refill must not hand back a piece the emptying bag closed on. Swapping any such piece
	// out of the opening window and past it keeps the bagful a permutation. The window sizes
	// and the too-small-pool case are in "Level shuffle" in doc/notes.md.
	const int tailWindow = ENDLESS_MAX_COURSES;
	const int headWindow = 2 * ENDLESS_MAX_COURSES - 1;
	if (refill > 0 && npool >= tailWindow + headWindow + 1)
	{
		int prev[ENDLESS_LEVEL_POOL_MAX];
		endlessShuffleOrder(prev, npool, refill - 1);
		for (int i = 0; i < headWindow; ++i)
		{
			if (!endlessShuffleInTail(prev, npool, tailWindow, order[i]))
				continue;
			for (int j = headWindow; j < npool; ++j)
			{
				if (endlessShuffleInTail(prev, npool, tailWindow, order[j]))
					continue;
				const int t = order[i];
				order[i] = order[j];
				order[j] = t;
				break;
			}
		}
	}

	const EndlessLevelPoolEntry *const drawn = &pool[order[position % npool]];
	*epOut  = drawn->ep;
	*secOut = drawn->sec;
	if (fileOut != NULL)
		*fileOut = drawn->file;
	return true;
}

// Pick an Endless-safe level from any installed episode.
bool endlessRandomSafeLevel(int *epOut, JE_byte *secOut, JE_byte *fileOut)
{
	// Sample the pool uniformly by level, both cuts of a twice-loaded section included.
	EndlessLevelPoolEntry pool[ENDLESS_LEVEL_POOL_MAX];
	const int npool = endlessBuildLevelPool(pool, (int)COUNTOF(pool));
	if (npool == 0)
		return false;

	// Relax the recent-history window when the safe pool is too small.
	for (int window = endlessRecentCount; window >= 0; --window)
	{
		for (int attempt = 0; attempt < npool * 4 + 8; ++attempt)
		{
			const int k = (int)(endlessRand() % (uint)npool);
			if (endlessLevelInRecent(pool[k].ep, pool[k].sec, window))
				continue;
			*epOut  = pool[k].ep;
			*secOut = pool[k].sec;
			if (fileOut != NULL)
				*fileOut = pool[k].file;
			return true;
		}
	}
	return false;
}

JE_byte endlessPickNextLevel(void)
{
	// Fallback when course generation cannot build candidates.
	int ep;
	JE_byte sec, file;
	if (!endlessRandomSafeLevel(&ep, &sec, &file))
	{
		forcedLvlFileNum = 0;
		return FIRST_LEVEL;
	}

	if (ep != episodeNum)
		JE_initEpisode(ep);
	forcedLvlFileNum = file;
	return sec;
}

// Level music excludes jingles and avoids immediate repeats.
static const JE_byte endlessLevelSongs[] = {  // omits shop #3, level-end #10, game-over #11, high-score #34, MusicMan #19, ZANAC3 #31, BEER #41
	1, 2, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 32, 33, 35, 36, 37, 38, 39, 40,
};

static void endlessPickLevelMusic(void)
{
	const int zone = endlessRunDepth + 1;

	// Retrying a zone keeps its existing track.
	if (endlessLastSong != 0 && endlessLastSongDepth == endlessRunDepth)
	{
		levelSong = endlessLastSong;
		return;
	}

	// Reconstruct a fallback previous track for old saves and debug jumps.
	JE_byte prev = 0;
	if (endlessRunDepth > 0)
	{
		endlessReseed((Uint64)(endlessRunDepth - 1) * 2 + 1);
		prev = endlessLevelSongs[endlessRand() % COUNTOF(endlessLevelSongs)];
		endlessReseed(endlessZonePhaseSalt(1));
	}

	// Milestones use pinned tracks without changing RNG draw order.
	const JE_byte pinned = endlessMilestoneSong(endlessMilestoneKindOfZone(zone));

	// Prefer exact saved or pinned tracks over the reconstructed fallback.
	const JE_byte prevPinned = endlessMilestoneSong(endlessMilestoneKindOfZone(zone - 1));
	if (prevPinned != 0)
		prev = prevPinned;
	if (endlessLastSong != 0 && endlessLastSongDepth == endlessRunDepth - 1)
		prev = endlessLastSong;
	const JE_byte nextPinned = endlessMilestoneSong(endlessMilestoneKindOfZone(zone + 1));
	// Avoid the next milestone's outpost track (the credits-zone outpost plays the finale send-off).
	const JE_byte nextShop = (nextPinned == 0) ? 0
	                       : (zone + 1 == ENDLESS_CREDITS_ZONE) ? (JE_byte)ENDLESS_FINALE_SHOP_SONG_LVL
	                       : (JE_byte)ENDLESS_MILESTONE_SHOP_SONG_LVL;

	JE_byte s = endlessLevelSongs[endlessRand() % COUNTOF(endlessLevelSongs)];
	for (int guard = 0; guard < 6 && (s == prev || s == nextPinned || s == nextShop); ++guard)
		s = endlessLevelSongs[endlessRand() % COUNTOF(endlessLevelSongs)];

	levelSong = (pinned != 0) ? pinned : s;

	// Save it for retries and the next zone.
	endlessLastSong      = levelSong;
	endlessLastSongDepth = endlessRunDepth;
}

// Level-script music events are ignored during milestones.
bool endlessMilestoneZone(void)
{
	return endlessMode && endlessMilestoneKind() != 0;
}

// Whether this zone shows the "light cone" spotlight; rolled in endlessRegenerateLevel.
static bool endlessLightCone = false;

bool endlessLightConeActive(void) { return endlessLightCone; }

// Bounds-safe base-level accessors for crash reporting.
const char *endlessBaseLevelName(void)     { return endlessBaseName; }
int         endlessBaseLevelEpisode(void)  { return endlessBaseEp; }
int         endlessBaseLevelSection(void)  { return endlessBaseLvl; }
const char *endlessPrevLevelName(void)     { return endlessPrevBaseName; }
int         endlessPrevLevelEpisode(void)  { return endlessPrevBaseEp; }
int         endlessPrevLevelSection(void)  { return endlessPrevBaseLvl; }

// Bounds-safe recent-level accessors for crash reporting.
int endlessRecentLevelCount(void)          { return endlessRecentCount; }
int endlessRecentLevelEpisode(int i)       { return (i >= 0 && i < endlessRecentCount) ? endlessRecentEp[i]  : 0; }
int endlessRecentLevelSection(int i)       { return (i >= 0 && i < endlessRecentCount) ? endlessRecentSec[i] : 0; }

void endlessRegenerateLevel(void)
{
	// Keep authored level content intact; only repair state required by random jumps.

	// Capture the authored level name before replacing it with the zone label.
	memcpy(endlessPrevBaseName, endlessBaseName, sizeof(endlessPrevBaseName));
	endlessPrevBaseEp  = endlessBaseEp;
	endlessPrevBaseLvl = endlessBaseLvl;
	SDL_strlcpy(endlessBaseName, levelName, sizeof(endlessBaseName));
	endlessBaseEp  = episodeNum;
	endlessBaseLvl = mainLevel;

	// Add the base level to the anti-repeat history.
	endlessRecordRecentLevel(episodeNum, mainLevel);

	// Endless has no datacubes; random level cube data is unsafe.
	snprintf(levelName, sizeof(levelName), "ZONE %d", endlessRunDepth + 1);
	cubeMax = 0;
	lastCubeMax = 0;

	// Pin the planet-map hub to one safe planet.
	mapOrigin = 1;
	mapPNum = 1;
	mapPlanet[0] = 1;
	mapSection[0] = mainLevel;

	// Clear special-mode flags that are unsafe after a random jump.
	galagaMode = false;
	extraGame = false;
	bonusLevelCurrent = false;
	normalBonusLevelCurrent = false;

	flareDuration = 0;
	flareStart = false;

	// Reset per-zone effects.
	endlessResetZoneEffects();

	// The shop consumes resume snapshots before a level starts.
	endlessResumeVisit = false;

	// Reseed the level-start phase by depth. A rerolled chart shifts every phase below with it,
	// so the zone it charts arrives with its own music and flavour (endlessZonePhaseSalt).
	endlessReseed(endlessZonePhaseSalt(1));

	endlessPickLevelMusic();

	// The light cone has its own per-zone phase.
	endlessReseed(endlessZonePhaseSalt(0x40000000));
	endlessLightCone = (endlessRand() % 10 == 0);

	// Gravity has a separate phase; 0x50000000 belongs to elite rolls.
	endlessReseed(endlessZonePhaseSalt(0x60000000));
	endlessRollGravityDir();
}

// Reset all per-level effect state without consuming RNG.
void endlessResetZoneEffects(void)
{
	endlessResetElites();
	endlessZoneTicks = 0;          // ENRAGE ramp
	memset(endlessTurbodriveTimer, 0, sizeof(endlessTurbodriveTimer));  // kill-fire windows
	endlessRetaliationTimer = 0;   // RETALIATION window
	endlessStaticLockoutReset();   // no Static Discharge generator lockout carried in
	endlessReviveGraceReset();     // ...and no leftover revive stun: the next zone opens shooting
	endlessResetZonePerkTimers();  // Opening Salvo / Countermeasure: neither charge nor cooldown crosses the outpost
	memset(endlessOverdriveStacks, 0, sizeof(endlessOverdriveStacks));
	memset(endlessComboKills, 0, sizeof(endlessComboKills));
	endlessResetKillDedup();       // no link number carried in from the last zone's final kill
	endlessResetCustomWeaponZone();  // outpost editor and shop previews must not count as combat use
	// Both ships start every zone in the air, whatever a late outpost packet said about the last one.
	for (unsigned p = 0; p < COUNTOF(endlessPlayerDowned); ++p)
		endlessPlayerDowned[p] = false;
}

// Dormant dispenser bases: a coin per zone up to ENDLESS_DISPENSER_ALWAYS_ZONE,
// where they stop being a surprise and become part of the furniture. The coin has
// its own salt, so it consumes nothing from the other level-start phases.
#define ENDLESS_DISPENSER_ALWAYS_ZONE 50

bool endlessDispenserBaseRoll(void)
{
	if (endlessRunDepth + 1 >= ENDLESS_DISPENSER_ALWAYS_ZONE)
		return true;
	return (endlessSplitMixSeed(endlessZonePhaseSalt(0x70000000)) & 1) != 0;
}

// Campaign debug effects use gameplay RNG because campaigns have no zone phase.
void endlessCampaignLevelStart(void)
{
	if (!endlessCampaignMods || endlessMode)
		return;

	endlessResetZoneEffects();
	// `^` sequences nothing, so the high and low halves swapped between compilers and the
	// two platforms seeded differently from the same stream. Draw them in named order.
	const Uint64 elite_hi = (Uint64)mt_rand(), elite_lo = (Uint64)mt_rand();
	endlessEliteRngState = endlessSplitMixSeed((elite_hi << 32) ^ elite_lo);
	const Uint64 zone_hi = (Uint64)mt_rand(), zone_lo = (Uint64)mt_rand();
	endlessReseed((zone_hi << 32) ^ zone_lo);
	endlessRollGravityDir();
}

// Refresh derived sector state after debug modifiers change mid-level.
void endlessRefreshModDerivedState(void)
{
	endlessRollGravityDir();
}
