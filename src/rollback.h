/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback state registry, frame-indexed snapshot ring, and single-player completeness test.
 * Every simulation variable that survives a restore must be registered.
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

/* Re-simulation mode. rollback_resim suppresses one-shot I/O and live input; the silent variant
 * also suppresses render capture until the presented replay pass. */
extern bool rollback_resim;
extern bool rollback_resim_silent;

/* State registry. */

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

/* Registers without allocating the snapshot ring.  For callers that need the
 * registry described (the layout fingerprint) but are not about to roll back. */
void rollback_ensure_registered(void);

/* Hash of registry names, sizes, and offsets. Peers require this and state size to match before
 * exchanging snapshots. */
Uint32 rollback_layout_fingerprint(void);
/* Address-independent FNV-1a of the complete registered state. */
Uint32 rollback_state_hash(void);

/* Snapshot ring keyed by simulation frame. */

void rollback_ring_reset(void);
void rollback_snapshot(Uint32 frame);
bool rollback_have_frame(Uint32 frame);
bool rollback_restore(Uint32 frame);

/* Wire-safe snapshots encode registered pointers as tags or global-array offsets. Export verifies
 * its encoding locally; adoption requires a matching registry layout. */
bool rollback_wire_export(Uint8 *dst);   /* dst: rollback_state_size() bytes    */
bool rollback_wire_adopt(Uint8 *buf);    /* decodes in place, then restores     */
/* Zero dead object-pool slots before export. enemy[] is exempt because APPROACH spawns can inherit
 * a recycled slot's sprite bank. */
void rollback_wire_canonicalize(void);

/* Per-frame simulation input. x/y are absolute post-movement positions, making replay idempotent. */
typedef struct
{
	Sint16 x, y;            /* ship position after the movement routine        */
	Sint16 velX, velY;      /* ship velocity after the movement routine: the   */
	                        /* classic physics tail and banking read it, so it */
	                        /* must cross the wire or machines with different  */
	                        /* Smooth Motion settings drift                    */
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

/* Docked-link movement intent and dominant axis. Tuple positions include the sender's dock pin, so
 * comparing positions would mistake carrier movement for local input. */
#define RB_MOVE_LEFT       (1u << 11)
#define RB_MOVE_RIGHT      (1u << 12)
#define RB_MOVE_UP         (1u << 13)
#define RB_MOVE_DOWN       (1u << 14)
#define RB_MOVE_MASK       (RB_MOVE_LEFT | RB_MOVE_RIGHT | RB_MOVE_UP | RB_MOVE_DOWN)

/* Single-player self-test. Replay each tick from its snapshot and report the first differing
 * registered item. */
extern bool rollback_selftest;                 /* config toggle                  */
bool rollback_selftest_active(void);           /* on && in-level && !netplay     */
/* Flip the toggle (debug menu).  Arms the registry + ring when switched on
 * mid-level, which the level-start path only does for an already-on self-test. */
void rollback_selftest_set(bool on);

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
/* Bounded automated replay support; zero disables the bound. */
void rollback_selftest_set_limit(unsigned long ticks);
Uint32 rollback_selftest_bounded_hash(void);

#endif /* ROLLBACK_H */
