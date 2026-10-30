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
#include "jukebox.h"

#include "audio.h"
#include "crashlog.h"
#include "font.h"
#include "joystick.h"
#include "keyboard.h"
#include "lds_play.h"
#include "mtrand.h"
#include "musmast.h"
#include "palette.h"
#include "sfx.h"
#include "starlib.h"
#include "timing.h"
#include "touch_ui.h"
#include "video.h"

#include <stdio.h>

static Palette vga_palette =
{
	{  0,   0,   0}, {  0,   0, 168}, {  0, 168,   0}, {  0, 168, 168},
	{168,   0,   0}, {168,   0, 168}, {168,  84,   0}, {168, 168, 168},
	{ 84,  84,  84}, { 84,  84, 252}, { 84, 252,  84}, { 84, 252, 252},
	{252,  84,  84}, {252,  84, 252}, {252, 252,  84}, {252, 252, 252},
	{  0,   0,   0}, { 20,  20,  20}, { 32,  32,  32}, { 44,  44,  44},
	{ 56,  56,  56}, { 68,  68,  68}, { 80,  80,  80}, { 96,  96,  96},
	{112, 112, 112}, {128, 128, 128}, {144, 144, 144}, {160, 160, 160},
	{180, 180, 180}, {200, 200, 200}, {224, 224, 224}, {252, 252, 252},
	{  0,   0, 252}, { 64,   0, 252}, {124,   0, 252}, {188,   0, 252},
	{252,   0, 252}, {252,   0, 188}, {252,   0, 124}, {252,   0,  64},
	{252,   0,   0}, {252,  64,   0}, {252, 124,   0}, {252, 188,   0},
	{252, 252,   0}, {188, 252,   0}, {124, 252,   0}, { 64, 252,   0},
	{  0, 252,   0}, {  0, 252,  64}, {  0, 252, 124}, {  0, 252, 188},
	{  0, 252, 252}, {  0, 188, 252}, {  0, 124, 252}, {  0,  64, 252},
	{124, 124, 252}, {156, 124, 252}, {188, 124, 252}, {220, 124, 252},
	{252, 124, 252}, {252, 124, 220}, {252, 124, 188}, {252, 124, 156},
	{252, 124, 124}, {252, 156, 124}, {252, 188, 124}, {252, 220, 124},
	{252, 252, 124}, {220, 252, 124}, {188, 252, 124}, {156, 252, 124},
	{124, 252, 124}, {124, 252, 156}, {124, 252, 188}, {124, 252, 220},
	{124, 252, 252}, {124, 220, 252}, {124, 188, 252}, {124, 156, 252},
	{180, 180, 252}, {196, 180, 252}, {216, 180, 252}, {232, 180, 252},
	{252, 180, 252}, {252, 180, 232}, {252, 180, 216}, {252, 180, 196},
	{252, 180, 180}, {252, 196, 180}, {252, 216, 180}, {252, 232, 180},
	{252, 252, 180}, {232, 252, 180}, {216, 252, 180}, {196, 252, 180},
	{180, 252, 180}, {180, 252, 196}, {180, 252, 216}, {180, 252, 232},
	{180, 252, 252}, {180, 232, 252}, {180, 216, 252}, {180, 196, 252},
	{  0,   0, 112}, { 28,   0, 112}, { 56,   0, 112}, { 84,   0, 112},
	{112,   0, 112}, {112,   0,  84}, {112,   0,  56}, {112,   0,  28},
	{112,   0,   0}, {112,  28,   0}, {112,  56,   0}, {112,  84,   0},
	{112, 112,   0}, { 84, 112,   0}, { 56, 112,   0}, { 28, 112,   0},
	{  0, 112,   0}, {  0, 112,  28}, {  0, 112,  56}, {  0, 112,  84},
	{  0, 112, 112}, {  0,  84, 112}, {  0,  56, 112}, {  0,  28, 112},
	{ 56,  56, 112}, { 68,  56, 112}, { 84,  56, 112}, { 96,  56, 112},
	{112,  56, 112}, {112,  56,  96}, {112,  56,  84}, {112,  56,  68},
	{112,  56,  56}, {112,  68,  56}, {112,  84,  56}, {112,  96,  56},
	{112, 112,  56}, { 96, 112,  56}, { 84, 112,  56}, { 68, 112,  56},
	{ 56, 112,  56}, { 56, 112,  68}, { 56, 112,  84}, { 56, 112,  96},
	{ 56, 112, 112}, { 56,  96, 112}, { 56,  84, 112}, { 56,  68, 112},
	{ 80,  80, 112}, { 88,  80, 112}, { 96,  80, 112}, {104,  80, 112},
	{112,  80, 112}, {112,  80, 104}, {112,  80,  96}, {112,  80,  88},
	{112,  80,  80}, {112,  88,  80}, {112,  96,  80}, {112, 104,  80},
	{112, 112,  80}, {104, 112,  80}, { 96, 112,  80}, { 88, 112,  80},
	{ 80, 112,  80}, { 80, 112,  88}, { 80, 112,  96}, { 80, 112, 104},
	{ 80, 112, 112}, { 80, 104, 112}, { 80,  96, 112}, { 80,  88, 112},
	{  0,   0,  64}, { 16,   0,  64}, { 32,   0,  64}, { 48,   0,  64},
	{ 64,   0,  64}, { 64,   0,  48}, { 64,   0,  32}, { 64,   0,  16},
	{ 64,   0,   0}, { 64,  16,   0}, { 64,  32,   0}, { 64,  48,   0},
	{ 64,  64,   0}, { 48,  64,   0}, { 32,  64,   0}, { 16,  64,   0},
	{  0,  64,   0}, {  0,  64,  16}, {  0,  64,  32}, {  0,  64,  48},
	{  0,  64,  64}, {  0,  48,  64}, {  0,  32,  64}, {  0,  16,  64},
	{ 32,  32,  64}, { 40,  32,  64}, { 48,  32,  64}, { 56,  32,  64},
	{ 64,  32,  64}, { 64,  32,  56}, { 64,  32,  48}, { 64,  32,  40},
	{ 64,  32,  32}, { 64,  40,  32}, { 64,  48,  32}, { 64,  56,  32},
	{ 64,  64,  32}, { 56,  64,  32}, { 48,  64,  32}, { 40,  64,  32},
	{ 32,  64,  32}, { 32,  64,  40}, { 32,  64,  48}, { 32,  64,  56},
	{ 32,  64,  64}, { 32,  56,  64}, { 32,  48,  64}, { 32,  40,  64},
	{ 44,  44,  64}, { 48,  44,  64}, { 52,  44,  64}, { 60,  44,  64},
	{ 64,  44,  64}, { 64,  44,  60}, { 64,  44,  52}, { 64,  44,  48},
	{ 64,  44,  44}, { 64,  48,  44}, { 64,  52,  44}, { 64,  60,  44},
	{ 64,  64,  44}, { 60,  64,  44}, { 52,  64,  44}, { 48,  64,  44},
	{ 44,  64,  44}, { 44,  64,  48}, { 44,  64,  52}, { 44,  64,  60},
	{ 44,  64,  64}, { 44,  60,  64}, { 44,  52,  64}, { 44,  48,  64},
	{  0,   0,   0}, {  0,   0,   0}, {  0,   0,   0}, {  0,   0,   0},
	{  0,   0,   0}, {  0,   0,   0}, {  0,   0,   0}, {  0,   0,   0}
};

static void jukebox_music_volume(bool muted, int volume)
{
	set_volume(muted ? 0 : (Uint8)volume, fxVolume);
}

static int jukebox_pick_sound(int fx_num, int step)
{
	const int picked = (fx_num + SOUND_COUNT + step) % SOUND_COUNT;
	JE_playSampleNum((JE_byte)(picked + 1));
	return picked;
}

static const struct
{
	JoystickMenuButton button;
	SDL_Scancode key;
} jukeboxPadButtons[] =
{
	{ JOYSTICK_FACE_WEST,     SDL_SCANCODE_F },
	{ JOYSTICK_FACE_NORTH,    SDL_SCANCODE_SLASH },
	{ JOYSTICK_SHOULDER_LEFT, SDL_SCANCODE_M },
};

#if defined(__SWITCH__)
static const char *const jukeboxHelp[2][2] =
{
	{ "Press B to quit the jukebox.", "The D-pad changes the song being played." },
	{ "The D-pad picks a sound, A plays it.", "L mutes the music. B quits the jukebox." },
};
#elif defined(__vita__)
static const char *const jukeboxHelp[2][2] =
{
	{ "Press Circle to quit the jukebox.", "The D-pad changes the song being played." },
	{ "The D-pad picks a sound, Cross plays it.", "L mutes the music. Circle quits the jukebox." },
};
#else
static const char *const jukeboxHelp[2][2] =
{
	{ "Press ESC to quit the jukebox.", "Arrow keys change the song being played." },
	{ "Arrow keys pick a sound, Enter plays it.", "M mutes the music. ESC quits the jukebox." },
};
#endif

unsigned jukebox_claimed_buttons(void)
{
	unsigned mask = 0;
	for (size_t b = 0; b < COUNTOF(jukeboxPadButtons); ++b)
		mask |= 1u << jukeboxPadButtons[b].button;
	return mask;
}

// Block-expand nonzero text pixels over the supersampled star buffer.
static void juke_overlay_text(SDL_Surface *text1x, SDL_Surface *hi, int scale)
{
	for (int y = 0; y < text1x->h; ++y)
	{
		const Uint8 *sp = (const Uint8 *)text1x->pixels + y * text1x->pitch;
		Uint8 *drow = (Uint8 *)hi->pixels + (y * scale) * hi->pitch;
		for (int x = 0; x < text1x->w; ++x)
		{
			const Uint8 c = sp[x];
			if (c == 0)
				continue;
			Uint8 *d = drow + x * scale;
			for (int ky = 0; ky < scale; ++ky, d += hi->pitch)
				for (int kx = 0; kx < scale; ++kx)
					d[kx] = c;
		}
	}
}

void jukebox(void)  // FKA Setup.jukeboxGo
{
	bool trigger_quit = false,  // true when user wants to quit
	     quitting = false;

	bool hide_text = false;

	crashlog_set_phase("jukebox");

	bool fade_looped_songs = true, fading_song = false;
	bool stopped = false;

	bool fx = false;
	int fx_num = 0;
	bool music_muted = false;

	int palette_fade_steps = 15;

	int diff[256][3];
	init_step_fade_palette(diff, vga_palette, 0, 255);

	JE_starlib_init();

	int fade_volume = tyrMusicVolume;

	// Render at the display rate: star movement is scaled to fractional classic
	// ~70Hz ticks, while per-tick logic (fades) runs on accumulated whole ticks.
	const Uint64 perf_freq = SDL_GetPerformanceFrequency();
	Uint64 last_frame = SDL_GetPerformanceCounter();
	float tick_acc = 0.0f;

	// Present stars through the game's supersampling path when enabled.
	SDL_Surface *juke_hi = NULL;
	{
		const int ss = effective_supersample();
		if (ss > 1)
			juke_hi = SDL_CreateRGBSurface(0, vga_width * ss, vga_height * ss, 8, 0, 0, 0, 0);
	}
	const int scale = juke_hi != NULL ? juke_hi->w / vga_width : 1;

	bool padWas[COUNTOF(jukeboxPadButtons)];
	for (size_t b = 0; b < COUNTOF(jukeboxPadButtons); ++b)
		padWas[b] = joystick_menu_button_down(jukeboxPadButtons[b].button);
	joystick_claim_menu_buttons(jukebox_claimed_buttons());

	for (; ; )
	{
		// Give touch users dedicated previous and next track buttons.
		touch_ui_set_layout(fx ? TOUCH_LAYOUT_JUKEBOX_SOUNDS : TOUCH_LAYOUT_JUKEBOX);

		Uint64 now = SDL_GetPerformanceCounter();
		float step = (float)((double)(now - last_frame) * 1000.0 / perf_freq) / get_delay_period();
		last_frame = now;
		if (step > 4.0f)
			step = 4.0f;  // don't jump after a stall (window drag, starlib pause)

		tick_acc += step;
		int whole_ticks = (int)tick_acc;
		tick_acc -= whole_ticks;

		if (!stopped && !audio_disabled)
		{
			if (songlooped && fade_looped_songs)
				fading_song = true;

			if (fading_song)
			{
				for (int t = 0; t < whole_ticks && fading_song; ++t)
				{
					if (fade_volume > 5)
					{
						fade_volume -= 2;
					}
					else
					{
						fade_volume = tyrMusicVolume;

						fading_song = false;
					}
				}

				jukebox_music_volume(fx && music_muted, fade_volume);
			}

			if (!playing || (songlooped && fade_looped_songs && !fading_song))
				play_song(mt_rand() % MUSIC_NUM);
		}

		SDL_FillRect(VGAScreenSeg, NULL, 0);
		if (scale > 1)
			SDL_FillRect(juke_hi, NULL, 0);

		// starlib input needs to be rewritten
		JE_starlib_main(step, scale > 1 ? juke_hi : VGAScreen, scale);

		// Face buttons by position: west hides the text, north switches songs and sounds, L mutes.
		for (size_t b = 0; b < COUNTOF(jukeboxPadButtons); ++b)
		{
			const bool down = joystick_menu_button_down(jukeboxPadButtons[b].button);
			if (down && !padWas[b])
				push_key(jukeboxPadButtons[b].key);
			padWas[b] = down;
		}

		push_joysticks_as_keyboard();
		service_SDL_events(true);

		if (!hide_text)
		{
			char buffer[60];
			
			if (fx)
				snprintf(buffer, sizeof(buffer), "%d %s%s", fx_num + 1, soundTitle[fx_num],
				         music_muted ? " - music muted" : "");
			else
				snprintf(buffer, sizeof(buffer), "%d %s", song_playing + 1, musicTitle[song_playing]);
			
			const int x = VGAScreen->w / 2;

			draw_font_hv(VGAScreen, x, 170, jukeboxHelp[fx][0], small_font, centered, 1, 0);
			draw_font_hv(VGAScreen, x, 180, jukeboxHelp[fx][1], small_font, centered, 1, 0);
			draw_font_hv(VGAScreen, x, 190, buffer, small_font, centered, 1, 4);
		}

		for (int t = 0; t < whole_ticks && palette_fade_steps > 0; ++t)
			step_fade_palette(diff, palette_fade_steps--, 0, 255);

		if (scale > 1)
		{
			// Stars are already in juke_hi; lay the crisp 1x text on top, then let
			// present_hi() palette-convert and downscale the supersampled frame.
			juke_overlay_text(VGAScreen, juke_hi, scale);
			present_hi(juke_hi);
		}
		else
		{
			JE_showVGA();
		}

		if (!output_vsync)
			limit_render_fps();

		// quit on mouse click
		Uint16 x, y;
		if (JE_mousePosition(&x, &y) > 0)
			trigger_quit = true;

		if (newkey)
		{
			switch (lastkey_scan)
			{
			case SDL_SCANCODE_ESCAPE: // quit jukebox
			case SDL_SCANCODE_Q:
				trigger_quit = true;
				break;

			case SDL_SCANCODE_SPACE:
			case SDL_SCANCODE_F:
				hide_text = !hide_text;
				break;

			case SDL_SCANCODE_V: // toggle song fade (was F, which now hides the text)
				fading_song = !fading_song;
				break;
			case SDL_SCANCODE_N:
				fade_looped_songs = !fade_looped_songs;
				break;

			case SDL_SCANCODE_SLASH: // switch to sfx mode
				fx = !fx;
				jukebox_music_volume(fx && music_muted, fade_volume);
				break;
			case SDL_SCANCODE_COMMA:
			case SDL_SCANCODE_PERIOD:
				if (fx)
				{
					const int direction = lastkey_scan == SDL_SCANCODE_PERIOD ? 1 : -1;
					fx_num = jukebox_pick_sound(fx_num, direction);
				}
				break;
			case SDL_SCANCODE_SEMICOLON:
				if (fx)
					JE_playSampleNum(fx_num + 1);
				break;
			case SDL_SCANCODE_M:
				if (fx)
				{
					music_muted = !music_muted;
					jukebox_music_volume(music_muted, fade_volume);
				}
				break;

			case SDL_SCANCODE_LEFT:
			case SDL_SCANCODE_UP:
				if (fx)
				{
					fx_num = jukebox_pick_sound(fx_num, -1);
					break;
				}
				play_song((song_playing > 0 ? song_playing : MUSIC_NUM) - 1);
				stopped = false;
				break;
			case SDL_SCANCODE_RETURN:
			case SDL_SCANCODE_RIGHT:
			case SDL_SCANCODE_DOWN:
				if (fx && lastkey_scan == SDL_SCANCODE_RETURN)
				{
					JE_playSampleNum(fx_num + 1);
					break;
				}
				if (fx)
				{
					fx_num = jukebox_pick_sound(fx_num, 1);
					break;
				}
				play_song((song_playing + 1) % MUSIC_NUM);
				stopped = false;
				break;
			case SDL_SCANCODE_S: // stop song
				stop_song();
				stopped = true;
				break;
			case SDL_SCANCODE_R: // restart song
				restart_song();
				stopped = false;
				break;

			default:
				break;
			}
		}
		
		// user wants to quit, start fade-out
		if (trigger_quit && !quitting)
		{
			palette_fade_steps = 15;
			
			SDL_Color black = { 0, 0, 0 };
			init_step_fade_solid(diff, black, 0, 255);
			
			quitting = true;
		}
		
		// if fade-out finished, we can finally quit
		if (quitting && palette_fade_steps == 0)
			break;
	}

	joystick_claim_menu_buttons(0);
	set_volume(tyrMusicVolume, fxVolume);

	if (juke_hi != NULL)
		SDL_FreeSurface(juke_hi);
}
