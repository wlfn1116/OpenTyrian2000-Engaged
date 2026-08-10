/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback netcode for the online Destruct minigame.  See destruct_rollback.h for the interface
 * and doc/notes.md for the invariants the battle simulation has to hold up.
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
#include "destruct_rollback.h"

#include "crashlog.h"
#include "joystick.h"
#include "keyboard.h"
#include "mainint.h"
#include "mtrand.h"
#include "network.h"
#include "rollback.h"
#include "video.h"

#include <stdlib.h>
#include <string.h>

#ifdef WITH_NETWORK

/* The battle simulation, described by destruct.c at drb_session_begin. */
static size_t   state_bytes;
static void   (*state_save)(void *dst);
static void   (*state_restore)(const void *src);
static Uint32 (*state_hash)(void);
static Uint8    held_actions;

static bool session_active;

/* Snapshot ring, keyed by frame.  Sized from the shared prediction bound, so a stall holds the
 * timeline rather than predicting past the deepest frame the ring can restore. */
static Uint8 *ring[ROLLBACK_RING];
static Uint32 ring_frame[ROLLBACK_RING];
static bool   ring_valid[ROLLBACK_RING];

#define DRB_HIST        64      /* input/canary history depth                */
#define DRB_REDUNDANCY  16      /* newest frames repeated in every packet    */
#define DRB_REC_BYTES   2       /* wire size of one input record             */
#define DRB_HDR_BYTES   28      /* wire size of the packet header            */
#define DRB_CANARY_PEND 8       /* received canaries awaiting our own frame  */

#define DRB_TIME_OUT     16000  /* ms stalled with a dead link -> halt       */
#define DRB_ABS_TIME_OUT 300000 /* ms stalled even with a live link          */

/* Rolling back onto one frame this many times without verifying it means the correction is not
 * converging; the scan then accepts the peer's record so the battle keeps moving. */
#define DRB_RESIM_GIVE_UP 16

typedef struct
{
	Uint8  actions, controls;
}
DrbInput;

typedef struct
{
	DrbInput in;
	Uint32   tag;   /* frame this slot holds; 0 = empty */
}
DrbSlot;

static DrbSlot local_hist[DRB_HIST];
static DrbSlot remote_hist[DRB_HIST];   /* the peer's records, as received      */
static DrbSlot remote_used[DRB_HIST];   /* what the simulation actually consumed */

static Uint32 drb_cur;          /* frame being simulated                       */
static Uint32 verified_upto;    /* simulation ran on truth through this frame  */
static Uint32 ctrl_done;        /* control bits acted on through this frame    */
static Uint32 peer_acked;       /* peer holds all our frames <= this           */
static Uint32 remote_contig;    /* we hold all peer frames <= this             */
static Uint32 remote_newest;    /* newest peer frame seen                      */

static bool   resim_active;
static bool   resim_silent;
static Uint32 resim_target;
static Uint32 resim_last_K, resim_repeat;
static bool   resim_livelock_reported;

static bool   pending_quit, pending_newmap;

static Uint16 drb_epoch;        /* rounds started; tells stale packets apart   */
static Uint32 last_resend_tick;

/* Desync canary: the round's RNG draw count and a summary hash of the battle, final once the
 * frame is verified. */
typedef struct
{
	Uint32 tag, rand, hash;
}
DrbCanary;

static DrbCanary canary[DRB_HIST];
static DrbCanary peer_pend[DRB_CANARY_PEND];
static int       peer_pend_n;
static Uint32    canary_checked_upto;
static bool      canary_reported;       /* one full report per session          */
static Uint32    canary_mismatches;

/* Session-long counters for the crash log; unlike the per-round state above these survive a
 * round reset. */
static struct
{
	Uint32 pkt_in;
	Uint32 refused_malformed, refused_window, refused_epoch;
	Uint32 stalls, rollbacks, resim_frames, deepest;
}
drb_diag;

/* Snapshot self-test.  Independent of a session: drb_active() stays false throughout, so the tick
 * keeps its offline round handling and the driver never runs. */
static bool          selftest_on;
static bool          selftest_verifying;   /* the pass that just ran was the replay */
static unsigned long selftest_budget, selftest_run, selftest_fails;
static Uint8        *selftest_ref;         /* state after the live pass   */
static Uint8        *selftest_cmp;         /* state after the replay pass */

bool drb_active(void)
{
	return session_active;
}

Uint32 drb_frame(void)
{
	return drb_cur;
}

bool drb_resim(void)
{
	return resim_active;
}

bool drb_resim_silent(void)
{
	return resim_silent;
}

/* Snapshot ring. */

static void drb_ring_reset(void)
{
	for (int i = 0; i < ROLLBACK_RING; ++i)
		ring_valid[i] = false;
}

static bool drb_ring_restore(Uint32 frame)
{
	const int slot = (int)(frame % ROLLBACK_RING);
	if (!ring_valid[slot] || ring_frame[slot] != frame)
		return false;
	state_restore(ring[slot]);
	return true;
}

static void *drb_alloc(size_t bytes)
{
	void *const p = malloc(bytes);
	if (p == NULL)
	{
		fprintf(stderr, "destruct rollback: out of memory (%zu bytes)\n", bytes);
		exit(1);
	}
	return p;
}

static void drb_ring_alloc(size_t stateBytes, void (*save)(void *dst),
                           void (*restore)(const void *src))
{
	state_bytes = stateBytes;
	state_save = save;
	state_restore = restore;

	for (int i = 0; i < ROLLBACK_RING; ++i)
		ring[i] = drb_alloc(state_bytes);
}

static void drb_ring_free(void)
{
	for (int i = 0; i < ROLLBACK_RING; ++i)
	{
		free(ring[i]);
		ring[i] = NULL;
	}
}

void drb_session_begin(size_t stateBytes, void (*save)(void *dst), void (*restore)(const void *src),
                       Uint32 (*hash)(void), Uint8 heldActions)
{
	drb_session_end();

	drb_ring_alloc(stateBytes, save, restore);
	state_hash = hash;
	held_actions = heldActions;

	memset(&drb_diag, 0, sizeof(drb_diag));
	drb_epoch = 0;
	canary_reported = false;
	session_active = true;

	drb_round_reset();
}

void drb_session_end(void)
{
	drb_selftest_disarm();
	drb_ring_free();
	session_active = false;
	resim_active = false;
	resim_silent = false;
}

void drb_round_reset(void)
{
	memset(local_hist, 0, sizeof(local_hist));
	memset(remote_hist, 0, sizeof(remote_hist));
	memset(remote_used, 0, sizeof(remote_used));
	memset(canary, 0, sizeof(canary));
	peer_pend_n = 0;
	canary_checked_upto = 0;
	canary_mismatches = 0;

	++drb_epoch;

	drb_cur = 1;
	verified_upto = 0;
	ctrl_done = 0;
	peer_acked = 0;
	remote_contig = 0;
	remote_newest = 0;

	resim_active = false;
	resim_silent = false;
	resim_target = 0;
	resim_last_K = resim_repeat = 0;
	resim_livelock_reported = false;
	pending_quit = pending_newmap = false;

	last_resend_tick = 0;

	drb_ring_reset();
}

void drb_frame_begin(void)
{
	if (!session_active && !selftest_on)
		return;

	const int slot = (int)(drb_cur % ROLLBACK_RING);
	state_save(ring[slot]);
	ring_frame[slot] = drb_cur;
	ring_valid[slot] = true;
}

/* Input history and prediction. */

void drb_record_local(Uint8 actions, Uint8 controls)
{
	DrbSlot *const s = &local_hist[drb_cur % DRB_HIST];
	s->in.actions = actions;
	s->in.controls = controls;
	s->tag = drb_cur;
}

/* Repeat the newest record we hold, keeping only the bits a player can hold down.  The unit,
 * weapon-up and weapon-down actions are edge triggered (destruct.c consumes the key as it reads
 * it), so predicting one would fire an action the peer never took. */
static void drb_predict_remote(Uint32 frame, DrbInput *out)
{
	memset(out, 0, sizeof(*out));

	/* The floor stops at frame 1: frame 0 exists in no round, and an empty slot carries tag 0. */
	const Uint32 floor_f = (frame > DRB_HIST / 2) ? frame - DRB_HIST / 2 : 1;
	for (Uint32 f = frame; f > floor_f; --f)
	{
		if (remote_hist[(f - 1) % DRB_HIST].tag == f - 1)
		{
			out->actions = remote_hist[(f - 1) % DRB_HIST].in.actions & held_actions;
			return;
		}
	}
}

void drb_frame_actions(Uint8 *local, Uint8 *peer)
{
	*local = (local_hist[drb_cur % DRB_HIST].tag == drb_cur)
	       ? local_hist[drb_cur % DRB_HIST].in.actions : 0;

	DrbSlot *const truth = &remote_hist[drb_cur % DRB_HIST];
	DrbInput used;

	if (truth->tag == drb_cur)
		used = truth->in;
	else
		drb_predict_remote(drb_cur, &used);

	/* Record what the frame consumed; a misprediction is this against the truth that follows. */
	remote_used[drb_cur % DRB_HIST].in = used;
	remote_used[drb_cur % DRB_HIST].tag = drb_cur;

	*peer = used.actions;
}

/* Advance verified_upto over frames whose consumed input matches the arrived truth, and return the
 * first frame where they differ (0 = none).  Only the action bits are compared: the control bits
 * never reach the simulation, and drb_process_controls reads them from the arrived record rather
 * than from what a frame consumed, so an unpredicted quit or new-map press changes no simulated
 * byte and buying a rollback for it would only re-derive the identical state. */
static Uint32 drb_scan_mispredict(void)
{
	while (verified_upto < drb_cur)
	{
		const Uint32 f = verified_upto + 1;

		if (remote_hist[f % DRB_HIST].tag != f)
			break;   /* truth not here yet */

		if (remote_used[f % DRB_HIST].tag == f)
		{
			const DrbInput *const used = &remote_used[f % DRB_HIST].in;
			const DrbInput *const got = &remote_hist[f % DRB_HIST].in;

			if (used->actions != got->actions)
			{
				/* Not converging: rolling back to f keeps landing on the same mismatch.  Accept
				 * the peer's record as consumed and move on; a divergence the canary reports
				 * beats a battle that never completes another frame. */
				if (f != resim_last_K || resim_repeat <= DRB_RESIM_GIVE_UP)
					return f;

				remote_used[f % DRB_HIST].in = *got;
				resim_repeat = 0;

				if (!resim_livelock_reported)
				{
					resim_livelock_reported = true;
					char detail[224];
					snprintf(detail, sizeof(detail),
					         "destruct frame %lu rolled back %d times without verifying "
					         "(current %lu, player %u)\n"
					         "  accepting the peer's input as consumed to keep the battle moving",
					         (unsigned long)f, DRB_RESIM_GIVE_UP,
					         (unsigned long)drb_cur, thisPlayerNum);
					crashlog_note_net("DESTRUCT ROLLBACK LIVELOCK", detail);
				}
			}
		}

		++verified_upto;
	}
	return 0;
}

/* Wire protocol. */

static void drb_send_input(void)
{
	Uint8 *const data = packet_out_temp->data;

	const Uint32 F = drb_cur;
	Uint32 from = peer_acked + 1;
	if (from < 1)
		from = 1;
	if ((Sint64)F - (Sint64)from + 1 > DRB_REDUNDANCY)
		from = F + 1 - DRB_REDUNDANCY;
	/* Signed: the peer can have acked beyond our own counter after running ahead through a fade.
	 * The packet then carries no records but still delivers the ack and the canary. */
	int count = (int)((Sint64)F - (Sint64)from + 1);
	if (count < 0)
		count = 0;

	SDLNet_Write16(PACKET_DESTRUCT_INPUT, &data[0]);
	SDLNet_Write16((Uint16)count,         &data[2]);
	SDLNet_Write32(F,                     &data[4]);
	SDLNet_Write32(remote_contig,         &data[8]);
	SDLNet_Write16(drb_epoch,             &data[12]);
	SDLNet_Write16(0,                     &data[14]);

	/* Canary for the newest frame our side considers final. */
	if (verified_upto > 0 && canary[verified_upto % DRB_HIST].tag == verified_upto)
	{
		const DrbCanary *const c = &canary[verified_upto % DRB_HIST];
		SDLNet_Write32(c->tag,  &data[16]);
		SDLNet_Write32(c->rand, &data[20]);
		SDLNet_Write32(c->hash, &data[24]);
	}
	else
	{
		memset(&data[16], 0, DRB_HDR_BYTES - 16);
	}

	int off = DRB_HDR_BYTES;
	for (Uint32 f = from; f <= F; ++f, off += DRB_REC_BYTES)
	{
		const DrbInput *const in = &local_hist[f % DRB_HIST].in;
		data[off] = in->actions;
		data[off + 1] = in->controls;
	}

	network_send_unacked(off);
	last_resend_tick = SDL_GetTicks();
}

void drb_handle_packet(const Uint8 *data, int len)
{
	if (!session_active)
		return;   /* not in a rollback battle: nothing to ingest it into */

	if (len < DRB_HDR_BYTES)
	{
		++drb_diag.refused_malformed;
		return;
	}

	const int    count = SDLNet_Read16(&data[2]);
	const Uint32 F     = SDLNet_Read32(&data[4]);
	const Uint32 ack   = SDLNet_Read32(&data[8]);

	if (count < 0 || count > DRB_REDUNDANCY || len < DRB_HDR_BYTES + count * DRB_REC_BYTES
	    || F < (Uint32)count)
	{
		++drb_diag.refused_malformed;
		return;
	}
	/* Datagrams still in flight from the previous round carry frame numbers this round's counter
	 * has no business accepting; a real peer can only be a prediction window ahead. */
	if (F > drb_cur + DRB_HIST)
	{
		++drb_diag.refused_window;
		return;
	}
	/* A short round leaves frame numbers small enough to pass that test, which the epoch catches
	 * instead.  Only strictly older is refused: a peer one round ahead is the machine that
	 * regenerated first, and refusing it would turn a start skew into a mutual stall. */
	if ((Sint16)(SDLNet_Read16(&data[12]) - drb_epoch) < 0)
	{
		++drb_diag.refused_epoch;
		return;
	}

	++drb_diag.pkt_in;

	if (ack > peer_acked && ack <= drb_cur)
		peer_acked = ack;
	if (F > remote_newest)
		remote_newest = F;

	{
		/* The peer repeats one frame's canary until its own side moves on, so take each frame
		 * exactly once: already queued, or already checked. */
		const Uint32 cf = SDLNet_Read32(&data[16]);
		bool have = cf == 0 || cf <= canary_checked_upto;
		for (int i = 0; !have && i < peer_pend_n; ++i)
			have = peer_pend[i].tag == cf;

		if (!have)
		{
			if (peer_pend_n == DRB_CANARY_PEND)
			{
				memmove(&peer_pend[0], &peer_pend[1], sizeof(peer_pend[0]) * (DRB_CANARY_PEND - 1));
				--peer_pend_n;
			}

			DrbCanary *const c = &peer_pend[peer_pend_n++];
			c->tag  = cf;
			c->rand = SDLNet_Read32(&data[20]);
			c->hash = SDLNet_Read32(&data[24]);
		}
	}

	/* Records run oldest to newest, ending at F. */
	const Uint32 first = F - (Uint32)count + 1;
	int off = DRB_HDR_BYTES;
	for (Uint32 f = first; f <= F; ++f, off += DRB_REC_BYTES)
	{
		if (f == 0 || f + DRB_HIST / 2 < drb_cur || f > drb_cur + DRB_HIST / 2)
			continue;   /* out of the useful window */

		DrbSlot *const s = &remote_hist[f % DRB_HIST];
		if (s->tag == f)
			continue;   /* redundancy: already holding this frame's truth */

		s->in.actions = data[off];
		s->in.controls = data[off + 1];
		s->tag = f;
	}

	while (remote_hist[(remote_contig + 1) % DRB_HIST].tag == remote_contig + 1)
		++remote_contig;
}

/* Desync canary. */

static void drb_stamp_canary(Uint32 frame)
{
	DrbCanary *const c = &canary[frame % DRB_HIST];
	c->tag = frame;
	c->rand = mt_rand_count;
	c->hash = state_hash();
}

static void drb_compare_canary(void)
{
	int kept = 0;

	for (int i = 0; i < peer_pend_n; ++i)
	{
		const DrbCanary *const theirs = &peer_pend[i];

		if (theirs->tag > verified_upto)
		{
			peer_pend[kept++] = *theirs;   /* our own copy is not final yet */
			continue;
		}
		if (theirs->tag > canary_checked_upto)
			canary_checked_upto = theirs->tag;

		const DrbCanary *const ours = &canary[theirs->tag % DRB_HIST];
		if (ours->tag != theirs->tag)
			continue;   /* out of window; too old to compare */
		if (ours->rand == theirs->rand && ours->hash == theirs->hash)
			continue;

		++canary_mismatches;
		if (canary_reported)
			continue;

		canary_reported = true;

		char detail[256];
		snprintf(detail, sizeof(detail),
		         "Destruct frame %lu, player %u, epoch %u\n"
		         "  rand draws : local %lu  remote %lu\n"
		         "  sim hash   : local %08lx  remote %08lx",
		         (unsigned long)theirs->tag, thisPlayerNum, (unsigned)drb_epoch,
		         (unsigned long)ours->rand, (unsigned long)theirs->rand,
		         (unsigned long)ours->hash, (unsigned long)theirs->hash);
		crashlog_netlog_line("DESTRUCT DESYNC", detail);
		network_diag_note_desync(-1);
	}

	peer_pend_n = kept;
}

/* Rollback trigger. */

static DrbStep drb_begin_resim(Uint32 K)
{
	/* Highest frame the timeline we are about to discard ever simulated. */
	const Uint32 high = (resim_active && resim_target > drb_cur) ? resim_target : drb_cur;

	/* Drop the consumed-input records of the discarded timeline, or a frame that stamps none on
	 * the way back would keep triggering a rollback to K. */
	for (Uint32 f = K; f <= high; ++f)
		if (remote_used[f % DRB_HIST].tag == f)
			memset(&remote_used[f % DRB_HIST], 0, sizeof(remote_used[0]));

	if (K == resim_last_K)
	{
		++resim_repeat;
	}
	else
	{
		resim_last_K = K;
		resim_repeat = 1;
	}

	if (!drb_ring_restore(K))
	{
		/* A missing snapshot inside the prediction window is fatal: continuing would quietly
		 * desynchronize the two battles. */
		char detail[160];
		snprintf(detail, sizeof(detail),
		         "no destruct snapshot for frame %lu (current %lu, verified %lu)",
		         (unsigned long)K, (unsigned long)drb_cur, (unsigned long)verified_upto);
		crashlog_note_net("DESTRUCT SNAPSHOT MISSING", detail);
		network_tyrian_halt(7, false);
	}

	++drb_diag.rollbacks;
	drb_diag.resim_frames += high - K + 1;
	if (high - K + 1 > drb_diag.deepest)
		drb_diag.deepest = high - K + 1;

	resim_active = true;
	/* `high`, not drb_cur: a second correction arriving mid-re-simulation must not shorten the
	 * pass to the frame being replayed.  A dropped tail turns those frames back into live passes,
	 * and a live pass re-samples input over records already sent to the peer. */
	resim_target = high;
	drb_cur = K;
	resim_silent = (K < resim_target);
	return DRB_STEP_RESIM;
}

/* Pump the world while stalled: OS events, inbound packets, a periodic resend in case the peer is
 * waiting on a lost packet, and the overlay that says why the battle stopped. */
static void drb_stall_pump(Uint32 wait_start, bool *stall_reported, const char *why)
{
	watchdog_heartbeat();
	service_SDL_events(false);

	if (SDL_GetTicks() - last_resend_tick > 100)
		drb_send_input();

	/* <= 0, not == 0: a receive error reads nothing, so skipping the sleep on it would spin this
	 * pump on a core for the whole stall. */
	if (network_check() <= 0)
		SDL_Delay(1);

	const Uint32 waited = SDL_GetTicks() - wait_start;

	if (waited > 700)
	{
		static Uint32 overlay_for = 0;
		static Uint32 last_present = 0;

		SDL_Surface *const save = VGAScreen;
		VGAScreen = VGAScreenSeg;
		if (overlay_for != wait_start)
		{
			overlay_for = wait_start;
			JE_drawNetworkNotice("Waiting for other player.");
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
		++drb_diag.stalls;
		char detail[256];
		snprintf(detail, sizeof(detail),
		         "%s\n"
		         "  player   : %u   round epoch: %u\n"
		         "  frame    : %lu   verified: %lu   peer ack: %lu   remote newest: %lu",
		         why, thisPlayerNum, (unsigned)drb_epoch,
		         (unsigned long)drb_cur, (unsigned long)verified_upto,
		         (unsigned long)peer_acked, (unsigned long)remote_newest);
		crashlog_note_net("DESTRUCT ROLLBACK STALL", detail);
	}

	/* A live peer that is merely slow (a round fade, a loading pause) must never trip the
	 * disconnect: keep-alives hold the link open, with a generous cap as the wedge backstop. */
	if ((waited > DRB_TIME_OUT && !network_peer_alive()) || waited > DRB_ABS_TIME_OUT)
	{
		fprintf(stderr, "error: no usable destruct input from the other player for %u ms\n",
		        (unsigned)waited);
		network_tyrian_halt(2, false);
	}
}

/* Act on the control bits of every frame both machines have agreed on.  Quit and new map are
 * irreversible, so a predicted record must never reach this. */
static void drb_process_controls(void)
{
	while (ctrl_done < verified_upto)
	{
		const Uint32 f = ++ctrl_done;
		const Uint8 mine = (local_hist[f % DRB_HIST].tag == f)
		                 ? local_hist[f % DRB_HIST].in.controls : 0;
		const Uint8 theirs = (remote_hist[f % DRB_HIST].tag == f)
		                   ? remote_hist[f % DRB_HIST].in.controls : 0;
		const Uint8 bits = mine | theirs;

		if (bits & DRB_CTRL_QUIT)
			pending_quit = true;
		if (bits & DRB_CTRL_NEWMAP)
			pending_newmap = true;
	}
}

DrbStep drb_driver(bool roundOver)
{
	if (!session_active)
		return DRB_STEP_PRESENT;

	if (resim_active)
	{
		/* A re-simulation pass for drb_cur just completed. */
		drb_stamp_canary(drb_cur);

		/* A round that ends inside a correction ends the chain with it: the frames past it were
		 * simulated on the timeline being discarded and belong to no round. */
		if (roundOver)
			resim_target = drb_cur;

		if (drb_cur < resim_target)
		{
			++drb_cur;
			resim_silent = (drb_cur < resim_target);
			return DRB_STEP_RESIM;
		}

		/* The corrected timeline has caught up; the pass that just ran is the presented one, and
		 * its input was already published by the live pass it replaced. */
		resim_active = false;
		resim_silent = false;
	}
	else
	{
		drb_send_input();
		drb_stamp_canary(drb_cur);
	}

	network_check();

	{
		const Uint32 K = drb_scan_mispredict();
		if (K != 0)
			return drb_begin_resim(K);
	}

	drb_compare_canary();
	drb_process_controls();

	if (pending_quit)
		return DRB_STEP_QUIT;
	if (pending_newmap)
		return DRB_STEP_NEWMAP;

	/* The round ended on this timeline.  Hold until every frame behind the verdict is confirmed,
	 * or a mispredicted last shot would reload one machine's map and not the other's. */
	if (roundOver)
	{
		const Uint32 wait_start = SDL_GetTicks();
		const Uint32 newest_at_start = remote_newest;
		bool stall_reported = false;

		while (verified_upto < drb_cur)
		{
			drb_stall_pump(wait_start, &stall_reported, "waiting to confirm the round end");

			const Uint32 K = drb_scan_mispredict();
			if (K != 0)
				return drb_begin_resim(K);

			/* A peer that stopped producing frames has left; end the round rather than wedge. */
			if (remote_newest == newest_at_start && SDL_GetTicks() - wait_start > 8000)
			{
				char detail[192];
				snprintf(detail, sizeof(detail),
				         "peer stopped simulating at frame %lu while we waited to confirm "
				         "frame %lu (player %u)\n  ending the round unconfirmed",
				         (unsigned long)remote_newest, (unsigned long)drb_cur, thisPlayerNum);
				crashlog_note_net("DESTRUCT ROUND-END TIMEOUT", detail);
				break;
			}
		}

		/* The confirmed frames may carry a quit the round end would otherwise swallow. */
		drb_process_controls();
		return pending_quit ? DRB_STEP_QUIT : DRB_STEP_NEWMAP;
	}

	/* Bound prediction depth and outstanding unacked history before advancing.  Signed
	 * differences throughout: after a round fade the peer can legitimately be ahead of us, with
	 * remote_contig and peer_acked past our own counter. */
	{
		const Uint32 wait_start = SDL_GetTicks();
		bool stall_reported = false;

		while ((Sint64)(drb_cur + 1) - (Sint64)remote_contig > ROLLBACK_MAX_PREDICT
		       || (Sint64)(drb_cur + 1) - (Sint64)peer_acked > DRB_REDUNDANCY - 1
		       /* Start-of-round barrier: until the peer's first record arrives it may still be
		        * fading in the previous round, and running a full prediction window against a
		        * peer that has produced nothing guarantees an opening rollback burst. */
		       || (remote_newest == 0 && drb_cur >= 3))
		{
			drb_stall_pump(wait_start, &stall_reported, "peer too far behind");

			const Uint32 K = drb_scan_mispredict();
			if (K != 0)
				return drb_begin_resim(K);
		}
	}

	++drb_cur;
	return DRB_STEP_PRESENT;
}

/* Snapshot self-test. */

void drb_selftest_arm(size_t stateBytes, void (*save)(void *dst), void (*restore)(const void *src),
                      unsigned long ticks)
{
	drb_session_end();

	drb_ring_alloc(stateBytes, save, restore);
	selftest_ref = drb_alloc(state_bytes);
	selftest_cmp = drb_alloc(state_bytes);

	selftest_budget = ticks;
	selftest_run = selftest_fails = 0;
	selftest_verifying = false;
	selftest_on = true;

	drb_epoch = 0;
	drb_round_reset();
}

void drb_selftest_disarm(void)
{
	free(selftest_ref);
	selftest_ref = NULL;
	free(selftest_cmp);
	selftest_cmp = NULL;
	selftest_on = false;
	selftest_verifying = false;
}

bool drb_selftest_active(void)
{
	return selftest_on;
}

unsigned long drb_selftest_ticks_run(void)
{
	return selftest_run;
}

unsigned long drb_selftest_failures(void)
{
	return selftest_fails;
}

void drb_selftest_feed(Uint8 localActions, Uint8 peerActions)
{
	drb_record_local(localActions, 0);

	DrbSlot *const s = &remote_hist[drb_cur % DRB_HIST];
	s->in.actions = peerActions;
	s->in.controls = 0;
	s->tag = drb_cur;
}

bool drb_selftest_tick(void)
{
	if (!selftest_on)
		return false;

	if (!selftest_verifying)
	{
		/* The live pass is done.  Keep what it produced, then replay the same frame from its
		 * own snapshot; the replay reads its input out of the history the live pass recorded. */
		state_save(selftest_ref);

		if (!drb_ring_restore(drb_cur))
		{
			fprintf(stderr, "destruct snapshot self-test: no snapshot for frame %lu\n",
			        (unsigned long)drb_cur);
			++selftest_fails;
			drb_selftest_disarm();
			return false;
		}

		selftest_verifying = true;
		resim_active = true;
		resim_silent = true;
		return true;
	}

	state_save(selftest_cmp);
	selftest_verifying = false;
	resim_active = false;
	resim_silent = false;

	if (memcmp(selftest_ref, selftest_cmp, state_bytes) != 0)
	{
		++selftest_fails;
		if (selftest_fails == 1)
		{
			size_t off = 0;
			while (off < state_bytes && selftest_ref[off] == selftest_cmp[off])
				++off;
			fprintf(stderr, "destruct snapshot self-test: frame %lu replayed differently, "
			                "first at byte %zu of %zu (%02x vs %02x)\n",
			        (unsigned long)drb_cur, off, state_bytes,
			        selftest_ref[off], selftest_cmp[off]);
		}

		/* Carry on from the timeline the live pass produced, so one bad frame does not turn the
		 * rest of the run into noise. */
		state_restore(selftest_ref);
	}

	++drb_cur;
	if (++selftest_run >= selftest_budget)
		drb_selftest_disarm();
	return false;
}

void drb_write_diagnostics(FILE *f)
{
	if (f == NULL || !session_active)
		return;

	fprintf(f, "  destruct rollback : frame %lu, verified %lu, epoch %u\n",
	        (unsigned long)drb_cur, (unsigned long)verified_upto, (unsigned)drb_epoch);
	fprintf(f, "    peer            : newest %lu, contiguous %lu, acked %lu\n",
	        (unsigned long)remote_newest, (unsigned long)remote_contig, (unsigned long)peer_acked);
	fprintf(f, "    rollbacks       : %lu (%lu resim frames, deepest %lu)\n",
	        (unsigned long)drb_diag.rollbacks, (unsigned long)drb_diag.resim_frames,
	        (unsigned long)drb_diag.deepest);
	fprintf(f, "    packets in      : %lu (refused: %lu malformed, %lu window, %lu epoch)\n",
	        (unsigned long)drb_diag.pkt_in, (unsigned long)drb_diag.refused_malformed,
	        (unsigned long)drb_diag.refused_window, (unsigned long)drb_diag.refused_epoch);
	fprintf(f, "    stalls          : %lu, desync mismatches %lu\n",
	        (unsigned long)drb_diag.stalls, (unsigned long)canary_mismatches);
}

#else  /* !WITH_NETWORK */

bool drb_active(void) { return false; }
Uint32 drb_frame(void) { return 0; }
bool drb_resim(void) { return false; }
bool drb_resim_silent(void) { return false; }

/* The snapshot the self-test exercises only exists in a network build, so neither does it. */
void drb_selftest_arm(size_t stateBytes, void (*save)(void *dst), void (*restore)(const void *src),
                      unsigned long ticks)
{
	(void)stateBytes; (void)save; (void)restore; (void)ticks;
}

void drb_selftest_disarm(void) { }
bool drb_selftest_active(void) { return false; }
void drb_selftest_feed(Uint8 localActions, Uint8 peerActions) { (void)localActions; (void)peerActions; }
bool drb_selftest_tick(void) { return false; }
unsigned long drb_selftest_ticks_run(void) { return 0; }
unsigned long drb_selftest_failures(void) { return 0; }

void drb_session_begin(size_t stateBytes, void (*save)(void *dst), void (*restore)(const void *src),
                       Uint32 (*hash)(void), Uint8 heldActions)
{
	(void)stateBytes; (void)save; (void)restore; (void)hash; (void)heldActions;
}

void drb_session_end(void) { }
void drb_round_reset(void) { }
void drb_frame_begin(void) { }

void drb_record_local(Uint8 actions, Uint8 controls) { (void)actions; (void)controls; }

void drb_frame_actions(Uint8 *local, Uint8 *peer)
{
	*local = 0;
	*peer = 0;
}

DrbStep drb_driver(bool roundOver)
{
	(void)roundOver;
	return DRB_STEP_PRESENT;
}

#endif /* WITH_NETWORK */
