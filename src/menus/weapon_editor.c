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
#include "weapon_editor.h"

#include "config.h"
#include "console_platform.h"
#include "custom_weapon.h"
#include "draw.h"
#include "episodes.h"
#include "explosions.h"
#include "font.h"
#include "game_loop.h"
#include "game_menu.h"
#include "game_state.h"
#include "health_bars.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "network.h"
#include "palette.h"
#include "picload.h"
#include "player.h"
#include "render_list.h"
#include "sfx.h"
#include "ship_editor.h"
#include "shots.h"
#include "sndmast.h"
#include "sparks.h"
#include "sprite.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Weapon Editor: live firing preview on the left, edit panel on the right.
 * Every edit materializes the weapon again so the preview stays current. */

enum
{
	CWF_LIB_SELECT,
	CWF_NAME,
	CWF_EQUIP_SLOT,
	CWF_ICON,
	CWF_COST,
	CWF_POWER_USE,
	CWF_POWER_LEVEL,
	CWF_TWO_MODES,
	CWF_FIRE_MODE,
	CWF_FIRE_RATE,
	CWF_BULLETS,
	CWF_PATTERN,
	CWF_ANIM,
	CWF_HOMING,
	CWF_ACCEL_Y,
	CWF_ACCEL_X,
	CWF_SPIRAL,
	CWF_SOUND,
	CWF_TRAIL,
	CWF_BLAST,
	CWF_BULLET_SEL,
	CWF_B_SPRITE,
	CWF_B_SPARKS,
	CWF_B_DAMAGE,
	CWF_B_CHAIN,
	CWF_B_LIFE,
	CWF_B_VELX,
	CWF_B_VELY,
	CWF_B_OFFX,
	CWF_B_OFFY,
	CWF_IMPORT_SRC,
	CWF_IMPORT_PWR,
	CWF_SK_MOUNT,
	CWF_SK_SPRITE,
	CWF_SK_FRAMES,
	CWF_SK_STEP,
	CWF_SK_ANIMATE,
	CWF_CHARGE,
	CWF_SHOW_TARGETS,
	CWF_SHOW_STATS,
	CWF_STARS,
	CWF_TARGET_SIZE,
	CWF_TARGET_SPREAD,
	CWF_TARGET_Y,
	CWF_TARGET_X,
	CWF_TARGET_ARMOR,
	CWF_PREVIEW_SHIP,
	CWF_SHIP_X,
	CWF_SHIP_Y,
	CWF_SPREAD,
	CWF_COUNT
};

static const struct { const char *label, *help; } cwFields[CWF_COUNT] =
{
	{ "Weapon",          "Choose which saved weapon to edit." },
	{ "Name",            "Type to rename. Backspace erases." },
	{ "Equip To",        "Mount it in a gun bay or as a sidekick." },
	{ "Shop Icon",       "The icon shown in the shop and on the HUD." },
	{ "Cost",            "The shop price." },
	{ "Power Use",       "Generator power drained by each shot." },
	{ "Level",           "The power level being edited and previewed." },
	{ "Fire Modes",      "A rear gun can toggle between two designs." },
	{ "Fire Mode",       "Which of the two rear designs you are editing." },
	{ "Fire Rate",       "Ticks between volleys. Lasers need Every tick." },
	{ "Bullets",         "Bullets fired in each volley." },
	{ "Pattern",         "Pattern length. Above Bullets varies volleys." },
	{ "Anim Frames",     "Sprite frames each bullet cycles through." },
	{ "Homing",          "Steers bullets toward enemies above 5." },
	{ "Accel Up",        "Vertical acceleration." },
	{ "Accel Side",      "Sideways acceleration." },
	{ "Spiral",          "Corkscrew radius. Off flies straight." },
	{ "Sound",           "Sound played when the weapon fires." },
	{ "Trail",           "Exhaust trail behind each bullet." },
	{ "Blast FX",        "Palette filter of the impact flash." },
	{ "Bullet",          "Which bullet the rows below edit." },
	{ "Sprite",          "Bullet sprite number." },
	{ "Sparks",          "Trail sparks in the bullet's own colour." },
	{ "Damage",          "Damage per hit. 99 freezes as Ice." },
	{ "Explode To",      "On a hit, burst into another weapon instead." },
	{ "Life",            "Lifetime in ticks, or an aim or animation code." },
	{ "Speed X",         "Plus moves right, minus left. Above 100 locks." },
	{ "Speed Y",         "Plus moves up, minus down. 98 hovers." },
	{ "Offset X",        "Horizontal spawn offset from the ship." },
	{ "Offset Y",        "Vertical spawn offset from the ship." },
	{ "Source",          "Stock weapon to copy from. The preview fires it." },
	{ "Source Level",    "Source level for Replace and Add To Level." },
	{ "Mount",           "Where the sidekick sits around the ship." },
	{ "Body Sprite",     "Sidekick body sprite. Charge stages step up from it." },
	{ "Body Frames",     "Body animation frames. 1 is static." },
	{ "Frame Step",      "Sprite gap between body animation frames." },
	{ "Animate",         "Animate the body always or once per shot." },
	{ "Charge Stages",   "Charge shots. Each stage is a level to edit." },
	{ "Targets",         "Practice targets that show hits, ice and homing." },
	{ "Stats",           "Damage and fire rate under the level chart." },
	{ "Star Background", "Show the drifting stars behind the preview." },
	{ "Target Size",     "Width of the practice targets." },
	{ "Target Spread",   "Space between the targets." },
	{ "Target Y",        "How high the targets sit. Drag them too." },
	{ "Target X",        "Slide the target row. Drag it too." },
	{ "Target Armor",    "Target toughness. Higher drains slower." },
	{ "Ship",            "The ship flying the preview. Equipped is your own." },
	{ "Ship X",          "Move the ship. Drag it too." },
	{ "Ship Y",          "Move the ship. Drag it too." },
	{ "Spread",          "How Auto-Scale lays out bullets. Auto picks at random." },
};

enum
{
	CWA_UNDO,
	CWA_REDO,
	CWA_SAVE,
	CWA_EQUIP,
	CWA_DONE,
	CWA_LIB_NEW,
	CWA_LIB_DUPLICATE,
	CWA_LIB_DELETE,
	CWA_REVERT,
	CWA_ADD_BULLET,
	CWA_REMOVE_BULLET,
	CWA_AUTOSCALE,
	CWA_COPY_ALL,
	CWA_COPY_LEVEL,
	CWA_PASTE_LEVEL,
	CWA_RANDOMIZE,
	CWA_RESET,
	CWA_RESET_ALL,
	CWA_IMPORT_LEVEL,
	CWA_IMPORT_ALL,
	CWA_ADD_LEVEL,
	CWA_ADD_ALL_LEVELS,
	CWA_RANGE_RESET,
	CWA_COUNT
};

static const struct { const char *label, *help; } cwActs[CWA_COUNT] =
{
	{ "Undo",          "Take back the last edit. Also Ctrl+Z." },
	{ "Redo",          "Restore an undone edit. Also Ctrl+Y." },
	{ "Save",          "Save the weapon library now. Also Ctrl+S." },
	{ "Equip",         "Equip this weapon and return to the shop." },
	{ "Done",          "Save the library and leave." },
	{ "New",           "Add a new weapon to the library." },
	{ "Duplicate",     "Copy this weapon into a new library slot." },
	{ "Delete",        "Remove this weapon from the library." },
	{ "Revert",        "Discard every change made since opening." },
	{ "Add Bullet",    "Duplicate the selected bullet." },
	{ "Remove Bullet", "Delete the selected bullet." },
	{ "Auto-Scale",    "Build the other levels from this one." },
	{ "Copy To All",   "Copy this level to every level." },
	{ "Copy Level",    "Copy this level for pasting elsewhere." },
	{ "Paste Level",   "Replace this level with the copied one." },
	{ "Randomize",     "Roll a random design and name for this level." },
	{ "Reset Level",   "Restore this level to the default blaster." },
	{ "Reset Weapon",  "Restore the whole weapon to its defaults." },
	{ "Replace Level", "Replace this level with the source's shots." },
	{ "Replace All",   "Replace the whole weapon with the source." },
	{ "Add To Level",  "Add the source's bullets to this level." },
	{ "Add To All",    "Add the source's bullets to every level." },
	{ "Reset Range",   "Restore the default ship and target layout." },
};

enum
{
	CWP_WEAPON,
	CWP_SHOTS,
	CWP_BULLET,
	CWP_LEVELS,
	CWP_IMPORT,
	CWP_SIDEKICK,
	CWP_RANGE,
	CWP_HELP,
	CWP_COUNT
};

static const struct { const char *name, *help; } cwPageInfo[CWP_COUNT] =
{
	{ "Weapon",   "Library, name, mount, icon and price." },
	{ "Shots",    "How each volley fires at the selected level." },
	{ "Bullet",   "Each bullet of the selected level." },
	{ "Levels",   "Power levels, fire modes and level tools." },
	{ "Import",   "Start from a stock weapon. The preview fires it." },
	{ "Sidekick", "Sidekick body, mount and charge stages." },
	{ "Range",    "Practice targets, stats and preview layout." },
	{ "Help",     "Keyboard, mouse and controller shortcuts." },
};

enum { CWL_END, CWL_FIELD, CWL_BUTTONS, CWL_GAP };

typedef struct
{
	Uint8 kind, count;
	Uint8 id[3];
} CwLine;

static const CwLine cwWeaponLines[] =
{
	{ CWL_FIELD, 1, { CWF_LIB_SELECT } },
	{ CWL_FIELD, 1, { CWF_NAME } },
	{ CWL_FIELD, 1, { CWF_EQUIP_SLOT } },
	{ CWL_FIELD, 1, { CWF_ICON } },
	{ CWL_FIELD, 1, { CWF_COST } },
	{ CWL_FIELD, 1, { CWF_POWER_USE } },
	{ CWL_GAP, 0, { 0 } },
	{ CWL_BUTTONS, 3, { CWA_LIB_NEW, CWA_LIB_DUPLICATE, CWA_LIB_DELETE } },
	{ CWL_BUTTONS, 1, { CWA_REVERT } },
	{ CWL_END, 0, { 0 } },
};

static const CwLine cwShotsLines[] =
{
	{ CWL_FIELD, 1, { CWF_POWER_LEVEL } },
	{ CWL_FIELD, 1, { CWF_FIRE_RATE } },
	{ CWL_FIELD, 1, { CWF_BULLETS } },
	{ CWL_FIELD, 1, { CWF_PATTERN } },
	{ CWL_FIELD, 1, { CWF_ANIM } },
	{ CWL_FIELD, 1, { CWF_HOMING } },
	{ CWL_FIELD, 1, { CWF_ACCEL_Y } },
	{ CWL_FIELD, 1, { CWF_ACCEL_X } },
	{ CWL_FIELD, 1, { CWF_SPIRAL } },
	{ CWL_FIELD, 1, { CWF_SOUND } },
	{ CWL_FIELD, 1, { CWF_TRAIL } },
	{ CWL_FIELD, 1, { CWF_BLAST } },
	{ CWL_END, 0, { 0 } },
};

static const CwLine cwBulletLines[] =
{
	{ CWL_FIELD, 1, { CWF_POWER_LEVEL } },
	{ CWL_FIELD, 1, { CWF_BULLET_SEL } },
	{ CWL_BUTTONS, 2, { CWA_ADD_BULLET, CWA_REMOVE_BULLET } },
	{ CWL_FIELD, 1, { CWF_B_SPRITE } },
	{ CWL_FIELD, 1, { CWF_B_SPARKS } },
	{ CWL_FIELD, 1, { CWF_B_DAMAGE } },
	{ CWL_FIELD, 1, { CWF_B_CHAIN } },
	{ CWL_FIELD, 1, { CWF_B_LIFE } },
	{ CWL_FIELD, 1, { CWF_B_VELX } },
	{ CWL_FIELD, 1, { CWF_B_VELY } },
	{ CWL_FIELD, 1, { CWF_B_OFFX } },
	{ CWL_FIELD, 1, { CWF_B_OFFY } },
	{ CWL_END, 0, { 0 } },
};

static const CwLine cwLevelsLines[] =
{
	{ CWL_FIELD, 1, { CWF_POWER_LEVEL } },
	{ CWL_FIELD, 1, { CWF_TWO_MODES } },
	{ CWL_FIELD, 1, { CWF_FIRE_MODE } },
	{ CWL_FIELD, 1, { CWF_SPREAD } },
	{ CWL_GAP, 0, { 0 } },
	{ CWL_BUTTONS, 2, { CWA_AUTOSCALE, CWA_COPY_ALL } },
	{ CWL_BUTTONS, 2, { CWA_COPY_LEVEL, CWA_PASTE_LEVEL } },
	{ CWL_BUTTONS, 2, { CWA_RANDOMIZE, CWA_RESET } },
	{ CWL_BUTTONS, 1, { CWA_RESET_ALL } },
	{ CWL_END, 0, { 0 } },
};

static const CwLine cwImportLines[] =
{
	{ CWL_FIELD, 1, { CWF_IMPORT_SRC } },
	{ CWL_FIELD, 1, { CWF_IMPORT_PWR } },
	{ CWL_GAP, 0, { 0 } },
	{ CWL_BUTTONS, 2, { CWA_IMPORT_LEVEL, CWA_IMPORT_ALL } },
	{ CWL_BUTTONS, 2, { CWA_ADD_LEVEL, CWA_ADD_ALL_LEVELS } },
	{ CWL_END, 0, { 0 } },
};

static const CwLine cwSidekickLines[] =
{
	{ CWL_FIELD, 1, { CWF_EQUIP_SLOT } },
	{ CWL_GAP, 0, { 0 } },
	{ CWL_FIELD, 1, { CWF_SK_MOUNT } },
	{ CWL_FIELD, 1, { CWF_SK_SPRITE } },
	{ CWL_FIELD, 1, { CWF_SK_FRAMES } },
	{ CWL_FIELD, 1, { CWF_SK_STEP } },
	{ CWL_FIELD, 1, { CWF_SK_ANIMATE } },
	{ CWL_FIELD, 1, { CWF_CHARGE } },
	{ CWL_END, 0, { 0 } },
};

static const CwLine cwRangeLines[] =
{
	{ CWL_FIELD, 1, { CWF_SHOW_TARGETS } },
	{ CWL_FIELD, 1, { CWF_SHOW_STATS } },
	{ CWL_FIELD, 1, { CWF_STARS } },
	{ CWL_FIELD, 1, { CWF_TARGET_SIZE } },
	{ CWL_FIELD, 1, { CWF_TARGET_SPREAD } },
	{ CWL_FIELD, 1, { CWF_TARGET_Y } },
	{ CWL_FIELD, 1, { CWF_TARGET_X } },
	{ CWL_FIELD, 1, { CWF_TARGET_ARMOR } },
	{ CWL_FIELD, 1, { CWF_PREVIEW_SHIP } },
	{ CWL_FIELD, 1, { CWF_SHIP_X } },
	{ CWL_FIELD, 1, { CWF_SHIP_Y } },
	{ CWL_BUTTONS, 1, { CWA_RANGE_RESET } },
	{ CWL_END, 0, { 0 } },
};

static const CwLine cwHelpLines[] =
{
	{ CWL_END, 0, { 0 } },
};

static const CwLine *const cwPageLines[CWP_COUNT] =
{
	cwWeaponLines, cwShotsLines, cwBulletLines, cwLevelsLines,
	cwImportLines, cwSidekickLines, cwRangeLines, cwHelpLines,
};

static const char *const cwHelpText[][2] =
{
#ifdef PLATFORM_HANDHELD
	{ "Up, Down",        "Choose a row" },
	{ "Left, Right",     "Value or page" },
	{ "Confirm",         "Use a button" },
	{ "Cancel",          "Save and leave" },
	{ "Tap a row",       "Select, change" },
	{ "Tap a tab",       "Open that page" },
	{ "Tap a level bar", "Edit that level" },
	{ "Drag preview",    "Ship and targets" },
#else
	{ "Tab",             "Next page" },
	{ "PgUp, PgDn",      "Change level" },
	{ "[ and ]",         "Change bullet" },
	{ "Shift, arrows",   "Step by ten" },
	{ "Digits",          "Type a value" },
	{ "Ctrl+Z, Ctrl+Y",  "Undo, redo" },
	{ "Ctrl+S",          "Save" },
	{ "Right-click row", "Step back" },
	{ "Click level bar", "Edit that level" },
	{ "Drag preview",    "Ship and targets" },
	{ "Esc",             "Save and leave" },
#endif
};

static const char *const cwSlotNames[CUSTOM_EQUIP_COUNT] =
{
	"Front Gun", "Rear Gun", "Left Sidekick", "Right Sidekick", "Both Sidekicks"
};

// Sidekick mount styles, indexed by the engine's option "tr" (0..4). "Trail Big" (tr 1) and
// "Front" (tr 2) draw a 2x2 body from spriteSheet10; the rest a single tile from spriteSheet9.
static const char *const cwMountNames[CUSTOM_SIDEKICK_MOUNTS] =
{
	"Side Pod", "Trail (Big)", "Front", "Trailing", "Orbit"
};

enum
{
	CW_BOX_X0 = 8, CW_BOX_Y0 = 8, CW_BOX_X1 = 143, CW_BOX_Y1 = 182,
	CW_BOX_W = CW_BOX_X1 - CW_BOX_X0 + 1, CW_BOX_H = CW_BOX_Y1 - CW_BOX_Y0 + 1,
	CW_BOX_MID = (CW_BOX_X0 + CW_BOX_X1 + 1) / 2,
	CW_RANGE_Y1 = 139,
	CW_TRAY_Y0 = 140,
	CW_BAR_X0 = 12, CW_BAR_PITCH = 12, CW_BAR_W = 8,
	CW_BAR_TOP = 144, CW_BAR_BOT = 159,
	CW_BAR_NUM_Y = 163,
	CW_STATS_Y = 172,
};

enum
{
	CW_PAN_X0 = 150, CW_PAN_X1 = 313, CW_PAN_Y0 = 7, CW_PAN_Y1 = 183,
	CW_PAN_MID = (CW_PAN_X0 + CW_PAN_X1) / 2,
	CW_INNER_X0 = CW_PAN_X0 + 2, CW_INNER_X1 = CW_PAN_X1 - 2,
	CW_LABEL_X = CW_PAN_X0 + 5, CW_VALUE_X = CW_PAN_X1 - 5,
	CW_TAB_Y0 = 19, CW_TAB_PITCH = 12, CW_TAB_COLS = 4,
	CW_PAGE_Y0 = 46, CW_ROW_H = 10, CW_GAP_H = 3,
	CW_ACTIONS_Y = CW_PAN_Y1 - 11,
};

enum
{
	CW_BG_SPACE = 0x50, CW_BG_BAND = 0x51, CW_BG_SOCKET = 0x53, CW_BG_RIM = 0x54,
	CW_BG_CLAMP = 0x55, CW_BG_SELECT = 0x5a, CW_BG_WELL = 0x60,
	CW_STAR_COUNT = 28,
};

enum { C_PANEL = 0xF1, C_WELL = 0xF0, C_DIV = 0xF6, C_HI = 0xFB, C_SEL = 0xF5 };

enum
{
	CW_SHIP_X_MIN = 14, CW_SHIP_X_MAX = 124,
	CW_SHIP_Y_MIN = 16, CW_SHIP_Y_MAX = 118,
	CW_TARGET_X_MIN = 20, CW_TARGET_X_MAX = 132,
	CW_TARGET_SIZE_MIN = 4, CW_TARGET_SIZE_MAX = 20,
};

enum { CWN_TABS, CWN_FIELD, CWN_BUTTONS, CWN_ACTIONS };
enum { CW_NAV_MAX = 20 };

typedef struct
{
	Uint8 kind, count;
	Uint8 id[CWP_COUNT];
	int y;
} CwNav;

static CwNav cwNav[CW_NAV_MAX];
static int cwNavCount;
static int cwSelRow, cwSelCell;
static int cwPage = CWP_WEAPON;
static int cwPageRow[CWP_COUNT];
static bool cwCanEquip;
static int cwArmed = -1;

static int cwImportSrc = 0;
static int cwImportPwr = 1;
static int cwBulletSel = 0;

static bool cwShowStats    = true;
static int  cwTargetCount  = 3;
static int  cwTargetSize   = 8;
static int  cwTargetSpread = 32;
static int  cwTargetY      = 36;
static int  cwTargetX      = 76;
static int  cwTargetArmor  = 120;
static int  cwShipX = 70;
static int  cwShipY = 116;
static int  cwTickShipX = 70;
static int  cwTickShipY = 116;

static JE_WeaponType cwLevelClipboard;
static bool cwLevelClipboardSet = false;

static int  cwNumField = -1;
static char cwNumText[12];

static char cwNotice[64];
static int  cwNoticeTicks = 0;

static bool cwDirty;
static Uint8 *cwRevertBuf;
static size_t cwRevertLen;

static int cwClamp(int v, int lo, int hi)
{
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

static void cwSetNotice(const char *text)
{
	SDL_strlcpy(cwNotice, text, sizeof(cwNotice));
	cwNoticeTicks = 40;
}

static const char *cwFitText(const char *text, int maxWidth, char *buf, size_t n)
{
	if (JE_textWidth(text, small_font) <= maxWidth)
		return text;
	for (int len = (int)strlen(text) - 1; len > 0; --len)
	{
		while (len > 0 && text[len - 1] == ' ')
			--len;
		snprintf(buf, n, "%.*s..", len, text);
		if (JE_textWidth(buf, small_font) <= maxWidth)
			return buf;
	}
	return "";
}

static bool cwIsSidekickSlot(void)
{
	return customWeaponEquipSlot == CUSTOM_EQUIP_LEFT ||
	       customWeaponEquipSlot == CUSTOM_EQUIP_RIGHT ||
	       customWeaponEquipSlot == CUSTOM_EQUIP_BOTH;
}

static bool cwLevelUsed(int level)
{
	return !cwIsSidekickSlot() || level < customWeaponChargeStages;
}

static JE_WeaponType *cwCur(void)
{
	const int m = cwClamp(customWeaponEditMode,  0, CUSTOM_WEAPON_MODES - 1);
	const int p = cwClamp(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	return &customWeaponRaw[m][p];
}

static int cwBulletIndex(void)
{
	return cwClamp(cwBulletSel, 0, cwClamp(cwCur()->multi, 1, CUSTOM_BULLETS_MAX) - 1);
}

typedef struct { int cx, cy, armor, hit, ice, dead, idle; } CwDummy;
enum { CW_DUMMY_MAX = 6 };
static CwDummy cwDummies[CW_DUMMY_MAX];
static int cwDummyCount = 0;

static int cwTargetYMin(void)
{
	return CW_BOX_Y0 + cwTargetSize + 3;
}

static int cwTargetYMax(void)
{
	return CW_RANGE_Y1 - cwTargetSize - 6;
}

static void cwPlaceDummies(void)
{
	for (int i = 0; i < cwDummyCount; ++i)
	{
		const int off = 2 * i - (cwDummyCount - 1);
		cwDummies[i].cx = cwClamp(cwTargetX + off * cwTargetSpread / 2,
		                          CW_BOX_X0 + cwTargetSize + 2, CW_BOX_X1 - cwTargetSize - 2);
		cwDummies[i].cy = cwClamp(cwTargetY, cwTargetYMin(), cwTargetYMax());
	}
}

static void cwLayoutDummies(void)
{
	cwDummyCount = cwClamp(cwTargetCount, 0, CW_DUMMY_MAX);
	for (int i = 0; i < cwDummyCount; ++i)
	{
		cwDummies[i].armor = cwTargetArmor;
		cwDummies[i].hit = cwDummies[i].ice = cwDummies[i].dead = cwDummies[i].idle = 0;
	}
	cwPlaceDummies();
}

typedef struct
{
	unsigned int frame;
	bool animating;
} CwPodAnim;

static CwPodAnim cwPodAnim[2];
static float cwOrbit = 0.0f;
static int cwTrackedShot = -1;
static int cwTrackedSeg = -1;
static int cwPreviewKey = -1;

static void cwClearPreviewShots(void)
{
	memset(shotAvail, 0, sizeof(shotAvail));
	memset(shotRepeat, 1, sizeof(shotRepeat));
	memset(shotMultiPos, 0, sizeof(shotMultiPos));
	for (int i = 0; i < MAX_EXPLOSIONS; ++i)
		explosions[i].ttl = 0;
	memset(cwPodAnim, 0, sizeof(cwPodAnim));
	cwTrackedShot = -1;
	cwTrackedSeg = -1;
}

// Undo tracks the working weapon only. Switching library slots starts a fresh history.
enum { CW_HISTORY_MAX = 64 };
static CustomWeaponSlot cwHistory[CW_HISTORY_MAX];
static int cwHistoryFirst, cwHistoryCount, cwHistoryPos;
static int cwHistoryMerge = -1;

static CustomWeaponSlot *cwHistoryAt(int i)
{
	return &cwHistory[(cwHistoryFirst + i) % CW_HISTORY_MAX];
}

static void cwCaptureWorking(CustomWeaponSlot *s)
{
	memset(s, 0, sizeof(*s));
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

static void cwRestoreWorking(const CustomWeaponSlot *s)
{
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
	cwHistoryFirst = 0;
	cwHistoryCount = 1;
	cwHistoryPos = 0;
	cwHistoryMerge = -1;
	cwCaptureWorking(cwHistoryAt(0));
}

static bool cwHistoryRecord(int merge)
{
	static CustomWeaponSlot next;
	cwCaptureWorking(&next);
	if (memcmp(&next, cwHistoryAt(cwHistoryPos), sizeof(next)) == 0)
		return false;

	cwDirty = true;
	if (merge >= 0 && merge == cwHistoryMerge && cwHistoryPos > 0 &&
	    cwHistoryPos == cwHistoryCount - 1)
	{
		*cwHistoryAt(cwHistoryPos) = next;
		return true;
	}

	cwHistoryCount = cwHistoryPos + 1;
	if (cwHistoryCount == CW_HISTORY_MAX)
	{
		cwHistoryFirst = (cwHistoryFirst + 1) % CW_HISTORY_MAX;
		--cwHistoryCount;
		--cwHistoryPos;
	}
	*cwHistoryAt(++cwHistoryPos) = next;
	cwHistoryCount = cwHistoryPos + 1;
	cwHistoryMerge = merge;
	return true;
}

static bool cwHistoryStep(bool redo)
{
	if (redo ? cwHistoryPos + 1 >= cwHistoryCount : cwHistoryPos <= 0)
		return false;
	cwHistoryPos += redo ? 1 : -1;
	cwRestoreWorking(cwHistoryAt(cwHistoryPos));
	cwHistoryMerge = -1;
	cwDirty = true;
	return true;
}

static int cwMergeKey(int field)
{
	return field | (customWeaponEditLevel << 6) | (customWeaponEditMode << 10) |
	       (cwBulletIndex() << 11);
}

static void cwFmtDamage(int a, char *b, size_t n)
{
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
	else if (d > 100 && d < 120) snprintf(b, n, "Anim %d fr", d - 100 + 1);
	else if (d >= 255)           snprintf(b, n, "Max");
	else                         snprintf(b, n, "%d", d);
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
	else            snprintf(b, n, "Still");
}

static void cwFmtTrail(int t, char *b, size_t n)
{
	if (t == 255 || t == 0) snprintf(b, n, "None");
	else if (t == 98)       snprintf(b, n, "Smoke");
	else if (t == 198)      snprintf(b, n, "Partial");
	else                    snprintf(b, n, "%d", t);
}

enum { CW_SPARK_GREY = 6 };

static int cwSparkBank(JE_word sg)
{
	return (sg > 1000 && sg < 60000) ? sg / 1000 : 0;
}

static int cwSpriteFrame(JE_word sg)
{
	return (cwSparkBank(sg) > 0) ? sg % 1000 : sg;
}

static bool cwCanSpark(JE_word sg)
{
	const int frame = cwSpriteFrame(sg);
	return frame >= 1 && frame <= 999;
}

static JE_word cwToggleSparks(JE_word sg)
{
	if (cwSparkBank(sg) > 0)
		return (JE_word)cwSpriteFrame(sg);
	if (!cwCanSpark(sg))
		return sg;
	const int bank = sprite2_dominant_bank((sg > 500) ? spriteSheet12 : spriteSheet8,
	                                       (sg > 500) ? sg - 500u : sg);
	return (JE_word)(((bank > 0) ? bank : CW_SPARK_GREY) * 1000 + sg);
}

static void cwFieldValue(int f, char *buf, size_t n)
{
	const JE_WeaponType *w = cwCur();
	const int b = cwBulletIndex();
	switch (f)
	{
	case CWF_LIB_SELECT:
		snprintf(buf, n, "%d of %d", customWeaponCurrentSlot + 1, customWeaponLibCount);
		break;
	case CWF_NAME:
		snprintf(buf, n, "%s", customWeaponName);
		break;
	case CWF_EQUIP_SLOT:
		snprintf(buf, n, "%s",
		         cwSlotNames[cwClamp(customWeaponEquipSlot, 0, CUSTOM_EQUIP_COUNT - 1)]);
		break;
	case CWF_ICON:
		snprintf(buf, n, "%d", customWeaponItemGraphic);
		break;
	case CWF_COST:
		snprintf(buf, n, "%d", customWeaponCost);
		break;
	case CWF_POWER_USE:
		snprintf(buf, n, "%d", customWeaponPowerUse);
		break;
	case CWF_POWER_LEVEL:
		if (cwIsSidekickSlot() && customWeaponChargeStages > 1 &&
		    cwLevelUsed(customWeaponEditLevel))
			snprintf(buf, n, "Stage %d of %d", customWeaponEditLevel + 1, customWeaponChargeStages);
		else if (!cwLevelUsed(customWeaponEditLevel))
			snprintf(buf, n, "%d, unused", customWeaponEditLevel + 1);
		else if (customWeaponEquipSlot == CUSTOM_EQUIP_REAR && customWeaponModes > 1)
			snprintf(buf, n, "%d of %d, Mode %d", customWeaponEditLevel + 1, CUSTOM_POWER_LEVELS,
			         customWeaponEditMode + 1);
		else
			snprintf(buf, n, "%d of %d", customWeaponEditLevel + 1, CUSTOM_POWER_LEVELS);
		break;
	case CWF_TWO_MODES:
		snprintf(buf, n, "%s", customWeaponModes >= 2 ? "Two" : "One");
		break;
	case CWF_FIRE_MODE:
		if (customWeaponModes >= 2)
			snprintf(buf, n, "%d of %d", customWeaponEditMode + 1, customWeaponModes);
		else
			snprintf(buf, n, "Only one");
		break;
	case CWF_FIRE_RATE:
		if (w->shotrepeat == 0)
			snprintf(buf, n, "Every tick");
		else
			snprintf(buf, n, "%d", w->shotrepeat);
		break;
	case CWF_BULLETS:
		snprintf(buf, n, "%d", w->multi);
		break;
	case CWF_PATTERN:
		snprintf(buf, n, "%d", w->max);
		break;
	case CWF_ANIM:
		snprintf(buf, n, "%d", w->weapani + 1);
		break;
	case CWF_HOMING:
		if (w->aim > 5)
			snprintf(buf, n, "On, %d", w->aim);
		else
			snprintf(buf, n, "Off");
		break;
	case CWF_ACCEL_Y:
		snprintf(buf, n, "%+d", w->acceleration);
		break;
	case CWF_ACCEL_X:
		snprintf(buf, n, "%+d", w->accelerationx);
		break;
	case CWF_SPIRAL:
		if (w->circlesize)
			snprintf(buf, n, "%d", w->circlesize);
		else
			snprintf(buf, n, "Off");
		break;
	case CWF_SOUND:
		if (w->sound)
			snprintf(buf, n, "%d", w->sound);
		else
			snprintf(buf, n, "None");
		break;
	case CWF_TRAIL:
		cwFmtTrail(w->trail, buf, n);
		break;
	case CWF_BLAST:
		snprintf(buf, n, "%d", w->shipblastfilter);
		break;
	case CWF_BULLET_SEL:
		snprintf(buf, n, "%d of %d", b + 1, cwClamp(w->multi, 1, CUSTOM_BULLETS_MAX));
		break;
	case CWF_B_SPRITE:
		snprintf(buf, n, "%d", cwSpriteFrame(w->sg[b]));
		break;
	case CWF_B_SPARKS:
		snprintf(buf, n, "%s", (cwSparkBank(w->sg[b]) > 0) ? "On" : "Off");
		break;
	case CWF_B_DAMAGE:
		if (w->attack[b] >= 101 && w->attack[b] <= 249)
			snprintf(buf, n, "Explodes");
		else
			cwFmtDamage(w->attack[b], buf, n);
		break;
	case CWF_B_CHAIN:
		if (w->attack[b] >= 101 && w->attack[b] <= 249)
			snprintf(buf, n, "Weapon %d", w->attack[b] - 100);
		else
			snprintf(buf, n, "Off");
		break;
	case CWF_B_LIFE:
		cwFmtLife(w->del[b], buf, n);
		break;
	case CWF_B_VELX:
		cwFmtVelX(w->sx[b], buf, n);
		break;
	case CWF_B_VELY:
		cwFmtVelY(w->sy[b], buf, n);
		break;
	case CWF_B_OFFX:
		snprintf(buf, n, "%+d", w->bx[b]);
		break;
	case CWF_B_OFFY:
		snprintf(buf, n, "%+d", w->by[b]);
		break;
	case CWF_IMPORT_SRC:
		if (customBulletPresetCount > 0)
			snprintf(buf, n, "%s",
			         customBulletPreset[cwClamp(cwImportSrc, 0, customBulletPresetCount - 1)].name);
		else
			snprintf(buf, n, "None");
		break;
	case CWF_IMPORT_PWR:
	{
		const int mp = customBulletMaxPower(cwImportSrc);
		if (mp > 1)
			snprintf(buf, n, "%d of %d", cwClamp(cwImportPwr, 1, mp), mp);
		else
			snprintf(buf, n, "Only one");
		break;
	}
	case CWF_SK_MOUNT:
		snprintf(buf, n, "%s",
		         cwMountNames[cwClamp(customSidekickMount, 0, CUSTOM_SIDEKICK_MOUNTS - 1)]);
		break;
	case CWF_SK_SPRITE:
		snprintf(buf, n, "%d", customSidekickSprite);
		break;
	case CWF_SK_FRAMES:
		snprintf(buf, n, "%d", customSidekickFrames);
		break;
	case CWF_SK_STEP:
		snprintf(buf, n, "%d", customSidekickFrameStep);
		break;
	case CWF_SK_ANIMATE:
		snprintf(buf, n, "%s", customSidekickAnimate == 1 ? "Always" : "Once per shot");
		break;
	case CWF_CHARGE:
		if (customWeaponChargeStages > 1)
			snprintf(buf, n, "%d stages", customWeaponChargeStages);
		else
			snprintf(buf, n, "Off");
		break;
	case CWF_SHOW_TARGETS:
		if (cwTargetCount > 0)
			snprintf(buf, n, "%d", cwTargetCount);
		else
			snprintf(buf, n, "Off");
		break;
	case CWF_SHOW_STATS:
		snprintf(buf, n, "%s", cwShowStats ? "On" : "Off");
		break;
	case CWF_STARS:
		snprintf(buf, n, "%s", shipEditorStars ? "On" : "Off");
		break;
	case CWF_TARGET_SIZE:
		snprintf(buf, n, "%d", 2 * cwTargetSize + 1);
		break;
	case CWF_TARGET_SPREAD:
		snprintf(buf, n, "%d", cwTargetSpread);
		break;
	case CWF_TARGET_Y:
		snprintf(buf, n, "%d", cwTargetY);
		break;
	case CWF_TARGET_X:
		snprintf(buf, n, "%d", cwTargetX);
		break;
	case CWF_TARGET_ARMOR:
		snprintf(buf, n, "%d", cwTargetArmor);
		break;
	case CWF_SPREAD:
		snprintf(buf, n, "%s", customWeaponSpreadName(customWeaponSpread));
		break;
	case CWF_PREVIEW_SHIP:
		if (weaponEditorShip > 0 && weaponEditorShip <= SHIP_DRAGONWING)
		{
			snprintf(buf, n, "%s", ships[weaponEditorShip].name);
			for (size_t len = strlen(buf); len > 0 && buf[len - 1] == ' '; --len)
				buf[len - 1] = '\0';
		}
		else
		{
			snprintf(buf, n, "Equipped");
		}
		break;
	case CWF_SHIP_X:
		snprintf(buf, n, "%d", cwShipX);
		break;
	case CWF_SHIP_Y:
		snprintf(buf, n, "%d", cwShipY);
		break;
	default:
		buf[0] = '\0';
		break;
	}
}

static bool cwFieldActive(int f)
{
	switch (f)
	{
	case CWF_TWO_MODES:
		return customWeaponEquipSlot == CUSTOM_EQUIP_REAR;
	case CWF_FIRE_MODE:
		return customWeaponEquipSlot == CUSTOM_EQUIP_REAR && customWeaponModes >= 2;
	case CWF_SK_MOUNT:
	case CWF_SK_SPRITE:
	case CWF_SK_FRAMES:
	case CWF_SK_STEP:
	case CWF_SK_ANIMATE:
	case CWF_CHARGE:
		return cwIsSidekickSlot();
	case CWF_IMPORT_SRC:
		return customBulletPresetCount > 0;
	case CWF_IMPORT_PWR:
		return customBulletMaxPower(cwImportSrc) > 1;
	case CWF_B_SPARKS:
		return cwCanSpark(cwCur()->sg[cwBulletIndex()]);
	default:
		return true;
	}
}

static const char *cwFieldInactiveReason(int f)
{
	switch (f)
	{
	case CWF_TWO_MODES:
		return "Only a rear gun has fire modes. Change Equip To.";
	case CWF_FIRE_MODE:
		return customWeaponEquipSlot == CUSTOM_EQUIP_REAR
		           ? "Set Fire Modes to Two first."
		           : "Only a rear gun has fire modes. Change Equip To.";
	case CWF_IMPORT_SRC:
		return "No stock weapons are loaded.";
	case CWF_IMPORT_PWR:
		return "This source has a single level.";
	case CWF_B_SPARKS:
		return "Only bullet sprites 1 to 999 can trail sparks.";
	default:
		return "Only used on a sidekick. Change Equip To.";
	}
}

// Numeric entry for rows whose range is too wide for step controls. A negative lower bound marks
// a signed field.
static bool cwNumericRange(int f, int *lo, int *hi)
{
	switch (f)
	{
	case CWF_COST:          *lo = 0;    *hi = 64000;              return true;
	case CWF_ICON:          *lo = 1;    *hi = 237;                return true;
	case CWF_POWER_USE:     *lo = 0;    *hi = 255;                return true;
	case CWF_SK_SPRITE:
	{
		const int cnt = customSidekickSpriteCount(customSidekickMount);
		*lo = 1;
		*hi = (cnt > 0) ? cnt : 65535;
		return true;
	}
	case CWF_SK_FRAMES:     *lo = 1;    *hi = 20;                 return true;
	case CWF_SK_STEP:       *lo = 0;    *hi = 40;                 return true;
	case CWF_CHARGE:        *lo = 1;    *hi = CUSTOM_POWER_LEVELS; return true;
	case CWF_FIRE_RATE:     *lo = 0;    *hi = 255;                return true;
	case CWF_BULLETS:       *lo = 1;    *hi = CUSTOM_BULLETS_MAX; return true;
	case CWF_PATTERN:       *lo = 1;    *hi = CUSTOM_BULLETS_MAX; return true;
	case CWF_ANIM:          *lo = 1;    *hi = 256;                return true;
	case CWF_HOMING:        *lo = 0;    *hi = 255;                return true;
	case CWF_ACCEL_Y:       *lo = -128; *hi = 127;                return true;
	case CWF_ACCEL_X:       *lo = -128; *hi = 127;                return true;
	case CWF_SPIRAL:        *lo = 0;    *hi = 255;                return true;
	case CWF_SOUND:         *lo = 0;    *hi = CUSTOM_SOUND_MAX;   return true;
	case CWF_TRAIL:         *lo = 0;    *hi = 255;                return true;
	case CWF_BLAST:         *lo = 0;    *hi = 255;                return true;
	case CWF_B_SPRITE:
		*lo = (cwSparkBank(cwCur()->sg[cwBulletIndex()]) > 0) ? 1 : 0;
		*hi = (*lo > 0) ? 999 : 65535;
		return true;
	case CWF_B_DAMAGE:      *lo = 0;    *hi = 99;                 return true;
	case CWF_B_CHAIN:       *lo = 0;    *hi = 149;                return true;
	case CWF_B_LIFE:        *lo = 0;    *hi = 255;                return true;
	case CWF_B_VELX:        *lo = -128; *hi = 127;                return true;
	case CWF_B_VELY:        *lo = -128; *hi = 127;                return true;
	case CWF_B_OFFX:        *lo = -128; *hi = 127;                return true;
	case CWF_B_OFFY:        *lo = -128; *hi = 127;                return true;
	case CWF_SHOW_TARGETS:  *lo = 0;    *hi = CW_DUMMY_MAX;       return true;
	case CWF_TARGET_SPREAD: *lo = 6;    *hi = 60;                 return true;
	case CWF_TARGET_SIZE:   *lo = 2 * CW_TARGET_SIZE_MIN + 1; *hi = 2 * CW_TARGET_SIZE_MAX + 1; return true;
	case CWF_TARGET_Y:      *lo = cwTargetYMin(); *hi = cwTargetYMax(); return true;
	case CWF_TARGET_X:      *lo = CW_TARGET_X_MIN; *hi = CW_TARGET_X_MAX; return true;
	case CWF_TARGET_ARMOR:  *lo = 20;   *hi = 600;                return true;
	case CWF_SHIP_X:        *lo = CW_SHIP_X_MIN;   *hi = CW_SHIP_X_MAX;   return true;
	case CWF_SHIP_Y:        *lo = CW_SHIP_Y_MIN;   *hi = CW_SHIP_Y_MAX;   return true;
	default:                                                      return false;
	}
}

static void cwSelectLevel(int level)
{
	level = cwClamp(level, 0, CUSTOM_POWER_LEVELS - 1);
	if (level == customWeaponEditLevel)
		return;
	customWeaponSelectLevel(level);
	cwHistoryMerge = -1;
	cwClearPreviewShots();
}

static void cwStepBullet(int dir)
{
	const int count = cwClamp(cwCur()->multi, 1, CUSTOM_BULLETS_MAX);
	cwBulletSel = (cwBulletIndex() + count + dir) % count;
	cwHistoryMerge = -1;
}

static void cwFieldAdjust(int f, int dir)
{
	JE_WeaponType *w = cwCur();
	const int b = cwBulletIndex();
	const int one = (dir >= 0) ? 1 : -1;
	switch (f)
	{
	case CWF_LIB_SELECT:
		if (customWeaponLibCount > 1)
		{
			customWeaponSelectSlot((customWeaponCurrentSlot + customWeaponLibCount + one) %
			                       customWeaponLibCount);
			cwHistoryReset();
			cwBulletSel = 0;
			cwClearPreviewShots();
		}
		return;
	case CWF_POWER_LEVEL:
		cwSelectLevel(customWeaponEditLevel + one);
		return;
	case CWF_FIRE_MODE:
		customWeaponSelectMode(cwClamp(customWeaponEditMode + one, 0, customWeaponModes - 1));
		cwHistoryMerge = -1;
		cwClearPreviewShots();
		return;
	case CWF_BULLET_SEL:
		cwBulletSel = cwClamp(b + dir, 0, cwClamp(w->multi, 1, CUSTOM_BULLETS_MAX) - 1);
		cwHistoryMerge = -1;
		return;
	case CWF_IMPORT_SRC:
		if (customBulletPresetCount > 0)
		{
			cwImportSrc =
			    ((cwImportSrc + dir) % customBulletPresetCount + customBulletPresetCount) %
			    customBulletPresetCount;
			cwImportPwr = customBulletMaxPower(cwImportSrc);
		}
		return;
	case CWF_IMPORT_PWR:
		cwImportPwr = cwClamp(cwImportPwr + dir, 1, customBulletMaxPower(cwImportSrc));
		return;
	case CWF_SHOW_TARGETS:
		cwTargetCount = cwClamp(cwTargetCount + dir, 0, CW_DUMMY_MAX);
		cwLayoutDummies();
		return;
	case CWF_SHOW_STATS:
		cwShowStats = !cwShowStats;
		return;
	case CWF_STARS:
		shipEditorStars = !shipEditorStars;
		return;
	case CWF_TARGET_SIZE:
		cwTargetSize = cwClamp(cwTargetSize + dir, CW_TARGET_SIZE_MIN, CW_TARGET_SIZE_MAX);
		cwTargetY = cwClamp(cwTargetY, cwTargetYMin(), cwTargetYMax());
		cwPlaceDummies();
		return;
	case CWF_TARGET_SPREAD:
		cwTargetSpread = cwClamp(cwTargetSpread + dir * 4, 6, 60);
		cwPlaceDummies();
		return;
	case CWF_TARGET_Y:
		cwTargetY = cwClamp(cwTargetY + dir * 4, cwTargetYMin(), cwTargetYMax());
		cwPlaceDummies();
		return;
	case CWF_TARGET_X:
		cwTargetX = cwClamp(cwTargetX + dir * 4, CW_TARGET_X_MIN, CW_TARGET_X_MAX);
		cwPlaceDummies();
		return;
	case CWF_TARGET_ARMOR:
		cwTargetArmor = cwClamp(cwTargetArmor + dir * 20, 20, 600);
		cwLayoutDummies();
		return;
	case CWF_PREVIEW_SHIP:
		weaponEditorShip = (weaponEditorShip + SHIP_DRAGONWING + 1 + one) % (SHIP_DRAGONWING + 1);
		return;
	case CWF_SPREAD:
		customWeaponSpread = (customWeaponSpread + CUSTOM_SPREAD_COUNT + one) % CUSTOM_SPREAD_COUNT;
		return;
	case CWF_SHIP_X:
		cwShipX = cwClamp(cwShipX + dir * 2, CW_SHIP_X_MIN, CW_SHIP_X_MAX);
		return;
	case CWF_SHIP_Y:
		cwShipY = cwClamp(cwShipY + dir * 2, CW_SHIP_Y_MIN, CW_SHIP_Y_MAX);
		return;

	case CWF_TWO_MODES:
		customWeaponModes = cwClamp(customWeaponModes + one, 1, CUSTOM_WEAPON_MODES);
		if (customWeaponModes < 2)
			customWeaponEditMode = 0;
		break;
	case CWF_EQUIP_SLOT:
		customWeaponEquipSlot =
		    (customWeaponEquipSlot + CUSTOM_EQUIP_COUNT + one) % CUSTOM_EQUIP_COUNT;
		if (customWeaponEquipSlot != CUSTOM_EQUIP_REAR)
			customWeaponEditMode = 0;
		break;
	case CWF_COST:
		customWeaponCost = cwClamp(customWeaponCost + dir * 500, 0, 64000);
		break;
	case CWF_ICON:
		customWeaponItemGraphic = cwClamp(customWeaponItemGraphic + dir, 1, 237);
		break;
	case CWF_POWER_USE:
		customWeaponPowerUse = cwClamp(customWeaponPowerUse + dir, 0, 255);
		break;
	case CWF_CHARGE:
		for (int i = 0; i < abs(dir); ++i)
			if ((dir > 0 ? customWeaponAddChargeState() : customWeaponRemoveChargeState()) < 0)
				break;
		break;
	case CWF_SK_MOUNT:
	{
		customSidekickMount =
		    (customSidekickMount + CUSTOM_SIDEKICK_MOUNTS + one) % CUSTOM_SIDEKICK_MOUNTS;
		const int cnt = customSidekickSpriteCount(customSidekickMount);
		if (cnt > 0)
			customSidekickSprite = cwClamp(customSidekickSprite, 1, cnt);
		break;
	}
	case CWF_SK_SPRITE:
	{
		const int cnt = customSidekickSpriteCount(customSidekickMount);
		customSidekickSprite = cwClamp(customSidekickSprite + dir, 1, (cnt > 0) ? cnt : 65535);
		break;
	}
	case CWF_SK_FRAMES:
		customSidekickFrames = cwClamp(customSidekickFrames + dir, 1, 20);
		break;
	case CWF_SK_STEP:
		customSidekickFrameStep = cwClamp(customSidekickFrameStep + dir, 0, 40);
		break;
	case CWF_SK_ANIMATE:
		customSidekickAnimate = (customSidekickAnimate == 1) ? 2 : 1;
		break;

	// Ranges span each field's full valid range so any imported value stays reachable.
	case CWF_FIRE_RATE:
		w->shotrepeat = (JE_byte)cwClamp(w->shotrepeat + dir, 0, 255);
		break;
	case CWF_BULLETS:
		w->multi = (JE_byte)cwClamp(w->multi + dir, 1, CUSTOM_BULLETS_MAX);
		if (w->max < w->multi)
			w->max = w->multi;
		break;
	case CWF_PATTERN:
		w->max = (JE_byte)cwClamp(w->max + dir, 1, CUSTOM_BULLETS_MAX);
		break;
	case CWF_ANIM:
		w->weapani = (JE_word)cwClamp((int)w->weapani + dir, 0, 255);
		break;
	case CWF_HOMING:
		w->aim = (JE_byte)cwClamp(w->aim + dir, 0, 255);
		break;
	case CWF_ACCEL_Y:
		w->acceleration = (JE_shortint)cwClamp(w->acceleration + dir, -128, 127);
		break;
	case CWF_ACCEL_X:
		w->accelerationx = (JE_shortint)cwClamp(w->accelerationx + dir, -128, 127);
		break;
	case CWF_SPIRAL:
		w->circlesize = (JE_byte)cwClamp(w->circlesize + dir, 0, 255);
		break;
	case CWF_SOUND:
		w->sound = (JE_byte)cwClamp(w->sound + dir, 0, CUSTOM_SOUND_MAX);
		break;
	case CWF_TRAIL:
		w->trail = (JE_byte)cwClamp(w->trail + dir, 0, 255);
		break;
	case CWF_BLAST:
		w->shipblastfilter = (JE_byte)cwClamp(w->shipblastfilter + dir, 0, 255);
		break;
	case CWF_B_SPRITE:
	{
		const int bank = cwSparkBank(w->sg[b]);
		w->sg[b] = (bank > 0)
		         ? (JE_word)(bank * 1000 + cwClamp(cwSpriteFrame(w->sg[b]) + dir, 1, 999))
		         : (JE_word)cwClamp((int)w->sg[b] + dir, 0, 65535);
		break;
	}
	case CWF_B_SPARKS:
		w->sg[b] = cwToggleSparks(w->sg[b]);
		break;
	case CWF_B_DAMAGE:
	{
		const int dmg = (w->attack[b] <= 99) ? w->attack[b] : 10;
		w->attack[b] = (JE_byte)cwClamp(dmg + dir, 0, 99);
		break;
	}
	case CWF_B_CHAIN:
	{
		const int tgt = (w->attack[b] >= 101 && w->attack[b] <= 249) ? w->attack[b] - 100 : 0;
		const int nt = cwClamp(tgt + dir, 0, 149);
		w->attack[b] = (JE_byte)(nt == 0 ? 10 : 100 + nt);
		break;
	}
	case CWF_B_LIFE:
		w->del[b] = (JE_byte)cwClamp(w->del[b] + dir, 0, 255);
		break;
	case CWF_B_VELX:
		w->sx[b] = (JE_shortint)cwClamp(w->sx[b] + dir, -128, 127);
		break;
	case CWF_B_VELY:
		w->sy[b] = (JE_shortint)cwClamp(w->sy[b] + dir, -128, 127);
		break;
	case CWF_B_OFFX:
		w->bx[b] = (JE_shortint)cwClamp(w->bx[b] + dir, -128, 127);
		break;
	case CWF_B_OFFY:
		w->by[b] = (JE_shortint)cwClamp(w->by[b] + dir, -128, 127);
		break;
	default:
		return;
	}
	customWeaponMaterialize();
	if (cwHistoryRecord(cwMergeKey(f)))
		cwClearPreviewShots();
	if (f == CWF_SOUND && cwCur()->sound > 0)
		JE_playSampleNum(cwCur()->sound);
}

static void cwSetNumeric(int f, int v)
{
	int lo, hi;
	if (!cwNumericRange(f, &lo, &hi))
		return;
	v = cwClamp(v, lo, hi);

	JE_WeaponType *w = cwCur();
	const int b = cwBulletIndex();
	switch (f)
	{
	case CWF_COST:          customWeaponCost         = v; break;
	case CWF_ICON:          customWeaponItemGraphic  = v; break;
	case CWF_POWER_USE:     customWeaponPowerUse     = v; break;
	case CWF_SK_SPRITE:     customSidekickSprite     = v; break;
	case CWF_SK_FRAMES:     customSidekickFrames     = v; break;
	case CWF_SK_STEP:       customSidekickFrameStep  = v; break;
	case CWF_CHARGE:        customWeaponChargeStages = v; break;
	case CWF_FIRE_RATE:     w->shotrepeat = (JE_byte)v; break;
	case CWF_BULLETS:
		w->multi = (JE_byte)v;
		if (w->max < w->multi)
			w->max = w->multi;
		break;
	case CWF_PATTERN:       w->max             = (JE_byte)v;       break;
	case CWF_ANIM:          w->weapani         = (JE_word)(v - 1); break;
	case CWF_HOMING:        w->aim             = (JE_byte)v;       break;
	case CWF_ACCEL_Y:       w->acceleration    = (JE_shortint)v;   break;
	case CWF_ACCEL_X:       w->accelerationx   = (JE_shortint)v;   break;
	case CWF_SPIRAL:        w->circlesize      = (JE_byte)v;       break;
	case CWF_SOUND:         w->sound           = (JE_byte)v;       break;
	case CWF_TRAIL:         w->trail           = (JE_byte)v;       break;
	case CWF_BLAST:         w->shipblastfilter = (JE_byte)v;       break;
	case CWF_B_SPRITE:      w->sg[b]           = (JE_word)(cwSparkBank(w->sg[b]) * 1000 + v); break;
	case CWF_B_DAMAGE:      w->attack[b]       = (JE_byte)v;       break;
	case CWF_B_CHAIN:       w->attack[b]       = (JE_byte)(v == 0 ? 10 : 100 + v); break;
	case CWF_B_LIFE:        w->del[b]          = (JE_byte)v;       break;
	case CWF_B_VELX:        w->sx[b]           = (JE_shortint)v;   break;
	case CWF_B_VELY:        w->sy[b]           = (JE_shortint)v;   break;
	case CWF_B_OFFX:        w->bx[b]           = (JE_shortint)v;   break;
	case CWF_B_OFFY:        w->by[b]           = (JE_shortint)v;   break;
	case CWF_SHOW_TARGETS:  cwTargetCount  = v; cwLayoutDummies(); return;
	case CWF_TARGET_SIZE:
		cwTargetSize = cwClamp((v - 1) / 2, CW_TARGET_SIZE_MIN, CW_TARGET_SIZE_MAX);
		cwTargetY = cwClamp(cwTargetY, cwTargetYMin(), cwTargetYMax());
		cwPlaceDummies();
		return;
	case CWF_TARGET_SPREAD: cwTargetSpread = v; cwPlaceDummies();  return;
	case CWF_TARGET_Y:      cwTargetY      = v; cwPlaceDummies();  return;
	case CWF_TARGET_X:      cwTargetX      = v; cwPlaceDummies();  return;
	case CWF_TARGET_ARMOR:  cwTargetArmor  = v; cwLayoutDummies(); return;
	case CWF_SHIP_X:        cwShipX = v; return;
	case CWF_SHIP_Y:        cwShipY = v; return;
	default:                return;
	}
	customWeaponMaterialize();
}

// Finalize any in-progress typed value to its field and leave typing mode. An empty
// buffer (or a lone '-') is discarded, leaving the field unchanged.
static void cwCommitNumeric(void)
{
	if (cwNumField < 0)
		return;
	if (cwNumText[0] != '\0' && !(cwNumText[0] == '-' && cwNumText[1] == '\0'))
	{
		cwSetNumeric(cwNumField, SDL_atoi(cwNumText));
		if (cwHistoryRecord(-1))
			cwClearPreviewShots();
	}
	cwNumField = -1;
	cwNumText[0] = '\0';
}

static bool cwActionAvailable(int a)
{
	switch (a)
	{
	case CWA_UNDO:          return cwHistoryPos > 0;
	case CWA_REDO:          return cwHistoryPos + 1 < cwHistoryCount;
	case CWA_EQUIP:         return cwCanEquip;
	case CWA_LIB_NEW:
	case CWA_LIB_DUPLICATE: return customWeaponLibCount < CUSTOM_WEAPON_LIB_MAX;
	case CWA_LIB_DELETE:    return customWeaponLibCount > 1;
	case CWA_REVERT:        return cwDirty && cwRevertLen > 0;
	case CWA_ADD_BULLET:    return cwCur()->multi < CUSTOM_BULLETS_MAX;
	case CWA_REMOVE_BULLET: return cwCur()->multi > 1;
	case CWA_PASTE_LEVEL:   return cwLevelClipboardSet;
	case CWA_IMPORT_LEVEL:
	case CWA_IMPORT_ALL:
	case CWA_ADD_LEVEL:
	case CWA_ADD_ALL_LEVELS: return customBulletPresetCount > 0;
	default:                return true;
	}
}

static const char *cwActionUnavailableNotice(int a)
{
	switch (a)
	{
	case CWA_UNDO:          return "Nothing to undo";
	case CWA_REDO:          return "Nothing to redo";
	case CWA_LIB_NEW:
	case CWA_LIB_DUPLICATE: return "The library is full";
	case CWA_LIB_DELETE:    return "The library keeps at least one weapon";
	case CWA_REVERT:        return "Nothing to revert";
	case CWA_ADD_BULLET:    return "No room for another bullet";
	case CWA_REMOVE_BULLET: return "A level keeps at least one bullet";
	case CWA_PASTE_LEVEL:   return "Copy a level first";
	default:                return NULL;
	}
}

static bool cwActionNeedsConfirm(int a)
{
	return a == CWA_LIB_DELETE || a == CWA_REVERT || a == CWA_RESET_ALL;
}

static void cwPerformAction(int a, bool *done, bool *equipped)
{
	if (!cwActionAvailable(a))
	{
		const char *const why = cwActionUnavailableNotice(a);
		if (why != NULL)
			cwSetNotice(why);
		cwArmed = -1;
		JE_playSampleNum(S_SPRING);
		return;
	}

	if (cwActionNeedsConfirm(a) && cwArmed != a)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "Choose %s again to confirm", cwActs[a].label);
		cwSetNotice(buf);
		cwArmed = a;
		JE_playSampleNum(S_CLICK);
		return;
	}
	cwArmed = -1;

	bool changed = false;
	bool resetHistory = false;
	switch (a)
	{
	case CWA_UNDO:
	case CWA_REDO:
		cwHistoryStep(a == CWA_REDO);
		cwSetNotice(a == CWA_REDO ? "Redo" : "Undo");
		break;
	case CWA_SAVE:
		customWeaponLibrarySave();
		save_opentyrian_config();
		cwSetNotice("Library saved");
		break;
	case CWA_EQUIP:
		if (!customWeaponEquip())
		{
			cwSetNotice("No free slot to equip it in");
			JE_playSampleNum(S_SPRING);
			return;
		}
		*equipped = true;
		*done = true;
		break;
	case CWA_DONE:
		*done = true;
		break;
	case CWA_LIB_NEW:
		customWeaponLibraryNew();
		cwSetNotice("New weapon added");
		resetHistory = true;
		break;
	case CWA_LIB_DUPLICATE:
		customWeaponLibraryDuplicate();
		cwSetNotice("Weapon duplicated");
		resetHistory = true;
		break;
	case CWA_LIB_DELETE:
		customWeaponLibraryDelete();
		cwSetNotice("Weapon deleted");
		resetHistory = true;
		break;
	case CWA_REVERT:
		if (!customWeaponRestoreLibrary(cwRevertBuf, cwRevertLen))
		{
			JE_playSampleNum(S_SPRING);
			return;
		}
		cwSetNotice("All changes reverted");
		resetHistory = true;
		break;
	case CWA_ADD_BULLET:
	{
		const int ni = customWeaponAddBullet(cwBulletIndex());
		if (ni >= 0)
			cwBulletSel = ni;
		customWeaponMaterialize();
		changed = true;
		break;
	}
	case CWA_REMOVE_BULLET:
	{
		const int ni = customWeaponRemoveBullet(cwBulletIndex());
		if (ni >= 0)
			cwBulletSel = ni;
		customWeaponMaterialize();
		changed = true;
		break;
	}
	case CWA_AUTOSCALE:
	{
		customWeaponAutoScaleLevels();
		customWeaponMaterialize();
		char notice[48];
		if (customWeaponLastSpread == CUSTOM_SPREAD_STOCK)
			snprintf(notice, sizeof(notice), "Rebuilt from the stock weapon");
		else
			snprintf(notice, sizeof(notice), "Rebuilt with the %s spread",
			         customWeaponSpreadName(customWeaponLastSpread));
		cwSetNotice(notice);
		changed = true;
		break;
	}
	case CWA_COPY_ALL:
		customWeaponCopyToAllLevels();
		customWeaponMaterialize();
		cwSetNotice("Copied to every level");
		changed = true;
		break;
	case CWA_COPY_LEVEL:
		cwLevelClipboard = *cwCur();
		cwLevelClipboardSet = true;
		cwSetNotice("Level copied");
		break;
	case CWA_PASTE_LEVEL:
		*cwCur() = cwLevelClipboard;
		customWeaponMaterialize();
		cwSetNotice("Level pasted");
		changed = true;
		break;
	case CWA_RANDOMIZE:
		customWeaponRandomize();
		customWeaponMaterialize();
		changed = true;
		break;
	case CWA_RESET:
		customWeaponReset();
		customWeaponMaterialize();
		cwSetNotice("Level reset");
		changed = true;
		break;
	case CWA_RESET_ALL:
		customWeaponResetAllLevels();
		customWeaponMaterialize();
		cwSetNotice("Weapon reset");
		changed = true;
		break;
	case CWA_IMPORT_LEVEL:
		customWeaponImportLevel(cwImportSrc, cwImportPwr);
		customWeaponMaterialize();
		cwSetNotice("Level replaced from the source");
		changed = true;
		break;
	case CWA_IMPORT_ALL:
		customWeaponImportAllLevels(cwImportSrc);
		customWeaponMaterialize();
		cwSetNotice("Weapon replaced from the source");
		changed = true;
		break;
	case CWA_ADD_LEVEL:
		customWeaponAddLevel(cwImportSrc, cwImportPwr);
		customWeaponMaterialize();
		cwSetNotice("Source bullets added to this level");
		changed = true;
		break;
	case CWA_ADD_ALL_LEVELS:
		customWeaponAddAllLevels(cwImportSrc);
		customWeaponMaterialize();
		cwSetNotice("Source bullets added to every level");
		changed = true;
		break;
	case CWA_RANGE_RESET:
		cwShowStats = true;
		cwTargetCount = 3;
		cwTargetSize = 8;
		cwTargetSpread = 32;
		cwTargetY = 36;
		cwTargetX = 76;
		cwTargetArmor = 120;
		cwShipX = 70;
		cwShipY = 116;
		weaponEditorShip = SHIP_GENCORE_II;
		cwLayoutDummies();
		cwSetNotice("Range reset");
		break;
	}
	JE_playSampleNum(S_SELECT);

	if (resetHistory)
	{
		cwHistoryReset();
		cwBulletSel = 0;
		cwClearPreviewShots();
		cwDirty = (a != CWA_REVERT);
	}
	else if (changed)
	{
		cwHistoryMerge = -1;
		if (cwHistoryRecord(-1))
			cwClearPreviewShots();
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
		return false;
	if (af > 1000)
		af %= 1000;
	Sprite2_array s = (af > 500) ? spriteSheet12 : spriteSheet8;
	int idx = (af > 500) ? (af - 500) : af;
	if (idx < 1 || idx > cwSprite2Count(s))
		return false;
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
		x += *data & 0x0f;
		const int count = (*data & 0xf0) >> 4;
		if (count == 0)
		{
			++row;
			x = 0;
			continue;
		}
		if (x < minx)
			minx = x;
		data += count;
		x += count;
		if (x - 1 > maxx)
			maxx = x - 1;
		if (row < miny)
			miny = row;
		if (row > maxy)
			maxy = row;
	}
	if (maxx < minx || maxy < miny)
		return false;
	*ox = minx;
	*oy = miny;
	*ow = maxx - minx + 1;
	*oh = maxy - miny + 1;
	return true;
}

static bool cwPreviewingSource(void)
{
	if (cwPage != CWP_IMPORT || customBulletPresetCount <= 0 || cwSelRow >= cwNavCount)
		return false;
	const CwNav *const n = &cwNav[cwSelRow];
	return n->kind == CWN_FIELD && (n->id[0] == CWF_IMPORT_SRC || n->id[0] == CWF_IMPORT_PWR);
}

static const CustomBulletPreset *cwSource(void)
{
	if (customBulletPresetCount <= 0)
		return NULL;
	return &customBulletPreset[cwClamp(cwImportSrc, 0, customBulletPresetCount - 1)];
}

static int cwSlotDamage(const JE_WeaponType *w, int slots, bool *ice, int depth)
{
	int dmg = 0;
	for (int i = 0; i < slots; ++i)
	{
		const int a = w->attack[i];
		if (a == 99)
		{
			*ice = true;
		}
		else if (a >= 101 && a <= 249)
		{
			dmg += 1;
			if (depth > 0 && a - 100 <= WEAP_NUM)
				dmg += cwSlotDamage(&weapons[a - 100],
				                    cwClamp(weapons[a - 100].multi, 1, CUSTOM_BULLETS_MAX), ice,
				                    depth - 1);
		}
		else
		{
			dmg += (a >= 250) ? a - 250 : a;
		}
	}
	return dmg;
}

static int cwVolleyDamage(const JE_WeaponType *w, bool *ice, int depth)
{
	return cwSlotDamage(w, cwClamp(w->multi, 1, CUSTOM_BULLETS_MAX), ice, depth);
}

static int cwDps(const JE_WeaponType *w)
{
	bool ice = false;
	const int multi = cwClamp(w->multi, 1, CUSTOM_BULLETS_MAX);
	const int slots = MAX(multi, cwClamp(w->max, 1, CUSTOM_BULLETS_MAX));
	return cwSlotDamage(w, slots, &ice, 2) * multi * 35 / (slots * (w->shotrepeat + 1));
}

static void cwDrawClamp(int x0, int y0, int x1, int y1, int arm, Uint8 c)
{
	fill_rectangle_xy(VGAScreen, x0, y0, x0 + arm, y0, c);
	fill_rectangle_xy(VGAScreen, x1 - arm, y0, x1, y0, c);
	fill_rectangle_xy(VGAScreen, x0, y1, x0 + arm, y1, c);
	fill_rectangle_xy(VGAScreen, x1 - arm, y1, x1, y1, c);
	fill_rectangle_xy(VGAScreen, x0, y0, x0, y0 + arm, c);
	fill_rectangle_xy(VGAScreen, x1, y0, x1, y0 + arm, c);
	fill_rectangle_xy(VGAScreen, x0, y1 - arm, x0, y1, c);
	fill_rectangle_xy(VGAScreen, x1, y1 - arm, x1, y1, c);
}

static void cwDrawDummy(const CwDummy *t)
{
	const int x0 = t->cx - cwTargetSize, y0 = t->cy - cwTargetSize;
	const int x1 = t->cx + cwTargetSize, y1 = t->cy + cwTargetSize;

	if (t->dead > 0)
	{
		if ((t->dead / 2) % 2 == 0)
			cwDrawClamp(x0 - 2, y0 - 2, x1 + 2, y1 + 2, 3, CW_BG_SELECT);
		return;
	}

	Uint8 body = CW_BG_WELL, rim = CW_BG_SOCKET, mark = CW_BG_SELECT;
	if (t->ice > 0)
	{
		body = 0x93;
		rim = 0x96;
		mark = 0x9b;
	}
	if (t->hit > 0)
	{
		body = 0x64;
		mark = 0x5e;
	}

	fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, body);
	JE_rectangle(VGAScreen, x0, y0, x1, y1, rim);
	cwDrawClamp(x0 - 2, y0 - 2, x1 + 2, y1 + 2, 3, CW_BG_CLAMP);
	fill_rectangle_xy(VGAScreen, x0 + 3, t->cy, t->cx - 2, t->cy, mark);
	fill_rectangle_xy(VGAScreen, t->cx + 2, t->cy, x1 - 3, t->cy, mark);
	fill_rectangle_xy(VGAScreen, t->cx, y0 + 3, t->cx, t->cy - 2, mark);
	fill_rectangle_xy(VGAScreen, t->cx, t->cy + 2, t->cx, y1 - 3, mark);
	JE_pix(VGAScreen, t->cx, t->cy, mark);
}

static void cwDrawDummyBars(void)
{
	for (int d = 0; d < cwDummyCount; ++d)
	{
		const CwDummy *t = &cwDummies[d];
		if (t->dead > 0 || t->armor >= cwTargetArmor || cwTargetArmor <= 0)
			continue;
		draw_enemy_hp_bar_box(RL_ID_ENEMYBAR_BASE + d, t->cx - cwTargetSize, t->cx + cwTargetSize,
		                      t->cy - cwTargetSize, t->cy + cwTargetSize + 1,
		                      (float)t->armor / (float)cwTargetArmor);
	}
}

static void cwTickDummies(void)
{
	enum { CW_DUMMY_REST_TICKS = 40 };
	for (int d = 0; d < cwDummyCount; ++d)
	{
		CwDummy *t = &cwDummies[d];
		if (t->idle < CW_DUMMY_REST_TICKS)
			++t->idle;
		else if (t->armor < cwTargetArmor)
			t->armor = MIN(cwTargetArmor, t->armor + MAX(1, cwTargetArmor / 12));
		if (t->hit > 0)
			--t->hit;
		if (t->ice > 0)
			--t->ice;
		if (t->dead > 0)
			--t->dead;
	}
}

// Preview Homing by nudging live shots toward the nearest dummy on X. The shop simulator has no
// target tracking, so this mirrors the gameplay correction here.
static void cwHomeShots(void)
{
	const JE_WeaponType *w = cwCur();
	if (cwPreviewingSource())
	{
		const int wn = customBulletPresetWeapon(cwImportSrc, customWeaponEditMode, cwImportPwr);
		if (wn <= 0)
			return;
		w = &weapons[wn];
	}
	if (w->aim <= 5 || cwDummyCount == 0)
		return;

	for (int z = 0; z < MAX_PWEAPON - 1; ++z)
	{
		if (shotAvail[z] == 0)
			continue;
		PlayerShotDataType *s = &playerShotData[z];
		if (s->shotXM > 100 || s->shotXM < -100)
			continue;
		const int scx = s->shotX + 6, scy = s->shotY + 6;
		int bi = -1, bd = INT_MAX;
		for (int d = 0; d < cwDummyCount; ++d)
		{
			const int dx = cwDummies[d].cx - scx, dy = cwDummies[d].cy - scy;
			const int dist = dx * dx + dy * dy;
			if (dist < bd)
			{
				bd = dist;
				bi = d;
			}
		}
		if (bi < 0)
			continue;
		if (scx < cwDummies[bi].cx && s->shotXM < 20)
			++s->shotXM;
		else if (scx > cwDummies[bi].cx && s->shotXM > -20)
			--s->shotXM;
	}
}

static void cwShotNextPos(const PlayerShotDataType *s, int *nx, int *ny)
{
	const int xm = s->shotXM + s->shotXC;
	const int ym = s->shotYM + s->shotYC;
	int x = s->shotX + ((xm <= 100) ? xm : 0);
	int y = s->shotY + ym;
	if (ym > 100)
		y += player[0].delta_y_shot_move - 120;
	if (s->shotComplicated != 0)
	{
		x += s->shotDevX + s->shotDirX;
		y += s->shotDevY + s->shotDirY;
	}
	*nx = x;
	*ny = y;
}

static void cwShotBox(JE_word sg, int x, int y, int *x0, int *y0, int *x1, int *y1)
{
	int ox, oy, ow, oh;
	if (!cwSpriteExtent(sg, &ox, &oy, &ow, &oh))
	{
		ox = 3;
		oy = 3;
		ow = 6;
		oh = 8;
	}
	*x0 = x + 1 + ox;
	*y0 = y + oy;
	*x1 = *x0 + ow - 1;
	*y1 = *y0 + oh - 1;
}

static void cwCollideShots(void)
{
	if (cwDummyCount == 0)
		return;

	int chainSpawns = 0;
	for (int z = 0; z < MAX_PWEAPON - 1; ++z)
	{
		if (shotAvail[z] == 0)
			continue;
		PlayerShotDataType *s = &playerShotData[z];

		int nx, ny, ax0, ay0, ax1, ay1, bx0, by0, bx1, by1;
		cwShotNextPos(s, &nx, &ny);
		cwShotBox(s->shotGr, s->shotX, s->shotY, &ax0, &ay0, &ax1, &ay1);
		cwShotBox(s->shotGr, nx, ny, &bx0, &by0, &bx1, &by1);
		const int sx0 = MIN(ax0, bx0), sy0 = MIN(ay0, by0);
		const int sx1 = MAX(ax1, bx1), sy1 = MAX(ay1, by1);

		for (int d = 0; d < cwDummyCount; ++d)
		{
			CwDummy *const t = &cwDummies[d];
			if (t->dead > 0)
				continue;
			const int tx0 = t->cx - cwTargetSize, ty0 = t->cy - cwTargetSize;
			const int tx1 = t->cx + cwTargetSize, ty1 = t->cy + cwTargetSize;
			if (sx1 < tx0 || sx0 > tx1 || sy1 < ty0 || sy0 > ty1)
				continue;

			const int hitX = (MAX(bx0, tx0) + MIN(bx1, tx1)) / 2 - 6;
			const int hitY = (MAX(by0, ty0) + MIN(by1, ty1)) / 2 - 7;
			if (s->chainReaction > 0 && s->chainReaction <= 149)
			{
				if (chainSpawns < 16)
				{
					shotMultiPos[SHOT_MISC] = 0;
					player_shot_create(0, SHOT_MISC, (JE_word)hitX, (JE_word)hitY, mouseX, mouseY,
					                   s->chainReaction, 1);
					++chainSpawns;
				}
				JE_setupExplosion(hitX, hitY, 0, 0, false, false);
				shotAvail[z] = 0;
			}
			else
			{
				if (s->shotDmg == 99)
				{
					t->ice = 18;
				}
				else
				{
					int dmg = s->shotDmg;
					if (dmg >= 250)
						dmg -= 250;
					t->armor -= dmg;
					t->hit = 3;
					if (t->armor <= 0)
					{
						t->armor = cwTargetArmor;
						t->dead = 10;
						JE_setupExplosion(t->cx - 6, t->cy - 7, 0, 1, false, false);
					}
				}
				t->idle = 0;
				JE_setupExplosion(hitX, hitY, 0, 0, false, false);
				if (s->shotDmg < 250)
					shotAvail[z] = 0;
			}
			break;
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

static void cwPodPos(const JE_OptionType *o, int side, bool both, int *x, int *y)
{
	const int dir = (side == 0) ? -1 : 1;
	switch (o->tr)
	{
	case 2:
		*x = both ? cwShipX + dir * FRONT_OPTION_SPREAD : cwShipX;
		*y = cwShipY - 20;
		break;
	case 4:
	{
		const int dx = (int)roundf(sinf(cwOrbit) * 20), dy = (int)roundf(cosf(cwOrbit) * 20);
		*x = cwShipX + (side == 0 ? dx : -dx);
		*y = cwShipY + (side == 0 ? dy : -dy);
		break;
	}
	case 1:
	case 3:
		*x = cwShipX + dir * 15;
		*y = cwShipY + 10;
		break;
	default:
		*x = cwShipX + (side == 0 ? -14 : 16);
		*y = cwShipY;
		break;
	}

	const bool big = (o->tr == 1 || o->tr == 2);
	*x = cwClamp(*x, CW_BOX_X0 + (big ? 6 : 0), CW_BOX_X1 - (big ? 18 : 12));
	*y = cwClamp(*y, CW_BOX_Y0, CW_RANGE_Y1 - (big ? 28 : 14));
}

typedef struct
{
	const JE_OptionType *option;
	bool side[2];
	int port;
	int weapon;
	int charge;
} CwPods;

static bool cwPreviewPods(CwPods *pods)
{
	memset(pods, 0, sizeof(*pods));
	if (cwPreviewingSource())
	{
		const CustomBulletPreset *const src = cwSource();
		if (src == NULL || src->sourceOption == 0 || src->sourceOption > OPTION_NUM)
			return false;
		pods->option = &options[src->sourceOption];
		pods->side[0] = true;
		pods->port = pods->option->wport;
		pods->weapon = customBulletPresetWeapon(cwImportSrc, 0, cwImportPwr);
		pods->charge = cwClamp(cwImportPwr - 1, 0, pods->option->pwr);
		return pods->weapon > 0;
	}

	if (!cwIsSidekickSlot() || customSidekickSlot <= 0 || customSidekickSlot > OPTION_NUM)
		return false;
	pods->option = &options[customSidekickSlot];
	pods->side[0] = (customWeaponEquipSlot != CUSTOM_EQUIP_RIGHT);
	pods->side[1] = (customWeaponEquipSlot != CUSTOM_EQUIP_LEFT);
	pods->port = pods->option->wport;
	pods->weapon = customWeaponScratchWeapon(0, customWeaponEditLevel);
	pods->charge = cwLevelUsed(customWeaponEditLevel) ? customWeaponEditLevel : 0;
	return true;
}

static void cwFireAndDrawPods(const CwPods *pods)
{
	const JE_OptionType *const o = pods->option;
	const bool both = pods->side[0] && pods->side[1];
	if (o->tr == 4)
		cwOrbit += both ? 0.2f : 0.15f;

	for (int side = 0; side < 2; ++side)
	{
		if (!pods->side[side])
			continue;

		int x, y;
		cwPodPos(o, side, both, &x, &y);

		const int shot_i = (side == 0) ? SHOT_LEFT_SIDEKICK : SHOT_RIGHT_SIDEKICK;
		if (shotRepeat[shot_i] > 0)
		{
			--shotRepeat[shot_i];
		}
		else
		{
			const int shotY = y + (o->tr == 1 ? SIDEKICK_TRAIL_SHOT_Y : 0);
			player_shot_create((JE_word)pods->port, shot_i, (JE_word)x, (JE_word)shotY, mouseX,
			                   mouseY, (JE_word)pods->weapon, 1);
			cwPodAnim[side].animating = true;
		}

		if (o->option == 0)
			continue;

		CwPodAnim *const anim = &cwPodAnim[side];
		const unsigned int frames = (unsigned int)cwClamp(o->ani, 1, 20);
		if (anim->frame >= frames)
			anim->frame = 0;
		if (anim->animating && ++anim->frame >= frames)
		{
			anim->frame = 0;
			anim->animating = (o->option == 1);
		}

		const unsigned int sprite = o->gr[anim->frame] + (unsigned int)pods->charge;
		rl_current_id = RL_ID_SIDEKICK_BASE + 2 + side;
		rl_shot_attach = 3;
		if (o->tr == 4)
		{
			const float ox = sinf(cwOrbit) * 20, oy = cosf(cwOrbit) * 20;
			const float dir = (side == 0) ? 1.0f : -1.0f;
			rl_current_sub_x = dir * (ox - roundf(ox));
			rl_current_sub_y = dir * (oy - roundf(oy));
		}
		if (o->tr == 1 || o->tr == 2)
			blit_sprite2x2(VGAScreen, x - 6, y, spriteSheet10, sprite);
		else
			blit_sprite2(VGAScreen, x, y, spriteSheet9, sprite);
		rl_current_id = 0;
		rl_shot_attach = 0;
		rl_current_sub_x = rl_current_sub_y = 0.0f;
	}
}

static int cwPreviewShip(void)
{
	if (weaponEditorShip > 0 && weaponEditorShip <= SHIP_DRAGONWING)
		return weaponEditorShip;
	const int ship = player[customWeaponLocalOwner()].items.ship;
	return (ship != 0) ? ship : 1;
}

static int cwPreviewSubjectKey(void)
{
	if (cwPreviewingSource())
		return 0x100000 | (cwImportSrc << 8) | (cwImportPwr << 1) | customWeaponEditMode;
	return (customWeaponEquipSlot << 8) | (customWeaponEditLevel << 1) | customWeaponEditMode;
}

static void cwDrawBoxBase(void);

// Track the selected segment's live shot. Reacquire by spawn column, then age,
// when the shot dies or no longer matches; call after shots move each tick.
static void cwUpdateTrackedShot(void)
{
	if (cwPage != CWP_BULLET || cwPreviewingSource())
	{
		cwTrackedShot = -1;
		cwTrackedSeg = -1;
		return;
	}
	const int b = cwBulletIndex();
	const JE_word sg = cwCur()->sg[b];
	if (cwTrackedShot >= 0 && cwTrackedShot < MAX_PWEAPON && cwTrackedSeg == b &&
	    shotAvail[cwTrackedShot] != 0 && playerShotData[cwTrackedShot].shotGr == sg)
		return;

	const int spawnX = player[0].x + cwCur()->bx[b] + 1;
	int best = -1, bestScore = INT_MAX;
	for (int i = 0; i < MAX_PWEAPON; ++i)
	{
		if (shotAvail[i] == 0 || playerShotData[i].shotGr != sg)
			continue;
		const int score = abs(playerShotData[i].shotX - spawnX) * 4 - playerShotData[i].shotY;
		if (score < bestScore)
		{
			bestScore = score;
			best = i;
		}
	}
	cwTrackedShot = best;
	cwTrackedSeg = (best >= 0) ? b : -1;
}

static void cwSimTick(void)
{
	const int key = cwPreviewSubjectKey();
	if (key != cwPreviewKey)
	{
		cwClearPreviewShots();
		cwPreviewKey = key;
	}

	cwDrawBoxBase();
	rl_begin_record();
	rl_mark_residual_base(VGAScreenSeg);

	power = lastPower = 900;
	rl_set_ship_vel(0, cwShipX - cwTickShipX, cwShipY - cwTickShipY);
	cwTickShipX = cwShipX;
	cwTickShipY = cwShipY;
	player[0].x = cwShipX;
	player[0].y = cwShipY;
	mouseX = player[0].x;
	mouseY = player[0].y;

	CwPods pods;
	const bool usePods = cwPreviewPods(&pods);
	if (!usePods)
	{
		int port = customWeaponPort;
		int weapon = customWeaponScratchWeapon(customWeaponEditMode, customWeaponEditLevel);
		if (cwPreviewingSource())
		{
			const CustomBulletPreset *const src = cwSource();
			port = (src != NULL) ? src->sourcePort : 0;
			weapon = customBulletPresetWeapon(cwImportSrc, customWeaponEditMode, cwImportPwr);
		}
		if (shotRepeat[SHOT_FRONT] > 0)
			--shotRepeat[SHOT_FRONT];
		else if (weapon > 0)
			player_shot_create((JE_word)port, SHOT_FRONT, player[0].x, player[0].y, mouseX, mouseY,
			                   (JE_word)weapon, 1);
	}

	if (cwDummyCount > 0)
	{
		cwHomeShots();
		cwCollideShots();
	}
	simulate_player_shots();
	JE_setSPClip(CW_BOX_X0, CW_BOX_Y0, CW_BOX_X1 + 1, CW_RANGE_Y1 + 1);
	JE_drawSP();
	JE_clearSPClip();

	rl_current_id = RL_ID_SHIP_BASE + 1;
	JE_drawItem(1, (JE_word)cwPreviewShip(), player[0].x - 5, player[0].y - 7);
	rl_current_id = 0;
	if (usePods)
		cwFireAndDrawPods(&pods);
	if (cwDummyCount > 0)
	{
		cwPumpExplosions();
		cwDrawDummyBars();
	}

	rl_end_record();
	rl_finalize();
	rl_capture_residual(VGAScreenSeg, game_screen);

	cwTickDummies();
	cwUpdateTrackedShot();
	if (cwNoticeTicks > 0)
		--cwNoticeTicks;
}

static void cwDrawCaption(void)
{
	char caption[48];
	Uint8 hue = 5;
	Sint8 value = 4;
	if (cwPreviewingSource())
	{
		const CustomBulletPreset *const src = cwSource();
		snprintf(caption, sizeof(caption), "Stock %s", (src != NULL) ? src->name : "");
		hue = 15;
		value = 2;
	}
	else
	{
		snprintf(caption, sizeof(caption), "%s", customWeaponName);
	}
	char fit[48];
	draw_font_hv_shadow(VGAScreen, CW_BOX_MID, CW_BOX_Y0 + 5,
	                    cwFitText(caption, CW_BOX_W - 8, fit, sizeof(fit)), small_font, centered,
	                    hue, value, true, 1);
}

static int cwBarX(int level)
{
	return CW_BAR_X0 + level * CW_BAR_PITCH;
}

static int cwChartHover(void)
{
	if (mouse_y < CW_BAR_TOP - 2 || mouse_y > CW_BAR_NUM_Y + 6 || mouse_x < CW_BAR_X0 - 2)
		return -1;
	const int i = (mouse_x - CW_BAR_X0 + 2) / CW_BAR_PITCH;
	return (i < CUSTOM_POWER_LEVELS) ? i : -1;
}

static void cwDrawLevelBar(int x0, int top, int shade)
{
	const int x1 = x0 + CW_BAR_W - 1;
	for (int y = CW_BAR_BOT, band = 0; y >= top; y -= 2, ++band)
	{
		const int c = MIN(shade + band / 2, 15);
		fill_rectangle_xy(VGAScreen, x0, MAX(y - 1, top), x1, y, (Uint8)(0x50 | c));
	}
	fill_rectangle_xy(VGAScreen, x0, top, x1, top, (Uint8)(0x50 | MIN(shade + 6, 15)));
	fill_rectangle_xy(VGAScreen, x0, top + 1, x0, CW_BAR_BOT, (Uint8)(0x50 | MIN(shade + 3, 15)));
	fill_rectangle_xy(VGAScreen, x1, top + 1, x1, CW_BAR_BOT, (Uint8)(0x50 | MAX(shade - 1, 0)));
}

static void cwDrawTray(void)
{
	fill_rectangle_xy(VGAScreen, CW_BOX_X0, CW_TRAY_Y0, CW_BOX_X1, CW_BOX_Y1, CW_BG_BAND);
	fill_rectangle_xy(VGAScreen, CW_BOX_X0, CW_TRAY_Y0, CW_BOX_X1, CW_TRAY_Y0, CW_BG_RIM);

	const int mode = cwClamp(customWeaponEditMode, 0, CUSTOM_WEAPON_MODES - 1);
	int dps[CUSTOM_POWER_LEVELS], maxDps = 1;
	for (int i = 0; i < CUSTOM_POWER_LEVELS; ++i)
	{
		dps[i] = cwDps(&customWeaponRaw[mode][i]);
		if (dps[i] > maxDps && cwLevelUsed(i))
			maxDps = dps[i];
	}

	const int sel = cwClamp(customWeaponEditLevel, 0, CUSTOM_POWER_LEVELS - 1);
	const int hover = mouseInactive ? -1 : cwChartHover();
	const int span = CW_BAR_BOT - CW_BAR_TOP + 1;
	for (int i = 0; i < CUSTOM_POWER_LEVELS; ++i)
	{
		const int x0 = cwBarX(i), x1 = x0 + CW_BAR_W - 1;
		const bool used = cwLevelUsed(i);
		fill_rectangle_xy(VGAScreen, x0, CW_BAR_TOP, x1, CW_BAR_BOT, CW_BG_BAND + 1);

		const int h = MAX(2, (cwClamp(dps[i], 0, maxDps) * span + maxDps / 2) / maxDps);
		const int shade = !used ? 2 : (i == sel) ? 8 : (i == hover) ? 6 : 4;
		cwDrawLevelBar(x0, CW_BAR_BOT - h + 1, shade);
		if (i == sel)
			cwDrawClamp(x0 - 2, CW_BAR_TOP - 2, x1 + 2, CW_BAR_BOT + 2, 2, CW_BG_SELECT);

		char num[4];
		snprintf(num, sizeof(num), "%d", i + 1);
		draw_font_hv_shadow(VGAScreen, (x0 + x1 + 1) / 2, CW_BAR_NUM_Y, num, small_font, centered,
		                    5, (i == sel) ? 8 : (i == hover) ? 6 : (used ? 3 : 0), true, 1);
	}

	if (!cwShowStats)
		return;

	const JE_WeaponType *w = cwCur();
	if (cwPreviewingSource())
	{
		const int wn = customBulletPresetWeapon(cwImportSrc, customWeaponEditMode, cwImportPwr);
		if (wn <= 0)
			return;
		w = &weapons[wn];
	}
	bool ice = false;
	const int dmg = cwVolleyDamage(w, &ice, 2);
	const int period = w->shotrepeat + 1;
	const int rate10 = 35 * 10 / period;
	char dpsText[24], volley[24], rate[16];
	if (ice && dmg == 0)
		snprintf(dpsText, sizeof(dpsText), "Ice");
	else
		snprintf(dpsText, sizeof(dpsText), "DPS %d", cwDps(w));
	snprintf(volley, sizeof(volley), "%d dmg", dmg);
	snprintf(rate, sizeof(rate), "%d.%d/s", rate10 / 10, rate10 % 10);
	draw_font_hv_shadow(VGAScreen, cwBarX(0), CW_STATS_Y, dpsText, small_font, left_aligned,
	                    5, 6, true, 1);
	draw_font_hv_shadow(VGAScreen, CW_BOX_MID, CW_STATS_Y, volley, small_font, centered,
	                    5, 4, true, 1);
	draw_font_hv_shadow(VGAScreen, cwBarX(CUSTOM_POWER_LEVELS - 1) + CW_BAR_W, CW_STATS_Y, rate,
	                    small_font, right_aligned, 5, 4, true, 1);
}

static void cwDrawBoxBase(void)
{
	fill_rectangle_xy(VGAScreen, CW_BOX_X0, CW_BOX_Y0, CW_BOX_X1, CW_RANGE_Y1, CW_BG_SPACE);
	cwDrawCaption();
	if (cwDummyCount > 0)
		for (int d = 0; d < cwDummyCount; ++d)
			cwDrawDummy(&cwDummies[d]);
	cwDrawTray();
}

static Uint8 cwBoxBase[CW_BOX_H][CW_BOX_W];

static void cwCaptureBoxBase(void)
{
	const Uint8 *src =
	    (const Uint8 *)VGAScreenSeg->pixels + CW_BOX_Y0 * VGAScreenSeg->pitch + CW_BOX_X0;
	for (int y = 0; y < CW_BOX_H; ++y, src += VGAScreenSeg->pitch)
		memcpy(cwBoxBase[y], src, CW_BOX_W);
}

static Uint32 cwStarHash(Uint32 v)
{
	v ^= v >> 16;
	v *= 0x7feb352du;
	v ^= v >> 15;
	v *= 0x846ca68bu;
	v ^= v >> 16;
	return v;
}

static void cwDrawStarCell(SDL_Surface *dst, int scale, int x_offset, int x, float fy, JE_byte c)
{
	if (x < CW_BOX_X0 || x > CW_BOX_X1)
		return;

	const int hx = (x + x_offset) * scale;
	const int hy = (int)(fy * (float)scale + 0.5f);
	for (int py = hy; py < hy + scale; ++py)
	{
		const int sy = py / scale;
		if (sy < CW_BOX_Y0 || sy > CW_RANGE_Y1)
			continue;
		Uint8 *row = (Uint8 *)dst->pixels + py * dst->pitch + hx;
		for (int k = 0; k < scale; ++k)
			if (row[k] == CW_BG_SPACE)
				row[k] = c;
	}
}

static void cwDrawStars(SDL_Surface *dst, int scale, int x_offset, Uint32 clock_ms)
{
	static const float speed_px_s[3] = { 5.0f, 9.0f, 14.0f };
	static const JE_byte shade[3] = { 0x56, 0x58, 0x5a };
	static const JE_byte flicker[4] = { 0, 1, 2, 1 };
	const float spanY = (float)(CW_RANGE_Y1 - CW_BOX_Y0 - 1);
	const Uint32 spanX = (Uint32)(CW_BOX_X1 - CW_BOX_X0 - 1);

	for (unsigned int i = 0; i < CW_STAR_COUNT; ++i)
	{
		const Uint32 h = cwStarHash(i + 1);
		const Uint32 h2 = cwStarHash(h ^ 0x9e3779b9u);
		const unsigned int layer = ((h >> 28) % 5 == 4) ? 2 : ((h >> 27) & 1);
		const int x = CW_BOX_X0 + 1 + (int)((i * spanX + h % spanX) / CW_STAR_COUNT);
		const float drift =
		    (float)(h2 % (Uint32)spanY) + (float)clock_ms * speed_px_s[layer] / 1000.0f;
		const float fy = (float)(CW_BOX_Y0 + 1) + fmodf(drift, spanY);

		JE_byte c = (JE_byte)(shade[layer] + ((h >> 8) % 7 == 0 ? 2 : 0));
		if (layer == 2)
			c += flicker[((clock_ms >> 8) + (h2 >> 4)) % 4];

		cwDrawStarCell(dst, scale, x_offset, x, fy, c);
		if (layer == 2)
		{
			const JE_byte arm = (JE_byte)(c - 4);
			cwDrawStarCell(dst, scale, x_offset, x - 1, fy, arm);
			cwDrawStarCell(dst, scale, x_offset, x + 1, fy, arm);
			cwDrawStarCell(dst, scale, x_offset, x, fy - 1.0f, arm);
			cwDrawStarCell(dst, scale, x_offset, x, fy + 1.0f, arm);
		}
	}
}

enum { CW_FLASH_MS = 200 };

static int cwFlashX0, cwFlashY0, cwFlashX1, cwFlashY1;
static Uint32 cwFlashStart_ms;

static void cwFlashRegion(int x0, int y0, int x1, int y1)
{
	cwFlashX0 = x0;
	cwFlashY0 = y0;
	cwFlashX1 = x1;
	cwFlashY1 = y1;
	cwFlashStart_ms = SDL_GetTicks();
}

static void cwDrawClickFlash(SDL_Surface *dst, int scale, int x_offset)
{
	if (cwFlashStart_ms == 0)
		return;
	const Uint32 age = SDL_GetTicks() - cwFlashStart_ms;
	if (age >= CW_FLASH_MS)
	{
		cwFlashStart_ms = 0;
		return;
	}

	static const Uint8 dither[2][2] = { { 0, 2 }, { 3, 1 } };
	const int glow4 = (int)((CW_FLASH_MS - age) * 10 / CW_FLASH_MS);
	const int hy1 = (cwFlashY1 + 1) * scale - 1;
	const int hx0 = (cwFlashX0 + x_offset) * scale;
	const int hx1 = (cwFlashX1 + 1 + x_offset) * scale - 1;
	for (int y = cwFlashY0 * scale; y <= hy1; ++y)
	{
		Uint8 *row = (Uint8 *)dst->pixels + y * dst->pitch;
		for (int x = hx0; x <= hx1; ++x)
		{
			const int lift = (glow4 + dither[y & 1][x & 1]) >> 2;
			if (lift == 0)
				continue;
			const int v = (row[x] & 0x0f) + lift;
			row[x] = (Uint8)((row[x] & 0xf0) | (v > 15 ? 15 : v));
		}
	}
}

static int cwSelectedField(void)
{
	if (cwSelRow < 0 || cwSelRow >= cwNavCount || cwNav[cwSelRow].kind != CWN_FIELD)
		return -1;
	return cwNav[cwSelRow].id[0];
}

static void cwDrawSegmentCorners(int x0, int y0, int x1, int y1)
{
	if (x0 < CW_BOX_X0)
		x0 = CW_BOX_X0;
	if (y0 < CW_BOX_Y0)
		y0 = CW_BOX_Y0;
	if (x1 > CW_BOX_X1)
		x1 = CW_BOX_X1;
	if (y1 > CW_RANGE_Y1)
		y1 = CW_RANGE_Y1;
	if (x1 - x0 < 2 || y1 - y0 < 2)
		return;
	cwDrawClamp(x0, y0, x1, y1, 1, 0x9C);
}

static void cwDrawLoupe(JE_word sg)
{
	enum { LX0 = CW_BOX_X0 + 3, LY1 = CW_RANGE_Y1 - 3, LW = 34 };
	const int x0 = LX0, y0 = LY1 - LW + 1, x1 = LX0 + LW - 1;
	fill_rectangle_xy(VGAScreen, x0, y0, x1, LY1, CW_BG_WELL);
	JE_rectangle(VGAScreen, x0, y0, x1, LY1, CW_BG_SOCKET);
	cwDrawClamp(x0 - 1, y0 - 1, x1 + 1, LY1 + 1, 3, CW_BG_SELECT);

	const int cx = (x0 + x1 + 1) / 2, cy = (y0 + LY1 + 1) / 2;
	Sprite2_array sheet;
	int index, sx, sy, sw, sh;
	if (cwResolveShotSprite(sg, &sheet, &index))
	{
		if (!cwSpriteExtent(sg, &sx, &sy, &sw, &sh))
		{
			sx = sy = 0;
			sw = 12;
			sh = 14;
		}
		blit_sprite2_scaled(VGAScreen, cx - (sx + sw / 2) * 2, cy - (sy + sh / 2) * 2, sheet, index,
		                    2, BLIT2_COPY, 0);
	}
	else if (sg >= 60000)
	{
		blit_sprite_blend(VGAScreen, cx - 6, cy - 6, OPTION_SHAPES, sg - 60001);
	}
}

static bool cwShipHit(int x, int y)
{
	return x >= cwShipX - 5 && x <= cwShipX + 18 && y >= cwShipY - 7 && y <= cwShipY + 20;
}

static int cwDummyHit(int x, int y)
{
	if (cwDummyCount == 0)
		return -1;
	for (int d = 0; d < cwDummyCount; ++d)
		if (abs(x - cwDummies[d].cx) <= cwTargetSize + 2 &&
		    abs(y - cwDummies[d].cy) <= cwTargetSize + 2)
			return d;
	return -1;
}

enum { CW_DRAG_NONE, CW_DRAG_SHIP, CW_DRAG_TARGETS };
static int cwDrag = CW_DRAG_NONE;
static int cwDragDx, cwDragDy;

static void cwDrawOverlay(float alpha)
{
	if (cwPage == CWP_BULLET && cwTrackedShot >= 0 && cwTrackedShot < MAX_PWEAPON &&
	    shotAvail[cwTrackedShot] != 0)
	{
		const PlayerShotDataType *s = &playerShotData[cwTrackedShot];
		const int dvx = (s->shotXM > 100 || s->shotXM < -100) ? 0 : s->shotXM;
		const int dvy = (s->shotYM > 100 || s->shotYM < -100) ? 0 : s->shotYM;
		const int px = s->shotX + 1 + (int)(dvx * alpha);
		const int py = s->shotY + (int)(dvy * alpha);
		int ox, oy, ow, oh;
		if (cwSpriteExtent(s->shotGr, &ox, &oy, &ow, &oh))
			cwDrawSegmentCorners(px + ox - 1, py + oy - 1, px + ox + ow, py + oy + oh);
	}

	if (cwSelectedField() == CWF_B_SPRITE)
		cwDrawLoupe(cwCur()->sg[cwBulletIndex()]);

	if (mouseInactive)
		return;
	const bool overShip =
	    cwDrag == CW_DRAG_SHIP || (cwDrag == CW_DRAG_NONE && cwShipHit(mouse_x, mouse_y));
	const bool overTargets = cwDrag == CW_DRAG_TARGETS ||
	                         (cwDrag == CW_DRAG_NONE && cwDummyHit(mouse_x, mouse_y) >= 0);
	if (overTargets)
	{
		for (int d = 0; d < cwDummyCount; ++d)
			cwDrawClamp(cwDummies[d].cx - cwTargetSize - 4, cwDummies[d].cy - cwTargetSize - 4,
			            cwDummies[d].cx + cwTargetSize + 4, cwDummies[d].cy + cwTargetSize + 4, 2,
			            cwDrag == CW_DRAG_TARGETS ? C_HI : CW_BG_SELECT);
	}
	else if (overShip)
	{
		cwDrawClamp(cwShipX - 7, cwShipY - 9, cwShipX + 20, cwShipY + 22, 3,
		            cwDrag == CW_DRAG_SHIP ? C_HI : CW_BG_SELECT);
	}
}

static SDL_Surface *cwHiFrame = NULL, *cwHiReplay = NULL;

static void cwCompositeRange(void)
{
	for (int y = CW_BOX_Y0; y <= CW_RANGE_Y1; ++y)
	{
		const Uint8 *src = (const Uint8 *)game_screen->pixels + y * game_screen->pitch;
		Uint8 *dst = (Uint8 *)VGAScreenSeg->pixels + y * VGAScreenSeg->pitch;
		for (int x = CW_BOX_X0; x <= CW_BOX_X1; ++x)
			if (src[x] != 0)
				dst[x] = src[x];
	}
}

static void cwCompositeRangeHi(SDL_Surface *frame, const SDL_Surface *replay, int ss, int x_offset)
{
	for (int y = CW_BOX_Y0; y <= CW_RANGE_Y1; ++y)
	{
		const Uint8 *live = (const Uint8 *)VGAScreenSeg->pixels + y * VGAScreenSeg->pitch;
		const Uint8 *base = cwBoxBase[y - CW_BOX_Y0];
		for (int x = CW_BOX_X0; x <= CW_BOX_X1; ++x)
		{
			if (live[x] != base[x - CW_BOX_X0])
				continue;
			for (int sy = 0; sy < ss; ++sy)
			{
				const Uint8 *s =
				    (const Uint8 *)replay->pixels + (y * ss + sy) * replay->pitch + x * ss;
				Uint8 *d =
				    (Uint8 *)frame->pixels + (y * ss + sy) * frame->pitch + (x + x_offset) * ss;
				for (int sx = 0; sx < ss; ++sx)
					if (s[sx] != 0)
						d[sx] = s[sx];
			}
		}
	}
}

static void cwPresentFrame(float alpha, Uint32 clock_ms)
{
	const int ss = effective_supersample();
	SDL_Surface *const frame = (ss > 1) ? video_ensure_scaled_surface(&cwHiFrame, ss) : NULL;
	SDL_Surface *const replay =
	    (frame != NULL) ? video_ensure_scaled_surface(&cwHiReplay, ss) : NULL;

	if (replay == NULL)
	{
		rl_replay_interp(game_screen, alpha, false, 1);
		cwCompositeRange();
		if (shipEditorStars)
			cwDrawStars(VGAScreenSeg, 1, 0, clock_ms);
		cwDrawOverlay(alpha);
		cwDrawClickFlash(VGAScreenSeg, 1, 0);
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		return;
	}

	cwDrawOverlay(alpha);
	rl_replay_interp(replay, alpha, false, ss);
	JE_mouseStart();
	const int x_offset = video_get_menu_x_offset();
	expand_frame_to_hi(video_compose_frame(), frame, ss);
	cwCompositeRangeHi(frame, replay, ss, x_offset);
	if (shipEditorStars)
		cwDrawStars(frame, ss, x_offset, clock_ms);
	cwDrawClickFlash(frame, ss, x_offset);
	JE_drawMouseToHiFrame(frame, ss, x_offset);
	present_hi(frame);
	JE_mouseReplace();
}

static void cwBuildNav(void)
{
	cwNavCount = 0;
	CwNav *n = &cwNav[cwNavCount++];
	n->kind = CWN_TABS;
	n->count = CWP_COUNT;
	n->y = CW_TAB_Y0;
	for (int p = 0; p < CWP_COUNT; ++p)
		n->id[p] = (Uint8)p;

	int y = CW_PAGE_Y0;
	for (const CwLine *l = cwPageLines[cwPage]; l->kind != CWL_END; ++l)
	{
		if (l->kind == CWL_GAP)
		{
			y += CW_GAP_H;
			continue;
		}
		n = &cwNav[cwNavCount++];
		n->kind = (l->kind == CWL_FIELD) ? CWN_FIELD : CWN_BUTTONS;
		n->count = l->count;
		memcpy(n->id, l->id, l->count);
		n->y = y;
		y += (l->kind == CWL_BUTTONS) ? CW_ROW_H + 1 : CW_ROW_H;
	}

	n = &cwNav[cwNavCount++];
	n->kind = CWN_ACTIONS;
	n->count = 0;
	n->y = CW_ACTIONS_Y;
	n->id[n->count++] = CWA_UNDO;
	n->id[n->count++] = CWA_REDO;
	n->id[n->count++] = CWA_SAVE;
	if (cwCanEquip)
		n->id[n->count++] = CWA_EQUIP;
	n->id[n->count++] = CWA_DONE;

	cwSelRow = cwClamp(cwSelRow, 0, cwNavCount - 1);
	if (cwNav[cwSelRow].kind == CWN_TABS)
		cwSelCell = cwPage;
	cwSelCell = cwClamp(cwSelCell, 0, cwNav[cwSelRow].count - 1);
}

static void cwCellRect(const CwNav *n, int cell, int *x0, int *y0, int *x1, int *y1)
{
	const int cols = (n->kind == CWN_TABS) ? CW_TAB_COLS : n->count;
	const int col = cell % cols, span = CW_INNER_X1 - CW_INNER_X0 + 2;
	*x0 = CW_INNER_X0 + col * span / cols;
	*x1 = CW_INNER_X0 + (col + 1) * span / cols - 2;
	if (n->kind == CWN_TABS)
	{
		*y0 = CW_TAB_Y0 + cell / cols * CW_TAB_PITCH;
		*y1 = *y0 + CW_TAB_PITCH - 2;
		return;
	}

	*y0 = n->y - 1;
	*y1 = (n->kind == CWN_ACTIONS) ? n->y + 9
	      : (n->kind == CWN_BUTTONS) ? n->y + CW_ROW_H - 2 : n->y + CW_ROW_H - 3;
}

static int cwCellCenterX(const CwNav *n, int cell)
{
	int x0, y0, x1, y1;
	cwCellRect(n, cell, &x0, &y0, &x1, &y1);
	return (x0 + x1) / 2;
}

static void cwDrawArrowGlyph(int x, int y, bool right, Uint8 c)
{
	for (int r = 0; r < 5; ++r)
	{
		const int w = 3 - abs(r - 2);
		for (int i = 0; i < w; ++i)
			JE_pix(VGAScreen, right ? x + i : x + 2 - i, y + r, c);
	}
}

static void cwDrawField(const CwNav *n, bool sel, Uint32 clock_ms)
{
	const int f = n->id[0];
	const int y = n->y;
	const bool active = cwFieldActive(f);
	fill_rectangle_xy(VGAScreen, CW_INNER_X0, y - 1, CW_INNER_X1, y + CW_ROW_H - 3,
	                  sel ? C_SEL : C_PANEL);

	char value[48];
	const bool typing = (cwNumField == f);
	if (typing)
		SDL_strlcpy(value, cwNumText, sizeof(value));
	else
		cwFieldValue(f, value, sizeof(value));

	const bool editingText = typing || (sel && f == CWF_NAME);
	const bool arrows = sel && active && !editingText;
	const int right = arrows ? CW_VALUE_X - 5 : (editingText ? CW_VALUE_X - 3 : CW_VALUE_X);
	const int room = right - CW_LABEL_X - (arrows ? 6 : 0);

	char fit[48];
	const char *shown = value;
	if (editingText)
	{
		while (*shown != '\0' && JE_textWidth(shown, small_font) > room)
			++shown;
	}
	else
	{
		shown = cwFitText(value, room, fit, sizeof(fit));
	}
	const int vw = JE_textWidth(shown, small_font);
	const int left = right - vw - (arrows ? 6 : 0);

	const Sint8 labelValue = !active ? 0 : (sel ? 5 : 3);
	const Sint8 textValue = !active ? 1 : (editingText ? 7 : (sel ? 6 : 5));
	if (CW_LABEL_X + JE_textWidth(cwFields[f].label, small_font) + 6 <= left)
		draw_font_hv_shadow(VGAScreen, CW_LABEL_X, y, cwFields[f].label, small_font, left_aligned,
		                    15, labelValue, false, 1);
	draw_font_hv_shadow(VGAScreen, right, y, shown, small_font, right_aligned,
	                    15, textValue, false, 1);

	if (arrows)
	{
		cwDrawArrowGlyph(right - vw - 5, y + 1, false, C_HI);
		cwDrawArrowGlyph(right + 2, y + 1, true, C_HI);
	}
#ifdef PLATFORM_HANDHELD
	const bool caret = typing;
#else
	const bool caret = editingText;
#endif
	if (caret && (clock_ms / 400) % 2 == 0)
		fill_rectangle_xy(VGAScreen, right + 1, y, right + 1, y + 5, C_HI);
}

static void cwDrawButtons(const CwNav *n, bool rowSel)
{
	for (int c = 0; c < n->count; ++c)
	{
		const int a = n->id[c];
		const bool sel = rowSel && c == cwSelCell;
		const bool available = cwActionAvailable(a);
		int x0, y0, x1, y1;
		cwCellRect(n, c, &x0, &y0, &x1, &y1);
		fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, sel ? C_SEL : C_DIV);

		const bool armed = (cwArmed == a);
		const Sint8 value = armed ? 7 : !available ? (sel ? 1 : -3) : (sel ? 6 : 4);
		draw_font_hv_shadow(VGAScreen, (x0 + x1 + 1) / 2, n->y, armed ? "Confirm" : cwActs[a].label,
		                    small_font, centered, 15, value, false, 1);
	}
}

static int cwTabUnderMouse(void)
{
	if (mouseInactive)
		return -1;
	for (int p = 0; p < CWP_COUNT; ++p)
	{
		int x0, y0, x1, y1;
		cwCellRect(&cwNav[0], p, &x0, &y0, &x1, &y1);
		if (mouse_x >= x0 && mouse_x <= x1 && mouse_y >= y0 && mouse_y <= y1)
			return p;
	}
	return -1;
}

static void cwDrawTabs(const CwNav *n, bool rowSel)
{
	const int hover = cwTabUnderMouse();
	for (int p = 0; p < CWP_COUNT; ++p)
	{
		int x0, y0, x1, y1;
		cwCellRect(n, p, &x0, &y0, &x1, &y1);
		const bool cur = (p == cwPage);
		const Uint8 fill = cur ? C_SEL : C_DIV;
		fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, (Uint8)(p == hover ? fill - 1 : fill));
		const bool dim = (p == CWP_SIDEKICK && !cwIsSidekickSlot());
		draw_font_hv_shadow(VGAScreen, (x0 + x1 + 1) / 2, y0 + 2, cwPageInfo[p].name, small_font,
		                    centered, 15, cur ? 6 : (dim ? 1 : (p == hover ? 6 : 4)), false, 1);
		if (cur && rowSel && mouseInactive)
			JE_rectangle(VGAScreen, x0, y0, x1, y1, C_HI);
	}
}

enum { CW_CARD_X0 = CW_INNER_X0 + 2, CW_CARD_W = 30, CW_CARD_H = 34 };

static void cwArtBox(Sprite2_array sheet, unsigned int index, bool big, int *x0, int *y0, int *x1,
                     int *y1)
{
	static SDL_Surface *scratch = NULL;
	const int w = big ? 24 : 12, h = big ? 28 : 14;
	*x0 = 0;
	*y0 = 0;
	*x1 = w - 1;
	*y1 = h - 1;
	if (scratch == NULL)
		scratch = SDL_CreateRGBSurface(0, 24, 28, 8, 0, 0, 0, 0);
	if (scratch == NULL)
		return;

	SDL_FillRect(scratch, NULL, 0);
	SDL_Surface *const saved = VGAScreen;
	VGAScreen = scratch;
	if (big)
		blit_sprite2x2(scratch, 0, 0, sheet, index);
	else
		blit_sprite2(scratch, 0, 0, sheet, index);
	VGAScreen = saved;

	int lx = w, ly = h, hx = -1, hy = -1;
	for (int y = 0; y < h; ++y)
	{
		const Uint8 *const row = (const Uint8 *)scratch->pixels + y * scratch->pitch;
		for (int x = 0; x < w; ++x)
			if (row[x] != 0)
			{
				lx = MIN(lx, x);
				hx = MAX(hx, x);
				ly = MIN(ly, y);
				hy = MAX(hy, y);
			}
	}
	if (hx >= 0)
	{
		*x0 = lx;
		*y0 = ly;
		*x1 = hx;
		*y1 = hy;
	}
}

static void cwDrawCardArt(int y0, Sprite2_array sheet, unsigned int index, bool big, int scale)
{
	int ax0, ay0, ax1, ay1;
	cwArtBox(sheet, index, big, &ax0, &ay0, &ax1, &ay1);
	const int x = CW_CARD_X0 + (CW_CARD_W - (ax1 - ax0 + 1) * scale) / 2 - ax0 * scale;
	const int y = y0 + (CW_CARD_H - (ay1 - ay0 + 1) * scale) / 2 - ay0 * scale;
	if (big)
		blit_sprite2x2(VGAScreen, x, y, sheet, index);
	else if (scale > 1)
		blit_sprite2_scaled(VGAScreen, x, y, sheet, index, scale, BLIT2_COPY, 0);
	else
		blit_sprite2(VGAScreen, x, y, sheet, index);
}

static void cwDrawCard(int y0, const char *line1, const char *line2, const char *line3)
{
	const int x0 = CW_CARD_X0, x1 = x0 + CW_CARD_W - 1, y1 = y0 + CW_CARD_H - 1;
	fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, C_WELL);
	JE_rectangle(VGAScreen, x0, y0, x1, y1, C_DIV);
	cwDrawClamp(x0 - 1, y0 - 1, x1 + 1, y1 + 1, 3, C_HI);

	const int tx = x1 + 6, room = CW_INNER_X1 - 2 - tx;
	char fit[48];
	draw_font_hv_shadow(VGAScreen, tx, y0 + 4, cwFitText(line1, room, fit, sizeof(fit)), small_font,
	                    left_aligned, 15, 6, false, 1);
	draw_font_hv_shadow(VGAScreen, tx, y0 + 14, cwFitText(line2, room, fit, sizeof(fit)),
	                    small_font, left_aligned, 15, 3, false, 1);
	draw_font_hv_shadow(VGAScreen, tx, y0 + 24, cwFitText(line3, room, fit, sizeof(fit)),
	                    small_font, left_aligned, 15, 3, false, 1);
}

static void cwDrawPageExtras(void)
{
	int x0, y0, x1, top;
	cwCellRect(&cwNav[cwNavCount - 2], 0, &x0, &y0, &x1, &top);
	const int bottom = CW_ACTIONS_Y - 4;
	const int cardY = (top + bottom + 2 - CW_CARD_H) / 2;
	switch (cwPage)
	{
	case CWP_WEAPON:
	{
		char price[32], mount[48];
		snprintf(price, sizeof(price), "Price %d", customWeaponCost);
		snprintf(mount, sizeof(mount), "%s, power %d",
		         cwSlotNames[cwClamp(customWeaponEquipSlot, 0, CUSTOM_EQUIP_COUNT - 1)],
		         customWeaponPowerUse);
		cwDrawCard(cardY, customWeaponName, mount, price);
		cwDrawCardArt(cardY, shopSpriteSheet,
		              (unsigned int)cwClamp(customWeaponItemGraphic, 1, 237), true, 1);
		break;
	}
	case CWP_LEVELS:
	{
		const JE_WeaponType *const w = cwCur();
		char level[32], volley[40], clip[32];
		if (cwIsSidekickSlot() && customWeaponChargeStages > 1 &&
		    cwLevelUsed(customWeaponEditLevel))
			snprintf(level, sizeof(level), "Stage %d of %d", customWeaponEditLevel + 1,
			         customWeaponChargeStages);
		else
			snprintf(level, sizeof(level), "Level %d of %d", customWeaponEditLevel + 1,
			         CUSTOM_POWER_LEVELS);
		const int rate10 = 35 * 10 / (w->shotrepeat + 1);
		snprintf(volley, sizeof(volley), "%d bullet%s, %d.%d/s", w->multi, w->multi == 1 ? "" : "s",
		         rate10 / 10, rate10 % 10);
		snprintf(clip, sizeof(clip), "%s",
		         cwLevelClipboardSet ? "A level is copied" : "Clipboard empty");
		cwDrawCard(cardY, level, volley, clip);
		Sprite2_array sheet;
		int index;
		if (cwResolveShotSprite(w->sg[0], &sheet, &index))
			cwDrawCardArt(cardY, sheet, (unsigned int)index, false, 2);
		break;
	}
	case CWP_IMPORT:
	{
		const CustomBulletPreset *const src = cwSource();
		if (src == NULL)
			break;
		char kind[40], detail[40];
		unsigned int icon;
		if (src->sourceOption > 0 && src->sourceOption <= OPTION_NUM)
		{
			icon = options[src->sourceOption].itemgraphic;
			snprintf(kind, sizeof(kind), "Sidekick");
			if (src->maxPower > 1)
				snprintf(detail, sizeof(detail), "%d charge stages", src->maxPower);
			else
				snprintf(detail, sizeof(detail), "One shot type");
		}
		else
		{
			icon = weaponPort[src->sourcePort].itemgraphic;
			const bool twoModes = weaponPort[src->sourcePort].opnum >= 2;
			snprintf(kind, sizeof(kind), "%s", twoModes ? "Gun, two fire modes" : "Gun");
			snprintf(detail, sizeof(detail), "%d power level%s", src->maxPower,
			         src->maxPower == 1 ? "" : "s");
		}
		cwDrawCard(cardY, src->name, kind, detail);
		if (icon > 0)
			cwDrawCardArt(cardY, shopSpriteSheet, icon, true, 1);
		break;
	}
	case CWP_SIDEKICK:
	{
		if (customSidekickSlot <= 0 || customSidekickSlot > OPTION_NUM)
			break;
		const JE_OptionType *const o = &options[customSidekickSlot];
		char frames[40], charge[40];
		if (customSidekickFrames > 1)
			snprintf(frames, sizeof(frames), "%d frames, step %d", customSidekickFrames,
			         customSidekickFrameStep);
		else
			snprintf(frames, sizeof(frames), "Sprite %d, 1 frame", customSidekickSprite);
		if (customWeaponChargeStages > 1)
			snprintf(charge, sizeof(charge), "%d charge stages", customWeaponChargeStages);
		else
			snprintf(charge, sizeof(charge), "Charge off");
		cwDrawCard(cardY, cwMountNames[cwClamp(customSidekickMount, 0, CUSTOM_SIDEKICK_MOUNTS - 1)],
		           frames, charge);
		const unsigned int sprite =
		    o->gr[0] + (cwLevelUsed(customWeaponEditLevel) ? customWeaponEditLevel : 0);
		const bool big = (o->tr == 1 || o->tr == 2);
		cwDrawCardArt(cardY, big ? spriteSheet10 : spriteSheet9, sprite, big, big ? 1 : 2);
		break;
	}
	case CWP_HELP:
	{
		int y = CW_PAGE_Y0;
		for (size_t i = 0; i < COUNTOF(cwHelpText); ++i, y += CW_ROW_H)
		{
			draw_font_hv_shadow(VGAScreen, CW_LABEL_X, y, cwHelpText[i][0], small_font,
			                    left_aligned, 15, 6, false, 1);
			draw_font_hv_shadow(VGAScreen, CW_LABEL_X + 76, y, cwHelpText[i][1], small_font,
			                    left_aligned, 15, 3, false, 1);
		}
		break;
	}
	default:
		break;
	}
}

static void cwDrawPanel(Uint32 clock_ms)
{
	fill_rectangle_xy(VGAScreen, CW_PAN_X0, CW_PAN_Y0, CW_PAN_X1, CW_PAN_Y1, C_PANEL);
	JE_rectangle(VGAScreen, CW_PAN_X0, CW_PAN_Y0, CW_PAN_X1, CW_PAN_Y1, C_HI);
	draw_font_hv_shadow(VGAScreen, CW_PAN_MID, CW_PAN_Y0 + 2, "WEAPON EDITOR", small_font,
	                    centered, 15, 3, false, 1);
	fill_rectangle_xy(VGAScreen, CW_INNER_X0, CW_PAN_Y0 + 10, CW_INNER_X1, CW_PAN_Y0 + 10, C_DIV);

	for (int r = 0; r < cwNavCount; ++r)
	{
		const CwNav *const n = &cwNav[r];
		const bool sel = (r == cwSelRow);
		switch (n->kind)
		{
		case CWN_TABS:
			cwDrawTabs(n, sel);
			break;
		case CWN_FIELD:
			cwDrawField(n, sel, clock_ms);
			break;
		default:
			cwDrawButtons(n, sel);
			break;
		}
	}

	fill_rectangle_xy(VGAScreen, CW_INNER_X0, CW_TAB_Y0 + 2 * CW_TAB_PITCH, CW_INNER_X1,
	                  CW_TAB_Y0 + 2 * CW_TAB_PITCH, C_DIV);
	fill_rectangle_xy(VGAScreen, CW_INNER_X0, CW_ACTIONS_Y - 3, CW_INNER_X1, CW_ACTIONS_Y - 3,
	                  C_DIV);
	cwDrawPageExtras();
}

static const char *cwHintText(char *buf, size_t n)
{
	if (cwNoticeTicks > 0)
		return cwNotice;
	if (cwNumField >= 0)
		return "Type a value. Enter sets it, Esc cancels.";

	const CwNav *const nav = &cwNav[cwSelRow];
	switch (nav->kind)
	{
	case CWN_TABS:
	{
		const int hover = cwTabUnderMouse();
		return cwPageInfo[hover >= 0 ? hover : cwPage].help;
	}
	case CWN_FIELD:
	{
		const int f = nav->id[0];
		int lo, hi;
		if (!cwFieldActive(f))
			return cwFieldInactiveReason(f);
#ifdef PLATFORM_HANDHELD
		if (f == CWF_NAME)
			return "Press Confirm to rename.";
#endif
		if (f == CWF_POWER_LEVEL && cwIsSidekickSlot() && customWeaponChargeStages > 1)
			return "The charge stage being edited and previewed.";
		if (cwNumericRange(f, &lo, &hi))
		{
			snprintf(buf, n, "%s Type a number.", cwFields[f].help);
			if (JE_textWidth(buf, small_font) <= LEGACY_WIDTH - 20)
				return buf;
		}
		return cwFields[f].help;
	}
	default:
	{
		const int a = nav->id[cwClamp(cwSelCell, 0, nav->count - 1)];
		if (cwArmed == a)
		{
			snprintf(buf, n, "Choose %s again to confirm.", cwActs[a].label);
			return buf;
		}
		return cwActs[a].help;
	}
	}
}

static void cwSwitchPage(int page, bool intoContent)
{
	const bool wasContent = cwSelRow > 0 && cwSelRow < cwNavCount - 1;
	if (wasContent)
		cwPageRow[cwPage] = cwSelRow;
	cwPage = (page + CWP_COUNT) % CWP_COUNT;
	cwArmed = -1;
	cwHistoryMerge = -1;
	cwBuildNav();

	if ((intoContent || wasContent) && cwNavCount > 2)
	{
		cwSelRow = cwClamp(cwPageRow[cwPage], 1, cwNavCount - 2);
		cwSelCell = 0;
	}
	else if (wasContent)
	{
		cwSelRow = 0;
	}
	if (cwNav[cwSelRow].kind == CWN_TABS)
		cwSelCell = cwPage;
}

static void cwSelect(int row, int cell)
{
	if (row != cwSelRow || cell != cwSelCell)
	{
		cwArmed = -1;
		cwHistoryMerge = -1;
		cwCommitNumeric();
	}
	cwSelRow = row;
	cwSelCell = cell;
}

static void cwMoveSelection(int dir)
{
	const int fromX = cwCellCenterX(&cwNav[cwSelRow], cwSelCell);
	const int row = (cwSelRow + cwNavCount + dir) % cwNavCount;
	int cell = 0;
	if (cwNav[row].kind == CWN_TABS)
	{
		cell = cwPage;
	}
	else
	{
		int best = INT_MAX;
		for (int c = 0; c < cwNav[row].count; ++c)
		{
			const int d = abs(cwCellCenterX(&cwNav[row], c) - fromX);
			if (d < best)
			{
				best = d;
				cell = c;
			}
		}
	}
	cwSelect(row, cell);
}

static void cwHitTestPanel(int mx, int my, int *row, int *cell)
{
	*row = -1;
	*cell = -1;
	if (mx < CW_INNER_X0 || mx > CW_INNER_X1)
		return;
	for (int r = 0; r < cwNavCount; ++r)
	{
		const CwNav *const n = &cwNav[r];
		for (int c = 0; c < n->count; ++c)
		{
			int x0, y0, x1, y1;
			cwCellRect(n, c, &x0, &y0, &x1, &y1);
			if (n->kind == CWN_FIELD)
				y1 = n->y + CW_ROW_H - 2;
			if (mx >= x0 && mx <= x1 && my >= y0 && my <= y1)
			{
				*row = r;
				*cell = c;
				return;
			}
		}
	}
}

static bool cwFieldCyclesOnEnter(int f)
{
	switch (f)
	{
	case CWF_LIB_SELECT:
	case CWF_POWER_LEVEL:
	case CWF_TWO_MODES:
	case CWF_FIRE_MODE:
	case CWF_EQUIP_SLOT:
	case CWF_IMPORT_SRC:
	case CWF_IMPORT_PWR:
	case CWF_BULLET_SEL:
	case CWF_SK_MOUNT:
	case CWF_SK_ANIMATE:
	case CWF_PREVIEW_SHIP:
	case CWF_SPREAD:
	case CWF_SHOW_STATS:
	case CWF_STARS:
	case CWF_B_SPARKS:
		return true;
	default:
		return false;
	}
}

static void cwAdjustSelected(int f, int dir)
{
	if (!cwFieldActive(f))
	{
		JE_playSampleNum(S_SPRING);
		return;
	}
	if ((f == CWF_POWER_LEVEL && (customWeaponEditLevel + dir < 0 ||
	                              customWeaponEditLevel + dir >= CUSTOM_POWER_LEVELS)) ||
	    (f == CWF_LIB_SELECT && customWeaponLibCount <= 1) ||
	    (f == CWF_BULLET_SEL && cwCur()->multi <= 1))
	{
		JE_playSampleNum(S_SPRING);
		return;
	}
	cwFieldAdjust(f, dir);
	if (f != CWF_SOUND || cwCur()->sound == 0)
		JE_playSampleNum(S_CURSOR);
}

#ifdef PLATFORM_HANDHELD
static void cwRenameWithKeyboard(void)
{
	char kb[sizeof(customWeaponName)];
	SDL_strlcpy(kb, customWeaponName, sizeof(kb));
	if (!console_swkbd(kb, sizeof(kb), sizeof(kb) - 1, kb, "Weapon name", false))
		return;

	size_t out = 0;
	for (const char *c = kb; *c != '\0' && out + 1 < sizeof(customWeaponName); ++c)
		if ((unsigned char)*c >= 32 && (unsigned char)*c < 127)
			customWeaponName[out++] = *c;
	customWeaponName[out] = '\0';
	customWeaponMaterialize();
	cwHistoryRecord(-1);
}
#endif

static void cwActivate(bool *done, bool *equipped)
{
	const CwNav *const n = &cwNav[cwSelRow];
	switch (n->kind)
	{
	case CWN_TABS:
		cwSwitchPage(cwPage + 1, false);
		JE_playSampleNum(S_CURSOR);
		break;
	case CWN_FIELD:
	{
		const int f = n->id[0];
		if (!cwFieldActive(f))
		{
			JE_playSampleNum(S_SPRING);
		}
#ifdef PLATFORM_HANDHELD
		else if (f == CWF_NAME)
		{
			cwRenameWithKeyboard();
		}
#endif
		else if (cwFieldCyclesOnEnter(f))
		{
			if (f == CWF_POWER_LEVEL && customWeaponEditLevel == CUSTOM_POWER_LEVELS - 1)
				cwSelectLevel(0);
			else
				cwFieldAdjust(f, 1);
			JE_playSampleNum(S_CURSOR);
		}
		break;
	}
	default:
		cwPerformAction(n->id[cwSelCell], done, equipped);
		break;
	}
}

static void cwTypeText(int selField)
{
	if (selField == CWF_NAME)
	{
#ifndef PLATFORM_HANDHELD
		if ((lastkey_mod & (KMOD_CTRL | KMOD_GUI)) != 0)
			return;
		size_t len = strlen(customWeaponName);
		for (size_t i = 0; last_text[i] != '\0'; ++i)
		{
			const unsigned char c = (unsigned char)last_text[i];
			if (c >= 32 && c < 127 && len < sizeof(customWeaponName) - 1)
				customWeaponName[len++] = (char)c;
		}
		customWeaponName[len] = '\0';
		customWeaponMaterialize();
		cwHistoryRecord(cwMergeKey(CWF_NAME));
#endif
		return;
	}

	int lo, hi;
	if (selField < 0 || !cwFieldActive(selField) || !cwNumericRange(selField, &lo, &hi))
		return;
	for (size_t i = 0; last_text[i] != '\0'; ++i)
	{
		const char c = last_text[i];
		const bool digit = (c >= '0' && c <= '9');
		const bool minus = (c == '-' && lo < 0);
		if (!digit && !minus)
			continue;
		if (cwNumField != selField)
		{
			cwNumField = selField;
			cwNumText[0] = '\0';
		}
		const size_t len = strlen(cwNumText);
		if (minus)
		{
			if (len == 0)
				SDL_strlcpy(cwNumText, "-", sizeof(cwNumText));
		}
		else if (len < sizeof(cwNumText) - 1)
		{
			cwNumText[len] = c;
			cwNumText[len + 1] = '\0';
		}
	}
}

static void cwHandleKey(bool *done, bool *equipped)
{
	const int selField = cwSelectedField();
	const bool commandKey = (lastkey_mod & (KMOD_CTRL | KMOD_GUI)) != 0;
	const bool shift = (lastkey_mod & KMOD_SHIFT) != 0;

	if (commandKey && (lastkey_scan == SDL_SCANCODE_Z || lastkey_scan == SDL_SCANCODE_Y ||
	                   lastkey_scan == SDL_SCANCODE_S))
	{
		cwCommitNumeric();
		const int a = (lastkey_scan == SDL_SCANCODE_S) ? CWA_SAVE
		            : (lastkey_scan == SDL_SCANCODE_Y || shift) ? CWA_REDO : CWA_UNDO;
		cwPerformAction(a, done, equipped);
		return;
	}

	switch (lastkey_scan)
	{
	case SDL_SCANCODE_UP:
	case SDL_SCANCODE_DOWN:
		cwMoveSelection(lastkey_scan == SDL_SCANCODE_UP ? -1 : 1);
		JE_playSampleNum(S_CURSOR);
		break;
	case SDL_SCANCODE_TAB:
		cwCommitNumeric();
		cwSwitchPage(cwPage + (shift ? -1 : 1), cwNav[cwSelRow].kind != CWN_TABS);
		JE_playSampleNum(S_CURSOR);
		break;
	case SDL_SCANCODE_PAGEUP:
	case SDL_SCANCODE_PAGEDOWN:
	{
		cwCommitNumeric();
		const int level = customWeaponEditLevel + (lastkey_scan == SDL_SCANCODE_PAGEUP ? -1 : 1);
		if (level < 0 || level >= CUSTOM_POWER_LEVELS)
		{
			JE_playSampleNum(S_SPRING);
			break;
		}
		cwSelectLevel(level);
		JE_playSampleNum(S_CURSOR);
		break;
	}
	case SDL_SCANCODE_LEFTBRACKET:
	case SDL_SCANCODE_RIGHTBRACKET:
		if (selField == CWF_NAME)
			break;
		cwCommitNumeric();
		cwStepBullet(lastkey_scan == SDL_SCANCODE_RIGHTBRACKET ? 1 : -1);
		JE_playSampleNum(S_CURSOR);
		break;
	case SDL_SCANCODE_LEFT:
	case SDL_SCANCODE_RIGHT:
	{
		const int dir = (lastkey_scan == SDL_SCANCODE_RIGHT) ? 1 : -1;
		if (cwNumField >= 0)
		{
			cwCommitNumeric();
			JE_playSampleNum(S_SELECT);
			break;
		}
		const CwNav *const n = &cwNav[cwSelRow];
		if (n->kind == CWN_TABS)
		{
			cwSwitchPage(cwPage + dir, false);
			JE_playSampleNum(S_CURSOR);
		}
		else if (n->kind == CWN_FIELD)
		{
			int lo, hi;
			const bool coarse = cwNumericRange(selField, &lo, &hi) || selField == CWF_IMPORT_SRC;
			cwAdjustSelected(selField, (shift && coarse) ? dir * 10 : dir);
		}
		else if (cwSelCell + dir >= 0 && cwSelCell + dir < n->count)
		{
			cwSelect(cwSelRow, cwSelCell + dir);
			JE_playSampleNum(S_CURSOR);
		}
		break;
	}
	case SDL_SCANCODE_BACKSPACE:
		if (cwNumField >= 0)
		{
			const size_t len = strlen(cwNumText);
			if (len > 0)
				cwNumText[len - 1] = '\0';
			if (cwNumText[0] == '\0')
				cwNumField = -1;
		}
#ifndef PLATFORM_HANDHELD
		else if (selField == CWF_NAME)
		{
			const size_t len = strlen(customWeaponName);
			if (len > 0)
				customWeaponName[len - 1] = '\0';
			customWeaponMaterialize();
			cwHistoryRecord(cwMergeKey(CWF_NAME));
		}
#endif
		break;
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_SPACE:
		if (cwNumField >= 0)
		{
			cwCommitNumeric();
			JE_playSampleNum(S_SELECT);
		}
		else
		{
			cwActivate(done, equipped);
		}
		break;
	case SDL_SCANCODE_ESCAPE:
		if (cwNumField >= 0)
		{
			cwNumField = -1;
			cwNumText[0] = '\0';
			JE_playSampleNum(S_CURSOR);
		}
		else
		{
			*done = true;
		}
		break;
	default:
		break;
	}
}

static void cwHandleBoxPress(void)
{
	const int bar = cwChartHover();
	if (bar >= 0)
	{
		if (bar != customWeaponEditLevel)
		{
			cwSelectLevel(bar);
			JE_playSampleNum(S_CURSOR);
		}
		cwFlashRegion(cwBarX(bar), CW_BAR_TOP, cwBarX(bar) + CW_BAR_W - 1, CW_BAR_BOT);
		return;
	}
	if (mouse_y > CW_RANGE_Y1)
		return;

	if (cwDummyHit(mouse_x, mouse_y) >= 0)
	{
		cwDrag = CW_DRAG_TARGETS;
		cwDragDx = mouse_x - cwTargetX;
		cwDragDy = mouse_y - cwTargetY;
		JE_playSampleNum(S_CLICK);
	}
	else if (cwShipHit(mouse_x, mouse_y))
	{
		cwDrag = CW_DRAG_SHIP;
		cwDragDx = mouse_x - cwShipX;
		cwDragDy = mouse_y - cwShipY;
		JE_playSampleNum(S_CLICK);
	}
}

static void cwUpdateDrag(void)
{
	if (cwDrag == CW_DRAG_NONE)
		return;
	if (!mousedown)
	{
		cwDrag = CW_DRAG_NONE;
		return;
	}
	if (cwDrag == CW_DRAG_SHIP)
	{
		cwShipX = cwClamp(mouse_x - cwDragDx, CW_SHIP_X_MIN, CW_SHIP_X_MAX);
		cwShipY = cwClamp(mouse_y - cwDragDy, CW_SHIP_Y_MIN, CW_SHIP_Y_MAX);
	}
	else
	{
		cwTargetX = cwClamp(mouse_x - cwDragDx, CW_TARGET_X_MIN, CW_TARGET_X_MAX);
		cwTargetY = cwClamp(mouse_y - cwDragDy, cwTargetYMin(), cwTargetYMax());
		cwPlaceDummies();
	}
}

static void cwHandleMouse(int *prev_mx, int *prev_my, bool *done, bool *equipped)
{
	const bool inBox = mouse_x >= CW_BOX_X0 && mouse_x <= CW_BOX_X1 &&
	                   mouse_y >= CW_BOX_Y0 && mouse_y <= CW_BOX_Y1;

	if (mouse_scroll != 0)
	{
		if (inBox)
		{
			const int level =
			    cwClamp(customWeaponEditLevel - mouse_scroll, 0, CUSTOM_POWER_LEVELS - 1);
			if (level != customWeaponEditLevel)
			{
				cwSelectLevel(level);
				JE_playSampleNum(S_CURSOR);
			}
		}
		else
		{
			for (int i = 0; i < abs(mouse_scroll); ++i)
				cwMoveSelection(mouse_scroll > 0 ? -1 : 1);
			JE_playSampleNum(S_CURSOR);
		}
		mouse_scroll = 0;
	}

	int hoverRow, hoverCell;
	cwHitTestPanel(mouse_x, mouse_y, &hoverRow, &hoverCell);
	const bool moved = (mouse_x != *prev_mx || mouse_y != *prev_my);
	if (hoverRow >= 0 && moved && cwDrag == CW_DRAG_NONE)
	{
		const bool tabs = (cwNav[hoverRow].kind == CWN_TABS);
		const int cell = tabs ? cwPage : hoverCell;
		int prevRow, prevCell;
		cwHitTestPanel(*prev_mx, *prev_my, &prevRow, &prevCell);
		const bool newTab = tabs && (prevRow != hoverRow || prevCell != hoverCell);
		if (hoverRow != cwSelRow || cell != cwSelCell || newTab)
		{
			cwSelect(hoverRow, cell);
			JE_playSampleNum(S_CURSOR);
		}
	}
	*prev_mx = mouse_x;
	*prev_my = mouse_y;

	cwUpdateDrag();

	if (!newmouse)
		return;
	newmouse = false;
	cwCommitNumeric();

	const bool rightButton = (lastmouse_but == SDL_BUTTON_RIGHT);
	if (hoverRow >= 0 && cwNav[hoverRow].kind == CWN_FIELD)
	{
		const int f = cwNav[hoverRow].id[0];
		cwSelect(hoverRow, 0);
		if (f == CWF_NAME)
		{
#ifdef PLATFORM_HANDHELD
			if (!rightButton)
				cwRenameWithKeyboard();
#endif
			return;
		}
		const int click = rightButton ? -1 : 1;
		cwAdjustSelected(f, (mouse_x < CW_PAN_MID) ? -click : click);
		return;
	}

	if (rightButton)
	{
		*done = true;
		return;
	}

	if (hoverRow >= 0)
	{
		const CwNav *const n = &cwNav[hoverRow];
		if (n->kind == CWN_TABS)
		{
			if (hoverCell != cwPage)
			{
				cwSwitchPage(hoverCell, false);
				JE_playSampleNum(S_CURSOR);
			}
			cwSelect(0, cwPage);
			return;
		}
		cwSelect(hoverRow, hoverCell);
		cwPerformAction(n->id[hoverCell], done, equipped);
		return;
	}

	if (inBox)
		cwHandleBoxPress();
}

bool JE_weaponEditor(bool canEquip)
{
	// The title-screen Setup menu can reach this before item data is loaded.
	if (weaponPort[1].name[0] == '\0')
		JE_loadItemDat();
	else if (customWeaponPort <= 0)
		customWeaponInit();

	// The impact explosions need the explosion sprite sheet, which the game only loads lazily at
	// level start; it can be NULL here (editor opened from Setup, or the between-levels shop).
	if (explosionSpriteSheet.data == NULL)
		JE_loadCompShapes(&explosionSpriteSheet, '6');
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');

	customWeaponSelectLevel(0);
	customWeaponSelectMode(0);
	customWeaponMaterialize();

	cwImportSrc =
	    cwClamp(cwImportSrc, 0, (customBulletPresetCount > 0) ? customBulletPresetCount - 1 : 0);
	cwImportPwr = customBulletMaxPower(cwImportSrc);
	cwBulletSel = 0;
	cwCanEquip = canEquip;

	cwRevertBuf = malloc(CUSTOM_WEAPON_LIBRARY_WIRE_MAX);
	cwRevertLen = (cwRevertBuf != NULL)
	            ? customWeaponSerializeLibrary(cwRevertBuf, CUSTOM_WEAPON_LIBRARY_WIRE_MAX) : 0;
	cwDirty = false;

	const bool prevCentered = (video_get_menu_x_offset() != 0);
	set_menu_centered(true);

	Palette savedPalette;
	memcpy(savedPalette, colors, sizeof(Palette));

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	JE_loadPic(VGAScreen, 1, true);
	memcpy(VGAScreen2->pixels, VGAScreen->pixels, (size_t)VGAScreen2->pitch * VGAScreen2->h);

	cwClearPreviewShots();
	cwLayoutDummies();
	cwPreviewKey = -1;
	cwDrag = CW_DRAG_NONE;
	cwFlashStart_ms = 0;
	cwNumField = -1;
	cwNumText[0] = '\0';
	cwNoticeTicks = 0;
	cwArmed = -1;
	cwHistoryReset();
	cwBuildNav();
	rl_forget_previous();

	wait_noinput(false, false, true);
	newkey = newmouse = new_text = false;
	mouse_scroll = 0;
	mouseTwoFingerRightClick = true;

	const double tick_ms = 3.0 * get_delay_period();
	double nextTick_ms = SDL_GetTicks();
	Uint32 clock_ms = 0, clockLast_ms = SDL_GetTicks();
	int prev_mx = mouse_x, prev_my = mouse_y;
	bool done = false, equipped = false;

	while (!done)
	{
		// A design session lasts minutes. The peer needs the keep-alives, and this machine
		// still owes acknowledgements and adoption for whatever the partner does meanwhile.
		NETWORK_KEEP_ALIVE();
		while (network_shop_pump())
			;

		Uint32 now = SDL_GetTicks();
		if ((double)now >= nextTick_ms)
		{
			nextTick_ms =
			    ((double)now - nextTick_ms > 4.0 * tick_ms) ? now + tick_ms : nextTick_ms + tick_ms;
			cwSimTick();
			shipEditorServiceMusic();
		}
		clock_ms += now - clockLast_ms;
		clockLast_ms = now;

		cwBuildNav();
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);
		JE_rectangle(VGAScreen, CW_BOX_X0 - 1, CW_BOX_Y0 - 1, CW_BOX_X1 + 1, CW_BOX_Y1 + 1, C_HI);
		cwDrawPanel(clock_ms);
		{
			char hintBuf[96];
			draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, vga_height - 12,
			                    cwHintText(hintBuf, sizeof(hintBuf)), small_font, centered, 15, 2,
			                    false, 1);
		}
		cwDrawBoxBase();
		cwCaptureBoxBase();

		touch_ui_set_layout(TOUCH_LAYOUT_LIST);
		push_joysticks_as_keyboard();
		service_SDL_events(false);
		mouseCursor = MOUSE_POINTER_NORMAL;

		rl_set_ship_override(0, (float)(cwShipX - cwTickShipX), (float)(cwShipY - cwTickShipY));
		now = SDL_GetTicks();
		float alpha = 1.0f;
		if (smoothMotion && tick_ms > 0.0)
		{
			alpha = 1.0f - (float)((nextTick_ms - (double)now) / tick_ms);
			alpha = (alpha < 0.0f) ? 0.0f : (alpha > 1.0f) ? 1.0f : alpha;
		}
		cwPresentFrame(alpha, clock_ms);

		if (new_text)
			cwTypeText(cwSelectedField());
		new_text = false;

		cwHandleMouse(&prev_mx, &prev_my, &done, &equipped);

		if (newkey)
			cwHandleKey(&done, &equipped);
		newkey = false;

		if (!output_vsync)
			limit_render_fps();
	}

	for (int j = 0; j < MAX_EXPLOSIONS; ++j)
		explosions[j].ttl = 0;
	rl_set_ship_override(0, 0.0f, 0.0f);
	rl_set_ship_vel(0, 0, 0);
	shipEditorEndMusicFade();
	rl_forget_previous();
	mouseTwoFingerRightClick = false;
	touch_ui_clear_layout();
	wait_noinput(false, false, true);

	VGAScreen = temp_surface;
	set_menu_centered(prevCentered);
	memcpy(colors, savedPalette, sizeof(Palette));
	set_palette(colors, 0, 255);

	free(cwRevertBuf);
	cwRevertBuf = NULL;
	cwRevertLen = 0;

	customWeaponLibrarySave();
	save_opentyrian_config();
	return equipped;
}
