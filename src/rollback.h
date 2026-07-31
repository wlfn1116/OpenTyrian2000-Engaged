/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback state engine: a registry of every piece of in-level simulation
 * state, a ring of whole-state snapshots keyed by sim frame, and a
 * single-player self-test that proves the registry is complete.
 *
 * The registry is the load-bearing part.  Rollback netcode re-simulates past
 * ticks from a restored snapshot; ANY sim-affecting variable missing from the
 * registry survives the restore and corrupts the replay.  The self-test
 * (snapshot -> tick -> restore -> re-tick -> compare every registered byte)
 * catches exactly that class of omission, offline, with no second machine.
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
#ifndef ROLLBACK_H
#define ROLLBACK_H

#include "opentyr.h"

#include "SDL.h"
#include <stddef.h>

/* Deepest re-simulation permitted, in sim ticks.  Netplay stalls (waits for the
 * peer) rather than predicting past this, so it also bounds snapshot memory. */
#define ROLLBACK_MAX_PREDICT 10
#define ROLLBACK_RING        (ROLLBACK_MAX_PREDICT + 3)

/* --- Re-simulation mode -------------------------------------------------------
 *
 * rollback_resim: the current pass through the level loop is a REPLAY of a tick
 * that already ran once.  Everything that must happen once per real tick is
 * suppressed: sound playback (the queue is drained silently), live input
 * sampling (input comes from the recorded per-frame tuples), SDL event
 * pumping, demo-file writes, network keep-alives.
 *
 * rollback_resim_silent: additionally suppress render capture.  True for every
 * replay pass except the one whose output is presented.
 *
 * Both are false in all single-player and local-2P play unless the self-test
 * is explicitly enabled, so the suppression gates are inert by default.
 */
extern bool rollback_resim;
extern bool rollback_resim_silent;

/* --- State registry ----------------------------------------------------------- */

void rollback_register(const char *name, void *ptr, size_t size);
/* For state that cannot be raw-copied (the RNG holds internal pointers). */
void rollback_register_callback(const char *name, size_t size,
                                void (*save)(void *dst), void (*restore)(const void *src));
/* Runs after every restore, for state that must be re-derived rather than
 * copied (interior pointers like player[].lives). */
void rollback_register_fixup(void (*fn)(void));
/* Registers everything (rollback_state.c + per-file registrars); idempotent. */
void rollback_register_all(void);
size_t rollback_state_size(void);

/* --- Snapshot ring, keyed by sim frame ---------------------------------------- */

void rollback_ring_reset(void);
void rollback_snapshot(Uint32 frame);
bool rollback_have_frame(Uint32 frame);
bool rollback_restore(Uint32 frame);

/* --- Per-frame input tuples ----------------------------------------------------
 *
 * The simulation's only doors for player input.  In netplay the wire carries
 * these; in the self-test they let a replayed tick reproduce the live-sampled
 * tick exactly.  x/y are the ABSOLUTE post-movement ship position -- idempotent
 * on replay, like the lockstep wire format before it.
 */
typedef struct
{
	Sint16 x, y;            /* ship position after the movement routine        */
	Sint16 mouseX, mouseY;  /* tick aim/anchor pair (self-test replay only)    */
	Sint16 sfTx, sfTy;      /* Street-Fighter-code target (self-test only)     */
	Sint16 accelX, accelY;  /* banking/aim acceleration the tick derived       */
	Uint8  difficulty;      /* host's difficultyLevel (host-authoritative)     */
	Uint16 buttons;         /* see RB_BTN_* / RB_REQ_* / RB_EV_* bits          */
	Uint16 linkAngle;       /* linked Dragonwing gun angle, 0..65535 = 0..2pi  */
} RbInput;

#define RB_BTN_FIRE        (1u << 0)
#define RB_BTN_LSIDEKICK   (1u << 1)
#define RB_BTN_RSIDEKICK   (1u << 2)
#define RB_BTN_CHANGEFIRE  (1u << 3)
#define RB_REQ_PAUSE       (1u << 4)   /* processed outside the sim, when confirmed */
#define RB_REQ_MENU        (1u << 5)   /* processed outside the sim, when confirmed */
#define RB_REQ_SKIPLEVEL   (1u << 6)   /* applied inside the sim at its frame       */
#define RB_REQ_NORTSHIP    (1u << 7)   /* applied inside the sim at its frame       */
#define RB_LINK_ANALOG     (1u << 8)   /* linkAngle holds an analog gun angle       */
#define RB_EV_DEMO_END     (1u << 9)   /* self-test only: demo ran out this tick    */
#define RB_EV_DISMISS      (1u << 10)  /* self-test only: game-over/demo dismissed  */

/* --- Single-player self-test ---------------------------------------------------
 *
 * When enabled (config key rollback_selftest), every in-level tick runs twice:
 * once live, then restored + replayed from the recorded tuples, and every
 * registered byte is compared.  A mismatch names the first divergent item --
 * that item (or something feeding it) is missing from the registry.
 */
extern bool rollback_selftest;                 /* config toggle                  */
bool rollback_selftest_active(void);           /* on && in-level && !netplay     */

void rollback_level_start(void);               /* reset ring + self-test state   */
void rollback_level_end(void);                 /* leaving the level loop         */
/* Loop-top hook: advance the tick index and snapshot (no-op mid-verify). */
void rollback_selftest_frame_begin(void);
/* Driver-site hook; true -> caller re-runs the tick (goto level_loop). */
bool rollback_selftest_tick(void);
/* A modal UI or live cheat mutated state outside the tuples: skip this tick. */
void rollback_taint(const char *why);
/* Temporary divergence probe: logs tick + pass + tag when the self-test is on. */
void rollback_dbg(const char *tag, int a, int b);

/* Tuple recording (pass 1) and replay lookup (pass 2), both players. */
void rollback_st_record(int player0, const RbInput *in);
void rollback_st_record_sf(int player0, Sint16 tx, Sint16 ty);
void rollback_st_event(Uint16 bit);            /* RB_EV_* on player 0's tuple    */
const RbInput *rollback_st_get(int player0);
Uint16 rollback_st_events(void);

extern unsigned long rollback_selftest_ticks, rollback_selftest_failures;

#endif /* ROLLBACK_H */
