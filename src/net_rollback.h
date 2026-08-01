/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback netcode.  The local ship answers to live input on the same tick --
 * exactly like single player -- while the peer's input is predicted and, when
 * the real packets arrive and disagree, the simulation restores a snapshot and
 * silently re-simulates the intervening ticks with the corrected inputs.
 *
 * Wire model: each tick both machines exchange their player's per-frame input
 * tuple (absolute post-movement position + buttons + request bits), with the
 * last up-to-16 frames repeated in every packet so ordinary UDP loss needs no
 * retransmission protocol.  Only confirmed frames (both inputs known) are
 * allowed to trigger irreversible transitions (level end), and a per-frame
 * desync canary extends the lockstep hash check to exact frame alignment.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef NET_ROLLBACK_H
#define NET_ROLLBACK_H

#include "opentyr.h"
#include "rollback.h"

#include <stdio.h>

/* Config default; the host's value decides for the session (settings block). */
extern bool net_rollback;

/* True while a network game is running in rollback mode (resolved from the
 * host's settings at connect).  All rollback code paths key off this, so
 * lockstep behaves exactly as before when it is false. */
bool nrb_active(void);
void nrb_set_session_mode(bool enabled);
bool nrb_session_mode(void);

/* The host's smooth-motion (VT) choice, adopted for the session: it selects
 * which ship-physics tail the SIMULATION runs, so it must match on both
 * machines (JE_playerMovement's vt_sim gate). */
void nrb_set_session_vt(bool enabled);
bool nrb_session_vt(void);

/* Desync recovery (config default; the host's value binds the session).  When
 * on, a canary mismatch is repaired in place: the host streams its whole
 * registered sim state (PACKET_RESYNC) and the joiner adopts it, then both
 * restart the input timeline at a fresh epoch -- one visible hitch instead of
 * a divergent rest-of-level.  Rollback sessions between same-build peers only;
 * anything else refuses the stream and falls back to today's behaviour. */
extern bool net_desync_recovery;
void nrb_set_session_recovery(bool enabled);
bool nrb_session_recovery(void);

/* Sim frame currently being simulated (1-based; 0 = before the first tick). */
Uint32 nrb_frame(void);

/* Live netplay health, for the perf overlay.  `predict` is how many frames ahead
 * of the peer's newest arrived input we are running (the depth every remote-ship
 * prediction is extrapolated over -- the number that decides how far the peer's
 * ship can be flung before a correction lands).  `depth` is the deepest rollback
 * so far this level, `rate` rollbacks per 100 frames, `desyncs` the canary's
 * mismatch count. */
void nrb_stats(Uint32 *predict, Uint32 *depth, Uint32 *rate, Uint32 *desyncs);

void nrb_level_reset(void);

/* Loop-top hook: snapshot this frame, then apply the previous frame's
 * sim-affecting request bits (skip level, nort ship).  Runs on normal AND
 * re-simulation passes -- the request application is part of the frame. */
void nrb_frame_begin(void);

/* JE_playerMovement hooks. */
void nrb_record_local(const RbInput *in);
void nrb_get_local(Uint32 frame, RbInput *out);
/* Truth if the peer's input for `frame` has arrived, else a prediction; either
 * way the returned tuple is recorded as what the sim used, so misprediction
 * detection compares against exactly this. */
void nrb_get_remote(Uint32 frame, RbInput *out);

/* Driver verdict for the end-of-tick network site. */
typedef enum
{
	NRB_STEP_PRESENT,   /* fall through: filtration, render finalize, present */
	NRB_STEP_RESIM,     /* goto level_loop: re-simulate (silently or finally) */
}
NrbStep;

NrbStep nrb_driver(void);

#ifdef WITH_NETWORK
/* Called from network_check() for inbound PACKET_INPUT datagrams. */
void nrb_handle_packet(const Uint8 *data, int len);

/* Append the rollback module's live state to the crash log's Network section
 * (network_write_diagnostics calls this when the session runs rollback). */
void nrb_write_diagnostics(FILE *f);
#endif

#endif /* NET_ROLLBACK_H */
