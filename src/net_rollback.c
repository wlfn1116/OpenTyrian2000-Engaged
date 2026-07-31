/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback netcode implementation.  See net_rollback.h for the model.
 *
 * Frame numbering: sim frames are 1-based within a level; frame 0 means
 * "before the first tick".  All ring indexing is frame % NRB_HIST with a tag
 * word, so stale slots can never masquerade as current ones.
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
#include "keyboard.h"
#include "mainint.h"
#include "network.h"
#include "nortsong.h"
#include "player.h"
#include "render_list.h"
#include "varz.h"

#include <stdio.h>
#include <string.h>

bool net_rollback = true;

static bool session_mode = false;

void nrb_set_session_mode(bool enabled)
{
	session_mode = enabled;
}

bool nrb_session_mode(void)
{
	return session_mode;
}

bool nrb_active(void)
{
	return isNetworkGame && session_mode;
}

#ifdef WITH_NETWORK

#include "SDL_net.h"

#define NRB_HIST        64            /* input/canary history depth           */
#define NRB_REDUNDANCY  16            /* newest frames repeated per packet    */
#define NRB_TIME_OUT    16000         /* ms without progress before giving up */
#define NRB_REC_BYTES   12            /* wire size of one input record        */
#define NRB_HDR_BYTES   32            /* wire size of the packet header       */

/* Wire-relevant tuple bits: the four buttons, the four requests, the analog
 * link flag.  The RB_EV_* bits are self-test-local and never leave a machine. */
#define NRB_WIRE_BUTTONS 0x01FFu
/* Bits a PREDICTED tuple may carry: held buttons + analog flag; one-shot
 * request pulses must never be predicted into existence. */
#define NRB_PREDICT_BUTTONS (RB_BTN_FIRE | RB_BTN_LSIDEKICK | RB_BTN_RSIDEKICK | \
                             RB_BTN_CHANGEFIRE | RB_LINK_ANALOG)

/* How much of a remote frame's tuple the simulation consumed.  The level-end
 * fade and a dead ship skip the movement/apply path entirely, but the frame's
 * REQUEST bits are still consumed at the next frame's begin -- so verification
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
static Uint32 peer_acked;               /* peer holds all our frames <= this  */
static Uint32 remote_contig;            /* we hold all peer frames <= this    */
static Uint32 remote_newest;            /* newest peer frame seen (timesync)  */

static bool   resim_active;
static Uint32 resim_target;

static RbInput remote_seed;             /* prediction anchor before any data  */

/* Desync canary: per-frame sim summary, final once the frame is verified. */
typedef struct { Uint32 tag, rand, ph, eh; } NrbCanary;
static NrbCanary canary[NRB_HIST];
static NrbCanary peer_canary;           /* newest received, compared once     */
static bool peer_canary_pending;
static int  canary_reported_level = -1;

/* Timesync: rolling frame-advantage estimate, exchanged both ways. */
static float  adv_ema;
static Sint16 their_adv_x8;
static Uint32 last_throttle_frame;
static Uint32 last_resend_tick;

/* Diagnostics. */
static Uint32 stat_rollbacks, stat_resim_frames, stat_deepest;

Uint32 nrb_frame(void)
{
	return nrb_cur;
}

void nrb_level_reset(void)
{
	memset(local_hist, 0, sizeof(local_hist));
	memset(remote_hist, 0, sizeof(remote_hist));
	memset(remote_used, 0, sizeof(remote_used));
	memset(canary, 0, sizeof(canary));
	peer_canary_pending = false;
	canary_reported_level = -1;

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

/* --- Frame begin: snapshot, then apply the previous frame's sim requests ------ */

void nrb_frame_begin(void)
{
	if (!nrb_active())
		return;

	/* Snapshot BEFORE the request application: the application is part of this
	 * frame's simulation and must repeat on a re-simulation pass. */
	rollback_snapshot(nrb_cur);

	if (nrb_cur >= 2)
	{
		const Uint32 pf = nrb_cur - 1;
		Uint16 bits = 0;

		if (local_hist[pf % NRB_HIST].tag == pf)
			bits |= local_hist[pf % NRB_HIST].in.buttons;

		/* Remote side: consume pf's request bits.  If the movement path already
		 * consumed the whole tuple this frame, use exactly that view (detector
		 * coherence).  Otherwise this IS the consumption -- truth if it has
		 * arrived, nothing if not -- and it is stamped so the detector can
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

/* --- Input history ------------------------------------------------------------ */

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

/* Compare only wire-carried fields; local-only tuple extras don't matter. */
static bool nrb_wire_differs(const RbInput *a, const RbInput *b)
{
	return a->x != b->x || a->y != b->y ||
	       a->accelX != b->accelX || a->accelY != b->accelY ||
	       ((a->buttons ^ b->buttons) & NRB_WIRE_BUTTONS) != 0 ||
	       a->linkAngle != b->linkAngle ||
	       a->difficulty != b->difficulty;
}

/* Advance verified_upto over frames whose used inputs match the arrived truth;
 * return the first frame where they differ (0 = none). */
static Uint32 nrb_scan_mispredict(void)
{
	while (verified_upto < nrb_cur)
	{
		const Uint32 f = verified_upto + 1;
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
			if (((remote_used[f % NRB_HIST].in.buttons ^
			      remote_hist[f % NRB_HIST].in.buttons) & NRB_SIM_REQS) != 0)
				return f;
		}
		else if (nrb_wire_differs(&remote_used[f % NRB_HIST].in, &remote_hist[f % NRB_HIST].in))
		{
			return f;
		}
		++verified_upto;
	}
	return 0;
}

/* --- Wire --------------------------------------------------------------------- */

static void nrb_send_input(void)
{
	Uint8 *data = packet_out_temp->data;

	const Uint32 F = nrb_cur;
	Uint32 from = peer_acked + 1;
	if (from < 1)
		from = 1;
	if (F + 1 - from > NRB_REDUNDANCY)
		from = F + 1 - NRB_REDUNDANCY;
	const int count = (int)(F - from + 1);

	SDLNet_Write16(PACKET_INPUT,   &data[0]);
	SDLNet_Write16((Uint16)count,  &data[2]);
	SDLNet_Write32(F,              &data[4]);
	SDLNet_Write32(remote_contig,  &data[8]);
	SDLNet_Write16((Uint16)(Sint16)(adv_ema * 8.0f), &data[12]);
	SDLNet_Write16(0,              &data[14]);

	/* Canary for the newest frame we know is final on our side. */
	if (verified_upto > 0 && canary[verified_upto % NRB_HIST].tag == verified_upto)
	{
		const NrbCanary *c = &canary[verified_upto % NRB_HIST];
		SDLNet_Write32(c->tag,  &data[16]);
		SDLNet_Write32(c->rand, &data[20]);
		SDLNet_Write32(c->ph,   &data[24]);
		SDLNet_Write32(c->eh,   &data[28]);
	}
	else
	{
		memset(&data[16], 0, 16);
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
	}

	network_send_unacked(off);
	last_resend_tick = SDL_GetTicks();
}

void nrb_handle_packet(const Uint8 *data, int len)
{
	if (len < NRB_HDR_BYTES)
		return;

	const int    count = SDLNet_Read16(&data[2]);
	const Uint32 F     = SDLNet_Read32(&data[4]);
	const Uint32 ack   = SDLNet_Read32(&data[8]);

	if (count < 0 || count > NRB_REDUNDANCY || len < NRB_HDR_BYTES + count * NRB_REC_BYTES)
		return;
	if (F < (Uint32)count)
		return;
	/* In-flight datagrams from the previous level carry frame numbers far past
	 * this level's counter; letting them in would poison the ack frontier and
	 * the timesync estimate.  A real peer can only be MAX_PREDICT ahead. */
	if (F > nrb_cur + NRB_HIST)
		return;

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
		const Uint32 cf = SDLNet_Read32(&data[16]);
		if (cf != 0)
		{
			peer_canary.tag  = cf;
			peer_canary.rand = SDLNet_Read32(&data[20]);
			peer_canary.ph   = SDLNet_Read32(&data[24]);
			peer_canary.eh   = SDLNet_Read32(&data[28]);
			peer_canary_pending = true;
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
		s->tag = f;
	}

	while (remote_hist[(remote_contig + 1) % NRB_HIST].tag == remote_contig + 1)
		++remote_contig;
}

/* --- Canary ------------------------------------------------------------------- */

static void nrb_stamp_canary(Uint32 frame)
{
	NrbCanary *c = &canary[frame % NRB_HIST];
	c->tag = frame;
	network_sim_state(&c->rand, &c->ph, &c->eh);
}

static void nrb_compare_canary(void)
{
	if (!peer_canary_pending)
		return;
	/* Compare only once OUR copy of that frame is final too. */
	if (peer_canary.tag > verified_upto)
		return;
	peer_canary_pending = false;

	const NrbCanary *ours = &canary[peer_canary.tag % NRB_HIST];
	if (ours->tag != peer_canary.tag)
		return;  /* out of window; too old to compare */

	if (ours->rand != peer_canary.rand || ours->ph != peer_canary.ph || ours->eh != peer_canary.eh)
	{
		if (canary_reported_level != curLoc)
		{
			canary_reported_level = curLoc;

			char detail[512];
			snprintf(detail, sizeof(detail),
			         "rollback frame %lu (level %d, player %u)\n"
			         "  rand draws : local %lu  remote %lu  %s\n"
			         "  players    : local %08x  remote %08x  %s\n"
			         "  enemies    : local %08x  remote %08x  %s",
			         (unsigned long)peer_canary.tag, (int)curLoc, thisPlayerNum,
			         (unsigned long)ours->rand, (unsigned long)peer_canary.rand,
			         ours->rand == peer_canary.rand ? "ok" : "DIFFERS",
			         (unsigned)ours->ph, (unsigned)peer_canary.ph,
			         ours->ph == peer_canary.ph ? "ok" : "DIFFERS",
			         (unsigned)ours->eh, (unsigned)peer_canary.eh,
			         ours->eh == peer_canary.eh ? "ok" : "DIFFERS");
			crashlog_note("NETWORK DESYNC (rollback)", detail);

			if (networkDesyncHalt)
				network_tyrian_halt(7, false);
		}
	}
}

/* --- Confirmed-frame request processing (pause / in-game menu) ----------------- */

static void nrb_process_requests(void)
{
	while (req_done < verified_upto)
	{
		const Uint32 f = ++req_done;
		const Uint16 lbits = (local_hist[f % NRB_HIST].tag == f) ? local_hist[f % NRB_HIST].in.buttons : 0;
		const Uint16 rbits = (remote_hist[f % NRB_HIST].tag == f) ? remote_hist[f % NRB_HIST].in.buttons : 0;
		const Uint16 bits = lbits | rbits;

		if (bits & RB_REQ_PAUSE)
			JE_pauseGame();

		if (bits & RB_REQ_MENU)
		{
			yourInGameMenuRequest = (lbits & RB_REQ_MENU) != 0;
			JE_doInGameSetup();
			yourInGameMenuRequest = false;
			if (haltGame)
				reallyEndLevel = true;
		}
	}
}

/* --- Rollback trigger ---------------------------------------------------------- */

/* Restore to frame K and arrange for the level loop to re-simulate K..target. */
static NrbStep nrb_begin_resim(Uint32 K)
{
	if (!rollback_restore(K))
	{
		/* The snapshot for a frame inside the prediction window is missing --
		 * that is a hard bug, and continuing would desync silently. */
		char detail[128];
		snprintf(detail, sizeof(detail),
		         "no snapshot for frame %lu (current %lu, verified %lu)",
		         (unsigned long)K, (unsigned long)nrb_cur, (unsigned long)verified_upto);
		crashlog_note("ROLLBACK SNAPSHOT MISSING", detail);
		network_tyrian_halt(7, false);
	}

	rl_abort_record();  /* drop the aborted pass's partial render recording */

	++stat_rollbacks;
	stat_resim_frames += nrb_cur - K + 1;
	if (nrb_cur - K + 1 > stat_deepest)
		stat_deepest = nrb_cur - K + 1;

	resim_active = true;
	resim_target = nrb_cur;
	nrb_cur = K;
	rollback_resim = true;
	rollback_resim_silent = (K < resim_target);
	return NRB_STEP_RESIM;
}

/* Pump the world while stalled: OS events, inbound packets, periodic input
 * resend (the peer may be waiting on a lost packet), bounded by timeout. */
static void nrb_stall_pump(Uint32 wait_start, bool *stall_reported, const char *why)
{
	service_SDL_events(false);

	if (SDL_GetTicks() - last_resend_tick > 100)
		nrb_send_input();

	if (network_check() == 0)
		SDL_Delay(1);

	const Uint32 waited = SDL_GetTicks() - wait_start;

	if (!*stall_reported && waited > 3000)
	{
		*stall_reported = true;
		char detail[256];
		snprintf(detail, sizeof(detail),
		         "%s\n"
		         "  player   : %u\n"
		         "  frame    : %lu   verified: %lu   peer ack: %lu   remote newest: %lu",
		         why, thisPlayerNum,
		         (unsigned long)nrb_cur, (unsigned long)verified_upto,
		         (unsigned long)peer_acked, (unsigned long)remote_newest);
		crashlog_note("ROLLBACK STALL", detail);
	}

	if (waited > NRB_TIME_OUT)
	{
		fprintf(stderr, "error: no usable input from the other player for %u ms\n",
		        (unsigned)waited);
		network_tyrian_halt(2, false);
	}
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

/* --- The driver ---------------------------------------------------------------- */

NrbStep nrb_driver(void)
{
	if (!nrb_active())
		return NRB_STEP_PRESENT;

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
			/* The tick recorded no tuple -- the whole input path is skipped
			 * during the level-end fade and while our ship's death sequence
			 * runs.  Stamp a neutral record so the wire always carries defined
			 * bytes and the peer's ack frontier keeps moving. */
			memset(&s->in, 0, sizeof(s->in));
			s->in.x = (Sint16)player[thisPlayerNum - 1].x;
			s->in.y = (Sint16)player[thisPlayerNum - 1].y;
			if (thisPlayerNum == 1)
				s->in.difficulty = (Uint8)difficultyLevel;
			s->tag = nrb_cur;
		}
		s->in.buttons |= (pauseRequest      ? RB_REQ_PAUSE     : 0) |
		                 (inGameMenuRequest ? RB_REQ_MENU      : 0) |
		                 (skipLevelRequest  ? RB_REQ_SKIPLEVEL : 0) |
		                 (nortShipRequest   ? RB_REQ_NORTSHIP  : 0);
		nrb_send_input();
		nrb_stamp_canary(nrb_cur);
	}

	JE_clearSpecialRequests();

	/* Ingest whatever has arrived and correct the timeline if needed. */
	network_check();
	{
		const Uint32 K = nrb_scan_mispredict();
		if (K != 0)
			return nrb_begin_resim(K);
	}

	nrb_compare_canary();
	nrb_process_requests();

	/* Irreversible transition gate: the level may only actually end once the
	 * frame that ended it is confirmed -- a predicted "end" can be rolled back
	 * (the restore rewinds reallyEndLevel itself). */
	if (reallyEndLevel)
	{
		const Uint32 wait_start = SDL_GetTicks();
		bool stall_reported = false;

		while (verified_upto < nrb_cur && reallyEndLevel)
		{
			nrb_stall_pump(wait_start, &stall_reported, "waiting to confirm level end");
			const Uint32 K = nrb_scan_mispredict();
			if (K != 0)
				return nrb_begin_resim(K);
		}
		return NRB_STEP_PRESENT;  /* confirmed: let the exit happen */
	}

	/* Bound prediction depth and outstanding unacked history before advancing. */
	{
		const Uint32 wait_start = SDL_GetTicks();
		bool stall_reported = false;

		while ((nrb_cur + 1) - remote_contig > ROLLBACK_MAX_PREDICT ||
		       (nrb_cur + 1) - peer_acked > NRB_REDUNDANCY - 1)
		{
			nrb_stall_pump(wait_start, &stall_reported, "peer too far behind");
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

#else /* !WITH_NETWORK */

Uint32 nrb_frame(void) { return 0; }
void nrb_level_reset(void) {}
void nrb_frame_begin(void) {}
void nrb_record_local(const RbInput *in) { (void)in; }
void nrb_get_local(Uint32 frame, RbInput *out) { (void)frame; memset(out, 0, sizeof(*out)); }
void nrb_get_remote(Uint32 frame, RbInput *out) { (void)frame; memset(out, 0, sizeof(*out)); }
NrbStep nrb_driver(void) { return NRB_STEP_PRESENT; }

#endif /* WITH_NETWORK */
