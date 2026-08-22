/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Android and iOS platform glue; see mobile_platform.h.
 */
#include "mobile_platform.h"

#if defined(__ANDROID__) || defined(TARGET_IOS)

#include "SDL.h"

#include "font.h"
#include "fonthand.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "nortsong.h"
#include "sndmast.h"
#include "vga256d.h"
#include "video.h"

#ifdef __ANDROID__
#include "SDL_system.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef TARGET_IOS
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

static char user_dir[512];
static char data_dir_path[512];

const char *mobile_user_dir(void)
{
	return user_dir;
}

const char *mobile_data_dir(void)
{
	return data_dir_path;
}

#ifdef __ANDROID__

// The APK keeps assets inside the archive, where the C library cannot reach them, so
// every data file is unpacked once into the writable directory. The list is generated at
// package time because asset directory enumeration is not exposed to C.
#define ASSET_LIST  "data/filelist.txt"

// Where the unpacked copy lands. Deliberately not "data": SDL_RWFromFile resolves a
// relative path against internal storage before it falls back to the asset system, so
// unpacking to <internal>/data would make each file shadow the asset it came from and
// freeze the game on whatever the first install wrote.
#define UNPACK_SUBDIR  "gamedata"

// Unpack one asset unless an identical-length copy is already in place. Returns false
// only when the asset itself could not be read; an unwritable destination is reported
// and skipped, since a partial data set still beats refusing to start.
static bool unpack_asset(const char *name)
{
	char asset_path[512], dest_path[512];
	snprintf(asset_path, sizeof(asset_path), "data/%s", name);
	snprintf(dest_path, sizeof(dest_path), "%s/%s", data_dir_path, name);

	SDL_RWops *src = SDL_RWFromFile(asset_path, "rb");
	if (src == NULL)
		return false;

	const Sint64 asset_size = SDL_RWsize(src);

	struct stat st;
	if (asset_size >= 0 && stat(dest_path, &st) == 0 && st.st_size == asset_size)
	{
		SDL_RWclose(src);
		return true;
	}

	FILE *dest = fopen(dest_path, "wb");
	if (dest == NULL)
	{
		fprintf(stderr, "warning: cannot write %s\n", dest_path);
		SDL_RWclose(src);
		return true;
	}

	char buffer[64 * 1024];
	for (size_t got; (got = SDL_RWread(src, buffer, 1, sizeof(buffer))) > 0; )
		fwrite(buffer, 1, got, dest);

	fclose(dest);
	SDL_RWclose(src);
	return true;
}

static void unpack_assets(void)
{
	SDL_RWops *list = SDL_RWFromFile(ASSET_LIST, "rb");
	if (list == NULL)
	{
		fprintf(stderr, "error: %s is missing from the APK\n", ASSET_LIST);
		return;
	}

	const Sint64 size = SDL_RWsize(list);
	char *text = size > 0 ? SDL_malloc((size_t)size + 1) : NULL;
	if (text == NULL)
	{
		SDL_RWclose(list);
		return;
	}

	const size_t got = SDL_RWread(list, text, 1, (size_t)size);
	text[got] = '\0';
	SDL_RWclose(list);

	// One name per line; CRLF survives a Windows-side edit of the list.
	for (char *line = text, *next; line != NULL && *line != '\0'; line = next)
	{
		next = strchr(line, '\n');
		if (next != NULL)
			*next++ = '\0';

		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' '))
			line[--len] = '\0';

		if (len > 0 && !unpack_asset(line))
			fprintf(stderr, "warning: asset data/%s is missing\n", line);
	}

	SDL_free(text);
}

#endif // __ANDROID__

void mobile_platform_init(void)
{
#ifdef __ANDROID__
	// Private app storage: no permission needed, and the system clears it on uninstall.
	const char *internal = SDL_AndroidGetInternalStoragePath();
	SDL_strlcpy(user_dir, internal != NULL ? internal : ".", sizeof(user_dir));
	snprintf(data_dir_path, sizeof(data_dir_path), "%s/%s", user_dir, UNPACK_SUBDIR);
	mkdir(user_dir, 0700);
	mkdir(data_dir_path, 0700);
	unpack_assets();
#else
	// iOS: the bundle is an ordinary read-only directory, so the data files are opened
	// where they were installed. SDL owns both strings it returns here.
	char *pref = SDL_GetPrefPath("OpenTyrian", "OpenTyrian2000");
	SDL_strlcpy(user_dir, pref != NULL ? pref : ".", sizeof(user_dir));
	SDL_free(pref);

	// SDL_GetPrefPath ends in a separator; dir_fopen() adds its own.
	const size_t len = strlen(user_dir);
	if (len > 1 && user_dir[len - 1] == '/')
		user_dir[len - 1] = '\0';

	char *base = SDL_GetBasePath();
	snprintf(data_dir_path, sizeof(data_dir_path), "%sdata", base != NULL ? base : "./");
	SDL_free(base);

	mkdir(user_dir, 0700);
#endif
}

void mobile_get_output_size(int *w, int *h)
{
	SDL_Rect bounds = { 0, 0, 640, 360 };
	SDL_GetDisplayBounds(0, &bounds);

	if (w != NULL)
		*w = bounds.w;
	if (h != NULL)
		*h = bounds.h;
}

bool mobile_get_local_ip(uint32_t *out)
{
#ifdef TARGET_IOS
	struct ifaddrs *list = NULL;
	if (getifaddrs(&list) != 0)
		return false;

	bool found = false;
	for (const struct ifaddrs *ifa = list; ifa != NULL && !found; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if ((ifa->ifa_flags & IFF_LOOPBACK) != 0 || (ifa->ifa_flags & IFF_UP) == 0)
			continue;

		*out = ((const struct sockaddr_in *)(const void *)ifa->ifa_addr)->sin_addr.s_addr;
		found = true;
	}

	freeifaddrs(list);
	return found;
#else
	(void)out;
	return false;  // SDL_net enumerates interfaces successfully on Android
#endif
}

/* Software keyboard.
 *
 * Neither system offers a modal text dialog through SDL, so the game draws the prompt and
 * SDL_StartTextInput() raises the platform IME over it. Everything typed arrives as
 * ordinary SDL_TEXTINPUT, which the loop below collects. */

#define SWKBD_PANEL_W   240
#define SWKBD_PANEL_Y1  50
#define SWKBD_PANEL_Y2  150
#define SWKBD_BUTTON_W  60
#define SWKBD_BUTTON_H  16
#define SWKBD_BUTTON_Y  (SWKBD_PANEL_Y2 - 26)

// Most callers are legacy 320-wide screens, which present pillarboxed. Drawing to the same
// axis they use puts the panel over the middle of the display in either mode.
static int swkbd_center_x(void)
{
	return vga_width / 2 - video_get_menu_x_offset();
}

static void swkbd_draw_button(int x, const char *label, bool highlight)
{
	fill_rectangle_xy(VGAScreen, x, SWKBD_BUTTON_Y, x + SWKBD_BUTTON_W, SWKBD_BUTTON_Y + SWKBD_BUTTON_H,
	                  highlight ? 240 : 236);
	JE_rectangle(VGAScreen, x, SWKBD_BUTTON_Y, x + SWKBD_BUTTON_W, SWKBD_BUTTON_Y + SWKBD_BUTTON_H, 244);
	draw_font_hv_shadow(VGAScreen, x + SWKBD_BUTTON_W / 2, SWKBD_BUTTON_Y + 4, label,
	                    small_font, centered, 15, -3, false, 2);
}

static bool swkbd_button_hit(int x)
{
	return lastmouse_x >= x && lastmouse_x <= x + SWKBD_BUTTON_W &&
	       lastmouse_y >= SWKBD_BUTTON_Y && lastmouse_y <= SWKBD_BUTTON_Y + SWKBD_BUTTON_H;
}

// Row by row rather than SDL_BlitSurface, which between two 8-bit surfaces goes through
// palette matching. Pitch carries padding these screens never use, so only w bytes move.
static void swkbd_copy_screen(SDL_Surface *dst, const SDL_Surface *src)
{
	for (int y = 0; y < src->h; ++y)
	{
		SDL_memcpy((Uint8 *)dst->pixels + (size_t)y * dst->pitch,
		           (const Uint8 *)src->pixels + (size_t)y * src->pitch, (size_t)src->w);
	}
}

/* SDL_ConvertSurface cannot copy VGAScreen, and neither can SDL_DuplicateSurface, which is
 * built on it: converting to an indexed format needs a populated destination palette, and
 * this game never fills a surface palette at all -- scale_and_flip maps the indices itself.
 * Both fail with "Empty destination palette". Build the copy the way video.c builds the
 * screen and move the rows across. */
static SDL_Surface *swkbd_clone_screen(const SDL_Surface *src)
{
	SDL_Surface *copy = SDL_CreateRGBSurface(0, src->w, src->h, 8, 0, 0, 0, 0);
	if (copy != NULL)
		swkbd_copy_screen(copy, src);

	return copy;
}

bool mobile_swkbd(char *out, size_t out_size, size_t max_len,
                  const char *initial, const char *guide, bool numeric)
{
	if (out_size == 0)
		return false;
	if (max_len == 0 || max_len > out_size - 1)
		max_len = out_size - 1;

	char text[256];
	SDL_strlcpy(text, initial != NULL ? initial : "", sizeof(text));
	if (strlen(text) > max_len)
		text[max_len] = '\0';
	size_t len = strlen(text);

	// Absolute tap-to-click, so the OK and Cancel boxes can be hit even when a level is
	// running behind this prompt.
	const bool was_relative = mouseGetRelative();
	mouseSetRelative(false);

	// Drop whatever opened this prompt so it is not read as the first keystroke.
	wait_noinput(true, true, true);
	service_SDL_events(true);

	SDL_SetHint(SDL_HINT_RETURN_KEY_HIDES_IME, "1");
	SDL_StartTextInput();

	// The panel covers a live frame, so keep an untouched copy to redraw from.
	SDL_Surface *backdrop = swkbd_clone_screen(VGAScreen);

	const int center_x = swkbd_center_x();
	const int panel_x1 = center_x - SWKBD_PANEL_W / 2;
	const int panel_x2 = center_x + SWKBD_PANEL_W / 2;
	const int ok_x = center_x - SWKBD_BUTTON_W - 6;
	const int cancel_x = center_x + 6;

	bool confirmed = false, done = false;
	int flash = 0;

	while (!done)
	{
		/* Restoring first is what makes the panel redrawable: JE_barShade halves the shade of
		 * the pixels already there rather than filling, so it can never erase the last frame.
		 * With no copy to restore from, fill instead -- a flatter panel, but text that is
		 * deleted has to leave. */
		if (backdrop != NULL)
			swkbd_copy_screen(VGAScreen, backdrop);
		else
			fill_rectangle_xy(VGAScreen, panel_x1, SWKBD_PANEL_Y1, panel_x2, SWKBD_PANEL_Y2, 0);

		// Twice, the second pass inset, the way the in-game menu and help boxes are drawn:
		// the interior drops far enough to read against and the rim stays one step lighter.
		JE_barShade(VGAScreen, panel_x1, SWKBD_PANEL_Y1, panel_x2, SWKBD_PANEL_Y2);
		JE_barShade(VGAScreen, panel_x1 + 2, SWKBD_PANEL_Y1 + 2, panel_x2 - 2, SWKBD_PANEL_Y2 - 2);
		JE_rectangle(VGAScreen, panel_x1, SWKBD_PANEL_Y1, panel_x2, SWKBD_PANEL_Y2, 244);

		if (guide != NULL)
		{
			draw_font_hv_shadow(VGAScreen, center_x, SWKBD_PANEL_Y1 + 8, guide,
			                    normal_font, centered, 15, -3, false, 2);
		}

		// Draw the caret separately so centred text does not shift as it blinks.
		{
			const int w = JE_textWidth(text, normal_font);
			const int x = center_x - w / 2;
			draw_font_hv_shadow(VGAScreen, x, SWKBD_PANEL_Y1 + 32, text,
			                    normal_font, left_aligned, 15, -1, false, 2);
			if (flash < 15)
				fill_rectangle_xy(VGAScreen, x + w + 2, SWKBD_PANEL_Y1 + 32, x + w + 3, SWKBD_PANEL_Y1 + 42, 252);
		}

		swkbd_draw_button(ok_x, "OK", true);
		swkbd_draw_button(cancel_x, "CANCEL", false);

		mouseCursor = MOUSE_POINTER_NORMAL;
		JE_mouseStart();
		JE_showVGA();
		JE_mouseReplace();

		// Pairing service_SDL_events with limit_render_fps drops SDL_TEXTINPUT events;
		// service_wait_delay is the pump that keeps them.
		setDelay(1);
		push_joysticks_as_keyboard();
		service_wait_delay();

		flash = (flash + 1) % 30;

		if (new_text)
		{
			for (const char *c = last_text; *c != '\0' && len < max_len; ++c)
			{
				if (numeric && (*c < '0' || *c > '9'))
					continue;
				text[len++] = *c;
			}
			text[len] = '\0';
			new_text = false;
		}

		if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
				confirmed = done = true;
				break;

			case SDL_SCANCODE_ESCAPE:
			case SDL_SCANCODE_AC_BACK:
				done = true;
				break;

			case SDL_SCANCODE_BACKSPACE:
				if (len > 0)
					text[--len] = '\0';
				break;

			default:
				break;
			}
			newkey = false;
		}

		if (newmouse)
		{
			if (swkbd_button_hit(ok_x))
				confirmed = done = true;
			else if (swkbd_button_hit(cancel_x))
				done = true;
			newmouse = false;
		}
	}

	SDL_StopTextInput();

	if (backdrop != NULL)
	{
		swkbd_copy_screen(VGAScreen, backdrop);
		SDL_FreeSurface(backdrop);
	}

	wait_noinput(true, true, true);
	service_SDL_events(true);
	newkey = newmouse = false;

	mouseSetRelative(was_relative);

	JE_playSampleNum(confirmed ? S_SELECT : S_SPRING);

	if (confirmed)
		SDL_strlcpy(out, text, out_size);

	return confirmed;
}

#endif // __ANDROID__ || TARGET_IOS
