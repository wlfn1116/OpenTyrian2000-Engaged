/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback state engine.  See rollback.h for the design overview.
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
#include "rollback.h"

#include "crashlog.h"
#include "endless.h"
#include "net_rollback.h"
#include "network.h"
#include "opentyr.h"
#include "varz.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool rollback_resim = false;
bool rollback_resim_silent = false;

bool rollback_selftest = false;
unsigned long rollback_selftest_ticks = 0, rollback_selftest_failures = 0;

/* Self-test results go to their own file next to the executable: this build is
 * a Windows-subsystem app, so stderr is a black hole. */
static void rb_log(const char *fmt, ...)
{
	static FILE *f;
	if (!f)
	{
		f = fopen("rollback_selftest.log", "a");
		if (!f)
			return;
	}

	va_list ap;
	va_start(ap, fmt);
	vfprintf(f, fmt, ap);
	va_end(ap);
	fputc('\n', f);
	fflush(f);
}

/* --- Registry ----------------------------------------------------------------- */

typedef struct
{
	const char *name;
	void       *ptr;                       /* raw entries                      */
	void      (*save)(void *dst);          /* callback entries                 */
	void      (*restore)(const void *src);
	size_t      size;
	size_t      offset;                    /* position within a snapshot slot  */
}
RbItem;

#define RB_MAX_ITEMS  512
#define RB_MAX_FIXUPS 8

static RbItem rb_items[RB_MAX_ITEMS];
static int    rb_item_count = 0;
static size_t rb_total_size = 0;
static bool   rb_registered = false;

/* Run after every restore, for state that must be re-derived rather than
 * copied (interior pointers like player[].lives). */
static void (*rb_fixups[RB_MAX_FIXUPS])(void);
static int rb_fixup_count = 0;

void rollback_register(const char *name, void *ptr, size_t size)
{
	if (rb_item_count >= RB_MAX_ITEMS)
	{
		fprintf(stderr, "rollback: item table full registering %s\n", name);
		exit(1);
	}
	RbItem *it = &rb_items[rb_item_count++];
	it->name = name;
	it->ptr = ptr;
	it->save = NULL;
	it->restore = NULL;
	it->size = size;
	it->offset = rb_total_size;
	rb_total_size += size;
}

void rollback_register_callback(const char *name, size_t size,
                                void (*save)(void *dst), void (*restore)(const void *src))
{
	rollback_register(name, NULL, size);
	rb_items[rb_item_count - 1].save = save;
	rb_items[rb_item_count - 1].restore = restore;
}

void rollback_register_fixup(void (*fn)(void))
{
	if (rb_fixup_count < RB_MAX_FIXUPS)
		rb_fixups[rb_fixup_count++] = fn;
}

size_t rollback_state_size(void)
{
	return rb_total_size;
}

/* --- Snapshot ring ------------------------------------------------------------ */

static Uint8 *rb_ring[ROLLBACK_RING];
static Uint32 rb_ring_frame[ROLLBACK_RING];
static bool   rb_ring_valid[ROLLBACK_RING];

/* Extra buffer for the self-test's "state after the live pass" reference. */
static Uint8 *rb_verify_buf;

static void rb_alloc_buffers(void)
{
	for (int i = 0; i < ROLLBACK_RING; ++i)
	{
		rb_ring[i] = malloc(rb_total_size);
		if (!rb_ring[i])
		{
			fprintf(stderr, "rollback: out of memory (%zu x %d)\n", rb_total_size, ROLLBACK_RING);
			exit(1);
		}
	}
	rb_verify_buf = malloc(rb_total_size);
	if (!rb_verify_buf)
	{
		fprintf(stderr, "rollback: out of memory (verify buffer)\n");
		exit(1);
	}
}

static void rb_save_to(Uint8 *buf)
{
	for (int i = 0; i < rb_item_count; ++i)
	{
		RbItem *it = &rb_items[i];
		if (it->save)
			it->save(buf + it->offset);
		else
			memcpy(buf + it->offset, it->ptr, it->size);
	}
}

static void rb_restore_from(const Uint8 *buf)
{
	for (int i = 0; i < rb_item_count; ++i)
	{
		RbItem *it = &rb_items[i];
		if (it->restore)
			it->restore(buf + it->offset);
		else
			memcpy(it->ptr, buf + it->offset, it->size);
	}
	for (int i = 0; i < rb_fixup_count; ++i)
		rb_fixups[i]();
}

void rollback_ring_reset(void)
{
	for (int i = 0; i < ROLLBACK_RING; ++i)
		rb_ring_valid[i] = false;
}

void rollback_snapshot(Uint32 frame)
{
	const int slot = (int)(frame % ROLLBACK_RING);
	rb_save_to(rb_ring[slot]);
	rb_ring_frame[slot] = frame;
	rb_ring_valid[slot] = true;
}

bool rollback_have_frame(Uint32 frame)
{
	const int slot = (int)(frame % ROLLBACK_RING);
	return rb_ring_valid[slot] && rb_ring_frame[slot] == frame;
}

bool rollback_restore(Uint32 frame)
{
	const int slot = (int)(frame % ROLLBACK_RING);
	if (!rb_ring_valid[slot] || rb_ring_frame[slot] != frame)
		return false;
	rb_restore_from(rb_ring[slot]);
	return true;
}

/* Compare live state to a reference buffer item by item.  Returns the number of
 * mismatching items; details for the first few land in `out`. */
static int rb_verify_against(const Uint8 *ref, char *out, size_t outsz)
{
	int bad = 0;
	size_t used = 0;

	for (int i = 0; i < rb_item_count; ++i)
	{
		RbItem *it = &rb_items[i];
		bool differs;
		size_t first_diff = 0;

		if (it->save)
		{
			/* Callback state has no live pointer; save it fresh and compare. */
			Uint8 *tmp = malloc(it->size);
			if (!tmp)
				continue;
			it->save(tmp);
			differs = memcmp(tmp, ref + it->offset, it->size) != 0;
			if (differs)
				while (first_diff < it->size && tmp[first_diff] == ref[it->offset + first_diff])
					++first_diff;
			free(tmp);
		}
		else
		{
			differs = memcmp(it->ptr, ref + it->offset, it->size) != 0;
			if (differs)
			{
				const Uint8 *live = it->ptr;
				while (first_diff < it->size && live[first_diff] == ref[it->offset + first_diff])
					++first_diff;
			}
		}

		if (differs)
		{
			++bad;
			if (out && bad <= 8)
			{
				/* Hex context: 8 bytes from the diff onward, live vs reference,
				 * so a report names not just the item but the actual values. */
				char live_hex[3 * 8 + 1] = "", ref_hex[3 * 8 + 1] = "";
				const Uint8 *live = it->ptr;   /* NULL for callback items */
				for (size_t b = 0; live && b < 8 && first_diff + b < it->size; ++b)
				{
					snprintf(live_hex + b * 3, 4, "%02x ", live[first_diff + b]);
					snprintf(ref_hex + b * 3, 4, "%02x ", ref[it->offset + first_diff + b]);
				}

				const int n = snprintf(out + used, outsz - used,
				                       "  %s: first diff at byte %zu of %zu (live %s/ ref %s)\n",
				                       it->name, first_diff, it->size, live_hex, ref_hex);
				if (n > 0 && (size_t)n < outsz - used)
					used += (size_t)n;
			}
		}
	}
	return bad;
}

/* --- Per-tick input tuples (self-test) ----------------------------------------
 *
 * The self-test replays exactly one tick, so a single record (not a ring) is
 * enough: both players' tuples plus tick-wide event bits.
 */
static RbInput st_input[2];
static Uint16  st_event_bits;

void rollback_st_record(int player0, const RbInput *in)
{
	Uint16 events = st_input[player0].buttons & (RB_EV_DEMO_END | RB_EV_DISMISS);
	st_input[player0] = *in;
	st_input[player0].buttons |= events;  /* events recorded before the tuple survive */
}

void rollback_st_record_sf(int player0, Sint16 tx, Sint16 ty)
{
	st_input[player0].sfTx = tx;
	st_input[player0].sfTy = ty;
}

void rollback_st_event(Uint16 bit)
{
	st_input[0].buttons |= bit;
	st_event_bits |= bit;
}

const RbInput *rollback_st_get(int player0)
{
	return &st_input[player0];
}

Uint16 rollback_st_events(void)
{
	return st_event_bits;
}

/* --- Self-test driver --------------------------------------------------------- */

static Uint32 st_frame = 0;
static bool   st_verifying = false;   /* replay pass in flight             */
static bool   st_tainted = false;
static char   st_taint_why[64];
static bool   st_level_active = false;

bool rollback_selftest_active(void)
{
	return rollback_selftest && st_level_active && !isNetworkGame && !endlessFxActive();
}

/* Temporary diagnostic probe for self-test divergence hunting: logs which pass
 * executed a tagged site.  Cheap no-op unless the self-test is armed. */
void rollback_dbg(const char *tag, int a, int b)
{
	if (!rollback_selftest_active())
		return;
	rb_log("dbg tick %lu pass %d: %s a=%d b=%d",
	       (unsigned long)st_frame, rollback_resim ? 2 : 1, tag, a, b);
}

void rollback_taint(const char *why)
{
	if (!st_tainted)
	{
		st_tainted = true;
		snprintf(st_taint_why, sizeof(st_taint_why), "%s", why ? why : "?");
		if (rollback_selftest_active())
			rb_log("taint: tick %lu skipped (%s)", (unsigned long)(st_frame), st_taint_why);
	}
}

void rollback_level_start(void)
{
	st_frame = 0;
	st_verifying = false;
	st_tainted = false;
	rollback_resim = false;
	rollback_resim_silent = false;
	memset(st_input, 0, sizeof(st_input));
	st_event_bits = 0;

	/* Registration + the multi-megabyte ring exist only when something will
	 * actually use them; pure single-player play without the self-test costs
	 * nothing.  The endless effect layer (zone timers, gravity carries, damage
	 * over time) sits outside the registry by design, so a replayed tick would
	 * advance it a second time -- the self-test does not arm there. */
	const bool selftest_will_arm = rollback_selftest && !isNetworkGame && !endlessFxActive();

	if (selftest_will_arm || (isNetworkGame && nrb_session_mode()))
	{
		rollback_register_all();
		rollback_ring_reset();
	}

	if (selftest_will_arm)
		rb_log("level start: selftest armed (state %zu bytes, demo=%s)",
		       rollback_state_size(), play_demo ? "yes" : "no");

	st_level_active = true;
}

void rollback_selftest_frame_begin(void)
{
	if (!rollback_selftest_active())
		return;
	if (st_verifying)
		return;  /* replay pass re-entering the loop: keep the same frame */

	++st_frame;
	memset(st_input, 0, sizeof(st_input));
	st_event_bits = 0;
	rollback_snapshot(st_frame);
}

bool rollback_selftest_tick(void)
{
	if (!rollback_selftest_active())
		return false;

	if (!st_verifying)
	{
		/* Live pass just finished. */
		if (st_tainted)
		{
			/* A modal UI or live cheat mutated state outside the tuples this
			 * tick; a replay could not match.  Skip verification, stay live. */
			st_tainted = false;
			return false;
		}
		if (!rollback_have_frame(st_frame))
			return false;

		rb_save_to(rb_verify_buf);          /* the answer the replay must hit */
		rollback_restore(st_frame);
		rollback_resim = true;              /* replay: no sound/live input    */
		st_verifying = true;
		return true;                        /* caller: re-run the tick        */
	}

	/* Replay pass just finished: every registered byte must match. */
	rollback_resim = false;
	st_verifying = false;
	++rollback_selftest_ticks;

	{
		char detail[640];
		const int bad = rb_verify_against(rb_verify_buf, detail, sizeof(detail));
		if (bad != 0)
		{
			++rollback_selftest_failures;
			char msg[768];
			snprintf(msg, sizeof(msg),
			         "tick %lu: %d registered item(s) diverged on replay\n%s",
			         (unsigned long)st_frame, bad, detail);
			crashlog_note("ROLLBACK SELFTEST", msg);
			rb_log("FAIL %s"
			       "  ctx: curLoc=%u endLevel=%d levelEnd=%d reallyEnd=%d demo=%d "
			       "p0(alive=%d expl=%u invuln=%u) events=%04x",
			       msg,
			       (unsigned)curLoc, (int)endLevel, (int)levelEnd, (int)reallyEndLevel,
			       (int)play_demo,
			       (int)player[0].is_alive, player[0].exploding_ticks,
			       player[0].invulnerable_ticks, (unsigned)st_event_bits);
		}
		else if (rollback_selftest_ticks % 350 == 0)
		{
			rb_log("ok: %lu ticks verified, %lu failures (state %zu bytes)",
			       rollback_selftest_ticks, rollback_selftest_failures,
			       rollback_state_size());
		}
	}
	return false;
}

/* Called when leaving the level (start_level / menu) so the self-test cannot
 * fire outside the loop; cheap to call from anywhere. */
void rollback_level_end(void)
{
	st_level_active = false;
	st_verifying = false;
	rollback_resim = false;
	rollback_resim_silent = false;
}

/* --- Registration root --------------------------------------------------------
 *
 * The extern-global list lives in rollback_state.c; files with sim-relevant
 * statics export a <file>_register_rollback() collected here.
 */
void rollback_state_register_globals(void);   /* rollback_state.c  */
void tyrian2_register_rollback(void);         /* tyrian2.c statics */

void rollback_register_all(void)
{
	if (rb_registered)
		return;
	rb_registered = true;

	rollback_state_register_globals();
	tyrian2_register_rollback();

	rb_alloc_buffers();

	fprintf(stderr, "rollback: %d items, %zu bytes per snapshot, %d-deep ring\n",
	        rb_item_count, rb_total_size, ROLLBACK_RING);
}
