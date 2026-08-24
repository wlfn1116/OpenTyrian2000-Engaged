/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback netcode for the online Destruct minigame.  See destruct_rollback.h for the interface
 * and doc/notes.md#rollback for the simulation invariants.
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
#include "destruct_rollback.h"

#include "crashlog.h"
#include "joystick.h"
#include "keyboard.h"
#include "mainint.h"
#include "mtrand.h"
#include "net_rollback.h"
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
static bool      canary_reported;       /* one full report per timeline         */
static Uint32    canary_mismatches;

/* Recovery header: generation, chunk index, chunk count, and payload size. ACK
 * and NAK use reserved indices; chunk zero also carries sizes and checksum. */
#define DRB_RS_HDR       12
#define DRB_RS_PRE       12
#define DRB_RS_PAYLOAD   (NET_PACKET_SIZE - DRB_RS_HDR)
#define DRB_RS_NAK       0xFFFFu
#define DRB_RS_ACK       0xFFFEu
#define DRB_RS_MAX       3                /* recovery attempts per round           */
#define DRB_RS_PROGRESS_TIME_OUT 8000     /* ms without a new chunk -> NAK         */
#define DRB_RS_ABS_TIME_OUT      60000    /* ms for the whole attempt              */
#define DRB_RS_ADOPT_TIME_OUT    6000     /* ms to answer a fully delivered stream */

/* NAK reason: retry transient failures, but stop after a permanent layout mismatch. */
#define DRB_NAK_RETRY    0u
#define DRB_NAK_FATAL    1u

static Uint32 resync_used;       /* attempts consumed this round (either role)          */
static Uint16 resync_gen;        /* newest attempt id sent (host) / adopted (joiner)    */
static bool   resync_wanted;     /* canary mismatch seen; the host acts on it           */
static bool   resync_layout_bad; /* session-scoped: no stream can ever pass the guard   */

/* Presentation: a recovery is in play, so the stall overlay names that instead of the wait.  Held
 * until the peer is heard on the fresh timeline, so one hitch does not show two messages. */
static bool   resync_notice;
static Uint16 resync_notice_epoch;

static bool drb_resync_dispatch(void);

/* Both roles arm this, at the mismatch and at the transfer, so neither machine calls the recovery
 * something the other does not. */
static void drb_notice_resync(void)
{
	resync_notice = true;
	resync_notice_epoch = drb_epoch;
}

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

/* Recovery wire probe, run once per self-test (see drb_selftest_resync_ok). */
static bool   selftest_probe_done, selftest_probe_ok;
static size_t selftest_probe_raw, selftest_probe_comp;

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
	resync_layout_bad = false;
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

/* Everything a fresh timeline needs, shared by the round start and a completed desync recovery.
 * A recovery is a round start as far as frames, histories and the epoch go; only the round's
 * recovery budget survives it. */
static void drb_reset_core(void)
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

void drb_round_reset(void)
{
	drb_reset_core();
	resync_used = 0;
	resync_gen = 0;
	resync_notice = false;
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

/* Advance through verified action input and return the first mismatch. Control
 * bits bypass simulation, so rolling back for them would reproduce identical state. */
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

		/* Repairable: arm a recovery.  The host acts on the flag at its driver site; the joiner's
		 * own copy is cleared when the stream arrives. */
		if (nrb_session_recovery() && resync_used < DRB_RS_MAX && !resync_layout_bad)
		{
			resync_wanted = true;
			drb_notice_resync();
		}

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
 * waiting on a lost packet, and the overlay that says why the battle stopped.  True means an
 * inbound desync recovery reset the timeline underneath the caller's wait. */
static bool drb_stall_pump(Uint32 wait_start, bool *stall_reported, const char *why)
{
	watchdog_heartbeat();
	service_SDL_events(false);

	if (SDL_GetTicks() - last_resend_tick > 100)
		drb_send_input();

	/* <= 0, not == 0: a receive error reads nothing, so skipping the sleep on it would spin this
	 * pump on a core for the whole stall. */
	if (network_check() <= 0)
		SDL_Delay(1);

	/* The peer can start a recovery while this side sits in a wait loop: a desync right at a round
	 * end leaves one machine here and the other streaming. */
	if (drb_resync_dispatch())
		return true;

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
			/* This wait IS the recovery when one is in play; the machine on the other side of it
			 * is already showing that. */
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

	return false;
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

/* Desync recovery engine.  The battle snapshot carries no relocatable pointers (destruct.c re-pins
 * the three it holds on every restore), so the host's blob is adopted as it arrives; see
 * doc/notes.md#rollback for the size guard. */

/* FNV-1a over the compressed stream: guards the assembly logic, not the link (UDP already
 * checksums each datagram). */
static Uint32 drb_rs_hash(const Uint8 *p, size_t n)
{
	Uint32 h = 2166136261u;
	for (size_t i = 0; i < n; ++i)
	{
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

/* Pump for the transfer loops: OS events, inbound datagrams, the overlay, the link timeouts. */
static bool drb_resync_pump(Uint32 wait_start, bool *reported, const char *why)
{
	watchdog_heartbeat();
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
			JE_drawNetworkNotice("Resyncing players.");
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
		++drb_diag.stalls;
		char detail[224];
		snprintf(detail, sizeof(detail),
		         "%s\n  player %u, attempt %lu, gen %u, %lu ms in",
		         why, thisPlayerNum, (unsigned long)resync_used,
		         (unsigned)resync_gen, (unsigned long)waited);
		crashlog_note_net("DESTRUCT RESYNC STALL", detail);
	}

	if (waited > DRB_TIME_OUT && !network_peer_alive())
		network_tyrian_halt(2, false);

	return waited <= DRB_RS_ABS_TIME_OUT;
}

/* One chunk onto the acknowledged channel.  `stream` = preamble + compressed. */
static void drb_resync_send_chunk(const Uint8 *stream, size_t total, Uint32 chunks, Uint32 idx)
{
	Uint8 *data = packet_out_temp->data;
	const size_t from = (size_t)idx * DRB_RS_PAYLOAD;
	size_t len = total - from;
	if (len > DRB_RS_PAYLOAD)
		len = DRB_RS_PAYLOAD;

	network_prepare(PACKET_DESTRUCT_RESYNC);
	SDLNet_Write16(resync_gen,     &data[4]);
	SDLNet_Write16((Uint16)idx,    &data[6]);
	SDLNet_Write16((Uint16)chunks, &data[8]);
	SDLNet_Write16((Uint16)len,    &data[10]);
	memcpy(&data[DRB_RS_HDR], stream + from, len);
	network_send(DRB_RS_HDR + (int)len);
}

static void drb_resync_send_nak(Uint16 gen, Uint16 reason)
{
	Uint8 *data = packet_out_temp->data;
	network_prepare(PACKET_DESTRUCT_RESYNC);
	SDLNet_Write16(gen,                &data[4]);
	SDLNet_Write16((Uint16)DRB_RS_NAK, &data[6]);
	SDLNet_Write16(reason,             &data[8]);
	SDLNet_Write16(0,                  &data[10]);
	network_send(DRB_RS_HDR);
}

/* The joiner's "I am on the new timeline".  The host resets onto it only when this arrives:
 * transport acks say the bytes were received, and the joiner acknowledges on receipt, before it has
 * parsed a single one of them. */
static void drb_resync_send_ack(Uint16 gen)
{
	Uint8 *data = packet_out_temp->data;
	network_prepare(PACKET_DESTRUCT_RESYNC);
	SDLNet_Write16(gen,                &data[4]);
	SDLNet_Write16((Uint16)DRB_RS_ACK, &data[6]);
	SDLNet_Write16(0,                  &data[8]);
	SDLNet_Write16(0,                  &data[10]);
	network_send(DRB_RS_HDR);
}

/* One host streaming attempt.  1 = streamed, adopted, timeline reset; 0 = failed but worth
 * retrying (joiner NAK, stall); -1 = stop trying. */
static int drb_resync_send_once(void)
{
	++resync_used;
	++resync_gen;
	drb_notice_resync();

	const size_t cap = state_bytes + state_bytes / 255 + 64;

	Uint8 *raw = malloc(state_bytes);
	Uint8 *stream = malloc(DRB_RS_PRE + cap);
	if (raw == NULL || stream == NULL)
	{
		free(raw);
		free(stream);
		return -1;
	}

	state_save(raw);

	const size_t comp_sz = nrb_resync_compress(raw, state_bytes, stream + DRB_RS_PRE, cap);
	free(raw);
	if (comp_sz == 0)
	{
		free(stream);
		return -1;
	}

	SDLNet_Write32((Uint32)state_bytes, &stream[0]);
	SDLNet_Write32((Uint32)comp_sz,     &stream[4]);
	SDLNet_Write32(drb_rs_hash(stream + DRB_RS_PRE, comp_sz), &stream[8]);

	const size_t total  = DRB_RS_PRE + comp_sz;
	const Uint32 chunks = (Uint32)((total + DRB_RS_PAYLOAD - 1) / DRB_RS_PAYLOAD);

	const Uint32 wait_start = SDL_GetTicks();
	bool reported = false;
	Uint32 sent = 0;
	Uint32 delivered_at = 0;  /* when the last chunk was acknowledged; 0 = not yet */
	int outcome = 0;  /* 1 done, 2 NAK: retry, 3 give up this round */
	const char *fail = NULL;
	char fail_ctx[160];

	while (outcome == 0)
	{
		char why[112];
		snprintf(why, sizeof(why), "%s: %lu/%lu chunks handed over, backlog %d",
		         delivered_at ? "waiting for the joiner to adopt the battle state"
		                      : "streaming the battle state",
		         (unsigned long)sent, (unsigned long)chunks, network_ack_backlog());
		if (!drb_resync_pump(wait_start, &reported, why))
		{
			fail = "timed out: the joiner stopped acknowledging";
			outcome = 3;
			break;
		}

		if (packet_in[0])
		{
			if (SDLNet_Read16(&packet_in[0]->data[0]) == PACKET_DESTRUCT_RESYNC)
			{
				const Uint16 g      = SDLNet_Read16(&packet_in[0]->data[4]);
				const Uint16 ix     = SDLNet_Read16(&packet_in[0]->data[6]);
				const Uint16 reason = SDLNet_Read16(&packet_in[0]->data[8]);
				network_update();
				if (ix == DRB_RS_ACK && g == resync_gen)
				{
					outcome = 1;
				}
				else if (ix == DRB_RS_NAK && g == resync_gen)
				{
					if (reason == DRB_NAK_FATAL)
					{
						/* No stream we can build will ever pass its check.  Retire recovery on
						 * this side too, so the remaining attempts are not spent re-sending the
						 * same refused bytes at a peer that has stopped listening for them. */
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
			else
			{
				network_update();  /* nothing else talks on this channel during a battle */
			}
			continue;
		}

		/* Keep half the reliable queue free because receipt acks precede consumption. */
		while (sent < chunks && network_ack_backlog() < NET_PACKET_QUEUE / 2)
		{
			drb_resync_send_chunk(stream, total, chunks, sent);
			++sent;
		}

		/* Transport delivery is not adoption; wait for the joiner's explicit resync ACK. */
		if (sent == chunks && network_ack_backlog() == 0)
		{
			if (delivered_at == 0)
			{
				delivered_at = SDL_GetTicks();
			}
			else if (SDL_GetTicks() - delivered_at > DRB_RS_ADOPT_TIME_OUT)
			{
				snprintf(fail_ctx, sizeof(fail_ctx),
				         "the joiner took the whole stream (%lu chunks) but never answered "
				         "within %d ms; not adopted, so our battle stands",
				         (unsigned long)chunks, DRB_RS_ADOPT_TIME_OUT);
				fail = fail_ctx;
				outcome = 2;
			}
		}
	}

	free(stream);

	if (outcome != 1 && fail != NULL)
	{
		char line[224];
		snprintf(line, sizeof(line),
		         "host attempt %lu of %d gen %u failed: %s  (%lu/%lu chunks sent, %lu ms)",
		         (unsigned long)resync_used, DRB_RS_MAX, (unsigned)resync_gen, fail,
		         (unsigned long)sent, (unsigned long)chunks,
		         (unsigned long)(SDL_GetTicks() - wait_start));
		crashlog_netlog_line("DESTRUCT RESYNC ABORT", line);
	}

	if (outcome == 1)
	{
		char detail[224];
		snprintf(detail, sizeof(detail),
		         "host streamed frame %lu state: %lu bytes (%lu compressed, %lu chunks) "
		         "in %lu ms, attempt %lu of %d",
		         (unsigned long)drb_cur, (unsigned long)state_bytes, (unsigned long)comp_sz,
		         (unsigned long)chunks, (unsigned long)(SDL_GetTicks() - wait_start),
		         (unsigned long)resync_used, DRB_RS_MAX);
		crashlog_note_net("DESTRUCT RESYNC", detail);

		drb_reset_core();
		return 1;
	}
	return outcome == 2 ? 0 : -1;
}

/* Host: run attempts until one lands or the round's budget is spent.  True = the timeline was
 * reset; the caller presents and starts frame 1. */
static bool drb_resync_host_run(void)
{
	resync_wanted = false;
	while (resync_used < DRB_RS_MAX)
	{
		const int r = drb_resync_send_once();
		if (r == 1)
			return true;
		if (r < 0)
			break;  /* The failing step already logged the cause. */
	}
	if (resync_used >= DRB_RS_MAX)
		crashlog_netlog_line("DESTRUCT RESYNC GIVE-UP",
		                     "host: attempt budget spent; playing on with divergent state");
	resync_notice = false;  /* no recovery is coming; stop announcing one */
	return false;
}

/* Joiner: packet_in[0] holds a resync chunk; assemble, validate, adopt.
 * True = adopted and timeline reset. */
static bool drb_resync_receive(void)
{
	Uint8 *comp = NULL, *raw = NULL;
	Uint32 comp_total = 0, want_crc = 0;
	Uint32 chunks = 0, next = 0;
	size_t got = 0;
	Uint16 gen = 0, seen_gen = 0;
	bool have_hdr = false;
	bool assembled = false, adopted = false;

	/* Failure cause for the one-line abort entry; every break below names one. */
	const char *abort = "unknown";
	char abort_ctx[160];

	const Uint32 wait_start = SDL_GetTicks();
	Uint32 last_progress = wait_start;
	bool reported = false;

	++resync_used;
	drb_notice_resync();

	for (;;)
	{
		if (packet_in[0] == NULL)
		{
			char why[112];
			if (have_hdr)
				snprintf(why, sizeof(why), "receiving the battle state: %lu/%lu chunks (gen %u)",
				         (unsigned long)next, (unsigned long)chunks, (unsigned)gen);
			else
				snprintf(why, sizeof(why), "receiving the battle state: no preamble yet (last gen seen %u)",
				         (unsigned)seen_gen);
			if (!drb_resync_pump(wait_start, &reported, why))
			{
				abort = "whole-attempt timeout";
				break;
			}
			if (SDL_GetTicks() - last_progress > DRB_RS_PROGRESS_TIME_OUT)
			{
				abort = "no chunk for 8 s (host stream dead, or its attempt budget spent)";
				break;
			}
			continue;
		}

		if (SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_DESTRUCT_RESYNC)
		{
			network_update();  /* nothing else talks on this channel during a battle */
			continue;
		}

		const int    len = packet_in[0]->len;
		const Uint16 g   = SDLNet_Read16(&packet_in[0]->data[4]);
		const Uint16 ix  = SDLNet_Read16(&packet_in[0]->data[6]);
		const Uint16 cn  = SDLNet_Read16(&packet_in[0]->data[8]);
		const Uint16 pl  = SDLNet_Read16(&packet_in[0]->data[10]);
		seen_gen = g;

		if (ix == DRB_RS_NAK || ix == DRB_RS_ACK || len < DRB_RS_HDR + pl || pl > DRB_RS_PAYLOAD)
		{
			network_update();  /* our own NAK/ACK echoed back, or malformed: drop */
			continue;
		}

		if (ix == 0)
		{
			/* First chunk of a (possibly restarted) stream: the preamble. */
			const Uint8 *p = &packet_in[0]->data[DRB_RS_HDR];
			if (pl < DRB_RS_PRE)
			{
				abort = "malformed preamble chunk";
				network_update();
				break;
			}
			const Uint32 their_state = SDLNet_Read32(&p[0]);
			comp_total = SDLNet_Read32(&p[4]);
			want_crc   = SDLNet_Read32(&p[8]);

			/* Layout guard: a different build (or word size) lays the battle out differently, and
			 * its bytes must never be restored here. */
			if (their_state != (Uint32)state_bytes ||
			    comp_total == 0 || comp_total > (Uint32)(state_bytes + state_bytes / 255 + 64))
			{
				/* Not a link failure: no retry against this peer can pass this test, so retire
				 * recovery rather than spend the budget re-streaming the same refused bytes. */
				resync_layout_bad = true;
				snprintf(abort_ctx, sizeof(abort_ctx),
				         "preamble refused: peer state %lu bytes vs ours %lu, compressed %lu "
				         "(mismatched builds, or PC<->console); recovery disabled for this session",
				         (unsigned long)their_state, (unsigned long)state_bytes,
				         (unsigned long)comp_total);
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

			const size_t body = (size_t)pl - DRB_RS_PRE;
			if (body > comp_total)
			{
				abort = "preamble body overflows the announced compressed size";
				network_update();
				break;
			}
			memcpy(comp, p + DRB_RS_PRE, body);
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
				/* The reliable channel delivers in order, so a skipped index means a chunk was
				 * acknowledged into a full inbound queue and dropped before we started consuming. */
				snprintf(abort_ctx, sizeof(abort_ctx),
				         "chunk index skip: got %u, expected %lu (a chunk was acked into our full "
				         "inbound queue and dropped)",
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
			memcpy(comp + got, &packet_in[0]->data[DRB_RS_HDR], pl);
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
			else if (drb_rs_hash(comp, comp_total) != want_crc)
			{
				abort = "checksum mismatch on the assembled stream";
			}
			else
			{
				raw = malloc(state_bytes);
				adopted = raw != NULL
				       && nrb_resync_expand(comp, comp_total, raw, state_bytes) == state_bytes;
				if (adopted)
					state_restore(raw);
				else
					abort = "state adopt failed (expand or memory)";
			}
			break;
		}
	}

	free(comp);
	free(raw);

	if (adopted)
	{
		resync_gen = gen;

		/* Before the reset, and before we present: the host is blocked waiting for exactly this,
		 * and it is the only evidence it will ever get that the bytes were taken rather than
		 * merely delivered. */
		drb_resync_send_ack(gen);

		char detail[192];
		snprintf(detail, sizeof(detail),
		         "joiner adopted the host battle: gen %u, %lu bytes, %lu ms, attempt %lu of %d",
		         (unsigned)gen, (unsigned long)state_bytes,
		         (unsigned long)(SDL_GetTicks() - wait_start),
		         (unsigned long)resync_used, DRB_RS_MAX);
		crashlog_note_net("DESTRUCT RESYNC", detail);

		drb_reset_core();
		return true;
	}

	/* One line per failed attempt, so the log names the culprit even when the battle plays on. */
	char line[288];
	snprintf(line, sizeof(line),
	         "joiner attempt %lu of %d failed: %s  (gen %u, %lu/%lu chunks, %lu ms)%s",
	         (unsigned long)resync_used, DRB_RS_MAX, abort,
	         (unsigned)(have_hdr ? gen : seen_gen),
	         (unsigned long)next, (unsigned long)chunks,
	         (unsigned long)(SDL_GetTicks() - wait_start),
	         resync_used > DRB_RS_MAX ? "   [over budget: started by stray chunks]" : "");
	crashlog_netlog_line("DESTRUCT RESYNC ABORT", line);

	/* Report whether a retry can help. A layout refusal retires recovery on the host. */
	drb_resync_send_nak(have_hdr ? gen : seen_gen,
	                    resync_layout_bad ? DRB_NAK_FATAL : DRB_NAK_RETRY);

	/* Spent: the host has no attempt left to answer that NAK with. */
	if (resync_used >= DRB_RS_MAX || resync_layout_bad)
		resync_notice = false;

	/* A refused stream leaves both peers on the old timeline; halt only after exhausting retries. */
	if (assembled && resync_used >= DRB_RS_MAX)
		network_tyrian_halt(7, false);

	return false;
}

/* Dispatch the queued resync packet.  True means adoption reset the timeline. */
static bool drb_resync_dispatch(void)
{
	if (packet_in[0] == NULL
	    || SDLNet_Read16(&packet_in[0]->data[0]) != PACKET_DESTRUCT_RESYNC)
		return false;

	if (!nrb_session_recovery())
	{
		network_update();  /* stray: keep the reliable queue's head moving */
		return false;
	}

	/* Drop leftover chunks once the attempt budget or the session layout has made adoption
	 * impossible. */
	if (!network_is_host && (resync_used >= DRB_RS_MAX || resync_layout_bad))
	{
		/* Reject a permanently incompatible restart at chunk 0; ignore its remaining chunks. */
		if (resync_layout_bad && SDLNet_Read16(&packet_in[0]->data[6]) == 0)
			drb_resync_send_nak(SDLNet_Read16(&packet_in[0]->data[4]), DRB_NAK_FATAL);
		network_update();
		return false;
	}

	if (network_is_host)
	{
		const Uint16 g      = SDLNet_Read16(&packet_in[0]->data[4]);
		const Uint16 ix     = SDLNet_Read16(&packet_in[0]->data[6]);
		const Uint16 reason = SDLNet_Read16(&packet_in[0]->data[8]);
		network_update();
		if (ix == DRB_RS_NAK && g == resync_gen)
		{
			if (reason == DRB_NAK_FATAL)
			{
				/* Arrived outside the streaming loop: a late refusal, or the joiner answering a
				 * stream restart it had already given up on. */
				if (!resync_layout_bad)
				{
					resync_layout_bad = true;
					crashlog_netlog_line("DESTRUCT RESYNC GIVE-UP",
					                     "host: joiner refused permanently (layout/build mismatch); "
					                     "recovery disabled for this session");
				}
				return false;
			}
			if (resync_used < DRB_RS_MAX)
				return drb_resync_host_run();
			crashlog_netlog_line("DESTRUCT RESYNC GIVE-UP",
			                     "host: joiner NAK ignored, attempt budget already spent this round");
		}
		return false;
	}

	return drb_resync_receive();
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

	/* Desync recovery rendezvous, after the two irreversible transitions above: a round that is
	 * already ending has nothing left to repair. */
	if (drb_resync_dispatch())
		return DRB_STEP_PRESENT;
	if (resync_wanted)
	{
		resync_wanted = false;
		if (network_is_host && drb_resync_host_run())
			return DRB_STEP_PRESENT;
	}
	if (resync_notice && drb_epoch != resync_notice_epoch && remote_newest > 0)
		resync_notice = false;

	/* The round ended on this timeline.  Hold until every frame behind the verdict is confirmed,
	 * or a mispredicted last shot would reload one machine's map and not the other's. */
	if (roundOver)
	{
		const Uint32 wait_start = SDL_GetTicks();
		Uint32 newest_seen = remote_newest;
		Uint32 newest_tick = wait_start;
		bool stall_reported = false;

		while (verified_upto < drb_cur)
		{
			if (drb_stall_pump(wait_start, &stall_reported, "waiting to confirm the round end"))
				return DRB_STEP_PRESENT;

			const Uint32 K = drb_scan_mispredict();
			if (K != 0)
				return drb_begin_resim(K);

			/* A peer that stopped producing frames has moved on or left; end the round rather
			 * than wedge. */
			if (nrb_peer_idle(SDL_GetTicks(), remote_newest, &newest_seen, &newest_tick))
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
			if (drb_stall_pump(wait_start, &stall_reported, "peer too far behind"))
				return DRB_STEP_PRESENT;

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
	selftest_probe_done = selftest_probe_ok = false;
	selftest_probe_raw = selftest_probe_comp = 0;
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

bool drb_selftest_resync_ok(void)
{
	return selftest_probe_ok;
}

void drb_selftest_resync_bytes(size_t *rawBytes, size_t *compressedBytes)
{
	*rawBytes = selftest_probe_raw;
	*compressedBytes = selftest_probe_comp;
}

/* Take the live battle through the recovery's serialization and back.  A terrain buffer that has
 * been shot at is the realistic case for both halves: whether the round trip is exact, and what a
 * transfer actually costs once the zero-run coder has seen real craters. */
static void drb_selftest_resync_probe(void)
{
	selftest_probe_done = true;

	const size_t cap = state_bytes + state_bytes / 255 + 64;
	Uint8 *const raw  = malloc(state_bytes);
	Uint8 *const comp = malloc(cap);
	Uint8 *const back = malloc(state_bytes);
	if (raw == NULL || comp == NULL || back == NULL)
	{
		free(raw);
		free(comp);
		free(back);
		return;
	}

	state_save(raw);
	const size_t comp_sz = nrb_resync_compress(raw, state_bytes, comp, cap);
	selftest_probe_raw = state_bytes;
	selftest_probe_comp = comp_sz;
	selftest_probe_ok = comp_sz != 0
	                 && nrb_resync_expand(comp, comp_sz, back, state_bytes) == state_bytes
	                 && memcmp(raw, back, state_bytes) == 0;

	free(raw);
	free(comp);
	free(back);
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
	++selftest_run;

	/* Halfway through the run, so the probe sees terrain that has been shot at. */
	if (!selftest_probe_done && selftest_run * 2 >= selftest_budget)
		drb_selftest_resync_probe();

	if (selftest_run >= selftest_budget)
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
	fprintf(f, "    recovery        : %s, %lu of %d attempts used this round, gen %u%s%s\n",
	        nrb_session_recovery() ? "on" : "off",
	        (unsigned long)resync_used, DRB_RS_MAX, (unsigned)resync_gen,
	        resync_layout_bad ? ", retired (layout mismatch)" : "",
	        resync_wanted ? "   RESYNC PENDING" : "");
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
bool drb_selftest_resync_ok(void) { return false; }

void drb_selftest_resync_bytes(size_t *rawBytes, size_t *compressedBytes)
{
	*rawBytes = 0;
	*compressedBytes = 0;
}

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
