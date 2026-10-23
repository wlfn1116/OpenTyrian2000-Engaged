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
#include "weapon_creator.h"

#include "backgrnd.h"
#include "config.h"
#include "console_platform.h"
#include "custom_weapon.h"
#include "episodes.h"
#include "font.h"
#include "fonthand.h"
#include "game_menu.h"
#include "joystick.h"
#include "keyboard.h"
#include "lvlmast.h"
#include "mouse.h"
#include "network.h"
#include "nortsong.h"
#include "palette.h"
#include "picload.h"
#include "player.h"
#include "render_list.h"
#include "shots.h"
#include "sndmast.h"
#include "sparks.h"
#include "sprite.h"
#include "touch_ui.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Custom Weapon Creator: live firing preview on the left, editor on the right.
 * Every edit materializes the weapon again so the preview stays current. */

enum  // rows of the editor list (weapon-wide, per-bullet, import, then actions)
{
	// "Library": which saved weapon is loaded, plus New / Duplicate / Delete
	CWROW_LIB_SELECT = 0, // which saved weapon (library slot) is loaded
	CWROW_LIB_NEW,        // inline action: add a fresh weapon
	CWROW_LIB_DUPLICATE,  // inline action: copy the current weapon
	CWROW_LIB_DELETE,     // inline action: remove the current weapon
	// Import actions sit directly above the import picker they act on
	CWROW_IMPORT_THIS, // inline action: import the source into this level
	CWROW_IMPORT_ALL,  // inline action: clone the whole source weapon
	CWROW_ADD_THIS,    // inline action: combine the source onto this level
	CWROW_ADD_ALL,     // inline action: combine the source onto every level
	CWROW_IMPORT_SRC,
	CWROW_IMPORT_PWR,
	// "Setup": weapon-wide identity + shop presentation
	CWROW_NAME,
	CWROW_EQUIP_SLOT,
	CWROW_COST,
	CWROW_ICON,        // weaponPort itemgraphic (shop/HUD icon)
	CWROW_POWER_USE,
	// Level controls also select charge stages for sidekick designs.
	CWROW_POWER_LEVEL,
	CWROW_TWO_MODES,   // opnum 1 or 2 (does a rear-gun toggle give a 2nd design?)
	CWROW_FIRE_MODE,   // which fire mode is being edited
	CWROW_CHARGE,      // sidekick charge stages (option pwr; the power levels ARE the stages)
	CWROW_ADD_CHARGE,    // inline action: add a charge state (sidekick only)
	CWROW_REMOVE_CHARGE, // inline action: remove a charge state
	CWROW_AUTOSCALE,   // inline action: build all levels from this one, scaled up + down
	CWROW_COPY_ALL,    // inline action: copy this level to every level (flatten)
	CWROW_COPY_LEVEL,  // inline action: copy this level for a later paste
	CWROW_PASTE_LEVEL, // inline action: replace this level from the editor clipboard
	// "Firing": this mode + power level's whole-volley raw fields
	CWROW_FIRE_RATE,   // shotrepeat
	CWROW_BULLETS,     // multi
	CWROW_PATTERN,     // max
	CWROW_ANIM,        // weapani
	CWROW_HOMING,      // aim
	CWROW_ACCEL_Y,     // acceleration
	CWROW_ACCEL_X,     // accelerationx
	CWROW_SPIRAL,      // circlesize
	CWROW_SOUND,       // sound
	CWROW_TRAIL,       // trail
	CWROW_BLAST,       // shipblastfilter
	// The selected bullet's per-bullet fields, with the add/remove actions right above
	CWROW_ADD_SEGMENT,     // inline action: duplicate the selected bullet
	CWROW_REMOVE_SEGMENT,  // inline action: delete the selected bullet
	CWROW_BULLET_SEL,  // which bullet the rows below edit
	CWROW_B_SPRITE,    // sg
	CWROW_B_DAMAGE,    // attack (1-98 dmg, 99 Ice)
	CWROW_B_CHAIN,     // attack 101-249: explode into weapon N on enemy hit
	CWROW_B_LIFE,      // del
	CWROW_B_VELX,      // sx
	CWROW_B_VELY,      // sy
	CWROW_B_OFFX,      // bx
	CWROW_B_OFFY,      // by
	// "Sidekick": the sidekick body's mount + sprite + animation (used when equipped as a sidekick)
	CWROW_SK_MOUNT,    // option tr: Side / Trail Big / Front / Trail / Orbit
	CWROW_SK_SPRITE,   // base body sprite (gr[0]); sheet depends on the mount
	CWROW_SK_FRAMES,   // animation frame count (ani)
	CWROW_SK_STEP,     // sprite step between animation frames
	CWROW_SK_ANIMATE,  // animate while firing / always
	// "Preview": practice targets / stats / ship position (editor-only test harness, shown last)
	CWROW_SHOW_TARGETS,// preview toggle: practice dummies so on-hit behaviour is visible
	CWROW_SHOW_STATS,  // preview toggle: live DPS / damage / fire-rate readout
	CWROW_TARGET_COUNT,// how many practice dummies
	CWROW_TARGET_SPREAD,// horizontal spacing between dummies
	CWROW_TARGET_HEIGHT,// vertical position of the dummy row
	CWROW_TARGET_X,    // horizontal position of the dummy row
	CWROW_TARGET_ARMOR,// dummy toughness (how fast the armour bar drains)
	CWROW_SHIP_X,      // preview ship X = where shots are fired from
	CWROW_SHIP_Y,      // preview ship Y
	CWROW_FIELD_COUNT
};

// Shared by inline actions and the buttons at the bottom.
enum
{
	CWACT_ADD_BULLET,
	CWACT_REMOVE_BULLET,
	CWACT_ADD_CHARGE,
	CWACT_REMOVE_CHARGE,
	CWACT_IMPORT_LEVEL,
	CWACT_IMPORT_ALL,
	CWACT_COPY_ALL,
	CWACT_RANDOMIZE,
	CWACT_RESET,
	CWACT_RESET_ALL,
	CWACT_EQUIP,
	CWACT_DONE,
	CWACT_LIB_NEW,
	CWACT_LIB_DUPLICATE,
	CWACT_LIB_DELETE,
	CWACT_AUTOSCALE,
	CWACT_SAVE,
	CWACT_ADD_LEVEL,
	CWACT_ADD_ALL_LEVELS,
	CWACT_COPY_LEVEL,
	CWACT_PASTE_LEVEL,
	CWACT_UNDO,
	CWACT_REDO,
	CWACT_COUNT
};

static const char *const cwActLabel[CWACT_COUNT] =
{
	"ADD SEGMENT",
	"REMOVE SEGMENT",
	"ADD CHARGE STATE",
	"REMOVE CHARGE STATE",
	"IMPORT THIS LEVEL",
	"IMPORT ALL LEVELS",
	"COPY TO ALL",
	"RANDOMIZE",
	"RESET",
	"RESET ALL",
	"EQUIP",
	"DONE",
	"NEW WEAPON",
	"DUPLICATE",
	"DELETE WEAPON",
	"AUTO-SCALE LEVELS",
	"SAVE",
	"ADD THIS LEVEL",
	"ADD ALL LEVELS",
	"COPY LEVEL",
	"PASTE LEVEL",
	"UNDO",
	"REDO"
};

// Map an inline action row to its action id; returns -1 for a normal (adjustable) field row.
static int cwInlineActionId(int row)
{
	switch (row)
	{
	case CWROW_ADD_SEGMENT:    return CWACT_ADD_BULLET;
	case CWROW_REMOVE_SEGMENT: return CWACT_REMOVE_BULLET;
	case CWROW_ADD_CHARGE:     return CWACT_ADD_CHARGE;
	case CWROW_REMOVE_CHARGE:  return CWACT_REMOVE_CHARGE;
	case CWROW_AUTOSCALE:      return CWACT_AUTOSCALE;
	case CWROW_COPY_ALL:       return CWACT_COPY_ALL;
	case CWROW_COPY_LEVEL:     return CWACT_COPY_LEVEL;
	case CWROW_PASTE_LEVEL:    return CWACT_PASTE_LEVEL;
	case CWROW_IMPORT_THIS:    return CWACT_IMPORT_LEVEL;
	case CWROW_IMPORT_ALL:     return CWACT_IMPORT_ALL;
	case CWROW_ADD_THIS:       return CWACT_ADD_LEVEL;
	case CWROW_ADD_ALL:        return CWACT_ADD_ALL_LEVELS;
	case CWROW_LIB_NEW:        return CWACT_LIB_NEW;
	case CWROW_LIB_DUPLICATE:  return CWACT_LIB_DUPLICATE;
	case CWROW_LIB_DELETE:     return CWACT_LIB_DELETE;
	default:                   return -1;
	}
}

// The category selector filters editor rows. Their order follows the workflow
// from saved design and import through setup, tuning, and preview.
enum
{
	CWCAT_ALL = 0,   // every row (the classic flat list)
	CWCAT_LIBRARY,   // pick / add / delete saved weapons
	CWCAT_IMPORT,    // seed the design from a stock weapon
	CWCAT_SETUP,     // weapon-wide identity: name / equip / cost / icon / power
	CWCAT_LEVELS,    // power-level / fire-mode variant, charge stages, fill-all actions
	CWCAT_FIRING,    // this design's whole-volley fields: rate / bullets / homing / ...
	CWCAT_BULLET,    // per-bullet segment editing
	CWCAT_SIDEKICK,  // sidekick body: mount / sprite / animation (when equipped as a sidekick)
	CWCAT_PREVIEW,   // preview / test options: target dummies, stats, ship position
	CWCAT_COUNT
};
static const char *const cwCatName[CWCAT_COUNT] = { "All", "Library", "Import", "Setup", "Levels", "Firing", "Bullet", "Sidekick", "Preview" };

// Which category a field row belongs to (also drives its colour band).
static int cwRowCategory(int row)
{
	switch (row)
	{
	case CWROW_LIB_SELECT: case CWROW_LIB_NEW: case CWROW_LIB_DUPLICATE: case CWROW_LIB_DELETE:
		return CWCAT_LIBRARY;
	case CWROW_POWER_LEVEL: case CWROW_TWO_MODES: case CWROW_FIRE_MODE:
	case CWROW_CHARGE: case CWROW_ADD_CHARGE: case CWROW_REMOVE_CHARGE:
	case CWROW_AUTOSCALE: case CWROW_COPY_ALL: case CWROW_COPY_LEVEL: case CWROW_PASTE_LEVEL:
		return CWCAT_LEVELS;
	case CWROW_SHOW_TARGETS: case CWROW_SHOW_STATS:
	case CWROW_TARGET_COUNT: case CWROW_TARGET_SPREAD: case CWROW_TARGET_HEIGHT:
	case CWROW_TARGET_X: case CWROW_TARGET_ARMOR: case CWROW_SHIP_X: case CWROW_SHIP_Y:
		return CWCAT_PREVIEW;
	case CWROW_NAME: case CWROW_EQUIP_SLOT: case CWROW_COST: case CWROW_ICON:
	case CWROW_POWER_USE:
		return CWCAT_SETUP;
	case CWROW_SK_MOUNT: case CWROW_SK_SPRITE: case CWROW_SK_FRAMES:
	case CWROW_SK_STEP: case CWROW_SK_ANIMATE:
		return CWCAT_SIDEKICK;
	case CWROW_FIRE_RATE: case CWROW_BULLETS: case CWROW_PATTERN: case CWROW_ANIM:
	case CWROW_HOMING: case CWROW_ACCEL_Y: case CWROW_ACCEL_X: case CWROW_SPIRAL:
	case CWROW_SOUND: case CWROW_TRAIL: case CWROW_BLAST:
		return CWCAT_FIRING;
	case CWROW_ADD_SEGMENT: case CWROW_REMOVE_SEGMENT: case CWROW_BULLET_SEL:
	case CWROW_B_SPRITE: case CWROW_B_DAMAGE: case CWROW_B_CHAIN: case CWROW_B_LIFE:
	case CWROW_B_VELX: case CWROW_B_VELY: case CWROW_B_OFFX: case CWROW_B_OFFY:
		return CWCAT_BULLET;
	case CWROW_IMPORT_THIS: case CWROW_IMPORT_ALL: case CWROW_ADD_THIS: case CWROW_ADD_ALL:
	case CWROW_IMPORT_SRC: case CWROW_IMPORT_PWR:
		return CWCAT_IMPORT;
	default:
		return CWCAT_SETUP;
	}
}

// Dark, muted colour bands from the shop backdrop palette (palette 0) so the bright row
// text stays legible over them; one per category, plus grey for the pinned action buttons.
enum
{
	CW_BG_LIBRARY  = 0x82,  // steel  - Library (pick / add / delete weapons)
	CW_BG_LEVELS   = 0x91,  // navy   - Levels (power level / modes / charge / fill-all)
	CW_BG_PREVIEW  = 0x63,  // slate  - Preview (targets / stats / ship)
	CW_BG_SETUP    = 0x53,  // purple - Setup (name / equip / cost / icon / power)
	CW_BG_SIDEKICK = 0x72,  // olive  - Sidekick (mount / sprite / animation) - tunable
	CW_BG_FIRING   = 0x24,  // green  - Firing (rate / bullets / homing / ...)
	CW_BG_BULLET   = 0xD2,  // teal   - Bullet (per-segment editing)
	CW_BG_IMPORT   = 0x13,  // amber  - Import (clone a stock weapon)
	CW_BG_ACTION   = 0x03,  // grey   - action buttons (randomize/reset/equip/done)
};

// The colour band for a category.
static Uint8 cwCatColor(int cat)
{
	switch (cat)
	{
	case CWCAT_LIBRARY:  return CW_BG_LIBRARY;
	case CWCAT_LEVELS:   return CW_BG_LEVELS;
	case CWCAT_PREVIEW:  return CW_BG_PREVIEW;
	case CWCAT_SETUP:    return CW_BG_SETUP;
	case CWCAT_SIDEKICK: return CW_BG_SIDEKICK;
	case CWCAT_FIRING:   return CW_BG_FIRING;
	case CWCAT_BULLET:   return CW_BG_BULLET;
	case CWCAT_IMPORT:   return CW_BG_IMPORT;
	default:             return CW_BG_ACTION;
	}
}

// The colour band for a field row, derived from its category.
static Uint8 cwGroupColor(int row) { return cwCatColor(cwRowCategory(row)); }

// Some rows only make sense for a particular Equip slot: the fire-mode toggle exists only on a
// rear gun, and charging is a sidekick-only mechanic (a front/rear custom gun can't charge).
static bool cwRowActive(int row)
{
	const int slot = customWeaponEquipSlot;
	const bool isRear  = (slot == CUSTOM_EQUIP_REAR);
	const bool isSidekick = (slot == CUSTOM_EQUIP_LEFT || slot == CUSTOM_EQUIP_RIGHT || slot == CUSTOM_EQUIP_BOTH);
	switch (row)
	{
	case CWROW_TWO_MODES: case CWROW_FIRE_MODE:
		return isRear;    // a fire-mode toggle only happens on a rear gun
	case CWROW_CHARGE: case CWROW_ADD_CHARGE: case CWROW_REMOVE_CHARGE:
		return isSidekick;   // charging only works for a sidekick
	case CWROW_SK_MOUNT: case CWROW_SK_SPRITE: case CWROW_SK_FRAMES:
	case CWROW_SK_STEP: case CWROW_SK_ANIMATE:
		return isSidekick;   // the sidekick body only matters when equipped as a sidekick
	default:
		return true;
	}
}

// Why a greyed row is inactive (shown on the hint line so it isn't a mystery).
static const char *cwRowInactiveReason(int row)
{
	switch (row)
	{
	case CWROW_TWO_MODES: case CWROW_FIRE_MODE:
		return "Only used on a Rear Gun - change Equip To";
	case CWROW_CHARGE: case CWROW_ADD_CHARGE: case CWROW_REMOVE_CHARGE:
	case CWROW_SK_MOUNT: case CWROW_SK_SPRITE: case CWROW_SK_FRAMES:
	case CWROW_SK_STEP: case CWROW_SK_ANIMATE:
		return "Only used on a Sidekick - change Equip To";
	default:
		return "Not used with the current Equip To";
	}
}

static const char *const cwSlotNames[] = { "Front Gun", "Rear Gun", "Left Sidekick", "Right Sidekick", "Both Sidekicks" };

// Sidekick mount styles, indexed by the engine's option "tr" (0..4). "Trail Big" (tr 1) and
// "Front" (tr 2) draw a 2x2 body from spriteSheet10; the rest a single tile from spriteSheet9.
static const char *const cwMountNames[] = { "Side Pod", "Trail (Big)", "Front", "Trailing", "Orbit" };

// Editor-only selection state (not part of the saved design).
static int cwImportSrc = 0;   // index into customBulletPreset[] to import from
static int cwImportPwr = 1;   // source power level for "Import This Level"
static int cwBulletSel = 0;   // which bullet the per-bullet rows edit

// Combat-preview settings (editor-only, not part of the saved design; kept across opens).
static bool cwShowTargets  = true;   // practice target dummies in the box
static bool cwShowStats    = true;   // live DPS / damage / fire-rate readout
static int  cwTargetCount  = 3;      // number of dummies (0..CW_DUMMY_MAX)
static int  cwTargetSpread = 32;     // horizontal spacing between dummies
static int  cwTargetHeight = 48;     // vertical position of the dummy row
static int  cwTargetX      = 76;     // horizontal centre of the dummy row (box centre ~76)
static int  cwTargetArmor  = 120;    // dummy toughness
static int  cwShipX = 70;            // preview ship position = shot origin (drives player[0].x each tick)
static int  cwShipY = 110;
static int  cwCategory = CWCAT_ALL;  // which row category the top selector is showing

static JE_WeaponType cwLevelClipboard;
static bool cwLevelClipboardSet = false;

// Direct numeric entry: on a numeric value row, typing digits builds an exact value (so a shop
// price of 900, or sprite 214, doesn't need scrolling in coarse steps).
static int  cwNumRow = -1;
static char cwNumText[12];

static char cwNotice[64];
static int  cwNoticeTicks = 0;
static int  cwDeleteConfirmTicks = 0;

static int cwClamp(int v, int lo, int hi)
{
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void cwSetNotice(const char *text)
{
	SDL_strlcpy(cwNotice, text, sizeof(cwNotice));
	cwNoticeTicks = 40;
}

static void cwClearPreviewShots(void)
{
	memset(shotAvail, 0, sizeof(shotAvail));
	memset(shotRepeat, 1, sizeof(shotRepeat));
	memset(shotMultiPos, 0, sizeof(shotMultiPos));
	for (int i = 0; i < MAX_EXPLOSIONS; ++i)
		explosions[i].ttl = 0;
}

// Undo tracks the working weapon only. Switching library slots starts a fresh history.
typedef struct
{
	CustomWeaponSlot weapon;
} CwHistoryState;

enum { CW_HISTORY_MAX = 16 };
static CwHistoryState cwHistory[CW_HISTORY_MAX];
static int cwHistoryCount = 0;
static int cwHistoryPos = 0;

static void cwCaptureWorking(CwHistoryState *state)
{
	memset(state, 0, sizeof(*state));
	CustomWeaponSlot *s = &state->weapon;
	SDL_strlcpy(s->name, customWeaponName, sizeof(s->name));
	s->cost         = customWeaponCost;
	s->powerUse     = customWeaponPowerUse;
	s->equipSlot    = customWeaponEquipSlot;
	s->itemGraphic  = customWeaponItemGraphic;
	s->chargeStages = customWeaponChargeStages;
	s->modes        = customWeaponModes;
	s->sidekickMount     = customSidekickMount;
	s->sidekickSprite    = customSidekickSprite;
	s->sidekickFrames    = customSidekickFrames;
	s->sidekickFrameStep = customSidekickFrameStep;
	s->sidekickAnimate   = customSidekickAnimate;
	memcpy(s->raw, customWeaponRaw, sizeof(customWeaponRaw));
}

static void cwRestoreWorking(const CwHistoryState *state)
{
	const CustomWeaponSlot *s = &state->weapon;
	SDL_strlcpy(customWeaponName, s->name, sizeof(customWeaponName));
	customWeaponCost         = s->cost;
	customWeaponPowerUse     = s->powerUse;
	customWeaponEquipSlot    = s->equipSlot;
	customWeaponItemGraphic  = s->itemGraphic;
	customWeaponChargeStages = s->chargeStages;
	customWeaponModes        = s->modes;
	customSidekickMount      = s->sidekickMount;
	customSidekickSprite     = s->sidekickSprite;
	customSidekickFrames     = s->sidekickFrames;
	customSidekickFrameStep  = s->sidekickFrameStep;
	customSidekickAnimate    = s->sidekickAnimate;
	memcpy(customWeaponRaw, s->raw, sizeof(customWeaponRaw));

	customWeaponEditMode = cwClamp(customWeaponEditMode, 0, customWeaponModes - 1);
	customWeaponMaterialize();
	cwClearPreviewShots();
}

static void cwHistoryReset(void)
{
	cwHistoryCount = 1;
	cwHistoryPos = 0;
	cwCaptureWorking(&cwHistory[0]);
}

static bool cwHistoryRecord(void)
{
	static CwHistoryState next;
	cwCaptureWorking(&next);
	if (cwHistoryCount > 0 && memcmp(&next, &cwHistory[cwHistoryPos], sizeof(next)) == 0)
		return false;

	cwHistoryCount = cwHistoryPos + 1;  // discard redo states after a new edit
	if (cwHistoryCount == CW_HISTORY_MAX)
	{
		memmove(&cwHistory[0], &cwHistory[1], sizeof(cwHistory[0]) * (CW_HISTORY_MAX - 1));
		--cwHistoryCount;
		--cwHistoryPos;
	}
	cwHistory[++cwHistoryPos] = next;
	cwHistoryCount = cwHistoryPos + 1;
	return true;
}

static bool cwHistoryUndo(void)
{
	if (cwHistoryPos <= 0)
		return false;
	cwRestoreWorking(&cwHistory[--cwHistoryPos]);
	cwSetNotice("Undo");
	return true;
}

static bool cwHistoryRedo(void)
{
	if (cwHistoryPos + 1 >= cwHistoryCount)
		return false;
	cwRestoreWorking(&cwHistory[++cwHistoryPos]);
	cwSetNotice("Redo");
	return true;
}

// Current mode + level's raw weapon (edits happen in place).
static JE_WeaponType *cwCur(void)
{
	const int m = cwClamp(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int p = cwClamp(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	return &customWeaponRaw[m][p];
}

// The selected bullet index, clamped to the weapon's current bullet count.
static int cwBulletIndex(void)
{
	int mx = cwCur()->multi;
	if (mx < 1) mx = 1;
	if (mx > CUSTOM_BULLETS_MAX) mx = CUSTOM_BULLETS_MAX;
	return cwClamp(cwBulletSel, 0, mx - 1);
}

static bool cwActionAvailable(int act)
{
	switch (act)
	{
	case CWACT_ADD_BULLET:    return cwCur()->multi < CUSTOM_BULLETS_MAX;
	case CWACT_REMOVE_BULLET: return cwCur()->multi > 1;
	case CWACT_ADD_CHARGE:    return customWeaponChargeStages < CUSTOM_POWER_LEVELS;
	case CWACT_REMOVE_CHARGE: return customWeaponChargeStages > 1;
	case CWACT_LIB_NEW:
	case CWACT_LIB_DUPLICATE:  return customWeaponLibCount < CUSTOM_WEAPON_LIB_MAX;
	case CWACT_LIB_DELETE:     return customWeaponLibCount > 1;
	case CWACT_PASTE_LEVEL:    return cwLevelClipboardSet;
	case CWACT_UNDO:           return cwHistoryPos > 0;
	case CWACT_REDO:           return cwHistoryPos + 1 < cwHistoryCount;
	default:                   return true;
	}
}

static const char *cwActionText(int act)
{
	if (act == CWACT_LIB_DELETE && cwDeleteConfirmTicks > 0)
		return "CONFIRM DELETE";
	return cwActLabel[act];
}

static const char *cwActionHelp(int act)
{
	switch (act)
	{
	case CWACT_UNDO:       return "Undo the last edit (Ctrl+Z)";
	case CWACT_REDO:       return "Redo the last undone edit (Ctrl+Y)";
	case CWACT_RANDOMIZE:  return "Randomize this level and name; Undo restores them";
	case CWACT_SAVE:       return "Save now (Ctrl+S); leaving also saves";
	case CWACT_RESET:      return "Reset this level; Undo restores it";
	case CWACT_RESET_ALL:  return "Reset the entire weapon; Undo restores it";
	case CWACT_EQUIP:      return "Equip this weapon and return";
	case CWACT_DONE:       return "Save and return";
	default:               return "Enter: use";
	}
}

// Preview dummies expose damage, ice, chaining, and homing behavior. Layout comes
// from cwLayoutDummies; collision and drawing live near JE_customWeaponCreator.
typedef struct { int cx, cy, armor, hit, ice, dead; } CwDummy;
enum { CW_DUMMY_MAX = 6, CW_DUMMY_R = 8 };
static CwDummy cwDummies[CW_DUMMY_MAX];
static int cwDummyCount = 0;

// Lay the active dummies out as a centred horizontal row from the current settings,
// resetting each to full armour. Called at creator entry and whenever a setting changes.
static void cwLayoutDummies(void)
{
	cwDummyCount = cwClamp(cwTargetCount, 0, CW_DUMMY_MAX);
	for (int i = 0; i < cwDummyCount; ++i)
	{
		const int off = 2 * i - (cwDummyCount - 1);   // symmetric fan: - .. 0 .. +
		cwDummies[i].cx    = cwClamp(cwTargetX + off * cwTargetSpread / 2, 8 + CW_DUMMY_R, 143 - CW_DUMMY_R);
		cwDummies[i].cy    = cwClamp(cwTargetHeight, 8 + CW_DUMMY_R, 168);
		cwDummies[i].armor = cwTargetArmor;
		cwDummies[i].hit   = cwDummies[i].ice = cwDummies[i].dead = 0;
	}
}

// Decoders for sentinel-encoded fields.
static void cwFmtDamage(int a, char *b, size_t n)
{
	// 101-249 (explode-into-weapon) is shown by the separate -Explode To row, not here.
	if (a <= 0)       snprintf(b, n, "None");
	else if (a == 99) snprintf(b, n, "Ice");
	else              snprintf(b, n, "%d", a);
}
static void cwFmtLife(int d, char *b, size_t n)
{
	if (d == 98)                 snprintf(b, n, "Aim X");
	else if (d == 99)            snprintf(b, n, "Aim X+Y");
	else if (d == 100)           snprintf(b, n, "Aim Y");
	else if (d == 121)           snprintf(b, n, "No Trail");
	else if (d > 100 && d < 120) snprintf(b, n, "Anim %dfr", d - 100 + 1);
	else if (d >= 255)           snprintf(b, n, "Life Max");
	else                         snprintf(b, n, "Life %d", d);
}
static void cwFmtVelY(int v, char *b, size_t n)
{
	if (v == 98)      snprintf(b, n, "Hover");
	else if (v > 100) snprintf(b, n, "Locked %d", v);
	else if (v > 0)   snprintf(b, n, "Up %d", v);
	else if (v < 0)   snprintf(b, n, "Down %d", -v);
	else              snprintf(b, n, "Still");
}
static void cwFmtVelX(int v, char *b, size_t n)
{
	if (v > 100)    snprintf(b, n, "Locked %d", v);
	else if (v > 0) snprintf(b, n, "Right %d", v);
	else if (v < 0) snprintf(b, n, "Left %d", -v);
	else            snprintf(b, n, "0");
}
static void cwFmtTrail(int t, char *b, size_t n)
{
	if (t == 255 || t == 0) snprintf(b, n, "None");
	else if (t == 98)       snprintf(b, n, "Smoke");
	else if (t == 198)      snprintf(b, n, "Partial");
	else                    snprintf(b, n, "%d", t);
}

static const char *cwRowLabel(int row)
{
	switch (row)
	{
	case CWROW_LIB_SELECT:  return "Weapon";
	case CWROW_POWER_LEVEL: return "Power Level";
	case CWROW_TWO_MODES:   return "Two Modes";
	case CWROW_FIRE_MODE:   return "Fire Mode";
	case CWROW_SHOW_TARGETS:return "Preview Targets";
	case CWROW_SHOW_STATS:  return "Stats Readout";
	case CWROW_TARGET_COUNT: return "Target Count";
	case CWROW_TARGET_SPREAD:return "Target Spread";
	case CWROW_TARGET_HEIGHT:return "Target Height";
	case CWROW_TARGET_X:     return "Target X";
	case CWROW_TARGET_ARMOR: return "Target Armor";
	case CWROW_SHIP_X:      return "Ship X";
	case CWROW_SHIP_Y:      return "Ship Y";
	case CWROW_NAME:        return "Name";
	case CWROW_EQUIP_SLOT:  return "Equip To";
	case CWROW_COST:        return "Cost";
	case CWROW_ICON:        return "Shop Icon";
	case CWROW_POWER_USE:   return "Power Use";
	case CWROW_SK_MOUNT:    return "Mount";
	case CWROW_SK_SPRITE:   return "Body Sprite";
	case CWROW_SK_FRAMES:   return "Body Frames";
	case CWROW_SK_STEP:     return "Frame Step";
	case CWROW_SK_ANIMATE:  return "Animate";
	case CWROW_CHARGE:      return "Charge Stages";
	case CWROW_IMPORT_SRC:  return "Import From";
	case CWROW_IMPORT_PWR:  return "Import Level";
	case CWROW_FIRE_RATE:   return "Fire Rate";
	case CWROW_BULLETS:     return "Bullets";
	case CWROW_PATTERN:     return "Pattern";
	case CWROW_ANIM:        return "Anim Frames";
	case CWROW_HOMING:      return "Homing";
	case CWROW_ACCEL_Y:     return "Accel Up";
	case CWROW_ACCEL_X:     return "Accel Side";
	case CWROW_SPIRAL:      return "Spiral";
	case CWROW_SOUND:       return "Sound";
	case CWROW_TRAIL:       return "Trail";
	case CWROW_BLAST:       return "Blast FX";
	case CWROW_BULLET_SEL:  return "Bullet Segment";
	case CWROW_B_SPRITE:    return "-Sprite";
	case CWROW_B_DAMAGE:    return "-Damage";
	case CWROW_B_CHAIN:     return "-Explode To";
	case CWROW_B_LIFE:      return "-Life";
	case CWROW_B_VELX:      return "-Speed X";
	case CWROW_B_VELY:      return "-Speed Y";
	case CWROW_B_OFFX:      return "-Offset X";
	case CWROW_B_OFFY:      return "-Offset Y";
	}
	return "";
}

// One-line description of the selected field, shown on the hint line.
static const char *cwRowHelp(int row)
{
	switch (row)
	{
	case CWROW_LIB_SELECT:  return "Pick a saved weapon (Left/Right)";
	case CWROW_LIB_NEW:       return "Enter: add a new blank weapon to the library";
	case CWROW_LIB_DUPLICATE: return "Enter: copy the current weapon to a new slot";
	case CWROW_LIB_DELETE:    return "Enter twice: delete the current weapon";
	case CWROW_POWER_LEVEL: return "Level being edited (= charge stage when charging)";
	case CWROW_TWO_MODES:   return "2nd design for the rear-gun fire toggle";
	case CWROW_FIRE_MODE:   return "Which fire mode you're editing";
	case CWROW_AUTOSCALE:   return "Enter: build all levels from this one (scale up + down)";
	case CWROW_COPY_ALL:    return "Enter: copy this level to every level (all identical)";
	case CWROW_COPY_LEVEL:  return "Enter: copy this level for pasting elsewhere";
	case CWROW_PASTE_LEVEL: return "Enter: replace this level with the copied level";
	case CWROW_SHOW_TARGETS:return "Practice targets: see hits, freeze, chains, homing";
	case CWROW_SHOW_STATS:  return "Live DPS / damage / fire-rate readout in the box";
	case CWROW_TARGET_COUNT: return "How many practice targets (0 = none)";
	case CWROW_TARGET_SPREAD:return "Horizontal spacing between targets";
	case CWROW_TARGET_HEIGHT:return "How high up the targets sit";
	case CWROW_TARGET_X:     return "Slide the whole target row left-right";
	case CWROW_TARGET_ARMOR: return "Target toughness (higher = armour drains slower)";
	case CWROW_SHIP_X:      return "Move the ship / shot origin left-right";
	case CWROW_SHIP_Y:      return "Move the ship / shot origin up-down";
#ifdef PLATFORM_HANDHELD
	case CWROW_NAME:        return "Confirm to rename";
#else
	case CWROW_NAME:        return "Type to rename";
#endif
	case CWROW_EQUIP_SLOT:  return "Where it mounts (gun or sidekick)";
	case CWROW_COST:        return "Shop price";
	case CWROW_POWER_USE:   return "Generator drain per shot";
	case CWROW_ICON:        return "Shop/HUD icon (preview shown bottom-left)";
	case CWROW_SK_MOUNT:    return "Where the sidekick sits (Front = ahead of the ship)";
	case CWROW_SK_SPRITE:   return "Sidekick body sprite; charge stages step up from here";
	case CWROW_SK_FRAMES:   return "Body animation frames (1 = static)";
	case CWROW_SK_STEP:     return "Sprite gap between body animation frames";
	case CWROW_SK_ANIMATE:  return "When the body animates (on fire / always)";
	case CWROW_CHARGE:      return "Sidekick charge shots (1 = off); edit each via Power Level";
	case CWROW_ADD_CHARGE:    return "Enter: add a charge shot, then tune its sprite below";
	case CWROW_REMOVE_CHARGE: return "Enter: remove the top charge shot";
	case CWROW_IMPORT_SRC:  return "Stock weapon to copy from";
	case CWROW_IMPORT_PWR:  return "Source level for Import This Level";
	case CWROW_FIRE_RATE:   return "Ticks between shots (0 = every tick; needed for solid lasers)";
	case CWROW_BULLETS:     return "Projectiles emitted each time the weapon fires";
	case CWROW_PATTERN:     return "Pattern length; above Bullets adds shot variation";
	case CWROW_ANIM:        return "Sprite frames cycled (1 = static, no animation)";
	case CWROW_HOMING:      return "Guides shots at enemies when above 5";
	case CWROW_ACCEL_Y:     return "Vertical acceleration";
	case CWROW_ACCEL_X:     return "Sideways acceleration";
	case CWROW_SPIRAL:      return "Corkscrew radius (0 = straight)";
	case CWROW_SOUND:       return "Fire sound effect";
	case CWROW_TRAIL:       return "Exhaust trail effect";
	case CWROW_BLAST:       return "Impact palette filter";
	case CWROW_ADD_SEGMENT:    return "Enter: duplicate the selected bullet";
	case CWROW_REMOVE_SEGMENT: return "Enter: delete the selected bullet";
	case CWROW_IMPORT_THIS:    return "Enter: copy the source into this level";
	case CWROW_IMPORT_ALL:     return "Enter: clone the whole source weapon";
	case CWROW_ADD_THIS:       return "Enter: add the source's shots onto this level (combine)";
	case CWROW_ADD_ALL:        return "Enter: add the source's shots onto every level (combine)";
	case CWROW_BULLET_SEL:  return "Which bullet the rows below edit";
	case CWROW_B_SPRITE:    return "Bullet sprite number";
	case CWROW_B_DAMAGE:    return "Damage per hit (99 = Ice); chaining is set below";
	case CWROW_B_CHAIN:     return "On enemy hit, burst into this weapon # (Off = deal Damage)";
	case CWROW_B_LIFE:      return "Life ticks, or Aim / No-Trail / Anim";
	case CWROW_B_VELX:      return "+ right, - left; above 100 locked";
	case CWROW_B_VELY:      return "+ up, - down; 98 hover, 100+ locked";
	case CWROW_B_OFFX:      return "Spawn X offset from ship";
	case CWROW_B_OFFY:      return "Spawn Y offset from ship";
	}
	return "";
}

static void cwRowValue(int row, char *buf, size_t n)
{
	JE_WeaponType *w = cwCur();
	const int b = cwBulletIndex();
	switch (row)
	{
	case CWROW_LIB_SELECT:
		snprintf(buf, n, "%d/%d %s", customWeaponCurrentSlot + 1, customWeaponLibCount, customWeaponName);
		break;
	case CWROW_POWER_LEVEL:
		// With charging on, levels 1..N ARE the charge stages, so this row doubles as the
		// charge-stage selector: show "Stage S/N" (stage 1 = uncharged, stage N = full).
		if (customWeaponChargeStages > 1 && customWeaponEditLevel < customWeaponChargeStages)
			snprintf(buf, n, "Stage %d / %d", customWeaponEditLevel + 1, customWeaponChargeStages);
		else
			snprintf(buf, n, "%d / %d", customWeaponEditLevel + 1, CUSTOM_POWER_LEVELS);
		break;
	case CWROW_TWO_MODES:   snprintf(buf, n, "%s", customWeaponModes >= 2 ? "Yes" : "No"); break;
	case CWROW_FIRE_MODE:
		if (customWeaponModes >= 2) snprintf(buf, n, "%d / %d", customWeaponEditMode + 1, customWeaponModes);
		else                        snprintf(buf, n, "n/a");
		break;
	case CWROW_SHOW_TARGETS: snprintf(buf, n, "%s", cwShowTargets ? "On" : "Off"); break;
	case CWROW_SHOW_STATS:   snprintf(buf, n, "%s", cwShowStats ? "On" : "Off"); break;
	case CWROW_TARGET_COUNT: snprintf(buf, n, "%d", cwTargetCount); break;
	case CWROW_TARGET_SPREAD:snprintf(buf, n, "%d", cwTargetSpread); break;
	case CWROW_TARGET_HEIGHT:snprintf(buf, n, "%d", cwTargetHeight); break;
	case CWROW_TARGET_X:     snprintf(buf, n, "%d", cwTargetX); break;
	case CWROW_TARGET_ARMOR: snprintf(buf, n, "%d", cwTargetArmor); break;
	case CWROW_SHIP_X:       snprintf(buf, n, "%d", cwShipX); break;
	case CWROW_SHIP_Y:       snprintf(buf, n, "%d", cwShipY); break;
	case CWROW_NAME:        snprintf(buf, n, "%s", customWeaponName); break;
	case CWROW_EQUIP_SLOT:  snprintf(buf, n, "%s", cwSlotNames[cwClamp(customWeaponEquipSlot, 0, CUSTOM_EQUIP_COUNT - 1)]); break;
	case CWROW_COST:        snprintf(buf, n, "%d", customWeaponCost); break;
	case CWROW_ICON:        snprintf(buf, n, "%d", customWeaponItemGraphic); break;
	case CWROW_POWER_USE:   snprintf(buf, n, "%d", customWeaponPowerUse); break;
	case CWROW_SK_MOUNT:    snprintf(buf, n, "%s", cwMountNames[cwClamp(customSidekickMount, 0, CUSTOM_SIDEKICK_MOUNTS - 1)]); break;
	case CWROW_SK_SPRITE:   snprintf(buf, n, "%d", customSidekickSprite); break;
	case CWROW_SK_FRAMES:   snprintf(buf, n, "%d", customSidekickFrames); break;
	case CWROW_SK_STEP:     snprintf(buf, n, "%d", customSidekickFrameStep); break;
	case CWROW_SK_ANIMATE:  snprintf(buf, n, "%s", customSidekickAnimate >= 2 ? "Always" : "On Fire"); break;
	case CWROW_CHARGE:      if (customWeaponChargeStages > 1) snprintf(buf, n, "%d stages (Lv 1-%d)", customWeaponChargeStages, customWeaponChargeStages); else snprintf(buf, n, "Off"); break;
	case CWROW_IMPORT_SRC:  snprintf(buf, n, "%s", customBulletPresetCount > 0 ? customBulletPreset[cwClamp(cwImportSrc, 0, customBulletPresetCount - 1)].name : "-"); break;
	case CWROW_IMPORT_PWR:
	{
		const int mp = customBulletMaxPower(cwImportSrc);
		if (mp > 1) snprintf(buf, n, "%d / %d", cwClamp(cwImportPwr, 1, mp), mp);
		else        snprintf(buf, n, "n/a");
		break;
	}
	case CWROW_FIRE_RATE:   if (w->shotrepeat == 0) snprintf(buf, n, "Every tick"); else snprintf(buf, n, "%d", w->shotrepeat); break;
	case CWROW_BULLETS:     snprintf(buf, n, "%d", w->multi); break;
	case CWROW_PATTERN:     snprintf(buf, n, "%d", w->max); break;
	case CWROW_ANIM:        snprintf(buf, n, "%d", w->weapani + 1); break;  // engine cycles weapani+1 frames
	case CWROW_HOMING:      if (w->aim > 5) snprintf(buf, n, "On (%d)", w->aim); else snprintf(buf, n, "Off"); break;
	case CWROW_ACCEL_Y:     snprintf(buf, n, "%+d", w->acceleration); break;
	case CWROW_ACCEL_X:     snprintf(buf, n, "%+d", w->accelerationx); break;
	case CWROW_SPIRAL:      if (w->circlesize) snprintf(buf, n, "%d", w->circlesize); else snprintf(buf, n, "Off"); break;
	case CWROW_SOUND:       if (w->sound) snprintf(buf, n, "%d", w->sound); else snprintf(buf, n, "None"); break;
	case CWROW_TRAIL:       cwFmtTrail(w->trail, buf, n); break;
	case CWROW_BLAST:       snprintf(buf, n, "%d", w->shipblastfilter); break;
	case CWROW_BULLET_SEL:  snprintf(buf, n, "%d / %d", b + 1, (w->multi < 1) ? 1 : w->multi); break;
	case CWROW_B_SPRITE:    snprintf(buf, n, "%d", w->sg[b]); break;
	case CWROW_B_DAMAGE:    if (w->attack[b] >= 101 && w->attack[b] <= 249) snprintf(buf, n, "Explodes"); else cwFmtDamage(w->attack[b], buf, n); break;
	case CWROW_B_CHAIN:     if (w->attack[b] >= 101 && w->attack[b] <= 249) snprintf(buf, n, "Weapon %d", w->attack[b] - 100); else snprintf(buf, n, "Off"); break;
	case CWROW_B_LIFE:      cwFmtLife(w->del[b], buf, n); break;
	case CWROW_B_VELX:      cwFmtVelX(w->sx[b], buf, n); break;
	case CWROW_B_VELY:      cwFmtVelY(w->sy[b], buf, n); break;
	case CWROW_B_OFFX:      snprintf(buf, n, "%+d", w->bx[b]); break;
	case CWROW_B_OFFY:      snprintf(buf, n, "%+d", w->by[b]); break;
	default:                buf[0] = '\0'; break;
	}
}

// Adjust a field by dir (-1/+1). Editor-only selection rows return early; every
// design field clamps to its valid range and re-materializes.
static void cwRowAdjust(int row, int dir)
{
	JE_WeaponType *w = cwCur();
	const int b = cwBulletIndex();
	switch (row)
	{
	case CWROW_LIB_SELECT:
		if (customWeaponLibCount > 0)
			customWeaponSelectSlot((customWeaponCurrentSlot + customWeaponLibCount + dir) % customWeaponLibCount);
		return;  // switches the working weapon (materializes internally)
	case CWROW_POWER_LEVEL:
		customWeaponSelectLevel(cwClamp(customWeaponEditLevel + dir, 0, CUSTOM_POWER_LEVELS - 1));
		return;  // just changes which (already-compiled) level the preview shows
	case CWROW_FIRE_MODE:
		if (customWeaponModes >= 2)
			customWeaponSelectMode(cwClamp(customWeaponEditMode + dir, 0, customWeaponModes - 1));
		return;  // view-only: both modes are already compiled
	case CWROW_SHOW_TARGETS:
		cwShowTargets = !cwShowTargets;  // binary preview switch (Left/Right/Enter all flip)
		return;  // editor-only preview state
	case CWROW_SHOW_STATS:
		cwShowStats = !cwShowStats;
		return;  // editor-only preview state
	case CWROW_TARGET_COUNT:
		cwTargetCount = cwClamp(cwTargetCount + dir, 0, CW_DUMMY_MAX);
		cwLayoutDummies();
		return;
	case CWROW_TARGET_SPREAD:
		cwTargetSpread = cwClamp(cwTargetSpread + dir * 4, 6, 60);
		cwLayoutDummies();
		return;
	case CWROW_TARGET_HEIGHT:
		cwTargetHeight = cwClamp(cwTargetHeight + dir * 4, 16, 160);
		cwLayoutDummies();
		return;
	case CWROW_TARGET_X:
		cwTargetX = cwClamp(cwTargetX + dir * 4, 20, 132);
		cwLayoutDummies();
		return;
	case CWROW_TARGET_ARMOR:
		cwTargetArmor = cwClamp(cwTargetArmor + dir * 20, 20, 600);
		cwLayoutDummies();
		return;
	case CWROW_SHIP_X:  // ranges keep the 24x28 ship sprite fully inside the 8..143 x 8..182 box
		cwShipX = cwClamp(cwShipX + dir * 2, 14, 124);
		return;
	case CWROW_SHIP_Y:
		cwShipY = cwClamp(cwShipY + dir * 2, 16, 160);
		return;
	case CWROW_IMPORT_SRC:
		if (customBulletPresetCount > 0)
		{
			cwImportSrc = (cwImportSrc + customBulletPresetCount + dir) % customBulletPresetCount;
			cwImportPwr = cwClamp(cwImportPwr, 1, customBulletMaxPower(cwImportSrc));
		}
		return;  // editor-only state
	case CWROW_IMPORT_PWR:
		cwImportPwr = cwClamp(cwImportPwr + dir, 1, customBulletMaxPower(cwImportSrc));
		return;  // editor-only state
	case CWROW_BULLET_SEL:
		cwBulletSel = cwClamp(cwBulletIndex() + dir, 0, (w->multi < 1 ? 1 : w->multi) - 1);
		return;  // editor-only selection

	case CWROW_TWO_MODES:   customWeaponModes = cwClamp(customWeaponModes + (dir >= 0 ? 1 : -1), 1, CUSTOM_WEAPON_MODES);
	                        if (customWeaponModes < 2)
	                            customWeaponEditMode = 0;
	                        break;
	case CWROW_EQUIP_SLOT:  customWeaponEquipSlot = (customWeaponEquipSlot + CUSTOM_EQUIP_COUNT + dir) % CUSTOM_EQUIP_COUNT; break;
	case CWROW_COST:        customWeaponCost      = cwClamp(customWeaponCost + dir * 500, 0, 64000); break;
	case CWROW_ICON:        customWeaponItemGraphic = cwClamp(customWeaponItemGraphic + dir, 1, 237); break;
	case CWROW_POWER_USE:   customWeaponPowerUse  = cwClamp(customWeaponPowerUse + dir, 0, 255); break;
	case CWROW_CHARGE:      customWeaponChargeStages = cwClamp(customWeaponChargeStages + dir, 1, CUSTOM_POWER_LEVELS); break;
	case CWROW_SK_MOUNT:
		customSidekickMount = (customSidekickMount + CUSTOM_SIDEKICK_MOUNTS + dir) % CUSTOM_SIDEKICK_MOUNTS;
		{  // the mount picks the body sheet, so re-clamp the sprite to the new sheet's 1-based range
			const int cnt = customSidekickSpriteCount(customSidekickMount);
			if (cnt > 0) customSidekickSprite = cwClamp(customSidekickSprite, 1, cnt);
		}
		break;
	case CWROW_SK_SPRITE:
		{  // body sprite is 1-based (blit reads offsetTable[index-1]); 0 would crash
			const int cnt = customSidekickSpriteCount(customSidekickMount);
			customSidekickSprite = cwClamp(customSidekickSprite + dir, 1, (cnt > 0) ? cnt : 65535);
		}
		break;
	case CWROW_SK_FRAMES:   customSidekickFrames    = cwClamp(customSidekickFrames + dir, 1, 20); break;
	case CWROW_SK_STEP:     customSidekickFrameStep = cwClamp(customSidekickFrameStep + dir, 0, 40); break;
	case CWROW_SK_ANIMATE:  customSidekickAnimate   = cwClamp(customSidekickAnimate + (dir >= 0 ? 1 : -1), 1, 2); break;

	// Ranges span each field's full valid range so any imported value stays reachable.
	case CWROW_FIRE_RATE:   w->shotrepeat = (JE_byte)cwClamp(w->shotrepeat + dir, 0, 255); break;
	case CWROW_BULLETS:     w->multi = (JE_byte)cwClamp(w->multi + dir, 1, CUSTOM_BULLETS_MAX);
	                        if (w->max < w->multi)
	                            w->max = w->multi;
	                        break;
	case CWROW_PATTERN:     w->max = (JE_byte)cwClamp(w->max + dir, 1, CUSTOM_BULLETS_MAX); break;
	case CWROW_ANIM:        w->weapani = (JE_word)cwClamp((int)w->weapani + dir, 0, 255); break;
	case CWROW_HOMING:      w->aim = (JE_byte)cwClamp(w->aim + dir, 0, 255); break;
	case CWROW_ACCEL_Y:     w->acceleration  = (JE_shortint)cwClamp(w->acceleration + dir, -128, 127); break;
	case CWROW_ACCEL_X:     w->accelerationx = (JE_shortint)cwClamp(w->accelerationx + dir, -128, 127); break;
	case CWROW_SPIRAL:      w->circlesize = (JE_byte)cwClamp(w->circlesize + dir, 0, 255); break;
	case CWROW_SOUND:       w->sound = (JE_byte)cwClamp(w->sound + dir, 0, CUSTOM_SOUND_MAX); break;
	case CWROW_TRAIL:       w->trail = (JE_byte)cwClamp(w->trail + dir, 0, 255); break;
	case CWROW_BLAST:       w->shipblastfilter = (JE_byte)cwClamp(w->shipblastfilter + dir, 0, 255); break;

	case CWROW_B_SPRITE:    w->sg[b] = (JE_word)cwClamp((int)w->sg[b] + dir, 0, 65535); break;
	case CWROW_B_DAMAGE:  // 0=None, 1-98 damage, 99 Ice; leaves the explode range to -Explode To
	{
		const int dmg = (w->attack[b] <= 99) ? w->attack[b] : 10;  // editing damage cancels an explosion
		w->attack[b] = (JE_byte)cwClamp(dmg + dir, 0, 99);
		break;
	}
	case CWROW_B_CHAIN:   // 0=Off (normal damage), else explode into weapon N (attack = 100+N)
	{
		const int tgt = (w->attack[b] >= 101 && w->attack[b] <= 249) ? w->attack[b] - 100 : 0;
		const int nt  = cwClamp(tgt + dir, 0, 149);
		w->attack[b] = (JE_byte)(nt == 0 ? 10 : 100 + nt);  // Off restores a modest damage
		break;
	}
	case CWROW_B_LIFE:      w->del[b] = (JE_byte)cwClamp(w->del[b] + dir, 0, 255); break;
	case CWROW_B_VELX:      w->sx[b] = (JE_shortint)cwClamp(w->sx[b] + dir, -128, 127); break;
	case CWROW_B_VELY:      w->sy[b] = (JE_shortint)cwClamp(w->sy[b] + dir, -128, 127); break;
	case CWROW_B_OFFX:      w->bx[b] = (JE_shortint)cwClamp(w->bx[b] + dir, -128, 127); break;
	case CWROW_B_OFFY:      w->by[b] = (JE_shortint)cwClamp(w->by[b] + dir, -128, 127); break;
	default: return;  // CWROW_NAME is edited by typing
	}
	customWeaponMaterialize();
}

static void cwAdjustRow(int row, int dir)
{
	const int oldSlot = customWeaponCurrentSlot;
	cwRowAdjust(row, dir);
	if (row == CWROW_LIB_SELECT)
	{
		cwDeleteConfirmTicks = 0;
		if (customWeaponCurrentSlot != oldSlot)
		{
			cwHistoryReset();
			cwClearPreviewShots();
		}
	}
	else if (row == CWROW_POWER_LEVEL || row == CWROW_FIRE_MODE)
	{
		cwClearPreviewShots();
	}
	else if (cwHistoryRecord())
	{
		cwClearPreviewShots();
	}
}

// Numeric entry for rows whose range is too wide for step controls. A negative lower bound marks
// a signed field.
static bool cwNumericRange(int row, int *lo, int *hi)
{
	switch (row)
	{
	case CWROW_COST:          *lo = 0;    *hi = 64000;            return true;
	case CWROW_ICON:          *lo = 1;    *hi = 237;              return true;
	case CWROW_POWER_USE:     *lo = 0;    *hi = 255;              return true;
	case CWROW_SK_SPRITE:     { const int cnt = customSidekickSpriteCount(customSidekickMount);
	                            *lo = 1; *hi = (cnt > 0) ? cnt : 65535; return true; }  // 1-based body sprite
	case CWROW_SK_FRAMES:     *lo = 1;    *hi = 20;               return true;
	case CWROW_SK_STEP:       *lo = 0;    *hi = 40;               return true;
	case CWROW_CHARGE:        *lo = 1;    *hi = CUSTOM_POWER_LEVELS; return true;
	case CWROW_FIRE_RATE:     *lo = 0;    *hi = 255;              return true;
	case CWROW_BULLETS:       *lo = 1;    *hi = CUSTOM_BULLETS_MAX; return true;
	case CWROW_PATTERN:       *lo = 1;    *hi = CUSTOM_BULLETS_MAX; return true;
	case CWROW_ANIM:          *lo = 1;    *hi = 256;              return true;
	case CWROW_HOMING:        *lo = 0;    *hi = 255;              return true;
	case CWROW_ACCEL_Y:       *lo = -128; *hi = 127;              return true;
	case CWROW_ACCEL_X:       *lo = -128; *hi = 127;              return true;
	case CWROW_SPIRAL:        *lo = 0;    *hi = 255;              return true;
	case CWROW_SOUND:         *lo = 0;    *hi = CUSTOM_SOUND_MAX; return true;
	case CWROW_TRAIL:         *lo = 0;    *hi = 255;              return true;
	case CWROW_BLAST:         *lo = 0;    *hi = 255;              return true;
	case CWROW_B_SPRITE:      *lo = 0;    *hi = 65535;            return true;
	case CWROW_B_DAMAGE:      *lo = 0;    *hi = 99;               return true;
	case CWROW_B_CHAIN:       *lo = 0;    *hi = 149;              return true;
	case CWROW_B_LIFE:        *lo = 0;    *hi = 255;              return true;
	case CWROW_B_VELX:        *lo = -128; *hi = 127;              return true;
	case CWROW_B_VELY:        *lo = -128; *hi = 127;              return true;
	case CWROW_B_OFFX:        *lo = -128; *hi = 127;              return true;
	case CWROW_B_OFFY:        *lo = -128; *hi = 127;              return true;
	// editor-only preview settings (coarse steps, so typing helps here too)
	case CWROW_TARGET_COUNT:  *lo = 0;    *hi = CW_DUMMY_MAX;     return true;
	case CWROW_TARGET_SPREAD: *lo = 6;    *hi = 60;               return true;
	case CWROW_TARGET_HEIGHT: *lo = 16;   *hi = 160;              return true;
	case CWROW_TARGET_X:      *lo = 20;   *hi = 132;              return true;
	case CWROW_TARGET_ARMOR:  *lo = 20;   *hi = 600;              return true;
	case CWROW_SHIP_X:        *lo = 14;   *hi = 124;              return true;
	case CWROW_SHIP_Y:        *lo = 16;   *hi = 160;              return true;
	default:                                                      return false;
	}
}

// Apply a typed value to a row (clamped to its range), re-materializing when it changed
// the actual design. Rows cwNumericRange rejects are ignored.
static void cwSetNumeric(int row, int v)
{
	int lo, hi;
	if (!cwNumericRange(row, &lo, &hi))
		return;
	v = cwClamp(v, lo, hi);

	JE_WeaponType *w = cwCur();
	const int b = cwBulletIndex();
	switch (row)
	{
	case CWROW_COST:          customWeaponCost        = v; break;
	case CWROW_ICON:          customWeaponItemGraphic = v; break;
	case CWROW_POWER_USE:     customWeaponPowerUse    = v; break;
	case CWROW_SK_SPRITE:     customSidekickSprite    = v; break;
	case CWROW_SK_FRAMES:     customSidekickFrames    = v; break;
	case CWROW_SK_STEP:       customSidekickFrameStep = v; break;
	case CWROW_CHARGE:        customWeaponChargeStages = v; break;
	case CWROW_FIRE_RATE:     w->shotrepeat      = (JE_byte)v;     break;
	case CWROW_BULLETS:       w->multi = (JE_byte)v;
	                          if (w->max < w->multi) w->max = w->multi;
	                          break;
	case CWROW_PATTERN:       w->max = (JE_byte)v; break;
	case CWROW_ANIM:          w->weapani = (JE_word)(v - 1); break;
	case CWROW_HOMING:        w->aim             = (JE_byte)v;     break;
	case CWROW_ACCEL_Y:       w->acceleration    = (JE_shortint)v; break;
	case CWROW_ACCEL_X:       w->accelerationx   = (JE_shortint)v; break;
	case CWROW_SPIRAL:        w->circlesize      = (JE_byte)v;     break;
	case CWROW_SOUND:         w->sound           = (JE_byte)v;     break;
	case CWROW_TRAIL:         w->trail           = (JE_byte)v;     break;
	case CWROW_BLAST:         w->shipblastfilter = (JE_byte)v;     break;
	case CWROW_B_SPRITE:      w->sg[b]     = (JE_word)v;     break;
	case CWROW_B_DAMAGE:      w->attack[b] = (JE_byte)v;     break;  // 0 none / 1-98 dmg / 99 Ice
	case CWROW_B_CHAIN:       w->attack[b] = (JE_byte)(v == 0 ? 10 : 100 + v); break;
	case CWROW_B_LIFE:        w->del[b]    = (JE_byte)v;     break;
	case CWROW_B_VELX:        w->sx[b]     = (JE_shortint)v; break;
	case CWROW_B_VELY:        w->sy[b]     = (JE_shortint)v; break;
	case CWROW_B_OFFX:        w->bx[b]     = (JE_shortint)v; break;
	case CWROW_B_OFFY:        w->by[b]     = (JE_shortint)v; break;
	case CWROW_TARGET_COUNT:  cwTargetCount  = v; cwLayoutDummies(); return;
	case CWROW_TARGET_SPREAD: cwTargetSpread = v; cwLayoutDummies(); return;
	case CWROW_TARGET_HEIGHT: cwTargetHeight = v; cwLayoutDummies(); return;
	case CWROW_TARGET_X:      cwTargetX      = v; cwLayoutDummies(); return;
	case CWROW_TARGET_ARMOR:  cwTargetArmor  = v; cwLayoutDummies(); return;
	case CWROW_SHIP_X:        cwShipX = v; return;
	case CWROW_SHIP_Y:        cwShipY = v; return;
	default: return;
	}
	customWeaponMaterialize();
}

// Finalize any in-progress typed value to its field and leave typing mode. An empty
// buffer (or a lone '-') is discarded, leaving the field unchanged.
static void cwCommitNumeric(void)
{
	if (cwNumRow < 0)
		return;
	if (cwNumText[0] != '\0' && !(cwNumText[0] == '-' && cwNumText[1] == '\0'))
	{
		cwSetNumeric(cwNumRow, SDL_atoi(cwNumText));
		if (cwHistoryRecord())
			cwClearPreviewShots();
	}
	cwNumRow = -1;
	cwNumText[0] = '\0';
}

// Run one editor action, shared by the inline action rows and the bottom action list.
// done/equipped are set by EQUIP and DONE (harmless for the other actions).
static void cwPerformAction(int act, bool *done, bool *equipped)
{
	bool changed = false;
	bool resetHistory = false;
	if (act != CWACT_LIB_DELETE)
		cwDeleteConfirmTicks = 0;

	switch (act)
	{
	case CWACT_ADD_BULLET:
	{
		const int ni = customWeaponAddBullet(cwBulletIndex());
		if (ni >= 0) { cwBulletSel = ni; customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; }
		else         JE_playSampleNum(S_SPRING);  // already at the segment limit
		break;
	}
	case CWACT_REMOVE_BULLET:
	{
		const int ni = customWeaponRemoveBullet(cwBulletIndex());
		if (ni >= 0) { cwBulletSel = ni; customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; }
		else         JE_playSampleNum(S_SPRING);  // can't remove the last segment
		break;
	}
	case CWACT_ADD_CHARGE:
		if (customWeaponAddChargeState() >= 0) { customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; }
		else                                   JE_playSampleNum(S_SPRING);  // already 11 states
		break;
	case CWACT_REMOVE_CHARGE:
		if (customWeaponRemoveChargeState() >= 0) { customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; }
		else                                      JE_playSampleNum(S_SPRING);  // only 1 shot left
		break;
	case CWACT_IMPORT_LEVEL:   customWeaponImportLevel(cwImportSrc, cwImportPwr); customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_IMPORT_ALL:     customWeaponImportAllLevels(cwImportSrc);          customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_ADD_LEVEL:      customWeaponAddLevel(cwImportSrc, cwImportPwr);    customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_ADD_ALL_LEVELS: customWeaponAddAllLevels(cwImportSrc);             customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_COPY_ALL:       customWeaponCopyToAllLevels();                     customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_AUTOSCALE:      customWeaponAutoScaleLevels();                     customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_RANDOMIZE:      customWeaponRandomize();                           customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_RESET:          customWeaponReset();                               customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_RESET_ALL:      customWeaponResetAllLevels();                      customWeaponMaterialize(); JE_playSampleNum(S_SELECT); changed = true; break;
	case CWACT_COPY_LEVEL:
		cwLevelClipboard = *cwCur();
		cwLevelClipboardSet = true;
		cwSetNotice("Level copied");
		JE_playSampleNum(S_SELECT);
		break;
	case CWACT_PASTE_LEVEL:
		if (cwLevelClipboardSet)
		{
			*cwCur() = cwLevelClipboard;
			customWeaponMaterialize();
			cwSetNotice("Level pasted");
			JE_playSampleNum(S_SELECT);
			changed = true;
		}
		else
		{
			cwSetNotice("Copy a level first");
			JE_playSampleNum(S_SPRING);
		}
		break;
	case CWACT_UNDO:
		if (cwHistoryUndo()) JE_playSampleNum(S_SELECT);
		else                 { cwSetNotice("Nothing to undo"); JE_playSampleNum(S_SPRING); }
		break;
	case CWACT_REDO:
		if (cwHistoryRedo()) JE_playSampleNum(S_SELECT);
		else                 { cwSetNotice("Nothing to redo"); JE_playSampleNum(S_SPRING); }
		break;
	case CWACT_EQUIP:
		if (customWeaponEquip()) { *equipped = true; JE_playSampleNum(S_SELECT); *done = true; }
		else                     JE_playSampleNum(S_SPRING);  // e.g. no free sidekick slot
		break;
	case CWACT_SAVE:
		customWeaponLibrarySave();   // flush the weapon library to disk now, without leaving
		save_opentyrian_config();
		JE_playSampleNum(S_SELECT);
		cwSetNotice("Weapon saved");
		break;
	case CWACT_DONE:         *done = true; break;
	case CWACT_LIB_NEW:
		if (customWeaponLibraryNew() >= 0) { JE_playSampleNum(S_SELECT); resetHistory = true; }
		else                               JE_playSampleNum(S_SPRING);  // library full
		break;
	case CWACT_LIB_DUPLICATE:
		if (customWeaponLibraryDuplicate() >= 0) { JE_playSampleNum(S_SELECT); resetHistory = true; }
		else                                     JE_playSampleNum(S_SPRING);  // library full
		break;
	case CWACT_LIB_DELETE:
		if (customWeaponLibCount <= 1)
		{
			JE_playSampleNum(S_SPRING);
		}
		else if (cwDeleteConfirmTicks <= 0)
		{
			cwDeleteConfirmTicks = 70;
			cwSetNotice("Press Delete Weapon again to confirm");
			cwNoticeTicks = cwDeleteConfirmTicks;
			JE_playSampleNum(S_CLICK);
		}
		else if (customWeaponLibraryDelete() >= 0)
		{
			cwDeleteConfirmTicks = 0;
			cwSetNotice("Weapon deleted");
			JE_playSampleNum(S_SELECT);
			resetHistory = true;
		}
		break;
	}

	if (resetHistory)
	{
		cwHistoryReset();
		cwClearPreviewShots();
	}
	else if (changed && cwHistoryRecord())
	{
		cwClearPreviewShots();
	}
}

// Restore an inclusive preview rectangle from the saved backdrop.
static void cwRestoreRect(int x0, int y0, int x1, int y1)
{
	if (x1 < x0 || y1 < y0)
		return;
	for (int y = y0; y <= y1; ++y)
	{
		const Uint8 *src = (const Uint8 *)VGAScreen2->pixels + y * VGAScreen2->pitch + x0;
		Uint8       *dst = (Uint8 *)VGAScreen->pixels + y * VGAScreen->pitch + x0;
		memcpy(dst, src, (size_t)(x1 - x0 + 1));
	}
}

// A Sprite2 sheet's first offset is the byte length of its JE_word offset table.
static int cwSprite2Count(Sprite2_array s)
{
	if (s.data == NULL || s.size < 2)
		return 0;
	return SDL_SwapLE16(((const Uint16 *)s.data)[0]) / 2;
}

// Resolve raw shot graphics to Sprite2 sheet 8 or 12; blended graphics return false.
static bool cwResolveShotSprite(JE_word sg, Sprite2_array *sheet, int *index)
{
	JE_word af = sg;
	if (af >= 60000)
		return false;                       // blended OPTION_SHAPES sprite; caller handles
	if (af > 1000)
		af %= 1000;
	Sprite2_array s = (af > 500) ? spriteSheet12 : spriteSheet8;
	int idx        = (af > 500) ? (af - 500) : af;
	if (idx < 1 || idx > cwSprite2Count(s))
		return false;                       // 0 = no sprite, or beyond the sheet
	*sheet = s;
	*index = idx;
	return true;
}

// Decode a Sprite2 sprite's opaque bounding box within its 12px-wide cell (rows are 12px;
// each control byte = low-nibble transparent skip + high-nibble opaque run, 0x0f ends).
static bool cwSpriteExtent(JE_word sg, int *ox, int *oy, int *ow, int *oh)
{
	Sprite2_array s;
	int index;
	if (!cwResolveShotSprite(sg, &s, &index))
		return false;

	const Uint8 *data = (const Uint8 *)s.data + SDL_SwapLE16(((const Uint16 *)s.data)[index - 1]);
	int x = 0, row = 0, minx = 12, maxx = -1, miny = 9999, maxy = -1;
	for (; *data != 0x0f; ++data)
	{
		x += *data & 0x0f;                  // transparent skip within the row
		int count = (*data & 0xf0) >> 4;    // opaque run length
		if (count == 0)                     // move to the next row
		{
			++row;
			x = 0;
			continue;
		}
		if (x < minx) minx = x;
		data += count;                      // skip the run's pixel bytes
		x    += count;
		if (x - 1 > maxx) maxx = x - 1;
		if (row < miny) miny = row;
		if (row > maxy) maxy = row;
	}
	if (maxx < minx || maxy < miny)
		return false;
	*ox = minx; *oy = miny; *ow = maxx - minx + 1; *oh = maxy - miny + 1;
	return true;
}

// Draw the segment's shot sprite at (x, y); its cell top-left; exactly as the shot
// renderer would (so the preview matches what actually fires).
static void cwDrawSegmentSprite(int x, int y, JE_word sg)
{
	Sprite2_array sheet;
	int index;
	if (cwResolveShotSprite(sg, &sheet, &index))
		blit_sprite2(VGAScreen, x, y, sheet, index);
	else if (sg >= 60000)
		blit_sprite_blend(VGAScreen, x, y, OPTION_SHAPES, sg - 60001);
}

// The editor row selected while presenting; drives the preview overlay (-1 = none).
static int cwOvRow = -1;
// The live player shot the segment corners are following (-1 = none). Locked once per
// tick in cwUpdateTrackedShot() so the brackets stay on one shot as it flies up.
static int cwTrackedShot = -1;
static int cwTrackedSeg  = -1;  // which bullet segment that shot belongs to (so switching
                                // segments re-locks even when segments share a sprite)

// Track the selected segment's live shot. Reacquire by spawn column, then age,
// when the shot dies or no longer matches; call after shots move each tick.
static void cwUpdateTrackedShot(int row)
{
	if (row < 0 || cwGroupColor(row) != CW_BG_BULLET)
	{
		cwTrackedShot = -1;
		cwTrackedSeg  = -1;
		return;
	}
	const int b = cwBulletIndex();
	const JE_word sg = cwCur()->sg[b];
	if (cwTrackedShot >= 0 && cwTrackedShot < MAX_PWEAPON && cwTrackedSeg == b
	    && shotAvail[cwTrackedShot] != 0 && playerShotData[cwTrackedShot].shotGr == sg)
		return;  // still valid for this segment; keep following it

	const int spawnX = player[0].x + cwCur()->bx[b] + 1;  // where this segment's shots appear
	int best = -1, bestScore = INT_MAX;
	for (int i = 0; i < MAX_PWEAPON; ++i)
		if (shotAvail[i] != 0 && playerShotData[i].shotGr == sg)
		{
			const int dx = playerShotData[i].shotX - spawnX;
			// nearest spawn column dominates; fresher (larger shotY) breaks ties
			const int score = (dx < 0 ? -dx : dx) * 4 - playerShotData[i].shotY;
			if (score < bestScore) { bestScore = score; best = i; }
		}
	cwTrackedShot = best;
	cwTrackedSeg  = (best >= 0) ? b : -1;
}

// Draw only the four 2px corner brackets of a box (no full outline), clipped to the box.
static void cwDrawSegmentCorners(int x0, int y0, int x1, int y1)
{
	enum { BX0 = 8, BY0 = 8, BX1 = 143, BY1 = 182 };
	if (x0 < BX0) x0 = BX0;
	if (y0 < BY0) y0 = BY0;
	if (x1 > BX1) x1 = BX1;
	if (y1 > BY1) y1 = BY1;
	if (x1 - x0 < 2 || y1 - y0 < 2)
		return;
	const Uint8 c = 0x9C;  // bright blue (palette 0 bank 9)
	fill_rectangle_xy(VGAScreen, x0, y0, x0 + 1, y0, c); fill_rectangle_xy(VGAScreen, x0, y0, x0, y0 + 1, c);
	fill_rectangle_xy(VGAScreen, x1 - 1, y0, x1, y0, c); fill_rectangle_xy(VGAScreen, x1, y0, x1, y0 + 1, c);
	fill_rectangle_xy(VGAScreen, x0, y1 - 1, x0, y1, c); fill_rectangle_xy(VGAScreen, x0, y1, x0 + 1, y1, c);
	fill_rectangle_xy(VGAScreen, x1, y1 - 1, x1, y1, c); fill_rectangle_xy(VGAScreen, x1 - 1, y1, x1, y1, c);
}

// Draw interpolated brackets around one live shot from the edited segment.
static void cwDrawPreviewOverlay(float alpha)
{
	const int row = cwOvRow;
	if (row < 0)
		return;
	const bool segCat  = (cwGroupColor(row) == CW_BG_BULLET);
	const bool iconRow = (row == CWROW_ICON);
	if (!segCat && !iconRow)
		return;

	JE_WeaponType *w = cwCur();
	const int b = cwBulletIndex();

	if (segCat && cwTrackedShot >= 0 && cwTrackedShot < MAX_PWEAPON && shotAvail[cwTrackedShot] != 0)
	{
		const PlayerShotDataType *s = &playerShotData[cwTrackedShot];
		const int dvx = (s->shotXM > 100 || s->shotXM < -100) ? 0 : s->shotXM;  // per-tick delta
		const int dvy = (s->shotYM > 100 || s->shotYM < -100) ? 0 : s->shotYM;  // (skip locked sentinels)
		// Player shots EXTRAPOLATE forward at the render rate (render_list.c:799/814:
		// cur + dx*alpha), so the brackets must too, or they trail the bullet by a tick.
		const int px = s->shotX + 1 + (int)(dvx * alpha);  // shot is drawn at shotX+1 (shots.c:111)
		const int py = s->shotY     + (int)(dvy * alpha);
		int ox, oy, ow, oh;
		if (cwSpriteExtent(s->shotGr, &ox, &oy, &ow, &oh))
			cwDrawSegmentCorners(px + ox - 1, py + oy - 1, px + ox + ow, py + oy + oh);
	}

	if (row == CWROW_B_SPRITE || iconRow)
	{
		enum { PV = 32 };
		const int px0 = 10, py0 = 182 - 2 - PV;
		fill_rectangle_xy(VGAScreen, px0, py0, px0 + PV, py0 + PV, 0);   // dark backing
		JE_rectangle(VGAScreen, px0, py0, px0 + PV, py0 + PV, 0xFB);     // frame
		const int cx = px0 + PV / 2, cy = py0 + PV / 2;
		if (iconRow)
		{
			blit_sprite2x2(VGAScreen, cx - 12, cy - 14, shopSpriteSheet, cwClamp(customWeaponItemGraphic, 1, 237));
		}
		else
		{
			int sx, sy, sw, sh;
			if (cwSpriteExtent(w->sg[b], &sx, &sy, &sw, &sh))
				cwDrawSegmentSprite(cx - (sx + sw / 2), cy - (sy + sh / 2), w->sg[b]);
			else
				cwDrawSegmentSprite(cx - 6, cy - 6, w->sg[b]);
		}
	}
}

// Target dummies expose damage, ice, chaining, explosions, and homing in the preview.

// Position the dummies from the current settings and clear any leftover explosions so the
// preview starts clean. Called when the creator opens.
static void cwResetDummies(void)
{
	cwLayoutDummies();
	for (int j = 0; j < MAX_EXPLOSIONS; ++j)
		explosions[j].ttl = 0;
}

// Preview Homing by nudging live shots toward the nearest dummy on X. The shop simulator has no
// target tracking, so this mirrors the gameplay correction here.
static void cwHomeShots(void)
{
	if (cwCur()->aim <= 5 || cwDummyCount == 0)
		return;
	for (int z = 0; z < MAX_PWEAPON - 1; ++z)
	{
		if (shotAvail[z] == 0)
			continue;
		PlayerShotDataType *s = &playerShotData[z];
		if (s->shotXM > 100 || s->shotXM < -100)
			continue;  // ship-locked sentinel; leave it alone
		const int scx = s->shotX + 6, scy = s->shotY + 6;
		int bi = -1, bd = INT_MAX;
		for (int d = 0; d < cwDummyCount; ++d)
		{
			const int dx = cwDummies[d].cx - scx, dy = cwDummies[d].cy - scy;
			const int dist = dx * dx + dy * dy;
			if (dist < bd) { bd = dist; bi = d; }
		}
		if (bi < 0)
			continue;
		if (scx < cwDummies[bi].cx && s->shotXM < 20)       ++s->shotXM;
		else if (scx > cwDummies[bi].cx && s->shotXM > -20) --s->shotXM;
	}
}

// Collide live shots against the dummies (called after they move).
static void cwCollideShots(void)
{
	if (cwDummyCount == 0)
		return;
	int chainSpawns = 0;  // cap per-tick cascade spawns so a self-chaining weapon stays tame
	for (int z = 0; z < MAX_PWEAPON - 1; ++z)
	{
		if (shotAvail[z] == 0)
			continue;
		PlayerShotDataType *s = &playerShotData[z];
		const int scx = s->shotX + 6, scy = s->shotY + 6;
		for (int d = 0; d < cwDummyCount; ++d)
		{
			if (abs(scx - cwDummies[d].cx) >= CW_DUMMY_R + 4 ||
			    abs(scy - cwDummies[d].cy) >= CW_DUMMY_R + 4)
				continue;

			if (s->chainReaction > 0 && s->chainReaction <= 149)
			{
				if (chainSpawns < 16)  // spawn the chained weapon at the impact point
				{
					shotMultiPos[SHOT_MISC] = 0;
					player_shot_create(0, SHOT_MISC, s->shotX, s->shotY, mouseX, mouseY,
					                   s->chainReaction, 1);
					++chainSpawns;
				}
				JE_setupExplosion(s->shotX, s->shotY, 0, 0, false, false);
				shotAvail[z] = 0;   // the carrier is consumed
			}
			else
			{
				if (s->shotDmg == 99)   // Ice; freezes instead of damaging (game sets dmg 0)
				{
					cwDummies[d].ice = 18;
				}
				else
				{
					int dmg = s->shotDmg;
					if (dmg >= 250) dmg -= 250;   // piercing carries damage + 250
					cwDummies[d].armor -= dmg;
					cwDummies[d].hit = 3;
					if (cwDummies[d].armor <= 0)
					{
						cwDummies[d].armor = cwTargetArmor;  // endless test dummy: refill
						cwDummies[d].dead  = 8;
					}
				}
				JE_setupExplosion(s->shotX, s->shotY, 0, 0, false, false);
				if (s->shotDmg < 250)   // non-piercing shots stop at the target
					shotAvail[z] = 0;
			}
			break;  // one dummy per shot per tick
		}
	}
}

// Advance preview explosions without gameplay background drift.
static void cwPumpExplosions(void)
{
	for (int j = 0; j < MAX_EXPLOSIONS; ++j)
	{
		if (explosions[j].ttl == 0)
			continue;
		if (!explosions[j].fixedPosition)
			explosions[j].sprite++;
		explosions[j].y += explosions[j].deltaY;
		rl_current_id = RL_ID_EXPL_BASE + j * 4 + (explosions[j].id_gen & 3);
		blit_sprite2(VGAScreen, explosions[j].x, explosions[j].y, explosionSpriteSheet, explosions[j].sprite + 1);
		rl_current_id = 0;
		explosions[j].ttl--;
	}
}

// Draw and update preview targets before shots so bullets pass in front.
static void cwDrawDummies(void)
{
	for (int d = 0; d < cwDummyCount; ++d)
	{
		CwDummy *t = &cwDummies[d];
		const int x0 = t->cx - CW_DUMMY_R, y0 = t->cy - CW_DUMMY_R;
		const int x1 = t->cx + CW_DUMMY_R, y1 = t->cy + CW_DUMMY_R;

		Uint8 body = 0x06, ring = 0xFB;              // steel body, gold frame
		if (t->ice  > 0) { body = 0x9A; ring = 0x9C; }   // frozen: blue
		if (t->hit  > 0)   body = 0x0D;                  // hit flash: bright
		if (t->dead > 0) { body = 0x0F; ring = 0xF5; }   // broken: white flash, red frame

		fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, body);
		fill_rectangle_xy(VGAScreen, x0 + 2, t->cy, x1 - 2, t->cy, 0);  // crosshair
		fill_rectangle_xy(VGAScreen, t->cx, y0 + 2, t->cx, y1 - 2, 0);
		JE_rectangle(VGAScreen, x0, y0, x1, y1, ring);

		// Armour bar just below, width proportional to armour left (green high, red low).
		const int bw   = 2 * CW_DUMMY_R;
		const int fill = (cwTargetArmor > 0) ? t->armor * bw / cwTargetArmor : 0;
		fill_rectangle_xy(VGAScreen, x0, y1 + 3, x1, y1 + 4, 0x02);
		if (fill > 0)
			fill_rectangle_xy(VGAScreen, x0, y1 + 3, x0 + fill, y1 + 4,
			                  (t->armor * 2 >= cwTargetArmor) ? 0x2B : 0xF5);

		if (t->armor < cwTargetArmor) ++t->armor;  // passive regen
		if (t->hit  > 0) --t->hit;
		if (t->ice  > 0) --t->ice;
		if (t->dead > 0) --t->dead;
	}
}

// Live stats readout (top-left of the box) for the level/mode currently previewing: approximate
// DPS, per-shot damage x bullet count, and fire rate.
static void cwDrawStats(void)
{
	const JE_WeaponType *w = cwCur();
	int multi = w->multi;
	if (multi < 1) multi = 1;
	if (multi > CUSTOM_BULLETS_MAX) multi = CUSTOM_BULLETS_MAX;

	int dmg = 0;
	bool ice = false, chain = false;
	for (int i = 0; i < multi; ++i)
	{
		const int a = w->attack[i];
		if (a == 99)                   ice = true;              // freeze, no HP damage
		else if (a >= 101 && a <= 249) chain = true;           // explodes into another weapon
		else if (a >= 250)             dmg += a - 250;          // piercing carries damage + 250
		else                           dmg += a;
	}
	const int period = w->shotrepeat + 1;     // ticks between volleys
	const int dps    = dmg * 35 / period;     // SIM_FPS = 35 ticks/sec
	const int rate10 = 35 * 10 / period;      // volleys/sec x10 (one decimal)

	char l1[24], l2[24], l3[24];
	snprintf(l1, sizeof(l1), "DPS ~%d%s", dps, chain ? " +ch" : (ice && dmg == 0) ? " ice" : "");
	snprintf(l2, sizeof(l2), "Volley %d dmg", dmg);
	snprintf(l3, sizeof(l3), "%d.%d/sec", rate10 / 10, rate10 % 10);

	enum { SX = 11, SY = 10 };
	fill_rectangle_xy(VGAScreen, SX - 2, SY - 1, SX + 92, SY + 24, 0);   // dark backing for legibility
	JE_rectangle(VGAScreen, SX - 2, SY - 1, SX + 92, SY + 24, 0xF4);     // subtle frame
	draw_font_hv_shadow(VGAScreen, SX, SY + 1,  l1, small_font, left_aligned, 15, 4, false, 1);
	draw_font_hv_shadow(VGAScreen, SX, SY + 9,  l2, small_font, left_aligned, 15, 1, false, 1);
	draw_font_hv_shadow(VGAScreen, SX, SY + 17, l3, small_font, left_aligned, 15, 1, false, 1);
}

// Draw the equipped sidekick's body sprite in the preview box, roughly where it would sit
// relative to the ship, so you can SEE the mount + sprite you're editing.
static void cwDrawSidekickPreview(void)
{
	const bool isSidekick = (customWeaponEquipSlot == CUSTOM_EQUIP_LEFT ||
	                         customWeaponEquipSlot == CUSTOM_EQUIP_RIGHT ||
	                         customWeaponEquipSlot == CUSTOM_EQUIP_BOTH);
	if (!isSidekick || customSidekickSlot <= 0 || customSidekickSlot > OPTION_NUM)
		return;

	const JE_OptionType *o = &options[customSidekickSlot];
	const int tr = o->tr;
	int x, y;
	switch (tr)
	{
	case 2:  x = cwShipX - 6;  y = cwShipY - 22; break;   // front (ahead of the ship)
	case 1:  x = cwShipX - 6;  y = cwShipY + 16; break;   // trailing (large body)
	case 3:  x = cwShipX - 4;  y = cwShipY + 16; break;   // trailing (single tile)
	case 4:  x = cwShipX + 16; y = cwShipY;      break;   // orbit (show to one side)
	default: x = cwShipX - 15; y = cwShipY;      break;   // side pod
	}
	// keep the whole body inside the box (8..143 x 8..182); the sidekick blit isn't clipped
	x = (x < 10) ? 10 : (x > 118) ? 118 : x;
	y = (y < 12) ? 12 : (y > 152) ? 152 : y;

	if (tr == 1 || tr == 2)
		blit_sprite2x2(VGAScreen, x, y, spriteSheet10, o->gr[0]);
	else
		blit_sprite2(VGAScreen, x, y, spriteSheet9, o->gr[0]);
}

bool JE_customWeaponCreator(bool canEquip)
{
	// The title-screen Setup menu can reach this before item data is loaded.
	if (weaponPort[1].name[0] == '\0')
		JE_loadItemDat();
	else if (customWeaponPort <= 0)
		customWeaponInit();

	// The impact explosions need the explosion sprite sheet, which the game only loads lazily at
	// level start; it can be NULL here (creator opened from Setup, or the between-levels shop).
	if (explosionSpriteSheet.data == NULL)
		JE_loadCompShapes(&explosionSpriteSheet, '6');

	customWeaponSelectLevel(0);   // always open editing power level 1 ...
	customWeaponSelectMode(0);    // ... fire mode 1
	customWeaponMaterialize();    // make sure all compiled slots reflect the saved design

	// Reset editor-only selection state each time the screen opens.
	cwImportSrc = cwClamp(cwImportSrc, 0, (customBulletPresetCount > 0) ? customBulletPresetCount - 1 : 0);
	cwImportPwr = customBulletMaxPower(cwImportSrc);
	cwBulletSel = 0;

	// These stay visible while the field list changes category.
	int actIds[8], actCount = 0;
	actIds[actCount++] = CWACT_UNDO;
	actIds[actCount++] = CWACT_REDO;
	actIds[actCount++] = CWACT_RANDOMIZE;
	actIds[actCount++] = CWACT_SAVE;
	actIds[actCount++] = CWACT_RESET;
	actIds[actCount++] = CWACT_RESET_ALL;
	if (canEquip)
		actIds[actCount++] = CWACT_EQUIP;
	actIds[actCount++] = CWACT_DONE;
	const int actRowCount = (actCount + 1) / 2;   // two actions per line

	const bool prevCentered = (video_get_menu_x_offset() != 0);
	set_menu_centered(true);

	Palette savedPalette;
	memcpy(savedPalette, colors, sizeof(Palette));  // restored on exit

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	// Save the shop backdrop for clipping and preview cleanup.
	JE_loadPic(VGAScreen, 1, true);
	memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

	// Fixed preview geometry lets the shop's interpolated presenter be reused.
	enum { BOX_X0 = 8, BOX_Y0 = 8, BOX_X1 = 143, BOX_Y1 = 182 };
	// panY0/panY1 sit one pixel above the interior so the panel's border lines up
	// exactly with the preview box's frame (drawn at BOX_Y0-1 .. BOX_Y1+1 = 7..183).
	const int panX0 = 150, panX1 = 313, panY0 = 7, panY1 = 183;
	const int itemsTop = panY0 + 13;
	const int row_h = 8;
	const int catY = itemsTop;                              // category selector (pinned, top)
	const int fieldsTop = catY + row_h + 3;                 // scrolling field rows start here
	const int actionsTop = panY1 - 2 - actRowCount * row_h; // action buttons (pinned, bottom; 2 per line)
	const int panMidX = (panX0 + panX1) / 2;                // split point for paired half-width buttons
	const int fieldVis = (actionsTop - 2 - fieldsTop) / row_h;  // field rows visible at once
	const int labelX = panX0 + 5, valueX = panX1 - 5;

	enum { C_PANEL = 0xF1, C_DIV = 0xF6, C_HI = 0xFB, C_LO = 0xF4, C_SEL = 0xF5 };

	const Uint8 previewShip = (player[0].items.ship != 0) ? player[0].items.ship : 1;

	JE_initWeaponView();  // reset shot arrays, place the ship at 72,110 (inside the box)
	cwResetDummies();     // place the practice targets + clear any leftover explosions

	int selected = 0, fieldScrollTop = 0;
	int prev_mx = mouse_x, prev_my = mouse_y;
	bool done = false, equipped = false;

	wait_noinput(false, false, true);
	newkey = newmouse = new_text = false;

	cwOvRow = -1;
	cwTrackedShot = -1;
	cwTrackedSeg = -1;
	cwNumRow = -1;         // not typing a number yet
	cwNumText[0] = '\0';
	cwNoticeTicks = 0;
	cwDeleteConfirmTicks = 0;
	cwHistoryReset();
	weaponSimOverlayFn = cwDrawPreviewOverlay;  // enable the sprite/hitbox overlay while open

	while (!done)
	{
		setDelay(3);

		// A design session lasts minutes. The peer needs the keep-alives, and this machine
		// still owes acknowledgements and adoption for whatever the partner does meanwhile.
		NETWORK_KEEP_ALIVE();
		while (network_shop_pump())
			;

		if (cwNoticeTicks > 0)
			--cwNoticeTicks;
		if (cwDeleteConfirmTicks > 0)
			--cwDeleteConfirmTicks;

		// Build the visible field list for the current category (All = every field row).
		int fieldRows[CWROW_FIELD_COUNT], fieldCount = 0;
		for (int r = 0; r < CWROW_FIELD_COUNT; ++r)
			if (cwCategory == CWCAT_ALL || cwRowCategory(r) == cwCategory)
				fieldRows[fieldCount++] = r;

		// Nav list: [0] = category selector, [1..fieldCount] = fields, then the action buttons.
		const int navCount = 1 + fieldCount + actCount;
		selected = cwClamp(selected, 0, navCount - 1);
		const int selField = (selected >= 1 && selected <= fieldCount) ? fieldRows[selected - 1] : -1;

		if (cwNumRow >= 0 && cwNumRow != selField)  // moved off the field being typed: apply it
			cwCommitNumeric();

		if (selField >= 0)  // keep the selected field inside the scrolling window
		{
			const int fi = selected - 1;
			if (fi < fieldScrollTop)                  fieldScrollTop = fi;
			else if (fi >= fieldScrollTop + fieldVis) fieldScrollTop = fi - fieldVis + 1;
		}
		fieldScrollTop = cwClamp(fieldScrollTop, 0, (fieldCount > fieldVis) ? fieldCount - fieldVis : 0);

		// Draw the preview box.
		power = lastPower = 900;  // never let the preview starve for power
		rl_begin_record();
		fill_rectangle_xy(VGAScreen, BOX_X0, BOX_Y0, BOX_X1, BOX_Y1, 0);
		update_and_draw_starfield(VGAScreen, 1);
		if (cwShowTargets)
			cwDrawDummies();  // practice targets behind the shots (also regen + tick flashes)
		player[0].x = cwShipX;   // user-positioned ship = shot origin
		player[0].y = cwShipY;
		mouseX = player[0].x;
		mouseY = player[0].y;
		if (shotRepeat[0] > 0)
			--shotRepeat[0];
		else  // preview the fire mode + power level currently being edited
			player_shot_create(customWeaponPort, 0, player[0].x, player[0].y, mouseX, mouseY,
			                   CUSTOM_WEAP_BASE + customWeaponEditMode * CUSTOM_POWER_LEVELS + customWeaponEditLevel, 1);
		if (cwShowTargets)
			cwHomeShots();    // curve the shots toward a target when Homing is on
		simulate_player_shots();
		JE_drawSP();          // draw the shot superspark trail (sparky custom bullets) inside
		                      // the box; cwRestoreRect below clips any that flew past the edges
		if (cwShowTargets)
			cwCollideShots(); // impacts, chain cascade, freeze, armour depletion
		JE_drawItem(1, previewShip, player[0].x - 5, player[0].y - 7);
		cwDrawSidekickPreview();  // show the sidekick body sprite when equipped as one
		if (cwShowTargets)
			cwPumpExplosions(); // draw the impact explosions on top
		if (cwShowStats)
			cwDrawStats();      // live DPS / damage / fire-rate HUD (independent of targets)
		rl_end_record();

		// Restore the backdrop outside the preview box.
		cwRestoreRect(0, 0, LEGACY_WIDTH - 1, BOX_Y0 - 1);                   // above
		cwRestoreRect(0, BOX_Y1 + 1, LEGACY_WIDTH - 1, vga_height - 1);      // below
		cwRestoreRect(0, BOX_Y0, BOX_X0 - 1, BOX_Y1);                        // left
		cwRestoreRect(BOX_X1 + 1, BOX_Y0, LEGACY_WIDTH - 1, BOX_Y1);         // right
		JE_rectangle(VGAScreen, BOX_X0 - 1, BOX_Y0 - 1, BOX_X1 + 1, BOX_Y1 + 1, C_HI);  // box frame

		// Draw the editor panel.
		fill_rectangle_xy(VGAScreen, panX0, panY0, panX1, panY1, C_PANEL);
		JE_rectangle(VGAScreen, panX0, panY0, panX1, panY1, C_HI);
		char editorContext[24];
		if (customWeaponModes > 1)
			snprintf(editorContext, sizeof(editorContext), "LV %d  M%d", customWeaponEditLevel + 1, customWeaponEditMode + 1);
		else
			snprintf(editorContext, sizeof(editorContext), "LV %d", customWeaponEditLevel + 1);
		draw_font_hv_shadow(VGAScreen, panX0 + 5, panY0 + 2, "CUSTOM WEAPON",
		                    small_font, left_aligned, 15, 3, false, 1);
		draw_font_hv_shadow(VGAScreen, panX1 - 5, panY0 + 2, editorContext,
		                    small_font, right_aligned, 15, 3, false, 1);
		fill_rectangle_xy(VGAScreen, panX0 + 2, panY0 + 11, panX1 - 2, panY0 + 11, C_DIV);

		// Category selector (pinned at top): Left/Right cycles which group of rows shows.
		{
			const bool sel = (selected == 0);
			fill_rectangle_xy(VGAScreen, panX0 + 2, catY - 1, panX1 - 2, catY + row_h - 2, sel ? C_SEL : C_PANEL);
			char cat[32];
			snprintf(cat, sizeof(cat), "< %s >", cwCatName[cwClamp(cwCategory, 0, CWCAT_COUNT - 1)]);
			draw_font_hv_shadow(VGAScreen, labelX, catY, "View", small_font, left_aligned, 15, sel ? 5 : 3, false, 1);
			draw_font_hv_shadow(VGAScreen, valueX, catY, cat, small_font, right_aligned, 15, sel ? 6 : 4, false, 1);
		}
		fill_rectangle_xy(VGAScreen, panX0 + 2, catY + row_h - 1, panX1 - 2, catY + row_h - 1, C_DIV);

		// Extend the focused category tint through the field area to the pinned actions.
		if (cwCategory != CWCAT_ALL)
			fill_rectangle_xy(VGAScreen, panX0 + 2, fieldsTop - 1, panX1 - 2, actionsTop - 3, cwCatColor(cwCategory));

		// Scrolling field rows for the selected category.
		for (int vis = 0; vis < fieldVis; ++vis)
		{
			const int fi = fieldScrollTop + vis;
			if (fi >= fieldCount)
				break;
			const int row = fieldRows[fi];
			const int ry = fieldsTop + vis * row_h;
			const bool sel = (selected == fi + 1);

			// Each row wears its category's tint; the selected row overrides it.
			fill_rectangle_xy(VGAScreen, panX0 + 2, ry - 1, panX1 - 2, ry + row_h - 2,
			                  sel ? C_SEL : cwGroupColor(row));
			// Text is a LIGHT shade of the row's own category hue (bank), so it always reads
			// lighter than its dark band; the selected row uses the gold UI bank.
			const Uint8 txtHue = sel ? 15 : (Uint8)(cwGroupColor(row) >> 4);
			// A row that doesn't apply to the current Equip slot is drawn dim (unless it's the
			// one under the cursor, which stays bright so its value + hint are still readable).
			const int inlineAct = cwInlineActionId(row);
			const bool dim = !sel && (!cwRowActive(row) ||
			                         (inlineAct >= 0 && !cwActionAvailable(inlineAct)));

			if (inlineAct >= 0)
			{
				draw_font_hv_shadow(VGAScreen, (panX0 + panX1) / 2, ry, cwActionText(inlineAct),
				                    small_font, centered, txtHue, dim ? 2 : 4, false, 1);
			}
			else
			{
				char val[48];
				const bool typing = (cwNumRow == row);
				if (typing)
					snprintf(val, sizeof(val), "%s_", cwNumText);  // show the digits typed so far + caret
				else
				{
					char raw[40];
					cwRowValue(row, raw, sizeof(raw));
					if (sel && row != CWROW_NAME)
						snprintf(val, sizeof(val), "< %s >", raw);
					else
						SDL_strlcpy(val, raw, sizeof(val));
				}
				const bool editing = typing || (row == CWROW_NAME && sel);
				// Long values (bullet/custom names) may need the whole row; only draw the label
				// when it won't collide with the right-aligned value.
				if (labelX + JE_textWidth(cwRowLabel(row), small_font) + 6 <= valueX - JE_textWidth(val, small_font))
					draw_font_hv_shadow(VGAScreen, labelX, ry, cwRowLabel(row), small_font, left_aligned, txtHue, dim ? 2 : 3, false, 1);
				draw_font_hv_shadow(VGAScreen, valueX, ry, val, small_font, right_aligned, txtHue, editing ? 6 : (dim ? 2 : 5), false, 1);
			}
		}

		if (fieldCount > fieldVis)  // field scrollbar (over the scrolling region only)
		{
			const int trackTop = fieldsTop - 1, trackBot = fieldsTop + fieldVis * row_h - 2;
			const int trackH = trackBot - trackTop;
			fill_rectangle_xy(VGAScreen, panX1 - 3, trackTop, panX1 - 2, trackBot, C_LO);
			int thumbH = trackH * fieldVis / fieldCount;
			if (thumbH < 4) thumbH = 4;
			const int denom = fieldCount - fieldVis;
			const int thumbY = trackTop + (denom > 0 ? (trackH - thumbH) * fieldScrollTop / denom : 0);
			fill_rectangle_xy(VGAScreen, panX1 - 3, thumbY, panX1 - 2, thumbY + thumbH, C_HI);
		}

		// Pin action buttons at the bottom, two per row; an odd final button spans both columns.
		fill_rectangle_xy(VGAScreen, panX0 + 2, actionsTop - 2, panX1 - 2, actionsTop - 2, C_DIV);
		for (int a = 0; a < actCount; ++a)
		{
			const int act = actIds[a];
			const int col = a % 2;
			const bool alone = (col == 0 && a + 1 >= actCount);   // even action with no right partner
			const int bx0 = alone ? panX0 + 2 : (col == 0 ? panX0 + 2 : panMidX + 1);
			const int bx1 = alone ? panX1 - 2 : (col == 0 ? panMidX - 1 : panX1 - 2);
			const int ry = actionsTop + (a / 2) * row_h;
			const bool sel = (selected == fieldCount + 1 + a);
			fill_rectangle_xy(VGAScreen, bx0, ry - 1, bx1, ry + row_h - 2, sel ? C_SEL : CW_BG_ACTION);
			draw_font_hv_shadow(VGAScreen, (bx0 + bx1) / 2, ry, cwActionText(act),
			                    small_font, centered, sel ? 15 : (Uint8)(CW_BG_ACTION >> 4),
			                    (!sel && !cwActionAvailable(act)) ? 2 : 4, false, 1);
		}

		{
			// Selected-item help on the bottom line.
			static char hintBuf[96];
			const char *hint;
			int lo, hi;
			if (cwNoticeTicks > 0)           hint = cwNotice;
			else if (cwNumRow >= 0)          hint = "Type a value   -   Enter: set   -   Esc: cancel";
			else if (selected == 0)          hint = "Click left/right: -/+   Tab: view   PgUp/Dn: level";
#ifdef PLATFORM_HANDHELD
			else if (selField == CWROW_NAME) hint = "Confirm to rename   -   Esc: back";
#else
			else if (selField == CWROW_NAME) hint = "Type to rename   -   Esc: back";
#endif
			else if (selField >= 0 && !cwRowActive(selField)) hint = cwRowInactiveReason(selField);
			else if (selField >= 0 && cwNumericRange(selField, &lo, &hi))
			{
				// Numeric field: hint that you can type an exact value, if the line still fits.
				snprintf(hintBuf, sizeof(hintBuf), "%s   -   or type a number", cwRowHelp(selField));
				hint = (JE_textWidth(hintBuf, small_font) <= LEGACY_WIDTH - 16) ? hintBuf : cwRowHelp(selField);
			}
			else if (selField >= 0)          hint = cwRowHelp(selField);
			else                             hint = cwActionHelp(actIds[selected - fieldCount - 1]);
			draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, vga_height - 12, hint, small_font, centered, 15, 2, false, 1);
		}

		rl_finalize();
		rl_capture_residual(VGAScreenSeg, game_screen);

		// The scrolling editor needs arrows, Select, and Back in addition to direct row taps.
		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		push_joysticks_as_keyboard();
		service_SDL_events(false);

		mouseCursor = MOUSE_POINTER_NORMAL;
		cwOvRow = selField;           // overlay keys off the selected field row (-1 = none)
		cwUpdateTrackedShot(selField);  // lock the hitbox onto one live shot (after they moved)
		JE_weaponSimSmoothPresent();  // smooth box interpolation + cursor + present (Smooth Motion on)
		menuWaitWithSmoothCursor();   // waits out the tick if Smooth Motion is off

		// Process input gathered during the tick.
		if (mouse_scroll != 0)
		{
			selected = cwClamp(selected - mouse_scroll, 0, navCount - 1);
			mouse_scroll = 0;
		}

		// Map the mouse to a nav item across the three panel zones (category / fields / actions).
		int hoverNav = -1;
		if (mouse_x >= panX0 && mouse_x <= panX1)
		{
			if (mouse_y >= catY && mouse_y < catY + row_h)
				hoverNav = 0;
			else if (mouse_y >= fieldsTop && mouse_y < fieldsTop + fieldVis * row_h)
			{
				const int fi = fieldScrollTop + (mouse_y - fieldsTop) / row_h;
				if (fi < fieldCount) hoverNav = 1 + fi;
			}
			else if (mouse_y >= actionsTop && mouse_y < actionsTop + actRowCount * row_h)
			{
				// two actions per line: pick the row, then the left/right half by mouse x
				int a = ((mouse_y - actionsTop) / row_h) * 2;
				if (a + 1 < actCount && mouse_x >= panMidX)   // row has a right half and we're on it
					a += 1;
				if (a < actCount)
					hoverNav = fieldCount + 1 + a;
			}
		}
		if (hoverNav >= 0 && (mouse_x != prev_mx || mouse_y != prev_my))
			selected = hoverNav;
		prev_mx = mouse_x;
		prev_my = mouse_y;

		if (newmouse)
		{
			if (cwNumRow >= 0)  // a click anywhere finalizes an in-progress typed value first
				cwCommitNumeric();
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				done = true;
			}
			else if (hoverNav >= 0)
			{
				selected = hoverNav;
				if (hoverNav == 0)
				{
					const int dir = (mouse_x < panMidX) ? -1 : 1;
					cwCategory = (cwCategory + CWCAT_COUNT + dir) % CWCAT_COUNT;
					fieldScrollTop = 0;
					JE_playSampleNum(S_CURSOR);
				}
				else if (hoverNav <= fieldCount)  // a field row: click adjusts / triggers inline action
				{
					const int f = fieldRows[hoverNav - 1];
					if (!cwRowActive(f))
						JE_playSampleNum(S_SPRING);  // doesn't apply to the current Equip slot
					else if (cwInlineActionId(f) >= 0)
						cwPerformAction(cwInlineActionId(f), &done, &equipped);
					else
					{
						cwAdjustRow(f, (mouse_x < panMidX) ? -1 : 1);
						JE_playSampleNum(S_CURSOR);
					}
				}
				else  // an action button
				{
					cwPerformAction(actIds[hoverNav - fieldCount - 1], &done, &equipped);
				}
			}
			newmouse = false;
		}

		if (selField == CWROW_NAME && new_text &&
		    (lastkey_mod & (KMOD_CTRL | KMOD_GUI)) == 0)
		{
			size_t len = strlen(customWeaponName);
			for (size_t ti = 0; last_text[ti] != '\0'; ++ti)
			{
				const unsigned char c = (unsigned char)last_text[ti];
				if (c >= 32 && c < 127 && len < sizeof(customWeaponName) - 1)
					customWeaponName[len++] = (char)c;
			}
			customWeaponName[len] = '\0';
			customWeaponMaterialize();
			cwHistoryRecord();
		}
		else if (new_text && selField >= 0 && cwRowActive(selField))  // direct numeric entry into a value field
		{
			int lo, hi;
			if (cwNumericRange(selField, &lo, &hi))
			{
				for (size_t ti = 0; last_text[ti] != '\0'; ++ti)
				{
					const char c = last_text[ti];
					const bool digit = (c >= '0' && c <= '9');
					const bool minus = (c == '-' && lo < 0);
					if (!digit && !minus)
						continue;
					if (cwNumRow != selField) { cwNumRow = selField; cwNumText[0] = '\0'; }  // begin typing
					size_t len = strlen(cwNumText);
					if (minus)
					{
						if (len == 0) { cwNumText[0] = '-'; cwNumText[1] = '\0'; }  // sign only as the first char
					}
					else if (len < sizeof(cwNumText) - 1)
					{
						cwNumText[len] = c;
						cwNumText[len + 1] = '\0';
					}
				}
			}
		}
		new_text = false;

		if (newkey)
		{
			const bool commandKey = (lastkey_mod & (KMOD_CTRL | KMOD_GUI)) != 0;
			if (commandKey && lastkey_scan == SDL_SCANCODE_Z)
			{
				if (cwNumRow >= 0)
					cwCommitNumeric();
				cwPerformAction((lastkey_mod & KMOD_SHIFT) ? CWACT_REDO : CWACT_UNDO, &done, &equipped);
			}
			else if (commandKey && lastkey_scan == SDL_SCANCODE_Y)
			{
				if (cwNumRow >= 0)
					cwCommitNumeric();
				cwPerformAction(CWACT_REDO, &done, &equipped);
			}
			else if (commandKey && lastkey_scan == SDL_SCANCODE_S)
			{
				if (cwNumRow >= 0)
					cwCommitNumeric();
				cwPerformAction(CWACT_SAVE, &done, &equipped);
			}
			else switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:    if (cwNumRow >= 0) cwCommitNumeric(); selected = (selected == 0) ? navCount - 1 : selected - 1; break;
			case SDL_SCANCODE_DOWN:  if (cwNumRow >= 0) cwCommitNumeric(); selected = (selected + 1) % navCount; break;
			case SDL_SCANCODE_TAB:
			{
				if (cwNumRow >= 0)
					cwCommitNumeric();
				const int dir = (lastkey_mod & KMOD_SHIFT) ? -1 : 1;
				cwCategory = (cwCategory + CWCAT_COUNT + dir) % CWCAT_COUNT;
				fieldScrollTop = 0;
				selected = 1;
				JE_playSampleNum(S_CURSOR);
				break;
			}
			case SDL_SCANCODE_PAGEUP:
			case SDL_SCANCODE_PAGEDOWN:
				if (cwNumRow >= 0)
					cwCommitNumeric();
				cwAdjustRow(CWROW_POWER_LEVEL, lastkey_scan == SDL_SCANCODE_PAGEUP ? -1 : 1);
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_LEFTBRACKET:
			case SDL_SCANCODE_RIGHTBRACKET:
				if (selField != CWROW_NAME)
				{
					if (cwNumRow >= 0)
						cwCommitNumeric();
					const int count = cwClamp(cwCur()->multi, 1, CUSTOM_BULLETS_MAX);
					const int dir = (lastkey_scan == SDL_SCANCODE_RIGHTBRACKET) ? 1 : -1;
					cwBulletSel = (cwBulletIndex() + count + dir) % count;
					JE_playSampleNum(S_CURSOR);
				}
				break;
			case SDL_SCANCODE_LEFT:
			{
				if (cwNumRow >= 0)             { cwCommitNumeric(); JE_playSampleNum(S_SELECT); break; }  // arrows finish typing
				if (selected == 0)             { cwCategory = (cwCategory + CWCAT_COUNT - 1) % CWCAT_COUNT; fieldScrollTop = 0; JE_playSampleNum(S_CURSOR); }
				else if (selField < 0)         break;
				else if (!cwRowActive(selField)) JE_playSampleNum(S_SPRING);  // greyed: doesn't apply here
				else
				{
					int lo, hi;
					const int step = ((lastkey_mod & KMOD_SHIFT) && cwNumericRange(selField, &lo, &hi)) ? 10 : 1;
					cwAdjustRow(selField, -step);
					JE_playSampleNum(S_CURSOR);
				}
				break;
			}
			case SDL_SCANCODE_RIGHT:
			{
				if (cwNumRow >= 0)             { cwCommitNumeric(); JE_playSampleNum(S_SELECT); break; }  // arrows finish typing
				if (selected == 0)             { cwCategory = (cwCategory + 1) % CWCAT_COUNT; fieldScrollTop = 0; JE_playSampleNum(S_CURSOR); }
				else if (selField < 0)         break;
				else if (!cwRowActive(selField)) JE_playSampleNum(S_SPRING);  // greyed: doesn't apply here
				else
				{
					int lo, hi;
					const int step = ((lastkey_mod & KMOD_SHIFT) && cwNumericRange(selField, &lo, &hi)) ? 10 : 1;
					cwAdjustRow(selField, step);
					JE_playSampleNum(S_CURSOR);
				}
				break;
			}
			case SDL_SCANCODE_BACKSPACE:
				if (cwNumRow >= 0)  // editing a number: erase the last typed digit
				{
					size_t len = strlen(cwNumText);
					if (len > 0)
						cwNumText[len - 1] = '\0';
					if (cwNumText[0] == '\0')
						cwNumRow = -1;  // buffer empty: leave typing mode, value unchanged
				}
				else if (selField == CWROW_NAME)
				{
					size_t len = strlen(customWeaponName);
					if (len > 0)
						customWeaponName[len - 1] = '\0';
					customWeaponMaterialize();
					cwHistoryRecord();
				}
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				if (cwNumRow >= 0)  // finish direct numeric entry
					{ cwCommitNumeric(); JE_playSampleNum(S_SELECT); }
				else if (selected == 0)  // category selector: Enter also cycles forward
					{ cwCategory = (cwCategory + 1) % CWCAT_COUNT; fieldScrollTop = 0; JE_playSampleNum(S_CURSOR); }
				else if (selField < 0)  // an action button
					cwPerformAction(actIds[selected - fieldCount - 1], &done, &equipped);
				else if (!cwRowActive(selField))
					JE_playSampleNum(S_SPRING);  // greyed: doesn't apply to the current Equip slot
				else if (cwInlineActionId(selField) >= 0)
					cwPerformAction(cwInlineActionId(selField), &done, &equipped);
#ifdef PLATFORM_HANDHELD
				else if (selField == CWROW_NAME)
				{
					// Open the system keyboard when Select activates the name row.
					char kb[sizeof(customWeaponName)];
					SDL_strlcpy(kb, customWeaponName, sizeof(kb));
					if (console_swkbd(kb, sizeof(kb), sizeof(kb) - 1, kb, "Weapon name", false))
					{
						size_t out = 0;
						for (const char *c = kb; *c != '\0' && out + 1 < sizeof(customWeaponName); ++c)
						{
							if ((unsigned char)*c >= 32 && (unsigned char)*c < 127)
								customWeaponName[out++] = *c;
						}
						customWeaponName[out] = '\0';
						customWeaponMaterialize();
						cwHistoryRecord();
					}
				}
#endif
				else if (selField == CWROW_LIB_SELECT || selField == CWROW_POWER_LEVEL ||
				         selField == CWROW_TWO_MODES ||
				         selField == CWROW_FIRE_MODE || selField == CWROW_SHOW_TARGETS ||
				         selField == CWROW_SHOW_STATS || selField == CWROW_EQUIP_SLOT ||
				         selField == CWROW_IMPORT_SRC || selField == CWROW_IMPORT_PWR ||
				         selField == CWROW_BULLET_SEL ||
				         selField == CWROW_SK_MOUNT || selField == CWROW_SK_ANIMATE)
				{
					cwAdjustRow(selField, +1);  // Enter cycles the enum-style / selector fields
					JE_playSampleNum(S_CURSOR);
				}
				break;
			case SDL_SCANCODE_ESCAPE:
				if (cwNumRow >= 0)  // cancel an in-progress typed value, stay in the editor
					{ cwNumRow = -1; cwNumText[0] = '\0'; JE_playSampleNum(S_CURSOR); }
				else
					done = true;
				break;
			default: break;
			}
			newkey = false;
		}
	}

	weaponSimOverlayFn = NULL;  // stop overlaying (JE_weaponSimSmoothPresent is shared with the shop)
	for (int j = 0; j < MAX_EXPLOSIONS; ++j)  // don't leak editor impacts back to the shop/game
		explosions[j].ttl = 0;
	touch_ui_clear_layout();   // nothing fades here, so take the cursor keys with the editor
	wait_noinput(false, false, true);

	VGAScreen = temp_surface;
	set_menu_centered(prevCentered);
	memcpy(colors, savedPalette, sizeof(Palette));  // restore the caller's palette
	set_palette(colors, 0, 255);

	customWeaponLibrarySave();  // persist the whole weapon library to its own file
	save_opentyrian_config();
	return equipped;
}
