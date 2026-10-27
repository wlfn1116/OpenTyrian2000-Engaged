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
#include "hud.h"

#include "config.h"
#include "debug_menu.h"
#include "draw.h"
#include "endless.h"
#include "episodes.h"
#include "font.h"
#include "game_state.h"
#include "health_bars.h"
#include "helptext.h"
#include "net_style.h"
#include "network.h"
#include "player.h"
#include "player_ship.h"
#include "qa.h"
#include "render_list.h"
#include "rollback.h"
#include "sprite.h"
#include "video.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

JE_word textErase;

/* Keep the last message so a visible pass can repaint anything dropped by
 * silent rollback re-simulation. See hud_message_dirty. */
static char text_window_tint[32];  // opening words, drawn in their own bank ahead of the left text
static unsigned int text_window_tint_bank;
static char text_window_left[96];
static char text_window_right[32];
static int text_window_right_x;
bool hud_message_dirty = false;

/* Always erase; silent replay may consume the countdown without drawing. */
void JE_repaintTextWindow(void)
{
	rl_note_offscreen_draw();
	blit_sprite(VGAScreenSeg, 16, vga_height - 11, OPTION_SHAPES, 36);  // in-game text area

	// The tinted opening advances x exactly as JE_outText would have, so the words that follow
	// sit where they would in one string.
	int x = 20;
	if (text_window_tint[0] != '\0')
	{
		JE_outText(VGAScreenSeg, x, vga_height - 10, text_window_tint, text_window_tint_bank, 4);
		x += JE_textWidth(text_window_tint, TINY_FONT);
	}

	JE_outText(VGAScreenSeg, x, vga_height - 10, text_window_left, 0, 4);
	if (text_window_right[0] != '\0')
		JE_outText(VGAScreenSeg, text_window_right_x - JE_textWidth(text_window_right, TINY_FONT), vga_height - 10, text_window_right, 0, 4);
}

// Draws a message at the bottom text window on the playing screen.
void JE_drawTextWindow(const char *text)
{
	SDL_strlcpy(text_window_left, text, sizeof(text_window_left));
	text_window_tint[0] = '\0';
	text_window_right[0] = '\0';
	hud_message_dirty = rollback_resim_silent;

	textErase = 100;
	JE_repaintTextWindow();
}

// Draw a split message-bar line: left text at x=20 and right text ending at right_x.
void JE_drawTextWindowSplit(const char *tint, unsigned int tint_bank, const char *left,
                            const char *right, int right_x)
{
	SDL_strlcpy(text_window_tint, (tint != NULL) ? tint : "", sizeof(text_window_tint));
	text_window_tint_bank = tint_bank;
	SDL_strlcpy(text_window_left, left, sizeof(text_window_left));
	SDL_strlcpy(text_window_right, right, sizeof(text_window_right));
	text_window_right_x = right_x;
	hud_message_dirty = rollback_resim_silent;

	textErase = 100;
	JE_repaintTextWindow();
}

void JE_drawPortConfigButtons(void) // rear weapon pattern indicator
{
	if (split_arcade_mode())
		return;
	const uint player_index = gameplay_local_player_index();

	const int x_lit = HUD_X(285);
	const int x_unlit = HUD_X(302);
	const bool was_recording = render_list_recording;
	render_list_recording = false;
	if (player[player_index].weapon_mode == 1)
	{
		blit_sprite(VGAScreenSeg, x_lit, 44, OPTION_SHAPES, 18);  // lit
		blit_sprite(VGAScreenSeg, x_unlit, 44, OPTION_SHAPES, 19);  // unlit
	}
	else // == 2
	{
		blit_sprite(VGAScreenSeg, x_lit, 44, OPTION_SHAPES, 19);  // unlit
		blit_sprite(VGAScreenSeg, x_unlit, 44, OPTION_SHAPES, 18);  // lit
	}
	render_list_recording = was_recording;
}

/* Bottom-band HUD layout; see hud.h for precedence. */

// The score row's text baseline. FULL_SHADE outlines the glyphs, so the rows actually touched
// are HUD_SCORE_Y-1 .. HUD_SCORE_Y+8.
#define HUD_SCORE_Y      (vga_height - 25)
#define HUD_ROW_H        10   // row pitch that keeps two shadowed TINY_FONT lines clear of each other
#define HUD_SUPERBOMB_Y  160  // row the superbomb icons blit at, in both bottom corners

uint hud_superbomb_count(uint p)
{
	return player_is_out(p) ? 0 : player[p].superbombs;
}

// Rightmost column player 1's superbomb row reaches (icons march right from x30).
static int hud_superbomb_p1_right(void)
{
	return 30 + 12 * (int)hud_superbomb_count(0);
}

// text row the FPS counter occupies
static int hud_fps_row(void)
{
	// Stack above player 2's score and superbombs; one-player leaves this corner free.
	if (!twoPlayerMode || (galagaMode && !coop_mode_active()))
		return HUD_SCORE_Y + 1;

	if (hud_superbomb_count(1) > 0)
		return HUD_SUPERBOMB_Y - HUD_ROW_H + 1;

	return HUD_SCORE_Y - HUD_ROW_H + 1;
}

int hud_bottom_band_top(void)
{
	int top = HUD_SCORE_Y - 1;  // both scores share this row; the shadow starts one above

	// Superbomb icons form their own row above the scores at both playfield edges, so a
	// centred full-width bar has to clear them as well.
	if (hud_superbomb_count(0) > 0 || hud_superbomb_count(1) > 0)
		top = HUD_SUPERBOMB_Y;

	if (show_fps)
	{
		const int fps_top = hud_fps_row() - 1;
		if (fps_top < top)
			top = fps_top;
	}

	return top;
}

/* Horizontal extent of the top HUD clusters. Keep in step with JE_inGameDisplays. */
static int hud_player_name_width(int index)
{
	char name[21];
	SDL_strlcpy(name, isNetworkGame ? JE_getName(index + 1)
	                                : miscText[(index == 0 ? 49 : 50) - 1], sizeof(name));
	return JE_textWidth(name, TINY_FONT);
}

static bool hud_lives_shown(void)
{
	return arcade_rules_active();
}

uint hud_lives_count(uint p)
{
	return player_is_out(p) ? 0 : *player[p].lives;
}

/* Draw one icon per remaining life, collapsing long rows to an icon and count. */
#define HUD_LIVES_ICONS_MAX 4

/* Left edge of player 2's collapsed lives count, mirrored from player 1's layout. */
static int hud_lives_count_left(const char *count)
{
	return PLAYFIELD_WIDTH + 4 - JE_textWidth(count, TINY_FONT);
}

/* Mirrored special-block geometry for the two playfield corners. */
#define HUD_SPECIAL_GAP       1   // between the icon and its ready light
#define HUD_SPECIAL_P1_X     25
#define HUD_SPECIAL_P2_RIGHT (PLAYFIELD_WIDTH + 22)   // the anchor hud_top_right_left_edge mirrors

bool hud_special_block_shown(uint p)
{
	return p < COUNTOF(player) && p == gameplay_local_player_index()
	    && player[p].items.special > 0;
}

bool hud_special_on_right(uint p)
{
	// Only two-player Arcade mirrors player two's block into the right corner.
	return p == 1 && hud_lives_shown();
}

int hud_special_icon_x(uint p)
{
	return hud_special_on_right(p) ? HUD_SPECIAL_P2_RIGHT - HUD_SPECIAL_ICON_W
	                               : HUD_SPECIAL_P1_X;
}

int hud_special_light_x(uint p)
{
	const int iconX = hud_special_icon_x(p);
	return hud_special_on_right(p) ? iconX - HUD_SPECIAL_GAP - HUD_SPECIAL_LIGHT_W
	                               : iconX + HUD_SPECIAL_ICON_W + HUD_SPECIAL_GAP;
}

int hud_lives_row_y(uint p)
{
	return hud_special_block_shown(p) ? HUD_LIVES_Y_SPECIAL : HUD_LIVES_Y;
}

/* Special-ready meter: drain while the effect burns, then refill during recharge.
 * JE_doSpecialShot publishes the clocks; this code only draws them. */
// Both frames paint the same block of the 12px sprite cell: columns 3..8 of rows 1..12.
#define HUD_LIGHT_ROW_FIRST  1
#define HUD_LIGHT_ROW_LAST  12
#define HUD_LIGHT_ROWS      (HUD_LIGHT_ROW_LAST - HUD_LIGHT_ROW_FIRST + 1)
#define HUD_LIGHT_INK_X      3
#define HUD_LIGHT_INK_W      6
#define HUD_LIGHT_FLASH     12   // ticks the ready pop lasts; also its opening brightness

static int  hud_light_charge_left = 0;  // recharge ticks still to run
static int  hud_light_burn_left = 0;    // ...and burn ticks, while a fired special is still going
static bool hud_light_armed = false;    // the special could fire this tick (sampled in specials.c)
static bool hud_light_fired = false;    // ...and one went off during it
static bool hud_light_await_pop = false;  // a fire is waiting on the special coming back
static bool hud_light_published = false;
static bool hud_light_sampled_this_tick = false;
static bool hud_light_rearm_pending = false;  // a special was equipped; its lockout starts a phase
static int  hud_light_charge_full = 0;  // what each phase started from, so both scale to themselves
static int  hud_light_burn_full = 0;
static int  hud_light_flash = 0;
static bool hud_light_was_ready = false;   // the meter at the previous live tick, and whether
static bool hud_light_meter_seen = false;  // there has been one; a reset leaves nothing to compare

// Previous and current meter state for display-rate interpolation.
static float hud_light_fill_prev = 0.0f, hud_light_fill_cur = 0.0f;
static int   hud_light_x = 0, hud_light_y = 0;
static bool  hud_light_shown = false;

static float hud_killfire_prev = 0.0f, hud_killfire_cur = 0.0f;
static int   hud_killfire_x0 = 0, hud_killfire_x1 = 0, hud_killfire_y0 = 0, hud_killfire_bank = 0;
static bool  hud_killfire_shown = false;

void hud_special_light_tick_begin(void)
{
	hud_light_sampled_this_tick = false;
	hud_light_published = false;

	// Apply equip requests on the next live tick; an active burn keeps its original scale.
	if (hud_light_rearm_pending && !rollback_resim_silent)
	{
		hud_light_rearm_pending = false;
		hud_light_charge_full = 0;
	}
}

// Equipping a special opens a meter phase. See doc/notes.md#gauges-and-effects.
void hud_special_light_rearm(uint p)
{
	if (hud_special_block_shown(p))
		hud_light_rearm_pending = true;
}

void hud_special_light_publish(int charge_ticks, int burn_ticks, bool armed, bool fired)
{
	hud_light_charge_left = charge_ticks;
	hud_light_burn_left = burn_ticks;

	/* The linked pair runs the shared special once from each player's movement pass. The second
	 * pass owns the final clocks, but it must not erase a ready or fired edge from the first. */
	if (hud_light_sampled_this_tick)
	{
		hud_light_armed |= armed;
		hud_light_fired |= fired;
	}
	else
	{
		hud_light_armed = armed;
		hud_light_fired = fired;
		hud_light_sampled_this_tick = true;
	}
	hud_light_published = true;
}

void hud_special_light_reset(void)
{
	hud_light_charge_left = 0;
	hud_light_burn_left = 0;
	hud_light_armed = false;
	hud_light_fired = false;
	// Reset ready-edge state so a new level does not pop for an already-ready special.
	hud_light_await_pop = false;
	hud_light_was_ready = false;
	hud_light_meter_seen = false;
	hud_light_published = false;
	hud_light_sampled_this_tick = false;
	hud_light_rearm_pending = false;
	hud_light_charge_full = 0;
	hud_light_burn_full = 0;
	hud_light_flash = 0;
	hud_light_fill_prev = hud_light_fill_cur = 0.0f;
	hud_light_shown = false;
	hud_killfire_shown = false;
}

/* Paint a fractional charged/spent boundary from the bottom of the meter. */
static void hud_special_light_paint(SDL_Surface *dst, int scale, int x, int y, float fill, int bright)
{
	const int rows = HUD_LIGHT_ROWS * scale;
	int lit = (int)(fill * (float)scale);  // lit sub-rows, measured up from the bar's bottom
	if (lit < 0)
		lit = 0;
	else if (lit > rows)
		lit = rows;

	const int first = HUD_LIGHT_ROW_FIRST * scale;
	const int last = (HUD_LIGHT_ROW_LAST + 1) * scale - 1;
	const int split = last - lit;  // last spent sub-row; the charged paint runs split+1 .. last

	if (split >= first)
		blit_sprite2_rows_bright_scaled(dst, x, y, spriteSheet9, 93, first, split, 0, scale);
	if (lit > 0)
		blit_sprite2_rows_bright_scaled(dst, x, y, spriteSheet9, 94, split + 1, last, bright, scale);
}

void hud_special_light_present(SDL_Surface *dst, int scale, float alpha)
{
	if (!hud_light_shown)
		return;

	// The pop decrements by exactly 1 a tick, so the previous value is always cur+1 -- the same
	// interpolation the HUD gauge and boss-bar flashes use.
	const int bright = hud_light_flash > 0 ? (int)(hud_light_flash + 1.0f - alpha + 0.5f) : 0;

	hud_special_light_paint(dst, scale, hud_light_x, hud_light_y,
	                        hud_light_fill_prev + (hud_light_fill_cur - hud_light_fill_prev) * alpha,
	                        bright);
}

void hud_killfire_bar_present(SDL_Surface *dst, int scale, float alpha)
{
	if (!hud_killfire_shown)
		return;

	const int x0 = hud_killfire_x0 * scale;
	const int x1 = (hud_killfire_x1 + 1) * scale - 1;
	const int y0 = hud_killfire_y0;
	fill_rectangle_xy(dst, x0, y0 * scale, x1, (y0 + 3) * scale - 1,
	                  (Uint8)(hud_killfire_bank * 16 + 2));

	const float fill = hud_killfire_prev + (hud_killfire_cur - hud_killfire_prev) * alpha;
	if (fill <= 0.0f)
		return;
	int right = x0 + (int)((fill + 1.0f) * scale + 0.5f) - 1;
	if (right > x1)
		right = x1;
	for (int r = 0; r < 3; ++r)
		fill_rectangle_xy(dst, x0, (y0 + r) * scale, right, (y0 + r + 1) * scale - 1,
		                  (Uint8)(hud_killfire_bank * 16 + 11 - r));
}

// Full meter: nothing burning and nothing left to recharge.
static bool hud_special_light_ready(void)
{
	return hud_light_burn_left == 0 && hud_light_charge_left == 0;
}

static void hud_special_light_step_flash(void)
{
	// A meter that has left full since the previous live tick owes a pop, whether or not this
	// machine ever saw the shot's edge. See doc/notes.md#gauges-and-effects.
	const bool ready = hud_special_light_ready();
	if (hud_light_meter_seen && hud_light_was_ready && !ready)
		hud_light_await_pop = true;
	hud_light_was_ready = ready;
	hud_light_meter_seen = true;

	// Check before arming the next shot so one-tick recharges and same-tick refires are visible.
	if (hud_light_await_pop && hud_light_armed)
	{
		hud_light_flash = HUD_LIGHT_FLASH;
		hud_light_await_pop = false;
		if (qa_net_gameplay_ticks > 0 && (qa_net_scenario == 5 || qa_net_scenario == 19))
			++qa_net_special_flashes;
	}
	else if (hud_light_flash > 0)
	{
		--hud_light_flash;
	}

	if (hud_light_fired)
		hud_light_await_pop = true;
}

// Compose replacement art over the shared lower half of a special icon.
void draw_special_icon(SDL_Surface *surface, int x, int y, JE_byte id)
{
	JE_word top = 0;
	const Sprite2_array *const sheet = JE_specialIconTop(id, &top);
	int x0, y0, x1, y1;

	if (sheet == NULL || !sprite2_ink_bounds(*sheet, top, &x0, &y0, &x1, &y1))
	{
		blit_sprite2x2(surface, x, y, spriteSheet10, special[id].itemgraphic);
		return;
	}

	const int half = HUD_SPECIAL_ICON_H / 2;
	blit_sprite2(surface, x,      y + half, spriteSheet10, SPECIAL_ICON_SHIP_GR + 19);
	blit_sprite2(surface, x + 12, y + half, spriteSheet10, SPECIAL_ICON_SHIP_GR + 20);

	// An odd-width sprite cannot sit dead centre, and the spare column reads better on its left.
	const int freeW = HUD_SPECIAL_ICON_W - (x1 - x0 + 1);
	const int freeH = half - (y1 - y0 + 1);
	blit_sprite2(surface, x + (freeW + 1) / 2 - x0, y + freeH / 2 - y0, *sheet, top);
}

/* Advance the special meter from its published clocks. Burn and recharge meet at empty, and the
 * fractional fill prevents long clocks from jumping a whole row at a time. */
static float hud_special_light_step(void)
{
	const bool burning = hud_light_burn_left > 0;

	// Specials without recharge keep a full meter.
	const bool ready = hud_special_light_ready();

	if (!rollback_resim_silent)
	{
		// Each phase is measured against its own opening value, so a short recharge can't inherit
		// a long one's scale and open half full.
		if (hud_light_burn_left > hud_light_burn_full)
			hud_light_burn_full = hud_light_burn_left;
		else if (!burning)
			hud_light_burn_full = 0;

		if (burning || hud_light_charge_left == 0)
			hud_light_charge_full = 0;
		else if (hud_light_charge_left > hud_light_charge_full)
			hud_light_charge_full = hud_light_charge_left;

		hud_special_light_step_flash();
	}

	if (ready)
		return HUD_LIGHT_ROWS;
	if (burning)
		return hud_light_burn_full > 0
		     ? (float)hud_light_burn_left * HUD_LIGHT_ROWS / hud_light_burn_full : 0.0f;
	if (hud_light_charge_full > 0)
		return (float)(hud_light_charge_full - hud_light_charge_left)
		     * HUD_LIGHT_ROWS / hud_light_charge_full;

	return 0.0f;
}

static void draw_special_ready_light(int x, int y)
{
	// Galaga's wing flies without a special shot at all, so JE_doSpecialShot never runs and there
	// is no charge to show; anything else that gates it out drops the light the same way.
	if (!hud_light_published)
	{
		hud_special_light_reset();
		return;
	}
	hud_light_published = false;

	const float filled = hud_special_light_step();

	if (!rollback_resim_silent)
	{
		hud_light_fill_prev = hud_light_fill_cur;
		hud_light_fill_cur = filled;
		hud_light_x = x;
		hud_light_y = y;
		hud_light_shown = true;
	}

	// Mark the ready light opaque so residual capture cannot punch holes in it.
	rl_mark_overlay_rect(x + HUD_LIGHT_INK_X, y + HUD_LIGHT_ROW_FIRST, HUD_LIGHT_INK_W, HUD_LIGHT_ROWS);

	hud_special_light_paint(VGAScreen, 1, x, y, filled, hud_light_flash);
}

int hud_top_left_right_edge(void)
{
	int right = 0;
	const uint local_player = gameplay_local_player_index();

	if (hud_special_block_shown(local_player) && !hud_special_on_right(local_player))
		right = hud_special_light_x(local_player) + HUD_SPECIAL_LIGHT_W;

	if (hud_lives_shown())
	{
		const uint lives = hud_lives_count(0);

		// The label sits at x28; the lives row starts at x30 and steps right, or collapses to
		// a single icon plus a count at x45 once past HUD_LIVES_ICONS_MAX.
		const int name_right = 28 + hud_player_name_width(0);
		const int lives_right = (lives > HUD_LIVES_ICONS_MAX) ? 45 + JE_textWidth("99", TINY_FONT)
		                      : (lives >= 1) ? 30 + (int)lives * 12
		                                     : 30;

		if (name_right > right)  right = name_right;
		if (lives_right > right) right = lives_right;
	}

	return right;
}

/* Test both instant and one-tick shared-special meter edges. */
void qa_test_special_light_events(void)
{
	hud_special_light_reset();

	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, true);
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();
	qa_check(hud_light_await_pop && hud_light_flash == 0,
	         "linked special light retains an instant special's fired edge across player two's pass");

	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();
	qa_check(!hud_light_await_pop && hud_light_flash == HUD_LIGHT_FLASH,
	         "linked instant special flashes on the next ready tick like single-player");

	hud_special_light_reset();
	hud_special_light_tick_begin();
	hud_special_light_publish(1, 0, true, true);
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();

	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, true);
	hud_special_light_publish(0, 0, false, false);
	hud_special_light_step_flash();
	qa_check(hud_light_await_pop && hud_light_flash == HUD_LIGHT_FLASH,
	         "linked one-tick recharge flashes and retains a same-tick refire for the next cycle");

	/* Rollback: the peer's shot and its whole recharge land on re-simulated ticks, so no live tick
	 * ever sees the fired edge. The meter having left full is what still owes the pop. */
	hud_special_light_reset();
	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();

	hud_special_light_tick_begin();
	hud_special_light_publish(4, 0, false, false);
	hud_special_light_step_flash();
	qa_check(hud_light_await_pop && hud_light_flash == 0,
	         "a meter that left full owes a pop for the shot only re-simulation saw");

	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();
	qa_check(!hud_light_await_pop && hud_light_flash == HUD_LIGHT_FLASH,
	         "that pop arrives when the meter comes back");

	/* A level that opens part way through a recharge has no full meter behind it, so it stays
	 * silent the way an outstanding fire from the previous level does. */
	hud_special_light_reset();
	hud_special_light_tick_begin();
	hud_special_light_publish(4, 0, false, false);
	hud_special_light_step_flash();
	hud_special_light_tick_begin();
	hud_special_light_publish(0, 0, true, false);
	hud_special_light_step_flash();
	qa_check(!hud_light_await_pop && hud_light_flash == 0,
	         "a recharge carried into a new level does not pop when it finishes");

	/* Verify that an equip starts a fresh meter phase despite the previous recharge. */
	const JE_byte saved_special = player[0].items.special;
	player[0].items.special = 1;  // the local ship needs a block for the meter to take the request

	hud_special_light_reset();
	hud_special_light_tick_begin();
	hud_special_light_publish(200, 0, false, false);
	hud_special_light_step();

	hud_special_light_tick_begin();
	hud_special_light_publish(120, 0, false, false);
	const float part_way = hud_special_light_step();
	qa_check(part_way > 4.0f && part_way < 5.5f,
	         "the meter fills across the recharge it is measuring");

	hud_special_light_rearm(0);
	hud_special_light_tick_begin();
	hud_special_light_publish(9, 0, false, false);
	qa_check(hud_special_light_step() == 0.0f,
	         "a special equipped mid-recharge opens its meter from empty");

	hud_special_light_tick_begin();
	hud_special_light_publish(4, 0, false, false);
	const float new_phase = hud_special_light_step();
	qa_check(new_phase > 6.0f && new_phase < 7.0f,
	         "and climbs across the lockout it arrived with");

	// A ship whose block is not on screen leaves the meter alone.
	player[0].items.special = 0;
	hud_special_light_rearm(0);
	qa_check(!hud_light_rearm_pending,
	         "a special equipped off screen does not rescale the meter");
	player[0].items.special = saved_special;

	hud_special_light_reset();
}

int hud_top_right_left_edge(void)
{
	int left = PLAYFIELD_RIGHT + 1;  // nothing claimed over there
	const uint local_player = gameplay_local_player_index();

	if (hud_special_block_shown(local_player) && hud_special_on_right(local_player))
		left = hud_special_light_x(local_player);

	if (!twoPlayerMode || (galagaMode && !coop_mode_active()) || !hud_lives_shown())
		return left;

	const uint lives = hud_lives_count(1);

	// Mirror of the above: the label is right-aligned to PLAYFIELD_WIDTH + 22 and the lives
	// row starts at PLAYFIELD_WIDTH + 7 stepping left. "99" stands in for the widest count.
	const int name_left = PLAYFIELD_WIDTH + 22 - hud_player_name_width(1);
	const int lives_left = (lives > HUD_LIVES_ICONS_MAX) ? hud_lives_count_left("99")
	                     : (lives >= 1) ? PLAYFIELD_WIDTH + 7 - ((int)lives - 1) * 12
	                                    : PLAYFIELD_WIDTH + 7;

	if (name_left < left)  left = name_left;
	if (lives_left < left) left = lives_left;

	return left;
}

// The FPS and kill counters slide left around a right-edge bar instead.

// Bottom row of the top-left cluster (special block, arcade name/lives rows), or -1.
int hud_top_left_bottom_edge(void)
{
	int bottom = -1;
	const uint local_player = gameplay_local_player_index();

	if (hud_special_block_shown(local_player) && !hud_special_on_right(local_player))
		bottom = HUD_SPECIAL_ICON_Y + HUD_SPECIAL_ICON_H - 1;

	// The deepest ink is the collapsed count at y+3 (shaded TINY_FONT rows end at y+11).
	// Using y+11 for plain icon rows too keeps the span steady across the collapse point.
	if (hud_lives_shown())
	{
		const int rowBottom = hud_lives_row_y(0) + 11;
		if (rowBottom > bottom)
			bottom = rowBottom;
	}

	return bottom;
}

// Bottom row of the top-right cluster, or -1. Player two's rows mirror in only when
// JE_inGameDisplays draws a second lives row.
int hud_top_right_bottom_edge(void)
{
	int bottom = -1;
	const uint local_player = gameplay_local_player_index();

	if (hud_special_block_shown(local_player) && hud_special_on_right(local_player))
		bottom = HUD_SPECIAL_ICON_Y + HUD_SPECIAL_ICON_H - 1;

	if (hud_lives_shown() && !(onePlayerAction && !dual_ship_mode()))
	{
		const int rowBottom = hud_lives_row_y(1) + 11;
		if (rowBottom > bottom)
			bottom = rowBottom;
	}

	return bottom;
}

// Top row of the bottom-left corner's claims: player one's score, and the superbomb row above
// it. A dual-ship session draws only the local ship's bombs, and draws them in this corner.
int hud_bottom_left_top_edge(void)
{
	int top = HUD_SCORE_Y - 1;

	if (hud_superbomb_count(dual_ship_mode() ? gameplay_local_player_index() : 0) > 0)
		top = HUD_SUPERBOMB_Y;

	return top;
}

// Top row of the bottom-right corner's claims (player two's score and superbombs), or
// vga_height with the corner empty.
int hud_bottom_right_top_edge(void)
{
	int top = vga_height;

	if (twoPlayerMode && (!galagaMode || coop_mode_active()))
	{
		top = HUD_SCORE_Y - 1;
		// A dual-ship session draws only the local ship's bombs, in the left corner.
		if (!dual_ship_mode() && hud_superbomb_count(1) > 0)
			top = HUD_SUPERBOMB_Y;
	}

	return top;
}

void JE_inGameDisplays(void)
{
	char stemp[21];
	char tempstr[256];

	// Mirror player scores inside the playfield edges. Account for JE_textWidth's trailing pixel and
	// FULL_SHADE's one-pixel outline.
	const int SCORE_INSET = 3;

	// Galaga hides the wing's score, but co-op's second ship is a player with a wallet.
	for (uint i = 0; i < ((twoPlayerMode && (!galagaMode || coop_mode_active())) ? 2 : 1); ++i)
	{
		// Name the two scores whenever the ships are independent; the linked pair reads as one
		// team and has no room for it.
		if (dual_ship_mode())
			snprintf(tempstr, sizeof(tempstr), "%s %lld", JE_getName(i + 1), (long long)player[i].cash);
		else
			snprintf(tempstr, sizeof(tempstr), "%lld", (long long)player[i].cash);

		// Mirror player one's left shadow edge against the right inset.
		const int width = JE_textWidth(tempstr, TINY_FONT);
		const int x = (i == 0)
		            ? PLAYFIELD_LEFT + SCORE_INSET
		            : PLAYFIELD_RIGHT - width - SCORE_INSET + 2;

		// Endless draws in the light cone's pale ink throughout: its random level order leaves no
		// fixed backdrop for the dark green default to sit against.
		if (smoothies[6 - 1] || endlessMode)
			JE_textShade(VGAScreen, x, vga_height - 25, tempstr, 8, 8, FULL_SHADE);
		else
			JE_textShade(VGAScreen, x, vga_height - 25, tempstr, 2, 4, FULL_SHADE);
	}

	// Show the local ship's Endless kill buff in the bottom-right. Shift it up or
	// left when a boss bar occupies that corner.
	bool killfire_drawn = false;
	const uint fxSaved = endlessFxPlayer();
	endlessSetFxPlayer(gameplay_local_player_index());
	if (endlessFxActive() && endlessTurbodriveActive())
	{
		const int bank = endlessKillBuffColorBank();
		const int baseRightX = PLAYFIELD_LEFT + PLAYFIELD_WIDTH - 5 + 2;  // +2px right of the FPS counter's edge
		const int rightX = baseRightX - boss_bar_hud_left_shift(baseRightX);
		const int yBase = 7;  // +7px down from the original placement

		// Place this last so it clears the FPS counter and a bottom boss bar.
		int floorRow = vga_height;  // nothing below to avoid
		if (show_fps)
		{
			const int fps_top = hud_fps_row() - 1;
			if (fps_top < floorRow)
				floorRow = fps_top;
		}
		{
			const int bar_top = boss_bar_bottom_band_top();
			if (bar_top < floorRow)
				floorRow = bar_top;
		}
		// Endless is one-player, so superbomb icons march rightward from the far side;
		// only a long row reaches under this readout.
		if (hud_superbomb_count(0) > 0
		    && hud_superbomb_p1_right() >= rightX - 60
		    && HUD_SUPERBOMB_Y < floorRow)
		{
			floorRow = HUD_SUPERBOMB_Y;
		}

		const int naturalBottom = vga_height - 26 + yBase;  // the timer bar's bottom row
		int yShift = naturalBottom - (floorRow - 2);
		if (yShift < 0)
			yShift = 0;

		char buf[48];

		snprintf(buf, sizeof(buf), "x%d", endlessKillBuffComboCount());
		JE_textShade(VGAScreen, rightX - JE_textWidth(buf, TINY_FONT), vga_height - 45 + yBase - yShift, buf, bank, 5, FULL_SHADE);

		// The middle line shows the current kill-fire boost or curse.
		if (endlessKillFireIsEvil())
		{
			snprintf(buf, sizeof(buf), "%s", endlessKillFireEvilName());
		}
		else
		{
			const int fireMult = endlessKillBuffFireMultiplier();
			const int dmgPct = endlessKillBuffDamagePercent();
			if (fireMult > 1 && dmgPct > 0)
				snprintf(buf, sizeof(buf), "FIREx%d DMG+%d%%", fireMult, dmgPct);
			else if (fireMult > 1)
				snprintf(buf, sizeof(buf), "FIREx%d", fireMult);
			else
				snprintf(buf, sizeof(buf), "DMG+%d%%", dmgPct);   // Overblast: damage only, no fire boost
		}
		JE_textShade(VGAScreen, rightX - JE_textWidth(buf, TINY_FONT), vga_height - 37 + yBase - yShift, buf, bank, 3, FULL_SHADE);

		const int bw = 60;
		const int mt = endlessKillBuffTicksMax();
		const int fillw = (mt > 0) ? bw * endlessKillBuffTicksLeft() / mt : 0;
		const int barX = rightX - bw;
		const int barY0 = vga_height - 28 + yBase - yShift;
		const int barY1 = vga_height - 26 + yBase - yShift;
		if (!rollback_resim_silent)
		{
			const float fill = (mt > 0) ? (float)(bw * endlessKillBuffTicksLeft()) / (float)mt
			                            : 0.0f;
			hud_killfire_prev = hud_killfire_shown ? hud_killfire_cur : fill;
			hud_killfire_cur = fill;
			hud_killfire_x0 = barX;
			hud_killfire_x1 = rightX;
			hud_killfire_y0 = barY0;
			hud_killfire_bank = bank;
			killfire_drawn = true;
		}
		rl_mark_overlay_rect(barX, barY0, rightX - barX + 1, barY1 - barY0 + 1);
		fill_rectangle_xy(VGAScreen, barX, barY0, rightX, barY1, bank * 16 + 2);  // dark track
		// Fill with a very weak vertical gradient within the buff's palette bank: brightest on the
		// top row, one shade darker per row down the bar's 3px height; a subtle top-to-bottom shade.
		if (fillw > 0)
			for (int y = barY0; y <= barY1; ++y)
			{
				int shade = 11 - (y - barY0);  // +11 top -> +9 bottom (one step per row; the bar is 3px tall)
				fill_rectangle_xy(VGAScreen, barX, y, barX + fillw, y, bank * 16 + shade);
			}
	}
	endlessSetFxPlayer(fxSaved);
	if (!rollback_resim_silent)
		hud_killfire_shown = killfire_drawn;

	/*Special Weapon?*/
	const uint local_player = gameplay_local_player_index();
	if (hud_special_block_shown(local_player))
	{
		draw_special_icon(VGAScreen, hud_special_icon_x(local_player), HUD_SPECIAL_ICON_Y,
		                  player[local_player].items.special);
		draw_special_ready_light(hud_special_light_x(local_player), HUD_SPECIAL_LIGHT_Y);
	}
	else
	{
		hud_special_light_reset();  // no block on screen; the next special charges from empty
	}

	/*Lives Left*/
	if (arcade_rules_active())
	{
		// One-player Action draws one row -- except in co-op, whose mini-games fly two
		// full ships and give each its own row, the same as Separate arcade.
		for (int temp = 0; temp < ((onePlayerAction && !dual_ship_mode()) ? 1 : 2); temp++)
		{
			const uint lives = hud_lives_count((uint)temp);

			// Only the ship carrying the special block moves down for it, so a Separate-arcade
			// joiner drops its own row and leaves player one's where it was.
			int y = hud_lives_row_y((uint)temp);
			// P2 anchors ride the widened playfield edge (legacy 270/250 would float
			// mid-field now), matching the name label's PLAYFIELD_WIDTH mapping below.
			tempW = (temp == 0) ? 30 : PLAYFIELD_WIDTH + 7;

			if (lives > HUD_LIVES_ICONS_MAX)
			{
				blit_sprite2(VGAScreen, tempW, y, spriteSheet9, 285);
				sprintf(tempstr, "%u", lives);
				// Both counts sit 3px from the icon: P1's runs right from x45, P2's is right-aligned
				// so its last pixel lands 3px left of the icon, whatever the digit count.
				tempW = (temp == 0) ? 45 : hud_lives_count_left(tempstr);
				JE_textShade(VGAScreen, tempW, y + 3, tempstr, 15, 1, FULL_SHADE);
			}
			else
			{
				for (uint i = 0; i < lives; ++i)
				{
					blit_sprite2(VGAScreen, tempW, y, spriteSheet9, 285);

					tempW += (temp == 0) ? 12 : -12;
				}
			}

			strcpy(stemp, (temp == 0) ? miscText[49-1] : miscText[50-1]);
			if (isNetworkGame)
			{
				strcpy(stemp, JE_getName(temp+1));
			}

			tempW = (temp == 0) ? 28 : (PLAYFIELD_WIDTH + 22 - JE_textWidth(stemp, TINY_FONT));
			JE_textShade(VGAScreen, tempW, y - HUD_LIVES_NAME_RISE, stemp, 2, 6, FULL_SHADE);
		}
	}

	/*Super Bombs!!*/
	// Each ship carries its own stock in a dual-ship session, so this machine shows only its
	// own row; the linked pair shows both, player two's counted in from the right edge.
	const uint first_bomb_player = dual_ship_mode() ? local_player : 0;
	const uint last_bomb_player = dual_ship_mode() ? local_player + 1 : COUNTOF(player);
	for (uint i = first_bomb_player; i < last_bomb_player; ++i)
	{
		int x = dual_ship_mode() || i == 0 ? 30 : PLAYFIELD_WIDTH + 7;

		for (uint j = hud_superbomb_count(i); j > 0; --j)
		{
			blit_sprite2(VGAScreen, x, 160, spriteSheet9, 304);
			x += (dual_ship_mode() || i == 0) ? 12 : -12;
		}
	}

	if (youAreCheating)
	{
		JE_outText(VGAScreen, 90, 170, "Cheaters always prosper.", 3, 4);
	}

	/* Optional FPS counter, bottom-right of the playfield (game_screen space, so
	 * the +24 composite offset keeps it just inside the right edge before the HUD). */
	if (show_fps)
	{
		char fps_str[16];
		snprintf(fps_str, sizeof(fps_str), "%d FPS", current_fps);

		// A right-side vertical boss bar owns the last few columns; slide left to clear it,
		// exactly as the endless readout does.
		const int fps_right_base = PLAYFIELD_LEFT + PLAYFIELD_WIDTH - 5;  // ~x318
		const int fps_right = fps_right_base - boss_bar_hud_left_shift(fps_right_base);
		const int fps_x = fps_right - JE_textWidth(fps_str, TINY_FONT);

		// Player 2's score is right-aligned into this same corner, so the counter stacks
		// above it when both are on screen (hud_fps_row).
		JE_textShade(VGAScreen, fps_x, hud_fps_row(), fps_str, 15, 2, FULL_SHADE);
	}

	JE_drawDebugOverlays();
}

static const int hud_sidekick_y[2][2] =
{
	{  64,  82 }, // one player HUD
	{ 108, 126 }, // two player HUD
};

bool hud_sidekicks_dirty = false;

uint hud_sidekick_player_index(void)
{
	// The strip has room for one ship's pods. A dual-ship game simulates both, so it shows
	// the local one; the split linked pair gives the pods to player two; otherwise player one.
	return dual_ship_mode() ? gameplay_local_player_index()
	                        : (split_arcade_mode() ? 1u : 0u);
}

int hud_sidekick_ammo_y(uint slot)
{
	return hud_sidekick_y[split_arcade_mode() ? 1 : 0][slot] + 13;
}

// Draw-only companion to JE_drawOptions: repaints the sidekick HUD boxes from current state.
void JE_drawOptionsHUD(void)
{
	// Skip HUD fills during silent replay because their sprite redraws are suppressed.
	if (rollback_resim_silent)
	{
		hud_sidekicks_dirty = true;
		return;
	}

	Player *this_player = &player[hud_sidekick_player_index()];
	const bool was_recording = render_list_recording;
	render_list_recording = false;

	for (uint i = 0; i < COUNTOF(this_player->sidekick); ++i)
	{
		JE_OptionType *this_option = &options[this_player->items.sidekick[i]];

		const int y = hud_sidekick_y[split_arcade_mode() ? 1 : 0][i];

		const int hud_x = HUD_X(284);
		fill_rectangle_xy(VGAScreenSeg, hud_x, y, hud_x + 28, y + 15, 0);
		if (this_option->icongr > 0)
			blit_sprite(VGAScreenSeg, hud_x, y, OPTION_SHAPES, this_option->icongr - 1);  // sidekick HUD icon
		draw_segmented_gauge(VGAScreenSeg, hud_x, y + 13, 112, 2, 2, AMMO_GAUGE_STEP(this_player->sidekick[i].ammo_max), this_player->sidekick[i].ammo);
	}

	render_list_recording = was_recording;
}

void JE_drawOptionLevel(void)
{
	if (split_arcade_mode())
	{
		for (temp = 1; temp <= 3; temp++)
		{
			fill_rectangle_xy(VGAScreenSeg, HUD_X(268), 127 + (temp - 1) * 6, HUD_X(269), 127 + 3 + (temp - 1) * 6, 193 + ((player[1].items.sidekick_level - 100) == temp) * 11);
		}
	}
}

void JE_drawOptions(void)
{
	SDL_Surface *temp_surface = VGAScreen;
	VGAScreen = VGAScreenSeg;

	JE_labelAmmoSidekicks();  // keep the shop names in step with the magazines we're about to load

	const uint first_player = dual_ship_mode() ? 0 : (twoPlayerMode ? 1 : 0);
	const uint last_player = dual_ship_mode() ? COUNTOF(player) : first_player + 1;
	for (uint p = first_player; p < last_player; ++p)
		JE_resetPlayerOptions(&player[p]);

	JE_drawOptionsHUD();

	VGAScreen = temp_surface;

	JE_drawOptionLevel();
}

int shieldGaugeFlash[2] = { 0, 0 };
int armorGaugeFlash[2]  = { 0, 0 };

// Local presentation timer; omitted from rollback.
#define SHIP_HP_BAR_HIT_TICKS 70   // ~2 seconds at the 35 Hz simulation rate
static int ship_hp_bar_hit[2] = { 0, 0 };

// clear On Hit timers
static void hud_ship_hp_bar_reset(void)
{
	ship_hp_bar_hit[0] = ship_hp_bar_hit[1] = 0;
}
static float gaugeFlashAlpha = 1.0f;

/* Tick draws use the 1x HUD; presentation draws gauges directly on the supersampled frame. */
static SDL_Surface *gauge_dst = NULL;
static int gauge_scale = 1;

/* Shield/armor levels at the previous and current tick. The bars are event-driven, so without
 * these the present pass could only redraw them at whole tick values and they would step. */
static float gauge_shield_prev[2], gauge_shield_cur[2];
static float gauge_armor_prev[2],  gauge_armor_cur[2];
static bool  gauge_interp = false;   // present pass: draw between the two, not at the live value
static float gaugeLevelAlpha = 1.0f;

static SDL_Surface *gauge_surface(void)
{
	return gauge_dst != NULL ? gauge_dst : VGAScreen;
}

static void gauge_fill(int x1, int y1, int x2, int y2, Uint8 color)
{
	const int s = gauge_scale;
	fill_rectangle_xy(gauge_surface(), x1 * s, y1 * s, (x2 + 1) * s - 1, (y2 + 1) * s - 1, color);
}

static float gauge_level(float prev, float cur, float live)
{
	if (!gauge_interp)
		return live;
	return prev + (cur - prev) * gaugeLevelAlpha;
}

static float gauge_shield_level(uint i)
{
	return gauge_level(gauge_shield_prev[i], gauge_shield_cur[i], (float)player[i].shield);
}

static float gauge_armor_level(uint i)
{
	return gauge_level(gauge_armor_prev[i], gauge_armor_cur[i], (float)player[i].armor);
}

// Seed both endpoints from the live values, so a level start (or a first draw) has nothing to
// interpolate away from.
void JE_resetGaugeRender(void)
{
	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		gauge_shield_prev[i] = gauge_shield_cur[i] = (float)player[i].shield;
		gauge_armor_prev[i]  = gauge_armor_cur[i]  = (float)player[i].armor;
	}
	hud_ship_hp_bar_reset();
}

static int gauge_flash_render(int cur)
{
	if (cur <= 0)
		return 0;
	int i = (int)(cur + 1.0f - gaugeFlashAlpha + 0.5f);
	if (i < 0)
		i = 0;
	return i;
}

static bool gauge_flash_any(void)
{
	for (int i = 0; i < 2; ++i)
		if (shieldGaugeFlash[i] > 0 || armorGaugeFlash[i] > 0)
			return true;
	return false;
}

// Silent replay defers raw gauge painting and requests one live repaint.
bool hud_bars_dirty = false;

static void JE_paintArmor(void);

// Draw-only repaint of both gauges from current state onto the HUD surface.
void JE_repaintShieldArmorBars(void)
{
	JE_wipeShieldArmorBars();
	SDL_Surface *saved = VGAScreen;
	VGAScreen = VGAScreenSeg;
	JE_drawShield();
	JE_paintArmor();
	VGAScreen = saved;
}

void JE_updateGaugeFlash(void)
{
	// Live ticks detect hits and advance the timer. Gauge samples still follow resimulation.
	if (!rollback_resim)
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			gauge_shield_prev[i] = gauge_shield_cur[i];
			gauge_armor_prev[i]  = gauge_armor_cur[i];

			const float now = (float)player[i].shield + (float)player[i].armor;
			if (player[i].is_alive && now < gauge_shield_cur[i] + gauge_armor_cur[i])
				ship_hp_bar_hit[i] = SHIP_HP_BAR_HIT_TICKS;
			else if (ship_hp_bar_hit[i] > 0)
				--ship_hp_bar_hit[i];
		}

	for (uint i = 0; i < COUNTOF(player); ++i)
	{
		gauge_shield_cur[i] = (float)player[i].shield;
		gauge_armor_cur[i]  = (float)player[i].armor;
	}

	if (!gauge_flash_any())
		return;

	// Step once per live tick. Re-simulation has already spent this presentation clock.
	if (!rollback_resim)
	{
		for (int i = 0; i < 2; ++i)
		{
			if (shieldGaugeFlash[i] > 0)
				--shieldGaugeFlash[i];
			if (armorGaugeFlash[i] > 0)
				--armorGaugeFlash[i];
		}
	}

	gaugeFlashAlpha = 1.0f;
	JE_repaintShieldArmorBars();
}

void gauge_bars_present(SDL_Surface *dst, int scale, float alpha)
{
	gauge_dst = dst;
	gauge_scale = scale;
	gaugeFlashAlpha = alpha;
	gaugeLevelAlpha = alpha;
	gauge_interp = true;

	JE_repaintShieldArmorBars();

	gauge_interp = false;
	gaugeLevelAlpha = 1.0f;
	gaugeFlashAlpha = 1.0f;
	gauge_scale = 1;
	gauge_dst = NULL;
}

// Clear the full slot before drawing an interpolated or supersampled bar. Callers redraw it now.
void JE_wipeShieldArmorBars(void)
{
	if (rollback_resim_silent)
	{
		hud_bars_dirty = true;
		return;
	}

	SDL_Surface *const saved = gauge_dst;
	if (gauge_dst == NULL)
		gauge_dst = VGAScreenSeg;  // the wipe never followed VGAScreen; the bar draws do

	for (uint g = 0; g < 2; ++g)
	{
		const int x = (g == 0) ? HUD_X(270) : HUD_X(307);
		if (!split_arcade_mode() || galagaMode)
		{
			gauge_fill(x, 137, x + 8, 194, 0);
		}
		else
		{
			for (uint i = 0; i < COUNTOF(player); ++i)
				gauge_fill(x, 60 + 134 * i - 44, x + 8, 60 + 134 * i, 0);
		}
	}

	gauge_dst = saved;
}

// Dim the remote ship's gauges after each repaint; the shade never compounds.
static bool gauge_is_remote(uint i)
{
	return isNetworkGame && thisPlayerNum >= 1 && thisPlayerNum <= 2 && i != thisPlayerNum - 1;
}

static void gauge_dim_rect(int x1, int y1, int x2, int y2)
{
	const int s = gauge_scale;
	SDL_Surface *const dst = gauge_surface();

	// Two shade passes (each halves the in-bank shade): a single one read too close to live.
	JE_barShade(dst, x1 * s, y1 * s, (x2 + 1) * s - 1, (y2 + 1) * s - 1);
	JE_barShade(dst, x1 * s, y1 * s, (x2 + 1) * s - 1, (y2 + 1) * s - 1);
}

// Label two-player gauge blocks in the shield bank, before the level fade-in.
void JE_drawPlayerTags(void)
{
	if (!split_arcade_mode() || galagaMode)
		return;

	for (uint i = 0; i < COUNTOF(player); ++i)
		JE_textShade(VGAScreen, HUD_X(289), 59 + 134 * i, (i == 0) ? "P1" : "P2", 0, 5, FULL_SHADE);
}

// Cap two-player gauge units to the 45-row region JE_wipeShieldArmorBars clears.
#define HUD_2P_GAUGE_UNITS_MAX 21

// Extend two-player bars into the final row covered by the wipe.
#define HUD_2P_GAUGE_TOP_PAD 1
static float hud_2p_gauge_units(float value)
{
	const float units = value * 0.8f;
	return (units > HUD_2P_GAUGE_UNITS_MAX) ? (float)HUD_2P_GAUGE_UNITS_MAX : units;
}

// Draw the full-shield marker on JE_dBar3's top row while the bar is below it.
static void draw_shield_ceiling_mark(int x, int bottom_y, float units_now, float units_max, int top_pad)
{
	if (units_now >= units_max)
		return;

	const int s = gauge_scale;
	const int y = (int)ceilf((float)((bottom_y - top_pad) * s) - (2.0f * units_max + 1.0f) * (float)s);
	if (y < 0)
		return;
	fill_rectangle_xy(gauge_surface(), x * s, y, (x + 9) * s - 1, y + s - 1, 68); /* <MXD> SEGa000 */
}

void JE_drawShield(void)
{
	if (rollback_resim_silent)
	{
		hud_bars_dirty = true;
		return;
	}

	if (split_arcade_mode() && !galagaMode)
	{
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			if (player_is_out(i))
				continue;

			const float units = hud_2p_gauge_units(gauge_shield_level(i));
			JE_dBar3_scaled(gauge_surface(), HUD_X(270), 60 + 134 * i, units, SHIELD_GAUGE_BASE,
			                gaugeGradShield, gauge_flash_render(shieldGaugeFlash[i]),
			                HUD_2P_GAUGE_TOP_PAD, gauge_scale);
			// Before the dim, so a remote player's mark fades with the rest of their gauge.
			draw_shield_ceiling_mark(HUD_X(270), 60 + 134 * i, units, hud_2p_gauge_units((float)player[i].shield_max), HUD_2P_GAUGE_TOP_PAD);
			if (gauge_is_remote(i))
				gauge_dim_rect(HUD_X(270), 60 + 134 * i - 44, HUD_X(278), 60 + 134 * i);
		}
	}
	else
	{
		const uint i = gameplay_local_player_index();
		if (player_is_out(i))
			return;

		const float units = gauge_shield_level(i);
		JE_dBar3_scaled(gauge_surface(), HUD_X(270), 194, units, SHIELD_GAUGE_BASE, gaugeGradShield,
		                gauge_flash_render(shieldGaugeFlash[i]), 0, gauge_scale);
		draw_shield_ceiling_mark(HUD_X(270), 194, units, (float)player[i].shield_max, 0);
	}
}

// Endless reinforced hulls can exceed the 28-unit armour bar.
const int armorGaugeLayerCol[ARMOR_GAUGE_LAYERS] = { 224, 32, 48, 64, 80, 112, 96, 128 };

static void endlessDrawArmorBar(float armor, int flash)
{
	const int maxLayers = ARMOR_GAUGE_LAYERS;
	const float layerUnits = (float)ARMOR_GAUGE_LAYER_UNITS;

	int total = 0;
	for (float t = armor; t > 0.0f && total < maxLayers; t -= layerUnits)
		++total;
	const int flashLayer = (total <= 1) ? 0 : total - 1;

	float a = armor;
	for (int layer = 0; a > 0.0f && layer < maxLayers; ++layer)
	{
		const float seg = (a > layerUnits) ? layerUnits : a;
		JE_dBar3_scaled(gauge_surface(), HUD_X(307), 194, seg, armorGaugeLayerCol[layer],
		                gaugeGradArmor, (layer == flashLayer) ? flash : 0, 0, gauge_scale);
		a -= layerUnits;
	}
}

// Small playfield health bars.

bool enemy_bar_place(int boxL, int boxR, int boxT, int boxB, int thick, bool vertical,
                     int *out_x, int *out_y, int *out_along)
{
	const int boxBottom = boxB - 1;                 // inclusive bottom sprite row
	const int cx = (boxL + boxR) / 2;               // box centre
	const int cy = (boxT + boxBottom) / 2;

	int along, x, y;

	if (!vertical)
	{
		// Horizontal bar: length spans the box width, inset 1px each end so boxes packed into a
		// row keep a visible gap between their bars.
		int xl = boxL + 1, xr = boxR - 1;
		along = xr - xl + 1;
		if (along < ENEMY_BAR_MIN_LEN)
			return false;
		if (along > ENEMY_BAR_MAX_LEN)
		{
			xl = cx - ENEMY_BAR_MAX_LEN / 2;
			along = ENEMY_BAR_MAX_LEN;
		}
		x = xl;

		switch (enemyBarPosition)
		{
		case ENEMY_BAR_POS_TOP:    y = boxT - thick - 1;                     break;  // above
		case ENEMY_BAR_POS_CENTER: y = cy - thick / 2;                       break;  // over the centre
		case ENEMY_BAR_POS_LEFT:   x = boxL - along - 1; y = cy - thick / 2; break;  // to the left
		case ENEMY_BAR_POS_RIGHT:  x = boxR + 2;         y = cy - thick / 2; break;  // to the right
		case ENEMY_BAR_POS_BOTTOM:
		default:                   y = boxB + 1;                             break;  // below (original)
		}
	}
	else
	{
		// Vertical bar: length spans the box height, inset 1px each end.
		int yt = boxT + 1, yb = boxBottom - 1;
		along = yb - yt + 1;
		if (along < ENEMY_BAR_MIN_LEN)
			return false;
		if (along > ENEMY_BAR_MAX_LEN)
		{
			yt = cy - ENEMY_BAR_MAX_LEN / 2;
			along = ENEMY_BAR_MAX_LEN;
		}
		y = yt;

		switch (enemyBarPosition)
		{
		case ENEMY_BAR_POS_LEFT:   x = boxL - thick - 1;                     break;  // to the left
		case ENEMY_BAR_POS_CENTER: x = cx - thick / 2;                       break;  // over the centre
		case ENEMY_BAR_POS_TOP:    y = boxT - along - 1; x = cx - thick / 2; break;  // above
		case ENEMY_BAR_POS_BOTTOM: y = boxB + 1;         x = cx - thick / 2; break;  // below
		case ENEMY_BAR_POS_RIGHT:
		default:                   x = boxR + 2;                             break;  // to the right
		}
	}

	*out_x = x;
	*out_y = y;
	*out_along = along;
	return true;
}

// Draw one gauge bar; `under` optionally colors its empty track.
static void ship_hp_bar_draw_one(int id, int x, int y, int along, bool vertical, float frac,
                                 int base, int under, Uint8 opacity)
{
	if (frac < 0.0f) frac = 0.0f; else if (frac > 1.0f) frac = 1.0f;

	const int fill = (int)(along * frac + 0.5f);
	// Use the enemy-bar shade rule inside the gauge's palette bank.
	const int col = (fill > 0) ? base + 5 + (int)(frac * 10.0f + 0.5f) : base;
	const Uint8 groove = (under != 0) ? (Uint8)(under + 10) : 0;

	rl_draw_hp_bar(VGAScreen, x, y, along, fill * RL_BAR_FILL_ONE, (Uint8)col, vertical, opacity,
	               groove);

	if (render_list_recording)
	{
		rl_current_id = id;
		rl_rec_hp_bar(x, y, along, fill * RL_BAR_FILL_ONE, (Uint8)col, vertical, opacity, groove);
		rl_current_id = 0;
	}
}

// Scale the configured bar opacity with the hull when Apply to Ship is enabled.
Uint8 hud_ship_hp_bar_opacity(void)
{
	const NetShipView view = netStyleLocalView();
	const int pct = view.shipOpacity
	                ? enemyBarOpacity * view.opacity / NET_OPACITY_FULL
	                : enemyBarOpacity;

	return (Uint8)(pct * 255 / 100);
}

void hud_draw_ship_hp_bars_at(int id, int boxL, int boxR, int boxT, int boxB,
                              uint shield, uint shieldMax, uint armor, uint armorMax)
{
	const Uint8 opacity = hud_ship_hp_bar_opacity();
	if (opacity == 0)
		return;

	const bool vertical = (enemyBarLayout == ENEMY_BAR_VERTICAL);
	// A ship with no shield fitted gets the armour bar alone; an always-empty gauge is noise.
	const int bars = (shieldMax > 0) ? 2 : 1;

	int x, y, along;
	if (!enemy_bar_place(boxL, boxR, boxT, boxB, bars * ENEMY_BAR_THICK, vertical, &x, &y, &along))
		return;

	// Match HUD order: shield above or left of armor.
	if (bars == 2)
	{
		ship_hp_bar_draw_one(id, x, y, along, vertical, (float)shield / (float)shieldMax,
		                     SHIELD_GAUGE_BASE, 0, opacity);
		if (vertical)
			x += ENEMY_BAR_THICK;
		else
			y += ENEMY_BAR_THICK;
	}

	// Show the current armor layer in the fill and the previous layer in the track.
	int layers = 1;
	for (uint a = armor;
	     a > ARMOR_GAUGE_LAYER_UNITS && layers < ARMOR_GAUGE_LAYERS;
	     a -= ARMOR_GAUGE_LAYER_UNITS)
		++layers;
	const int topArmor = (int)armor - (layers - 1) * ARMOR_GAUGE_LAYER_UNITS;

	// Only the layer containing armorMax may be shorter than a normal gauge layer.
	int layerMax = (int)armorMax - (layers - 1) * ARMOR_GAUGE_LAYER_UNITS;
	if (layerMax > ARMOR_GAUGE_LAYER_UNITS || layerMax < 1)
		layerMax = ARMOR_GAUGE_LAYER_UNITS;

	ship_hp_bar_draw_one(id, x, y, along, vertical,
	                     (float)topArmor / (float)layerMax,
	                     armorGaugeLayerCol[layers - 1],
	                     (layers > 1) ? armorGaugeLayerCol[layers - 2] : 0, opacity);
}

void hud_draw_ship_hp_bars_preview(int x, int y, int along,
                                   uint shield, uint shieldMax, uint armor, uint armorMax)
{
	if (along < 1 || shieldMax == 0 || armorMax == 0)
		return;

	ship_hp_bar_draw_one(0, x, y, along, false, (float)shield / (float)shieldMax,
	                     SHIELD_GAUGE_BASE, 0, 255);
	ship_hp_bar_draw_one(0, x, y + ENEMY_BAR_THICK, along, false, (float)armor / (float)armorMax,
	                     armorGaugeLayerCol[0], 0, 255);
}

// Hull bounds; graphics 0 and 1 are two-piece sentinels.
void hud_ship_hp_bar_box(uint seat, int *l, int *r, int *t, int *b)
{
	Sprite2_array *sheet;
	const JE_word gr = player_seat_hull(seat, &sheet);
	const bool wide = (gr == 0 || gr == 1);

	*l = (int)player[seat].x + (wide ? -17 : -5);
	*r = *l + (wide ? 48 : 24) - 1;
	*t = (int)player[seat].y - 7;
	*b = *t + 28;
}

void hud_draw_ship_hp_bars(void)
{
	const NetShipView view = netStyleLocalView();

	// Linked Arcade already shows both ships on its shared HUD.
	if (view.hpBars == NET_HP_BARS_OFF || !isNetworkGame || !twoPlayerMode
	    || split_arcade_mode())
		return;

	const uint seat = 1u - netStyleLocalSeat();
	if (seat >= COUNTOF(player) || !player[seat].is_alive)
		return;
	if (view.hpBars == NET_HP_BARS_ON_HIT && ship_hp_bar_hit[seat] <= 0)
		return;

	int l, r, t, b;
	hud_ship_hp_bar_box(seat, &l, &r, &t, &b);
	hud_draw_ship_hp_bars_at(RL_ID_SHIP_BAR_BASE + 1 + (int)seat, l, r, t, b,
	                         player[seat].shield, player[seat].shield_max,
	                         player[seat].armor, player[seat].initial_armor);
}

void JE_drawArmor(void)
{
	// Clamp classic armor before the draw gate; Endless uses rollover layers above 28.
	if (!endlessFxActive())
		for (uint i = 0; i < COUNTOF(player); ++i)
			if (player[i].armor > 28)
				player[i].armor = 28;

	JE_paintArmor();
}

static void JE_paintArmor(void)
{
	if (rollback_resim_silent)
	{
		hud_bars_dirty = true;
		return;
	}

	if (split_arcade_mode() && !galagaMode)
	{
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			if (player_is_out(i))
				continue;

			JE_dBar3_scaled(gauge_surface(), HUD_X(307), 60 + 134 * i,
			                hud_2p_gauge_units(gauge_armor_level(i)), armorGaugeLayerCol[0],
			                gaugeGradArmor, gauge_flash_render(armorGaugeFlash[i]),
			                HUD_2P_GAUGE_TOP_PAD, gauge_scale);
			if (gauge_is_remote(i))
				gauge_dim_rect(HUD_X(307), 60 + 134 * i - 44, HUD_X(315), 60 + 134 * i);
		}
	}
	else if (endlessFxActive())
	{
		const uint i = gameplay_local_player_index();
		if (player_is_out(i))
			return;

		endlessDrawArmorBar(gauge_armor_level(i), gauge_flash_render(armorGaugeFlash[i]));
	}
	else
	{
		const uint i = gameplay_local_player_index();
		if (player_is_out(i))
			return;

		JE_dBar3_scaled(gauge_surface(), HUD_X(307), 194, gauge_armor_level(i),
		                armorGaugeLayerCol[0], gaugeGradArmor,
		                gauge_flash_render(armorGaugeFlash[i]), 0, gauge_scale);
	}
}
