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
#include "game_menu.h"

#include "backgrnd.h"
#include "config.h"
#include "crashlog.h"
#include "custom_episode.h"
#include "custom_weapon.h"
#include "debug_levels.h"
#include "debug_menu.h"
#include "draw.h"
#include "editship.h"
#include "endless.h"
#include "endless_internal.h"  // the QA auto-visit reaches the perk and course internals
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "game_loop.h"
#include "game_state.h"
#include "help.h"
#include "hud.h"
#include "item_prices.h"
#include "joystick.h"
#include "keyboard.h"
#include "level_end.h"
#include "level_load.h"
#include "loudness.h"
#include "mouse.h"
#include "musmast.h"
#include "net_style.h"
#include "network.h"
#include "palette.h"
#include "params.h"
#include "pcxmast.h"
#include "picload.h"
#include "player.h"
#include "qa.h"
#include "render_list.h"
#include "save_menu.h"
#include "savegame.h"
#include "sfx.h"
#include "shots.h"
#include "sparks.h"
#include "sprite.h"
#include "str_util.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"
#include "weapon_creator.h"

#include <assert.h>
#include <math.h>

enum
{
	MENU_FULL_GAME       =  0,
	MENU_UPGRADES        =  1,
	MENU_OPTIONS         =  2,
	MENU_PLAY_NEXT_LEVEL =  3,
	MENU_UPGRADE_SUB     =  4,
	MENU_KEYBOARD_CONFIG =  5,
	MENU_LOAD_SAVE       =  6,
	MENU_DATA_CUBES      =  7,
	MENU_DATA_CUBE_SUB   =  8,
	MENU_2_PLAYER_ARCADE =  9,
	MENU_1_PLAYER_ARCADE = 10,  // Also networked games.
	MENU_LIMITED_OPTIONS = 11,  // The options page minus Load Game; see OPT_* below.
	MENU_JOYSTICK_CONFIG = 12,
	MENU_SUPER_TYRIAN = 13,
	MENU_MOUSE_CONFIG = 14,  // T2000
	// Retired debug-level grid. Keep the hole because menu tables are indexed by these IDs.
	MENU_ESHOP = 16,
	MENU_PERKS = 17,
	// Online settings and their picker pages.
	MENU_ONLINE = 18,
	MENU_ONLINE_COLOR = 19,
	MENU_ONLINE_OPACITY = 20,
	MENU_ONLINE_HPBARS = 21,
};

// Center of the asymmetric monitor readout used by shop cash and Endless rank.
#define MENU_MONITOR_CENTER_X 77

// Online Options omits Load Game, shifting later rows up.
enum
{
	OPT_LOAD = 2, OPT_SAVE, OPT_MUSIC, OPT_SOUND, OPT_SENS, OPT_JOYSTICK, OPT_KEYBOARD,
	OPT_MOUSE, OPT_EXIT,
	OPTIONS_ROWS = OPT_EXIT - OPT_LOAD + 1,
};

// Baseline of an options row, matching JE_drawMenuChoices' pitch.
#define MENU_ROW_H 16
#define OPTIONS_ROW_Y(row) (38 + ((row) - 2) * MENU_ROW_H)

// Keep menu rows above the help line.
#define MENU_HELP_TEXT_Y 187

// Online color grid, rebuilt per visit because Endless reserves some palette banks.
#define ONLINE_COLOR_COLS     5
#define ONLINE_COLOR_CELLS    (NET_SHIP_COLORS + 2)   // Off, every bank, and Done
#define ONLINE_COLOR_X0     168
#define ONLINE_COLOR_Y0      46
#define ONLINE_COLOR_CELL_W  26
#define ONLINE_COLOR_CELL_H  24
#define ONLINE_COLOR_PITCH_X (ONLINE_COLOR_CELL_W + 2)
#define ONLINE_COLOR_PITCH_Y (ONLINE_COLOR_CELL_H + 3)
#define ONLINE_COLOR_LEGEND_Y 156
#define ONLINE_COLOR_LEGEND_GAP 70   // "You" to "Partner" along the legend row
// Six glyph rows and a one-pixel shadow.
#define ONLINE_COLOR_LABEL_H   7
// Non-black panel shades prevent the starfield from drawing over labels.
#define ONLINE_CELL_HI    0xfb
#define ONLINE_CELL_LO    0xf4
#define ONLINE_CELL_PANEL 0xf2

// Partner opacity rows: ship toggle, percentages, then Done.
#define ONLINE_OPACITY_STEPS ((NET_OPACITY_FULL - NET_OPACITY_MIN) / NET_OPACITY_STEP + 1)
#define ONLINE_OPACITY_TOGGLE_ROW 2   // the percentages below it are what it applies
#define ONLINE_OPACITY_FIRST_ROW  3
#define ONLINE_OPACITY_ROW_H  12
#define ONLINE_OPACITY_Y0     38
#define ONLINE_OPACITY_SWATCH_X   250
#define ONLINE_OPACITY_SWATCH_W    46
#define ONLINE_OPACITY_SWATCH_H     7
// Sample a bright hull over a dark, starred background.
#define ONLINE_OPACITY_HULL_SHADE  14
#define ONLINE_OPACITY_BACK_SHADE   2
#define ONLINE_OPACITY_STAR_SHADE  12

/*** Structs ***/
struct cube_struct
{
	char title[81];
	char header[13];
	int face_sprite;
	char text[90][36];
	int last_line;
};

/*** Globals ***/
static int joystick_config = 0; // which joystick is being configured in menu

static JE_word yLoc;
static JE_shortint yChg;
static JE_word datacube_scroll_from;
static bool datacube_smooth = false;
static int newPal, curPal, oldPal;
static JE_boolean quikSave;
static JE_byte oldMenu;
static JE_boolean backFromHelp;
static JE_integer lastDirection;
static JE_boolean firstMenu9, paletteChanged;
static JE_MenuChoiceType menuChoices;
static JE_integer col, colC;
static JE_byte lastCurSel;
static JE_integer curMenu;
static JE_boolean inputDetected;
static JE_byte curSel[MENU_MAX]; /* [1..maxmenu] */

// Load/save cursor row from the last slot picked, restored on each outpost visit so a
// quicksave keeps landing on the same slot across levels.
static JE_byte lastLoadSaveSel = 2;
static JE_byte curItemType, curItem, cursor;
static JE_boolean leftPower, rightPower, rightPowerAfford;
static JE_byte currentCube;
static uint shopPlayerIndex;

// Map a full Options row to the current page.
static int options_row(int fullRow)
{
	return (curMenu == MENU_LIMITED_OPTIONS) ? fullRow - 1 : fullRow;
}

// Recover the full Options row.
static int options_full_row(int row)
{
	return (curMenu == MENU_LIMITED_OPTIONS) ? row + 1 : row;
}

// Insert Customize above Options while keeping existing menu logic on stock row numbers.
static const struct { int menu, optionsRow, stockRows; } outpostMenus[] = {
	{ MENU_FULL_GAME,       5, 7 },
	{ MENU_2_PLAYER_ARCADE, 5, 6 },
	{ MENU_1_PLAYER_ARCADE, 3, 4 },
	{ MENU_SUPER_TYRIAN,    4, 5 },
};

// Match the menu chosen at the start of JE_itemScreen().
static int outpost_menu(void)
{
	if (superTyrian)
		return MENU_SUPER_TYRIAN;
	if ((isNetworkGame && !coop_mode_active()) || onePlayerAction)
		return MENU_1_PLAYER_ARCADE;
	if (split_arcade_mode())
		return MENU_2_PLAYER_ARCADE;
	return MENU_FULL_GAME;
}

// Return the online Customize row, or 0 when absent.
static int outpost_customize_row(int menu)
{
	if (!isNetworkGame)
		return 0;

	for (uint i = 0; i < COUNTOF(outpostMenus); ++i)
		if (outpostMenus[i].menu == menu)
			return outpostMenus[i].optionsRow;

	return 0;
}

// Where an offline row lands on the menu now drawn.
static int outpost_row(int menu, int stockRow)
{
	const int customize = outpost_customize_row(menu);
	return (customize != 0 && stockRow >= customize) ? stockRow + 1 : stockRow;
}

// Map a displayed row back to stock numbering; Customize maps to Options.
static int outpost_stock_row(int menu, int row)
{
	const int customize = outpost_customize_row(menu);
	return (customize != 0 && row > customize) ? row - 1 : row;
}

// Separator height before checking whether the menu still fits.
static int outpost_gap_px_raw(int menu, int row)
{
	if (menu == MENU_FULL_GAME)
		return (row >= outpost_row(menu, 7)) ? MENU_ROW_H : 0;

	if (menu == MENU_2_PLAYER_ARCADE)
		return (row > outpost_row(menu, 3) ? MENU_ROW_H : 0)
		     + (row > outpost_row(menu, 4) ? MENU_ROW_H : 0);

	return 0;
}

// Drop optional separators when added rows would reach the help text.
static bool outpost_gaps_drawn(int menu)
{
	const int last = menuChoices[menu];
	return OPTIONS_ROW_Y(last) + outpost_gap_px_raw(menu, last) + MENU_ROW_H <= MENU_HELP_TEXT_Y;
}

// Return the separator height only when it fits.
static int outpost_row_gap_px(int menu, int row)
{
	return outpost_gaps_drawn(menu) ? outpost_gap_px_raw(menu, row) : 0;
}

uint JE_shopPlayerIndex(void)
{
	return shopPlayerIndex;
}

// Online pickers preview the affected seat; other pages preview the shopper.
static uint shop_draw_seat(void)
{
	if (curMenu == MENU_ONLINE_COLOR)
		return netStyleLocalSeat();
	if (curMenu == MENU_ONLINE_OPACITY || curMenu == MENU_ONLINE_HPBARS)
		return 1u - netStyleLocalSeat();
	return shopPlayerIndex;
}

static Player *shopPlayer(void)
{
	return &player[shop_draw_seat()];
}

// MENU_PERKS serves both the forced choice and the read-only owned-perk list.
static bool endlessPerkListMode = false;
static int  perkListId[32];

// Owned-perk list scroll state. Rows are one-based.
static int perkListScrollTop = 1;
static int perkListPrevSel = 0;
#define PERK_LIST_VIS 14

static JE_boolean keyboardUsed;
static JE_byte weaponSimTime;

// Vertical scroll for the buy/sell sub-item list (MENU_UPGRADE_SUB) when it overflows:
// the 1-based first visible row; the window follows the selection.
static int upgradeSubScrollTop = 1;
static int upgradeSubPrevSel = 0;    // curSel last frame; the view follows only when it changes

// Flush the current joystick's bindings to opentyrian.cfg right away: the Switch HOME-menu
// exit never runs JE_tyrianHalt's config write.
static void save_joystick_config_now(void)
{
	if (joystick_config >= 0 && joystick_config < joysticks)
		save_joystick_assignments(&opentyrian_config, joystick_config);
	save_opentyrian_config();
}

// Overflow pages show five rows so None and Done stay on the same page.
static int upgradeSubVisibleRows(void)
{
	const int total_rows = menuChoices[MENU_UPGRADE_SUB] - 1;  // items (incl None) + DONE
	return total_rows <= 6 ? 6 : 5;
}

/* Touch arrows are needed only for outpost lists that can scroll beyond their window. */
static bool outpostListScrolls(void)
{
	if (curMenu == MENU_UPGRADE_SUB)
		return upgradeSubVisibleRows() < 6;

	if (curMenu == MENU_PERKS && endlessPerkListMode)
		return menuChoices[MENU_PERKS] - 1 > PERK_LIST_VIS;

	return false;
}

/* Show the rear-mode button only while its weapon-preview row is selected. */
static bool outpostRearModeCyclable(void)
{
	return curMenu == MENU_UPGRADE_SUB && curSel[MENU_UPGRADES] == 4
		&& weaponPort[shopPlayer()->items.weapon[REAR_WEAPON].id].opnum == 2;
}

static JE_byte planetAni, planetAniWait;
static JE_byte currentDotNum, currentDotWait;
static JE_real navX, navY, newNavX, newNavY;
static JE_integer tempNavX, tempNavY;
static JE_byte planetDots[5]; /* [1..5] */
static JE_integer planetDotX[5][10], planetDotY[5][10]; /* [1..5, 1..10] */

// Where the planet monitor was last shown (nav pan start) for display-rate
// interpolation of the lock-on scroll. See JE_navScreenSmoothPresent.
static JE_real nav_smooth_prev_x, nav_smooth_prev_y;
static void JE_drawNavMonitor(void);
static void JE_navScreenAdvance(void);
static void JE_navDrawFrame(JE_real dispNavX, JE_real dispNavY);
static void JE_navScreenSmoothPresent(void);
static void refresh_help_bar_ping(void);
static PlayerItems old_items[2];  // shared shop-entry snapshots

static struct cube_struct cube[4];

// Endless menu sizes are adjusted at runtime.
static const JE_MenuChoiceType menuChoicesDefault = { 9, 9, 9, 0, 0, 11, (SAVE_FILES_NUM / 2) + 2, 0, 0, 6, 4, 6, 7, 5, 6, 0, 7, 5, 0, 0, 0, 0 };

// Pickers return to Customize; its Esc handler returns to the active outpost.
static const JE_byte menuEsc[MENU_MAX] = { 0, 1, 1, 1, 2, 3, 3, 1, 8, 0, 0, 11, 3, 0, 3, 1, 1, 1,
                                           MENU_FULL_GAME + 1, MENU_ONLINE + 1, MENU_ONLINE + 1,
                                           MENU_ONLINE + 1 };
static const JE_byte itemAvailMap[7] = { 1, 2, 3, 9, 4, 6, 7 };
static const JE_word planetX[21] = { 200, 150, 240, 300, 270, 280, 320, 260, 220, 150, 160, 210, 80, 240, 220, 180, 310, 330, 150, 240, 200 };
static const JE_word planetY[21] = {  40,  90,  90,  80, 170,  30,  50, 130, 120, 150, 220, 200, 80,  50, 160,  10,  55,  55,  90,  90,  40 };
static const uint cube_line_chars = sizeof(*cube->text) - 1;
static const uint cube_line_width = 150;

/*** Functions ***/
static Uint8 *playeritem_map(PlayerItems *items, uint i)
{
	Uint8 *const map[] =
	{
		&items->ship,
		&items->weapon[FRONT_WEAPON].id,
		&items->weapon[REAR_WEAPON].id,
		&items->shield,
		&items->generator,
		&items->sidekick[LEFT_SIDEKICK],
		&items->sidekick[RIGHT_SIDEKICK],
	};
	assert(i < COUNTOF(map));
	return map[i];
}

static void stock_owned_items(PlayerItems *owned)
{
	for (int i = 0; i < 7; i++)
	{
		int item = *playeritem_map(owned, i);

		int slot = 0;
		for (; slot < itemAvailMax[itemAvailMap[i] - 1]; ++slot)
		{
			if (itemAvail[itemAvailMap[i] - 1][slot] == item)
				break;
		}

		if (slot == itemAvailMax[itemAvailMap[i] - 1])
		{
			itemAvail[itemAvailMap[i] - 1][slot] = item;
			itemAvailMax[itemAvailMap[i] - 1]++;
		}
	}
}

static void ensure_equipped_items_visible(void)
{
	/* Restore equipped items to inventory after debug loadout edits. */
	stock_owned_items(&shopPlayer()->items);
}

static void shop_stock_owned_items(void)
{
	stock_owned_items(endlessMode ? &shopPlayer()->items : &shopPlayer()->last_items);
}

/* Preview the post-purchase wallet; unaffordable rows may make it temporarily negative. */
Sint64 JE_cashLeft(void)
{
	Sint64 tempL = shopPlayer()->cash;

	// Only item rows 2 through 8 index playeritem_map.
	if (curSel[MENU_UPGRADES] < 2 || curSel[MENU_UPGRADES] > 8)
		return tempL;

	JE_word itemNum = *playeritem_map(&shopPlayer()->items, curSel[MENU_UPGRADES] - 2);

	tempL -= JE_getCost(curSel[MENU_UPGRADES], itemNum);

	tempW = 0;

	switch (curSel[MENU_UPGRADES])
	{
	case 3:
	case 4:
	{
		Sint64 base_cost = weaponPort[itemNum].cost;
		if (expertMode)
			base_cost = base_cost * expertUpgradeCostMult;  // matches the power-upgrade scaling in JE_getCost
		for (uint i = 1; i < shopPlayer()->items.weapon[curSel[MENU_UPGRADES] - 3].power; ++i)
			tempL -= weapon_upgrade_cost(base_cost, i);
		break;
	}
	}

	return tempL;
}

// Tick-drawn power gauge with a clean background saved for display-rate interpolation.
enum { PB_X0 = 141, PB_X1 = 149, PB_Y0 = 56, PB_Y1 = 146 };  // gauge slot (inclusive)

static Uint8 menu_power_bg[PB_Y1 - PB_Y0 + 1][PB_X1 - PB_X0 + 1];
static bool menu_power_bg_valid = false;
static int menu_power_prev = 500, menu_power_cur = 500;  // gauge value (0..900) at prev/cur tick

static void menu_power_capture_bg(SDL_Surface *s)
{
	const Uint8 *base = (const Uint8 *)s->pixels + PB_Y0 * s->pitch + PB_X0;
	for (int y = 0; y <= PB_Y1 - PB_Y0; y++)
		memcpy(menu_power_bg[y], base + y * s->pitch, PB_X1 - PB_X0 + 1);
	menu_power_bg_valid = true;
}

// Draw the generator gauge at the given level (0..900), restoring the captured
// background first so redrawing at a lower level leaves no stub.
static void draw_menu_power_bar(SDL_Surface *s, int power_value)
{
	if (power_value > 900)
		power_value = 900;
	else if (power_value < 0)
		power_value = 0;

	if (menu_power_bg_valid)  // erase the previous bar back to the clean interface
	{
		Uint8 *base = (Uint8 *)s->pixels + PB_Y0 * s->pitch + PB_X0;
		for (int y = 0; y <= PB_Y1 - PB_Y0; y++)
			memcpy(base + y * s->pitch, menu_power_bg[y], PB_X1 - PB_X0 + 1);
	}

	// Left/Right generator gradient: mirror the in-game horizontal ramp (shades 115..123).
	// Only the shading follows the setting; the preview keeps its own gauge position and size.
	if (gaugeGradGenerator == GAUGE_GRAD_LEFT || gaugeGradGenerator == GAUGE_GRAD_RIGHT)
	{
		// Squished 2px: at full power the bar top sits at PB_Y0+2 (2px lower than the vertical
		// modes), compressing the whole ramp into a 2px-shorter span; bottom stays at PB_Y1.
		const int barTop = PB_Y1 - power_value * (PB_Y1 - PB_Y0 - 2) / 900;
		for (int j = 0; j <= PB_X1 - PB_X0; j++)
		{
			const int off = (gaugeGradGenerator == GAUGE_GRAD_RIGHT) ? j : (PB_X1 - PB_X0 - j);
			fill_rectangle_xy(s, PB_X0 + j, barTop, PB_X0 + j, PB_Y1, (Uint8)(113 + 2 + off));
		}
		return;
	}

	int temp, temp2, temp3;

	for (temp = 147 - power_value / 10; temp <= 146; temp++)
	{
		temp2 = 113 + (146 - temp) / 9 + 2;
		temp3 = (temp + 1) % 6;
		if (temp3 == 1)
			temp2 += 3;
		else if (temp3 != 0)
			temp2 += 2;

		JE_pix(s, 141, temp, temp2 - 3);
		JE_pix(s, 142, temp, temp2 - 3);
		JE_pix(s, 143, temp, temp2 - 2);
		JE_pix(s, 144, temp, temp2 - 1);
		fill_rectangle_xy(s, 145, temp, 149, temp, temp2);
	}

	// Bright leading edge one row above the bar top.
	temp = 147 - power_value / 10;
	temp2 = 113 + (146 - temp) / 9 + 4;

	JE_pix(s, 141, temp - 1, temp2 - 1);
	JE_pix(s, 142, temp - 1, temp2 - 1);
	JE_pix(s, 143, temp - 1, temp2 - 1);
	JE_pix(s, 144, temp - 1, temp2 - 1);
	fill_rectangle_xy(s, 145, temp - 1, 149, temp - 1, temp2);
}

void (*weaponSimOverlayFn)(float alpha) = NULL;

// Preview region (inclusive), in VGAScreen coordinates.
enum { SIM_BOX_X0 = 8, SIM_BOX_Y0 = 8, SIM_BOX_X1 = 143, SIM_BOX_Y1 = 182 };

// Supersampled present: the NxN replay of the recorded frame, and the frame handed to
// present_hi. Kept allocated (like the gameplay hi buffers) and resized on a factor change.
static SDL_Surface *sim_hi_replay = NULL, *sim_hi_frame = NULL;
static SDL_Surface *datacube_hi_canvas = NULL;

void game_menu_deinit(void)
{
	SDL_FreeSurface(sim_hi_replay);
	sim_hi_replay = NULL;
	SDL_FreeSurface(sim_hi_frame);
	sim_hi_frame = NULL;
	SDL_FreeSurface(datacube_hi_canvas);
	datacube_hi_canvas = NULL;
}

// Present the preview box supersampled, so its ship, shots, and orbiting satellites sit at
// sub-pixel positions between ticks as they do in play.
static bool JE_weaponSimPresentHi(float alpha, int scale)
{
	SDL_Surface *const replay = video_ensure_scaled_surface(&sim_hi_replay, scale);
	SDL_Surface *const frame = video_ensure_scaled_surface(&sim_hi_frame, scale);
	if (replay == NULL || frame == NULL)
		return false;

	rl_replay_interp(replay, alpha, false, scale);

	// The menu around the preview is 1x art, so expanding it is exact; only the box gains
	// sub-pixel detail.
	const int x_offset = video_get_menu_x_offset();
	expand_frame_to_hi(video_compose_frame(), frame, scale);

	const int row_bytes = (SIM_BOX_X1 - SIM_BOX_X0 + 1) * scale,
	          rows = (SIM_BOX_Y1 - SIM_BOX_Y0 + 1) * scale;
	const Uint8 *src = (const Uint8 *)replay->pixels
	                 + (SIM_BOX_Y0 * scale) * replay->pitch + SIM_BOX_X0 * scale;
	Uint8 *dst = (Uint8 *)frame->pixels
	           + (SIM_BOX_Y0 * scale) * frame->pitch + (SIM_BOX_X0 + x_offset) * scale;
	for (int row = 0; row < rows; ++row)
	{
		memcpy(dst, src, (size_t)row_bytes);
		src += replay->pitch;
		dst += frame->pitch;
	}

	// The copy above erased whatever the caller drew onto the box after the 1x replay: the
	// generator gauge's left columns, the weapon creator's overlay.
	for (int y = SIM_BOX_Y0; y <= SIM_BOX_Y1; ++y)
	{
		const Uint8 *const ref = (const Uint8 *)game_screen->pixels + y * game_screen->pitch;
		const Uint8 *const live = (const Uint8 *)VGAScreenSeg->pixels + y * VGAScreenSeg->pitch;
		for (int x = SIM_BOX_X0; x <= SIM_BOX_X1; ++x)
		{
			if (live[x] == ref[x])
				continue;
			Uint8 *d = (Uint8 *)frame->pixels + (y * scale) * frame->pitch + (x + x_offset) * scale;
			for (int k = 0; k < scale; ++k, d += frame->pitch)
				memset(d, live[x], scale);
		}
	}

	// The cursor goes on last: the box copy above would otherwise erase it wherever it
	// overlaps the preview.
	JE_drawMouseToHiFrame(frame, scale, x_offset);
	present_hi(frame);
	return true;
}

// Replay the completed weapon-preview recording inside its preview box.
void JE_weaponSimSmoothPresent(void)
{
	// Smooth Motion off: present the already-drawn authoritative frame once and let
	// menuWaitWithSmoothCursor() wait out the tick; classic, non-interpolated.
	if (!smoothMotion)
	{
		if (weaponSimOverlayFn) weaponSimOverlayFn(1.0f);
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		return;
	}

	const float period = 3.0f * get_delay_period();  // setDelay(3) duration (ms)

	// Present interpolated frames for the whole tick, paced by the menu's own setDelay()
	// timer; alpha sweeps 0..1 and the wait_delay() after us becomes a no-op.
	for (;;)
	{
		const Uint32 remaining = getDelayTicks();  // ms until the setDelay(3) target
		if (remaining == 0 || period <= 0.0f)
			break;

		float alpha = 1.0f - (float)remaining / period;
		if (alpha < 0.0f)
			alpha = 0.0f;
		else if (alpha > 1.0f)
			alpha = 1.0f;

		// Reconstruct the recorded frame at interpolated positions into game_screen, then copy just
		// the preview region over the live menu surface.
		rl_replay_interp(game_screen, alpha, false, 1);

		const Uint8 *src = (const Uint8 *)game_screen->pixels
		                 + SIM_BOX_Y0 * game_screen->pitch + SIM_BOX_X0;
		Uint8 *dst = (Uint8 *)VGAScreenSeg->pixels
		           + SIM_BOX_Y0 * VGAScreenSeg->pitch + SIM_BOX_X0;
		for (int row = SIM_BOX_Y0; row <= SIM_BOX_Y1; ++row)
		{
			memcpy(dst, src, SIM_BOX_X1 - SIM_BOX_X0 + 1);
			src += game_screen->pitch;
			dst += VGAScreenSeg->pitch;
		}

		// The gauge sits partly outside the copied region and is a non-blit overlay,
		// so redraw it here at the interpolated level.
		if (menu_power_bg_valid)
		{
			float level = (float)menu_power_prev + ((float)menu_power_cur - (float)menu_power_prev) * alpha;
			draw_menu_power_bar(VGAScreenSeg, (int)(level + 0.5f));
		}

		if (weaponSimOverlayFn) weaponSimOverlayFn(alpha);
		JE_mouseStart();

		// Sub-pixel preview when supersampling is on, classic present otherwise.
		const int ss = effective_supersample();
		if (ss <= 1 || !JE_weaponSimPresentHi(alpha, ss))
			JE_showVGA();

		JE_mouseReplace();

		if (!output_vsync)
			limit_render_fps();
		service_SDL_events(false);
	}
}

// Re-present ordinary menus at the display rate for the rest of the tick.
void menuWaitWithSmoothCursor(void)
{
	for (;;)
	{
		if (getDelayTicks() == 0)
			break;
		JE_mouseStart();   // services SDL events + draws the cursor at its live pos
		JE_showVGA();
		JE_mouseReplace(); // restore the pixels under the cursor for the next pass
		if (!output_vsync)
			limit_render_fps();
	}
}

static void datacube_text_hi(SDL_Surface *dst, int x, float y, const char *s, int scale)
{
	const int yh = (int)lrintf(y * scale);
	int bright = 0;

	for (int i = 0; s[i] != '\0'; ++i)
	{
		const int sprite_id = font_ascii[(unsigned char)s[i]];

		if (s[i] == ' ')
		{
			x += 6;
		}
		else if (s[i] == '~')
		{
			bright = (bright == 0) ? 4 : 0;
		}
		else if (sprite_id != -1 && sprite_exists(TINY_FONT, sprite_id))
		{
			blit_sprite_table_scaled(dst, (x + 1) * scale, yh + scale, TINY_FONT, sprite_id, scale,
			                         BLITT_DARK, 0, 0, false);
			blit_sprite_table_scaled(dst, x * scale, yh, TINY_FONT, sprite_id, scale,
			                         BLITT_HV_UNSAFE, 14, (Sint8)(3 + bright), false);
			x += sprite(TINY_FONT, sprite_id)->width + 1;
		}
	}
}

static bool datacube_present_hi(const struct cube_struct *c, float y_loc, int scale)
{
	SDL_Surface *const canvas = video_ensure_scaled_surface(&datacube_hi_canvas, scale);
	SDL_Surface *const frame = video_ensure_scaled_surface(&sim_hi_frame, scale);
	if (canvas == NULL || frame == NULL)
		return false;

	fill_rectangle_xy(canvas, 160 * scale, 49 * scale, 311 * scale - 1, 157 * scale - 1, 228);
	const int first = (int)(y_loc / 12.0f);
	float y = 50.0f - (y_loc - 12.0f * (float)first);
	for (int line = first + 1; line <= first + 10 && line <= c->last_line; ++line)
	{
		datacube_text_hi(canvas, 161, y, c->text[line - 1], scale);
		y += 12.0f;
	}

	const int x_offset = video_get_menu_x_offset();
	expand_frame_to_hi(video_compose_frame(), frame, scale);
	for (int row = 49 * scale; row < 157 * scale; ++row)
		memcpy((Uint8 *)frame->pixels + row * frame->pitch + (160 + x_offset) * scale,
		       (const Uint8 *)canvas->pixels + row * canvas->pitch + 160 * scale,
		       (size_t)(151 * scale));

	JE_drawMouseToHiFrame(frame, scale, x_offset);
	present_hi(frame);
	return true;
}

static void datacube_smooth_present(const struct cube_struct *c, JE_word from, JE_word to)
{
	const float period = get_delay_period();
	Uint32 remaining;
	do
	{
		remaining = getDelayTicks();
		float alpha = (period > 0.0f) ? 1.0f - (float)remaining / period : 1.0f;
		if (alpha < 0.0f)
			alpha = 0.0f;
		else if (alpha > 1.0f)
			alpha = 1.0f;

		JE_mouseStart();
		const int ss = effective_supersample();
		const float y_loc = (float)from + ((float)to - (float)from) * alpha;
		if (ss <= 1 || !datacube_present_hi(c, y_loc, ss))
			JE_showVGA();
		JE_mouseReplace();

		if (!output_vsync)
			limit_render_fps();
		service_SDL_events(false);
	}
	while (remaining > 0);
}

/* Add or remove the two Debug Mode rows from the buy/sell menu. */
static void configure_buysell_debug_menu(void)
{
	static char quitLabel[sizeof(menuInt[1][6])];
	static bool quitLabelSaved = false;

	if (!quitLabelSaved)
	{
		// menuInt[1][6] holds the stock "Quit Game" entry as loaded from the
		// game's menu text (item 7 in the unmodified buy/sell menu).
		SDL_strlcpy(quitLabel, menuInt[1][6], sizeof(quitLabel));
		quitLabelSaved = true;
	}

	if (debugMode)
	{
		menuChoices[MENU_FULL_GAME] = 9;
		SDL_strlcpy(menuInt[1][6], "Debug Menu", sizeof(menuInt[1][6]));
		SDL_strlcpy(menuInt[1][7], "Debug Level", sizeof(menuInt[1][7]));
		SDL_strlcpy(menuInt[1][8], quitLabel, sizeof(menuInt[1][8]));
	}
	else
	{
		menuChoices[MENU_FULL_GAME] = 7;
		SDL_strlcpy(menuInt[1][6], quitLabel, sizeof(menuInt[1][6]));
	}
}

/* Install Endless shop labels and restore captured campaign labels when leaving. */
static void configure_endless_shop_menu(void)
{
	static char stockCubes[sizeof(menuInt[1][1])];
	static char stockSpecs[sizeof(menuInt[1][2])];
	static char stockFront[sizeof(menuInt[2][2])];
	static char stockRear[sizeof(menuInt[2][3])];
	static bool stockSaved = false;

	if (!stockSaved)
	{
		SDL_strlcpy(stockCubes, menuInt[1][1], sizeof(stockCubes));
		SDL_strlcpy(stockSpecs, menuInt[1][2], sizeof(stockSpecs));
		SDL_strlcpy(stockFront, menuInt[2][2], sizeof(stockFront));
		SDL_strlcpy(stockRear,  menuInt[2][3], sizeof(stockRear));
		stockSaved = true;
	}

	if (endlessMode)
	{
		// Front buy/sell menu: item 2 opens the E-Shop; item 3 opens the read-only Perks list
		// (endless has no use for Ship Specs; your run-persistent perks matter more).
		SDL_strlcpy(menuInt[1][1], "E-Shop", sizeof(menuInt[1][1]));
		SDL_strlcpy(menuInt[1][2], "Perks", sizeof(menuInt[1][2]));

		// These rows also feed the category header and the item-list title (JE_genItemMenu).
		// The category names the slot; each row's tag names the gun's issuing bay.
		SDL_strlcpy(menuInt[2][2], "Primary Gun", sizeof(menuInt[2][2]));
		SDL_strlcpy(menuInt[2][3], "Secondary Gun", sizeof(menuInt[2][3]));

		// E-Shop submenu (menuInt row MENU_ESHOP+1): title, 11 buys, Done. Names only; the
		// exact cost of each buy is shown in the help line at the bottom (see the help block).
		char (*e)[24] = menuInt[MENU_ESHOP + 1];
		SDL_strlcpy(e[0], "Endless Shop", sizeof(e[0]));
		// Keep E-Shop rows grouped by display category.
		SDL_strlcpy(e[1], "Buy Shop Reroll", sizeof(e[1]));
		SDL_strlcpy(e[2], endlessCleanseMaxed() ? "Sabotage Maxed" : "Buy Sector Sabotage", sizeof(e[2]));
		SDL_strlcpy(e[3], endlessHullMaxed() ? "Hull Maxed" : "Buy Reinforce", sizeof(e[3]));
		SDL_strlcpy(e[4], "Buy Extra Perk", sizeof(e[4]));
		SDL_strlcpy(e[5], "Buy Special Weapon", sizeof(e[5]));
		SDL_strlcpy(e[6], endlessBuffKindBought() == ENDLESS_BUFF_KIND_TURBODRIVE ? "Turbodrive ON" : "Buy Turbodrive", sizeof(e[6]));
		SDL_strlcpy(e[7], endlessBuffKindBought() == ENDLESS_BUFF_KIND_OVERBLAST ? "Overblast ON" : "Buy Overblast", sizeof(e[7]));
		SDL_strlcpy(e[8], endlessBuffKindBought() == ENDLESS_BUFF_KIND_OVERDRIVE ? "Overdrive ON" : "Buy Overdrive", sizeof(e[8]));
		SDL_strlcpy(e[9], endlessReviveArmed() ? "Revive Ready" : "Buy Revive", sizeof(e[9]));
		SDL_strlcpy(e[10], endlessBombFull() ? "Bombs Full" : "Buy Bomb", sizeof(e[10]));
		SDL_strlcpy(e[11], "Buy Gamble", sizeof(e[11]));
		SDL_strlcpy(e[12], "Done", sizeof(e[12]));
		menuChoices[MENU_ESHOP] = 13;
	}
	else
	{
		SDL_strlcpy(menuInt[1][1], stockCubes, sizeof(menuInt[1][1]));
		SDL_strlcpy(menuInt[1][2], stockSpecs, sizeof(menuInt[1][2]));
		SDL_strlcpy(menuInt[2][2], stockFront, sizeof(menuInt[2][2]));
		SDL_strlcpy(menuInt[2][3], stockRear,  sizeof(menuInt[2][3]));
	}
}

/* Populate MENU_PERKS from the current offers, followed by Take the Cash. */
static void configure_endless_perk_menu(void)
{
	char (*p)[24] = menuInt[MENU_PERKS + 1];
	const int n = endlessPerkChoiceCount();

	SDL_strlcpy(p[0], "Choose a Perk", sizeof(p[0]));
	for (int i = 0; i < n; ++i)
		SDL_strlcpy(p[i + 1], endlessPerkChoiceName(i), sizeof(p[i + 1]));
	SDL_strlcpy(p[n + 1], "Take the Cash", sizeof(p[n + 1]));

	menuChoices[MENU_PERKS] = n + 2;  // rows 2..(n+1) = perks; row (n+2) = decline
}

void shopOpenPendingPerkPick(void)
{
	endlessPerkListMode = false;
	configure_endless_perk_menu();
	curSel[MENU_PERKS] = 2;
	curMenu = MENU_PERKS;
}

// Populate the scrolling, read-only perk list shown in place of Ship Specs.
static void configure_endless_perk_list_menu(void)
{
	SDL_strlcpy(menuInt[MENU_PERKS + 1][0], "Perks", sizeof(menuInt[MENU_PERKS + 1][0]));

	// Perks are personal, so the list is this player's own collection.
	const int total = endlessPerkCount();
	const int cap   = (int)(sizeof(perkListId) / sizeof(perkListId[0]));
	int n = 0;
	for (int id = 0; id < total && n < cap; ++id)
		if (endlessPerkEffective(endlessEconomyIndex(), id) > 0)
			perkListId[n++] = id;

	if (n == 0)              // nothing earned yet: a single "(none yet)" info row
		perkListId[n++] = -1;

	menuChoices[MENU_PERKS] = n + 2;  // rows 2..(n+1) = perk/info lines; row (n+2) = Done

	perkListScrollTop = 1;  // open at the top of the list
	perkListPrevSel   = 0;  // force the window to re-follow the selection on the first draw
}

/* Return the 1-based Radar reroll row, or 0 when unavailable. */
static int endlessCourseRerollRow(void)
{
	return endlessCourseRerollOffered() ? endlessCourseCount() + 2 : 0;
}

/* Populate MENU_PLAY_NEXT_LEVEL from the endless course slate: one planet per route, then the
 * reroll row when it is offered, then Exit. Called on entry and again after a reroll. */
static void configure_endless_course_menu(void)
{
	char (*p)[24] = menuInt[MENU_PLAY_NEXT_LEVEL + 1];
	const int n = endlessCourseCount();

	mapPNum = (JE_byte)n;
	for (int i = 0; i < n; ++i)
	{
		mapPlanet[i] = endlessCoursePlanet(i);
		mapSection[i] = endlessCourseSection(i);
	}
	JE_computeDots();
	navX = planetX[mapOrigin - 1];
	navY = planetY[mapOrigin - 1];
	newNavX = navX;
	newNavY = navY;

	int row = 0;
	SDL_strlcpy(p[row], "Chart a Course", sizeof(p[row]));
	for (int i = 0; i < n; ++i)
		SDL_strlcpy(p[++row], endlessCourseName(i), sizeof(p[row]));
	if (endlessCourseRerollRow() != 0)
		SDL_strlcpy(p[++row], "Reroll", sizeof(p[row]));
	SDL_strlcpy(p[++row], miscText[5], sizeof(p[row]));

	menuChoices[MENU_PLAY_NEXT_LEVEL] = row + 1;
	curSel[MENU_PLAY_NEXT_LEVEL] = 2;
}

/* Draw the scrolling, read-only Endless perk list from perkListId[]. */
static void draw_endless_perk_list(void)
{
	const int total_rows = menuChoices[MENU_PERKS] - 1;   // perk/info rows + Done; tempW = 1..total_rows
	const int sel_row    = curSel[MENU_PERKS] - 1;        // tempW of the selected row
	const int visible    = PERK_LIST_VIS;

	int max_top = total_rows - visible + 1;
	if (max_top < 1)
		max_top = 1;
	if (perkListScrollTop > max_top)
		perkListScrollTop = max_top;
	if (perkListScrollTop < 1)
		perkListScrollTop = 1;
	// Follow the selection only when it just moved (keyboard / mouse), keeping the selected row visible.
	if (curSel[MENU_PERKS] != perkListPrevSel)
	{
		if (sel_row < perkListScrollTop)
			perkListScrollTop = sel_row;
		else if (sel_row > perkListScrollTop + visible - 1)
			perkListScrollTop = sel_row - visible + 1;
		perkListPrevSel = curSel[MENU_PERKS];
	}

	for (int tempW = 1; tempW <= total_rows; ++tempW)
	{
		if (tempW < perkListScrollTop || tempW >= perkListScrollTop + visible)
			continue;  // scrolled out of view
		const int tempY = 38 + (tempW - perkListScrollTop) * 10;

		const bool doneRow = (tempW == total_rows);
		const int  id      = doneRow ? -1 : perkListId[tempW - 1];  // -1 also flags the "(none yet)" info row

		char rowStr[32];
		if (doneRow)
			SDL_strlcpy(rowStr, "Done", sizeof(rowStr));
		else
			SDL_strlcpy(rowStr, id >= 0 ? endlessPerkName(id) : "(none yet)", sizeof(rowStr));

		const bool selected = (tempW == sel_row);

		char line[34];
		if (selected)  // leading '~' toggles the highlight, as in JE_drawMenuChoices
		{
			line[0] = '~';
			SDL_strlcpy(line + 1, rowStr, sizeof(line) - 1);
		}
		else
			SDL_strlcpy(line, rowStr, sizeof(line));

		JE_outTextAndDarken(VGAScreen, 166, tempY, line, 15, 2, TINY_FONT);

		// Owned/max stack count (e.g. "2/5"), right-aligned just left of the scroll-bar column.
		if (id >= 0)
		{
			char cnt[16];
			snprintf(cnt, sizeof(cnt), "%d/%d",
			         endlessPerkEffective(endlessEconomyIndex(), id), endlessPerkMaxStack(id));

			char cline[18];
			if (selected)  // match the row's highlight so the count brightens with its name
			{
				cline[0] = '~';
				SDL_strlcpy(cline + 1, cnt, sizeof(cline) - 1);
			}
			else
				SDL_strlcpy(cline, cnt, sizeof(cline));

			const int count_right = 302;  // a few px left of the scroll-bar track (x=306)
			JE_outTextAndDarken(VGAScreen, count_right - JE_textWidth(cnt, TINY_FONT), tempY, cline, 15, 2, TINY_FONT);
		}
	}

	// Scroll bar (track + thumb), shown only when the list overflows one page.
	if (total_rows > visible)
	{
		const int track_x  = 306;
		const int track_y0 = 38;
		const int track_y1 = 38 + visible * 10 - 4;
		const int track_h  = track_y1 - track_y0;
		int thumb_h = track_h * visible / total_rows;
		if (thumb_h < 6)
			thumb_h = 6;
		const int thumb_y = track_y0 + (track_h - thumb_h) * (perkListScrollTop - 1) / (max_top - 1);
		fill_rectangle_xy(VGAScreen, track_x, track_y0, track_x + 2, track_y1, 241);
		fill_rectangle_xy(VGAScreen, track_x, thumb_y, track_x + 2, thumb_y + thumb_h, 252);
	}
}

/* Insert debug rows before Quit and preserve the stock label for restoration.
 * menuIntIdx selects the menuInt row; defaultChoices includes the original Quit row. */
static void configure_mode_debug_menu(int menu, int menuIntIdx, int defaultChoices,
                                      char *savedQuit, bool *quitSaved)
{
	const size_t entrySize = sizeof(menuInt[0][0]);

	if (!*quitSaved)
	{
		SDL_strlcpy(savedQuit, menuInt[menuIntIdx][defaultChoices - 1], entrySize);
		*quitSaved = true;
	}

	if (debugMode)
	{
		menuChoices[menu] = defaultChoices + 2;
		SDL_strlcpy(menuInt[menuIntIdx][defaultChoices - 1], "Debug Menu",  entrySize);
		SDL_strlcpy(menuInt[menuIntIdx][defaultChoices],     "Debug Level", entrySize);
		SDL_strlcpy(menuInt[menuIntIdx][defaultChoices + 1], savedQuit,     entrySize);
	}
	else
	{
		menuChoices[menu] = defaultChoices;
		SDL_strlcpy(menuInt[menuIntIdx][defaultChoices - 1], savedQuit, entrySize);
	}
}

/* Set up the Debug Mode entries for the arcade / Super Tyrian mode menus (these use
 * their menuChoicesDefault item counts; the buy/sell menu is special-cased). */
static void configure_arcade_debug_menus(void)
{
	static char quit1P[sizeof(menuInt[0][0])], quit2P[sizeof(menuInt[0][0])], quitST[sizeof(menuInt[0][0])];
	static bool saved1P = false, saved2P = false, savedST = false;

	configure_mode_debug_menu(MENU_1_PLAYER_ARCADE, MENU_1_PLAYER_ARCADE + 1, 4, quit1P, &saved1P);
	configure_mode_debug_menu(MENU_2_PLAYER_ARCADE, MENU_2_PLAYER_ARCADE + 1, 6, quit2P, &saved2P);
	configure_mode_debug_menu(MENU_SUPER_TYRIAN,    MENU_SUPER_TYRIAN + 1,    5, quitST, &savedST);
}

/* Toggle the Custom row and move Done between items 9 and 10. */
static void configure_custom_weapon_menu(void)
{
	const size_t entrySize = sizeof(menuInt[0][0]);
	static char savedDone[sizeof(menuInt[0][0])];
	static bool savedDoneValid = false;

	if (!savedDoneValid)
	{
		SDL_strlcpy(savedDone, menuInt[2][8], entrySize);  // stock "Done" (item 9)
		savedDoneValid = true;
	}

	if (customWeaponEnabled)
	{
		menuChoices[MENU_UPGRADES] = 10;
		SDL_strlcpy(menuInt[2][8], "Custom", entrySize);   // item 9
		SDL_strlcpy(menuInt[2][9], savedDone, entrySize);  // item 10 = Done
	}
	else
	{
		menuChoices[MENU_UPGRADES] = 9;
		SDL_strlcpy(menuInt[2][8], savedDone, entrySize);  // restore Done at item 9
	}
}

/* Insert Sensitivity after Sound Volume in the options menu, then build the online page from it.
 * Shift labels once; menuChoices resets on each JE_itemScreen entry. */
static void configure_options_sens_menu(void)
{
	const size_t entrySize = sizeof(menuInt[0][0]);
	static bool shifted = false;

	if (!shifted)
	{
		// MENU_OPTIONS (menuInt[3]): items 6..9 (labels [5]..[8]) move down to 7..10 ([6]..[9]).
		SDL_strlcpy(menuInt[3][9], menuInt[3][8], entrySize);  // Exit
		SDL_strlcpy(menuInt[3][8], menuInt[3][7], entrySize);  // Mouse Setup
		SDL_strlcpy(menuInt[3][7], menuInt[3][6], entrySize);  // Keyboard Setup
		SDL_strlcpy(menuInt[3][6], menuInt[3][5], entrySize);  // Joystick Setup
		SDL_strlcpy(menuInt[3][5], "Sens", entrySize);  // new item 6

		// Online Options omits Load Game; every row below it moves up one.
		SDL_strlcpy(menuInt[12][0], menuInt[3][0], entrySize);  // title
		for (int i = 1; i < OPTIONS_ROWS; ++i)
			SDL_strlcpy(menuInt[12][i], menuInt[3][i + 1], entrySize);

		shifted = true;
	}

	menuChoices[MENU_OPTIONS] = OPTIONS_ROWS + 1;          // rows 2..10
	menuChoices[MENU_LIMITED_OPTIONS] = OPTIONS_ROWS;      // rows 2..9, one shorter for Load Game
}

// First and last cells both use NONE for Off and Done; position distinguishes them.
static int online_color_cell[ONLINE_COLOR_CELLS];
static int online_color_cells = 2;

static void configure_online_color_grid(void)
{
	online_color_cells = 0;
	online_color_cell[online_color_cells++] = NET_SHIP_COLOR_NONE;   // Off

	for (int color = 1; color <= NET_SHIP_COLORS; ++color)
		if (!netStyleColorReserved(color))
			online_color_cell[online_color_cells++] = color;

	online_color_cell[online_color_cells++] = NET_SHIP_COLOR_NONE;   // Done
	menuChoices[MENU_ONLINE_COLOR] = (JE_byte)(online_color_cells + 1);
}

// Which cell a dye sits in, or the Off cell when this visit does not offer it.
static int online_color_find_cell(int color)
{
	for (int cell = 1; cell < online_color_cells - 1; ++cell)
		if (online_color_cell[cell] == color)
			return cell;
	return 0;
}

static void online_color_cell_rect(int cell, int *x0, int *y0, int *x1, int *y1)
{
	*x0 = ONLINE_COLOR_X0 + (cell % ONLINE_COLOR_COLS) * ONLINE_COLOR_PITCH_X;
	*y0 = ONLINE_COLOR_Y0 + (cell / ONLINE_COLOR_COLS) * ONLINE_COLOR_PITCH_Y;
	*x1 = *x0 + ONLINE_COLOR_CELL_W - 1;
	*y1 = *y0 + ONLINE_COLOR_CELL_H - 1;
}

// Wrap through cell order because reserved colors may leave the last row short.
static JE_byte online_color_step(JE_byte sel, int step)
{
	int cell = (int)sel - 2 + step;
	while (cell < 0)
		cell += online_color_cells;
	return (JE_byte)(2 + cell % online_color_cells);
}

// Linked Arcade omits HP Bars because its shared HUD already shows both ships.
enum { ONLINE_ROW_COLOR = 2, ONLINE_ROW_OPACITY, ONLINE_ROW_HPBARS, ONLINE_ROW_EXIT };

static bool online_hp_bars_offered(void)
{
	return !split_arcade_mode();
}

static int online_full_row(int row)
{
	return (!online_hp_bars_offered() && row >= ONLINE_ROW_HPBARS) ? row + 1 : row;
}

// Store and announce this seat's view so either player can host a resume.
static void online_set_local_view(NetShipView view)
{
	const NetShipView was = netStyleLocalView();
	netStyleSetLocalView(view);

	const NetShipView now = netStyleLocalView();
	if (now.opacity != was.opacity || now.shipOpacity != was.shipOpacity || now.hpBars != was.hpBars)
		network_player_look_publish();  // publish immediately instead of waiting for keep-alive
}

static void draw_online_hpbars_page(void)
{
	const int cursor = curSel[MENU_ONLINE_HPBARS];
	if (cursor >= 2 && cursor < menuChoices[MENU_ONLINE_HPBARS])
	{
		NetShipView view = netStyleLocalView();
		view.hpBars = (Uint8)(cursor - 2);
		online_set_local_view(view);
	}

	JE_drawMenuChoices();

	// Match the in-level view; the simulator draws the bars.
	netStylePreviewSet(netStylePeerColor(), netStyleLocalView().opacity);
}

// Value pickers confirm with Done; the Online page retains its Exit row.
#define ONLINE_PICKER_DONE "Done"

// Run after debug-row setup, with menuChoices reset to stock counts.
static void configure_outpost_customize_menu(void)
{
	for (uint i = 0; i < COUNTOF(outpostMenus); ++i)
		if (outpost_customize_row(outpostMenus[i].menu) != 0)
			++menuChoices[outpostMenus[i].menu];
}

// Generate Online menus because tyrian.hdt has no entries for them.
static void configure_online_menus(void)
{
	const size_t entrySize = sizeof(menuInt[0][0]);
	char (*online)[24] = menuInt[MENU_ONLINE + 1];

	SDL_strlcpy(online[0], "Customize", entrySize);

	int row = 1;
	SDL_strlcpy(online[row++], "Ship Color", entrySize);
	SDL_strlcpy(online[row++], "Partner Opacity", entrySize);
	if (online_hp_bars_offered())
		SDL_strlcpy(online[row++], "Partner HP Bars", entrySize);
	SDL_strlcpy(online[row], menuInt[3][OPTIONS_ROWS], entrySize);  // the options page's own Exit
	menuChoices[MENU_ONLINE] = (JE_byte)(row + 1);

	SDL_strlcpy(menuInt[MENU_ONLINE_COLOR + 1][0], "Ship Color", entrySize);
	SDL_strlcpy(menuInt[MENU_ONLINE_OPACITY + 1][0], "Opacity", entrySize);

	char (*bars)[24] = menuInt[MENU_ONLINE_HPBARS + 1];
	SDL_strlcpy(bars[0], "HP Bars", entrySize);
	SDL_strlcpy(bars[1], "Off", entrySize);
	SDL_strlcpy(bars[2], "On Hit", entrySize);
	SDL_strlcpy(bars[3], "Always", entrySize);
	SDL_strlcpy(bars[4], ONLINE_PICKER_DONE, entrySize);
	menuChoices[MENU_ONLINE_HPBARS] = 5;

	configure_online_color_grid();
	menuChoices[MENU_ONLINE_OPACITY] = ONLINE_OPACITY_STEPS + 3;
}

bool game_menu_test_outpost_rows(char *detail, size_t detailSize)
{
	if (detail != NULL && detailSize != 0)
		detail[0] = '\0';

	const bool savedNet = isNetworkGame;
	const JE_boolean savedSuper = superTyrian, savedOne = onePlayerAction, savedTwo = twoPlayerMode;
	const JE_boolean savedCoopC = coopCampaignMode, savedCoopE = coopEndlessMode;
	const JE_boolean savedSep = arcadeSeparateMode;

	// Online modes that reach an outpost.
	static const struct { const char *name; bool coop, separate, super; } modes[] = {
		{ "co-op Campaign",  true,  false, false },
		{ "co-op Endless",   true,  false, false },
		{ "Linked Arcade",   false, false, false },
		{ "Separate Arcade", false, true,  false },
		{ "SuperTyrian",     false, false, true  },
	};

	const char *fault = NULL;

	isNetworkGame = true;
	twoPlayerMode = true;
	for (uint m = 0; m < COUNTOF(modes) && fault == NULL; ++m)
	{
		coopCampaignMode = modes[m].coop;
		coopEndlessMode = false;
		arcadeSeparateMode = modes[m].separate;
		superTyrian = modes[m].super;
		onePlayerAction = false;

		const int menu = outpost_menu();
		const int customize = outpost_customize_row(menu);
		if (customize == 0)
		{
			fault = "an online mode opened an outpost menu with no Customize row";
			break;
		}

		// Customize occupies Options' stock row and shifts Options down.
		if (outpost_stock_row(menu, customize) != customize
		    || outpost_stock_row(menu, customize + 1) != customize)
			fault = "Customize did not land directly above Options";

		// Display rows round-trip to stock numbering, including debug rows.
		for (int stock = 2; fault == NULL && stock <= 11; ++stock)
			if (outpost_stock_row(menu, outpost_row(menu, stock)) != stock)
				fault = "an outpost row lost its offline row across the insertion";

		// Customize adds one row after debug setup.
		const JE_byte before = menuChoices[menu];
		configure_outpost_customize_menu();
		const bool grew = (menuChoices[menu] == before + 1);
		menuChoices[menu] = before;
		if (fault == NULL && !grew)
			fault = "the outpost menu did not make room for its Customize row";

		// The insertion point holds the stock Options label.
		if (fault == NULL && strcmp(menuInt[menu + 1][customize - 1], menuInt[MENU_OPTIONS + 1][0]) != 0)
			fault = "the Customize row was not measured against the Options row";
	}

	// Offline outposts keep stock numbering.
	isNetworkGame = false;
	for (uint i = 0; fault == NULL && i < COUNTOF(outpostMenus); ++i)
		if (outpost_customize_row(outpostMenus[i].menu) != 0
		    || outpost_row(outpostMenus[i].menu, 5) != 5)
			fault = "an offline outpost menu grew a Customize row";

	// Keep optional gaps when they fit; never let them reach the help line.
	const JE_boolean savedDebug = debugMode;
	for (uint i = 0; fault == NULL && i < COUNTOF(outpostMenus); ++i)
	{
		const int menu = outpostMenus[i].menu;

		// Debug setup starts from these stock counts.
		const int stockDefault = (menu == MENU_FULL_GAME)
		                         ? menuChoicesDefault[menu] - 2   // default includes debug rows
		                         : menuChoicesDefault[menu];
		if (outpostMenus[i].stockRows != stockDefault)
		{
			fault = "an outpost menu's stock row count drifted from menuChoicesDefault";
			break;
		}

		const JE_byte savedCount = menuChoices[menu];
		for (int debug = 0; debug <= 1 && fault == NULL; ++debug)
			for (int online = 0; online <= 1 && fault == NULL; ++online)
			{
				debugMode = (debug != 0);
				isNetworkGame = (online != 0);

				const int last = outpostMenus[i].stockRows + (debug ? 2 : 0)
				               + (outpost_customize_row(menu) != 0 ? 1 : 0);
				menuChoices[menu] = (JE_byte)last;

				if (OPTIONS_ROW_Y(last) + outpost_row_gap_px(menu, last) + MENU_ROW_H > MENU_HELP_TEXT_Y)
					fault = "an outpost menu's last row reached the help line";
				else if (!outpost_gaps_drawn(menu)
				         && OPTIONS_ROW_Y(last) + outpost_gap_px_raw(menu, last) + MENU_ROW_H
				            <= MENU_HELP_TEXT_Y)
					fault = "an outpost menu dropped a blank line it had room for";
			}
		menuChoices[menu] = savedCount;
	}
	debugMode = savedDebug;

	arcadeSeparateMode = savedSep;
	coopEndlessMode = savedCoopE;
	coopCampaignMode = savedCoopC;
	twoPlayerMode = savedTwo;
	onePlayerAction = savedOne;
	superTyrian = savedSuper;
	isNetworkGame = savedNet;

	if (fault != NULL && detail != NULL && detailSize != 0)
		snprintf(detail, detailSize, "%s", fault);
	return fault == NULL;
}

// Online pickers reserve the left panel for the weapon simulator.
static bool menu_is_online_picker(void)
{
	return curMenu == MENU_ONLINE_COLOR || curMenu == MENU_ONLINE_OPACITY
	    || curMenu == MENU_ONLINE_HPBARS;
}

// Show both dyes so the player can avoid matching the partner.
static void online_color_legend(int x, int y, const char *who, int color)
{
	const int swatchW = 10, swatchH = 7;

	// An empty outline for no dye, and otherwise one shade well inside the bank's ramp.
	if (color == NET_SHIP_COLOR_NONE)
		JE_rectangle(VGAScreen, x, y, x + swatchW, y + swatchH, ONLINE_CELL_LO);
	else
		fill_rectangle_xy(VGAScreen, x, y, x + swatchW, y + swatchH,
		                  (Uint8)(((color - 1) << 4) | 0x0b));

	draw_font_hv_shadow(VGAScreen, x + swatchW + 5, y, who, small_font, left_aligned, 15, 3, false, 1);
}

// The highlighted cell applies immediately; the last cell only returns.
static void draw_online_color_page(void)
{
	const uint seat = netStyleLocalSeat();
	const int cell = curSel[MENU_ONLINE_COLOR] - 2;
	if (cell >= 0 && cell < online_color_cells - 1 && netStyleSeatColor(seat) != online_color_cell[cell])
	{
		netStyleSetSeatColor(seat, online_color_cell[cell]);
		network_player_look_publish();  // publish immediately instead of waiting for keep-alive
	}

	for (int i = 0; i < online_color_cells; ++i)
	{
		int x0, y0, x1, y1;
		online_color_cell_rect(i, &x0, &y0, &x1, &y1);

		const bool selected = (i == cell);
		const int color = online_color_cell[i];

		if (color != NET_SHIP_COLOR_NONE)
		{
			// Show the full shade ramp used by the dye.
			const Uint8 bank = (Uint8)((color - 1) << 4);
			for (int y = y0; y <= y1; ++y)
			{
				const int shade = 2 + (y - y0) * 13 / (ONLINE_COLOR_CELL_H - 1);
				fill_rectangle_xy(VGAScreen, x0, y, x1, y, (Uint8)(bank | shade));
			}
		}
		else
		{
			fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, ONLINE_CELL_PANEL);
			draw_font_hv_shadow(VGAScreen, (x0 + x1 + 1) / 2,
			                    y0 + (ONLINE_COLOR_CELL_H - ONLINE_COLOR_LABEL_H) / 2,
			                    (i == 0) ? "Off" : ONLINE_PICKER_DONE, small_font, centered,
			                    15, selected ? 6 : 3, false, 1);
		}

		JE_rectangle(VGAScreen, x0 - 1, y0 - 1, x1 + 1, y1 + 1,
		             selected ? ONLINE_CELL_HI : ONLINE_CELL_LO);
	}

	online_color_legend(ONLINE_COLOR_X0, ONLINE_COLOR_LEGEND_Y, "You", netStyleSeatColor(seat));
	online_color_legend(ONLINE_COLOR_X0 + ONLINE_COLOR_LEGEND_GAP, ONLINE_COLOR_LEGEND_Y,
	                    "Partner", netStylePeerColor());

	// Preview this ship's selected color at full opacity.
	netStylePreviewSet(netStyleSeatColor(seat), NET_OPACITY_FULL);
}

// Draw background detail behind each opacity sample so the fade is visible.
static void draw_opacity_sample(int x0, int y0, int w, int h, int sixteenths)
{
	static const int starX[] = { 4, 12, 21, 29, 38, 43 };
	static const int starY[] = { 1, 4, 2, 5, 1, 3 };

	fill_rectangle_xy(VGAScreen, x0, y0, x0 + w - 1, y0 + h - 1, ONLINE_OPACITY_BACK_SHADE);
	for (unsigned int i = 0; i < COUNTOF(starX); ++i)
		if (starX[i] < w && starY[i] < h)
			JE_pix(VGAScreen, x0 + starX[i], y0 + starY[i], ONLINE_OPACITY_STAR_SHADE);

	// Match the partner's selected dye.
	const int color = netStylePeerColor();
	const Uint8 bank = (color != NET_SHIP_COLOR_NONE) ? (Uint8)((color - 1) << 4) : 0x00;

	Uint8 *const pixels = (Uint8 *)VGAScreen->pixels;
	for (int y = y0; y < y0 + h; ++y)
	{
		Uint8 *const scan = pixels + y * VGAScreen->pitch;
		for (int x = x0; x < x0 + w; ++x)
		{
			int shade = (ONLINE_OPACITY_HULL_SHADE * sixteenths
			             + (scan[x] & 0x0f) * (NET_STYLE_SOLID - sixteenths) + 8) / NET_STYLE_SOLID;
			if (shade > 0x0f)
				shade = 0x0f;
			scan[x] = (Uint8)(bank | shade);
		}
	}
}

// The opacity a row stands for, counting down from full.
static int online_opacity_of_row(int row)
{
	return NET_OPACITY_FULL - (row - ONLINE_OPACITY_FIRST_ROW) * NET_OPACITY_STEP;
}

// Draw exact opacity samples beside each percentage.
static void draw_online_opacity_page(void)
{
	const int cursor = curSel[MENU_ONLINE_OPACITY];
	if (cursor >= ONLINE_OPACITY_FIRST_ROW && cursor < menuChoices[MENU_ONLINE_OPACITY])
	{
		NetShipView view = netStyleLocalView();
		view.opacity = (Uint8)online_opacity_of_row(cursor);
		online_set_local_view(view);
	}

	for (int row = 2; row <= menuChoices[MENU_ONLINE_OPACITY]; ++row)
	{
		const int y = ONLINE_OPACITY_Y0 + (row - 2) * ONLINE_OPACITY_ROW_H;
		const bool leave = (row == menuChoices[MENU_ONLINE_OPACITY]);
		const bool toggle = (row == ONLINE_OPACITY_TOGGLE_ROW);
		const int textCol = (row == cursor) ? 15 : 28;  // the keyboard page's own two shades
		char label[16];

		if (toggle)
			SDL_strlcpy(label, "Apply to Ship", sizeof(label));
		else if (leave)
			SDL_strlcpy(label, ONLINE_PICKER_DONE, sizeof(label));
		else
			snprintf(label, sizeof(label), "%d%%", online_opacity_of_row(row));

		JE_textShade(VGAScreen, 166, y, label, textCol / 16, textCol % 16 - 8, DARKEN);

		if (toggle)
		{
			JE_textShade(VGAScreen, ONLINE_OPACITY_SWATCH_X, y,
			             netStyleLocalView().shipOpacity ? "On" : "Off",
			             textCol / 16, textCol % 16 - 8, DARKEN);
		}
		else if (!leave)
		{
			const int sixteenths =
				(online_opacity_of_row(row) * NET_STYLE_SOLID + 50) / NET_OPACITY_FULL;
			draw_opacity_sample(ONLINE_OPACITY_SWATCH_X, y,
			                    ONLINE_OPACITY_SWATCH_W, ONLINE_OPACITY_SWATCH_H, sixteenths);
		}
	}

	// Preview the partner; net_style.c applies the ship toggle.
	netStylePreviewSet(netStylePeerColor(), netStyleLocalView().opacity);
}

/* Map the resolved shop submenu to a static crash-log phase string.
 * Keep this positional table aligned with the MENU_* enum. */
static void set_shop_phase(void)
{
	static const char *const names[MENU_MAX] = {
		"shop: buy/sell",             //  0  MENU_FULL_GAME
		"shop: upgrade ship",         //  1  MENU_UPGRADES
		"shop: options",              //  2  MENU_OPTIONS
		"shop: play next level",      //  3  MENU_PLAY_NEXT_LEVEL
		"shop: buy item",             //  4  MENU_UPGRADE_SUB
		"shop: keyboard config",      //  5  MENU_KEYBOARD_CONFIG
		"shop: load/save game",       //  6  MENU_LOAD_SAVE
		"shop: data cubes",           //  7  MENU_DATA_CUBES
		"shop: reading data cube",    //  8  MENU_DATA_CUBE_SUB
		"shop: 2-player menu",        //  9  MENU_2_PLAYER_ARCADE
		"shop: arcade/network menu",  // 10  MENU_1_PLAYER_ARCADE
		"shop: options",              // 11  MENU_LIMITED_OPTIONS
		"shop: joystick config",      // 12  MENU_JOYSTICK_CONFIG
		"shop: SuperTyrian menu",     // 13  MENU_SUPER_TYRIAN
		"shop: mouse config",         // 14  MENU_MOUSE_CONFIG
		"shop: (retired slot 15)",    // 15  the removed debug play-level grid; unreachable
		"shop: endless E-Shop",       // 16  MENU_ESHOP
		"shop: endless perks",        // 17  MENU_PERKS
		"shop: online look",          // 18  MENU_ONLINE
		"shop: online ship color",    // 19  MENU_ONLINE_COLOR
		"shop: online opacity",       // 20  MENU_ONLINE_OPACITY
		"shop: online HP bars",       // 21  MENU_ONLINE_HPBARS
	};
	if ((unsigned)curMenu < MENU_MAX && names[curMenu] != NULL)
		crashlog_set_phase(names[curMenu]);
}

// Sort every merchant category (itemAvail rows) ascending by item id, with the "None" entry (id
// 0) always sunk to the BOTTOM of its list.
static void sort_shop_inventory(void)
{
	for (int x = 0; x < 9; x++)
	{
		if (itemAvailMax[x] <= 1)
			continue;
		for (int a = 0; a < itemAvailMax[x] - 1; a++)
		{
			for (int b = a; b < itemAvailMax[x]; b++)
			{
				if (itemAvail[x][a] == 0 || (itemAvail[x][a] > itemAvail[x][b] && itemAvail[x][b] != 0))
				{
					JE_byte t = itemAvail[x][a];
					itemAvail[x][a] = itemAvail[x][b];
					itemAvail[x][b] = t;
				}
			}
		}
	}
}

/* Two-player shop readout.
 * Wrap long item names within the left panel instead of under the menu column. */
#define SHOP_2P_TOP     26   // first row
#define SHOP_2P_X       25   // player rows
#define SHOP_2P_SUB_X   31   // indented detail rows
#define SHOP_2P_RIGHT  138   // right edge; text stops here, well clear of the menu column (x=166)
#define SHOP_2P_ROW_H    8

// Draw "<label> <value>", wrapping the value word by word. Returns the row after the last drawn.
static int draw_2p_info_row(int x, int y, int bright, const char *label, const char *value)
{
	char line[96], probe[96];
	SDL_strlcpy(line, label, sizeof(line));
	int row_x = x;

	for (const char *w = value; *w != '\0'; )
	{
		if (*w == ' ')
		{
			++w;
			continue;
		}

		const char *end = w;
		while (*end != '\0' && *end != ' ')
			++end;

		snprintf(probe, sizeof(probe), "%s%s%.*s", line, line[0] == '\0' ? "" : " ", (int)(end - w), w);

		// Break only when the row already holds something, so a word too wide even on a row
		// of its own is still drawn (clipped by the panel) rather than looping forever.
		if (line[0] != '\0' && row_x + JE_textWidth(probe, TINY_FONT) > SHOP_2P_RIGHT)
		{
			JE_textShade(VGAScreen, row_x, y, line, 15, bright, FULL_SHADE);
			y += SHOP_2P_ROW_H;
			row_x = x + 6;
			line[0] = '\0';
			continue;
		}

		SDL_strlcpy(line, probe, sizeof(line));
		w = end;
	}

	JE_textShade(VGAScreen, row_x, y, line, 15, bright, FULL_SHADE);
	return y + SHOP_2P_ROW_H;
}

// Center peer-wait notices in the shop's 320-pixel field; detail and hint are optional.
void shopWaitNotice(const char *text, const char *detail, const char *hint)
{
	JE_barShade(VGAScreen, 3, 3, LEGACY_WIDTH - 4, 196);
	JE_barShade(VGAScreen, 1, 1, LEGACY_WIDTH - 2, 198);

	// Black-outlined: the shade above still leaves bright backdrop pixels under the text.
	JE_dStringOutlined(VGAScreen, JE_fontCenter(text, SMALL_FONT_SHAPES), 92, text, SMALL_FONT_SHAPES);
	if (detail != NULL)
	{
		draw_font_hv_full_shadow(VGAScreen, LEGACY_WIDTH / 2, 110, detail,
		                         small_font, centered, 15, 4, true, 1);
	}
	if (hint != NULL)
	{
		draw_font_hv_full_shadow(VGAScreen, LEGACY_WIDTH / 2, 122, hint,
		                         small_font, centered, 15, 2, true, 1);
	}
}

// Present and pace every rendezvous pass from one shared call site.
void shopWaitFrame(void)
{
	// Synthesize keys so controller and touch users can follow the Esc prompt.
	push_joysticks_as_keyboard();

	service_SDL_events(false);
	mouseCursor = MOUSE_POINTER_NORMAL;
	JE_mouseStart();
	JE_showVGA();
	JE_mouseReplace();
	if (!output_vsync)
		limit_render_fps();
}

/* The co-op half of leaving the outpost. Returns false when the player withdrew and wants to
 * keep outfitting; see doc/notes.md#session-and-outpost. */
static bool shopCampaignRendezvous(void)
{
	network_shop_send_state(true);

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: rendezvous committed\n");
		fflush(stderr);
	}

	Uint32 qaBeat = SDL_GetTicks();
	(void)qaBeat;
#define QA_RENDEZVOUS_TRACE(which)                                                              \
	do {                                                                                        \
		if (qa_net_gameplay_ticks > 0 && SDL_GetTicks() - qaBeat > 2000)                        \
		{                                                                                       \
			qaBeat = SDL_GetTicks();                                                            \
			int qaLD, qaLL, qaMySeq, qaPeerSeq;                                                 \
			network_shop_debug_state(&qaLD, &qaLL, &qaMySeq, &qaPeerSeq);                       \
			fprintf(stderr, "net gameplay: rendezvous waiting (%s) done=%d lock=%d head=%04x"   \
			                " sync=%d mine=%d/%d seq=%d peerseq=%d\n",                          \
			        (which), network_shop_peer_done() ? 1 : 0,                                  \
			        network_shop_peer_locked() ? 1 : 0,                                         \
			        packet_in[0] ? (unsigned)SDLNet_Read16(&packet_in[0]->data[0]) : 0u,        \
			        network_is_sync() ? 1 : 0, qaLD, qaLL, qaMySeq, qaPeerSeq);                 \
			fflush(stderr);                                                                     \
		}                                                                                       \
	} while (0)

	/* An ordered departure packet proves the peer completed this rendezvous. Leave it queued for
	 * the departure handshake; consuming it here would deadlock that handshake. */
	bool peerDeparted = false;

	for (;;)
	{
		// Step one: both committed. Esc withdraws, but only until the peer's commit lands,
		// because from that point the peer is allowed to lock and stop listening.
		while (!network_shop_peer_done() && !peerDeparted)
		{
			shopWaitFrame();
			network_shop_keepalive();
			QA_RENDEZVOUS_TRACE("commit");

			if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
			{
				newkey = false;
				JE_playSampleNum(S_SPRING);
				// Revoke any charted sector before the withdrawal publishes, or the packet
				// still carries it and the two machines can chart different courses.
				endlessCoopCourse = -1;
				network_shop_send_state(false);
				return false;
			}
			newkey = false;

			if (network_shop_pump() || network_debug_sync_pump(false))
				continue;
			if (network_shop_departure_pending())
			{
				peerDeparted = true;
				break;
			}
			network_update();
			network_check();
		}

		if (qa_net_gameplay_ticks > 0)
		{
			fprintf(stderr, "net gameplay: rendezvous peer done%s\n",
			        peerDeparted ? " (already departed)" : "");
			fflush(stderr);
		}

		// Both machines are now in this loop draining packets, which is the one window wide
		// enough for a custom weapon design. It is a no-op unless the design changed.
		network_custom_weapon_publish();
		// A ship record may require the design while the local Weapon Creator is off.
		if (extraShipsUseCustomWeapon())
			network_custom_weapon_publish_resume();
		network_extra_ships_publish();

		if (qa_net_gameplay_ticks > 0)
		{
			fprintf(stderr, "net gameplay: rendezvous publishing lock\n");
			fflush(stderr);
		}

		// Step two: publish the lock and wait for the peer's. A withdrawal already in flight
		// clears the peer's commit here, which unlocks us and reopens the first wait.
		network_shop_set_locked(true);
		while (!network_shop_peer_locked() && network_shop_peer_done() && !peerDeparted)
		{
			shopWaitFrame();
			network_shop_keepalive();
			QA_RENDEZVOUS_TRACE("lock");

			/* Withdrawal races a peer that may already have acted on our lock. Drop the lock and
			 * commit, then re-commit only if its departure packet proves it already left. */
			if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
			{
				newkey = false;
				network_shop_set_locked(false);
				network_shop_send_state(false);

				const Uint32 until = SDL_GetTicks() + 1500;
				bool departed = false;
				while (!departed && SDL_GetTicks() < until)
				{
					shopWaitFrame();
					network_shop_keepalive();
					if (network_shop_pump() || network_debug_sync_pump(false))
						continue;
					departed = network_shop_departure_pending();
					if (departed)
						break;
					network_update();
					network_check();
				}

				if (!departed)
				{
					JE_playSampleNum(S_SPRING);
					endlessCoopCourse = -1;
					return false;
				}

				// Too late to take it back; leave with them.
				network_shop_send_state(true);
				network_shop_set_locked(true);
				peerDeparted = true;
				break;
			}
			newkey = false;

			if (network_shop_pump() || network_debug_sync_pump(false))
				continue;
			if (network_shop_departure_pending())
			{
				peerDeparted = true;
				break;
			}
			network_update();
			network_check();
		}

		if (peerDeparted || network_shop_peer_done())
		{
			if (qa_net_gameplay_ticks > 0)
			{
				fprintf(stderr, "net gameplay: rendezvous locked%s\n",
				        peerDeparted ? " (peer already departed)" : "");
				fflush(stderr);
			}
			// Leave only after the peer has published its outpost checkpoint.
			if (qa_net_outpost_quit && !network_is_host && !peerDeparted &&
			    network_session_saveable)
			{
				fprintf(stderr, "net gameplay: joiner leaves from the outpost\n");
				fflush(stderr);
				network_prepare(PACKET_QUIT);
				network_send(4);
				network_tyrian_halt(0, true);   // does not return
			}
			return true;
		}

		network_shop_set_locked(false);
	}
#undef QA_RENDEZVOUS_TRACE
}

/* Committed online course index, or -1 while the charting player is choosing. */
int endlessCoopCourse = -1;

/* Wait for the charting player's course. The normal pre-commit wait accepts Esc; the fallback
 * wait is bounded and selects the first route if the peers reached the rendezvous without one. */
/* Returned instead of a sector index when the partner took a debug zone jump: there is no course
 * to wait for, and the caller should leave the outpost with the jump the poll just adopted. */
#define SHOP_COURSE_JUMPED (-2)

static int shopEndlessAwaitCourse(bool escapable)
{
	shopWaitNotice("Partner is charting a course.", "They pick the next sector.",
	               escapable ? "Press Esc to go back." : NULL);

	const Uint32 started = SDL_GetTicks();
	while (escapable || SDL_GetTicks() - started < 8000)
	{
		shopWaitFrame();
		network_shop_keepalive();

		if (escapable && newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			newkey = false;
			JE_playSampleNum(S_SPRING);
			return -1;
		}
		newkey = false;

		const int course = network_shop_peer_course();
		if (course >= 0)
			return course;

		// A peer's debug zone jump replaces the pending course choice.
		if (network_endless_jump_poll())
			return SHOP_COURSE_JUMPED;

		if (network_shop_pump() || network_debug_sync_pump(false))
			continue;

		// Committed, and the peer is already at the departure handshake: no sector is coming, and
		// the network_update below would swallow the very packet that handshake waits on.
		if (!escapable && network_shop_departure_pending())
			break;

		if (escapable && !network_peer_alive())
			return -1;

		network_update();
		network_check();
	}

	// A peer that never sent one has torn the outpost down some other way; the first route is
	// always launchable, so take it rather than hanging here.
	crashlog_netlog_line("ENDLESS COURSE TIMEOUT",
	                     "the charting player never published a sector; this machine took the "
	                     "first route on the slate, which the canary will report if they differ.");
	return 0;
}

// The outpost's route before a level was picked, so a withdrawn commit can put it back.
typedef struct
{
	JE_byte mainLevel, nextLevel, lvlFileNum, forcedLvlFileNum;
}
ShopOutpostRoute;

/* Wait at a departure gate; false means the local player withdrew. */
static bool shopDepartureGate(void)
{
	network_depart_gate_publish(true);

	newkey = false;   // the press that chose Start Level must not close this screen too

	bool esc = false;
	for (;;)
	{
		shopWaitFrame();

		// Sticky: the pump below skips the rest of the pass, so a debug block landing on the
		// same frame as the press would otherwise swallow it.
		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
			esc = true;
		newkey = false;

		if (!esc && network_debug_sync_pump(false))
			continue;

		const int peerGate = network_depart_gate_peer();
		const Uint16 head = (peerGate < 0) ? network_inbound_head() : 0;

		switch (network_depart_gate_step(esc, peerGate, head))
		{
		case DEPART_GATE_WITHDRAW:
			JE_playSampleNum(S_SPRING);
			network_depart_gate_publish(false);
			return false;

		case DEPART_GATE_GO:
			return true;

		case DEPART_GATE_WAIT:
			break;
		}

		if (peerGate >= 0)
			continue;   // the gate packet is retired; nothing else to advance this pass

		// A peer that has gone leaves nobody to wait for; the commit path times out on its own.
		if (!network_peer_alive())
			return true;

		network_update();
		network_check();
	}
}

/* Commit the shared level, or restore the menu state after a withdrawal. */
static void shopLeaveOutpost(const ShopOutpostRoute *route)
{
	// Show one notice for both rendezvous; shading twice would darken the frame.
	// Campaign may wait here while the other player finishes outfitting.
	shopWaitNotice("Waiting for other player.",
	               coop_mode_active() ? "They are still in the outpost." : NULL,
	               "Press Esc to go back.");

	// Settle the next level while both peers are still in menu code. A staged debug
	// pick outranks the campaign route and takes both players with it.
	JE_byte myPickEp = 0, myPickSec = 0, myPickFile = 0;
	const bool myPick = debugLevelPickGet(&myPickEp, &myPickSec, &myPickFile);

	if (coop_mode_active())
	{
		shopPlayer()->last_items = shopPlayer()->items;

		if (!shopCampaignRendezvous())
		{
			mainLevel = route->mainLevel;
			nextLevel = route->nextLevel;
			lvlFileNum = route->lvlFileNum;
			forcedLvlFileNum = route->forcedLvlFileNum;
			jumpSection = false;
			gameLoaded = false;
			endlessCoopCourse = -1;
			debugLevelPickReset();

			curMenu = MENU_FULL_GAME;
			newPal = 1;
			return;
		}

		/* Fold the shared course after both players finish buying. Locked sorties and loaded games
		 * already own a route; a debug jump is settled first and replaces the course. */
		const bool endlessJumping = endlessCoop() && network_endless_jump_poll();

		if (endlessCoop() && !endlessLockedSortie && !gameLoaded && !endlessJumping)
		{
			int course = endlessCoopCourse;
			if (qa_net_gameplay_ticks > 0)
			{
				fprintf(stderr, "net gameplay: folding course %d\n", course);
				fflush(stderr);
			}
			if (course < 0)
				course = shopEndlessAwaitCourse(false);
			// The chart turn passes in endlessOnSectorCleared, once the sector is flown.
			shopSelectLevel(endlessSelectCourse(course), 0);
			endlessCoopCourse = -1;
			if (qa_net_gameplay_ticks > 0)
			{
				fprintf(stderr, "net gameplay: course folded (mainLevel %d)\n", (int)mainLevel);
				fflush(stderr);
			}
		}

		// Resolve a campaign disagreement only after both players commit. Debug picks and Endless
		// courses are already settled through their own paths.
		if (!myPick && !endlessCoop())
			network_shop_adopt_host_level();
		network_shop_end();
	}

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: departure handshake\n");
		fflush(stderr);
	}

	// Send an Endless zone jump's level and run state together.
	Uint8 myJumpBlock[ENDLESS_DEBUG_BLOCK_SIZE];
	const bool myJump = endlessJumpPickGet(myJumpBlock);

	/* The gate, then the commit. The commit survives a peer's withdrawal; see
	 * doc/notes.md#session-and-outpost. */
	bool committed = false;

	for (;;)
	{
		if (!coop_mode_active() && !shopDepartureGate())
		{
			mainLevel = route->mainLevel;
			nextLevel = route->nextLevel;
			lvlFileNum = route->lvlFileNum;
			forcedLvlFileNum = route->forcedLvlFileNum;
			jumpSection = false;
			gameLoaded = false;
			debugLevelPickReset();

			curMenu = MENU_FULL_GAME;
			newPal = 1;
			return;
		}

		if (!committed)
		{
			committed = true;

			network_prepare(PACKET_WAITING);
			packet_out_temp->data[4] = myPick ? 1 : 0;
			packet_out_temp->data[5] = myPickEp;
			packet_out_temp->data[6] = myPickSec;
			packet_out_temp->data[7] = myPickFile;
			packet_out_temp->data[8] = myJump ? 1 : 0;
			packet_out_temp->data[9] = myJump ? (Uint8)ENDLESS_DEBUG_BLOCK_SIZE : 0;
			if (myJump)
				memcpy(&packet_out_temp->data[10], myJumpBlock, ENDLESS_DEBUG_BLOCK_SIZE);
			// PACKET_WAITING + level pick + endless jump
			network_send(10 + (myJump ? ENDLESS_DEBUG_BLOCK_SIZE : 0));
		}

		bool reopened = false;
		for (;;)
		{
			shopWaitFrame();

			// A debug-menu edit made in the shop rides in ahead of the WAITING packet (reliable and
			// ordered), so both machines load the level with the same loadouts.
			if (network_debug_sync_pump(false) || network_shop_pump())
				continue;

			const int peerGate = network_depart_gate_peer();
			const Uint16 head = (peerGate < 0) ? network_inbound_head() : 0;
			const DepartWaitStep step = network_depart_wait_step(peerGate, head);

			if (step == DEPART_WAIT_REOPENED)
			{
				reopened = true;
				break;
			}

			if (step == DEPART_WAIT_DONE)
			{
				// Adopt the other player's browser pick.  If we made one too the host's wins,
				// so the two machines can never resolve the tie in opposite directions.
				if (packet_in[0]->len >= 8 && packet_in[0]->data[4] != 0 &&
				    (!myPick || !network_is_host))
				{
					// Capture the current level as the jump's return point.
					mainLevel = route->mainLevel;
					nextLevel = route->nextLevel;
					lvlFileNum = route->lvlFileNum;
					forcedLvlFileNum = route->forcedLvlFileNum;
					debugLevelPickApply(packet_in[0]->data[5],
					                    packet_in[0]->data[6],
					                    packet_in[0]->data[7]);
				}

				// Adopt the host's Endless jump state with its level choice.
				if (packet_in[0]->len > 10 && packet_in[0]->data[8] != 0 &&
				    (!myJump || !network_is_host))
				{
					// The length bounds the read; a truncated packet's own length byte does not.
					const size_t have = (size_t)packet_in[0]->len - 10;
					const size_t said = packet_in[0]->data[9];
					endlessJumpPickApply(&packet_in[0]->data[10], said < have ? said : have);
				}

				network_update();
				break;
			}

			if (peerGate >= 0)
				continue;   // a re-announced gate; the commit behind it is what this phase wants

			network_update();
			network_check();
		}

		if (!reopened)
			break;
	}

	network_state_reset();

	while (!network_is_sync())
	{
		shopWaitFrame();

		network_check();
	}
}

/* Wire gameplay drives the real outpost protocol without menu input. It covers
 * purchases, perks, weapon designs, course selection, and the start barrier. */
static void qa_shop_auto_visit(void)
{
	const ShopOutpostRoute route = { mainLevel, nextLevel, lvlFileNum, forcedLvlFileNum };
	const uint me = thisPlayerNum - 1u;
	static bool saveRouteTested = false;

	/* Exercise the real online Options action once. Restore the otherwise-unused menu globals
	 * immediately: the automated visit drives the outpost protocol directly after this check. */
	if (qa_net_scenario == 13 && !saveRouteTested)
	{
		const int savedMenu = curMenu;
		const bool savedPerformSave = performSave;
		const bool savedQuickSave = quikSave;

		curMenu = MENU_LIMITED_OPTIONS;
		JE_menuFunction((JE_byte)options_row(OPT_SAVE));
		const bool okay = curMenu == MENU_LOAD_SAVE && performSave;
		fprintf(stderr, "NET SAVE ROUTE %s player=%u\n", okay ? "PASS" : "FAIL", thisPlayerNum);
		fflush(stderr);

		curMenu = savedMenu;
		performSave = savedPerformSave;
		quikSave = savedQuickSave;
		saveRouteTested = true;
	}

	if (endlessMode)
	{
		/* Report both derived wallets for cross-peer comparison. */
		printf("NET ZONE WALLETS depth=%d p1=%lld p2=%lld bounty=%llu mods=%016llx\n",
		       endlessRunDepth, (long long)player[0].cash, (long long)player[1].cash,
		       (unsigned long long)endlessCashBySource[ENDLESS_CASH_BOUNTY],
		       (unsigned long long)endlessActiveMods);
		fflush(stdout);
	}

	// Hold a final barrier before ending the scheduled network test.
	if (qa_net_zones > 0 && qa_net_zones_cleared >= qa_net_zones)
	{
		network_prepare(PACKET_WAITING);
		network_send(4);  // PACKET_WAITING (final barrier)

		const Uint32 barrier = SDL_GetTicks();
		while (SDL_GetTicks() - barrier < 20000)
		{
			watchdog_heartbeat();
			network_check();
			if (network_shop_pump() || network_debug_sync_pump(false))
				continue;
			if (packet_in[0] != NULL
			    && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_WAITING)
			{
				network_update();
				break;
			}
			SDL_Delay(1);
		}
		qa_net_zone_verdict();
	}

	fprintf(stderr, "net gameplay: outpost auto-visit (depth %d)\n",
	        endlessMode ? endlessRunDepth : -1);
	fflush(stderr);

	shopPlayerIndex = gameplay_local_player_index();
	debugLevelPickReset();
	network_shop_begin();
	customWeaponNetPrepare();

	// A queued perk pick: take the first offer for this machine's own ship through the real
	// grant path; the shop packets publish it and the peer's own pick arrives the same way.
	if (endlessMode && endlessPerkPending)
	{
		if (endlessPerkChoiceCount() > 0)
			endlessPerkGrant(me, endlessPerkChoice[0], 1);
		endlessPerkPending = false;
	}

	if (coopCampaignMode)
	{
		// A small design of this machine's own, distinct per ship, compiled and equipped as
		// the front gun; the rendezvous publishes it so both machines fly both designs.
		customWeaponEnabled = true;
		SDL_strlcpy(customWeaponName, thisPlayerNum == 1 ? "QA Wire Alpha" : "QA Wire Beta",
		            sizeof(customWeaponName));
		customWeaponRaw[0][0].multi = 2;
		customWeaponRaw[0][0].max = 2;
		customWeaponRaw[0][0].attack[0] = (JE_byte)(8 + thisPlayerNum);
		customWeaponNetPrepare();
		player[me].items.weapon[FRONT_WEAPON].id =
			(Uint8)customWeaponOwnerPort[customWeaponLocalOwner()];
	}

	// A scripted purchase, so every visit exercises the transaction path in both directions.
	player_add_cash(shopPlayer(), 100 + (Sint64)thisPlayerNum);
	network_shop_send_transaction();

	if (endlessCoop() && !endlessLockedSortie && !gameLoaded)
	{
		const int course = 0;
		endlessCourseMod[course] = qa_net_zone_mods(endlessRunDepth);
		if (endlessLocalPlayerCharts())
		{
			endlessCoopCourse = course;
		}
		else
		{
			const int got = shopEndlessAwaitCourse(true);
			if (got >= 0)
				endlessCoopCourse = got;
		}
		jumpSection = true;   // the sector itself is folded at the rendezvous
	}
	else
	{
		// The campaign departure is a planet pick; take the first one on this outpost's map.
		shopSelectLevel(mapSection[0], 0);
	}

	shopLeaveOutpost(&route);

	fprintf(stderr, "net gameplay: outpost auto-visit done (mainLevel %d)\n", (int)mainLevel);
	fflush(stderr);
}

void JE_itemScreen(void)
{
	// A gameplay wire test drives the level itself; there is no player to walk the outpost.
	// Online co-op still owes the outpost protocol, which the auto-visit runs without a menu.
	if (qa_net_gameplay_ticks > 0)
	{
		if (isNetworkGame && coop_mode_active())
		{
			qa_shop_auto_visit();
			return;
		}
		fprintf(stderr, "net gameplay: outpost skipped\n");
		fflush(stderr);
		return;
	}

	bool quit = false;
	// Latch one-shot presses without blocking outpost animation while the button is held.
	bool awaitClickRelease = false;
	// Full-ship online modes show the local ship's hull, guns, and cash. Modes with one shared
	// arsenal continue to use player 0.
	shopPlayerIndex = gameplay_local_player_index();

	crashlog_set_phase("shop / buy-sell menu");

	// A staged debug-browser pick is good for the launch it was made for and nothing else.
	debugLevelPickReset();

	/* Center the buy/sell screen independently from the gameplay HUD */
	set_menu_centered(true);

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');

	load_cubes();

	VGAScreen = VGAScreenSeg;

	memcpy(menuChoices, menuChoicesDefault, sizeof(menuChoices));
	configure_buysell_debug_menu();
	configure_arcade_debug_menus();
	configure_outpost_customize_menu();
	configure_custom_weapon_menu();
	configure_options_sens_menu();
	configure_online_menus();
	configure_endless_shop_menu();
	if (isNetworkGame && coop_mode_active())
	{
		network_shop_begin();

		// Both machines fly both ships, so each player's design needs its own reserved slots
		// here; the designs themselves are exchanged at the rendezvous on the way out.
		customWeaponNetPrepare();
	}

	play_song(songBuy);

	JE_loadPic(VGAScreen, 1, false);

	curPal = 1;
	newPal = 0;

	JE_showVGA();

	set_palette(colors, 0, 255);

	col = 1;
	gameLoaded = false;
	curItemType = 1;
	cursor = 1;
	curItem = 0;

	for (unsigned int i = 0; i < COUNTOF(curSel); ++i)
		curSel[i] = 2;
	curSel[MENU_LOAD_SAVE] = lastLoadSaveSel;

	curMenu = MENU_FULL_GAME;

	// Open the one-shot post-zone perk pick before the normal outpost menu.
	if (endlessMode && endlessPerkPending)
	{
		endlessPerkListMode = false;  // this is the forced PICK, not the read-only list
		configure_endless_perk_menu();
		curSel[MENU_PERKS] = 2;
		curMenu = MENU_PERKS;
	}

	int temp_weapon_power[7]; // assumes there'll never be more than 6 weapons to choose from, 7th is "Done"

	// Taken before anything can pick a level, so a withdrawn commit restores the arrival route
	// rather than whichever one an earlier attempt happened to leave behind.
	const ShopOutpostRoute outpostRoute = { mainLevel, nextLevel, lvlFileNum, forcedLvlFileNum };

	/* JE: (* Check for where Pitems and Select match up - if no match then add to the itemavail list *) */
	shop_stock_owned_items();

	memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

	keyboardUsed = false;
	firstMenu9 = false;
	backFromHelp = false;

	/* JE: Sort items in merchant inventory (None sinks to the bottom of each list) */
	sort_shop_inventory();
	endlessHoistStartWeapon();  // endless zone 1: the run's starting gun leads the front-weapon list

	do
	{
		/* Re-apply centering after returning from submenus */
		set_menu_centered(true);
		quit = false;

		// Picker pages reapply their preview each frame.
		netStylePreviewClear();

		JE_getShipInfo();

		if (curMenu == MENU_FULL_GAME)
		{
			if (split_arcade_mode())
				curMenu = MENU_2_PLAYER_ARCADE;

			if ((isNetworkGame && !coop_mode_active()) || onePlayerAction)
				curMenu = MENU_1_PLAYER_ARCADE;

			if (superTyrian)
				curMenu = MENU_SUPER_TYRIAN;
		}

		set_shop_phase();  // crash-log breadcrumb: record which shop submenu is now open
		while (network_shop_pump())
			;

		// Keep the sidekick "Ammo N" labels current: an endless perk pick can grow every magazine
		// without leaving this screen. A no-op unless the bonus actually moved.
		JE_labelAmmoSidekicks();

		// Refresh labels after peer purchases change shared E-Shop stock.
		if (endlessCoop())
			configure_endless_shop_menu();

		paletteChanged = false;

		leftPower = false;
		rightPower = false;

		/* firstMenu9 refers to menu 8 after reindexing. */
		if (curMenu != MENU_DATA_CUBE_SUB || firstMenu9)
		{
			memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);
		}

		if (curMenu == MENU_UPGRADES &&
		    (curSel[curMenu] == 3 || curSel[curMenu] == 4))
		{
			// Reset temporary power levels when selecting either weapon bay.
			const uint item       = shopPlayer()->items.weapon[curSel[MENU_UPGRADES] - 3].id,
			           item_power = shopPlayer()->items.weapon[curSel[MENU_UPGRADES] - 3].power,
			           i = curSel[MENU_UPGRADES] - 2;  // 1 or 2 (front or rear)

			// set power level of owned weapon
			for (int slot = 0; slot < itemAvailMax[itemAvailMap[i]-1]; ++slot)
			{
				if (itemAvail[itemAvailMap[i]-1][slot] == item)
					temp_weapon_power[slot] = item_power;
				else
					temp_weapon_power[slot] = 1;
			}

			// set power level for "Done"
			temp_weapon_power[itemAvailMax[itemAvailMap[i]-1]] = item_power;
		}

		if (curMenu == MENU_PLAY_NEXT_LEVEL)
		{
			planetAni = 0;
			keyboardUsed = false;
			currentDotNum = 0;
			currentDotWait = 8;
			planetAniWait = 3;
			JE_updateNavScreen();
		}

		/* Draw menu title for everything but upgrade ship submenus */
		if (curMenu != MENU_UPGRADE_SUB)
		{
			JE_drawMenuHeader();
		}

		/* Draw menu choices for simple menus */
		if (curMenu == MENU_PERKS && endlessPerkListMode)
		{
			// Endless read-only Perks list: its own scrolling renderer (the list can exceed the
			// menuInt row count, so it can't go through the menuInt-backed JE_drawMenuChoices).
			draw_endless_perk_list();
		}
		else if (curMenu == MENU_ONLINE_COLOR)
		{
			draw_online_color_page();
		}
		else if (curMenu == MENU_ONLINE_OPACITY)
		{
			draw_online_opacity_page();
		}
		else if (curMenu == MENU_ONLINE_HPBARS)
		{
			draw_online_hpbars_page();
		}
		else if ((curMenu >= MENU_FULL_GAME && curMenu <= MENU_PLAY_NEXT_LEVEL) ||
		    (curMenu >= MENU_2_PLAYER_ARCADE && curMenu <= MENU_LIMITED_OPTIONS) ||
		    curMenu >= MENU_SUPER_TYRIAN)
		{
			JE_drawMenuChoices();
		}

		if (curMenu == MENU_MOUSE_CONFIG)
		{
			for (int i = 0; i < 3; ++i)
			{
				int tempY = 51 + i * 24;
				JE_textShade(VGAScreen, 215, tempY, joyButtonNames[mouseSettings[i] - 1], 15, 2, PART_SHADE);
			}
		}

		/* Data cube icons */
		if (curMenu == MENU_FULL_GAME)
		{
			for (int i = 1; i <= cubeMax; i++)
			{
				blit_sprite_dark(VGAScreen, 190 + i * 18 + 2, 37 + 1, OPTION_SHAPES, 34, false);
				blit_sprite(VGAScreen, 190 + i * 18, 37, OPTION_SHAPES, 34);  // data cube
			}
		}

		/* load/save menu */
		if (curMenu == MENU_LOAD_SAVE)
		{
			int min, max;

			// Online sessions run as two-player, so they share the 2-player page (slots 12-22).
			if (twoPlayerMode)
			{
				min = 13;
				max = 24;
			}
			else
			{
				min = 2;
				max = 13;
			}

			for (int x = min; x <= max; x++)
			{
				/* Highlight if current selection */
				temp2 = (x - min + 2 == curSel[curMenu]) ? 15 : 28;

				const bool slotLocked = x < max && !performSave && saveFiles[x-2].level > 0 &&
				                        save_custom_locked(&saveFiles[x-2]);
				if (slotLocked && temp2 == 28)
					temp2 = 24;

				/* Write save game slot */
				if (x == max)
					strcpy(tempStr, miscText[6-1]);
				else if (saveFiles[x-2].level == 0)
					strcpy(tempStr, miscText[3-1]);
				else
					strcpy(tempStr, saveFiles[x-2].name);

				int tempY = 38 + (x - min)*11;

				JE_textShade(VGAScreen, 163, tempY, tempStr, temp2 / 16, temp2 % 16 - 8, DARKEN);

				if (x < max) /* x == max isn't a save slot */
				{
					/* Highlight if current selection */
					temp2 = (x - min + 2 == curSel[curMenu]) ? 252 : (slotLocked ? 248 : 250);

					if (saveFiles[x-2].level == 0)
					{
						strcpy(tempStr, "-----"); /* Empty save slot */
					}
					else
					{
						char buf[20];

						strcpy(tempStr, saveFiles[x-2].levelName);

						if (endlessSlotHasRun((JE_byte)(x - 1)))
							SDL_strlcpy(buf, "End", sizeof buf);
						else if (saveFiles[x-2].customEpFile[0] != '\0')
							SDL_strlcpy(buf, "CLv", sizeof buf);
						else
							snprintf(buf, sizeof buf, "%s%d",
							         miscTextB[1-1], saveFiles[x-2].episode);
						JE_textShade(VGAScreen, 297, tempY, buf, temp2 / 16, temp2 % 16 - 8, DARKEN);
					}

					JE_textShade(VGAScreen, 245, tempY, tempStr, temp2 / 16, temp2 % 16 - 8, DARKEN);
				}

				JE_drawMenuHeader();
			}
		}

		if (curMenu == MENU_KEYBOARD_CONFIG)
		{
			for (int x = 2; x <= 11; x++)
			{
				if (x == curSel[curMenu])
				{
					temp2 = 15;
				}
				else
				{
					temp2 = 28;
				}

				JE_textShade(VGAScreen, 166, 38 + (x - 2)*12, menuInt[curMenu + 1][x-1], temp2 / 16, temp2 % 16 - 8, DARKEN);

				if (x < 10) /* 10 = reset to defaults, 11 = done */
				{
					temp2 = (x == curSel[curMenu]) ? 252 : 250;
					JE_textShade(VGAScreen, 236, 38 + (x - 2)*12, SDL_GetScancodeName(keySettings[x-2]), temp2 / 16, temp2 % 16 - 8, DARKEN);
				}
			}

			menuChoices[MENU_KEYBOARD_CONFIG] = 11;
		}

		if (curMenu == MENU_JOYSTICK_CONFIG)
		{
			const char *const menu_item[] =
			{
				"JOYSTICK",
				"ANALOG AXES",
				" SENSITIVITY",
				" THRESHOLD",
				menuInt[6][1],
				menuInt[6][4],
				menuInt[6][2],
				menuInt[6][3],
				menuInt[6][5],
				menuInt[6][6],
				menuInt[6][7],
				menuInt[6][8],
				"MENU",
				"PAUSE",
				menuInt[6][9],
				menuInt[6][10]
			};

			for (uint i = 0; i < COUNTOF(menu_item); i++)
			{
				int temp = (i == curSel[curMenu] - 2u) ? 15 : 28;

				JE_textShade(VGAScreen, 166, 38 + i * 8, menu_item[i], temp / 16, temp % 16 - 8, DARKEN);

				temp = (i == curSel[curMenu] - 2u) ? 252 : 250;

				char value[30] = "";
				if (joysticks == 0 && i < 14) // no joysticks, everything disabled
				{
					sprintf(value, "-");
				}
				else if (i == 0) // joystick number
				{
					sprintf(value, "%d", joystick_config + 1);
				}
				else if (i == 1) // joystick is analog
				{
					sprintf(value, "%s", joystick[joystick_config].analog ? "TRUE" : "FALSE");
				}
				else if (i < 4)  // joystick analog settings
				{
					if (!joystick[joystick_config].analog)
						temp -= 3;
					sprintf(value, "%d", i == 2 ? joystick[joystick_config].sensitivity : joystick[joystick_config].threshold);
				}
				else if (i < 14) // assignments
				{
					joystick_assignments_to_string(value, sizeof(value), joystick[joystick_config].assignment[i - 4]);
				}

				JE_textShade(VGAScreen, 236, 38 + i * 8, value, temp / 16, temp % 16 - 8, DARKEN);
			}

			menuChoices[curMenu] = COUNTOF(menu_item) + 1;
		}

		if (curMenu == MENU_UPGRADE_SUB)
		{
			/* Move cursor until we hit either "Done" or a weapon the player can afford */
			while (curSel[MENU_UPGRADE_SUB] < menuChoices[MENU_UPGRADE_SUB] &&
				JE_getCost(curSel[MENU_UPGRADES], itemAvail[itemAvailMap[curSel[MENU_UPGRADES] - 2] - 1][curSel[MENU_UPGRADE_SUB] - 2]) > shopPlayer()->cash)
			{
				curSel[MENU_UPGRADE_SUB] += lastDirection;
				if (curSel[MENU_UPGRADE_SUB] < 2)
					curSel[MENU_UPGRADE_SUB] = menuChoices[MENU_UPGRADE_SUB];
				else if (curSel[MENU_UPGRADE_SUB] > menuChoices[MENU_UPGRADE_SUB])
					curSel[MENU_UPGRADE_SUB] = 2;
			}

			if (curSel[MENU_UPGRADE_SUB] == menuChoices[MENU_UPGRADE_SUB])
			{
				/* If cursor on "Done", use previous weapon */
				*playeritem_map(&shopPlayer()->items, curSel[MENU_UPGRADES] - 2) = *playeritem_map(&old_items[shopPlayerIndex], curSel[MENU_UPGRADES] - 2);
			}
			else
			{
				/* Otherwise display the selected weapon */
				*playeritem_map(&shopPlayer()->items, curSel[MENU_UPGRADES] - 2) = itemAvail[itemAvailMap[curSel[MENU_UPGRADES]-2]-1][curSel[MENU_UPGRADE_SUB]-2];
			}

			/* Get power level info for front and rear weapons */
			if ((curSel[MENU_UPGRADES] == 3 || curSel[MENU_UPGRADES] == 4) && curSel[MENU_UPGRADE_SUB] < menuChoices[MENU_UPGRADE_SUB])
			{
				if (curSel[MENU_UPGRADES] == 4 && itemAvail[itemAvailMap[curSel[MENU_UPGRADES]-2]-1][curSel[MENU_UPGRADE_SUB]-2] == 0)
				{
					// "None" on rear weapon menu cannot be upgraded
					// ("None" on front weapon menu can; this is accurate to the original game)
					leftPower = false;
					rightPower = false;
				}
				else
				{
					const uint port = curSel[MENU_UPGRADES] - 3,  // 0 or 1 (front or back)
					           item_level = shopPlayer()->items.weapon[port].power;

					// calculate upgradeCost
					JE_getCost(curSel[MENU_UPGRADES], itemAvail[itemAvailMap[curSel[MENU_UPGRADES]-2]-1][curSel[MENU_UPGRADE_SUB]-2]);

					leftPower  = item_level > 1;  // can downgrade
					rightPower = item_level < 11; // can upgrade

					if (rightPower)
					{
						const Sint64 cash_left = JE_cashLeft();
						rightPowerAfford = cash_left >= upgradeCost; // can afford upgrade
					}
				}
			}
			else
			{
				// Nothing else can be upgraded or downgraded
				leftPower = false;
				rightPower = false;
			}

			/* submenu title  e.g., "Left Sidekick" */
			JE_dString(VGAScreen, 74 + JE_fontCenter(menuInt[2][curSel[MENU_UPGRADES]-1], FONT_SHAPES), 10, menuInt[2][curSel[MENU_UPGRADES]-1], FONT_SHAPES);

			/* Vertical scroll when the list overflows: the window shows visible_rows
			   rows from upgradeSubScrollTop and keeps the selected row visible. */
			const int sub_total_rows = menuChoices[curMenu] - 1;   // rows are tempW = 1..sub_total_rows
			const int sub_sel_row    = curSel[curMenu] - 1;        // tempW of the selected row
			const int visible_rows   = upgradeSubVisibleRows();
			int sub_max_top = sub_total_rows - visible_rows + 1;
			if (sub_max_top < 1)
				sub_max_top = 1;
			if (upgradeSubScrollTop > sub_max_top)
				upgradeSubScrollTop = sub_max_top;
			if (upgradeSubScrollTop < 1)
				upgradeSubScrollTop = 1;
			// Follow the selection only when it just moved (keyboard / mouse wheel /
			// click / the affordability skip above), so the selected row stays visible.
			if (curSel[curMenu] != upgradeSubPrevSel)
			{
				if (sub_sel_row < upgradeSubScrollTop)
					upgradeSubScrollTop = sub_sel_row;
				else if (sub_sel_row > upgradeSubScrollTop + visible_rows - 1)
					upgradeSubScrollTop = sub_sel_row - visible_rows + 1;
				upgradeSubPrevSel = curSel[curMenu];
			}

			/* Iterate through all submenu options */
			for (tempW = 1; tempW < menuChoices[curMenu]; tempW++)
			{
				if (tempW < upgradeSubScrollTop || tempW >= upgradeSubScrollTop + visible_rows)
					continue;  // scrolled out of view
				int tempY = 40 + (tempW - upgradeSubScrollTop) * 26; /* Calculate y position */
				Sint64 temp_cost;

				/* Item or None/Done. */
				if (tempW < menuChoices[MENU_UPGRADE_SUB] - 1)
				{
					/* Get base cost for choice */
					temp_cost = JE_getCost(curSel[MENU_UPGRADES], itemAvail[itemAvailMap[curSel[MENU_UPGRADES]-2]-1][tempW-1]);
				}
				else
				{
					/* "None" is free. */
					temp_cost = 0;
				}

				int afford_shade = (temp_cost > shopPlayer()->cash) ? 4 : 0;  // can player afford current weapon at all

				temp = itemAvail[itemAvailMap[curSel[MENU_UPGRADES]-2]-1][tempW-1]; /* Item ID */
				switch (curSel[MENU_UPGRADES]-1)
				{
					case 1: /* ship */
						if (temp > 90)
							snprintf(tempStr, sizeof(tempStr), "Custom Ship %d", temp - 90);
						else
							strcpy(tempStr, JE_shipName(temp));
						break;
					case 2: /* front and rear weapon */
					case 3:
						strcpy(tempStr, JE_weaponPortName(temp));
						break;
					case 4: /* shields */
						strcpy(tempStr, shields[temp].name);
						break;
					case 5: /* generator */
						strcpy(tempStr, powerSys[temp].name);
						break;
					case 6: /* sidekicks */
					case 7:
						strcpy(tempStr, options[temp].name);
						break;
				}
				if (tempW == curSel[curMenu]-1)
					temp2 = 15;
				else
					temp2 = 28;

				JE_getShipInfo();

				// Hoisted out of the marker block: a row tag ends its column here too.
				const bool sub_has_scrollbar = sub_total_rows > visible_rows;
				const int marker_x = SHOP_ITEM_MARKER_X(sub_has_scrollbar);

				/* item-owned marker (pulled left of the scroll bar when it is shown) */
				if (temp == *playeritem_map(&old_items[shopPlayerIndex], curSel[MENU_UPGRADES] - 2) && temp != 0 && tempW != menuChoices[curMenu]-1)
				{
					const int marker_bar_right = sub_has_scrollbar ? 288 : 300;
					fill_rectangle_xy(VGAScreen, 160, tempY+7, marker_bar_right, tempY+11, 227);
					blit_sprite2(VGAScreen, marker_x, tempY+2, shopSpriteSheet, SHOP_OWNED_MARKER_SPRITE);
				}

				/* Draw DONE */
				if (tempW == menuChoices[curMenu]-1)
				{
					strcpy(tempStr, miscText[13]);
				}

				/* A hull drawn as two halves needs wider columns than the rest. */
				ShopItemColumns cols = { SHOP_ITEM_ICON_X, SHOP_ITEM_NAME_X, SHOP_ITEM_COST_X };
				if (tempW < menuChoices[curMenu]-1 && curSel[MENU_UPGRADES]-1 == 1)
					cols = shop_ship_item_columns(temp);

				JE_textShade(VGAScreen, cols.nameX, tempY, tempStr,
				             temp2 / 16, temp2 % 16 - 8 - afford_shade, DARKEN);

				/* Draw every item icon except DONE. None uses a blank icon. */
				if (tempW < menuChoices[curMenu]-1)
				{
					JE_drawItem(curSel[MENU_UPGRADES]-1, temp, cols.iconX, tempY-4);
				}

				/* Make selected text brighter */
				temp2 = (tempW == curSel[curMenu]-1) ? 15 : 28;

				/* Draw Cost: if it's not the DONE option */
				if (tempW != menuChoices[curMenu]-1)
				{
					char buf[32];

					snprintf(buf, sizeof buf, "Cost: %lld", (long long)temp_cost);
					JE_textShade(VGAScreen, cols.costX, tempY+10, buf,
					             temp2 / 16, temp2 % 16 - 8 - afford_shade, DARKEN);

					// 3 is the front weapon list and 4 the rear; only Endless stocks both
					// of them from one pool of ports.
					const bool rear_list = curSel[MENU_UPGRADES] == 4;
					const char *tag = (curSel[MENU_UPGRADES] == 3 || rear_list)
					                ? shop_weapon_row_tag(temp, rear_list, endlessMode) : NULL;
					if (tag != NULL)
					{
						const int cost_right = cols.costX + JE_textWidth(buf, TINY_FONT);
						const int tag_w = JE_textWidth(tag, TINY_FONT);
						const int tag_x = shop_row_tag_x(cost_right, tag_w, marker_x);

						JE_textShade(VGAScreen, tag_x, tempY+10, tag,
						             temp2 / 16, temp2 % 16 - 8 - afford_shade, DARKEN);
					}
				}
			}

			/* Scroll bar (track + thumb) shown only when the list overflows. */
			if (sub_total_rows > visible_rows)
			{
				const int track_x  = 306;
				const int track_y0 = 38;
				const int track_y1 = 38 + visible_rows * 26 - 6;
				const int track_h  = track_y1 - track_y0;
				int thumb_h = track_h * visible_rows / sub_total_rows;
				if (thumb_h < 6)
					thumb_h = 6;
				const int thumb_y = track_y0 + (track_h - thumb_h) * (upgradeSubScrollTop - 1) / (sub_max_top - 1);
				fill_rectangle_xy(VGAScreen, track_x, track_y0, track_x + 2, track_y1, 241);
				fill_rectangle_xy(VGAScreen, track_x, thumb_y, track_x + 2, thumb_y + thumb_h, 252);
			}
		}

		/* Draw current money and shield/armor bars, when appropriate */
		if (((curMenu <= MENU_OPTIONS ||
		      curMenu == MENU_KEYBOARD_CONFIG ||
		      curMenu == MENU_LOAD_SAVE ||
		      curMenu >= MENU_1_PLAYER_ARCADE) &&
		     !split_arcade_mode() && !menu_is_online_picker()) ||
		    (curMenu == MENU_UPGRADE_SUB &&
		     (curSel[MENU_UPGRADES] >= 1 && curSel[MENU_UPGRADES] <= 6)))
		{
			if (curMenu != MENU_UPGRADE_SUB)
			{
				char buf[20];

				snprintf(buf, sizeof buf, "%lld", (long long)shopPlayer()->cash);
				// Center cash in the monitor slot used by the course rank.
				JE_textShade(VGAScreen, MENU_MONITOR_CENTER_X - JE_textWidth(buf, TINY_FONT) / 2, 172, buf, 1, 6, DARKEN);
			}
			if (endlessMode)
			{
				// Draw reinforced armour in 28-point rollover rows.
				static const int shopArmorLayerCol[] = { 14, 30, 46, 62, 78, 110, 94, 126 };
				int a = shopPlayer()->armor;
				for (int L = 0; a > 0 && L < (int)COUNTOF(shopArmorLayerCol); ++L)
				{
					JE_barDrawShadow(VGAScreen, 42, 152, 3, shopArmorLayerCol[L], (a > 28) ? 28 : a, 2, 13);
					a -= 28;
				}
			}
			else
				JE_barDrawShadow(VGAScreen, 42, 152, 3, 14, shopPlayer()->armor, 2, 13);
			// Shield ceiling rescaled so a full 28-unit gauge fills exactly 10 bars and every weaker
			// shield proportionally fewer; matching the in-game gauge.
			{
				int shieldBars = (int)((arcade_shield_max(shopPlayer()) * 10 + ARCADE_FULL_BAR / 2) / ARCADE_FULL_BAR);
				if (shieldBars > 10)
					shieldBars = 10;
				JE_barDrawShadow(VGAScreen, 104, 152, 1, 14, shieldBars, 2, 13);
			}
		}

		/* Draw crap on the left side of the screen, i.e. two player scores, ship graphic, etc. */
		if (!menu_is_online_picker() &&
			((curMenu >= MENU_FULL_GAME && curMenu <= MENU_OPTIONS) ||
			curMenu == MENU_KEYBOARD_CONFIG ||
			curMenu == MENU_LOAD_SAVE ||
			(curMenu >= MENU_2_PLAYER_ARCADE) ||
			(curMenu == MENU_UPGRADE_SUB &&
				(curSel[MENU_UPGRADES] == 2 || curSel[MENU_UPGRADES] == 5))))
		{
			if (split_arcade_mode())
			{
				char buf[80];
				int y = SHOP_2P_TOP;

				for (uint i = 0; i < 2; ++i)
				{
					// A network game labels the two totals with the players' own names; the same
					// swap the level-complete screen makes.
					char label[80];
					JE_playerScoreLabel((JE_byte)(i + 1), label, sizeof(label));
					snprintf(buf, sizeof(buf), "%s %lld", label, (long long)player[i].cash);

					y = draw_2p_info_row(SHOP_2P_X, y, 4, "", buf);

					snprintf(buf, sizeof(buf), "%d", *player[i].lives);
					y = draw_2p_info_row(SHOP_2P_SUB_X, y, 2, "Lives:", buf);

					// Player 1 flies the front gun and the special, player 2 the rear gun and
					// both sidekicks; the same split the powerup pickups use (JE_eventSystem).
					if (i == 0)
					{
						y = draw_2p_info_row(SHOP_2P_SUB_X, y, 2, "Front gun:",
							weaponPort[shopPlayer()->items.weapon[FRONT_WEAPON].id].name);
						y = draw_2p_info_row(SHOP_2P_SUB_X, y, 2, "Special:",
							JE_specialName(shopPlayer()->items.special));
						y += 4;  // gap between the two blocks
					}
					else
					{
						y = draw_2p_info_row(SHOP_2P_SUB_X, y, 2, "Rear gun:",
							weaponPort[player[1].items.weapon[REAR_WEAPON].id].name);
						y = draw_2p_info_row(SHOP_2P_SUB_X, y, 2, "Sidekick L:",
							options[player[1].items.sidekick[LEFT_SIDEKICK]].name);
						y = draw_2p_info_row(SHOP_2P_SUB_X, y, 2, "Sidekick R:",
							options[player[1].items.sidekick[RIGHT_SIDEKICK]].name);
					}
				}
			}
			else if (superArcadeMode != SA_NONE || superTyrian)
			{
				helpBoxColor = 15;
				helpBoxBrightness = 4;
				// The hull named here is this ship's own, not the session global: online the two players
				// pick separately, and the global carries ship one's pick.
				const uint sa_ship = player_sa_ship(shopPlayer());
				if (!superTyrian)
					JE_helpBox(VGAScreen, 35, 25, superShips[sa_ship != SA_NONE ? sa_ship : superArcadeMode], 18);
				else
					JE_helpBox(VGAScreen, 35, 25, superShips[SA+3], 18);
				helpBoxBrightness = 1;

				JE_textShade(VGAScreen, 25, 50, superShips[SA+1], 15, 0, FULL_SHADE);
				JE_helpBox(VGAScreen,   25, 60, weaponPort[shopPlayer()->items.weapon[FRONT_WEAPON].id].name, 22);
				JE_textShade(VGAScreen, 25, 120, superShips[SA+2], 15, 0, FULL_SHADE);
				JE_helpBox(VGAScreen,   25, 130, JE_specialName(shopPlayer()->items.special), 22);
			}
			else
			{
				draw_ship_illustration();
			}
		}

		/* Changing the volume? */
		if (curMenu == MENU_OPTIONS ||
		    curMenu == MENU_LIMITED_OPTIONS)
		{
			// Round the bar count (+6) exactly like the in-game Esc menu, so the same
			// volume never draws a different number of bars in the two menus.
			JE_barDrawShadow(VGAScreen, 225, OPTIONS_ROW_Y(options_row(OPT_MUSIC)), 1,
			                 music_disabled ? 12 : 16, (tyrMusicVolume + 6) / 12, 3, 13);
			JE_barDrawShadow(VGAScreen, 225, OPTIONS_ROW_Y(options_row(OPT_SOUND)), 1,
			                 samples_disabled ? 12 : 16, (fxVolume + 6) / 12, 3, 13);
			// Compare drawn segments so the sensitivity midpoint marker flips on its bar.
			{
				const int y = OPTIONS_ROW_Y(options_row(OPT_SENS));
				const int amt = (ship_sensitivity + 6) / 12;
				const int mark = (SHIP_SENS_DEFAULT + 6) / 12;
				JE_barDrawShadow(VGAScreen, 225, y, 1, 16, amt, 3, 13);
				JE_barDrawMark(VGAScreen, 225, y,
				               amt >= mark ? SHIP_SENS_MARK_COL : SHIP_SENS_MARK_COL_DIM, mark, 3, 13);
			}
		}

		/* "firstmenu9" refers to menu 8 because of reindexing */
		if (curMenu == MENU_DATA_CUBES ||
		    (curMenu == MENU_DATA_CUBE_SUB && (firstMenu9 || backFromHelp)))
		{
			firstMenu9 = false;
			menuChoices[MENU_DATA_CUBES] = cubeMax + 2;
			fill_rectangle_xy(VGAScreen, 1, 1, 145, 170, 0);

			blit_sprite(VGAScreenSeg, 1, 1, OPTION_SHAPES, 20); /* Portrait area background */

			if (curMenu == MENU_DATA_CUBES)
			{
				if (cubeMax == 0)
				{
					JE_helpBox(VGAScreen, 166, 80, miscText[16 - 1], 30);
					tempW = 160;
					temp2 = 252;
				}
				else
				{
					for (int x = 1; x <= cubeMax; x++)
					{
						JE_drawCube(VGAScreenSeg, 166, 38 + (x - 1) * 28, 13, 0);
						if (x + 1 == curSel[curMenu])
						{
							temp2 = 252;
						}
						else
						{
							temp2 = 250;
						}

						helpBoxColor = temp2 / 16;
						helpBoxBrightness = (temp2 % 16) - 8;
						helpBoxShadeType = DARKEN;
						JE_helpBox(VGAScreen, 192, 44 + (x - 1) * 28, cube[x - 1].title, 24);
					}
					int x = cubeMax + 1;
					if (x + 1 == curSel[curMenu])
					{
						temp2 = 252;
					}
					else
					{
						temp2 = 250;
					}
					tempW = 44 + (x - 1) * 28;
				}

				JE_textShade(VGAScreen, 172, tempW, miscText[6 - 1], temp2 / 16, (temp2 % 16) - 8, DARKEN);
			}

			if (curSel[MENU_DATA_CUBES] < menuChoices[MENU_DATA_CUBES])
			{
				const int face_sprite = cube[curSel[MENU_DATA_CUBES] - 2].face_sprite;

				if (face_sprite != -1)
				{
					const int face_x = 77 - (sprite(FACE_SHAPES, face_sprite)->width / 2),
					          face_y = 92 - (sprite(FACE_SHAPES, face_sprite)->height / 2);

					blit_sprite(VGAScreenSeg, face_x, face_y, FACE_SHAPES, face_sprite);  // datacube face

					// modify palette for face
					paletteChanged = true;
					temp2 = (face_sprite < 12) ? facepal[face_sprite] : 0;
					newPal = 0;

					for (temp = 1; temp <= 255 - (3 * 16); temp++)
						colors[temp] = palettes[temp2][temp];
				}
			}
		}

		/* 2 player input devices */
		if (curMenu == MENU_2_PLAYER_ARCADE)
		{
			for (uint i = 0; i < COUNTOF(inputDevice); i++)
			{
				if (inputDevice[i] > 2 + joysticks)
					inputDevice[i] = inputDevice[i == 0 ? 1 : 0] == 1 ? 2 : 1;

				char temp[64];
				if (joysticks > 1 && inputDevice[i] > 2)
					sprintf(temp, "%s %d", inputDevices[2], inputDevice[i] - 2);
				else
					sprintf(temp, "%s", inputDevices[inputDevice[i] - 1]);
				JE_dString(VGAScreen, 186, 38 + 2 * (i + 1) * 16, temp, SMALL_FONT_SHAPES);
			}
		}

		/* JE: { - Step VI - Help text for current cursor location } */

		flash = false;

		/* JE: {Reset player weapons} */
		memset(shotMultiPos, 0, sizeof(shotMultiPos));

		JE_drawScore();

		JE_drawMainMenuHelpText();

		if (newPal > 0) /* newPal remains one-based. */
		{
			curPal = newPal;
			memcpy(colors, palettes[newPal - 1], sizeof(colors));
			set_palette(palettes[newPal - 1], 0, 255);
			newPal = 0;
		}

		/* datacube title under face */
		if ((curMenu == MENU_DATA_CUBES || curMenu == MENU_DATA_CUBE_SUB) &&
			curSel[MENU_DATA_CUBES] < menuChoices[MENU_DATA_CUBES])
		{
			JE_textShade(VGAScreen, 75 - JE_textWidth(cube[curSel[MENU_DATA_CUBES] - 2].header, TINY_FONT) / 2, 173, cube[curSel[MENU_DATA_CUBES] - 2].header, 14, 3, DARKEN);
		}

		/* Process input after drawing the screen. */
		service_SDL_events(true);

		if (constantPlay)
		{
			mainLevel = mapSection[mapPNum-1];
			jumpSection = true;
		}
		else
		{
			do
			{
			/* Animate the active menu and handle events that do not need the outer input path. */

				// Refresh conditional buttons before each outpost frame is presented.
				if (outpostListScrolls())
					touch_ui_set_layout(TOUCH_LAYOUT_LIST);
				else
					touch_ui_clear_layout();

				if (outpostRearModeCyclable())
					touch_ui_set_extra(TOUCH_BTN_REAR_MODE);
				else
					touch_ui_clear_extra();

				NETWORK_KEEP_ALIVE();
				while (network_shop_pump())
					;

				mouseCursor = MOUSE_POINTER_NORMAL;

				col += colC;
				if (col < -2 || col > 6)
				{
					colC = (-1 * colC);
				}

				// data cube reading
				if (curMenu == MENU_DATA_CUBE_SUB)
				{
					if (mouseX > 164 && mouseX < 299 && mouseY > 47 && mouseY < 153)
					{
						if (mouseY > 100)
							mouseCursor = MOUSE_POINTER_DOWN;
						else
							mouseCursor = MOUSE_POINTER_UP;
					}

					fill_rectangle_xy(VGAScreen, 160, 49, 310, 158, 228);
					if (yLoc + yChg < 0)
					{
						yChg = 0;
						yLoc = 0;
					}

					datacube_scroll_from = yLoc;
					yLoc += yChg;
					temp = yLoc / 12;
					temp2 = yLoc % 12;
					tempW = 38 + 12 - temp2;
					temp3 = cube[curSel[MENU_DATA_CUBES] - 2].last_line;

					for (int x = temp + 1; x <= temp + 10; x++)
					{
						if (x <= temp3)
						{
							JE_outTextAndDarken(VGAScreen, 161, tempW, cube[curSel[MENU_DATA_CUBES] - 2].text[x-1], 14, 3, TINY_FONT);
							tempW += 12;
						}
					}

					fill_rectangle_xy(VGAScreen, 160, 39, 310, 48, 228);
					fill_rectangle_xy(VGAScreen, 160, 157, 310, 166, 228);

					int percent_read = (cube[currentCube].last_line <= 9)
					                   ? 100
					                   : (yLoc * 100) / ((cube[currentCube].last_line - 9) * 12);

					char buf[55];
					snprintf(buf, sizeof(buf), "%s %d%%", miscText[11], percent_read);
					JE_outTextAndDarken(VGAScreen, 176, 160, buf, 14, 1, TINY_FONT);

					JE_dString(VGAScreen, 260, 160, miscText[12], SMALL_FONT_SHAPES);

					if (temp2 == 0)
						yChg = 0;

					const int scroll_step = yLoc - datacube_scroll_from;
					datacube_smooth = smoothMotion && effective_supersample() > 1 &&
					                  !backFromHelp && scroll_step != 0 && abs(scroll_step) <= 2;
					if (!datacube_smooth)
					{
						JE_mouseStart();

						JE_showVGA();

						if (backFromHelp)
						{
							fade_palette(colors, 10, 0, 255);
							backFromHelp = false;
						}
						JE_mouseReplace();
					}

					setDelay(1);
				}
				else
				{
					/* current menu is not 8 (read data cube) */

					if (curMenu == MENU_PLAY_NEXT_LEVEL)
					{
						// Remember the current pan, advance one tick, draw the authoritative
						// frame; JE_navScreenSmoothPresent interpolates between the two.
						nav_smooth_prev_x = navX;
						nav_smooth_prev_y = navY;
						JE_navScreenAdvance();
						JE_navDrawFrame(navX, navY);
					}

					if (curMenu == MENU_DATA_CUBES &&
					    curSel[MENU_DATA_CUBES] < menuChoices[MENU_DATA_CUBES])
					{
						/* Draw the animated cube. */
						blit_sprite_hv_blend(VGAScreenSeg, 166, 38 + (curSel[MENU_DATA_CUBES] - 2) * 28, OPTION_SHAPES, 25, 13, col);
					}

					if (menu_is_online_picker() ||
					    (curMenu == MENU_UPGRADE_SUB &&
					    (curSel[MENU_UPGRADES] == 3 ||
					     curSel[MENU_UPGRADES] == 4 ||
					     (curSel[MENU_UPGRADES] >= 6 &&
					      curSel[MENU_UPGRADES] <= 8))))
					{
						setDelay(3);

						// Record the weapon-sim frame for interpolated presentation
						// (preview region only). VGAScreen == VGAScreenSeg here.
						rl_begin_record();
						JE_weaponSimUpdate();
						JE_drawScore();
						rl_end_record();
						rl_finalize();
						// Capture preview overlays without changing the saved menu backdrop.
						rl_capture_residual(VGAScreenSeg, game_screen);

						service_SDL_events(false);

						if (newPal > 0)
						{
							curPal = newPal;
							set_palette(palettes[newPal - 1], 0, 255);
							newPal = 0;
						}

						if (paletteChanged)
						{
							set_palette(colors, 0, 255);
							paletteChanged = false;
						}

						if (backFromHelp)
						{
							fade_palette(colors, 10, 0, 255);
							backFromHelp = false;
						}

						JE_weaponSimSmoothPresent();  // cursor + showVGA happen inside

					}
					else if (curMenu == MENU_PLAY_NEXT_LEVEL)
					{
						// Planet nav screen: JE_navScreenSmoothPresent shows the pan
						// interpolated and consumes the whole tick (the wait below is a no-op).
						setDelay(2);

						if (newPal > 0)
						{
							curPal = newPal;
							set_palette(palettes[newPal - 1], 0, 255);
							newPal = 0;
						}

						if (paletteChanged)
						{
							set_palette(colors, 0, 255);
							paletteChanged = false;
						}

						JE_navScreenSmoothPresent();  // cursor + showVGA happen inside

						if (backFromHelp)
						{
							fade_palette(colors, 10, 0, 255);
							backFromHelp = false;
						}
					}
					else  /* current menu is anything but weapon sim or datacube */
					{
						setDelay(2);

						JE_drawScore();

						// The help bar is only composed when the selection changes, so the
						// online round-trip figure on it gets repainted here instead.
						refresh_help_bar_ping();

						if (newPal > 0)
						{
							curPal = newPal;
							set_palette(palettes[newPal - 1], 0, 255);
							newPal = 0;
						}

						JE_mouseStart();

						if (paletteChanged)
						{
							set_palette(colors, 0, 255);
							paletteChanged = false;
						}

						JE_showVGA(); /* SYN: This is the where the screen updates for most menus */

						JE_mouseReplace();

						if (backFromHelp)
						{
							fade_palette(colors, 10, 0, 255);
							backFromHelp = false;
						}

					}
				}

				if (curMenu == MENU_DATA_CUBE_SUB && datacube_smooth)
					datacube_smooth_present(&cube[curSel[MENU_DATA_CUBES] - 2],
					                        datacube_scroll_from, yLoc);
				else
					menuWaitWithSmoothCursor();  // was wait_delay(); keeps the cursor smooth

				push_joysticks_as_keyboard();
				service_SDL_events(false);
				mouseButton = JE_mousePosition(&mouseX, &mouseY);

				// Keep presenting the outpost until an acted-on press is released.
				if (!mousedown)
					awaitClickRelease = false;

				inputDetected = newkey || (mouseButton > 0 && !awaitClickRelease);

#if defined(__SWITCH__) || defined(__vita__)
				// Shoulder-button edges cycle the preview's rear-fire mode, matching
				// the [ and ] keys below.
				{
#if defined(__SWITCH__)
					static const int shoulder_btn[2] = { 6, 7 };  // switch-sdl2: 6 = L, 7 = R
#else
					static const int shoulder_btn[2] = { 4, 5 };  // Vita: 4 = L, 5 = R
#endif
					static bool shoulder_was[2];
					for (int s = 0; s < 2; ++s)
					{
						const bool down = joysticks > 0 && joystick[0].handle != NULL &&
						                  SDL_JoystickGetButton(joystick[0].handle, shoulder_btn[s]) != 0;

						if (down && !shoulder_was[s] &&
						    curMenu == MENU_UPGRADE_SUB && curSel[MENU_UPGRADES] == 4)
						{
							const uint opnum = weaponPort[shopPlayer()->items.weapon[REAR_WEAPON].id].opnum;
							if (s == 0)
								shopPlayer()->weapon_mode = (shopPlayer()->weapon_mode > 1) ? shopPlayer()->weapon_mode - 1 : opnum;
							else if (++shopPlayer()->weapon_mode > opnum)
								shopPlayer()->weapon_mode = 1;
						}
						shoulder_was[s] = down;
					}
				}
#endif

				/* Mouse wheel moves the buy/sell sub-list selection like the up/down
				   arrows; inputDetected makes the outer loop redraw at the new selection. */
				if (curMenu == MENU_UPGRADE_SUB && mouse_scroll != 0)
				{
					const int dir = (mouse_scroll > 0) ? -1 : 1;  // wheel up = towards the top
					int steps = (mouse_scroll > 0) ? mouse_scroll : -mouse_scroll;
					while (steps-- > 0)
					{
						lastDirection = dir;

						curSel[curMenu] += dir;
						if (curSel[curMenu] < 2)
							curSel[curMenu] = menuChoices[curMenu];
						else if (curSel[curMenu] > menuChoices[curMenu])
							curSel[curMenu] = 2;

						// keep the front/rear weapon power preview in sync, as the arrows do
						if (curSel[MENU_UPGRADES] == 3 || curSel[MENU_UPGRADES] == 4)
						{
							shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power = temp_weapon_power[curSel[MENU_UPGRADE_SUB]-2];
							if (curSel[MENU_UPGRADES] == 4)
								shopPlayer()->weapon_mode = 1;
						}
					}
					JE_playSampleNum(S_CURSOR);
					mouse_scroll = 0;
					inputDetected = true;  // redraw at the new selection
				}

				// Endless Hardcore forbids quick save/load. Online Campaign also blocks Alt+S while
				// an item preview is temporarily mutating the local loadout and cash.
				if (curMenu != MENU_LOAD_SAVE &&
				    (!(isNetworkGame && coop_mode_active()) || curMenu != MENU_UPGRADE_SUB) &&
				    !(endlessMode && endlessHardcore()))
				{
					if (keysactive[SDL_SCANCODE_S] && (keysactive[SDL_SCANCODE_LALT] || keysactive[SDL_SCANCODE_RALT]))
					{
						if (curMenu == MENU_DATA_CUBE_SUB ||
						    curMenu == MENU_DATA_CUBES)
						{
							curMenu = MENU_FULL_GAME;
						}
						quikSave = true;
						oldMenu = curMenu;
						curMenu = MENU_LOAD_SAVE;
						performSave = true;
						newPal = 1;
						oldPal = curPal;
					}
					// No quick-load online: loading is a host-side act at session start, and a
					// mid-session local load would fork the two sims on the spot.
					if (keysactive[SDL_SCANCODE_L] && (keysactive[SDL_SCANCODE_LALT] || keysactive[SDL_SCANCODE_RALT]) &&
					    !isNetworkGame)
					{
						if (curMenu == MENU_DATA_CUBE_SUB ||
						    curMenu == MENU_DATA_CUBES)
						{
							curMenu = MENU_FULL_GAME;
						}
						quikSave = true;
						oldMenu = curMenu;
						curMenu = MENU_LOAD_SAVE;
						performSave = false;
						newPal = 1;
						oldPal = curPal;
					}
				}

				if (curMenu == MENU_DATA_CUBE_SUB)
				{
					if (mouseButton > 0 && mouseCursor != MOUSE_POINTER_NORMAL)
					{
						inputDetected = false;
						if (mouseCursor == MOUSE_POINTER_UP)
							yChg = -1;
						else
							yChg = 1;
					}

					if (keysactive[SDL_SCANCODE_PAGEUP])
					{
						yChg = -2;
						inputDetected = false;
					}
					if (keysactive[SDL_SCANCODE_PAGEDOWN])
					{
						yChg = 2;
						inputDetected = false;
					}

					bool joystick_up = false, joystick_down = false;
					for (int j = 0; j < joysticks; j++)
					{
						joystick_up |= joystick[j].direction[0];
						joystick_down |= joystick[j].direction[2];
					}

					if (keysactive[SDL_SCANCODE_UP] || joystick_up)
					{
						yChg = -1;
						inputDetected = false;
					}

					if (keysactive[SDL_SCANCODE_DOWN] || joystick_down)
					{
						yChg = 1;
						inputDetected = false;
					}

					if (yChg < 0 && yLoc == 0)
					{
						yChg = 0;
					}
					if (yChg  > 0 && (yLoc / 12) > cube[currentCube].last_line - 10)
					{
						yChg = 0;
					}
				}

			} while (!inputDetected);
		}

		keyboardUsed = false;

		/* The rest of this just grabs input events, handles them, then proceeds on. */

		if (mouseButton > 0 && !awaitClickRelease)
		{
			lastDirection = 1;

			mouseButton = JE_mousePosition(&mouseX, &mouseY);

			if (curMenu == MENU_DATA_CUBES && cubeMax == 0)
			{
				curMenu = MENU_FULL_GAME;
				JE_playSampleNum(S_SPRING);
				newPal = 1;
			}

			if (curMenu == MENU_DATA_CUBE_SUB)
			{
				if ((mouseX > 258) && (mouseX < 290) && (mouseY > 159) && (mouseY < 171))
				{
					curMenu = MENU_DATA_CUBES;
					JE_playSampleNum(S_SPRING);
				}
			}

			if (curMenu == MENU_OPTIONS ||
			    curMenu == MENU_LIMITED_OPTIONS)
			{
				const int yMusic = OPTIONS_ROW_Y(options_row(OPT_MUSIC));
				const int ySound = OPTIONS_ROW_Y(options_row(OPT_SOUND));
				const int ySens  = OPTIONS_ROW_Y(options_row(OPT_SENS));

				if ((mouseX >= (225 - 4)) && (mouseY >= yMusic) && (mouseY <= yMusic + 12))
				{
					if (music_disabled)
					{
						music_disabled = false;
						restart_song();
					}

					curSel[curMenu] = options_row(OPT_MUSIC);

					// Same bar-width -> value mapping as the in-game Esc menu (continuous over
					// the full bar), instead of the old quantize-to-12 step.
					const int w = ((255 + 6) / 12) * (3 + 1) - 1;
					const int value = (mouseX - 225) * 255 / (w - 1);
					tyrMusicVolume = MIN(MAX(0, value), 255);
				}

				if ((mouseX >= (225 - 4)) && (mouseY >= ySound) && (mouseY <= ySound + 12))
				{
					samples_disabled = false;

					curSel[curMenu] = options_row(OPT_SOUND);

					const int w = ((255 + 6) / 12) * (3 + 1) - 1;
					const int value = (mouseX - 225) * 255 / (w - 1);
					fxVolume = MIN(MAX(0, value), 255);
				}

				// Ship sensitivity bar: drag to set, same feel as the volume bars.
				if ((mouseX >= (225 - 4)) && (mouseY >= ySens) && (mouseY <= ySens + 12))
				{
					curSel[curMenu] = options_row(OPT_SENS);

					const int w = ((SHIP_SENS_MAX + 6) / 12) * (3 + 1) - 1;
					const int value = (mouseX - 225) * SHIP_SENS_MAX / (w - 1);
					ship_sensitivity = MIN(MAX(0, value), SHIP_SENS_MAX);
				}

				set_volume(tyrMusicVolume, fxVolume);

				JE_playSampleNum(S_CURSOR);
			}

			if (mouseY > 20 && curMenu != MENU_DATA_CUBE_SUB)
			{
				int selection = menuChoices[curMenu] + 1; /* invalid by default */

				// The dye grid maps its own clicks; a press in a gap between cells is a miss.
				if (curMenu == MENU_ONLINE_COLOR)
				{
					for (int i = 0; i < online_color_cells; ++i)
					{
						int cx0, cy0, cx1, cy1;
						online_color_cell_rect(i, &cx0, &cy0, &cx1, &cy1);
						if (mouseX >= cx0 && mouseX <= cx1 && mouseY >= cy0 && mouseY <= cy1)
						{
							selection = i + 2;
							break;
						}
					}
				}
				else if (mouseX > 170 && mouseX < 308)
				{
					// E-Shop and perk choices use the same row pitch as buy/sell.
					const JE_byte mouseSelectionY[MENU_MAX] = { 16, 16, 16, 16, 26, 12, 11, 28, 0, 16, 16, 16, 8, 16, 24, 16, 16, 16, 16, 16, ONLINE_OPACITY_ROW_H, 16 };

					// The read-only perk LIST draws at tight 10px rows in a small font (JE_drawMenuChoices);
					// the forced perk PICK keeps the standard 16px pitch.
					const int rowPitch = (curMenu == MENU_ESHOP || (curMenu == MENU_PERKS && endlessPerkListMode)) ? 10 : mouseSelectionY[curMenu];
					selection = (mouseY - 38) / rowPitch + 2;

					/* The upgrade sub-list scrolls, so map the click through the
					   current scroll window; a click below the last visible row misses. */
					if (curMenu == MENU_UPGRADE_SUB)
					{
						const int vis = (mouseY - 38) / 26;
						selection = (vis >= 0 && vis < upgradeSubVisibleRows())
						            ? upgradeSubScrollTop + vis + 1
						            : menuChoices[curMenu] + 1;  // outside the list
					}

					/* The read-only Perks list scrolls too; map the click through its window
					   (10px rows), a click below the last visible row misses. */
					if (curMenu == MENU_PERKS && endlessPerkListMode)
					{
						const int vis = (mouseY - 38) / 10;
						const int tempW = perkListScrollTop + vis;  // 1-based row; curSel = tempW + 1
						selection = (vis >= 0 && vis < PERK_LIST_VIS && tempW >= 1 && tempW <= menuChoices[curMenu] - 1)
						            ? tempW + 1
						            : menuChoices[curMenu] + 1;  // outside the visible list
					}

					if (curMenu == MENU_2_PLAYER_ARCADE && outpost_gaps_drawn(curMenu))
					{
						if (selection > 5)
							selection--;
						if (selection > 3)
							selection--;
					}

					if (curMenu == MENU_FULL_GAME && outpost_gaps_drawn(curMenu))
					{
						// First row below the Start Level separator.
						const int gap = outpost_row(curMenu, 7);
						if (debugMode)
						{
							// The separator shifts the debug rows and Quit.
							if (selection > gap)
								selection--;
						}
						else
						{
							// Clamp the separator and everything below it to Quit.
							if (selection > gap)
								selection = gap;
						}
					}

					/* is play next level screen? */
					if (curMenu == MENU_PLAY_NEXT_LEVEL)
					{
						/* Its trailing rows are each drawn a blank line below their menu index, so a
						 * click lands one row low. The blank above the reroll row is a miss. */
						const int reroll = endlessCourseRerollRow();
						if (reroll != 0 && selection == reroll)
							selection = menuChoices[curMenu] + 1;
						else if (reroll != 0 && selection == reroll + 1)
							selection = reroll;
						else if (selection == menuChoices[curMenu] + 1)
							selection = menuChoices[curMenu];
					}

					// Map clicks below the blank gap to the final action row.
					if (((curMenu == MENU_PERKS && !endlessPerkListMode) || curMenu == MENU_ESHOP) && selection > menuChoices[curMenu])
						selection = menuChoices[curMenu];
				}

				if (selection <= menuChoices[curMenu])
				{
					if (curMenu == MENU_UPGRADE_SUB &&
						selection == menuChoices[MENU_UPGRADE_SUB])
					{
						player_set_cash(shopPlayer(), JE_cashLeft());
						endlessShopTradeCommit();
						network_shop_send_transaction();
						curMenu = MENU_UPGRADES;
						JE_playSampleNum(S_ITEM);
					}
					else
					{
						JE_playSampleNum(S_CLICK);
						if (curSel[curMenu] == selection)
						{
							JE_menuFunction(curSel[curMenu]);
						}
						else
						{
							if (curMenu == MENU_UPGRADE_SUB &&
								JE_getCost(curSel[MENU_UPGRADES], itemAvail[itemAvailMap[curSel[MENU_UPGRADES] - 2] - 1][selection - 2]) > shopPlayer()->cash)
							{
								JE_playSampleNum(S_CLINK);
							}
							else
							{
								if (curSel[MENU_UPGRADES] == 4)
									shopPlayer()->weapon_mode = 1;

								curSel[curMenu] = selection;
							}

							/* in front or rear weapon upgrade screen? */
							if (curMenu == MENU_UPGRADE_SUB &&
								(curSel[MENU_UPGRADES] == 3 || curSel[MENU_UPGRADES] == 4))
							{
								shopPlayer()->items.weapon[curSel[MENU_UPGRADES] - 3].power = temp_weapon_power[curSel[MENU_UPGRADE_SUB] - 2];
							}
						}
					}
				}

				awaitClickRelease = true;
			}

			if (curMenu == MENU_UPGRADE_SUB &&
			    (curSel[MENU_UPGRADES] == 3 || curSel[MENU_UPGRADES] == 4))
			{
				if ((mouseX >= 23) && (mouseX <= 36) && (mouseY >= 149) && (mouseY <= 168))
				{
					JE_playSampleNum(S_CURSOR);
					switch (curSel[MENU_UPGRADES])
					{
					case 3:
					case 4:
						if (leftPower)
							shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power = --temp_weapon_power[curSel[MENU_UPGRADE_SUB]-2];
						else
							JE_playSampleNum(S_CLINK);

						break;
					}
					awaitClickRelease = true;
				}

				if ((mouseX >= 119) && (mouseX <= 131) && (mouseY >= 149) && (mouseY <= 168))
				{
					JE_playSampleNum(S_CURSOR);
					switch (curSel[MENU_UPGRADES])
					{
					case 3:
					case 4:
						if (rightPower && rightPowerAfford)
							shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power = ++temp_weapon_power[curSel[MENU_UPGRADE_SUB]-2];
						else
							JE_playSampleNum(S_CLINK);

						break;
					}
					awaitClickRelease = true;
				}
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_SLASH:
				// if in rear weapon upgrade screen
				if (curMenu == MENU_UPGRADE_SUB && curSel[MENU_UPGRADES] == 4)
				{
					// cycle weapon modes
					if (++shopPlayer()->weapon_mode > weaponPort[shopPlayer()->items.weapon[REAR_WEAPON].id].opnum)
						shopPlayer()->weapon_mode = 1;
				}
				break;

			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
				keyboardUsed = true;

				// if front or rear weapon, update "Done" power level
				if (curMenu == MENU_UPGRADE_SUB && (curSel[MENU_UPGRADES] == 3 || curSel[MENU_UPGRADES] == 4))
					temp_weapon_power[itemAvailMax[itemAvailMap[curSel[MENU_UPGRADES]-2]-1]] = shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power;

				JE_menuFunction(curSel[curMenu]);
				break;

			case SDL_SCANCODE_ESCAPE:
				keyboardUsed = true;

				// Esc takes cash on a forced pick and backs out of the read-only list.
				if (curMenu == MENU_PERKS)
				{
					if (endlessPerkListMode)
						endlessPerkListMode = false;   // fall through to the normal Esc handling
					else
					{
						JE_menuFunction(menuChoices[MENU_PERKS]);
						break;
					}
				}

				JE_playSampleNum(S_SPRING);
				if (curMenu == MENU_LOAD_SAVE && quikSave)
				{
					curMenu = oldMenu;
					newPal = oldPal;
				}
				else if (curMenu == MENU_LIMITED_OPTIONS || curMenu == MENU_ONLINE)
				{
					newPal = 1;
					curMenu = outpost_menu();
				}
				else if (menuEsc[curMenu] == 0)
				{
					if (JE_quitRequest())
					{
						gameLoaded = true;
						mainLevel = 0;
					}
				}
				else
				{
					if (curMenu == MENU_UPGRADE_SUB)  // leaving upgrade menu without buying
					{
						shopPlayer()->items = old_items[shopPlayerIndex];
						curSel[MENU_UPGRADE_SUB] = lastCurSel;
						player_set_cash(shopPlayer(), JE_cashLeft());
						endlessShopTradeCommit();
						network_shop_send_transaction();
					}

					if (curMenu != MENU_DATA_CUBE_SUB)
						newPal = 1;

					curMenu = menuEsc[curMenu] - 1;

					// Return online option sub-screens to the online page.
					if (curMenu == MENU_OPTIONS && isNetworkGame)
						curMenu = MENU_LIMITED_OPTIONS;
				}
				break;

			case SDL_SCANCODE_F1:
				if (!isNetworkGame)
				{
					fade_black(10);
					JE_helpSystem(2);

					play_song(songBuy);

					JE_loadPic(VGAScreen, 1, false);
					newPal = 1;

					switch (curMenu)
					{
					case 3:
						newPal = 18;
						break;
					case 7:
					case 8:
						break;
					}

					memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

					curPal = newPal;
					memcpy(colors, palettes[newPal-1], sizeof(colors));
					JE_showVGA();
					newPal = 0;
					backFromHelp = true;
				}
				break;

			case SDL_SCANCODE_UP:
				keyboardUsed = true;
				lastDirection = -1;

				if (curMenu != MENU_DATA_CUBE_SUB)
					JE_playSampleNum(S_CURSOR);

				// The dye picker is a grid, so up and down cross a whole row of it.
				if (curMenu == MENU_ONLINE_COLOR)
				{
					curSel[curMenu] = online_color_step(curSel[curMenu], -ONLINE_COLOR_COLS);
					break;
				}

				curSel[curMenu]--;
				if (curSel[curMenu] < 2)
					curSel[curMenu] = menuChoices[curMenu];

				// if in front or rear weapon upgrade screen
				if (curMenu == MENU_UPGRADE_SUB &&
				    (curSel[MENU_UPGRADES] == 3 || curSel[MENU_UPGRADES] == 4))
				{
					shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power = temp_weapon_power[curSel[MENU_UPGRADE_SUB]-2];
					if (curSel[MENU_UPGRADES] == 4)
						shopPlayer()->weapon_mode = 1;
				}

				// if joystick config, skip disabled items when digital
				if (curMenu == MENU_JOYSTICK_CONFIG &&
				    joysticks > 0 &&
				    !joystick[joystick_config].analog &&
				    curSel[curMenu] == 5)
				{
					curSel[curMenu] = 3;
				}

				break;

			case SDL_SCANCODE_DOWN:
				keyboardUsed = true;
				lastDirection = 1;

				if (curMenu != MENU_DATA_CUBE_SUB)
					JE_playSampleNum(S_CURSOR);

				if (curMenu == MENU_ONLINE_COLOR)
				{
					curSel[curMenu] = online_color_step(curSel[curMenu], ONLINE_COLOR_COLS);
					break;
				}

				curSel[curMenu]++;
				if (curSel[curMenu] > menuChoices[curMenu])
					curSel[curMenu] = 2;

				// if in front or rear weapon upgrade screen
				if (curMenu == MENU_UPGRADE_SUB &&
				    (curSel[MENU_UPGRADES] == 3 || curSel[MENU_UPGRADES] == 4))
				{
					shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power = temp_weapon_power[curSel[MENU_UPGRADE_SUB]-2];
					if (curSel[MENU_UPGRADES] == 4)
						shopPlayer()->weapon_mode = 1;
				}

				// if in joystick config, skip disabled items when digital
				if (curMenu == MENU_JOYSTICK_CONFIG &&
				    joysticks > 0 &&
				    !joystick[joystick_config].analog &&
				    curSel[curMenu] == 4)
				{
					curSel[curMenu] = 6;
				}

				break;

			case SDL_SCANCODE_HOME:
				if (curMenu == MENU_DATA_CUBE_SUB)
					yLoc = 0;
				break;

			case SDL_SCANCODE_END:
				if (curMenu == MENU_DATA_CUBE_SUB)
					yLoc = (cube[currentCube].last_line - 9) * 12;
				break;

			case SDL_SCANCODE_LEFT:
				// Left and right step one cell of the dye grid; up and down cross a whole row.
				if (curMenu == MENU_ONLINE_COLOR)
				{
					JE_playSampleNum(S_CURSOR);
					curSel[curMenu] = online_color_step(curSel[curMenu], -1);
					break;
				}

				if (curMenu == MENU_JOYSTICK_CONFIG)
				{
					if (joysticks > 0)
					{
						switch (curSel[curMenu])
						{
							case 2:
								if (joystick_config == 0)
									joystick_config = joysticks;
								joystick_config--;
								break;
							case 3:
								joystick[joystick_config].analog = !joystick[joystick_config].analog;
								break;
							case 4:
								if (joystick[joystick_config].sensitivity == 0)
									joystick[joystick_config].sensitivity = 10;
								else
									joystick[joystick_config].sensitivity--;
								break;
							case 5:
								if (joystick[joystick_config].threshold == 0)
									joystick[joystick_config].threshold = 10;
								else
									joystick[joystick_config].threshold--;
								break;
							default:
								break;
						}
					}
				}

				if (curMenu == MENU_2_PLAYER_ARCADE)
				{
					switch (curSel[curMenu])
					{
					case 3:
					case 4:
						JE_playSampleNum(S_CURSOR);

						int temp = curSel[curMenu] - 3;
						do
						{
							if (joysticks == 0)
								inputDevice[temp == 0 ? 1 : 0] = inputDevice[temp]; // swap controllers
							if (inputDevice[temp] <= 1)
								inputDevice[temp] = 2 + joysticks;
							else
								inputDevice[temp]--;
						} while (inputDevice[temp] == inputDevice[temp == 0 ? 1 : 0]);
						break;
					}
				}

				if (curMenu == MENU_OPTIONS ||
				    curMenu == MENU_UPGRADE_SUB ||
				    curMenu == MENU_LIMITED_OPTIONS)
				{
					JE_playSampleNum(S_CURSOR);
				}

				switch (curMenu)
				{
				case MENU_OPTIONS:
				case MENU_LIMITED_OPTIONS:
					switch (options_full_row(curSel[curMenu]))
					{
					case OPT_MUSIC:
						JE_changeVolume(&tyrMusicVolume, -12, &fxVolume, 0);
						if (music_disabled)
						{
							music_disabled = false;
							restart_song();
						}
						break;
					case OPT_SOUND:
						JE_changeVolume(&tyrMusicVolume, 0, &fxVolume, -12);
						samples_disabled = false;
						break;
					case OPT_SENS:
						ship_sensitivity -= 12;
						if (ship_sensitivity < 0)
							ship_sensitivity = 0;
						break;
					}
					break;
				case 4:
					switch (curSel[MENU_UPGRADES])
					{
					case 3:
					case 4:
						if (leftPower)
							shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power = --temp_weapon_power[curSel[MENU_UPGRADE_SUB]-2];
						else
							JE_playSampleNum(S_CLINK);

						break;
					}
					break;
				}
				break;

			case SDL_SCANCODE_RIGHT:
				if (curMenu == MENU_ONLINE_COLOR)
				{
					JE_playSampleNum(S_CURSOR);
					curSel[curMenu] = online_color_step(curSel[curMenu], 1);
					break;
				}

				if (curMenu == MENU_JOYSTICK_CONFIG)
				{
					if (joysticks > 0)
					{
						switch (curSel[curMenu])
						{
							case 2:
								joystick_config++;
								joystick_config %= joysticks;
								break;
							case 3:
								joystick[joystick_config].analog = !joystick[joystick_config].analog;
								break;
							case 4:
								joystick[joystick_config].sensitivity++;
								joystick[joystick_config].sensitivity %= 11;
								break;
							case 5:
								joystick[joystick_config].threshold++;
								joystick[joystick_config].threshold %= 11;
								break;
							default:
								break;
						}
					}
				}

				if (curMenu == MENU_2_PLAYER_ARCADE)
				{
					switch (curSel[curMenu])
					{
					case 3:
					case 4:
						JE_playSampleNum(S_CURSOR);

						int temp = curSel[curMenu] - 3;
						do
						{
							if (joysticks == 0)
								inputDevice[temp == 0 ? 1 : 0] = inputDevice[temp]; // swap controllers
							if (inputDevice[temp] >= 2 + joysticks)
								inputDevice[temp] = 1;
							else
								inputDevice[temp]++;
						} while (inputDevice[temp] == inputDevice[temp == 0 ? 1 : 0]);
						break;
					}
				}

				if (curMenu == MENU_OPTIONS ||
				    curMenu == MENU_UPGRADE_SUB ||
				    curMenu == MENU_LIMITED_OPTIONS)
				{
					JE_playSampleNum(S_CURSOR);
				}

				switch (curMenu)
				{
				case MENU_OPTIONS:
				case MENU_LIMITED_OPTIONS:
					switch (options_full_row(curSel[curMenu]))
					{
					case OPT_MUSIC:
						JE_changeVolume(&tyrMusicVolume, 12, &fxVolume, 0);
						if (music_disabled)
						{
							music_disabled = false;
							restart_song();
						}
						break;
					case OPT_SOUND:
						JE_changeVolume(&tyrMusicVolume, 0, &fxVolume, 12);
						samples_disabled = false;
						break;
					case OPT_SENS:
						ship_sensitivity += 12;
						if (ship_sensitivity > SHIP_SENS_MAX)
							ship_sensitivity = SHIP_SENS_MAX;
						break;
					}
					break;
				case 4:
					switch (curSel[MENU_UPGRADES])
					{
					case 3:
					case 4:
						if (rightPower && rightPowerAfford)
							shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power = ++temp_weapon_power[curSel[MENU_UPGRADE_SUB]-2];
						else
							JE_playSampleNum(S_CLINK);

						break;
					}
					break;
				}
				break;

			default:
				break;
			}
		}

		// Keep departure inside the menu loop so a withdrawn Campaign commit can resume shopping.
		if (isNetworkGame && !quit && (gameLoaded || jumpSection))
			shopLeaveOutpost(&outpostRoute);

	} while (!(quit || gameLoaded || jumpSection));

	netStylePreviewClear();

	if (gameLoaded)
		fade_black(10);

	/* Keep the buy/sell screen centered until after the fade when jumping to
	   the next level.  Otherwise, reset the offset now. */
	if (!jumpSection)
		set_menu_centered(false);
}

void draw_ship_illustration(void)
{
	// full of evil hardcoding

	// ship
	{
		assert(shopPlayer()->items.ship > 0);

		const int sprite_id = (shopPlayer()->items.ship < COUNTOF(ships))  // shipedit ships get a default
		                      ? ships[shopPlayer()->items.ship].bigshipgraphic - 1
		                      : 31;

		const int ship_x[] = { 31, 0, 0, 0, 35, 31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 36, 30 },
		          ship_y[] = { 36, 0, 0, 0, 33, 35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 33, 30 };

		const int x = ship_x[sprite_id - 27],
		          y = ship_y[sprite_id - 27];

		// Gencore II's opaque square corner covers part of the lighter panel bevel.
		// Save the bevel first and restore it after drawing the sprite.
		Uint8 * const seg = (Uint8 *)VGAScreenSeg->pixels;
		const int segpitch = VGAScreenSeg->pitch;
		const bool patch_corner = (sprite_id == 45);
		Uint8 under[3] = { 0 };
		if (patch_corner)
		{
			under[0] = seg[(y    ) * segpitch + (x + 93)];
			under[1] = seg[(y    ) * segpitch + (x + 94)];
			under[2] = seg[(y + 1) * segpitch + (x + 94)];
		}

		blit_sprite(VGAScreenSeg, x, y, OPTION_SHAPES, sprite_id);

		if (patch_corner)
		{
			seg[(y    ) * segpitch + (x + 93)] = under[0];
			seg[(y    ) * segpitch + (x + 94)] = under[1];
			seg[(y + 1) * segpitch + (x + 94)] = under[2];
		}
	}

	// generator
	{
		assert(shopPlayer()->items.generator > 0 && shopPlayer()->items.generator < 7);

		const int sprite_id = (shopPlayer()->items.generator == 1)  // generator 1 and generator 2 have the same sprite
		                      ? shopPlayer()->items.generator + 15
		                      : shopPlayer()->items.generator + 14;

		const int generator_x[5] = { 62, 64, 67, 66, 63 },
		          generator_y[5] = { 84, 85, 86, 84, 97 };
		const int x = generator_x[sprite_id - 16],
		          y = generator_y[sprite_id - 16];

		blit_sprite(VGAScreenSeg, x, y, WEAPON_SHAPES, sprite_id);
	}

	const int weapon_sprites[60] =
	{
		-1,  0,  1,  2,  3,  4,  5,  6,  7,  8,
		 9, 10, 11, 21,  5, 13, -1, 14, 15,  0,
		14,  9,  8,  2, 15,  0, 13,  0,  8,  8,
		11,  1,  0,  0,  0,  0,  0,  0,  0,  0,
		 0,  2,  1,  0,  0,  1,  1,  1
	};

	// Center each weapon by its sprite; fallback mounts may have a different width.
	const int weapon_sprite_x[22] =
	{
		61, 52, 54, 61, 66, 51, 53, 66, 41, 27,
		49, 43, 43, 39, 59, 41, 62, 63, 67, 66,
		63, 51
	};

	// Fall back to a weapon's native mount table when Endless puts it in the other bay.
	const int front_weapon_xy_list[60] =
	{
		 -1,  4,  9,  3,  8,  2,  5, 10,  1, -1,
		 -1, -1, -1,  7,  8, -1, -1,  0, -1,  4,
		  0, -1, -1,  3, -1,  4, -1,  4, -1, -1,
		 -1,  9,  0,  0,  0,  0,  0,  0,  0,  0,
		  0,  3,  9,  4,  4,  9,  9,  9
	};
	const int front_weapon_y[12] = { 38, 53, 41, 36, 48, 35, 41, 35, 53, 41, 39, 31 };

	const int rear_weapon_xy_list[60] =
	{
		-1, -1, -1, -1, -1, -1, -1, -1, -1,  0,
		 1,  2,  3, -1,  4,  5, -1, -1,  6, -1,
		-1,  1,  0, -1,  6, -1,  5, -1,  0,  0,
		 3,  0,  0,  0,  0,  0,  0,  0,  0,  0,
		 0, -1, -1, -1, -1, -1, -1, -1
	};
	const int rear_weapon_y[7] = { 92, 92, 113, 102, 97, 96, 76 };

	for (int slot = FRONT_WEAPON; slot <= REAR_WEAPON; ++slot)
	{
		const int id = shopPlayer()->items.weapon[slot].id;
		if (id <= 0 || id >= 60 || weapon_sprites[id] < 0)
			continue;

		const int fi = front_weapon_xy_list[id];  // front-mount index, or -1
		const int ri = rear_weapon_xy_list[id];   // rear-mount index, or -1

		// Unauthored special weapon (fill default in both tables): pin to front.
		const bool front_only = (fi == 0 && ri == 0);

		int use_fi = -1, use_ri = -1;
		if (front_only)
			use_fi = fi;
		else if (slot == FRONT_WEAPON)  // prefer front, fall back to rear
		{
			if (fi >= 0) use_fi = fi;
			else         use_ri = ri;
		}
		else  // REAR_WEAPON: prefer rear, fall back to front
		{
			if (ri >= 0) use_ri = ri;
			else         use_fi = fi;
		}

		int y;
		if (use_fi >= 0)
			y = front_weapon_y[use_fi];
		else if (use_ri >= 0)
			y = rear_weapon_y[use_ri];
		else
			continue;  // no illustration mount point for this weapon

		blit_sprite(VGAScreenSeg, weapon_sprite_x[weapon_sprites[id]], y, WEAPON_SHAPES, weapon_sprites[id]);
	}

	// sidekicks
	JE_drawItem(6, shopPlayer()->items.sidekick[LEFT_SIDEKICK], 3, 84);
	JE_drawItem(7, shopPlayer()->items.sidekick[RIGHT_SIDEKICK], 129, 84);

	// shield
	blit_sprite_hv(VGAScreenSeg, 28, 23, OPTION_SHAPES, 26, 15, shields[shopPlayer()->items.shield].mpwr - 10);
}

void load_cubes(void)
{
	for (int cube_slot = 0; cube_slot < cubeMax; ++cube_slot)
	{
		memset(cube[cube_slot].text, 0, sizeof(cube->text));

		load_cube(cube_slot, cubeList[cube_slot]);
	}
}

bool load_cube(int cube_slot, int cube_index)
{
	FILE *f = dir_fopen_die(JE_episodeDir(), cube_file, "rb");

	char buf[256];

	// seek to the cube
	while (cube_index > 0)
	{
		read_encrypted_pascal_string(buf, sizeof(buf), f);
		if (buf[0] == '*')
			--cube_index;
	}

	str_pop_int(&buf[4], &cube[cube_slot].face_sprite);
	--cube[cube_slot].face_sprite;

	read_encrypted_pascal_string(cube[cube_slot].title, sizeof(cube[cube_slot].title), f);
	read_encrypted_pascal_string(cube[cube_slot].header, sizeof(cube[cube_slot].header), f);

	uint line = 0, line_chars = 0, line_width = 0;

	// for each line of decrypted text, split the line into words
	// and add them individually to the lines of wrapped text
	for (; ; )
	{
		read_encrypted_pascal_string(buf, sizeof(buf), f);

		// end of data
		if (buf[0] == '*')
			break;

		// new paragraph
		if (strlen(buf) == 0)
		{
			if (line_chars == 0)
				line += 4;  // subsequent new paragaphs indicate 4-line break
			else
				++line;
			line_chars = 0;
			line_width = 0;

			continue;
		}

		uint word_start = 0;
		for (uint i = 0; ; ++i)
		{
			bool end_of_line = (buf[i] == '\0'),
			     end_of_word = end_of_line || (buf[i] == ' ');

			if (end_of_word)
			{
				buf[i] = '\0';

				char *word = &buf[word_start];
				word_start = i + 1;

				uint word_chars = strlen(word),
				     word_width = JE_textWidth(word, TINY_FONT);

				// word won't fit; no can do
				if (word_chars > cube_line_chars || word_width > cube_line_width)
					break;

				bool prepend_space = true;

				line_chars += word_chars + (prepend_space ? 1 : 0);
				line_width += word_width + (prepend_space ? 6 : 0);

				// word won't fit on current line; use next
				if (line_chars > cube_line_chars || line_width > cube_line_width)
				{
					++line;
					line_chars = word_chars;
					line_width = word_width;

					prepend_space = false;
				}

				// append word
				if (line < COUNTOF(cube->text))
				{
					if (prepend_space)
						strcat(cube[cube_slot].text[line], " ");
					strcat(cube[cube_slot].text[line], word);

					// track last line with text
					cube[cube_slot].last_line = line + 1;
				}
			}

			if (end_of_line)
				break;
		}
	}

	fclose(f);

	return true;
}

// Apply the previewed seat's style only to ship art.
static void blit_preview_ship2x2(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	const NetShipStyle style = netStyleForSeat(shop_draw_seat());
	blit_sprite2x2_alpha(surface, x, y, sheet, index, style.bank, style.opacity);
}

static void blit_preview_ship2(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	const NetShipStyle style = netStyleForSeat(shop_draw_seat());
	blit_sprite2_alpha(surface, x, y, sheet, index, style.bank, style.opacity);
}

// Sentinel graphics, not items.ship, identify Linked Arcade's second hull.
static JE_word shop_seat_hull(uint seat, Sprite2_array **sheet)
{
	*sheet = (seat == 1) ? shipGr2ptr : shipGrPtr;
	return (seat == 1) ? shipGr2 : shipGr;
}

// Graphics 0 and 1 are the wide Dragonwing and Nort Ship sentinels.
static void draw_preview_hull(uint seat, int x, int y)
{
	Sprite2_array *sheet = NULL;
	const JE_word gr = shop_seat_hull(seat, &sheet);
	if (sheet == NULL)
		return;

	if (gr == 0)
	{
		blit_preview_ship2x2(VGAScreen, x - SHOP_WIDE_HULL_HALF, y, *sheet, 13);
		blit_preview_ship2x2(VGAScreen, x + SHOP_WIDE_HULL_HALF, y, *sheet, 51);
	}
	else if (gr == 1)
	{
		blit_preview_ship2x2(VGAScreen, x - SHOP_WIDE_HULL_HALF, y, *sheet, 220);
		blit_preview_ship2x2(VGAScreen, x + SHOP_WIDE_HULL_HALF, y, *sheet, 222);
	}
	else
	{
		blit_preview_ship2x2(VGAScreen, x, y, *sheet, gr);
	}
}

void JE_drawItem(JE_byte itemType, JE_word itemNum, JE_word x, JE_word y)
{
	JE_word tempW = 0;

	if (itemNum > 0)
	{
		switch (itemType)
		{
			case 2:
			case 3:
				tempW = weaponPort[itemNum].itemgraphic;
				break;
			case 5:
				tempW = powerSys[itemNum].itemgraphic;
				break;
			case 6:
			case 7:
				tempW = options[itemNum].itemgraphic;
				break;
			case 4:
				tempW = shields[itemNum].itemgraphic;
				break;
		}

		if (itemType == 1)
		{
			if (itemNum > 90)
			{
				shipGrPtr = &spriteSheet9;
				shipGr = JE_SGr(shop_draw_seat(), itemNum - 90, &shipGrPtr);
				if (shipGr == 0)
				{
					blit_preview_ship2x2(VGAScreen, x - SHOP_WIDE_HULL_HALF, y, *shipGrPtr, 13);
					blit_preview_ship2x2(VGAScreen, x + SHOP_WIDE_HULL_HALF, y, *shipGrPtr, 51);
				}
				else if (shipGr == 1)
				{
					blit_preview_ship2x2(VGAScreen, x - SHOP_WIDE_HULL_HALF, y, *shipGrPtr, 220);
					blit_preview_ship2x2(VGAScreen, x + SHOP_WIDE_HULL_HALF, y, *shipGrPtr, 222);
				}
				else
					blit_preview_ship2x2(VGAScreen, x, y, *shipGrPtr, shipGr);
			}
			else if (ships[itemNum].shipgraphic == 0)
			{
				// The Dragonwing's shipgraphic (0) is a sentinel too (see the Nort Ship below):
				// draw the two flat-banking halves the gameplay draw uses.
				blit_preview_ship2x2(VGAScreen, x - SHOP_WIDE_HULL_HALF, y, spriteSheet9, 13);
				blit_preview_ship2x2(VGAScreen, x + SHOP_WIDE_HULL_HALF, y, spriteSheet9, 51);
			}
			else if (ships[itemNum].shipgraphic == 1)
			{
				// The Nort Ship's shipgraphic (1) is a sentinel, not a real sprite index; the gameplay draw
				// (JE_playerMovement) special-cases it into a two-piece hull.
				blit_preview_ship2x2(VGAScreen, x - SHOP_WIDE_HULL_HALF, y, spriteSheet9, 220);
				blit_preview_ship2x2(VGAScreen, x + SHOP_WIDE_HULL_HALF, y, spriteSheet9, 222);
			}
			else if (ships[itemNum].shipgraphic > 500)
			{
				blit_preview_ship2x2(VGAScreen, x, y, spriteSheetT2000, ships[itemNum].shipgraphic - 500);
			}
			else
			{
				blit_preview_ship2x2(VGAScreen, x, y, spriteSheet9, ships[itemNum].shipgraphic);
			}
		}
		else if (tempW > 0)
		{
			blit_sprite2x2(VGAScreen, x, y, shopSpriteSheet, tempW);
		}
	}
}

/* The Nort Ship and Dragonwing use 48px icons in a 24px column. See
 * doc/notes.md#extra-ships. */
#define SHOP_NAME_X_DRAGONWING 209  // its halves paint through x=207
#define SHOP_NAME_X_NORT_SHIP  203  // its halves paint through x=201

ShopItemColumns shop_ship_item_columns(JE_word shipId)
{
	ShopItemColumns cols = { SHOP_ITEM_ICON_X, SHOP_ITEM_NAME_X, SHOP_ITEM_COST_X };

	// shipgraphic 0 is the Dragonwing and 1 the Nort Ship; every other hull is a single 2x2.
	if (shipId == 0 || shipId > SHIP_DRAGONWING || ships[shipId].shipgraphic > 1)
		return cols;

	cols.iconX += SHOP_WIDE_HULL_HALF;
	cols.nameX = (ships[shipId].shipgraphic == 0) ? SHOP_NAME_X_DRAGONWING : SHOP_NAME_X_NORT_SHIP;
	cols.costX = cols.nameX + (SHOP_ITEM_COST_X - SHOP_ITEM_NAME_X);
	return cols;
}

#define SHOP_OPTION_WEAPON_PORT 16  // holds the sidekick weapon table; no bay issues a gun from it

/* The ports the shipped game issues as rear guns. See doc/notes.md#item-tables. */
static const JE_byte shopRearGunPorts[] = { 9, 10, 11, 12, 14, 15, 18, 21, 22, 24, 26, 28, 29, 30, 37, 38 };

ShopWeaponBay shop_weapon_port_bay(JE_word port)
{
	if (port == 0 || port == SHOP_OPTION_WEAPON_PORT ||
	    port > SHOP_REAL_WEAPON_PORTS || customWeaponPortIsCustom(port))
		return SHOP_BAY_UNKNOWN;

	for (uint i = 0; i < COUNTOF(shopRearGunPorts); ++i)
		if (shopRearGunPorts[i] == port)
			return SHOP_BAY_REAR;

	return SHOP_BAY_FRONT;
}

const char *shop_weapon_row_tag(JE_word port, bool rearList, bool mixedBays)
{
	if (port == 0 || port > PORT_NUM)
		return NULL;  // None, and any id past the port table

	// The rear list gives the Dual-Mode tag the column. A two-mode port is always a rear gun,
	// so it only ever covers a Rear tag.
	if (rearList && weaponPort[port].opnum == 2)
		return SHOP_DUAL_MODE_TAG;

	if (!mixedBays)
		return NULL;

	switch (shop_weapon_port_bay(port))
	{
	case SHOP_BAY_FRONT: return SHOP_FRONT_GUN_TAG;
	case SHOP_BAY_REAR:  return SHOP_REAR_GUN_TAG;
	default:             return NULL;
	}
}

int shop_row_tag_x(int costRight, int tagW, int markerX)
{
	const int costEnd = costRight + 4;  // a word's gap after the cost text

	int x = markerX - SHOP_ROW_TAG_MARKER_GAP - tagW;
	if (x < costEnd)
		x = MIN(costEnd, SHOP_ITEM_LIST_RIGHT - tagW);
	return x;
}

void JE_drawMenuHeader(void)
{
	switch (curMenu)
	{
		case MENU_DATA_CUBE_SUB:
			strcpy(tempStr, cube[curSel[MENU_DATA_CUBES]-2].header);
			break;
		case MENU_DATA_CUBES:
			strcpy(tempStr, menuInt[1][1]);
			break;
		case MENU_LOAD_SAVE:
			strcpy(tempStr, menuInt[3][performSave + 1]);
			break;
		default:
			strcpy(tempStr, menuInt[curMenu + 1][0]);
			break;
	}
	JE_dString(VGAScreen, 74 + JE_fontCenter(tempStr, FONT_SHAPES), 10, tempStr, FONT_SHAPES);
}

// Endless E-Shop: tint each buy row by WHAT IT IS, so related purchases share a colour and the
// player can read the menu at a glance.
static unsigned int endless_eshop_row_bank(JE_byte x)
{
	switch (x)
	{
	case 7: case 8: case 9:  return 12;  // Turbodrive / Overblast / Overdrive; kill-fire BUFFS (green)
	case 4: case 5:          return  8;  // Reinforce / Extra Perk            ; permanent UPGRADES (cyan)
	case 6:                  return  4;  // Special Weapon                    ; OFFENSE (red)
	case 10: case 11:        return  5;  // Revive / Bomb                     ; held CONSUMABLES (purple)
	case 12:                 return  7;  // Gamble                            ; RISK (fiery red/yellow)
	default:                 return 15;  // Reroll / Sabotage / Done          ; neutral shop actions (gold)
	}
}

void JE_drawMenuChoices(void)
{
	JE_byte x;
	char *str;
	// Endless read-only perk list: the normal buy/sell menu, just drawn in a small font at tight rows.
	const bool perkList = (curMenu == MENU_PERKS && endlessPerkListMode);
	// The endless E-Shop holds many items, so it reuses the same compact small-font / tight-row list.
	const bool tightFont = perkList || (curMenu == MENU_ESHOP);

	for (x = 2; x <= menuChoices[curMenu]; x++)
	{
		int line_height = tightFont ? 10 : 16;
		int tempY = 38 + (x - 1) * line_height;
		tempY += outpost_row_gap_px(curMenu, x);

		// Endless perk menu / E-Shop: a blank line separating the buys from the final Done row.
		if ((curMenu == MENU_PERKS || curMenu == MENU_ESHOP) && x == menuChoices[curMenu])
		{
			tempY += tightFont ? 10 : 16;
		}

		/* Chart a Course keeps a blank line above its trailing rows: Exit, and the endless Radar
		 * reroll when the visit offers one. Everything above them closes up by one row. */
		if (!(curMenu == MENU_PLAY_NEXT_LEVEL &&
			(x == menuChoices[curMenu] || x == endlessCourseRerollRow())))
		{
			tempY -= line_height;
		}

		if (curMenu == MENU_MOUSE_CONFIG)
		{
			tempY += (x-2) * 8;
		}

		// Customize has no menuInt row; later labels use stock row numbers.
		const char* entry = (x == outpost_customize_row(curMenu))
		                    ? "Customize"
		                    : menuInt[curMenu + 1][outpost_stock_row(curMenu, x) - 1];

		str = malloc_die(strlen(entry) + 2);
		if (curSel[curMenu] == x)
		{
			str[0] = '~';
			strcpy(str + 1, entry);
		}
		else
		{
			strcpy(str, entry);
		}

		const unsigned int font = SMALL_FONT_SHAPES;
		const int text_x = 166;
		if (tightFont)
		{
			// Dense Endless lists use TINY_FONT; E-Shop rows also use category colours.
			unsigned int bank = (curMenu == MENU_ESHOP) ? endless_eshop_row_bank(x) : 15;
			JE_outTextAndDarken(VGAScreen, text_x, tempY, str, bank, 2, TINY_FONT);
		}
		else
		{
			JE_dString(VGAScreen, text_x, tempY, str, font);
		}
		free(str);

		// Perk pick rows show their count in the help line.

		// After giving up a zone, dim choices locked to the launch-time loadout. Start Level remains
		// available for the retry.
		if (endlessMode && endlessLockedSortie && curMenu == MENU_FULL_GAME && (x == 2 || x == 4))
			JE_dStringDarken(VGAScreen, text_x, tempY, entry, font);

		// Endless hardcore forbids all saving and loading, so both rows draw dimmed. The online
		// page drops Load Game, so resolve the row through options_full_row.
		if (endlessMode && endlessHardcore() &&
		    (curMenu == MENU_OPTIONS || curMenu == MENU_LIMITED_OPTIONS) &&
		    (options_full_row(x) == OPT_LOAD || options_full_row(x) == OPT_SAVE))
			JE_dStringDarken(VGAScreen, text_x, tempY, entry, font);

		// Endless: the Radar reroll is one per outpost, so the row greys out once it is spent.
		if (curMenu == MENU_PLAY_NEXT_LEVEL && x == endlessCourseRerollRow()
		    && endlessCourseRerollSpent())
			JE_dStringDarken(VGAScreen, text_x, tempY, entry, font);
	}
}

// Course threats grow from the top-left; boons grow from the bottom-right.
#define ENDLESS_MODS_LEFT_X    22
#define ENDLESS_MODS_RIGHT_X  132
#define ENDLESS_MODS_TOP_Y     20
#define ENDLESS_MODS_BOTTOM_Y 159
#define ENDLESS_MODS_ROW_H      9

// Center danger grade on the monitor window, not its asymmetric lower slot.
#define ENDLESS_RANK_CX        MENU_MONITOR_CENTER_X
#define ENDLESS_RANK_Y        173   // endlessModText draws the body AT this row (no +1 like DARKEN)

// Green-to-red rank ramp as {bank, brightness}. Keep it aligned with endlessRankName; the chosen
// brightness range keeps every glyph shade inside its palette bank.
static const struct { unsigned int bank; int bright; } endlessRankHue[11] = {
	{  0,  0 },  // F     green        (bank 0 shade 7)
	{  0, -1 },  // E     green
	{  0, -2 },  // D     deep green
	{ 15,  5 },  // C     pale yellow
	{ 15,  4 },  // B     amber
	{ 15,  3 },  // A     orange
	{ 15,  2 },  // S     orange
	{ 15,  1 },  // S+    orange-red
	{ 15,  0 },  // S++   red-orange
	{ 15, -1 },  // S+++  red
	{ 15, -2 },  // END   darkest red; off the letter scale, the 100th-zone finale alone
};

// Map danger bands from endlessModTable to bank-15 brightness.
static int endlessThreatShade(int weight)
{
	if (weight >= 25) return -2;
	if (weight >= 14) return -1;
	if (weight >= 10) return 0;
	return 1;
}

// Draw threats covered by queued Sabotage in white. Bank 13 offset 4 keeps all
// three font shades white without overflowing into the next palette bank.
#define ENDLESS_MOD_CLEANSED_BANK   13
#define ENDLESS_MOD_CLEANSED_BRIGHT  4

// Draw a one-pixel black outline, then the tinted modifier text.
static void endlessModText(int x, int y, const char *s, unsigned int bank, int brightness)
{
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx)
			if (dx != 0 || dy != 0)
				JE_outText(VGAScreen, x + dx, y + dy, s, 0, -1);
	JE_outTextAndDarken(VGAScreen, x, y, s, bank, brightness, TINY_FONT);
}

static void JE_drawEndlessCourseMods(void)
{
	if (!endlessMode || curMenu != MENU_PLAY_NEXT_LEVEL)
		return;
	const int course = curSel[MENU_PLAY_NEXT_LEVEL] - 2;
	if (course < 0 || course >= endlessCourseCount())
		return;

	// Center the outlined rank under the map, tinting only the grade by danger.
	const char *rank  = endlessCourseRank(course);
	const int   level = endlessCourseRankLevel(course);
	char full[16];
	snprintf(full, sizeof(full), "RANK %s", rank);
	const int rx = ENDLESS_RANK_CX - JE_textWidth(full, TINY_FONT) / 2;
	endlessModText(rx, ENDLESS_RANK_Y, "RANK ", 14, 1);
	if (level >= 0)
		endlessModText(rx + JE_textWidth("RANK ", TINY_FONT), ENDLESS_RANK_Y,
		               rank, endlessRankHue[level].bank, endlessRankHue[level].bright);

	EndlessCourseModRow rows[16];
	const int n = endlessCourseModRows(course, rows, (int)COUNTOF(rows));

	int boons = 0;
	for (int i = 0; i < n; ++i)
		if (!rows[i].hostile)
			++boons;

	int threat_y = ENDLESS_MODS_TOP_Y;
	int boon_y   = ENDLESS_MODS_BOTTOM_Y - (boons - 1) * ENDLESS_MODS_ROW_H;
	for (int i = 0; i < n; ++i)
	{
		// Draw the modifier selected for Sabotage in white.
		const bool     cleansed = rows[i].cleansed;
		const unsigned bank     = cleansed ? ENDLESS_MOD_CLEANSED_BANK : (rows[i].hostile ? 15u : 0u);

		if (rows[i].hostile)
		{
			if (threat_y > ENDLESS_MODS_BOTTOM_Y)
				break;  // no real course has this many rows; never spill off the monitor
			endlessModText(ENDLESS_MODS_LEFT_X, threat_y, rows[i].word, bank,
			               cleansed ? ENDLESS_MOD_CLEANSED_BRIGHT : endlessThreatShade(rows[i].weight));
			threat_y += ENDLESS_MODS_ROW_H;
		}
		else
		{
			endlessModText(ENDLESS_MODS_RIGHT_X - JE_textWidth(rows[i].word, TINY_FONT),
			               boon_y, rows[i].word, bank, cleansed ? ENDLESS_MOD_CLEANSED_BRIGHT : 0);
			boon_y += ENDLESS_MODS_ROW_H;
		}
	}
}

// Draw the whole planet monitor at the current tempNavX/tempNavY. Pure draw, no
// animation state change, so it can be re-issued at an interpolated pan.
static void JE_drawNavMonitor(void)
{
	JE_byte x;

	fill_rectangle_xy(VGAScreen, 19, 16, 135, 169, 2);
	JE_drawNavLines(true);
	JE_drawNavLines(false);
	JE_drawDots();

	for (x = 0; x < 11; x++)
		JE_drawPlanet(x);

	// mapPNum is the number of valid planet entries; menuChoices also counts action rows.
	for (x = 0; x < mapPNum; x++)
	{
		if (mapPlanet[x] > 11)
			JE_drawPlanet(mapPlanet[x] - 1);
	}

	if (mapOrigin > 11)
		JE_drawPlanet(mapOrigin - 1);

	blit_sprite(VGAScreenSeg, 0, 0, OPTION_SHAPES, 28);  // navigation screen interface

	JE_drawEndlessCourseMods();  // endless: the highlighted course's threats/boons, on the monitor

	fill_rectangle_xy(VGAScreen, 314, 0, 319, 199, 230);
}

// Advance the lock-on pan (navX/navY easing toward the selected planet), the
// planet spin animation, and the travel-dot reveal by one menu logic tick.
static void JE_navScreenAdvance(void)
{
	// Radar and Exit rows have no mapPlanet entry.
	const bool onPlanet = curSel[MENU_PLAY_NEXT_LEVEL] >= 2
	                   && curSel[MENU_PLAY_NEXT_LEVEL] - 2 < mapPNum;
	if (onPlanet)
	{
		const unsigned int origin_x_offset = sprite(PLANET_SHAPES, PGR[mapOrigin-1]-1)->width / 2,
		                   origin_y_offset = sprite(PLANET_SHAPES, PGR[mapOrigin-1]-1)->height / 2,
		                   dest_x_offset = sprite(PLANET_SHAPES, PGR[mapPlanet[curSel[MENU_PLAY_NEXT_LEVEL]-2] - 1]-1)->width / 2,
		                   dest_y_offset = sprite(PLANET_SHAPES, PGR[mapPlanet[curSel[MENU_PLAY_NEXT_LEVEL]-2] - 1]-1)->height / 2;

		newNavX = (planetX[mapOrigin-1] - origin_x_offset
		          + planetX[mapPlanet[curSel[MENU_PLAY_NEXT_LEVEL]-2] - 1] - dest_x_offset) / 2.0f;
		newNavY = (planetY[mapOrigin-1] - origin_y_offset
		          + planetY[mapPlanet[curSel[MENU_PLAY_NEXT_LEVEL]-2] - 1] - dest_y_offset) / 2.0f;
	}

	navX = navX + (newNavX - navX) / 2.0f;
	navY = navY + (newNavY - navY) / 2.0f;

	if (fabsf(newNavX - navX) < 1)
		navX = newNavX;
	if (fabsf(newNavY - navY) < 1)
		navY = newNavY;

	if (planetAniWait > 0)
	{
		planetAniWait--;
	}
	else
	{
		planetAni++;
		if (planetAni > 14)
			planetAni = 0;
		planetAniWait = 3;
	}

	if (currentDotWait > 0)
	{
		currentDotWait--;
	}
	else
	{
		if (onPlanet && currentDotNum < planetDots[curSel[MENU_PLAY_NEXT_LEVEL]-2])
			currentDotNum++;
		currentDotWait = 5;
	}
}

void JE_updateNavScreen(void)
{
	tempNavX = roundf(navX);
	tempNavY = roundf(navY);
	JE_drawNavMonitor();
	JE_navScreenAdvance();
}

// Draw the complete play-next-level frame (monitor at dispNavX/dispNavY plus menu
// chrome), mirroring the menu loop's per-tick draw so it can be re-issued per frame.
static void JE_navDrawFrame(JE_real dispNavX, JE_real dispNavY)
{
	tempNavX = roundf(dispNavX);
	tempNavY = roundf(dispNavY);
	JE_drawNavMonitor();
	JE_drawMainMenuHelpText();
	JE_drawMenuHeader();
	JE_drawMenuChoices();
	if (extraGame)
		JE_dString(VGAScreen, 170, 140, miscText[68 - 1], FONT_SHAPES);
	JE_drawScore();
}

// Redraw the planet monitor at an interpolated pan for each displayed frame.
static void JE_navScreenSmoothPresent(void)
{
	// Smooth Motion off: present the authoritative frame (already drawn at the
	// current position) once and let menuWaitWithSmoothCursor wait out the tick.
	if (!smoothMotion)
	{
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		return;
	}

	const float period = 2.0f * get_delay_period();  // setDelay(2) duration (ms)

	for (;;)
	{
		const Uint32 remaining = getDelayTicks();  // ms until the setDelay(2) target
		if (remaining == 0 || period <= 0.0f)
			break;

		float alpha = 1.0f - (float)remaining / period;
		if (alpha < 0.0f)
			alpha = 0.0f;
		else if (alpha > 1.0f)
			alpha = 1.0f;

		// Restore the backdrop because darkened text shadows are not idempotent.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		JE_navDrawFrame(nav_smooth_prev_x + (navX - nav_smooth_prev_x) * alpha,
		                nav_smooth_prev_y + (navY - nav_smooth_prev_y) * alpha);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		if (!output_vsync)
			limit_render_fps();
		service_SDL_events(false);
	}
}

void JE_drawLines(SDL_Surface *surface, JE_boolean dark)
{
	JE_byte x, y;
	JE_integer tempX, tempY;
	JE_integer tempX2, tempY2;
	JE_word tempW, tempW2;

	tempX2 = -10;
	tempY2 = 0;

	tempW = 0;
	for (x = 0; x < 20; x++)
	{
		tempW += 15;
		tempX = tempW - tempX2;

		if (tempX > 18 && tempX < 135)
		{
			if (dark)
				JE_rectangle(surface, tempX + 1, 0, tempX + 1, 199, 32+3);
			else
				JE_rectangle(surface, tempX, 0, tempX, 199, 32+5);
		}
	}

	tempW = 0;
	for (y = 0; y < 20; y++)
	{
		tempW += 15;
		tempY = tempW - tempY2;

		if (tempY > 15 && tempY < 169)
		{
			if (dark)
				JE_rectangle(surface, 0, tempY + 1, 319, tempY + 1, 32+3);
			else
				JE_rectangle(surface, 0, tempY, 319, tempY, 32+5);

			tempW2 = 0;

			for (x = 0; x < 20; x++)
			{
				tempW2 += 15;
				tempX = tempW2 - tempX2;
				if (tempX > 18 && tempX < 135)
				{
					JE_pix3(surface, tempX, tempY, 32+6);
				}
			}
		}
	}
}

/* Navigation-screen line effect, distinct from JE_drawLines. */

void JE_drawNavLines(JE_boolean dark)
{
	JE_byte x, y;
	JE_integer tempX, tempY;
	JE_integer tempX2, tempY2;
	JE_word tempW, tempW2;

	tempX2 = tempNavX >> 1;
	tempY2 = tempNavY >> 1;

	tempW = 0;
	for (x = 1; x <= 20; x++)
	{
		tempW += 15;
		tempX = tempW - tempX2;

		if (tempX > 18 && tempX < 135)
		{
			if (dark)
				JE_rectangle(VGAScreen, tempX + 1, 16, tempX + 1, 169, 1);
			else
				JE_rectangle(VGAScreen, tempX, 16, tempX, 169, 5);
		}
	}

	tempW = 0;
	for (y = 1; y <= 20; y++)
	{
		tempW += 15;
		tempY = tempW - tempY2;

		if (tempY > 15 && tempY < 169)
		{
			if (dark)
				JE_rectangle(VGAScreen, 19, tempY + 1, 135, tempY + 1, 1);
			else
				JE_rectangle(VGAScreen, 8, tempY, 160, tempY, 5);

			tempW2 = 0;

			for (x = 0; x < 20; x++)
			{
				tempW2 += 15;
				tempX = tempW2 - tempX2;
				if (tempX > 18 && tempX < 135)
					JE_pix3(VGAScreen, tempX, tempY, 7);
			}
		}
	}
}

void JE_drawDots(void)
{
	JE_byte x, y;
	JE_integer tempX, tempY;

	for (x = 0; x < mapPNum; x++)
	{
		for (y = 0; y < planetDots[x]; y++)
		{
			tempX = planetDotX[x][y] - tempNavX + 66 - 2;
			tempY = planetDotY[x][y] - tempNavY + 85 - 2;
			if (tempX > 0 && tempX < 140 && tempY > 0 && tempY < 168)
				blit_sprite(VGAScreenSeg, tempX, tempY, OPTION_SHAPES, (x == curSel[MENU_PLAY_NEXT_LEVEL]-2 && y < currentDotNum) ? 30 : 29);
		}
	}
}

void JE_drawPlanet(JE_byte planetNum)
{
	// Planet tables contain 21 entries; reject stale or invalid indexes.
	if (planetNum >= 21)
		return;

	JE_integer tempZ = PGR[planetNum]-1,
	           tempX = planetX[planetNum] + 66 - tempNavX - sprite(PLANET_SHAPES, tempZ)->width / 2,
	           tempY = planetY[planetNum] + 85 - tempNavY - sprite(PLANET_SHAPES, tempZ)->height / 2;

	if (tempX > -7 && tempX + sprite(PLANET_SHAPES, tempZ)->width < 170 && tempY > 0 && tempY < 160)
	{
		if (PAni[planetNum])
			tempZ += planetAni;

		blit_sprite_dark(VGAScreenSeg, tempX + 3, tempY + 3, PLANET_SHAPES, tempZ, false);
		blit_sprite(VGAScreenSeg, tempX, tempY, PLANET_SHAPES, tempZ);  // planets
	}
}

void JE_scaleBitmap(SDL_Surface *dst_bitmap, const SDL_Surface *src_bitmap,  int x1, int y1, int x2, int y2)
{
	/* Scale the Ship Specs source surface into the requested destination rectangle. */

	assert(src_bitmap != NULL && dst_bitmap != NULL);
	assert(x1 >= 0 && y1 >= 0 && x2 < dst_bitmap->w && y2 < dst_bitmap->h);

	int w = x2 - x1 + 1,
	    h = y2 - y1 + 1;
	float base_skip_w = src_bitmap->pitch / (float)w,
	      base_skip_h = src_bitmap->h / (float)h;
	float cumulative_skip_w, cumulative_skip_h;

	// Scale each destination row from the corresponding source coordinates.
	Uint8 *dst = dst_bitmap->pixels;  /* 8-bit specific */
	const Uint8 *src, *src_w;  /* 8-bit specific */

	dst += y1 * dst_bitmap->pitch + x1;
	cumulative_skip_h = 0;

	for (int i = 0; i < h; i++)
	{
		//this sets src to the beginning of our desired line
		src = src_w = (Uint8 *)(src_bitmap->pixels) + (src_bitmap->w * ((unsigned int)cumulative_skip_h));
		cumulative_skip_h += base_skip_h;
		cumulative_skip_w = 0;

		for (int j = 0; j < w; j++)
		{
			//copy and move pointers
			*dst = *src;
			dst++;

			cumulative_skip_w += base_skip_w;
			src = src_w + ((unsigned int)cumulative_skip_w); //value is floored
		}

		dst += dst_bitmap->pitch - w;
	}
}

void JE_initWeaponView(void)
{
	fill_rectangle_xy(VGAScreen, 8, 8, 144, 177, 0);

	shopPlayer()->sidekick[LEFT_SIDEKICK].x = 72 - 15;
	shopPlayer()->sidekick[LEFT_SIDEKICK].y = 120;
	shopPlayer()->sidekick[RIGHT_SIDEKICK].x = 72 + 15;
	shopPlayer()->sidekick[RIGHT_SIDEKICK].y = 120;

	shopPlayer()->x = 72;
	shopPlayer()->y = 110;
	shopPlayer()->delta_x_shot_move = 0;
	shopPlayer()->delta_y_shot_move = 0;
	shopPlayer()->last_x_explosion_follow = 72;
	shopPlayer()->last_y_explosion_follow = 110;
	power = 500;
	lastPower = 500;
	menu_power_prev = menu_power_cur = 500;  // reset gauge interpolation state
	menu_power_bg_valid = false;             // background re-captured on first draw

	memset(shotAvail, 0, sizeof(shotAvail));

	memset(shotRepeat, 1, sizeof(shotRepeat));
	memset(shotMultiPos, 0, sizeof(shotMultiPos));

	initialize_starfield();
}

void JE_computeDots(void)
{
	JE_integer tempX, tempY;
	JE_longint distX, distY;
	JE_byte x, y;

	for (x = 0; x < mapPNum; x++)
	{
		distX = (int)(planetX[mapPlanet[x]-1]) - (int)(planetX[mapOrigin-1]);
		distY = (int)(planetY[mapPlanet[x]-1]) - (int)(planetY[mapOrigin-1]);
		tempX = abs(distX) + abs(distY);

		if (tempX != 0)
			planetDots[x] = roundf(sqrtf(sqrtf((distX * distX) + (distY * distY)))) - 1;
		else
			planetDots[x] = 0;

		if (planetDots[x] > 10)
			planetDots[x] = 10;

		for (y = 0; y < planetDots[x]; y++)
		{
			tempX = JE_partWay(planetX[mapOrigin-1], planetX[mapPlanet[x]-1], planetDots[x], y);
			tempY = JE_partWay(planetY[mapOrigin-1], planetY[mapPlanet[x]-1], planetDots[x], y);
			/* ??? Why does it use temp? =P */
			planetDotX[x][y] = tempX;
			planetDotY[x][y] = tempY;
		}
	}
}

JE_integer JE_partWay(JE_integer start, JE_integer finish, JE_byte dots, JE_byte dist)
{
	return (finish - start) / (dots + 2) * (dist + 1) + start;
}

void JE_doShipSpecs(void)
{
	/* Build the Ship Specs image in existing scratch surfaces, then scale it onscreen. */

	// Create the source image.
	wait_noinput(true, true, true);
	JE_drawShipSpecs(game_screen, VGAScreen2);

	// Restore the scratch surface.
	JE_loadPic(VGAScreen2, 1, false);

	// Present the scaled image.
	JE_playSampleNum(S_SPRING);
	JE_scaleInPicture(VGAScreen, game_screen);
	wait_input(true, true, true);
}

// E-Shop cost highlight under shop palette 1.
#define ENDLESS_COST_HL_BANK   1
#define ENDLESS_COST_HL_BRIGHT 6

// Chart-a-Course runs under palette 18, where bank 1 clashes; bank 14 (the course text's own
// colour) reads correctly there.
#define ENDLESS_COURSE_HL_BANK   14
#define ENDLESS_COURSE_HL_BRIGHT 6

// Shared right edge for figures in Endless help bars.
#define ENDLESS_COURSE_PAYOUT_RIGHT 305

/* Leftmost x at which `right` can sit flush against that edge, backing off to just after `text`
 * when an unusually long description would otherwise reach it. */
int help_bar_right_x(const char *text, const char *right)
{
	const int afterText = 10 + JE_textWidth(text, TINY_FONT) + 5;
	const int x = ENDLESS_COURSE_PAYOUT_RIGHT - JE_textWidth(right, TINY_FONT);
	return (x < afterText) ? afterText : x;
}

/* Draw `right` flush against that edge on the help bar. */
static void draw_help_bar_right(const char *text, const char *right, unsigned int bank, int brightness)
{
	JE_textShade(VGAScreen, help_bar_right_x(text, right), 187, right, bank, brightness, DARKEN);
}

/* Repaint ping every frame from a saved maximum-width help-bar band. DARKEN is not idempotent. */
#define PING_BAND_Y      186
#define PING_BAND_H       12
#define PING_BAND_RIGHT  (ENDLESS_COURSE_PAYOUT_RIGHT + 2)  // DARKEN's shadow sits a pixel right
#define PING_BAND_MAX_W   96
#define PING_WIDEST      "Ping: 9999 ms"
#define PING_BANK         14  // secondary chrome, matching the endless seed readout; not a price
#define PING_BRIGHT        3

static char ping_str[16];
static int ping_band_x = 0, ping_band_w = 0;
static bool ping_shown = false;
static Uint8 ping_band_bak[PING_BAND_H * PING_BAND_MAX_W];

static void draw_help_bar_ping(void)
{
	const int ms = network_ping_ms();
	if (ms < 0)
		SDL_strlcpy(ping_str, "Ping: --", sizeof(ping_str));
	else
		snprintf(ping_str, sizeof(ping_str), "Ping: %d ms", ms);

	int x = ENDLESS_COURSE_PAYOUT_RIGHT - JE_textWidth(ping_str, TINY_FONT);
	if (x < ping_band_x)
		x = ping_band_x;
	JE_textShade(VGAScreen, x, 187, ping_str, PING_BANK, PING_BRIGHT, DARKEN);
}

/* Call with the help bar already composed and the ping not yet drawn over it. */
static void save_help_bar_ping_band(const char *text)
{
	ping_shown = false;

	// Omit a figure that would collide with the description.
	ping_band_x = ENDLESS_COURSE_PAYOUT_RIGHT - JE_textWidth(PING_WIDEST, TINY_FONT);
	if (ping_band_x < 10 + JE_textWidth(text, TINY_FONT) + 5)
		return;

	ping_band_w = PING_BAND_RIGHT - ping_band_x;
	if (ping_band_w > PING_BAND_MAX_W)
		ping_band_w = PING_BAND_MAX_W;
	if (ping_band_w <= 0)
		return;

	for (int i = 0; i < PING_BAND_H; i++)
	{
		memcpy(&ping_band_bak[i * PING_BAND_MAX_W],
		       (Uint8 *)VGAScreen->pixels + (size_t)(PING_BAND_Y + i) * VGAScreen->pitch + ping_band_x,
		       ping_band_w);
	}

	ping_shown = true;
}

/* Put the saved pixels back and redraw with a fresh reading. No-op off the network. */
static void refresh_help_bar_ping(void)
{
	if (!ping_shown)
		return;

	for (int i = 0; i < PING_BAND_H; i++)
	{
		memcpy((Uint8 *)VGAScreen->pixels + (size_t)(PING_BAND_Y + i) * VGAScreen->pitch + ping_band_x,
		       &ping_band_bak[i * PING_BAND_MAX_W],
		       ping_band_w);
	}

	draw_help_bar_ping();
}

void JE_drawMainMenuHelpText(void)
{
	// Use bounded string copy; source help rows are one byte shorter than tempStr.
	char tempStr[67];
	// Endless: the two kinds of flush-right figure. Money is highlighted; a stack count isn't money,
	// so it keeps the description's own colour. Never both at once; see the draw below.
	char costStr[24] = "";   // price / payout / buyout
	char ownedStr[24] = "";  // an offered perk's "Owned n/max"
	JE_byte temp;

	temp = curSel[curMenu] - 2;

	if (curMenu == MENU_JOYSTICK_CONFIG) // joystick settings menu help
	{
		const int help[16] = { 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 24, 11 };
		SDL_strlcpy(tempStr, mainMenuHelp[help[curSel[curMenu] - 2]], sizeof(tempStr));
	}
	else if (curMenu < MENU_PLAY_NEXT_LEVEL || curMenu >= MENU_2_PLAYER_ARCADE)
	{
		// Resolve inserted rows before using stock help indices.
		const JE_byte sel = (JE_byte)outpost_stock_row(curMenu, curSel[curMenu]);
		const int stockLast = outpost_stock_row(curMenu, menuChoices[curMenu]);
		temp = (JE_byte)(sel - 2);

		if (curSel[curMenu] == outpost_customize_row(curMenu))
		{
			SDL_strlcpy(tempStr, "Tell the two ships apart on screen.", sizeof(tempStr));
		}
		else if (debugMode && curMenu == MENU_FULL_GAME && sel == 7)
		{
			snprintf(tempStr, sizeof(tempStr), "Debug: equip your ship.");
		}
		else if (debugMode && curMenu == MENU_FULL_GAME && sel == 8)
		{
			snprintf(tempStr, sizeof(tempStr), "Debug: select a level.");
		}
		else if (debugMode && curMenu == MENU_FULL_GAME && sel == 9)
		{
			SDL_strlcpy(tempStr, mainMenuHelp[5 - 1], sizeof(tempStr));
		}
		else if (debugMode &&
		         (curMenu == MENU_1_PLAYER_ARCADE || curMenu == MENU_2_PLAYER_ARCADE || curMenu == MENU_SUPER_TYRIAN) &&
		         sel >= stockLast - 2)
		{
			// The inserted Debug entries have no menuHelp[] slot (0 would index
			// mainMenuHelp[-1]); supply their text, and read Quit's from its original slot.
			if (sel == stockLast - 2)
				snprintf(tempStr, sizeof(tempStr), "Debug: equip your ship.");
			else if (sel == stockLast - 1)
				snprintf(tempStr, sizeof(tempStr), "Debug: select a level.");
			else
				SDL_strlcpy(tempStr, mainMenuHelp[menuHelp[curMenu][stockLast - 4] - 1], sizeof(tempStr));
		}
		else if (endlessMode && endlessLockedSortie && curMenu == MENU_FULL_GAME && sel == 6)
		{
			snprintf(tempStr, sizeof(tempStr), "Retry the level you gave up.");
		}
		else if (endlessMode && endlessLockedSortie && curMenu == MENU_FULL_GAME &&
		         (sel == 2 || sel == 4))
		{
			// Locked "gave up the level" outpost: the E-Shop and loadout are frozen.
			snprintf(tempStr, sizeof(tempStr), "Locked - you gave up the level.");
		}
		else if (endlessMode && curMenu == MENU_FULL_GAME && sel == 2)
		{
			snprintf(tempStr, sizeof(tempStr), "Open the E-Shop.");
		}
		else if (endlessMode && curMenu == MENU_FULL_GAME && sel == 3)
		{
			snprintf(tempStr, sizeof(tempStr), "Review the perks you've acquired.");
		}
		else if (endlessMode && curMenu == MENU_UPGRADES && (sel == 3 || sel == 4))
		{
			// The stock data help names the Front/Rear Gun rows, which endless renames.
			SDL_strlcpy(tempStr, sel == 3 ? SHOP_PRIMARY_GUN_HELP : SHOP_SECONDARY_GUN_HELP,
			            sizeof(tempStr));
		}
		else if (curMenu == MENU_ESHOP)
		{
			// The E-Shop has no menuHelp[] row, so supply each entry's help directly. The
			// description goes in tempStr; the price goes in costStr, drawn HIGHLIGHTED after it.
			switch (curSel[MENU_ESHOP])
			{
			case 2:
				SDL_strlcpy(tempStr, "Reroll the shop stock.", sizeof(tempStr));
				snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessRerollPrice());
				break;
			case 4:
				if (endlessHullMaxed())
					SDL_strlcpy(tempStr, "Hull upgrades maxed out.", sizeof(tempStr));
				else
				{
					SDL_strlcpy(tempStr, "Permanently boost max armor.", sizeof(tempStr));
					snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessHullPrice());
				}
				break;
			case 7:
				if (endlessBuffKindBought() == ENDLESS_BUFF_KIND_TURBODRIVE)
					SDL_strlcpy(tempStr, "Turbodrive active for next sector.", sizeof(tempStr));
				else if (endlessBuffKindBought() != ENDLESS_BUFF_KIND_NONE)
					SDL_strlcpy(tempStr, "A kill-fire buff is already bought.", sizeof(tempStr));
				else if (endlessBuffOnCooldown())
					snprintf(tempStr, sizeof(tempStr), "Drives recharge in %d sector(s).", endlessBuffCooldownLeft());
				else
				{
					SDL_strlcpy(tempStr, "Each kill quickens your guns.", sizeof(tempStr));
					snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessTurbodrivePrice());
				}
				break;
			case 8:
				if (endlessBuffKindBought() == ENDLESS_BUFF_KIND_OVERBLAST)
					SDL_strlcpy(tempStr, "Overblast active for next sector.", sizeof(tempStr));
				else if (endlessBuffKindBought() != ENDLESS_BUFF_KIND_NONE)
					SDL_strlcpy(tempStr, "A kill-fire buff is already bought.", sizeof(tempStr));
				else if (endlessBuffOnCooldown())
					snprintf(tempStr, sizeof(tempStr), "Drives recharge in %d sector(s).", endlessBuffCooldownLeft());
				else
				{
					SDL_strlcpy(tempStr, "Each kill stacks your shot damage.", sizeof(tempStr));
					snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessOverblastPrice());
				}
				break;
			case 9:
				if (endlessBuffKindBought() == ENDLESS_BUFF_KIND_OVERDRIVE)
					SDL_strlcpy(tempStr, "Overdrive active for next sector.", sizeof(tempStr));
				else if (endlessBuffKindBought() != ENDLESS_BUFF_KIND_NONE)
					SDL_strlcpy(tempStr, "A kill-fire buff is already bought.", sizeof(tempStr));
				else if (endlessBuffOnCooldown())
					snprintf(tempStr, sizeof(tempStr), "Drives recharge in %d sector(s).", endlessBuffCooldownLeft());
				else
				{
					SDL_strlcpy(tempStr, "Turbodrive + Overblast together.", sizeof(tempStr));
					snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessOverdrivePrice());
				}
				break;
			case 6:
				if (endlessLastGrantedSpecial()[0] != '\0')
					snprintf(tempStr, sizeof(tempStr), "Got %s!  Buy another:", endlessLastGrantedSpecial());
				else
					SDL_strlcpy(tempStr, "Buy a random special weapon.", sizeof(tempStr));
				snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessSpecialPrice());
				break;
			case 11:  // Buy Bomb
				if (endlessBombFull())
					SDL_strlcpy(tempStr, "Bomb stockpile is full (10).", sizeof(tempStr));
				else
				{
					SDL_strlcpy(tempStr, "Stock an extra bomb.", sizeof(tempStr));
					snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessBombPrice());
				}
				break;
			case 10:  // Revive Token
				if (endlessReviveArmed())
					SDL_strlcpy(tempStr, "A revive is armed - survive one death.", sizeof(tempStr));
				else
				{
					SDL_strlcpy(tempStr, "Survive one lethal hit.", sizeof(tempStr));
					snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessRevivePrice());
				}
				break;
			case 5:  // Extra Perk
				SDL_strlcpy(tempStr, "Pick a bonus perk now, from four.", sizeof(tempStr));
				snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessExtraPerkPrice());
				break;
			case 3:  // Sabotage Sector
				// Maxed prints no price, like Hull/Bombs: a cost next to an unbuyable row reads as a
				// failed purchase rather than a full queue.
				if (endlessCleanseMaxed())
					snprintf(tempStr, sizeof(tempStr), "Sabotage queue is full (%d).", ENDLESS_CLEANSE_MAX_CHARGES);
				else
				{
					if (endlessCleanseCharges() > 0)
						snprintf(tempStr, sizeof(tempStr), "%d strip(s) queued.  Buy more:", endlessCleanseCharges());
					else
						SDL_strlcpy(tempStr, "Strip the next sector's worst danger.", sizeof(tempStr));
					snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessCleansePrice());
				}
				break;
			case 12:  // Gamble
				if (endlessGambleResult()[0] != '\0')
					SDL_strlcpy(tempStr, endlessGambleResult(), sizeof(tempStr));
				else
					SDL_strlcpy(tempStr, "Roll the dice: fortune or ruin.", sizeof(tempStr));
				snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessGamblePrice());
				break;
			default:
				SDL_strlcpy(tempStr, "Return to the buy/sell menu.", sizeof(tempStr));
				break;
			}
		}
		else if (curMenu == MENU_PERKS)
		{
			if (endlessPerkListMode)
			{
				// Perk rows already show stack counts; help shows only the effect.
				if (curSel[MENU_PERKS] == menuChoices[MENU_PERKS])
					SDL_strlcpy(tempStr, "Return to the buy/sell menu.", sizeof(tempStr));
				else
				{
					const int id = perkListId[curSel[MENU_PERKS] - 2];
					SDL_strlcpy(tempStr, id >= 0 ? endlessPerkDesc(id) : "You haven't earned any perks yet.",
					            sizeof(tempStr));
				}
			}
			// Perk pick: the hovered perk's effect, with the stack count flush right; the pick rows are
			// bare names, so the help line is the only place it shows.
			else if (curSel[MENU_PERKS] == menuChoices[MENU_PERKS])
			{
				SDL_strlcpy(tempStr, "Take no perk: depth and perks pay more.", sizeof(tempStr));
				snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessPerkDeclineBonus());
			}
			else
			{
				SDL_strlcpy(tempStr, endlessPerkChoiceDesc(curSel[MENU_PERKS] - 2), sizeof(tempStr));
				SDL_strlcpy(ownedStr, endlessPerkChoiceOwnedText(curSel[MENU_PERKS] - 2), sizeof(ownedStr));
			}
		}
		else if (customWeaponEnabled && curMenu == MENU_UPGRADES && curSel[curMenu] >= 9)
		{
			// Supply help for the inserted Custom row and shifted Done row.
			if (curSel[curMenu] == 9)
				SDL_strlcpy(tempStr, "Create and equip a custom weapon.", sizeof(tempStr));
			else
				SDL_strlcpy(tempStr, mainMenuHelp[12 - 1], sizeof(tempStr));  // Done
		}
		else if (curMenu == MENU_OPTIONS || curMenu == MENU_LIMITED_OPTIONS)
		{
			// Zero entries use local help for rows absent from the data file.
			const int help = menuHelp[curMenu][temp];
			if (help == 0)
				SDL_strlcpy(tempStr, SHIP_SENS_HELP, sizeof(tempStr));
			else
				SDL_strlcpy(tempStr, mainMenuHelp[help - 1], sizeof(tempStr));
		}
		else if (curMenu == MENU_ONLINE)
		{
			switch (online_full_row(curSel[MENU_ONLINE]))
			{
			case ONLINE_ROW_COLOR:
				SDL_strlcpy(tempStr, "Dye your ship; the other player sees it too.", sizeof(tempStr));
				break;
			case ONLINE_ROW_OPACITY:
				SDL_strlcpy(tempStr, "How solid the other player looks. Local only.", sizeof(tempStr));
				break;
			case ONLINE_ROW_HPBARS:
				SDL_strlcpy(tempStr, "Small shield and armor bars on their ship.", sizeof(tempStr));
				break;
			default:
				SDL_strlcpy(tempStr, mainMenuHelp[12 - 1], sizeof(tempStr));  // Exit
				break;
			}
		}
		else if (curMenu == MENU_ONLINE_COLOR)
		{
			const int cell = curSel[MENU_ONLINE_COLOR] - 2;
			if (cell >= online_color_cells - 1)
				SDL_strlcpy(tempStr, "Keep this color and go back.", sizeof(tempStr));
			else if (cell <= 0)
				SDL_strlcpy(tempStr, "Fly the hull's own colors.", sizeof(tempStr));
			else
				snprintf(tempStr, sizeof(tempStr), "Color %d.  Arrows try one, Enter keeps it.",
				         online_color_cell[cell]);
		}
		else if (curMenu == MENU_ONLINE_HPBARS)
		{
			switch (curSel[MENU_ONLINE_HPBARS])
			{
			case 2 + NET_HP_BARS_OFF:
				SDL_strlcpy(tempStr, "No bars on the other player's ship.", sizeof(tempStr));
				break;
			case 2 + NET_HP_BARS_ON_HIT:
				SDL_strlcpy(tempStr, "Show their bars for 2 seconds after a hit.", sizeof(tempStr));
				break;
			case 2 + NET_HP_BARS_ALWAYS:
				SDL_strlcpy(tempStr, "Keep their bars up the whole level.", sizeof(tempStr));
				break;
			default:
				SDL_strlcpy(tempStr, "Keep this setting and go back.", sizeof(tempStr));
				break;
			}
		}
		else if (curMenu == MENU_ONLINE_OPACITY)
		{
			if (curSel[MENU_ONLINE_OPACITY] == ONLINE_OPACITY_TOGGLE_ROW)
				SDL_strlcpy(tempStr, "Off keeps their ship solid, shots faded.", sizeof(tempStr));
			else if (curSel[MENU_ONLINE_OPACITY] == menuChoices[MENU_ONLINE_OPACITY])
				SDL_strlcpy(tempStr, "Keep this setting and go back.", sizeof(tempStr));
			else if (curSel[MENU_ONLINE_OPACITY] == ONLINE_OPACITY_FIRST_ROW)
				SDL_strlcpy(tempStr, "Draw the other player at full strength.", sizeof(tempStr));
			else
				snprintf(tempStr, sizeof(tempStr),
				         "Draw the other player and their shots at %d%%.",
				         online_opacity_of_row(curSel[MENU_ONLINE_OPACITY]));
		}
		else
		{
			SDL_strlcpy(tempStr, mainMenuHelp[(menuHelp[curMenu][temp]) - 1], sizeof(tempStr));
		}
	}
	else if (curMenu == MENU_KEYBOARD_CONFIG &&
	         curSel[MENU_KEYBOARD_CONFIG] == 10)
	{
		SDL_strlcpy(tempStr, mainMenuHelp[25-1], sizeof(tempStr));
	}
	else if (leftPower || rightPower)
	{
		SDL_strlcpy(tempStr, mainMenuHelp[24-1], sizeof(tempStr));
	}
	else if (curMenu == MENU_PLAY_NEXT_LEVEL && curSel[curMenu] == endlessCourseRerollRow())
	{
		SDL_strlcpy(tempStr, endlessCourseRerollHelp(), sizeof(tempStr));
	}
	else if (endlessMode && curMenu == MENU_PLAY_NEXT_LEVEL && temp < endlessCourseCount())
	{
		SDL_strlcpy(tempStr, endlessCourseHelp(temp), sizeof(tempStr));
		// '~' changes font brightness and cannot be printed literally here.
		snprintf(costStr, sizeof(costStr), "$%lld", (long long)endlessCoursePayout(temp));
	}
	else if (temp == menuChoices[curMenu] - 2 ||
	         (curMenu == MENU_DATA_CUBES && cubeMax == 0))
	{
		SDL_strlcpy(tempStr, mainMenuHelp[12-1], sizeof(tempStr));
	}
	else
	{
		SDL_strlcpy(tempStr, mainMenuHelp[17 + curMenu - 3], sizeof(tempStr));
	}
	
	JE_textShade(VGAScreen, 10, MENU_HELP_TEXT_Y, tempStr, 14, 1, DARKEN);
	// Right-align prices and payouts with palette-specific highlighting; perk stack counts stay neutral.
	if (costStr[0] != '\0')
	{
		const bool course = (curMenu == MENU_PLAY_NEXT_LEVEL);
		draw_help_bar_right(tempStr, costStr,
		                    course ? ENDLESS_COURSE_HL_BANK : ENDLESS_COST_HL_BANK,
		                    course ? ENDLESS_COURSE_HL_BRIGHT : ENDLESS_COST_HL_BRIGHT);
	}
	else if (ownedStr[0] != '\0')
		draw_help_bar_right(tempStr, ownedStr, 14, 1);  // same bank/brightness as the help text itself

	// Endless: show the run's seed on the E-Shop help line, right-aligned opposite "Open the
	// E-Shop." so it's always visible from the outpost.
	if (endlessMode && !isNetworkGame && curMenu == MENU_FULL_GAME && curSel[curMenu] == 2)
	{
		char seedStr[16 + ENDLESS_SEED_MAXLEN];
		snprintf(seedStr, sizeof(seedStr), "Seed: %s", endlessSeedString());
		const int afterText = 10 + JE_textWidth(tempStr, TINY_FONT) + 8;
		int seed_x = ENDLESS_COURSE_PAYOUT_RIGHT - JE_textWidth(seedStr, TINY_FONT);
		if (seed_x < afterText)  // never overlap the help text on the left
			seed_x = afterText;
		JE_textShade(VGAScreen, seed_x, 187, seedStr, 14, 3, DARKEN);
	}

	// Price and seed figures take priority over the online RTT display.
	ping_shown = false;
	if (isNetworkGame && costStr[0] == '\0' && ownedStr[0] == '\0')
	{
		// A missing figure band means the description occupies the right edge.
		save_help_bar_ping_band(tempStr);
		if (ping_shown)
			draw_help_bar_ping();
	}
}

typedef struct
{
	JE_byte slot;
	const char *savename;
}
SaveRequestText;

static void save_request_message(const void *ctx)
{
	const SaveRequestText *text = ctx;

	// Use the body-text bank so the question remains bright on saved-game palettes.
	JE_textShade(VGAScreen, 70, 66, miscText[68], 12, 5, FULL_SHADE); // Are you sure you want to save?
	JE_textShade(VGAScreen, 74, 90, miscText[1], 12, 1, FULL_SHADE); // Save name:
	JE_textShade(VGAScreen, 135, 90, text->savename, 12, 1, FULL_SHADE);
	JE_textShade(VGAScreen, 74, 100, miscText[69], 12, 1, FULL_SHADE); // Original save:
	JE_textShade(VGAScreen, 140, 100, saveFiles[text->slot-1].name, 12, 1, FULL_SHADE);
}

static void quit_request_message(const void *ctx)
{
	(void)ctx;
	JE_textShade(VGAScreen, 70, 60, miscText[28], 0, 5, FULL_SHADE);
	JE_helpBox(VGAScreen, 70, 90, miscText[30], 30);
}

static bool shop_yes_no(void (*draw_message)(const void *ctx), const void *ctx, bool pump_shop)
{
	bool yes_selected = true, done = false;

	JE_clearKeyboard();
	wait_noinput(true, true, true);

	JE_barShade(VGAScreen, 65, 55, 255, 155);

	while (!done)
	{
		Uint8 col = 8;
		int colC = 1;

		do
		{
			service_SDL_events(true);
			setDelay(4);

			// A player thinking on this prompt must not read as a dead connection.
			NETWORK_KEEP_ALIVE();
			if (pump_shop)
			{
				while (network_shop_pump())
					;
			}

			blit_sprite(VGAScreen, 50, 50, OPTION_SHAPES, 35);  // message box
			draw_message(ctx);

			col += colC;
			if (col > 8 || col < 2)
				colC = -colC;

			int temp_x, temp_c;

			temp_x = 54 + 45 - (JE_textWidth(miscText[9], FONT_SHAPES) / 2);
			temp_c = yes_selected ? col - 12 : -5;

			JE_outTextAdjust(VGAScreen, temp_x, 128, miscText[9], 15, temp_c, FONT_SHAPES, true);

			temp_x = 149 + 45 - (JE_textWidth(miscText[10], FONT_SHAPES) / 2);
			temp_c = !yes_selected ? col - 12 : -5;

			JE_outTextAdjust(VGAScreen, temp_x, 128, miscText[10], 15, temp_c, FONT_SHAPES, true);

			if (has_mouse)
			{
				JE_mouseStart();
				JE_showVGA();
				JE_mouseReplace();
			}
			else
			{
				JE_showVGA();
			}

			// not wait_delay(): keep the cursor at display rate for the rest of the tick
			menuWaitWithSmoothCursor();

			push_joysticks_as_keyboard();
			service_SDL_events(false);

		} while (!newkey && !mousedown);

		if (mousedown)
		{
			if (lastmouse_y > 123 && lastmouse_y < 149)
			{
				if (lastmouse_x > 56 && lastmouse_x < 142)
				{
					yes_selected = true;
					done = true;
				}
				else if (lastmouse_x > 151 && lastmouse_x < 237)
				{
					yes_selected = false;
					done = true;
				}
			}
			mousedown = false;
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
				case SDL_SCANCODE_LEFT:
				case SDL_SCANCODE_RIGHT:
				case SDL_SCANCODE_TAB:
					yes_selected = !yes_selected;
					JE_playSampleNum(S_CURSOR);
					break;
				case SDL_SCANCODE_RETURN:
				case SDL_SCANCODE_SPACE:
					done = true;
					break;
				case SDL_SCANCODE_ESCAPE:
					yes_selected = false;
					done = true;
					break;
				default:
					break;
			}
		}
	}

	return yes_selected;
}

JE_boolean JE_saveRequest(JE_byte slot, const char *savename)
{
	const SaveRequestText text = { slot, savename };
	const bool save_selected = shop_yes_no(save_request_message, &text, false);

	JE_playSampleNum(save_selected ? S_SELECT : S_CLICK);
	return save_selected;
}

JE_boolean JE_quitRequest(void)
{
	const bool quit_selected = shop_yes_no(quit_request_message, NULL, true);

	JE_playSampleNum(quit_selected ? S_SPRING : S_CLICK);

	if (isNetworkGame && quit_selected)
	{
		network_prepare(PACKET_QUIT);
		network_send(4);  // PACKET QUIT

		network_tyrian_halt(0, true);
	}

	return quit_selected;
}

void JE_genItemMenu(JE_byte itemNum)
{
	menuChoices[MENU_UPGRADE_SUB] = itemAvailMax[itemAvailMap[itemNum - 2] - 1] + 2;

	temp3 = 2;
	temp2 = *playeritem_map(&shopPlayer()->items, itemNum - 2);

	strcpy(menuInt[5][0], menuInt[2][itemNum - 1]);

	for (tempW = 0; tempW < itemAvailMax[itemAvailMap[itemNum - 2] - 1]; tempW++)
	{
		temp = itemAvail[itemAvailMap[itemNum - 2] - 1][tempW];
		switch (itemNum)
		{
		case 2:
			strcpy(tempStr, JE_shipName(temp));
			break;
		case 3:
		case 4:
			strcpy(tempStr, JE_weaponPortName(temp));
			break;
		case 5:
			strcpy(tempStr, shields[temp].name);
			break;
		case 6:
			strcpy(tempStr, powerSys[temp].name);
			break;
		case 7:
		case 8:
			strcpy(tempStr, options[temp].name);
			break;
		}
		if (temp == temp2)
		{
			temp3 = tempW + 2;
		}
		strcpy(menuInt[5][tempW], tempStr);
	}

	strcpy(menuInt[5][tempW], miscText[13]);

	curSel[MENU_UPGRADE_SUB] = temp3;
}

void JE_scaleInPicture(SDL_Surface *dst, const SDL_Surface *src)
{
	if (smoothMotion)
	{
		const int last = vga_width / 2;
		const float duration = (float)((last - 2) / 2 + 1) * get_delay_period();
		const float counter_to_ms = 1000.0f / (float)SDL_GetPerformanceFrequency();
		const Uint64 begin = SDL_GetPerformanceCounter();

		for (;;)
		{
			if (JE_anyButton())
				break;

			float t = (float)(SDL_GetPerformanceCounter() - begin) * counter_to_ms / duration;
			if (t > 1.0f)
				t = 1.0f;
			const int i = 2 + (int)((float)(last - 2) * t);

			JE_scaleBitmap(dst, src, last - i, 0, last + i - 1,
			               100 + roundf(i * 200.0f / vga_width) - 1);
			JE_showVGA();

			if (t >= 1.0f)
				break;
			if (!output_vsync)
				limit_render_fps();
		}
		return;
	}

	// Scale to vga_width so the final frame samples the full surface 1:1.
	for (int i = 2; i <= vga_width / 2; i += 2)
	{
		if (JE_anyButton())
			break;

		JE_scaleBitmap(dst, src, vga_width / 2 - i, 0, vga_width / 2 + i - 1,
			100 + roundf(i * 200.0f / vga_width) - 1);
		JE_showVGA();

		SDL_Delay(1);
	}
}

void JE_drawScore(void)
{
	char cl[24];
	if (curMenu == MENU_UPGRADE_SUB)
	{
		sprintf(cl, "%lld", (long long)JE_cashLeft());
		// Keep the power readout centered in the shop's monitor slot.
		JE_textShade(VGAScreen, MENU_MONITOR_CENTER_X - JE_textWidth(cl, TINY_FONT) / 2, 172, cl, 1, 6, DARKEN);
	}
}

void shopSelectLevel(JE_word section, JE_byte file_num)
{
	mainLevel = (JE_byte)section;
	if (file_num != 0)
	{
		lvlFileNum = file_num;
		// Make the exact file survive JE_loadMap's section rescan (which would reset lvlFileNum to
		// the section's first ']L'), so a debug/level pick of a section's non-first cut actually loads.
		forcedLvlFileNum = file_num;
	}
	nextLevel = mainLevel;
	jumpSection = true;
}

void JE_menuFunction(JE_byte select)
{
	JE_byte x;
	JE_word curSelect;

	col = 0;
	colC = -1;
	JE_playSampleNum(S_CLICK);

	curSelect = curSel[curMenu];

	// Handle Customize, then restore stock row numbers for the existing switches.
	if (outpost_customize_row(curMenu) != 0)
	{
		if (select == outpost_customize_row(curMenu))
		{
			curSel[MENU_ONLINE] = ONLINE_ROW_COLOR;
			curMenu = MENU_ONLINE;
			return;
		}
		select = (JE_byte)outpost_stock_row(curMenu, select);
	}

	switch (curMenu)
	{
	case MENU_FULL_GAME:
		switch (select)
		{
		case 2: //cubes -> endless: open the E-Shop submenu
			if (endlessMode)
			{
				if (endlessLockedSortie)  // locked "gave up the level" outpost: the E-Shop is off-limits
					break;
				curMenu = MENU_ESHOP;
				curSel[MENU_ESHOP] = 2;
				configure_endless_shop_menu();  // refresh the E-Shop prices on entry
			}
			else
			{
				curMenu = MENU_DATA_CUBES;
				curSel[MENU_DATA_CUBES] = 2;
			}
			break;
		case 3: // endless: read-only Perks list; campaign: stock Ship Specs
			if (endlessMode)
			{
				endlessPerkListMode = true;
				configure_endless_perk_list_menu();
				curSel[MENU_PERKS] = 2;
				curMenu = MENU_PERKS;
			}
			else
				JE_doShipSpecs();
			break;
		case 4://upgradeship
			if (endlessMode && endlessLockedSortie)  // locked: the loadout is frozen to the launch-time choices
				break;
			curMenu = MENU_UPGRADES;
			break;
		case 5: //options
			curMenu = isNetworkGame ? MENU_LIMITED_OPTIONS : MENU_OPTIONS;
			break;
		case 6: //nextlevel
			if (endlessMode && endlessLockedSortie)
			{
				// Relaunch a surrendered zone without charting a new course.
				endlessArmLockedRelaunch();
				break;
			}
			// Online Endless: the other player charts this one, so Play Next Level waits here for the
			// sector they pick and only then commits to leaving.
			if (endlessCoop() && !endlessLocalPlayerCharts())
			{
				const int course = shopEndlessAwaitCourse(true);
				if (course == SHOP_COURSE_JUMPED)
				{
					// The partner jumped; their level and run state are already adopted. Leave
					// with no course of our own, and the fold is skipped on both machines.
					endlessCoopCourse = -1;
					jumpSection = true;
					break;
				}
				if (course < 0)
				{
					newPal = 1;  // the notice shaded the frame; the loop repaints under a fresh palette
					break;
				}
				endlessCoopCourse = course;
				jumpSection = true;
				break;
			}
			curMenu = MENU_PLAY_NEXT_LEVEL;
			newPal = 18;
			if (endlessMode)
			{
				// Endless next-level rows are pre-generated courses with their modifiers.
				configure_endless_course_menu();
			}
			else
			{
				JE_computeDots();
				navX = planetX[mapOrigin - 1];
				navY = planetY[mapOrigin - 1];
				newNavX = navX;
				newNavY = navY;
				menuChoices[MENU_PLAY_NEXT_LEVEL] = mapPNum + 2;
				curSel[MENU_PLAY_NEXT_LEVEL] = 2;
				strcpy(menuInt[4][0], "Start Level");
				for (x = 0; x < mapPNum; x++)
				{
					temp = mapPlanet[x];
					strcpy(menuInt[4][x + 1], pName[temp - 1]);
				}
				strcpy(menuInt[4][x + 1], miscText[5]);
			}
			break;
		case 7:
			if (debugMode)
			{
				// Debug Menu (equipment): only present when Debug Mode is on.
				JE_debugMenu(true);
				ensure_equipped_items_visible();
				old_items[shopPlayerIndex] = shopPlayer()->items;
				shopPlayer()->last_items = shopPlayer()->items;
			}
			else
			{
				// With Debug Mode off, item 7 is the stock "Quit Game".
				if (JE_quitRequest())
				{
					gameLoaded = true;
					mainLevel = 0;
				}
			}
			break;
		case 8: // debug play level (only reachable when Debug Mode is on)
			// Scrollable picker; on selection it arms the jump (jumpSection),
			// so the buy/sell loop exits and the chosen level loads.
			JE_debugLevelSelect();
			break;
		case 9: //quit (only reachable when Debug Mode is on)
			if (JE_quitRequest())
			{
				gameLoaded = true;
				mainLevel = 0;
			}
			break;
		}
		break;

	case MENU_PERKS:
		if (endlessPerkListMode)
		{
			endlessPerkListMode = false;
			curMenu = MENU_FULL_GAME;
			break;
		}
		if (select == menuChoices[MENU_PERKS])  // "Take the Cash" (decline)
			endlessDeclinePerk();
		else
			endlessTakePerk(select - 2);        // offers are rows 2.., choice index = select - 2
		endlessPerkPending = false;             // consumed; can't return this visit
		// Both are purchases as far as the other machine is concerned: the perk row and the wallet
		// both moved, and its mirror of this ship is stale until it hears so.
		network_shop_send_transaction();
		JE_playSampleNum(S_SELECT);
		curMenu = MENU_FULL_GAME;
		break;

	case MENU_ESHOP:  // endless E-Shop: reroll / reinforce / kill-fire buff / random special
		if (select == menuChoices[MENU_ESHOP]) //done
		{
			curMenu = MENU_FULL_GAME;
		}
		else if (select == 5)  // Extra Perk: charge, then open the perk PICK menu to choose one
		{
			if (endlessTryBuyExtraPerk())
			{
				network_shop_send_transaction();
				JE_playSampleNum(S_SELECT);
				endlessPerkListMode = false;   // a real forced PICK, not the read-only list
				configure_endless_perk_menu();
				curSel[MENU_PERKS] = 2;
				curMenu = MENU_PERKS;
			}
			else
				JE_playSampleNum(S_SPRING);
			configure_endless_shop_menu();
		}
		else
		{
			bool bought = false;
			switch (select)  // Extra Perk (5) handled above; order matches configure_endless_shop_menu
			{
			case 2: bought = endlessTryReroll();          break;  // reroll the shop stock
			case 3: bought = endlessTryBuyCleanse();      break;  // sabotage: strip the next sector's worst mod
			case 4: bought = endlessTryReinforce();       break;  // +max armor (run-persistent)
			case 6: bought = endlessTryBuySpecial();      break;  // random special weapon
			case 7: bought = endlessTryBuyTurbodrive();  break;  // Turbodrive (kill-fire boost)
			case 8: bought = endlessTryBuyOverblast();    break;  // Overblast (damage-only stacks)
			case 9: bought = endlessTryBuyOverdrive();   break;  // Overdrive (Turbodrive + Overblast together)
			case 10: bought = endlessTryBuyRevive();      break;  // one-shot revive token
			case 11: bought = endlessTryBuyBomb();         break;  // +1 superbomb (cap 10)
			case 12: bought = endlessTryGamble();         break;  // random good/bad outcome (pinned last)
			}
			JE_playSampleNum(bought ? S_SELECT : S_SPRING);
			if (bought)
				network_shop_send_transaction();  // cash, drives, charges and tokens all live in the block
			if (bought && select == 2)  // a reroll regenerated the stock: re-sort so None sinks to the bottom
			{
				sort_shop_inventory();
				endlessHoistStartWeapon();  // ...and keep the starting gun leading the list at zone 1
			}
			configure_endless_shop_menu();  // refresh prices / bought-state labels
			// A free perk pick can only come from the Gamble (select 12); no other buy grants one.
			if (bought && select == 12 && endlessGambleWonPerk())  // gamble handed us a free perk: open the pick menu now
			{
				endlessPerkListMode = false;   // a real forced PICK, not the read-only list
				configure_endless_perk_menu();
				curSel[MENU_PERKS] = 2;
				curMenu = MENU_PERKS;
			}
			endlessClearGamblePerk();  // one-shot: never let "won a perk" persist past the buy that set it
		}
		break;

	case MENU_UPGRADES:
		if (select == menuChoices[MENU_UPGRADES]) //done (item 9, or 10 when Custom is present)
		{
			curMenu = MENU_FULL_GAME;
		}
		else if (customWeaponEnabled && select == 9) // Custom: open the creator, equip from there
		{
			const bool equipped = JE_customWeaponCreator(true);
			if (equipped)
			{
				// Equipped: reflect the new loadout so the shop keeps it and the custom
				// weapon shows in its Front/Rear weapon list.
				ensure_equipped_items_visible();
				old_items[shopPlayerIndex] = shopPlayer()->items;
				shopPlayer()->last_items = shopPlayer()->items;
			}

			// The new loadout has to reach the other machine like any other purchase; the
			// design behind it goes out at the rendezvous.
			if (equipped)
				network_shop_send_transaction();
		}
		else // selected item to upgrade
		{
			old_items[shopPlayerIndex] = shopPlayer()->items;

			weaponSimTime = 0;
			lastDirection = 1;
			JE_genItemMenu(select);
			JE_initWeaponView();
			upgradeSubScrollTop = 1;  // start the (possibly scrolling) sub-list at the top
			upgradeSubPrevSel = 0;    // force the view to snap to the selection on entry
			curMenu = MENU_UPGRADE_SUB;
			lastCurSel = curSel[MENU_UPGRADE_SUB];
			// Temporarily include the trade-in value. No other cash operation may run
			// before endlessShopTradeCommit restores and audits the balance.
			endlessShopTradeBegin();
			player_set_cash(shopPlayer(), shopPlayer()->cash * 2 - JE_cashLeft());
		}
		break;

	// Map offline and online option rows to shared OPT_* identifiers.
	case MENU_OPTIONS:
	case MENU_LIMITED_OPTIONS:
	{
		/* Resolve against the active page before changing curMenu. Online row 2 is Save, while
		 * the load/save page uses the offline Options numbering. */
		const int option = options_full_row(select);
		switch (option)
		{
		case OPT_LOAD:
		case OPT_SAVE:
			// Endless hardcore forbids ALL saving/loading (these rows are greyed out in
			// JE_drawMenuChoices); a deny beep confirms the press did nothing.
			if (endlessMode && endlessHardcore())
			{
				JE_playSampleNum(S_SPRING);
				break;
			}
			curMenu = MENU_LOAD_SAVE;
			performSave = (option == OPT_SAVE);
			quikSave = false;
			break;
		case OPT_JOYSTICK:
			curMenu = MENU_JOYSTICK_CONFIG;
			break;
		case OPT_KEYBOARD:
			curMenu = MENU_KEYBOARD_CONFIG;
			break;
		case OPT_MOUSE:
			curMenu = MENU_MOUSE_CONFIG;
			break;
		case OPT_EXIT:
			curMenu = outpost_menu();
			break;
		}
		break;
	}

	// Open each picker on its current value.
	case MENU_ONLINE:
		switch (online_full_row(select))
		{
		case ONLINE_ROW_COLOR:
			// Endless-reserved banks can change while the shop remains open.
			configure_online_color_grid();
			curSel[MENU_ONLINE_COLOR] =
				(JE_byte)(2 + online_color_find_cell(netStyleSeatColor(netStyleLocalSeat())));
			curMenu = MENU_ONLINE_COLOR;
			JE_initWeaponView();
			break;
		case ONLINE_ROW_OPACITY:
			curSel[MENU_ONLINE_OPACITY] = (JE_byte)(ONLINE_OPACITY_FIRST_ROW
				+ (NET_OPACITY_FULL - netStyleLocalView().opacity) / NET_OPACITY_STEP);
			curMenu = MENU_ONLINE_OPACITY;
			JE_initWeaponView();
			break;
		case ONLINE_ROW_HPBARS:
			curSel[MENU_ONLINE_HPBARS] = (JE_byte)(2 + netStyleLocalView().hpBars);
			curMenu = MENU_ONLINE_HPBARS;
			JE_initWeaponView();
			break;
		default:
			curMenu = outpost_menu();
			break;
		}
		break;

	// Values apply on highlight; pressing a value or Done returns.
	case MENU_ONLINE_COLOR:
	case MENU_ONLINE_HPBARS:
		curMenu = MENU_ONLINE;
		break;

	case MENU_ONLINE_OPACITY:
		// The ship toggle changes in place; other rows return.
		if (select == ONLINE_OPACITY_TOGGLE_ROW)
		{
			NetShipView view = netStyleLocalView();
			view.shipOpacity = !view.shipOpacity;
			online_set_local_view(view);

			JE_playSampleNum(S_CLICK);
			break;
		}
		curMenu = MENU_ONLINE;
		break;

	case MENU_PLAY_NEXT_LEVEL:
		if (select == endlessCourseRerollRow())
		{
			// Radar perk: chart this outpost again, once. The row is drawn greyed once it is
			// spent, so a press then only confirms that nothing happened.
			if (endlessCourseRerollSpent())
			{
				JE_playSampleNum(S_CLINK);
				break;
			}
			endlessCourseReroll();
			configure_endless_course_menu();
			JE_playSampleNum(S_ITEM);
			break;
		}
		if (select == menuChoices[MENU_PLAY_NEXT_LEVEL]) //exit
		{
			curMenu = MENU_FULL_GAME;
			newPal = 1;
		}
		else if (endlessCoop())
		{
			// Online Endless: hold the pick until both players have finished shopping, so the
			// sector is folded from a complete pair of purchase sets (see shopLeaveOutpost).
			endlessCoopCourse = (int)curSelect - 2;
			jumpSection = true;
		}
		else if (endlessMode)
		{
			// Endless: the chosen entry is a course; apply its mutators + episode, then launch.
			shopSelectLevel(endlessSelectCourse(curSelect - 2), 0);
		}
		else
		{
			shopSelectLevel(mapSection[curSelect - 2], 0);
		}
		break;

	case MENU_UPGRADE_SUB:
		if (curSel[MENU_UPGRADE_SUB] < menuChoices[MENU_UPGRADE_SUB])
		{
			// select done
			curSel[MENU_UPGRADE_SUB] = menuChoices[MENU_UPGRADE_SUB];
		}
		else // if done is selected
		{
			JE_playSampleNum(S_ITEM);

			player_set_cash(shopPlayer(), JE_cashLeft());
			endlessShopTradeCommit();
			network_shop_send_transaction();
			curMenu = MENU_UPGRADES;
		}
		break;

	case MENU_KEYBOARD_CONFIG:
		if (curSelect == 10) /* reset to defaults */
		{
			memcpy(keySettings, defaultKeySettings, sizeof(keySettings));
		}
		else if (curSelect == 11) /* done */
		{
			curMenu = isNetworkGame
				? MENU_LIMITED_OPTIONS
				: MENU_OPTIONS;
		}
		else /* change key */
		{
			temp2 = 254;
			int tempY = 38 + (curSelect - 2) * 12;
			JE_textShade(VGAScreen, 236, tempY, SDL_GetScancodeName(keySettings[curSelect-2]), (temp2 / 16), (temp2 % 16) - 8, DARKEN);
			JE_showVGA();

			wait_noinput(true, true, true);

			col = 248;
			colC = 1;

			do
			{
				setDelay(1);

				// The capture blocks until a key arrives; online, the wait can outlast the
				// peer's timeout unless the connection is serviced through it.
				NETWORK_KEEP_ALIVE();
				while (network_shop_pump())
					;

				col += colC;
				if (col < 243 || col > 248)
				{
					colC *= -1;
				}
				JE_rectangle(VGAScreen, 230, tempY - 2, 300, tempY + 7, col);

				poll_joysticks();
				service_SDL_events(true);

				JE_showVGA();

				wait_delay();
			} while (!newkey && !mousedown && !joydown);
			
			if (newkey)
			{
				// already used? then swap
				for (uint i = 0; i < COUNTOF(keySettings); ++i)
				{
					if (keySettings[i] == lastkey_scan)
					{
						keySettings[i] = keySettings[curSelect-2];
						break;
					}
				}
				
				if (lastkey_scan != SDL_SCANCODE_ESCAPE && // reserved for menu
				    lastkey_scan != SDL_SCANCODE_F11 &&    // reserved for gamma
				    lastkey_scan != SDL_SCANCODE_P)        // reserved for pause
				{
					JE_playSampleNum(S_CLICK);
					keySettings[curSelect-2] = lastkey_scan;
					++curSelect;
				}
			}
		}
		break;

	case MENU_LOAD_SAVE:
		if (curSelect == 13)
		{
			if (quikSave)
			{
				curMenu = oldMenu;
				newPal = oldPal;
			}
			else
			{
				curMenu = isNetworkGame ? MENU_LIMITED_OPTIONS : MENU_OPTIONS;
			}
		}
		else
		{
			lastLoadSaveSel = (JE_byte)curSelect;
			if (twoPlayerMode)
				temp = 11;
			else
				temp = 0;
			JE_operation(curSelect - 1 + temp);
			if (quikSave)
			{
				curMenu = oldMenu;
				newPal = oldPal;
			}
		}
		break;

	case MENU_DATA_CUBES:
		if (curSelect == menuChoices[curMenu])
		{
			curMenu = MENU_FULL_GAME;
			newPal = 1;
		}
		else
		{
			if (cubeMax > 0)
			{
				firstMenu9 = true;
				curMenu = MENU_DATA_CUBE_SUB;
				yLoc = 0;
				yChg = 0;
				currentCube = curSel[MENU_DATA_CUBES] - 2;
			}
			else
			{
				curMenu = MENU_FULL_GAME;
				newPal = 1;
			}
		}
		break;

	case MENU_DATA_CUBE_SUB:
		curMenu = MENU_DATA_CUBES;
		break;

	case MENU_2_PLAYER_ARCADE:
		switch (select)
		{
		case 2:
			mainLevel = mapSection[mapPNum-1];
			jumpSection = true;
			break;
		case 3:
		case 4:
			JE_playSampleNum(S_CURSOR);

			int temp = curSel[curMenu] - 3;
			do
			{
				if (joysticks == 0)
					inputDevice[temp == 0 ? 1 : 0] = inputDevice[temp]; // swap controllers
				if (inputDevice[temp] >= 2 + joysticks)
					inputDevice[temp] = 1;
				else
					inputDevice[temp]++;
			} while (inputDevice[temp] == inputDevice[temp == 0 ? 1 : 0]);
			break;
		case 5:
			curMenu = isNetworkGame ? MENU_LIMITED_OPTIONS : MENU_OPTIONS;
			break;
		case 6:
			if (debugMode)
			{
				// Debug Menu (equipment); only present when Debug Mode is on.
				JE_debugMenu(true);
				ensure_equipped_items_visible();
				old_items[shopPlayerIndex] = shopPlayer()->items;
				shopPlayer()->last_items = shopPlayer()->items;
			}
			else if (JE_quitRequest())
			{
				// With Debug Mode off, item 6 is the stock "Quit Game".
				gameLoaded = true;
				mainLevel = 0;
			}
			break;
		case 7: // debug play level (only reachable when Debug Mode is on)
			JE_debugLevelSelect();
			break;
		case 8: // quit (only reachable when Debug Mode is on)
			if (JE_quitRequest())
			{
				gameLoaded = true;
				mainLevel = 0;
			}
			break;
		}
		break;

	case MENU_1_PLAYER_ARCADE:
		switch (select)
		{
		case 2:
			mainLevel = mapSection[mapPNum-1];
			jumpSection = true;
			break;
		case 3:
			curMenu = isNetworkGame
				? MENU_LIMITED_OPTIONS
				: MENU_OPTIONS;
			break;
		case 4:
			if (debugMode)
			{
				// Debug Menu (equipment); only present when Debug Mode is on.
				JE_debugMenu(true);
				ensure_equipped_items_visible();
				old_items[shopPlayerIndex] = shopPlayer()->items;
				shopPlayer()->last_items = shopPlayer()->items;
			}
			else if (JE_quitRequest())
			{
				// With Debug Mode off, item 4 is the stock "Quit Game".
				gameLoaded = true;
				mainLevel = 0;
			}
			break;
		case 5: // debug play level (only reachable when Debug Mode is on)
			JE_debugLevelSelect();
			break;
		case 6: // quit (only reachable when Debug Mode is on)
			if (JE_quitRequest())
			{
				gameLoaded = true;
				mainLevel = 0;
			}
			break;
		}
		break;

	case MENU_JOYSTICK_CONFIG:
		if (joysticks == 0 && select != 17)
			break;

		switch (select)
		{
		case 2:
			joystick_config++;
			joystick_config %= joysticks;
			break;
		case 3:
			joystick[joystick_config].analog = !joystick[joystick_config].analog;
			break;
		case 4:
			if (joystick[joystick_config].analog)
			{
				joystick[joystick_config].sensitivity++;
				joystick[joystick_config].sensitivity %= 11;
			}
			break;
		case 5:
			if (joystick[joystick_config].analog)
			{
				joystick[joystick_config].threshold++;
				joystick[joystick_config].threshold %= 11;
			}
			break;
		case 16:
			reset_joystick_assignments(joystick_config);
			save_joystick_config_now();
			break;
		case 17:
			save_joystick_config_now();  // also persists analog/sensitivity/threshold on the way out
			curMenu = isNetworkGame
				? MENU_LIMITED_OPTIONS
				: MENU_OPTIONS;
			break;
		default:
			if (joysticks == 0)
				break;

			JE_rectangle(VGAScreen, 235, 21 + select * 8, 310, 30 + select * 8, 248);

			Joystick_assignment temp;
			if (detect_joystick_assignment(joystick_config, &temp))
			{
				// if the detected assignment was already set, unset it
				for (uint i = 0; i < COUNTOF(*joystick->assignment); i++)
				{
					if (joystick_assignment_cmp(&temp, &joystick[joystick_config].assignment[select - 6][i]))
					{
						joystick[joystick_config].assignment[select - 6][i].type = NONE;
						goto joystick_assign_done;
					}
				}

				// if there is an empty assignment, set it
				for (uint i = 0; i < COUNTOF(*joystick->assignment); i++)
				{
					if (joystick[joystick_config].assignment[select - 6][i].type == NONE)
					{
						joystick[joystick_config].assignment[select - 6][i] = temp;
						goto joystick_assign_done;
					}
				}

				// if no assignments are empty, shift them all forward and set the last one
				for (uint i = 0; i < COUNTOF(*joystick->assignment); i++)
				{
					if (i == COUNTOF(*joystick->assignment) - 1)
						joystick[joystick_config].assignment[select - 6][i] = temp;
					else
						joystick[joystick_config].assignment[select - 6][i] = joystick[joystick_config].assignment[select - 6][i + 1];
				}

joystick_assign_done:
				curSelect++;

				poll_joysticks();
				save_joystick_config_now();  // persist the new binding to disk immediately
			}
		}
		break;

	case MENU_SUPER_TYRIAN:
		switch (select)
		{
		case 2:
			mainLevel = mapSection[mapPNum-1];
			jumpSection = true;
			break;
		case 3:
			JE_doShipSpecs();
			break;
		case 4:
			curMenu = isNetworkGame ? MENU_LIMITED_OPTIONS : MENU_OPTIONS;
			break;
		case 5:
			if (debugMode)
			{
				// Debug Menu (equipment); only present when Debug Mode is on.
				JE_debugMenu(true);
				ensure_equipped_items_visible();
				old_items[shopPlayerIndex] = shopPlayer()->items;
				shopPlayer()->last_items = shopPlayer()->items;
			}
			else if (JE_quitRequest())
			{
				// With Debug Mode off, item 5 is the stock "Quit Game".
				if (isNetworkGame)
				{
					JE_tyrianHalt(0);
				}
				gameLoaded = true;
				mainLevel = 0;
			}
			break;
		case 6: // debug play level (only reachable when Debug Mode is on)
			JE_debugLevelSelect();
			break;
		case 7: // quit (only reachable when Debug Mode is on)
			if (JE_quitRequest())
			{
				if (isNetworkGame)
				{
					JE_tyrianHalt(0);
				}
				gameLoaded = true;
				mainLevel = 0;
			}
			break;
		}
		break;

	case MENU_MOUSE_CONFIG:
		switch(curSel[curMenu])
		{
			case 2:
			case 3:
			case 4:
				temp = curSel[curMenu] - 2;
				if (++mouseSettings[temp] > 5)
					mouseSettings[temp] = 1;
				break;
			case 5:
				memcpy(mouseSettings, defaultMouseSettings, sizeof(mouseSettings));
				break;
			case 6:
				curMenu = isNetworkGame ? MENU_LIMITED_OPTIONS : MENU_OPTIONS;
				break;
		}
	}

	old_items[shopPlayerIndex] = shopPlayer()->items;
}

void JE_drawShipSpecs(SDL_Surface * screen, SDL_Surface * temp_screen)
{
	/* Build the ship description image using the supplied temporary surface. */

	int temp_x = 0, temp_y = 0, temp_index;
	Uint8 *src, *dst;

	// Draw the text and frame first.
	JE_clr256(screen);
	JE_drawLines(screen, true);
	JE_drawLines(screen, false);
	JE_rectangle(screen, 0, 0, 319, 199, 37);
	JE_rectangle(screen, 1, 1, 318, 198, 35);

	verticalHeight = 9;
	JE_outText(screen, 10, 2, ships[shopPlayer()->items.ship].name, 12, 3);
	JE_helpBox(screen, 100, 20, shipInfo[shopPlayer()->items.ship-1][0], 40);
	JE_helpBox(screen, 100, 100, shipInfo[shopPlayer()->items.ship-1][1], 40);
	verticalHeight = 7;

	JE_outText(screen, JE_fontCenter(miscText[4], TINY_FONT), 190, miscText[4], 12, 2);

	//now draw the green ship over that.
	//This hardcoded stuff is for positioning our little ship graphic
	if (shopPlayer()->items.ship > 90)
	{
		temp_index = 32;
	}
	else if (shopPlayer()->items.ship > 0)
	{
		temp_index = ships[shopPlayer()->items.ship].bigshipgraphic;
	}
	else
	{
		temp_index = ships[old_items[shopPlayerIndex].ship].bigshipgraphic;
	}

	switch (temp_index)
	{
		case 32:
			temp_x = 35;
			temp_y = 33;
			break;
		case 28:
			temp_x = 31;
			temp_y = 36;
			break;
		case 33:
			temp_x = 31;
			temp_y = 35;
			break;
		case 45:
			temp_x = 36;
			temp_y = 33;
			break;
		case 46:
			temp_x = 30;
			temp_y = 30;
			break;
		default:
			assert(0);
	}
	temp_x -= 30;

	//draw the ship into our temp buffer.
	JE_clr256(temp_screen);
	blit_sprite(temp_screen, temp_x, temp_y, OPTION_SHAPES, temp_index - 1);  // ship illustration

	/* Convert the composed ship image to the green Ship Specs palette one line at a time. */
	dst = screen->pixels;
	src = temp_screen->pixels;
	for (int y = 0; y < screen->h; y++)
	{
		for (int x = 0; x < screen->pitch; x++)
		{
			int avg = 0;
			if (y > 0)
				avg += *(src - screen->pitch) & 0x0f;
			if (y < screen->h - 1)
				avg += *(src + screen->pitch) & 0x0f;
			if (x > 0)
				avg += *(src - 1) & 0x0f;
			if (x < screen->pitch - 1)
				avg += *(src + 1) & 0x0f;
			avg /= 4;

			if ((*src & 0x0f) > avg)
				*dst = (*src & 0x0f) | 0xc0;
			src++;
			dst++;
		}
	}
}

// Draw preview sidekicks with the same body sheets and mounts as gameplay.
static void JE_drawSimSidekicks(void)
{
	for (uint i = 0; i < 2; ++i)
	{
		const JE_OptionType *o = &options[shopPlayer()->items.sidekick[i]];
		if (o->option == 0)
			continue;  // slot empty ("None")

		// The equipped option can change as you browse the shop without the frame counter being
		// reset, so keep it in range before indexing gr[], then advance it like gameplay.
		if (shopPlayer()->sidekick[i].animation_frame >= o->ani)
			shopPlayer()->sidekick[i].animation_frame = 0;
		if (shopPlayer()->sidekick[i].animation_enabled)
		{
			if (++shopPlayer()->sidekick[i].animation_frame >= o->ani)
			{
				shopPlayer()->sidekick[i].animation_frame = 0;
				shopPlayer()->sidekick[i].animation_enabled = (o->option == 1);
			}
		}

		const int x = shopPlayer()->sidekick[i].x, y = shopPlayer()->sidekick[i].y;
		const uint sprite = o->gr[shopPlayer()->sidekick[i].animation_frame];

		// Give moving sidekicks stable render IDs across preview ticks.
		rl_current_id = RL_ID_SIDEKICK_BASE + 2 + (int)i;
		if (o->tr == 4)
		{
			// Sub-pixel remainder of the rounded orbit offset, as in gameplay
			// (the sidekick draw in JE_mainGamePlayerFunctions).
			const float ox = sinf(optionSatelliteRotate) * 20,
			            oy = cosf(optionSatelliteRotate) * 20;
			const float dir = (i == LEFT_SIDEKICK) ? 1.0f : -1.0f;  // right slot mirrors
			rl_current_sub_x = dir * (ox - roundf(ox));
			rl_current_sub_y = dir * (oy - roundf(oy));
		}
		if (o->tr == 1 || o->tr == 2)
			blit_preview_ship2x2(VGAScreen, x - 6, y, spriteSheet10, sprite);
		else
			blit_preview_ship2(VGAScreen, x, y, spriteSheet9, sprite);
		rl_current_id = 0;
		rl_current_sub_x = rl_current_sub_y = 0.0f;
	}
}

void JE_weaponSimUpdate(void)
{
	char buf[32];

	JE_weaponViewFrame();

	++weaponSimTime;
	weaponSimTime %= 150;

	draw_preview_hull(shop_draw_seat(), shopPlayer()->x - 5, shopPlayer()->y - 7);

	JE_drawSimSidekicks();  // pods on top of the ship, matching gameplay layering

	// Reuse the gameplay worker with half-full samples; outpost values are empty.
	if (curMenu == MENU_ONLINE_HPBARS && netStyleLocalView().hpBars != NET_HP_BARS_OFF)
	{
		int l, r, t, b;
		hud_ship_hp_bar_box(shop_draw_seat(), &l, &r, &t, &b);
		hud_draw_ship_hp_bars_at(0, l, r, t, b, HUD_HP_BAR_SAMPLE_SHIELD,
		                         HUD_HP_BAR_SAMPLE_SHIELD_MAX, HUD_HP_BAR_SAMPLE_ARMOR,
		                         HUD_HP_BAR_SAMPLE_ARMOR_MAX);
	}

	// Draw power and cost HUD after ships and sidekicks.
	if (leftPower || rightPower)
	{
		if (!leftPower)
			blit_sprite(VGAScreenSeg, 24, 149, OPTION_SHAPES, 13);  // downgrade disabled
		if (!rightPower || !rightPowerAfford)
			blit_sprite(VGAScreenSeg, 119, 149, OPTION_SHAPES, 14);  // upgrade disabled

		temp = shopPlayer()->items.weapon[curSel[MENU_UPGRADES]-3].power;

		// The blinking caption only; the touch button is asked for from the shop's own frame
		// path, which still runs once this row is no longer selected (outpostRearModeCyclable).
		if (outpostRearModeCyclable() && (weaponSimTime >= 75))
		{
#if defined(__SWITCH__) || defined(__vita__)
			// Console shoulder-button help must fit inside the preview border.
			JE_outTextOutlined(VGAScreen, 22, 137, "[L/R] Rear Weapon Mode", 1, 4);
#else
			JE_outTextOutlined(VGAScreen, 28, 137, miscText[70], 1, 4);
#endif
		}
		else
		{
			// This row sits over the preview's starfield and passing ships, so it carries an outline.
			if (leftPower)
			{
				sprintf(buf, "%lld", (long long)downgradeCost);
				JE_outTextOutlined(VGAScreen, 26, 137, buf, 1, 4);
			}
			if (rightPower)
			{
				sprintf(buf, "%lld", (long long)upgradeCost);
				JE_outTextOutlined(VGAScreen, 104, 137, buf, (rightPowerAfford) ? 1 : 7, 4);
			}

			sprintf(buf, "%s %d", miscTextB[5], temp);
			JE_outTextOutlined(VGAScreen, 58, 137, buf, 15, 4);
		}

		for (int x = 1; x <= temp; x++)
		{
			fill_rectangle_xy(VGAScreen, 39 + x * 6, 151, 39 + x * 6 + 4, 151, 251);
			JE_pix(VGAScreen, 39 + x * 6, 151, 252);
			fill_rectangle_xy(VGAScreen, 39 + x * 6, 152, 39 + x * 6 + 4, 164, 250);
			fill_rectangle_xy(VGAScreen, 39 + x * 6, 165, 39 + x * 6 + 4, 165, 249);
		}
	}
	else
		blit_sprite(VGAScreenSeg, 20, 146, OPTION_SHAPES, 17);  // hide power level interface
}

void JE_weaponViewFrame(void)
{
	fill_rectangle_xy(VGAScreen, 8, 8, 143, 182, 0);

	/* JE: (* Port Configuration Display *)
	(*    drawportconfigbuttons;*/

	// Keep stars inside the preview box; black menu pixels otherwise look like empty space.
	starfield_set_clip(8, 8, 143, 182);
	update_and_draw_starfield(VGAScreen, 1);
	starfield_clear_clip();

	mouseX = shopPlayer()->x;
	mouseY = shopPlayer()->y;

	// Apply owned fire-rate perks to the preview. Adrenaline is excluded because
	// it requires hull damage.
	if (endlessMode)
	{
		const int dec = endlessPerkPreviewFireDecrements();
		for (unsigned i = 0; i < COUNTOF(shotRepeat); i++)
			for (int k = 0; k < dec && shotRepeat[i] > 0; k++)
				--shotRepeat[i];
	}

	// create shots in weapon simulator
	for (uint i = 0; i < 2; ++i)
	{
		if (shotRepeat[i] > 0)
		{
			--shotRepeat[i];
		}
		else
		{
			const uint item       = shopPlayer()->items.weapon[i].id,
			           item_power = shopPlayer()->items.weapon[i].power - 1,
			           item_mode = (i == REAR_WEAPON) ? shopPlayer()->weapon_mode - 1 : 0;

			// Zica Laser Lv11 tweaks: mirror the in-game fire (JE_mainGamePlayerFunctions) so the
			// preview matches.
			const bool zica_l11 = (item == 5 && item_power == 10);
			JE_word l11_primary = weaponPort[item].op[item_mode][item_power];
			if (zica_l11 && zicaLaserLength == ZICA_LEN_LONG)
				l11_primary = ZICA_LONG_WEAP_LEFT;

			b = player_shot_create(item, i, shopPlayer()->x, shopPlayer()->y, mouseX, mouseY, l11_primary, 1);

			if (b < MAX_PWEAPON && zica_l11 && (zicaLaserLength == ZICA_LEN_LONG || zicaLaserBuff))
			{
				JE_word saved_poweruse = weaponPort[item].poweruse;
				weaponPort[item].poweruse = 0;
				if (zicaLaserLength == ZICA_LEN_LONG)
					player_shot_create(item, i, shopPlayer()->x, shopPlayer()->y, mouseX, mouseY, ZICA_LONG_WEAP_RIGHT, 1);
				if (zicaLaserBuff)
					player_shot_create(item, i, shopPlayer()->x, shopPlayer()->y, mouseX, mouseY, weaponPort[item].op[item_mode][9], 1);
				weaponPort[item].poweruse = saved_poweruse;
			}
		}
	}

	// Position and fire preview sidekicks from their gameplay mounts.
	const bool bothFront = options[shopPlayer()->items.sidekick[LEFT_SIDEKICK]].tr == 2
	                    && options[shopPlayer()->items.sidekick[RIGHT_SIDEKICK]].tr == 2;

	// advance the shared satellite angle exactly like gameplay
	if (options[shopPlayer()->items.sidekick[LEFT_SIDEKICK]].tr == 4
	    && options[shopPlayer()->items.sidekick[RIGHT_SIDEKICK]].tr == 4)
		optionSatelliteRotate += 0.2f;
	else if (options[shopPlayer()->items.sidekick[LEFT_SIDEKICK]].tr == 4
	         || options[shopPlayer()->items.sidekick[RIGHT_SIDEKICK]].tr == 4)
		optionSatelliteRotate += 0.15f;

	for (uint i = 0; i < 2; ++i)
	{
		const uint item = shopPlayer()->items.sidekick[i];
		const JE_OptionType *o = &options[item];
		const uint shot_i = (i == LEFT_SIDEKICK) ? SHOT_LEFT_SIDEKICK : SHOT_RIGHT_SIDEKICK;

		switch (o->tr)
		{
		case 0:  // fixed side pod (e.g. Zica Supercharger)
			shopPlayer()->sidekick[i].x = shopPlayer()->x + ((i == LEFT_SIDEKICK) ? -14 : 16);
			shopPlayer()->sidekick[i].y = shopPlayer()->y;
			break;
		case 2:  // front-mounted
			shopPlayer()->sidekick[i].x = !bothFront ? shopPlayer()->x
			                        : (i == LEFT_SIDEKICK ? shopPlayer()->x - FRONT_OPTION_SPREAD
			                                              : shopPlayer()->x + FRONT_OPTION_SPREAD);
			shopPlayer()->sidekick[i].y = MAX(10, shopPlayer()->y - 20);
			break;
		case 4:  // orbiting satellite (e.g. Satellite Marlo); the two slots orbit opposite ends
		{
			const int dx = roundf(sinf(optionSatelliteRotate) * 20),
			          dy = roundf(cosf(optionSatelliteRotate) * 20);
			shopPlayer()->sidekick[i].x = shopPlayer()->x + ((i == LEFT_SIDEKICK) ? dx : -dx);
			shopPlayer()->sidekick[i].y = shopPlayer()->y + ((i == LEFT_SIDEKICK) ? dy : -dy);
			break;
		}
		default:  // Show trailing companions side by side.
			shopPlayer()->sidekick[i].x = (i == LEFT_SIDEKICK) ? 72 - 15 : 72 + 15;
			shopPlayer()->sidekick[i].y = 120;
			break;
		}

		if (o->wport > 0)
		{
			if (shotRepeat[shot_i] > 0)
			{
				--shotRepeat[shot_i];
			}
			else
			{
				// Same drop as gameplay for a trailing large body (see player.h).
				const int shot_y = shopPlayer()->sidekick[i].y + (o->tr == 1 ? SIDEKICK_TRAIL_SHOT_Y : 0);
				// Twin Pods as in play: the own volley inboard, the twin outboard, centred on the pod.
				// Use the previewed seat's Twin Pods offset.
				const int twin_dx = endlessPerkTwinPodOffset(shop_draw_seat(), i);
				const int shot_x = shopPlayer()->sidekick[i].x - twin_dx;

				b = player_shot_create(o->wport, shot_i, shot_x, shot_y, mouseX, mouseY, o->wpnum, 1);
				player_shot_create_twin(b, o->wport, i, twin_dx, shopPlayer()->sidekick[i].x, shot_y,
				                        mouseX, mouseY, o->wpnum, 1);
				shopPlayer()->sidekick[i].animation_enabled = true;  // animate the body while it fires
			}
		}
	}

	simulate_player_shots();

	// Clip long-lived spark trails to the preview box.
	JE_setSPClip(8, 8, 143, 182);
	JE_drawSP();  // draw the shot superspark trails inside the box before the interface sprite
	              // frames it; without this the sparks generate but never show
	JE_clearSPClip();

	blit_sprite(VGAScreenSeg, 0, 0, OPTION_SHAPES, 12); // upgrade interface

	// weapon mode indicator
	if (shopPlayer()->weapon_mode == 1)
	{
		blit_sprite(VGAScreenSeg, 3, 56, OPTION_SHAPES, 18);  // lit
		blit_sprite(VGAScreenSeg, 3, 64, OPTION_SHAPES, 19);  // unlit
	}
	else // == 2
	{
		blit_sprite(VGAScreenSeg, 3, 56, OPTION_SHAPES, 19);  // unlit
		blit_sprite(VGAScreenSeg, 3, 64, OPTION_SHAPES, 18);  // lit
	}

	/* Power bar. */

	power += powerAdd;
	if (power > 900)
		power = 900;

	// Save the clean gauge background for display-rate interpolation.
	menu_power_capture_bg(VGAScreen);
	menu_power_prev = menu_power_cur;
	menu_power_cur = power;

	draw_menu_power_bar(VGAScreen, power);

	lastPower = 147 - (power / 10);

}

void qa_test_endless_shop_stock(void)
{
	const JE_boolean savedEndless = endlessMode, savedTwo = twoPlayerMode;
	const JE_boolean savedOne = onePlayerAction, savedCoopEndless = coopEndlessMode;
	const bool savedNet = isNetworkGame;
	const uint savedNum = thisPlayerNum, savedShopIdx = shopPlayerIndex;
	Player savedPlayers[COUNTOF(player)];
	JE_byte savedAvail[9][10], savedAvailMax[9];
	memcpy(savedPlayers, player, sizeof(savedPlayers));
	memcpy(savedAvail, itemAvail, sizeof(savedAvail));
	memcpy(savedAvailMax, itemAvailMax, sizeof(savedAvailMax));

	endlessMode = true;
	coopEndlessMode = false;
	twoPlayerMode = false;
	onePlayerAction = false;
	isNetworkGame = false;
	thisPlayerNum = 1;
	shopPlayerIndex = 0;

	const JE_byte carried = 39, tradedIn = 41, absent = 43, customShip = 91;
	memset(&player[0].items, 0, sizeof(player[0].items));
	player[0].items.ship = 1;
	player[0].items.weapon[FRONT_WEAPON].id = carried;
	player[0].items.weapon[FRONT_WEAPON].power = 1;
	player[0].items.generator = 1;
	player[0].items.shield = 1;
	player[0].last_items = player[0].items;
	player[0].last_items.weapon[FRONT_WEAPON].id = tradedIn;
	player[0].last_items.ship = customShip;

	memset(itemAvail, 0, sizeof(itemAvail));
	memset(itemAvailMax, 0, sizeof(itemAvailMax));
	const int frontRow = itemAvailMap[1] - 1, shipRow = itemAvailMap[0] - 1;
	itemAvail[frontRow][0] = carried;
	itemAvail[frontRow][1] = 45;
	itemAvailMax[frontRow] = 2;
	itemAvail[shipRow][0] = player[0].items.ship;
	itemAvailMax[shipRow] = 1;

	shop_stock_owned_items();
	qa_check(itemAvailMax[frontRow] == 2
	         && itemAvail[frontRow][0] == carried && itemAvail[frontRow][1] == 45,
	         "an Endless outpost does not restock the gear a purchase replaced");
	qa_check(itemAvailMax[shipRow] == 1 && itemAvail[shipRow][0] == player[0].items.ship,
	         "...including a custom ship, which nothing else stocks");

	player[0].items.weapon[FRONT_WEAPON].id = absent;
	shop_stock_owned_items();
	qa_check(itemAvailMax[frontRow] == 3 && itemAvail[frontRow][2] == absent,
	         "...but gear the ship carries and the shelves lack is still stocked");

	endlessMode = false;
	memset(itemAvail, 0, sizeof(itemAvail));
	memset(itemAvailMax, 0, sizeof(itemAvailMax));
	itemAvail[frontRow][0] = carried;
	itemAvailMax[frontRow] = 1;
	shop_stock_owned_items();
	qa_check(itemAvailMax[frontRow] == 2 && itemAvail[frontRow][1] == tradedIn,
	         "a campaign shop keeps stocking from the last level's loadout");

	memcpy(itemAvailMax, savedAvailMax, sizeof(itemAvailMax));
	memcpy(itemAvail, savedAvail, sizeof(itemAvail));
	memcpy(player, savedPlayers, sizeof(savedPlayers));
	shopPlayerIndex = savedShopIdx;
	thisPlayerNum = savedNum;
	isNetworkGame = savedNet;
	coopEndlessMode = savedCoopEndless;
	onePlayerAction = savedOne;
	twoPlayerMode = savedTwo;
	endlessMode = savedEndless;
}
