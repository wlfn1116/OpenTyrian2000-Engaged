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
#ifndef GAME_MENU_H
#define GAME_MENU_H

#include "helptext.h"
#include "opentyr.h"

typedef JE_byte JE_MenuChoiceType[MENU_MAX];

Sint64 JE_cashLeft(void);
uint JE_shopPlayerIndex(void);
void JE_itemScreen(void);

// QA hook for online outpost row mapping; writes a failure reason to detail.
bool game_menu_test_outpost_rows(char *detail, size_t detailSize);

// Release the shop preview's supersampled frames. Called from JE_tyrianShutdown.
void game_menu_deinit(void);

// Fill `out` with up to `maxOut` combat-level section numbers for `episode` (the same level
// scan the debug level picker uses); returns how many were written.
uint JE_getLevelSections(int episode, JE_byte *out, JE_byte *fileOut, uint maxOut);

// Look up the authored level name at (episode, section, fileNum) without loading the level,
// into `out` (<= outSize, set to "" if not found). fileNum 0 matches the section's first ']L';
// a non-zero fileNum distinguishes a section's alternate cut. Used by the endless Radar perk.
void JE_getLevelSectionName(int episode, JE_byte section, JE_byte fileNum, char *out, size_t outSize);

void load_cubes(void);
bool load_cube(int cube_slot, int cube_index);

void JE_drawItem(JE_byte itemType, JE_word itemNum, JE_word x, JE_word y);

// Item-list columns: the icon anchor, the label rows drawn beside it, and the last column a row
// may paint (the scroll-bar track is at 306).
#define SHOP_ITEM_ICON_X 160
#define SHOP_ITEM_NAME_X 185
#define SHOP_ITEM_COST_X 187
#define SHOP_ITEM_LIST_RIGHT 304

// Where the "already owned" marker starts, and how far short of it a row tag stops. The marker
// sprite paints from its own column, so a tag that ended there would run into the icon.
#define SHOP_ITEM_MARKER_X(scrollbar) ((scrollbar) ? 286 : 298)
#define SHOP_ROW_TAG_MARKER_GAP 4
#define SHOP_OWNED_MARKER_SPRITE 247  // its frame in shopSpriteSheet

// Marks a port with two fire modes. Only the rear weapon list draws it: the front bay always
// fires op[0], so the same port has no mode to toggle there.
#define SHOP_DUAL_MODE_TAG "Dual-Mode"

// Name the bay the shipped game issues a gun for. Only Endless stocks a list from both bays
// (its Primary/Secondary Gun categories), and it tags every classified row.
#define SHOP_FRONT_GUN_TAG "Front"
#define SHOP_REAR_GUN_TAG  "Rear"

// Help lines for those renamed rows. QA keeps them on the bar; a line past the ping band
// hides the row's online ping (save_help_bar_ping_band).
#define SHOP_PRIMARY_GUN_HELP   "Change or upgrade your primary weapon."
#define SHOP_SECONDARY_GUN_HELP "Your secondary gun. Dual-Mode guns can switch between modes."

// The stock Tyrian 2000 weapon table fills ports 1-47 with real weapons; ports 48-60 are dummy
// "Test" placeholders (see custom_weapon.c), so no shop offers a front or rear weapon above 47.
#define SHOP_REAL_WEAPON_PORTS 47

// Half the width of a hull drawn as two 2x2 halves: JE_drawItem sits each half this far either
// side of the anchor, so such a hull also overhangs the icon column by this much.
#define SHOP_WIDE_HULL_HALF 12

typedef struct
{
	int iconX, nameX, costX;
} ShopItemColumns;

// Columns for one ship row of the item list. A hull drawn as two 2x2 halves (the Nort Ship and
// the Dragonwing) is 48px wide against a 24px icon column, so it takes an anchor shifted right
// and a label column of its own; every other ship takes the standard columns.
ShopItemColumns shop_ship_item_columns(JE_word shipId);

// The bay the shipped game issues a weapon port for. Ports it issues to neither bay (None, the
// sidekick weapon table, a custom design) are unknown. See doc/notes.md#item-tables.
typedef enum
{
	SHOP_BAY_UNKNOWN,
	SHOP_BAY_FRONT,
	SHOP_BAY_REAR,
} ShopWeaponBay;

ShopWeaponBay shop_weapon_port_bay(JE_word port);

// The tag one weapon row prints after its cost, or NULL for none. `rearList` selects which of the
// two weapon menus is drawn, `mixedBays` whether its stock can hold guns issued for either bay.
const char *shop_weapon_row_tag(JE_word port, bool rearList, bool mixedBays);

// x of a row tag, from the right edge of the cost text, the tag's width and the marker column.
// A cost text wide enough to reach that column pushes the tag right of it.
int shop_row_tag_x(int costRight, int tagW, int markerX);

// x of a figure drawn flush right on the endless help bar after `text`; a description long enough
// to reach it pushes the figure right of its usual edge instead.
int help_bar_right_x(const char *text, const char *right);

void JE_drawMenuHeader(void);
void JE_drawMenuChoices(void);
void JE_updateNavScreen(void);
void JE_drawNavLines(JE_boolean dark);
void JE_drawLines(SDL_Surface *surface, JE_boolean dark);
void JE_drawDots(void);
void JE_drawPlanet(JE_byte planetNum);
void draw_ship_illustration(void);
void JE_scaleBitmap(SDL_Surface *dst, const SDL_Surface *src, int x1, int y1, int x2, int y2);
void JE_initWeaponView(void);
void JE_computeDots(void);
JE_integer JE_partWay(JE_integer start, JE_integer finish, JE_byte dots, JE_byte dist);
void JE_doShipSpecs(void);
void JE_drawMainMenuHelpText(void);
JE_boolean JE_saveRequest(JE_byte slot, const char *savename);
JE_boolean JE_quitRequest(void);
void JE_genItemMenu(JE_byte itemnum);
void JE_scaleInPicture(SDL_Surface *dst, const SDL_Surface *src);
void JE_drawScore(void);
void JE_menuFunction(JE_byte select);
bool JE_debugLevelSelect(void);

// The endless effect layer's control panel, opened from the debug menu: sector modifiers,
// personal buffs, perks and the zone-scaling readout, applied in place rather than launching a
// level.
void endlessDebugTuneScreen(void);

// The debug level browser drops straight into a level, skipping the campaign route that
// normally leads there.
bool debugLevelJumpTake(void);
void debugLevelJumpReturn(void);

// Stage and synchronize a debug-browser level choice for the next network launch.
// debugLevelPickGet() reports whether this machine has a browser pick staged (and what it is);
// debugLevelPickApply() adopts a peer's staged pick through the same path.
bool debugLevelPickGet(JE_byte *episode, JE_byte *section, JE_byte *fileNum);
void debugLevelPickApply(JE_byte episode, JE_byte section, JE_byte fileNum);
void debugLevelPickReset(void);

/* Stage and adopt the whole Endless debug block paired with a network level jump. */
bool endlessJumpPickGet(Uint8 *block);              // fills ENDLESS_DEBUG_BLOCK_SIZE bytes
void endlessJumpPickApply(const Uint8 *block, size_t len);
void endlessJumpPickStage(void);                    // capture the state this machine just applied
void endlessJumpPickReset(void);
bool JE_customWeaponCreator(bool canEquip);
void JE_drawShipSpecs(SDL_Surface *, SDL_Surface *);
void JE_weaponSimUpdate(void);
void JE_weaponViewFrame(void);

// Online Endless: the sector index the charting player committed to, or -1 for none.
extern int endlessCoopCourse;

#ifdef WITH_NETWORK
// The outpost's wait presentation, also used by the waits network.c owns.
void shopWaitNotice(const char *text, const char *detail, const char *hint);
void shopWaitFrame(void);
#endif

#endif // GAME_MENU_H
