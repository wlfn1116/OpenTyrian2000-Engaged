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
#include "vt_ship.h"

#include "config.h"
#include "demo.h"
#include "endless.h"
#include "game_state.h"
#include "joystick.h"
#include "keyboard.h"
#include "mouse.h"
#include "net_rollback.h"
#include "network.h"
#include "opentyr.h"
#include "player.h"
#include "player_ship.h"
#include "render_list.h"
#include "rollback.h"
#include "timing.h"
#include "video.h"

#include "SDL.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Render-rate ship movement: the displayed ship extrapolates its last per-tick velocity each
// frame and is reconciled to the 35Hz simulation through the ship override.
static int ship_tick_x[2], ship_tick_y[2];   // ship position captured at the last tick
static int ship_vel_x[2], ship_vel_y[2];       // per-tick movement (cur - prev tick)
static bool ship_pred_have_tick = false;

void vt_ship_pred_reset(void)
{
	ship_pred_have_tick = false;
	memset(ship_vel_x, 0, sizeof(ship_vel_x));
	memset(ship_vel_y, 0, sizeof(ship_vel_y));
	for (int p = 0; p < 2; ++p)
	{
		ship_tick_x[p] = player[p].x;
		ship_tick_y[p] = player[p].y;
	}
}

void vt_ship_pred_tick(void)
{
	const int players = twoPlayerMode ? 2 : 1;
	for (int p = 0; p < players; ++p)
	{
		if (ship_pred_have_tick)
		{
			ship_vel_x[p] = player[p].x - ship_tick_x[p];
			ship_vel_y[p] = player[p].y - ship_tick_y[p];
		}
		ship_tick_x[p] = player[p].x;
		ship_tick_y[p] = player[p].y;
	}
	ship_pred_have_tick = true;
}

void vt_ship_update_override(float alpha)
{
	// Extrapolate actual tick velocity. Rollback peers ease presentation-only corrections; shadows
	// and charge meters share the ship offset, while sidekicks interpolate separately.
	static float  rsm_x[2], rsm_y[2];
	static bool   rsm_valid[2];
	static Uint64 rsm_last_counter;
	float rsm_ease = 1.0f;
	if (nrb_active())
	{
		const Uint64 now = SDL_GetPerformanceCounter();
		if (rsm_last_counter != 0)
		{
			const float dt_ms = (float)(now - rsm_last_counter) * 1000.0f
			                  / (float)SDL_GetPerformanceFrequency();
			// Exponential easing remains frame-rate independent when a frame exceeds the time constant.
			rsm_ease = 1.0f - expf(-dt_ms / 60.0f);
			if (rsm_ease > 1.0f) rsm_ease = 1.0f;
			if (rsm_ease < 0.0f) rsm_ease = 0.0f;
		}
		rsm_last_counter = now;
	}

	const int players = twoPlayerMode ? 2 : 1;
	for (int p = 0; p < players; ++p)
	{
		const bool local  = (uint)(p + 1) == thisPlayerNum;
		const bool docked = (p == 1 && twoPlayerLinked);

		// VT drives the local rollback ship per render frame; docked ships still use the carrier.
		if (nrb_active() && vt_ship_owns() && local && !docked)
			continue;

		// A docked ship shares player 1's presentation offset between ticks.
		if (nrb_active() && docked)
		{
			rl_set_ship_override(p, rl_get_ship_override_dx(0), rl_get_ship_override_dy(0));
			continue;
		}

		int vx = ship_vel_x[p], vy = ship_vel_y[p];
		// A large jump (warp, dragonwing spawn, level start) isn't real velocity;
		// snap rather than fling the ship along a bogus extrapolation.
		const bool snapped = vx > 40 || vx < -40 || vy > 40 || vy < -40;
		if (snapped)
		{
			vx = 0;
			vy = 0;
		}

		if (nrb_active() && !local)
		{
			// Clamp remote extrapolation to plausible ship speed so corrections are not amplified.
			const int RVMAX = 6;
			if (vx >  RVMAX) vx =  RVMAX; else if (vx < -RVMAX) vx = -RVMAX;
			if (vy >  RVMAX) vy =  RVMAX; else if (vy < -RVMAX) vy = -RVMAX;

			const float tgt_x = (float)ship_tick_x[p] + vx * alpha;
			const float tgt_y = (float)ship_tick_y[p] + vy * alpha;

			// Ease remote corrections, but snap across jumps larger than any ordinary misprediction.
			if (!rsm_valid[p] ||
			    tgt_x - rsm_x[p] > 100.0f || tgt_x - rsm_x[p] < -100.0f ||
			    tgt_y - rsm_y[p] > 100.0f || tgt_y - rsm_y[p] < -100.0f)
			{
				rsm_x[p] = tgt_x;
				rsm_y[p] = tgt_y;
				rsm_valid[p] = true;
			}
			else
			{
				rsm_x[p] += (tgt_x - rsm_x[p]) * rsm_ease;
				rsm_y[p] += (tgt_y - rsm_y[p]) * rsm_ease;
			}

			rl_set_ship_override(p, rsm_x[p] - (float)ship_tick_x[p],
			                        rsm_y[p] - (float)ship_tick_y[p]);
			continue;
		}
		if (endLevel && levelEnd > 0 && !snapped)
			--vy;

		// Float offset: the replay rounds it at the render scale, so a supersampled
		// ship glides on the sub-pixel grid instead of stepping whole pixels.
		rl_set_ship_override(p, vx * alpha, vy * alpha);
	}
}

// Variable-timestep (VT) player ship: the ship alone is simulated at the render rate with
// real dt while the world stays on the fixed 35Hz tick.
bool vt_ship = true;

#define VT_ACCEL      1.0f  // velocity gained per tick while a direction is held (orig accelXC: +1/tick)
#define VT_DIRECT     1.0f  // immediate px/tick while a direction is held (orig CURRENT_KEY_SPEED; kills momentum lag)
#define VT_FRICTION_X 1.0f  // velocity bled per tick with no x input (orig ~1/tick)
#define VT_FRICTION_Y 0.5f  // orig y friction fires every 2nd tick => half rate
#define VT_VMAX       4.0f  // velocity clamp/axis (orig: vel clamp 4); +VT_DIRECT gives ~5 total like the original
// VT_MOUSE_SENS lives in vt_ship.h: the classic per-tick mouse path shares it

static float vt_x[2], vt_y[2], vt_vx[2], vt_vy[2];
static int vt_wrote_vx[2], vt_wrote_vy[2];  // last velocity VT wrote to player[]
static bool vt_seeded[2] = { false, false };

/* Lockstep does not predict the local sprite because shots, sidekicks, and collisions remain at the
 * simulation position. Lower network_delay instead. */

// Raw (un-inverted) mouse motion accumulated by vt_ship_step since the last twiddle
// read; consumed by vt_ship_twiddle_dir (rationale there).
static float vt_twiddle_mx[2], vt_twiddle_my[2];

bool vt_ship_owns(void)
{
	return vt_ship
	    && smoothMotion                            // user's Graphics-menu toggle
	    && smoothScroll != 0 && frameCountMax > 0  // the render-rate present loop must run
	    // Demos replay fixed-tick input and cannot use render-rate ship movement.
	    && !play_demo && !record_demo
	    && !endLevel;
}

static void vt_seed_player(int p)
{
	vt_x[p] = (float)player[p].x;
	vt_y[p] = (float)player[p].y;
	vt_vx[p] = (float)player[p].x_velocity;
	vt_vy[p] = (float)player[p].y_velocity;
	vt_wrote_vx[p] = player[p].x_velocity;
	vt_wrote_vy[p] = player[p].y_velocity;
	vt_seeded[p] = true;
}

static float vt_friction(float v, float f)
{
	if (v > 0.0f) { v -= f; if (v < 0.0f) v = 0.0f; }
	else if (v < 0.0f) { v += f; if (v > 0.0f) v = 0.0f; }
	return v;
}

static void vt_ship_step_player(int p, float dt)
{
	if (!player[p].is_alive)
	{
		// Reset only this dead ship's offset; the other player may still use VT.
		vt_seeded[p] = false;
		rl_set_ship_override(p, 0.0f, 0.0f);
		return;
	}

	// Reseed after local discontinuities. Netplay seeds once because its simulation position lags VT.
	if (!vt_seeded[p]
	    || (!isNetworkGame
	        && (abs(player[p].x - (int)lrintf(vt_x[p])) > 8
	         || abs(player[p].y - (int)lrintf(vt_y[p])) > 8)))
	{
		vt_seed_player(p);
	}

	// Directional input feeds momentum; relative mouse motion applies directly.
	const bool invert_y = smoothies[9 - 1];
	float ix = 0.0f, iy = 0.0f;   // directional input -> momentum
	float mdx = 0.0f, mdy = 0.0f; // mouse relative motion -> direct position

	// Per-player device selection applies only to local two-player. Other modes read any device;
	// 0 = any, 1 = keyboard, 2 = mouse, 3+ = one joystick.
	const JE_byte device = (twoPlayerMode && !galagaMode && !isNetworkGame)
	                     ? inputDevice[p]
	                     : 0;
	const bool use_keyboard = (device == 0 || device == 1);
	const bool use_mouse    = (device == 0 || device == 2);
	const bool use_joystick = (device == 0 || device >= 3);
	const int  joy_first    = (device >= 3) ? device - 3 : 0;
	const int  joy_last     = (device >= 3) ? device - 3 : joysticks - 1;

	if (use_keyboard)
	{
		if (keysactive[keySettings[KEY_SETTING_UP]])    iy -= 1.0f;
		if (keysactive[keySettings[KEY_SETTING_DOWN]])  iy += 1.0f;
		if (keysactive[keySettings[KEY_SETTING_LEFT]])  ix -= 1.0f;
		if (keysactive[keySettings[KEY_SETTING_RIGHT]]) ix += 1.0f;
	}

	if (use_joystick)
	{
		for (int j = joy_first; j <= joy_last && j < joysticks; ++j)
		{
			poll_joystick(j);

			// Preserve edge-triggered actions consumed by the display-rate poll.
			if (joystick[j].action_pressed[4]) ingamemenu_pressed = true;  // "menu"
			if (joystick[j].action_pressed[5])                              // "pause"
			{
				if (joystick_pause_opens_menu(j))
					ingamemenu_pressed = true;
				else
					pause_pressed = true;
			}
			if (joystick[j].action_pressed[1]) changefire_pressed = true;   // "change fire"

			if (joystick[j].analog)
			{
				// Stick deflection -> proportional momentum input (clamped ~[-1,1]).
				float ax = joystick_axis_reduce(j, joystick[j].x) / 32.0f;
				float ay = joystick_axis_reduce(j, joystick[j].y) / 32.0f;
				ix += (ax < -1.0f) ? -1.0f : (ax > 1.0f) ? 1.0f : ax;
				iy += (ay < -1.0f) ? -1.0f : (ay > 1.0f) ? 1.0f : ay;
			}

			// D-pad / digital directions, read in both modes so the d-pad works even when
			// the stick is configured analog. (The combined input is clamped below.)
			if (joystick[j].direction[0]) iy -= 1.0f;  // up
			if (joystick[j].direction[2]) iy += 1.0f;  // down
			if (joystick[j].direction[3]) ix -= 1.0f;  // left
			if (joystick[j].direction[1]) ix += 1.0f;  // right
		}
	}

	if (has_mouse && use_mouse)
	{
		// Float relative motion since our last frame (per-frame sampling would round
		// small/diagonal motion away); returns 0 when relative mode is off.
		float mxr = 0.0f, myr = 0.0f;
		mouseGetRelativeMotionF(&mxr, &myr);
		mdx = mxr * VT_MOUSE_SENS;
		mdy = myr * VT_MOUSE_SENS;

		// Stash the raw mouse direction for the twiddle-code detector, un-inverted;
		// the detector applies the "inverted controls" flip itself, like keyboard.
		vt_twiddle_mx[p] += mxr;
		vt_twiddle_my[p] += myr;
	}

	if (invert_y) { iy = -iy; mdy = -mdy; }

	// Clamp combined directional input (keyboard + analog stick + d-pad) to full
	// deflection so overlapping sources can't push past 1.
	if (ix < -1.0f) ix = -1.0f; else if (ix > 1.0f) ix = 1.0f;
	if (iy < -1.0f) iy = -1.0f; else if (iy > 1.0f) iy = 1.0f;

	// Integrate momentum; dt=1 is one classic tick.
	if (ix != 0.0f)
		vt_vx[p] += ix * VT_ACCEL * dt;
	else
		vt_vx[p] = vt_friction(vt_vx[p], VT_FRICTION_X * dt);

	if (iy != 0.0f)
		vt_vy[p] += iy * VT_ACCEL * dt;
	else
		vt_vy[p] = vt_friction(vt_vy[p], VT_FRICTION_Y * dt);

	if (vt_vx[p] >  VT_VMAX) vt_vx[p] =  VT_VMAX;
	if (vt_vx[p] < -VT_VMAX) vt_vx[p] = -VT_VMAX;
	if (vt_vy[p] >  VT_VMAX) vt_vy[p] =  VT_VMAX;
	if (vt_vy[p] < -VT_VMAX) vt_vy[p] = -VT_VMAX;

	// Combine momentum, immediate directional movement, and mouse/touch delta. Endless movement
	// scaling applies to the complete displacement.
	const float mscale = endlessShipMoveScale((uint)p);
	const float step_x = ((vt_vx[p] + ix * VT_DIRECT) * dt + mdx) * mscale;
	const float step_y = ((vt_vy[p] + iy * VT_DIRECT) * dt + mdy) * mscale;

	// Online this is an unlagged VT position; player[p].x trails by the network delay and cannot be
	// its base.
	vt_x[p] += step_x;
	vt_y[p] += step_y;

	// Apply render-rate-independent Endless gravity; the bounds below handle every heading. Online,
	// folding it into VT motion lets vt_ship_commit_net deliver it exactly once.
	vt_x[p] += endlessShipGravityDriftX((uint)p) * dt;
	vt_y[p] += endlessShipGravityDriftY((uint)p) * dt;

	// Same playfield bounds the sim enforces (player_ship.c). Stop velocity at walls
	// so we don't accumulate phantom momentum while held against an edge.
	if (vt_x[p] > PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN) { vt_x[p] = PLAYFIELD_WIDTH - SHIP_RIGHT_MARGIN; if (vt_vx[p] > 0) vt_vx[p] = 0; }
	if (vt_x[p] < SHIP_LEFT_MARGIN)                    { vt_x[p] = SHIP_LEFT_MARGIN;                    if (vt_vx[p] < 0) vt_vx[p] = 0; }
	if (vt_y[p] > SHIP_BOTTOM_MARGIN)                  { vt_y[p] = SHIP_BOTTOM_MARGIN;                  if (vt_vy[p] > 0) vt_vy[p] = 0; }
	if (vt_y[p] < SHIP_TOP_MARGIN)                     { vt_y[p] = SHIP_TOP_MARGIN;                     if (vt_vy[p] < 0) vt_vy[p] = 0; }

	if (isNetworkGame)
	{
		// Rollback applies local VT at the next tick, so present the live integrator position.
		// Lockstep presents the simulation position so sprite, shots, and hitbox agree.
		if (nrb_active() && (uint)(p + 1) == thisPlayerNum)
			rl_set_ship_override(p, vt_x[p] - (float)ship_tick_x[p], vt_y[p] - (float)ship_tick_y[p]);
		return;
	}

	// Write back so the next 35Hz tick (collisions, firing, homing) sees the
	// current position/velocity.
	player[p].x = (int)lrintf(vt_x[p]);
	player[p].y = (int)lrintf(vt_y[p]);
	player[p].x_velocity = (int)lrintf(vt_vx[p]);
	player[p].y_velocity = (int)lrintf(vt_vy[p]);
	vt_wrote_vx[p] = player[p].x_velocity;
	vt_wrote_vy[p] = player[p].y_velocity;

	// Display the recorded (tick-time) ship sprite at its new continuous position via the override
	// channel the interpolator already understands.
	rl_set_ship_override(p, vt_x[p] - (float)ship_tick_x[p], vt_y[p] - (float)ship_tick_y[p]);
}

// Netplay: publish VT's current (unlagged) position into the ship, so the netcode picks it up
// as this tick's absolute.
void vt_ship_commit_net(int player_index)
{
	if (!vt_ship_owns() || !isNetworkGame)
		return;

	const int p = (player_index <= 0) ? 0 : 1;
	if (!player[p].is_alive || !vt_seeded[p])
		return;  // nothing meaningful to hand over; the simulation keeps the ship where it is

	// Expose the display-rate position for delay-based network input.
	player[p].x = (int)lrintf(vt_x[p]);
	player[p].y = (int)lrintf(vt_y[p]);

	// The wire carries position. Local tilt and banking still need the matching velocity.
	player[p].x_velocity = (int)lrintf(vt_vx[p]);
	player[p].y_velocity = (int)lrintf(vt_vy[p]);
	vt_wrote_vx[p] = player[p].x_velocity;
	vt_wrote_vy[p] = player[p].y_velocity;
}

// The other player in a network game: its ship is placed from the deltas arriving on the wire,
// so VT must not integrate input for it; that input belongs to the machine it is sitting at.
static bool vt_player_is_net_remote(int p)
{
	return isNetworkGame && (uint)(p + 1) != thisPlayerNum;
}

// Docked Dragonwing placement remains owned by simulation until it undocks.
static bool vt_player_is_sim_driven(int p)
{
	return vt_player_is_net_remote(p) || (p == 1 && twoPlayerLinked);
}

void vt_ship_step(float dt)  // dt = this frame's fraction of a 35Hz tick
{
	if (dt <= 0.0f)
		return;

	const int players = twoPlayerMode ? 2 : 1;
	for (int p = 0; p < players; ++p)
	{
		if (vt_player_is_sim_driven(p))
		{
			vt_seeded[p] = false;

			// In netplay VT owns no ship's rendering at all; vt_ship_update_override() runs
			// immediately after this and drives both ships. Leave the channel unchanged.
			if (!isNetworkGame)
			{
				if (p == 1 && twoPlayerLinked)
				{
					// A docked Dragonwing shares player 1's current sub-tick offset.
					rl_set_ship_override(p, rl_get_ship_override_dx(0), rl_get_ship_override_dy(0));
				}
				else
				{
					rl_set_ship_override(p, 0.0f, 0.0f);
				}
			}
			continue;
		}

		vt_ship_step_player(p, dt);
	}
}

// Refresh trailing-sidekick history when VT, rather than the sim tick, moves the ship.
static int hist_prev_x[2] = { -32768, -32768 }, hist_prev_y[2] = { -32768, -32768 };

static void vt_refresh_position_history(int p)
{
	if (player[p].x == hist_prev_x[p] && player[p].y == hist_prev_y[p])
		return;
	hist_prev_x[p] = player[p].x;
	hist_prev_y[p] = player[p].y;

	for (unsigned int i = 1; i < COUNTOF(player[p].old_x); ++i)
	{
		player[p].old_x[i - 1] = player[p].old_x[i];
		player[p].old_y[i - 1] = player[p].old_y[i];
	}
	player[p].old_x[COUNTOF(player[p].old_x) - 1] = player[p].x;
	player[p].old_y[COUNTOF(player[p].old_x) - 1] = player[p].y;
}

static void vt_ship_tick_player(int p)
{
	if (!vt_seeded[p] || !player[p].is_alive)
		return;

	// Reseed display-rate motion after an offline respawn, warp, or link jump.
	if (!isNetworkGame
	    && (abs(player[p].x - (int)lrintf(vt_x[p])) > 8
	     || abs(player[p].y - (int)lrintf(vt_y[p])) > 8))
	{
		vt_seed_player(p);
		return;
	}

	// Fold external velocity impulses (magnet fields, knockback) into VT: they modify
	// player.x_velocity during the tick and JE_playerMovement's integration is skipped.
	vt_vx[p] += (float)(player[p].x_velocity - vt_wrote_vx[p]);
	vt_vy[p] += (float)(player[p].y_velocity - vt_wrote_vy[p]);

	// Online peers apply positions mid-tick, where the normal history update already runs.
	if (!isNetworkGame)
		vt_refresh_position_history(p);
}

void vt_ship_tick(void)  // once per 35Hz tick, before vt_ship_pred_tick()
{
	if (!vt_ship_owns())
		return;

	const int players = twoPlayerMode ? 2 : 1;
	for (int p = 0; p < players; ++p)
	{
		if (vt_player_is_net_remote(p))
			continue;

		if (vt_player_is_sim_driven(p))
			continue;

		vt_ship_tick_player(p);
	}
}

// Per-tick ship movement for shots that track the ship (delta_x_shot_move, e.g. the laser).
void vt_ship_shot_delta(int player_index, int *out_dx, int *out_dy)
{
	const int p = (player_index <= 0) ? 0 : 1;
	if (!ship_pred_have_tick)
	{
		*out_dx = 0;
		*out_dy = 0;
		return;
	}
	*out_dx = player[p].x - ship_tick_x[p];
	*out_dy = player[p].y - ship_tick_y[p];
}

// Hand the twiddle-code detector the mouse direction since the last call as a -1/0/+1 per axis
// (raw / un-inverted), then reset the accumulator.
void vt_ship_twiddle_dir(int player_index, int *out_dx, int *out_dy)
{
	const int p = (player_index <= 0) ? 0 : 1;
	const float deadzone = 1.0f;  // screen px of motion before a direction counts

	const float ax = vt_twiddle_mx[p], ay = vt_twiddle_my[p];
	vt_twiddle_mx[p] = 0.0f;
	vt_twiddle_my[p] = 0.0f;

	*out_dx = 0;
	*out_dy = 0;

	// Resolve the twiddle cone before quantizing display-rate input.
	if (ax > deadzone)       *out_dx =  1;
	else if (ax < -deadzone) *out_dx = -1;
	if (ay > deadzone)       *out_dy =  1;
	else if (ay < -deadzone) *out_dy = -1;

	if (*out_dx != 0 && *out_dy != 0)
	{
		if (fabsf(ax) > 2.0f * fabsf(ay))
			*out_dy = 0;
		else if (fabsf(ay) > 2.0f * fabsf(ax))
			*out_dx = 0;
	}
}

void vt_ship_display_position(int p, float *out_x, float *out_y)
{
	*out_x = (float)ship_tick_x[p] + rl_get_ship_override_dx(p);
	*out_y = (float)ship_tick_y[p] + rl_get_ship_override_dy(p);
}

void vt_ship_publish_velocity(void)
{
	for (int p = 0; p < (twoPlayerMode ? 2 : 1); ++p)
		rl_set_ship_vel(p, ship_vel_x[p], ship_vel_y[p]);
}

void vt_ship_register_rollback(void)
{
	rollback_register("t2.shipTick",         ship_tick_x, sizeof(ship_tick_x));
	rollback_register("t2.shipTickY",        ship_tick_y, sizeof(ship_tick_y));
	/* The latch that arms them. */
	rollback_register("t2.shipPredHave",     &ship_pred_have_tick, sizeof(ship_pred_have_tick));
}
