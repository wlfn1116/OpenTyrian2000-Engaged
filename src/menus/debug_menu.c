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
#include "debug_menu.h"

#include "audio.h"
#include "config.h"
#include "console_platform.h"
#include "crashlog.h"
#include "debug_flags.h"
#include "debug_levels.h"
#include "draw.h"
#include "endless.h"
#include "episodes.h"
#include "extra_ships.h"
#include "font.h"
#include "game_loop.h"
#include "game_state.h"
#include "helptext.h"
#include "hud.h"
#include "ingame_menu.h"
#include "joystick.h"
#include "keyboard.h"
#include "lvlmast.h"
#include "mouse.h"
#include "musmast.h"
#include "net_rollback.h"
#include "network.h"
#include "player.h"
#include "player_ship.h"
#include "rollback.h"
#include "sfx.h"
#include "shots.h"
#include "sndmast.h"
#include "sprite.h"
#include "sprite_viewer.h"
#include "touch_ui.h"
#include "video.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

int debug_panel_row_at(int mx, int my, int px0, int px1, int items_top,
                       int row_h, int visibleRows, int scrollTop, int count)
{
	if (mx < px0 || mx > px1 || my < items_top)
		return -1;
	const int vis = (my - items_top) / row_h;
	if (vis < 0 || vis >= visibleRows)
		return -1;
	const int idx = scrollTop + vis;
	return (idx >= 0 && idx < count) ? idx : -1;
}

void debug_panel_frame(int px0, int py0, int px1, int py1, int title_h, int mid_x,
                       const char *title)
{
	fill_rectangle_xy(VGAScreen, px0, py0, px1, py1, PANEL_BG);
	fill_rectangle_xy(VGAScreen, px0, py0, px1, py0, PANEL_EDGE_HI);
	fill_rectangle_xy(VGAScreen, px0, py0, px0, py1, PANEL_EDGE_HI);
	fill_rectangle_xy(VGAScreen, px0, py1, px1, py1, PANEL_EDGE_LO);
	fill_rectangle_xy(VGAScreen, px1, py0, px1, py1, PANEL_EDGE_LO);
	fill_rectangle_xy(VGAScreen, px0 + 1, py0 + 1, px1 - 1, py0 + title_h - 1, PANEL_TITLE_BG);
	fill_rectangle_xy(VGAScreen, px0 + 1, py0 + title_h, px1 - 1, py0 + title_h, PANEL_DIVIDER);
	draw_font_hv_shadow(VGAScreen, mid_x, py0 + 3, title, normal_font, centered, 15, 4, true, 1);
}

void debug_panel_scrollbar(int px1, int items_top, int row_h, int visibleRows, int scrollTop,
                           int count)
{
	if (count <= visibleRows)
		return;

	const int track_top = items_top - 1;
	const int track_bot = items_top + visibleRows * row_h - 2;
	const int track_h = track_bot - track_top;
	fill_rectangle_xy(VGAScreen, px1 - 4, track_top, px1 - 3, track_bot, PANEL_EDGE_LO);

	int thumb_h = track_h * visibleRows / count;
	if (thumb_h < 4)
		thumb_h = 4;
	const int denom = count - visibleRows;
	const int thumb_y = track_top + (denom > 0 ? (track_h - thumb_h) * scrollTop / denom : 0);
	fill_rectangle_xy(VGAScreen, px1 - 4, thumb_y, px1 - 3, thumb_y + thumb_h, PANEL_EDGE_HI);
}

#if defined(__SWITCH__) || defined(__vita__)
void debug_panel_shoulder_paging(bool was[2])
{
#if defined(__SWITCH__)
	static const int shoulder_btn[2] = { 6, 7 };  // switch-sdl2: 6 = L, 7 = R
#else
	static const int shoulder_btn[2] = { 4, 5 };  // Vita: 4 = L, 5 = R
#endif
	for (int b = 0; b < 2; ++b)
	{
		const bool down = joysticks > 0 && joystick[0].handle != NULL &&
		                  SDL_JoystickGetButton(joystick[0].handle, shoulder_btn[b]) != 0;
		if (down && !was[b] && !newkey)
		{
			newkey = true;
			lastkey_scan = (b == 0) ? SDL_SCANCODE_PAGEUP : SDL_SCANCODE_PAGEDOWN;
		}
		was[b] = down;
	}
}
#endif

/* Expert-mode tuning submenu of the debug menu; rows come from the shared
 * expertSettings table plus a synthetic final "Reset Defaults" row. */
static void JE_expertSettingsMenu(int off_x, int off_y)
{
	const size_t resetRow = (size_t)expertSettingsCount;  // synthetic last row
	const size_t rowCount = resetRow + 1;
	size_t selected = 0;
	int prev_mx = mouse_x, prev_my = mouse_y;  // for motion-based hover

	wait_noinput(false, false, true);
	newkey = newmouse = false;  // don't let the click/key that opened us leak in

	const int px0 = DEBUG_MENU_X + off_x, py0 = DEBUG_MENU_Y + off_y;
	const int px1 = DEBUG_MENU_X + DEBUG_MENU_WIDTH - 1 + off_x, py1 = vga_height - 5 + off_y;
	const int title_h = 15;
	const int items_top = py0 + title_h + 3;
	const int items_bottom = py1 - 9;
	// >= 1: divisor in debug_panel_row_at
	const int row_h = MAX(1, (items_bottom - items_top) / (int)rowCount);
	const int mid_x = (px0 + px1) / 2;

	bool done = false;
	while (!done)
	{
		debug_panel_frame(px0, py0, px1, py1, title_h, mid_x, "EXPERT  SETTINGS");

		for (size_t i = 0; i < rowCount; ++i)
		{
			int ry = items_top + (int)i * row_h;
			bool sel = (i == selected);

			const char* label;
			char buf[24];
			if (i == resetRow)
			{
				label = "Reset Defaults";
				buf[0] = '\0';
			}
			else
			{
				const ExpertSetting* s = &expertSettings[i];
				label = s->label;
				if (s->fmt == 'x')
					snprintf(buf, sizeof(buf), "x%d", *s->value);
				else
					snprintf(buf, sizeof(buf), "%d%%", *s->value);
			}

			if (sel)
				fill_rectangle_xy(VGAScreen, px0 + 3, ry - 1, px1 - 3, ry + row_h - 2,
				                  PANEL_SEL_BAR);

			draw_font_hv_shadow(VGAScreen, px0 + 12, ry, label, small_font, left_aligned,
			                    15, sel ? 5 : -1, true, 1);
			draw_font_hv_shadow(VGAScreen, px1 - 9, ry, buf, small_font, right_aligned,
			                    15, sel ? 5 : 0, true, 1);

			if (sel)
				draw_font_hv(VGAScreen, px0 + 5, ry, ">", small_font, left_aligned, 15, 6);
		}

		draw_font_hv(VGAScreen, mid_x, py1 - 8, "Left/Right: change    Esc: back",
		             small_font, centered, 15, -3);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		NETWORK_KEEP_ALIVE();  // nothing else pumps packets while this panel is up

		/* wheel moves the selection; hover highlights on pointer motion; left-click
		 * advances a value (Enter on the Reset row), right-click decreases it */
		{
			if (mouse_scroll != 0)
			{
				int ns = (int)selected - mouse_scroll;
				if (ns < 0)
					ns = 0;
				if (ns > (int)rowCount - 1)
					ns = (int)rowCount - 1;
				selected = (size_t)ns;
				mouse_scroll = 0;
			}
			if (mouse_x != prev_mx || mouse_y != prev_my)
			{
				const int hov = debug_panel_row_at(mouse_x, mouse_y, px0, px1, items_top,
				                             row_h, (int)rowCount, 0, (int)rowCount);
				if (hov >= 0)
					selected = (size_t)hov;
			}
			prev_mx = mouse_x;
			prev_my = mouse_y;
			if (newmouse)
			{
				const int r = debug_panel_row_at(lastmouse_x, lastmouse_y, px0, px1, items_top,
				                           row_h, (int)rowCount, 0, (int)rowCount);
				if (r >= 0)
				{
					selected = (size_t)r;
					newkey = true;
					lastkey_scan = (lastmouse_but == SDL_BUTTON_RIGHT) ? SDL_SCANCODE_LEFT
					             : ((size_t)r == resetRow ? SDL_SCANCODE_RETURN : SDL_SCANCODE_RIGHT);
				}
				newmouse = false;
			}
		}

		if (newkey)
		{
			ExpertSetting* s = (selected == resetRow) ? NULL : &expertSettings[selected];
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				selected = (selected == 0) ? rowCount - 1 : selected - 1;
				break;
			case SDL_SCANCODE_DOWN:
				selected = (selected + 1) % rowCount;
				break;
			case SDL_SCANCODE_LEFT:
				if (s != NULL && *s->value - s->step >= s->lo)
					*s->value -= s->step;
				break;
			case SDL_SCANCODE_RIGHT:
				if (s != NULL && *s->value + s->step <= s->hi)
					*s->value += s->step;
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				if (s == NULL)  // Reset Defaults
					for (int j = 0; j < expertSettingsCount; ++j)
						*expertSettings[j].value = expertSettings[j].def;
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

	wait_noinput(false, false, true);
}

/* God Mode: a single 4-state debug-menu option backed by the two independent
 * infinite-shield / infinite-armor cheat flags the engine already honours. */
enum { GOD_OFF = 0, GOD_ON, GOD_ARMOR_ONLY, GOD_SHIELD_ONLY };
static const char *const debug_god_mode_names[] = { "OFF", "ON", "Armor Only", "Shield Only" };

static int debug_god_mode_get(void)
{
	if (cheatInfiniteShields)
		return cheatInfiniteArmor ? GOD_ON : GOD_SHIELD_ONLY;
	return cheatInfiniteArmor ? GOD_ARMOR_ONLY : GOD_OFF;
}
static void debug_god_mode_set(int g)
{
	cheatInfiniteShields = (g == GOD_ON || g == GOD_SHIELD_ONLY);
	cheatInfiniteArmor   = (g == GOD_ON || g == GOD_ARMOR_ONLY);
}

// The special-weapon id a twiddle (keyboardCombos row) triggers: its entry in
// (100, 100+SPECIAL_NUM], returned as 1..SPECIAL_NUM, or 0 if none is valid.
static int twiddle_special_id(int row)
{
	for (int k = 0; k < 8; ++k)
	{
		const int v = keyboardCombos[row][k];
		if (v > 100 && v <= 100 + SPECIAL_NUM)
			return v - 100;
	}
	return 0;
}

// Accept only specials with valid HUD art, names, and effect types.
bool debug_special_is_safe(int id)
{
	if (id == 0)
		return true;  // None; clears the equipped special, no icon drawn
	if (id < 1 || id > SPECIAL_NUM)
		return false;

	// Sprite count of spriteSheet10: entry[0] of the Uint16 offset table is the byte offset to
	// sprite 1, which makes entry[0] / 2 the sprite count.
	unsigned iconMax = 0;
	if (spriteSheet10.data != NULL && spriteSheet10.size >= sizeof(Uint16))
		iconMax = SDL_SwapLE16(((Uint16 *)spriteSheet10.data)[0]) / (unsigned)sizeof(Uint16);

	return special[id].name[0] != '\0'
	    && special[id].stype >= 1 && special[id].stype <= 18
	    && special[id].itemgraphic >= 1 && special[id].itemgraphic <= iconMax;
}

// Debug-only: fault on purpose so the crash logger runs end-to-end (Force Crash row). Pointer must
// be a volatile file-scope global or /O2 folds the null store away.
static int *volatile debug_crash_ptr;  // NULL; never assigned -> the dereference faults
static void debug_force_crash(void)
{
	*debug_crash_ptr = 0xDEAD;
}

/* Debug row identity is independent of display order. New rows need an enum,
 * label, help entry, and a place in dbgRows. */
enum {
	DBG_PLAYER,
	DBG_SHIP, DBG_FRONT_WEAPON, DBG_FRONT_POWER, DBG_REAR_WEAPON, DBG_REAR_POWER,
	DBG_SHIELD, DBG_GENERATOR, DBG_SIDEKICK_L, DBG_SIDEKICK_R, DBG_SPECIAL,
	DBG_TWIDDLE, DBG_AUTOFIRE_TWIDDLE, DBG_TOGGLE_FIRE,
	DBG_AUTOFIRE_SPECIAL, DBG_AUTOFIRE_CHARGE, DBG_INSTANT_CHARGE, DBG_INF_SIDEKICK_AMMO, DBG_INF_GENERATOR,
	DBG_GOD_MODE, DBG_NOCLIP, DBG_EXPERT_MODE, DBG_EXPERT_SETTINGS, DBG_AUTO_DIFFICULTY,
	DBG_DIFFICULTY, DBG_ADD_CASH, DBG_NO_ENEMY_FIRE, DBG_SKIP_LEVEL,
	DBG_PLAY_SOUND, DBG_PLAY_MUSIC, DBG_SPRITE_VIEWER, DBG_HITBOX, DBG_PERF,
	DBG_ENDLESS_FX, DBG_ENDLESS_TUNE,
	DBG_HANG_TIMEOUT, DBG_ROLLBACK_SELFTEST, DBG_FORCE_CRASH,
	DBG_ROW_COUNT
};

static const char *const dbgLabel[DBG_ROW_COUNT] = {
	[DBG_PLAYER]              = "Edit Player",
	[DBG_SHIP]                = "Ship",
	[DBG_FRONT_WEAPON]        = "Front Weapon",
	[DBG_FRONT_POWER]         = "Front Power",
	[DBG_REAR_WEAPON]         = "Rear Weapon",
	[DBG_REAR_POWER]          = "Rear Power",
	[DBG_SHIELD]              = "Shield",
	[DBG_GENERATOR]           = "Generator",
	[DBG_SIDEKICK_L]          = "Sidekick L",
	[DBG_SIDEKICK_R]          = "Sidekick R",
	[DBG_SPECIAL]             = "Special",
	[DBG_TWIDDLE]             = "Twiddle",
	[DBG_AUTOFIRE_TWIDDLE]    = "Autofire Twiddle",
	[DBG_TOGGLE_FIRE]         = "Toggle Fire",
	[DBG_AUTOFIRE_SPECIAL]    = "Autofire Special",
	[DBG_AUTOFIRE_CHARGE]     = "Autofire Charge Sidekicks",
	[DBG_INSTANT_CHARGE]      = "Instant Charge Sidekicks",
	[DBG_INF_SIDEKICK_AMMO]   = "Inf Sidekick Ammo",
	[DBG_INF_GENERATOR]       = "Inf Generator",
	[DBG_GOD_MODE]            = "God Mode",
	[DBG_NOCLIP]              = "Noclip",
	[DBG_EXPERT_MODE]         = "Expert Mode",
	[DBG_EXPERT_SETTINGS]     = "Expert Settings",
	[DBG_AUTO_DIFFICULTY]     = "Auto-Adjust Difficulty",
	[DBG_DIFFICULTY]          = "Difficulty",
	[DBG_ADD_CASH]            = "Add Cash",
	[DBG_NO_ENEMY_FIRE]       = "No Enemy Fire",
	[DBG_SKIP_LEVEL]          = "Skip to Next Level",
	[DBG_PLAY_SOUND]          = "Play Sound",
	[DBG_PLAY_MUSIC]          = "Play Music",
	[DBG_SPRITE_VIEWER]       = "Sprite Viewer",
	[DBG_HITBOX]              = "Hitbox Overlay",
	[DBG_PERF]                = "Perf Overlay",
	[DBG_HANG_TIMEOUT]        = "Hang Watchdog",
	[DBG_ROLLBACK_SELFTEST]   = "Rollback Self-Test",
	[DBG_ENDLESS_FX]          = "Endless Effects",
	[DBG_ENDLESS_TUNE]        = "Endless Mods and Scaling",
	[DBG_FORCE_CRASH]         = "Force Crash (test)",
};

/* One line per row, shown under the list while that row is selected: what it DOES, not what it is
 * called. Keep each under ~40 characters; that is the panel's inner width in small_font. */
static const char *const dbgHelp[DBG_ROW_COUNT] = {
	[DBG_PLAYER]              = "Whose gear the rows below edit",
	[DBG_SHIP]                = "Swap the hull; red = no such ship",
	[DBG_FRONT_WEAPON]        = "Front gun; red = no such weapon",
	[DBG_FRONT_POWER]         = "Front gun power level, 1 to 11",
	[DBG_REAR_WEAPON]         = "Rear gun; red = no such weapon",
	[DBG_REAR_POWER]          = "Rear gun power level, 1 to 11",
	[DBG_SHIELD]              = "Shield type, which sets max shield",
	[DBG_GENERATOR]           = "Generator type, which sets power",
	[DBG_SIDEKICK_L]          = "Left sidekick pod",
	[DBG_SIDEKICK_R]          = "Right sidekick pod",
	[DBG_SPECIAL]             = "Equipped special (safe ids only)",
	[DBG_TWIDDLE]             = "Pick a twiddle special; Enter fires it",
	[DBG_AUTOFIRE_TWIDDLE]    = "Refire that twiddle while fire is held",
	[DBG_TOGGLE_FIRE]         = "Fire button toggles auto-fire",
	[DBG_AUTOFIRE_SPECIAL]    = "Fire your special as you shoot",
	[DBG_AUTOFIRE_CHARGE]     = "When charge pods release on their own",
	[DBG_INSTANT_CHARGE]      = "Charge pods sit at full charge",
	[DBG_INF_SIDEKICK_AMMO]   = "Sidekick ammo never runs out",
	[DBG_INF_GENERATOR]       = "Generator power never drains",
	[DBG_GOD_MODE]            = "Invulnerable: armor, shield, or both",
	[DBG_NOCLIP]              = "Fly through everything (+ ghost look)",
	[DBG_EXPERT_MODE]         = "Harder rules: tough enemies, dear shop",
	[DBG_EXPERT_SETTINGS]     = "Opens the expert multipliers",
	[DBG_AUTO_DIFFICULTY]     = "Score bumps difficulty between levels",
	[DBG_DIFFICULTY]          = "The difficulty being played right now",
	[DBG_ADD_CASH]            = "Type an amount, or L/R for max cash",
	[DBG_NO_ENEMY_FIRE]       = "Enemies never shoot",
	[DBG_SKIP_LEVEL]          = "Ends the level now and moves on",
	[DBG_PLAY_SOUND]          = "L/R picks a sample; Enter plays it",
	[DBG_PLAY_MUSIC]          = "L/R picks a track; Enter plays it",
	[DBG_SPRITE_VIEWER]       = "Opens the sprite sheet browser",
	[DBG_HITBOX]              = "Draw hit boxes on enemies and ship",
	[DBG_PERF]                = "FPS, enemy and shot counts on screen",
	[DBG_HANG_TIMEOUT]        = "Seconds of freeze before the log fires",
	[DBG_ROLLBACK_SELFTEST]   = "Replay every tick to verify rollback",
	[DBG_ENDLESS_FX]          = "Endless mods/perks in a normal game",
	[DBG_ENDLESS_TUNE]        = "Opens the mod, perk and scaling panel",
	[DBG_FORCE_CRASH]         = "Faults on purpose to test the crash log",
};

/* The display order: the rows above under non-selectable headings (id < 0). Grouping is the whole
 * point; a flat 34-row list of hull ids, cheats and diagnostics reads as noise. */
static const struct { int id; const char *heading; } dbgRows[] = {
	{ -1, "SURVIVAL" },   // first: the rows most often reached for mid-level
	{ DBG_GOD_MODE, NULL }, { DBG_NOCLIP, NULL }, { DBG_NO_ENEMY_FIRE, NULL },
	{ DBG_ADD_CASH, NULL },
	{ -1, "LOADOUT" },
	{ DBG_PLAYER, NULL },
	{ DBG_SHIP, NULL }, { DBG_FRONT_WEAPON, NULL }, { DBG_FRONT_POWER, NULL },
	{ DBG_REAR_WEAPON, NULL }, { DBG_REAR_POWER, NULL }, { DBG_SHIELD, NULL },
	{ DBG_GENERATOR, NULL }, { DBG_SIDEKICK_L, NULL }, { DBG_SIDEKICK_R, NULL },
	{ DBG_SPECIAL, NULL },
	{ -1, "FIRING" },
	{ DBG_TOGGLE_FIRE, NULL }, { DBG_TWIDDLE, NULL }, { DBG_AUTOFIRE_TWIDDLE, NULL },
	{ DBG_AUTOFIRE_SPECIAL, NULL }, { DBG_AUTOFIRE_CHARGE, NULL }, { DBG_INSTANT_CHARGE, NULL },
	{ DBG_INF_SIDEKICK_AMMO, NULL }, { DBG_INF_GENERATOR, NULL },
	{ -1, "DIFFICULTY" },
	{ DBG_EXPERT_MODE, NULL }, { DBG_EXPERT_SETTINGS, NULL }, { DBG_AUTO_DIFFICULTY, NULL },
	{ DBG_DIFFICULTY, NULL },
	{ -1, "ENDLESS EFFECTS" },
	{ DBG_ENDLESS_FX, NULL }, { DBG_ENDLESS_TUNE, NULL },
	{ -1, "LEVEL" },
	{ DBG_SKIP_LEVEL, NULL },
	{ -1, "DIAGNOSTICS" },
	{ DBG_PLAY_SOUND, NULL }, { DBG_PLAY_MUSIC, NULL }, { DBG_SPRITE_VIEWER, NULL },
	{ DBG_HITBOX, NULL }, { DBG_PERF, NULL }, { DBG_HANG_TIMEOUT, NULL },
	{ DBG_ROLLBACK_SELFTEST, NULL }, { DBG_FORCE_CRASH, NULL },
};
#define DBG_DISPLAY_ROWS  ((int)COUNTOF(dbgRows))
#define DBG_HEADING_COUNT 7
// Catches the slip that would otherwise go unnoticed: a row added to the enum but never placed in
// dbgRows, leaving it unreachable in the menu. Bump DBG_HEADING_COUNT when adding a heading.
COMPILE_TIME_ASSERT(dbg_rows_cover_every_row, DBG_DISPLAY_ROWS == DBG_ROW_COUNT + DBG_HEADING_COUNT);

/* dbgVis maps visible rows to dbgRows for the current mode. Rebuilt whenever the menu opens. */
static int dbgVis[DBG_DISPLAY_ROWS];
static int dbgVisCount;

static bool dbgRowApplies(int id)
{
	switch (id)
	{
	case DBG_PLAYER:
		return twoPlayerMode;
	case DBG_ENDLESS_FX:
	case DBG_ENDLESS_TUNE:
	case DBG_ROLLBACK_SELFTEST:  // netplay drives the rollback engine for real; the self-test never arms there
		return !isNetworkGame;
	default:
		return true;
	}
}

static void dbgBuildVisibleRows(void)
{
	dbgVisCount = 0;
	for (int i = 0; i < DBG_DISPLAY_ROWS; ++i)
		if (dbgRows[i].id < 0 || dbgRowApplies(dbgRows[i].id))
			dbgVis[dbgVisCount++] = i;

	// Drop a heading whose whole group went with it, or it would sit over the next group's rows.
	int keep = 0;
	for (int i = 0; i < dbgVisCount; ++i)
	{
		const bool heading = dbgRows[dbgVis[i]].id < 0;
		if (heading && (i + 1 == dbgVisCount || dbgRows[dbgVis[i + 1]].id < 0))
			continue;
		dbgVis[keep++] = dbgVis[i];
	}
	dbgVisCount = keep;
}

static bool dbgRowIsHeading(int r)
{
	return r < 0 || r >= dbgVisCount || dbgRows[dbgVis[r]].id < 0;
}

static int dbgRowId(int r)
{
	return dbgRows[dbgVis[r]].id;
}

/* Step one selectable row in `dir`, wrapping past the headings. */
static int dbgRowStep(int r, int dir)
{
	for (int n = 0; n < dbgVisCount; ++n)
	{
		r += dir;
		if (r < 0)
			r = dbgVisCount - 1;
		else if (r >= dbgVisCount)
			r = 0;
		if (!dbgRowIsHeading(r))
			return r;
	}
	return r;
}

/* The nearest selectable row at or after `r` (then searching back); for any jump that can land
 * on a heading: clamping, paging, Home/End, a click. */
static int dbgRowSnap(int r)
{
	if (r < 0)
		r = 0;
	if (r > dbgVisCount - 1)
		r = dbgVisCount - 1;
	for (int i = r; i < dbgVisCount; ++i)
		if (!dbgRowIsHeading(i))
			return i;
	for (int i = r; i >= 0; --i)
		if (!dbgRowIsHeading(i))
			return i;
	return r;
}

/* Toggle campaign modifier effects outside a real Endless run. Arm through the shared path to clear
 * stale outpost purchases, then persist immediately for crash-safe debug sessions. */
static void debug_toggle_campaign_mods(void)
{
	if (endlessMode)
		return;
	if (!endlessCampaignMods)
		endlessCampaignModsArm();
	endlessCampaignMods = !endlessCampaignMods;
	save_opentyrian_config();
}

/* Flip the rollback self-test. Goes through rollback_selftest_set() rather than the flag, so
 * switching it on mid-level also arms the registry and snapshot ring. */
static void debug_toggle_rollback_selftest(void)
{
	rollback_selftest_set(!rollback_selftest);
	save_opentyrian_config();
}

/* Does this row write into the edited player's items? Those all need the refresh below; nothing
 * else does. (The Edit Player row itself changes no items, only which player they belong to.) */
static bool dbgRowIsLoadout(int id)
{
	switch (id)
	{
	case DBG_SHIP:   case DBG_FRONT_WEAPON: case DBG_FRONT_POWER:
	case DBG_REAR_WEAPON: case DBG_REAR_POWER: case DBG_SHIELD:
	case DBG_GENERATOR: case DBG_SIDEKICK_L: case DBG_SIDEKICK_R:
	case DBG_SPECIAL:
		return true;
	default:
		return false;
	}
}

/* Rebuild cached player state after a mid-level debug loadout edit. */
/* True only when the debug menu overlays the gameplay HUD. */
static bool debugMenuOverHud = false;

/* Debug ship changes take full armor. Custom-ship changes preserve live gauge ratios. */
// Refresh the sidebar in menu backdrops captured before a loadout edit.
static void hud_strip_snapshot(void)
{
	if (VGAScreenSeg == NULL || VGAScreen2 == NULL || rollback_resim_silent)
		return;

	const int x0 = PLAYFIELD_WIDTH;
	const int w = vga_width - x0;
	if (w <= 0)
		return;

	for (int y = 0; y < vga_height; ++y)
	{
		memcpy((Uint8 *)VGAScreen2->pixels + (size_t)y * VGAScreen2->pitch + x0,
		       (Uint8 *)VGAScreenSeg->pixels + (size_t)y * VGAScreenSeg->pitch + x0, (size_t)w);
	}
}

static void debug_apply_loadout_change(int pnum, bool shipChanged, bool carryGauges)
{
	uint keptArmor[COUNTOF(player)];
	uint keptArmorMax[COUNTOF(player)];
	uint keptShield[COUNTOF(player)];
	uint keptShieldMax[COUNTOF(player)];
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		keptArmor[i] = player[i].armor;
		keptArmorMax[i] = player[i].initial_armor;
		keptShield[i] = player[i].shield;
		keptShieldMax[i] = player[i].shield_max;
	}

	// A weapon that no longer exists can leave the multi-shot phase pointing at a pattern the new
	// one doesn't have, and weapon_mode can outrun the new port's configuration count.
	memset(shotMultiPos, 0, sizeof(shotMultiPos));
	for (uint i = 0; i < COUNTOF(player); ++i)
		if (player[i].weapon_mode > JE_portConfigs(&player[i]))
			player[i].weapon_mode = 1;

	JE_getShipInfo();   // shipGr + shipGrPtr, hull, initial_armor, hit box, powerAdd

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		// JE_getShipInfo rewrites BOTH players' armor from their hulls; only the player whose
		// hull actually changed should take that, so everyone else keeps what they had.
		if ((int)i == pnum && carryGauges)
			player[i].armor = player_carry_gauge(keptArmor[i], keptArmorMax[i],
			                                      player[i].initial_armor);
		else if ((int)i != pnum || !shipChanged)
			player[i].armor = keptArmor[i];
		if (player[i].armor > player[i].initial_armor)
			player[i].armor = player[i].initial_armor;

		// The shield ceiling is set only at level start (game_loop.c), so without this a shield
		// swap keeps the old one's maximum and the gauge scale goes with it.
		player[i].shield_max = arcade_shield_max(&player[i]);
		if ((int)i == pnum && carryGauges)
			player[i].shield = player_carry_gauge(keptShield[i], keptShieldMax[i],
			                                      player[i].shield_max);
		else
			player[i].shield = keptShield[i];
		if (player[i].shield > player[i].shield_max)
			player[i].shield = player[i].shield_max;
	}

	// Repaint the event-driven gauges on a gameplay HUD after changing equipment.
	// JE_drawOptions also paints, so calling it over shop art would leave HUD elements behind.
	if (debugMenuOverHud)
	{
		JE_wipeShieldArmorBars();
		JE_drawArmor();
		JE_drawShield();
		JE_drawOptions();   // re-seeds the sidekick pods' ammo, refill cadence and style from options[]
		JE_drawPortConfigButtons();  // the mode arrows belong to the rear gun that just changed

		// Refresh the captured menu background immediately.
		hud_strip_snapshot();

		// Repaint again after paths that replace the whole screen on exit.
		hud_sidekicks_dirty = true;
		hud_bars_dirty = true;
	}
}

void extraShipLoadoutRefresh(uint pnum, bool overHud)
{
	const bool wasOverHud = debugMenuOverHud;
	SDL_Surface *const savedScreen = VGAScreen;
	debugMenuOverHud = overHud;
	if (overHud)
		VGAScreen = VGAScreenSeg;
	debug_apply_loadout_change((int)pnum, false, true);
	VGAScreen = savedScreen;
	debugMenuOverHud = wasOverHud;
}

/* Rebuild item-derived caches after adopting a peer's debug edit. */
void debugLoadoutRefresh(bool overHud)
{
	const bool wasOverHud = debugMenuOverHud;
	debugMenuOverHud = overHud;
	debug_apply_loadout_change(-1, false, false);
	debugMenuOverHud = wasOverHud;
}

void JE_debugMenu(bool center)
{
	SDL_Surface* temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	// See debugMenuOverHud: uncentered == opened from inside a level, which is the only place the
	// gameplay HUD exists to be repainted. Saved and restored so a nested open can't clear it.
	const bool wasOverHud = debugMenuOverHud;
	debugMenuOverHud = !center;

	// gameplay runs the mouse in relative mode; switch to absolute so the menu
	// can use the pointer, and restore on exit.
	const bool wasRelative = mouseGetRelative();
	mouseSetRelative(false);

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // mouse pointer sprites

	int off_x = 0, off_y = 0;
	if (center)
	{
		int menu_width = MIN(vga_width, LEGACY_WIDTH);
		off_x = (menu_width - DEBUG_MENU_WIDTH) / 2 - DEBUG_MENU_X;
		off_y = (vga_height - DEBUG_MENU_HEIGHT) / 2 - DEBUG_MENU_Y + 1;
	}

	dbgBuildVisibleRows();
	const int menuCount = dbgVisCount;
	int selected = dbgRowSnap(0);   // first real row, never the heading above it

	// Snapshot before re-arming twiddles so every resulting change is published.
	if (isNetworkGame)
		network_debug_sync_mark();

	/* transient debug-action values; persist across menu opens within a session */
	static int dbgSoundId = 1, dbgMusicId = 0, dbgTwiddleId = 0;
	debugTwiddleSpecial = (JE_byte)twiddle_special_id(dbgTwiddleId);  // keep the armed twiddle in sync

	/* Loadout target, reset to the local player each time the menu opens. */
	int dbgPlayer = 0;
	if (twoPlayerMode && isNetworkGame && thisPlayerNum >= 1 && thisPlayerNum <= COUNTOF(player))
		dbgPlayer = (int)thisPlayerNum - 1;

	/* Add Cash is an inline numeric field with a fresh buffer on each open. */
	const int cashMaxDigits = 12;              // CASH_MAX is 12 digits; the field can type the whole ceiling
	const Sint64 cashMax = CASH_MAX;
	char dbgCashStr[16] = "";
	char dbgHangStr[8] = "";                   // inline typed field for the Hang Watchdog row (seconds)

	wait_noinput(false, false, true);
	newkey = newmouse = false;  // don't let the click/key that opened us leak in

	/* Panel geometry. Spans the DEBUG_MENU_X..+WIDTH area (centred in the
	 * playfield) that the in-game caller saves and restores around this menu. */
	const int px0 = DEBUG_MENU_X + off_x, py0 = DEBUG_MENU_Y + off_y;
	const int px1 = DEBUG_MENU_X + DEBUG_MENU_WIDTH - 1 + off_x, py1 = vga_height - 5 + off_y;
	const int title_h = 15;                 /* height of the title strip      */
	const int items_top = py0 + title_h + 3;
	const int items_bottom = py1 - 18;      /* two footer lines: the row's help, then the keys */
	/* Fixed row density (matching the original ~19-row menu); the list scrolls
	 * when there are more items than fit, so rows never get squashed. */
	const int kVisibleTarget = 19;
	const int visibleRows = (menuCount < kVisibleTarget) ? menuCount : kVisibleTarget;
	const int row_h = (items_bottom - items_top) / visibleRows;
	const int mid_x = (px0 + px1) / 2;
	int scrollTop = 0;  /* index of the first visible row */
	int prev_mx = mouse_x, prev_my = mouse_y;  /* for motion-based hover */

	bool done = false;
	while (!done)
	{
		/* Solid panel, fully repainted every frame: nothing from the game
		 * behind can bleed through and no shading/text can accumulate. */
		debug_panel_frame(px0, py0, px1, py1, title_h, mid_x, "DEBUG  MENU");

		/* keep the selection within the scrolled window */
		if (selected < scrollTop)
			scrollTop = selected;
		else if (selected >= scrollTop + visibleRows)
			scrollTop = selected - visibleRows + 1;
		/* keep a heading on screen with the first row under it, so a selection never floats
		 * context-free at the top of the window */
		if (scrollTop > 0 && scrollTop == selected && dbgRowIsHeading(selected - 1))
			--scrollTop;
		if (scrollTop > menuCount - visibleRows)
			scrollTop = menuCount - visibleRows;
		if (scrollTop < 0)
			scrollTop = 0;

		for (int vis = 0; vis < visibleRows; ++vis)
		{
			int i = scrollTop + vis;
			int ry = items_top + vis * row_h;
			bool sel = (i == selected);

			if (dbgRowIsHeading(i))
			{
				const char *const heading = dbgRows[dbgVis[i]].heading;
				draw_font_hv_shadow(VGAScreen, px0 + 6, ry, heading, small_font, left_aligned, 15, 3, true, 1);
				const int rule_x = px0 + 10 + JE_textWidth(heading, small_font);
				if (rule_x < px1 - 9)
					fill_rectangle_xy(VGAScreen, rule_x, ry + 3, px1 - 9, ry + 3, PANEL_DIVIDER);
				continue;
			}

			const int id = dbgRowId(i);
			const PlayerItems *const it = &player[dbgPlayer].items;
			char buf[40];
			bool invalid = false;
			switch (id)
			{
			case DBG_PLAYER:
				// Across the network, name which ship is yours; the two machines number the
				// players the same way, so "2" alone doesn't say whose gear you're about to rewrite.
				if (isNetworkGame && (int)thisPlayerNum == dbgPlayer + 1)
					snprintf(buf, sizeof(buf), "%d (you)", dbgPlayer + 1);
				else
					snprintf(buf, sizeof(buf), "%d", dbgPlayer + 1);
				break;
			case DBG_SHIP:
				if (it->ship <= SHIP_DRAGONWING)
					snprintf(buf, sizeof(buf), "%s", ships[it->ship].name);
				else if (extraShipIdIsCustom(it->ship) && extraAvailFor((uint)dbgPlayer))
					snprintf(buf, sizeof(buf), "Extra Ship %d", it->ship - 90);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->ship);
					invalid = true;
				}
				break;
			case DBG_FRONT_WEAPON:
				if (it->weapon[FRONT_WEAPON].id <= PORT_NUM)
					snprintf(buf, sizeof(buf), "%s", weaponPort[it->weapon[FRONT_WEAPON].id].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->weapon[FRONT_WEAPON].id);
					invalid = true;
				}
				break;
			case DBG_FRONT_POWER:
				snprintf(buf, sizeof(buf), "%d", it->weapon[FRONT_WEAPON].power);
				break;
			case DBG_REAR_WEAPON:
				if (it->weapon[REAR_WEAPON].id <= PORT_NUM)
					snprintf(buf, sizeof(buf), "%s", weaponPort[it->weapon[REAR_WEAPON].id].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->weapon[REAR_WEAPON].id);
					invalid = true;
				}
				break;
			case DBG_REAR_POWER:
				snprintf(buf, sizeof(buf), "%d", it->weapon[REAR_WEAPON].power);
				break;
			case DBG_SHIELD:
				if (it->shield <= SHIELD_NUM)
					snprintf(buf, sizeof(buf), "%s", shields[it->shield].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->shield);
					invalid = true;
				}
				break;
			case DBG_GENERATOR:
				if (it->generator <= POWER_NUM)
					snprintf(buf, sizeof(buf), "%s", powerSys[it->generator].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->generator);
					invalid = true;
				}
				break;
			case DBG_SIDEKICK_L:
				if (it->sidekick[LEFT_SIDEKICK] <= OPTION_NUM)
					snprintf(buf, sizeof(buf), "%s", options[it->sidekick[LEFT_SIDEKICK]].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->sidekick[LEFT_SIDEKICK]);
					invalid = true;
				}
				break;
			case DBG_SIDEKICK_R:
				if (it->sidekick[RIGHT_SIDEKICK] <= OPTION_NUM)
					snprintf(buf, sizeof(buf), "%s", options[it->sidekick[RIGHT_SIDEKICK]].name);
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->sidekick[RIGHT_SIDEKICK]);
					invalid = true;
				}
				break;
			case DBG_SPECIAL:
				if (it->special <= SPECIAL_NUM)
					snprintf(buf, sizeof(buf), "%s", JE_specialName(it->special));
				else
				{
					snprintf(buf, sizeof(buf), "%d", it->special);
					invalid = true;
				}
				break;
			case DBG_TWIDDLE:
			{
				const int sid = twiddle_special_id(dbgTwiddleId);
				if (sid >= 1 && sid <= SPECIAL_NUM)
					snprintf(buf, sizeof(buf), "%s", JE_specialName((JE_byte)sid));
				else
				{
					snprintf(buf, sizeof(buf), "#%d", dbgTwiddleId + 1);
					invalid = true;
				}
				break;
			}
			case DBG_AUTOFIRE_TWIDDLE:
				sprintf(buf, "%s", debugAutofireTwiddle ? "ON" : "OFF");
				break;
			case DBG_TOGGLE_FIRE:
				sprintf(buf, "%s", debugToggleFire ? "ON" : "OFF");
				break;
			case DBG_AUTOFIRE_SPECIAL:
				sprintf(buf, "%s", autoFireSpecial ? "ON" : "OFF");
				break;
			case DBG_AUTOFIRE_CHARGE:
			{
				static const char *const modes[CHARGE_AUTOFIRE_NUM] = { "No", "Yes", "Fully charged only", "Yes (fastest)" };
				snprintf(buf, sizeof(buf), "%s", modes[chargeSidekickAutofire % CHARGE_AUTOFIRE_NUM]);
				break;
			}
			case DBG_INSTANT_CHARGE:
				sprintf(buf, "%s", cheatInstantCharge ? "ON" : "OFF");
				break;
			case DBG_INF_SIDEKICK_AMMO:
				sprintf(buf, "%s", cheatInfiniteSidekickAmmo ? "ON" : "OFF");
				break;
			case DBG_INF_GENERATOR:
				sprintf(buf, "%s", cheatInfiniteGenerator ? "ON" : "OFF");
				break;
			case DBG_GOD_MODE:
				snprintf(buf, sizeof(buf), "%s", debug_god_mode_names[debug_god_mode_get()]);
				break;
			case DBG_NOCLIP:
			{
				static const char *const modes[NOCLIP_NUM] = { "OFF", "ON", "ON (Transparent)" };
				snprintf(buf, sizeof(buf), "%s", modes[noclipMode % NOCLIP_NUM]);
				break;
			}
			case DBG_EXPERT_MODE:
				sprintf(buf, "%s", expertMode ? "ON" : "OFF");
				break;
			case DBG_EXPERT_SETTINGS:
				sprintf(buf, "%s", ">>");  // drill-in to Expert Settings submenu
				break;
			case DBG_AUTO_DIFFICULTY:
				sprintf(buf, "%s", difficultyAdjust ? "ON" : "OFF");
				break;
			case DBG_DIFFICULTY:
				snprintf(buf, sizeof(buf), "%s", difficultyNameB[difficultyLevel]);
				break;
			case DBG_ADD_CASH:  // editable: type a value while selected, Enter sets cash to it
				if (sel)
					snprintf(buf, sizeof(buf), "%s|", dbgCashStr);  // your input + '|' caret (empty => just the caret)
				else
					snprintf(buf, sizeof(buf), "%lld", (long long)player[dbgPlayer].cash);  // live cash when not editing
				break;
			case DBG_NO_ENEMY_FIRE:
				sprintf(buf, "%s", cheatNoEnemyFire ? "ON" : "OFF");
				break;
			case DBG_SKIP_LEVEL:  // action
				sprintf(buf, "%s", "[Enter]");
				break;
			case DBG_PLAY_SOUND:  // Left/Right id, Enter plays
				snprintf(buf, sizeof(buf), "%d", dbgSoundId);
				break;
			case DBG_PLAY_MUSIC:  // Left/Right id, Enter plays
				snprintf(buf, sizeof(buf), "%d", dbgMusicId);
				break;
			case DBG_SPRITE_VIEWER:
				sprintf(buf, "%s", ">>");  // drill-in to Sprite Viewer submenu
				break;
			case DBG_HITBOX:
				sprintf(buf, "%s", debugHitboxOverlay ? "ON" : "OFF");
				break;
			case DBG_PERF:
				sprintf(buf, "%s", debugPerfOverlay ? "ON" : "OFF");
				break;
			case DBG_HANG_TIMEOUT:  // editable: type seconds while selected, Enter applies (clamped)
				if (sel)
					snprintf(buf, sizeof(buf), "%s|", dbgHangStr);  // your input + '|' caret
				else
					snprintf(buf, sizeof(buf), "%ds", crashlog_get_hang_timeout());  // live value
				break;
			case DBG_ENDLESS_FX:
				// Endless runs always enable this layer, so report its fixed state.
				if (endlessMode)
					sprintf(buf, "%s", "ENDLESS");
				else
					sprintf(buf, "%s", endlessCampaignMods ? "ON" : "OFF");
				break;
			case DBG_ENDLESS_TUNE:
			{
				// Summarise what the layer is actually carrying, so the panel's state is visible
				// without opening it: mods, perk stacks and pinned levers.
				int perks = 0;
				for (int p = 0; p < endlessPerkCount(); ++p)
					perks += endlessPerkGetOwned(p);
				const int pins = endlessScalingOverrideCount();
				const int mods = endlessPopCount64(endlessActiveMods);
				if (mods || perks || pins)
					snprintf(buf, sizeof(buf), "%dm %dp %dx", mods, perks, pins);
				else
					sprintf(buf, "%s", ">>");
				break;
			}
			case DBG_ROLLBACK_SELFTEST:
				// Show the running failure count while verification is armed.
				if (!rollback_selftest)
					sprintf(buf, "%s", "OFF");
				else if (endlessFxActive())
					sprintf(buf, "%s", "ON (idle: endless)");  // the effect layer is outside the registry
				else if (rollback_selftest_failures > 0)
					snprintf(buf, sizeof(buf), "ON  %lu FAIL", rollback_selftest_failures);
				else if (rollback_selftest_ticks > 0)
					snprintf(buf, sizeof(buf), "ON  %lu ok", rollback_selftest_ticks);
				else
					sprintf(buf, "%s", "ON");
				break;
			case DBG_FORCE_CRASH:  // Crash-logger test action.
				sprintf(buf, "%s", "[Enter]");
				break;
			default:
				buf[0] = '\0';
				break;
			}

			/* trim trailing whitespace */
			for (int j = (int)strlen(buf) - 1; j >= 0 && isspace((unsigned char)buf[j]); --j)
				buf[j] = '\0';

			/* highlight bar behind the active row */
			if (sel)
				fill_rectangle_xy(VGAScreen, px0 + 3, ry - 1, px1 - 3, ry + row_h - 2,
				                  PANEL_SEL_BAR);

			/* label (left) */
			draw_font_hv_shadow(VGAScreen, px0 + 14, ry, dbgLabel[id], small_font, left_aligned,
			                    15, sel ? 5 : -1, true, 1);

			/* value (right); dim plain "OFF", red for invalid entries */
			Sint8 val_bright = sel ? 5 : 0;
			if (!invalid && strcmp(buf, "OFF") == 0)
				val_bright -= 4;
			draw_font_hv_shadow(VGAScreen, px1 - 9, ry, buf, small_font, right_aligned,
			                    invalid ? 4 : 15, val_bright, true, 1);

			/* selection marker */
			if (sel)
				draw_font_hv(VGAScreen, px0 + 5, ry, ">", small_font, left_aligned, 15, 6);
		}

		debug_panel_scrollbar(px1, items_top, row_h, visibleRows, scrollTop, menuCount);

		/* Footer line 1: what the selected row DOES; 34 cheats and diagnostics is far too many
		 * to carry their meaning in the label alone. Line 2: the keys, named for the platform. */
		const int shownId = dbgRowId(selected);
		draw_font_hv(VGAScreen, mid_x, py1 - 17, dbgHelp[shownId], small_font, centered, 15, 1);
#if defined(__SWITCH__) || defined(__vita__)
		draw_font_hv(VGAScreen, mid_x, py1 - 8,
		             (shownId == DBG_ADD_CASH || shownId == DBG_HANG_TIMEOUT)
		                 ? "A types a number   B close"
		                 : "Left/Right change   A use   B close   L/R page",
		             small_font, centered, 15, -3);
#else
		draw_font_hv(VGAScreen, mid_x, py1 - 8,
		             (shownId == DBG_ADD_CASH || shownId == DBG_HANG_TIMEOUT)
		                 ? "Type a number   Enter set   Esc close"
		                 : "Left/Right change   Enter use   Esc close",
		             small_font, centered, 15, -3);
#endif

		// A scrolling list, so a tap only reaches the rows already drawn: this screen keeps
		// the cursor keys.
		touch_ui_set_layout(TOUCH_LAYOUT_LIST);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		// Keep pumping packets while the modal panel stops gameplay.
		NETWORK_KEEP_ALIVE();

#if defined(__SWITCH__) || defined(__vita__)
		{
			static bool shoulder_was[2];
			debug_panel_shoulder_paging(shoulder_was);
		}
#endif

		/* wheel scrolls the selection; hover highlights on pointer motion; a click
		 * acts on the row (left = activate/advance, right = reverse, Enter rows drill in) */
		{
			if (mouse_scroll != 0)
			{
				const int dir = (mouse_scroll > 0) ? -1 : 1;
				for (int n = (mouse_scroll > 0) ? mouse_scroll : -mouse_scroll; n > 0; --n)
					selected = dbgRowStep(selected, dir);
				mouse_scroll = 0;
			}
			if (mouse_x != prev_mx || mouse_y != prev_my)
			{
				const int hov = debug_panel_row_at(mouse_x, mouse_y, px0, px1, items_top,
				                             row_h, visibleRows, scrollTop, menuCount);
				if (hov >= 0 && !dbgRowIsHeading(hov))
					selected = hov;
			}
			prev_mx = mouse_x;
			prev_my = mouse_y;
			if (newmouse)
			{
				const int r = debug_panel_row_at(lastmouse_x, lastmouse_y, px0, px1, items_top,
				                           row_h, visibleRows, scrollTop, menuCount);
				if (r >= 0 && !dbgRowIsHeading(r))
				{
					const int rid = dbgRowId(r);
					selected = r;
					const bool enterRow = (rid == DBG_EXPERT_SETTINGS || rid == DBG_ADD_CASH ||
					                       rid == DBG_SKIP_LEVEL || rid == DBG_PLAY_SOUND ||
					                       rid == DBG_PLAY_MUSIC || rid == DBG_SPRITE_VIEWER ||
					                       rid == DBG_TWIDDLE || rid == DBG_FORCE_CRASH ||
					                       rid == DBG_ENDLESS_TUNE || rid == DBG_HANG_TIMEOUT);
					newkey = true;
					lastkey_scan = (lastmouse_but == SDL_BUTTON_RIGHT) ? SDL_SCANCODE_LEFT
					             : (enterRow ? SDL_SCANCODE_RETURN : SDL_SCANCODE_RIGHT);
				}
				newmouse = false;
			}
		}

		if (newkey)
		{
			// Read the row id HERE, not with the footer above: a click this same frame moves
			// `selected` and then synthesizes the key, so the id must follow that move.
			const int selId = dbgRowId(selected);
			// For the loadout refresh below: only an actual hull SWAP may re-armor the player, so
			// compare rather than assume the Ship row was touched (Left at id 0 changes nothing).
			const int editPlayer = dbgPlayer;   // the Edit Player row may move the selector below
			PlayerItems *const edit = &player[editPlayer].items;
			const JE_byte shipBefore = edit->ship;
			const bool editKey = (lastkey_scan == SDL_SCANCODE_LEFT || lastkey_scan == SDL_SCANCODE_RIGHT ||
			                      lastkey_scan == SDL_SCANCODE_RETURN || lastkey_scan == SDL_SCANCODE_KP_ENTER ||
			                      lastkey_scan == SDL_SCANCODE_SPACE);
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				selected = dbgRowStep(selected, -1);
				break;
			case SDL_SCANCODE_DOWN:
				selected = dbgRowStep(selected, 1);
				break;
			case SDL_SCANCODE_PAGEUP:
				selected = dbgRowSnap(selected - visibleRows);
				break;
			case SDL_SCANCODE_PAGEDOWN:
				selected = dbgRowSnap(selected + visibleRows);
				break;
			case SDL_SCANCODE_HOME:
				selected = dbgRowSnap(0);
				break;
			case SDL_SCANCODE_END:
				selected = dbgRowSnap(menuCount - 1);
				break;
			case SDL_SCANCODE_LEFT:
				switch (selId)
				{
				case DBG_PLAYER: dbgPlayer = (dbgPlayer + (int)COUNTOF(player) - 1) % (int)COUNTOF(player); break;
				case DBG_SHIP:
					// Extra ships occupy IDs 91 through 100.
					if (edit->ship > 91) --edit->ship;
					else if (edit->ship == 91) edit->ship = SHIP_DRAGONWING;
					else if (edit->ship > 0) --edit->ship;
					if (edit->ship > 90)
						endlessNoteCustomShip();
					break;
				case DBG_FRONT_WEAPON: if (edit->weapon[FRONT_WEAPON].id > 0) --edit->weapon[FRONT_WEAPON].id; break;
				case DBG_FRONT_POWER: if (edit->weapon[FRONT_WEAPON].power > 1) --edit->weapon[FRONT_WEAPON].power; break;
				case DBG_REAR_WEAPON: if (edit->weapon[REAR_WEAPON].id > 0) --edit->weapon[REAR_WEAPON].id; break;
				case DBG_REAR_POWER: if (edit->weapon[REAR_WEAPON].power > 1) --edit->weapon[REAR_WEAPON].power; break;
				case DBG_SHIELD: if (edit->shield > 0) --edit->shield; break;
				case DBG_GENERATOR: if (edit->generator > 0) --edit->generator; break;
				case DBG_SIDEKICK_L: if (edit->sidekick[LEFT_SIDEKICK] > 0) --edit->sidekick[LEFT_SIDEKICK]; break;
				case DBG_SIDEKICK_R: if (edit->sidekick[RIGHT_SIDEKICK] > 0) --edit->sidekick[RIGHT_SIDEKICK]; break;
				case DBG_SPECIAL:  // step to the previous crash-safe special (skip bad-icon slots)
					for (int nid = (int)edit->special - 1; nid >= 0; --nid)
						if (debug_special_is_safe(nid)) { edit->special = (JE_byte)nid; break; }
					break;
				case DBG_TWIDDLE:
					dbgTwiddleId = (dbgTwiddleId + (int)COUNTOF(keyboardCombos) - 1) % (int)COUNTOF(keyboardCombos);
					debugTwiddleSpecial = (JE_byte)twiddle_special_id(dbgTwiddleId);
					break;
				case DBG_AUTOFIRE_TWIDDLE: debugAutofireTwiddle = !debugAutofireTwiddle; break;
				case DBG_TOGGLE_FIRE: debugToggleFire = !debugToggleFire; debugToggleFireActive = false; break;
				case DBG_AUTOFIRE_SPECIAL: autoFireSpecial = !autoFireSpecial; break;
				case DBG_AUTOFIRE_CHARGE: chargeSidekickAutofire = (chargeSidekickAutofire + CHARGE_AUTOFIRE_NUM - 1) % CHARGE_AUTOFIRE_NUM; break;
				case DBG_INSTANT_CHARGE: cheatInstantCharge = !cheatInstantCharge; break;
				case DBG_INF_SIDEKICK_AMMO: cheatInfiniteSidekickAmmo = !cheatInfiniteSidekickAmmo; break;
				case DBG_INF_GENERATOR: cheatInfiniteGenerator = !cheatInfiniteGenerator; break;
				case DBG_GOD_MODE: debug_god_mode_set((debug_god_mode_get() + 3) % 4); break;
				case DBG_NOCLIP: noclipMode = (noclipMode + NOCLIP_NUM - 1) % NOCLIP_NUM; break;
				case DBG_EXPERT_MODE: expertMode = !expertMode; break;
				case DBG_EXPERT_SETTINGS: break;  // opens on Right/Enter
				case DBG_AUTO_DIFFICULTY: difficultyAdjust = !difficultyAdjust; break;
				case DBG_DIFFICULTY: if (difficultyLevel > DIFFICULTY_WIMP) --difficultyLevel; break;
				case DBG_ADD_CASH:
					player_set_cash(&player[editPlayer], cashMax);
					endlessCashDebugOverwrite();
					snprintf(dbgCashStr, sizeof(dbgCashStr), "%lld", (long long)cashMax);
					break;
				case DBG_NO_ENEMY_FIRE: cheatNoEnemyFire = !cheatNoEnemyFire; break;
				case DBG_PLAY_SOUND: if (dbgSoundId > 1) --dbgSoundId; break;
				case DBG_PLAY_MUSIC: if (dbgMusicId > 0) --dbgMusicId; break;
				case DBG_SPRITE_VIEWER: break;  // opens on Right/Enter
				// The layer is already active during an Endless run; changing the flag there
				// would desynchronize the row's "ENDLESS" readout.
				case DBG_ENDLESS_FX: debug_toggle_campaign_mods(); break;
				case DBG_ENDLESS_TUNE: break;  // opens on Right/Enter
				case DBG_HITBOX: debugHitboxOverlay = !debugHitboxOverlay; break;
				case DBG_PERF: debugPerfOverlay = !debugPerfOverlay; break;
				case DBG_ROLLBACK_SELFTEST: debug_toggle_rollback_selftest(); break;
				default: break;  // Hang Watchdog / Skip Level are Enter-only actions
				}
				break;
			case SDL_SCANCODE_RIGHT:
				switch (selId)
				{
				// Clamp indexes to the inclusive X_NUM table bound.
				case DBG_PLAYER: dbgPlayer = (dbgPlayer + 1) % (int)COUNTOF(player); break;
				case DBG_SHIP:
					if (edit->ship < SHIP_DRAGONWING) ++edit->ship;
					else if (edit->ship == SHIP_DRAGONWING &&
					         extraShipsAllowedInGame() && extraAvailFor((uint)editPlayer)) edit->ship = 91;
					else if (extraShipsAllowedInGame() &&
					         edit->ship >= 91 && edit->ship < 100) ++edit->ship;
					if (edit->ship > 90)
						endlessNoteCustomShip();
					break;
				case DBG_FRONT_WEAPON: if (edit->weapon[FRONT_WEAPON].id < PORT_NUM) ++edit->weapon[FRONT_WEAPON].id; break;
				case DBG_FRONT_POWER: if (edit->weapon[FRONT_WEAPON].power < 11) ++edit->weapon[FRONT_WEAPON].power; break;
				case DBG_REAR_WEAPON: if (edit->weapon[REAR_WEAPON].id < PORT_NUM) ++edit->weapon[REAR_WEAPON].id; break;
				case DBG_REAR_POWER: if (edit->weapon[REAR_WEAPON].power < 11) ++edit->weapon[REAR_WEAPON].power; break;
				case DBG_SHIELD: if (edit->shield < SHIELD_NUM) ++edit->shield; break;
				case DBG_GENERATOR: if (edit->generator < POWER_NUM) ++edit->generator; break;
				case DBG_SIDEKICK_L: if (edit->sidekick[LEFT_SIDEKICK] < OPTION_NUM) ++edit->sidekick[LEFT_SIDEKICK]; break;
				case DBG_SIDEKICK_R: if (edit->sidekick[RIGHT_SIDEKICK] < OPTION_NUM) ++edit->sidekick[RIGHT_SIDEKICK]; break;
				case DBG_SPECIAL:  // step to the next crash-safe special (skip bad-icon slots)
					for (int nid = (int)edit->special + 1; nid <= SPECIAL_NUM; ++nid)
						if (debug_special_is_safe(nid)) { edit->special = (JE_byte)nid; break; }
					break;
				case DBG_TWIDDLE:
					dbgTwiddleId = (dbgTwiddleId + 1) % (int)COUNTOF(keyboardCombos);
					debugTwiddleSpecial = (JE_byte)twiddle_special_id(dbgTwiddleId);
					break;
				case DBG_AUTOFIRE_TWIDDLE: debugAutofireTwiddle = !debugAutofireTwiddle; break;
				case DBG_TOGGLE_FIRE: debugToggleFire = !debugToggleFire; debugToggleFireActive = false; break;
				case DBG_AUTOFIRE_SPECIAL: autoFireSpecial = !autoFireSpecial; break;
				case DBG_AUTOFIRE_CHARGE: chargeSidekickAutofire = (chargeSidekickAutofire + 1) % CHARGE_AUTOFIRE_NUM; break;
				case DBG_INSTANT_CHARGE: cheatInstantCharge = !cheatInstantCharge; break;
				case DBG_INF_SIDEKICK_AMMO: cheatInfiniteSidekickAmmo = !cheatInfiniteSidekickAmmo; break;
				case DBG_INF_GENERATOR: cheatInfiniteGenerator = !cheatInfiniteGenerator; break;
				case DBG_GOD_MODE: debug_god_mode_set((debug_god_mode_get() + 1) % 4); break;
				case DBG_NOCLIP: noclipMode = (noclipMode + 1) % NOCLIP_NUM; break;
				case DBG_EXPERT_MODE: expertMode = !expertMode; break;
				case DBG_EXPERT_SETTINGS: JE_expertSettingsMenu(off_x, off_y); break;
				case DBG_AUTO_DIFFICULTY: difficultyAdjust = !difficultyAdjust; break;
				case DBG_DIFFICULTY: if (difficultyLevel < DIFFICULTY_10) ++difficultyLevel; break;
				case DBG_ADD_CASH:
					player_set_cash(&player[editPlayer], cashMax);
					endlessCashDebugOverwrite();
					snprintf(dbgCashStr, sizeof(dbgCashStr), "%lld", (long long)cashMax);
					break;
				case DBG_NO_ENEMY_FIRE: cheatNoEnemyFire = !cheatNoEnemyFire; break;
				case DBG_PLAY_SOUND: if (dbgSoundId < SOUND_COUNT) ++dbgSoundId; break;
				case DBG_PLAY_MUSIC: if (dbgMusicId < MUSIC_NUM - 1) ++dbgMusicId; break;
				case DBG_SPRITE_VIEWER: JE_spriteViewer(off_x, off_y); break;
				case DBG_ENDLESS_FX: debug_toggle_campaign_mods(); break;
				case DBG_ENDLESS_TUNE: endlessDebugTuneScreen(); break;
				case DBG_HITBOX: debugHitboxOverlay = !debugHitboxOverlay; break;
				case DBG_PERF: debugPerfOverlay = !debugPerfOverlay; break;
				case DBG_ROLLBACK_SELFTEST: debug_toggle_rollback_selftest(); break;
				default: break;  // Hang Watchdog / Skip Level are Enter-only actions
				}
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				switch (selId)
				{
				case DBG_EXPERT_SETTINGS:  // drill-in
					JE_expertSettingsMenu(off_x, off_y);
					break;
				case DBG_SPRITE_VIEWER:  // drill-in
					JE_spriteViewer(off_x, off_y);
					break;
				case DBG_ENDLESS_TUNE:  // drill-in: sector mods, personal buffs, perks, zone scaling
					endlessDebugTuneScreen();
					break;
				case DBG_TWIDDLE:  // request a one-shot fire of the selected twiddle's special
				{
					debugTwiddleSpecial = (JE_byte)twiddle_special_id(dbgTwiddleId);
					if (debugTwiddleSpecial > 0)
					{
						debugTwiddleTrigger = true;
						done = true;  // close the menu so it fires when gameplay resumes
					}
					break;
				}
				// Declare debug wallet overwrites without classifying them as earned income.
				case DBG_ADD_CASH:
#ifdef PLATFORM_HANDHELD
					// No physical keyboard here: pop the software keyboard to fill the field.
					console_swkbd(dbgCashStr, sizeof(dbgCashStr), cashMaxDigits, dbgCashStr, "Add Cash", true);
#endif
					if (dbgCashStr[0])  // ignore a bare Enter on an empty field (don't zero cash by accident)
					{
						Sint64 v = 0;
						for (const char *c = dbgCashStr; *c >= '0' && *c <= '9'; ++c)
							v = v * 10 + (*c - '0');
						player_set_cash(&player[editPlayer], v);
						endlessCashDebugOverwrite();
					}
					break;
				case DBG_HANG_TIMEOUT:  // apply the typed watchdog timeout in seconds (clamps to range)
#ifdef PLATFORM_HANDHELD
					console_swkbd(dbgHangStr, sizeof(dbgHangStr), sizeof(dbgHangStr) - 1, dbgHangStr, "Hang timeout (seconds)", true);
#endif
					if (dbgHangStr[0])  // ignore a bare Enter on an empty field
					{
						int v = 0;
						for (const char *c = dbgHangStr; *c >= '0' && *c <= '9'; ++c)
							v = v * 10 + (*c - '0');
						crashlog_set_hang_timeout(v);  // clamps into [MIN, MAX]
						dbgHangStr[0] = '\0';  // committed -> clear so the row shows the applied (clamped) value
					}
					break;
				case DBG_SKIP_LEVEL:  // flag it and close so the game processes it
					// Online skip-level uses a synchronized rollback request bit.
					if (isNetworkGame && !center)
						skipLevelRequest = true;
					else
						reallyEndLevel = true;
					done = true;
					break;
				case DBG_FORCE_CRASH:
					debug_force_crash();
					break;
				case DBG_PLAY_SOUND:  // Play the selected sound sample
					JE_playSampleNum((JE_byte)dbgSoundId);
					break;
				case DBG_PLAY_MUSIC:  // Play the selected music track
					play_song((unsigned int)dbgMusicId);
					break;
				default:
					break;
				}
				break;
			case SDL_SCANCODE_ESCAPE:
				done = true;
				break;
			default:
			{
				/* Numeric fields read scancodes because this menu may drop SDL_TEXTINPUT. */
				char *editStr = (selId == DBG_ADD_CASH)     ? dbgCashStr
				              : (selId == DBG_HANG_TIMEOUT) ? dbgHangStr
				                                            : NULL;
				const int editMax = (selId == DBG_ADD_CASH) ? cashMaxDigits : 4;
				if (editStr != NULL)
				{
					int digit = -1;
					if (lastkey_scan >= SDL_SCANCODE_1 && lastkey_scan <= SDL_SCANCODE_9)
						digit = lastkey_scan - SDL_SCANCODE_1 + 1;
					else if (lastkey_scan == SDL_SCANCODE_0)
						digit = 0;
					else if (lastkey_scan >= SDL_SCANCODE_KP_1 && lastkey_scan <= SDL_SCANCODE_KP_9)
						digit = lastkey_scan - SDL_SCANCODE_KP_1 + 1;
					else if (lastkey_scan == SDL_SCANCODE_KP_0)
						digit = 0;

					if (digit >= 0)
					{
						size_t l = strlen(editStr);
						if (strcmp(editStr, "0") == 0)  // replace a lone leading zero
							l = 0;
						if ((int)l < editMax)
						{
							editStr[l] = (char)('0' + digit);
							editStr[l + 1] = '\0';
						}
					}
					else if (lastkey_scan == SDL_SCANCODE_BACKSPACE || lastkey_scan == SDL_SCANCODE_DELETE)
					{
						size_t l = strlen(editStr);
						if (l > 0)
							editStr[l - 1] = '\0';
					}
				}
				break;
			}
			}

			// Rebuild loadout-derived caches after any row handler.
			if (editKey && dbgRowIsLoadout(selId))
				debug_apply_loadout_change(editPlayer, edit->ship != shipBefore, false);

			newkey = false;
		}
	}

	// Publish simulation-affecting debug edits to the peer.
	if (isNetworkGame)
		network_debug_sync_send();

	// Nothing fades on the way out of here, so the cursor keys would sit over the screen
	// underneath until the request went stale. Drop them with the panel.
	touch_ui_clear_layout();

	mouseSetRelative(wasRelative);
	debugMenuOverHud = wasOverHud;

	VGAScreen = temp_surface;
}

// Hitboxes use simulation coordinates and snap between ticks. The performance
// overlay is drawn at presentation time.
void JE_drawDebugOverlays(void)
{
	enum { COL_ENEMY = 124, COL_PLAYER = 0xFB, COL_PSHOT = 0xFB, COL_ESHOT = 124 };

	if (debugHitboxOverlay)
	{
		// Draw enemy hitboxes from the same anchors used by collision checks.
		for (int i = 0; i < 100; ++i)
		{
			if (enemyAvail[i] != 0)
				continue;
			const bool small = (enemy[i].enemycycle > 0);
			const int cx = enemy[i].ex + enemy[i].mapoffset + (centeredShotHitboxes ? 6 : 0);
			const int cy = enemy[i].ey + (centeredShotHitboxes ? 7 : (small ? -6 : -12));
			const int hw = small ? 13 : 25;
			const int hh = small ? 15 : 29;
			draw_rect_outline(VGAScreen, cx - hw, cy - hh, cx + hw, cy + hh, COL_ENEMY);
		}

		// player ship body box, centred on the hull sprite (drawn ~7px right/down
		// of x,y) so it matches the centred collision in JE_playerCollide
		for (uint p = 0; p < (twoPlayerMode ? 2u : 1u); ++p)
		{
			if (!player[p].is_alive)
				continue;
			const int cx = player[p].x + 7, cy = player[p].y + 7;
			draw_rect_outline(VGAScreen, cx - 12, cy - 14, cx + 12, cy + 14, COL_PLAYER);

			// ...and within it the box enemy shots are tested against, which Centered Hitboxes
			// moves onto the middle of the hull and Classic leaves on the ship's own position.
			const int sx = cx - (centeredShotHitboxes ? 0 : 7);
			const int sy = cy - (centeredShotHitboxes ? 0 : 7);
			const int hw = endlessHitboxScale((int)player[p].shot_hit_area_x);
			const int hh = endlessHitboxScale((int)player[p].shot_hit_area_y);
			draw_rect_outline(VGAScreen, sx - hw, sy - hh, sx + hw, sy + hh, COL_PLAYER);
		}

		// projectiles, marked at the point each one's own hit test is taken from. The last slot is
		// the Zinglon pillar's flag rather than a shot, and carries no shot data to read.
		for (int i = 0; i < MAX_PWEAPON - 1; ++i)
		{
			if (shotAvail[i] == 0)
				continue;
			int dx, dy;
			player_shot_hit_offset(playerShotData[i].shotGr + playerShotData[i].shotAni, &dx, &dy);
			const int sx = playerShotData[i].shotX + dx, sy = playerShotData[i].shotY + dy;
			draw_rect_outline(VGAScreen, sx - 1, sy - 1, sx + 1, sy + 1, COL_PSHOT);
		}
		for (int i = 0; i < ENEMY_SHOT_MAX; ++i)
		{
			if (enemyShotAvail[i] != 0)
				continue;
			int dx, dy;
			enemy_shot_hit_offset(enemyShot[i].sgr, enemyShot[i].animate, &dx, &dy);
			const int sx = enemyShot[i].sx + dx, sy = enemyShot[i].sy + dy;
			draw_rect_outline(VGAScreen, sx - 1, sy - 1, sx + 1, sy + 1, COL_ESHOT);
		}
	}
}

static Uint32 perfOverlayRowsCalls[VIDEO_ROWS_SITES];

/* Draw the performance overlay last, on the final composited frame. */
void JE_drawPerfOverlay(SDL_Surface *dst, int scale)
{
	if (!debugPerfOverlay)
		return;

	int activeEnemies = 0, pShots = 0, eShots = 0;
	for (int i = 0; i < 100; ++i)
		if (enemyAvail[i] != 1)
			++activeEnemies;
	for (int i = 0; i < MAX_PWEAPON; ++i)
		if (shotAvail[i] != 0)
			++pShots;
	for (int i = 0; i < ENEMY_SHOT_MAX; ++i)
		if (enemyShotAvail[i] == 0)
			++eShots;

	const int ms = (current_fps > 0) ? (1000 / current_fps) : 0;
	const int px = 4;  // just inside the playfield's left edge
	int py = 4;
	char buf[40];

	snprintf(buf, sizeof(buf), "FPS %d (%dms)", current_fps, ms);
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;

	FrameTimes times;
	frame_times_get(&times);
	snprintf(buf, sizeof(buf), "TICK %.2f DRAW %.2fms", times.tick_ms, times.draw_ms);
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
	snprintf(buf, sizeof(buf), "PRES %.2f MAX %.1fms", times.present_ms, times.worst_ms);
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;

	snprintf(buf, sizeof(buf), "ENEMIES %d", activeEnemies);
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
	snprintf(buf, sizeof(buf), "SHOTS P%d E%d", pShots, eShots);
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
	snprintf(buf, sizeof(buf), "ALPHA %d%%", (int)(debug_interp_alpha * 100.0f + 0.5f));
	JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;

	static const char *const rowsPaths[VIDEO_ROWS_SITES] = {
		[VIDEO_ROWS_HI] = "HI", [VIDEO_ROWS_FIT] = "NATIVE", [VIDEO_ROWS_REPLAY] = "REPLAY",
		[VIDEO_ROWS_COMPOSE] = "COMPOSE", [VIDEO_ROWS_SMOOTHIE] = "SMOOTHIE",
		[VIDEO_ROWS_TICK] = "TICK", [VIDEO_ROWS_FLAT] = "PLAIN",
	};
	bool rowsShown = false;
	for (int site = 0; site < VIDEO_ROWS_SITES; ++site)
	{
		VideoRowsStats rows;
		video_rows_stats(site, &rows);
		const bool used = rows.calls != perfOverlayRowsCalls[site];
		perfOverlayRowsCalls[site] = rows.calls;
		if (!used)
			continue;

		switch (rows.mode)
		{
		case VIDEO_ROWS_UNUSED:
			continue;
		case VIDEO_ROWS_SMALL:
			snprintf(buf, sizeof(buf), "ROWS %s 1T SMALL", rowsPaths[site]);
			break;
		case VIDEO_ROWS_NO_POOL:
			snprintf(buf, sizeof(buf), "ROWS %s OFF", rowsPaths[site]);
			break;
		case VIDEO_ROWS_TUNING:
			snprintf(buf, sizeof(buf), "ROWS %s TUNING", rowsPaths[site]);
			break;
		case VIDEO_ROWS_ONE_THREAD:
		case VIDEO_ROWS_THREADED:
			snprintf(buf, sizeof(buf), "ROWS %s %dT %.2f/%.2fms", rowsPaths[site],
			         rows.mode == VIDEO_ROWS_THREADED ? rows.threads : 1,
			         rows.one_ms, rows.many_ms);
			break;
		case VIDEO_ROWS_FIXED:
			snprintf(buf, sizeof(buf), "ROWS %s %dT SET", rowsPaths[site], rows.threads);
			break;
		}
		JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
		rowsShown = true;
	}
	if (!rowsShown)
	{
		snprintf(buf, sizeof(buf), "ROWS UNUSED");
		JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
	}

	// PRED is prediction lead; RB is rollback rate/depth; DESYNC counts canary mismatches.
	if (nrb_active())
	{
		Uint32 predict, depth, rate, desyncs;
		nrb_stats(&predict, &depth, &rate, &desyncs);

		snprintf(buf, sizeof(buf), "PRED %u", (unsigned)predict);
		JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
		snprintf(buf, sizeof(buf), "RB %u%%/%u", (unsigned)rate, (unsigned)depth);
		JE_textShadeScaled(dst, px, py, buf, 15, 2, scale); py += 9;
		if (desyncs != 0)
		{
			snprintf(buf, sizeof(buf), "DESYNC %u", (unsigned)desyncs);
			JE_textShadeScaled(dst, px, py, buf, 15, 2, scale);
		}
	}
}
