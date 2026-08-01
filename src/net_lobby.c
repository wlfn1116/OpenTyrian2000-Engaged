/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "net_lobby.h"

#include "config.h"
#include "console_platform.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "net_rollback.h"
#include "network.h"
#include "nortsong.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "sprite.h"
#include "vga256d.h"
#include "video.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_NETWORK

/* Menus render on a 320px virtual screen centred in the wider VGA buffer (same convention as
 * menus.c) -- centring on vga_width instead would drift when the menu is blitted over. */
#define LOBBY_XCENTER  (320 / 2)

// The font has no glyph for '&', '<', '>', '@', '^', '_' or '`' (they silently vanish) and
// treats '~' as a brightness toggle, so every string here avoids them -- hence "and", and
// "Address" rather than an '@'-style label.

static char lobby_status[64];  // transient one-line feedback under the menu

// Render the backdrop and title into VGAScreen2 once; each frame then restores from it
// instead of re-decoding the picture (the pattern the other menus in menus.c use).
static void lobbyPrepareBackdrop(const char *title)
{
	JE_loadPic(VGAScreen2, 2, false);
	draw_font_hv_shadow(VGAScreen2, LOBBY_XCENTER, 20, title, large_font, centered, 15, -3, false, 2);
}

static void lobbyRestoreBackdrop(void)
{
	memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);
}

// Wait for a key, a button, or mouse MOTION; returns true if the mouse moved.  Motion has to
// wake the caller: the cursor is composited into the frame by JE_mouseStart/JE_mouseReplace,
// so a loop that only wakes on clicks never redraws and the pointer appears to be missing.
// Same contract as menus.c's menuWaitForInput, which every other menu in the game uses.
static bool lobbyWaitForInput(void)
{
	const Uint16 startMouseX = mouse_x;
	const Uint16 startMouseY = mouse_y;

	for (;;)
	{
		push_joysticks_as_keyboard();
		service_SDL_events(false);

		NETWORK_KEEP_ALIVE();

		const bool mouseMoved = mouse_x != startMouseX || mouse_y != startMouseY;
		if (newkey || newmouse || mouseMoved)
			return mouseMoved;

		SDL_Delay(1);  // brief idle poll; a still cursor doesn't need redrawing
	}
}

// Blocking single-line prompt.  `filter` returns true for characters the field accepts, and
// `numeric` asks for a keypad rather than a full keyboard where the platform has one.
// Returns false if the player cancelled.
static bool lobbyTextEntry(const char *title, const char *prompt, char *buf, size_t buf_size,
                           bool (*filter)(char), bool numeric)
{
#if defined(__SWITCH__) || defined(__vita__)
	// No physical keyboard on the consoles, and nothing there produces SDL_TEXTINPUT, so the
	// field below would never see a character.  The system keyboard replaces it wholesale.
	(void)title;

	char kb[64];
	SDL_strlcpy(kb, buf, sizeof(kb));

	const bool confirmed = console_swkbd(kb, sizeof(kb), buf_size - 1, kb, prompt, numeric);

	// Drop the button that opened this field along with anything the keyboard left behind,
	// so the menu we return to does not act on it a second time.  Same wind-down the desktop
	// field does on its way in.
	wait_noinput(true, true, true);
	service_SDL_events(true);
	newkey = newmouse = false;

	if (!confirmed)
	{
		JE_playSampleNum(S_SPRING);
		return false;
	}

	// Still filter: the Vita's IME has no numeric mode, and neither keyboard restricts the
	// character set, so a port field can come back with letters in it.
	size_t out = 0;
	for (const char *c = kb; *c != '\0' && out + 1 < buf_size; ++c)
	{
		if (filter(*c))
			buf[out++] = *c;
	}
	buf[out] = '\0';

	if (out == 0)
		return false;  // an empty address or port is never useful

	JE_playSampleNum(S_SELECT);
	return true;
#else
	(void)numeric;  // no separate keypad to ask for; the field just filters what is typed

	size_t len = strlen(buf);
	int flash = 0;

	// Wait for the key that opened this field to be released, then drop it, so it is not read
	// again as the field's first keystroke.
	wait_noinput(true, true, true);
	service_SDL_events(true);

	lobbyPrepareBackdrop(title);

	for (;;)
	{
		lobbyRestoreBackdrop();

		draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 70, prompt, normal_font, centered, 15, -3, false, 2);

		// Field: the typed text, centred on its own width only -- appending a
		// caret CHARACTER changed the width and made the whole line shuffle
		// left and right as it blinked (and '_' has no glyph in this font
		// anyway).  The caret is a drawn bar just past the text instead, so
		// the text never moves and the caret is actually visible.
		{
			const int w = JE_textWidth(buf, normal_font);
			const int x = LOBBY_XCENTER - w / 2;
			draw_font_hv_shadow(VGAScreen, x, 100, buf, normal_font, left_aligned, 15, -1, false, 2);
			if (flash < 15)
				fill_rectangle_xy(VGAScreen, x + w + 2, 100, x + w + 3, 110, 252);
		}

		draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 150, "Enter to accept, Esc to cancel",
		                    normal_font, centered, 15, -5, false, 2);

		// Composite the mouse cursor like every other lobby screen; without
		// this the pointer simply disappears while a field is open.
		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		// The text field must be pumped with service_wait_delay: pairing service_SDL_events
		// with limit_render_fps drops SDL_TEXTINPUT events and swallows keystrokes.
		setDelay(1);
		push_joysticks_as_keyboard();
		service_wait_delay();

		flash = (flash + 1) % 30;

		if (new_text)
		{
			for (const char *c = last_text; *c != '\0'; ++c)
			{
				const char ch = *c;
				if (len + 1 < buf_size && filter(ch))
					buf[len++] = ch;
			}
			buf[len] = '\0';
			new_text = false;
		}

		if (newkey)
		{
			switch (lastkey_scan)
			{
			// service_wait_delay() pumps events without clearing the "new input" flags, so
			// both exits have to consume the keypress themselves -- otherwise the menu we
			// return to sees it still pending and acts on it a second time.
			case SDL_SCANCODE_ESCAPE:
				JE_playSampleNum(S_SPRING);
				newkey = newmouse = false;
				return false;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
				if (len == 0)
					break;  // an empty address or port is never useful; keep waiting
				JE_playSampleNum(S_SELECT);
				newkey = newmouse = false;
				return true;

			case SDL_SCANCODE_BACKSPACE:
				if (len > 0)
					buf[--len] = '\0';
				break;

			// Paste replaces the field rather than appending: it is pre-filled with the
			// last-used value, and a copied address wants to take its place whole.
			case SDL_SCANCODE_V:
				if ((lastkey_mod & (KMOD_CTRL | KMOD_GUI)) != 0)
				{
					char *clip = SDL_GetClipboardText();
					if (clip != NULL)
					{
						char pasted[64];
						size_t out = 0;
						for (const char *c = clip; *c != '\0' && out + 1 < sizeof(pasted) && out + 1 < buf_size; ++c)
						{
							if (filter(*c))
								pasted[out++] = *c;
						}
						pasted[out] = '\0';
						SDL_free(clip);

						if (out > 0)
						{
							memcpy(buf, pasted, out + 1);
							len = out;
						}
					}
				}
				break;

			default:
				break;
			}
			newkey = false;
		}
	}
#endif
}

static bool filterDigits(char c)
{
	return c >= '0' && c <= '9';
}

// Ports we can actually bind and dial: 49152 up is the ephemeral range the joiner's own socket
// is drawn from.  Used for the listen port and for a typed ":port" suffix alike.
static bool lobbyValidPort(const char *text, Uint16 *out)
{
	const int port = atoi(text);
	if (port <= 0 || port >= 49152)
		return false;

	*out = (Uint16)port;
	return true;
}

static bool filterAddress(char c)
{
	// IPv4, a hostname, or either with a ":port" suffix.
	return isalnum((unsigned char)c) || c == '.' || c == '-' || c == ':';
}

static bool filterName(char c)
{
	return isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '.';
}

// List the machine's own addresses so a LAN host can read one out to the other player.
// Drawn under the "waiting" message; silently skipped if none can be determined.
static void lobbyDrawLocalAddresses(int y)
{
	IPaddress addr[8];
	const int count = network_local_addresses(addr, (int)COUNTOF(addr));

	if (count <= 0)
		return;

	draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, y, "This machine:", normal_font, centered, 15, -5, false, 2);

	int shown = 0;
	for (int i = 0; i < count && shown < 3; ++i)
	{
		const Uint32 host = SDL_SwapBE32(addr[i].host);
		if (host == 0)
			continue;

		char line[48];
		snprintf(line, sizeof(line), "%u.%u.%u.%u:%u",
		         (host >> 24) & 0xff, (host >> 16) & 0xff, (host >> 8) & 0xff, host & 0xff,
		         (unsigned)network_player_port);
		draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, y + 14 + shown * 12, line,
		                    normal_font, centered, 15, -3, false, 2);
		++shown;
	}
}

#define LOBBY_MAX_FOUND 8

// Discovery-wait poll: re-present the static "searching" frame with the cursor composited at
// its live position, so the pointer keeps moving through network_discover's blocking window.
static void lobbyDiscoverPoll(void)
{
	mouseCursor = MOUSE_POINTER_NORMAL;
	JE_mouseStart();
	JE_showVGA();
	JE_mouseReplace();
	if (!output_vsync)
		limit_render_fps();
}

// Probe the LAN and let the player pick from what answered.  Returns the chosen host, or NULL
// if nothing was found or the player backed out.  The returned pointer is into `hosts`.
static const NetworkHostInfo *lobbyPickLanGame(NetworkHostInfo *hosts, int *out_count)
{
	// Draw the "searching" frame first: the probe blocks for its whole timeout,
	// re-presented with a live cursor from lobbyDiscoverPoll.
	lobbyPrepareBackdrop("Find LAN Games");
	lobbyRestoreBackdrop();
	draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 90, "Searching the local network...",
	                    normal_font, centered, 15, -2, false, 2);
	mouseCursor = MOUSE_POINTER_NORMAL;
	JE_mouseStart();
	JE_showVGA();
	JE_mouseReplace();
	fade_palette(colors, 10, 0, 255);

	const int count = network_discover(hosts, LOBBY_MAX_FOUND, 1500, lobbyDiscoverPoll);
	*out_count = count;

	if (count == 0)
		return NULL;

	size_t selectedIndex = 0;
	int wItem[LOBBY_MAX_FOUND] = { 0 };

	const int yItems = 60;
	const int dyItems = 18;
	const int hItem = 13;

	for (;;)
	{
		lobbyRestoreBackdrop();

		for (int i = 0; i < count; ++i)
		{
			char line[80];
			if (hosts[i].name[0])
				snprintf(line, sizeof(line), "%s  -  %s:%u", hosts[i].name, hosts[i].address, hosts[i].port);
			else
				snprintf(line, sizeof(line), "%s:%u", hosts[i].address, hosts[i].port);

			wItem[i] = JE_textWidth(line, normal_font);
			const int x = LOBBY_XCENTER - wItem[i] / 2;
			const int y = yItems + dyItems * i;

			draw_font_hv_shadow(VGAScreen, x, y, line, normal_font, left_aligned, 15,
			                    -4 + ((size_t)i == selectedIndex ? 2 : 0), false, 2);
		}

		draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 180, "Enter to join, Esc to go back",
		                    normal_font, centered, 15, -5, false, 2);

		mouseCursor = MOUSE_POINTER_NORMAL;

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();

		const bool mouseMoved = lobbyWaitForInput();

		bool action = false;

		if (mouseMoved || newmouse)
		{
			for (int i = 0; i < count; ++i)
			{
				const int x = LOBBY_XCENTER - wItem[i] / 2;
				const int y = yItems + dyItems * i;

				if (mouse_x >= x && mouse_x < x + wItem[i] && mouse_y >= y && mouse_y < y + hItem)
				{
					if (selectedIndex != (size_t)i)
					{
						JE_playSampleNum(S_CURSOR);
						selectedIndex = (size_t)i;
					}
					if (newmouse && lastmouse_but == SDL_BUTTON_LEFT)
						action = true;
					break;
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);
				return NULL;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex == 0) ? (size_t)count - 1 : selectedIndex - 1;
				break;

			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex + 1) % (size_t)count;
				break;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				action = true;
				break;

			case SDL_SCANCODE_ESCAPE:
				JE_playSampleNum(S_SPRING);
				return NULL;

			default:
				break;
			}
		}

		if (action)
		{
			JE_playSampleNum(S_SELECT);
			newkey = newmouse = false;
			return &hosts[selectedIndex];
		}
	}
}

// Everything that has to be settled before the machine starts listening.  Returns true to go
// ahead and host, with the port applied and the slot choice made; false to go back.
static bool lobbyHostMenu(char *port_buf, size_t port_buf_size)
{
	enum
	{
		ITEM_PORT = 0,
		ITEM_NETCODE,
		ITEM_RECOVERY,
		ITEM_PLAYER,
		ITEM_SPEED,
		ITEM_START,
		ITEM_BACK,
		ITEM_COUNT,
	};

	char status[64] = "";

	size_t selectedIndex = ITEM_START;
	int wItem[ITEM_COUNT] = { 0 };

	// Seven rows plus a gap separating the settings from Start Hosting/Back; the block
	// starts high enough that the last row stays clear of the status line at y=175.
	const int yItems = 52;
	const int dyItems = 17;
	const int hItem = 13;
	const int yGroupGap = 8;  // extra space above ITEM_START

	lobbyPrepareBackdrop("Host Game");

	for (;;)
	{
		// Desync recovery is a rollback-only repair: the lockstep path never runs the
		// canary compare that arms it, so delay-based forces the row off and dead.
		const bool recoveryLocked = !net_rollback;
		if (recoveryLocked)
			net_desync_recovery = false;

		char portItem[32];
		snprintf(portItem, sizeof(portItem), "Listen Port: %s", port_buf[0] ? port_buf : "(none)");

		char speedItem[32];
		snprintf(speedItem, sizeof(speedItem), "Game Speed: %s", gameSpeedText[network_host_game_speed - 1]);

		const char *items[ITEM_COUNT];
		items[ITEM_PORT] = portItem;
		items[ITEM_NETCODE] = net_rollback
		                    ? "Netcode: Rollback"
		                    : "Netcode: Delay-Based";
		items[ITEM_RECOVERY] = net_desync_recovery
		                     ? "Desync Recovery: On"
		                     : "Desync Recovery: Off";
		items[ITEM_PLAYER] = network_host_player == 2
		                   ? "Host Flies: Dragonwing"
		                   : "Host Flies: Silver Ship";
		items[ITEM_SPEED] = speedItem;
		items[ITEM_START] = "Start Hosting";
		items[ITEM_BACK] = "Back";

		lobbyRestoreBackdrop();

		for (size_t i = 0; i < ITEM_COUNT; ++i)
		{
			wItem[i] = JE_textWidth(items[i], normal_font);
			const int x = LOBBY_XCENTER - wItem[i] / 2;
			const int y = yItems + dyItems * (int)i + (i >= ITEM_START ? yGroupGap : 0);

			const bool disabled = i == ITEM_RECOVERY && recoveryLocked;
			draw_font_hv_shadow(VGAScreen, x, y, items[i], normal_font, left_aligned, 15,
			                    -4 + (i == selectedIndex ? 2 : 0) + (disabled ? -4 : 0), false, 2);
		}

		if (status[0])
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 175, status, normal_font, centered, 15, -3, false, 2);

		mouseCursor = MOUSE_POINTER_NORMAL;

		// Drop whatever opened this screen (or closed the port field) before waiting for a
		// fresh press; otherwise the first frame reads it again and acts on the selected row.
		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();

		const bool mouseMoved = lobbyWaitForInput();

		bool action = false;
		int cycleDir = 1;  // left arrow cycles multi-value rows backward

		if (mouseMoved || newmouse)
		{
			for (size_t i = 0; i < ITEM_COUNT; ++i)
			{
				if (i == ITEM_RECOVERY && recoveryLocked)
					continue;

				const int x = LOBBY_XCENTER - wItem[i] / 2;
				const int y = yItems + dyItems * (int)i + (i >= ITEM_START ? yGroupGap : 0);

				if (mouse_x >= x && mouse_x < x + wItem[i] && mouse_y >= y && mouse_y < y + hItem)
				{
					if (selectedIndex != i)
					{
						JE_playSampleNum(S_CURSOR);
						selectedIndex = i;
					}
					if (newmouse && lastmouse_but == SDL_BUTTON_LEFT)
						action = true;
					break;
				}
			}
		}

		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);
				return false;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				do
					selectedIndex = (selectedIndex == 0) ? ITEM_COUNT - 1 : selectedIndex - 1;
				while (selectedIndex == ITEM_RECOVERY && recoveryLocked);
				break;

			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				do
					selectedIndex = (selectedIndex + 1) % ITEM_COUNT;
				while (selectedIndex == ITEM_RECOVERY && recoveryLocked);
				break;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				action = true;
				break;

			// The value rows answer to left/right as well, the way every other
			// setting in the game does.
			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_RIGHT:
				if (selectedIndex == ITEM_PLAYER || selectedIndex == ITEM_RECOVERY ||
				    selectedIndex == ITEM_NETCODE || selectedIndex == ITEM_SPEED)
				{
					action = true;
					if (lastkey_scan == SDL_SCANCODE_LEFT)
						cycleDir = -1;
				}
				break;

			case SDL_SCANCODE_ESCAPE:
				JE_playSampleNum(S_SPRING);
				return false;

			default:
				break;
			}
		}

		if (!action)
			continue;

		status[0] = '\0';

		switch (selectedIndex)
		{
		case ITEM_PORT:
		{
			JE_playSampleNum(S_SELECT);

			const bool entered = lobbyTextEntry("Host Game", "Listen on port:", port_buf,
			                                    port_buf_size, filterDigits, true);

			// The field drew its own screen over the backdrop, whatever the answer was.
			lobbyPrepareBackdrop("Host Game");

			Uint16 port;
			if (entered && !lobbyValidPort(port_buf, &port))
				SDL_strlcpy(status, "Port must be 1 to 49151.", sizeof(status));
			break;
		}

		case ITEM_NETCODE:
			// Rollback (local input lands the same tick, the peer is predicted and
			// corrected) vs the original delay-based lockstep.  Host-authoritative:
			// the joiner adopts it from the settings block (bit 4) like every other
			// sim-binding choice.
			JE_playSampleNum(S_CLICK);
			net_rollback = !net_rollback;
			break;

		case ITEM_RECOVERY:
			// On a detected desync the host streams its state and the joiner adopts
			// it -- one hitch instead of a divergent rest-of-level.  The host's
			// value binds the session (settings block bit 6), like every other
			// sim-affecting setting; rollback sessions only.
			if (recoveryLocked)
				break;
			JE_playSampleNum(S_CLICK);
			net_desync_recovery = !net_desync_recovery;
			break;

		case ITEM_PLAYER:
			// Player 2 is the Dragonwing, so this is the row that lets a host fly it; the
			// joiner is told which slot is left during the handshake.
			JE_playSampleNum(S_CLICK);
			network_host_player = (network_host_player == 2) ? 1 : 2;
			break;

		case ITEM_SPEED:
			// Forced on both players for the session: the host applies it at connect and
			// the joiner adopts it from the settings block (see network_connect).
			JE_playSampleNum(S_CLICK);
			network_host_game_speed += cycleDir;
			if (network_host_game_speed > 5)
				network_host_game_speed = 1;
			else if (network_host_game_speed < 1)
				network_host_game_speed = 5;
			break;

		case ITEM_START:
		{
			Uint16 port;
			if (!lobbyValidPort(port_buf, &port))
			{
				SDL_strlcpy(status, "Port must be 1 to 49151.", sizeof(status));
				break;
			}

			JE_playSampleNum(S_SELECT);
			network_listen_port = port;
			network_player_port = port;
			newkey = newmouse = false;
			return true;
		}

		case ITEM_BACK:
			JE_playSampleNum(S_SPRING);
			return false;

		default:
			break;
		}
	}
}

// Tear the session down after a failed or cancelled attempt so the title screen is reachable
// again and a second try starts from a clean slate.
static void lobbyAbort(const char *status)
{
	network_shutdown();

	isNetworkGame = false;
	network_from_lobby = false;
	network_is_host = false;

	SDL_strlcpy(lobby_status, status, sizeof(lobby_status));
}

// Shared setup for both roles.  Returns true once connected.
static bool lobbyStartSession(bool as_host)
{
	isNetworkGame = true;
	network_from_lobby = true;
	network_is_host = as_host;

	// The host takes the slot it asked for; the joiner assumes it is hosted by a player 1 and
	// corrects itself from the host's connect packet, which is the first word it gets on the
	// subject (see network_connect).
	networkHostPlayerNum = (as_host && network_host_player == 2) ? 2 : 1;
	thisPlayerNum = as_host ? networkHostPlayerNum : 3 - networkHostPlayerNum;

	if (network_init() != 0)
	{
		lobbyAbort(as_host ? "Could not open that port." : "Could not start networking.");
		return false;
	}

	// Draw the waiting screen before connecting: network_connect() blocks until the peer
	// appears, so whatever is on screen now is what the player looks at while it waits.
	lobbyPrepareBackdrop("Multiplayer");
	lobbyRestoreBackdrop();
	draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 70,
	                    as_host ? "Waiting for a player to join..." : "Connecting...",
	                    normal_font, centered, 15, -2, false, 2);
	if (as_host)
		lobbyDrawLocalAddresses(100);
	draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 160, "Esc to cancel", normal_font, centered, 15, -5, false, 2);
	JE_showVGA();
	fade_palette(colors, 10, 0, 255);

	if (network_connect() != 0)
	{
		lobbyAbort(as_host ? "Cancelled." : "Could not reach that host.");
		return false;
	}

	return true;
}

bool networkLobby(void)
{
	enum
	{
		ITEM_HOST = 0,
		ITEM_JOIN,
		ITEM_FIND,
		ITEM_NAME,
		ITEM_BACK,
		ITEM_COUNT,
	};

	NetworkHostInfo found[LOBBY_MAX_FOUND];

	// Pre-filled from the config so the common case is Host/Join then Enter.
	char port_buf[8];
	char addr_buf[64];
	char name_buf[NET_NAME_MAX + 1];

	snprintf(port_buf, sizeof(port_buf), "%u", (unsigned)network_listen_port);
	SDL_strlcpy(addr_buf, network_opponent_host ? network_opponent_host : "", sizeof(addr_buf));
	SDL_strlcpy(name_buf, network_player_name, sizeof(name_buf));

	lobby_status[0] = '\0';

	size_t selectedIndex = ITEM_HOST;
	int wMenuItem[ITEM_COUNT] = { 0 };

	const int yMenuItems = 60;
	const int dyMenuItems = 24;
	const int hMenuItem = 13;
	const int yGapAfterJoin = 12;  // visually separates the connect actions from the settings below

	fade_black(10);

	// Faded in after the first frame is composed; the sub-screens fade on their own, so
	// coming back from one has to fade in again.
	bool restart = true;

	for (;;)
	{
		// A render cap below the 35Hz sim rate throttles the lockstep session to this
		// machine's render rate for BOTH players (the present sits inside the tick loop),
		// so the lobby refuses to start a netgame under one.  Uncapped (0) is fine.
		const bool fpsLocked = fps_cap > 0 && fps_cap < 35;

		char nameItem[48];
		snprintf(nameItem, sizeof(nameItem), "Your Nickname: %s", name_buf[0] ? name_buf : "(none)");

		const char *items[ITEM_COUNT];
		items[ITEM_HOST] = "Host Game";
		items[ITEM_FIND] = "Find LAN Games";
		items[ITEM_JOIN] = "Join by IP Address";
		items[ITEM_NAME] = nameItem;
		items[ITEM_BACK] = "Back";

		if (restart)
			lobbyPrepareBackdrop("Multiplayer");
		lobbyRestoreBackdrop();

		if (fpsLocked)
		{
			// The lock replaces the menu wholesale: no rows at all, just the notice.
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 90, "Multiplayer cannot be played below 35 fps",
			                    normal_font, centered, 15, -1, false, 2);
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 102, "due to stability concerns.",
			                    normal_font, centered, 15, -1, false, 2);
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 160, "Esc to go back",
			                    normal_font, centered, 15, -5, false, 2);
		}
		else
		{
			for (size_t i = 0; i < ITEM_COUNT; ++i)
			{
				wMenuItem[i] = JE_textWidth(items[i], normal_font);
				const int x = LOBBY_XCENTER - wMenuItem[i] / 2;
				const int y = yMenuItems + dyMenuItems * (int)i + (i >= ITEM_NAME ? yGapAfterJoin : 0);
				const bool selected = i == selectedIndex;

				draw_font_hv_shadow(VGAScreen, x, y, items[i], normal_font, left_aligned, 15,
				                    -4 + (selected ? 2 : 0), false, 2);
			}
		}

		if (lobby_status[0])
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 183, lobby_status, normal_font, centered, 15, -3, false, 2);

		mouseCursor = MOUSE_POINTER_NORMAL;

		// Clear any input left over from a sub-screen before waiting for a fresh one.
		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();  // pace the cursor redraw to the render-fps cap

		if (restart)
		{
			fade_palette(colors, 10, 0, 255);
			restart = false;
		}

		// Input.
		bool action = false, cancel = false;

		const bool mouseMoved = lobbyWaitForInput();

		if (!fpsLocked && (mouseMoved || newmouse))
		{
			// Hover highlights, and a click inside the hovered item activates it.
			for (size_t i = 0; i < ITEM_COUNT; ++i)
			{
				const int xItem = LOBBY_XCENTER - wMenuItem[i] / 2;
				const int yItem = yMenuItems + dyMenuItems * (int)i + (i >= ITEM_NAME ? yGapAfterJoin : 0);

				if (mouse_x >= xItem && mouse_x < xItem + wMenuItem[i] &&
				    mouse_y >= yItem && mouse_y < yItem + hMenuItem)
				{
					if (selectedIndex != i)
					{
						JE_playSampleNum(S_CURSOR);
						selectedIndex = i;
					}

					if (newmouse && lastmouse_but == SDL_BUTTON_LEFT &&
					    lastmouse_x >= xItem && lastmouse_x < xItem + wMenuItem[i] &&
					    lastmouse_y >= yItem && lastmouse_y < yItem + hMenuItem)
					{
						action = true;
					}

					break;
				}
			}
		}

		// Mouse and keyboard are mutually exclusive here, so a click can never fall through
		// into the key handler below.
		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);
				cancel = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			// The rows are hidden while the fps lock is up; only Esc (and right-click
			// above) still answers, so the arrows and Enter fall through dead.
			case SDL_SCANCODE_UP:
				if (fpsLocked)
					break;
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex == 0) ? ITEM_COUNT - 1 : selectedIndex - 1;
				break;

			case SDL_SCANCODE_DOWN:
				if (fpsLocked)
					break;
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex + 1) % ITEM_COUNT;
				break;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				if (!fpsLocked)
					action = true;
				break;

			case SDL_SCANCODE_ESCAPE:
				JE_playSampleNum(S_SPRING);
				cancel = true;
				break;

			default:
				break;
			}
		}

		if (cancel)
		{
			fade_black(15);
			return false;
		}

		if (!action)
			continue;

		lobby_status[0] = '\0';

		// Every action below opens a sub-screen that overwrites the backdrop, so the menu has
		// to rebuild it when control comes back here.
		restart = true;

		switch (selectedIndex)
		{
		case ITEM_HOST:
			JE_playSampleNum(S_SELECT);

			if (lobbyHostMenu(port_buf, sizeof(port_buf)) && lobbyStartSession(true))
				return true;
			break;

		case ITEM_FIND:
		{
			JE_playSampleNum(S_SELECT);

			int count = 0;
			const NetworkHostInfo *chosen = lobbyPickLanGame(found, &count);

			if (chosen == NULL)
			{
				if (count == 0)
					SDL_strlcpy(lobby_status, "No games found on this network.", sizeof(lobby_status));
				break;
			}

			free(network_opponent_host);
			network_opponent_host = malloc_die(strlen(chosen->address) + 1);
			strcpy(network_opponent_host, chosen->address);
			network_opponent_port = chosen->port;

			// Remember it as the typed address too, so "Join by Address" is pre-filled with
			// whatever was last actually played.
			snprintf(addr_buf, sizeof(addr_buf), "%s:%u", chosen->address, chosen->port);

			network_player_port = 0;  // any free port; see the Join case

			if (lobbyStartSession(false))
				return true;
			break;
		}

		case ITEM_JOIN:
		{
			JE_playSampleNum(S_SELECT);

			if (!lobbyTextEntry("Join by IP Address", "Host address (or address:port):", addr_buf,
			                    sizeof(addr_buf), filterAddress, false))
				break;

			// Split an optional ":port" suffix; without one, the default port is assumed.
			char host_only[64];
			SDL_strlcpy(host_only, addr_buf, sizeof(host_only));
			network_opponent_port = 1333;

			char *const colon = strrchr(host_only, ':');
			if (colon)
			{
				if (!lobbyValidPort(colon + 1, &network_opponent_port))
				{
					SDL_strlcpy(lobby_status, "Port must be 1 to 49151.", sizeof(lobby_status));
					break;
				}
				*colon = '\0';
			}

			if (host_only[0] == '\0')
			{
				SDL_strlcpy(lobby_status, "Enter a host address.", sizeof(lobby_status));
				break;
			}

			free(network_opponent_host);
			network_opponent_host = malloc_die(strlen(host_only) + 1);
			strcpy(network_opponent_host, host_only);

			// Bind our own socket to an ephemeral port: two players on one machine must not
			// both try to claim the listen port, and a joiner has no reason to need a fixed one.
			network_player_port = 0;

			if (lobbyStartSession(false))
				return true;
			break;
		}

		case ITEM_NAME:
			JE_playSampleNum(S_SELECT);
			if (lobbyTextEntry("Multiplayer", "Your name:", name_buf, sizeof(name_buf), filterName, false))
				network_set_player_name(name_buf);
			break;

		case ITEM_BACK:
			JE_playSampleNum(S_SPRING);
			fade_black(15);
			return false;

		default:
			break;
		}
	}
}

#else  /* !WITH_NETWORK */

bool networkLobby(void)
{
	return false;
}

#endif
