/* Standard MIDI File parser for the MIDI backends. See midi_smf.h. */
#include "midi_smf.h"

#if defined(WITH_MIDI) || defined(_WIN32)

#include <stdlib.h>
#include <string.h>

// Growable scratch event list (absolute ticks) before the tempo map is applied.
typedef struct
{
	Uint32 tick;
	Uint32 seq;    // preserves order among same-tick events (stable merge)
	Uint8  type;   // 0 channel, 1 sysex, 2 tempo, 3 loopStart marker
	Uint8  status, d1, d2;
	Uint32 tempo;  // type 2
	Uint32 sx_off, sx_len;  // type 1
} TmpEv;

static Uint32 rd_u32(const Uint8 *p) { return ((Uint32)p[0] << 24) | ((Uint32)p[1] << 16) | ((Uint32)p[2] << 8) | p[3]; }
static Uint16 rd_u16(const Uint8 *p) { return (Uint16)(((Uint16)p[0] << 8) | p[1]); }

static Uint32 read_vlq(const Uint8 **pp, const Uint8 *end)
{
	Uint32 v = 0;
	while (*pp < end)
	{
		Uint8 b = *(*pp)++;
		v = (v << 7) | (b & 0x7F);
		if (!(b & 0x80))
			break;
	}
	return v;
}

static int cmp_tmp(const void *a, const void *b)
{
	const TmpEv *x = (const TmpEv *)a, *y = (const TmpEv *)b;
	if (x->tick != y->tick) return (x->tick < y->tick) ? -1 : 1;
	if (x->seq  != y->seq)  return (x->seq  < y->seq)  ? -1 : 1;
	return 0;
}

void midi_smf_free(MidiSong *song)
{
	free(song->events); song->events = NULL; song->event_count = 0;
	free(song->sysex);  song->sysex = NULL;
	song->loop_marker = false;
	song->loop_us = 0;
	song->loop_idx = 0;
}

// Grow from a 256-entry floor until `need` fits. Failure returns NULL and preserves the original block.
static void *grow_buf(void *old, size_t *cap, size_t need, size_t elem)
{
	size_t ncap = *cap;
	while (need > ncap)
		ncap = ncap ? ncap * 2 : 256;
	if (ncap == *cap)
		return old;

	void *grown = realloc(old, ncap * elem);
	if (grown != NULL)
		*cap = ncap;
	return grown;
}

#define GROW_OR_FAIL(ptr, cap, need) \
	do { \
		void *grown_ = grow_buf((ptr), &(cap), (need), sizeof(*(ptr))); \
		if (grown_ == NULL) goto oom; \
		(ptr) = grown_; \
	} while (0)

bool midi_smf_parse(MidiSong *song, const Uint8 *data, size_t size)
{
	midi_smf_free(song);

	if (size < 14 || memcmp(data, "MThd", 4) != 0)
		return false;
	Uint32 hlen = rd_u32(data + 4);
	if (hlen < 6)
		return false;
	Uint16 ntrks = rd_u16(data + 10);
	Uint16 division = rd_u16(data + 12);

	const Uint8 *p = data + 8 + hlen;
	const Uint8 *end = data + size;

	TmpEv *tmp = NULL; size_t ntmp = 0, captmp = 0;
	Uint8 *sx = NULL;  size_t nsx = 0, capsx = 0;
	Uint32 seq = 0;

	for (int t = 0; t < ntrks && p + 8 <= end; ++t)
	{
		if (memcmp(p, "MTrk", 4) != 0)
			break;
		Uint32 tlen = rd_u32(p + 4);
		const Uint8 *tp = p + 8;
		const Uint8 *tend = tp + tlen;
		if (tend > end || tend < tp)  // clamp a bogus/overflowing length
			tend = end;
		p = tend;

		Uint32 tick = 0;
		Uint8 running = 0;
		while (tp < tend)
		{
			tick += read_vlq(&tp, tend);
			if (tp >= tend)
				break;

			Uint8 status = *tp;
			if (status & 0x80) { tp++; }
			else               { status = running; }  // running status
			if (status < 0x80)
				break;  // malformed

			if (status == 0xFF)  // meta event
			{
				if (tp >= tend) break;
				Uint8 mtype = *tp++;
				Uint32 mlen = read_vlq(&tp, tend);
				if (mlen > (Uint32)(tend - tp)) mlen = (Uint32)(tend - tp);
				if (mtype == 0x51 && mlen >= 3)
				{
					TmpEv e; memset(&e, 0, sizeof(e));
					e.tick = tick; e.seq = seq++; e.type = 2;
					e.tempo = ((Uint32)tp[0] << 16) | ((Uint32)tp[1] << 8) | tp[2];
					GROW_OR_FAIL(tmp, captmp, ntmp + 1);
					tmp[ntmp++] = e;
				}
				else if (mtype == 0x06 && mlen == 9 && memcmp(tp, "loopStart", 9) == 0)
				{
					// Marker written by the LDS->MIDI conversion at the song's
					// jump-back position; the player loops here, not to time 0.
					TmpEv e; memset(&e, 0, sizeof(e));
					e.tick = tick; e.seq = seq++; e.type = 3;
					GROW_OR_FAIL(tmp, captmp, ntmp + 1);
					tmp[ntmp++] = e;
				}
				tp += mlen;
				running = 0;  // meta cancels running status
				continue;
			}

			if (status == 0xF0 || status == 0xF7)  // SysEx
			{
				Uint32 slen = read_vlq(&tp, tend);
				if (slen > (Uint32)(tend - tp)) slen = (Uint32)(tend - tp);
				Uint32 off = (Uint32)nsx;
				Uint32 need = slen + (status == 0xF0 ? 1u : 0u);
				GROW_OR_FAIL(sx, capsx, nsx + need);
				if (status == 0xF0) sx[nsx++] = 0xF0;
				memcpy(sx + nsx, tp, slen); nsx += slen;
				TmpEv e; memset(&e, 0, sizeof(e));
				e.tick = tick; e.seq = seq++; e.type = 1;
				e.sx_off = off; e.sx_len = (Uint32)nsx - off;
				GROW_OR_FAIL(tmp, captmp, ntmp + 1);
				tmp[ntmp++] = e;
				tp += slen;
				running = 0;
				continue;
			}

			// channel-voice message
			Uint8 hi = status & 0xF0;
			int nbytes = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
			Uint8 d1 = 0, d2 = 0;
			if (nbytes >= 1) { if (tp >= tend) break; d1 = *tp++; }
			if (nbytes >= 2) { if (tp >= tend) break; d2 = *tp++; }
			running = status;

			TmpEv e; memset(&e, 0, sizeof(e));
			e.tick = tick; e.seq = seq++; e.type = 0;
			e.status = status; e.d1 = d1; e.d2 = d2;
			GROW_OR_FAIL(tmp, captmp, ntmp + 1);
			tmp[ntmp++] = e;
		}
	}

	if (ntmp == 0) goto oom;  // use the allocation-failure cleanup path

	qsort(tmp, ntmp, sizeof(*tmp), cmp_tmp);

	song->events = malloc(ntmp * sizeof(*song->events));
	if (song->events == NULL) goto oom;
	song->event_count = 0;
	song->sysex = sx;  // ownership transferred (sx_off values index into it)

	// Apply the tempo map to turn ticks into absolute microseconds.
	const bool smpte = (division & 0x8000) != 0;
	double us_per_tick = 0.0;   // SMPTE: fixed
	Uint32 ppqn = 0;
	if (smpte)
	{
		int fps = 256 - (division >> 8);      // -(int8) high byte: 24/25/29/30
		int tpf = division & 0xFF;
		if (fps <= 0) fps = 24;
		if (tpf <= 0) tpf = 1;
		us_per_tick = 1000000.0 / ((double)fps * (double)tpf);
	}
	else
	{
		ppqn = division ? division : 480;
	}

	Uint64 cur_us = 0;
	Uint32 last_tick = 0;
	Uint32 tempo = 500000;  // 120 BPM until told otherwise
	for (size_t i = 0; i < ntmp; ++i)
	{
		Uint32 dtick = tmp[i].tick - last_tick;
		last_tick = tmp[i].tick;
		if (smpte) cur_us += (Uint64)((double)dtick * us_per_tick);
		else       cur_us += (Uint64)dtick * tempo / ppqn;

		if (tmp[i].type == 2)
		{
			if (tmp[i].tempo)
				tempo = tmp[i].tempo;
			continue;
		}
		if (tmp[i].type == 3)
		{
			if (!song->loop_marker)
			{
				song->loop_marker = true;
				song->loop_us = cur_us;
			}
			continue;
		}

		MidiEvent *o = &song->events[song->event_count++];
		o->time_us = cur_us;
		if (tmp[i].type == 1)
		{
			o->status = 0xF0; o->d1 = o->d2 = 0;
			o->sx_off = tmp[i].sx_off; o->sx_len = tmp[i].sx_len;
		}
		else
		{
			o->status = tmp[i].status; o->d1 = tmp[i].d1; o->d2 = tmp[i].d2;
			o->sx_off = 0; o->sx_len = 0;
		}
	}

	free(tmp);
	if (song->event_count == 0) { midi_smf_free(song); return false; }

	// Resolve the loop marker to the first event at/after it (events sharing the
	// marker's timestamp are included regardless of their order in the file).
	if (song->loop_marker)
	{
		song->loop_idx = song->event_count;
		for (size_t i = 0; i < song->event_count; ++i)
		{
			if (song->events[i].time_us >= song->loop_us)
			{
				song->loop_idx = i;
				break;
			}
		}
		// A marker past the last event falls back to a full restart.
		if (song->loop_idx >= song->event_count)
			song->loop_marker = false;
	}

	return true;

oom:  // shared bail-out: sx is still owned here (ownership moves to song->sysex further down)
	free(tmp);
	free(sx);
	return false;
}

#endif /* WITH_MIDI || _WIN32 */
