/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback netcode for the online Destruct minigame.  The battle simulation is snapshotted as one
 * opaque blob (units, walls, shots, explosions, and the destructible terrain buffer), so a
 * mispredicted peer input is corrected by restoring a frame and replaying it.  Leaving a round or
 * the session requires input both machines have confirmed.
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
#ifndef DESTRUCT_ROLLBACK_H
#define DESTRUCT_ROLLBACK_H

#include "opentyr.h"

#include "SDL.h"
#include <stdio.h>
#include <stddef.h>

/* Control bits, carried by every input record. Both are irreversible transitions, so the driver
 * acts on them only once both machines' record for the frame has arrived. */
#define DRB_CTRL_QUIT   0x01
#define DRB_CTRL_NEWMAP 0x04

/* True while the current Destruct session runs rollback instead of the delay-based lockstep. */
bool drb_active(void);

/* Arm the module for a session. */
void drb_session_begin(size_t stateBytes, void (*save)(void *dst), void (*restore)(const void *src),
                       Uint32 (*hash)(void), Uint8 heldActions);
void drb_session_end(void);

/* Start a fresh timeline (frame 1, empty histories, next epoch).  Each round runs one. */
void drb_round_reset(void);

/* Frame being simulated; 1-based, 0 before the round's first tick. */
Uint32 drb_frame(void);

/* Re-simulation suppresses input, sound, and delay; silent passes also skip presentation. */
bool drb_resim(void);
bool drb_resim_silent(void);

/* Tick hooks: snapshot, record local input, then fetch the simulated action pair. */
void drb_frame_begin(void);
void drb_record_local(Uint8 actions, Uint8 controls);
void drb_frame_actions(Uint8 *local, Uint8 *peer);

typedef enum
{
	DRB_STEP_PRESENT,   /* the frame stands: present it and move on      */
	DRB_STEP_RESIM,     /* correction pending: run the tick body again   */
	DRB_STEP_NEWMAP,    /* confirmed round end: generate a fresh map     */
	DRB_STEP_QUIT,      /* confirmed session end                         */
}
DrbStep;

/* End-of-tick driver: publish this frame's input, ingest the peer's, and either correct the
 * timeline or let the frame stand. */
DrbStep drb_driver(bool roundOver);

/* Offline snapshot self-test. Simulate each scripted tick twice, restoring its snapshot before the
 * second pass, and compare the complete resulting state. */
void drb_selftest_arm(size_t stateBytes, void (*save)(void *dst), void (*restore)(const void *src),
                      unsigned long ticks);
void drb_selftest_disarm(void);
bool drb_selftest_active(void);

/* Record the frame's scripted input for both sides.  Called where a live pass would sample keys. */
void drb_selftest_feed(Uint8 localActions, Uint8 peerActions);

/* Driver-site hook; true means the caller re-runs the tick from this frame's snapshot. */
bool drb_selftest_tick(void);

unsigned long drb_selftest_ticks_run(void);
unsigned long drb_selftest_failures(void);

/* Wire-transfer probe, run once during the self-test on live battle state: the recovery's own
 * serialization taken end to end (snapshot, compress, expand, compare). */
bool drb_selftest_resync_ok(void);
void drb_selftest_resync_bytes(size_t *rawBytes, size_t *compressedBytes);

#ifdef WITH_NETWORK
/* Called from network_check() for inbound PACKET_DESTRUCT_INPUT datagrams. */
void drb_handle_packet(const Uint8 *data, int len);

/* Append this module's live state to the crash log's Network section. */
void drb_write_diagnostics(FILE *f);
#endif

#endif /* DESTRUCT_ROLLBACK_H */
