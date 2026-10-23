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
#ifndef HUD_H
#define HUD_H

#include "opentyr.h"

#include "SDL.h"

// Set when a message-bar line was posted from a silent rollback pass, which drew nothing;
// JE_main clears it by repainting the held line on the next pass that reaches the screen.
extern bool hud_message_dirty;

void JE_drawTextWindow(const char *text);
void JE_drawTextWindowSplit(const char *tint, unsigned int tint_bank, const char *left,
                            const char *right, int right_x);
void JE_repaintTextWindow(void);
void JE_drawPortConfigButtons(void);
void draw_special_icon(SDL_Surface *surface, int x, int y, JE_byte id);

void JE_inGameDisplays(void);

/* Bottom-band HUD precedence: scores, FPS, boss bars, then the Endless readout. */
int hud_fps_row(void);            // text row the FPS counter occupies
int hud_bottom_band_top(void);    // topmost row the scores/FPS claim anywhere across the width

// Horizontal extent of the top corner clusters (name label, lives row, special-weapon block),
// so a centred TOP boss bar stops short of them instead of being clipped to a legacy constant.
int hud_top_left_right_edge(void);
int hud_top_right_left_edge(void);

// Vertical HUD bounds used by side-hugging boss bars; -1 means an empty top corner.
int hud_top_left_bottom_edge(void);
int hud_top_right_bottom_edge(void);
int hud_bottom_left_top_edge(void);
int hud_bottom_right_top_edge(void);

/* Special icon and ready light for the locally drawn HUD. The name and lives move
 * down to clear it; boss-bar layout reads these bounds. */
#define HUD_SPECIAL_ICON_W   24  // blit_sprite2x2: two 12px columns...
#define HUD_SPECIAL_ICON_H   28  // ...by two 14px rows
#define HUD_SPECIAL_ICON_Y    1
#define HUD_SPECIAL_LIGHT_W  12  // one sprite2 column...
#define HUD_SPECIAL_LIGHT_H  14
#define HUD_SPECIAL_LIGHT_Y   8  // ...centred against the icon's rows
// Tiny-font baselines leave one blank row around adjacent HUD elements.
#define HUD_LIVES_NAME_RISE  10  // rows the name label sits above the lives row
#define HUD_LIVES_Y          18
#define HUD_LIVES_Y_SPECIAL  41  // pushed below the icon when this ship holds a special
bool hud_special_block_shown(uint p);   // ship p's special is the one drawn at the top
bool hud_special_on_right(uint p);      // ...mirrored into the right corner, not the left one
int  hud_special_icon_x(uint p);
int  hud_special_light_x(uint p);
int  hud_lives_row_y(uint p);           // row ship p's lives sit on, name HUD_LIVES_NAME_RISE above
// Icons these rows draw, and the counts their layout is measured from. Zero for a ship that is out.
uint hud_lives_count(uint p);
uint hud_superbomb_count(uint p);
// Per-ship recharge and active-effect clocks for the ready light. `armed` is
// sampled at the fire gate; `fired` covers specials with no recharge.
void hud_special_light_publish(int charge_ticks, int burn_ticks, bool armed, bool fired);
// Drop the meter's carried-over state; level setup calls it so a level cannot open on the previous
// level's cooldown (which would read as the special arming, and pop).
void hud_special_light_reset(void);
// Begin a meter phase for a newly equipped special.
void hud_special_light_rearm(uint p);
void hud_special_light_tick_begin(void);
// Repaint the ready light for one displayed frame: the meter at `alpha` between the previous and
// current tick's fill, at `scale` into the supersampled playfield, over what the residual re-applied.
void hud_special_light_present(SDL_Surface *dst, int scale, float alpha);
void hud_killfire_bar_present(SDL_Surface *dst, int scale, float alpha);

#define GAUGE_FLASH_START 6

void JE_drawOptionsHUD(void);
// Player and row owning the sidekick HUD strip currently being drawn.
uint hud_sidekick_player_index(void);
int  hud_sidekick_ammo_y(uint slot);
void JE_drawPlayerTags(void);  // two-player HUD "P1"/"P2" marks; no-op otherwise
extern bool hud_sidekicks_dirty;
void JE_drawOptionLevel(void);

void JE_wipeShieldArmorBars(void);
void JE_drawShield(void);
void JE_drawArmor(void);

// Shared geometry for enemy and partner health bars.
enum
{
	ENEMY_BAR_MIN_HP  = 1,   // any enemy that survives a hit
	ENEMY_BAR_MAX_LEN = 48,  // cap wide link groups
	ENEMY_BAR_MIN_LEN = 3,   // skip unreadable bars
	ENEMY_BAR_THICK   = 2,
};

/* Place a bar block around a box using enemy-bar settings. Returns false if the box is too small. */
bool enemy_bar_place(int boxL, int boxR, int boxT, int boxB, int thick, bool vertical,
                     int *out_x, int *out_y, int *out_along);

// The palette bank the shield gauge is drawn in; the armour gauge's is armorGaugeLayerCol[0].
#define SHIELD_GAUGE_BASE 144

// Armor rollover units and palette bank for each full layer.
#define ARMOR_GAUGE_LAYER_UNITS 28
#define ARMOR_GAUGE_LAYERS 8
extern const int armorGaugeLayerCol[ARMOR_GAUGE_LAYERS];

/* Draw the partner's gauges with enemy-bar placement. `_at` also accepts preview values;
 * shieldMax 0 omits the shield. */
void hud_draw_ship_hp_bars(void);
void hud_draw_ship_hp_bars_at(int id, int boxL, int boxR, int boxT, int boxB,
                              uint shield, uint shieldMax, uint armor, uint armorMax);
void hud_ship_hp_bar_box(uint seat, int *l, int *r, int *t, int *b);
Uint8 hud_ship_hp_bar_opacity(void);
void hud_draw_ship_hp_bars_preview(int x, int y, int along,
                                   uint shield, uint shieldMax, uint armor, uint armorMax);

// Half-full samples for the outpost preview.
#define HUD_HP_BAR_SAMPLE_SHIELD     1
#define HUD_HP_BAR_SAMPLE_SHIELD_MAX 2
#define HUD_HP_BAR_SAMPLE_ARMOR      (ARMOR_GAUGE_LAYER_UNITS / 2)
#define HUD_HP_BAR_SAMPLE_ARMOR_MAX  ARMOR_GAUGE_LAYER_UNITS
void hud_ship_hp_bar_reset(void);   // clear On Hit timers

extern int shieldGaugeFlash[2];
extern int armorGaugeFlash[2];
void JE_updateGaugeFlash(void);
void JE_resetGaugeRender(void);
// Repaint both gauges for one displayed frame: at `alpha` between the previous and current tick's
// levels, and at `scale` into the supersampled frame (dst NULL = the classic 1x HUD surface).
void gauge_bars_present(SDL_Surface *dst, int scale, float alpha);
extern bool hud_bars_dirty;
void JE_repaintShieldArmorBars(void);

#endif /* HUD_H */
