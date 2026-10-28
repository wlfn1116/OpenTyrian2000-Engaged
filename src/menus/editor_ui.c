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
#include "editor_ui.h"

#include "audio.h"
#include "draw.h"
#include "font.h"
#include "lds_play.h"
#include "mtrand.h"
#include "musmast.h"
#include "network.h"
#include "sfx.h"
#include "timing.h"
#include "video.h"

#include <math.h>

// Jukebox-style music keeper: a track that ends or loops through fades into another random one.
static bool editorFadingSong = false;
static int editorFadeVolume;

static void editorPlayRandomSong(void)
{
	if (editorFadingSong)
	{
		editorFadingSong = false;
		set_volume(tyrMusicVolume, fxVolume);
	}

	// play_song is a no-op on the song already selected; replay it explicitly.
	const unsigned int song = mt_rand() % MUSIC_NUM;
	if (song == song_playing)
		restart_song();
	else
		play_song(song);
}

// Both editor loops run on setDelay(3) frames, so one call steps three classic ticks.
void editorServiceMusic(void)
{
	if (audio_disabled || music_disabled)
		return;

	if (songlooped && !editorFadingSong)
	{
		editorFadingSong = true;
		editorFadeVolume = tyrMusicVolume;
	}

	if (editorFadingSong)
	{
		for (int t = 0; t < 3 && editorFadingSong; ++t)
		{
			if (editorFadeVolume > 5)
				editorFadeVolume -= 2;
			else
			{
				editorFadeVolume = tyrMusicVolume;
				editorFadingSong = false;
			}
		}
		set_volume(editorFadeVolume, fxVolume);
	}

	if (!playing || (songlooped && !editorFadingSong))
		editorPlayRandomSong();
}

void editorEndMusicFade(void)
{
	if (!editorFadingSong)
		return;
	editorFadingSong = false;
	set_volume(tyrMusicVolume, fxVolume);
}

EditorMusicTick editorMusicTick(bool fromOutpost)
{
	return fromOutpost ? NULL : editorServiceMusic;
}

void editorServiceNetwork(void)
{
	NETWORK_KEEP_ALIVE();
	while (network_shop_pump())
		;
}

double editorTickMs(void)
{
	return EDITOR_TICK_DELAY * (double)get_delay_period();
}

void editorDrawHint(const char *text)
{
	draw_font_hv_shadow(VGAScreen, LEGACY_WIDTH / 2, vga_height - 12, text, small_font, centered,
	                    15, 2, false, 1);
}

void editorDrawPanel(int x0, int y0, int x1, int y1)
{
	fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, C_PANEL);
	JE_rectangle(VGAScreen, x0, y0, x1, y1, C_HI);
}

void editorDrawWell(int x0, int y0, int x1, int y1, Uint8 fill, Uint8 rim, Uint8 clamp)
{
	fill_rectangle_xy(VGAScreen, x0, y0, x1, y1, fill);
	JE_rectangle(VGAScreen, x0, y0, x1, y1, rim);
	draw_corner_clamps(VGAScreen, x0 - 1, y0 - 1, x1 + 1, y1 + 1, 3, clamp);
}

static Uint32 editorStarHash(Uint32 v)
{
	v ^= v >> 16;
	v *= 0x7feb352du;
	v ^= v >> 15;
	v *= 0x846ca68bu;
	v ^= v >> 16;
	return v;
}

void editorDrawStars(SDL_Surface *dst, int scale, int x_offset, Uint32 clock_ms, int x0, int y0,
                     int x1, int y1, unsigned int count, EditorStarCell cell)
{
	static const float speed_px_s[3] = { 5.0f, 9.0f, 14.0f };
	static const JE_byte shade[3] = { 0x56, 0x58, 0x5a };
	static const JE_byte flicker[4] = { 0, 1, 2, 1 };
	const float spanY = (float)(y1 - y0 - 1);
	const Uint32 spanX = (Uint32)(x1 - x0 - 1);

	for (unsigned int i = 0; i < count; ++i)
	{
		const Uint32 h = editorStarHash(i + 1);
		const Uint32 h2 = editorStarHash(h ^ 0x9e3779b9u);
		const unsigned int layer = ((h >> 28) % 5 == 4) ? 2 : ((h >> 27) & 1);
		const int x = x0 + 1 + (int)((i * spanX + h % spanX) / count);
		const float drift =
		    (float)(h2 % (Uint32)spanY) + (float)clock_ms * speed_px_s[layer] / 1000.0f;
		const float fy = (float)(y0 + 1) + fmodf(drift, spanY);

		JE_byte c = (JE_byte)(shade[layer] + ((h >> 8) % 7 == 0 ? 2 : 0));
		if (layer == 2)
			c += flicker[((clock_ms >> 8) + (h2 >> 4)) % 4];

		cell(dst, scale, x_offset, x, fy, c);
		if (layer == 2)
		{
			const JE_byte arm = (JE_byte)(c - 4);
			cell(dst, scale, x_offset, x - 1, fy, arm);
			cell(dst, scale, x_offset, x + 1, fy, arm);
			cell(dst, scale, x_offset, x, fy - 1.0f, arm);
			cell(dst, scale, x_offset, x, fy + 1.0f, arm);
		}
	}
}

enum { EDITOR_FLASH_MS = 200 };

void editorFlashStart(EditorFlash *flash, int x0, int y0, int x1, int y1)
{
	flash->x0 = x0;
	flash->y0 = y0;
	flash->x1 = x1;
	flash->y1 = y1;
	flash->start_ms = SDL_GetTicks();
}

void editorDrawFlash(EditorFlash *flash, SDL_Surface *dst, int scale, int x_offset)
{
	if (flash->start_ms == 0)
		return;
	const Uint32 age = SDL_GetTicks() - flash->start_ms;
	if (age >= EDITOR_FLASH_MS)
	{
		flash->start_ms = 0;
		return;
	}

	static const Uint8 dither[2][2] = { { 0, 2 }, { 3, 1 } };
	const int glow4 = (int)((EDITOR_FLASH_MS - age) * 10 / EDITOR_FLASH_MS);
	const int hy1 = (flash->y1 + 1) * scale - 1;
	const int hx0 = (flash->x0 + x_offset) * scale;
	const int hx1 = (flash->x1 + 1 + x_offset) * scale - 1;
	for (int y = flash->y0 * scale; y <= hy1; ++y)
	{
		Uint8 *row = (Uint8 *)dst->pixels + y * dst->pitch;
		for (int x = hx0; x <= hx1; ++x)
		{
			const int lift = (glow4 + dither[y & 1][x & 1]) >> 2;
			if (lift == 0)
				continue;
			const int v = (row[x] & 0x0f) + lift;
			row[x] = (Uint8)((row[x] & 0xf0) | (v > 15 ? 15 : v));
		}
	}
}
