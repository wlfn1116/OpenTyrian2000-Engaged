#define QA_INTERNAL_ACCESS

#include "qa.h"
#include "qa_internal.h"

#include "audio.h"
#include "be_bytes.h"

#include "SDL.h"
#ifdef WITH_MIDI
#include <midiproc.h>
#endif

#include <stdio.h>
#include <string.h>

/* Setup > Sound filter presets at the rates audio devices grant. */
void qa_test_sfx_filter(void)
{
	const int rates[] = { 22050, 44100, 48000 };
	for (size_t r = 0; r < COUNTOF(rates); ++r)
	{
		qa_check(sfx_filter_gain(SFX_FILTER_OFF, rates[r], true) == 1.0f,
		         "the Off sound filter leaves the signal alone");
		for (int f = SFX_FILTER_LIGHT; f < SFX_FILTER_COUNT; ++f)
		{
			const float dc = sfx_filter_gain(f, rates[r], false);
			qa_check(dc > 0.99f && dc < 1.01f,
			         "a sound filter keeps a steady level at full volume");
			qa_check(sfx_filter_gain(f, rates[r], true) < 0.01f,
			         "a sound filter silences the highest frequency the device can play");
		}
	}
	qa_check(sfx_filter_gain(SFX_FILTER_LIGHT, 0, false) == 1.0f,
	         "a sound filter without an output rate stays off");
}

#ifdef WITH_MIDI

/* LDS detune, envelope release, channel ownership, and truncation checks. */

#define QA_LDS_PATCH_BYTES 46
#define QA_LDS_TEMPO       7    // a row every 8 ticks, so one position spans 512 of them
#define QA_LDS_ROWS        64
#define QA_LDS_NOTE        60   // pattern note byte, in whole semitones
#define QA_LDS_VELOCITY    100
#define QA_LDS_WHEEL_RANGE 12   // semitones, as MIDIProcessorLDS.cpp announces over RPN
#define QA_LDS_BEND_CENTER 8192

/* Program identifies voice groups in this probe. Detune uses 1/16 semitone units, and channel 8
 * remains free for the stop command. */
static const struct
{
	JE_byte program;
	JE_byte level;    // pattern volume for this voice, 0 to 63
	JE_byte carMisc;  // carrier register 0x20; bit 5 makes the voice sustain
	JE_byte carAD;    // carrier attack and decay rates
	JE_byte carSR;    // carrier sustain level and release rate
	int finetune;     // as the patch record stores it
	int detune;       // what should survive: this patch's offset within its group
} qaLdsVoices[] = {
	{ 48, 10, 0x00, 0x00, 0x00,   0,   0 },  // three layers of one voice, already centred
	{ 48, 14, 0x00, 0x00, 0x00,  -2,  -2 },
	{ 48, 18, 0x00, 0x00, 0x00,   2,   2 },
	{ 49, 22, 0x00, 0x00, 0x00,  12,  12 },  // spread wide enough that the offset moves the key
	{ 49, 26, 0x00, 0x00, 0x00, -12, -12 },
	{ 50, 30, 0x00, 0x05, 0xF0, -12,   0 },  // standing alone: voicing stays out of the synth.
	{ 51, 34, 0x00, 0x02, 0xF0,  32,   0 },  // 5 decays inside the song, 6 outlasts it,
	{ 52, 38, 0x20, 0x05, 0xF0,   5,   0 },  // 7 sustains. None of the three may overrun.
};

typedef struct
{
	int key;     // -1 until the voice sounds
	int bend;    // wheel value in force when it sounded
	int notes;   // note-ons seen, so a repeat is caught
	int level;   // -1 until a volume controller reaches this channel
	int levels;  // volume controllers seen, so a stray one is caught
	int held;    // ticks between the note-on and its release, -1 while unreleased
	unsigned on; // tick the note sounded on
} QaLdsVoice;

static JE_byte *qa_lds_put16(JE_byte *p, unsigned value)
{
	*p++ = (JE_byte)(value & 0xff);
	*p++ = (JE_byte)((value >> 8) & 0xff);
	return p;
}

/* One position holding one note per voice and then a stop, so every patch sounds exactly once and
 * the converter's walk ends without needing a position jump. Returns the song's length. */
static size_t qa_lds_build(JE_byte *out)
{
	const unsigned voices = COUNTOF(qaLdsVoices);
	JE_byte *p = out;

	*p++ = 0;                // melodic mode
	p = qa_lds_put16(p, 0);  // speed, which the converter ignores
	*p++ = QA_LDS_TEMPO;     // ticks per row, less one
	*p++ = QA_LDS_ROWS;      // rows per position
	for (unsigned i = 0; i < 9; ++i)
		*p++ = 0;            // no channel delay
	*p++ = 0;                // percussion register

	p = qa_lds_put16(p, voices);
	for (unsigned v = 0; v < voices; ++v)
	{
		memset(p, 0, QA_LDS_PATCH_BYTES);
		p[14] = (JE_byte)(qaLdsVoices[v].finetune & 0xff);  // finetune, a signed byte on disk
		p[5] = qaLdsVoices[v].carMisc;
		p[7] = qaLdsVoices[v].carAD;
		p[8] = qaLdsVoices[v].carSR;
		p[40] = qaLdsVoices[v].program;
		p[41] = QA_LDS_VELOCITY;
		p += QA_LDS_PATCH_BYTES;
	}

	p = qa_lds_put16(p, 1);  // one position
	for (unsigned ch = 0; ch < 9; ++ch)
	{
		p = qa_lds_put16(p, ch * 6);  // byte offset into this channel's pattern words
		*p++ = 0;                     // transpose
	}

	p = qa_lds_put16(p, 0);  // digital sample count

	for (unsigned ch = 0; ch < 9; ++ch)
	{
		// Voices set volume, play once, then wait. Channel 8 stops on the final row.
		const bool voiced = ch < voices;

		p = qa_lds_put16(p, voiced ? 0xfd00u | qaLdsVoices[ch].level : 0x8000u | (QA_LDS_ROWS - 2));
		p = qa_lds_put16(p, voiced ? ((unsigned)QA_LDS_NOTE << 8) | ch : 0xfc00u);

		// Omit unread trailing words so truncation checks cover the parser's actual boundary.
		if (voiced)
			p = qa_lds_put16(p, 0x8000u | (QA_LDS_ROWS - 3));
	}

	return (size_t)(p - out);
}

static const JE_byte *qa_smf_vlq(const JE_byte *p, const JE_byte *end, unsigned *out)
{
	unsigned value = 0;

	for (int i = 0; i < 4 && p < end; ++i)
	{
		value = (value << 7) | (*p & 0x7f);
		if ((*p++ & 0x80) == 0)
		{
			*out = value;
			return p;
		}
	}
	return NULL;
}

/* Walk every track, following running status, and record the key and the wheel value in force for
 * each channel's first note-on. */
static bool qa_smf_scan(const JE_byte *data, size_t size, QaLdsVoice *voice, unsigned voices)
{
	const JE_byte *p = data, *end = data + size;
	int bend[16];

	if (size < 14 || memcmp(p, "MThd", 4) != 0)
		return false;
	p += 8 + be_read32(p + 4);

	for (unsigned i = 0; i < COUNTOF(bend); ++i)
		bend[i] = QA_LDS_BEND_CENTER;

	while (end - p >= 8)
	{
		const unsigned length = be_read32(p + 4);
		const JE_byte *track = p + 8;
		const JE_byte *trackEnd = track + length;
		JE_byte status = 0;
		unsigned now = 0;

		if (memcmp(p, "MTrk", 4) != 0 || (size_t)(end - track) < length)
			return false;
		p = trackEnd;

		while (track < trackEnd)
		{
			unsigned delta, payload;

			track = qa_smf_vlq(track, trackEnd, &delta);
			now += delta;
			if (track == NULL || track >= trackEnd)
				return false;

			if (*track & 0x80)
				status = *track++;
			if (status < 0x80)
				return false;

			if (status == 0xff)  // meta: a type byte, then a counted payload
			{
				if (track >= trackEnd)
					return false;
				++track;
				track = qa_smf_vlq(track, trackEnd, &payload);
			}
			else if (status == 0xf0 || status == 0xf7)  // sysex: a counted payload
			{
				track = qa_smf_vlq(track, trackEnd, &payload);
			}
			else
			{
				const unsigned kind = status & 0xf0;
				const unsigned channel = status & 0x0f;

				payload = (kind == 0xc0 || kind == 0xd0) ? 1 : 2;
				if ((size_t)(trackEnd - track) < payload)
					return false;

				if (kind == 0xb0 && track[0] == 7 && channel < voices)
				{
					if (voice[channel].levels++ == 0)
						voice[channel].level = track[1];
				}
				else if (kind == 0xe0)
					bend[channel] = (int)track[0] | ((int)track[1] << 7);
				else if (kind == 0x90 && track[1] != 0 && channel < voices)
				{
					if (voice[channel].notes++ == 0)
					{
						voice[channel].key = track[0];
						voice[channel].bend = bend[channel];
						voice[channel].on = now;
					}
				}
				else if (kind == 0x80 && channel < voices && voice[channel].held < 0
				         && voice[channel].notes > 0)
				{
					voice[channel].held = (int) (now - voice[channel].on);
				}
			}

			if (track == NULL || (size_t)(trackEnd - track) < payload)
				return false;
			track += payload;
		}
	}
	return true;
}

void qa_test_lds_midi_detune(void)
{
	const unsigned voices = COUNTOF(qaLdsVoices);
	JE_byte song[640];
	QaLdsVoice voice[16];
	JE_byte *smf = NULL;
	size_t smfSize = 0;

	qa_check(voices <= 8, "the detune probe leaves a channel free to stop the song");

	const size_t songSize = qa_lds_build(song);
	qa_check(songSize <= sizeof(song), "the probe song fits its buffer");

	HMIDIContainer container = MIDPROC_Container_Create();
	qa_check(MIDPROC_Process(song, songSize, "lds", container),
	         "the converter reads the probe song");

	MIDPROC_Container_SerializeAsSMF(container, &smf, &smfSize);
	qa_check(smf != NULL && smfSize > 0, "the converted song serializes to an SMF");

	for (unsigned i = 0; i < COUNTOF(voice); ++i)
	{
		voice[i].key = -1;
		voice[i].bend = QA_LDS_BEND_CENTER;
		voice[i].notes = 0;
		voice[i].level = -1;
		voice[i].levels = 0;
		voice[i].held = -1;
		voice[i].on = 0;
	}
	qa_check(smf != NULL && qa_smf_scan(smf, smfSize, voice, voices), "the SMF parses");

	for (unsigned v = 0; v < voices; ++v)
	{
		const int target = (QA_LDS_NOTE * 16) + qaLdsVoices[v].detune;
		const int wantKey = (target + 8) >> 4;
		const int wantBend = QA_LDS_BEND_CENTER
		                   + (((target - (wantKey * 16)) * 512) / QA_LDS_WHEEL_RANGE);
		const int wantLevel = (qaLdsVoices[v].level & 0x3f) * 127 / 63;

		if (voice[v].notes != 1 || voice[v].key != wantKey || voice[v].bend != wantBend
		    || voice[v].levels != 1 || voice[v].level != wantLevel)
			fprintf(stderr,
			        "# voice %u: %d note(s), key %d bend %d level %d (x%d), want %d / %d / %d\n",
			        v, voice[v].notes, voice[v].key, voice[v].bend, voice[v].level, voice[v].levels,
			        wantKey, wantBend, wantLevel);

		qa_check(voice[v].notes == 1, "a detuned patch sounds exactly once");
		qa_check(voice[v].key == wantKey, "a patch sounds the key its reduced detune lands on");
		qa_check(voice[v].bend == wantBend, "a patch leaves its detune remainder on the wheel");
		qa_check(voice[v].levels == 1, "a voice takes exactly one volume controller");
		qa_check(voice[v].level == wantLevel, "a volume controller reaches its own voice");
	}

	/* Voice 5 decays before the end, voice 6 outlasts it, and voice 7 sustains. */
	qa_check(voices == 8, "the probe still has the eight voices these checks name");
	if (voices == 8)
	{
		const int songEnd = voice[0].held;

		fprintf(stderr, "# held ticks: decays %d, outlasts %d, sustains %d, song end %d\n",
		        voice[5].held, voice[6].held, voice[7].held, songEnd);
		qa_check(voice[5].held > 0, "a decaying voice is released by its envelope");
		qa_check(voice[5].held < songEnd, "a decaying voice goes quiet before the song ends");
		qa_check(voice[6].held == songEnd, "an envelope past the song end releases at the end");
		qa_check(voice[7].held == songEnd, "an envelope never cuts a sustaining voice short");
	}

	MIDPROC_FreeSerialized(smf);
	MIDPROC_Container_Delete(container);

	/* Every proper prefix must be rejected. Sanitizers cover out-of-bounds reads. */
	size_t accepted = 0, firstAccepted = 0;
	for (size_t cut = 1; cut < songSize; ++cut)
	{
		HMIDIContainer truncated = MIDPROC_Container_Create();

		if (MIDPROC_Process(song, cut, "lds", truncated))
		{
			if (accepted++ == 0)
				firstAccepted = cut;
		}

		MIDPROC_Container_Delete(truncated);
	}

	if (accepted != 0)
		fprintf(stderr, "# %u of %u truncations accepted, first at %u byte(s)\n",
		        (unsigned)accepted, (unsigned)songSize - 1, (unsigned)firstAccepted);
	qa_check(accepted == 0, "the converter rejects every truncation of the probe song");
}

#endif /* WITH_MIDI */
