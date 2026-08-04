/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback netcode. Each tick exchanges redundant input tuples; missing remote input is predicted
 * and corrected by restoring a snapshot and replaying. Irreversible transitions require confirmed
 * input from both peers.
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

/* True while the current network session uses rollback. */
bool nrb_active(void);
void nrb_set_session_mode(bool enabled);
bool nrb_session_mode(void);

/* Host Smooth Motion choice; it selects the simulation path in JE_playerMovement. */
void nrb_set_session_vt(bool enabled);
bool nrb_session_vt(void);

/* Host-controlled desync recovery. Same-build rollback peers can adopt the host snapshot and start
 * a new input epoch; incompatible peers reject the transfer. */
extern bool net_desync_recovery;
void nrb_set_session_recovery(bool enabled);
bool nrb_session_recovery(void);

/* Sim frame currently being simulated (1-based; 0 = before the first tick). */
Uint32 nrb_frame(void);

/* Perf-overlay counters: current prediction lead, maximum rollback depth, rollbacks per 100 frames,
 * and canary mismatch count. */
void nrb_stats(Uint32 *predict, Uint32 *depth, Uint32 *rate, Uint32 *desyncs);

void nrb_level_reset(void);

/* Snapshot the frame and apply the previous frame's simulation requests. Runs during replay too. */
void nrb_frame_begin(void);

/* JE_playerMovement hooks. */
void nrb_record_local(const RbInput *in);
void nrb_get_local(Uint32 frame, RbInput *out);
/* Return received or predicted peer input and record the tuple used by the simulation. */
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
