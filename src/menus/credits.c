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
#include "credits.h"

#include "config.h"
#include "episodes.h"
#include "file.h"
#include "fonthand.h"
#include "helptext.h"
#include "keyboard.h"
#include "loudness.h"
#include "mtrand.h"
#include "network.h"
#include "nortsong.h"
#include "nortvars.h"
#include "palette.h"
#include "sprite.h"
#include "tyrian2.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A credits row is a blank spacer when it's the lone "." marker (or empty); the same test the
// roll's draw loop below uses.
static bool credits_line_blank(const char *s)
{
	return s[0] == '\0' || strcmp(s, ".") == 0;
}

static SDL_Surface *credits_hi_canvas = NULL, *credits_hi_frame = NULL;

static void credits_text_hi(SDL_Surface *dst, int x_hi, int y_hi, const char *s,
                            unsigned int filter, int brightness, int scale)
{
	int x = 0, bright = 0;

	for (int i = 0; s[i] != '\0'; ++i)
	{
		const int sprite_id = font_ascii[(unsigned char)s[i]];

		if (s[i] == ' ')
		{
			x += 6;
		}
		else if (s[i] == '~')
		{
			bright = (bright == 0) ? 4 : 0;
		}
		else if (sprite_id != -1 && sprite_exists(TINY_FONT, sprite_id))
		{
			blit_sprite_table_scaled(dst, x_hi + x * scale, y_hi, SMALL_FONT_SHAPES, sprite_id,
			                         scale, BLITT_HV, (Uint8)filter, (Sint8)(brightness + bright),
			                         false);
			x += sprite(SMALL_FONT_SHAPES, sprite_id)->width + 1;
		}
	}
}

static void credits_present_hi(const char *text, size_t stride, int lines, int ticks, bool held,
                               float alpha, float ship_x0, float ship_x1, bool ship_snap,
                               unsigned int ship_sprite, JE_byte pic, JE_byte fade, int scale)
{
	SDL_Surface *const canvas = video_ensure_scaled_surface(&credits_hi_canvas, scale);
	SDL_Surface *const frame = video_ensure_scaled_surface(&credits_hi_frame, scale);
	if (canvas == NULL || frame == NULL)
	{
		JE_showVGA();
		return;
	}

	SDL_FillRect(canvas, NULL, 0);

	blit_sprite_table_scaled(canvas, (319 - sprite(EXTRA_SHAPES, pic)->width) * scale,
	                         (100 - (sprite(EXTRA_SHAPES, pic)->height / 2)) * scale,
	                         EXTRA_SHAPES, pic, scale, BLITT_HV, 0x0, (Sint8)(fade - 15), false);

	float t = held ? (float)ticks : (float)ticks - 1.0f + alpha;
	if (t < 0.0f)
		t = 0.0f;

	const float ship_x = ship_snap ? ship_x1 : ship_x0 + (ship_x1 - ship_x0) * alpha;
	const float ship_t = (ship_snap || held) ? (float)ticks : t;
	const float ship_y = (float)(vga_height - 16) - fmodf(ship_t, (float)vga_height);
	const int sx = (int)lrintf(ship_x * scale), sy = (int)lrintf(ship_y * scale);
	static const int part_dx[4] = { 0, 12, 0, 12 }, part_dy[4] = { 0, 0, 14, 14 };
	static const unsigned int part_index[4] = { 0, 1, 19, 20 };
	for (int k = 0; k < 4; ++k)
		blit_sprite2_scaled(canvas, sx + part_dx[k] * scale, sy + part_dy[k] * scale, spriteSheet9,
		                    ship_sprite + part_index[k], scale, BLIT2_COPY, 0);

	const float scroll = t / 3.0f;
	const int bottom_line = (int)floorf(scroll / 20.0f);
	float y = 20.0f - (scroll - 20.0f * (float)bottom_line);

	for (int line = bottom_line - 10; line < bottom_line; ++line)
	{
		const char *const row = text + (size_t)line * stride;
		if (line >= 0 && line < lines && strcmp(row, ".") != 0 && strlen(row))
		{
			const Uint8 color = row[0] - 65;
			const char *const s = &row[1];
			const int x = 110 - JE_textWidth(s, SMALL_FONT_SHAPES) / 2;
			const int y_whole = (int)floorf(y);

			credits_text_hi(canvas, (x + abs((y_whole / 18) % 4 - 2) - 1) * scale,
			                (int)lrintf((y - 1.0f) * scale), s, color, -8, scale);
			credits_text_hi(canvas, x * scale, (int)lrintf(y * scale), s, color, -2, scale);
		}

		y += 20.0f;
	}

	fill_rectangle_xy(canvas, 0, 0, 320 * scale - 1, 11 * scale - 1, 0);
	fill_rectangle_xy(canvas, 0, (vga_height - 10) * scale, vga_width * scale - 1,
	                  vga_height * scale - 1, 0);

	expand_frame_to_hi(video_compose_frame(), frame, scale);
	const int x_offset = video_get_menu_x_offset();
	const int copy_w = (x_offset > 0 ? LEGACY_WIDTH : vga_width) * scale;
	for (int row = 0; row < vga_height * scale; ++row)
		memcpy((Uint8 *)frame->pixels + row * frame->pitch + x_offset * scale,
		       (const Uint8 *)canvas->pixels + row * canvas->pitch, (size_t)copy_w);

	present_hi(frame);
}

void JE_playCredits(void)
{
	// Append the fork credit in memory without changing tyrian.cdt.
	enum { lines_file = 126 };
	enum { lines_extra = 3 };
	enum { lines_max = lines_file + lines_extra };
	enum { line_max_length = 65 };

	char credstr[lines_max][line_max_length + 1];

	int lines = 0;

	JE_byte currentpic = 0, fade = 0;
	JE_shortint fadechg = 1;
	JE_byte currentship = 0;
	JE_integer shipx = 0, shipxwait = 0;
	JE_shortint shipxc = 0, shipxca = 0;

	load_sprites_file(EXTRA_SHAPES, "estsc.shp");

	setDelay2(1000);

	play_song(8);

	// load credits text
	FILE *f = dir_fopen_die(data_dir(), "tyrian.cdt", "rb");
	for (lines = 0; lines < lines_file; ++lines)
	{
		read_encrypted_pascal_string(credstr[lines], sizeof(credstr[lines]), f);
	}
	fclose(f);

	// Insert the fork credit into the blank run before the final card. Finding the
	// gap from the end keeps the original fade and held frame in place.
	static const char *const credits_extra[lines_extra] = {
		"Mwlfn",
		"LOpenTyrian 2000",
		"LEngaged",
	};
	int endLine = lines_file - 1;
	while (endLine > 0 && credits_line_blank(credstr[endLine]))
		--endLine;
	int ins = endLine;
	while (ins > 0 && credits_line_blank(credstr[ins - 1]))
		--ins;
	ins += 4;
	if (ins > endLine)
		ins = endLine;  // a blank run too short to sit inside: go directly above the End card
	memmove(credstr[ins + lines_extra], credstr[ins], (lines_file - ins) * sizeof(credstr[0]));
	for (int i = 0; i < lines_extra; ++i)
		SDL_strlcpy(credstr[ins + i], credits_extra[i], sizeof(credstr[0]));
	lines = lines_max;

	memcpy(colors, palettes[6-1], sizeof(colors));
	JE_clr256(VGAScreen);
	JE_showVGA();
	fade_palette(colors, 2, 0, 255);

	// A press surviving the fades would end the roll on its first JE_anyButton.
	wait_noinput(true, true, true);
	newkey = newmouse = false;

	const int ticks_max = lines * 20 * 3;

	// Advance credits at the tick rate but present every display frame for smooth motion.
	const float cred_period = get_delay_period();
	const float cred_counter_to_ms = 1000.0f / (float)SDL_GetPerformanceFrequency();
	Uint64 cred_last = SDL_GetPerformanceCounter();
	float cred_accum = 0.0f;

	int prev_ticks = -1;
	for (int ticks = 0; ticks < ticks_max; ++ticks)
	{
		const JE_integer prev_shipx = shipx;
		const bool held = (ticks == prev_ticks);
		prev_ticks = ticks;
		setDelay(1);
		JE_clr256(VGAScreen);

		blit_sprite_hv(VGAScreenSeg, 319 - sprite(EXTRA_SHAPES, currentpic)->width, 100 - (sprite(EXTRA_SHAPES, currentpic)->height / 2), EXTRA_SHAPES, currentpic, 0x0, fade - 15);

		fade += fadechg;
		if (fade == 0 && fadechg == -1)
		{
			fadechg = 1;
			++currentpic;
			if (currentpic >= sprite_table[EXTRA_SHAPES].count)
				currentpic = 0;
		}
		if (fade == 15)
			fadechg = 0;

		if (getDelayTicks2() == 0)
		{
			fadechg = -1;
			setDelay2(900);
		}

		if (ticks % 200 == 0)
		{
			currentship = (mt_rand() % 11) + 1;
			shipxwait = (mt_rand() % 80) + 10;
			if ((mt_rand() % 2) == 1)
			{
				shipx = 1;
				shipxc = 0;
				shipxca = 1;
			}
			else
			{
				shipx = 900;
				shipxc = 0;
				shipxca = -1;
			}
		}

		shipxwait--;
		if (shipxwait == 0)
		{
			if (shipx == 1 || shipx == 900)
				shipxc = 0;
			shipxca = -shipxca;
			shipxwait = (mt_rand() % 40) + 15;
		}
		shipxc += shipxca;
		shipx += shipxc;
		if (shipx < 1)
		{
			shipx = 1;
			shipxwait = 1;
		}
		if (shipx > 900)
		{
			shipx = 900;
			shipxwait = 1;
		}
		int tmp_unknown = shipxc * shipxc;
		if (450 + tmp_unknown < 0 || 450 + tmp_unknown > 900)
		{
			if (shipxca < 0 && shipxc < 0)
				shipxwait = 1;
			if (shipxca > 0 && shipxc > 0)
				shipxwait = 1;
		}

		uint ship_sprite = ships[currentship].shipgraphic;
		if (shipxc < -10)
			ship_sprite -= (shipxc < -20) ? 4 : 2;
		else if (shipxc > 10)
			ship_sprite += (shipxc > 20) ? 4 : 2;

		blit_sprite2x2(VGAScreen, shipx / 40, (vga_height - 16) - (ticks % vga_height), spriteSheet9, ship_sprite);

		const int bottom_line = (ticks / 3) / 20;
		int y = 20 - ((ticks / 3) % 20);

		for (int line = bottom_line - 10; line < bottom_line; ++line)
		{
			if (line >= 0 && line < lines_max)
			{
				if (strcmp(&credstr[line][0], ".") != 0 && strlen(credstr[line]))
				{
					const Uint8 color = credstr[line][0] - 65;
					const char *text = &credstr[line][1];

					const int x = 110 - JE_textWidth(text, SMALL_FONT_SHAPES) / 2;

					JE_outTextAdjust(VGAScreen, x + abs((y / 18) % 4 - 2) - 1, y - 1, text, color, -8, SMALL_FONT_SHAPES, false);
					JE_outTextAdjust(VGAScreen, x,                             y,     text, color, -2, SMALL_FONT_SHAPES, false);
				}
			}

			y += 20;
		}

		fill_rectangle_xy(VGAScreen, 0,  0, 319, 10, 0);
		fill_rectangle_xy(VGAScreen, 0, vga_height - 10, vga_width - 1, vga_height - 1, 0);

		if (currentpic == sprite_table[EXTRA_SHAPES].count - 1)
			JE_outTextAdjust(VGAScreen, 5, vga_height, miscText[54], 2, -2, SMALL_FONT_SHAPES, false);  // levels-in-episode

		if (bottom_line == lines_max - 8)
			fade_song();

		if (ticks == ticks_max - 1)
		{
			--ticks;
			play_song(9);
		}

		NETWORK_KEEP_ALIVE();
		// Credits can run for minutes. Drain outpost traffic so it cannot block the rendezvous.
		while (network_shop_pump())
			;

		if (smoothMotion)
		{
			// Present the level-clear animation throughout each simulation tick.
			const int ss = effective_supersample();
			bool creditsDone = false;
			for (;;)
			{
				if (ss > 1)
				{
					float alpha = cred_accum / cred_period;
					if (alpha > 1.0f)
						alpha = 1.0f;
					credits_present_hi(&credstr[0][0], sizeof(credstr[0]), lines_max, ticks, held,
					                   alpha, prev_shipx / 40.0f, shipx / 40.0f,
					                   ticks % vga_height == 0, ship_sprite, currentpic, fade, ss);
				}
				else
				{
					JE_showVGA();
				}
				if (!output_vsync)
					limit_render_fps();
				if (JE_anyButton())
				{
					creditsDone = true;
					break;
				}

				const Uint64 now = SDL_GetPerformanceCounter();
				cred_accum += (float)(now - cred_last) * cred_counter_to_ms;
				cred_last = now;
				if (cred_accum >= cred_period)
				{
					cred_accum -= cred_period;
					if (cred_accum > cred_period)
						cred_accum = cred_period;  // never bank more than one tick of backlog
					break;
				}
			}
			if (creditsDone)
				break;
		}
		else
		{
			JE_showVGA();

			wait_delay();

			if (JE_anyButton())
				break;
		}
	}

	fade_black(10);

	free_sprites(EXTRA_SHAPES);
	SDL_FreeSurface(credits_hi_canvas);
	credits_hi_canvas = NULL;
	SDL_FreeSurface(credits_hi_frame);
	credits_hi_frame = NULL;
}
