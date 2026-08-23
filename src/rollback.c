/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 *
 * Rollback state engine.  See rollback.h for the design overview.
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
#include "rollback.h"

#include "crashlog.h"
#include "endless.h"
#include "episodes.h"
#include "net_rollback.h"
#include "network.h"
#include "opentyr.h"
#include "shots.h"
#include "sprite.h"
#include "varz.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool rollback_resim = false;
bool rollback_resim_silent = false;

bool rollback_selftest = false;
unsigned long rollback_selftest_ticks = 0, rollback_selftest_failures = 0;
static unsigned long rollback_selftest_limit = 0;
static Uint32 rollback_selftest_limit_hash = 0;

/* Self-test results go to their own file next to the executable: this build is
 * a Windows-subsystem app, so stderr is a black hole. */
static FILE *rb_log_file;

static void rb_log_open(void)
{
	if (rb_log_file)
		return;
	rb_log_file = fopen("rollback_selftest.log", "a");
	if (rb_log_file)
	{
		/* Fully buffered, with a big buffer: the trace writes a line per tick for
		 * a whole demo, and on a console every unbuffered line is its own SD-card
		 * write; enough I/O to disturb the frame timing being measured. */
		static char buf[64 * 1024];
		setvbuf(rb_log_file, buf, _IOFBF, sizeof(buf));
	}
}

/* Flush points are deliberate and rare: the periodic progress line, a failure,
 * and leaving the level.  That bounds what a hard kill can lose to a few seconds
 * of trace without paying a write per line. */
static void rb_log_flush(void)
{
	if (rb_log_file)
		fflush(rb_log_file);
}

static void rb_log(const char *fmt, ...)
{
	rb_log_open();
	if (!rb_log_file)
		return;

	va_list ap;
	va_start(ap, fmt);
	vfprintf(rb_log_file, fmt, ap);
	va_end(ap);
	fputc('\n', rb_log_file);
}

/* State registry. */

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

/* Snapshot ring. */

static Uint8 *rb_ring[ROLLBACK_RING];
static Uint32 rb_ring_frame[ROLLBACK_RING];
static bool   rb_ring_valid[ROLLBACK_RING];

/* Extra buffer for the self-test's "state after the live pass" reference. */
static Uint8 *rb_verify_buf;

/* Scratch for the demo trace's pointer-relocated snapshot (see rb_item_hash). */
static Uint8 *rb_trace_buf;

static void rb_alloc_buffers(void)
{
	if (rb_ring[0] != NULL)
		return;  /* the fingerprint path can register long before anything rolls back */

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
	rb_trace_buf = malloc(rb_total_size);
	if (!rb_trace_buf)
	{
		fprintf(stderr, "rollback: out of memory (trace buffer)\n");
		exit(1);
	}
}

void rollback_deinit(void)
{
	for (int i = 0; i < ROLLBACK_RING; ++i)
	{
		free(rb_ring[i]);
		rb_ring[i] = NULL;
		rb_ring_valid[i] = false;
	}
	free(rb_verify_buf);
	rb_verify_buf = NULL;
	free(rb_trace_buf);
	rb_trace_buf = NULL;

	if (rb_log_file != NULL)
	{
		fclose(rb_log_file);
		rb_log_file = NULL;
	}

	rb_item_count = 0;
	rb_total_size = 0;
	rb_fixup_count = 0;
	rb_registered = false;
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

	// The discarded timeline may have painted the shield/armor gauges (e.g. a
	// mispredicted hit); repaint them from the restored state on the next live pass.
	hud_bars_dirty = true;
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

static int rb_verify_against(const Uint8 *ref, char *out, size_t outsz);
static bool rb_reloc_walk(Uint8 *buf, bool encode);

/* Demo traces hash relocated state to exclude process-specific pointers.
 * Keep tracing to narrow frame windows because each tick emits hundreds of rows. */
#define RB_TRACE_ITEMS_FROM 1
#define RB_TRACE_ITEMS_TO   0

/* Refresh rb_trace_buf with an address-independent copy of live state.  False if
 * the relocation failed, in which case the caller must not hash it. */
static bool rb_trace_snapshot(void)
{
	if (rb_trace_buf == NULL)
		return false;
	rb_save_to(rb_trace_buf);
	return rb_reloc_walk(rb_trace_buf, true);
}

/* FNV-1a over one entry's relocated bytes.  Raw entries only; the single
 * callback entry is the RNG, whose draw count the trace carries verbatim. */
static Uint32 rb_item_hash(const RbItem *it)
{
	Uint32 h = 2166136261u;
	const Uint8 *p = rb_trace_buf + it->offset;
	for (size_t i = 0; i < it->size; ++i)
	{
		h ^= p[i];
		h *= 16777619u;
	}
	return h;
}

/* Registry entries that only a co-op session moves. The trace hash skips them while no such
 * session is running, so single-player replay fixtures keep hashing the byte stream they were
 * recorded against. Endless entries all carry the "endless." prefix. */
static bool rb_item_is_coop_only(const char *name)
{
	return strcmp(name, "coopCampaignMode") == 0
	    || strcmp(name, "coopEndlessMode") == 0
	    || strncmp(name, "endless.", 8) == 0;
}

Uint32 rollback_state_hash(void)
{
	if (rb_ring[0] == NULL)
		rollback_register_all();
	if (!rb_trace_snapshot())
		return 0;

	Uint32 h = 2166136261u;
	for (int item = 0; item < rb_item_count; ++item)
	{
		const RbItem *const it = &rb_items[item];
		const Uint8 *const bytes = rb_trace_buf + it->offset;

		/* Legacy replay fixtures hash the pre-co-op registry byte stream. Preserve that projection
		 * while no dual-ship session is running; the per-ship block it skips is live state in
		 * Separate arcade as much as in co-op, and the co-op canaries cover the rest. */
		if (!dual_ship_mode() && rb_item_is_coop_only(it->name))
			continue;
		if (!dual_ship_mode() && strcmp(it->name, "player") == 0 && it->size == sizeof(player))
		{
			const size_t prefix = offsetof(Player, generator_power);
			const size_t suffix = offsetof(Player, x);
			const size_t legacy_payload = prefix + sizeof(Player) - suffix;
			const size_t legacy_stride = (legacy_payload + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
			for (uint p = 0; p < COUNTOF(player); ++p)
			{
				const Uint8 *const player_bytes = bytes + p * sizeof(Player);
				for (size_t i = 0; i < prefix; ++i)
				{
					h ^= player_bytes[i];
					h *= 16777619u;
				}
				for (size_t i = suffix; i < sizeof(Player); ++i)
				{
					h ^= player_bytes[i];
					h *= 16777619u;
				}
				for (size_t i = legacy_payload; i < legacy_stride; ++i)
				{
					h ^= 0;
					h *= 16777619u;
				}
			}
			continue;
		}

		for (size_t i = 0; i < it->size; ++i)
		{
			h ^= bytes[i];
			h *= 16777619u;
		}
	}
	return h;
}

void rollback_selftest_set_limit(unsigned long ticks)
{
	rollback_selftest_limit = ticks;
	rollback_selftest_limit_hash = 0;
}

Uint32 rollback_selftest_bounded_hash(void)
{
	return rollback_selftest_limit_hash;
}

/* Encode registered pointers as stable sprite-sheet ids, enemy or map offsets,
 * and player indices. mapYPos may legally be one element before its array. */

enum
{
	RB_RELOC_SHEET,   /* Sprite2_array*: rb_sheet_tab index                     */
	RB_RELOC_SHIP,    /* Sprite2_array*: rb_ship_tab index                      */
	RB_RELOC_EDAT,    /* void*: element index into enemyDat                     */
	RB_RELOC_LIVES,   /* JE_byte*: player index; points inside player[].items   */
	RB_RELOC_MAP1,    /* JE_byte**: element offset into megaData1.mainmap       */
	RB_RELOC_MAP2,
	RB_RELOC_MAP3,
};

#define RB_CODE_NULL 0xFFFFFFFFu   /* NULL for the EDAT and MAP kinds */

/* Index 0 is NULL so a zero code is never a valid live pointer by accident. */
static Sprite2_array *const rb_sheet_tab[] = {
	NULL, &spriteSheet10, &spriteSheet11,
	&enemySpriteSheets[0], &enemySpriteSheets[1],
	&enemySpriteSheets[2], &enemySpriteSheets[3],
};
static Sprite2_array *const rb_ship_tab[] = { NULL, &spriteSheet9, &spriteSheetT2000 };

typedef struct
{
	const char *item;      /* registry name                            */
	size_t      field_ofs; /* field offset within one element          */
	size_t      stride;    /* element stride (0: single field, count 1) */
	int         count;
	int         kind;
}
RbReloc;

static const RbReloc rb_relocs[] = {
	{ "player",     offsetof(Player, lives),                            sizeof(Player),                    COUNTOF(player), RB_RELOC_LIVES },
	{ "enemy",      offsetof(struct JE_SingleEnemyType, sprite2s),    sizeof(struct JE_SingleEnemyType), 100, RB_RELOC_SHEET },
	{ "enemy",      offsetof(struct JE_SingleEnemyType, enemydatofs), sizeof(struct JE_SingleEnemyType), 100, RB_RELOC_EDAT  },
	{ "shipGrPtr",  0, 0, 1, RB_RELOC_SHIP },
	{ "shipGr2ptr", 0, 0, 1, RB_RELOC_SHIP },
	{ "mapYPos",    0, 0, 1, RB_RELOC_MAP1 },
	{ "mapY2Pos",   0, 0, 1, RB_RELOC_MAP2 },
	{ "mapY3Pos",   0, 0, 1, RB_RELOC_MAP3 },
	{ "BKwrap1",    0, 0, 1, RB_RELOC_MAP1 },
	{ "BKwrap1to",  0, 0, 1, RB_RELOC_MAP1 },
	{ "BKwrap2",    0, 0, 1, RB_RELOC_MAP2 },
	{ "BKwrap2to",  0, 0, 1, RB_RELOC_MAP2 },
	{ "BKwrap3",    0, 0, 1, RB_RELOC_MAP3 },
	{ "BKwrap3to",  0, 0, 1, RB_RELOC_MAP3 },
};

static void rb_map_base(int kind, JE_byte ***base, ptrdiff_t *count)
{
	switch (kind)
	{
	case RB_RELOC_MAP1: *base = &megaData1.mainmap[0][0]; *count = 300 * 14; break;
	case RB_RELOC_MAP2: *base = &megaData2.mainmap[0][0]; *count = 600 * 14; break;
	default:            *base = &megaData3.mainmap[0][0]; *count = 600 * 15; break;
	}
}

/* The pointer field is sizeof(void*) bytes; the code always occupies the first
 * four with the rest zeroed, so encode/decode is layout-symmetric per build. */
static Uint32 rb_field_read_code(const Uint8 *field)
{
	Uint32 code;
	memcpy(&code, field, sizeof(code));
	return code;
}

static void rb_field_write_code(Uint8 *field, Uint32 code)
{
	memset(field, 0, sizeof(void *));
	memcpy(field, &code, sizeof(code));
}

/* Encode one pointer field in the buffer; false = a pointer with no known home. */
static bool rb_reloc_encode_field(Uint8 *field, int kind)
{
	void *v;
	memcpy(&v, field, sizeof(v));

	switch (kind)
	{
	case RB_RELOC_SHEET:
	case RB_RELOC_SHIP:
	{
		Sprite2_array *const *tab = (kind == RB_RELOC_SHEET) ? rb_sheet_tab : rb_ship_tab;
		const int n = (kind == RB_RELOC_SHEET) ? (int)COUNTOF(rb_sheet_tab) : (int)COUNTOF(rb_ship_tab);
		for (int i = 0; i < n; ++i)
		{
			if (v == tab[i])
			{
				rb_field_write_code(field, (Uint32)i);
				return true;
			}
		}
		return false;
	}
	case RB_RELOC_EDAT:
	{
		if (v == NULL)
		{
			rb_field_write_code(field, RB_CODE_NULL);
			return true;
		}
		const ptrdiff_t byte_d = (char *)v - (char *)&enemyDat[0];
		if (byte_d < 0 || byte_d % (ptrdiff_t)sizeof(enemyDat[0]) != 0)
			return false;
		const ptrdiff_t idx = byte_d / (ptrdiff_t)sizeof(enemyDat[0]);
		if (idx > ENEMY_NUM)
			return false;
		rb_field_write_code(field, (Uint32)idx);
		return true;
	}
	case RB_RELOC_LIVES:
	{
		for (uint i = 0; i < COUNTOF(player); ++i)
		{
			if (v == &player[i].items.weapon[i].power)
			{
				rb_field_write_code(field, i);
				return true;
			}
		}
		return false;
	}
	default:
	{
		if (v == NULL)
		{
			rb_field_write_code(field, RB_CODE_NULL);
			return true;
		}
		JE_byte **base;
		ptrdiff_t count;
		rb_map_base(kind, &base, &count);
		const ptrdiff_t d = (JE_byte **)v - base;
		if (d < -1 || d > count)  /* -1: the level-init bias; count: one-past */
			return false;
		rb_field_write_code(field, (Uint32)(Sint32)d);
		return true;
	}
	}
}

static bool rb_reloc_decode_field(Uint8 *field, int kind)
{
	const Uint32 code = rb_field_read_code(field);
	void *v;

	switch (kind)
	{
	case RB_RELOC_SHEET:
	case RB_RELOC_SHIP:
	{
		Sprite2_array *const *tab = (kind == RB_RELOC_SHEET) ? rb_sheet_tab : rb_ship_tab;
		const Uint32 n = (kind == RB_RELOC_SHEET) ? (Uint32)COUNTOF(rb_sheet_tab) : (Uint32)COUNTOF(rb_ship_tab);
		if (code >= n)
			return false;
		v = tab[code];
		break;
	}
	case RB_RELOC_EDAT:
		if (code == RB_CODE_NULL)
			v = NULL;
		else if (code > ENEMY_NUM)
			return false;
		else
			v = &enemyDat[code];
		break;
	case RB_RELOC_LIVES:
		if (code >= COUNTOF(player))
			return false;
		v = &player[code].items.weapon[code].power;
		break;
	default:
	{
		if (code == RB_CODE_NULL)
		{
			v = NULL;
			break;
		}
		JE_byte **base;
		ptrdiff_t count;
		rb_map_base(kind, &base, &count);
		const Sint32 d = (Sint32)code;
		if (d < -1 || (ptrdiff_t)d > count)
			return false;
		v = base + d;
		break;
	}
	}

	memset(field, 0, sizeof(void *));
	memcpy(field, &v, sizeof(v));
	return true;
}

/* Run every relocation over a snapshot buffer, one direction. */
static bool rb_reloc_walk(Uint8 *buf, bool encode)
{
	for (size_t r = 0; r < COUNTOF(rb_relocs); ++r)
	{
		const RbReloc *rel = &rb_relocs[r];

		const RbItem *it = NULL;
		for (int i = 0; i < rb_item_count; ++i)
		{
			if (strcmp(rb_items[i].name, rel->item) == 0)
			{
				it = &rb_items[i];
				break;
			}
		}
		if (it == NULL)
			return false;  /* registry and reloc table out of step */

		for (int e = 0; e < rel->count; ++e)
		{
			const size_t ofs = it->offset + rel->field_ofs + rel->stride * (size_t)e;
			if (ofs + sizeof(void *) > it->offset + it->size)
				return false;

			if (encode ? !rb_reloc_encode_field(buf + ofs, rel->kind)
			           : !rb_reloc_decode_field(buf + ofs, rel->kind))
				return false;
		}
	}
	return true;
}

bool rollback_wire_export(Uint8 *dst)
{
	if (!rb_registered)
		return false;

	rb_save_to(dst);
	if (!rb_reloc_walk(dst, true))
		return false;

	/* Prove the payload: decode a copy and compare it against live state (which
	 * rb_save_to just captured and nothing has touched since).  A mismatch means
	 * an encode/decode bug or an unregistered relocation; refuse to ship it. */
	Uint8 *chk = malloc(rb_total_size);
	if (!chk)
		return false;
	memcpy(chk, dst, rb_total_size);

	bool ok = rb_reloc_walk(chk, false);
	if (ok)
		ok = rb_verify_against(chk, NULL, 0) == 0;

	free(chk);
	return ok;
}

bool rollback_wire_adopt(Uint8 *buf)
{
	if (!rb_registered)
		return false;
	if (!rb_reloc_walk(buf, false))
		return false;
	rb_restore_from(buf);
	return true;
}

void rollback_wire_canonicalize(void)
{
	for (int i = 0; i < MAX_PWEAPON; ++i)
		if (shotAvail[i] == 0)
			memset(&playerShotData[i], 0, sizeof(playerShotData[i]));

	for (int i = 0; i < ENEMY_SHOT_MAX; ++i)
		if (enemyShotAvail[i])  /* true = free slot */
			memset(&enemyShot[i], 0, sizeof(enemyShot[i]));

	for (int i = 0; i < MAX_EXPLOSIONS; ++i)
		if (explosions[i].ttl == 0)
			memset(&explosions[i], 0, sizeof(explosions[i]));

	for (int i = 0; i < MAX_REPEATING_EXPLOSIONS; ++i)
		if (rep_explosions[i].ttl == 0)
			memset(&rep_explosions[i], 0, sizeof(rep_explosions[i]));
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
				/* Include eight live and reference bytes from the first mismatch. */
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

/* One tick of both players' input and shared events for snapshot self-tests. */
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

/* Self-test driver. */

static Uint32 st_frame = 0;
static bool   st_verifying = false;   /* replay pass in flight             */
static bool   st_tainted = false;
static char   st_taint_why[64];
static bool   st_level_active = false;

/* The self-test skips levels flown with Endless effects, so nothing in that half of the game is
 * covered by it. A QA run can lift the exclusion to measure one of those systems on purpose; see
 * qa_run_replay_fixture. Off outside the test flag, which leaves ordinary play untouched. */
static bool st_allow_endless = false;

void rollback_selftest_allow_endless(bool on)
{
	st_allow_endless = on;
}

bool rollback_selftest_active(void)
{
	return rollback_selftest && st_level_active && !isNetworkGame
	       && (!endlessFxActive() || st_allow_endless);
}

/* Allocate buffers when the self-test becomes active mid-level; an empty registry would replay
 * without restoring state. */
static void rb_selftest_arm(void)
{
	/* The connect handshake registers metadata without allocating the ring. */
	if (rb_ring[0] != NULL)
		return;
	rollback_register_all();
	rollback_ring_reset();
	rb_log("selftest armed mid-level (state %zu bytes)", rollback_state_size());
}

/* Toggled mid-session from the debug menu. */
void rollback_selftest_set(bool on)
{
	rollback_selftest = on;
	if (rollback_selftest_active())
		rb_selftest_arm();
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

	/* Allocate the ring only for rollback or the self-test. Endless effects are not registered, so
	 * the self-test remains disabled there. */
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

	rb_selftest_arm();  /* no-op unless it was armed after the level started */

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

		/* Demo traces compare deterministic state across platforms; demo_num identifies the input stream. */
		if (play_demo)
		{
			Uint32 rand_draws, ph, eh;
			network_sim_state(&rand_draws, &ph, &eh);
			rb_log("demo%u t %lu r %lu p %08x e %08x loc %u",
			       (unsigned)demo_num, (unsigned long)st_frame,
			       (unsigned long)rand_draws, (unsigned)ph, (unsigned)eh,
			       (unsigned)curLoc);

#if RB_TRACE_ITEMS_TO > 0
			if (st_frame >= RB_TRACE_ITEMS_FROM && st_frame <= RB_TRACE_ITEMS_TO && rb_trace_snapshot())
			{
				for (int i = 0; i < rb_item_count; ++i)
					if (rb_items[i].ptr != NULL)
						rb_log("  item t %lu %-24s %08x", (unsigned long)st_frame, rb_items[i].name,
						       (unsigned)rb_item_hash(&rb_items[i]));
			}
#endif
		}

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
			crashlog_note_net("ROLLBACK SELFTEST", msg);
			rb_log("FAIL %s"
			       "  ctx: curLoc=%u endLevel=%d levelEnd=%d reallyEnd=%d demo=%d "
			       "p0(alive=%d expl=%u invuln=%u) events=%04x",
			       msg,
			       (unsigned)curLoc, (int)endLevel, (int)levelEnd, (int)reallyEndLevel,
			       (int)play_demo,
			       (int)player[0].is_alive, player[0].exploding_ticks,
			       player[0].invulnerable_ticks, (unsigned)st_event_bits);
			rb_log_flush();
		}
		else if (rollback_selftest_ticks % 350 == 0)
		{
			rb_log("ok: %lu ticks verified, %lu failures (state %zu bytes)",
			       rollback_selftest_ticks, rollback_selftest_failures,
			       rollback_state_size());
			rb_log_flush();
		}
	}

	if (rollback_selftest_limit != 0 && rollback_selftest_ticks >= rollback_selftest_limit)
	{
		rollback_selftest_limit_hash = rollback_state_hash();
		reallyEndLevel = true;
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
	rb_log_flush();  /* the level's trace is complete; get it on disk */
}

/* Registration root. Public globals live in rollback_state.c; modules register
 * their own private simulation state here. */
void rollback_state_register_globals(void);   /* rollback_state.c  */
void tyrian2_register_rollback(void);         /* tyrian2.c statics */

void rollback_ensure_registered(void)
{
	if (rb_registered)
		return;
	rb_registered = true;

	rollback_state_register_globals();
	tyrian2_register_rollback();
}

void rollback_register_all(void)
{
	const bool first = !rb_registered;

	rollback_ensure_registered();
	rb_alloc_buffers();

	if (first)
		fprintf(stderr, "rollback: %d items, %zu bytes per snapshot, %d-deep ring\n",
		        rb_item_count, rb_total_size, ROLLBACK_RING);
}

/* Registry shape, not registry contents.  Names go in because two builds can
 * reach the same total by different routes, and offsets go in because the wire
 * snapshot is positional; an item that merely MOVED would corrupt the peer
 * just as thoroughly as one that changed size. */
Uint32 rollback_layout_fingerprint(void)
{
	rollback_ensure_registered();

	Uint32 h = 2166136261u;
	#define FP_BYTE(v) do { h ^= (Uint8)(v); h *= 16777619u; } while (0)
	#define FP_WORD(v) do { \
		const Uint32 w_ = (Uint32)(v); \
		for (int b_ = 0; b_ < 4; ++b_) \
			FP_BYTE(w_ >> (b_ * 8)); \
	} while (0)

	FP_WORD(rb_item_count);
	FP_WORD(rb_total_size);
	for (int i = 0; i < rb_item_count; ++i)
	{
		for (const char *n = rb_items[i].name; *n != '\0'; ++n)
			FP_BYTE(*n);
		FP_BYTE(0);
		FP_WORD(rb_items[i].size);
		FP_WORD(rb_items[i].offset);
	}

	#undef FP_WORD
	#undef FP_BYTE
	return h;
}
