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
#include "endless.h"
#include "console_platform.h"
#include "episodes.h"
#include "file.h"
#include "font.h"
#include "fonthand.h"
#include "helptext.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "menus.h"
#include "net_rollback.h"
#include "network.h"
#include "nortsong.h"
#include "opentyr.h"
#include "palette.h"
#include "picload.h"
#include "player.h"
#include "qa.h"
#include "sprite.h"
#include "vga256d.h"
#include "video.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_NETWORK

/* Menus render on a 320px virtual screen centred in the wider VGA buffer (same convention as
 * menus.c); centring on vga_width instead would drift when the menu is blitted over. */
#define LOBBY_XCENTER  (320 / 2)

// The font has no glyph for '&', '<', '>', '@', '^', '_' or '`' (they silently vanish) and
// treats '~' as a brightness toggle, so every string here avoids them; hence "and", and
// "Address" rather than an '@'-style label.

static char lobby_status[64];  // transient one-line feedback under the menu

static const int lobby_difficulties[] =
{
	DIFFICULTY_EASY,
	DIFFICULTY_NORMAL,
	DIFFICULTY_HARD,
	DIFFICULTY_IMPOSSIBLE,
	DIFFICULTY_SUICIDE,
	DIFFICULTY_LORD_OF_GAME,
};

/* Row labels, help lines and value names for the host and Endless settings pages. At file
 * scope so qa_test_net_lobby_strings below can measure each against its row budget; a row
 * that outgrows it overlaps its neighbour only on whichever machine opens that screen. */
static const char *const lobbyHostLabel[] =
{
	"Listen Port", "Game Type", "Battle Mode", "Episode", "Endless Setup", "Difficulty", "Ships",
	"Host Flies", "Credit", "Double Earnings", "Game Speed", "Netcode", "Desync Recovery",
};

static const char *const lobbyHostHelp[] =
{
	"The port other players connect to.",
	"Campaign and Endless share cash; the rest score.",
	"Which Destruct battle both players fight.",
	"Which episode the session plays.",
	"Seed, run mode, and who charts each course.",
	"Applies to both players for the whole game.",
	"Linked is the classic pair; Separate, a ship each.",
	"Which ship you take; the joiner gets the other.",
	"Shared pays a kill or pickup to both players.",
	"Individual splits the take; combat cash pays twice.",
	"Game speed, forced on both players.",
	"Rollback hides latency; delay-based is lockstep.",
	"Repairs a desync from the host's state.",
	"Open the port and wait for a player.",
	"Return to the online multiplayer menu.",
};

static const char *const lobbyHostAction[] = { "Start Hosting", "Back" };

static const char *const lobbyTypeValue[]    = { "Arcade", "Campaign", "Endless",
                                                 "SuperTyrian", "Super Arcade", "Destruct" };
// SuperTyrian has no difficulty ladder, only the two variants the solo mode picks with Scroll Lock.
static const char *const lobbyVariantValue[] = { "Standard", "Scrollock" };
static const char lobbyVariantLabel[] = "Variant";
static const char lobbyVariantHelp[] = "Scrollock is the gentler SuperTyrian run.";
static const char *const lobbyPlayerValue[]  = { "Player 1", "Player 2", "Silver Ship", "Dragonwing" };
// Destruct wears the Host Flies row as a side pick, the way SuperTyrian rebadges Difficulty.
static const char lobbySideLabel[] = "Host Fights On";
static const char *const lobbySideValue[]    = { "Left Side", "Right Side" };
static const char lobbySideHelp[] = "Which side you man; the joiner gets the other.";
static const char *const lobbyShipsValue[]   = { "Linked", "Separate" };
static const char *const lobbyCreditValue[]  = { "Shared", "Individual" };
static const char *const lobbyOnOffValue[]   = { "On", "Off" };
static const char *const lobbyNetcodeValue[] = { "Rollback", "Delay-Based" };

static const char *const lobbyEndlessLabel[] =
	{ "Seed", "Run Mode", "Charts Course", "Combo Feed" };

static const char *const lobbyEndlessHelp[] =
{
	"A named seed repeats a run; blank rolls one.",
	"How a fatal hit and saving are handled.",
	"Who picks the next sector at the outpost.",
	"Whose drive streak a kill feeds.",
	"Return to the host settings.",
};

static const char *const lobbyEndlessRunModeHelp[] =
{
	"Relaxed: a fatal hit offers a retry.",
	"Standard: a fatal hit ends the run.",
	"Hardcore: no saving, and no second chances.",
};

static int lobbyCycleEpisode(int episode, int direction)
{
	for (int tries = 0; tries < EPISODE_MAX; ++tries)
	{
		episode += direction;
		if (episode > EPISODE_MAX)
			episode = 1;
		else if (episode < 1)
			episode = EPISODE_MAX;
		if (episodeAvail[episode - 1])
			return episode;
	}
	return 1;
}

static int lobbyCycleDifficulty(int difficulty, int direction)
{
	int index = 0;
	for (uint i = 0; i < COUNTOF(lobby_difficulties); ++i)
		if (lobby_difficulties[i] == difficulty)
			index = (int)i;
	index = (index + direction + (int)COUNTOF(lobby_difficulties)) % (int)COUNTOF(lobby_difficulties);
	return lobby_difficulties[index];
}

/* SuperTyrian reads the difficulty field as its variant, so selecting it moves that one field off
 * the ladder and onto Standard/Scrollock. Both are also ladder rungs (Lord of Game and Suicide),
 * so the two meanings are indistinguishable once swapped: these park each side while the other is
 * showing, and cycling the type through SuperTyrian and back out puts the rung it found back. */
static int lobbyLadderDifficulty = DIFFICULTY_NORMAL;
static int lobbySuperTyrianVariant = DIFFICULTY_LORD_OF_GAME;

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

		// Draw the caret separately so centred text does not shift as it blinks.
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
			// both exits have to consume the keypress themselves; otherwise the menu we
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

// A run seed is hashed, never parsed, so any printable character is fair game.
static bool filterSeed(char c)
{
	return c >= 32 && c < 127;
}

/* The Endless lobby's own settings. They live on their own page because the host menu is already
 * eight rows deep and the block has to stay inside the 200-row screen. */
static void lobbyEndlessMenu(void)
{
	enum
	{
		ITEM_SEED = 0,
		ITEM_RUNMODE,
		ITEM_CHOOSER,
		ITEM_COMBO,
		SETTING_COUNT,
		ITEM_BACK = SETTING_COUNT,
		ITEM_COUNT,
	};

	COMPILE_TIME_ASSERT(endless_lobby_labels, COUNTOF(lobbyEndlessLabel) == SETTING_COUNT);
	COMPILE_TIME_ASSERT(endless_lobby_help, COUNTOF(lobbyEndlessHelp) == ITEM_COUNT);
	COMPILE_TIME_ASSERT(endless_lobby_mode_help,
	                    COUNTOF(lobbyEndlessRunModeHelp) == ENDLESS_RUNMODE_COUNT);

	size_t selectedIndex = ITEM_SEED;
	int wBack = 0;

	const int ySettings = 60;
	const int dySettings = 14;
	const int hSetting = 12;
	const int yHelp = 120;
	const int yModeHelp = 134;
	const int yBack = 158;

	lobbyPrepareBackdrop("Endless Setup");

	for (;;)
	{
		const char *itemValue[SETTING_COUNT];
		itemValue[ITEM_SEED] = network_host_endless_seed[0] ? network_host_endless_seed : "(random)";
		itemValue[ITEM_RUNMODE] = endlessRunModeName((EndlessRunMode)network_host_endless_run_mode);
		itemValue[ITEM_CHOOSER] = endlessCourseChooserName((EndlessCourseChooser)network_host_endless_chooser);
		itemValue[ITEM_COMBO] = network_host_endless_combo_shared ? lobbyCreditValue[0] : lobbyCreditValue[1];

		int blockW = 150;
		for (int i = 0; i < SETTING_COUNT; ++i)
		{
			blockW = MAX(blockW, JE_textWidth(lobbyEndlessLabel[i], small_font) + 20
			                     + JE_textWidth(itemValue[i], small_font));
		}
		blockW = MIN(blockW, 300);

		const int xLabel = LOBBY_XCENTER - blockW / 2;
		const int xValue = xLabel + blockW;

		lobbyRestoreBackdrop();

		for (int i = 0; i < SETTING_COUNT; ++i)
		{
			const bool selected = (int)selectedIndex == i;
			const int y = ySettings + dySettings * i;
			draw_font_hv_shadow(VGAScreen, xLabel, y, lobbyEndlessLabel[i], small_font, left_aligned, 15,
			                    selected ? 6 : 2, false, 1);
			draw_font_hv_shadow(VGAScreen, xValue, y, itemValue[i], small_font, right_aligned, 15,
			                    selected ? 6 : 4, false, 1);
		}

		draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, yHelp, lobbyEndlessHelp[selectedIndex],
		                    small_font, centered, 15, 2, false, 1);
		draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, yModeHelp,
		                    lobbyEndlessRunModeHelp[network_host_endless_run_mode % ENDLESS_RUNMODE_COUNT],
		                    small_font, centered, 15, 4, false, 1);

		wBack = JE_textWidth("Back", normal_font);
		draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, yBack, "Back", normal_font, centered, 15,
		                    -4 + (selectedIndex == ITEM_BACK ? 2 : 0), false, 2);

		mouseCursor = MOUSE_POINTER_NORMAL;
		service_SDL_events(true);

		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();
		if (!output_vsync)
			limit_render_fps();

		const bool mouseMoved = lobbyWaitForInput();

		bool action = false;
		int cycleDir = 1;

		if (mouseMoved || newmouse)
		{
			for (size_t i = 0; i < ITEM_COUNT; ++i)
			{
				int x0, x1, y;
				if (i < SETTING_COUNT)
				{
					x0 = xLabel;
					x1 = xValue;
					y = ySettings + dySettings * (int)i;
				}
				else
				{
					x0 = LOBBY_XCENTER - wBack / 2;
					x1 = x0 + wBack;
					y = yBack;
				}

				const int h = i < SETTING_COUNT ? hSetting : 13;
				if (mouse_x >= x0 && mouse_x < x1 && mouse_y >= y && mouse_y < y + h)
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
				return;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex == 0) ? ITEM_COUNT - 1 : selectedIndex - 1;
				break;

			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				selectedIndex = (selectedIndex + 1) % ITEM_COUNT;
				break;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				action = true;
				break;

			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_RIGHT:
				if (selectedIndex == ITEM_RUNMODE || selectedIndex == ITEM_CHOOSER
				    || selectedIndex == ITEM_COMBO)
				{
					action = true;
					if (lastkey_scan == SDL_SCANCODE_LEFT)
						cycleDir = -1;
				}
				break;

			case SDL_SCANCODE_ESCAPE:
				JE_playSampleNum(S_SPRING);
				return;

			default:
				break;
			}
		}

		if (!action)
			continue;

		switch (selectedIndex)
		{
		case ITEM_SEED:
			JE_playSampleNum(S_SELECT);
			// A cancelled field leaves the seed alone; clearing it is done by accepting a blank.
			if (!lobbyTextEntry("Endless Setup", "Run seed:", network_host_endless_seed,
			                    sizeof(network_host_endless_seed), filterSeed, false))
			{
				network_host_endless_seed[0] = '\0';
			}
			lobbyPrepareBackdrop("Endless Setup");
			break;

		case ITEM_RUNMODE:
			JE_playSampleNum(S_CLICK);
			network_host_endless_run_mode =
				(network_host_endless_run_mode + ENDLESS_RUNMODE_COUNT + cycleDir) % ENDLESS_RUNMODE_COUNT;
			break;

		case ITEM_CHOOSER:
			JE_playSampleNum(S_CLICK);
			network_host_endless_chooser =
				(network_host_endless_chooser + ENDLESS_PICK_COUNT + cycleDir) % ENDLESS_PICK_COUNT;
			break;

		case ITEM_COMBO:
			// Individual keeps each ship's kill-fire streak its own, which is what a drive one
			// player paid for is worth; Shared has every kill feed both.
			JE_playSampleNum(S_CLICK);
			network_host_endless_combo_shared = !network_host_endless_combo_shared;
			break;

		default:
			JE_playSampleNum(S_SPRING);
			return;
		}
	}
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

		// Drop whatever opened this screen before waiting for a fresh press, like every other
		// lobby loop: the Enter or click that started the search stays latched through the
		// whole discovery window and would read here as an instant join of the top host.
		service_SDL_events(true);

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
//
// Laid out like the Endless record page: a centred block of small-font "Label ....... Value"
// rows, one line of help for whichever row the cursor is on, and the two actions under it in
// the menu font.  Spelling every setting out in full on its own normal-font row made the
// screen a wall of text; the value column carries that now.
static bool lobbyHostMenu(char *port_buf, size_t port_buf_size)
{
	enum
	{
		ITEM_PORT = 0,
		ITEM_TYPE,
		ITEM_BATTLE,
		ITEM_EPISODE,
		ITEM_ENDLESS,
		ITEM_DIFFICULTY,
		ITEM_SHIPS,
		ITEM_PLAYER,
		ITEM_CREDIT,
		ITEM_DOUBLE,
		ITEM_SPEED,
		ITEM_NETCODE,
		ITEM_RECOVERY,
		SETTING_COUNT,
		ITEM_START = SETTING_COUNT,
		ITEM_BACK,
		ITEM_COUNT,
	};

	COMPILE_TIME_ASSERT(host_lobby_labels, COUNTOF(lobbyHostLabel) == SETTING_COUNT);
	COMPILE_TIME_ASSERT(host_lobby_help, COUNTOF(lobbyHostHelp) == ITEM_COUNT);

	char status[64] = "";

	size_t selectedIndex = ITEM_START;
	int wAction[COUNTOF(lobbyHostAction)] = { 0 };

	// Eight small-font rows, the help line, and the two actions, fitted between the title
	// (large_font tops out 20px tall, so it can reach y=40) and the 200-row screen.
	// Nine rows at most (a co-op lobby paying Individual), fitted between the title and the help.
	const int ySettings = 44;
	const int dySettings = 11;
	const int hSetting = 10;
	const int yHelp = 148;
	const int yActions = 160;
	const int dyActions = 16;
	const int hAction = 13;
	const int yStatus = 191;

	lobbyPrepareBackdrop("Host Game");

	for (;;)
	{
		// Desync recovery is a rollback-only repair: the lockstep path never runs the canary
		// compare that arms it, so delay-based forces the setting off and takes the row off the
		// page entirely. It was never reachable while locked, so showing it dimmed only offered
		// a choice that was not there.  (Destruct hides the row too, below, but without clearing
		// the setting: it is a main-game preference the minigame has no business rewriting.)
		if (!net_rollback)
			net_desync_recovery = false;

		/* Both co-op types give the two slots the same kind of ship, so which one the host takes
		 * decides nothing there; that row only exists to pick the Dragonwing in Arcade, and Credit
		 * is the opposite way round. Endless always starts at episode 1 and brings its own settings
		 * page instead. */
		const bool endless = network_game_type == NETWORK_GAME_ENDLESS;
		const bool coop = endless || network_game_type == NETWORK_GAME_CAMPAIGN;
		/* SuperTyrian and Super Arcade are the one-player rulesets flown as two personal ships, so
		 * they settle Ships and Host Flies themselves; SuperTyrian replaces the difficulty ladder
		 * with its two variants, and Super Arcade picks no ship here at all (each player chooses
		 * their own on the screen that follows). */
		const bool super = network_game_type_is_super(network_game_type);
		const bool variant = network_game_type == NETWORK_GAME_SUPERTYRIAN;
		/* Destruct brings its own battle-mode row and mans a side rather than flying a ship; it
		 * has no episode, no difficulty ladder, no rollback (lockstep only, so the netcode pair
		 * goes with it), and no speed choice: its tick is the rate the two machines trade state
		 * packets at, so both sides play it at Normal. */
		const bool destruct = network_game_type == NETWORK_GAME_DESTRUCT;

		bool hidden[SETTING_COUNT];
		hidden[ITEM_PORT]       = false;
		hidden[ITEM_TYPE]       = false;
		hidden[ITEM_BATTLE]     = !destruct;
		hidden[ITEM_EPISODE]    = endless || destruct;
		hidden[ITEM_ENDLESS]    = !endless;
		hidden[ITEM_DIFFICULTY] = destruct;
		// The Ships row is Arcade's own; Host Flies only means anything for the Linked pair,
		// and doubles as Destruct's side pick.
		hidden[ITEM_SHIPS]      = coop || super || destruct;
		hidden[ITEM_PLAYER]     = destruct ? false : (coop || super || arcadeSeparateShips);
		hidden[ITEM_CREDIT]     = !coop;
		// Doubling pickups is only meaningful when the take is split in the first place.
		hidden[ITEM_DOUBLE]     = !coop || coopSharedCredit;
		// Like the netcode rows, hidden without clearing: the stored speed is the host's
		// preference for every other game type, which Destruct has no business rewriting.
		hidden[ITEM_SPEED]      = destruct;
		hidden[ITEM_NETCODE]    = destruct;
		hidden[ITEM_RECOVERY]   = !net_rollback || destruct;

		// A row the current game type hides cannot stay selected; settle on the next visible one.
		while (selectedIndex < SETTING_COUNT && hidden[selectedIndex])
			selectedIndex = (selectedIndex + 1) % ITEM_COUNT;

		/* One row wears two names: SuperTyrian's variant sits where every other type keeps its
		 * difficulty, because that is what it is on the wire (Standard is Lord of Game, Scrollock
		 * is Suicide), and PACKET_DETAILS already carries it. */
		const char *itemLabel[SETTING_COUNT];
		for (int i = 0; i < SETTING_COUNT; ++i)
			itemLabel[i] = lobbyHostLabel[i];
		if (variant)
			itemLabel[ITEM_DIFFICULTY] = lobbyVariantLabel;
		// Destruct's rebadge of Host Flies: a side is fought on, not flown.
		if (destruct)
			itemLabel[ITEM_PLAYER] = lobbySideLabel;

		const char *itemValue[SETTING_COUNT];
		itemValue[ITEM_PORT] = port_buf[0] ? port_buf : "(none)";
		itemValue[ITEM_TYPE] = lobbyTypeValue[network_game_type];
		itemValue[ITEM_BATTLE] = destructModeName[(network_host_destruct_mode >= 0
		                          && network_host_destruct_mode < DESTRUCT_MODES)
		                         ? network_host_destruct_mode : 0];
		itemValue[ITEM_EPISODE] = episode_name[network_host_episode];
		itemValue[ITEM_ENDLESS] = endlessRunModeName((EndlessRunMode)network_host_endless_run_mode);
		itemValue[ITEM_DIFFICULTY] = variant
		                           ? lobbyVariantValue[network_host_difficulty == DIFFICULTY_SUICIDE ? 1 : 0]
		                           : difficultyNameB[network_host_difficulty];
		itemValue[ITEM_SHIPS] = arcadeSeparateShips ? lobbyShipsValue[1] : lobbyShipsValue[0];
		itemValue[ITEM_PLAYER] = destruct
		                       ? lobbySideValue[network_host_player == 2 ? 1 : 0]
		                       : network_host_player == 2
		                       ? (coop ? lobbyPlayerValue[1] : lobbyPlayerValue[3])
		                       : (coop ? lobbyPlayerValue[0] : lobbyPlayerValue[2]);
		itemValue[ITEM_CREDIT] = coopSharedCredit ? lobbyCreditValue[0] : lobbyCreditValue[1];
		itemValue[ITEM_DOUBLE] = coopDoubleEarnings ? lobbyOnOffValue[0] : lobbyOnOffValue[1];
		itemValue[ITEM_SPEED] = gameSpeedText[network_host_game_speed - 1];
		itemValue[ITEM_NETCODE] = net_rollback ? lobbyNetcodeValue[0] : lobbyNetcodeValue[1];
		itemValue[ITEM_RECOVERY] = net_desync_recovery ? lobbyOnOffValue[0] : lobbyOnOffValue[1];

		// A hidden row leaves no gap: the ones under it move up, so `rowY` is the single place
		// the draw and the hit test agree on where a row ended up (-1 = not on screen).
		int rowY[SETTING_COUNT];
		int shown = 0;
		for (int i = 0; i < SETTING_COUNT; ++i)
			rowY[i] = hidden[i] ? -1 : ySettings + dySettings * shown++;

		// Size the block to its widest visible row and hang the columns off its edges, so no
		// value shifts the labels as it changes.  The floor keeps a screenful of short values
		// from looking cramped; the ceiling keeps a long data-file name inside the 320px field.
		int blockW = 150;
		for (int i = 0; i < SETTING_COUNT; ++i)
		{
			if (rowY[i] < 0)
				continue;
			blockW = MAX(blockW, JE_textWidth(itemLabel[i], small_font) + 20
			                     + JE_textWidth(itemValue[i], small_font));
		}
		blockW = MIN(blockW, 300);

		const int xLabel = LOBBY_XCENTER - blockW / 2;
		const int xValue = xLabel + blockW;

		lobbyRestoreBackdrop();

		for (int i = 0; i < SETTING_COUNT; ++i)
		{
			if (rowY[i] < 0)
				continue;

			const bool selected = (int)selectedIndex == i;

			// small_font sits low in bank 15, so its offsets run positive.
			const int labelValue = selected ? 6 : 2;
			const int valueValue = selected ? 6 : 4;

			draw_font_hv_shadow(VGAScreen, xLabel, rowY[i], itemLabel[i], small_font, left_aligned, 15,
			                    labelValue, false, 1);
			draw_font_hv_shadow(VGAScreen, xValue, rowY[i], itemValue[i], small_font, right_aligned, 15,
			                    valueValue, false, 1);
		}

		const char *helpLine = lobbyHostHelp[selectedIndex];
		if (selectedIndex == ITEM_DIFFICULTY && variant)
			helpLine = lobbyVariantHelp;
		else if (selectedIndex == ITEM_PLAYER && destruct)
			helpLine = lobbySideHelp;
		draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, yHelp, helpLine,
		                    small_font, centered, 15, 2, false, 1);

		for (uint i = 0; i < COUNTOF(lobbyHostAction); ++i)
		{
			wAction[i] = JE_textWidth(lobbyHostAction[i], normal_font);
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, yActions + dyActions * (int)i, lobbyHostAction[i],
			                    normal_font, centered, 15,
			                    -4 + (selectedIndex == ITEM_START + i ? 2 : 0), false, 2);
		}

		if (status[0])
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, yStatus, status, small_font, centered, 15, 6, false, 1);

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
				if (i < SETTING_COUNT && rowY[i] < 0)
					continue;

				int x0, x1, y;
				if (i < SETTING_COUNT)
				{
					// The whole label-to-value span answers, gap included.
					x0 = xLabel;
					x1 = xValue;
					y = rowY[i];
				}
				else
				{
					x0 = LOBBY_XCENTER - wAction[i - ITEM_START] / 2;
					x1 = x0 + wAction[i - ITEM_START];
					y = yActions + dyActions * (int)(i - ITEM_START);
				}

				const int h = i < SETTING_COUNT ? hSetting : hAction;

				if (mouse_x >= x0 && mouse_x < x1 && mouse_y >= y && mouse_y < y + h)
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
			// Both walks step over anything the frame above left off the screen: the recovery
			// row a delay-based session has no use for, and the slot row a Campaign one hasn't.
			case SDL_SCANCODE_UP:
				JE_playSampleNum(S_CURSOR);
				do
					selectedIndex = (selectedIndex == 0) ? ITEM_COUNT - 1 : selectedIndex - 1;
				while (selectedIndex < SETTING_COUNT && hidden[selectedIndex]);
				break;

			case SDL_SCANCODE_DOWN:
				JE_playSampleNum(S_CURSOR);
				do
					selectedIndex = (selectedIndex + 1) % ITEM_COUNT;
				while (selectedIndex < SETTING_COUNT && hidden[selectedIndex]);
				break;

			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
			case SDL_SCANCODE_SPACE:
				action = true;
				break;

			// Every cycling row answers to left/right as well, the way every other
			// setting in the game does.  The port and the Endless page open a screen of
			// their own, so those stay Enter-only.
			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_RIGHT:
				if (selectedIndex < SETTING_COUNT && selectedIndex != ITEM_PORT
				    && selectedIndex != ITEM_ENDLESS)
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

		case ITEM_TYPE:
		{
			JE_playSampleNum(S_CLICK);
			const bool wasVariant = network_game_type == NETWORK_GAME_SUPERTYRIAN;
			network_game_type = (NetworkGameType)((network_game_type + NETWORK_GAME_TYPE_COUNT + cycleDir)
			                                      % NETWORK_GAME_TYPE_COUNT);
			// Crossing into or out of SuperTyrian changes what the difficulty row means, so park
			// the value it is leaving behind and take back the one it is returning to. Without the
			// swap a ladder rung left over would read as "Standard" while the session flew at it,
			// and cycling back out would strand the row on Lord of Game.
			if ((network_game_type == NETWORK_GAME_SUPERTYRIAN) != wasVariant)
			{
				if (wasVariant)
				{
					lobbySuperTyrianVariant = network_host_difficulty;
					network_host_difficulty = lobbyLadderDifficulty;
				}
				else
				{
					lobbyLadderDifficulty = network_host_difficulty;
					network_host_difficulty = lobbySuperTyrianVariant;
				}
			}
			break;
		}

		case ITEM_BATTLE:
			// The five data-backed battles only: Custom is built from each machine's own config
			// file, so the two players would generate different armies from the same seed.
			JE_playSampleNum(S_CLICK);
			network_host_destruct_mode =
				(network_host_destruct_mode + DESTRUCT_MODES + cycleDir) % DESTRUCT_MODES;
			break;

		case ITEM_ENDLESS:
			JE_playSampleNum(S_SELECT);
			lobbyEndlessMenu();
			lobbyPrepareBackdrop("Host Game");
			break;

		case ITEM_EPISODE:
			JE_playSampleNum(S_CLICK);
			network_host_episode = lobbyCycleEpisode(network_host_episode, cycleDir);
			break;

		case ITEM_DIFFICULTY:
			JE_playSampleNum(S_CLICK);
			// SuperTyrian's two variants ride the same field; every other type cycles the ladder.
			if (network_game_type == NETWORK_GAME_SUPERTYRIAN)
				network_host_difficulty = (network_host_difficulty == DIFFICULTY_SUICIDE)
				                        ? DIFFICULTY_LORD_OF_GAME : DIFFICULTY_SUICIDE;
			else
				network_host_difficulty = lobbyCycleDifficulty(network_host_difficulty, cycleDir);
			break;

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
			// it; one hitch instead of a divergent rest-of-level.  The host's
			// value binds the session (settings block bit 6), like every other
			// sim-affecting setting; rollback sessions only, and off the page otherwise.
			JE_playSampleNum(S_CLICK);
			net_desync_recovery = !net_desync_recovery;
			break;

		case ITEM_SHIPS:
			// Linked flies the classic Silver + Dragonwing pair; Separate gives each player
			// their own single-player-style arcade ship. Host-authoritative (settings bit 11).
			JE_playSampleNum(S_CLICK);
			arcadeSeparateShips = !arcadeSeparateShips;
			break;

		case ITEM_PLAYER:
			// Arcade's player 2 is the Dragonwing; Destruct's is the right-hand side.
			// The joiner is told which slot remains during the handshake.
			JE_playSampleNum(S_CLICK);
			network_host_player = (network_host_player == 2) ? 1 : 2;
			break;

		case ITEM_DOUBLE:
			// Compensates the split rather than beating it: a pickup pays its collector twice,
			// so a pair sharing the field ends up near what one player alone would have taken.
			JE_playSampleNum(S_CLICK);
			coopDoubleEarnings = !coopDoubleEarnings;
			break;

		case ITEM_CREDIT:
			// Shared pays a kill or a score pickup to both players in full; Individual pays
			// the shot's owner or the collector.  The host's value binds the session (settings
			// block bit 9) so both machines award the same cash.
			JE_playSampleNum(S_CLICK);
			coopSharedCredit = !coopSharedCredit;
			break;

		case ITEM_SPEED:
			// Forced on both players for the session: the host applies it at connect and
			// the joiner adopts it from the settings block (see network_connect).  Destruct
			// hides the row and pins Normal instead.
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
	// subject (see network_connect).  Campaign offers no such choice -- both slots fly the same
	// kind of ship -- so it always hosts as player 1, leaving network_host_player as the Arcade
	// preference it is remembered for.  Destruct reads the same slot as its side: 1 left, 2 right.
	const bool slotChoiceApplies = network_game_type == NETWORK_GAME_ARCADE
	                            || network_game_type == NETWORK_GAME_DESTRUCT;
	networkHostPlayerNum = (as_host && slotChoiceApplies && network_host_player == 2) ? 2 : 1;
	thisPlayerNum = as_host ? networkHostPlayerNum : 3 - networkHostPlayerNum;

	// Settle the run seeds before the connect packet goes out with them; the joiner takes both
	// from there. A blank Endless field rolls one, so "(random)" means a different run each time
	// it is hosted; the Destruct terrain seed is always rolled fresh.
	if (as_host)
	{
		network_endless_session_begin();
		network_destruct_session_begin();
	}

	if (network_init() != 0)
	{
		lobbyAbort(as_host ? "Could not open that port." : "Could not start networking.");
		return false;
	}

	// Draw the waiting screen before connecting: network_connect() blocks until the peer
	// appears, so whatever is on screen now is what the player looks at while it waits.
	lobbyPrepareBackdrop("Online Multiplayer");
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
	if (network_opponent_host != NULL && network_opponent_host[0] != '\0')
		snprintf(addr_buf, sizeof(addr_buf), "%s:%u", network_opponent_host, (unsigned)network_opponent_port);
	else
		addr_buf[0] = '\0';
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
			lobbyPrepareBackdrop("Online Multiplayer");
		lobbyRestoreBackdrop();

		if (fpsLocked)
		{
			// The lock replaces the menu wholesale: no rows at all, just the notice.
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 90, "Online Multiplayer cannot be played below",
			                    normal_font, centered, 15, -1, false, 2);
			draw_font_hv_shadow(VGAScreen, LOBBY_XCENTER, 102, "35 fps due to stability concerns.",
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
			if (lobbyTextEntry("Online Multiplayer", "Your Nickname:", name_buf, sizeof(name_buf), filterName, false))
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

/* ---- string checks ------------------------------------------------------------------- */

/* Run by qa_test_online_suite. Rows are label + 20px gap + value hung off a block clamped to
 * 300px, so a pair past that is two columns drawn into each other; help lines are centred on
 * a 320px field. Checked here because an overflow is only visible on whichever machine opens
 * the screen with that value selected. */

static bool lobbyStringDrawable(const char *s)
{
	if (s == NULL || s[0] == '\0')
		return false;
	for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; ++p)
		if (*p != ' ' && font_ascii[*p] < 0)
			return false;
	return true;
}

static void lobbyCheckRow(const char *rowLabel, const char *value)
{
	char label[160];
	snprintf(label, sizeof(label), "lobby row '%s' fits its value '%s'", rowLabel, value);
	qa_check(lobbyStringDrawable(value)
	         && JE_textWidth(rowLabel, small_font) + 20 + JE_textWidth(value, small_font) <= 300,
	         label);
}

static void lobbyCheckHelp(const char *help)
{
	char label[160];
	snprintf(label, sizeof(label), "lobby help line fits: '%s'", help);
	qa_check(lobbyStringDrawable(help) && JE_textWidth(help, small_font) <= 300, label);
}

// The widest string of len-1 characters the filter admits, by tiling its widest glyph.
static void lobbyWorstString(char *out, size_t len, bool (*filter)(char))
{
	int widest = -1;
	char pick = '0';
	for (unsigned char c = 33; c < 127; ++c)
	{
		if (!filter((char)c) || font_ascii[c] < 0)
			continue;

		const char one[2] = { (char)c, '\0' };
		const int w = JE_textWidth(one, small_font);
		if (w > widest)
		{
			widest = w;
			pick = (char)c;
		}
	}
	memset(out, pick, len - 1);
	out[len - 1] = '\0';
}

void qa_test_net_lobby_strings(void)
{
	// Host rows, in lobbyHostLabel order, against every value the row can show.
	char portWorst[6];
	lobbyWorstString(portWorst, sizeof(portWorst), filterDigits);
	lobbyCheckRow(lobbyHostLabel[0], "(none)");
	lobbyCheckRow(lobbyHostLabel[0], portWorst);
	for (uint i = 0; i < COUNTOF(lobbyTypeValue); ++i)
		lobbyCheckRow(lobbyHostLabel[1], lobbyTypeValue[i]);
	// The battle-mode names come out of the data file, so they are measured, not trusted.
	for (int m = 0; m < DESTRUCT_MODES; ++m)
		lobbyCheckRow(lobbyHostLabel[2], destructModeName[m]);
	for (int e = 1; e <= EPISODE_MAX; ++e)
		lobbyCheckRow(lobbyHostLabel[3], episode_name[e]);
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
		lobbyCheckRow(lobbyHostLabel[4], endlessRunModeName((EndlessRunMode)m));
	for (uint d = 0; d < COUNTOF(lobby_difficulties); ++d)
		lobbyCheckRow(lobbyHostLabel[5], difficultyNameB[lobby_difficulties[d]]);
	// The same row under its SuperTyrian name and values.
	for (uint i = 0; i < COUNTOF(lobbyVariantValue); ++i)
		lobbyCheckRow(lobbyVariantLabel, lobbyVariantValue[i]);
	for (uint i = 0; i < COUNTOF(lobbyShipsValue); ++i)
		lobbyCheckRow(lobbyHostLabel[6], lobbyShipsValue[i]);
	for (uint i = 0; i < COUNTOF(lobbyPlayerValue); ++i)
		lobbyCheckRow(lobbyHostLabel[7], lobbyPlayerValue[i]);
	// ...and the same row wearing Destruct's name and side values.
	for (uint i = 0; i < COUNTOF(lobbySideValue); ++i)
		lobbyCheckRow(lobbySideLabel, lobbySideValue[i]);
	for (uint i = 0; i < COUNTOF(lobbyCreditValue); ++i)
		lobbyCheckRow(lobbyHostLabel[8], lobbyCreditValue[i]);
	for (uint i = 0; i < COUNTOF(lobbyOnOffValue); ++i)
		lobbyCheckRow(lobbyHostLabel[9], lobbyOnOffValue[i]);
	for (uint s = 0; s < COUNTOF(gameSpeedText); ++s)
		lobbyCheckRow(lobbyHostLabel[10], gameSpeedText[s]);
	for (uint i = 0; i < COUNTOF(lobbyNetcodeValue); ++i)
		lobbyCheckRow(lobbyHostLabel[11], lobbyNetcodeValue[i]);
	for (uint i = 0; i < COUNTOF(lobbyOnOffValue); ++i)
		lobbyCheckRow(lobbyHostLabel[12], lobbyOnOffValue[i]);

	// Endless page rows, in lobbyEndlessLabel order. The seed is user-typed, so its worst
	// case is the widest glyph its entry filter admits, tiled to the field's limit.
	char seedWorst[NET_ENDLESS_SEED_MAX];
	lobbyWorstString(seedWorst, sizeof(seedWorst), filterSeed);
	lobbyCheckRow(lobbyEndlessLabel[0], "(random)");
	lobbyCheckRow(lobbyEndlessLabel[0], seedWorst);
	for (int m = 0; m < ENDLESS_RUNMODE_COUNT; ++m)
		lobbyCheckRow(lobbyEndlessLabel[1], endlessRunModeName((EndlessRunMode)m));
	for (int c = 0; c < ENDLESS_PICK_COUNT; ++c)
		lobbyCheckRow(lobbyEndlessLabel[2], endlessCourseChooserName((EndlessCourseChooser)c));
	for (uint i = 0; i < COUNTOF(lobbyCreditValue); ++i)
		lobbyCheckRow(lobbyEndlessLabel[3], lobbyCreditValue[i]);

	for (uint i = 0; i < COUNTOF(lobbyHostHelp); ++i)
		lobbyCheckHelp(lobbyHostHelp[i]);
	lobbyCheckHelp(lobbyVariantHelp);
	lobbyCheckHelp(lobbySideHelp);
	for (uint i = 0; i < COUNTOF(lobbyEndlessHelp); ++i)
		lobbyCheckHelp(lobbyEndlessHelp[i]);
	for (uint i = 0; i < COUNTOF(lobbyEndlessRunModeHelp); ++i)
		lobbyCheckHelp(lobbyEndlessRunModeHelp[i]);

	for (uint i = 0; i < COUNTOF(lobbyHostAction); ++i)
	{
		char label[96];
		snprintf(label, sizeof(label), "lobby action '%s' fits", lobbyHostAction[i]);
		qa_check(lobbyStringDrawable(lobbyHostAction[i])
		         && JE_textWidth(lobbyHostAction[i], normal_font) <= 300, label);
	}
}

#else  /* !WITH_NETWORK */

bool networkLobby(void)
{
	return false;
}

#endif
