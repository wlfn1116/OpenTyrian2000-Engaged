/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback netcode implementation. Frames are 1-based; tagged ring slots reject stale entries.
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
#include "net_rollback.h"

#include "config.h"
#include "crashlog.h"
#include "endless.h"
#include "fonthand.h"
#include "keyboard.h"
#include "mainint.h"
#include "network.h"
#include "nortsong.h"
#include "player.h"
#include "qa.h"
#include "render_list.h"
#include "sprite.h"
#include "varz.h"
#include "vga256d.h"
#include "video.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool net_rollback = true;
bool net_desync_recovery = true;

static bool session_mode = false;
static bool session_vt = true;
static bool session_recovery = false;

#ifdef WITH_NETWORK
/* Levels started in this session, stamped on every input packet.  A peer stalled
 * at the end of the previous level keeps re-sending its records for as long as
 * the stall timeout allows, and those frame numbers can fall inside the new
 * level's acceptance window; the epoch is what tells them apart. */
static Uint16 nrb_epoch;

/* Session-long wire/stall counters for the crash log (nrb_write_diagnostics).
 * Unlike the per-level stats below these survive nrb_reset_core; they reset
 * when a session starts. */
static struct
{
	Uint32 pkt_in;                    /* input packets accepted                */
	Uint32 refused_malformed, refused_window, refused_epoch;
	Uint32 stalls;                    /* waits that earned a >3 s crashlog note */
} nrb_diag;

/* The peer's registry has a different layout (mismatched builds, or PC<->console),
 * so its bytes can never be adopted here.  Session-scoped like the counters above,
 * not per level: the peer's build does not change mid-session, and each retry costs
 * the host a full state export to be refused on chunk 0 again. */
static bool resync_layout_bad;
#endif

void nrb_set_session_mode(bool enabled)
{
	session_mode = enabled;
#ifdef WITH_NETWORK
	/* Both machines run this at connect time, so both start the count from zero. */
	nrb_epoch = 0;
	memset(&nrb_diag, 0, sizeof(nrb_diag));
	resync_layout_bad = false;
#endif
}

bool nrb_session_mode(void)
{
	return session_mode;
}

/* The HOST's smooth-motion (VT) preference, adopted by the session: the ship
 * physics tail it selects is simulation code, so both machines must run the
 * same one no matter what each machine's own presentation settings say. */
void nrb_set_session_vt(bool enabled)
{
	session_vt = enabled;
}

bool nrb_session_vt(void)
{
	return session_vt;
}

void nrb_set_session_recovery(bool enabled)
{
	session_recovery = enabled;
}

bool nrb_session_recovery(void)
{
	return session_recovery;
}

bool nrb_active(void)
{
	return isNetworkGame && session_mode;
}

#ifdef WITH_NETWORK

/* The UDP types come in via network.h above, which picks the real SDL_net or the Vita's
 * stand-in for it. */

#define NRB_HIST        64            /* input/canary history depth           */
#define NRB_REDUNDANCY  16            /* newest frames repeated per packet    */
#define NRB_TIME_OUT    16000         /* ms stalled with a DEAD link -> halt  */
#define NRB_ABS_TIME_OUT 300000       /* ms stalled even with a live link:    */
                                      /* something is wedged beyond waiting   */
#define NRB_REC_BYTES   14            /* wire size of one input record        */
#define NRB_HDR_BYTES   48            /* wire size of the packet header       */

/* Wire-relevant tuple bits: the four buttons, the four requests, the analog
 * link flag, and the four RB_MOVE_* docked-link intent bits.  The RB_EV_*
 * bits (9-10) are self-test-local and never leave a machine. */
#define NRB_WIRE_BUTTONS (0x01FFu | RB_MOVE_MASK)
/* Of those, the bits the SIMULATION reads.  Pause and in-game-menu requests are
 * processed outside the sim, from remote_hist rather than from what the frame
 * consumed, so an unpredicted pulse changes no simulated byte; comparing them
 * only bought a rollback that re-derived the identical state. */
#define NRB_SIM_BUTTONS (NRB_WIRE_BUTTONS & ~(RB_REQ_PAUSE | RB_REQ_MENU))
/* Bits a PREDICTED tuple may carry: held buttons + analog flag + held movement
 * intent; one-shot request pulses must never be predicted into existence. */
#define NRB_PREDICT_BUTTONS (RB_BTN_FIRE | RB_BTN_LSIDEKICK | RB_BTN_RSIDEKICK | \
                             RB_BTN_CHANGEFIRE | RB_LINK_ANALOG | RB_MOVE_MASK)

/* How much of a remote frame's tuple the simulation consumed.  The level-end
 * fade and a dead ship skip the movement/apply path entirely, but the frame's
 * REQUEST bits are still consumed at the next frame's start, so verification
 * must compare exactly what was consumed, no more and no less. */
enum
{
	NRB_USED_NONE = 0,   /* nothing consumed: any truth is compatible        */
	NRB_USED_REQS = 1,   /* only the skip/nort request bits were consumed    */
	NRB_USED_FULL = 2,   /* the movement path consumed the whole tuple       */
};

typedef struct
{
	RbInput in;
	Uint32  tag;   /* frame this slot holds; 0 = empty */
	Uint8   kind;  /* NRB_USED_* (remote_used only)    */
}
NrbSlot;

static NrbSlot local_hist[NRB_HIST];
static NrbSlot remote_hist[NRB_HIST];   /* truth, as received                 */
static NrbSlot remote_used[NRB_HIST];   /* what the sim actually consumed     */

/* Request bits that are frame-locked into the sim (pause/menu are processed
 * outside the sim at confirm time and deliberately excluded). */
#define NRB_SIM_REQS (RB_REQ_SKIPLEVEL | RB_REQ_NORTSHIP)

static Uint32 nrb_cur;                  /* frame being simulated              */
static Uint32 verified_upto;            /* sim used truth through this frame  */
static Uint32 req_done;                 /* pause/menu processed through this  */
static Uint32 req_at;                   /* frame the menu opens after; 0=none */
static bool   req_local_menu;           /* our own press, not the peer's      */
static Uint32 peer_acked;               /* peer holds all our frames <= this  */
static Uint32 remote_contig;            /* we hold all peer frames <= this    */
static Uint32 remote_newest;            /* newest peer frame seen (timesync)  */

static bool   resim_active;
static Uint32 resim_target;

static RbInput remote_seed;             /* prediction anchor before any data  */

/* Desync canary: per-frame sim summary, final once the frame is verified.
 * Alongside the three hashes it carries RAW values (scroll clock, link flag,
 * both ships' positions) so a mismatch report names the actual divergence
 * instead of two opaque words. */
typedef struct
{
	Uint32 tag, rand, ph, eh, xh;
	Uint16 curLoc;
	Uint8  linked;
	Sint16 px[2], py[2];
}
NrbCanary;
static NrbCanary canary[NRB_HIST];
/* Raw fields behind each canary's two hashes, kept so the one-per-level desync
 * report can print the disputed frame itself.  Valid only where canary[].tag
 * matches, so it needs no clearing of its own. */
static NetSimDetail canary_detail[NRB_HIST];
/* Per-pool breakdown behind the combined `xh`.  Only four bytes of the input header
 * were spare, so the wire carries one hash for all five pools; this says which of
 * them moved, and is captured at the same moment for the same reason. */
static NetSimPools  canary_pools[NRB_HIST];
/* Received canaries waiting for OUR copy of their frame to become final.  A
 * single slot starved the check: at any steady prediction depth the arriving
 * canary was always ahead of verified_upto, and the next packet overwrote it
 * before it could ever be compared. */
#define NRB_CANARY_PEND 8
static NrbCanary peer_pend[NRB_CANARY_PEND];
static int       peer_pend_n;
static Uint32    canary_checked_upto;   /* newest peer frame already compared */
static bool   canary_reported;          /* one full report per level          */
static Uint32 canary_mismatches;        /* further ones only counted          */

/* Wire-test bookkeeping. Session-scoped, unlike the per-level counters above: a recovery or a
 * level restart resets those, and the verdict has to see what happened across the whole run. */
static Uint16 qa_corrupt_epoch = 0xFFFF;
static Uint32 qa_desyncs_total, qa_resyncs_total;

/* Timesync: rolling frame-advantage estimate, exchanged both ways. */
static float  adv_ema;
static Sint16 their_adv_x8;
static Uint32 last_throttle_frame;
static Uint32 last_resend_tick;

/* Diagnostics. */
static Uint32 stat_rollbacks, stat_resim_frames, stat_deepest;

/* Desync recovery streams registered host state, then starts a new input epoch.
 * Chunk layout after the four-byte reliable header:
 *   [4]  Uint16 gen          attempt id within the level; NAK carries the gen refused
 *   [6]  Uint16 chunk index  0xFFFF = NAK (joiner could not assemble/adopt)
 *   [8]  Uint16 chunk count
 *   [10] Uint16 payload bytes
 * Chunk zero prefixes registry size, compressed size, and FNV-1a checksum before zero-run RLE. */
#define NRB_RS_HDR       12
#define NRB_RS_PRE       12
#define NRB_RS_PAYLOAD   (NET_PACKET_SIZE - NRB_RS_HDR)
#define NRB_RS_NAK       0xFFFFu          /* joiner -> host: did NOT adopt        */
#define NRB_RS_ACK       0xFFFEu          /* joiner -> host: adopted, gen G       */
#define NRB_RS_MAX       3                /* recovery attempts per level          */
#define NRB_RS_PROGRESS_TIME_OUT 8000     /* ms without a new chunk -> NAK        */
#define NRB_RS_ABS_TIME_OUT      60000    /* ms for the whole attempt             */
#define NRB_RS_ADOPT_TIME_OUT    6000     /* ms to answer a fully delivered stream */

/* NAK reason: retry transient failures, but stop after a permanent layout mismatch. */
#define NRB_NAK_RETRY    0u               /* transient: stall, checksum, short assembly */
#define NRB_NAK_FATAL    1u               /* permanent: layout/build mismatch           */

static Uint32 resync_used;   /* attempts consumed this level (either role)        */
static Uint16 resync_gen;    /* newest attempt id sent (host) / adopted (joiner)  */
static bool   resync_wanted; /* canary mismatch seen; the host acts on it         */

/* Presentation: a recovery is in play, so the stall overlay names that instead of
 * the wait.  Held from the mismatch until the peer is heard on the fresh timeline
 * the host finishes streaming while the joiner is still adopting, and one
 * hitch showing two different messages read like two different faults. */
static bool   resync_notice;
static Uint16 resync_notice_epoch;  /* epoch the notice was armed in */

static bool nrb_resync_dispatch(void);

/* Both roles arm this, at the mismatch and at the transfer, so neither machine
 * calls the recovery something the other does not. */
static void nrb_notice_resync(void)
{
	resync_notice = true;
	resync_notice_epoch = nrb_epoch;
}

/* Livelock guard: consecutive rollbacks that landed on the same frame. */
static Uint32 resim_last_K, resim_repeat;
static bool   resim_livelock_reported;

/* The level end was settled out of band (in-game menu quit), not predicted, so
 * it needs no further confirmation from the peer.  See nrb_process_requests. */
static bool end_agreed;

Uint32 nrb_frame(void)
{
	return nrb_cur;
}

void nrb_stats(Uint32 *predict, Uint32 *depth, Uint32 *rate, Uint32 *desyncs)
{
	*predict = (nrb_cur > remote_newest) ? nrb_cur - remote_newest : 0;
	*depth   = stat_deepest;
	*rate    = (nrb_cur > 1) ? stat_rollbacks * 100 / (nrb_cur - 1) : 0;
	*desyncs = canary_mismatches;
}

/* Everything a fresh timeline needs: shared by the level start and a completed
 * desync recovery (which IS a level start as far as frames, histories and the
 * epoch are concerned; only the per-level recovery budget survives it). */
static void nrb_reset_core(void)
{
	memset(local_hist, 0, sizeof(local_hist));
	memset(remote_hist, 0, sizeof(remote_hist));
	memset(remote_used, 0, sizeof(remote_used));
	memset(canary, 0, sizeof(canary));
	peer_pend_n = 0;
	canary_checked_upto = 0;
	canary_reported = false;
	canary_mismatches = 0;
	resync_wanted = false;

	++nrb_epoch;

	nrb_cur = 1;
	verified_upto = 0;
	req_done = 0;
	peer_acked = 0;
	remote_contig = 0;
	remote_newest = 0;
	resim_active = false;
	rollback_resim = false;
	rollback_resim_silent = false;

	adv_ema = 0.0f;
	their_adv_x8 = 0;
	last_throttle_frame = 0;
	last_resend_tick = 0;

	stat_rollbacks = stat_resim_frames = stat_deepest = 0;
	resim_last_K = resim_repeat = 0;
	resim_livelock_reported = false;
	end_agreed = false;
	req_at = 0;
	req_local_menu = false;

	/* Until the peer's first packet, predict "parked at spawn, no buttons".
	 * Ship spawn positions are part of deterministic level init, so both
	 * machines derive the identical seed. */
	memset(&remote_seed, 0, sizeof(remote_seed));
	{
		const uint ri = 2 - thisPlayerNum;  /* remote player, 0-based */
		remote_seed.x = (Sint16)player[ri].x;
		remote_seed.y = (Sint16)player[ri].y;
	}

	rollback_ring_reset();
}

void nrb_level_reset(void)
{
	nrb_reset_core();
	resync_used = 0;
	resync_gen = 0;
	resync_notice = false;
}

/* Frame start snapshots before applying the previous frame's simulation requests. */

void nrb_frame_begin(void)
{
	if (!nrb_active())
		return;

	/* Snapshot BEFORE the request application: the application is part of this
	 * frame's simulation and must repeat on a re-simulation pass. */
	rollback_snapshot(nrb_cur);

	if (qa_net_gameplay_ticks > 0 && nrb_cur == 1 && !rollback_resim)
	{
		fprintf(stderr, "net gameplay: level running (epoch %u)\n", (unsigned)nrb_epoch);
		fflush(stderr);
	}

	/* Wire-test desync: the joiner bends one frame of the epoch that first reaches it, after
	 * the snapshot so every simulation pass through that frame bends it exactly once. Armor,
	 * because the input tuples carry positions and would repair those; the divergence is then
	 * stable until a recovery opens a later epoch, where the gate goes quiet. */
	if (qa_net_corrupt_frame > 0 && thisPlayerNum != networkHostPlayerNum
	    && nrb_cur == qa_net_corrupt_frame)
	{
		if (qa_corrupt_epoch == 0xFFFF)
			qa_corrupt_epoch = nrb_epoch;
		if (nrb_epoch == qa_corrupt_epoch && player[0].armor > 6)
			player[0].armor -= 5;
	}

	if (nrb_cur >= 2)
	{
		const Uint32 pf = nrb_cur - 1;
		Uint16 bits = 0;

		if (local_hist[pf % NRB_HIST].tag == pf)
			bits |= local_hist[pf % NRB_HIST].in.buttons;

		/* Remote side: consume pf's request bits.  If the movement path already
		 * consumed the whole tuple this frame, use exactly that view (detector
		 * coherence).  Otherwise this IS the consumption; truth if it has
		 * arrived or nothing if not, then stamp it so the detector can
		 * roll us back should a request pulse turn up later. */
		{
			NrbSlot *u = &remote_used[pf % NRB_HIST];
			if (u->tag == pf && u->kind == NRB_USED_FULL)
			{
				bits |= u->in.buttons;
			}
			else
			{
				if (remote_hist[pf % NRB_HIST].tag == pf)
					u->in = remote_hist[pf % NRB_HIST].in;
				else
					memset(&u->in, 0, sizeof(u->in));
				u->tag = pf;
				u->kind = NRB_USED_REQS;
				bits |= u->in.buttons;
			}
		}

		if (bits & RB_REQ_SKIPLEVEL)
		{
			levelTimer = true;
			levelTimerCountdown = 0;
			endLevel = true;
			levelEnd = 40;
		}
		if (bits & RB_REQ_NORTSHIP)
		{
			player[0].items.ship = 12;                     /* Nort Ship          */
			player[0].items.special = 13;                  /* Astral Zone        */
			player[0].items.weapon[FRONT_WEAPON].id = 36;  /* Super Pulse        */
			player[0].items.weapon[REAR_WEAPON].id = 37;   /* Spreader           */
			shipGr = 1;
		}
	}
}

/* Input history. */

void nrb_record_local(const RbInput *in)
{
	NrbSlot *s = &local_hist[nrb_cur % NRB_HIST];
	s->in = *in;
	s->tag = nrb_cur;
}

void nrb_get_local(Uint32 frame, RbInput *out)
{
	NrbSlot *s = &local_hist[frame % NRB_HIST];
	if (s->tag == frame)
	{
		*out = s->in;
	}
	else
	{
		/* Should be unreachable: we only replay frames we recorded. */
		memset(out, 0, sizeof(*out));
		out->x = (Sint16)player[thisPlayerNum - 1].x;
		out->y = (Sint16)player[thisPlayerNum - 1].y;
	}
}

/* Damped linear extrapolation: follow the last observed per-frame delta for a
 * few frames, then hold.  Exact for parked and constant-velocity motion, small
 * corrections otherwise. */
static void nrb_predict_remote(Uint32 frame, RbInput *out)
{
	RbInput base = remote_seed;
	Uint32 bf = 0;

	const Uint32 floor_f = (frame > NRB_HIST / 2) ? frame - NRB_HIST / 2 : 0;
	for (Uint32 f = frame; f > floor_f; --f)
	{
		if (remote_hist[(f - 1) % NRB_HIST].tag == f - 1)
		{
			base = remote_hist[(f - 1) % NRB_HIST].in;
			bf = f - 1;
			break;
		}
	}

	int dx = 0, dy = 0;
	if (bf >= 2 && remote_hist[(bf - 1) % NRB_HIST].tag == bf - 1)
	{
		dx = (int)base.x - (int)remote_hist[(bf - 1) % NRB_HIST].in.x;
		dy = (int)base.y - (int)remote_hist[(bf - 1) % NRB_HIST].in.y;
		if (dx > 6) dx = 6; else if (dx < -6) dx = -6;
		if (dy > 6) dy = 6; else if (dy < -6) dy = -6;
	}

	Uint32 steps = (bf > 0) ? frame - bf : 0;
	if (steps > 5)
		steps = 5;

	*out = base;
	out->x = (Sint16)((int)base.x + dx * (int)steps);
	out->y = (Sint16)((int)base.y + dy * (int)steps);

	/* Velocity: integrate the engine's own rule (vel += accel per tick, clamped
	 * to the classic ±4) instead of holding it flat.  Held-flat velocity was
	 * the top mispredictor while the fused pair manoeuvred; every accel tick
	 * forced a rollback, and the resim cost snowballed on long fused levels. */
	{
		int pv = (int)base.velX + (int)base.accelX * (int)steps;
		if (pv > 4) pv = 4; else if (pv < -4) pv = -4;
		out->velX = (Sint16)pv;

		pv = (int)base.velY + (int)base.accelY * (int)steps;
		if (pv > 4) pv = 4; else if (pv < -4) pv = -4;
		out->velY = (Sint16)pv;
	}

	out->buttons &= NRB_PREDICT_BUTTONS;
}

void nrb_get_remote(Uint32 frame, RbInput *out)
{
	NrbSlot *truth = &remote_hist[frame % NRB_HIST];

	if (truth->tag == frame)
		*out = truth->in;
	else
		nrb_predict_remote(frame, out);

	/* Record what the sim consumed; misprediction = used != truth-later. */
	remote_used[frame % NRB_HIST].in = *out;
	remote_used[frame % NRB_HIST].tag = frame;
	remote_used[frame % NRB_HIST].kind = NRB_USED_FULL;
}

/* Compare only wire-carried fields; local-only tuple extras don't matter.
 * linkAngle reaches the sim only through the docked turret-rotate branch,
 * which requires fire held; an aim-only stick wiggle must not buy a
 * rollback that re-derives identical state. */
static bool nrb_wire_differs(const RbInput *a, const RbInput *b)
{
	return a->x != b->x || a->y != b->y ||
	       a->velX != b->velX || a->velY != b->velY ||
	       a->accelX != b->accelX || a->accelY != b->accelY ||
	       ((a->buttons ^ b->buttons) & NRB_SIM_BUTTONS) != 0 ||
	       (((a->buttons | b->buttons) & RB_BTN_FIRE) && a->linkAngle != b->linkAngle) ||
	       a->difficulty != b->difficulty;
}

/* How many rollbacks onto one frame are allowed before we stop trying to
 * correct it.  A correction that does not converge would otherwise spin the
 * restore/re-simulate cycle forever without completing a single frame. */
#define NRB_RESIM_GIVE_UP 16

/* Advance verified_upto over frames whose used inputs match the arrived truth;
 * return the first frame where they differ (0 = none). */
static Uint32 nrb_scan_mispredict(void)
{
	while (verified_upto < nrb_cur)
	{
		const Uint32 f = verified_upto + 1;
		bool differs = false;

		if (remote_hist[f % NRB_HIST].tag != f)
			break;  /* truth not here yet */
		if (remote_used[f % NRB_HIST].tag != f ||
		    remote_used[f % NRB_HIST].kind == NRB_USED_NONE)
		{
			/* The sim consumed nothing from this remote frame (level-end fade,
			 * dead ship): any truth is compatible, so it verifies as-is. */
			++verified_upto;
			continue;
		}
		if (remote_used[f % NRB_HIST].kind == NRB_USED_REQS)
		{
			/* Only the frame-locked request bits were consumed. */
			differs = ((remote_used[f % NRB_HIST].in.buttons ^
			            remote_hist[f % NRB_HIST].in.buttons) & NRB_SIM_REQS) != 0;
		}
		else
		{
			differs = nrb_wire_differs(&remote_used[f % NRB_HIST].in,
			                           &remote_hist[f % NRB_HIST].in);
		}

		if (differs)
		{
			/* Not converging: rolling back to f keeps landing on the same
			 * mismatch.  Accept the truth as consumed and move on; a state
			 * divergence the canary will report beats an unrecoverable freeze,
			 * and the machine stays responsive enough to quit. */
			if (f != resim_last_K || resim_repeat <= NRB_RESIM_GIVE_UP)
				return f;

			remote_used[f % NRB_HIST].in = remote_hist[f % NRB_HIST].in;
			resim_repeat = 0;

			if (!resim_livelock_reported)
			{
				resim_livelock_reported = true;
				char detail[224];
				snprintf(detail, sizeof(detail),
				         "frame %lu rolled back %d times without verifying "
				         "(current %lu, player %u)\n"
				         "  accepting the peer's input as consumed to keep the simulation moving",
				         (unsigned long)f, NRB_RESIM_GIVE_UP,
				         (unsigned long)nrb_cur, thisPlayerNum);
				crashlog_note_net("ROLLBACK LIVELOCK", detail);
			}
		}
		++verified_upto;
	}
	return 0;
}

/* Wire protocol. */

static void nrb_send_input(void)
{
	Uint8 *data = packet_out_temp->data;

	const Uint32 F = nrb_cur;
	Uint32 from = peer_acked + 1;
	if (from < 1)
		from = 1;
	if ((Sint64)F - (Sint64)from + 1 > NRB_REDUNDANCY)
		from = F + 1 - NRB_REDUNDANCY;
	/* Signed: the peer can have acked BEYOND our own counter after it ran ahead
	 * through our menu pause; the packet then carries no records (count 0) but
	 * still delivers ack/canary/timesync. */
	int count = (int)((Sint64)F - (Sint64)from + 1);
	if (count < 0)
		count = 0;

	SDLNet_Write16(PACKET_INPUT,   &data[0]);
	SDLNet_Write16((Uint16)count,  &data[2]);
	SDLNet_Write32(F,              &data[4]);
	SDLNet_Write32(remote_contig,  &data[8]);
	SDLNet_Write16((Uint16)(Sint16)(adv_ema * 8.0f), &data[12]);
	SDLNet_Write16(nrb_epoch,      &data[14]);

	/* Canary for the newest frame we know is final on our side: the three
	 * hashes plus raw context so a mismatch report can name the divergence. */
	if (verified_upto > 0 && canary[verified_upto % NRB_HIST].tag == verified_upto)
	{
		const NrbCanary *c = &canary[verified_upto % NRB_HIST];
		SDLNet_Write32(c->tag,  &data[16]);
		SDLNet_Write32(c->rand, &data[20]);
		SDLNet_Write32(c->ph,   &data[24]);
		SDLNet_Write32(c->eh,   &data[28]);
		SDLNet_Write16(c->curLoc, &data[32]);
		data[34] = c->linked;
		data[35] = 0;
		SDLNet_Write16((Uint16)c->px[0], &data[36]);
		SDLNet_Write16((Uint16)c->py[0], &data[38]);
		SDLNet_Write16((Uint16)c->px[1], &data[40]);
		SDLNet_Write16((Uint16)c->py[1], &data[42]);
		SDLNet_Write32(c->xh,   &data[44]);   /* the header's last spare word */
	}
	else
	{
		memset(&data[16], 0, NRB_HDR_BYTES - 16);
	}

	int off = NRB_HDR_BYTES;
	for (Uint32 f = from; f <= F; ++f, off += NRB_REC_BYTES)
	{
		const RbInput *in = &local_hist[f % NRB_HIST].in;
		SDLNet_Write16((Uint16)in->x, &data[off]);
		SDLNet_Write16((Uint16)in->y, &data[off + 2]);
		data[off + 4] = (Uint8)(Sint8)in->accelX;
		data[off + 5] = (Uint8)(Sint8)in->accelY;
		SDLNet_Write16(in->buttons & NRB_WIRE_BUTTONS, &data[off + 6]);
		SDLNet_Write16(in->linkAngle, &data[off + 8]);
		data[off + 10] = in->difficulty;
		data[off + 11] = 0;
		data[off + 12] = (Uint8)(Sint8)in->velX;
		data[off + 13] = (Uint8)(Sint8)in->velY;
	}

	network_send_unacked(off);
	last_resend_tick = SDL_GetTicks();
}

void nrb_handle_packet(const Uint8 *data, int len)
{
	if (len < NRB_HDR_BYTES)
	{
		++nrb_diag.refused_malformed;
		return;
	}

	const int    count = SDLNet_Read16(&data[2]);
	const Uint32 F     = SDLNet_Read32(&data[4]);
	const Uint32 ack   = SDLNet_Read32(&data[8]);

	if (count < 0 || count > NRB_REDUNDANCY || len < NRB_HDR_BYTES + count * NRB_REC_BYTES)
	{
		++nrb_diag.refused_malformed;
		return;
	}
	if (F < (Uint32)count)
	{
		++nrb_diag.refused_malformed;
		return;
	}
	/* In-flight datagrams from the previous level carry frame numbers far past
	 * this level's counter; letting them in would poison the ack frontier and
	 * the timesync estimate.  A real peer can only be MAX_PREDICT ahead. */
	if (F > nrb_cur + NRB_HIST)
	{
		++nrb_diag.refused_window;
		return;
	}
	/* ...and a SHORT previous level leaves frame numbers small enough to pass that
	 * test, which the epoch catches instead.  Only strictly older is refused: a
	 * peer that is a level ahead of us is the pre-existing behaviour, and refusing
	 * it would turn a one-sided level-start skew into a mutual stall. */
	if ((Sint16)(SDLNet_Read16(&data[14]) - nrb_epoch) < 0)
	{
		++nrb_diag.refused_epoch;
		return;
	}

	++nrb_diag.pkt_in;

	their_adv_x8 = (Sint16)SDLNet_Read16(&data[12]);

	if (ack > peer_acked && ack <= nrb_cur)
		peer_acked = ack;
	if (F > remote_newest)
	{
		remote_newest = F;
		/* Timesync sample: how far ahead of the peer are we running? */
		const float sample = (float)((Sint64)nrb_cur - (Sint64)remote_newest);
		adv_ema = adv_ema * 0.9f + sample * 0.1f;
	}

	{
		/* The peer repeats one frame's canary until its own side moves on, so take
		 * each frame exactly once: already queued, or already checked. */
		const Uint32 cf = SDLNet_Read32(&data[16]);
		bool have = cf == 0 || cf <= canary_checked_upto;
		for (int i = 0; !have && i < peer_pend_n; ++i)
			have = peer_pend[i].tag == cf;

		if (!have)
		{
			if (peer_pend_n == NRB_CANARY_PEND)
			{
				/* Full: drop the oldest.  It is the one whose frame our own side is
				 * least likely to still hold a canary for anyway. */
				memmove(&peer_pend[0], &peer_pend[1], sizeof(peer_pend[0]) * (NRB_CANARY_PEND - 1));
				--peer_pend_n;
			}

			NrbCanary *const c = &peer_pend[peer_pend_n++];
			c->tag  = cf;
			c->rand = SDLNet_Read32(&data[20]);
			c->ph   = SDLNet_Read32(&data[24]);
			c->eh   = SDLNet_Read32(&data[28]);
			c->curLoc = SDLNet_Read16(&data[32]);
			c->linked = data[34];
			c->px[0] = (Sint16)SDLNet_Read16(&data[36]);
			c->py[0] = (Sint16)SDLNet_Read16(&data[38]);
			c->px[1] = (Sint16)SDLNet_Read16(&data[40]);
			c->py[1] = (Sint16)SDLNet_Read16(&data[42]);
			c->xh    = SDLNet_Read32(&data[44]);
		}
	}

	const Uint32 from = F - (Uint32)count + 1;
	int off = NRB_HDR_BYTES;
	for (Uint32 f = from; f <= F; ++f, off += NRB_REC_BYTES)
	{
		if (f == 0 || f + NRB_HIST / 2 < nrb_cur || f > nrb_cur + NRB_HIST / 2)
			continue;  /* out of the useful window */

		NrbSlot *s = &remote_hist[f % NRB_HIST];
		if (s->tag == f)
			continue;  /* already have the truth for this frame */

		memset(&s->in, 0, sizeof(s->in));
		s->in.x = (Sint16)SDLNet_Read16(&data[off]);
		s->in.y = (Sint16)SDLNet_Read16(&data[off + 2]);
		s->in.accelX = (Sint8)data[off + 4];
		s->in.accelY = (Sint8)data[off + 5];
		s->in.buttons = SDLNet_Read16(&data[off + 6]) & NRB_WIRE_BUTTONS;
		s->in.linkAngle = SDLNet_Read16(&data[off + 8]);
		s->in.difficulty = data[off + 10];
		s->in.velX = (Sint8)data[off + 12];
		s->in.velY = (Sint8)data[off + 13];
		s->tag = f;
	}

	while (remote_hist[(remote_contig + 1) % NRB_HIST].tag == remote_contig + 1)
		++remote_contig;
}

/* State canary. */

static void nrb_stamp_canary(Uint32 frame)
{
	NrbCanary *c = &canary[frame % NRB_HIST];
	c->tag = frame;
	network_sim_state(&c->rand, &c->ph, &c->eh);
	c->xh = network_sim_pools(&canary_pools[frame % NRB_HIST]);
	network_sim_detail(&canary_detail[frame % NRB_HIST]);
	c->curLoc = (Uint16)curLoc;
	c->linked = twoPlayerLinked ? 1 : 0;
	c->px[0] = (Sint16)player[0].x;
	c->py[0] = (Sint16)player[0].y;
	c->px[1] = (Sint16)player[1].x;
	c->py[1] = (Sint16)player[1].y;
}

/* Bounded append for the desync report below; off saturates at cap - 1. */
static int nrb_apf(char *buf, int cap, int off, const char *fmt, ...)
{
	if (off < 0 || off >= cap - 1)
		return cap - 1;
	va_list ap;
	va_start(ap, fmt);
	const int n = vsnprintf(buf + off, (size_t)(cap - off), fmt, ap);
	va_end(ap);
	if (n < 0 || off + n > cap - 1)
		return cap - 1;
	return off + n;
}

/* One input column of the window below, carrying every field nrb_wire_differs()
 * compares.  Printing x,y alone showed two identical columns beside frames that
 * had legitimately rolled back on a velocity the report never named. */
static int nrb_apf_tuple(char *buf, int cap, int off, const NrbSlot *s, Uint32 f)
{
	if (s->tag != f)
		return nrb_apf(buf, cap, off, " (gone) |");
	return nrb_apf(buf, cap, off, " %4d,%-4d %04x v%d,%d a%d,%d L%u d%u |",
	               s->in.x, s->in.y, (unsigned)s->in.buttons,
	               s->in.velX, s->in.velY, s->in.accelX, s->in.accelY,
	               (unsigned)s->in.linkAngle, (unsigned)s->in.difficulty);
}

/* One received canary against our own copy of its frame.  The caller has already
 * established that our side considers that frame final. */
static void nrb_check_canary(const NrbCanary *const peer)
{
	const NrbCanary *ours = &canary[peer->tag % NRB_HIST];
	if (ours->tag != peer->tag)
		return;  /* out of window; too old to compare */

	if (ours->rand != peer->rand || ours->ph != peer->ph ||
	    ours->eh != peer->eh || ours->xh != peer->xh ||
	    ours->curLoc != peer->curLoc || ours->linked != peer->linked)
	{
		++canary_mismatches;
		++qa_desyncs_total;

		/* Write one detailed report per level; later mismatches only increment the counter. */
		if (!canary_reported)
		{
			canary_reported = true;

			/* Compare adjacent frames to distinguish state divergence from frame-counter skew. */
			const NrbCanary *m1 = &canary[(peer->tag - 1) % NRB_HIST];
			const NrbCanary *p1 = &canary[(peer->tag + 1) % NRB_HIST];
			const char *skew = "";
			if (peer->tag >= 2 && m1->tag == peer->tag - 1 &&
			    m1->rand == peer->rand && m1->ph == peer->ph &&
			    m1->eh == peer->eh && m1->curLoc == peer->curLoc)
				skew = "\n  NOTE: remote frame N equals our frame N-1 -- FRAME COUNTERS SKEWED (we run one ahead)";
			else if (p1->tag == peer->tag + 1 &&
			         p1->rand == peer->rand && p1->ph == peer->ph &&
			         p1->eh == peer->eh && p1->curLoc == peer->curLoc)
				skew = "\n  NOTE: remote frame N equals our frame N+1 -- FRAME COUNTERS SKEWED (we run one behind)";

			/* Static: the enemy table can outgrow a comfortable stack frame, and
			 * this writes at most once per level from the main thread. */
			static char detail[12288];
			int off = nrb_apf(detail, sizeof(detail), 0,
			         "rollback frame %lu (player %u, current frame %lu)\n"
			         "  rand draws : local %lu  remote %lu  %s\n"
			         "  players    : local %08x  remote %08x  %s\n"
			         "  enemies    : local %08x  remote %08x  %s\n"
			         "  pools      : local %08x  remote %08x  %s\n"
			         "  curLoc     : local %u  remote %u  %s\n"
			         "  linked     : local %u  remote %u  %s\n"
			         "  P1 pos     : local %d,%d  remote %d,%d\n"
			         "  P2 pos     : local %d,%d  remote %d,%d\n"
			         "  rollbacks so far: %lu (deepest %lu, resim frames %lu)%s\n",
			         (unsigned long)peer->tag, thisPlayerNum, (unsigned long)nrb_cur,
			         (unsigned long)ours->rand, (unsigned long)peer->rand,
			         ours->rand == peer->rand ? "ok" : "DIFFERS",
			         (unsigned)ours->ph, (unsigned)peer->ph,
			         ours->ph == peer->ph ? "ok" : "DIFFERS",
			         (unsigned)ours->eh, (unsigned)peer->eh,
			         ours->eh == peer->eh ? "ok" : "DIFFERS",
			         (unsigned)ours->xh, (unsigned)peer->xh,
			         ours->xh == peer->xh ? "ok" : "DIFFERS",
			         (unsigned)ours->curLoc, (unsigned)peer->curLoc,
			         ours->curLoc == peer->curLoc ? "ok" : "DIFFERS",
			         (unsigned)ours->linked, (unsigned)peer->linked,
			         ours->linked == peer->linked ? "ok" : "DIFFERS",
			         ours->px[0], ours->py[0], peer->px[0], peer->py[0],
			         ours->px[1], ours->py[1], peer->px[1], peer->py[1],
			         (unsigned long)stat_rollbacks, (unsigned long)stat_deepest,
			         (unsigned long)stat_resim_frames, skew);

			/* Log the raw fields behind each disputed hash for comparison with the peer. */
			{
				const NetSimDetail *d = &canary_detail[peer->tag % NRB_HIST];

				off = nrb_apf(detail, sizeof(detail), off,
				              "  --- local frame %lu, hashed fields (diff against the peer's log) ---\n",
				              (unsigned long)peer->tag);
				for (int i = 0; i < 2; ++i)
					off = nrb_apf(detail, sizeof(detail), off,
					              "  P%d: x=%ld y=%ld armor=%ld shield=%ld alive=%ld cash=%ld\n",
					              i + 1, (long)d->p[i].x, (long)d->p[i].y, (long)d->p[i].armor,
					              (long)d->p[i].shield, (long)d->p[i].alive, (long)d->p[i].cash);
				/* Print bounded pool detail before the potentially long enemy table. */
				const NetSimPools *x = &canary_pools[peer->tag % NRB_HIST];
				off = nrb_apf(detail, sizeof(detail), off,
				              "  pools: explosions %08x (%u live)  repeating %08x (%u)\n"
				              "         enemy shots %08x (%u)  player shots %08x (%u)  sound %08x\n",
				              (unsigned)x->explosions, (unsigned)x->n_expl,
				              (unsigned)x->rep_explosions, (unsigned)x->n_rep,
				              (unsigned)x->enemy_shots, (unsigned)x->n_eshot,
				              (unsigned)x->player_shots, (unsigned)x->n_pshot,
				              (unsigned)x->sound);

				/* Include a bounded player-shot sample; other pool state is covered elsewhere. */
				if (x->n_pshot > 0)
				{
					const Uint16 shown = x->n_pshot < NET_SIM_DETAIL_SHOTS
					                   ? x->n_pshot : NET_SIM_DETAIL_SHOTS;
					off = nrb_apf(detail, sizeof(detail), off,
					              "  player shots: %u live, showing %u (idx: x,y xm,ym xc,yc dmg avail pl pierce)\n",
					              (unsigned)x->n_pshot, (unsigned)shown);
					for (Uint16 i = 0; i < shown; ++i)
						off = nrb_apf(detail, sizeof(detail), off,
						              "  %4u: %5ld,%-5ld m%ld,%-4ld c%ld,%-4ld d%-4u v%-4u p%u k%u\n",
						              (unsigned)x->pshot[i].idx,
						              (long)x->pshot[i].x, (long)x->pshot[i].y,
						              (long)x->pshot[i].xm, (long)x->pshot[i].ym,
						              (long)x->pshot[i].xc, (long)x->pshot[i].yc,
						              (unsigned)x->pshot[i].dmg, (unsigned)x->pshot[i].avail,
						              (unsigned)x->pshot[i].playernum, (unsigned)x->pshot[i].pierce);
				}

				off = nrb_apf(detail, sizeof(detail), off,
				              "  enemies: %u live (idx: ex,ey armor avail type)\n",
				              (unsigned)d->enemy_count);
				for (Uint16 i = 0; i < d->enemy_count && i < NET_SIM_DETAIL_ENEMIES; ++i)
					off = nrb_apf(detail, sizeof(detail), off,
					              "  %3u: %5ld,%-5ld a%-5ld v%u t%u\n",
					              (unsigned)d->e[i].idx, (long)d->e[i].ex, (long)d->e[i].ey,
					              (long)d->e[i].armorleft, (unsigned)d->e[i].avail,
					              (unsigned)d->e[i].type);
			}

			/* Input window up to the disputed frame: what we sent, the peer's
			 * truth, and what the sim consumed.  "CONSUMED != TRUTH" on a verified
			 * frame means the rollback verifier missed a misprediction, a different
			 * failure from simulation code reading state outside the tuples. */
			{
				const Uint32 lo = peer->tag > 7 ? peer->tag - 7 : 1;

				off = nrb_apf(detail, sizeof(detail), off,
				              "  inputs %lu..%lu (x,y btn v<vel> a<accel> L<linkAngle> d<difficulty>)\n"
				              "  (frame: local | remote truth | consumed kind):\n",
				              (unsigned long)lo, (unsigned long)peer->tag);
				for (Uint32 f = lo; f <= peer->tag; ++f)
				{
					const NrbSlot *L = &local_hist[f % NRB_HIST];
					const NrbSlot *R = &remote_hist[f % NRB_HIST];
					const NrbSlot *U = &remote_used[f % NRB_HIST];

					off = nrb_apf(detail, sizeof(detail), off, "  %6lu:", (unsigned long)f);
					off = nrb_apf_tuple(detail, sizeof(detail), off, L, f);
					off = nrb_apf_tuple(detail, sizeof(detail), off, R, f);

					if (U->tag != f)
						off = nrb_apf(detail, sizeof(detail), off, " (gone)\n");
					else
					{
						bool bad = false;
						if (R->tag == f)
						{
							if (U->kind == NRB_USED_FULL)
								bad = U->in.x != R->in.x || U->in.y != R->in.y ||
								      U->in.velX != R->in.velX || U->in.velY != R->in.velY ||
								      U->in.accelX != R->in.accelX || U->in.accelY != R->in.accelY ||
								      U->in.linkAngle != R->in.linkAngle ||
								      U->in.difficulty != R->in.difficulty ||
								      ((U->in.buttons ^ R->in.buttons) & NRB_SIM_BUTTONS) != 0;
							else if (U->kind == NRB_USED_REQS)
								bad = ((U->in.buttons ^ R->in.buttons) & NRB_SIM_REQS) != 0;
						}
						off = nrb_apf(detail, sizeof(detail), off, " k%u%s\n",
						              (unsigned)U->kind,
						              bad ? "  !! CONSUMED != TRUTH on a verified frame" : "");
					}
				}
			}

			network_diag_note_desync((int)mainLevel);
			crashlog_note_net("NETWORK DESYNC (rollback)", detail);

			if (networkDesyncHalt)
				network_tyrian_halt(7, false);
		}

		/* Repairable: arm a recovery.  The host acts on the flag at its driver
		 * site; the joiner's own copy is cleared when the stream arrives. */
		if (nrb_session_recovery() && resync_used < NRB_RS_MAX && !resync_layout_bad)
		{
			resync_wanted = true;
			nrb_notice_resync();
		}
	}
}

/* Compare every pending canary whose frame our side has finalised, and keep the
 * rest for a later frame. */
static void nrb_compare_canary(void)
{
	int keep = 0;

	for (int i = 0; i < peer_pend_n; ++i)
	{
		if (peer_pend[i].tag > verified_upto)
		{
			peer_pend[keep++] = peer_pend[i];   /* our copy is not final yet */
			continue;
		}

		nrb_check_canary(&peer_pend[i]);
		if (peer_pend[i].tag > canary_checked_upto)
			canary_checked_upto = peer_pend[i].tag;
	}

	peer_pend_n = keep;
}

/* Schedule state-changing menu requests beyond prediction and wait for peer confirmation.
 * Presentation-only pause requests remain immediate. */
#define NRB_REQ_LEAD (ROLLBACK_MAX_PREDICT + 2)

static void nrb_process_requests(void)
{
	while (req_done < verified_upto)
	{
		const Uint32 f = ++req_done;
		const Uint16 lbits = (local_hist[f % NRB_HIST].tag == f) ? local_hist[f % NRB_HIST].in.buttons : 0;
		const Uint16 rbits = (remote_hist[f % NRB_HIST].tag == f) ? remote_hist[f % NRB_HIST].in.buttons : 0;
		const Uint16 bits = lbits | rbits;

		// RB_REQ_PAUSE is dead: online cannot pause, so a set bit from any source is ignored.

		if (bits & RB_REQ_MENU)
		{
			/* Coalesce nearby requests into the earliest scheduled opening. */
			const Uint32 at = f + NRB_REQ_LEAD;
			if (req_at == 0 || at < req_at)
				req_at = at;
			req_local_menu |= (lbits & RB_REQ_MENU) != 0;
		}
	}
}

/* Rollback trigger. */

/* Restore to frame K and arrange for the level loop to re-simulate K..target. */
static NrbStep nrb_begin_resim(Uint32 K)
{
	/* Highest frame the timeline we are about to discard ever simulated. */
	const Uint32 high = (resim_active && resim_target > nrb_cur) ? resim_target : nrb_cur;

	/* Clear consumed-input records for the discarded timeline. Early-return frames may not stamp
	 * them again; retaining stale records can trigger an endless rollback to K. */
	for (Uint32 f = K; f <= high; ++f)
		if (remote_used[f % NRB_HIST].tag == f)
			memset(&remote_used[f % NRB_HIST], 0, sizeof(remote_used[0]));

	/* Count consecutive rollbacks onto the same frame; the scan uses this to
	 * break out of a correction that is not converging (see NRB_RESIM_GIVE_UP). */
	if (K == resim_last_K)
		++resim_repeat;
	else
	{
		resim_last_K = K;
		resim_repeat = 1;
	}

	if (!rollback_restore(K))
	{
		/* A missing snapshot inside the prediction window is fatal because continuing
		 * would silently desynchronize the peers. */
		char detail[128];
		snprintf(detail, sizeof(detail),
		         "no snapshot for frame %lu (current %lu, verified %lu)",
		         (unsigned long)K, (unsigned long)nrb_cur, (unsigned long)verified_upto);
		crashlog_note_net("ROLLBACK SNAPSHOT MISSING", detail);
		network_tyrian_halt(7, false);
	}

	rl_abort_record();  /* drop the aborted pass's partial render recording */

	++stat_rollbacks;
	stat_resim_frames += high - K + 1;
	if (high - K + 1 > stat_deepest)
		stat_deepest = high - K + 1;

	resim_active = true;
	/* `high`, not nrb_cur: a second correction arriving mid-re-simulation must
	 * not shorten the pass to the frame we happen to be replaying.  Dropping the
	 * tail turned those frames back into normal passes, and a normal pass
	 * re-samples live input over local_hist entries already sent to the peer and
	 * guarantees a desynchronization. */
	resim_target = high;
	nrb_cur = K;
	rollback_resim = true;
	rollback_resim_silent = (K < resim_target);
	return NRB_STEP_RESIM;
}

/* A peer that has reached a between-levels handshake is never going to produce the frames a stall
 * is waiting for. Every packet named here is only ever sent from outside a level, so one at the
 * head of the reliable queue settles it: end ours as well, out of band. The packet stays where it
 * is; the rendezvous that follows is what reads it. `head` is 0 when the queue is empty.
 *
 * Returns true when the level was ended. Called by the stall pump and directly by the test suite,
 * which is the only way the quit case below gets covered. */
bool nrb_peer_left_level(Uint16 head)
{
	if (head != PACKET_WAITING && head != PACKET_DETAILS && head != PACKET_GAME_QUIT
	    && head != PACKET_SHOP_SYNC && head != PACKET_ENDLESS_RUN)
		return false;

	reallyEndLevel = true;
	end_agreed = true;

	/* A quit is not a clear. Both other paths that read this packet say so; while this one
	 * stayed silent the peer banked the zone and deepened while the player who quit reopened
	 * the same outpost, and the pair spent the rest of the run one zone apart, charting from
	 * slates that no longer matched. */
	if (head == PACKET_GAME_QUIT)
	{
		playerEndLevel = true;
		if (coopEndlessMode)
			endlessCoopPeerQuitLevel();
	}
	return true;
}

/* Pump the world while stalled: OS events, inbound packets, periodic input
 * resend (the peer may be waiting on a lost packet), bounded by timeout.
 * Returns true when an inbound desync recovery reset the timeline underneath
 * the wait; the caller must abandon it and fall through to present. */
static bool nrb_stall_pump(Uint32 wait_start, bool *stall_reported, const char *why)
{
	service_SDL_events(false);

	if (SDL_GetTicks() - last_resend_tick > 100)
		nrb_send_input();

	// <= 0, not == 0: a receive error reads nothing, so skipping the sleep on it would
	// spin this pump on a core for the whole stall.
	if (network_check() <= 0)
		SDL_Delay(1);

	// The peer can start a recovery while we sit in a wait loop (a desync right
	// at the level end is the classic case).  Handle it here, or its chunks
	// would rot at the head of the reliable queue until the wait timed out.
	if (nrb_resync_dispatch())
		return true;

	const Uint32 waited = SDL_GetTicks() - wait_start;

	// After ~0.7s, say what's happening instead of freezing silently: the other
	// player may still be reading the level-clear tally or shopping.  Same
	// overlay pattern JE_pauseGame uses mid-level.
	if (waited > 700)
	{
		static Uint32 overlay_for = 0;
		static Uint32 last_present = 0;

		SDL_Surface *const save = VGAScreen;
		VGAScreen = VGAScreenSeg;
		if (overlay_for != wait_start)
		{
			overlay_for = wait_start;
			// This wait IS the recovery when one is in play; the machine on the
			// other side of it stops simulating to stream or adopt state.
			JE_drawNetworkNotice(resync_notice ? "Resyncing players." : "Waiting for other player.");
			last_present = 0;
		}
		if (SDL_GetTicks() - last_present > 100)
		{
			last_present = SDL_GetTicks();
			JE_showVGA();
		}
		VGAScreen = save;
	}

	if (!*stall_reported && waited > 3000)
	{
		*stall_reported = true;
		++nrb_diag.stalls;
		char detail[256];
		snprintf(detail, sizeof(detail),
		         "%s\n"
		         "  player   : %u   level epoch: %u\n"
		         "  frame    : %lu   verified: %lu   peer ack: %lu   remote newest: %lu",
		         why, thisPlayerNum, (unsigned)nrb_epoch,
		         (unsigned long)nrb_cur, (unsigned long)verified_upto,
		         (unsigned long)peer_acked, (unsigned long)remote_newest);
		crashlog_note_net("ROLLBACK STALL", detail);
	}

	if (nrb_peer_left_level(packet_in[0] != NULL ? SDLNet_Read16(&packet_in[0]->data[0]) : 0))
		return true;

	// A LIVE peer that is merely slow (menus, loading, level tally) must never
	// trip the disconnect: keep-alives hold the link open indefinitely, with a
	// generous absolute cap as the wedge backstop.
	if ((waited > NRB_TIME_OUT && !network_peer_alive()) || waited > NRB_ABS_TIME_OUT)
	{
		fprintf(stderr, "error: no usable input from the other player for %u ms\n",
		        (unsigned)waited);
		network_tyrian_halt(2, false);
	}

	return false;
}

/* Desync recovery engine. */

/* FNV-1a over the compressed stream: guards the assembly logic, not the link
 * (UDP already checksums each datagram). */
static Uint32 nrb_rs_hash(const Uint8 *p, size_t n)
{
	Uint32 h = 2166136261u;
	for (size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

/* Zero-run RLE uses token 0x00 plus a two-byte run; nonzero tokens prefix literal lengths.
 * Runs shorter than four bytes stay literal. Return zero on destination overflow. */
size_t nrb_resync_compress(const Uint8 *src, size_t n, Uint8 *dst, size_t cap)
{
	size_t in = 0, out = 0;
	while (in < n)
	{
		size_t zrun = 0;
		while (in + zrun < n && src[in + zrun] == 0 && zrun < 65535)
			++zrun;
		if (zrun >= 4)
		{
			if (out + 3 > cap)
				return 0;
			dst[out++] = 0x00;
			dst[out++] = (Uint8)(zrun >> 8);
			dst[out++] = (Uint8)zrun;
			in += zrun;
			continue;
		}

		size_t lit = 0;
		while (in + lit < n && lit < 255)
		{
			if (src[in + lit] == 0)
			{
				size_t z = 1;
				while (in + lit + z < n && src[in + lit + z] == 0 && z < 4)
					++z;
				if (z >= 4)
					break;
			}
			++lit;
		}
		if (out + 1 + lit > cap)
			return 0;
		dst[out++] = (Uint8)lit;
		memcpy(dst + out, src + in, lit);
		out += lit;
		in += lit;
	}
	return out;
}

size_t nrb_resync_expand(const Uint8 *src, size_t n, Uint8 *dst, size_t cap)
{
	size_t in = 0, out = 0;
	while (in < n)
	{
		const Uint8 tok = src[in++];
		if (tok == 0x00)
		{
			if (in + 2 > n)
				return 0;
			const size_t zrun = ((size_t)src[in] << 8) | src[in + 1];
			in += 2;
			if (zrun == 0 || out + zrun > cap)
				return 0;
			memset(dst + out, 0, zrun);
			out += zrun;
		}
		else
		{
			if (in + tok > n || out + tok > cap)
				return 0;
			memcpy(dst + out, src + in, tok);
			in += tok;
			out += tok;
		}
	}
	return out;
}

/* Pump for the transfer loops: OS events, inbound datagrams, the overlay, the
 * link timeouts.  False = this attempt ran out of time.  Not nrb_stall_pump:
 * that one resends input records (noise while a transfer owns the channel),
 * dispatches inbound recoveries (this IS one), and names the wrong wait. */
static bool nrb_resync_pump(Uint32 wait_start, bool *reported, const char *why)
{
	service_SDL_events(false);

	if (network_check() <= 0)
		SDL_Delay(1);

	const Uint32 waited = SDL_GetTicks() - wait_start;

	if (waited > 300)
	{
		static Uint32 overlay_for = 0;
		static Uint32 last_present = 0;

		SDL_Surface *const save = VGAScreen;
		VGAScreen = VGAScreenSeg;
		if (overlay_for != wait_start)
		{
			overlay_for = wait_start;
			JE_barShade(VGAScreen, 3, 60, 257, 80);
			JE_barShade(VGAScreen, 5, 62, 255, 78);
			JE_dString(VGAScreen, 10, 65, "Resyncing players.", SMALL_FONT_SHAPES);
			last_present = 0;
		}
		if (SDL_GetTicks() - last_present > 100)
		{
			last_present = SDL_GetTicks();
			JE_showVGA();
		}
		VGAScreen = save;
	}

	if (!*reported && waited > 3000)
	{
		*reported = true;
		++nrb_diag.stalls;
		char detail[224];
		snprintf(detail, sizeof(detail),
		         "%s\n  player %u, attempt %lu, gen %u, %lu ms in",
		         why, thisPlayerNum, (unsigned long)resync_used,
		         (unsigned)resync_gen, (unsigned long)waited);
		crashlog_note_net("RESYNC STALL", detail);
	}

	if (waited > NRB_TIME_OUT && !network_peer_alive())
		network_tyrian_halt(2, false);

	return waited <= NRB_RS_ABS_TIME_OUT;
}

/* One chunk onto the acknowledged channel.  `stream` = preamble + compressed. */
static void nrb_resync_send_chunk(const Uint8 *stream, size_t total, Uint32 chunks, Uint32 idx)
{
	Uint8 *data = packet_out_temp->data;
	const size_t from = (size_t)idx * NRB_RS_PAYLOAD;
	size_t len = total - from;
	if (len > NRB_RS_PAYLOAD)
		len = NRB_RS_PAYLOAD;

	network_prepare(PACKET_RESYNC);
	SDLNet_Write16(resync_gen,     &data[4]);
	SDLNet_Write16((Uint16)idx,    &data[6]);
	SDLNet_Write16((Uint16)chunks, &data[8]);
	SDLNet_Write16((Uint16)len,    &data[10]);
	memcpy(&data[NRB_RS_HDR], stream + from, len);
	network_send(NRB_RS_HDR + (int)len);
}

static void nrb_resync_send_nak(Uint16 gen, Uint16 reason)
{
	Uint8 *data = packet_out_temp->data;
	network_prepare(PACKET_RESYNC);
	SDLNet_Write16(gen,                &data[4]);
	SDLNet_Write16((Uint16)NRB_RS_NAK, &data[6]);
	SDLNet_Write16(reason,             &data[8]);
	SDLNet_Write16(0,                  &data[10]);
	network_send(NRB_RS_HDR);
}

/* The joiner's "I am on the new timeline".  The host resets onto it only when this
 * arrives: transport acks say the bytes were RECEIVED, and the joiner acknowledges
 * on receipt, before it has parsed a single one of them. */
static void nrb_resync_send_ack(Uint16 gen)
{
	Uint8 *data = packet_out_temp->data;
	network_prepare(PACKET_RESYNC);
	SDLNet_Write16(gen,                &data[4]);
	SDLNet_Write16((Uint16)NRB_RS_ACK, &data[6]);
	SDLNet_Write16(0,                  &data[8]);
	SDLNet_Write16(0,                  &data[10]);
	network_send(NRB_RS_HDR);
}

/* One host streaming attempt.  1 = streamed, acknowledged, timeline reset;
 * 0 = failed but worth retrying (joiner NAK, stall); -1 = stop trying. */
static int nrb_resync_send_once(void)
{
	++resync_used;
	++qa_resyncs_total;
	++resync_gen;
	nrb_notice_resync();

	const size_t state_sz = rollback_state_size();
	const size_t cap = state_sz + state_sz / 255 + 64;

	Uint8 *raw = malloc(state_sz);
	Uint8 *stream = malloc(NRB_RS_PRE + cap);
	if (raw == NULL || stream == NULL)
	{
		free(raw);
		free(stream);
		return -1;
	}

	/* Canonicalize LIVE state first: the joiner adopts these bytes, so this is
	 * the step that makes the two machines byte-identical afterwards. */
	rollback_wire_canonicalize();

	if (!rollback_wire_export(raw))
	{
		/* A pointer with no relocation, or the export failed its own decode
		 * check.  Not a link problem: retrying cannot help this level. */
		crashlog_note_net("NETWORK RESYNC REFUSED",
		              "wire export failed (unrelocatable pointer or self-check); keeping the divergent game");
		free(raw);
		free(stream);
		return -1;
	}

	const size_t comp_sz = nrb_resync_compress(raw, state_sz, stream + NRB_RS_PRE, cap);
	free(raw);
	if (comp_sz == 0)
	{
		free(stream);
		return -1;
	}

	SDLNet_Write32((Uint32)state_sz, &stream[0]);
	SDLNet_Write32((Uint32)comp_sz,  &stream[4]);
	SDLNet_Write32(nrb_rs_hash(stream + NRB_RS_PRE, comp_sz), &stream[8]);

	const size_t total  = NRB_RS_PRE + comp_sz;
	const Uint32 chunks = (Uint32)((total + NRB_RS_PAYLOAD - 1) / NRB_RS_PAYLOAD);

	const Uint32 wait_start = SDL_GetTicks();
	bool reported = false;
	Uint32 sent = 0;
	Uint32 delivered_at = 0;  /* when the last chunk was acknowledged; 0 = not yet */
	int outcome = 0;  /* 1 done, 2 NAK: retry, 3 give up this level */
	const char *fail = NULL;  /* set where a failure is OURS to report */
	char fail_ctx[160];

	while (outcome == 0)
	{
		char why[112];
		snprintf(why, sizeof(why), "%s: %lu/%lu chunks handed over, backlog %d",
		         delivered_at ? "waiting for the joiner to adopt the resync state"
		                      : "streaming the resync state",
		         (unsigned long)sent, (unsigned long)chunks, network_ack_backlog());
		if (!nrb_resync_pump(wait_start, &reported, why))
		{
			/* Timed out with a live link: the peer is not consuming.  A retry
			 * would just spend another minute on the same silence. */
			fail = "timed out: the joiner stopped acknowledging";
			outcome = 3;
			break;
		}

		if (packet_in[0])
		{
			const Uint16 type = SDLNet_Read16(&packet_in[0]->data[0]);
			if (type == PACKET_RESYNC)
			{
				const Uint16 g      = SDLNet_Read16(&packet_in[0]->data[4]);
				const Uint16 ix     = SDLNet_Read16(&packet_in[0]->data[6]);
				const Uint16 reason = SDLNet_Read16(&packet_in[0]->data[8]);
				network_update();
				if (ix == NRB_RS_ACK && g == resync_gen)
				{
					/* The joiner is on the new timeline.  Only now may we be. */
					outcome = 1;
				}
				else if (ix == NRB_RS_NAK && g == resync_gen)
				{
					if (reason == NRB_NAK_FATAL)
					{
						/* No stream we can build will ever pass its check.  Retire
						 * recovery for the session on THIS side too, so the remaining
						 * attempts are not spent re-sending the same refused bytes at
						 * a peer that has already stopped listening for them. */
						resync_layout_bad = true;
						fail = "the joiner refused permanently (its abort entry names why); "
						       "recovery disabled for this session";
						outcome = 3;
					}
					else
					{
						fail = "the joiner NAKed the stream (its abort entry names why)";
						outcome = 2;
					}
				}
				/* other gens: stale echoes of an abandoned attempt */
			}
			else if (type == PACKET_GAME_QUIT)
			{
				/* The peer left the level under us (the in-game menu's quit
				 * path).  The level is over; recovery is moot. */
				network_update();
				reallyEndLevel = true;
				playerEndLevel = true;
				end_agreed = true;
				if (coopEndlessMode)
					endlessCoopPeerQuitLevel();
				outcome = 3;
			}
			else if (type == PACKET_WAITING || type == PACKET_DETAILS)
			{
				/* The peer is already in a between-levels handshake: it left
				 * the level and nothing over there is listening for chunks.
				 * The packet belongs to the level-end machinery; leave it. */
				outcome = 3;
			}
			else
			{
				network_update();  /* pause/menu presses are lost in the hitch */
			}
			continue;
		}

		/* Keep half the reliable queue free because receipt ACKs precede consumption. */
		while (sent < chunks && network_ack_backlog() < NET_PACKET_QUEUE / 2)
		{
			nrb_resync_send_chunk(stream, total, chunks, sent);
			++sent;
		}

		/* Transport delivery is not adoption; wait for the joiner's explicit resync ACK. */
		if (sent == chunks && network_ack_backlog() == 0)
		{
			if (delivered_at == 0)
				delivered_at = SDL_GetTicks();
			else if (SDL_GetTicks() - delivered_at > NRB_RS_ADOPT_TIME_OUT)
			{
				/* Retry an unanswered delivery without resetting the host timeline. */
				snprintf(fail_ctx, sizeof(fail_ctx),
				         "the joiner took the whole stream (%lu chunks) but never answered "
				         "within %d ms -- not adopted, so our timeline stands",
				         (unsigned long)chunks, NRB_RS_ADOPT_TIME_OUT);
				fail = fail_ctx;
				outcome = 2;
			}
		}
	}

	free(stream);

	if (outcome != 1 && fail != NULL)
	{
		/* Peer-departure exits (quit, between-levels handshake) are not logged
		 * here: the level is over and the recovery simply became moot. */
		char line[224];
		snprintf(line, sizeof(line),
		         "host attempt %lu of %d gen %u failed: %s  (%lu/%lu chunks sent, %lu ms)",
		         (unsigned long)resync_used, NRB_RS_MAX, (unsigned)resync_gen, fail,
		         (unsigned long)sent, (unsigned long)chunks,
		         (unsigned long)(SDL_GetTicks() - wait_start));
		crashlog_netlog_line("NETWORK RESYNC ABORT", line);
	}

	if (outcome == 1)
	{
		char detail[224];
		snprintf(detail, sizeof(detail),
		         "host streamed frame %lu state: %lu bytes (%lu compressed, %lu chunks) "
		         "in %lu ms, attempt %lu of %d",
		         (unsigned long)nrb_cur, (unsigned long)state_sz, (unsigned long)comp_sz,
		         (unsigned long)chunks, (unsigned long)(SDL_GetTicks() - wait_start),
		         (unsigned long)resync_used, NRB_RS_MAX);
		crashlog_note_net("NETWORK RESYNC", detail);

		nrb_reset_core();
		JE_clearSpecialRequests();
		return 1;
	}
	return outcome == 2 ? 0 : -1;
}

/* Host: run attempts until one lands or the level's budget is spent.
 * True = the timeline was reset; the caller presents and starts frame 1. */
static bool nrb_resync_host_run(void)
{
	resync_wanted = false;
	while (resync_used < NRB_RS_MAX)
	{
		const int r = nrb_resync_send_once();
		if (r == 1)
			return true;
		if (r < 0)
			break;  /* hard stop; its own entry named the reason */
	}
	if (resync_used >= NRB_RS_MAX)
		crashlog_netlog_line("NETWORK RESYNC GIVE-UP",
		                     "host: attempt budget spent; playing on with divergent state");
	resync_notice = false;  /* no recovery is coming; stop announcing one */
	return false;
}

/* Joiner: packet_in[0] holds a resync chunk; assemble, validate, adopt.
 * True = adopted and timeline reset. */
static bool nrb_resync_receive(void)
{
	const size_t state_sz = rollback_state_size();

	Uint8 *comp = NULL, *raw = NULL;
	Uint32 comp_total = 0, want_crc = 0;
	Uint32 chunks = 0, next = 0;
	size_t got = 0;
	Uint16 gen = 0, seen_gen = 0;
	bool have_hdr = false;
	bool assembled = false, adopted = false, level_over = false;

	/* Failure cause for the one-line abort entry; every break below names one. */
	const char *abort = "unknown";
	char abort_ctx[160];

	const Uint32 wait_start = SDL_GetTicks();
	Uint32 last_progress = wait_start;
	bool reported = false;

	++resync_used;
	++qa_resyncs_total;
	nrb_notice_resync();

	for (;;)
	{
		if (packet_in[0] == NULL)
		{
			char why[112];
			if (have_hdr)
				snprintf(why, sizeof(why), "receiving the resync state: %lu/%lu chunks (gen %u)",
				         (unsigned long)next, (unsigned long)chunks, (unsigned)gen);
			else
				snprintf(why, sizeof(why), "receiving the resync state: no preamble yet (last gen seen %u)",
				         (unsigned)seen_gen);
			if (!nrb_resync_pump(wait_start, &reported, why))
			{
				abort = "whole-attempt timeout";
				break;
			}
			if (SDL_GetTicks() - last_progress > NRB_RS_PROGRESS_TIME_OUT)
			{
				abort = "no chunk for 8 s (host stream dead, or its attempt budget spent)";
				break;
			}
			continue;
		}

		const Uint16 type = SDLNet_Read16(&packet_in[0]->data[0]);
		if (type != PACKET_RESYNC)
		{
			if (type == PACKET_GAME_QUIT)
			{
				network_update();
				reallyEndLevel = true;
				playerEndLevel = true;
				end_agreed = true;
				if (coopEndlessMode)
					endlessCoopPeerQuitLevel();
				level_over = true;
				break;
			}
			if (type == PACKET_WAITING || type == PACKET_DETAILS)
			{
				/* The peer is already in a between-levels handshake: the stream
				 * is dead, and this packet is a rendezvous release the level
				 * machinery is (or will be) blocked on.  Same rule as the send
				 * side: abort and leave it at the queue head, never consume. */
				abort = "peer already in the between-levels handshake";
				break;
			}
			network_update();
			continue;
		}

		const int    len = packet_in[0]->len;
		const Uint16 g   = SDLNet_Read16(&packet_in[0]->data[4]);
		const Uint16 ix  = SDLNet_Read16(&packet_in[0]->data[6]);
		const Uint16 cn  = SDLNet_Read16(&packet_in[0]->data[8]);
		const Uint16 pl  = SDLNet_Read16(&packet_in[0]->data[10]);
		seen_gen = g;

		if (ix == NRB_RS_NAK || ix == NRB_RS_ACK || len < NRB_RS_HDR + pl || pl > NRB_RS_PAYLOAD)
		{
			network_update();  /* our own NAK/ACK echoed back, or malformed: drop */
			continue;
		}

		if (ix == 0)
		{
			/* First chunk of a (possibly restarted) stream: the preamble. */
			const Uint8 *p = &packet_in[0]->data[NRB_RS_HDR];
			if (pl < NRB_RS_PRE)
			{
				abort = "malformed preamble chunk";
				network_update();
				break;
			}
			const Uint32 their_state = SDLNet_Read32(&p[0]);
			comp_total = SDLNet_Read32(&p[4]);
			want_crc   = SDLNet_Read32(&p[8]);

			/* Layout guard: a different build (or platform) has a different
			 * registry, and its bytes must never be restored here. */
			if (their_state != (Uint32)state_sz ||
			    comp_total == 0 || comp_total > (Uint32)(state_sz + state_sz / 255 + 64))
			{
				/* Not a link failure: no retry against this peer can ever pass
				 * this test, so retire recovery for the session rather than
				 * spend the budget re-streaming the same refused bytes. */
				resync_layout_bad = true;
				snprintf(abort_ctx, sizeof(abort_ctx),
				         "preamble refused: peer state %lu bytes vs ours %lu, compressed %lu "
				         "(mismatched builds, or PC<->console); recovery disabled for this session",
				         (unsigned long)their_state, (unsigned long)state_sz, (unsigned long)comp_total);
				abort = abort_ctx;
				network_update();
				break;
			}

			free(comp);
			comp = malloc(comp_total);
			if (comp == NULL)
			{
				abort = "out of memory for the compressed stream";
				network_update();
				break;
			}
			gen = g;
			chunks = cn;
			have_hdr = true;
			next = 0;

			const size_t body = (size_t)pl - NRB_RS_PRE;
			if (body > comp_total)
			{
				abort = "preamble body overflows the announced compressed size";
				network_update();
				break;
			}
			memcpy(comp, p + NRB_RS_PRE, body);
			got = body;
		}
		else
		{
			if (!have_hdr || g != gen || cn != chunks)
			{
				network_update();  /* tail of an abandoned attempt */
				continue;
			}
			if (ix != next)
			{
				/* The reliable channel delivers in order, so a skipped index
				 * means a chunk was acknowledged into a full inbound queue and
				 * dropped before we started consuming.  Unrecoverable. */
				snprintf(abort_ctx, sizeof(abort_ctx),
				         "chunk index skip: got %u, expected %lu -- a chunk was acked into our full "
				         "inbound queue and dropped (see acked-dropped)",
				         (unsigned)ix, (unsigned long)next);
				abort = abort_ctx;
				network_update();
				break;
			}
			if (got + pl > comp_total)
			{
				abort = "chunk data overflows the announced compressed size";
				network_update();
				break;
			}
			memcpy(comp + got, &packet_in[0]->data[NRB_RS_HDR], pl);
			got += pl;
		}

		network_update();
		last_progress = SDL_GetTicks();
		++next;

		if (next == chunks)
		{
			assembled = true;
			if (got != comp_total)
			{
				snprintf(abort_ctx, sizeof(abort_ctx), "assembled short: %lu of %lu compressed bytes",
				         (unsigned long)got, (unsigned long)comp_total);
				abort = abort_ctx;
			}
			else if (nrb_rs_hash(comp, comp_total) != want_crc)
			{
				abort = "checksum mismatch on the assembled stream";
			}
			else
			{
				raw = malloc(state_sz);
				adopted = raw != NULL &&
				          nrb_resync_expand(comp, comp_total, raw, state_sz) == state_sz &&
				          rollback_wire_adopt(raw);
				if (!adopted)
					abort = "state adopt failed (expand, relocation, or memory)";
			}
			break;
		}
	}

	free(comp);
	free(raw);

	if (adopted)
	{
		resync_gen = gen;

		/* Before the reset, and before we present: the host is blocked waiting for
		 * exactly this, and it is the only evidence it will ever get that the bytes
		 * were not merely delivered but taken. */
		nrb_resync_send_ack(gen);

		char detail[192];
		snprintf(detail, sizeof(detail),
		         "joiner adopted the host state: gen %u, %lu bytes, %lu ms, attempt %lu of %d",
		         (unsigned)gen, (unsigned long)state_sz,
		         (unsigned long)(SDL_GetTicks() - wait_start),
		         (unsigned long)resync_used, NRB_RS_MAX);
		crashlog_note_net("NETWORK RESYNC", detail);

		nrb_reset_core();
		JE_clearSpecialRequests();
		return true;
	}

	if (!level_over)
	{
		/* One line per failed attempt, so the log names the culprit even when the
		 * level plays on: which attempt, why it died, and how far it got. */
		char line[288];
		snprintf(line, sizeof(line),
		         "joiner attempt %lu of %d failed: %s  (gen %u, %lu/%lu chunks, %lu ms)%s",
		         (unsigned long)resync_used, NRB_RS_MAX, abort,
		         (unsigned)(have_hdr ? gen : seen_gen),
		         (unsigned long)next, (unsigned long)chunks,
		         (unsigned long)(SDL_GetTicks() - wait_start),
		         resync_used > NRB_RS_MAX ? "   [over budget: started by stray chunks]" : "");
		crashlog_netlog_line("NETWORK RESYNC ABORT", line);

		/* The NAK is the only "I did not adopt" the host ever hears; transport
		 * acks say the bytes arrived, not that they were taken.  The reason is what
		 * separates "try again" from "no stream you can build will ever pass", so a
		 * layout refusal retires recovery on the host too instead of burning the
		 * remaining attempts on identical bytes we have already stopped listening for. */
		nrb_resync_send_nak(have_hdr ? gen : seen_gen,
		                    resync_layout_bad ? NRB_NAK_FATAL : NRB_NAK_RETRY);

		/* Spent: the host has no attempt left to answer that NAK with. */
		if (resync_used >= NRB_RS_MAX || resync_layout_bad)
			resync_notice = false;

		/* A refused stream leaves both peers on the old timeline; halt only after exhausting retries. */
		if (assembled && resync_used >= NRB_RS_MAX)
			network_tyrian_halt(7, false);
	}
	return false;
}

/* Dispatch the queued resync packet. True means adoption reset the timeline. */
static bool nrb_resync_dispatch(void)
{
	if (packet_in[0] == NULL || SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_RESYNC)
		return false;

	if (!nrb_session_recovery())
	{
		network_update();  /* stray: keep the reliable queue's head moving */
		return false;
	}

	/* Drop leftover chunks once the attempt budget or session layout has made adoption impossible. */
	if (!network_is_host && (resync_used >= NRB_RS_MAX || resync_layout_bad))
	{
		/* Reject a permanently incompatible restart at chunk 0; ignore its remaining chunks. */
		if (resync_layout_bad && SDLNet_Read16(&packet_in[0]->data[6]) == 0)
			nrb_resync_send_nak(SDLNet_Read16(&packet_in[0]->data[4]), NRB_NAK_FATAL);
		network_update();
		return false;
	}

	if (network_is_host)
	{
		const Uint16 g      = SDLNet_Read16(&packet_in[0]->data[4]);
		const Uint16 ix     = SDLNet_Read16(&packet_in[0]->data[6]);
		const Uint16 reason = SDLNet_Read16(&packet_in[0]->data[8]);
		network_update();
		if (ix == NRB_RS_NAK && g == resync_gen)
		{
			if (reason == NRB_NAK_FATAL)
			{
				/* Arrived outside the streaming loop; a late refusal, or the
				 * joiner answering a stream restart it had already given up on. */
				if (!resync_layout_bad)
				{
					resync_layout_bad = true;
					crashlog_netlog_line("NETWORK RESYNC GIVE-UP",
					                     "host: joiner refused permanently (layout/build mismatch); "
					                     "recovery disabled for this session");
				}
				return false;
			}
			if (resync_used < NRB_RS_MAX)
				return nrb_resync_host_run();
			/* Without this line, the joiner's next attempt stalls against total
			 * silence and its log never says the host had already given up. */
			crashlog_netlog_line("NETWORK RESYNC GIVE-UP",
			                     "host: joiner NAK ignored, attempt budget already spent this level");
		}
		return false;
	}

	return nrb_resync_receive();
}

/* Verdicts for the scheduled-menu wait below. */
enum
{
	NRB_MENU_OPEN,     /* frame confirmed on both machines: open the menu      */
	NRB_MENU_RESIM,    /* rollback first; the caller re-enters after it        */
	NRB_MENU_ABANDON,  /* a desync recovery reset the timeline: no menu frame  */
};

/* The scheduled menu frame has been reached; wait until it is confirmed, at
 * which point both machines hold identical state and the menu may open. */
static int nrb_menu_frame_ready(Uint32 *resim_from)
{
	const Uint32 wait_start = SDL_GetTicks();
	const Uint32 newest_at_start = remote_newest;
	bool stall_reported = false;

	while (verified_upto < nrb_cur)
	{
		if (nrb_stall_pump(wait_start, &stall_reported, "waiting to open the in-game menu"))
			return NRB_MENU_ABANDON;

		const Uint32 K = nrb_scan_mispredict();
		if (K != 0)
		{
			*resim_from = K;
			return NRB_MENU_RESIM;
		}

		/* A peer idle this long has left through an unmodeled route. Stop waiting so
		 * the local player retains access to the menu and quit path. */
		if (remote_newest == newest_at_start && SDL_GetTicks() - wait_start > 8000)
		{
			char detail[192];
			snprintf(detail, sizeof(detail),
			         "peer stopped simulating at frame %lu while we waited to open the menu "
			         "at frame %lu (player %u)\n  opening it unsynchronised",
			         (unsigned long)remote_newest, (unsigned long)nrb_cur, thisPlayerNum);
			crashlog_note_net("ROLLBACK MENU TIMEOUT", detail);
			break;
		}
	}

	return NRB_MENU_OPEN;
}

/* Give back time when we are consistently ahead of the peer, GGPO-style: each
 * side reports its perceived frame advantage; the side that is ahead sleeps
 * one tick occasionally until the difference closes. */
static void nrb_timesync(void)
{
	const float diff = adv_ema - (float)their_adv_x8 / 8.0f;
	if (diff >= 2.0f && nrb_cur - last_throttle_frame >= 35)
	{
		last_throttle_frame = nrb_cur;
		Uint32 ms = (Uint32)((float)frameCountMax * get_delay_period());
		if (ms > 100)
			ms = 100;
		SDL_Delay(ms);
	}
}

/* Rollback driver. */

/* Bounded wire-test run: report what the netcode did and leave the process. The peer already
 * holds the redundant input stream for the frames it still owes itself, so a short drain is
 * enough for it to finish on its own. */
static void nrb_qa_gameplay_verdict(void)
{
	int rc;
	if (qa_net_corrupt_frame > 0)
	{
		/* A recovery must have run and the timeline being flown at the limit must be clean
		 * again. Only the host requires its own detection: it is the side that initiates, so
		 * a recovery on the host implies one, while the joiner can adopt the stream before
		 * its own compare ever fires. Totals are session-scoped because both a recovery and
		 * a level restart wipe the per-level counters. */
		const bool detected = (thisPlayerNum == networkHostPlayerNum)
		                    ? qa_desyncs_total >= 1 : true;
		rc = (detected && qa_resyncs_total >= 1 && canary_mismatches == 0) ? 0 : 1;
	}
	else
	{
		/* No divergence allowed, and the run must have actually exercised a rollback, or the
		 * proxy's faults never reached the prediction path and this proved nothing. */
		rc = (qa_desyncs_total == 0 && stat_deepest >= 1) ? 0 : 1;
	}

	// The save/resume scenario's first stage: bank the LAST LEVEL record the resume will load.
	if (qa_net_save_exit && rc == 0)
		JE_saveGame(22, "LAST LEVEL    ");

	printf("NET GAMEPLAY %s player=%u frames=%lu epoch=%u depth=%lu desyncs=%lu resyncs=%lu\n",
	       rc == 0 ? "PASS" : "FAIL", thisPlayerNum, (unsigned long)nrb_cur,
	       (unsigned)nrb_epoch, (unsigned long)stat_deepest,
	       (unsigned long)qa_desyncs_total, (unsigned long)qa_resyncs_total);
	fflush(stdout);

	const Uint32 start = SDL_GetTicks();
	while (SDL_GetTicks() - start < 1200)
	{
		watchdog_heartbeat();
		network_check();
		SDL_Delay(1);
	}
	exit(rc);
}

NrbStep nrb_driver(void)
{
	if (!nrb_active())
		return NRB_STEP_PRESENT;

	if (qa_net_gameplay_ticks > 0 && !resim_active && nrb_cur > qa_net_gameplay_ticks)
		nrb_qa_gameplay_verdict();

	if (resim_active)
	{
		/* A re-simulation pass for nrb_cur just completed. */
		nrb_stamp_canary(nrb_cur);

		if (nrb_cur < resim_target)
		{
			++nrb_cur;
			rollback_resim = true;
			rollback_resim_silent = (nrb_cur < resim_target);
			return NRB_STEP_RESIM;
		}

		/* Corrected timeline has caught up; the pass that just ran recorded
		 * rendering and falls through to present. */
		resim_active = false;
		rollback_resim = false;
		rollback_resim_silent = false;
		/* Input for this frame was already sent by the original pass. */
	}
	else
	{
		/* Normal pass of frame nrb_cur: fold the tick's request flags into the
		 * local record, publish it, stamp the canary. */
		NrbSlot *s = &local_hist[nrb_cur % NRB_HIST];
		if (s->tag != nrb_cur)
		{
			/* The tick recorded no tuple; the whole input path is skipped
			 * during the level-end fade and while our ship's death sequence
			 * runs.  Stamp a neutral record so the wire always carries defined
			 * bytes and the peer's ack frontier keeps moving. */
			memset(&s->in, 0, sizeof(s->in));
			s->in.x = (Sint16)player[thisPlayerNum - 1].x;
			s->in.y = (Sint16)player[thisPlayerNum - 1].y;
			{
				const int vx = player[thisPlayerNum - 1].x_velocity;
				const int vy = player[thisPlayerNum - 1].y_velocity;
				s->in.velX = (Sint16)(vx > 127 ? 127 : (vx < -127 ? -127 : vx));
				s->in.velY = (Sint16)(vy > 127 ? 127 : (vy < -127 ? -127 : vy));
			}
			if (thisPlayerNum == networkHostPlayerNum)
				s->in.difficulty = (Uint8)difficultyLevel;
			s->tag = nrb_cur;
		}
		// RB_REQ_PAUSE is never sent: online cannot pause. The bit stays reserved.
		s->in.buttons |= (inGameMenuRequest ? RB_REQ_MENU      : 0) |
		                 (skipLevelRequest  ? RB_REQ_SKIPLEVEL : 0) |
		                 (nortShipRequest   ? RB_REQ_NORTSHIP  : 0);
		nrb_send_input();
		nrb_stamp_canary(nrb_cur);
	}

	JE_clearSpecialRequests();

	/* Ingest whatever has arrived and correct the timeline if needed.
	 * network_check() drains the socket itself (see NET_DRAIN_MAX). */
	network_check();

	/* The recovery is over once the peer turns up on the timeline it handed us:
	 * our epoch has moved on and the peer's first frame there has landed. */
	if (resync_notice && nrb_epoch != resync_notice_epoch && remote_newest > 0)
		resync_notice = false;

	{
		const Uint32 K = nrb_scan_mispredict();
		if (K != 0)
			return nrb_begin_resim(K);
	}

	nrb_compare_canary();
	nrb_process_requests();

	/* Desync recovery rendezvous.  Either path that fires here leaves the frame
	 * machinery reset; this pass presents, the next tick simulates the fresh
	 * timeline's frame 1. Nothing below this point (menus, gates, prediction bounds)
	 * may run after one. */
	if (nrb_resync_dispatch())
		return NRB_STEP_PRESENT;
	if (resync_wanted)
	{
		resync_wanted = false;
		if (network_is_host && nrb_resync_host_run())
			return NRB_STEP_PRESENT;
	}

	/* Scheduled menu rendezvous.  req_at is always strictly ahead of nrb_cur when
	 * it is set (see NRB_REQ_LEAD), so this fires on exactly that frame, on both
	 * machines, with the same simulation behind it. */
	if (req_at != 0 && nrb_cur >= req_at)
	{
		Uint32 K = 0;
		switch (nrb_menu_frame_ready(&K))
		{
		case NRB_MENU_RESIM:
			return nrb_begin_resim(K);
		case NRB_MENU_ABANDON:
			return NRB_STEP_PRESENT;
		default:
			break;
		}

		const bool local_menu = req_local_menu;
		req_at = 0;
		req_local_menu = false;

		yourInGameMenuRequest = local_menu;
		JE_doInGameSetup();
		yourInGameMenuRequest = false;
		if (haltGame)
			reallyEndLevel = true;

		/* The synchronized in-game menu confirms Quit to Outpost out of band. */
		if (reallyEndLevel)
			end_agreed = true;
	}

	/* Confirm predicted level ends; out-of-band menu agreement needs no extra frame. */
	if (reallyEndLevel && !end_agreed)
	{
		const Uint32 wait_start = SDL_GetTicks();
		const Uint32 newest_at_start = remote_newest;
		bool stall_reported = false;

		while (verified_upto < nrb_cur && reallyEndLevel)
		{
			if (nrb_stall_pump(wait_start, &stall_reported, "waiting to confirm level end"))
				return NRB_STEP_PRESENT;  /* recovery reset the timeline; the end it
				                             was confirming no longer exists */
			const Uint32 K = nrb_scan_mispredict();
			if (K != 0)
				return nrb_begin_resim(K);

			/* If the peer stops producing frames, rely on the next shop rendezvous instead of wedging. */
			if (remote_newest == newest_at_start && SDL_GetTicks() - wait_start > 8000)
			{
				char detail[192];
				snprintf(detail, sizeof(detail),
				         "peer stopped simulating at frame %lu while we waited to confirm "
				         "frame %lu (player %u)\n  ending the level unconfirmed",
				         (unsigned long)remote_newest, (unsigned long)nrb_cur, thisPlayerNum);
				crashlog_note_net("ROLLBACK LEVEL-END TIMEOUT", detail);
				break;
			}
		}
		return NRB_STEP_PRESENT;  /* confirmed: let the exit happen */
	}
	if (reallyEndLevel)
		return NRB_STEP_PRESENT;

	/* Bound prediction depth and outstanding unacked history before advancing.
	 * SIGNED differences: after a long modal (options menu) the peer may
	 * legitimately be AHEAD of us; remote_contig/peer_acked beyond our own
	 * frame counter.  The old unsigned subtraction underflowed to a huge value
	 * there and stalled both machines into the disconnect timeout. */
	{
		const Uint32 wait_start = SDL_GetTicks();
		bool stall_reported = false;

		while ((Sint64)(nrb_cur + 1) - (Sint64)remote_contig > ROLLBACK_MAX_PREDICT ||
		       (Sint64)(nrb_cur + 1) - (Sint64)peer_acked > NRB_REDUNDANCY - 1 ||
		       /* Start-of-level barrier: until the peer's first input arrives
		        * (it may still be in the shop), hold at frame 3 instead of
		        * running MAX_PREDICT frames of pure prediction; the peer's
		        * ship sitting frozen at spawn for 10 frames looked broken and
		        * guaranteed an opening rollback burst. */
		       (remote_newest == 0 && nrb_cur >= 3))
		{
			if (nrb_stall_pump(wait_start, &stall_reported, "peer too far behind"))
				return NRB_STEP_PRESENT;
			const Uint32 K = nrb_scan_mispredict();
			if (K != 0)
				return nrb_begin_resim(K);
		}
	}

	nrb_timesync();

	if (stat_rollbacks > 0 && nrb_cur % 350 == 0)
	{
		fprintf(stderr, "rollback: frame %lu, %lu rollbacks, %lu resim frames, deepest %lu\n",
		        (unsigned long)nrb_cur, (unsigned long)stat_rollbacks,
		        (unsigned long)stat_resim_frames, (unsigned long)stat_deepest);
	}

	++nrb_cur;
	return NRB_STEP_PRESENT;
}

/* Crash-log rollback section.
 *
 * Appended under the crash log's Network section.  Reads only statics; safe
 * from a fault handler or the watchdog thread.
 */
void nrb_write_diagnostics(FILE *f)
{
	if (f == NULL)
		return;

	fprintf(f, "  Rollback:\n");
	fprintf(f, "    frames:     current=%lu verified=%lu remote_contig=%lu remote_newest=%lu peer_acked=%lu\n",
	        (unsigned long)nrb_cur, (unsigned long)verified_upto, (unsigned long)remote_contig,
	        (unsigned long)remote_newest, (unsigned long)peer_acked);
	fprintf(f, "    predict:    depth=%lu (cap %d)   adv_ema=%.2f their_adv=%.2f   epoch=%u\n",
	        (unsigned long)((nrb_cur > remote_newest) ? nrb_cur - remote_newest : 0),
	        ROLLBACK_MAX_PREDICT, (double)adv_ema, (double)their_adv_x8 / 8.0, (unsigned)nrb_epoch);
	fprintf(f, "    rollbacks:  %lu this level (deepest %lu, resim frames %lu)%s%s\n",
	        (unsigned long)stat_rollbacks, (unsigned long)stat_deepest,
	        (unsigned long)stat_resim_frames,
	        resim_active ? "   RESIM ACTIVE" : "",
	        resim_livelock_reported ? "   LIVELOCK reported" : "");
	fprintf(f, "    canary:     mismatches=%lu checked_upto=%lu pending=%d%s\n",
	        (unsigned long)canary_mismatches, (unsigned long)canary_checked_upto, peer_pend_n,
	        canary_reported ? "   (desync reported this level)" : "");
	fprintf(f, "    recovery:   attempts=%lu/%d gen=%u%s\n",
	        (unsigned long)resync_used, NRB_RS_MAX, (unsigned)resync_gen,
	        resync_wanted ? "   RESYNC PENDING" : "");
	fprintf(f, "    input pkts: ok=%lu   refused: malformed=%lu window=%lu epoch=%lu   stalls=%lu\n",
	        (unsigned long)nrb_diag.pkt_in, (unsigned long)nrb_diag.refused_malformed,
	        (unsigned long)nrb_diag.refused_window, (unsigned long)nrb_diag.refused_epoch,
	        (unsigned long)nrb_diag.stalls);
	if (req_at != 0)
		fprintf(f, "    menu:       scheduled for frame %lu%s\n",
		        (unsigned long)req_at, req_local_menu ? " (our press)" : "");
	if (end_agreed)
		fprintf(f, "    level end agreed out of band\n");
}

#else /* !WITH_NETWORK */

Uint32 nrb_frame(void) { return 0; }
void nrb_stats(Uint32 *predict, Uint32 *depth, Uint32 *rate, Uint32 *desyncs)
{
	*predict = *depth = *rate = *desyncs = 0;
}
void nrb_level_reset(void) {}
void nrb_frame_begin(void) {}
void nrb_record_local(const RbInput *in) { (void)in; }
void nrb_get_local(Uint32 frame, RbInput *out) { (void)frame; memset(out, 0, sizeof(*out)); }
void nrb_get_remote(Uint32 frame, RbInput *out) { (void)frame; memset(out, 0, sizeof(*out)); }
NrbStep nrb_driver(void) { return NRB_STEP_PRESENT; }

#endif /* WITH_NETWORK */
