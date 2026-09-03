/* Record 35 Hz playfield draws and replay them at interpolated display positions. */
#ifndef RENDER_LIST_H
#define RENDER_LIST_H

#include "opentyr.h"
#include "sprite.h"

#include "SDL.h"

#include <stdbool.h>

typedef enum
{
	RC_SPRITE2 = 0,
	RC_SPRITE2_CLIP,
	RC_SPRITE2_BLEND,
	RC_SPRITE2_DARKEN,
	RC_SPRITE2_SOLID,   // silhouette in one flat colour (outline pass); colour in `filter`
	RC_SPRITE2_FILTER,
	RC_SPRITE2_FILTER_CLIP,
	RC_SPRITE2_FILTER_BRIGHT,  // recoloured with its shade lifted; bank and lift in `filter`
	RC_SPRITE2_FILTER_BRIGHT_CLIP,
	RC_SPRITE2_BLEND_FILTER,  // recoloured and blended in one pass; bank and lift in `filter`
	// Partial opacity in filter's low nibble; DYE forms use its high nibble as the bank.
	RC_SPRITE2_ALPHA,
	RC_SPRITE2_ALPHA_CLIP,
	RC_SPRITE2_ALPHA_DYE,
	RC_SPRITE2_ALPHA_DYE_CLIP,
	RC_SPRITE,
	RC_SPRITE_BLEND,
	RC_SPRITE_HV,
	RC_SPRITE_HV_BLEND,
	RC_SPRITE_HV_UNSAFE,
	RC_SPRITE_DARK,
	RC_SPRITE_ALPHA,    // table sprite at partial opacity; bank in `hue`, opacity in `value`
	RC_BG_ROW,
	RC_BG_ROW_BLEND,
	RC_STAR,
	RC_FILTER_SCREEN,   // full-screen colour filter (JE_filterScreenApply)
	RC_ICED_BLUR,       // smoothie feedback filters: dst = filter(dst, src), src = the
	RC_LAVA_FILTER,     // buffer named by `surface`. These read the previous frame's
	RC_WATER_FILTER,    // main buffer (trails/plasma), so it persists across frames.
	RC_BLUR,            // like RC_ICED_BLUR but preserves the source pixel hue
	RC_SUPERPIXEL,      // explosion spark (JE_drawSP): 5-pixel additive blend, foreground
	RC_HP_BAR,          // enemy health bar; interpolates with its enemy (same id matching)
} RenderCmdKind;

typedef struct
{
	Uint8 kind;
	int x, y;

	// 0 = main playfield, 1 = feedback background scratch.
	Uint8 surface;

	// Cross-frame identity and tick displacement. Interpolated IDs replay backward from (x,y);
	// extrapolated IDs use their recorded velocity and acceleration to replay forward.
	int id;
	int dx, dy;

	// Per-tick acceleration (shots only; 0 for everything else).
	int acc_x, acc_y;

	// sprite2 family
	Sprite2_array sheet;
	unsigned int index;

	// sprite (table) family
	unsigned int table;

	// Extra Parallax mirror width and map[0] column; width 0 disables it.
	Uint8 **map;
	Sint8 bg_mirror_w;
	Sint8 bg_col0;

	// star: column (constant), float row, and this tick's row motion (for interp)
	int star_x;
	float star_y;
	float star_dy;
	Uint8 star_color;

	// Self-contained superpixel motion, brightness, and colour.
	int sp_dx, sp_dy;
	Uint8 sp_z, sp_color, sp_bright;

	// enemy health bar (RC_HP_BAR): length along the fill axis, filled-pixel count, fill colour.
	int bar_w, bar_fill;
	Uint8 bar_col;
	Uint8 bar_vertical;
	Uint8 bar_opacity;
	Uint8 bar_groove;

	// full-screen filter: colour bank + brightness, plus this tick's brightness
	// motion (cur - prev) so the flash/fade ramp interpolates across frames.
	int filt_col;
	int filt_bright;
	int filt_dbright;

	// modifiers
	Uint8 hue;
	Sint8 value;
	bool black;
	Uint8 filter;

	// Tracking-shot attachment: bit 0 X, bit 1 Y, bit 2 player; 0 means detached.
	Uint8 ship_attach;

	// Current and previous sub-pixel remainders discarded by simulation rounding.
	float sub_x, sub_y;
	float sub_dx, sub_dy;

	// Horizontal correction for an entity anchored to a background layer.
	// Finalize normalizes par_frac to the layer's recorded anchor. Layer 0 is unbound.
	float par_frac, par_frac_dx;
	float par_anchor;
	Uint8 par_layer;

	// Vertical layer phase and entity motion; layer 0 is unbound.
	int par_ybase;
	float par_yfrac;
	int par_yown100;
	Uint8 par_ylayer;
}
RenderCmd;

// When true, the leaf blit functions append a command to the active list.
// Replay turns this off so re-issued blits are not recorded again.
extern bool render_list_recording;

// Stable entity ID for cross-frame matching; 0 is static and never interpolated.
extern int rl_current_id;

// Per-axis ship attachment for the next recorded command(s) (see ship_attach in
// RenderCmd). Shots set this around their blit; 0 otherwise.
extern int rl_shot_attach;

// Parallax fraction stamped onto subsequent commands; 0 otherwise.
extern float rl_current_par_frac;

// Background layer and absolute X anchor; layer 0 is unbound.
extern int rl_current_par_layer;
extern float rl_current_par_anchor;

// Vertical layer, whole-pixel correction, and fractional phase for subsequent commands.
extern float rl_current_par_yfrac;
extern int rl_current_par_ybase, rl_current_par_ylayer;

// Per-tick velocity stamped onto subsequent commands for extrapolation.
extern int rl_current_vel_x, rl_current_vel_y;

// Per-tick acceleration stamped onto subsequent commands for extrapolation.
extern int rl_current_acc_x, rl_current_acc_y;

// Sub-pixel remainder stamped onto subsequent commands; magnitude is at most 0.5 px.
extern float rl_current_sub_x, rl_current_sub_y;

// Identity ranges must not overlap and must stay below RL_ID_MAX.
enum
{
	RL_ID_FILTER = 8,        // full-screen colour filter (one per tick; brightness interpolates)
	RL_ID_BG_BASE = 16,      // + layer (1..3)
	RL_ID_ENEMY_BASE = 2000, // + slot
	RL_ID_ENEMYBAR_BASE = 2500, // + slot (enemy health bar; interpolates with its enemy)
	RL_ID_PSHOT_BASE = 3000, // + slot (0 .. MAX_PWEAPON-1; reaches ~10999 at MAX_PWEAPON = 8000)
	RL_ID_ESHOT_BASE = 12000, // + slot (0 .. ENEMY_SHOT_MAX-1)
	RL_ID_EXPL_BASE = 13000, // + slot (0 .. MAX_EXPLOSIONS-1); also the upper bound of the "shot" id range
	RL_ID_SHIP_BASE = 14000, // + player
	// Banking trim needs a separate ID, but stays in the ship override range.
	RL_ID_SHIP_TRIM_BASE = 14002, // + player
	// Variable command count; kept in the ship override range for interpolation.
	RL_ID_SHIP_BAR_BASE = 14006,  // + player
	RL_ID_SIDEKICK_BASE = 15000, // + player*2 + slot
	RL_ID_LINKGUN_BASE = 15010,  // + 0..2: linked-Dragonwing turret aim markers.  The three
	                             // Marker shots need stable IDs because their pool slots drift.
	RL_ID_MAX = 16384,
};

// Begin/finish recording the current tick's playfield draws.
void rl_begin_record(void);
void rl_end_record(void);
// Abandon a recording mid-tick (rollback re-simulation): discard the partial
// list and keep the last complete frame as the interpolation baseline.
void rl_abort_record(void);
void rl_deinit(void);

// Number of commands captured for the current frame.
size_t rl_count(void);

// Match the just-recorded frame against the previous one and compute each
// command's per-tick motion (dx,dy). Call once after rl_end_record.
void rl_finalize(void);

// Re-draw every captured command into dst at its recorded position (alpha=1).
void rl_replay(SDL_Surface *dst);

// Smoothie levels replay feedback backgrounds and foregrounds separately. scale=1 is the
// classic path; larger values use the supersampled grid.
void rl_replay_bg(SDL_Surface *dst, float alpha, int scale, bool split);
void rl_replay_bg_tail(SDL_Surface *dst, float alpha, int scale);
// bg_scale/bg_alpha are the filtered background's actual spatial and temporal phase. split tells
// bound entities that layers from the high-resolution tail retain ordinary display phase.
void rl_replay_fg(SDL_Surface *dst, float alpha, int scale,
                  int bg_scale, float bg_alpha, bool split);

// Replay at (x,y) - (dx,dy)*(1-alpha), then apply the residual.
// Feedback mode preserves the destination for smoothie trails.
void rl_replay_interp(SDL_Surface *dst, float alpha, bool feedback, int scale);

// Mark an opaque overlay for full residual capture; diff-only capture can leave
// holes after its background moves. Coordinates are relative to game_screen.
void rl_mark_overlay_rect(int x, int y, int w, int h);

// Capture non-blit pixels missing from a replay into a same-size 8-bit scratch surface.
void rl_capture_residual(SDL_Surface *reference, SDL_Surface *scratch);

// Capture residual from a before/after diff of the authoritative frame.
void rl_capture_residual_delta(SDL_Surface *before, SDL_Surface *after);

// Draw a ship at its render-rate offset instead of its interpolated tick position.
// Sidekicks retain their own interpolation.
void rl_set_ship_override(int player, float dx, float dy);
void rl_clear_ship_override(void);
float rl_get_ship_override_dx(int player);
float rl_get_ship_override_dy(int player);

// Authoritative ship velocity used to recover relative motion from attached shots.
void rl_set_ship_vel(int player, int vx, int vy);

// Completeness gate: clear scratch, replay the captured list into it, and return the
// number of bytes differing from reference (0 = the list fully reproduces the frame).
size_t rl_replay_and_compare(SDL_Surface *scratch, SDL_Surface *reference);

// Recorder helpers, called from the leaf blit functions when recording.
void rl_rec_sprite2(int x, int y, Sprite2_array sheet, unsigned int index, RenderCmdKind kind);
void rl_rec_sprite2_filter(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter, bool clip);
void rl_rec_sprite2_filter_bright(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter, bool clip);
void rl_rec_sprite2_blend_filter(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter);
void rl_rec_sprite2_alpha(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter, bool dye, bool clip);
void rl_rec_sprite2_solid(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 color);
void rl_rec_sprite(int x, int y, unsigned int table, unsigned int index, RenderCmdKind kind, Uint8 hue, Sint8 value, bool black);
void rl_rec_bg_row(int x, int y, Uint8 **map, bool blend, int mirror_w, int col0);
void rl_rec_star(int x, float y, float dy, Uint8 color);
void rl_rec_superpixel(int x, int y, int dx, int dy, Uint8 z, Uint8 color, Uint8 bright);
void rl_rec_hp_bar(int x, int y, int along, int fill, Uint8 col, bool vertical, Uint8 opacity, Uint8 groove);
// Draw an enemy health bar (shared by the authoritative tick draw and the
// interpolated replay so they produce identical pixels).
void rl_draw_hp_bar(SDL_Surface *dst, int x, int y, int along, int fill, Uint8 col, bool vertical, Uint8 opacity, Uint8 groove);
void rl_rec_filter_screen(int col, int brightness);
void rl_rec_smoothie_filter(RenderCmdKind kind);  // RC_ICED_BLUR / RC_LAVA_FILTER / RC_WATER_FILTER / RC_BLUR

// Plotted value of one superpixel tap over background `bg`: the z-driven shade, lifted `bright`
// steps and clamped inside `color`'s own bank so a bright spark can't bleed into the next bank.
static inline Uint8 rl_superpixel_value(Uint8 bg, Uint8 z, Uint8 color, Uint8 bright)
{
	unsigned int shade = (((bg & 0x0f) + z) >> 1) + bright;
	if (shade > 0x0f)
		shade = 0x0f;
	return (Uint8)shade + color;
}

#endif /* RENDER_LIST_H */
