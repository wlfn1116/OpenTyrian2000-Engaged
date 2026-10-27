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
#include "ingame_menu.h"

#include "audio.h"
#include "config.h"
#include "debug_menu.h"
#include "draw.h"
#include "endless.h"
#include "episodes.h"
#include "extra_ships.h"
#include "font.h"
#include "game_menu.h"
#include "game_state.h"
#include "helptext.h"
#include "hud.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "mtrand.h"
#include "musmast.h"
#include "net_rollback.h"
#include "net_start.h"
#include "net_style.h"
#include "network.h"
#include "palette.h"
#include "params.h"
#include "player.h"
#include "qa.h"
#include "render_list.h"
#include "rollback.h"
#include "sfx.h"
#include "ship_skin.h"
#include "sndmast.h"
#include "sprite.h"
#include "timing.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

static Uint8 debug_menu_backup[DEBUG_MENU_WIDTH * DEBUG_MENU_HEIGHT];

typedef struct
{
	PlayerItems items;
	bool valid;
}
ExtraShipReturn;

// Each seat keeps its standard loadout for restoration.
static ExtraShipReturn extraShipReturn[2];

static JE_boolean JE_inGameSetup(void);

static bool extraShipsAllowedForMode(bool network, bool twoPlayer, bool coop, bool separate,
	                                 bool timedBattle, bool superTyrianActive, int superArcade)
{
	if (timedBattle || superTyrianActive || superArcade != SA_NONE)
		return false;
	return network ? twoPlayer && (coop || separate) : !twoPlayer;
}

bool extraShipsAllowedInGame(void)
{
	return extraShipsAllowedForMode(isNetworkGame, twoPlayerMode, coop_mode_active(),
	                                arcade_separate_mode(), timedBattleMode,
	                                superTyrian, superArcadeMode);
}

void extraShipReturnReset(void)
{
	memset(extraShipReturn, 0, sizeof(extraShipReturn));
}

void extraShipRememberStandard(uint pnum)
{
	if (pnum >= COUNTOF(extraShipReturn) || extraShipIdIsCustom(player[pnum].items.ship))
		return;

	extraShipReturn[pnum].items = player[pnum].items;
	extraShipReturn[pnum].valid = true;
}

static bool extraShipRestoreStandard(uint pnum)
{
	if (pnum >= COUNTOF(extraShipReturn) || !extraShipReturn[pnum].valid)
		return false;

	PlayerItems *const items = &player[pnum].items;
	PlayerItems restored = extraShipReturn[pnum].items;
	for (uint i = 0; i < COUNTOF(restored.weapon); ++i)
		restored.weapon[i].power = items->weapon[i].power;
	restored.sidekick_series = items->sidekick_series;
	restored.sidekick_level = items->sidekick_level;
	restored.super_arcade_mode = items->super_arcade_mode;
	*items = restored;
	return true;
}

void JE_doInGameSetup(void)
{
	// A modal UI mid-tick (which may even change sim-affecting settings) makes
	// the tick non-replayable; skip self-test verification of this tick.
	rollback_taint("ingame-setup");

	// Suspend render-list capture while nested gameplay menus draw their own frames.
	const bool rl_was_recording = render_list_recording;
	render_list_recording = false;

	mouseSetRelative(false);
	joystick_rumble_stop_all();

	haltGame = false;

	JE_repaintShieldArmorBars();

	// Rollback releases the verified menu frame through PACKET_GAME_MENU.
	const bool rollback = nrb_active();
	const Uint16 release = rollback ? PACKET_GAME_MENU : PACKET_WAITING;
	if (isNetworkGame && !rollback)
	{
		network_prepare(PACKET_GAME_MENU);
		network_send(4);  // PACKET_GAME_MENU

		// Present the frozen frame with the cursor composited while we wait for
		// the peer's menu packet, like the rendezvous loops below.
		SDL_Surface *const save_surface = VGAScreen;
		VGAScreen = VGAScreenSeg;

		while (true)
		{
			service_SDL_events(false);

			mouseCursor = MOUSE_POINTER_NORMAL;
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (!output_vsync)
				limit_render_fps();

			if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_GAME_MENU)
			{
				network_update();
				break;
			}

			network_update();
			network_check();
		}

		VGAScreen = save_surface;
	}

	if (yourInGameMenuRequest)
	{
		// A gameplay wire test has no player at the menu: it closes immediately, no quit.
		if (qa_net_gameplay_ticks == 0 && JE_inGameSetup())
		{
			reallyEndLevel = true;
			playerEndLevel = true;
		}
		quitRequested = false;

		keysactive[SDL_SCANCODE_ESCAPE] = false;

		if (isNetworkGame)
		{
			if (!playerEndLevel)
			{
				network_prepare(release);
				network_send(rollback ? 4 + nrb_menu_release_fill(&packet_out_temp->data[4]) : 4);
			}
			else
			{
				network_prepare(PACKET_GAME_QUIT);
				network_send(4);  // PACKET_GAMEQUIT
			}
		}
	}

	if (isNetworkGame)
	{
		SDL_Surface *temp_surface = VGAScreen;
		VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

		if (!yourInGameMenuRequest)
		{
			JE_drawNetworkNotice("Other player in options menu.");
			JE_showVGA();

			while (true)
			{
				service_SDL_events(false);
				// Keep the mouse cursor alive while we wait on the other player.
				mouseCursor = MOUSE_POINTER_NORMAL;
				JE_mouseStart();
				JE_showVGA();
				JE_mouseReplace();
				if (!output_vsync)
					limit_render_fps();

				// The other player may be in the debug menu: adopt whatever it rewrote before the
				// release that frees us.  Reliable and ordered, so it always arrives first.
				if (network_debug_sync_pump(true))
					continue;

				if (packet_in[0])
				{
					if (SDLNet_Read16(&packet_in[0]->data[0]) == release)
					{
						// Consume the release before the next rendezvous can see it.
						network_update();
						network_check();
						break;
					}
					else if (SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_GAME_QUIT)
					{
						reallyEndLevel = true;
						playerEndLevel = true;
						if (coopEndlessMode)
							endlessCoopPeerQuitLevel();

						// Left queued on purpose: the level-end paths are the ones that read
						// a peer's quit (see nrb_peer_left_level).
						network_check();
						break;
					}
				}

				network_update();
				network_check();
			}
		}

		while (!network_is_sync())
		{
			service_SDL_events(false);
			mouseCursor = MOUSE_POINTER_NORMAL;
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (!output_vsync)
				limit_render_fps();

			// Both players can reach the menu on the same frame, in which case neither ran the
			// wait loop above and this is where a debug block from the other one turns up.
			if (network_debug_sync_pump(true))
				continue;

			network_check();
		}

		VGAScreen = temp_surface; /* side-effect of game_screen */
	}

	yourInGameMenuRequest = false;

	mouseSetRelative(true);

	render_list_recording = rl_was_recording;
}

// Pause-menu cheats and debug shortcuts. True closes the pause menu.
static int extraMenuNextCustomShipSlot(int slot, int dir, bool canReturn)
{
	slot = (slot + (dir > 0 ? 1 : 10)) % 11;
	if (slot == 0 && !canReturn)
		slot = (dir > 0) ? 1 : 10;
	return slot;
}

// Switch loadouts while preserving the live armor and shield ratios.
static void extraMenuCycleCustomShip(uint pnum, int dir)
{
	Player *const p = &player[pnum];
	const JE_byte *const table = extraShipsFor(pnum);

	int slot = extraShipIdIsCustom(p->items.ship) ? p->items.ship - EXTRA_SHIP_ID_BASE : 0;
	extraShipRememberStandard(pnum);
	slot = extraMenuNextCustomShipSlot(slot, dir, extraShipReturn[pnum].valid);

	rollback_taint(pnum == 0 ? "edit-ship-1" : "edit-ship-2");
	if (slot == 0)
	{
		if (extraShipRestoreStandard(pnum))
			extraShipLoadoutRefresh(pnum, true);
		return;
	}

	const int base = (slot - 1) * 15;
	endlessNoteCustomShip();  // an Endless record earns its "C" for a custom hull too

	p->items.ship = (JE_byte)(90 + slot);
	p->items.weapon[FRONT_WEAPON].id = extraShipResolvePort(pnum, table[base + 1]);
	p->items.weapon[REAR_WEAPON].id = extraShipResolvePort(pnum, table[base + 2]);
	// A file from another machine may name a special whose icon the HUD cannot draw.
	p->items.special = debug_special_is_safe(table[base + 3]) ? table[base + 3] : 0;
	p->items.sidekick[LEFT_SIDEKICK] = table[base + 4];
	p->items.sidekick[RIGHT_SIDEKICK] = table[base + 5];
	p->items.generator = table[base + 6];
	p->items.shield = table[base + 8];

	extraShipLoadoutRefresh(pnum, true);
}

static void extraMenuDrawShipPreview(uint pnum, int cx, int y, int banking, bool skinned)
{
	Sprite2_array *sheet;
	JE_word gr = player_seat_hull(pnum, &sheet);
	if (skinned)
		shipSkinApply(pnum, &sheet, &gr);
	if (sheet == NULL || sheet->data == NULL)
		return;
	const NetShipStyle style = netStyleForSeat(pnum);
	const int pose = banking * 2;

	if (gr == 0)
	{
		blit_sprite2x2_alpha(VGAScreen, cx - 2 * SHOP_WIDE_HULL_HALF, y, *sheet, 13 + pose,
		                     style.bank, style.opacity);
		blit_sprite2x2_alpha(VGAScreen, cx, y, *sheet, 51 + pose, style.bank, style.opacity);
	}
	else if (gr == 1)
	{
		// Nort Ship keeps its center hull and adds an edge sprite for banked poses.
		blit_sprite2x2_alpha(VGAScreen, cx - 2 * SHOP_WIDE_HULL_HALF, y, *sheet, 220,
		                     style.bank, style.opacity);
		blit_sprite2x2_alpha(VGAScreen, cx, y, *sheet, 222, style.bank, style.opacity);
		unsigned int trim = 0;
		int trimX = 0;
		switch (banking)
		{
		case -2: trim = 59; trimX = cx + SHOP_WIDE_HULL_HALF; break;
		case -1: trim = 58; trimX = cx + SHOP_WIDE_HULL_HALF; break;
		case  1: trim = 39; trimX = cx - 2 * SHOP_WIDE_HULL_HALF; break;
		case  2: trim = 40; trimX = cx - 2 * SHOP_WIDE_HULL_HALF; break;
		default: break;
		}
		if (trim != 0)
			blit_sprite2_alpha(VGAScreen, trimX, y + 14, *sheet, trim,
			                   style.bank, style.opacity);
	}
	else
		blit_sprite2x2_alpha(VGAScreen, cx - SHOP_WIDE_HULL_HALF, y, *sheet, gr + pose,
		                     style.bank, style.opacity);
}

void qa_test_extra_ship_return(void)
{
	Player savedPlayers[COUNTOF(player)];
	memcpy(savedPlayers, player, sizeof(savedPlayers));
	ExtraShipReturn savedReturn[COUNTOF(extraShipReturn)];
	memcpy(savedReturn, extraShipReturn, sizeof(savedReturn));

	extraShipReturnReset();
	PlayerItems standard = player[0].items;
	standard.ship = 7;
	standard.generator = 3;
	standard.shield = 6;
	standard.weapon[FRONT_WEAPON].id = 14;
	standard.weapon[REAR_WEAPON].id = 15;
	standard.sidekick[LEFT_SIDEKICK] = 16;
	standard.sidekick[RIGHT_SIDEKICK] = 17;
	standard.special = 8;
	player[0].items = standard;
	extraShipRememberStandard(0);

	player[0].items.ship = 91;
	player[0].items.generator = 9;
	player[0].items.shield = 10;
	player[0].items.weapon[FRONT_WEAPON].id = 11;
	player[0].items.weapon[REAR_WEAPON].id = 12;
	player[0].items.weapon[FRONT_WEAPON].power = 9;
	player[0].items.sidekick[LEFT_SIDEKICK] = 13;
	player[0].items.sidekick[RIGHT_SIDEKICK] = 14;
	player[0].items.special = 15;
	extraShipRememberStandard(0);  // an extra ship must not replace the return point
	const bool restored = extraShipRestoreStandard(0);
	PlayerItems expected = standard;
	expected.weapon[FRONT_WEAPON].power = 9;
	qa_check(restored && memcmp(&player[0].items, &expected, sizeof(expected)) == 0,
	         "the extra-ship return point restores every item the custom ship replaced");
	qa_check(player[0].items.weapon[FRONT_WEAPON].power == 9,
	         "returning to the standard loadout keeps upgrades earned after the switch");

	PlayerItems standard2 = player[1].items;
	standard2.ship = 8;
	standard2.generator = 4;
	standard2.shield = 7;
	player[1].items = standard2;
	extraShipRememberStandard(1);
	player[0].items.ship = 91;
	player[1].items.ship = 92;
	player[1].items.generator = 10;
	player[1].items.weapon[FRONT_WEAPON].power = 8;
	const PlayerItems seat0Custom = player[0].items;
	PlayerItems expected2 = standard2;
	expected2.weapon[FRONT_WEAPON].power = 8;
	qa_check(extraShipRestoreStandard(1)
	         && memcmp(&player[1].items, &expected2, sizeof(expected2)) == 0
	         && memcmp(&player[0].items, &seat0Custom, sizeof(seat0Custom)) == 0,
	         "each online seat restores its own standard loadout without changing the other seat");

	qa_check(player_carry_gauge(80, 80, 200) == 200
	         && player_carry_gauge(40, 80, 200) == 100
	         && player_carry_gauge(300, 200, 80) == 80
	         && player_carry_gauge(0, 0, 250) == 250,
	         "custom-ship gauges preserve full, damaged, clamped, and uninitialized states");
	qa_check(extraMenuNextCustomShipSlot(0, +1, true) == 1
	         && extraMenuNextCustomShipSlot(0, -1, true) == 10
	         && extraMenuNextCustomShipSlot(10, +1, true) == 0
	         && extraMenuNextCustomShipSlot(1, -1, true) == 0,
	         "the custom-ship cycler puts the standard loadout before slots 1 through 10");
	qa_check(extraMenuNextCustomShipSlot(10, +1, false) == 1
	         && extraMenuNextCustomShipSlot(1, -1, false) == 10,
	         "a custom loadout with no known predecessor skips the unavailable return point");
	qa_check(extraShipPreviewBank(0) == 0
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS) == -1
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS * 2) == -2
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS * 4) == 0
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS * 6) == 2
	         && extraShipPreviewBank(EXTRA_SHIP_PREVIEW_BANK_MS * 8) == 0,
		         "the custom-ship preview sweeps from center to both sides and back");
	struct ExtraShipModeCase
	{
		bool network, twoPlayer, coop, separate, timedBattle, superTyrian, allowed;
		int superArcade;
		const char *name;
	};
	static const struct ExtraShipModeCase modeCases[] = {
		{ false, false, false, false, false, false, true,  SA_NONE,      "1 Player Campaign" },
		{ false, false, false, false, false, false, true,  SA_NONE,      "1 Player Endless" },
		{ false, false, false, false, false, false, true,  SA_NONE,      "1 Player Arcade" },
		{ true,  true,  true,  false, false, false, true,  SA_NONE,      "Online Campaign" },
		{ true,  true,  true,  false, false, false, true,  SA_NONE,      "Online Endless" },
		{ true,  true,  false, true,  false, false, true,  SA_NONE,      "Online Separate Arcade" },
		{ false, false, false, false, true,  false, false, SA_NONE,      "1 Player Timed Battle" },
		{ false, true,  false, false, false, false, false, SA_NONE,      "2 Player Arcade" },
		{ false, false, false, false, false, false, false, SA_NORTSHIPZ, "1 Player Super Arcade" },
		{ false, false, false, false, false, true,  false, SA_NONE,      "SuperTyrian" },
		{ true,  true,  false, false, false, false, false, SA_NONE,      "Online Linked Arcade" },
		{ true,  true,  false, true,  true,  false, false, SA_NONE,      "Online Timed Battle" },
		{ true,  true,  false, true,  false, true,  false, SA_NONE,      "Online SuperTyrian" },
		{ true,  true,  false, true,  false, false, false, SA_NORTSHIPZ, "Online Super Arcade" },
	};
	for (uint i = 0; i < COUNTOF(modeCases); ++i)
	{
		const struct ExtraShipModeCase *const c = &modeCases[i];
		char label[128];
		snprintf(label, sizeof(label), "custom-ship availability matches %s", c->name);
		qa_check(extraShipsAllowedForMode(c->network, c->twoPlayer, c->coop, c->separate,
		                                      c->timedBattle, c->superTyrian, c->superArcade)
		         == c->allowed, label);
	}

	memcpy(player, savedPlayers, sizeof(savedPlayers));
	memcpy(extraShipReturn, savedReturn, sizeof(extraShipReturn));
}

static bool JE_extraMenu(void)
{
	enum { PAGE_ROOT, PAGE_CHEATS, PAGE_DEBUG };
	enum
	{
		ROOT_INVINCIBILITY, ROOT_CUSTOM_SHIP, ROOT_SHIP_SKIN, ROOT_CHEATS, ROOT_DEBUG, ROOT_RETURN,
		ROOT_COUNT
	};

	// The cheat combos (and invincibility) are only valid in a normal solo game,
	// matching the guards on the key handlers in JE_mainKeyboardInput.
	const bool cheatsAllowed = !isNetworkGame && !twoPlayerMode && !superTyrian && superArcadeMode == SA_NONE;

	const uint shipSeat = (isNetworkGame && thisPlayerNum >= 1) ? (uint)(thisPlayerNum - 1) : 0;
	const bool shipRowAllowed = extraShipsAllowedInGame() && extraAvailFor(shipSeat);

	// Footer help is drawn as two short lines (description + key combo) so long
	// combos never overflow the panel.
	static const char *const rootLabels[ROOT_COUNT] = {
		"Invincibility", "Custom Ship", "Ship Skin", "Cheat Codes...", "Debug Codes...", "Return"
	};
	static const char *const rootDesc[ROOT_COUNT] = {
		"Don't die when armor runs out.",
		"Cycle editor ships or restore your loadout.",
		"Wear another hull's look. Stats stay the same.",
		"Trigger the F-key cheat combos.",
		"The Backspace debug-key combos.",
		"Return to the pause menu.",
	};
	static const char *const rootCombo[ROOT_COUNT] = {
		"", "Tab+Number in flight (offline)", "", "", "", ""
	};

	static const char *const cheatLabels[] = { "Nort Ship", "Self-Destruct", "Skip Level", "Return" };
	static const char *const cheatDesc[] = {
		"Switch to the Nort Ship.",
		"Zero your ship's armor.",
		"Jump to the next level.",
		"Back to Extra.",
	};
	static const char *const cheatCombo[] = { "F2+F4+F6+F7+F9+\\+/", "F2+F3+F4", "F2+F6+F7", "" };

	static const char *const debugLabels[] = { "Debug Overlay", "Hyper-Speed", "Level Filter", "Random Music", "Screenshot Pause", "Return" };
	static const char *const debugDesc[] = {
		"Show the debug info overlay.",
		"Fast-forward gameplay.",
		"Tint the level colors.",
		"Play a random music track.",
		"Freeze; hides the pause text.",
		"Back to Extra.",
	};
	static const char *const debugCombo[] = { "F10+Backspace", "Backspace+1", "Backspace+minus", "Backspace+ScrollLock", "Backspace+NumLock", "" };

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	const bool wasRelative = mouseGetRelative();
	mouseSetRelative(false);

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // mouse pointer sprites

	// The pause loop repaints its full background after this opaque panel closes.

	wait_noinput_consume(false, false, true);  // don't let the click/key that opened us leak in

	if (isNetworkGame)
		network_debug_sync_mark();

	// Panel geometry: reuse the debug menu's horizontal span, compact and centered.
	const int px0 = DEBUG_MENU_X;
	const int px1 = DEBUG_MENU_X + DEBUG_MENU_WIDTH - 1;
	const int title_h = 15;
	const int row_h = 16;
	const int footer_h = 26;  // two footer lines (description + key combo)
	const int maxRows = 6;
	const int panel_h = title_h + 4 + maxRows * row_h + footer_h;
	int py0 = DEBUG_MENU_Y + (DEBUG_MENU_HEIGHT - panel_h) / 2;
	if (py0 < DEBUG_MENU_Y)
		py0 = DEBUG_MENU_Y;
	const int py1 = py0 + panel_h;
	const int items_top = py0 + title_h + 4;
	const int mid_x = (px0 + px1) / 2;
	const int preview_x0 = px1 - 75;
	const int preview_x1 = px1 - 10;
	const int preview_y0 = items_top + (ROOT_SHIP_SKIN + 1) * row_h + 1;
	const int preview_y1 = py1 - footer_h - 5;

	int page = PAGE_ROOT;
	size_t selected = 0;
	int prev_mx = mouse_x, prev_my = mouse_y;
	Uint32 shipPreviewStart = SDL_GetTicks();
	int shipPreviewRow = -1;

	bool closeMenu = false;
	bool returnToGame = false;

	while (!closeMenu)
	{
		const bool skinRowAllowed = shipSkinWearable(shipSeat);
		Sprite2_array *shipSheet;
		const JE_word shipHull = player_seat_hull(shipSeat, &shipSheet);

		// Rows for the current page.
		const char *const *labels;
		const char *const *descs;
		const char *const *combos;
		int count;
		const char *title;
		switch (page)
		{
		case PAGE_CHEATS: labels = cheatLabels; descs = cheatDesc; combos = cheatCombo; count = 4; title = "CHEAT  CODES"; break;
		case PAGE_DEBUG:  labels = debugLabels; descs = debugDesc; combos = debugCombo; count = 6; title = "DEBUG  CODES"; break;
		default:          labels = rootLabels;  descs = rootDesc;  combos = rootCombo;  count = ROOT_COUNT;
		                  title = "EXTRA"; break;
		}

		if ((int)selected >= count)
			selected = count - 1;

		debug_panel_frame(px0, py0, px1, py1, title_h, mid_x, title);

		for (int i = 0; i < count; ++i)
		{
			const int ry = items_top + i * row_h;
			const bool sel = ((int)selected == i);

			// Grey out cheat rows (and the paths to them) when cheats aren't valid.
			bool enabled = true;
			if (page == PAGE_ROOT && (i == ROOT_INVINCIBILITY || i == ROOT_CHEATS))
				enabled = cheatsAllowed;
			else if (page == PAGE_ROOT && i == ROOT_CUSTOM_SHIP)
				enabled = shipRowAllowed;
			else if (page == PAGE_ROOT && i == ROOT_SHIP_SKIN)
				enabled = skinRowAllowed;
			else if (page == PAGE_CHEATS && i < 3)
				enabled = cheatsAllowed;
			else if (page == PAGE_DEBUG && (i == 1 || i == 2))
				enabled = !isNetworkGame;

			if (sel)
				fill_rectangle_xy(VGAScreen, px0 + 3, ry - 1, px1 - 3, ry + row_h - 3,
				                  PANEL_SEL_BAR);

			const int bright = enabled ? (sel ? 5 : -1) : -6;
			draw_font_hv_shadow(VGAScreen, px0 + 14, ry, labels[i], small_font, left_aligned, 15, bright, true, 1);

			// Value column (toggles / cyclers).
			char valbuf[32];
			const char *value = NULL;
			if (page == PAGE_ROOT && i == ROOT_INVINCIBILITY)
				value = youAreCheating ? "On" : "Off";
			else if (page == PAGE_ROOT && i == ROOT_CUSTOM_SHIP)
			{
				if (player[shipSeat].items.ship > 90 && player[shipSeat].items.ship <= 100)
				{
					snprintf(valbuf, sizeof(valbuf), "Ship %d", player[shipSeat].items.ship - 90);
					value = valbuf;
				}
				else
					value = "Standard";
			}
			else if (page == PAGE_ROOT && i == ROOT_SHIP_SKIN)
				value = shipSkinName(shipSkinWorn(shipSeat, shipSheet, shipHull), valbuf,
				                     sizeof(valbuf));
			else if (page == PAGE_DEBUG && i == 0)
				value = debug ? "On" : "Off";
			else if (page == PAGE_DEBUG && i == 1)
				value = fastPlay == 0 ? "Off" : (fastPlay == 1 ? "On" : "Max");
			else if (page == PAGE_DEBUG && i == 2)
			{
				if (levelFilter == -99)
					value = "Off";
				else
				{
					snprintf(valbuf, sizeof(valbuf), "%d", levelFilter);
					value = valbuf;
				}
			}
			else if (page == PAGE_DEBUG && i == 4)
				value = superPause ? "On" : "Off";

			if (value != NULL)
				draw_font_hv_shadow(VGAScreen, px1 - 12, ry, value, small_font, right_aligned, 15, bright, true, 1);

			if (sel)
				draw_font_hv(VGAScreen, px0 + 5, ry, ">", small_font, left_aligned, 15, 6);
		}

		const int previewRow = (page != PAGE_ROOT) ? -1
		                     : (selected == ROOT_CUSTOM_SHIP && shipRowAllowed) ? ROOT_CUSTOM_SHIP
		                     : (selected == ROOT_SHIP_SKIN && skinRowAllowed) ? ROOT_SHIP_SKIN : -1;
		const bool showShipPreview = previewRow >= 0;
		if (showShipPreview && previewRow != shipPreviewRow)
			shipPreviewStart = SDL_GetTicks();
		shipPreviewRow = previewRow;
		if (showShipPreview)
		{
			fill_rectangle_xy(VGAScreen, preview_x0, preview_y0, preview_x1, preview_y1, 0);
			JE_rectangle(VGAScreen, preview_x0, preview_y0, preview_x1, preview_y1, PANEL_EDGE_HI);
			// A composed hull is 28 pixels tall; center it in the preview box.
			extraMenuDrawShipPreview(shipSeat, (preview_x0 + preview_x1) / 2,
			                         (preview_y0 + preview_y1 - 27) / 2,
			                         extraShipPreviewBank(SDL_GetTicks() - shipPreviewStart),
			                         previewRow == ROOT_SHIP_SKIN);
		}

		const char *desc = descs[selected];
		if (page == PAGE_ROOT && selected == ROOT_SHIP_SKIN && !skinRowAllowed &&
		    extraShipsAllowedInGame())
			desc = "Two-piece hulls keep their own look.";

		// Footer (two lines so long key combos don't overflow the panel): the
		// selected row's description, then its key combo.
		fill_rectangle_xy(VGAScreen, px0 + 1, py1 - footer_h, px1 - 1, py1 - footer_h,
		                  PANEL_DIVIDER);
		draw_font_hv(VGAScreen, px0 + 6, py1 - footer_h + 4, desc, small_font, left_aligned, 15, -3);
		if (combos[selected][0] != '\0')
		{
			char keyline[40];
			snprintf(keyline, sizeof(keyline), "Keys: %s", combos[selected]);
			draw_font_hv(VGAScreen, px0 + 6, py1 - footer_h + 15, keyline, small_font, left_aligned, 15, -3);
		}

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		NETWORK_KEEP_ALIVE();  // nothing else pumps packets while this panel is up

		// Mouse: hover to select, wheel to move, left-click to activate, right-click back.
		bool activate = false, goBack = false;
		int adjustDir = 0;  // -1/+1 from Left/Right on a toggle/cycler row

		if (mouse_scroll != 0)
		{
			int ns = (int)selected - mouse_scroll;
			ns = MAX(0, MIN(count - 1, ns));
			selected = (size_t)ns;
			mouse_scroll = 0;
		}
		const bool overShipPreview = showShipPreview &&
		                                 mouse_x >= preview_x0 && mouse_x <= preview_x1 &&
		                                 mouse_y >= preview_y0 && mouse_y <= preview_y1;
		if (mouse_x != prev_mx || mouse_y != prev_my)
		{
			prev_mx = mouse_x;
			prev_my = mouse_y;
			if (!overShipPreview && mouse_x >= px0 && mouse_x <= px1 && mouse_y >= items_top)
			{
				const int vis = (mouse_y - items_top) / row_h;
				if (vis >= 0 && vis < count)
					selected = (size_t)vis;
			}
		}
		if (newmouse)
		{
			const bool clickedShipPreview = showShipPreview &&
			                                lastmouse_x >= preview_x0 && lastmouse_x <= preview_x1 &&
			                                lastmouse_y >= preview_y0 && lastmouse_y <= preview_y1;
			if (lastmouse_but == SDL_BUTTON_LEFT && clickedShipPreview)
			{
				activate = true;
			}
			else if (lastmouse_but == SDL_BUTTON_LEFT && lastmouse_x >= px0 && lastmouse_x <= px1 && lastmouse_y >= items_top)
			{
				const int vis = (lastmouse_y - items_top) / row_h;
				if (vis >= 0 && vis < count)
				{
					selected = (size_t)vis;
					activate = true;
				}
			}
			else if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				goBack = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				selected = selected == 0 ? (size_t)(count - 1) : selected - 1;
				break;
			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				selected = (int)selected == count - 1 ? 0 : selected + 1;
				break;
			case SDL_SCANCODE_LEFT:
				adjustDir = -1;
				break;
			case SDL_SCANCODE_RIGHT:
				adjustDir = +1;
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_SPACE:
				activate = true;
				break;
			case SDL_SCANCODE_ESCAPE:
				goBack = true;
				break;
			default:
				break;
			}
		}

		if (goBack)
		{
			JE_playSampleNum(S_SPRING);
			if (page == PAGE_ROOT)
				closeMenu = true;
			else
			{
				// land back on the row that opened it
				selected = (page == PAGE_CHEATS) ? ROOT_CHEATS : ROOT_DEBUG;
				page = PAGE_ROOT;
			}
			continue;
		}

		// Left/Right adjusts the toggles and cyclers in place.
		if (adjustDir != 0)
		{
			if (page == PAGE_ROOT && selected == ROOT_INVINCIBILITY && cheatsAllowed)
			{
				youAreCheating = !youAreCheating;
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_ROOT && selected == ROOT_CUSTOM_SHIP && shipRowAllowed)
			{
				extraMenuCycleCustomShip(shipSeat, adjustDir);
				shipPreviewStart = SDL_GetTicks();
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_ROOT && selected == ROOT_SHIP_SKIN && skinRowAllowed)
			{
				shipSkinCycle(shipSeat, shipSheet, shipHull, adjustDir);
				shipPreviewStart = SDL_GetTicks();
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_DEBUG && selected == 0)
			{
				debug = !debug;
				debugHist = 1; debugHistCount = 1; lastDebugTime = SDL_GetTicks();
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_DEBUG && (selected == 1 || selected == 2) && isNetworkGame)
			{
				JE_playSampleNum(S_SPRING);
			}
			else if (page == PAGE_DEBUG && selected == 1)
			{
				fastPlay = (fastPlay + (adjustDir > 0 ? 1 : 2)) % 3;
				JE_setNewGameSpeed();
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_DEBUG && selected == 2)
			{
				if (adjustDir > 0)
				{
					if (levelFilter == -99) levelFilter = 0;
					else { levelFilter++; if (levelFilter == 16) levelFilter = -99; }
				}
				else
				{
					if (levelFilter == -99) levelFilter = 15;
					else if (levelFilter == 0) levelFilter = -99;
					else levelFilter--;
				}
				JE_playSampleNum(S_CURSOR);
			}
			else if (page == PAGE_DEBUG && selected == 4)
			{
				superPause = !superPause;
				JE_playSampleNum(S_CURSOR);
			}
			continue;
		}

		if (!activate)
			continue;

		// Activate the selected row.
		if (page == PAGE_ROOT)
		{
			switch (selected)
			{
			case ROOT_INVINCIBILITY:
				if (cheatsAllowed) { youAreCheating = !youAreCheating; JE_playSampleNum(S_SELECT); }
				else JE_playSampleNum(S_SPRING);
				break;
			case ROOT_CUSTOM_SHIP:  // a tap cycles forward, so it works without arrow keys
				if (shipRowAllowed)
				{
					extraMenuCycleCustomShip(shipSeat, +1);
					shipPreviewStart = SDL_GetTicks();
					JE_playSampleNum(S_SELECT);
				}
				else JE_playSampleNum(S_SPRING);
				break;
			case ROOT_SHIP_SKIN:
				if (skinRowAllowed)
				{
					shipSkinCycle(shipSeat, shipSheet, shipHull, +1);
					shipPreviewStart = SDL_GetTicks();
					JE_playSampleNum(S_SELECT);
				}
				else JE_playSampleNum(S_SPRING);
				break;
			case ROOT_CHEATS:
				if (cheatsAllowed) { page = PAGE_CHEATS; selected = 0; JE_playSampleNum(S_SELECT); }
				else JE_playSampleNum(S_SPRING);
				break;
			case ROOT_DEBUG:
				page = PAGE_DEBUG; selected = 0; JE_playSampleNum(S_SELECT);
				break;
			default:  // Return
				JE_playSampleNum(S_SELECT);
				closeMenu = true;
				break;
			}
		}
		else if (page == PAGE_CHEATS)
		{
			if (selected == 3)  // Return
			{
				JE_playSampleNum(S_SELECT);
				page = PAGE_ROOT; selected = ROOT_CHEATS;
			}
			else if (!cheatsAllowed)
			{
				JE_playSampleNum(S_SPRING);
			}
			else
			{
				JE_playSampleNum(S_SELECT);
				switch (selected)
				{
				case 0:  // Nort Ship
					player[0].items.ship = 12;
					player[0].items.special = 13;
					player[0].items.weapon[FRONT_WEAPON].id = 36;
					player[0].items.weapon[REAR_WEAPON].id = 37;
					shipGr = 1;
					break;
				case 1:  // Self-Destruct (zero armor)
					youAreCheating = false;
					for (uint i = 0; i < COUNTOF(player); ++i)
						player[i].armor = 0;
					break;
				case 2:  // Skip Level
					levelTimer = true;
					levelTimerCountdown = 0;
					endLevel = true;
					levelEnd = 40;
					break;
				default:
					break;
				}
				returnToGame = true;
				closeMenu = true;
			}
		}
		else if ((selected == 1 || selected == 2) && isNetworkGame)
		{
			JE_playSampleNum(S_SPRING);
		}
		else  // PAGE_DEBUG
		{
			JE_playSampleNum(S_SELECT);
			switch (selected)
			{
			case 0:  // Debug Overlay
				debug = !debug;
				debugHist = 1; debugHistCount = 1; lastDebugTime = SDL_GetTicks();
				break;
			case 1:  // Hyper-Speed
				fastPlay = (fastPlay + 1) % 3;
				JE_setNewGameSpeed();
				break;
			case 2:  // Level Filter
				if (levelFilter == -99) levelFilter = 0;
				else { levelFilter++; if (levelFilter == 16) levelFilter = -99; }
				break;
			case 3:  // Random Music
				play_random_song();
				break;
			case 4:  // Screenshot Pause
				superPause = !superPause;
				break;
			default:  // Return
				page = PAGE_ROOT; selected = ROOT_DEBUG;
				break;
			}
		}
	}

	// Publish a ship switch through the pause menu's existing simulation-state sync.
	if (isNetworkGame)
		network_debug_sync_send();

	// Restore input/mouse state; the caller repaints the pause menu.
	newkey = newmouse = false;
	mouseSetRelative(wasRelative);
	VGAScreen = temp_surface;

	return returnToGame;
}

static JE_boolean JE_inGameSetup(void)
{
	bool result = false;

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	enum MenuItemIndex
	{
		MENU_ITEM_MUSIC_VOLUME = 0,
		MENU_ITEM_EFFECTS_VOLUME,
		MENU_ITEM_SHIP_SENS,         // "Sensitivity" slider: touch on consoles, mouse on desktop
		MENU_ITEM_DETAIL_LEVEL,
		MENU_ITEM_GAME_SPEED,
		MENU_ITEM_EXTRA,
		MENU_ITEM_DEBUG,
		MENU_ITEM_RETURN_TO_GAME,
		MENU_ITEM_QUIT,
	};

	// Indexed by id (help for the sensitivity/Extra/Debug/Return/Quit rows is overridden below).
	const size_t helpIndexes[] = { 14, 14, 14, 27, 28, 26, 26, 26, 26 };

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	bool restart = true;

	/* Indexed by MenuItemIndex (id), not by visible row. */
	const char* const menuNames[] =
	{
			inGameText[0],
			inGameText[1],
			SHIP_SENS_NAME,
			inGameText[2],
			inGameText[3],
			"Extra",
			"Debug Menu",
			inGameText[4],
			inGameText[5],
	};

	/* Debug Menu is conditional; online sessions hide Game Speed. */
	enum MenuItemIndex items[COUNTOF(menuNames)];
	size_t menuItemsCount = 0;
	for (size_t i = 0; i < COUNTOF(menuNames); ++i)
	{
		if (i == MENU_ITEM_DEBUG && !debugMode)
			continue;
		if (i == MENU_ITEM_GAME_SPEED && isNetworkGame)
			continue;
		items[menuItemsCount++] = (enum MenuItemIndex)i;
	}

	size_t selectedIndex = 0;

	const int hMenuItem = 13;

	// JE_barShade bounds are inclusive.
	const int yMenuBoxTop = 13, yMenuBoxBottom = 148;

	// Center the visible rows. Nine rows use the tightest 14 px pitch.
	const int menuRows = (int)menuItemsCount;
	const int yMenuMargin = 5;
	const int hMenuRowSpan = yMenuBoxBottom - yMenuBoxTop - 2 * yMenuMargin;
	const int dyMenuItems = menuRows > 1 ? (hMenuRowSpan - hMenuItem) / (menuRows - 1) : 0;
	const int hMenuBlock = dyMenuItems * (menuRows - 1) + hMenuItem;
	const int yMenuItems = yMenuBoxTop + (yMenuBoxBottom - yMenuBoxTop - hMenuBlock) / 2;

	/* Center the legacy left-aligned boxes in the 299-pixel playfield. */
	const int xOfs = (PLAYFIELD_WIDTH - 215) / 2 - 3;      /* main box: x 3..217 */
	const int xHelpOfs = (PLAYFIELD_WIDTH - 255) / 2 - 3;  /* help box: x 3..257 */
	const int xHelpMid = 3 + xHelpOfs + 255 / 2;

	/* Tiny-font baseline for vertically centered help text. */
	const int yHelpBoxTop = 152, yHelpBoxBottom = 168;
	const int yHelpText = (yHelpBoxTop + yHelpBoxBottom) / 2 - 3;

	const int xMenuItem = 10 + xOfs;
	const int xMenuItemName = xMenuItem;
	const int wMenuItemName = 110;
	const int xMenuItemValue = xMenuItemName + wMenuItemName;
	const int wMenuItemValue = 90;
	const int wMenuItem = wMenuItemName + wMenuItemValue;

	for (bool done = false; !done; )
	{
		if (restart)
		{
			JE_barShade(VGAScreen, 3 + xOfs, yMenuBoxTop,     217 + xOfs, yMenuBoxBottom);
			JE_barShade(VGAScreen, 5 + xOfs, yMenuBoxTop + 2, 215 + xOfs, yMenuBoxBottom - 2);

			// Help box
			JE_barShade(VGAScreen, 3 + xHelpOfs, yHelpBoxTop,     257 + xHelpOfs, yHelpBoxBottom);
			JE_barShade(VGAScreen, 5 + xHelpOfs, yHelpBoxTop + 2, 255 + xHelpOfs, yHelpBoxBottom - 2);

			memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);

			mouseCursor = MOUSE_POINTER_NORMAL;

			restart = false;
		}

		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Draw menu items.
		for (size_t i = 0; i < menuItemsCount; ++i)
		{
			const int y = yMenuItems + dyMenuItems * i;

			const enum MenuItemIndex itemId = items[i];

			const char* const name = menuNames[itemId];

			const bool selected = i == selectedIndex;

			draw_font_hv_shadow(VGAScreen, xMenuItemName, y, name, normal_font, left_aligned, 15, -4 + (selected ? 2 : 0), false, 2);

			switch (itemId)
			{
			case MENU_ITEM_MUSIC_VOLUME:
			{
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, music_disabled ? 12 : 16, (tyrMusicVolume + 6) / 12, 3, 13);
				break;
			}
			case MENU_ITEM_EFFECTS_VOLUME:
			{
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, samples_disabled ? 12 : 16, (fxVolume + 6) / 12, 3, 13);
				break;
			}
			case MENU_ITEM_SHIP_SENS:
			{
				// Compare drawn segments so the classic midpoint marker changes exactly on its bar.
				const int amt = (ship_sensitivity + 6) / 12;
				const int mark = (SHIP_SENS_DEFAULT + 6) / 12;
				JE_barDrawShadow(VGAScreen, xMenuItemValue, y, 1, 16, amt, 3, 13);
				JE_barDrawMark(VGAScreen, xMenuItemValue, y,
				               amt >= mark ? SHIP_SENS_MARK_COL : SHIP_SENS_MARK_COL_DIM, mark, 3, 13);
				break;
			}
			case MENU_ITEM_DETAIL_LEVEL:
			{
				draw_font_hv_shadow(VGAScreen, xMenuItemValue, y, detailLevel[processorType-1], normal_font, left_aligned, 15, -4 + (selected ? 2 : 0), false, 2);
				break;
			}
			case MENU_ITEM_GAME_SPEED:
			{
				draw_font_hv_shadow(VGAScreen, xMenuItemValue, y, gameSpeedText[gameSpeed - 1], normal_font, left_aligned, 15, -4 + (selected ? 2 : 0), false, 2);
				break;
			}
			case MENU_ITEM_DEBUG:
			{
				break;
			}
			default:
				break;
			}
		}

		// Draw help text.
		const enum MenuItemIndex selectedId = items[selectedIndex];
		const char* pause_help = mainMenuHelp[helpIndexes[selectedId]];
		if (selectedId == MENU_ITEM_EXTRA)
			pause_help = "Cheats and bonus options.";
		else if (selectedId == MENU_ITEM_DEBUG)
			pause_help = "Open debug menu.";
		else if (selectedId == MENU_ITEM_RETURN_TO_GAME)
			pause_help = "Return to game.";
		else if (selectedId == MENU_ITEM_QUIT)
			pause_help = endlessMode ? "Give up the level; return to the outpost." : "Quit playing the level.";
		else if (selectedId == MENU_ITEM_SHIP_SENS)
			pause_help = SHIP_SENS_HELP;
		/* Center within the help box. Overwide text clamps to its left edge, preventing
		 * negative-x row wrapping while retaining the prior right-edge overflow. */
		int xHelpText = xHelpMid - JE_textWidth(pause_help, TINY_FONT) / 2;
		if (xHelpText < 5 + xHelpOfs)
			xHelpText = 5 + xHelpOfs;
		JE_outTextAdjust(VGAScreen, xHelpText, yHelpText, pause_help, 14, 6, TINY_FONT, true);

		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		bool mouseMoved = false;
		do
		{
			idle_delay(1);  // fine poll so the cursor redraws at display rate on motion

			float oldMouseX = mouse_xf;
			float oldMouseY = mouse_yf;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			NETWORK_KEEP_ALIVE();

			mouseMoved = mouse_xf != oldMouseX || mouse_yf != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		// Handle interaction.

		bool action = false;
		bool leftAction = false;
		bool rightAction = false;

		if (mouseMoved || newmouse)
		{
			// Find menu item that was hovered or clicked.
			if (mouse_x >= xMenuItem && mouse_x < xMenuItem + wMenuItem)
			{
				for (size_t i = 0; i < menuItemsCount; ++i)
				{
					const int yMenuItem = yMenuItems + dyMenuItems * i;
					if (mouse_y >= yMenuItem && mouse_y < yMenuItem + hMenuItem)
					{
						if (selectedIndex != i)
						{
							JE_playSampleNum(S_CURSOR);

							selectedIndex = i;
						}

						if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
						    lastmouse_x >= xMenuItem && lastmouse_x < xMenuItem + wMenuItem &&
						    lastmouse_y >= yMenuItem && lastmouse_y < yMenuItem + hMenuItem)
						{
							// Act on menu item via name.
							if (lastmouse_x >= xMenuItemName && lastmouse_x < xMenuItemName + wMenuItemName)
							{
								action = true;
							}

							// Act on menu item via value.
							else if (lastmouse_x >= xMenuItemValue && lastmouse_x < xMenuItemValue + wMenuItemValue)
							{
								switch (items[i])
								{
								case MENU_ITEM_MUSIC_VOLUME:
								{
									JE_playSampleNum(S_CURSOR);

									const int w = ((255 + 6) / 12) * (3 + 1) - 1;

									int value = (lastmouse_x - xMenuItemValue) * 255 / (w - 1);
									tyrMusicVolume = MIN(MAX(0, value), 255);

									set_volume(tyrMusicVolume, fxVolume);
									break;
								}
								case MENU_ITEM_EFFECTS_VOLUME:
								{
									const int w = ((255 + 6) / 12) * (3 + 1) - 1;

									int value = (lastmouse_x - xMenuItemValue) * 255 / (w - 1);
									fxVolume = MIN(MAX(0, value), 255);

									set_volume(tyrMusicVolume, fxVolume);

									JE_playSampleNum(S_CURSOR);
									break;
								}
								case MENU_ITEM_SHIP_SENS:
								{
									const int w = ((SHIP_SENS_MAX + 6) / 12) * (3 + 1) - 1;

									int value = (lastmouse_x - xMenuItemValue) * SHIP_SENS_MAX / (w - 1);
									ship_sensitivity = MIN(MAX(0, value), SHIP_SENS_MAX);

									JE_playSampleNum(S_CURSOR);
									break;
								}
								case MENU_ITEM_DETAIL_LEVEL:
								case MENU_ITEM_GAME_SPEED:
								{
									rightAction = true;
									break;
								}
								default:
									break;
								}
							}
						}

						break;
					}
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);

				done = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == 0
					? menuItemsCount - 1
					: selectedIndex - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selectedIndex = selectedIndex == menuItemsCount - 1
					? 0
					: selectedIndex + 1;
				break;
			}
			case SDL_SCANCODE_LEFT:
			{
				leftAction = true;
				break;
			}
			case SDL_SCANCODE_RIGHT:
			{
				rightAction = true;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			case SDL_SCANCODE_ESCAPE:
			{
				JE_playSampleNum(S_SPRING);

				done = true;
				break;
			}
			case SDL_SCANCODE_W:
			{
				if (items[selectedIndex] == MENU_ITEM_DETAIL_LEVEL)
				{
					processorType = 6;
					JE_initProcessorType();
				}
				break;
			}
			default:
				break;
			}
		}

		if (action)
		{
			switch (items[selectedIndex])
			{
			case MENU_ITEM_MUSIC_VOLUME:
			{
				JE_playSampleNum(S_SELECT);

				set_music_disabled(!music_disabled);
				break;
			}
			case MENU_ITEM_EFFECTS_VOLUME:
			{
				samples_disabled = !samples_disabled;

				JE_playSampleNum(S_SELECT);
				break;
			}
			case MENU_ITEM_EXTRA:
			{
				JE_playSampleNum(S_SELECT);

				// JE_extraMenu draws its own opaque panel and restores the screen
				// on exit; it returns true if a triggered cheat wants the game back.
				if (JE_extraMenu())
					done = true;
				break;
			}
			case MENU_ITEM_DEBUG:
			{
				JE_playSampleNum(S_SELECT);

				/* capture debug menu area */
				for (int yy = 0; yy < DEBUG_MENU_HEIGHT; ++yy)
				{
					memcpy(&debug_menu_backup[yy * DEBUG_MENU_WIDTH],
						(Uint8*)VGAScreen2->pixels +
						(DEBUG_MENU_Y + yy) * VGAScreen2->pitch + DEBUG_MENU_X,
						DEBUG_MENU_WIDTH);
				}

				JE_debugMenu(false);

				/* restore debug menu area */
				for (int yy = 0; yy < DEBUG_MENU_HEIGHT; ++yy)
				{
					memcpy((Uint8*)VGAScreen->pixels +
						(DEBUG_MENU_Y + yy) * VGAScreen->pitch + DEBUG_MENU_X,
						&debug_menu_backup[yy * DEBUG_MENU_WIDTH],
						DEBUG_MENU_WIDTH);
				}

				restart = true;
				continue; /* redraw menu after exiting debug */
			}
			case MENU_ITEM_RETURN_TO_GAME:
			{
				JE_playSampleNum(S_SELECT);

				done = true;
				break;
			}
			case MENU_ITEM_QUIT:
			{
				JE_playSampleNum(S_SELECT);

				if (constantPlay)
					JE_tyrianHalt(0);

				if (isNetworkGame)
				{
					/*Tell other computer to exit*/
					// playerEndLevel carries an Endless quit to the peer.
					haltGame = !endlessMode;
					playerEndLevel = true;
				}

				// End the level and let the game loop restore the Endless launch snapshot.
				if (endlessMode)
					endlessQuitToOutpost = true;

				result = true;
				done = true;
				break;
			}
			default:
				break;
			}
		}
		else if (leftAction || rightAction)
		{
			const int dir = leftAction ? -1 : 1;

			switch (items[selectedIndex])
			{
			case MENU_ITEM_MUSIC_VOLUME:
			{
				JE_playSampleNum(S_CURSOR);

				JE_changeVolume(&tyrMusicVolume, dir * 12, &fxVolume, 0);
				break;
			}
			case MENU_ITEM_EFFECTS_VOLUME:
			{
				JE_changeVolume(&tyrMusicVolume, 0, &fxVolume, dir * 12);

				JE_playSampleNum(S_CURSOR);
				break;
			}
			case MENU_ITEM_SHIP_SENS:
			{
				ship_sensitivity = MIN(MAX(0, ship_sensitivity + dir * 12), SHIP_SENS_MAX);

				JE_playSampleNum(S_CURSOR);
				break;
			}
			case MENU_ITEM_DETAIL_LEVEL:
			{
				JE_playSampleNum(S_CURSOR);

				if (dir > 0)
					processorType = processorType < 4 ? processorType + 1 : 1;
				else
					processorType = processorType > 1 ? processorType - 1 : 4;
				JE_initProcessorType();
				JE_setNewGameSpeed();
				break;
			}
			case MENU_ITEM_GAME_SPEED:
			{
				JE_playSampleNum(S_CURSOR);

				if (dir > 0)
					gameSpeed = gameSpeed < 5 ? gameSpeed + 1 : 1;
				else
					gameSpeed = gameSpeed > 1 ? gameSpeed - 1 : 5;
				JE_initProcessorType();
				JE_setNewGameSpeed();
				break;
			}
			default:
				break;
			}
		}
	}

	VGAScreen = temp_surface; /* side-effect of game_screen */

	return result;
}

/* Relaxed-mode death prompt. Esc and right-click are inert; one of the three rows must be chosen. */
/* The Relaxed death prompt's rows, one per EndlessDeathChoice. At file scope so
 * qa_test_endless_death_menu below can measure every line against the panel's width clamp. */
static const char *const endlessDeathRowName[] = { "Restart Zone", "Return to Outpost", "End Run" };
static const char *const endlessDeathRowHelp[] =
{
	"Fly this zone again as you launched it.",
	"Back to the outpost for a new course.",
	"End the run and see the summary.",
};
static const char endlessDeathTitle[] = "SHIP DESTROYED";

/* Pinned without the input loop: the rows cover the whole choice enum, and every line fits
 * the panel, whose width self-sizes but clamps at the playfield's edge and would clip. */
void qa_test_endless_death_menu(void)
{
	COMPILE_TIME_ASSERT(death_rows_cover_choices,
	                    COUNTOF(endlessDeathRowName) == ENDLESS_DEATH_END_RUN + 1);
	COMPILE_TIME_ASSERT(death_help_covers_choices,
	                    COUNTOF(endlessDeathRowHelp) == ENDLESS_DEATH_END_RUN + 1);

	char label[128];
	const int widthMax = PLAYFIELD_WIDTH - 32 - 24;  // the panel clamp, minus its padding

	qa_check(JE_textWidth(endlessDeathTitle, normal_font) <= widthMax,
	         "the death prompt's title fits its panel");
	for (uint i = 0; i < COUNTOF(endlessDeathRowName); ++i)
	{
		snprintf(label, sizeof(label), "death prompt row '%s' fits its panel",
		         endlessDeathRowName[i]);
		qa_check(JE_textWidth(endlessDeathRowName[i], normal_font) <= widthMax, label);
		snprintf(label, sizeof(label), "death prompt help '%s' fits its panel",
		         endlessDeathRowHelp[i]);
		qa_check(JE_textWidth(endlessDeathRowHelp[i], small_font) <= widthMax, label);
	}
}

EndlessDeathChoice JE_endlessDeathMenu(void)
{
	SDL_Surface *const temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	// Font bodies occupy different parts of bank 15, so their shade offsets use opposite signs.
	const int titleValue = -1, rowValueOn = -2, rowValueOff = -4, helpValue = 4;

	// Size the panel to its widest line.
	int contentW = JE_textWidth(endlessDeathTitle, normal_font);
	for (int i = 0; i < (int)COUNTOF(endlessDeathRowName); ++i)
	{
		contentW = MAX(contentW, JE_textWidth(endlessDeathRowName[i], normal_font));
		contentW = MAX(contentW, JE_textWidth(endlessDeathRowHelp[i], small_font));
	}

	// Center within the playfield, excluding the right-side HUD.
	const int panelW = MIN(contentW + 24, PLAYFIELD_WIDTH - 32);
	const int px0 = (PLAYFIELD_WIDTH - panelW) / 2, px1 = px0 + panelW;

	// Center within the 184-row playfield above the message bar.
	const int playfieldRows = 184, panelH = 104;
	const int py0 = (playfieldRows - panelH) / 2, py1 = py0 + panelH;

	const int midX = (px0 + px1) / 2;
	const int rowY0 = py0 + 34, rowPitch = 18, rowH = 14;
	const int rowX0 = px0 + 6, rowX1 = px1 - 6;

	// Borrow the menu's bank-15 text ramp; level palettes do not define it consistently.
	SDL_Color savedRamp[16];
	memcpy(savedRamp, &colors[240], sizeof(savedRamp));
	memcpy(&colors[240], &palettes[0][240], sizeof(savedRamp));
	set_palette(colors, 240, 255);

	JE_barShade(VGAScreen, px0, py0, px1, py1);
	JE_barShade(VGAScreen, px0 + 2, py0 + 2, px1 - 2, py1 - 2);

	memcpy(VGAScreen2->pixels, VGAScreen->pixels, (size_t)VGAScreen2->pitch * VGAScreen2->h);

	int selected = 0;
	bool firstFrame = true;

	// Start the fade when the panel appears, even if the wreck animation was skipped.
	MusicFadeOut deathFade;
	music_fade_out_init(&deathFade);

	for (bool done = false; !done; )
	{
		// Restore background.
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		draw_font_hv_shadow(VGAScreen, midX, py0 + 10, endlessDeathTitle, normal_font, centered, 15, titleValue, false, 2);

		for (int i = 0; i < (int)COUNTOF(endlessDeathRowName); ++i)
		{
			draw_font_hv_shadow(VGAScreen, midX, rowY0 + rowPitch * i, endlessDeathRowName[i], normal_font, centered,
			                    15, i == selected ? rowValueOn : rowValueOff, false, 2);
		}

		draw_font_hv_shadow(VGAScreen, midX, py1 - 16, endlessDeathRowHelp[selected], small_font, centered, 15, helpValue, true, 1);

		service_SDL_events(true);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		if (firstFrame)
		{
			// Ignore controls held through the fatal hit without delaying the first presentation or fade.
			firstFrame = false;

			service_SDL_events(false);
			while (keydown || mousedown || joydown)
			{
				music_fade_out_tick(&deathFade);

				NETWORK_KEEP_ALIVE();
				idle_delay(1);
				poll_joysticks();
				service_SDL_events(false);
			}

			newkey = newmouse = false;
		}

		bool mouseMoved = false;
		do
		{
			music_fade_out_tick(&deathFade);

			idle_delay(1);  // fine poll so the cursor redraws at display rate on motion

			const float oldMouseX = mouse_xf;
			const float oldMouseY = mouse_yf;

			push_joysticks_as_keyboard();
			service_SDL_events(false);

			// Keep draining the peer while this screen is open. Otherwise repeated wait
			// packets fill the receive queue and later traffic is lost.
			NETWORK_KEEP_ALIVE();
			while (network_shop_pump())
				;

			mouseMoved = mouse_xf != oldMouseX || mouse_yf != oldMouseY;
		} while (!(newkey || newmouse || mouseMoved));

		bool action = false;

		if ((mouseMoved || newmouse) && mouse_x >= rowX0 && mouse_x < rowX1)
		{
			for (int i = 0; i < (int)COUNTOF(endlessDeathRowName); ++i)
			{
				const int y = rowY0 + rowPitch * i;
				if (mouse_y < y - 2 || mouse_y >= y - 2 + rowH)
					continue;

				if (selected != i)
				{
					JE_playSampleNum(S_CURSOR);

					selected = i;
				}

				if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
				    lastmouse_x >= rowX0 && lastmouse_x < rowX1 &&
				    lastmouse_y >= y - 2 && lastmouse_y < y - 2 + rowH)
				{
					action = true;
				}

				break;
			}
		}

		if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
			{
				JE_playSampleNum(S_CURSOR);

				selected = selected == 0 ? (int)COUNTOF(endlessDeathRowName) - 1 : selected - 1;
				break;
			}
			case SDL_SCANCODE_DOWN:
			{
				JE_playSampleNum(S_CURSOR);

				selected = selected == (int)COUNTOF(endlessDeathRowName) - 1 ? 0 : selected + 1;
				break;
			}
			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_RETURN:
			{
				action = true;
				break;
			}
			default:
				break;
			}
		}

		if (action)
		{
			JE_playSampleNum(S_SELECT);

			done = true;
		}
	}

	// Consume the confirmation and finish the music ramp before returning.
	service_SDL_events(false);
	while (keydown || mousedown || joydown || !deathFade.done)
	{
		music_fade_out_tick(&deathFade);

		NETWORK_KEEP_ALIVE();
		idle_delay(1);
		poll_joysticks();
		service_SDL_events(false);
	}

	// Restore `colors` without presenting it under the still-visible panel.
	memcpy(&colors[240], savedRamp, sizeof(savedRamp));

	VGAScreen = temp_surface; /* side-effect of game_screen */

	return (EndlessDeathChoice)selected;
}
