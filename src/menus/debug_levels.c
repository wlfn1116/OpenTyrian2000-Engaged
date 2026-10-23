/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "debug_levels.h"

#include "config.h"
#include "console_platform.h"
#include "debug_menu.h"
#include "endless.h"
#include "endless_internal.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "game_menu.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "lvllib.h"
#include "mouse.h"
#include "network.h"
#include "player.h"
#include "touch_ui.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Preserve the source outpost and loadout around debug jumps into ENGAGE mini-games.
 * Their normal exit depends on campaign state that a browser launch does not have. */
static struct
{
	bool armed;                        // a browser pick is in flight; good for one level only
	JE_byte episode, section;          // where the jump was made from
	JE_byte lvlFile;
	Player players[COUNTOF(player)];   // full loadout: ship, weapons, cash, armor
	JE_boolean superTyrian, onePlayerAction, twoPlayerMode;
}
debugJump;

/* Arm the snapshot; call right before the browser's shopSelectLevel(), and before any
 * JE_initEpisode() it does, so the recorded episode is the one being left. */
static void select_debug_level_capture(void)
{
	debugJump.episode = (JE_byte)episodeNum;
	debugJump.section = mainLevel;
	debugJump.lvlFile = lvlFileNum;
	memcpy(debugJump.players, player, sizeof(debugJump.players));
	debugJump.superTyrian = superTyrian;
	debugJump.onePlayerAction = onePlayerAction;
	debugJump.twoPlayerMode = twoPlayerMode;
	debugJump.armed = true;
}

bool debugLevelJumpTake(void)
{
	const bool armed = debugJump.armed;
	debugJump.armed = false;  // always disarm: one level, so it can never apply to the next one
	return armed;
}

/* Stage a debug level pick for the next network launch and clear it when the shop reopens. */
static struct
{
	bool    armed;
	JE_byte episode, section, fileNum;
}
debugPick;

static void debug_level_pick_stage(JE_byte episode, JE_byte section, JE_byte fileNum)
{
	debugPick.armed = true;
	debugPick.episode = episode;
	debugPick.section = section;
	debugPick.fileNum = fileNum;
}

void debugLevelPickReset(void)
{
	debugPick.armed = false;
	endlessJumpPickReset();
}

/* Whole Endless debug block staged with a network level jump. It includes depth, modifiers, and
 * both ships' perks and personal buffs. */
static struct {
	bool  armed;
	Uint8 block[ENDLESS_DEBUG_BLOCK_SIZE];
} endlessJumpPick;

void endlessJumpPickStage(void)
{
	endlessJumpPick.armed = true;
	endlessPackDebugBlock(endlessJumpPick.block);
}

void endlessJumpPickReset(void)
{
	endlessJumpPick.armed = false;
}

bool endlessJumpPickGet(Uint8 *block)
{
	if (!endlessJumpPick.armed)
		return false;

	memcpy(block, endlessJumpPick.block, sizeof(endlessJumpPick.block));
	return true;
}

void endlessJumpPickApply(const Uint8 *block, size_t len)
{
	// A short block is a truncated packet, with nothing usable in the fields past its end.
	if (len < ENDLESS_DEBUG_BLOCK_SIZE)
		return;

	memcpy(endlessJumpPick.block, block, ENDLESS_DEBUG_BLOCK_SIZE);
	endlessJumpPick.armed = true;
	endlessUnpackDebugBlock(endlessJumpPick.block);
}

bool debugLevelPickGet(JE_byte *episode, JE_byte *section, JE_byte *fileNum)
{
	if (!debugPick.armed)
		return false;

	*episode = debugPick.episode;
	*section = debugPick.section;
	*fileNum = debugPick.fileNum;
	return true;
}

void debugLevelPickApply(JE_byte episode, JE_byte section, JE_byte fileNum)
{
	const JE_byte startEp = (JE_byte)episodeNum;

	// Same sequence the browser itself runs: snapshot the outpost we are leaving (while episodeNum
	// still points at it), switch episode if the pick lives in another one, then arm the jump.
	select_debug_level_capture();
	if (episode != 0 && episode != (JE_byte)episodeNum)
		JE_initEpisode(episode);
	if ((JE_byte)episodeNum != startEp)
		initial_episode_num = episodeNum;
	shopSelectLevel(section, fileNum);

	debug_level_pick_stage(episode, section, fileNum);
}

void debugLevelJumpReturn(void)
{
	if (debugJump.episode != episodeNum)
		JE_initEpisode(debugJump.episode);  // reloads level + item data; set the level fields after

	memcpy(player, debugJump.players, sizeof(debugJump.players));
	superTyrian     = debugJump.superTyrian;      // ']e' forced these on for the mini-game
	onePlayerAction = debugJump.onePlayerAction;
	twoPlayerMode   = debugJump.twoPlayerMode;

	mainLevel = debugJump.section;
	saveLevel = mainLevel;
	nextLevel = mainLevel;
	lvlFileNum = debugJump.lvlFile;
	forcedLvlFileNum = 0;  // the browser's one-shot file override died with the mini-game
}

/* Endless sector modifiers exposed by the debug jump screen. Personal kill-fire effects use
 * endlessDebugBuffMods; NULL hints reuse endlessModWord. */
enum { EMG_DANGER, EMG_BOON, EMG_DEAL, EMG_GROUPS };  // ...and the headings, in list order:
static const char *const endlessDebugModGroupName[EMG_GROUPS] = { "DANGERS", "BOONS", "GAMBLE DEALS" };

static const struct { Uint64 bit; Uint8 grp; const char *name; const char *hint; } endlessDebugSectorMods[] = {
	{ ENDLESS_MOD_FORTIFIED,   EMG_DANGER, "Fortified",       NULL },
	{ ENDLESS_MOD_FRENZY,      EMG_DANGER, "Frenzy",          NULL },
	{ ENDLESS_MOD_SWIFT,       EMG_DANGER, "Swift",           NULL },
	{ ENDLESS_MOD_DEVASTATING, EMG_DANGER, "Devastating",     NULL },
	{ ENDLESS_MOD_ELITEPACK,   EMG_DANGER, "Elite Pack",      NULL },
	{ ENDLESS_MOD_APEX,        EMG_DANGER, "Apex Swarm",      NULL },
	{ ENDLESS_MOD_LEGION,      EMG_DANGER, "Legion",          NULL },
	{ ENDLESS_MOD_ENRAGE,      EMG_DANGER, "Enrage",          NULL },
	{ ENDLESS_MOD_KAMIKAZE,    EMG_DANGER, "Kamikaze (mid)",  NULL },
	{ ENDLESS_MOD_HOMING,      EMG_DANGER, "Homing (light)",  NULL },
	{ ENDLESS_MOD_GRAVITY,     EMG_DANGER, "Gravity Well",    NULL },
	{ ENDLESS_MOD_GRAVITY_OMNI,EMG_DANGER, "Gravity (omni)",  "pull on a random heading" },
	{ ENDLESS_MOD_OVERCLOCK,   EMG_DANGER, "Overclock",       NULL },
	{ ENDLESS_MOD_SLIPSTREAM,  EMG_DANGER, "Slipstream",      NULL },
	{ ENDLESS_MOD_OVERLOAD,    EMG_DANGER, "Overload",        NULL },
	{ ENDLESS_MOD_WARP,        EMG_DANGER, "Warp Speed",      NULL },
	{ ENDLESS_MOD_TOPSY,       EMG_DANGER, "Topsy-Turvy",     NULL },
	{ ENDLESS_MOD_SLUGGISH,    EMG_DANGER, "Sluggish Ship",   NULL },
	{ ENDLESS_MOD_SHIELDLESS,  EMG_DANGER, "No Shield Regen", NULL },
	{ ENDLESS_MOD_DEADGEN,     EMG_DANGER, "Dead Generator",  NULL },
	{ ENDLESS_MOD_MARTYRDOM,   EMG_DANGER, "Martyrdom",       NULL },
	{ ENDLESS_MOD_SEEKER,      EMG_DANGER, "Seeker Rounds",   NULL },
	{ ENDLESS_MOD_TWINSEEK,    EMG_DANGER, "Twin Seekers",    NULL },
	{ ENDLESS_MOD_HUNTER,      EMG_DANGER, "Hunter Rounds",   NULL },
	{ ENDLESS_MOD_TRUEAIM,     EMG_DANGER, "True Aim",        NULL },
	{ ENDLESS_MOD_KILLSHOT,    EMG_DANGER, "Kill Shot",       NULL },
	{ ENDLESS_MOD_STATIC,      EMG_DANGER, "Static Discharge",NULL },
	{ ENDLESS_MOD_RETALIATION, EMG_DANGER, "Retaliation",     NULL },
	{ ENDLESS_MOD_FRAGILE,     EMG_BOON,   "Fragile",         NULL },
	{ ENDLESS_MOD_BOUNTY,      EMG_BOON,   "Bounty",          NULL },
	{ ENDLESS_MOD_OVERCHARGE,  EMG_BOON,   "Overcharged",     NULL },
	{ ENDLESS_MOD_DILATION,    EMG_BOON,   "Time Dilation",   NULL },
	{ ENDLESS_MOD_FAVOR,       EMG_BOON,   "Merchant Favor",  NULL },
	{ ENDLESS_MOD_CURSED,      EMG_BOON,   "Cursed Bounty",   NULL },
	{ ENDLESS_MOD_NOCHAMP,     EMG_BOON,   "No Champions",    NULL },
	{ ENDLESS_MOD_NOELITE,     EMG_BOON,   "No Elites",       NULL },
	{ ENDLESS_MOD_AEGIS,       EMG_BOON,   "Aegis Gate",      NULL },
	{ ENDLESS_MOD_FLAKSCREEN,  EMG_BOON,   "Flak Screen",     NULL },
	{ ENDLESS_MOD_AUXREACTOR,  EMG_BOON,   "Aux Reactor",     NULL },
	{ ENDLESS_MOD_LOWPROFILE,  EMG_BOON,   "Low Profile",     NULL },
	{ ENDLESS_MOD_GIANTKILLER, EMG_BOON,   "Giant Killer",    NULL },
	{ ENDLESS_MOD_CLEANSIGNALS,EMG_BOON,   "Clean Signals",   NULL },
	{ ENDLESS_MOD_SHOCKWAVE,   EMG_BOON,   "Shockwave",       NULL },
	{ ENDLESS_MOD_SOFTLANDING, EMG_BOON,   "Soft Landing",    NULL },
	{ ENDLESS_MOD_STARCHARTS,  EMG_BOON,   "Star Charts",     NULL },
	{ ENDLESS_MOD_BREAKTHROUGH,EMG_BOON,   "Breakthrough",    NULL },
	// Gamble-only next-sector effects. Each can be isolated for debugging.
	{ ENDLESS_MOD_MARKED,      EMG_DEAL,   "Marked (boss+)",  "next boss beefed up" },
	{ ENDLESS_MOD_NITRO,       EMG_DEAL,   "Nitro (1-hit)",   "you hit hard, any hit kills" },
	{ ENDLESS_MOD_OVERHEAT,    EMG_DEAL,   "Overheat DoT",    NULL },
	{ ENDLESS_MOD_DUD,         EMG_DEAL,   "Dud Bombs",       "superbombs will not fire" },
	{ ENDLESS_MOD_RAMPAGE,     EMG_DEAL,   "Rampage (ram!)",  NULL },
};

// Personal kill-fire effects use their own debug list.
static const struct { Uint64 bit; const char *name; const char *hint; } endlessDebugBuffMods[] = {
	{ ENDLESS_MOD_TURBODRIVE,  "Turbodrive",  NULL },
	{ ENDLESS_MOD_OVERBLAST,   "Overblast",   NULL },
	{ ENDLESS_MOD_OVERDRIVE,   "Overdrive",   NULL },
	{ ENDLESS_MOD_BACKFIRE,    "Backfire",    NULL },  // the evil mirrors, in the same order
	{ ENDLESS_MOD_BURNOUT,     "Burnout",     NULL },
	{ ENDLESS_MOD_MISFIRE,     "Misfire",     NULL },
};

/* Installed levels shared by both debug pickers. Keep the allLevel prefix distinct from the
 * endlessBaseName crash-log state in endless_level.c. */
#define ALL_LEVEL_MAX 256
static int     allLevelEp[ALL_LEVEL_MAX];
static JE_word allLevelSec[ALL_LEVEL_MAX];
static JE_byte allLevelFile[ALL_LEVEL_MAX];
static char    allLevelName[ALL_LEVEL_MAX][18];
static int     allLevelCount;

// Gather each installed episode's ]L levels and remember the owning episode for debug jumps.
static void loadAllLevels(void)
{
	allLevelCount = 0;
	for (int ep = 1; ep <= EPISODE_MAX && allLevelCount < ALL_LEVEL_MAX; ep++)
	{
		if (!episodeAvail[ep - 1])
			continue;
		const unsigned int levelFileCount = JE_levelFileCount(ep);
		if (levelFileCount == 0)
			continue;

		char fname[16];
		snprintf(fname, sizeof(fname), "levels%d.dat", ep);
		FILE *f = dir_fopen_warn(data_dir(), fname, "rb");
		if (f == NULL)
			continue;

		JE_word section = 0;
		long end = ftell_eof(f);
		char s[256];
		while (ftell(f) < end && allLevelCount < ALL_LEVEL_MAX)
		{
			read_encrypted_pascal_string(s, sizeof(s), f);
			if (s[0] == '*')
				section++;
			if (s[0] == ']' && s[1] == 'L')
			{
				const int fileNum = atoi(s + 25);
				if (fileNum < 1 || (unsigned int)fileNum > levelFileCount)
					continue;

				allLevelEp[allLevelCount] = ep;
				allLevelSec[allLevelCount] = section;

				char name_buf[10];
				SDL_strlcpy(name_buf, s + 13, sizeof(name_buf));
				size_t len = strlen(name_buf);
				while (len > 0 && name_buf[len - 1] == ' ')
					name_buf[--len] = '\0';
				SDL_strlcpy(allLevelName[allLevelCount], name_buf, sizeof(allLevelName[0]));

				allLevelFile[allLevelCount] = (JE_byte)fileNum;
				allLevelCount++;
			}
		}
		fclose(f);
	}
}

/* Endless debug jump and tuning screens. */
/* Shared row model with non-selectable headings and no fixed offsets. */
typedef struct { Uint8 kind; short idx; const char *label; } PickerRow;

enum  // row kinds
{
	EDR_HEADER,   // group heading: drawn, never selectable
	EDR_ZONE,     // the zone number
	EDR_BASE,     // Random / the chosen base level (drills into EDS_BASE)
	EDR_OPEN,     // drill into screen `idx`
	EDR_ACTION,   // idx = EDA_*
	EDR_LAUNCH,   // start the zone (jump form) / apply and close (tune form)
	EDR_SECMOD, EDR_BUFMOD, EDR_PERK, EDR_GAMBLE,
	EDR_SHIP,     // which ship the perk and personal-buff pages edit (co-op only)
	EDR_CAMPFX,   // the master "run endless effects in a normal game" toggle (tune form, outside endless)
	EDR_SCALE,    // one scaling lever: reads out at the previewed zone, Left/Right pins an override (idx = ESO_*)
	EDR_SCALEINFO,// a scaling figure with no override of its own (idx = EDI_*)
	EDR_SCALEDIFF,// the difficulty the preview is computed at
	EDR_LEVEL     // a level from allLevel*: the endless base list AND the campaign picker
};
enum { EDS_HUB, EDS_SECMOD, EDS_BUFMOD, EDS_PERK, EDS_GAMBLE, EDS_SCALING, EDS_BASE, EDS_SCREENS };
enum { EDA_CLEAR, EDA_RESET, EDA_UNPIN };

// The read-only figures on the scaling page: derived from depth like the levers, but with no knob
// of their own (the bounties are economy, and the three ramp figures are the levers' own clock).
enum { EDI_ELITEBOUNTY, EDI_CHAMPBOUNTY, EDI_EFFDEPTH, EDI_RAMPZONE, EDI_RAMPPCT, EDI_COUNT };

// Zones the curve shown on a selected lever's help line samples.
static const int endlessCurveZones[] = { 1, 25, 50, 100, 200 };

// The base-level list is the longest screen: every level, an episode heading each, plus Random.
#define EDBG_MAX_ROWS (ALL_LEVEL_MAX + EPISODE_MAX + 8)

static bool pickerRowSelectable(const PickerRow *r)
{
	return r->kind != EDR_HEADER;
}

/* Step one selectable row in `dir`, wrapping past the headings. */
static int pickerRowStep(const PickerRow *rows, int rowCount, int sel, int dir)
{
	for (int n = 0; n < rowCount; ++n)
	{
		sel += dir;
		if (sel < 0)
			sel = rowCount - 1;
		else if (sel >= rowCount)
			sel = 0;
		if (pickerRowSelectable(&rows[sel]))
			return sel;
	}
	return sel;
}

/* The nearest selectable row at or after `sel` (then searching back); for any jump that can land
 * on a heading: clamping, paging, Home/End, a letter jump, a click. */
static int pickerRowSnap(const PickerRow *rows, int rowCount, int sel)
{
	if (sel < 0)
		sel = 0;
	if (sel > rowCount - 1)
		sel = rowCount - 1;
	for (int i = sel; i < rowCount; ++i)
		if (pickerRowSelectable(&rows[i]))
			return i;
	for (int i = sel; i >= 0; --i)
		if (pickerRowSelectable(&rows[i]))
			return i;
	return sel;
}

/* One scaling lever's value out of a snapshot, so the readout, the curve line and the override
 * seeding all agree on which field a row means. `info` picks an EDI_* read-only figure instead. */
static long endlessScaleFieldOf(const EndlessScaling *sc, int kind, int idx)
{
	if (kind == EDR_SCALEINFO)
	{
		switch (idx)
		{
		case EDI_ELITEBOUNTY: return sc->eliteBounty;
		case EDI_CHAMPBOUNTY: return sc->champBounty;
		case EDI_EFFDEPTH:    return sc->effDepth;
		case EDI_RAMPZONE:    return sc->diffZone;
		default:              return sc->rampPercent;
		}
	}
	switch (idx)
	{
	case ESO_ARMOR:       return sc->armorPct;
	case ESO_BOSSHP:      return sc->bossMult100;
	case ESO_FIREDELAY:   return sc->fireDelayPct;
	case ESO_SHOTSPEED:   return sc->shotSpeedPct;
	case ESO_SHOTDMG:     return sc->shotDmgPct;
	case ESO_CONTACT:     return sc->contactPct;
	case ESO_TIDE:        return sc->tide;
	case ESO_EXTRASHOTS:  return sc->extraShots;
	case ESO_ELITECHANCE: return sc->elitePct;
	case ESO_ELITEHP:     return sc->eliteHpMult100;
	case ESO_PIERCEDMG:   return sc->piercePct;
	case ESO_PIERCELOCK:  return sc->pierceLock100;
	default:              return sc->playerDmgPct;
	}
}

/* Apply shared modifiers and per-player perks and buffs from the debug slate. */
static bool endlessDebugApplySlate(Uint64 sectorMods, const Uint64 *buffs, int perks[2][32],
                                   int perkCount)
{
	const Uint64 folded = endlessFoldPurchasedMods(sectorMods & ~ENDLESS_PERSONAL_MOD_MASK, 0);
	const bool modsMoved = (folded != endlessActiveMods);

	endlessActiveMods = folded;
	for (uint p = 0; p < 2; ++p)
	{
		endlessSetPersonalBuffMods(p, buffs[p]);   // after the mask: it folds against it
		for (int i = 0; i < perkCount; ++i)
			endlessPerkSetOwnedFor(p, i, perks[p][i]);
	}
	return modsMoved;
}

/* Jump mode launches a chosen zone; tune mode applies effects in place. */
static bool endlessDebugScreen(bool jumpMode)
{
	// Outside endless the effect layer is opt-in, and the toggle for it lives on this screen.
	const bool campaignForm = !jumpMode && !endlessMode;
	const JE_byte startEp = episodeNum;
	if (jumpMode)
		loadAllLevels();   // only the jump form has a Base Level row / list to fill

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	const int pw = 248;
	// Centre in whatever content width is actually in force.
	const int contentW = (video_get_menu_x_offset() != 0) ? LEGACY_WIDTH : vga_width;
	const int px0 = (contentW - pw) / 2;
	const int px1 = px0 + pw - 1;
	const int py0 = 12;
	const int py1 = vga_height - 12;
	const int title_h = 15;
	const int items_top = py0 + title_h + 3;
	const int items_bottom = py1 - 19;   // two footer lines: the selected row's help, then the keys
	const int row_h = 10;
	const int pageRows = (items_bottom - items_top) / row_h;
	const int mid_x = (px0 + px1) / 2;
	const int val_x = px1 - 10;          // right edge of the value column, clear of the scrollbar

	const char *const screenTitle[EDS_SCREENS] = {
		jumpMode ? "ENDLESS  -  ZONE JUMP" : "ENDLESS EFFECTS",
		"SECTOR MODIFIERS", "PERSONAL BUFFS",
		"PERKS", "GAMBLE OUTCOMES", "ZONE SCALING", "BASE LEVEL"
	};
#if defined(__SWITCH__) || defined(__vita__)
	const char *const screenKeys[EDS_SCREENS] = {
		jumpMode ? "A Open   B Close" : "A Open   B Apply and close",
		"A Toggle   B Back   L/R Page",
		"A Toggle   B Back",
		"A Stack   B Back   L/R Page",
		"A Fire   B Back   L/R Page",
		"A Pin/unpin   L/R Adjust   B Back",
		"A Pick   B Back   L/R Episode",
	};
#else
	const char *const screenKeys[EDS_SCREENS] = {
		jumpMode ? "Enter Open   Esc Close" : "Enter Open   Esc Apply and close",
		"Enter Toggle   Esc Back   PgUp/PgDn",
		"Enter Toggle   Esc Back",
		"Enter Stack   L/R Adjust   Esc Back",
		"Enter Fire   Esc Back",
		"Enter Pin/unpin   L/R Adjust   Esc Back",
		"Enter Pick   L/R Episode   Esc Back",
	};
#endif

	const int NSEC   = (int)COUNTOF(endlessDebugSectorMods);
	const int NBUF   = (int)COUNTOF(endlessDebugBuffMods);
	const int NGAM   = endlessGambleOutcomeCount();
	int       NPERKS = endlessPerkCount();

	/* Perks and personal buffs belong to one ship, so the screen edits them per ship and the Edit
	 * Ship row picks which. The sector modifiers below them are the run's and stay shared. */
	int    dbgPerks[2][32];            // owned stacks per ship per perk, pre-loaded from the run
	if (NPERKS > (int)COUNTOF(dbgPerks[0]))
		NPERKS = (int)COUNTOF(dbgPerks[0]);

	const Uint64 runMods = endlessActiveMods | endlessPendingMods();

	int    dbgShip = (int)endlessEconomyIndex();   // opens on your own ship
	int    dbgZone = endlessRunDepth + 1;
	bool   dbgZoneTyped = false;       // a number is being typed digit-by-digit (desktop keyboard)
	int    dbgBase = -1;               // -1 = Random, else an index into endlessBase*
	int    dbgBaseLast = 0;            // last real level picked, so the Base row can toggle back to it
	Uint64 dbgMods = runMods & ~ENDLESS_PERSONAL_MOD_MASK;   // sector bits, pre-toggled from the run
	Uint64 dbgBuffs[2];                // ...and each ship's personal-buff bits
	for (uint p = 0; p < COUNTOF(dbgBuffs); ++p)
	{
		dbgBuffs[p] = endlessPersonalBuffMods(p);
		for (int i = 0; i < NPERKS; ++i)
			dbgPerks[p][i] = endlessPerkGetOwnedFor(p, i);
	}

	char dbgGambleMsg[48];             // last outcome fired here, shown as the gamble list's help
	dbgGambleMsg[0] = '\0';

	// The difficulty the SCALING page computes at.
	int dbgDiff = -1;
	bool dbgCampFx = endlessCampaignMods;

	int  screen = EDS_HUB;
	int  sel[EDS_SCREENS] = { 0 }, top[EDS_SCREENS] = { 0 };
	int  focusBase = -2;               // on opening the level list, land on the current pick (-2 = no request)
	int  prev_mx = mouse_x, prev_my = mouse_y;
	bool chosen = false, done = false;

	wait_noinput(false, false, true);
	newkey = newmouse = false;         // don't let the press that opened us leak in

	while (!done)
	{
		PickerRow rows[EDBG_MAX_ROWS];
		char epHeads[EPISODE_MAX][12];     // "EPISODE n" headings, alive as long as the rows point at them
		int  rowCount = 0;

		#define EDBG_ADD(k, i, l) do { \
			if (rowCount < (int)COUNTOF(rows)) { \
				rows[rowCount].kind = (Uint8)(k); rows[rowCount].idx = (short)(i); \
				rows[rowCount].label = (l); ++rowCount; \
			} } while (0)

		switch (screen)
		{
		case EDS_HUB:
			if (campaignForm)
			{
				EDBG_ADD(EDR_HEADER, 0, "IN A NORMAL GAME");
				EDBG_ADD(EDR_CAMPFX, 0, "Endless Effects");
			}
			EDBG_ADD(EDR_HEADER, 0, "ZONE");
			EDBG_ADD(EDR_ZONE, 0, jumpMode ? "Zone" : "Virtual Zone");
			if (jumpMode)
				EDBG_ADD(EDR_BASE, 0, "Base Level");
			EDBG_ADD(EDR_HEADER, 0, "SLATE");
			EDBG_ADD(EDR_OPEN, EDS_SECMOD, "Sector Modifiers");
			if (coopEndlessMode)
				EDBG_ADD(EDR_SHIP, 0, "Edit Ship");
			EDBG_ADD(EDR_OPEN, EDS_BUFMOD, "Personal Buffs");
			EDBG_ADD(EDR_OPEN, EDS_PERK, "Perks");
			EDBG_ADD(EDR_HEADER, 0, "TOOLS");
			EDBG_ADD(EDR_OPEN, EDS_SCALING, "Zone Scaling");
			EDBG_ADD(EDR_OPEN, EDS_GAMBLE, "Gamble Outcomes");
			EDBG_ADD(EDR_ACTION, EDA_CLEAR, "Clear Everything");
			EDBG_ADD(EDR_ACTION, EDA_RESET, "Reset To Run State");
			EDBG_ADD(EDR_LAUNCH, 0, jumpMode ? "START ZONE" : "APPLY AND CLOSE");
			break;

		case EDS_SCALING:
			EDBG_ADD(EDR_HEADER, 0, "PREVIEW AT");
			EDBG_ADD(EDR_ZONE, 0, jumpMode ? "Zone" : "Virtual Zone");
			EDBG_ADD(EDR_SCALEDIFF, 0, "Difficulty");
			EDBG_ADD(EDR_HEADER, 0, "ENEMY INTENSITY");
			EDBG_ADD(EDR_SCALE, ESO_ARMOR, "Enemy HP %");
			EDBG_ADD(EDR_SCALE, ESO_BOSSHP, "Boss HP x");
			EDBG_ADD(EDR_SCALE, ESO_PIERCELOCK, "Boss Pierce Lock");
			EDBG_ADD(EDR_SCALE, ESO_FIREDELAY, "Fire Cooldown %");
			EDBG_ADD(EDR_SCALE, ESO_SHOTSPEED, "Shot Speed %");
			EDBG_ADD(EDR_SCALE, ESO_SHOTDMG, "Shot Damage %");
			EDBG_ADD(EDR_SCALE, ESO_CONTACT, "Ram Damage %");
			EDBG_ADD(EDR_HEADER, 0, "RISING TIDE");
			EDBG_ADD(EDR_SCALE, ESO_TIDE, "Tide Level");
			EDBG_ADD(EDR_SCALE, ESO_EXTRASHOTS, "Extra Shots");
			EDBG_ADD(EDR_HEADER, 0, "ELITES");
			EDBG_ADD(EDR_SCALE, ESO_ELITECHANCE, "Elite Share %");
			EDBG_ADD(EDR_SCALE, ESO_ELITEHP, "Elite HP x");
			EDBG_ADD(EDR_SCALEINFO, EDI_ELITEBOUNTY, "Elite Bounty");
			EDBG_ADD(EDR_SCALEINFO, EDI_CHAMPBOUNTY, "Champion Bounty");
			EDBG_ADD(EDR_HEADER, 0, "PLAYER");
			EDBG_ADD(EDR_SCALE, ESO_PLAYERDMG, "Your Damage %");
			EDBG_ADD(EDR_SCALE, ESO_PIERCEDMG, "Pierce Damage %");
			EDBG_ADD(EDR_HEADER, 0, "THE RAMP ITSELF");
			EDBG_ADD(EDR_SCALEINFO, EDI_EFFDEPTH, "Effective Depth");
			EDBG_ADD(EDR_SCALEINFO, EDI_RAMPZONE, "Ramp Zone");
			EDBG_ADD(EDR_SCALEINFO, EDI_RAMPPCT, "Difficulty Ramp %");
			EDBG_ADD(EDR_ACTION, EDA_UNPIN, "Clear All Pins");
			break;

		case EDS_SECMOD:
			for (int g = 0; g < EMG_GROUPS; ++g)
			{
				EDBG_ADD(EDR_HEADER, 0, endlessDebugModGroupName[g]);
				for (int i = 0; i < NSEC; ++i)
					if (endlessDebugSectorMods[i].grp == g)
						EDBG_ADD(EDR_SECMOD, i, endlessDebugSectorMods[i].name);
			}
			break;

		case EDS_BUFMOD:
			EDBG_ADD(EDR_HEADER, 0, "BUFFS AND THEIR EVIL MIRRORS");
			for (int i = 0; i < NBUF; ++i)
				EDBG_ADD(EDR_BUFMOD, i, endlessDebugBuffMods[i].name);
			break;

		case EDS_PERK:
			EDBG_ADD(EDR_HEADER, 0, "OWNED STACKS");
			for (int i = 0; i < NPERKS; ++i)
				EDBG_ADD(EDR_PERK, i, endlessPerkName(i));
			break;

		case EDS_GAMBLE:
			EDBG_ADD(EDR_HEADER, 0, "FIRES AT ONCE  -  NO FEE");
			for (int i = 0; i < NGAM; ++i)
				EDBG_ADD(EDR_GAMBLE, i, endlessGambleOutcomeName(i));
			break;

		default:  // EDS_BASE
		{
			EDBG_ADD(EDR_LEVEL, -1, "Random");
			int lastEp = 0;
			for (int i = 0; i < allLevelCount; ++i)
			{
				const int ep = allLevelEp[i];
				if (ep != lastEp && ep >= 1 && ep <= EPISODE_MAX)
				{
					lastEp = ep;
					snprintf(epHeads[ep - 1], sizeof(epHeads[0]), "EPISODE %d", ep);
					EDBG_ADD(EDR_HEADER, ep, epHeads[ep - 1]);
				}
				EDBG_ADD(EDR_LEVEL, i, allLevelName[i]);
			}
			break;
		}
		}

		#undef EDBG_ADD

		// The list may have moved under the selection (a screen change, a shorter list than last
		// visit): clamp it, and never leave it parked on a heading.
		int s = pickerRowSnap(rows, rowCount, sel[screen]);
		if (focusBase >= -1 && screen == EDS_BASE)
		{
			for (int r = 0; r < rowCount; ++r)
				if (rows[r].kind == EDR_LEVEL && rows[r].idx == focusBase)
				{
					s = r;
					break;
				}
			focusBase = -2;
		}

		const int visibleRows = (pageRows < rowCount) ? pageRows : rowCount;

		int scrollTop = top[screen];
		if (s < scrollTop)
			scrollTop = s;
		else if (s >= scrollTop + visibleRows)
			scrollTop = s - visibleRows + 1;
		// keep a heading on screen with the first row under it, so a selection never floats
		// context-free at the top of the window
		if (scrollTop > 0 && scrollTop == s && !pickerRowSelectable(&rows[s - 1]))
			--scrollTop;
		if (scrollTop > rowCount - visibleRows)
			scrollTop = rowCount - visibleRows;
		if (scrollTop < 0)
			scrollTop = 0;

		sel[screen] = s;
		top[screen] = scrollTop;

		// Recompute scaling readouts after every editable value.
		EndlessScaling sc;
		endlessScalingSnapshot(dbgZone, dbgDiff, dbgMods, &sc);

		debug_panel_frame(px0, py0, px1, py1, title_h, mid_x, screenTitle[screen]);

		for (int vis = 0; vis < visibleRows; ++vis)
		{
			const int i = scrollTop + vis;
			const int ry = items_top + vis * row_h;
			const bool isSel = (i == s);

			if (rows[i].kind == EDR_HEADER)
			{
				draw_font_hv_shadow(VGAScreen, px0 + 6, ry, rows[i].label, small_font, left_aligned, 15, 3, true, 1);
				const int rule_x = px0 + 10 + JE_textWidth(rows[i].label, small_font);
				if (rule_x < val_x)
					fill_rectangle_xy(VGAScreen, rule_x, ry + 3, val_x, ry + 3, PANEL_DIVIDER);
				continue;
			}

			if (isSel)
				fill_rectangle_xy(VGAScreen, px0 + 3, ry - 1, px1 - 3, ry + row_h - 2,
				                  PANEL_SEL_BAR);

			char  val[40];
			Sint8 labBright = isSel ? 5 : -1, valBright = isSel ? 5 : -1;
			int   labX = px0 + 12;
			val[0] = '\0';

			switch (rows[i].kind)
			{
			case EDR_ZONE:
			{
				// tag the milestone classes, so a jump meant to test one can't miss by a zone
				const int kind = endlessMilestoneKindOfZone(dbgZone);
				snprintf(val, sizeof(val), "%d%s%s", dbgZone, dbgZoneTyped ? "_" : "",
				         kind == 2 ? "   THE END" : kind == 1 ? "   MILESTONE" : kind == 3 ? "   minor" : "");
				valBright = 6;
				break;
			}
			case EDR_BASE:
				if (dbgBase < 0)
					SDL_strlcpy(val, "Random", sizeof(val));
				else
					snprintf(val, sizeof(val), "%s (Ep %d)", allLevelName[dbgBase], allLevelEp[dbgBase]);
				valBright = 6;
				break;
			case EDR_OPEN:
			{
				int n = 0;
				if (rows[i].idx == EDS_SECMOD)
				{
					for (int m = 0; m < NSEC; ++m)
						if (dbgMods & endlessDebugSectorMods[m].bit)
							++n;
					if (n)
						snprintf(val, sizeof(val), "%d on", n);
				}
				else if (rows[i].idx == EDS_BUFMOD)
				{
					for (int m = 0; m < NBUF; ++m)
						if (dbgBuffs[dbgShip] & endlessDebugBuffMods[m].bit)
							++n;
					if (n)
						snprintf(val, sizeof(val), "%d on", n);
				}
				else if (rows[i].idx == EDS_PERK)
				{
					for (int p = 0; p < NPERKS; ++p)
						n += dbgPerks[dbgShip][p];
					if (n)
						snprintf(val, sizeof(val), "%d owned", n);
				}
				if (val[0] == '\0')
					SDL_strlcpy(val, rows[i].idx == EDS_GAMBLE ? "..." : "none", sizeof(val));
				valBright = n ? 6 : (isSel ? 4 : -3);
				break;
			}
			case EDR_ACTION:
				labBright = isSel ? 6 : 1;
				if (rows[i].idx == EDA_UNPIN)
				{
					const int np = endlessScalingOverrideCount();
					if (np > 0)
						snprintf(val, sizeof(val), "%d pinned", np);
					valBright = np ? 6 : -3;
				}
				break;
			case EDR_LAUNCH:
				labBright = isSel ? 6 : 2;
				break;
			case EDR_CAMPFX:
				SDL_strlcpy(val, dbgCampFx ? "ON" : "OFF", sizeof(val));
				labBright = isSel ? 5 : (dbgCampFx ? 2 : -1);
				valBright = dbgCampFx ? 6 : (isSel ? 4 : -3);
				break;
			case EDR_SCALEDIFF:
				if (dbgDiff < 0)
					snprintf(val, sizeof(val), "Current  (%s)", difficultyNameB[difficultyLevel % 11]);
				else
					SDL_strlcpy(val, difficultyNameB[dbgDiff % 11], sizeof(val));
				valBright = 6;
				break;
			case EDR_SCALE:
			{
				// A pinned lever is flagged right in the value column; without the marker a forced
				// figure is indistinguishable from one the ramp produced.
				const bool pinned = endlessScalingOverride[rows[i].idx].active;
				const long v = endlessScaleFieldOf(&sc, EDR_SCALE, rows[i].idx);
				// Display fixed-point lockout and HP values in their user-facing units.
				if (rows[i].idx == ESO_PIERCELOCK || rows[i].idx == ESO_BOSSHP
				    || rows[i].idx == ESO_ELITEHP)
					snprintf(val, sizeof(val), "%ld.%02ld%s", v / 100, v % 100, pinned ? "  PIN" : "");
				else
					snprintf(val, sizeof(val), "%ld%s", v, pinned ? "  PIN" : "");
				labBright = isSel ? 5 : (pinned ? 2 : -1);
				valBright = pinned ? 6 : (isSel ? 5 : 1);
				labX = px0 + 16;
				break;
			}
			case EDR_SCALEINFO:
				snprintf(val, sizeof(val), "%ld", endlessScaleFieldOf(&sc, EDR_SCALEINFO, rows[i].idx));
				labBright = isSel ? 5 : -1;
				valBright = isSel ? 5 : 1;
				labX = px0 + 16;
				break;
			case EDR_SHIP:
				snprintf(val, sizeof(val), "%s%s", dbgShip == 0 ? "One" : "Two",
				         dbgShip == (int)endlessEconomyIndex() ? "  (yours)" : "");
				valBright = 6;
				break;
			case EDR_SECMOD:
			case EDR_BUFMOD:
			{
				const bool sector = (rows[i].kind == EDR_SECMOD);
				const Uint64 bit = sector ? endlessDebugSectorMods[rows[i].idx].bit
				                          : endlessDebugBuffMods[rows[i].idx].bit;
				const bool on = ((sector ? dbgMods : dbgBuffs[dbgShip]) & bit) != 0;
				SDL_strlcpy(val, on ? "ON" : "-", sizeof(val));
				labBright = isSel ? 5 : (on ? 2 : -1);
				valBright = on ? 6 : (isSel ? 4 : -3);
				labX = px0 + 16;
				break;
			}
			case EDR_PERK:
			{
				const int p = rows[i].idx, n = dbgPerks[dbgShip][p];
				if (n > 0)
					snprintf(val, sizeof(val), "%d/%d", n, endlessPerkMaxStack(p));
				else
					SDL_strlcpy(val, "-", sizeof(val));
				labBright = isSel ? 5 : (n > 0 ? 2 : -1);
				valBright = n > 0 ? 6 : (isSel ? 4 : -3);
				labX = px0 + 16;
				break;
			}
			case EDR_GAMBLE:
				if (isSel)
					SDL_strlcpy(val, "fire", sizeof(val));
				valBright = 6;
				labX = px0 + 16;
				break;
			default:  // EDR_LEVEL
			{
				const bool picked = (rows[i].idx == dbgBase);
				if (rows[i].idx >= 0)
					snprintf(val, sizeof(val), "L%d", allLevelSec[rows[i].idx]);
				else if (picked)
					SDL_strlcpy(val, "picked", sizeof(val));
				labBright = isSel ? 5 : (picked ? 6 : -1);
				valBright = picked ? 6 : (isSel ? 4 : -3);
				labX = px0 + 16;
				break;
			}
			}

			draw_font_hv_shadow(VGAScreen, labX, ry, rows[i].label, small_font, left_aligned, 15, labBright, true, 1);
			if (val[0] != '\0')
				draw_font_hv_shadow(VGAScreen, val_x, ry, val, small_font, right_aligned, 15, valBright, true, 1);
			if (isSel)
				draw_font_hv(VGAScreen, px0 + 5, ry, ">", small_font, left_aligned, 15, 6);
		}

		if (rowCount > visibleRows)
		{
			const int track_top = items_top - 1;
			const int track_bot = items_top + visibleRows * row_h - 2;
			const int track_h = track_bot - track_top;
			fill_rectangle_xy(VGAScreen, px1 - 4, track_top, px1 - 3, track_bot, PANEL_EDGE_LO);
			int thumb_h = track_h * visibleRows / rowCount;
			if (thumb_h < 4)
				thumb_h = 4;
			const int denom = rowCount - visibleRows;
			const int thumb_y = track_top + (denom > 0 ? (track_h - thumb_h) * scrollTop / denom : 0);
			fill_rectangle_xy(VGAScreen, px1 - 4, thumb_y, px1 - 3, thumb_y + thumb_h,
			                  PANEL_EDGE_HI);
		}

		// Footer line 1: what the selected row IS. Every list row can explain itself, so none of
		// the 48 modifier names has to carry its meaning in the name alone.
		char helpBuf[96];
		const char *help = "";
		switch (rows[s].kind)
		{
		case EDR_CAMPFX:
			help = "Endless mods, perks and scaling in a normal game";
			break;
		case EDR_SCALEDIFF:
			help = "The ramp is tilted 50%-160% by difficulty";
			break;
		case EDR_SCALE:
		case EDR_SCALEINFO:
		{
			// Sample the curve at its start, pivot, and ceiling using the selected difficulty.
			int n = 0;
			helpBuf[0] = '\0';
			for (unsigned c = 0; c < COUNTOF(endlessCurveZones); ++c)
			{
				EndlessScaling cs;
				endlessScalingSnapshot(endlessCurveZones[c], dbgDiff, dbgMods, &cs);
				const long cv = endlessScaleFieldOf(&cs, rows[s].kind, rows[s].idx);
				// One decimal for the fixed-point pierce lockout (see the row draw above); the help
				// line is only 96 bytes wide, so it gets tenths where the row itself gets hundredths.
				if (rows[s].kind == EDR_SCALE && rows[s].idx == ESO_PIERCELOCK)
					n += snprintf(helpBuf + n, sizeof(helpBuf) - (size_t)n, "%sz%d:%ld.%ld",
					              c ? " " : "", endlessCurveZones[c], cv / 100, (cv % 100) / 10);
				else
					n += snprintf(helpBuf + n, sizeof(helpBuf) - (size_t)n, "%sz%d:%ld",
					              c ? " " : "", endlessCurveZones[c], cv);
				if (n >= (int)sizeof(helpBuf))
					break;
			}
			help = helpBuf;
			break;
		}
		case EDR_ZONE:
#if defined(__SWITCH__) || defined(__vita__)
			help = "Confirm types a number;  L/R  +-10";
#else
			help = "Type a number;  L/R  +-1,  PgUp/PgDn  +-10";
#endif
			break;
		case EDR_BASE:
			help = "The shipped level under the zone";
			break;
		case EDR_OPEN:
			help = rows[s].idx == EDS_SECMOD  ? "Dangers and boons the zone flies with"
			     : rows[s].idx == EDS_BUFMOD  ? "Kill-fire buffs and their evil mirrors"
			     : rows[s].idx == EDS_PERK    ? "Perk stacks the chosen ship carries in"
			     : rows[s].idx == EDS_SCALING ? "Every depth-scaled lever, and how it ramps"
			     :                              "Fire any outcome without paying the fee";
			break;
		case EDR_SHIP:
			help = "Which ship the buffs and perks below belong to";
			break;
		case EDR_ACTION:
			help = rows[s].idx == EDA_CLEAR ? "Turns every modifier and perk off"
			     : rows[s].idx == EDA_UNPIN ? "Hand every lever back to the depth ramp"
			     :                            "Back to what the live run holds now";
			break;
		case EDR_LAUNCH:
			help = jumpMode ? "Jump straight into the zone as set up"
			                : "Apply this slate to play from here on";
			break;
		case EDR_SECMOD:
			help = endlessDebugSectorMods[rows[s].idx].hint
			     ? endlessDebugSectorMods[rows[s].idx].hint
			     : endlessModWord(endlessDebugSectorMods[rows[s].idx].bit);
			break;
		case EDR_BUFMOD:
			help = endlessDebugBuffMods[rows[s].idx].hint
			     ? endlessDebugBuffMods[rows[s].idx].hint
			     : endlessModWord(endlessDebugBuffMods[rows[s].idx].bit);
			break;
		case EDR_PERK:
			help = endlessPerkDesc(rows[s].idx);
			break;
		case EDR_GAMBLE:
			help = (dbgGambleMsg[0] != '\0') ? dbgGambleMsg : "Confirm fires this outcome now";
			break;
		case EDR_LEVEL:
			if (rows[s].idx < 0)
				help = "Let the run's own picker choose a level";
			else
			{
				snprintf(helpBuf, sizeof(helpBuf), "Episode %d   section %d   file %d",
				         allLevelEp[rows[s].idx], allLevelSec[rows[s].idx], allLevelFile[rows[s].idx]);
				help = helpBuf;
			}
			break;
		default:
			break;
		}
		if (help[0] != '\0')
			draw_font_hv(VGAScreen, mid_x, py1 - 18, help, small_font, centered, 15, 1);
		draw_font_hv(VGAScreen, mid_x, py1 - 9, screenKeys[screen], small_font, centered, 15, -3);

		// A scrolling list: this screen keeps the cursor keys (see JE_debugMenu).
		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		push_joysticks_as_keyboard();
		service_SDL_events(true);

		NETWORK_KEEP_ALIVE();  // browsing every level in the game can take a while; hold the link

#if defined(__SWITCH__) || defined(__vita__)
		// Map shoulder-button edges to Page Up and Page Down. A real key wins
		// if both inputs arrive together.
		{
#if defined(__SWITCH__)
			static const int shoulder_btn[2] = { 6, 7 };  // switch-sdl2: 6 = L, 7 = R
#else
			static const int shoulder_btn[2] = { 4, 5 };  // Vita: 4 = L, 5 = R
#endif
			static bool shoulder_was[2];
			for (int b = 0; b < 2; ++b)
			{
				const bool down = joysticks > 0 && joystick[0].handle != NULL &&
				                  SDL_JoystickGetButton(joystick[0].handle, shoulder_btn[b]) != 0;
				if (down && !shoulder_was[b] && !newkey)
				{
					newkey = true;
					lastkey_scan = (b == 0) ? SDL_SCANCODE_PAGEUP : SDL_SCANCODE_PAGEDOWN;
				}
				shoulder_was[b] = down;
			}
		}
#endif

		/* wheel moves the selection; hover highlights; left-click acts on the row (the same thing
		 * Confirm does there); right-click backs out. */
		if (mouse_scroll != 0)
		{
			const int dir = (mouse_scroll > 0) ? -1 : 1;
			for (int n = (mouse_scroll > 0) ? mouse_scroll : -mouse_scroll; n > 0; --n)
				s = pickerRowStep(rows, rowCount, s, dir);
			sel[screen] = s;
			mouse_scroll = 0;
		}
		if (mouse_x != prev_mx || mouse_y != prev_my)
		{
			const int hov = debug_panel_row_at(mouse_x, mouse_y, px0, px1, items_top, row_h,
			                                   visibleRows, scrollTop, rowCount);
			if (hov >= 0 && pickerRowSelectable(&rows[hov]))
				sel[screen] = s = hov;
		}
		prev_mx = mouse_x;
		prev_my = mouse_y;
		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				newkey = true;
				lastkey_scan = SDL_SCANCODE_ESCAPE;
			}
			else
			{
				const int r = debug_panel_row_at(lastmouse_x, lastmouse_y, px0, px1, items_top,
				                                 row_h, visibleRows, scrollTop, rowCount);
				if (r >= 0 && pickerRowSelectable(&rows[r]))
				{
					sel[screen] = s = r;
					newkey = true;
					lastkey_scan = SDL_SCANCODE_RETURN;
				}
			}
			newmouse = false;
		}

		if (newkey)
		{
			const int selKind  = rows[s].kind;
			const int selIdx   = rows[s].idx;
			const int digit    = scancode_digit(lastkey_scan);
			const int keyScreen = screen;   // remember it: a drill-in below may move `screen`

			if (digit >= 0 && selKind == EDR_ZONE)   // digits type a zone number (desktop keyboard)
			{
				if (!dbgZoneTyped)
				{
					dbgZone = digit;
					dbgZoneTyped = true;
				}
				else if (dbgZone < 1000)
					dbgZone = dbgZone * 10 + digit;
				if (dbgZone < 1)
					dbgZone = 1;
			}
			else if (screen != EDS_HUB && lastkey_scan >= SDL_SCANCODE_A && lastkey_scan <= SDL_SCANCODE_Z)
			{
				// letter jump: the fastest way through 250-odd level names on a keyboard
				const char want = (char)('a' + (lastkey_scan - SDL_SCANCODE_A));
				for (int n = 1; n <= rowCount; ++n)
				{
					const int c = (s + n) % rowCount;
					if (!pickerRowSelectable(&rows[c]) || rows[c].label == NULL)
						continue;
					char first = rows[c].label[0];
					if (first >= 'A' && first <= 'Z')
						first = (char)(first - 'A' + 'a');
					if (first == want)
					{
						s = c;
						break;
					}
				}
			}
			else switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				s = pickerRowStep(rows, rowCount, s, -1);
				dbgZoneTyped = false;
				break;
			case SDL_SCANCODE_DOWN:
				s = pickerRowStep(rows, rowCount, s, 1);
				dbgZoneTyped = false;
				break;
			case SDL_SCANCODE_PAGEUP:
			case SDL_SCANCODE_PAGEDOWN:
			{
				const int dir = (lastkey_scan == SDL_SCANCODE_PAGEDOWN) ? 1 : -1;
				if (selKind == EDR_ZONE)   // the Zone row pages by 10; the only fast way there on a pad
				{
					dbgZone += dir * 10;
					if (dbgZone < 1)
						dbgZone = 1;
					if (dbgZone > 9999)
						dbgZone = 9999;
					dbgZoneTyped = false;
				}
				else
					s = pickerRowSnap(rows, rowCount, s + dir * visibleRows);
				break;
			}
			case SDL_SCANCODE_HOME:
				s = pickerRowSnap(rows, rowCount, 0);
				break;
			case SDL_SCANCODE_END:
				s = pickerRowSnap(rows, rowCount, rowCount - 1);
				break;
			case SDL_SCANCODE_BACKSPACE:
				if (selKind == EDR_ZONE)
				{
					dbgZone /= 10;
					if (dbgZone < 1)
						dbgZone = 1;
					dbgZoneTyped = true;
				}
				break;
			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_RIGHT:
			{
				const int dir = (lastkey_scan == SDL_SCANCODE_RIGHT) ? 1 : -1;
				switch (selKind)
				{
				case EDR_ZONE:
					dbgZone += dir;
					if (dbgZone < 1)
						dbgZone = 1;
					if (dbgZone > 9999)
						dbgZone = 9999;
					dbgZoneTyped = false;
					break;
				case EDR_BASE:   // flip between Random and the last level picked from the list
					dbgBase = (dbgBase < 0 && allLevelCount > 0) ? dbgBaseLast : -1;
					break;
				case EDR_CAMPFX:
					dbgCampFx = !dbgCampFx;
					break;
				case EDR_SCALEDIFF:
					// -1 ("current") sits below Wimp, so one step down from it wraps to the top.
					dbgDiff += dir;
					if (dbgDiff < -1)
						dbgDiff = DIFFICULTY_10;
					else if (dbgDiff > DIFFICULTY_10)
						dbgDiff = -1;
					break;
				case EDR_SCALE:
				{
					// Start a newly pinned lever from its current derived value.
					EndlessScalingOverride *ov = &endlessScalingOverride[selIdx];
					const int lo = endlessScalingOverrideMin(selIdx);
					const int hi = endlessScalingOverrideMax(selIdx);
					if (!ov->active)
					{
						ov->value = (int)endlessScaleFieldOf(&sc, EDR_SCALE, selIdx);
						ov->active = true;
					}
					const int step = ((hi - lo) >= 100) ? 5 : 1;
					ov->value += dir * step;
					if (ov->value < lo)
						ov->value = lo;
					if (ov->value > hi)
						ov->value = hi;
					break;
				}
				case EDR_SHIP:
					dbgShip ^= 1;
					break;
				case EDR_SECMOD:
					dbgMods ^= endlessDebugSectorMods[selIdx].bit;
					break;
				case EDR_BUFMOD:
					dbgBuffs[dbgShip] ^= endlessDebugBuffMods[selIdx].bit;
					break;
				case EDR_PERK:
				{
					const int mx = endlessPerkMaxStack(selIdx);
					int *const stack = &dbgPerks[dbgShip][selIdx];
					*stack += dir;                       // wrapping at both ends
					if (*stack < 0)
						*stack = mx;
					else if (*stack > mx)
						*stack = 0;
					break;
				}
				case EDR_LEVEL:
				{
					// jump a whole episode; 250-odd levels is far too many to walk one by one
					const int curEp = (selIdx >= 0) ? allLevelEp[selIdx] : 0;
					int target = -1;
					if (dir > 0)
					{
						for (int r = s + 1; r < rowCount && target < 0; ++r)
							if (rows[r].kind == EDR_LEVEL && rows[r].idx >= 0 && allLevelEp[rows[r].idx] != curEp)
								target = r;
					}
					else
					{
						int firstOfCur = s;   // the top of the block we're standing in
						for (int r = s - 1; r >= 0; --r)
						{
							if (rows[r].kind != EDR_LEVEL)
								continue;
							if (rows[r].idx >= 0 && allLevelEp[rows[r].idx] == curEp)
								firstOfCur = r;
							else
								break;
						}
						if (firstOfCur < s)
							target = firstOfCur;   // not at the top yet: go there first
						else
						{
							int prevEp = 0;
							for (int r = firstOfCur - 1; r >= 0 && prevEp == 0; --r)
								if (rows[r].kind == EDR_LEVEL && rows[r].idx >= 0)
									prevEp = allLevelEp[rows[r].idx];
							if (prevEp != 0)
							{
								for (int r = 0; r < rowCount && target < 0; ++r)
									if (rows[r].kind == EDR_LEVEL && rows[r].idx >= 0 && allLevelEp[rows[r].idx] == prevEp)
										target = r;
							}
							else
								target = 0;        // above episode 1 sits Random
						}
					}
					if (target >= 0)
						s = target;
					break;
				}
				default:
					break;
				}
				break;
			}
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				switch (selKind)
				{
				case EDR_ZONE:
#ifdef PLATFORM_HANDHELD
				{
					// No physical keyboard here: the software keypad is how a far zone
					// gets reached without thousands of button presses.
					char kb[8];
					snprintf(kb, sizeof(kb), "%d", dbgZone);
					if (console_swkbd(kb, sizeof(kb), 4, kb, "Zone number", true))
					{
						int v = 0;
						for (const char *c = kb; *c >= '0' && *c <= '9'; ++c)
							v = v * 10 + (*c - '0');
						dbgZone = (v < 1) ? 1 : (v > 9999) ? 9999 : v;
					}
					dbgZoneTyped = false;
				}
#else
					dbgZoneTyped = false;   // the next digit starts a fresh number
#endif
					break;
				case EDR_BASE:
					focusBase = dbgBase;    // open the list standing on the current pick
					screen = EDS_BASE;
					break;
				case EDR_OPEN:
					screen = selIdx;
					break;
				case EDR_ACTION:
					if (selIdx == EDA_UNPIN)
					{
						endlessScalingOverridesClear();
					}
					else if (selIdx == EDA_CLEAR)
					{
						dbgMods = 0;
						for (uint q = 0; q < COUNTOF(dbgBuffs); ++q)
						{
							dbgBuffs[q] = 0;
							for (int p = 0; p < NPERKS; ++p)
								dbgPerks[q][p] = 0;
						}
						endlessScalingOverridesClear();   // "everything" has to include the pinned levers
					}
					else
					{
						dbgMods = runMods & ~ENDLESS_PERSONAL_MOD_MASK;
						for (uint q = 0; q < COUNTOF(dbgBuffs); ++q)
						{
							dbgBuffs[q] = endlessPersonalBuffMods(q);
							for (int p = 0; p < NPERKS; ++p)
								dbgPerks[q][p] = endlessPerkGetOwnedFor(q, p);
						}
					}
					break;
				case EDR_CAMPFX:
					dbgCampFx = !dbgCampFx;
					break;
				case EDR_SCALEDIFF:
					dbgDiff = (dbgDiff < 0) ? DIFFICULTY_NORMAL : -1;
					break;
				case EDR_SCALE:
					// Pin the lever at whatever the ramp currently says, or hand it back to the ramp.
					if (endlessScalingOverride[selIdx].active)
						endlessScalingOverride[selIdx].active = false;
					else
					{
						endlessScalingOverride[selIdx].value = (int)endlessScaleFieldOf(&sc, EDR_SCALE, selIdx);
						endlessScalingOverride[selIdx].active = true;
					}
					break;
				case EDR_SHIP:
					dbgShip ^= 1;
					break;
				case EDR_SECMOD:
					dbgMods ^= endlessDebugSectorMods[selIdx].bit;
					break;
				case EDR_BUFMOD:
					dbgBuffs[dbgShip] ^= endlessDebugBuffMods[selIdx].bit;
					break;
				case EDR_PERK:
					if (++dbgPerks[dbgShip][selIdx] > endlessPerkMaxStack(selIdx))
						dbgPerks[dbgShip][selIdx] = 0;   // wraps back to off past the max
					break;
				case EDR_GAMBLE:
					endlessForceGambleOutcome(selIdx);
					SDL_strlcpy(dbgGambleMsg, endlessGambleResult(), sizeof(dbgGambleMsg));
					break;
				case EDR_LEVEL:
					dbgBase = selIdx;
					if (selIdx >= 0)
						dbgBaseLast = selIdx;
					screen = EDS_HUB;
					break;
				case EDR_LAUNCH:
					if (!jumpMode)
					{
						done = true;   // the apply happens once, after the loop; Esc takes the same path
						break;
					}
					endlessRunDepth = (dbgZone > 0) ? dbgZone - 1 : 0;  // jump to the chosen zone
					// Apply the chosen combo + each ship's buffs and perks + any gamble outcome
					// fired on the gamble list since this screen opened.
					endlessDebugApplySlate(dbgMods, dbgBuffs, dbgPerks, NPERKS);
					if (dbgBase < 0)
					{
						const JE_byte sec = endlessPickNextLevel();  // random endless-safe level (switches episode)
						if (episodeNum != startEp)
							initial_episode_num = episodeNum;
						shopSelectLevel(sec, 0);
						// Publish the concrete random-level result, not another roll request.
						debug_level_pick_stage((JE_byte)episodeNum, sec, 0);
					}
					else
					{
						if (allLevelEp[dbgBase] != episodeNum)
							JE_initEpisode((JE_byte)allLevelEp[dbgBase]);
						if (episodeNum != startEp)
							initial_episode_num = episodeNum;
						// Sets forcedLvlFileNum.
						shopSelectLevel(allLevelSec[dbgBase], allLevelFile[dbgBase]);
						debug_level_pick_stage((JE_byte)allLevelEp[dbgBase],
						                       (JE_byte)allLevelSec[dbgBase],
						                       (JE_byte)allLevelFile[dbgBase]);
					}
					// ...and the run state around it, which the level pick alone does not carry:
					// the slate the apply above just landed, both ships included.
					endlessJumpPickStage();
					// Publish a debug jump immediately so it can release a peer's course wait.
					network_endless_jump_publish();
					chosen = true;
					done = true;
					break;
				default:
					break;
				}
				break;
			case SDL_SCANCODE_ESCAPE:
				if (screen == EDS_HUB)
					done = true;
				else
					screen = EDS_HUB;   // a list always backs out to the hub, never straight out
				break;
			default:
				break;
			}

			// File the moved selection back under the screen it belongs to; NOT under `screen`,
			// which a drill-in may have just changed (that would drag the old row index with us).
			sel[keyScreen] = s;
			newkey = false;
		}
	}

	wait_noinput(false, false, true);

	VGAScreen = temp_surface;

	// TUNE form: the edits land now, on the way out.
	if (!jumpMode)
	{
		// Arm FIRST: it clears the stale outpost purchases, including the pending kill-fire buy the
		// fold below would otherwise read back out of a finished run.
		if (campaignForm && dbgCampFx && !endlessCampaignMods)
			endlessCampaignModsArm();
		if (campaignForm)
			endlessCampaignMods = dbgCampFx;

		endlessRunDepth = (dbgZone > 0) ? dbgZone - 1 : 0;
		// The panel can change mods mid-level; re-roll what that decides. Only on a real change:
		// the roll draws the endless stream, and the partner adopting this edit draws it once too.
		if (endlessDebugApplySlate(dbgMods, dbgBuffs, dbgPerks, NPERKS))
			endlessRefreshModDerivedState();

		// Persist debug setup immediately; active Endless runs keep their own saved slate.
		save_opentyrian_config();
	}

	touch_ui_clear_layout();   // nothing fades here, so take the cursor keys with the panel

	// A debug "Free perk pick" outcome was fired but we're not launching a level -> open the perk
	// pick now, on return to the menu (the shop's front-gate perk gate already passed this visit).
	if (!chosen && endlessMode && endlessPerkPending)
		shopOpenPendingPerkPick();
	return chosen;
}

/* Campaign debug picker, grouped by episode in one list. */
/* The debug menu's way in: the same screen with no level jump attached, so the effect layer can be
 * retuned mid-game (and, outside endless, switched on for a normal campaign). */
void endlessDebugTuneScreen(void)
{
	(void)endlessDebugScreen(false);
}

bool JE_debugLevelSelect(void)
{
	if (endlessMode)
		return endlessDebugScreen(true);  // endless gets its own Zone / Base Level / Modifiers form

	const JE_byte startEp = episodeNum;
	loadAllLevels();

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	const int pw = 248;
	// Centered within the legacy 320px content area (set_menu_centered(true) is
	// active on this screen); using vga_width would double-offset it.
	const int px0 = (LEGACY_WIDTH - pw) / 2;
	const int px1 = px0 + pw - 1;
	const int py0 = 12;
	const int py1 = vga_height - 12;
	const int title_h = 15;
	const int items_top = py0 + title_h + 3;
	const int items_bottom = py1 - 19;   // two footer lines: the level's details, then the keys
	const int row_h = 10;
	const int pageRows = (items_bottom - items_top) / row_h;
	const int mid_x = (px0 + px1) / 2;
	const int val_x = px1 - 10;          // right edge of the value column, clear of the scrollbar

	// Build the rows once: the level list can't change while the screen is open.
	PickerRow rows[ALL_LEVEL_MAX + EPISODE_MAX];
	char      epHeads[EPISODE_MAX][12];
	int       rowCount = 0;
	int       lastEp = 0;
	for (int i = 0; i < allLevelCount && rowCount < (int)COUNTOF(rows) - 1; ++i)
	{
		const int ep = allLevelEp[i];
		if (ep != lastEp && ep >= 1 && ep <= EPISODE_MAX)
		{
			lastEp = ep;
			snprintf(epHeads[ep - 1], sizeof(epHeads[0]), "EPISODE %d", ep);
			rows[rowCount].kind = EDR_HEADER;
			rows[rowCount].idx = (short)ep;
			rows[rowCount].label = epHeads[ep - 1];
			++rowCount;
		}
		rows[rowCount].kind = EDR_LEVEL;
		rows[rowCount].idx = (short)i;
		rows[rowCount].label = allLevelName[i];
		++rowCount;
	}

	// Open standing on the level being played, so the common case; "replay this one, or the one
	// after it"; needs no scrolling at all.
	int selected = 0;
	for (int r = 0; r < rowCount; ++r)
		if (rows[r].kind == EDR_LEVEL && allLevelEp[rows[r].idx] == episodeNum &&
		    allLevelSec[rows[r].idx] == mainLevel)
		{
			selected = r;
			break;
		}
	selected = pickerRowSnap(rows, rowCount, selected);

	int  scrollTop = 0;
	int  prev_mx = mouse_x, prev_my = mouse_y;  // for motion-based hover
	bool chosen = false, done = false;

	wait_noinput(false, false, true);
	newkey = newmouse = false;  // don't let the click/key that opened us leak in

	while (!done)
	{
		const int visibleRows = (pageRows < rowCount) ? pageRows : rowCount;

		if (rowCount > 0)
		{
			selected = pickerRowSnap(rows, rowCount, selected);

			if (selected < scrollTop)
				scrollTop = selected;
			else if (selected >= scrollTop + visibleRows)
				scrollTop = selected - visibleRows + 1;
			// keep the episode heading on screen with the first level under it
			if (scrollTop > 0 && scrollTop == selected && !pickerRowSelectable(&rows[selected - 1]))
				--scrollTop;
			if (scrollTop > rowCount - visibleRows)
				scrollTop = rowCount - visibleRows;
			if (scrollTop < 0)
				scrollTop = 0;
		}

		debug_panel_frame(px0, py0, px1, py1, title_h, mid_x, "DEBUG  LEVELS");

		if (rowCount == 0)
		{
			draw_font_hv(VGAScreen, mid_x, items_top + 8, "(no levels)", small_font, centered, 15, -2);
		}
		else
		{
			for (int vis = 0; vis < visibleRows; ++vis)
			{
				const int i = scrollTop + vis;
				const int ry = items_top + vis * row_h;
				const bool sel = (i == selected);

				if (rows[i].kind == EDR_HEADER)
				{
					draw_font_hv_shadow(VGAScreen, px0 + 6, ry, rows[i].label, small_font, left_aligned, 15, 3, true, 1);
					const int rule_x = px0 + 10 + JE_textWidth(rows[i].label, small_font);
					if (rule_x < val_x)
						fill_rectangle_xy(VGAScreen, rule_x, ry + 3, val_x, ry + 3, PANEL_DIVIDER);
					continue;
				}

				if (sel)
					fill_rectangle_xy(VGAScreen, px0 + 3, ry - 1, px1 - 3, ry + row_h - 2,
					                  PANEL_SEL_BAR);

				const int lv = rows[i].idx;
				// The level you are standing on right now, called out so a jump can't land back
				// where it started by accident.
				const bool here = (allLevelEp[lv] == episodeNum && allLevelSec[lv] == mainLevel);

				char info[12];
				snprintf(info, sizeof(info), "L%d", allLevelSec[lv]);

				draw_font_hv_shadow(VGAScreen, px0 + 16, ry, rows[i].label, small_font, left_aligned,
				                    15, sel ? 5 : (here ? 4 : -1), true, 1);
				draw_font_hv_shadow(VGAScreen, val_x, ry, info, small_font, right_aligned,
				                    15, sel ? 4 : (here ? 3 : -2), true, 1);

				if (sel)
					draw_font_hv(VGAScreen, px0 + 5, ry, ">", small_font, left_aligned, 15, 6);
			}

			/* scrollbar (only when the list overflows the window) */
			if (rowCount > visibleRows)
			{
				const int track_top = items_top - 1;
				const int track_bot = items_top + visibleRows * row_h - 2;
				const int track_h = track_bot - track_top;
				fill_rectangle_xy(VGAScreen, px1 - 4, track_top, px1 - 3, track_bot, PANEL_EDGE_LO);

				int thumb_h = track_h * visibleRows / rowCount;
				if (thumb_h < 4)
					thumb_h = 4;
				const int denom = rowCount - visibleRows;
				const int thumb_y = track_top + (denom > 0 ? (track_h - thumb_h) * scrollTop / denom : 0);
				fill_rectangle_xy(VGAScreen, px1 - 4, thumb_y, px1 - 3, thumb_y + thumb_h,
				                  PANEL_EDGE_HI);
			}
		}

		// Footer line 1: where the selected level actually lives, which is what you need when a
		// jump misbehaves; the episode it belongs to, its section, and its level file.
		if (rowCount > 0 && rows[selected].kind == EDR_LEVEL)
		{
			const int lv = rows[selected].idx;
			char detail[64];
			snprintf(detail, sizeof(detail), "Episode %d   section %d   file %d",
			         allLevelEp[lv], allLevelSec[lv], allLevelFile[lv]);
			draw_font_hv(VGAScreen, mid_x, py1 - 18, detail, small_font, centered, 15, 1);
		}
#if defined(__SWITCH__) || defined(__vita__)
		draw_font_hv(VGAScreen, mid_x, py1 - 9, "A Play   B Back   L/R Episode",
		             small_font, centered, 15, -3);
#else
		draw_font_hv(VGAScreen, mid_x, py1 - 9, "Enter Play   L/R Episode   Esc Back",
		             small_font, centered, 15, -3);
#endif

		// A scrolling list: this screen keeps the cursor keys (see JE_debugMenu).
		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		NETWORK_KEEP_ALIVE();  // browsing every level in the game can take a while; hold the link

#if defined(__SWITCH__) || defined(__vita__)
		// The shoulder buttons page the list, read raw and synthesized into PageUp/PageDown; same
		// pattern as endlessDebugScreen; see the note there.
		{
#if defined(__SWITCH__)
			static const int shoulder_btn[2] = { 6, 7 };  // switch-sdl2: 6 = L, 7 = R
#else
			static const int shoulder_btn[2] = { 4, 5 };  // Vita: 4 = L, 5 = R
#endif
			static bool shoulder_was[2];
			for (int b = 0; b < 2; ++b)
			{
				const bool down = joysticks > 0 && joystick[0].handle != NULL &&
				                  SDL_JoystickGetButton(joystick[0].handle, shoulder_btn[b]) != 0;
				if (down && !shoulder_was[b] && !newkey)
				{
					newkey = true;
					lastkey_scan = (b == 0) ? SDL_SCANCODE_PAGEUP : SDL_SCANCODE_PAGEDOWN;
				}
				shoulder_was[b] = down;
			}
		}
#endif

		/* wheel moves the selection; hover highlights on pointer motion; a click plays the row
		 * under it; right-click backs out. */
		if (mouse_scroll != 0)
		{
			const int dir = (mouse_scroll > 0) ? -1 : 1;
			for (int n = (mouse_scroll > 0) ? mouse_scroll : -mouse_scroll; n > 0 && rowCount > 0; --n)
				selected = pickerRowStep(rows, rowCount, selected, dir);
			mouse_scroll = 0;
		}
		if (rowCount > 0 && (mouse_x != prev_mx || mouse_y != prev_my))
		{
			const int hov = debug_panel_row_at(mouse_x, mouse_y, px0, px1, items_top, row_h,
			                                   visibleRows, scrollTop, rowCount);
			if (hov >= 0 && pickerRowSelectable(&rows[hov]))
				selected = hov;
		}
		prev_mx = mouse_x;
		prev_my = mouse_y;
		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				newkey = true;
				lastkey_scan = SDL_SCANCODE_ESCAPE;
			}
			else
			{
				const int r = debug_panel_row_at(lastmouse_x, lastmouse_y, px0, px1, items_top,
				                                 row_h, visibleRows, scrollTop, rowCount);
				if (r >= 0 && pickerRowSelectable(&rows[r]))
				{
					selected = r;
					newkey = true;
					lastkey_scan = SDL_SCANCODE_RETURN;
				}
			}
			newmouse = false;
		}

		if (newkey)
		{
			if (rowCount > 0 && lastkey_scan >= SDL_SCANCODE_A && lastkey_scan <= SDL_SCANCODE_Z)
			{
				// letter jump: the fastest way through 250-odd level names on a keyboard
				const char want = (char)('a' + (lastkey_scan - SDL_SCANCODE_A));
				for (int n = 1; n <= rowCount; ++n)
				{
					const int c = (selected + n) % rowCount;
					if (!pickerRowSelectable(&rows[c]) || rows[c].label == NULL)
						continue;
					char first = rows[c].label[0];
					if (first >= 'A' && first <= 'Z')
						first = (char)(first - 'A' + 'a');
					if (first == want)
					{
						selected = c;
						break;
					}
				}
			}
			else switch (lastkey_scan)
			{
			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_RIGHT:
			{
				// jump a whole episode; what the old per-episode paging did, without the mode
				const int dir = (lastkey_scan == SDL_SCANCODE_RIGHT) ? 1 : -1;
				if (rowCount == 0)
					break;
				const int curEp = allLevelEp[rows[selected].idx];
				int target = -1;
				if (dir > 0)
				{
					for (int r = selected + 1; r < rowCount && target < 0; ++r)
						if (rows[r].kind == EDR_LEVEL && allLevelEp[rows[r].idx] != curEp)
							target = r;
				}
				else
				{
					int firstOfCur = selected;   // the top of the block we're standing in
					for (int r = selected - 1; r >= 0; --r)
					{
						if (rows[r].kind != EDR_LEVEL)
							continue;
						if (allLevelEp[rows[r].idx] == curEp)
							firstOfCur = r;
						else
							break;
					}
					if (firstOfCur < selected)
						target = firstOfCur;   // not at the top of this episode yet: go there first
					else
					{
						int prevEp = 0;
						for (int r = firstOfCur - 1; r >= 0 && prevEp == 0; --r)
							if (rows[r].kind == EDR_LEVEL)
								prevEp = allLevelEp[rows[r].idx];
						if (prevEp != 0)
						{
							for (int r = 0; r < rowCount && target < 0; ++r)
								if (rows[r].kind == EDR_LEVEL && allLevelEp[rows[r].idx] == prevEp)
									target = r;
						}
					}
				}
				if (target >= 0)
					selected = target;
				break;
			}
			case SDL_SCANCODE_UP:
				selected = pickerRowStep(rows, rowCount, selected, -1);
				break;
			case SDL_SCANCODE_DOWN:
				selected = pickerRowStep(rows, rowCount, selected, 1);
				break;
			case SDL_SCANCODE_PAGEUP:
				selected = pickerRowSnap(rows, rowCount, selected - visibleRows);
				break;
			case SDL_SCANCODE_PAGEDOWN:
				selected = pickerRowSnap(rows, rowCount, selected + visibleRows);
				break;
			case SDL_SCANCODE_HOME:
				selected = pickerRowSnap(rows, rowCount, 0);
				break;
			case SDL_SCANCODE_END:
				selected = pickerRowSnap(rows, rowCount, rowCount - 1);
				break;
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				if (rowCount > 0 && rows[selected].kind == EDR_LEVEL)
				{
					// Snapshot the return point before switching to the target episode.
					const int lv = rows[selected].idx;
					select_debug_level_capture();
					JE_initEpisode((JE_byte)allLevelEp[lv]);  // no-op if already current
					if (episodeNum != startEp)
						initial_episode_num = episodeNum;
					shopSelectLevel(allLevelSec[lv], allLevelFile[lv]);
					// Network game: hand the pick to the other player at the shop rendezvous.
					debug_level_pick_stage((JE_byte)allLevelEp[lv], allLevelSec[lv], allLevelFile[lv]);
					chosen = true;
					done = true;
				}
				break;
			case SDL_SCANCODE_ESCAPE:
				done = true;
				break;
			default:
				break;
			}

			newkey = false;
		}
	}

	touch_ui_clear_layout();   // nothing fades here, so take the cursor keys with the panel
	wait_noinput(false, false, true);

	VGAScreen = temp_surface;
	return chosen;
}
