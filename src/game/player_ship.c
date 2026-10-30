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
#include "player_ship.h"

#include "audio.h"
#include "background.h"
#include "config.h"
#include "console_platform.h"
#include "debug_flags.h"
#include "debug_menu.h"
#include "demo.h"
#include "draw.h"
#include "endless.h"
#include "enemy_death.h"
#include "episodes.h"
#include "explosions.h"
#include "extra_ships.h"
#include "file.h"
#include "font.h"
#include "game_loop.h"
#include "game_state.h"
#include "health_bars.h"
#include "help.h"
#include "helptext.h"
#include "hud.h"
#include "ingame_menu.h"
#include "joystick.h"
#include "keyboard.h"
#include "lvlmast.h"
#include "mouse.h"
#include "mtrand.h"
#include "musmast.h"
#include "net_rollback.h"
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
#include "shots.h"
#include "sim_math.h"
#include "sndmast.h"
#include "sparks.h"
#include "specials.h"
#include "sprite.h"
#include "timing.h"
#include "video.h"
#include "vt_ship.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool button[4];
bool pause_pressed = false, ingamemenu_pressed = false, changefire_pressed = false;
JE_shortint constantLastX;

static const JE_word SASpecialWeaponB[SA] /* [1..SA] */ = {37, 6, 15, 40, 16, 14, 41, 48, 47 };

static const JE_byte specialArcadeWeapon[PORT_NUM] /* [1..Portnum] */ =
{
	17,17,18,0,0,0,10,0,0,0,0,0,44,0,10,0,19,0,0,-0,0,0,0,0,0,0,
	-0,0,0,0,45,0,0,0,0,0,0,0,0,0,0,0
};

static const JE_byte optionSelect[16][3][2] /* [0..15, 1..3, 1..2] */ =
{	/*  MAIN    OPT    FRONT */
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ { 1, 1},{16,16},{30,30} },  /*Single Shot*/
	{ { 2, 2},{29,29},{29,20} },  /*Dual Shot*/
	{ { 3, 3},{21,21},{12, 0} },  /*Charge Cannon*/
	{ { 4, 4},{18,18},{16,23} },  /*Vulcan*/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ { 6, 6},{29,16},{ 0,22} },  /*Super Missile*/
	{ { 7, 7},{19,19},{19,28} },  /*Atom Bomb*/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ {10,10},{21,21},{21,27} },  /*Mini Missile*/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ {13,13},{17,17},{13,26} },  /*MicroBomb*/
	{ { 0, 0},{ 0, 0},{ 0, 0} },  /**/
	{ {15,15},{15,16},{15,16} }   /*Post-It*/
};

const JE_word linkGunWeapons[38] /* [1..38] */ =
{
	0,0,0,0,0,0,0,0,444,445,446,447,0,448,449,0,0,0,0,0,450,451,0,506,0,564,
	  445,446,447,448,449,445,446,447,448,449,450,451
};

static const JE_word chargeGunWeapons[38] /* [1..38] */ =
{
	0,0,0,0,0,0,0,0,476,458,464,482,0,488,470,0,0,0,0,0,494,500,0,528,0,558,
	  458,458,458,458,458,458,458,458,458,458,458,458
};

/* YKS: Twiddle cheat sheet:
 * 1: UP
 * 2: DOWN
 * 3: LEFT
 * 4: RIGHT
 * 5: UP+FIRE
 * 6: DOWN+FIRE
 * 7: LEFT+FIRE
 * 8: RIGHT+FIRE
 * 9: Release all keys (directions and fire)
 */
const JE_byte keyboardCombos[26][8] /* [1..26, 1..8] */ =
{
	{ 2, 1,   2,   5, 137,           0, 0, 0}, /*Invulnerability*/
	{ 4, 3,   2,   5, 138,           0, 0, 0}, /*Atom Bomb*/
	{ 3, 4,   6, 139,             0, 0, 0, 0}, /*Seeker Bombs*/
	{ 2, 5, 142,               0, 0, 0, 0, 0}, /*Ice Blast*/
	{ 6, 2,   6, 143,             0, 0, 0, 0}, /*Auto Repair*/
	{ 6, 7,   5,   8,   6,   7,  5, 112     }, /*Spin Wave*/
	{ 7, 8, 101,               0, 0, 0, 0, 0}, /*Repulsor*/
	{ 1, 7,   6, 146,             0, 0, 0, 0}, /*Protron Field*/
	{ 8, 6,   7,   1, 120,           0, 0, 0}, /*Minefield*/
	{ 3, 6,   8,   5, 121,           0, 0, 0}, /*Post-It Blast*/
	{ 1, 2,   7,   8, 119,           0, 0, 0}, /*Drone Ship - TBC*/
	{ 3, 4,   3,   6, 123,           0, 0, 0}, /*Repair Player 2*/
	{ 6, 7,   5,   8, 124,           0, 0, 0}, /*Super Bomb - TBC*/
	{ 1, 6, 125,               0, 0, 0, 0, 0}, /*Hot Dog*/
	{ 9, 5, 126,               0, 0, 0, 0, 0}, /*Lightning UP      */
	{ 1, 7, 127,               0, 0, 0, 0, 0}, /*Lightning UP+LEFT */
	{ 1, 8, 128,               0, 0, 0, 0, 0}, /*Lightning UP+RIGHT*/
	{ 9, 7, 129,               0, 0, 0, 0, 0}, /*Lightning    LEFT */
	{ 9, 8, 130,               0, 0, 0, 0, 0}, /*Lightning    RIGHT*/
	{ 4, 2,   3,   5, 131,           0, 0, 0}, /*Warfly            */
	{ 3, 1,   2,   8, 132,           0, 0, 0}, /*FrontBlaster      */
	{ 2, 4,   5, 133,             0, 0, 0, 0}, /*Gerund            */
	{ 3, 4,   2,   8, 134,           0, 0, 0}, /*FireBomb          */
	{ 1, 4,   6, 135,             0, 0, 0, 0}, /*Indigo            */
	{ 1, 3,   6, 137,             0, 0, 0, 0}, /*Invulnerability [easier] */
	{ 1, 4,   3,   4,   7, 136,         0, 0}  /*D-Media Protron Drone    */
};

static const JE_byte shipCombosB[21] /* [1..21] */ =
	{15,16,17,18,19,20,21,22,23,24, 7, 8, 5,25,14, 4, 6, 3, 9, 2,26};

const JE_byte shipCombos[19][3] /* [0..12, 1..3] */ =
{
	{ 5, 4, 7},  /*2nd Player ship*/
	{ 1, 2, 0},  /*USP Talon*/
	{14, 4, 0},  /*Super Carrot*/
	{ 4, 5, 0},  /*Gencore Phoenix*/
	{ 6, 5, 0},  /*Gencore Maelstrom*/
	{ 7, 8, 0},  /*MicroCorp Stalker*/
	{ 7, 9, 0},  /*MicroCorp Stalker-B*/
	{10, 3, 5},  /*Prototype Stalker-C*/
	{ 5, 8, 9},  /*Stalker*/
	{ 1, 3, 0},  /*USP Fang*/
	{ 7,16,17},  /*U-Ship*/
	{ 2,11,12},  /*1st Player ship*/
	{ 3, 8,10},  /*Nort ship*/
	{ 0, 0, 0},  // Dummy entry added for Stalker 21.126
	{ 1, 0, 0},  /*Storm*/
	{ 4, 0, 0},  /*Red Dragon*/
	{ 5, 9, 2},  /*Gencore II*/
	{ 0, 0, 0},  /*PeteZoomer*/
	{ 0, 0, 0}   /*Rum Bottle*/
};

/*Street-Fighter Commands*/
JE_byte SFCurrentCode[2][21]; /* [1..2, 1..21] */
JE_byte SFExecuted[2]; /* [1..2] */

const int shadowYDist = 10;

JE_real optionSatelliteRotate;

JE_integer optionAttachmentMove[2];                               // per sidekick slot (LEFT/RIGHT)
JE_boolean optionAttachmentLinked[2], optionAttachmentReturn[2];  // so both front options can launch

JE_byte chargeWait, chargeLevel, chargeMax, chargeGr, chargeGrWait;

/*Street Fighter codes*/
void JE_SFCodes(JE_byte playerNum_, JE_integer PX_, JE_integer PY_, JE_integer mouseX_, JE_integer mouseY_)
{
	JE_byte temp, temp2, temp3, temp4, temp5;

	uint ship = player[playerNum_-1].items.ship;

	/* Linked Dragonwing uses the shared player-two combo row; full ships use their own. */
	if (playerNum_ == 2 && !dual_ship_mode())
	{
		ship = 0;
	}

	// A Dragonwing bought at the Endless outpost twiddles off that same shared row.
	if (ship == SHIP_DRAGONWING)
	{
		ship = 0;
	}

	// SuperTyrian hands one ship every combo; otherwise the ship needs a row of its own, which a
	// shipedit "extra" ship (id above 90) has not got.
	if (!superTyrian && ship >= COUNTOF(shipCombos))
		return;

	/*Get direction*/
	temp2 = (mouseY_ > PY_) +    /*UP*/
	        (mouseY_ < PY_) +    /*DOWN*/
	        (PX_ < mouseX_) +    /*LEFT*/
	        (PX_ > mouseX_);     /*RIGHT*/
	temp = (mouseY_ > PY_) * 1 + /*UP*/
	       (mouseY_ < PY_) * 2 + /*DOWN*/
	       (PX_ < mouseX_) * 3 + /*LEFT*/
	       (PX_ > mouseX_) * 4;  /*RIGHT*/

	if (temp == 0) // no direction being pressed
	{
		if (!button[0]) // if fire button is released
		{
			temp = 9;
			temp2 = 1;
		}
		else
		{
			temp2 = 0;
			temp = 99;
		}
	}

	if (temp2 != 1) // more than one direction pressed: neither advance nor cancel
		return;

	temp += button[0] * 4;

	temp3 = superTyrian ? 21 : 3;
	for (temp2 = 0; temp2 < temp3; temp2++)
	{
		/*Use SuperTyrian ShipCombos or not?*/
		temp5 = superTyrian ? shipCombosB[temp2] : shipCombos[ship][temp2];

		// temp5 == selected combo in ship
		if (temp5 == 0) /* combo doesn't exists */
		{
			// mark twiddles as cancelled/finished
			SFCurrentCode[playerNum_-1][temp2] = 0;
		}
		else
		{
			// get next combo key
			temp4 = keyboardCombos[temp5-1][SFCurrentCode[playerNum_-1][temp2]];

			// correct key
			if (temp4 == temp)
			{
				SFCurrentCode[playerNum_-1][temp2]++;

				temp4 = keyboardCombos[temp5-1][SFCurrentCode[playerNum_-1][temp2]];
				if (temp4 > 100 && temp4 <= 100 + SPECIAL_NUM)
				{
					SFCurrentCode[playerNum_-1][temp2] = 0;
					SFExecuted[playerNum_-1] = temp4 - 100;
				}
			}
			else
			{
				/* Wrong input resets the combo; held accepted input and neutral input do not. */
				if (temp != 9 &&
				    (SFCurrentCode[playerNum_-1][temp2] == 0 ||
				     keyboardCombos[temp5-1][SFCurrentCode[playerNum_-1][temp2]-1] != temp))
				{
					SFCurrentCode[playerNum_-1][temp2] = 0;
				}
			}
		}
	}
}

/* Collapse movement into the 2:1 twiddle cone. Shallow diagonals remain neutral; Topsy Turvy
 * mirrors horizontal intent here. See doc/notes.md#twiddles-and-specials. */
void SF_twiddleTarget(int px, int py, int dx, int dy, int *out_x, int *out_y)
{
	if (smoothies[9-1])
		dx = -dx;

	if (abs(dx) > 2 * abs(dy))
		dy = 0;
	else if (abs(dy) > 2 * abs(dx))
		dx = 0;

	*out_x = px - (dx > 0 ? 1 : dx < 0 ? -1 : 0);
	*out_y = py - (dy > 0 ? 1 : dy < 0 ? -1 : 0);
}

/* Index local lockstep history by packet sequence. */
#define NET_OWN_RING 16  // >= NET_PACKET_QUEUE, so an entry outlives the queue that names it

static Sint16 net_own_x[NET_OWN_RING], net_own_y[NET_OWN_RING], net_own_pull[NET_OWN_RING];
static Uint16 net_own_buttons[NET_OWN_RING];

static void net_own_state_store(Uint16 sync, int x, int y, Uint16 buttons, int pull)
{
	const unsigned slot = (unsigned)sync % NET_OWN_RING;
	net_own_x[slot] = (Sint16)x;
	net_own_y[slot] = (Sint16)y;
	net_own_buttons[slot] = buttons;
	net_own_pull[slot] = (Sint16)pull;
}

static void net_own_state_load(Uint16 sync, int *x, int *y, Uint16 *buttons, int *pull)
{
	const unsigned slot = (unsigned)sync % NET_OWN_RING;
	*x = net_own_x[slot];
	*y = net_own_y[slot];
	*buttons = net_own_buttons[slot];
	*pull = net_own_pull[slot];
}

void JE_mainKeyboardInput(void)
{
	JE_gammaCheck();

	/* { Network Request Commands } */

	if (!isNetworkGame)
	{
		/* { Edited Ships } for Player 1 */
		if (extraAvail && extraShipsAllowedInGame() && keysactive[SDL_SCANCODE_TAB])
		{
			for (x = SDL_SCANCODE_1; x <= SDL_SCANCODE_0; x++)
			{
				if (keysactive[x])
				{
					rollback_taint("edit-ship-1");
					if (extraShipEquip(0, x - SDL_SCANCODE_1 + 1))
						extraShipLoadoutRefresh(0, true);

					keysactive[x] = false;
				}
			}
		}
	}

	/* { In-Game Help } */
	if (keysactive[SDL_SCANCODE_F1])
	{
		if (isNetworkGame)
		{
			helpRequest = true;
		}
		else
		{
			JE_inGameHelp();
			skipStarShowVGA = true;
		}
	}

	/* {!Activate Nort Ship!} */
	if (keysactive[SDL_SCANCODE_F2] && keysactive[SDL_SCANCODE_F4] && keysactive[SDL_SCANCODE_F6] && keysactive[SDL_SCANCODE_F7] &&
	    keysactive[SDL_SCANCODE_F9] && keysactive[SDL_SCANCODE_BACKSLASH] && keysactive[SDL_SCANCODE_SLASH])
	{
		if (isNetworkGame)
		{
			nortShipRequest = true;
		}
		else
		{
			rollback_taint("nort-ship");
			player[0].items.ship = 12;                     // Nort Ship
			player[0].items.special = 13;                  // Astral Zone
			player[0].items.weapon[FRONT_WEAPON].id = 36;  // NortShip Super Pulse
			player[0].items.weapon[REAR_WEAPON].id = 37;   // NortShip Spreader
			shipGr = 1;
		}
	}

	/* {Cheating} */
	if (!isNetworkGame && !twoPlayerMode && !superTyrian && superArcadeMode == SA_NONE)
	{
		if (keysactive[SDL_SCANCODE_F2] && keysactive[SDL_SCANCODE_F3] && keysactive[SDL_SCANCODE_F6])
		{
			rollback_taint("cheat-toggle");
			youAreCheating = !youAreCheating;
			keysactive[SDL_SCANCODE_F2] = false;
		}

		if (keysactive[SDL_SCANCODE_F2] && keysactive[SDL_SCANCODE_F3] && (keysactive[SDL_SCANCODE_F4] || keysactive[SDL_SCANCODE_F5]))
		{
			rollback_taint("cheat-armor");
			for (uint i = 0; i < COUNTOF(player); ++i)
				player[i].armor = 0;

			youAreCheating = !youAreCheating;
			JE_drawTextWindow(miscText[63-1]);
		}

		if (constantPlay && keysactive[SDL_SCANCODE_C])
		{
			rollback_taint("cheat-constant");
			youAreCheating = !youAreCheating;
			keysactive[SDL_SCANCODE_C] = false;
		}
	}

	if (superTyrian)
	{
		youAreCheating = false;
	}

	/* {Personal Commands} */

	/* {DEBUG} */
	if (keysactive[SDL_SCANCODE_F10] && keysactive[SDL_SCANCODE_BACKSPACE])
	{
		keysactive[SDL_SCANCODE_F10] = false;
		debug = !debug;

		debugHist = 1;
		debugHistCount = 1;

		/* YKS: clock ticks since midnight replaced by SDL_GetTicks */
		lastDebugTime = SDL_GetTicks();
	}

	/* {CHEAT-SKIP LEVEL} */
	if (keysactive[SDL_SCANCODE_F2] && keysactive[SDL_SCANCODE_F6] && (keysactive[SDL_SCANCODE_F7] || keysactive[SDL_SCANCODE_F8]) && !keysactive[SDL_SCANCODE_F9] &&
	    !superTyrian && superArcadeMode == SA_NONE)
	{
		if (isNetworkGame)
		{
			skipLevelRequest = true;
		}
		else
		{
			rollback_taint("skip-level");
			levelTimer = true;
			levelTimerCountdown = 0;
			endLevel = true;
			levelEnd = 40;
		}
	}

	/* pause game */
	pause_pressed = pause_pressed || keysactive[SDL_SCANCODE_P];

	/* in-game setup */
	ingamemenu_pressed = ingamemenu_pressed || keysactive[SDL_SCANCODE_ESCAPE];

	if (keysactive[SDL_SCANCODE_BACKSPACE])
	{
		/* toggle screenshot pause */
		if (keysactive[SDL_SCANCODE_NUMLOCKCLEAR])
			superPause = !superPause;

		/* {SMOOTHIES} */
		if (!isNetworkGame && keysactive[SDL_SCANCODE_F12] && keysactive[SDL_SCANCODE_SCROLLLOCK])
		{
			rollback_taint("smoothies-key");
			for (temp = SDL_SCANCODE_2; temp <= SDL_SCANCODE_9; temp++)
				if (keysactive[temp])
					smoothies[temp-SDL_SCANCODE_2] = !smoothies[temp-SDL_SCANCODE_2];
			if (keysactive[SDL_SCANCODE_0])
				smoothies[8] = !smoothies[8];
		}
		else

		/* {CYCLE THROUGH FILTER COLORS} */
		if (!isNetworkGame && keysactive[SDL_SCANCODE_MINUS])
		{
			rollback_taint("filter-key");
			if (levelFilter == -99)
			{
				levelFilter = 0;
			}
			else
			{
				levelFilter++;
				if (levelFilter == 16)
					levelFilter = -99;
			}
		}
		else

		/* {HYPER-SPEED} */
		if (!isNetworkGame && keysactive[SDL_SCANCODE_1])
		{
			rollback_taint("hyper-speed");
			fastPlay++;
			if (fastPlay > 2)
				fastPlay = 0;
			keysactive[SDL_SCANCODE_1] = false;
			JE_setNewGameSpeed();
		}

		/* {IN-GAME RANDOM MUSIC SELECTION} */
		if (keysactive[SDL_SCANCODE_SCROLLLOCK])
			play_random_song();
	}
}

void JE_pauseGame(void)
{
	// Pause is offline-only. It halts this machine alone, so online it would strand the
	// other player; this guard holds whatever route the call arrived by.
	if (isNetworkGame)
		return;

	// A modal UI mid-tick makes the tick non-replayable; skip self-test verify.
	rollback_taint("pause");

	// The pause overlay presents frames while the tick's render-list recording is
	// active; suspend it (see JE_doInGameSetup).
	const bool rl_was_recording = render_list_recording;
	render_list_recording = false;

	mouseSetRelative(false);
	joystick_rumble_stop_all();

	JE_boolean done = false;
	JE_word mouseX, mouseY;

	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */

	JE_repaintShieldArmorBars();

	if (!superPause)
	{
		JE_dString(VGAScreenSeg, 120, 90, miscText[22], FONT_SHAPES);

		VGAScreen = VGAScreenSeg;
		if (!present_overlay_on_last_hi_frame())
			JE_showVGA();
	}

	set_volume(tyrMusicVolume / 2, fxVolume);

	wait_noinput(false, false, true); // debounce before the next input loop

	do
	{
		setDelay(2);

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		if ((newkey && lastkey_scan != SDL_SCANCODE_LCTRL && lastkey_scan != SDL_SCANCODE_RCTRL && lastkey_scan != SDL_SCANCODE_LALT && lastkey_scan != SDL_SCANCODE_RALT) ||
			JE_mousePosition(&mouseX, &mouseY) > 0)
		{
			done = true;
		}

		wait_delay();
	} while (!done);

	set_volume(tyrMusicVolume, fxVolume);

	VGAScreen = temp_surface; /* side-effect of game_screen */

	mouseSetRelative(true);

	render_list_recording = rl_was_recording;
}

// Player seat currently being drawn by JE_playerMovement.
static uint ship_draw_seat = 0;

// Combine the online style with kill-fire tint; kill-fire tint wins.
static NetShipStyle ship_draw_tinted(NetShipStyle style)
{
	const int tint = endlessShipTintFilter();
	if (tint)
		style.bank = (Sint8)(tint >> 4);
	return style;
}

static NetShipStyle ship_draw_style(void)
{
	return ship_draw_tinted(netStyleForSeat(ship_draw_seat));
}

// Shared hull and trim blits preserve the caller's render-list tag.
static void blit_ship2x2(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	const NetShipStyle style = ship_draw_style();
	blit_sprite2x2_alpha(surface, x, y, sheet, index, style.bank, style.opacity);
}

static void blit_ship2(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	const NetShipStyle style = ship_draw_style();
	blit_sprite2_alpha(surface, x, y, sheet, index, style.bank, style.opacity);
}

// Faded ships omit shadows because shadows cannot share sprite opacity.
static bool ship_draw_casts_shadow(void)
{
	return netStyleForSeat(ship_draw_seat).opacity >= NET_STYLE_SOLID;
}

// Hull indicators fade with the ship but keep their original bank.
static void blit_ship_indicator2(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	blit_sprite2_alpha(surface, x, y, sheet, index, -1, netStyleForSeat(ship_draw_seat).opacity);
}

// Damage and noclip blends ignore drive tint and halve the online opacity.
static void blit_ship2x2_blend(SDL_Surface *surface, int x, int y, Sprite2_array sheet, unsigned int index)
{
	const NetShipStyle style = netStyleForSeat(ship_draw_seat);
	if (netStyleIsPlain(style))
		blit_sprite2x2_blend(surface, x, y, sheet, index);
	else
		blit_sprite2x2_alpha(surface, x, y, sheet, index, style.bank, (Uint8)(style.opacity / 2));
}

// Resting/home position for a front-mounted (style 2) option.
static int front_option_home_x(const Player *this_player, uint i)
{
	const bool both = this_player->sidekick[LEFT_SIDEKICK].style == 2
	               && this_player->sidekick[RIGHT_SIDEKICK].style == 2;
	if (!both)
		return this_player->x;
	return (i == LEFT_SIDEKICK) ? this_player->x - FRONT_OPTION_SPREAD
	                            : this_player->x + FRONT_OPTION_SPREAD;
}

// Front-mounted (launchable) option physics for one sidekick slot.
static void JE_frontOption(Player *this_player, uint i, int home_x, JE_boolean launch_pressed)
{
	int temp;

	if (!optionAttachmentLinked[i])
	{
		this_player->sidekick[i].y += optionAttachmentMove[i] / 2;
		if (optionAttachmentMove[i] >= -2)
		{
			if (optionAttachmentReturn[i])
				temp = 2;
			else
				temp = 0;

			if (this_player->sidekick[i].y > (this_player->y - 20) + 5)
			{
				temp = 2;
				optionAttachmentMove[i] -= 1 + optionAttachmentReturn[i];
			}
			else if (this_player->sidekick[i].y > (this_player->y - 20) - 0)
			{
				temp = 3;
				if (optionAttachmentMove[i] > 0)
					optionAttachmentMove[i]--;
				else
					optionAttachmentMove[i]++;
			}
			else if (this_player->sidekick[i].y > (this_player->y - 20) - 5)
			{
				temp = 2;
				optionAttachmentMove[i]++;
			}
			else if (optionAttachmentMove[i] < 2 + optionAttachmentReturn[i] * 4)
			{
				optionAttachmentMove[i] += 1 + optionAttachmentReturn[i];
			}

			if (optionAttachmentReturn[i])
				temp = temp * 2;
			if (abs(this_player->sidekick[i].x - home_x) < temp)
				temp = 1;

			if (this_player->sidekick[i].x > home_x)
				this_player->sidekick[i].x -= temp;
			else if (this_player->sidekick[i].x < home_x)
				this_player->sidekick[i].x += temp;

			if (abs(this_player->sidekick[i].y - (this_player->y - 20)) + abs(this_player->sidekick[i].x - home_x) < 8)
			{
				optionAttachmentLinked[i] = true;
				soundQueue[2] = S_CLINK;
			}

			if (launch_pressed)
				optionAttachmentReturn[i] = true;
		}
		else  // sidekick needs to catch up to player
		{
			optionAttachmentMove[i] += 1 + optionAttachmentReturn[i];
			JE_setupExplosion(this_player->sidekick[i].x + 1, this_player->sidekick[i].y + 10, 0, 0, false, false);
		}
	}
	else
	{
		this_player->sidekick[i].x = home_x;
		this_player->sidekick[i].y = this_player->y - 20;
		if (launch_pressed)
		{
			optionAttachmentLinked[i] = false;
			optionAttachmentReturn[i] = false;
			optionAttachmentMove[i] = -20;
			soundQueue[3] = S_WEAPON_26;
		}
	}

	if (this_player->sidekick[i].y < 10)
		this_player->sidekick[i].y = 10;
}

static Uint16 link_gun_angle_to_wire(float angle)
{
	while (angle < 0.0f)
		angle += (float)(2.0 * M_PI);
	while (angle >= (float)(2.0 * M_PI))
		angle -= (float)(2.0 * M_PI);

	// Quantized to 256 steps (~1.4 degrees): raw stick jitter in the low bits reads as a fresh
	// aim every tick and needlessly buys rollback corrections or delay-state churn.
	return (Uint16)(angle * (float)(65536.0 / (2.0 * M_PI))) & 0xFF00;
}

static float link_gun_angle_from_wire(Uint16 angle)
{
	return (float)angle * (float)(2.0 * M_PI / 65536.0);
}

/* Encode dominant movement and the twiddle cone in RB_MOVE_* bits. */
Uint16 rb_move_bits(int dx, int dy)
{
	Uint16 bits = 0;
	if (abs(dx) > abs(dy))
		bits |= (dx > 0) ? RB_MOVE_RIGHT : RB_MOVE_LEFT;
	else if (dy != 0)
		bits |= (dy > 0) ? RB_MOVE_DOWN : RB_MOVE_UP;

	int tx, ty;
	SF_twiddleTarget(0, 0, dx, dy, &tx, &ty);
	if (tx != 0 && ty != 0)
		bits |= RB_MOVE_DIAG;
	return bits;
}

/* The direction the twiddle detector rebuilds from those bits. Any two-axis target reads as
 * neutral, so a DIAG tick becomes one diagonal; which one does not matter. */
void rb_move_dir(Uint16 bits, int *out_dx, int *out_dy)
{
	*out_dx = (bits & RB_MOVE_RIGHT) ? 1 : (bits & RB_MOVE_LEFT) ? -1 : 0;
	*out_dy = (bits & RB_MOVE_DOWN) ? 1 : (bits & RB_MOVE_UP) ? -1 : 0;
	if (bits & RB_MOVE_DIAG)
	{
		*out_dx = 1;
		*out_dy = 1;
	}
}

/* Capture the complete post-movement input tuple used by rollback. */
static void rb_fill_tuple(RbInput *in, const Player *this_player,
                          JE_word mx, JE_word my,
                          JE_integer accelXC, JE_integer accelYC,
                          int pullX, int pullY,
                          bool link_analog, float link_angle)
{
	memset(in, 0, sizeof(*in));
	in->x = (Sint16)this_player->x;
	in->y = (Sint16)this_player->y;
	in->velX = (Sint16)(this_player->x_velocity > 127 ? 127 :
	                    (this_player->x_velocity < -127 ? -127 : this_player->x_velocity));
	in->velY = (Sint16)(this_player->y_velocity > 127 ? 127 :
	                    (this_player->y_velocity < -127 ? -127 : this_player->y_velocity));
	in->mouseX = (Sint16)mx;
	in->mouseY = (Sint16)my;
	/* The wire carries one signed byte per axis; the sim never produces more. */
	in->accelX = (Sint16)(accelXC > 127 ? 127 : (accelXC < -127 ? -127 : accelXC));
	in->accelY = (Sint16)(accelYC > 127 ? 127 : (accelYC < -127 ? -127 : accelYC));
	in->pullX = (Sint16)(pullX > 127 ? 127 : (pullX < -127 ? -127 : pullX));

	Uint16 buttons = 0;
	for (int i = 4 - 1; i >= 0; i--)
	{
		buttons <<= 1;
		buttons |= button[i] ? 1 : 0;
	}
	in->buttons = buttons;

	// Movement intent for the docked-link tests and the twiddle detector: this tick's real input
	// displacement, captured before the dock pin rewrites x/y.
	in->buttons |= rb_move_bits((int)in->x - (int)in->mouseX - pullX,
	                            (int)in->y - (int)in->mouseY - pullY);

	if (link_analog)
	{
		in->buttons |= RB_LINK_ANALOG;
		in->linkAngle = link_gun_angle_to_wire(link_angle);
	}
}

static void rb_apply_tuple(const RbInput *in, Player *this_player,
                           JE_integer *accelXC_, JE_integer *accelYC_, int *pullX_,
                           bool *link_analog, float *link_angle)
{
	Uint16 buttons = in->buttons;
	for (int i = 0; i < 4; i++)
	{
		button[i] = buttons & 1;
		buttons >>= 1;
	}
	this_player->x = in->x;
	this_player->y = in->y;
	this_player->x_velocity = (int)in->velX;
	this_player->y_velocity = (int)in->velY;
	*accelXC_ = (JE_integer)in->accelX;
	*accelYC_ = (JE_integer)in->accelY;
	*pullX_ = (int)in->pullX;
	*link_analog = (in->buttons & RB_LINK_ANALOG) != 0;
	*link_angle = link_gun_angle_from_wire(in->linkAngle);
}

/* Repaint the displayed ship's ammo gauge. Silent rollback passes set the HUD dirty instead of
 * drawing predicted values. */
static void JE_drawSidekickAmmoGauge(JE_byte playerNum, uint slot, int ammo, int ammo_max, bool wipe)
{
	if ((uint)(playerNum - 1) != hud_sidekick_player_index())
		return;

	if (rollback_resim_silent)
	{
		hud_sidekicks_dirty = true;
		return;
	}

	const int y = hud_sidekick_ammo_y(slot);
	const int hud_x = HUD_X(284);

	if (wipe)
		fill_rectangle_xy(VGAScreenSeg, hud_x, y, hud_x + 28, y + 2, 0);
	draw_segmented_gauge(VGAScreenSeg, hud_x, y, 112, 2, 2, AMMO_GAUGE_STEP(ammo_max), ammo);
}

/* Apply ship-owned Endless effects. Co-op calls this for both ships; other modes keep the
 * player-one effect owner. */
static bool endless_ship_effects_apply(const Player *this_player)
{
	return endlessFxActive() && (coopEndlessMode || this_player == &player[0]);
}

void endlessPerShipTick(Player *this_player)
{
	if (!endless_ship_effects_apply(this_player))
		return;

	// the VT ship (normal play) applies gravity in vt_ship_step
	if (!vt_ship_owns() && !isNetworkGame)
	{
		// X is nonzero only for an omnidirectional well; both axes are clamped to the playfield
		// at the end of JE_playerMovement, so a sideways/up pull just pins the ship at that edge.
		const uint p = (uint)(this_player - player);
		this_player->x += endlessGravityPullX(p);
		this_player->y += endlessGravityPullY(p);
	}

	// Rapid Cyclers and Turbodrive both reduce the firing delay. Let the counter
	// reach zero so stacked bonuses can fire once per tick.
	const int dec = endlessPerkFireDecrements()
	              + (endlessTurbodriveActive() ? endlessKillBuffFireDecrements() : 0);
	for (unsigned i = 0; i < COUNTOF(shotRepeat); i++)
		for (int k = 0; k < dec && shotRepeat[i] > 0; k++)
			--shotRepeat[i];

	// Rapid Recharge perk: extra decrements to the special cooldown gate + each sidekick's
	// ammo-refill counter (skips main guns). The accumulator is stateful and read exactly once.
	const int specDec = endlessPerkSpecialCooldownDecrements();
	for (int k = 0; k < specDec && shotRepeat[SHOT_SPECIAL] > 0; k++)
		--shotRepeat[SHOT_SPECIAL];

	for (uint i = 0; i < COUNTOF(this_player->sidekick); i++)
	{
		if (this_player->sidekick[i].ammo_max <= 0)
			continue;  // only weapons that actually use recharging ammo
		for (int k = 0; k < specDec && this_player->sidekick[i].ammo_refill_ticks > 0; k++)
			--this_player->sidekick[i].ammo_refill_ticks;
	}
}

/* Spend a charged Opening Salvo on the volley this ship is about to fire, and answer whether
 * one was there. See doc/notes.md#perks. */
bool endlessArmOpeningSalvoForTick(Player *this_player, JE_byte playerNum_)
{
	if (!endlessFxActive() || !(coopEndlessMode || this_player == &player[0]))
		return false;
	if (twoPlayerLinked && playerNum_ == 2)
		return false;
	if (twoPlayerMode && !dual_ship_mode() && playerNum_ != 1)
		return false;
	if (this_player->items.weapon[SHOT_FRONT].id == 0 || shotRepeat[SHOT_FRONT] > 0 || !button[1-1])
		return false;

	return endlessOpeningSalvoConsume();
}

static int ship_axis_velocity(int velocity, bool frictionDue, int accel)
{
	if (frictionDue)
	{
		if (velocity < 0)
			++velocity;
		else if (velocity > 0)
			--velocity;
	}
	velocity += accel;
	return MIN(MAX(-4, velocity), 4);
}

static float shipMoveCarry[2][2];
static float mouseMoveCarry[2][2];

void player_ship_reset_move_carry(void)
{
	memset(shipMoveCarry, 0, sizeof(shipMoveCarry));
	memset(mouseMoveCarry, 0, sizeof(mouseMoveCarry));
}

bool player_ship_move_carry_reset_self_test(void)
{
	for (unsigned seat = 0; seat < 2; ++seat)
		for (unsigned axis = 0; axis < 2; ++axis)
		{
			shipMoveCarry[seat][axis] = 0.25f + (float)(seat + axis);
			mouseMoveCarry[seat][axis] = -0.5f - (float)(seat + axis);
		}
	player_ship_reset_move_carry();

	bool cleared = true;
	for (unsigned seat = 0; seat < 2; ++seat)
		for (unsigned axis = 0; axis < 2; ++axis)
			cleared = cleared && shipMoveCarry[seat][axis] == 0.0f &&
			          mouseMoveCarry[seat][axis] == 0.0f;
	return cleared;
}

static int ship_scaled_axis(int start, int pos, int momentum, float scale, float *carry)
{
	const float want = (float)(pos - start + momentum) * scale + *carry;
	const int moved = (int)(want >= 0.0f ? want + 0.5f : want - 0.5f);
	*carry = want - (float)moved;
	return start + moved - momentum;
}

int player_ship_banking(bool vt, bool velocity_synced, int x_velocity, int moved_x, int pull_x)
{
	int banking;
	if (vt)
	{
		// Bank from momentum like the classic path: a tick's displacement varies with how many
		// display frames it spans. Mouse and touch steering add no momentum and bank from the
		// displacement, as does Delay-Based netplay, whose wire carries no velocity.
		if (velocity_synced && x_velocity != 0)
			banking = x_velocity / 2;
		else
			banking = (moved_x - pull_x) / 2;
	}
	else
	{
		banking = x_velocity / 2 + (moved_x - pull_x) / 6;
	}
	return MAX(-2, MIN(banking, 2));
}

bool player_ship_move_scale_self_test(void)
{
	static const float scales[] = { 1.0f, 0.45f, 1.5f };
	enum { TICKS = 200 };

	bool ok = true;
	for (unsigned s = 0; s < COUNTOF(scales); ++s)
	{
		int plainX = 0, plainV = 0, scaledX = 0, scaledV = 0;
		unsigned plainFriction = 0, scaledFriction = 0;
		float carry = 0.0f;
		for (int tick = 0; tick < TICKS; ++tick)
		{
			const bool plainDue = plainFriction == 0;
			plainFriction = plainDue ? 1 : plainFriction - 1;
			plainV = ship_axis_velocity(plainV, plainDue, CURRENT_KEY_SPEED);
			plainX += CURRENT_KEY_SPEED + plainV;

			const bool scaledDue = scaledFriction == 0;
			const int momentum = ship_axis_velocity(scaledV, scaledDue, CURRENT_KEY_SPEED);
			const int live = ship_scaled_axis(scaledX, scaledX + CURRENT_KEY_SPEED, momentum,
			                                  scales[s], &carry);
			scaledFriction = scaledDue ? 1 : scaledFriction - 1;
			scaledV = ship_axis_velocity(scaledV, scaledDue, CURRENT_KEY_SPEED);
			scaledX = live + scaledV;
		}
		const int want = (int)((float)plainX * scales[s] + 0.5f);
		ok = ok && scaledX >= want - 1 && scaledX <= want + 1;
	}
	return ok;
}

static void JE_playerMovement(Player *this_player,
                       JE_byte inputDevice,
                       JE_byte playerNum_,
                       JE_word shipGr_,
                       Sprite2_array *shipGrPtr_,
                       JE_word *mouseX_, JE_word *mouseY_)
{
	JE_integer mouseXC, mouseYC;
	JE_integer accelXC, accelYC;
	int pullX, pullY;

	// Everything from here to the ship blit is this ship's: its own drives, tint and cadence.
	endlessSetFxPlayer((uint)(this_player - &player[0]));
	ship_draw_seat = (uint)(this_player - &player[0]);

	Sprite2_array *look_sheet = shipGrPtr_;
	JE_word look_gr = shipGr_;
	shipSkinApply(ship_draw_seat, &look_sheet, &look_gr);

	if (playerNum_ == 2 || !twoPlayerMode || dual_ship_mode())
	{
		tempW = weaponPort[this_player->items.weapon[REAR_WEAPON].id].opnum;

		if (this_player->weapon_mode > tempW)
			this_player->weapon_mode = 1;
	}

	/* Run shared Endless hooks once; personal hooks run for each ship below. */
	if (endlessFxActive() && this_player == &player[0])
	{
		endlessGameplayTick();
		if (endlessConsumeArmorHudDirty())  // the Overheat DoT just shaved hull; repaint the event-driven armor bar
		{
			JE_wipeShieldArmorBars();
			VGAScreen = VGAScreenSeg;
			JE_drawShield();
			JE_drawArmor();
			VGAScreen = game_screen;
		}
	}

	endlessPerShipTick(this_player);

	// Lockstep state packets; rollback mode replaces them with the input stream.
	if (isNetworkGame && thisPlayerNum == playerNum_ && !nrb_active())
	{
		network_state_prepare();
		memset(&packet_state_out[0]->data[4], 0, 10);
	}

redo:

	if (isNetworkGame)
	{
		inputDevice = 0;
	}

	mouseXC = 0;
	mouseYC = 0;
	accelXC = 0;
	accelYC = 0;
	pullX = 0;
	pullY = 0;

	// Select the ship-physics path. Online peers use the host's choice; a docked Dragonwing still
	// follows the fixed-tick path that pins it to player 1.
	const bool vt_sim_owns = isNetworkGame
	                       ? (nrb_session_vt() && frameCountMax > 0 && !endLevel)
	                       : vt_ship_owns();
	const bool vt = vt_sim_owns && !(playerNum_ == 2 && twoPlayerLinked);
	// Local control settings shape only the tuple this machine records.
	const bool vt_input = vt_ship_owns() && !(playerNum_ == 2 && twoPlayerLinked);

	bool link_gun_analog = false;
	float link_gun_angle = 0;

	/* Draw Player */
	if (!this_player->is_alive)
	{
		if (this_player->exploding_ticks > 0)
		{
			--this_player->exploding_ticks;

			// The wreck is gone: the ship either respawns below or is out with empty gauges.
			// Both states need one repaint, and no other event guarantees it.
			if (this_player->exploding_ticks == 0)
				hud_bars_dirty = true;

			if (levelEndFxWait > 0)
			{
				levelEndFxWait--;
			}
			else
			{
				levelEndFxWait = (mt_rand() % 6) + 3;
				if ((mt_rand() % 3) == 1)
					soundQueue[6] = S_EXPLOSION_9;
				else
					soundQueue[5] = S_EXPLOSION_11;
			}

			int explosion_x = this_player->x + (mt_rand() % 32) - 16;
			int explosion_y = this_player->y + (mt_rand() % 32) - 16;
			JE_setupExplosionLarge(false, 0, explosion_x, explosion_y + 7);
			JE_setupExplosionLarge(false, 0, this_player->x, this_player->y + 7);

			if (levelEnd > 0)
				levelEnd--;
		}
		else
		{
			if (arcade_rules_active())
			{
				if (*this_player->lives > 1)  // respawn if any extra lives
				{
					--(*this_player->lives);
					// One life poorer, so both ceilings come back down before the refill below
					// re-derives the respawn armour and shield from them.
					arcade_rescale_to_lives(this_player);

					reallyEndLevel = false;
					shotMultiPos[playerNum_-1] = 0;
					calc_purple_balls_needed(this_player);
					twoPlayerLinked = false;
					// Galaga loses the spawned wing on a respawn; co-op's second ship is a
					// player, not a wing, so the pair stays a pair.
					if (galagaMode && !coop_mode_active())
						twoPlayerMode = false;
					this_player->y = SHIP_BOTTOM_MARGIN;
					this_player->invulnerable_ticks = 100;
					this_player->is_alive = true;
					endLevel = false;

					// Life-scaled arcade ships respawn with both gauges at their new ceilings.
					const bool boostedRefill = arcade_life_scaling_active();

					if (galagaMode || episodeNum == 4 || boostedRefill)
						this_player->armor = this_player->initial_armor;
					else
						this_player->armor = this_player->initial_armor / 2;

					if (galagaMode)
						this_player->shield = 0;  // galaga starts you shieldless, boost or not
					else if (boostedRefill)
						this_player->shield = this_player->shield_max;
					else
						this_player->shield = this_player->shield_max / 2;

					VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
					JE_drawArmor();
					JE_drawShield();
					VGAScreen = game_screen; /* side-effect of game_screen */
					goto redo;
				}
				else
				{
					if (galagaMode && !coop_mode_active())
						twoPlayerMode = false;
					if (allPlayersGone && isNetworkGame)
						reallyEndLevel = true;
				}

			}
		}
	}
	else if (constantDie)
	{
		// finished exploding?  start dying again
		if (this_player->exploding_ticks == 0)
		{
			this_player->shield = 0;

			if (this_player->armor > 0)
			{
				--this_player->armor;
			}
			else
			{
				this_player->is_alive = false;
				this_player->exploding_ticks = 60;
				if (coopEndlessMode)
					endlessPlayerDowned[this_player - &player[0]] = true;
				levelEnd = 40;
			}

			JE_wipeShieldArmorBars();
			VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
			JE_drawArmor();
			VGAScreen = game_screen; /* side-effect of game_screen */

			// Preserve the mode's repeating instant-death loop.
			if (*player[0].lives < ARCADE_LIVES_MAX)
			{
				++(*player[0].lives);
				arcade_rescale_to_lives(&player[0]);
			}
		}
	}

	if (!this_player->is_alive)
	{
		explosionFollowAmountX = explosionFollowAmountY = 0;
		return;
	}

	if (!endLevel)
	{
		*mouseX_ = this_player->x;
		*mouseY_ = this_player->y;
		// Snapshot classic-path movement before the Endless move scale rescales it.
		const int moveStartX = this_player->x;
		const int moveStartY = this_player->y;
		button[1-1] = false;
		button[2-1] = false;
		button[3-1] = false;
		button[4-1] = false;

		// Script live fire for the wire test; replay uses the recorded tuple.
		if (qa_net_gameplay_ticks > 0 && (!isNetworkGame || playerNum_ == thisPlayerNum)
		    && !rollback_resim && !endLevel)
		{
			button[0] = true;
			button[1] = ((nrb_frame() >> 4) & 1) != 0;
			button[2] = ((nrb_frame() >> 5) & 1) != 0;
		}

		// Use wire-carried movement intent in rollback and position deltas otherwise.
		Uint16 linkIntent = 0;
		bool   haveLinkIntent = false;

		/* Movement. */

		// Fold accumulated display-rate motion into the ship before netcode samples it.
		if (isNetworkGame && playerNum_ == thisPlayerNum && !rollback_resim)
			vt_ship_commit_net(playerNum_ - 1);

		// The live-sampling block never runs on a rollback/self-test replay pass:
		// a replayed tick takes its input from the recorded tuples instead.
		if ((!isNetworkGame || playerNum_ == thisPlayerNum) && !rollback_resim)
		{
			if (vt_input)
				vt_ship_take_pull(playerNum_ - 1, &pullX, &pullY);

			if (endLevel)
			{
				this_player->y -= 2;
			}
			else
			{
				if (record_demo || play_demo)
					inputDevice = 1;  // keyboard is required device for demo recording

				// demo playback input
				if (play_demo && playerNum_ == 1)
				{
					if (!replay_demo_keys())
					{
						endLevel = true;
						levelEnd = 40;
						// One-shot input event outside the movement tuple; recorded
						// so a self-test replay reproduces it.
						if (rollback_selftest_active())
							rollback_st_event(RB_EV_DEMO_END);
					}
				}

				/* joystick input */
				if ((inputDevice == 0 || inputDevice >= 3) && joysticks > 0)
				{
					int j = inputDevice  == 0 ? 0 : inputDevice - 3;
					int j_max = inputDevice == 0 ? joysticks : inputDevice - 3 + 1;
					for (; j < j_max; j++)
					{
						poll_joystick(j);

						if (joystick[j].analog)
						{
							mouseXC += joystick_axis_reduce(j, joystick[j].x);
							mouseYC += joystick_axis_reduce(j, joystick[j].y);

							link_gun_analog = joystick_analog_angle(j, &link_gun_angle);
						}
						else if (!vt_input)
						{
							this_player->x += (joystick[j].direction[3] ? -CURRENT_KEY_SPEED : 0) + (joystick[j].direction[1] ? CURRENT_KEY_SPEED : 0);
							this_player->y += (joystick[j].direction[0] ? -CURRENT_KEY_SPEED : 0) + (joystick[j].direction[2] ? CURRENT_KEY_SPEED : 0);
						}

						button[0] |= joystick[j].action[0];
						button[1] |= joystick[j].action[2];
						button[2] |= joystick[j].action[3];
						button[3] |= joystick[j].action_pressed[1];

						ingamemenu_pressed |= joystick[j].action_pressed[4];
						if (joystick_pause_opens_menu(j))
							ingamemenu_pressed |= joystick[j].action_pressed[5];
						else
							pause_pressed |= joystick[j].action_pressed[5];
					}
				}

				// Drain the render-rate controller/touch latch even when no controller is attached.
				button[3] |= changefire_pressed;
				changefire_pressed = false;

				service_SDL_events(false);

				// Classic mouse and touch movement changes position without adding momentum.
				int mouseDirectDX = 0, mouseDirectDY = 0;

				/* mouse input */
				if ((inputDevice == 0 || inputDevice == 2) && has_mouse)
				{
#ifdef PLATFORM_HANDHELD
					/* Touch auto-fire shares mouse_pressed[0]. Toggle Fire ignores it so drags
					 * neither shoot nor flip the latch; physical buttons still reach it. */
					if (!debugToggleFire)
						button[0] |= mouse_pressed[0];
#else
					button[0] |= mouse_pressed[0];
#endif
					button[1] |= mouse_pressed[1];
					button[2] |= mouse_pressed[2];
					button[3] |= mouse_pressed[3];

					if (!vt_input)
					{
						if (!isNetworkGame)
						{
							// Match VT mouse scaling and retain sub-pixel motion. The old
							// integer path lost slow movement and clipped fast flicks.
							float *const carry = mouseMoveCarry[playerNum_ >= 2 ? 1 : 0];
							float mxr, myr;
							mouseGetRelativeMotionF(&mxr, &myr);
							if (smoothies[9-1])
								myr = -myr;  // inverted-control levels, same as the VT path
							carry[0] += mxr * VT_MOUSE_SENS;
							carry[1] += myr * VT_MOUSE_SENS;
							mouseDirectDX = (int)carry[0];
							mouseDirectDY = (int)carry[1];
							carry[0] -= (float)mouseDirectDX;
							carry[1] -= (float)mouseDirectDY;
						}
						else
						{
							// Network lockstep: keep the original integer read feeding
							// the synced mouseXC pipeline.
							Sint32 mouseXR;
							Sint32 mouseYR;
							mouseGetRelativePosition(&mouseXR, &mouseYR);
							mouseXC += mouseXR;
							mouseYC += mouseYR;
						}
					}
				}

				/* keyboard input */
				if ((inputDevice == 0 || inputDevice == 1) && !play_demo)
				{
					if (!vt_input)
					{
						if (keysactive[keySettings[KEY_SETTING_UP]])
							this_player->y -= CURRENT_KEY_SPEED;
						if (keysactive[keySettings[KEY_SETTING_DOWN]])
							this_player->y += CURRENT_KEY_SPEED;

						if (keysactive[keySettings[KEY_SETTING_LEFT]])
							this_player->x -= CURRENT_KEY_SPEED;
						if (keysactive[keySettings[KEY_SETTING_RIGHT]])
							this_player->x += CURRENT_KEY_SPEED;
					}

					button[0] = button[0] || keysactive[keySettings[KEY_SETTING_FIRE]];
					button[3] = button[3] || keysactive[keySettings[KEY_SETTING_CHANGE_FIRE]];
					button[1] = button[1] || keysactive[keySettings[KEY_SETTING_LEFT_SIDEKICK]];
					button[2] = button[2] || keysactive[keySettings[KEY_SETTING_RIGHT_SIDEKICK]];

					if (constantPlay)
					{
						for (unsigned int i = 0; i < 4; i++)
							button[i] = true;

						++this_player->y;
						this_player->x += constantLastX;
					}

					// Record input transitions in the legacy demo stream.
					if (record_demo)
						record_demo_keys();
				}

				/* Debug Toggle Fire changes state only on a press edge. Console touch
				 * drag is excluded above and cannot toggle it. */
				if (debugToggleFire && playerNum_ == 1 && !play_demo && !record_demo)
				{
					static bool toggleFirePrevHeld = false;
					if (button[0] && !toggleFirePrevHeld)
						debugToggleFireActive = !debugToggleFireActive;
					toggleFirePrevHeld = button[0];
					button[0] = debugToggleFireActive;
				}

				if (smoothies[9-1])
				{
					// Mirror classic input only; display-rate input was already inverted.
					if (!vt_input)
						*mouseY_ = this_player->y - (*mouseY_ - this_player->y);
					mouseYC = -mouseYC;
				}

				accelXC += this_player->x - *mouseX_;
				accelYC += this_player->y - *mouseY_;

				if (mouseXC > 30)
					mouseXC = 30;
				else if (mouseXC < -30)
					mouseXC = -30;
				if (mouseYC > 30)
					mouseYC = 30;
				else if (mouseYC < -30)
					mouseYC = -30;

				if (!vt_input)
				{
					if (mouseXC > 0)
						this_player->x += (mouseXC + 3) / 4;
					else if (mouseXC < 0)
						this_player->x += (mouseXC - 3) / 4;
					if (mouseYC > 0)
						this_player->y += (mouseYC + 3) / 4;
					else if (mouseYC < 0)
						this_player->y += (mouseYC - 3) / 4;

					// Apply direct pointer movement after acceleration capture.
					this_player->x += mouseDirectDX;
					this_player->y += mouseDirectDY;
				}

				if (mouseXC > 3)
					accelXC++;
				else if (mouseXC < -2)
					accelXC--;
				if (mouseYC > 2)
					accelYC++;
				else if (mouseYC < -2)
					accelYC--;

				// Smooth-Motion-off Endless move scale: scale the completed movement, momentum
				// included, carrying fractions per ship so every input keeps one long-run rate.
				{
					const uint moveIdx = (playerNum_ >= 2) ? 1u : 0u;
					const float ms = vt_input ? 1.0f : endlessShipMoveScale(moveIdx);
					if (ms != 1.0f)
					{
						const bool xDue = this_player->x_friction_ticks == 0;
						const bool yDue = this_player->y_friction_ticks == 0;
						const int momentumX = vt ? 0 : ship_axis_velocity(this_player->x_velocity,
						                                                  xDue, accelXC);
						const int momentumY = vt ? 0 : ship_axis_velocity(this_player->y_velocity,
						                                                  yDue, accelYC);
						this_player->x = ship_scaled_axis(moveStartX, this_player->x, momentumX, ms,
						                                  &shipMoveCarry[moveIdx][0]);
						this_player->y = ship_scaled_axis(moveStartY, this_player->y, momentumY, ms,
						                                  &shipMoveCarry[moveIdx][1]);
					}
				}

				if (isNetworkGame && !vt_input && endless_ship_effects_apply(this_player))
				{
					endlessGravityInputPull((uint)(this_player - player), &pullX, &pullY);
					this_player->x += pullX;
					this_player->y += pullY;
				}

			}   /*endLevel*/

			/* Live-network regression for the delay packet's linked-gun fields. Player two supplies
			 * stable analog aim and alternating movement so the fused turret consumes both fields. */
			if (isNetworkGame && qa_net_scenario == 19 && playerNum_ == thisPlayerNum
			    && playerNum_ == 2 && !rollback_resim && !endLevel)
			{
				this_player->x = MIN(MAX(this_player->x + ((curLoc & 1) ? 2 : -2), 60), 240);
				button[0] = true;
				link_gun_analog = true;
				link_gun_angle = 1.25f;
			}

			if (isNetworkGame && playerNum_ == thisPlayerNum && nrb_active())
			{
				// Consume local input immediately and send it redundantly for the peer's
				// rollback timeline. Wire tests vary it to defeat held-input prediction.
				if (qa_net_gameplay_ticks > 0)
				{
					const int wiggle = ((nrb_frame() >> 3) & 1) ? 2 : -2;
					this_player->x = MIN(MAX(this_player->x + wiggle, 60), 240);
				}

				RbInput in;
				rb_fill_tuple(&in, this_player, *mouseX_, *mouseY_,
				              accelXC, accelYC, pullX, pullY, link_gun_analog, link_gun_angle);
				if (thisPlayerNum == networkHostPlayerNum)
					in.difficulty = (Uint8)difficultyLevel;  // host dictates
				nrb_record_local(&in);
				linkIntent = in.buttons;
				haveLinkIntent = true;

				// Apply the wire-quantized analog angle on both simulations.
				if (in.buttons & RB_LINK_ANALOG)
					link_gun_angle = link_gun_angle_from_wire(in.linkAngle);
			}
			else if (isNetworkGame && playerNum_ == thisPlayerNum)
			{
				Uint16 buttons = 0;
				for (int i = 4 - 1; i >= 0; i--)
				{
					buttons <<= 1;
					buttons |= button[i];
				}
				/* Linked Dragonwing control depends on movement intent. Deriving it from the
				 * final position and a private mouse anchor can produce different results. */
				buttons |= rb_move_bits(this_player->x - *mouseX_ - pullX,
				                        this_player->y - *mouseY_ - pullY);

				// Absolute positions are idempotent when a lost packet is reconstructed.
				SDLNet_Write16(this_player->x, &packet_state_out[0]->data[4]);
				SDLNet_Write16(this_player->y, &packet_state_out[0]->data[6]);
				SDLNet_Write16(accelXC,        &packet_state_out[0]->data[8]);
				SDLNet_Write16(accelYC,        &packet_state_out[0]->data[10]);
				SDLNet_Write16(buttons,        &packet_state_out[0]->data[12]);
				SDLNet_Write16(link_gun_analog ? 1 : 0,
				               &packet_state_out[0]->data[NET_STATE_LINK_FLAGS]);
				const Uint16 link_angle_wire = link_gun_analog
				                               ? link_gun_angle_to_wire(link_gun_angle) : 0;
				SDLNet_Write16(link_angle_wire,
				               &packet_state_out[0]->data[NET_STATE_LINK_ANGLE]);
				SDLNet_Write16((Uint16)pullX, &packet_state_out[0]->data[NET_STATE_PULL]);

				// Key this history by the tick named in the remote packet. The inbound and
				// outbound queues can shift independently after loss recovery.
				net_own_state_store(SDLNet_Read16(&packet_state_out[0]->data[2]),
				                    this_player->x, this_player->y, buttons, pullX);

				this_player->x = moveStartX;
				this_player->y = moveStartY;

				button[0] = false;
				button[1] = false;
				button[2] = false;
				button[3] = false;

				accelXC = 0;
				accelYC = 0;
				pullX = 0;
				link_gun_analog = false;
				link_gun_angle = 0.0f;
			}

			RbInput scripted;
			if (qa_replay_coop_input(playerNum_ - 1, this_player->x, this_player->y, &scripted))
				rb_apply_tuple(&scripted, this_player, &accelXC, &accelYC, &pullX,
				               &link_gun_analog, &link_gun_angle);

			// Self-test: capture the effective tuple for BOTH players so a replay
			// of this tick can reproduce the live-sampled movement exactly.
			if (rollback_selftest_active())
			{
				RbInput in;
				rb_fill_tuple(&in, this_player, *mouseX_, *mouseY_,
				              accelXC, accelYC, pullX, pullY, link_gun_analog, link_gun_angle);
				rollback_st_record(playerNum_ - 1, &in);
			}

			if (isNetworkGame)
				*mouseY_ = (JE_word)moveStartY;
		}  /*isNetworkGame*/

		/* --- Movement Routine Ending --- */

		moveOk = true;

		// Self-test replay: both players' movement comes from the recorded tuples.
		if (rollback_selftest_active() && rollback_resim)
		{
			const RbInput *st = rollback_st_get(playerNum_ - 1);
			rb_apply_tuple(st, this_player, &accelXC, &accelYC, &pullX,
			               &link_gun_analog, &link_gun_angle);
			*mouseX_ = (JE_word)st->mouseX;
			*mouseY_ = (JE_word)st->mouseY;
			if (playerNum_ == 1 && (rollback_st_events() & RB_EV_DEMO_END))
			{
				endLevel = true;
				levelEnd = 40;
			}
		}

		if (isNetworkGame && nrb_active())
		{
			if (playerNum_ != thisPlayerNum)
			{
				// Apply received or predicted peer input through the same tuple path.
				RbInput in;
				nrb_get_remote(nrb_frame(), &in);
				if (thisPlayerNum != networkHostPlayerNum)
				{
					// Clamp host difficulty before using it as a table index.
					const JE_shortint d = (JE_shortint)in.difficulty;
					difficultyLevel = (d >= DIFFICULTY_WIMP && d <= DIFFICULTY_10)
					                ? d : DIFFICULTY_NORMAL;
				}
				rb_apply_tuple(&in, this_player, &accelXC, &accelYC, &pullX,
				               &link_gun_analog, &link_gun_angle);
				linkIntent = in.buttons;
				haveLinkIntent = true;
			}
			else if (rollback_resim)
			{
				// Replaying our own past frame: consume the recorded tuple.
				RbInput in;
				nrb_get_local(nrb_frame(), &in);
				rb_apply_tuple(&in, this_player, &accelXC, &accelYC, &pullX,
				               &link_gun_analog, &link_gun_angle);
				linkIntent = in.buttons;
				haveLinkIntent = true;
			}
			// else: normal pass, local player; the live values stand as-is.
		}
		else if (isNetworkGame && !network_state_is_reset())
		{
			if (playerNum_ != thisPlayerNum)
			{
				if (thisPlayerNum != networkHostPlayerNum)
					difficultyLevel = SDLNet_Read16(&packet_state_in[0]->data[16]);

				Uint16 buttons = SDLNet_Read16(&packet_state_in[0]->data[12]);
				linkIntent = buttons;
				haveLinkIntent = true;
				for (int i = 0; i < 4; i++)
				{
					button[i] = buttons & 1;
					buttons >>= 1;
				}

				// Absolute, so assign rather than accumulate (see the send side above).
				this_player->x = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[4]);
				this_player->y = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[6]);
				accelXC = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[8]);
				accelYC = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[10]);
				pullX = (Sint16)SDLNet_Read16(&packet_state_in[0]->data[NET_STATE_PULL]);
				link_gun_analog = (SDLNet_Read16(
					&packet_state_in[0]->data[NET_STATE_LINK_FLAGS]) & 1) != 0;
				link_gun_angle = link_gun_angle_from_wire(SDLNet_Read16(
					&packet_state_in[0]->data[NET_STATE_LINK_ANGLE]));
			}
			else
			{
				// Replay local state from sequence-keyed history at the peer's logical tick.
				const Uint16 tick = SDLNet_Read16(&packet_state_in[0]->data[2]);

				int own_x, own_y;
				Uint16 buttons;
				net_own_state_load(tick, &own_x, &own_y, &buttons, &pullX);
				linkIntent = buttons;
				haveLinkIntent = true;

				for (int i = 0; i < 4; i++)
				{
					button[i] = buttons & 1;
					buttons >>= 1;
				}

				this_player->x = own_x;
				this_player->y = own_y;
				accelXC = (Sint16)SDLNet_Read16(&packet_state_out[network_delay]->data[8]);
				accelYC = (Sint16)SDLNet_Read16(&packet_state_out[network_delay]->data[10]);
				link_gun_analog = (SDLNet_Read16(
					&packet_state_out[network_delay]->data[NET_STATE_LINK_FLAGS]) & 1) != 0;
				link_gun_angle = link_gun_angle_from_wire(SDLNet_Read16(
					&packet_state_out[network_delay]->data[NET_STATE_LINK_ANGLE]));
			}
		}

		/*Street-Fighter codes*/
		// Network play derives twiddle direction from synchronized movement. Self-test replay
		// uses the recorded target; neither path may sample current local controls.
		if (rollback_selftest_active() && rollback_resim)
		{
			const RbInput *st = rollback_st_get(playerNum_ - 1);
			JE_SFCodes(playerNum_, this_player->x, this_player->y, st->sfTx, st->sfTy);
		}
		else if (isNetworkGame && haveLinkIntent)
		{
			/* Rebuild a one-pixel twiddle target from wire-carried movement intent. */
			int dirx, diry, tx, ty;
			rb_move_dir(linkIntent, &dirx, &diry);
			SF_twiddleTarget(this_player->x, this_player->y, dirx, diry, &tx, &ty);
			JE_SFCodes(playerNum_, this_player->x, this_player->y, tx, ty);
		}
		else if (isNetworkGame)
		{
			/* Delay-Based mode has no consumable tuple during its initial queue fill.
			 * Feed a neutral direction on both peers during that interval. */
			JE_SFCodes(playerNum_, this_player->x, this_player->y,
			           this_player->x, this_player->y);
		}
		else if (vt && !isNetworkGame)
		{
			// Rebuild twiddle direction separately when movement itself is skipped.
			int dirx = 0, diry = 0;
			if ((inputDevice == 0 || inputDevice == 1) && !play_demo)
			{
				if (keysactive[keySettings[KEY_SETTING_LEFT]])  --dirx;
				if (keysactive[keySettings[KEY_SETTING_RIGHT]]) ++dirx;
				if (keysactive[keySettings[KEY_SETTING_UP]])    --diry;
				if (keysactive[keySettings[KEY_SETTING_DOWN]])  ++diry;
			}
			if ((inputDevice == 0 || inputDevice >= 3) && joysticks > 0)
			{
				int j     = inputDevice == 0 ? 0 : inputDevice - 3;
				int j_max = inputDevice == 0 ? joysticks : inputDevice - 3 + 1;
				for (; j < j_max; j++)
				{
					if (joystick[j].direction[3]) --dirx;  // left
					if (joystick[j].direction[1]) ++dirx;  // right
					if (joystick[j].direction[0]) --diry;  // up
					if (joystick[j].direction[2]) ++diry;  // down
				}
			}
			// Drain and fold display-rate pointer direction into twiddle input.
			{
				int mdirx = 0, mdiry = 0;
				vt_ship_twiddle_dir(playerNum_ - 1, &mdirx, &mdiry);
				dirx += mdirx;
				diry += mdiry;
			}
			if (smoothies[9-1])  // inverted-control levels flip the vertical axis
				diry = -diry;
			if (mouseXC < 0) --dirx; else if (mouseXC > 0) ++dirx;  // analog stick (accumulated above;
			if (mouseYC < 0) --diry; else if (mouseYC > 0) ++diry;  //   mouseYC already flipped if inverted)

			int tx, ty;
			SF_twiddleTarget(this_player->x, this_player->y, dirx, diry, &tx, &ty);
			if (rollback_selftest_active())
				rollback_st_record_sf(playerNum_ - 1, (Sint16)tx, (Sint16)ty);
			JE_SFCodes(playerNum_, this_player->x, this_player->y, tx, ty);
		}
		else
		{
			// Classic movement: the tick's displacement is the intent, the same quantity the
			// wire tuple measures.
			int tx, ty;
			SF_twiddleTarget(this_player->x, this_player->y,
			               (int)this_player->x - (int)*mouseX_,
			               (int)this_player->y - (int)*mouseY_, &tx, &ty);
			if (rollback_selftest_active() && !rollback_resim)
				rollback_st_record_sf(playerNum_ - 1, (Sint16)tx, (Sint16)ty);
			JE_SFCodes(playerNum_, this_player->x, this_player->y, tx, ty);
		}

		if (moveOk)
		{
			/* Linking. */

			// Keep the delay/analog wire regression in the fused state whose turret reads this field.
			if (isNetworkGame && qa_net_scenario == 19)
				twoPlayerLinked = true;

			// Rollback uses tuple intent because docked positions include each peer's predicted carrier.
			const bool linkMoved = haveLinkIntent
				? (linkIntent & RB_MOVE_MASK) != 0
				: (this_player->x != *mouseX_ || this_player->y != *mouseY_);

			if (split_arcade_mode() && !twoPlayerLinked && !linkMoved &&
			    abs(player[0].x - player[1].x) < 8 && abs(player[0].y - player[1].y) < 8 &&
			    player[0].is_alive && player[1].is_alive && !galagaMode)
			{
				twoPlayerLinked = true;
			}

			// Fuse/unfuse cues are played presentation-side in game_loop.c (link_cue_state):
			// queueing them here loses them to the sidekick-fire slot and to rollback.

			if (playerNum_ == 1 && (button[3-1] || button[2-1]) && !galagaMode)
				twoPlayerLinked = false;

			if (twoPlayerMode && twoPlayerLinked && playerNum_ == 2 && linkMoved)
			{
				if (button[0])
				{
					if (link_gun_analog)
					{
						linkGunDirec = link_gun_angle;
					}
					else
					{
						JE_real tempR;

						if (haveLinkIntent)
						{
							if (linkIntent & (RB_MOVE_LEFT | RB_MOVE_RIGHT))
								tempR = (linkIntent & RB_MOVE_RIGHT) ? M_PI_2 : (M_PI + M_PI_2);
							else
								tempR = (linkIntent & RB_MOVE_DOWN) ? 0 : M_PI;
						}
						else if (abs(this_player->x - *mouseX_) > abs(this_player->y - *mouseY_))
							tempR = (this_player->x - *mouseX_ > 0) ? M_PI_2 : (M_PI + M_PI_2);
						else
							tempR = (this_player->y - *mouseY_ > 0) ? 0 : M_PI;

						if (fabsf(linkGunDirec - tempR) < 0.3f)
							linkGunDirec = tempR;
						else if (linkGunDirec < tempR && linkGunDirec - tempR > -3.24f)
							linkGunDirec += 0.2f;
						else if (linkGunDirec - tempR < M_PI)
							linkGunDirec -= 0.2f;
						else
							linkGunDirec += 0.2f;
					}

					if (linkGunDirec >= (2 * M_PI))
						linkGunDirec -= (2 * M_PI);
					else if (linkGunDirec < 0)
						linkGunDirec += (2 * M_PI);
				}
				else if (!galagaMode)
				{
					twoPlayerLinked = false;
				}
			}
		}
	}

	if (levelEnd > 0 && all_players_dead())
		reallyEndLevel = true;

	/* End Level Fade-Out */
	if (this_player->is_alive && endLevel)
	{
		if (levelEnd == 0)
		{
			reallyEndLevel = true;
		}
		else
		{
			this_player->y -= levelEndWarp;
			if (this_player->y < -vga_height)
				reallyEndLevel = true;

			int trail_spacing = 1;
			int trail_y = this_player->y;
			int num_trails = abs(41 - levelEnd);
			if (num_trails > 20)
				num_trails = 20;

			for (int i = 0; i < num_trails; i++)
			{
				trail_y += trail_spacing;
				trail_spacing++;
			}

			// Give the whole warp comet the ship id so the render-rate override
			// moves it as one rigid, interpolated object.
			rl_current_id = RL_ID_SHIP_BASE + playerNum_;
			for (int i = 1; i < num_trails; i++)
			{
				trail_y -= trail_spacing;
				trail_spacing--;

				if (trail_y > 0 && trail_y < 170)
				{
					if (shipGr_ == 0)
					{
						blit_ship2x2(VGAScreen, this_player->x - 17, trail_y - 7, *shipGrPtr_, 13);
						blit_ship2x2(VGAScreen, this_player->x + 7 , trail_y - 7, *shipGrPtr_, 51);
					}
					else if (shipGr_ == 1)
					{
						blit_ship2x2(VGAScreen, this_player->x - 17, trail_y - 7, *shipGrPtr_, 220);
						blit_ship2x2(VGAScreen, this_player->x + 7 , trail_y - 7, *shipGrPtr_, 222);
					}
					else
					{
						blit_ship2x2(VGAScreen, this_player->x - 5, trail_y - 7, *look_sheet, look_gr);
					}
				}
			}
			rl_current_id = 0;
		}
	}

	if (play_demo)
	{
		const int playfield_left = PLAYFIELD_LEFT;
		const int insert_coin_x = playfield_left + (PLAYFIELD_WIDTH - JE_textWidth(miscText[7], SMALL_FONT_SHAPES)) / 2;
		const int insert_coin_y = 10;
		JE_dString(VGAScreen, insert_coin_x, insert_coin_y, miscText[7], SMALL_FONT_SHAPES); // insert coin
	}

	if (this_player->is_alive && !endLevel)
	{
		if (!twoPlayerLinked || playerNum_ < 2)
		{
		if (!split_arcade_mode() || shipGr2 != 0)  // if not dragonwing
			{
				if (this_player->sidekick[LEFT_SIDEKICK].style == 0)
				{
					this_player->sidekick[LEFT_SIDEKICK].x = *mouseX_ - 14;
					this_player->sidekick[LEFT_SIDEKICK].y = *mouseY_;
				}

				if (this_player->sidekick[RIGHT_SIDEKICK].style == 0)
				{
					this_player->sidekick[RIGHT_SIDEKICK].x = *mouseX_ + 16;
					this_player->sidekick[RIGHT_SIDEKICK].y = *mouseY_;
				}
			}

			if (!vt)
			{
				const bool xDue = this_player->x_friction_ticks == 0;
				const bool yDue = this_player->y_friction_ticks == 0;
				this_player->x_friction_ticks = xDue ? 1 : this_player->x_friction_ticks - 1;
				this_player->y_friction_ticks = yDue ? 2 : this_player->y_friction_ticks - 1;

				this_player->x_velocity = ship_axis_velocity(this_player->x_velocity, xDue,
				                                             accelXC);
				this_player->y_velocity = ship_axis_velocity(this_player->y_velocity, yDue,
				                                             accelYC);

				this_player->x += this_player->x_velocity;
				this_player->y += this_player->y_velocity;
			}

			// if player moved, add new ship x, y history entry
			if (this_player->x - *mouseX_ != 0 || this_player->y - *mouseY_ != 0)
			{
				for (uint i = 1; i < COUNTOF(player->old_x); ++i)
				{
					this_player->old_x[i - 1] = this_player->old_x[i];
					this_player->old_y[i - 1] = this_player->old_y[i];
				}
				this_player->old_x[COUNTOF(player->old_x) - 1] = this_player->x;
				this_player->old_y[COUNTOF(player->old_x) - 1] = this_player->y;
			}
		}
		else  /*twoPlayerLinked*/
		{
			if (shipGr_ == 0)
				this_player->x = player[0].x - 1;
			else
				this_player->x = player[0].x;
			this_player->y = player[0].y + 8;

			this_player->x_velocity = player[0].x_velocity;
			this_player->y_velocity = 4;

			// Keep docked position history current for trailing sidekicks. Compare with the newest stored
			// position because the dock pin rewrites x/y every tick.
			if (this_player->x != this_player->old_x[COUNTOF(player->old_x) - 1] ||
			    this_player->y != this_player->old_y[COUNTOF(player->old_x) - 1])
			{
				for (uint i = 1; i < COUNTOF(player->old_x); ++i)
				{
					this_player->old_x[i - 1] = this_player->old_x[i];
					this_player->old_y[i - 1] = this_player->old_y[i];
				}
				this_player->old_x[COUNTOF(player->old_x) - 1] = this_player->x;
				this_player->old_y[COUNTOF(player->old_x) - 1] = this_player->y;
			}

			// turret direction marker/shield
			shotMultiPos[SHOT_MISC] = 0;
			b = player_shot_create(0, SHOT_MISC, this_player->x + 1 + roundf(sim_sinf(linkGunDirec + 0.2f) * 26), this_player->y + roundf(sim_cosf(linkGunDirec + 0.2f) * 26), *mouseX_, *mouseY_, 148, playerNum_);
			link_marker_slot[0] = (b >= 0 && b < MAX_PWEAPON) ? b : -1;
			shotMultiPos[SHOT_MISC] = 0;
			b = player_shot_create(0, SHOT_MISC, this_player->x + 1 + roundf(sim_sinf(linkGunDirec - 0.2f) * 26), this_player->y + roundf(sim_cosf(linkGunDirec - 0.2f) * 26), *mouseX_, *mouseY_, 148, playerNum_);
			link_marker_slot[1] = (b >= 0 && b < MAX_PWEAPON) ? b : -1;
			shotMultiPos[SHOT_MISC] = 0;
			b = player_shot_create(0, SHOT_MISC, this_player->x + 1 + roundf(sim_sinf(linkGunDirec) * 26), this_player->y + roundf(sim_cosf(linkGunDirec) * 26), *mouseX_, *mouseY_, 147, playerNum_);
			link_marker_slot[2] = (b >= 0 && b < MAX_PWEAPON) ? b : -1;

			if (shotRepeat[SHOT_REAR] > 0)
			{
				--shotRepeat[SHOT_REAR];
			}
			else if (button[1-1])
			{
				const JE_byte rear_weapon_id = this_player->items.weapon[REAR_WEAPON].id;
				if (rear_weapon_id > 0 && rear_weapon_id <= COUNTOF(linkGunWeapons))
				{
					shotMultiPos[SHOT_REAR] = 0;
					b = player_shot_create(0, SHOT_REAR,
					                       this_player->x + 1 + roundf(sim_sinf(linkGunDirec) * 20),
					                       this_player->y + roundf(sim_cosf(linkGunDirec) * 20),
					                       *mouseX_, *mouseY_, linkGunWeapons[rear_weapon_id - 1],
					                       playerNum_);
					player_shot_set_direction(b, rear_weapon_id, linkGunDirec);
				}
			}
		}
	}

	if (!endLevel)
	{
		if (this_player->x > PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN)
		{
			this_player->x = PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN;
			constantLastX = -constantLastX;
		}
		if (this_player->x < SHIP_LEFT_MARGIN)
		{
			this_player->x = SHIP_LEFT_MARGIN;
			constantLastX = -constantLastX;
		}

		// Both seats share one floor, online and local; a docked wing pinned 8px
		// below player one clamps to the same edge.
		if (this_player->y > SHIP_BOTTOM_MARGIN)
			this_player->y = SHIP_BOTTOM_MARGIN;

		if (this_player->y < SHIP_TOP_MARGIN)
			this_player->y = SHIP_TOP_MARGIN;

		// Determines the ship banking sprite to display, depending on horizontal velocity and acceleration
		int moved_x = this_player->x - *mouseX_;
		if (vt)
		{
			int moved_y;
			vt_ship_shot_delta(playerNum_ - 1, &moved_x, &moved_y);
		}
		const int ship_banking = player_ship_banking(vt, !isNetworkGame || nrb_active(),
		                                             this_player->x_velocity, moved_x, pullX);

		int ship_sprite = ship_banking * 2 + shipGr_;
		const int look_sprite = ship_banking * 2 + look_gr;

		explosionFollowAmountX = this_player->x - this_player->last_x_explosion_follow;
		explosionFollowAmountY = this_player->y - this_player->last_y_explosion_follow;

		if (explosionFollowAmountY < 0)
			explosionFollowAmountY = 0;

		this_player->last_x_explosion_follow = this_player->x;
		this_player->last_y_explosion_follow = this_player->y;

		// Tag the ship (shadow + hull) for cross-frame interpolation.
		rl_current_id = RL_ID_SHIP_SHADOW_BASE + playerNum_;
		rl_shot_attach = 3 | ((playerNum_ - 1) << 2);
		rl_current_sub_x = (float)mapX2Ofs - mapX2Ofs_f;

		// Recenter the cast shadow against background-2 parallax. Extra Parallax
		// widens that sweep, so it needs the larger midpoint offset.
		const int shadow_light_dx = extraParallax ? 34 : 18;

		if (shipGr_ == 0)
		{
			if (background2 && ship_draw_casts_shadow())
			{
				blit_sprite2x2_darken(VGAScreen, this_player->x - 17 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite + 13);
				blit_sprite2x2_darken(VGAScreen, this_player->x + 7 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite + 51);
				if (superWild)
				{
					blit_sprite2x2_darken(VGAScreen, this_player->x - 16 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite + 13);
					blit_sprite2x2_darken(VGAScreen, this_player->x + 6 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, ship_sprite + 51);
				}
			}
		}
		else if (shipGr_ == 1)
		{
			if (background2 && ship_draw_casts_shadow())
			{
				blit_sprite2x2_darken(VGAScreen, this_player->x - 17 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, 220);
				blit_sprite2x2_darken(VGAScreen, this_player->x + 7 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *shipGrPtr_, 222);
			}
		}
		else
		{
			if (background2 && ship_draw_casts_shadow())
			{
				blit_sprite2x2_darken(VGAScreen, this_player->x - 5 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *look_sheet, look_sprite);
				if (superWild)
				{
					blit_sprite2x2_darken(VGAScreen, this_player->x - 4 - mapX2Ofs + shadow_light_dx, this_player->y - 7 + shadowYDist, *look_sheet, look_sprite);
				}
			}
		}

		rl_current_id = RL_ID_SHIP_BASE + playerNum_;
		rl_shot_attach = 0;
		rl_current_sub_x = 0.0f;

		// Noclip transparent mode draws the hull semi-transparent (same blended blit
		// as the post-hit invulnerability flash), so it reads as a "ghost" ship.
		if (this_player->invulnerable_ticks > 0 || noclipMode == NOCLIP_TRANSPARENT)
		{
			if (this_player->invulnerable_ticks > 0)
				--this_player->invulnerable_ticks;

			if (shipGr_ == 0)
			{
				blit_ship2x2_blend(VGAScreen, this_player->x - 17, this_player->y - 7, *shipGrPtr_, ship_sprite + 13);
				blit_ship2x2_blend(VGAScreen, this_player->x + 7 , this_player->y - 7, *shipGrPtr_, ship_sprite + 51);
			}
			else if (shipGr_ == 1)
			{
				blit_ship2x2_blend(VGAScreen, this_player->x - 17, this_player->y - 7, *shipGrPtr_, 220);
				blit_ship2x2_blend(VGAScreen, this_player->x + 7 , this_player->y - 7, *shipGrPtr_, 222);
			}
			else
				blit_ship2x2_blend(VGAScreen, this_player->x - 5, this_player->y - 7, *look_sheet, look_sprite);
		}
		else
		{
			if (shipGr_ == 0)
			{
				blit_ship2x2(VGAScreen, this_player->x - 17, this_player->y - 7, *shipGrPtr_, ship_sprite + 13);
				blit_ship2x2(VGAScreen, this_player->x + 7, this_player->y - 7, *shipGrPtr_, ship_sprite + 51);
			}
			else if (shipGr_ == 1)
			{
				blit_ship2x2(VGAScreen, this_player->x - 17, this_player->y - 7, *shipGrPtr_, 220);
				blit_ship2x2(VGAScreen, this_player->x + 7, this_player->y - 7, *shipGrPtr_, 222);

				// Keep banking trim separate so its changing blit count does not snap
				// the interpolated hull.
				rl_current_id = RL_ID_SHIP_TRIM_BASE + playerNum_;
				int ship_banking = 0;
				switch (ship_sprite)
				{
				case 5:
					blit_ship2(VGAScreen, this_player->x - 17, this_player->y + 7, *shipGrPtr_, 40);
					tempW = this_player->x - 7;
					ship_banking = -2;
					break;
				case 3:
					blit_ship2(VGAScreen, this_player->x - 17, this_player->y + 7, *shipGrPtr_, 39);
					tempW = this_player->x - 7;
					ship_banking = -1;
					break;
				case 1:
					ship_banking = 0;
					break;
				case -1:
					blit_ship2(VGAScreen, this_player->x + 19, this_player->y + 7, *shipGrPtr_, 58);
					tempW = this_player->x + 9;
					ship_banking = 1;
					break;
				case -3:
					blit_ship2(VGAScreen, this_player->x + 19, this_player->y + 7, *shipGrPtr_, 59);
					tempW = this_player->x + 9;
					ship_banking = 2;
					break;
				}
				rl_current_id = RL_ID_SHIP_BASE + playerNum_;  // back to the hull for anything after
				if (ship_banking != 0)  // NortSparks
				{
					if (shotRepeat[SHOT_NORTSPARKS] > 0)
					{
						--shotRepeat[SHOT_NORTSPARKS];
					}
					else
					{
						// Sequenced: as arguments the two draws were unordered, and MSVC and GCC
						// pick opposite orders, so the jitter landed on opposite axes per platform.
						const int spark_x = tempW + (mt_rand() % 8) - 4;
						const int spark_y = this_player->y + (mt_rand() % 8) - 4;
						b = player_shot_create(0, SHOT_NORTSPARKS, spark_x, spark_y, *mouseX_, *mouseY_, 671, playerNum_);
						shotRepeat[SHOT_NORTSPARKS] = abs(ship_banking) - 1;
					}
				}
			}
			else
			{
				blit_ship2x2(VGAScreen, this_player->x - 5, this_player->y - 7, *look_sheet, look_sprite);
			}
		}

		rl_current_id = 0;  // end ship tag

		/*Options Location*/
		if (shipGr_ == 0)  // The Dragonwing's wide hull sets sidekicks further out, either seat.
		{
			if (this_player->sidekick[LEFT_SIDEKICK].style == 0)
			{
				this_player->sidekick[LEFT_SIDEKICK].x = this_player->x - 14 + ship_banking * 2;
				this_player->sidekick[LEFT_SIDEKICK].y = this_player->y;
			}

			if (this_player->sidekick[RIGHT_SIDEKICK].style == 0)
			{
				this_player->sidekick[RIGHT_SIDEKICK].x = this_player->x + 17 + ship_banking * 2;
				this_player->sidekick[RIGHT_SIDEKICK].y = this_player->y;
			}
		}
	}  // !endLevel

	if (moveOk)
	{
		if (this_player->is_alive)
		{
			if (!endLevel)
			{
				if (vt)
				{
					// VT moves the ship between ticks, so (x - last_x_shot_move) reads ~0;
					// use the inter-tick delta so tracking shots (laser, main pulse) follow.
					vt_ship_shot_delta(playerNum_ - 1,
					                   &this_player->delta_x_shot_move,
					                   &this_player->delta_y_shot_move);

					// Weapons 98-100 inherit motion from the tick's starting position.
					// Reconstruct it because VT otherwise leaves a zero delta here.
					*mouseX_ = this_player->x - this_player->delta_x_shot_move;
					*mouseY_ = this_player->y - this_player->delta_y_shot_move;
				}
				else
				{
					this_player->delta_x_shot_move = this_player->x - this_player->last_x_shot_move;
					this_player->delta_y_shot_move = this_player->y - this_player->last_y_shot_move;
				}

				/* PLAYER SHOT Change */
				if (button[4-1])
				{
					portConfigChange = true;
					if (portConfigDone)
					{
						shotMultiPos[SHOT_REAR] = 0;

						// Cycle this Super Arcade ship's paired special.
						const uint sa_ship = player_sa_ship(this_player);
						if (sa_ship != SA_NONE && sa_ship <= SA_LASTSHIP)
						{
							shotMultiPos[SHOT_SPECIAL] = 0;
							shotMultiPos[SHOT_SPECIAL2] = 0;
							if (this_player->items.special == SASpecialWeapon[sa_ship-1])
							{
								this_player->items.special = SASpecialWeaponB[sa_ship-1];
								this_player->weapon_mode = 2;
							}
							else
							{
								this_player->items.special = SASpecialWeapon[sa_ship-1];
								this_player->weapon_mode = 1;
							}
							if (dual_ship_mode())
							{
								this_player->shot_multi_pos[SHOT_SPECIAL] = 0;
								this_player->shot_multi_pos[SHOT_SPECIAL2] = 0;
							}
						}
						// This ship's own bay: the partner's toggle is simulated here too, and
						// wrapping it against the local gun kept the change off this screen.
						else if (++this_player->weapon_mode > JE_portConfigs(this_player))
							this_player->weapon_mode = 1;

						JE_drawPortConfigButtons();
						portConfigDone = false;
					}
				}

				/* PLAYER SHOT Creation */

				// Reassert the firing ship's Endless effect context.
				endlessSetFxPlayer((uint)(this_player - &player[0]));

				// Ahead of the special, so a special pressed with the volley belongs to it.
				endlessArmOpeningSalvoForTick(this_player, playerNum_);

				/*SpecialShot*/
				if (!galagaMode)
					JE_doSpecialShot(playerNum_, &this_player->armor, &this_player->shield);

				/*Normal Main Weapons*/
				if (!(twoPlayerLinked && playerNum_ == 2))
				{
					int min, max;

					if (!twoPlayerMode || dual_ship_mode())
						min = 1, max = 2;
					else
						min = max = playerNum_;

					for (temp = min - 1; temp < max; temp++)
					{
						// min/max are 1 or 2, so temp is a bay index. It is also the shared global
						// scratch byte, so its range isn't visible at the subscripts below.
						OT_ASSUME(temp < COUNTOF(this_player->items.weapon));
						const uint item = this_player->items.weapon[temp].id;

						if (item > 0)
						{
							if (shotRepeat[temp] > 0)
							{
								--shotRepeat[temp];
							}
							else if (button[1-1])
							{
								// Use effective Arcade weapon power; the fire cursor self-wraps.
								const uint item_power = galagaMode ? 0 : arcade_weapon_power(this_player, temp) - 1,
								           item_mode = (temp == REAR_WEAPON) ? this_player->weapon_mode - 1 : 0;

								// Opening Salvo was armed for this tick's whole volley before the
								// special went out; see endlessArmOpeningSalvoForTick.
								b = player_shot_fire_port(item, temp, this_player->x,
								                          this_player->y, *mouseX_, *mouseY_,
								                          item_mode, item_power, playerNum_);
							}
						}
					}
				}

				/*Super Charge Weapons*/
				// The charge meter is the Dragonwing's, so only the linked pair grows one.
				if (playerNum_ == 2 && !dual_ship_mode())
				{

					if (!twoPlayerLinked)
					{
						rl_current_id = RL_ID_SHIP_BASE + playerNum_;  // charge meter rides with the ship
						blit_ship_indicator2(VGAScreen, this_player->x + (shipGr_ == 0) + 1, this_player->y - 13, spriteSheet10, 77 + chargeLevel + chargeGr * 19);
						rl_current_id = 0;
					}

					if (chargeGrWait > 0)
					{
						chargeGrWait--;
					}
					else
					{
						chargeGr++;
						if (chargeGr == 4)
							chargeGr = 0;
						chargeGrWait = 3;
					}

					if (chargeLevel > 0)
					{
						fill_rectangle_xy(VGAScreenSeg, HUD_X(269), 107 + (chargeLevel - 1) * 3, HUD_X(275), 108 + (chargeLevel - 1) * 3, 193);
					}

					if (chargeWait > 0)
					{
						chargeWait--;
					}
					else
					{
						if (chargeLevel < chargeMax)
							chargeLevel++;

						chargeWait = 28 - this_player->items.weapon[REAR_WEAPON].power * 2;
						if (difficultyLevel > DIFFICULTY_HARD)
							chargeWait -= 5;
					}

					if (chargeLevel > 0)
						fill_rectangle_xy(VGAScreenSeg, HUD_X(269), 107 + (chargeLevel - 1) * 3, HUD_X(275), 108 + (chargeLevel - 1) * 3, 204);

					if (shotRepeat[SHOT_P2_CHARGE] > 0)
					{
						--shotRepeat[SHOT_P2_CHARGE];
					}
					else if (button[1-1] && (!twoPlayerLinked || chargeLevel > 0))
					{
						shotMultiPos[SHOT_P2_CHARGE] = 0;
						b = player_shot_create(16, SHOT_P2_CHARGE, this_player->x, this_player->y, *mouseX_, *mouseY_, chargeGunWeapons[player[1].items.weapon[REAR_WEAPON].id-1] + chargeLevel, playerNum_);

						if (chargeLevel > 0)
							fill_rectangle_xy(VGAScreenSeg, HUD_X(269), 107 + (chargeLevel - 1) * 3, HUD_X(275), 108 + (chargeLevel - 1) * 3, 193);

						chargeLevel = 0;
						chargeWait = 30 - this_player->items.weapon[REAR_WEAPON].power * 2;
					}
				}

				/* Super bomb. */
				temp = playerNum_;
				if (temp == 0)
					temp = 1;  /*Get whether player 1 or 2*/

				// Clamp the shared scratch player number before indexing player[].
				const uint bombPlayer = (temp >= 1 && temp <= COUNTOF(player)) ? temp - 1 : 0;

				if (player[bombPlayer].superbombs > 0)
				{
					if (shotRepeat[SHOT_P1_SUPERBOMB + temp-1] > 0)
					{
						--shotRepeat[SHOT_P1_SUPERBOMB + temp-1];
					}
					else if ((button[3-1] || button[2-1])
					         && !(endlessFxActive() && (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_DUD)))
					{  // Dud (gamble curse): the bombs are aboard but jammed; the fire press does nothing this sector
						--player[bombPlayer].superbombs;
						shotMultiPos[SHOT_P1_SUPERBOMB + temp-1] = 0;
						b = player_shot_create(16, SHOT_P1_SUPERBOMB + temp-1, this_player->x, this_player->y, *mouseX_, *mouseY_, 535, playerNum_);
					}
				}

				// sidekicks

				if (this_player->sidekick[LEFT_SIDEKICK].style == 4 && this_player->sidekick[RIGHT_SIDEKICK].style == 4)
					optionSatelliteRotate += 0.2f;
				else if (this_player->sidekick[LEFT_SIDEKICK].style == 4 || this_player->sidekick[RIGHT_SIDEKICK].style == 4)
					optionSatelliteRotate += 0.15f;

				switch (this_player->sidekick[LEFT_SIDEKICK].style)
				{
				case 1:  // trailing
				case 3:
					this_player->sidekick[LEFT_SIDEKICK].x = this_player->old_x[COUNTOF(player->old_x) / 2 - 1];
					this_player->sidekick[LEFT_SIDEKICK].y = this_player->old_y[COUNTOF(player->old_x) / 2 - 1];
					break;
				case 2:  // front-mounted (launchable)
					JE_frontOption(this_player, LEFT_SIDEKICK, front_option_home_x(this_player, LEFT_SIDEKICK), button[1 + LEFT_SIDEKICK]);
					break;
				case 4:  // orbiting
					this_player->sidekick[LEFT_SIDEKICK].x = this_player->x + roundf(sim_sinf(optionSatelliteRotate) * 20);
					this_player->sidekick[LEFT_SIDEKICK].y = this_player->y + roundf(sim_cosf(optionSatelliteRotate) * 20);
					break;
				}

				switch (this_player->sidekick[RIGHT_SIDEKICK].style)
				{
				case 4:  // orbiting
					this_player->sidekick[RIGHT_SIDEKICK].x = this_player->x - roundf(sim_sinf(optionSatelliteRotate) * 20);
					this_player->sidekick[RIGHT_SIDEKICK].y = this_player->y - roundf(sim_cosf(optionSatelliteRotate) * 20);
					break;
				case 1:  // trailing
				case 3:
					this_player->sidekick[RIGHT_SIDEKICK].x = this_player->old_x[0];
					this_player->sidekick[RIGHT_SIDEKICK].y = this_player->old_y[0];
					break;
				case 2:  // front-mounted (launchable)
					JE_frontOption(this_player, RIGHT_SIDEKICK, front_option_home_x(this_player, RIGHT_SIDEKICK), button[1 + RIGHT_SIDEKICK]);
					break;
				}

				if (playerNum_ == 2 || !twoPlayerMode || dual_ship_mode())
				{
					for (uint i = 0; i < COUNTOF(player->items.sidekick); ++i)
					{
						uint shot_i = (i == 0) ? SHOT_LEFT_SIDEKICK : SHOT_RIGHT_SIDEKICK;

						JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

						// A trailing large body draws downward from the pod position, so it fires
						// from lower down than the position itself.
						const int shot_y = this_player->sidekick[i].y
						                 + (this_player->sidekick[i].style == 1 ? SIDEKICK_TRAIL_SHOT_Y : 0);
						// Center Twin Pods around the owning sidekick mount.
						const int twin_dx = endlessPerkTwinPodOffset((uint)(this_player - &player[0]), i);
						const int shot_x = this_player->sidekick[i].x - twin_dx;

						// fire/refill sidekick
						if (this_option->wport > 0)
						{
							if (shotRepeat[shot_i] > 0)
							{
								--shotRepeat[shot_i];
							}
							else
							{
								const int ammo_max = cheatInfiniteSidekickAmmo ? 0 : this_player->sidekick[i].ammo_max;

								if (!cheatInfiniteSidekickAmmo && ammo_max > 0)  // sidekick has limited ammo
								{
									if (this_player->sidekick[i].ammo_refill_ticks > 0)
									{
										--this_player->sidekick[i].ammo_refill_ticks;
									}
									else  // refill one ammo
									{
										this_player->sidekick[i].ammo_refill_ticks = this_player->sidekick[i].ammo_refill_ticks_max;

										if (this_player->sidekick[i].ammo < ammo_max)
											++this_player->sidekick[i].ammo;

										JE_drawSidekickAmmoGauge(playerNum_, i, this_player->sidekick[i].ammo, ammo_max, false);
									}

									if (button[1 + i] && (cheatInfiniteSidekickAmmo || this_player->sidekick[i].ammo > 0))
									{
										b = player_shot_create(this_option->wport, shot_i, shot_x, shot_y, *mouseX_, *mouseY_, this_option->wpnum + this_player->sidekick[i].charge, playerNum_);

										if (!cheatInfiniteSidekickAmmo)
											--this_player->sidekick[i].ammo;

										// Twin Pods: the twin spends the next round, so the last
										// round in the magazine fires alone.
										if (cheatInfiniteSidekickAmmo || this_player->sidekick[i].ammo > 0)
										{
											const JE_integer twin = player_shot_create_twin(
												b, this_option->wport, i, twin_dx,
												this_player->sidekick[i].x, shot_y, *mouseX_, *mouseY_,
												this_option->wpnum + this_player->sidekick[i].charge, playerNum_);
											if (twin < MAX_PWEAPON && !cheatInfiniteSidekickAmmo)
												--this_player->sidekick[i].ammo;
										}

										if (this_player->sidekick[i].charge > 0)
										{
											shotMultiPos[shot_i] = 0;
											this_player->sidekick[i].charge = 0;
										}
										this_player->sidekick[i].charge_ticks = endlessPerkChargeTicks(20);
										this_player->sidekick[i].animation_enabled = true;

										if (!cheatInfiniteSidekickAmmo)
											JE_drawSidekickAmmoGauge(playerNum_, i, this_player->sidekick[i].ammo, ammo_max, true);
									}
								}
								else  // has infinite ammo
								{
									// The setting only gates main-button autofire for charge sidekicks.
									const bool charge_autofire =
										chargeSidekickAutofire == CHARGE_AUTOFIRE_ON
										|| chargeSidekickAutofire == CHARGE_AUTOFIRE_FAST
										|| (chargeSidekickAutofire == CHARGE_AUTOFIRE_FULL
										    && this_player->sidekick[i].charge >= this_option->pwr);

									if ((button[0] && (charge_autofire || !this_option->pwr)) || button[1 + i])
									{
										b = player_shot_create(this_option->wport, shot_i, shot_x, shot_y, *mouseX_, *mouseY_, this_option->wpnum + this_player->sidekick[i].charge, playerNum_);
										// Twin Pods: no magazine here, so the twin costs generator power only.
										player_shot_create_twin(
											b, this_option->wport, i, twin_dx, this_player->sidekick[i].x, shot_y,
											*mouseX_, *mouseY_, this_option->wpnum + this_player->sidekick[i].charge,
											playerNum_);

										if (this_player->sidekick[i].charge > 0)
										{
											shotMultiPos[shot_i] = 0;
											this_player->sidekick[i].charge = 0;
										}
										this_player->sidekick[i].charge_ticks = endlessPerkChargeTicks(20);
										this_player->sidekick[i].animation_enabled = true;

										// Fast autofire uses the shortest repeat among all charge stages.
										if (chargeSidekickAutofire == CHARGE_AUTOFIRE_FAST && this_option->pwr > 0)
										{
											JE_byte fastest = weapons[this_option->wpnum].shotrepeat;
											for (uint s = 1; s <= this_option->pwr; ++s)
												if (weapons[this_option->wpnum + s].shotrepeat < fastest)
													fastest = weapons[this_option->wpnum + s].shotrepeat;
											shotRepeat[shot_i] = fastest;
										}
									}
								}
							}
						}
					}
				}  // end of if player has sidekicks
			}  // !endLevel
		} // this_player->is_alive
	} // moveOK

	// draw sidekicks
	if ((playerNum_ == 2 || !twoPlayerMode || dual_ship_mode()) && !endLevel)
	{
		// Sidekicks are dyed by the drive of the ship that flies them, so re-assert the owner:
		// the volleys fired above leave the effect context on whoever they last fired for.
		endlessSetFxPlayer((uint)(this_player - &player[0]));

		for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
		{
			JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

			if (this_option->option > 0)
			{
				if (this_player->sidekick[i].animation_enabled)
				{
					if (++this_player->sidekick[i].animation_frame >= this_option->ani)
					{
						this_player->sidekick[i].animation_frame = 0;
						this_player->sidekick[i].animation_enabled = (this_option->option == 1);
					}
				}

				const int x = this_player->sidekick[i].x,
				          y = this_player->sidekick[i].y;
				const uint sprite = this_option->gr[this_player->sidekick[i].animation_frame] + this_player->sidekick[i].charge;

				rl_current_id = RL_ID_SIDEKICK_BASE + playerNum_ * 2 + (int)i;
				// Pods and satellites are ship-relative, so attach both axes to the
				// render-rate ship. Their own offset or orbit still interpolates.
				const uint style = this_player->sidekick[i].style;
				const bool rigid = style == 0 || style == 4
				                || (style == 2 && optionAttachmentLinked[i]);
				rl_shot_attach = rigid ? (3 | ((playerNum_ - 1) << 2)) : 0;
				if (this_player->sidekick[i].style == 4)
				{
					// Preserve the orbit's discarded sub-pixel remainder for replay.
					const float ox = sim_sinf(optionSatelliteRotate) * 20,
					            oy = sim_cosf(optionSatelliteRotate) * 20;
					const float dir = (i == LEFT_SIDEKICK) ? 1.0f : -1.0f;  // right slot mirrors
					rl_current_sub_x = dir * (ox - roundf(ox));
					rl_current_sub_y = dir * (oy - roundf(oy));
				}
				const NetShipStyle look = ship_draw_tinted(netStyleForSidekick(ship_draw_seat));
				if (this_player->sidekick[i].style == 1 || this_player->sidekick[i].style == 2)
					blit_sprite2x2_alpha(VGAScreen, x - 6, y, spriteSheet10, sprite, look.bank,
					                     look.opacity);
				else
					blit_sprite2_alpha(VGAScreen, x, y, spriteSheet9, sprite, look.bank,
					                   look.opacity);
				rl_current_id = 0;
				rl_shot_attach = 0;
				rl_current_sub_x = rl_current_sub_y = 0.0f;
			}

			if (cheatInstantCharge)
			{
				// debug: skip the timed ramp, hold the sidekick at full charge
				this_player->sidekick[i].charge = this_option->pwr;
			}
			else if (--this_player->sidekick[i].charge_ticks == 0)
			{
				if (this_player->sidekick[i].charge < this_option->pwr)
					++this_player->sidekick[i].charge;
				this_player->sidekick[i].charge_ticks = endlessPerkChargeTicks(20);
			}
		}
	}
}

static void coop_ship_runtime_load(Player *this_player)
{
	power = this_player->generator_power;
	powerAdd = this_player->generator_power_add;
	shieldWait = this_player->shield_wait;
	memcpy(shotRepeat, this_player->shot_repeat, sizeof(shotRepeat));
	memcpy(shotMultiPos, this_player->shot_multi_pos, sizeof(shotMultiPos));
	portConfigChange = false;
	portConfigDone = this_player->port_config_done;
	optionSatelliteRotate = this_player->option_satellite_rotate;
	memcpy(optionAttachmentMove, this_player->option_attachment_move, sizeof(optionAttachmentMove));
	memcpy(optionAttachmentLinked, this_player->option_attachment_linked, sizeof(optionAttachmentLinked));
	memcpy(optionAttachmentReturn, this_player->option_attachment_return, sizeof(optionAttachmentReturn));
	fireButtonHeld = this_player->special_fire_held;
	zinglonDuration = this_player->zinglon_duration;
	zinglonRamp = this_player->zinglon_ramp;
	astralDuration = this_player->astral_duration;
	flareDuration = this_player->flare_duration;
	flareStart = this_player->flare_start;
	flareColChg = this_player->flare_color_change;
	specialWait = this_player->special_wait;
	nextSpecialWait = this_player->next_special_wait;
	spraySpecial = this_player->spray_special;
	specialWeaponFilter = this_player->special_weapon_filter;
	specialWeaponFreq = this_player->special_weapon_freq;
	specialWeaponWpn = this_player->special_weapon_wpn;
	linkToPlayer = this_player->special_link_to_player;
}

static void coop_ship_runtime_save(Player *this_player)
{
	this_player->generator_power = (Uint16)power;
	this_player->generator_power_add = (Uint16)powerAdd;
	this_player->shield_wait = shieldWait;
	memcpy(this_player->shot_repeat, shotRepeat, sizeof(shotRepeat));
	memcpy(this_player->shot_multi_pos, shotMultiPos, sizeof(shotMultiPos));
	this_player->port_config_change = portConfigChange;
	this_player->port_config_done = portConfigDone;
	this_player->option_satellite_rotate = optionSatelliteRotate;
	memcpy(this_player->option_attachment_move, optionAttachmentMove, sizeof(optionAttachmentMove));
	memcpy(this_player->option_attachment_linked, optionAttachmentLinked, sizeof(optionAttachmentLinked));
	memcpy(this_player->option_attachment_return, optionAttachmentReturn, sizeof(optionAttachmentReturn));
	this_player->special_fire_held = fireButtonHeld;
	this_player->zinglon_duration = zinglonDuration;
	this_player->zinglon_ramp = zinglonRamp;
	this_player->astral_duration = astralDuration;
	this_player->flare_duration = flareDuration;
	this_player->flare_start = flareStart;
	this_player->flare_color_change = flareColChg;
	this_player->special_wait = specialWait;
	this_player->next_special_wait = nextSpecialWait;
	this_player->spray_special = spraySpecial;
	this_player->special_weapon_filter = specialWeaponFilter;
	this_player->special_weapon_freq = specialWeaponFreq;
	this_player->special_weapon_wpn = specialWeaponWpn;
	this_player->special_link_to_player = linkToPlayer;
}

void coop_ship_runtime_reset(void)
{
	for (uint p = 0; p < COUNTOF(player); ++p)
	{
		Player *const this_player = &player[p];
		this_player->generator_power = 0;
		this_player->generator_power_add = powerSys[this_player->items.generator].power;
		this_player->shield_wait = 15;
		memset(this_player->shot_repeat, 1, sizeof(this_player->shot_repeat));
		memset(this_player->shot_multi_pos, 0, sizeof(this_player->shot_multi_pos));
		this_player->port_config_change = false;
		this_player->port_config_done = false;
		this_player->option_satellite_rotate = 0.0f;
		memset(this_player->option_attachment_move, 0, sizeof(this_player->option_attachment_move));
		memset(this_player->option_attachment_linked, 1, sizeof(this_player->option_attachment_linked));
		memset(this_player->option_attachment_return, 0, sizeof(this_player->option_attachment_return));
		this_player->special_fire_held = false;
		this_player->zinglon_duration = 0;
		this_player->zinglon_ramp = 0;
		this_player->astral_duration = 0;
		this_player->flare_duration = 0;
		this_player->flare_start = false;
		this_player->flare_color_change = 0;
		this_player->special_wait = 0;
		this_player->next_special_wait = 0;
		this_player->spray_special = false;
		this_player->special_weapon_filter = -99;
		this_player->special_weapon_freq = 0;
		this_player->special_weapon_wpn = 0;
		this_player->special_link_to_player = false;
	}
}

/* Presentation-only slots for the linked Dragonwing's three aim markers. */
int link_marker_slot[3] = { -1, -1, -1 };

void JE_mainGamePlayerFunctions(void)
{
	/* Player movement and input. */
	hud_special_light_tick_begin();

	// Clear freed aim-marker slots before pool reuse.
	link_marker_slot[0] = link_marker_slot[1] = link_marker_slot[2] = -1;

	if (endLevel && levelEnd > 0)
	{
		levelEnd--;
		levelEndWarp++;
	}

	/*Reset Street-Fighter commands*/
	memset(SFExecuted, 0, sizeof(SFExecuted));

	portConfigChange = false;

	if (twoPlayerMode)
	{
		if (dual_ship_mode())
			coop_ship_runtime_load(&player[0]);
		JE_playerMovement(&player[0],
		                  !galagaMode ? inputDevice[0] : 0, 1, shipGr, shipGrPtr,
		                  &mouseX, &mouseY);
		if (dual_ship_mode())
		{
			coop_ship_runtime_save(&player[0]);
			if (!player[0].port_config_change)
				player[0].port_config_done = true;
			coop_ship_runtime_load(&player[1]);
		}
		JE_playerMovement(&player[1],
		                  !galagaMode ? inputDevice[1] : 0, 2, shipGr2, shipGr2ptr,
		                  &mouseXB, &mouseYB);
		if (dual_ship_mode())
		{
			coop_ship_runtime_save(&player[1]);
			if (!player[1].port_config_change)
				player[1].port_config_done = true;
			astralDuration = MAX(player[0].astral_duration, player[1].astral_duration);
			// Use one ship's matched duration and ramp for the shared pillar.
			const uint zinglonLead = player[1].zinglon_duration > player[0].zinglon_duration ? 1 : 0;
			zinglonDuration = player[zinglonLead].zinglon_duration;
			zinglonRamp = player[zinglonLead].zinglon_ramp;
			shotAvail[MAX_PWEAPON - 1] =
				((player[0].zinglon_duration > 1 && player[0].zinglon_duration % 5 == 0) ||
				 (player[1].zinglon_duration > 1 && player[1].zinglon_duration % 5 == 0));
		}
	}
	else
	{
		JE_playerMovement(&player[0],
		                  0, 1, shipGr, shipGrPtr,
		                  &mouseX, &mouseY);
	}

	/* == Parallax Map Scrolling == */
	JE_word tempX;
	if (twoPlayerMode)
		tempX = (player[0].x + player[1].x) / 2;
	else
		tempX = player[0].x;

	// w_f is the shared float driver for all three layers: mapX3Ofs = w_f, mapX2Ofs = (w_f-17)*2/3,
	// mapXOfs = mapX2Ofs/2 (the original coupled 4:2:1 ratio); tempW is its floor. They move together.
	float w_f;
	bool bg2CrispLeft = false;  // Extra Parallax OFF only: render bg2 crisply at the far-left extreme (below)
	if (extraParallax)
	{
		// Pan the near layer edge-to-edge across the ship's actual travel.
		// Derive w_f so deeper layers keep the stock coupled ratio.
		const float travel = (float)((PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN) - SHIP_LEFT_MARGIN);
		float uu = (tempX - SHIP_LEFT_MARGIN) / travel;
		if (uu < 0.0f)
			uu = 0.0f;
		else if (uu > 1.0f)
			uu = 1.0f;
		const float near_flush_left = (float)(PLAYFIELD_LEFT - PLAYFIELD_X_SHIFT);  // 36
		const float near_slack      = (float)(14 * 24 - PLAYFIELD_WIDTH);           // 336 - 299 = 37
		w_f = 3.0f * (near_flush_left - near_slack * uu) + 17.0f;                   // 125 (far-left) .. 14 (far-right)
	}
	else
	{
		// Stock amplitude and normalization; the original parallax formula (the only OFF-mode
		// deviation is the deliberate far-left bg2 sub-pixel snap applied at the very end).
		const float left_bound = 40.0f;
		const float right_bound = PLAYFIELD_WIDTH + 64;
		float u = (tempX - left_bound) / (right_bound - left_bound);
		if (u < 0.0f)
			u = 0.0f;
		else if (u > 1.0f)
			u = 1.0f;
		w_f = (1.0f - u) * (float)(24 * 3);
		bg2CrispLeft = (u <= 0.0f);  // ship pushed fully to the left (parallax pinned at its leftmost)
	}

	tempW = floorf(w_f);
	mapX3Ofs = tempW;
	mapX3Pos = mapX3Ofs % 24;
	mapX3bpPos = 1 - (mapX3Ofs / 24);

	mapX2Ofs   = ((tempW-17) * 2) / 3;
	mapX2Pos   = mapX2Ofs % 24;
	mapX2bpPos = 1 - (mapX2Ofs / 24);

	oldMapXOfs = mapXOfs;
	oldMapXOfs_f = mapXOfs_f;  // both still hold the PREVIOUS tick's value here (updated below)
	mapXOfs    = mapX2Ofs / 2;  // near layer rides half the mid layer; original coupled ratio
	mapXPos    = mapXOfs % 24;
	mapXbpPos  = 1 - (mapXOfs / 24);

	if (background3x1)
	{
		mapX3Ofs = mapXOfs;
		mapX3Pos = mapXPos;
		mapX3bpPos = mapXbpPos - 1;
	}

	// Un-floored mirror of the same offsets (render-list sub-pixel interpolation of the pan),
	// derived from the same w_f so the fraction matches the integer offsets above.
	mapX3Ofs_f = w_f;
	mapX2Ofs_f = ((w_f - 17.0f) * 2.0f) / 3.0f;
	mapXOfs_f  = mapX2Ofs_f / 2.0f;
	if (background3x1)
		mapX3Ofs_f = mapXOfs_f;

	// At the stock left edge, snap bg2 interpolation to the integer anchor to prevent a one-pixel shift.
	if (bg2CrispLeft)
		mapX2Ofs_f = (float)mapX2Ofs;

	// Clamp bg2 to -1 so its 336-pixel strip covers the right edge in both integer and smooth paths.
	if (mapX2Ofs < -1)
	{
		mapX2Ofs = -1;
		mapX2Pos = mapX2Ofs % 24;
		mapX2bpPos = 1 - (mapX2Ofs / 24);
	}
	if (mapX2Ofs_f < -1.0f)
		mapX2Ofs_f = -1.0f;
}

// Find the next `]L` name at or after a section, including routing sections with no name of their
// own. The file-size bound protects the die-on-EOF reader from invalid section numbers.
static void JE_getLevelName(int levelNum, char *out, size_t outSize)
{
	if (outSize == 0)
		return;
	out[0] = '\0';

	FILE *f = dir_fopen(JE_episodeDir(), episode_file, "rb");
	if (f == NULL)
		return;

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);

	char s[256];

	// Seek past the first `levelNum` section markers ('*') to the target section.
	int x = 0;
	while (x < levelNum && ftell(f) < fsize)
	{
		read_encrypted_pascal_string(s, sizeof(s), f);
		if (s[0] == '*')
			x++;
	}

	// Find the next "]L" declaration (across sections, to resolve routing sections).
	while (ftell(f) < fsize)
	{
		s[0] = '\0';
		read_encrypted_pascal_string(s, sizeof(s), f);
		if (s[0] == ']' && s[1] == 'L' && strlen(s) >= 13)
		{
			char name[10];
			SDL_strlcpy(name, s + 13, sizeof(name));  // 9-char name field, as the game does
			for (int i = (int)strlen(name) - 1; i >= 0 && name[i] == ' '; --i)
				name[i] = '\0';  // trim the space padding
			SDL_strlcpy(out, name, outSize);
			break;
		}
	}

	fclose(f);
}

void JE_playerCollide(Player *this_player, JE_byte playerNum_)
{
	char tempStr[256];

	for (int z = 0; z < 100; z++)
	{
		if (enemyAvail[z] != 1)
		{
			int enemy_screen_x = enemy[z].ex + enemy[z].mapoffset;

			// Both refs sit ~equally left/up of their sprite centres (ship +7/+7,
			// enemy +6/+7), so the ref-to-ref test is effectively centre-to-centre.
			bool touching = abs(this_player->x - enemy_screen_x) < 12 && abs(this_player->y - enemy[z].ey) < 14;

			// Sampled before the branch below clears enemyAvail, which endlessSpecialPickup reads.
			const bool specialPickup = endlessSpecialPickup(z);

			// An Endless "?" draws one small glyph inside that footprint, so it keeps the vanilla
			// rule (ship centre inside the pickup's box) measured against the glyph.
			if (specialPickup)
			{
				const int shipCentreX = this_player->x + 7;
				const int shipCentreY = this_player->y + 7;
				touching = shipCentreX >= enemy_screen_x + ENDLESS_SPECIAL_GLYPH_X0
				        && shipCentreX <= enemy_screen_x + ENDLESS_SPECIAL_GLYPH_X1
				        && shipCentreY >= enemy[z].ey + ENDLESS_SPECIAL_GLYPH_Y0
				        && shipCentreY <= enemy[z].ey + ENDLESS_SPECIAL_GLYPH_Y1;
			}

			if (touching)
			{   /*Collide*/
				int evalue = enemy[z].evalue;
				if (evalue > 29999)
				{
					if (evalue == 30000)  // spawn dragonwing in galaga mode, otherwise just a purple ball
					{
						player_award_pickup_cash(this_player, 100);

						if (!galagaMode)
						{
							handle_got_purple_ball(this_player);
						}
						else
						{
							// spawn the dragonwing?
							if (twoPlayerMode)
								player_award_pickup_cash(this_player, 2400);
							// Co-op already flies two independent ships: the ball only pays,
							// and must not link the pair or clobber the second ship's state.
							if (!coop_mode_active())
							{
								twoPlayerMode = true;
								twoPlayerLinked = true;
								player[1].items.weapon[REAR_WEAPON].power = 1;
								arcade_rescale_to_lives(&player[1]);  // that power IS the spawned wing's life count
								player[1].armor = 10;
								player[1].is_alive = true;
							}
						}
						enemyAvail[z] = 1;
						soundQueue[7] = S_POWERUP;
					}
					else if (superArcadeMode != SA_NONE && evalue > 30000)
					{
						shotMultiPos[SHOT_FRONT] = 0;
						shotRepeat[SHOT_FRONT] = 10;

						/* A ball's color indexes the collecting ship's arsenal. This matters
						 * online, where the two Super Arcade ships may differ. */
						tempW = player_sa_ball_weapon(this_player, (uint)(evalue - 30000 - 1));

						// if picked up already-owned weapon, power weapon up
						if (tempW == this_player->items.weapon[FRONT_WEAPON].id)
						{
							player_award_pickup_cash(this_player, 1000);
							power_up_weapon(this_player, FRONT_WEAPON);
						}
						// else weapon also gives purple ball
						else
						{
							handle_got_purple_ball(this_player);
						}

						this_player->items.weapon[FRONT_WEAPON].id = tempW;
						if (dual_ship_mode())
							this_player->shot_multi_pos[SHOT_FRONT] = 0;
						player_award_pickup_cash(this_player, 200);
						soundQueue[7] = S_POWERUP;
						enemyAvail[z] = 1;
					}
					else if (evalue > 32100)
					{
						if (playerNum_ == 1 || dual_ship_mode())
						{
							player_award_pickup_cash(this_player, 250);
							this_player->items.special = evalue - 32100;
							shotMultiPos[SHOT_SPECIAL] = 0;
							shotRepeat[SHOT_SPECIAL] = 10;
							shotMultiPos[SHOT_SPECIAL2] = 0;
							shotRepeat[SHOT_SPECIAL2] = 0;
							if (dual_ship_mode())
							{
								this_player->shot_multi_pos[SHOT_SPECIAL] = 0;
								this_player->shot_repeat[SHOT_SPECIAL] = 10;
								this_player->shot_multi_pos[SHOT_SPECIAL2] = 0;
								this_player->shot_repeat[SHOT_SPECIAL2] = 0;
							}
							hud_special_light_rearm((uint)(this_player - player));

							if (isNetworkGame)
								snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(dual_ship_mode() ? playerNum_ : 1), miscTextB[4-1], JE_specialName((JE_byte)(evalue - 32100)));
							else if (twoPlayerMode)
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[43-1], JE_specialName((JE_byte)(evalue - 32100)));
							else
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[64-1], JE_specialName((JE_byte)(evalue - 32100)));
							JE_drawTextWindow(tempStr);
							soundQueue[7] = S_POWERUP;
							enemyAvail[z] = 1;
						}
					}
					else if (evalue > 32000)
					{
						if (dual_ship_mode())
						{
							enemyAvail[z] = 1;
							for (uint i = 0; i < COUNTOF(this_player->items.sidekick); ++i)
								this_player->items.sidekick[i] = evalue - 32000;
							this_player->shot_multi_pos[SHOT_LEFT_SIDEKICK] = 0;
							this_player->shot_multi_pos[SHOT_RIGHT_SIDEKICK] = 0;
							snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(playerNum_), miscTextB[4-1], options[evalue - 32000].name);
							JE_drawTextWindow(tempStr);
							JE_resetPlayerOptions(this_player);
							JE_drawOptionsHUD();
							soundQueue[7] = S_POWERUP;
						}
						else if (playerNum_ == 2)
						{
							enemyAvail[z] = 1;
							if (isNetworkGame)
								snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(2), miscTextB[4-1], options[evalue - 32000].name);
							else
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[44-1], options[evalue - 32000].name);
							JE_drawTextWindow(tempStr);

							// New sidekicks start at minimum power; matching pickups raise their power.
							if (evalue - 32000u != player[1].items.sidekick_series)
							{
								player[1].items.sidekick_series = evalue - 32000;
								player[1].items.sidekick_level = 101;
							}
							else if (player[1].items.sidekick_level < 103)
							{
								++player[1].items.sidekick_level;
							}

							uint temp = player[1].items.sidekick_level - 100 - 1;
							for (uint i = 0; i < COUNTOF(player[1].items.sidekick); ++i)
								player[1].items.sidekick[i] = optionSelect[player[1].items.sidekick_series][temp][i];

							shotMultiPos[SHOT_LEFT_SIDEKICK] = 0;
							shotMultiPos[SHOT_RIGHT_SIDEKICK] = 0;
							JE_drawOptions();
							soundQueue[7] = S_POWERUP;
						}
						else if (onePlayerAction)
						{
							enemyAvail[z] = 1;
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[64-1], options[evalue - 32000].name);
							JE_drawTextWindow(tempStr);

							for (uint i = 0; i < COUNTOF(player[0].items.sidekick); ++i)
								player[0].items.sidekick[i] = evalue - 32000;
							shotMultiPos[SHOT_LEFT_SIDEKICK] = 0;
							shotMultiPos[SHOT_RIGHT_SIDEKICK] = 0;

							JE_drawOptions();
							soundQueue[7] = S_POWERUP;
						}
						if (enemyAvail[z] == 1)
							player_award_pickup_cash(this_player, 250);
					}
					else if (evalue > 31000)
					{
						player_award_pickup_cash(this_player, 250);
						if (dual_ship_mode())
						{
							snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(playerNum_), miscTextB[4-1], weaponPort[evalue - 31000].name);
							JE_drawTextWindow(tempStr);
							this_player->items.weapon[REAR_WEAPON].id = evalue - 31000;
							this_player->shot_multi_pos[SHOT_REAR] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;
						}
						else if (playerNum_ == 2)
						{
							if (isNetworkGame)
								snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(2), miscTextB[4-1], weaponPort[evalue - 31000].name);
							else
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[44-1], weaponPort[evalue - 31000].name);
							JE_drawTextWindow(tempStr);
							player[1].items.weapon[REAR_WEAPON].id = evalue - 31000;
							shotMultiPos[SHOT_REAR] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;
						}
						else if (onePlayerAction)
						{
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[64-1], weaponPort[evalue - 31000].name);
							JE_drawTextWindow(tempStr);
							player[0].items.weapon[REAR_WEAPON].id = evalue - 31000;
							shotMultiPos[SHOT_REAR] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;

							if (player[0].items.weapon[REAR_WEAPON].power == 0)  // does this ever happen?
								player[0].items.weapon[REAR_WEAPON].power = 1;
						}
					}
					else if (evalue > 30000)
					{
						if (dual_ship_mode() || (playerNum_ == 1 && twoPlayerMode))
						{
							if (isNetworkGame)
								snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(dual_ship_mode() ? playerNum_ : 1), miscTextB[4-1], weaponPort[evalue - 30000].name);
							else
								snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[43-1], weaponPort[evalue - 30000].name);
							JE_drawTextWindow(tempStr);
							Player *const pickup_player = dual_ship_mode() ? this_player : &player[0];
							pickup_player->items.weapon[FRONT_WEAPON].id = evalue - 30000;
							shotMultiPos[SHOT_FRONT] = 0;
							if (dual_ship_mode())
								pickup_player->shot_multi_pos[SHOT_FRONT] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;
						}
						else if (onePlayerAction)
						{
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[64-1], weaponPort[evalue - 30000].name);
							JE_drawTextWindow(tempStr);
							player[0].items.weapon[FRONT_WEAPON].id = evalue - 30000;
							shotMultiPos[SHOT_FRONT] = 0;
							enemyAvail[z] = 1;
							soundQueue[7] = S_POWERUP;
						}

						if (enemyAvail[z] == 1 && !coop_mode_active())
						{
							// The arcade gun's paired special follows the same ship the gun went to.
							Player *const special_player = arcade_separate_mode() ? this_player : &player[0];
							special_player->items.special = specialArcadeWeapon[evalue - 30000-1];
							if (special_player->items.special > 0)
							{
								shotMultiPos[SHOT_SPECIAL] = 0;
								shotRepeat[SHOT_SPECIAL] = 0;
								shotMultiPos[SHOT_SPECIAL2] = 0;
								shotRepeat[SHOT_SPECIAL2] = 0;
								if (arcade_separate_mode())
								{
									special_player->shot_multi_pos[SHOT_SPECIAL] = 0;
									special_player->shot_repeat[SHOT_SPECIAL] = 0;
									special_player->shot_multi_pos[SHOT_SPECIAL2] = 0;
									special_player->shot_repeat[SHOT_SPECIAL2] = 0;
								}
								hud_special_light_rearm((uint)(special_player - player));
							}
							player_award_pickup_cash(this_player, 250);
						}

					}
				}
				else if (evalue > 20000)
				{
					if (twoPlayerLinked)
					{
						// share the armor evenly between linked players
						for (uint i = 0; i < COUNTOF(player); ++i)
						{
							player[i].armor += (evalue - 20000) / COUNTOF(player);
							const uint armorCap = arcade_life_scaling_active() ? player[i].initial_armor : 28;
							if (player[i].armor > armorCap)
								player[i].armor = armorCap;
						}
					}
					else
					{
						this_player->armor += evalue - 20000;
					// Endless and arcade use their raised hull ceiling; campaign effects never lower the classic 28.
					const uint armorCap = (endlessMode || arcade_life_scaling_active())
					                    ? this_player->initial_armor
					                    : (endlessCampaignMods && this_player->initial_armor > 28)
					                      ? this_player->initial_armor : 28;
						if (this_player->armor > armorCap)
							this_player->armor = armorCap;
					}
					enemyAvail[z] = 1;
					VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
					JE_drawArmor();
					VGAScreen = game_screen; /* side-effect of game_screen */
					soundQueue[7] = S_POWERUP;
				}
				else if (evalue > 10000 && enemyAvail[z] == 2)
				{
					if (endlessMode)
					{
						enemyAvail[z] = 1;
						soundQueue[7] = S_POWERUP;
						endlessGrantSpecial((uint)(this_player - &player[0]));
					}
					else if (!bonusLevel)
					{
						play_song(30);  /*Zanac*/
						bonusLevel = true;
						nextLevel = evalue - 10000;
						enemyAvail[z] = 1;

						// Show the secret destination in the temporary HUD message.
						char secretName[16];
						JE_getLevelName(nextLevel, secretName, sizeof(secretName));
						if (secretName[0] != '\0')
							snprintf(tempStr, sizeof(tempStr), "Secret Level: %s", secretName);
						else
							strcpy(tempStr, "Secret Level!");
						JE_drawTextWindow(tempStr);
					}
				}
				else if (enemy[z].scoreitem)
				{
					enemyAvail[z] = 1;
					soundQueue[7] = S_ITEM;
					if (evalue == 1)
					{
						cubeMax++;
						soundQueue[3] = V_DATA_CUBE;
										if (endlessMode)
										{
											cubeMax--;
											soundQueue[3] = S_POWERUP;
											endlessGrantSpecial((uint)(this_player - &player[0]));
										}
					}
					else if (evalue == -1)  // got front weapon powerup
					{
						if (isNetworkGame)
							snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(dual_ship_mode() ? playerNum_ : 1), miscTextB[4-1], miscText[45-1]);
						else if (twoPlayerMode)
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[43-1], miscText[45-1]);
						else
							strcpy(tempStr, miscText[45-1]);
						JE_drawTextWindow(tempStr);

						power_up_weapon(dual_ship_mode() ? this_player : &player[0], FRONT_WEAPON);
						soundQueue[7] = S_POWERUP;
					}
					else if (evalue == -2)  // got rear weapon powerup
					{
						if (isNetworkGame)
							snprintf(tempStr, sizeof(tempStr), "%s %s %s", JE_getName(dual_ship_mode() ? playerNum_ : 2), miscTextB[4-1], miscText[46-1]);
						else if (twoPlayerMode)
							snprintf(tempStr, sizeof(tempStr), "%s %s", miscText[44-1], miscText[46-1]);
						else
							strcpy(tempStr, miscText[46-1]);
						JE_drawTextWindow(tempStr);

						power_up_weapon(dual_ship_mode() ? this_player : (twoPlayerMode ? &player[1] : &player[0]), REAR_WEAPON);
						soundQueue[7] = S_POWERUP;
					}
					else if (evalue == -3)
					{
						// picked up orbiting asteroid killer
						shotMultiPos[SHOT_MISC] = 0;
						b = player_shot_create(0, SHOT_MISC, this_player->x, this_player->y, mouseX, mouseY, 104, playerNum_);
						shotAvail[z] = 0;
					}
					else if (evalue == -4)
					{
						if (player[playerNum_-1].superbombs < 10)
							++player[playerNum_-1].superbombs;
					}
					else if (evalue == -5)
					{
						Player *const hotdog_player = dual_ship_mode() ? this_player : &player[0];
						hotdog_player->items.weapon[FRONT_WEAPON].id = 25;  // HOT DOG!
						hotdog_player->items.weapon[REAR_WEAPON].id = 26;
						if (!dual_ship_mode())
							player[1].items.weapon[REAR_WEAPON].id = 26;

						hotdog_player->last_items = hotdog_player->items;

						if (dual_ship_mode())
						{
							hotdog_player->weapon_mode = 1;
							memset(hotdog_player->shot_multi_pos, 0, sizeof(hotdog_player->shot_multi_pos));
						}
						else
						{
							for (uint i = 0; i < COUNTOF(player); ++i)
								player[i].weapon_mode = 1;
						}

						memset(shotMultiPos, 0, sizeof(shotMultiPos));
					}
					else
					{
						// Bounty Hunter multiplies this where the Endless effect layer runs.
						const uint collector = (uint)(this_player - &player[0]);
						const long picked = endlessScorePickupValue(collector, evalue);
						if (twoPlayerLinked)
						{
							// players get equal share of pick-up cash when linked
							for (uint i = 0; i < COUNTOF(player); ++i)
								player_add_cash(&player[i], picked / (long)COUNTOF(player));
						}
						else
						{
							player_award_pickup_cash(this_player, picked);
						}
					}
					// The authored graphic names the original item; a data cube's spells out "DATA".
					if (!specialPickup)
						JE_setupExplosion(enemy_screen_x, enemy[z].ey, 0,
						                  enemy_explosion_type(&enemy[z]), true, false);
				}
				// Low Profile shrinks damaging collisions, never pickup range.
				else if ((this_player->invulnerable_ticks == 0
				          || endlessRamWhileInvulnerable(this_player->invulnerable_ticks)) &&
				         enemyAvail[z] == 0 && !noclipMode &&
				         (enemyDat[enemy[z].enemytype].explosiontype & 1) == 0 && // explosiontype & 1 == 0: not ground enemy
				         abs(this_player->x - enemy_screen_x) < endlessHitboxScale(12) &&
				         abs(this_player->y - enemy[z].ey) < endlessHitboxScale(14))
				{
					// The ram perks and the kill credit below belong to the ship doing the ramming.
					endlessSetFxPlayer((uint)(this_player - &player[0]));

					int armorleft = enemy[z].armorleft;
					if (armorleft > damageRate)
						armorleft = damageRate;

					// Ram bonuses affect enemy damage only; the ship still takes the stock share.
					const int knifePct = endlessFxActive() ? endlessPerkKnifeFightPercent((unsigned)z) : 0;
					int damage_to_enemy = endlessPerkProwRamDamage(damageRate);
					const int knifeRam = endlessPerkKnifeFightBonus(damage_to_enemy, knifePct);
					if (knifeRam > 0)
						endlessPerkKnifeFightBlood((unsigned)z, knifePct);
					// An open Opening Salvo window lifts the ram as it lifts a volley. Its lift and
					// Ramming uses Opening Salvo without spending it; Knife Fight adds separately.
					damage_to_enemy = endlessOpeningSalvoScale(damage_to_enemy) + knifeRam;
					if (damage_to_enemy > enemy[z].armorleft)
						damage_to_enemy = enemy[z].armorleft;

					int playerHit = armorleft;
					if (endlessFxActive() && (endlessActiveMods & ENDLESS_MOD_RAMPAGE))
						playerHit = playerHit * 3 / 2;
					// Apply incoming ram modifiers in one rounded pass.
					if (endlessFxActive() && playerHit > 0)
					{
						const Sint64 pct = (Sint64)endlessContactDamagePercent()
						                 * endlessEliteContactPercent(enemy[z].eliteState)
						                 * endlessPerkProwContactPercent();
						const Sint64 scale = 100LL * 100 * 100;
						playerHit = (int)((playerHit * pct + scale / 2) / scale);
						if (playerHit < 1)
							playerHit = 1;   // a real hit still costs a point
					}
					if (playerHit > 255)
						playerHit = 255;
					// On the Ramming cadence above, an invulnerable ship rams without being rammed.
					if (this_player->invulnerable_ticks == 0)
						JE_playerDamage((JE_byte)playerHit, this_player);

					// player ship gets push-back from collision
					if (enemy[z].armorleft > 0)
					{
						this_player->x_velocity += (enemy[z].exc * enemy[z].armorleft) / 2;
						this_player->y_velocity += (enemy[z].eyc * enemy[z].armorleft) / 2;
					}

					// Nx boss HP (expert mode and/or endless depth) and the endless tier, spent
					// through the hull's accumulator: 1 armor per N damage dealt.
					damage_to_enemy = enemy_spend_damage(z, damage_to_enemy);

					int armorleft2 = enemy[z].armorleft;
					if (armorleft2 == 255)
						armorleft2 = 30000;

					temp = enemy[z].linknum;
					if (temp == 0)
						temp = 255;

					b = z;

					if (armorleft2 > damage_to_enemy)
					{
						// damage enemy
						if (enemy[z].armorleft != 255)
						{
							if (!enemy[z].healthbar_seen)
							{
								enemy_note_full_armor(&enemy[z]);
								enemy[z].healthbar_seen = true;
							}
							enemy[z].armorleft -= damage_to_enemy;
						}
						soundQueue[5] = S_ENEMY_HIT;
					}
					else if (reworkActive(REWORK_RAMMING))
					{
						// A ram kill is a kill, taken the way a killing shot takes it and
						// credited to the ship that rammed.
						enemy_kill_group((unsigned)z, (int)playerNum_ - 1, (int)playerNum_ - 1);
					}
					else
					{
						// A ram removes the enemy without awarding a kill: no payout, no bounty, no
						// death effects.
						for (temp2 = 0; temp2 < 100; temp2++)
						{
							if (enemyAvail[temp2] != 1)
							{
								temp3 = enemy[temp2].linknum;
								if (temp2 == b ||
									(temp != 255 &&
									 (temp == temp3 || temp - 100 == temp3 ||
									  (temp3 > 40 && temp3 / 20 == temp / 20 && temp3 <= temp))))
								{
									int enemy_screen_x = enemy[temp2].ex + enemy[temp2].mapoffset;

									enemy[temp2].linknum = 0;

									enemyAvail[temp2] = 1;

									explosionFilter = endlessEliteTint(enemy[temp2].eliteState);
									if (enemyDat[enemy[temp2].enemytype].esize == 1)
									{
										JE_setupExplosionLarge(enemy[temp2].enemyground, enemy[temp2].explonum, enemy_screen_x, enemy[temp2].ey);
										soundQueue[6] = S_EXPLOSION_9;
									}
									else
									{
										JE_setupExplosion(enemy_screen_x, enemy[temp2].ey, 0, 1, false, false);
										explosion_rumble_add(EXPLOSION_RUMBLE_SMALL, false,
										                     enemy_screen_x, enemy[temp2].ey);
										soundQueue[5] = S_EXPLOSION_4;
									}
									explosionFilter = 0;
								}
							}
						}
						enemyAvail[z] = 1;
					}
				}
			}

		}
	}
}

void JE_resetPlayerOptions(Player *this_player)
{
	// Personal perks: Ordnance Reserves and Rapid Recharge size this ship's own magazines.
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer((uint)(this_player - player));

	for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
	{
		JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

		// Scale refill cadence so larger Ordnance Reserves magazines fill in the stock time.
		this_player->sidekick[i].ammo =
		this_player->sidekick[i].ammo_max = endlessPerkSidekickAmmo(this_option->ammo);

		this_player->sidekick[i].ammo_refill_ticks =
		this_player->sidekick[i].ammo_refill_ticks_max =
			endlessPerkSidekickRefillTicks((105 - this_option->ammo) * 4, this_option->ammo);

		this_player->sidekick[i].style = this_option->tr;

		this_player->sidekick[i].animation_enabled = (this_option->option == 1);
		this_player->sidekick[i].animation_frame = 0;

		this_player->sidekick[i].charge = 0;
		this_player->sidekick[i].charge_ticks = endlessPerkChargeTicks(20);
	}

	endlessSetFxPlayer(fxSaved);
}

/* Which ships carry the Endless effect layer: both of them in an online co-op run, player 1
 * otherwise (the campaign debug layer only ever equips one). */
static bool endlessFxShip(const Player *this_player)
{
	return endlessFxActive() && (coopEndlessMode || this_player == &player[0]);
}

/* Damage sites read the struck ship's generator reserve, not the weapon code's power scratch. */
static int endlessGeneratorGet(const Player *this_player)
{
	return dual_ship_mode() ? (int)this_player->generator_power : (int)power;
}

static void endlessGeneratorSet(Player *this_player, int v)
{
	if (v < 0)
		v = 0;
	if (v > 900)
		v = 900;
	if (dual_ship_mode())
		this_player->generator_power = (Uint16)v;
	else
		power = (uint)v;
}

/* Rewind both the ordinary special cadence and the flare-imposed wait. */
static void endlessKineticCoolSpecial(Player *this_player)
{
	JE_byte *const clocks[2] = {
		dual_ship_mode() ? &this_player->shot_repeat[SHOT_SPECIAL] : &shotRepeat[SHOT_SPECIAL],
		dual_ship_mode() ? &this_player->special_wait              : &specialWait,
	};

	for (uint i = 0; i < COUNTOF(clocks); ++i)
		*clocks[i] -= (JE_byte)endlessPerkKineticCooldownCut(*clocks[i]);
}

/* Feed sidekick magazines and charge ramps; mark event-drawn ammo gauges dirty. */
static void endlessKineticFeedSidekicks(Player *this_player, int rounds, int stages)
{
	bool refilled = false;

	for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
	{
		const int cap = this_player->sidekick[i].ammo_max;
		if (rounds > 0 && cap > 0 && this_player->sidekick[i].ammo < cap)
		{
			this_player->sidekick[i].ammo = MIN(this_player->sidekick[i].ammo + rounds, cap);
			refilled = true;
		}

		// options[].pwr is the pod's top charge stage; 0 means it has no ramp to advance.
		const uint stageCap = options[this_player->items.sidekick[i]].pwr;
		if (stages > 0 && this_player->sidekick[i].charge < stageCap)
			this_player->sidekick[i].charge =
				MIN(this_player->sidekick[i].charge + (uint)stages, stageCap);
	}

	if (refilled && (uint)(this_player - player) == hud_sidekick_player_index())
		hud_sidekicks_dirty = true;
}

// Clear enemy shots within this ship's Countermeasures reach. Return the number cleared.
int endlessCountermeasureBurst(Player *this_player)
{
	const int cmRadius = endlessPerkCountermeasureRadius();
	if (cmRadius <= 0)
		return 0;

	// The perk's radius begins at the hull edge, not the ship's centre.
	const int reachX = (int)this_player->shot_hit_area_x + cmRadius;
	const int reachY = (int)this_player->shot_hit_area_y + cmRadius;

	int swept = 0;
	for (int es = 0; es < ENEMY_SHOT_MAX; ++es)
	{
		if (!enemyShotAvail[es]
		    && abs(enemyShot[es].sx - this_player->x) <= reachX
		    && abs(enemyShot[es].sy - this_player->y) <= reachY)
		{
			enemy_shot_vaporise_sparks(es);
			JE_setupExplosion(enemyShot[es].sx, enemyShot[es].sy, 0, 0, false, false);
			enemyShotAvail[es] = true;
			++swept;
		}
	}

	// Announce only a burst that cleared at least one shot.
	if (swept > 0)
	{
		JE_setupExplosion(this_player->x - reachX, this_player->y, 0, 0, false, false);
		JE_setupExplosion(this_player->x + reachX, this_player->y, 0, 0, false, false);
		JE_setupExplosion(this_player->x, this_player->y - reachY, 0, 0, false, false);
		JE_setupExplosion(this_player->x, this_player->y + reachY, 0, 0, false, false);
		soundQueue[4] = S_WEAPON_7;  // point-defense "thunk"
	}

	return swept;
}

static void shield_hit_ring(const Player *this_player)
{
	JE_setupExplosion(this_player->x - 17, this_player->y - 12, 0, 14, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x - 5 , this_player->y - 12, 0, 15, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x + 7 , this_player->y - 12, 0, 16, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x + 19, this_player->y - 12, 0, 17, false, !twoPlayerMode);

	JE_setupExplosion(this_player->x - 17, this_player->y + 2 , 0, 18, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x + 19, this_player->y + 2 , 0, 19, false, !twoPlayerMode);

	JE_setupExplosion(this_player->x - 17, this_player->y + 16, 0, 20, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x - 5 , this_player->y + 16, 0, 21, false, !twoPlayerMode);
	JE_setupExplosion(this_player->x + 7 , this_player->y + 16, 0, 22, false, !twoPlayerMode);
}

// The input device steering `seat`'s ship, as JE_playerMovement routes it, or -1 where no
// controller here does: in a demo, and for the remote seat of an online game.
static int seat_rumble_device(uint seat)
{
	if (play_demo || (isNetworkGame && seat + 1 != thisPlayerNum))
		return -1;

	const bool ownDevice = twoPlayerMode && !isNetworkGame && !galagaMode;
	return ownDevice ? inputDevice[seat] : 0;
}

static void ship_rumble(const Player *this_player, float low, float high, Uint32 duration_ms)
{
	const int device = seat_rumble_device((uint)(this_player - &player[0]));
	if (device >= 0)
		joystick_rumble(device, low, high, duration_ms);
}

void player_rumble_level(const float low[2], const float high[2], Uint32 hold_ms)
{
	int device[2] = { seat_rumble_device(0), twoPlayerMode ? seat_rumble_device(1) : -1 };
	float lo[2] = { low[0], low[1] }, hi[2] = { high[0], high[1] };

	// One controller steering both ships, as in a solo Galaga level, feels the stronger.
	if (device[1] >= 0 && device[1] == device[0])
	{
		lo[0] = MAX(lo[0], lo[1]);
		hi[0] = MAX(hi[0], hi[1]);
		device[1] = -1;
	}

	for (uint seat = 0; seat < 2; ++seat)
		if (device[seat] >= 0)
			joystick_rumble_level(device[seat], lo[seat], hi[seat], hold_ms);
}

JE_byte JE_playerDamage(JE_byte temp,
                        Player *this_player)
{
	// Nitro and the rest of the personal deals belong to the ship being hit.
	endlessSetFxPlayer((uint)(this_player - &player[0]));

	int playerDamage = 0;
	soundQueue[7] = S_SHIELD_HIT;

	const int oldShield = this_player->shield;
	const int oldArmor  = this_player->armor;
	const bool wasAlive = this_player->is_alive;

	if (cheatInfiniteShields)
		return 0;

	// Endless Bulwark relic: soften each incoming hit by a flat amount, but always leave at
	// least 1 damage (only the main player carries perks; a lone hit of 0 is left untouched).
	if (endlessFxShip(this_player) && temp > 1)
	{
		int t = (int)temp - endlessPlayerDamageReduce();
		temp = (t < 1) ? 1 : (JE_byte)t;
	}

	// Nitro (gamble deal): the hull is stripped for raw firepower, so any hit that lands is fatal.
	if (endlessFxShip(this_player) && (endlessPlayerMods[endlessFxPlayer()] & ENDLESS_MOD_NITRO))
		temp = 255;

	// Countermeasure Suite arms only when damage reaches the hull.
	bool cmHullHit = false;

	/* Player Damage Routines */
	if (this_player->shield < temp)
	{
		playerDamage = temp;
		temp -= this_player->shield;
		this_player->shield = 0;

		// Aegis Gate spends the remaining shield and blocks overflow before hull-hit
		// effects. The helper also starts its cooldown.
		if (endlessFxShip(this_player) && temp > 0
		    && endlessAegisGateConsume(oldShield, temp))
		{
			temp = 0;
			// Mark a block with the full shield ring and its unique clink sound.
			shield_hit_ring(this_player);
			soundQueue[4] = S_CLINK;  // Deflection cue.
		}

		if (temp > 0)
			cmHullHit = true;

		if (temp > 0 && !cheatInfiniteArmor)
		{
			/*Through Shields - Now Armor */

			if (this_player->armor < temp)
			{
				temp -= this_player->armor;
				this_player->armor = 0;

				if (this_player->is_alive && !youAreCheating)
				{
					if (endlessMode && endlessConsumeRevive((uint)(this_player - &player[0])))
					{
						// A revive restores armor and stuns enemy fire. Clear the current volley,
						// show each bullet popping, and grant brief invulnerability.
						this_player->invulnerable_ticks = 100;
						for (int es = 0; es < ENEMY_SHOT_MAX; ++es)
						{
							if (!enemyShotAvail[es])
								JE_setupExplosion(enemyShot[es].sx, enemyShot[es].sy, 0, 0, false, false);
							enemyShotAvail[es] = 1;
						}
						soundQueue[3] = S_POWERUP;
					}
					else
					{
						if (!timedBattleMode)
							levelTimer = false;
						this_player->is_alive = false;
						this_player->exploding_ticks = 60;
						// A co-op ship that goes down spectates until the zone ends; the outpost the
						// survivor reaches puts it back in the air. Both down is an ordinary death.
						if (coopEndlessMode)
							endlessPlayerDowned[this_player - &player[0]] = true;
						levelEnd = 40;
						tempVolume = tyrMusicVolume;
						soundQueue[1] = S_EXPLOSION_22;
					}
				}
			}
			else
			{
				this_player->armor -= temp;
				soundQueue[7] = S_HULL_HIT;
			}
		}
	}
	else
	{
		this_player->shield -= temp;

		// Shield-hit explosions follow the hull's local presentation style.
		explosionOpacity = netStyleForSeat((this_player == &player[1]) ? 1u : 0u).opacity;

		shield_hit_ring(this_player);

		explosionOpacity = NET_STYLE_SOLID;
	}

	// Arm presentation-only gauge flashes only on the live pass; rollback must not restart them.
	const int gi = (this_player == &player[1]) ? 1 : 0;
	if (!rollback_resim && gaugeFlashShield && this_player->shield < (uint)oldShield)
		shieldGaugeFlash[gi] = GAUGE_FLASH_START;
	if (!rollback_resim && gaugeFlashArmor && this_player->armor < (uint)oldArmor)
		armorGaugeFlash[gi] = GAUGE_FLASH_START;

	// cmHullHit still marks a hull hit when the infinite-armor cheat keeps armor from dropping.
	if (!rollback_resim)
	{
		if (wasAlive && !this_player->is_alive)
		{
			ship_rumble(this_player, 1.0f, 0.6f, 600);
		}
		else if (cmHullHit)
		{
			// A hit rumbles harder the more armor it takes, up to 12 points.
			const int taken = oldArmor - (int)this_player->armor;
			const float severity = MIN((float)MAX(taken, 0) / 12.0f, 1.0f);
			ship_rumble(this_player, 0.45f + 0.45f * severity, 0.3f + 0.3f * severity, 220);
		}
		else if (this_player->shield < (uint)oldShield)
		{
			ship_rumble(this_player, 0.25f, 0.15f, 120);
		}
	}

	/* Kinetic Converter's generator share needs shield absorption; its other effects need any hit. */
	if (endlessFxShip(this_player) && (this_player->shield < (uint)oldShield || cmHullHit))
	{
		// Re-cap at the generator ceiling: the tick's own recharge/cap already ran.
		// shields[].tpwr is the shield's per-point charge cost, the natural power<->shield rate.
		if (this_player->shield < (uint)oldShield)
		{
			const int gained = endlessPerkKineticPower(oldShield - this_player->shield,
			                                           shields[this_player->items.shield].tpwr);
			if (gained > 0)
				endlessGeneratorSet(this_player, endlessGeneratorGet(this_player) + gained);
		}

		endlessKineticCoolSpecial(this_player);

		// Rounds are stateful: this is the one call per hit, so its answer has to be spent here.
		endlessKineticFeedSidekicks(this_player, endlessPerkKineticAmmoRounds(),
		                            endlessPerkKineticChargeStages());
	}

	if (endlessFxShip(this_player) && cmHullHit)
		endlessCountermeasureBurst(this_player);

	// Failsafe extends hull-hit invulnerability without shortening a longer revive/respawn window.
	if (endlessFxShip(this_player) && cmHullHit && this_player->is_alive)
	{
		const uint failsafe = (uint)endlessPerkFailsafeTicks();
		if (this_player->invulnerable_ticks < failsafe)
			this_player->invulnerable_ticks = failsafe;
	}

	JE_wipeShieldArmorBars();
	VGAScreen = VGAScreenSeg; /* side-effect of game_screen */
	JE_drawShield();
	JE_drawArmor();
	VGAScreen = game_screen; /* side-effect of game_screen */

	if (endlessFxShip(this_player))
		endlessRegenHitTaken();

	// Static Discharge uses the actual shield and armor loss because the return value is zero for a
	// fully absorbed hit. Drain only the affected ship's available generator reserve.
	if (endlessFxShip(this_player))
	{
		int lost = 0;
		if (this_player->shield < (uint)oldShield) lost += oldShield - this_player->shield;
		if (this_player->armor  < (uint)oldArmor)  lost += oldArmor  - this_player->armor;
		if (lost > 0)
		{
			const int drain = (int)endlessStaticDischargeDrain((unsigned)lost);
			endlessGeneratorSet(this_player, endlessGeneratorGet(this_player) - drain);
		}
	}

	return playerDamage;
}

/* How many firing patterns one ship's rear bay offers. */
JE_word JE_portConfigs(const Player *this_player)
{
	return tempW = weaponPort[this_player->items.weapon[REAR_WEAPON].id].opnum;
}
