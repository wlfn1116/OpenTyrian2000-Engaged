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
#include "health_bars.h"

#include "config.h"
#include "draw.h"
#include "endless.h"
#include "enemy_death.h"
#include "game_loop.h"
#include "game_state.h"
#include "hud.h"
#include "render_list.h"
#include "video.h"

#include <limits.h>
#include <string.h>

// Before damage, the latest armor write becomes full health. After damage, full health can only
// grow. See doc/notes.md#combat.
void enemy_note_full_armor(struct JE_SingleEnemyType *enemy)
{
	if (enemy->armorleft >= 255)
		return;  // the invincible sentinel is not a health value
	if (!enemy->healthbar_seen || enemy->armorleft > enemy->healthbar_max)
		enemy->healthbar_max = enemy->armorleft;
}

boss_bar_t boss_bar[2];

// Link group 0 means both "unlinked enemy" and "unused bar slot". Never treat it as a match.
bool enemy_has_boss_bar(JE_byte linknum)
{
	if (linknum == 0)
		return false;
	for (unsigned int i = 0; i < COUNTOF(boss_bar); i++)
		if (linknum == boss_bar[i].link_num)
			return true;
	return false;
}

// Light this group's health bar for the frames the flash lasts. A hull with no bar has none to light.
void boss_bar_note_hit(JE_byte linknum)
{
	if (linknum == 0)
		return;
	for (unsigned int i = 0; i < COUNTOF(boss_bar); i++)
		if (linknum == boss_bar[i].link_num)
			boss_bar[i].color = 6;
}

static void JE_barX(JE_word x1, JE_word y1, JE_word x2, JE_word y2, JE_byte col)
{
	fill_rectangle_xy(VGAScreen, x1, y1,     x2, y1,     col + 1);
	fill_rectangle_xy(VGAScreen, x1, y1 + 1, x2, y2 - 1, col    );
	fill_rectangle_xy(VGAScreen, x1, y2,     x2, y2,     col - 1);
}

#define BOSS_BAR_GREY_BANK ENEMY_FLASH_BANK
#define HEALTH_BAR_BANK 112

/* Return the link group's bar palette and flash lift. Invulnerable groups use the grey bank. */
static void boss_bar_colours(JE_byte link_num, int *base, int *lift)
{
	int tier = 0;              // highest endless tier among the group's live parts
	unsigned int armor = 256;  // its most-damaged part, on boss_bar_survey's terms
	Uint32 cue = 0;            // frames left on the youngest arming cue among them

	if (link_num != 0)
	{
		for (unsigned int e = 0; e < COUNTOF(enemy); e++)
		{
			if (enemyAvail[e] != 0 || enemy[e].linknum != link_num)
				continue;

			if (enemy[e].eliteState > tier)
				tier = enemy[e].eliteState;
			if (enemy[e].armorleft < armor)
				armor = enemy[e].armorleft;

			const Uint32 left = enemy_armed_flash_left(e);
			if (left > cue)
				cue = left;
		}
	}

	*lift = (vulnerableCue != VULN_CUE_OFF) ? (int)cue : 0;

	if (vulnerableCue != VULN_CUE_OFF && armor == 255)
	{
		*base = BOSS_BAR_GREY_BANK;
		return;
	}

	const Uint8 tint = endlessFxActive() ? endlessEliteTint(tier) : 0;
	*base = (tint != 0) ? tint : HEALTH_BAR_BANK;  // palette bank 7 (default)
}

static void bbfill(SDL_Surface *dst, int x0, int y0, int x1, int y1, int scale, Uint8 color)
{
	fill_rectangle_xy(dst, x0 * scale, y0 * scale, (x1 + 1) * scale - 1, (y1 + 1) * scale - 1, color);
}

// Draw one boss bar within the supplied palette bank; fraction is 0..1.
#define BOSS_BAR_MIN_SIDE 4   // a frame any smaller draws nothing at all

// Mark only boss bars large enough to draw; marking an empty rectangle freezes its background.
static void boss_bar_mark_overlay(int gx, int gy, int gw, int gh)
{
	if (gw >= BOSS_BAR_MIN_SIDE && gh >= BOSS_BAR_MIN_SIDE)
		rl_mark_overlay_rect(gx, gy, gw, gh);
}

static void draw_boss_bar_gauge(SDL_Surface *dst, int scale, int gx, int gy, int gw, int gh,
                                bool horizontal, float fraction, int flash, int base)
{
	if (gw < BOSS_BAR_MIN_SIDE || gh < BOSS_BAR_MIN_SIDE)
		return;
	if (fraction < 0.0f)
		fraction = 0.0f;
	else if (fraction > 1.0f)
		fraction = 1.0f;

	const int BASE  = base;        // palette bank 7 normally; elite/champion tint in endless
	const int FRAME = BASE + 6;    // visible outline
	const int TRACK = BASE + 2;    // dark empty groove

	const int ix = gx + 1, iy = gy + 1;     // inner track origin
	const int iw = gw - 2, ih = gh - 2;     // inner track size
	const int cross = horizontal ? ih : iw; // bar thickness (across the fill)
	const int along = horizontal ? iw : ih; // bar length (along the fill)

	// Outline + recessed empty groove (always drawn, so a depleted bar still reads).
	bbfill(dst, gx, gy, gx + gw - 1, gy + gh - 1, scale, (Uint8)FRAME);
	bbfill(dst, ix, iy, ix + iw - 1, iy + ih - 1, scale, (Uint8)TRACK);

	const int fillLen = (int)(along * fraction * scale + 0.5f);
	if (fillLen <= 0)
		return;  // boss at (near) zero: just the empty groove shows
	const int capLen = (fillLen < scale) ? fillLen : scale;

	// Glossy fill: a brightness gradient across the thickness (highlight near the
	// first edge, darker at the far edge), lifted by the hit-flash amount.
	for (int c = 0; c < cross; ++c)
	{
		int shade = (cross <= 1) ? 13 : 15 - (c * 6) / (cross - 1);  // 127 -> ~121
		int col = BASE + shade + flash;
		if (col > BASE + 15)
			col = BASE + 15;

		if (horizontal)
			fill_rectangle_xy(dst, ix * scale, (iy + c) * scale, ix * scale + fillLen - 1,
			                  (iy + c + 1) * scale - 1, (Uint8)col);
		else
			fill_rectangle_xy(dst, (ix + c) * scale, (iy + ih) * scale - fillLen,
			                  (ix + c + 1) * scale - 1, (iy + ih) * scale - 1, (Uint8)col);
	}

	// Bright leading-edge cap at the tip of the fill for a crisp, readable edge.
	if (horizontal)
		fill_rectangle_xy(dst, ix * scale + fillLen - capLen, iy * scale, ix * scale + fillLen - 1,
		                  (iy + ih) * scale - 1, (Uint8)(BASE + 15));
	else
		fill_rectangle_xy(dst, ix * scale, (iy + ih) * scale - fillLen, (ix + iw) * scale - 1,
		                  (iy + ih) * scale - fillLen + capLen - 1, (Uint8)(BASE + 15));
}

// Shared by draw_boss_bars_enhanced and boss_bar_right_edge_x (below), so the two can never
// drift apart: the enhanced gauge's thickness and the gap between two grouped bars.
static const int BOSS_BAR_THICK = 7;
static const int BOSS_BAR_GAP   = 4;

static int boss_flash_render(int color, float alpha)
{
	if (color <= 0)
		return 0;
	int f = (int)(color + 1.0f - alpha + 0.5f);
	if (f < 0)
		f = 0;
	return f;
}

static Uint8 boss_fill_prev[2], boss_fill_cur[2], boss_fill_link[2];

void boss_bar_present_tick(void)
{
	for (unsigned int b = 0; b < COUNTOF(boss_bar); b++)
	{
		if (boss_bar[b].link_num != boss_fill_link[b])
		{
			boss_fill_link[b] = boss_bar[b].link_num;
			boss_fill_cur[b] = boss_bar[b].fill;
		}
		boss_fill_prev[b] = boss_fill_cur[b];
		boss_fill_cur[b] = boss_bar[b].fill;
	}
}

static float boss_bar_present_fill(unsigned int b, float alpha)
{
	const float fill = boss_fill_prev[b] + (boss_fill_cur[b] - boss_fill_prev[b]) * alpha;
	return fill / (float)BOSS_BAR_FULL;
}

// Blank rows or columns left between a bar's frame and the nearest HUD ink.
#define BOSS_BAR_CLEAR 2

void boss_bar_vertical_span(bool onLeft, int *top, int *bot)
{
	// The side bars are tuned a row lower than that shared clearance: three blank rows above
	// the frame and one below. The trailing step is off the neighbour's own inked row.
	const int aboveGap = BOSS_BAR_CLEAR + 1;
	const int belowGap = BOSS_BAR_CLEAR - 1;
	const int topMin = 7;    // three blank rows under the low-armor WARNING strip (rows 0..3)
	const int botMax = 176;  // one above the WARNING text's first row

	int vTop = (onLeft ? hud_top_left_bottom_edge() : hud_top_right_bottom_edge())
	         + aboveGap + 1;
	if (vTop < topMin)
		vTop = topMin;
	int vBot = (onLeft ? hud_bottom_left_top_edge() : hud_bottom_right_top_edge())
	         - belowGap - 1;
	if (vBot > botMax)
		vBot = botMax;

	*top = vTop;
	*bot = vBot;
}

// Lay out and draw the enhanced boss bars per the player's Enhancements
// settings (bossBarLayout / bossBarTwoMode). barCount is 1 or 2.
static void draw_boss_bars_enhanced(SDL_Surface *dst, int scale, float flashAlpha, bool tickDraw, unsigned int barCount)
{
	// Bars draw into game_screen (playfield space); JE_inGameDisplays draws the corner HUD
	// indicators in the same space, so keep bars centred on the playfield and clear of them.
	const int PF_L  = PLAYFIELD_LEFT;        // 24: left visible edge
	const int PF_R  = PLAYFIELD_RIGHT;       // 322: right visible edge, just before the HUD
	const int PF_CX = PF_L + PLAYFIELD_WIDTH / 2;    // 173: playfield centre
	const bool two  = (barCount == 2);

	const int THICK = BOSS_BAR_THICK;   // bar thickness
	const int GAP   = BOSS_BAR_GAP;     // spacing between two grouped bars

	const bool vertical  = (bossBarLayout == BOSS_BAR_LEFT || bossBarLayout == BOSS_BAR_RIGHT);
	const bool splitMode = (bossBarTwoMode == BOSS_BAR_TWO_SPLIT);
	const bool stackMode = (bossBarTwoMode == BOSS_BAR_TWO_STACKED);

	if (!vertical)
	{
		// Horizontal bars.
		const bool top = (bossBarLayout == BOSS_BAR_TOP);
		const bool sideBySide = two && splitMode;  // halves on one row; else stacked rows

		// Clear corner HUD clusters before placing top bars.
		const int fieldL = PF_L + BOSS_BAR_CLEAR;
		const int fieldR = PF_R - BOSS_BAR_CLEAR;
		int leftClear  = top ? (hud_top_left_right_edge() + BOSS_BAR_CLEAR) : fieldL;
		int rightClear = top ? (hud_top_right_left_edge() - BOSS_BAR_CLEAR - 1) : fieldR;

		// With no corner cluster on that side the measured edge falls outside the playfield,
		// so pin both limits to the visible area.
		if (leftClear < fieldL)   leftClear = fieldL;
		if (rightClear > fieldR)  rightClear = fieldR;
		const int leftHalf   = PF_CX - leftClear;
		const int rightHalf  = rightClear - PF_CX;
		const int half       = (leftHalf < rightHalf) ? leftHalf : rightHalf;
		const int fullL      = PF_CX - half;
		const int fullR      = PF_CX + half;
		const int fullW      = fullR - fullL + 1;

		// Clear the level timer at top and the current score/FPS band at bottom.
		const int topAnchor = levelTimer ? 18 : 6;
		const int botAnchor = hud_bottom_band_top() - BOSS_BAR_CLEAR - 1;

		for (unsigned int b = 0; b < barCount; b++)
		{
			int bx = fullL, bw = fullW, by;

			if (sideBySide)
			{
				bw = (fullW - GAP) / 2;
				bx = (b == 0) ? fullL : fullR - bw + 1;
			}

			if (top)
				by = topAnchor + ((two && !sideBySide) ? (int)b * (THICK + GAP) : 0);
			else  // bottom: stacked grows upward, bar 0 on top
				by = botAnchor - THICK + 1
				   - ((two && !sideBySide) ? (int)(barCount - 1 - b) * (THICK + GAP) : 0);

			if (tickDraw)  // the authoritative tick draw
				boss_bar_mark_overlay(bx, by, bw, THICK);

			int base, lift;
			boss_bar_colours(boss_bar[b].link_num, &base, &lift);
			const int hit = boss_flash_render(boss_bar[b].color, flashAlpha);

			draw_boss_bar_gauge(dst, scale, bx, by, bw, THICK, true,
			                    tickDraw ? boss_bar[b].fill / (float)BOSS_BAR_FULL
			                             : boss_bar_present_fill(b, flashAlpha),
			                    (lift > hit) ? lift : hit, base);
		}
	}
	else
	{
		// Derive vertical-bar spans from the corner HUD drawn this tick.
		const int edgeL = PF_L + BOSS_BAR_CLEAR;   // clear columns inside the left edge
		const int edgeR = PF_R - BOSS_BAR_CLEAR;   // ...and before the HUD sidebar

		for (unsigned int b = 0; b < barCount; b++)
		{
			const bool onLeft = (two && splitMode)
				? (b == 0)                              // one bar on each side
				: (bossBarLayout == BOSS_BAR_LEFT);     // single, or both on chosen side

			// Parallel ("Together") offsets the second bar inward; split/stacked share a column.
			const int slot = (two && !splitMode && !stackMode) ? (int)b : 0;

			int bx = onLeft
				? edgeL + slot * (THICK + GAP)
				: edgeR - THICK + 1 - slot * (THICK + GAP);

			int vTop, vBot;
			boss_bar_vertical_span(onLeft, &vTop, &vBot);

			// Stacked: split this side's span into a top half and a bottom half, the upper
			// bar shifted one row further down and the lower one row up.
			if (two && stackMode)
			{
				const int mid = (vTop + vBot) / 2;
				if (b == 0)
				{
					vTop += 1;
					vBot = mid - GAP / 2 + 1;      // upper bar
				}
				else
				{
					vTop = mid + GAP / 2;          // lower bar
					vBot -= 1;
				}
			}

			if (tickDraw)  // see the horizontal branch
				boss_bar_mark_overlay(bx, vTop, THICK, vBot - vTop + 1);

			int base, lift;
			boss_bar_colours(boss_bar[b].link_num, &base, &lift);
			const int hit = boss_flash_render(boss_bar[b].color, flashAlpha);

			draw_boss_bar_gauge(dst, scale, bx, vTop, THICK, vBot - vTop + 1, false,
			                    tickDraw ? boss_bar[b].fill / (float)BOSS_BAR_FULL
			                             : boss_bar_present_fill(b, flashAlpha),
			                    (lift > hit) ? lift : hit, base);
		}
	}
}

// Original compact double-sided boss bar (kept for the "Classic" setting).
static void draw_boss_bars_classic(unsigned int bars)
{
	const int playfield_left = PLAYFIELD_LEFT;
	const int center_x = playfield_left + PLAYFIELD_WIDTH / 2;

	for (unsigned int b = 0; b < bars; b++)
	{
		unsigned int x;

		if (bars == 2)
			x = center_x + ((b == 0) ? -30 : 30);
		else
			x = center_x + ((levelTimer) ? 95 : 0);  // level timer and boss bar would overlap

		unsigned int y = (levelTimer) ? 15 : 7;

		int base, lift;
		boss_bar_colours(boss_bar[b].link_num, &base, &lift);
		const int flash = (lift > boss_bar[b].color) ? lift : boss_bar[b].color;

		JE_barX(x - 25, y, x + 25, y + 5, base + 3);
		JE_barX(x - (boss_bar[b].fill / 10), y, x + (boss_bar[b].fill + 5) / 10, y + 5,
		        base + 6 + flash);
	}
}

// Draw one enemy-group bar around its on-screen bounds. Settings choose placement
// and opacity; the bank-7 color ramp tracks remaining health.
static void draw_enemy_hp_bar(int id, int boxL, int boxR, int boxT, int boxB, float frac,
                              int barBase, float par_frac, int par_layer, float par_anchor,
                              int par_ybase, float par_yfrac, int par_ylayer)
{
	if (frac < 0.0f) frac = 0.0f; else if (frac > 1.0f) frac = 1.0f;

	const Uint8 opacity = (Uint8)(enemyBarOpacity * 255 / 100);
	if (opacity == 0)
		return;  // fully transparent: nothing to draw or record

	const bool vertical = (enemyBarLayout == ENEMY_BAR_VERTICAL);

	int x, y, along;
	if (!enemy_bar_place(boxL, boxR, boxT, boxB, ENEMY_BAR_THICK, vertical, &x, &y, &along))
		return;

	const int fill = (int)(along * frac * RL_BAR_FILL_ONE + 0.5f);
	// Shade the fill by remaining health within the bar's palette bank.
	const int col = (fill > 0) ? barBase + 5 + (int)(frac * 10.0f + 0.5f) : barBase;

	// Draw into the authoritative tick frame.
	rl_draw_hp_bar(VGAScreen, x, y, along, fill, (Uint8)col, vertical, opacity, 0);

	// Record the bar under its enemy slot for matching and interpolation.
	if (render_list_recording)
	{
		rl_current_id = id;
		rl_current_par_frac = par_frac;    // float the bar's parallax to match its enemy
		rl_current_par_layer = par_layer;
		rl_current_par_anchor = par_anchor;
		rl_current_par_ybase = par_ybase;
		rl_current_par_yfrac = par_yfrac;  // float the bar's vertical scroll to match its enemy
		rl_current_par_ylayer = par_ylayer;
		rl_rec_hp_bar(x, y, along, fill, (Uint8)col, vertical, opacity, 0);
		rl_current_id = 0;
		rl_current_par_frac = 0.0f;
		rl_current_par_layer = 0;
		rl_current_par_anchor = 0.0f;
		rl_current_par_ybase = 0;
		rl_current_par_yfrac = 0.0f;
		rl_current_par_ylayer = 0;
	}
}

void draw_enemy_hp_bar_box(int id, int boxL, int boxR, int boxT, int boxB, float frac)
{
	draw_enemy_hp_bar(id, boxL, boxR, boxT, boxB, frac, HEALTH_BAR_BANK, 0.0f, 0, 0.0f, 0, 0.0f, 0);
}

// Draw one small bar per damaged, non-boss linked hull.
void draw_enemy_health_bars(void)
{
	if (!enemyBars)
		return;

	bool done[100] = { false };

	for (unsigned int e = 0; e < 100; e++)
	{
		if (enemyAvail[e] != 0 || done[e])
			continue;

		const int link = enemy[e].linknum;

		// Groups that already own a boss bar are handled by draw_boss_bar().
		if (link != 0 && (link == boss_bar[0].link_num || link == boss_bar[1].link_num))
		{
			done[e] = true;
			continue;
		}

		// Accumulate the group's on-screen bounding box and most-damaged fraction.
		bool shown = false;
		float frac = 1.0f;
		int left = 99999, right = -99999, top = 99999, bottom = -99999;

		for (unsigned int f = e; f < 100; f++)
		{
			// Skip freed (1) and lingering non-damageable (2) slots; this also shrinks
			// a linkgroup's bar to just its surviving parts.
			if (enemyAvail[f] != 0)
				continue;
			if (link == 0 ? (f != e) : (enemy[f].linknum != (JE_byte)link))
				continue;

			done[f] = true;

			// A size-1 enemy spans four 12x14 cells around its anchor.
			const bool big = (enemy[f].size == 1);
			const int sx = enemy[f].ex + enemy[f].mapoffset + (big ? -6 : 0);
			const int sy = enemy[f].ey + (big ? -7 : 0);
			const int sw = big ? 24 : 12;
			const int sh = big ? 28 : 14;

			if (sx < left)             left = sx;
			if (sx + sw - 1 > right)   right = sx + sw - 1;
			if (sy < top)              top = sy;
			if (sy + sh > bottom)      bottom = sy + sh;

			// Bar only while alive AND damageable: armorleft == 0 is dead/dying, 255 is the "invincible"
			// sentinel.
			if (enemy[f].healthbar_seen && enemy[f].armorleft > 0 && enemy[f].armorleft < 255 &&
			    enemy[f].healthbar_max >= ENEMY_BAR_MIN_HP)
			{
				shown = true;
				const float f2 = (float)enemy[f].armorleft / (float)enemy[f].healthbar_max;
				if (f2 < frac)
					frac = f2;
			}
		}

		if (shown)
		{
			// Endless special enemies get a bar in their tint bank (elite / champion) so the
			// bar reads as part of the enemy; ordinary enemies keep the bank-7 yellow ramp.
			const Uint8 barTint = endlessEliteTint(enemy[e].eliteState);
			const int barBase = (barTint != 0) ? barTint : HEALTH_BAR_BANK;  // palette bank 7
			// Preserve the representative enemy's draw-batch anchor for its bar.
			const int par_layer = (e < 25) ? 2 : (e < 50) ? 1 : (e < 75) ? 3 : 1;
			const float par_anchor = (float)(enemy[e].mapoffset - PLAYFIELD_X_SHIFT) + enemy[e].mapoffset_frac;
			draw_enemy_hp_bar(RL_ID_ENEMYBAR_BASE + (int)e, left, right, top, bottom, frac,
			                  barBase, enemy[e].mapoffset_frac, par_layer, par_anchor,
			                  enemy[e].scroll_ybase, enemy[e].scroll_yfrac,
			                  enemy[e].scroll_ylayer);
		}
	}
}

JE_byte boss_bar_fill(unsigned int armorleft, unsigned int full)
{
	if (armorleft >= 255)
		return BOSS_BAR_FULL;  // invincible: a scripted phase shows a full bar
	if (full == 0 || armorleft >= full)
		return BOSS_BAR_FULL;
	return (JE_byte)((armorleft * BOSS_BAR_FULL + full / 2) / full);
}

void boss_bar_survey(JE_byte link_num, unsigned int *out_armor, unsigned int *out_full)
{
	unsigned int armor = 256;  // higher than armor max
	unsigned int full = 0;

	for (unsigned int e = 0; e < COUNTOF(enemy); e++)  // find most damaged
	{
		if (enemyAvail[e] == 0 && enemy[e].linknum == link_num)
			if (enemy[e].armorleft < armor)
			{
				armor = enemy[e].armorleft;
				full = enemy[e].healthbar_max;
			}
	}

	*out_armor = armor;
	*out_full = full;
}

static unsigned int boss_bar_count(void)
{
	return (boss_bar[0].link_num != 0 ? 1 : 0) + (boss_bar[1].link_num != 0 ? 1 : 0);
}

void boss_bar_tick(void)
{
	for (unsigned int b = 0; b < COUNTOF(boss_bar); b++)
	{
		if (boss_bar[b].link_num == 0)
			continue;

		unsigned int armor, full;
		boss_bar_survey(boss_bar[b].link_num, &armor, &full);

		if (armor > 255 || armor == 0)  // boss dead?
		{
			boss_bar[b].link_num = 0;
			// Count a boss once, when its active health bar disappears on death.
			if (endlessMode)
				++endlessRunBossKills;
			// SHOCKWAVE boon, top tier: a boss bar emptying wipes the WHOLE field of enemy fire, so the
			// screenful of bullets a dying boss leaves behind can't kill you after the fact.
			if (endlessShockwaveActive())
				endlessShockwaveClear(0, 0, -1);
		}
		else
			boss_bar[b].fill = boss_bar_fill(armor, full);
	}

	// if only one bar left, make it the first one
	if (boss_bar_count() == 1 && boss_bar[0].link_num == 0)
	{
		memcpy(&boss_bar[0], &boss_bar[1], sizeof(boss_bar_t));
		boss_bar[1].link_num = 0;
	}
}

void boss_bar_fade_flash(void)
{
	for (unsigned int b = 0; b < boss_bar_count(); b++)
		if (boss_bar[b].color > 0)
			boss_bar[b].color--;
}

void draw_boss_bar(void)
{
	const unsigned int bars = boss_bar_count();
	if (bars == 0)
		return;

	if (bossBarStyle == BOSS_BAR_ENHANCED)
		draw_boss_bars_enhanced(VGAScreen, 1, 1.0f, true, bars);
	else
		draw_boss_bars_classic(bars);
}

static void boss_bar_x_scaled(SDL_Surface *dst, int x1, int y1, int x2, int y2, int scale, int col)
{
	fill_rectangle_xy(dst, x1, y1 * scale, x2, (y1 + 1) * scale - 1, (Uint8)(col + 1));
	fill_rectangle_xy(dst, x1, (y1 + 1) * scale, x2, y2 * scale - 1, (Uint8)col);
	fill_rectangle_xy(dst, x1, y2 * scale, x2, (y2 + 1) * scale - 1, (Uint8)(col - 1));
}

static void draw_boss_bars_classic_present(SDL_Surface *dst, int scale, float alpha,
                                           unsigned int bars)
{
	const int center_x = PLAYFIELD_LEFT + PLAYFIELD_WIDTH / 2;

	for (unsigned int b = 0; b < bars; b++)
	{
		int x;
		if (bars == 2)
			x = center_x + ((b == 0) ? -30 : 30);
		else
			x = center_x + ((levelTimer) ? 95 : 0);
		const int y = (levelTimer) ? 15 : 7;

		int base, lift;
		boss_bar_colours(boss_bar[b].link_num, &base, &lift);
		const int hit = boss_flash_render(boss_bar[b].color, alpha);
		const int flash = (lift > hit) ? lift : hit;
		const float fill = boss_bar_present_fill(b, alpha) * BOSS_BAR_FULL;

		boss_bar_x_scaled(dst, (x - 25) * scale, y, (x + 26) * scale - 1, y + 5, scale, base + 3);
		boss_bar_x_scaled(dst, x * scale - (int)(fill * scale / 10.0f), y,
		                  (x + 1) * scale - 1 + (int)((fill + 5.0f) * scale / 10.0f), y + 5, scale,
		                  base + 6 + flash);
	}
}

// Redraw boss bars with an interpolated fill and hit flash.
void draw_boss_bar_present(SDL_Surface *dst, int scale, float alpha)
{
	const unsigned int bars = boss_bar_count();
	if (bars == 0)
		return;

	if (bossBarStyle == BOSS_BAR_ENHANCED)
		draw_boss_bars_enhanced(dst, scale, alpha, false, bars);
	else
		draw_boss_bars_classic_present(dst, scale, alpha, bars);
}

// Shift needed to clear a right-side enhanced boss bar, or 0. Keep this geometry in sync with
// draw_boss_bars_enhanced.
int boss_bar_hud_left_shift(int hudRightX)
{
	const unsigned int bars = boss_bar_count();
	if (bars == 0 || bossBarStyle != BOSS_BAR_ENHANCED)
		return 0;
	if (bossBarLayout != BOSS_BAR_LEFT && bossBarLayout != BOSS_BAR_RIGHT)
		return 0;  // horizontal layouts never occupy the right edge column

	const bool splitMode = (bossBarTwoMode == BOSS_BAR_TWO_SPLIT);
	const bool onRight = (bossBarLayout == BOSS_BAR_RIGHT) || (bars == 2 && splitMode);
	if (!onRight)
		return 0;  // vertical bar(s) confined to the left edge

	// Two bars side-by-side ("Together") widen the occupied column; Stacked/Split/single share
	// just one THICK-wide column (see the bx formula in draw_boss_bars_enhanced).
	const bool together = (bars == 2 && !splitMode && bossBarTwoMode == BOSS_BAR_TWO_TOGETHER);
	const int slots = together ? 1 : 0;
	const int leftmostX = (PLAYFIELD_RIGHT - BOSS_BAR_CLEAR) - BOSS_BAR_THICK + 1
	                    - slots * (BOSS_BAR_THICK + BOSS_BAR_GAP);

	return (hudRightX >= leftmostX) ? (hudRightX - leftmostX + 4) : 0;  // +4px clearance
}

// True while an Enhanced BOTTOM horizontal boss bar is shown; it can span near the full
// playfield width and sits close to the bottom edge, unlike TOP which never reaches there.
static bool boss_bar_hud_needs_up_shift(void)
{
	return boss_bar_count() > 0 && bossBarStyle == BOSS_BAR_ENHANCED
	    && bossBarLayout == BOSS_BAR_BOTTOM;
}

// Top row of a bottom Enhanced boss bar, or INT_MAX. Keep in step with draw_boss_bars_enhanced.
int boss_bar_bottom_band_top(void)
{
	const unsigned int bars = boss_bar_count();
	if (!boss_bar_hud_needs_up_shift())
		return INT_MAX;

	// Two bars stack into rows unless Split puts them side by side on one row.
	const bool stackedRows = (bars == 2 && bossBarTwoMode != BOSS_BAR_TWO_SPLIT);
	const int rows = stackedRows ? 2 : 1;

	const int botAnchor = hud_bottom_band_top() - BOSS_BAR_CLEAR - 1;   // as in the draw
	return botAnchor - (rows * BOSS_BAR_THICK + (rows - 1) * BOSS_BAR_GAP) + 1;
}
