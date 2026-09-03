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
#ifndef MAININT_H
#define MAININT_H

#include "config.h"
#include "opentyr.h"
#include "palette.h"
#include "player.h"
#include "sprite.h"

extern bool button[4]; // fire, left fire, right fire, mode swap

extern JE_shortint constantLastX;
extern JE_word textErase;
extern Sint64 upgradeCost;
extern Sint64 downgradeCost;
extern JE_boolean performSave;
extern JE_boolean jumpSection;
extern JE_boolean useLastBank;

extern bool pause_pressed, ingamemenu_pressed, changefire_pressed;

/*void JE_textMenuWait(JE_word waittime, JE_boolean dogamma);*/

// Set when a message-bar line was posted from a silent rollback pass, which drew nothing;
// JE_main clears it by repainting the held line on the next pass that reaches the screen.
extern bool hud_message_dirty;

void JE_drawTextWindow(const char *text);
void JE_drawTextWindowSplit(const char *tint, unsigned int tint_bank, const char *left,
                            const char *right, int right_x);
void JE_repaintTextWindow(void);
void JE_initPlayerData(void);
void JE_highScoreScreen(void);
// The co-op Campaign board's second line: who was flying, then the terms their figure was earned
// on, shortened to widthPx. The Px call is the width the board itself gives it.
void coopCampaignRecordLine(char *out, size_t outSize, const CoopCampaignScore *record,
                            int widthPx);
int coopCampaignRecordLineWidthPx(void);
// One ship's Endless per-tick work; see the definition in mainint.c.
void endlessPerShipTick(Player *this_player);
// Spend a charged Opening Salvo on the volley this ship fires this tick, before the special does.
bool endlessArmOpeningSalvoForTick(Player *this_player, JE_byte playerNum_);
void JE_gammaCorrect_func(JE_byte *col, JE_real r);
void JE_gammaCorrect(Palette *colorBuffer, JE_byte gamma);
JE_boolean JE_gammaCheck(void);
/* void JE_textMenuWait(JE_word *waitTime, JE_boolean doGamma); /!\ In setup.h */
void JE_nextEpisode(void);
void JE_helpSystem(JE_byte startTopic);
void JE_doInGameSetup(void);
JE_boolean JE_inGameSetup(void);

// Only specials with a complete HUD icon set are safe to equip.
bool debug_special_is_safe(int id);
void draw_special_icon(SDL_Surface *surface, int x, int y, JE_byte id);
// One-line "the other machine is not with us yet" panel over a frozen gameplay frame. Draws into
// whatever VGAScreen points at, so callers must have pointed it at VGAScreenSeg first.
void JE_drawNetworkNotice(const char *text);

// Endless death prompt (Hardcore off only), shown over the frozen death frame before the run
// summary. Values double as the row order.
typedef enum
{
	ENDLESS_DEATH_RESTART = 0,  // fly the same zone again from the launch-time snapshot
	ENDLESS_DEATH_OUTPOST,      // back to the outpost, exactly like the pause menu's Quit Level
	ENDLESS_DEATH_END_RUN,      // on to the Run Over summary
}
EndlessDeathChoice;

EndlessDeathChoice JE_endlessDeathMenu(void);
void JE_debugMenu(bool center);
// Rebuild loadout-derived state while preserving live armour and shield.
void debugLoadoutRefresh(bool overHud);
bool JE_extraMenu(void);
void JE_inGameHelp(void);
void JE_sortHighScores(void);
void JE_highScoreCheck(void);
// How an online Timed Battle ends: the two purses side by side and who took the race. Replaces
// the solo mode's name-entry dialog, which blocks on a keyboard the other machine cannot see.
void JE_timedBattleResult(void);
// Whether the score-based difficulty drift between levels runs; false for the whole of an Endless
// run, which stays on the rung it launched with. Public so the unit suite can pin that.
bool difficulty_adjust_active(void);
void adjust_difficulty(void);

bool load_next_demo(void);
bool replay_demo_keys(void);

void JE_SFCodes(JE_byte playerNum_, JE_integer PX_, JE_integer PY_, JE_integer mouseX_, JE_integer mouseY_);
// Resolves a movement intent (positive right and down) to the one-pixel target JE_SFCodes
// reads: the dominant axis inside the 2:1 cone, both axes (a neutral tick) outside it.
void SF_twiddleTarget(int px, int py, int dx, int dy, int *out_x, int *out_y);
// The wire form of that intent (RB_MOVE_* in rollback.h) and the direction rebuilt from it.
Uint16 rb_move_bits(int dx, int dy);
void rb_move_dir(Uint16 bits, int *out_dx, int *out_dy);

Sint64 weapon_upgrade_cost(Sint64 base_cost, unsigned int power);
Sint64 JE_getCost(JE_byte itemType, JE_word itemNum);
Sint64 JE_getValue(JE_byte itemType, JE_word itemNum);
Sint64 JE_totalScore(const Player *);

void JE_drawPortConfigButtons(void);
void JE_outCharGlow(JE_word x, JE_word y, const char *s);

void JE_playCredits(void);
void JE_endLevelAni(void);
void JE_drawCube(SDL_Surface * screen, JE_word x, JE_word y, JE_byte filter, JE_byte brightness);
void JE_handleChat(void);
bool str_pop_int(char *str, int *val);
// net2p pins it to the 2-player page for the online host and returns the loaded slot;
// saving turns it into a save menu (returns 0; the saving happens inside).
int JE_loadScreen(bool net2p, bool saving);
void JE_saveTransferUpload(void);
void JE_saveTransferDownload(void);
/* Which sessions may load a given record. The save pages show an incompatible slot dimmed and
 * unselectable rather than hiding it. */
bool save_type_compatible(const JE_SaveFileType *rec, JE_byte slot, bool net2p);
bool save_custom_locked(const JE_SaveFileType *rec);
void JE_operation(JE_byte slot);
void JE_inGameDisplays(void);
// Debug perf readout, drawn onto the finished frame by the present loop so nothing overdraws it.
void JE_drawPerfOverlay(SDL_Surface *dst, int scale);

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
// Repaint the ready light for one displayed frame: the meter at `alpha` between the previous and
// current tick's fill, at `scale` into the supersampled playfield, over what the residual re-applied.
void hud_special_light_present(SDL_Surface *dst, int scale, float alpha);
void JE_mainKeyboardInput(void);
void JE_pauseGame(void);

void JE_playerMovement(Player *this_player, JE_byte inputDevice, JE_byte playerNum, JE_word shipGr, Sprite2_array *shipGrPtr_, JE_word *mouseX, JE_word *mouseY);
void JE_mainGamePlayerFunctions(void);

// Pool slots of this tick's linked-Dragonwing aim markers (-1 = none); the
// shot draw maps them to stable render-list ids so the indicator interpolates.
extern int link_marker_slot[3];
const char *JE_getName(JE_byte pnum);
void JE_playerScoreLabel(JE_byte pnum, char *out, size_t outSize);

void JE_playerCollide(Player *this_player, JE_byte playerNum);

#endif /* MAININT_H */
