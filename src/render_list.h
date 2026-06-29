/*
 * OpenTyrian2000: high-refresh render-interpolation groundwork.
 *
 * The simulation runs at a fixed ~35Hz tick. To present smoothly at an
 * arbitrary higher display rate (60/120/144/180/uncapped) WITHOUT speeding the
 * game up, we record every playfield draw the tick performs into an ordered
 * "render list", then re-draw that list — at interpolated positions — once per
 * displayed frame. Re-running the game's draw code is impossible (it mutates
 * state, spawns explosions, queues sound, and consumes the RNG), so the render
 * list is the only safe way to reproduce a frame between ticks.
 *
 * Stage 1 goal: record the list and prove a replay reproduces the frame
 * pixel-for-pixel. Interpolation (prev/cur positions) builds on top later.
 */
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
	RC_SPRITE2_FILTER,
	RC_SPRITE2_FILTER_CLIP,
	RC_SPRITE,
	RC_SPRITE_BLEND,
	RC_SPRITE_HV,
	RC_SPRITE_HV_BLEND,
	RC_SPRITE_HV_UNSAFE,
	RC_SPRITE_DARK,
	RC_BG_ROW,
	RC_BG_ROW_BLEND,
	RC_STAR,
	RC_FILTER_SCREEN,   // full-screen colour filter (JE_filterScreen)
	RC_ICED_BLUR,       // smoothie feedback filters: dst = filter(dst, src) where
	RC_LAVA_FILTER,     // src is the buffer named by `surface`. These read the
	RC_WATER_FILTER,    // previous frame's main buffer (trails/plasma), so the
	                    // main buffer persists across interpolated frames.
} RenderCmdKind;

typedef struct
{
	Uint8 kind;
	int x, y;

	// Target/source buffer for smoothie levels: 0 = main playfield buffer,
	// 1 = the background scratch (VGAScreen2). Backgrounds draw to the scratch,
	// then a filter blends it into the main buffer; entities draw to the main
	// buffer. Always 0 on non-smoothie levels.
	Uint8 surface;

	// Identity for cross-frame matching, and the per-command screen motion this
	// tick (cur - prev). Interpolated replay draws at (x,y) - (dx,dy)*(1-alpha).
	int id;
	int dx, dy;

	// sprite2 family
	Sprite2_array sheet;
	unsigned int index;

	// sprite (table) family
	unsigned int table;

	// background row
	Uint8 **map;

	// star: column (constant), float row, and this tick's row motion (for interp)
	int star_x;
	float star_y;
	float star_dy;
	Uint8 star_color;

	// full-screen filter
	int filt_col;
	int filt_bright;

	// modifiers
	Uint8 hue;
	Sint8 value;
	bool black;
	Uint8 filter;

	// Ship-attachment for tracking shots, per axis (bit0 = X tracks the ship,
	// bit1 = Y tracks the ship, bit2 = player index). Such an axis is drawn at
	// the ship's render-rate position instead of being interpolated, so e.g. the
	// laser/main-pulse base stays on the gun during strafes. 0 = not attached.
	Uint8 ship_attach;
}
RenderCmd;

// When true, the leaf blit functions append a command to the active list.
// Replay turns this off so re-issued blits are not recorded again.
extern bool render_list_recording;

// The id stamped onto subsequent recorded commands. The game sets this before
// drawing a logical entity (enemy slot, ship, background layer, ...) so the
// same entity can be matched across frames. 0 = untagged/static (never
// interpolated — drawn at its recorded position).
extern int rl_current_id;

// Per-axis ship attachment for the next recorded command(s) (see ship_attach in
// RenderCmd). Shots set this around their blit; 0 otherwise.
extern int rl_shot_attach;

// Identity ranges for rl_current_id (kept < RL_ID_MAX).
enum
{
	RL_ID_BG_BASE = 16,      // + layer (1..3)
	RL_ID_ENEMY_BASE = 2000, // + slot
	RL_ID_PSHOT_BASE = 3000, // + slot
	RL_ID_ESHOT_BASE = 4000, // + slot
	RL_ID_EXPL_BASE = 5000,  // + slot
	RL_ID_SHIP_BASE = 6000,  // + player
	RL_ID_SIDEKICK_BASE = 7000, // + player*2 + slot
	RL_ID_MAX = 8192,
};

// Begin/finish recording the current tick's playfield draws.
void rl_begin_record(void);
void rl_end_record(void);

// Number of commands captured for the current frame.
size_t rl_count(void);

// Match the just-recorded frame against the previous one and compute each
// command's per-tick motion (dx,dy). Call once after rl_end_record.
void rl_finalize(void);

// Re-draw every captured command into dst at its recorded position (alpha=1).
void rl_replay(SDL_Surface *dst);

// Re-draw every captured command into dst at an interpolated position:
// (x,y) - (dx,dy)*(1-alpha). alpha in [0,1]; 1 reproduces the exact frame.
// Also re-applies the captured residual (non-blit playfield pixels).
//   feedback=false: dst is cleared first (normal levels).
//   feedback=true:  dst is NOT cleared so the smoothie filters' trails persist
//                   across frames; caller must seed dst from the tick frame. `dt`
//                   is this frame's fraction of a tick (frame_time/tick_period),
//                   used to keep the feedback trail length frame-rate-independent.
void rl_replay_interp(SDL_Surface *dst, float alpha, bool feedback, float dt);

// Capture the residual: pixels in `reference` (the authoritative frame) that a
// blit-only replay doesn't reproduce — i.e. non-blit draws like superpixels and
// boss-health bars. `scratch` is a same-size 8-bit work surface. Call after the
// tick's drawing is complete, before interpolated frames are presented.
void rl_capture_residual(SDL_Surface *reference, SDL_Surface *scratch);

// Capture residual from a before/after diff of the authoritative frame: the
// pixels that changed between the two snapshots. Used on feedback (smoothie)
// levels to grab the overlays drawn after the per-pixel filters (boss bar,
// in-game displays) without flagging the whole filtered playfield, so the
// overlays can be re-applied unfiltered on top of every interpolated frame.
void rl_capture_residual_delta(SDL_Surface *before, SDL_Surface *after);

// Drop captured residual (so rl_replay_interp applies none). For callers whose
// frame is fully reproduced by recorded blits.
void rl_clear_residual(void);

// Ship override: during interpolated replay, the hull/shadow/charge of player
// `player` (0 or 1) are drawn at their recorded position PLUS (dx,dy) instead of
// being time-interpolated, driving each ship at the render rate (Phase 4).
// (Sidekicks are excluded by id range and interpolate by their own motion.)
void rl_set_ship_override(int player, int dx, int dy);
void rl_clear_ship_override(void);

// Stage 1 completeness gate: clear scratch, replay the captured list into it,
// and return the number of bytes that differ from reference. Zero means the
// render list fully reproduces the frame.
size_t rl_replay_and_compare(SDL_Surface *scratch, SDL_Surface *reference);

// Recorder helpers, called from the leaf blit functions when recording.
void rl_rec_sprite2(int x, int y, Sprite2_array sheet, unsigned int index, RenderCmdKind kind);
void rl_rec_sprite2_filter(int x, int y, Sprite2_array sheet, unsigned int index, Uint8 filter, bool clip);
void rl_rec_sprite(int x, int y, unsigned int table, unsigned int index, RenderCmdKind kind, Uint8 hue, Sint8 value, bool black);
void rl_rec_bg_row(int x, int y, Uint8 **map, bool blend);
void rl_rec_star(int x, float y, float dy, Uint8 color);
void rl_rec_filter_screen(int col, int brightness);
void rl_rec_smoothie_filter(RenderCmdKind kind);  // RC_ICED_BLUR / RC_LAVA_FILTER / RC_WATER_FILTER

#endif /* RENDER_LIST_H */
