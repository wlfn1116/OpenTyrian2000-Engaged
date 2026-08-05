/* Endless run seeds, structural RNG, and seed selection. */

#include "endless.h"
#include "endless_internal.h"

#include "config.h"
#include "font.h"
#include "fonthand.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "mtrand.h"
#include "nortsong.h"
#include "palette.h"
#include "picload.h"
#include "player.h"
#include "sndmast.h"
#include "sprite.h"
#include "varz.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

// Structural choices use SplitMix64, isolated from gameplay's mt_rand stream.
char   endlessRunSeed[ENDLESS_SEED_MAXLEN] = "";
static Uint64 endlessSeedHash = 0;
static Uint64 endlessRngState = 0;
Uint64 endlessEliteRngState = 0;

// FNV-1a maps any seed string to 64 bits.
static Uint64 endlessHashString(const char *s)
{
	Uint64 h = 14695981039346656037ULL;
	for (; *s != '\0'; ++s)
		h = (h ^ (Uint8)*s) * 1099511628211ULL;
	return h;
}

// One SplitMix64 step, shared by the structural and elite streams.
static Uint32 endlessSplitMixNext(Uint64 *state)
{
	Uint64 z = (*state += 0x9E3779B97F4A7C15ULL);
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	z ^= z >> 31;
	return (Uint32)(z >> 32);
}

// Derive an independent, repeatable stream for a salted phase.
Uint64 endlessSplitMixSeed(Uint64 salt)
{
	Uint64 z = endlessSeedHash + (salt + 1) * 0x9E3779B97F4A7C15ULL;
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	z ^= z >> 31;
	return z;
}

// Structural random for levels, courses, perks, and shop stock.
Uint32 endlessRand(void)
{
	return endlessSplitMixNext(&endlessRngState);
}

// Restart a structural phase without depending on earlier player choices.
void endlessReseed(Uint64 salt)
{
	endlessRngState = endlessSplitMixSeed(salt);
}

// Elite tiers use a separate per-zone stream.
Uint32 endlessEliteRand(void)
{
	return endlessSplitMixNext(&endlessEliteRngState);
}

void endlessSetSeed(const char *s)
{
	SDL_strlcpy(endlessRunSeed, (s != NULL) ? s : "", sizeof(endlessRunSeed));
	endlessSeedHash = endlessHashString(endlessRunSeed);
	endlessReseed(0);
}

const char *endlessSeedString(void)
{
	return endlessRunSeed;
}

// Seed and run mode selection before the difficulty screen.
bool endlessSeedSelect(char *outSeed, size_t outN, EndlessRunMode *outMode)
{
	if (shopSpriteSheet.data == NULL)
		JE_loadCompShapes(&shopSpriteSheet, '1');

	char seed[ENDLESS_SEED_MAXLEN] = "";
	size_t len = 0;
	EndlessRunMode mode = ENDLESS_RUNMODE_STANDARD;

	enum { ROW_SEED, ROW_RANDOM, ROW_MODE, ROW_START, ROW_COUNT };
	int selected = ROW_SEED;

	const int xCenter = 320 / 2;
	const int yRecord = 42;
	const int yRows   = 82;
	const int dyRows  = 20;
	const int hRow    = 15;

	// Cache the static background in VGAScreen2.
	JE_loadPic(VGAScreen2, 2, false);
	draw_font_hv_shadow(VGAScreen2, xCenter, 20, "ENDLESS", large_font, centered, 15, -3, false, 2);
	draw_font_hv_shadow(VGAScreen2, xCenter, 54, "Type a seed for a repeatable run,",  small_font, centered, 15, 2, false, 1);
	draw_font_hv_shadow(VGAScreen2, xCenter, 64, "or leave it blank for a random one.", small_font, centered, 15, 2, false, 1);

	wait_noinput(true, true, true);

	bool first = true, done = false, commit = false;
	int prev_mx = mouse_x, prev_my = mouse_y;
	newkey = newmouse = new_text = false;
	while (!done)
	{
		memcpy(VGAScreen->pixels, VGAScreen2->pixels, (size_t)VGAScreen->pitch * VGAScreen->h);

		// The record belongs to the mode selected below, so it follows the Mode row.
		char recordLine[48];
		if (endlessBestZone[mode] > 0)
			snprintf(recordLine, sizeof(recordLine), "Furthest zone: %d%s",
			         endlessBestZone[mode], endlessRecordCustomMark(mode));
		else
			SDL_strlcpy(recordLine, "No zone record yet", sizeof(recordLine));
		draw_font_hv_shadow(VGAScreen, xCenter, yRecord, recordLine, small_font, centered, 15, 4, false, 1);

		char seedRow[48];
		if (len > 0)
			snprintf(seedRow, sizeof(seedRow), "Seed: %s_", seed);
		else
			SDL_strlcpy(seedRow, "Seed: (random)", sizeof(seedRow));
		char modeRow[32];
		snprintf(modeRow, sizeof(modeRow), "Mode: %s", endlessRunModeName(mode));
		const char *label[ROW_COUNT] = { seedRow, "Randomize", modeRow, "Start" };

		int rowW[ROW_COUNT];
		for (int i = 0; i < ROW_COUNT; ++i)
		{
			rowW[i] = JE_textWidth(label[i], normal_font);
			draw_font_hv_shadow(VGAScreen, xCenter, yRows + dyRows * i, label[i],
			                    normal_font, centered, 15, -4 + (i == selected ? 2 : 0), false, 2);
		}

		// Explain the selected mode's death and save policy.
		const char *modeHelp;
		switch (mode)
		{
		case ENDLESS_RUNMODE_HARDCORE: modeHelp = "Hardcore: no saving, and no second chances."; break;
		case ENDLESS_RUNMODE_STANDARD: modeHelp = "Standard: save anytime; a fatal hit ends the run."; break;
		default:                       modeHelp = "Relaxed: save anytime; a fatal hit offers a retry."; break;
		}
		draw_font_hv_shadow(VGAScreen, xCenter, yRows + dyRows * ROW_COUNT + 4,
		                    modeHelp, small_font, centered, 15, 2, false, 1);
		draw_font_hv_shadow(VGAScreen, xCenter, yRows + dyRows * ROW_COUNT + 18,
		                    "Up/Down Move    Enter Select    Esc Back", small_font, centered, 15, 4, false, 1);

		if (first)
		{
			fade_palette(colors, 10, 0, 255);
			first = false;
		}

		// Present until input arrives or the menu tick ends.
		mouseCursor = MOUSE_POINTER_NORMAL;
		push_joysticks_as_keyboard();
		setDelay(1);
		for (;;)
		{
			JE_mouseStart();
			JE_showVGA();
			JE_mouseReplace();
			if (newkey || newmouse || new_text || getDelayTicks() == 0)
				break;
			if (!output_vsync)
				limit_render_fps();
		}

		// Mouse movement changes selection; a resting pointer does not.
		int hover = -1;
		for (int i = 0; i < ROW_COUNT; ++i)
		{
			const int x0 = xCenter - rowW[i] / 2, x1 = xCenter + rowW[i] / 2;
			const int y0 = yRows + dyRows * i,    y1 = y0 + hRow;
			if (mouse_x >= x0 && mouse_x < x1 && mouse_y >= y0 && mouse_y < y1)
				hover = i;
		}
		const bool mouseMoved = (mouse_x != prev_mx || mouse_y != prev_my);
		prev_mx = mouse_x;
		prev_my = mouse_y;
		if (mouseMoved && hover >= 0 && hover != selected)
		{
			selected = hover;
			JE_playSampleNum(S_CURSOR);
		}

		bool activate = false;
		if (newmouse)
		{
			if (lastmouse_but == SDL_BUTTON_RIGHT)
			{
				JE_playSampleNum(S_SPRING);
				done = true;
			}
			else if (hover >= 0)
			{
				selected = hover;
				activate = true;
			}
		}
		else if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_UP:
				selected = (selected == 0) ? ROW_COUNT - 1 : selected - 1;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_DOWN:
				selected = (selected + 1) % ROW_COUNT;
				JE_playSampleNum(S_CURSOR);
				break;
			case SDL_SCANCODE_BACKSPACE:
				if (len > 0)
					seed[--len] = '\0';
				selected = ROW_SEED;
				break;
			case SDL_SCANCODE_LEFT:
				if (selected == ROW_MODE)
				{
					mode = (mode == 0) ? ENDLESS_RUNMODE_COUNT - 1 : mode - 1;
					JE_playSampleNum(S_CLICK);
				}
				break;
			case SDL_SCANCODE_RIGHT:
				if (selected == ROW_MODE)
				{
					mode = (mode + 1) % ENDLESS_RUNMODE_COUNT;
					JE_playSampleNum(S_CLICK);
				}
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_KP_ENTER:
				activate = true;
				break;
			case SDL_SCANCODE_ESCAPE:
				JE_playSampleNum(S_SPRING);
				done = true;
				break;
			default:
				break;
			}
		}

		// Printable ASCII is valid in a seed.
		if (new_text)
		{
			for (size_t ti = 0; last_text[ti] != '\0'; ++ti)
			{
				const unsigned char c = (unsigned char)last_text[ti];
				if (c >= 32 && c < 127 && len < sizeof(seed) - 1)
					seed[len++] = (char)c;
			}
			seed[len] = '\0';
			selected = ROW_SEED;
		}

		if (activate && !done)
		{
			if (selected == ROW_RANDOM)
			{
				snprintf(seed, sizeof(seed), "%lu", (unsigned long)(1u + mt_rand() % 999999999u));
				len = strlen(seed);
				JE_playSampleNum(S_SELECT);
			}
			else if (selected == ROW_MODE)
			{
				mode = (mode + 1) % ENDLESS_RUNMODE_COUNT;   // Enter cycles forward; Left/Right also step it
				JE_playSampleNum(S_CLICK);
			}
			else
			{
				if (len == 0)
					snprintf(seed, sizeof(seed), "%lu", (unsigned long)(1u + mt_rand() % 999999999u));
				SDL_strlcpy(outSeed, seed, outN);
				if (outMode)
					*outMode = mode;
				JE_playSampleNum(S_SELECT);
				commit = true;
				done = true;
			}
		}

		newkey = newmouse = new_text = false;
	}

	fade_black(commit ? 10 : 15);
	return commit;
}
