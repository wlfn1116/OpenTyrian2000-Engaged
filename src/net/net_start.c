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
#include "net_start.h"

#include "config.h"
#include "crashlog.h"
#include "custom_episode.h"
#include "custom_weapon.h"
#include "draw.h"
#include "endless.h"
#include "episodes.h"
#include "extra_ships.h"
#include "font.h"
#include "game_state.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "menus.h"
#include "mouse.h"
#include "net_rollback.h"
#include "network.h"
#include "palette.h"
#include "picload.h"
#include "player.h"
#include "qa.h"
#include "savegame.h"
#include "sfx.h"
#include "sndmast.h"
#include "sprite.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

/* Start a fresh online Endless run. Everything it needs came over in the connect packet, so both
 * machines build the same run without exchanging anything further. */
static void networkEndlessNewRun(void)
{
	endlessResetRun();
	endlessSetSeed(network_endless_session_seed);
	endlessRunMode = (EndlessRunMode)network_host_endless_run_mode;
	endlessCourseChooser = (EndlessCourseChooser)network_host_endless_chooser;
	endlessCoopComboShared = network_host_endless_combo_shared;
	endlessRunBaseRule = (EndlessBaseRule)network_host_endless_base_rule;
	endlessRecordRunStart();

	endlessMode = true;
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		player[i].is_dragonwing = false;
		player[i].lives = player_lives_counter(i);
		player[i].items = player[0].items;
		player[i].last_items = player[0].last_items;
		player[i].cash = 0;
	}

	// The stake is the run's first income for each ship, booked so earned - spent stays true.
	endlessCashResync();
	endlessCashCredit(endlessStartingCash(), ENDLESS_CASH_START);
	player[endlessPartnerIndex()].cash = player[endlessEconomyIndex()].cash;

	endlessApplyStartingLoadout();
	endlessReseedPlayers(0);
}

/* Resume an online Endless run: the host loads it from its own save file and streams the
 * record, and the joiner adopts it. */
static bool networkEndlessResume(JE_byte slot)
{
	const Uint32 begin = SDL_GetTicks();

	bool okay;
	if (thisPlayerNum == networkHostPlayerNum)
	{
		okay = endlessLoadSlot(slot);
		if (okay)
			network_endless_run_publish();
	}
	else
	{
		okay = network_endless_run_receive(20000);
	}

	// A bounded wait that succeeds late leaves no trace. Put a number on the handoff so a
	// slow resume is attributable from the net log alone.
	const Uint32 spent = SDL_GetTicks() - begin;
	if (okay && spent > 3000)
	{
		char detail[120];
		snprintf(detail, sizeof(detail),
		         "the %s spent %lu ms in the run handoff before the outpost could open.",
		         thisPlayerNum == networkHostPlayerNum ? "host (sidecar load + transfer)"
		                                              : "joiner (transfer wait + adopt)",
		         (unsigned long)spent);
		crashlog_netlog_line("ENDLESS RESUME SLOW", detail);
	}

	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: endless resume %s, slot %d\n",
		        okay ? "ok" : "FAILED", (int)slot);
		fflush(stderr);
	}

	return okay;
}

/* Steps the host adds to the lobby's difficulty, which the joiner subtracts again so both land
 * on the same initialDifficulty. */
int networkDifficultyBump(void)
{
	return dual_ship_mode() ? 0 : 1;
}

/* Let each player choose an independent Super Arcade ship while servicing the connection. Returns
 * 1..SA after both picks settle, or 0 if the session ends. */
static int networkSuperArcadeShipSelect(void)
{
	// Command-line netplay reaches here without the title screen having loaded these.
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');  // need mouse pointer sprites

	network_sa_ship_reset();

	int selected = 0;                 // index into SAShip, 0..SA-1
	int chosen = 0;                   // 1..SA once this player has committed
	int peer = 0;
	bool restart = true;
	int wName[SA] = { 0 };
	// Track the last published local pick and latest received peer pick.
	int announcedPick = 0;
	bool announcedSawPeer = false;
	// Wait for the press that opened this screen to be released before accepting a ship.
	bool armed = false;
	const Uint32 armDeadline = SDL_GetTicks() + 500;
	// This screen redraws every frame (it has to: the peer's pick can land at any moment), so
	// hover is re-evaluated only on real motion, or a parked cursor would veto the arrow keys.
	Sint32 lastSeenMouseX = mouse_x, lastSeenMouseY = mouse_y;

	// Wire runs have no keyboard: take the slot's own ship so the two peers differ on purpose.
	if (qa_net_gameplay_ticks > 0)
		chosen = (thisPlayerNum == 2) ? 2 : 1;

	while (true)
	{
		if (restart)
		{
			JE_loadPic(VGAScreen2, 2, false);
			// Use the shop palette shared by the raw-index ship sprites.
			memcpy(colors, palettes[0], sizeof(colors));
			for (int y = 0; y < VGAScreen2->h; ++y)
			{
				Uint8 *const row = (Uint8 *)VGAScreen2->pixels + y * VGAScreen2->pitch;
				for (int x = 0; x < VGAScreen2->w; ++x)
				{
					const Uint8 dim = (row[x] & 0xf0) | ((row[x] & 0x0f) >> 3);
					const SDL_Color c = colors[dim];
					row[x] = (c.r + c.g + c.b > 96) ? 0 : dim;
				}
			}
			draw_font_hv_shadow(VGAScreen2, 320 / 2, SA_PICK_HEADER_Y, superShips[0], large_font,
			                    centered, 15, -3, false, 2);
		}
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Nine names in two columns; the pick is the player's own, so the partner's is only
		// reported, never enforced.
		for (int i = 0; i < SA; ++i)
		{
			const bool onIt = (i == selected);
			wName[i] = JE_textWidth(superShips[i + 1], small_font);
			// Small-font rows use a positive bank-15 offset and one-pixel shadow.
			draw_font_hv_shadow(VGAScreen, sa_pick_name_x(i), sa_pick_name_y(i), superShips[i + 1],
			                    small_font, left_aligned, 15,
			                    2 + (onIt ? 4 : 0) + (chosen == i + 1 ? 2 : 0), false, 1);
		}

		// The highlighted hull, so a name that means nothing yet still shows what it flies.
		const JE_word gr = ships[SAShip[selected]].shipgraphic;
		if (gr > 500)
			blit_sprite2x2(VGAScreen, 320 / 2 - 12, SA_PICK_SHIP_Y, spriteSheetT2000, gr - 500);
		else if (gr == 1)
		{
			// Nort Ship: shipgraphic 1 is a sentinel, so its two halves are blitted by hand.
			blit_sprite2x2(VGAScreen, 320 / 2 - 24, SA_PICK_SHIP_Y, spriteSheet9, 220);
			blit_sprite2x2(VGAScreen, 320 / 2,      SA_PICK_SHIP_Y, spriteSheet9, 222);
		}
		else
			blit_sprite2x2(VGAScreen, 320 / 2 - 12, SA_PICK_SHIP_Y, spriteSheet9, gr);

		const char *status = chosen == 0
		                   ? "Choose your ship."
		                   : (peer == 0 ? "Waiting for the other player..." : "Both ready.");
		draw_font_hv_shadow(VGAScreen, 320 / 2, SA_PICK_STATUS_Y, status, small_font, centered, 15, 6, false, 1);
		// The partner's pick, as soon as it lands: it changes nothing here, but a player who
		// chose first should see something happen when the other one commits.
		if (peer != 0)
		{
			char line[64];
			snprintf(line, sizeof(line), "Player %u flies %s", thisPlayerNum == 2 ? 1u : 2u, superShips[peer]);
			draw_font_hv_shadow(VGAScreen, 320 / 2, SA_PICK_PEER_Y, line, small_font, centered, 15, 4, false, 1);
		}
		else if (chosen != 0)
		{
			// The same line carries the way out while the wait is the only thing happening.
			draw_font_hv_shadow(VGAScreen, 320 / 2, SA_PICK_PEER_Y, SA_PICK_UNPICK_HINT,
			                    small_font, centered, 15, 2, false, 1);
		}

		// Fade in after the handshake's black frame.
		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		// Use the normal menu presenter so cursor motion follows display pacing.
		if (!output_vsync)
			limit_render_fps();
		if (restart)
		{
			fade_palette(colors, 10, 0, 255);
			restart = false;
		}

		// A console has no keyboard: every other interactive screen turns the pad into arrows,
		// Return and Escape here, and without it this one is dead on the Switch and the Vita.
		push_joysticks_as_keyboard();
		service_SDL_events(false);
		if (!armed)
		{
			armed = (!newkey && !newmouse && !joydown && !mousedown) || SDL_GetTicks() >= armDeadline;
			newkey = false;
			newmouse = false;
		}
		network_check();
		peer = network_sa_ship_peer();

		// Keep at most one ship-pick publication in flight.
		if ((chosen != announcedPick || (peer != 0) != announcedSawPeer) && network_is_sync())
		{
			announcedPick = chosen;
			announcedSawPeer = (peer != 0);
			network_sa_ship_publish(announcedPick, announcedSawPeer);
		}

		// Both have a ship, and the peer has said they hold ours -- so neither of us is still
		// offering the take-back, and the pair we leave with is the pair they leave with.
		if (chosen != 0 && peer != 0 && network_sa_ship_peer_saw_us()
		    && announcedSawPeer && network_is_sync())
			return chosen;
		if (!network_peer_alive())
			return 0;

		// A committed pick can be taken back for as long as the partner has not made one. Once
		// they have, the pair is settled and the screen is already on its way out.
		const bool canPick = chosen == 0;
		const bool canUnpick = chosen != 0 && peer == 0;

		// Hover and click, like every other ship or episode menu.
		const bool mouseMoved = mouse_x != lastSeenMouseX || mouse_y != lastSeenMouseY;
		lastSeenMouseX = mouse_x;
		lastSeenMouseY = mouse_y;
		if (canPick && (newmouse || mouseMoved))
		{
			for (int i = 0; i < SA; ++i)
			{
				const int x = sa_pick_name_x(i), y = sa_pick_name_y(i);
				if (mouse_x < x || mouse_x >= x + wName[i] || mouse_y < y || mouse_y >= y + SA_PICK_ROW_H)
					continue;

				if (selected != i)
				{
					JE_playSampleNum(S_CURSOR);
					selected = i;
				}
				if (newmouse && lastmouse_but == SDL_BUTTON_LEFT
				    && lastmouse_x >= x && lastmouse_x < x + wName[i]
				    && lastmouse_y >= y && lastmouse_y < y + SA_PICK_ROW_H)
				{
					JE_playSampleNum(S_SELECT);
					chosen = i + 1;
				}
				break;
			}
			newmouse = false;
		}
		// Right-click takes a pick back, the same as Escape does below.
		if (canUnpick && newmouse && lastmouse_but == SDL_BUTTON_RIGHT)
		{
			JE_playSampleNum(S_SPRING);
			chosen = 0;
			newmouse = false;
		}

		// Escape is the one key that means something in both states, so it is read first.
		if (newkey && lastkey_scan == SDL_SCANCODE_ESCAPE)
		{
			if (canUnpick)
			{
				// Withdraw the pick while the peer is still choosing.
				JE_playSampleNum(S_SPRING);
				chosen = 0;
			}
			else if (chosen == 0)
			{
				// Leaving before committing ends the session; the partner is sitting on this same
				// screen and has to be told, or it waits out the dead-link timeout.
				JE_playSampleNum(S_SPRING);
				network_prepare(PACKET_QUIT);
				network_send(4);  // PACKET QUIT
				network_tyrian_halt(0, true);   // does not return
			}
			// Otherwise both have picked and the game is starting, so there is nothing to leave.
			newkey = false;
		}
		else if (canPick && newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				selected = (selected == 0) ? SA - 1 : selected - 1;
				break;

			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				selected = (selected == SA - 1) ? 0 : selected + 1;
				break;

			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_RIGHT:
				JE_playSampleNum(S_CURSOR);
				selected = (selected + SA_PICK_ROWS) % SA;   // hop between the two columns
				break;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				JE_playSampleNum(S_SELECT);
				chosen = selected + 1;
				break;

			default:
				break;
			}
			newkey = false;
		}
	}
}

/* Equip and record one player's chosen Super Arcade ship. */
void networkSuperArcadeEquip(Player *this_player, int ship)
{
	const uint i = (uint)ship - 1;

	this_player->items.ship = SAShip[i];
	this_player->items.super_arcade_mode = (Uint8)ship;
	this_player->items.weapon[FRONT_WEAPON].id = SAWeapon[i][0];
	this_player->items.weapon[REAR_WEAPON].id = 0;   // Super Arcade issues no rear gun
	this_player->items.special = SASpecialWeapon[i];
	if (ship == SA_NORTSHIPZ)
		for (uint s = 0; s < COUNTOF(this_player->items.sidekick); ++s)
			this_player->items.sidekick[s] = 24;  // Companion Ship Quicksilver
	this_player->last_items = this_player->items;
}

/* Equip one online SuperTyrian ship and clear inherited rear-bay state. */
void networkSuperTyrianEquip(Player *this_player)
{
	this_player->items.ship = 13;                     // The Stalker 21.126
	this_player->items.weapon[FRONT_WEAPON].id = 39;  // Atomic RailGun
	this_player->items.weapon[REAR_WEAPON].id = 0;    // SuperTyrian issues no rear gun
	this_player->items.super_arcade_mode = SA_SUPERTYRIAN;
	this_player->last_items = this_player->items;
}

/* Both Timed Battle players confirm a start card before scoring begins. The
 * shared screen keeps the link alive until either confirmation or Escape. */
static void networkTimedBattleReady(void)
{
	NetReadyScreen ready;
	network_ready_screen_begin(&ready);

	JE_loadPic(VGAScreen2, 2, false);
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 20, "Timed Battle", large_font, centered, 15, -3, false, 2);
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 48, timed_battle_name[timeBattleSelection], normal_font,
	                    centered, 15, -3, false, 2);

	char line[128];
	snprintf(line, sizeof(line), "%s   -   You fly as player %u",
	         difficultyNameB[initialDifficulty], thisPlayerNum);
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 72, line, small_font, centered, 15, 2, false, 1);
	if (network_opponent_name[0] != '\0')
	{
		snprintf(line, sizeof(line), "Racing %s", network_opponent_name);
		draw_font_hv_shadow(VGAScreen2, 320 / 2, 84, line, small_font, centered, 15, 2, false, 1);
	}
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 108, "The clock is the level. Whoever banks the most",
	                    small_font, centered, 15, 4, false, 1);
	draw_font_hv_shadow(VGAScreen2, 320 / 2, 120, "cash before it runs out wins the timed battle.",
	                    small_font, centered, 15, 4, false, 1);

	bool faded = false;

	for (;;)
	{
		// Expose ready and Back before presenting the first frame.
		touch_ui_set_layout(TOUCH_LAYOUT_CONFIRM);

		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// Where the pair stands, brightened as each side commits: the wait has no timeout, so the
		// screen has to say which of the two it is still waiting on.
		draw_font_hv_shadow(VGAScreen, 320 / 2, 150,
		                    ready.localReady ? "You are ready." : "Press a key when you are ready.",
		                    small_font, centered, 15, ready.localReady ? 6 : 2, false, 1);
		draw_font_hv_shadow(VGAScreen, 320 / 2, 162,
		                    ready.peerReady ? "The other player is ready."
		                                    : "Waiting for the other player...",
		                    small_font, centered, 15, ready.peerReady ? 6 : 2, false, 1);
		// Esc is a step back rather than a way out while this player stands confirmed, so it takes
		// two presses to leave from there. Says which one it is about to be.
		draw_font_hv_shadow(VGAScreen, 320 / 2, 178,
		                    ready.localReady ? "Esc takes back your ready."
		                                     : "Esc leaves the session.",
		                    small_font, centered, 15, 0, false, 1);

		JE_showVGA();
		if (!faded)
		{
			fade_palette(colors, 10, 0, 255);
			faded = true;
		}
		if (!output_vsync)
			limit_render_fps();

		if (network_ready_screen_frame(&ready))
			break;
	}

	newkey = false;
	fade_black(10);
}

/* Builds the joiner's wait-screen rows. Also used by layout tests. */
int networkGuestWaitRows(const char **label, const char **value)
{
	const bool endless = network_game_type == NETWORK_GAME_ENDLESS;
	const bool coop = endless || network_game_type == NETWORK_GAME_CAMPAIGN;
	const bool superTyrianGame = network_game_type == NETWORK_GAME_SUPERTYRIAN;

	int rows = 0;

	label[rows] = "Host";
	value[rows++] = network_opponent_name[0] ? network_opponent_name : "(unnamed)";
	label[rows] = "Game Type";
	switch (network_game_type)
	{
	case NETWORK_GAME_ENDLESS:      value[rows++] = "Endless";      break;
	case NETWORK_GAME_CAMPAIGN:     value[rows++] = "Campaign";     break;
	case NETWORK_GAME_SUPERTYRIAN:  value[rows++] = "SuperTyrian";  break;
	case NETWORK_GAME_SUPERARCADE:  value[rows++] = "Super Arcade"; break;
	// Destruct never reaches this screen; the remaining cases are Arcade.
	default:  value[rows++] = timedBattleMode ? "Timed Battle" : "Arcade";  break;
	}
	if (timedBattleMode)
	{
		// Match the lobby's Mode/Level labels.
		label[rows] = "Level";
		value[rows++] = timed_battle_name[timeBattleSelection];
	}
	else if (!endless)
	{
		label[rows] = "Episode";
		if (network_host_custom_file[0] != '\0')
		{
			// Fall back to the advertised file name before download.
			static char customShown[28];
			const int idx = customEpisodeFindByFile(network_host_custom_file);
			const char *const name = (idx >= 0 && customEpisodeTitle(idx)[0] != '\0')
			                       ? customEpisodeTitle(idx) : network_host_custom_file;
			SDL_strlcpy(customShown, name, sizeof(customShown));
			if (strlen(name) >= sizeof(customShown))
				memcpy(&customShown[sizeof(customShown) - 4], "...", 4);
			value[rows++] = customShown;
		}
		else
			value[rows++] = episode_name[network_host_episode];
	}
	// SuperTyrian uses this field for its two variants.
	label[rows] = superTyrianGame ? "Variant" : "Difficulty";
	value[rows++] = superTyrianGame
	              ? (network_host_difficulty == DIFFICULTY_SUICIDE ? "Scrollock" : "Standard")
	              : difficultyNameB[network_host_difficulty];
	if (endless)
	{
		label[rows] = "Run Mode";
		value[rows++] = endlessRunModeName((EndlessRunMode)network_host_endless_run_mode);
		if (network_host_custom_endless != CUSTOM_ENDLESS_OFF)
		{
			// The host supplies this value with its collection.
			label[rows] = "Custom Levels";
			value[rows++] = network_host_custom_endless == CUSTOM_ENDLESS_ONLY
			              ? "Custom Only" : "Mixed";
		}
		label[rows] = "Base Level";
		value[rows++] = endlessBaseLevelRuleName(network_host_endless_base_rule);
		label[rows] = "Charts Course";
		value[rows++] = endlessCourseChooserName((EndlessCourseChooser)network_host_endless_chooser);
		label[rows] = "Combo Feed";
		value[rows++] = network_host_endless_combo_shared ? "Shared" : "Individual";
		label[rows] = "Seed";
		value[rows++] = network_endless_session_seed[0] ? network_endless_session_seed : "(random)";
	}
	if (coop)
	{
		label[rows] = "Credit";
		value[rows++] = coop_credit_is_shared() ? "Shared" : "Individual";
		if (!coop_credit_is_shared())
		{
			label[rows] = "Double Earnings";
			value[rows++] = coop_earnings_are_doubled() ? "On" : "Off";
		}
	}
	else if (network_game_type == NETWORK_GAME_SUPERARCADE)
	{
		label[rows] = "Ships";
		value[rows++] = "You choose";   // each player picks their own on the next screen
	}
	else if (arcadeSeparateMode)
	{
		label[rows] = "Ships";
		value[rows++] = "Separate";
	}
	else
	{
		// Campaign gives both slots the same ship type.
		label[rows] = "You Fly";
		value[rows++] = thisPlayerNum == 2 ? "Dragonwing" : "Silver Ship";
	}
	label[rows] = "Game Speed";
	// Clamp again before indexing the display table.
	value[rows++] = gameSpeedText[MIN(MAX(gameSpeed, 1), 5) - 1];
	label[rows] = "Netcode";
	value[rows++] = nrb_session_mode() ? "Rollback" : "Delay-Based";
	if (nrb_session_mode())
	{
		// Lockstep has no desync-recovery state.
		label[rows] = "Desync Recovery";
		value[rows++] = nrb_session_recovery() ? "On" : "Off";
	}

	return rows;
}

static void networkCustomSyncScreen(void)
{
	JE_clr256(VGAScreen);
	draw_font_hv_shadow(VGAScreen, 320 / 2, 90, "Syncing custom levels...",
	                    normal_font, centered, 15, -2, false, 2);
	JE_showVGA();
	fade_palette(colors, 10, 0, 255);
}

void networkStartScreen(void)
{
	// Lobby games are already connected. Command-line netplay and its lobby-settings
	// wire test still connect here.
	if (!network_from_lobby || (qa_net_gameplay_ticks > 0 && qa_net_lobby_settings))
	{
		JE_loadPic(VGAScreen, 2, false);
		draw_font_hv_shadow(VGAScreen, 320 / 2, 20, "Online Multiplayer", large_font, centered, 15, -3, false, 2);
		memcpy(VGAScreen2->pixels, VGAScreen->pixels, VGAScreen2->pitch * VGAScreen2->h);
		JE_dStringOutlined(VGAScreen, JE_fontCenter("Waiting for other player.", SMALL_FONT_SHAPES), 140, "Waiting for other player.", SMALL_FONT_SHAPES);
		JE_showVGA();
		fade_palette(colors, 10, 0, 255);

		network_connect();
	}
	else
	{
		// Stage the backdrop copied by the joiner's wait screen.
		JE_loadPic(VGAScreen2, 2, false);
		draw_font_hv_shadow(VGAScreen2, 320 / 2, 20, "Online Multiplayer", large_font, centered, 15, -3, false, 2);
	}

	// Destruct receives its mode, teams, and terrain seed from the lobby. Skip the campaign
	// handshake and wait for the reliable channel before entering the minigame.
	if (network_game_type == NETWORK_GAME_DESTRUCT)
	{
		while (!network_is_sync())
		{
			service_SDL_events(false);

			mouseCursor = MOUSE_POINTER_NORMAL;
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (!output_vsync)
				limit_render_fps();

			network_check();
		}

		fade_black(10);
		loadDestruct = true;
		return;
	}

	twoPlayerMode = true;
	coopCampaignMode = network_game_type == NETWORK_GAME_CAMPAIGN;
	coopEndlessMode = network_game_type == NETWORK_GAME_ENDLESS;
	/* Timed Battle uses Separate Arcade ships with its script-facing mode flag. */
	timedBattleMode = network_timed_battle();
	if (timedBattleMode)
	{
		timeBattleSelection = (JE_byte)network_host_battle_level;
		arcadeSeparateMode = true;
	}
	/* The two one-player rulesets flown online give each player a complete ship, so they run in
	 * the Separate arcade shape whatever the host's Mode row says. */
	if (network_game_type_is_super(network_game_type))
		arcadeSeparateMode = true;
	bool resumed = false;
	if (qa_net_gameplay_ticks > 0)
	{
		fprintf(stderr, "net gameplay: connected, player %u\n", thisPlayerNum);
		fflush(stderr);
	}
	if (thisPlayerNum == networkHostPlayerNum)
	{
		fade_black(10);

		// A loaded record travels in the details packet for the joiner to adopt.
		const int resumeSlot = timedBattleMode ? 0 : networkHostStartSelect();
		if (resumeSlot > 0)
		{
			// Restore co-op mode from the session and seat from the save.
			coopCampaignMode = network_game_type == NETWORK_GAME_CAMPAIGN;
			coopEndlessMode = network_game_type == NETWORK_GAME_ENDLESS;

			networkHostPlayerNum = save_slot_online_player((JE_byte)resumeSlot);
			thisPlayerNum = networkHostPlayerNum;

			customEpisodeScan();
			const JE_SaveFileType *const resumeRec = &saveFiles[resumeSlot - 1];

			static char required[CUSTOM_EPISODE_MAX][CUSTOM_EPISODE_FILE_LEN];
			int requiredCount = 0;
			if (coopEndlessMode)
			{
				requiredCount = customEpisodeCollectionNames(resumeRec->customCollection,
				                                             required, CUSTOM_EPISODE_MAX);
				if (requiredCount == 0)
					for (int i = 0; i < customEpisodeCount() && requiredCount < CUSTOM_EPISODE_MAX; ++i)
						SDL_strlcpy(required[requiredCount++], customEpisodeFile(i),
						            CUSTOM_EPISODE_FILE_LEN);
			}
			if (resumeRec->customEpFile[0] != '\0')
			{
				bool listed = false;
				for (int i = 0; i < requiredCount && !listed; ++i)
					listed = SDL_strcasecmp(required[i], resumeRec->customEpFile) == 0;
				if (!listed && requiredCount < CUSTOM_EPISODE_MAX)
					SDL_strlcpy(required[requiredCount++], resumeRec->customEpFile,
					            CUSTOM_EPISODE_FILE_LEN);
			}

			SDL_strlcpy(network_host_custom_file, resumeRec->customEpFile,
			            sizeof(network_host_custom_file));
			network_host_custom_size = 0;
			network_host_custom_hash = 0;
			{
				const int haveIdx = customEpisodeFindByFile(network_host_custom_file);
				if (haveIdx >= 0)
					customEpisodeIdentity(haveIdx, &network_host_custom_size,
					                      &network_host_custom_hash);
			}
			network_host_custom_endless =
				(coopEndlessMode &&
				 (customEndlessMode == CUSTOM_ENDLESS_MIXED || customEndlessMode == CUSTOM_ENDLESS_ONLY))
				? customEndlessMode : CUSTOM_ENDLESS_OFF;

			network_prepare(PACKET_DETAILS);
			SDLNet_Write16(network_game_type, &packet_out_temp->data[4]);
			SDLNet_Write16(episodeNum, &packet_out_temp->data[6]);
			SDLNet_Write16(difficultyLevel, &packet_out_temp->data[8]);
			save_record_pack(&packet_out_temp->data[10], &saveFiles[resumeSlot - 1]);
			packet_out_temp->data[10 + SAVE_RECORD_PACKED_SIZE] = (Uint8)networkHostPlayerNum;
			memset(&packet_out_temp->data[11 + SAVE_RECORD_PACKED_SIZE], 0, CUSTOM_EPISODE_FILE_LEN);
			memcpy(&packet_out_temp->data[11 + SAVE_RECORD_PACKED_SIZE], network_host_custom_file,
			       strlen(network_host_custom_file));
			SDLNet_Write32(network_host_custom_size,
			               &packet_out_temp->data[11 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE]);
			SDLNet_Write32(network_host_custom_hash,
			               &packet_out_temp->data[15 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE]);
			packet_out_temp->data[19 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE] =
				(JE_byte)(requiredCount > 0 ? 1 : 0);
			packet_out_temp->data[20 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE] =
				(JE_byte)network_host_custom_endless;
			network_send(21 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE);

			if (requiredCount > 0)
			{
				networkCustomSyncScreen();
				if (!network_custom_required_serve(required, requiredCount,
				        coopEndlessMode ? network_host_custom_endless : -1))
					network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
				if (resumeRec->customEpFile[0] != '\0' && !customEpisodeActive())
				{
					JE_loadGame((JE_byte)resumeSlot);
					if (!customEpisodeActive())
						network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
				}
			}

			// The save record carries the two loadouts; the Endless run behind them is a record of its
			// own, so it follows on the reliable channel before either machine plays a tick.
			if (coopEndlessMode && !networkEndlessResume((JE_byte)resumeSlot))
				network_tyrian_halt(3, false);

			resumed = true;
		}
		else if (resumeSlot == 0)
		{
			const bool customSession = network_host_custom_file[0] != '\0' &&
			                           !coopEndlessMode && !timedBattleMode;
			// Endless traverses episodes as it deepens, so it always opens on the first one, and a
			// battle level is reached through the episode that holds it (the ']T' jump list).
			if (customSession)
			{
				// The selected file may have disappeared since the lobby scan.
				if (!networkCustomEpisodeActivate())
					network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
			}
			else
			{
				JE_initEpisode(coopEndlessMode ? 1
				               : timedBattleMode ? (JE_byte)network_timed_battle_episode(timeBattleSelection)
				               : network_host_episode);
			}
			/* Record the lobby-selected starting episode. */
			initial_episode_num = episodeNum;
			difficultyLevel = network_host_difficulty;
			initialDifficulty = difficultyLevel;

			difficultyLevel += networkDifficultyBump();

			network_prepare(PACKET_DETAILS);
			SDLNet_Write16(network_game_type, &packet_out_temp->data[4]);
			SDLNet_Write16(episodeNum, &packet_out_temp->data[6]);
			SDLNet_Write16(difficultyLevel, &packet_out_temp->data[8]);
			network_send(10);  // PACKET_DETAILS

			if (customSession ||
			    (coopEndlessMode && network_host_custom_endless != CUSTOM_ENDLESS_OFF))
				networkCustomSyncScreen();

			if (customSession && !network_custom_level_serve())
				network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);

			if (coopEndlessMode && network_host_custom_endless != CUSTOM_ENDLESS_OFF &&
			    !network_custom_endless_serve())
				network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
		}
		else
		{
			network_prepare(PACKET_QUIT);
			network_send(4);  // PACKET QUIT

			network_tyrian_halt(0, true);
		}
	}
	else
	{
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, VGAScreen->pitch * VGAScreen->h);

		// Show the host's settled handshake settings while details arrive. Waiting
		// for a later confirmation would stall the host in the outpost.
		{
			const char *label[GUEST_WAIT_ROWS_CAP], *value[GUEST_WAIT_ROWS_CAP];
			const int rows = networkGuestWaitRows(label, value);

			// Center fixed label and value columns around the widest row.
			int blockW = 140;
			for (int i = 0; i < rows; ++i)
			{
				blockW = MAX(blockW, JE_textWidth(label[i], small_font) + 20
				                     + JE_textWidth(value[i], small_font));
			}
			blockW = MIN(blockW, 300);

			const int xLabel = LEGACY_WIDTH / 2 - blockW / 2;
			const int xValue = xLabel + blockW;

			// Centre the rows, status, and Esc hint below the title.
			const int dyRow = guest_wait_row_h(rows);
			const int blockH = rows * dyRow + guest_wait_gap(rows)
			                   + GUEST_WAIT_LINE_H + GUEST_WAIT_HINT_H;
			const int yTop = GUEST_WAIT_TOP + (GUEST_WAIT_BOTTOM - GUEST_WAIT_TOP - blockH) / 2;

			for (int i = 0; i < rows; ++i)
			{
				const int y = yTop + dyRow * i;
				draw_font_hv_shadow(VGAScreen, xLabel, y, label[i], small_font, left_aligned, 15, 2, false, 1);
				draw_font_hv_shadow(VGAScreen, xValue, y, value[i], small_font, right_aligned, 15, 4, false, 1);
			}

			const int yWait = yTop + rows * dyRow + guest_wait_gap(rows);
			JE_dString(VGAScreen, JE_fontCenter(networkText[4 - 1], SMALL_FONT_SHAPES),
			           yWait, networkText[4 - 1], SMALL_FONT_SHAPES);
			draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, yWait + GUEST_WAIT_LINE_H + 8,
			                    GUEST_WAIT_HINT, small_font, centered, 15, 0, false, 1);
		}

		JE_showVGA();

		// until opponent sends details packet
		newkey = false;  // the press that drove the lobby's join is still latched
		while (true)
		{
			push_joysticks_as_keyboard();
			service_SDL_events(false);

			// Keep the mouse cursor alive while the host picks episode/difficulty.
			mouseCursor = MOUSE_POINTER_NORMAL;
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (!output_vsync)
				limit_render_fps();

			// Process cancel before a newly arrived start packet.
			if ((newkey && lastkey_scan == SDL_SCANCODE_ESCAPE) || qa_net_guest_esc)
			{
				if (qa_net_guest_esc)
				{
					fprintf(stderr, "net gameplay: joiner escapes the details wait\n");
					fflush(stderr);
				}
				JE_playSampleNum(S_SPRING);
				network_prepare(PACKET_QUIT);
				network_send(4);  // PACKET_QUIT
				network_tyrian_halt(0, true);   // does not return
			}

			// Validate length before reading the reused packet buffer.
			if (packet_in[0] && SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_DETAILS &&
			    packet_in[0]->len >= 10)
				break;

			network_update();
			network_check();
		}

		UDPpacket *const details_packet = packet_in[0];
		if (details_packet == NULL)
		{
			network_tyrian_halt(3, false);
			return;
		}
		const int their_game_type  = SDLNet_Read16(&details_packet->data[4]);
		const int their_episode    = SDLNet_Read16(&details_packet->data[6]);
		const int their_difficulty = SDLNet_Read16(&details_packet->data[8]);

		// Reject corrupt host episode values before constructing level filenames.
		if (their_game_type != (int)network_game_type ||
		    their_episode < 1 || their_episode > EPISODE_MAX ||
		    their_difficulty < 1 || their_difficulty > DIFFICULTY_10)
		{
			fprintf(stderr, "error: opponent sent unusable game details (%d/%d/%d)\n",
			        their_game_type, their_episode, their_difficulty);
			network_tyrian_halt(3, false);
		}
		network_host_episode = their_episode;
		network_host_difficulty = their_difficulty;

		if (details_packet->len >= 10 + SAVE_RECORD_PACKED_SIZE)
		{
			// Resume form: adopt the host's save record wholesale. Input devices stay local;
			// they name hardware on the host's desk, not simulation state.
			JE_SaveFileType rec;
			save_record_unpack(&rec, &details_packet->data[10]);
			rec.input1 = inputDevice[0];
			rec.input2 = inputDevice[1];

			if (details_packet->len < 21 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE)
			{
				fprintf(stderr, "error: opponent sent a truncated resume record\n");
				network_tyrian_halt(3, false);
			}

			const uint hostSeat = details_packet->data[10 + SAVE_RECORD_PACKED_SIZE];
			char tailName[CUSTOM_EPISODE_FILE_LEN];
			memcpy(tailName, &details_packet->data[11 + SAVE_RECORD_PACKED_SIZE],
			       CUSTOM_EPISODE_FILE_LEN);
			tailName[CUSTOM_EPISODE_FILE_LEN - 1] = '\0';
			const Uint32 customSize = SDLNet_Read32(
				&details_packet->data[11 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE]);
			const Uint32 customHash = SDLNet_Read32(
				&details_packet->data[15 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE]);
			const bool tailList =
				details_packet->data[19 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE] != 0;
			const int tailMode =
				details_packet->data[20 + CUSTOM_EPISODE_FILE_LEN + SAVE_RECORD_PACKED_SIZE];

			network_update();

			// Restore the host's saved seat; the joiner takes the other one.
			if (hostSeat == 1 || hostSeat == 2)
			{
				networkHostPlayerNum = hostSeat;
				thisPlayerNum = 3 - networkHostPlayerNum;
			}

			if (tailName[0] != '\0' && !customEpisodeFileNameValid(tailName))
			{
				fprintf(stderr, "error: host sent an unusable custom episode name\n");
				network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
			}

			memset(rec.customEpFile, 0, sizeof(rec.customEpFile));
			if (tailName[0] != '\0')
				SDL_strlcpy(rec.customEpFile, tailName, sizeof(rec.customEpFile));
			SDL_strlcpy(network_host_custom_file, tailName,
			            sizeof(network_host_custom_file));
			network_host_custom_size = customSize;
			network_host_custom_hash = customHash;
			network_host_custom_endless =
				(network_game_type == NETWORK_GAME_ENDLESS &&
				 (tailMode == CUSTOM_ENDLESS_MIXED || tailMode == CUSTOM_ENDLESS_ONLY))
				? tailMode : CUSTOM_ENDLESS_OFF;

			if (tailList)
			{
				networkCustomSyncScreen();
				if (!network_custom_required_fetch(
				        network_game_type == NETWORK_GAME_ENDLESS
				        ? network_host_custom_endless : -1))
					network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
			}

			gameJustLoaded = true;
			JE_loadGameRecord(&rec, true);
			if (customEpisodeActive() != (tailName[0] != '\0'))
				network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
			coopCampaignMode = network_game_type == NETWORK_GAME_CAMPAIGN;
			coopEndlessMode = network_game_type == NETWORK_GAME_ENDLESS;

			// Same rule as the host's publish above: no run means no session.
			if (coopEndlessMode && !networkEndlessResume(0))
				network_tyrian_halt(3, false);

			resumed = true;
		}
		else
		{
			network_update();

			if (network_host_custom_file[0] != '\0' &&
			    network_game_type != NETWORK_GAME_ENDLESS && !network_host_timed_battle)
			{
				networkCustomSyncScreen();
				if (!network_custom_level_fetch() || !networkCustomEpisodeActivate())
					network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
			}
			else
			{
				if (network_game_type == NETWORK_GAME_ENDLESS &&
				    network_host_custom_endless != CUSTOM_ENDLESS_OFF)
				{
					networkCustomSyncScreen();
					if (!network_custom_endless_fetch())
						network_tyrian_halt(NET_HALT_CUSTOM_SYNC, false);
				}
				JE_initEpisode(their_episode);
			}
			initial_episode_num = episodeNum;  // as the host does; see its branch above
			difficultyLevel = their_difficulty;
			initialDifficulty = difficultyLevel - networkDifficultyBump();
		}
		fade_black(10);
	}

	if (!resumed)
	{
		if (coopEndlessMode)
		{
			networkEndlessNewRun();
		}
		else if (coop_mode_active())
		{
			static const Uint32 initial_cash[] = { 10000, 15000, 20000, 30000, 20000 };
			const Uint32 cash = initial_cash[episodeNum - 1];
			player[1].items = player[0].items;
			player[1].last_items = player[0].last_items;
			for (uint i = 0; i < COUNTOF(player); ++i)
			{
				player[i].cash = cash;
				player[i].is_dragonwing = false;
				player[i].lives = player_lives_counter(i);
			}
		}
		else
		{
			for (uint i = 0; i < COUNTOF(player); ++i)
				player[i].cash = 0;

			if (arcade_separate_mode())
			{
				if (network_game_type == NETWORK_GAME_SUPERTYRIAN)
				{
					// Equip both players with the standard SuperTyrian loadout.
					superTyrian = true;
					for (uint i = 0; i < COUNTOF(player); ++i)
						networkSuperTyrianEquip(&player[i]);
				}
				else if (network_game_type == NETWORK_GAME_SUPERARCADE)
				{
					// The session global records player one's Super Arcade ship for saves.
					const int mine = networkSuperArcadeShipSelect();
					const int theirs = network_sa_ship_peer();
					if (mine == 0 || theirs == 0)
						network_tyrian_halt(3, false);   // the pair never met; do not guess a ship
					const uint me = thisPlayerNum >= 2 ? 1u : 0u;
					networkSuperArcadeEquip(&player[me], mine);
					networkSuperArcadeEquip(&player[1 - me], theirs);
					superArcadeMode = (JE_byte)player[0].items.super_arcade_mode;
					fade_black(10);
				}
				else
				{
					// Start each Separate Arcade player with the solo Arcade loadout.
					player[0].items.ship = 8;  // Stalker
					// Neither ship flies a Super Arcade ruleset here, and the save record's own
					// discriminator is these two bytes, so say so rather than inherit it.
					player[0].items.super_arcade_mode = SA_NONE;
					player[1].items = player[0].items;
				}
				for (uint i = 0; i < COUNTOF(player); ++i)
				{
					player[i].is_dragonwing = false;
					player[i].lives = player_lives_counter(i);
					player[i].last_items = player[i].items;
				}
			}
			else
			{
				player[0].items.ship = 11;  // Silver Ship
			}
		}

		// Gameplay wire tests: both machines mount the same scripted sidekick combination.
		if (qa_net_gameplay_ticks > 0 && qa_net_loadout > 0)
			qa_net_apply_loadout(qa_net_loadout);
		if (qa_net_gameplay_ticks > 0 && (qa_net_scenario == 5 || qa_net_scenario == 19))
			qa_net_apply_linked_special();
	}
	else if (network_game_type == NETWORK_GAME_CAMPAIGN)
	{
	/* Exchange custom designs before materializing a resumed Campaign loadout. */
		network_custom_weapon_publish();
	}

	/* Publish any referenced weapon, then exchange ships before the first outpost or level. */
	if (custom_ships_multiplayer_mode())
	{
		customWeaponNetPrepare();
		if (extraShipsUseCustomWeapon())
			network_custom_weapon_publish();
		network_extra_ships_publish();
		network_custom_content_rendezvous();
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

		network_check();
	}

	// A race is scored from the first tick, so it does not begin until both players say so.
	if (timedBattleMode)
		networkTimedBattleReady();

	if (qa_net_gameplay_ticks > 0)
	{
		// The session flags as this machine armed or adopted them (the doubled-pickups
		// scenario asserts both peers print the same pair), and the soak baseline.
		network_test_mem_mark();
		fprintf(stderr, "net gameplay: details settled, starting the game\n");
		// Verify both peers received the same per-ship Arcade rulesets.
		fprintf(stderr, "net session flags: shared=%d doubled=%d separate=%d st=%d sa1=%d sa2=%d\n",
		        coop_credit_is_shared() ? 1 : 0, coop_earnings_are_doubled() ? 1 : 0,
		        arcade_separate_mode() ? 1 : 0, superTyrian ? 1 : 0,
		        (int)player[0].items.super_arcade_mode, (int)player[1].items.super_arcade_mode);
		fflush(stderr);
	}
}

void JE_drawNetworkNotice(const char *text)
{
	// Drawn into VGAScreenSeg, so this is display space: the playfield is x in [0,
	// PLAYFIELD_WIDTH) and y in [0, 184), with the HUD to the right of it.
	const int playfieldRows = 184;
	const int panelW = MIN(JE_textWidth(text, normal_font) + 28, PLAYFIELD_WIDTH - 16);
	const int panelH = 26;

	const int px0 = (PLAYFIELD_WIDTH - panelW) / 2, px1 = px0 + panelW;
	const int py0 = (playfieldRows - panelH) / 2, py1 = py0 + panelH;

	JE_barShade(VGAScreen, px0, py0, px1, py1);
	JE_barShade(VGAScreen, px0 + 2, py0 + 2, px1 - 2, py1 - 2);
	JE_dStringOutlined(VGAScreen, (px0 + px1) / 2 - JE_textWidth(text, normal_font) / 2, py0 + 7,
	                   text, SMALL_FONT_SHAPES);
}
