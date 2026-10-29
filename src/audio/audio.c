/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
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
#include "audio.h"

#include "custom_audio.h"
#include "file.h"
#include "lds_play.h"
#include "musmast.h"
#include "opentyr.h"
#include "sfx.h"
#include "xmas.h"

// Optional MIDI music backends; FluidSynth or the OS synth (see audio.h and
// OPL stays the default.
#ifdef WITH_MIDI
#include <midiproc.h>
#include "win_native_midi.h"  // NATIVE_MIDI plays through our own deadlock-free Win32 player
#include "fluid_music.h"       // FLUIDSYNTH plays through our own libfluidsynth player
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // GetModuleFileNameA and GetFileAttributesExA, for soundfont autodetect
#endif
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define OUTPUT_QUALITY 4  // 44.1 kHz

int audioSampleRate = 0;

static bool music_stopped = true;
unsigned int song_playing = 0;

bool audio_disabled = false, music_disabled = false, samples_disabled = false;

// Selected music synthesizer. Always defined (config/menu reference it); only the
// MIDI devices require a WITH_MIDI build, and init_audio() forces OPL otherwise.
MusicDevice music_device = OPL;
char soundfont[4096] = { 0 };
int custom_sounds = CUSTOM_SOUNDS_OFF;

const char *const custom_sounds_names[CUSTOM_SOUNDS_COUNT] = {
	"Off",
	"OST",
	"SFX",
	"Both",
};

// True when FluidSynth is active with a readable SoundFont.
bool midi_soundfont_loaded = false;

const char *const music_device_names[MUSIC_DEVICE_MAX] = {
	"OPL3",
	"FluidSynth",
	"Native MIDI",
};

// Basename of the active SoundFont path, for status/UI display; "" when unset.
const char *soundfont_basename(void)
{
	const char *base = soundfont;
	for (const char *p = soundfont; *p != '\0'; ++p)
		if (*p == '/' || *p == '\\')
			base = p + 1;
	return base;
}

#ifdef WITH_MIDI
// music_device values that route through a MIDI backend rather than the OPL path.
// (FLUIDSYNTH==1, NATIVE_MIDI==2 are distinct bits, so this masks correctly.)
static const uint8_t IS_MIDI_DEVICE = FLUIDSYNTH | NATIVE_MIDI;

// Per-song converted SMF plus loop/duration metadata; the active MIDI backend
// (fluid_music or win_native_midi) owns playback.
typedef struct MidiData {
	Uint8 *data;
	Uint32 size;
	Uint32 duration;
	Uint32 loop_start;
	Uint32 loop_end;
} MidiData;
static MidiData *midi_data = NULL;
#endif

static SDL_AudioDeviceID audioDevice = 0;

static Uint8 musicVolume = 255;
static Uint8 sampleVolume = 255;

static const float volumeRange = 30.0f;  // dB

// Fixed point Q20.12; needs to be able to store (10 * INT16_MIN/MAX)
static Sint32 volumeFactorTable[256];
#define TO_FIXED(x) ((Sint32)((x) * (1 << 12)))
#define FIXED_TO_INT(x) ((Sint32)((x) >> 12))

// Twice the Loudness update rate; classic Tyrian usually ran it near 69.57 Hz.
static const int ldsUpdate2Rate = 139;  // 69.5 * 2

static int samplesPerLdsUpdate;
static int samplesPerLdsUpdateFrac;

static int samplesUntilLdsUpdate = 0;
static int samplesUntilLdsUpdateFrac = 0;

static FILE *music_file = NULL;
static Uint32 *song_offset;
static Uint16 song_count = 0;

typedef enum
{
	SONG_ENDING_UNKNOWN = 0,
	SONG_ENDING_LOOPS,
	SONG_ENDING_ONCE,
} SongEnding;

static Uint8 *song_ending = NULL;

#define NO_SONG_PLAYING 0xFFFFFFFF

#define OST_FADE_LEVEL_FULL 255

static CustomTrack *ostTrack = NULL;
static bool ostLoops = false;
static bool ostDone = false;
static bool ostFading = false;
static int ostFadeLevel = OST_FADE_LEVEL_FULL;
static int ostFadeSamples = 0;
static int ostFadeFrac = 0;

#define CHANNEL_COUNT 9  // 8 soundQueue slots + SFX_CUE_CHANNEL (audio.h)
static const Sint16 *channelSamples[CHANNEL_COUNT];
static const Sint16 *channelSampleStart[CHANNEL_COUNT];  // identifies the sound for the copy limit
static size_t channelSampleCount[CHANNEL_COUNT] = { 0 };
static Uint8 channelVolume[CHANNEL_COUNT];
#define CHANNEL_VOLUME_LEVELS 8

int sfx_filter = SFX_FILTER_OFF;
int sfx_copy_limit = 0;

// Second-order Butterworth low-pass, transposed direct form II.
typedef struct
{
	float b0, b1, b2, a1, a2;  // normalized by a0
	float z1, z2;
} SfxBiquad;

static const float sfxFilterCutoffHz[SFX_FILTER_COUNT] = { 0.0f, 4000.0f, 2800.0f };

// Owned by the audio callback, which redesigns the filter when sfx_filter changes.
static SfxBiquad sfxBiquad[AUDIO_OUTPUT_CHANNELS];
static int sfxBiquadFilter = -1;  // preset sfxBiquad was designed for; -1 forces a redesign
static bool sfxBiquadActive = false;

static void audioCallback(void *userdata, Uint8 *stream, int size);

static void load_song(unsigned int song_num);
static void close_ost_track(void);

#ifdef WITH_MIDI

// Called from the MIDI player's sequencer thread when a one-shot song ends (looping
// songs loop internally). The game polls `playing` to know when a jingle has ended.
static void midi_finished(void)
{
	playing = false;
	songlooped = false;
}

static bool file_readable(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL)
		return false;
	fclose(f);
	return true;
}

// Tear down whichever MIDI backend is active. Both are safe no-ops if their backend
// was never started, so this can run unconditionally on any audio teardown.
static void deinit_midi(void)
{
	midi_soundfont_loaded = false;
	fm_quit();   // stop + tear down the FluidSynth player
	wnm_quit();  // stop + close the native player
}

// Convert each LDS song in music.mus to an in-memory SMF (midiproc), recording
// loop/duration metadata (loop_end == 0xFFFFFFFF marks a one-shot song).
static void convert_midi_data(void)
{
	midi_data = malloc_die(song_count * sizeof(*midi_data));

	for (unsigned int i = 0; i < song_count; ++i)
	{
		memset(&midi_data[i], 0, sizeof(MidiData));
		midi_data[i].loop_end = 0xFFFFFFFF;

		Uint32 start = song_offset[i];
		Uint32 end = song_offset[i + 1];
		Uint32 size = end - start;
		Uint8 *buf = malloc_die(size);

		fseek(music_file, start, SEEK_SET);
		if (fread(buf, 1, size, music_file) != size)
		{
			fprintf(stderr, "warning: failed to read song %d\n", i + 1);
			free(buf);
			continue;
		}

		HMIDIContainer midi_container = MIDPROC_Container_Create();
		if (!MIDPROC_Process(buf, size, "lds", midi_container))
		{
			fprintf(stderr, "warning: failed to process song %d\n", i + 1);
			MIDPROC_Container_Delete(midi_container);
			free(buf);
			continue;
		}

		size_t midi_data_size = 0;
		MIDPROC_Container_SerializeAsSMF(midi_container, &(midi_data[i].data), &midi_data_size);
		midi_data[i].size = (Uint32)midi_data_size;
		if (midi_data[i].size == 0)
		{
			fprintf(stderr, "warning: failed to serialize song %d\n", i + 1);
			MIDPROC_Container_Delete(midi_container);
			free(buf);
			continue;
		}

		midi_data[i].duration = MIDPROC_Container_GetDuration(midi_container, 0, false);
		MIDPROC_Container_DetectLoops(midi_container, false, true, false, false);
		midi_data[i].loop_start = MIDPROC_Container_GetLoopBeginTimestamp(midi_container, 0, false);
		midi_data[i].loop_end = MIDPROC_Container_GetLoopEndTimestamp(midi_container, 0, false);

		MIDPROC_Container_Delete(midi_container);
		free(buf);
	}
}

static bool midi_song_loops(unsigned int song_num)
{
	return midi_data[song_num].loop_end <= midi_data[song_num].duration;
}

// Start a song on the active MIDI backend. A looping song (loop_end <= duration)
// repeats forever from its loopStart marker; a one-shot plays once and trips midi_finished.
static void play_midi_song(unsigned int song_num)
{
	if (midi_data[song_num].data == NULL || midi_data[song_num].size == 0)
		return;

	bool loops = midi_song_loops(song_num);

	if (music_device == NATIVE_MIDI)
	{
		wnm_set_volume(musicVolume);
		if (!wnm_play(midi_data[song_num].data, midi_data[song_num].size, loops, midi_finished))
			return;
	}
	else  // FLUIDSYNTH
	{
		fm_set_volume(musicVolume);
		if (!fm_play(midi_data[song_num].data, midi_data[song_num].size, loops, midi_finished))
			return;
	}

	song_playing = song_num;
	playing = true;
	songlooped = false;
}

// True for the SoundFont file extensions FluidSynth can actually load. Excludes
// e.g. .sfz (a different format) and .sfArk (compressed), which it can't.
static bool is_soundfont_ext(const char *name)
{
	const char *dot = strrchr(name, '.');
	if (dot == NULL)
		return false;
	return SDL_strcasecmp(dot, ".sf2") == 0
	    || SDL_strcasecmp(dot, ".sf3") == 0
	    || SDL_strcasecmp(dot, ".sf") == 0;
}

static void stop_midi_song(void)
{
	if (music_device == NATIVE_MIDI)
		wnm_stop();
	else if (music_device == FLUIDSYNTH)
		fm_stop();
}

#ifdef _WIN32
// Executable directory without a trailing slash, or empty when unknown.
static const char *exe_dir(void)
{
	static char dir[MAX_PATH];
	static bool resolved = false;

	if (!resolved)
	{
		resolved = true;
		DWORD len = GetModuleFileNameA(NULL, dir, MAX_PATH);
		char *slash = (len > 0 && len < MAX_PATH) ? strrchr(dir, '\\') : NULL;
		if (slash != NULL)
			*slash = '\0';
		else
			dir[0] = '\0';
	}
	return dir;
}

typedef struct
{
	const char *dir;
	char *best;
	size_t bestSize;
	FILETIME bestTime;
} SoundfontScan;

static bool consider_soundfont(const char *name, void *context)
{
	SoundfontScan *const scan = context;
	if (!is_soundfont_ext(name))
		return true;

	char path[4096];
	WIN32_FILE_ATTRIBUTE_DATA info;
	snprintf(path, sizeof(path), "%s/%s", scan->dir, name);
	if (!GetFileAttributesExA(path, GetFileExInfoStandard, &info))
		return true;
	if (scan->best[0] == '\0' || CompareFileTime(&info.ftLastWriteTime, &scan->bestTime) > 0)
	{
		SDL_strlcpy(scan->best, path, scan->bestSize);
		scan->bestTime = info.ftLastWriteTime;
	}
	return true;
}

// Keep the newest loadable SoundFont found across all scanned directories.
static void scan_dir_for_soundfont(SoundfontScan *scan, const char *dir)
{
	if (dir == NULL || dir[0] == '\0')
		return;

	scan->dir = dir;
	dir_each_file(dir, consider_soundfont, scan);
}
#endif

#define SOUNDFONT_DIR_COUNT (2 + CUSTOM_AUDIO_FOLDER_COUNT * CUSTOM_AUDIO_DIR_COUNT)

static size_t soundfont_dirs(const char *dirs[SOUNDFONT_DIR_COUNT])
{
	size_t count = 0;
	dirs[count++] = data_dir();
#ifdef _WIN32
	dirs[count++] = exe_dir();
#endif
	for (int folder = 0; folder < CUSTOM_AUDIO_FOLDER_COUNT; ++folder)
		count += custom_audio_dirs((CustomAudioFolder)folder, &dirs[count]);
	return count;
}

// Find the newest SoundFont beside the executable or in the data, mus or sfx folders.
static bool find_soundfont(char *out, size_t outSize)
{
	out[0] = '\0';
#ifdef _WIN32
	SoundfontScan scan = { NULL, out, outSize, { 0, 0 } };
	const char *dirs[SOUNDFONT_DIR_COUNT];
	const size_t dirCount = soundfont_dirs(dirs);
	for (size_t i = 0; i < dirCount; ++i)
		scan_dir_for_soundfont(&scan, dirs[i]);
#else
	(void)outSize;  // no autodetect off Windows; those ports need an explicit config path
#endif
	return out[0] != '\0';
}

// Cached SoundFont availability: -1 unscanned, 0 absent, 1 found.
static int soundfont_scan = -1;

// If no SoundFont is configured, adopt the newest one found next to the .exe, in
// the data dir or in a mus or sfx folder so a dropped-in bank just works.
static void autodetect_soundfont(void)
{
	if (soundfont[0] != '\0')
		return;  // an explicit config/CLI soundfont already chosen; respect it
	find_soundfont(soundfont, sizeof(soundfont));
}

// Re-anchor a stale configured SoundFont path (survives moved installs and CWD
// changes); clear it if unresolvable so autodetect runs.
static void resolve_soundfont(void)
{
	if (soundfont[0] == '\0')
		return;
	if (file_readable(soundfont))
		return;  // resolves already (absolute path, or relative with a matching CWD)

	// Same filename, re-anchored: the data folder first, then next to the .exe, then mus and sfx.
	const char *dirs[SOUNDFONT_DIR_COUNT];
	const size_t dirCount = soundfont_dirs(dirs);
	char candidate[4096];
	for (size_t i = 0; i < dirCount; ++i)
	{
		if (dirs[i] == NULL || dirs[i][0] == '\0')
			continue;
		snprintf(candidate, sizeof(candidate), "%s/%s", dirs[i], soundfont_basename());
		if (file_readable(candidate))
		{
			fprintf(stderr, "midi: soundfont '%s' re-anchored to '%s'\n", soundfont, candidate);
			SDL_strlcpy(soundfont, candidate, sizeof(soundfont));
			return;
		}
	}

	fprintf(stderr, "midi: configured soundfont '%s' not found; will autodetect\n", soundfont);
	soundfont[0] = '\0';
}

bool soundfont_available(void)
{
	if (soundfont_scan < 0)
	{
		char found[sizeof(soundfont)];
		soundfont_scan = ((soundfont[0] != '\0' && file_readable(soundfont))
		                  || find_soundfont(found, sizeof(found))) ? 1 : 0;
	}
	return soundfont_scan != 0;
}

#else  /* !WITH_MIDI; FluidSynth isn't compiled in, so no SoundFont can be used */

bool soundfont_available(void) { return false; }

#endif /* WITH_MIDI */

static void set_output_rate(int rate)
{
	audioSampleRate = rate;

	samplesPerLdsUpdate = 2 * (audioSampleRate / ldsUpdate2Rate);
	samplesPerLdsUpdateFrac = 2 * (audioSampleRate % ldsUpdate2Rate);

	volumeFactorTable[0] = 0;
	for (size_t i = 1; i < 256; ++i)
		volumeFactorTable[i] = TO_FIXED(powf(10, (255 - i) * (-volumeRange / (20.0f * 255))));

	opl_init();
}

bool init_audio(void)
{
#ifndef WITH_MIDI
	music_device = OPL;  // no MIDI support compiled in
#else
	soundfont_scan = -1;  // rescan: a .sf2 may have been dropped in since the last start

	#ifdef NO_NATIVE_MIDI
	if (music_device == NATIVE_MIDI)
		music_device = FLUIDSYNTH;
	#endif
#endif

	if (audio_disabled)
		return false;

	SDL_AudioSpec ask, got;

	ask.freq = 11025 * OUTPUT_QUALITY;
	ask.format = AUDIO_S16SYS;
	ask.channels = AUDIO_OUTPUT_CHANNELS;
	ask.samples = 256 * OUTPUT_QUALITY; // ~23 ms
	ask.callback = audioCallback;

	if (SDL_InitSubSystem(SDL_INIT_AUDIO))
	{
		fprintf(stderr, "error: failed to initialize SDL audio: %s\n", SDL_GetError());
		audio_disabled = true;
		return false;
	}

	int allowedChanges = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE;
#if SDL_VERSION_ATLEAST(2, 0, 9)
	allowedChanges |= SDL_AUDIO_ALLOW_SAMPLES_CHANGE;
#endif
	sfxBiquadFilter = -1;  // the granted rate may differ from the last device's
	audioDevice = SDL_OpenAudioDevice(/*device*/ NULL, /*iscapture*/ 0, &ask, &got, allowedChanges);

	if (audioDevice == 0)
	{
		fprintf(stderr, "error: SDL failed to open audio device: %s\n", SDL_GetError());
		audio_disabled = true;
		return false;
	}

	set_output_rate(got.freq);

#ifdef WITH_MIDI
	if (music_device == FLUIDSYNTH)
	{
		resolve_soundfont();     // re-anchor a configured path (survives moves/CWD)
		autodetect_soundfont();  // if none is configured, adopt one from the exe/data dirs
		if (soundfont[0] == '\0')
		{
			// Nothing to load: FluidSynth would render silence. The Sound menu grays the
			// option out for the same reason, so don't leave it selected either.
			fprintf(stderr, "error: no SoundFont found for FluidSynth, falling back to OPL...\n");
			music_device = OPL;
		}
		else if (!fm_init(soundfont, audioSampleRate))
		{
			fprintf(stderr, "error: failed to initialize FluidSynth, falling back to OPL...\n");
			music_device = OPL;
		}
		else
		{
			midi_soundfont_loaded = fm_soundfont_loaded();  // confirm the .sf2 was heard
		}
	}
	else if (music_device == NATIVE_MIDI)
	{
		// Our own Win32 player; ignores SoundFonts (uses the OS synth).
		if (!wnm_init())
		{
			fprintf(stderr, "error: failed to open native MIDI, falling back to OPL...\n");
			music_device = OPL;
		}
	}
#endif

	SDL_PauseAudioDevice(audioDevice, 0); // unpause

	return true;
}

// Tear down and re-open the audio device(s), resuming whatever song was playing.
// Used when the music device or soundfont changes at runtime (see the menu).
bool restart_audio(void)
{
	if (audio_disabled)
		return false;

	unsigned int prev_song = song_playing;

	deinit_audio();
	if (!init_audio())
		return false;

	if (prev_song != NO_SONG_PLAYING)
		play_song(prev_song);

	return true;
}

// RBJ cookbook low-pass with Q = 1/sqrt(2). Returns false for Off or an unusable rate.
static bool sfxBiquadDesign(SfxBiquad *f, int filter, int sample_rate)
{
	if (filter <= SFX_FILTER_OFF || filter >= SFX_FILTER_COUNT || sample_rate <= 0)
		return false;

	const float w0 = 2.0f * (float)M_PI * sfxFilterCutoffHz[filter] / (float)sample_rate;
	if (w0 >= (float)M_PI)
		return false;

	const float cosw = cosf(w0);
	const float alpha = sinf(w0) * 0.70710678f;  // sin(w0) / (2 * Q)
	const float a0 = 1.0f + alpha;

	f->b0 = (1.0f - cosw) * 0.5f / a0;
	f->b1 = (1.0f - cosw) / a0;
	f->b2 = f->b0;
	f->a1 = -2.0f * cosw / a0;
	f->a2 = (1.0f - alpha) / a0;
	f->z1 = f->z2 = 0.0f;
	return true;
}

static float sfxBiquadStep(SfxBiquad *f, float x)
{
	const float y = f->b0 * x + f->z1;
	f->z1 = f->b1 * x - f->a1 * y + f->z2;
	f->z2 = f->b2 * x - f->a2 * y;

	// A silent tail below 1/4096 of a sample step is inaudible; zeroing it avoids denormals.
	if (x == 0.0f && fabsf(f->z1) < 1.0f && fabsf(f->z2) < 1.0f)
		f->z1 = f->z2 = 0.0f;
	return y;
}

float sfx_filter_gain(int filter, int sample_rate, bool nyquist)
{
	SfxBiquad f;
	if (!sfxBiquadDesign(&f, filter, sample_rate))
		return 1.0f;

	// Run long enough to settle, then report the last output's magnitude.
	const float amplitude = (float)TO_FIXED(INT16_MAX);
	float y = 0.0f;
	for (int i = 0; i < 4096; ++i)
		y = sfxBiquadStep(&f, (nyquist && (i & 1)) ? -amplitude : amplitude);
	return fabsf(y) / amplitude;
}

// Carry fractional samples between Loudness updates.
static int ldsUpdateSamples(int *frac)
{
	int samples = samplesPerLdsUpdate;
	*frac += samplesPerLdsUpdateFrac;
	if (*frac >= ldsUpdate2Rate)
	{
		samples += 1;
		*frac -= ldsUpdate2Rate;
	}
	return samples;
}

static void renderOpl(Sint16 *samples, int framesCount)
{
	Sint16 *remaining = samples;
	int remainingCount = framesCount;
	while (remainingCount > 0)
	{
		if (samplesUntilLdsUpdate == 0)
		{
			lds_update();
			samplesUntilLdsUpdate += ldsUpdateSamples(&samplesUntilLdsUpdateFrac);
		}

		int count = MIN(samplesUntilLdsUpdate, remainingCount);

		opl_update(remaining, count);

		remaining += count;
		remainingCount -= count;

		samplesUntilLdsUpdate -= count;
	}

	for (int frame = framesCount - 1; frame >= 0; --frame)
	{
		const Sint16 sample = samples[frame];
		for (int c = 0; c < AUDIO_OUTPUT_CHANNELS; ++c)
			samples[frame * AUDIO_OUTPUT_CHANNELS + c] = sample;
	}
}

static void finishOst(void)
{
	ostDone = true;
	playing = false;
	songlooped = false;
}

static int fadeOst(Sint16 *samples, int framesCount)
{
	for (int frame = 0; frame < framesCount; ++frame)
	{
		if (ostFadeSamples == 0)
		{
			if (ostFadeLevel == 0)
				return frame;
			--ostFadeLevel;
			ostFadeSamples = ldsUpdateSamples(&ostFadeFrac);
		}
		--ostFadeSamples;

		const Sint32 factor = volumeFactorTable[ostFadeLevel];
		for (int c = 0; c < AUDIO_OUTPUT_CHANNELS; ++c)
		{
			Sint16 *const sample = &samples[frame * AUDIO_OUTPUT_CHANNELS + c];
			*sample = (Sint16)FIXED_TO_INT(*sample * factor);
		}
	}
	return framesCount;
}

static void renderOst(Sint16 *samples, int framesCount)
{
	int rendered = 0;
	if (!ostDone)
	{
		bool looped = false;
		rendered = custom_track_render(ostTrack, samples, framesCount, ostLoops, &looped);
		if (looped)
			songlooped = true;
		if (ostFading)
			rendered = fadeOst(samples, rendered);
		if (rendered < framesCount)
			finishOst();
	}

	memset(&samples[rendered * AUDIO_OUTPUT_CHANNELS], 0,
	       (size_t)(framesCount - rendered) * AUDIO_OUTPUT_CHANNELS * sizeof(*samples));
}

// Renders a custom soundtrack track or OPL music (when OPL is selected) plus the sample channels.
// MIDI backends play on their own thread, so music stays silent here and samples still mix.
static void audioCallback(void *userdata, Uint8 *stream, int size)
{
	(void)userdata;

	Sint16 *const samples = (Sint16 *)stream;
	const int framesCount = size / (int)(AUDIO_OUTPUT_CHANNELS * sizeof (Sint16));
	const int samplesCount = framesCount * AUDIO_OUTPUT_CHANNELS;

	Sint32 musicVolumeFactor = volumeFactorTable[musicVolume];

	if (ostTrack != NULL && !music_disabled && !music_stopped)
	{
		renderOst(samples, framesCount);
	}
	else if ((music_device == OPL) && !music_disabled && !music_stopped)
	{
		renderOpl(samples, framesCount);
		musicVolumeFactor *= 2;  // OPL emulator is too quiet
	}
	else
	{
		for (int i = 0; i < samplesCount; ++i)
			samples[i] = 0;
	}

	if (samples_disabled && !music_disabled)
	{
		// Mix music
		Sint16 *remaining = samples;
		int remainingCount = samplesCount;
		while (remainingCount > 0)
		{
			Sint32 sample = *remaining * musicVolumeFactor;

			sample = FIXED_TO_INT(sample);
			*remaining = MIN(MAX(INT16_MIN, sample), INT16_MAX);

			remaining += 1;
			remainingCount -= 1;
		}
	}
	else if (!samples_disabled)
	{
		Sint32 sampleVolumeFactor = volumeFactorTable[sampleVolume];
		Sint32 sampleVolumeFactors[CHANNEL_VOLUME_LEVELS];
		for (int i = 0; i < CHANNEL_VOLUME_LEVELS; ++i)
			sampleVolumeFactors[i] = sampleVolumeFactor * (i + 1) / CHANNEL_VOLUME_LEVELS;

		const int filter = sfx_filter;
		if (filter != sfxBiquadFilter)
		{
			sfxBiquadFilter = filter;
			for (int c = 0; c < AUDIO_OUTPUT_CHANNELS; ++c)
				sfxBiquadActive = sfxBiquadDesign(&sfxBiquad[c], filter, audioSampleRate);
		}

		// Mix music and channels
		Sint16 *remaining = samples;
		int remainingCount = framesCount;
		while (remainingCount > 0)
		{
			Sint32 effects[AUDIO_OUTPUT_CHANNELS] = { 0 };

			for (size_t i = 0; i < CHANNEL_COUNT; ++i)
			{
				if (channelSampleCount[i] > 0)
				{
					for (int c = 0; c < AUDIO_OUTPUT_CHANNELS; ++c)
						effects[c] += channelSamples[i][c] * sampleVolumeFactors[channelVolume[i]];

					channelSamples[i] += AUDIO_OUTPUT_CHANNELS;
					channelSampleCount[i] -= AUDIO_OUTPUT_CHANNELS;
				}
			}

			for (int c = 0; c < AUDIO_OUTPUT_CHANNELS; ++c)
			{
				if (sfxBiquadActive)
					effects[c] = (Sint32)lrintf(sfxBiquadStep(&sfxBiquad[c], (float)effects[c]));

				Sint32 sample = FIXED_TO_INT(remaining[c] * musicVolumeFactor + effects[c]);
				remaining[c] = MIN(MAX(INT16_MIN, sample), INT16_MAX);
			}

			remaining += AUDIO_OUTPUT_CHANNELS;
			remainingCount -= 1;
		}
	}
}

void deinit_audio(void)
{
	if (audio_disabled)
		return;

#ifdef WITH_MIDI
	deinit_midi();  // safe no-op unless a MIDI device is open
#endif

	if (audioDevice != 0)
	{
		SDL_PauseAudioDevice(audioDevice, 1); // pause
		SDL_CloseAudioDevice(audioDevice);
		audioDevice = 0;
	}

	close_ost_track();

	SDL_QuitSubSystem(SDL_INIT_AUDIO);

	song_playing = NO_SONG_PLAYING;
	music_stopped = true;

	memset(channelSampleCount, 0, sizeof channelSampleCount);

	lds_free();
}

void music_deinit(void)
{
#ifdef WITH_MIDI
	if (midi_data != NULL)
	{
		for (unsigned int i = 0; i < song_count; ++i)
			MIDPROC_FreeSerialized(midi_data[i].data);
		free(midi_data);
		midi_data = NULL;
	}
#endif

	free(song_offset);
	song_offset = NULL;
	free(song_ending);
	song_ending = NULL;
	song_count = 0;

	if (music_file != NULL)
	{
		fclose(music_file);
		music_file = NULL;
	}
}

void load_music(void)  // FKA NortSong.loadSong
{
	if (music_file == NULL)
	{
		music_file = dir_fopen_die(data_dir(), "music.mus", "rb");

		fread_u16_die(&song_count, 1, music_file);

		song_offset = malloc_die((song_count + 1) * sizeof(*song_offset));

		fread_u32_die(song_offset, song_count, music_file);

		song_offset[song_count] = ftell_eof(music_file);

		song_ending = malloc_die(song_count);
		memset(song_ending, SONG_ENDING_UNKNOWN, song_count);

#ifdef WITH_MIDI
		convert_midi_data();
#endif
	}
}

static void load_song(unsigned int song_num)  // FKA NortSong.loadSong
{
	if (song_num < song_count)
	{
		unsigned int song_size = song_offset[song_num + 1] - song_offset[song_num];
		lds_load(music_file, song_offset[song_num], song_size);
	}
	else
	{
		fprintf(stderr, "warning: failed to load song %d\n", song_num + 1);
	}
}

static bool song_loops(unsigned int song_num)
{
	if (song_ending[song_num] == SONG_ENDING_UNKNOWN)
	{
		load_song(song_num);
		song_ending[song_num] = lds_plays_once() ? SONG_ENDING_ONCE : SONG_ENDING_LOOPS;
	}
	return song_ending[song_num] == SONG_ENDING_LOOPS;
}

static CustomTrack *open_ost_track(unsigned int song_num)
{
	if ((custom_sounds & CUSTOM_SOUNDS_OST) == 0)
		return NULL;

	const char *dirs[CUSTOM_AUDIO_DIR_COUNT];
	const size_t dirCount = custom_audio_dirs(CUSTOM_AUDIO_MUSIC, dirs);
	char path[CUSTOM_AUDIO_PATH_MAX];
	const char *const title = song_num < MUSIC_NUM ? musicTitle[song_num] : NULL;
	if (!custom_audio_find(dirs, dirCount, song_num + 1, title, path, sizeof(path)))
		return NULL;
	return custom_track_open(path, audioSampleRate, AUDIO_OUTPUT_CHANNELS);
}

static void reset_ost_playback(void)
{
	ostDone = false;
	ostFading = false;
	ostFadeLevel = OST_FADE_LEVEL_FULL;
	ostFadeSamples = 0;
	ostFadeFrac = 0;
	playing = true;
	songlooped = false;
	music_stopped = false;
}

static void start_ost_track(unsigned int song_num, CustomTrack *track)
{
	SDL_LockAudioDevice(audioDevice);

	music_stopped = true;

	SDL_UnlockAudioDevice(audioDevice);

#ifdef WITH_MIDI
	stop_midi_song();
#endif
	const bool loops = song_loops(song_num);

	SDL_LockAudioDevice(audioDevice);

	CustomTrack *const previous = ostTrack;
	ostTrack = track;
	ostLoops = loops;
	reset_ost_playback();

	SDL_UnlockAudioDevice(audioDevice);

	custom_track_close(previous);
	song_playing = song_num;
}

static void close_ost_track(void)
{
	if (ostTrack == NULL)
		return;

	SDL_LockAudioDevice(audioDevice);

	CustomTrack *const previous = ostTrack;
	ostTrack = NULL;
	music_stopped = true;

	SDL_UnlockAudioDevice(audioDevice);

	custom_track_close(previous);
	song_playing = NO_SONG_PLAYING;
}

void play_random_song(void)
{
	play_song((unsigned int)rand() % MUSIC_NUM);
}

void play_song(unsigned int song_num)  // FKA NortSong.playSong
{
	if (audio_disabled)
		return;
	if (song_num >= song_count)
	{
		fprintf(stderr, "warning: song %d does not exist\n", song_num + 1);
		return;
	}

	if (song_num != song_playing)
	{
		CustomTrack *const track = open_ost_track(song_num);
		if (track != NULL)
		{
			start_ost_track(song_num, track);
			return;
		}
		close_ost_track();
	}

#ifdef WITH_MIDI
	if ((music_device & IS_MIDI_DEVICE) && ostTrack == NULL)
	{
		if (music_disabled)
			return;
		if (song_num != song_playing)
		{
			play_midi_song(song_num);  // start the requested song
		}
		else
		{
			// Repeated requests for the selected song only resume it. Level-end code
			// requests its jingle every tick, so restarting would loop a one-shot.
			if (music_device == NATIVE_MIDI) wnm_resume();  // was paused by stop_song
			else                             fm_resume();
		}
		return;
	}
#endif

	if (song_num != song_playing)
	{
		SDL_LockAudioDevice(audioDevice);

		music_stopped = true;

		SDL_UnlockAudioDevice(audioDevice);

		load_song(song_num);

		song_playing = song_num;
	}

	SDL_LockAudioDevice(audioDevice);

	music_stopped = false;

	SDL_UnlockAudioDevice(audioDevice);
}

// Forget which song is selected, so the next play_song() reloads the track even when it names
// the one already up.
void clear_song_selection(void)
{
	song_playing = NO_SONG_PLAYING;
}

void restart_song(void)  // FKA Player.selectSong(1)
{
	if (audio_disabled)
		return;

	if (ostTrack != NULL)
	{
		SDL_LockAudioDevice(audioDevice);

		custom_track_rewind(ostTrack);
		reset_ost_playback();

		SDL_UnlockAudioDevice(audioDevice);
		return;
	}

#ifdef WITH_MIDI
	if (music_device & IS_MIDI_DEVICE)
	{
		if (!music_disabled && song_playing < song_count)
			play_midi_song(song_playing);  // replay from the start
		return;
	}
#endif

	SDL_LockAudioDevice(audioDevice);

	lds_rewind();

	music_stopped = false;

	SDL_UnlockAudioDevice(audioDevice);
}

void stop_song(void)  // FKA Player.selectSong(0)
{
	if (audio_disabled)
		return;

#ifdef WITH_MIDI
	if ((music_device & IS_MIDI_DEVICE) && ostTrack == NULL)
	{
		if (music_device == NATIVE_MIDI) wnm_pause();
		else                             fm_pause();
		return;
	}
#endif

	SDL_LockAudioDevice(audioDevice);

	music_stopped = true;

	SDL_UnlockAudioDevice(audioDevice);
}

void fade_song(void)  // FKA Player.selectSong($C001)
{
	if (audio_disabled)
		return;

	if (ostTrack != NULL)
	{
		SDL_LockAudioDevice(audioDevice);

		ostFading = true;

		SDL_UnlockAudioDevice(audioDevice);
		return;
	}

#ifdef WITH_MIDI
	if (music_device & IS_MIDI_DEVICE)
	{
		// Ramps the music down over 6 s, then trips the finish hook (playing=false),
		// exactly like the OPL lds_fade path the callers expect.
		if (music_device == NATIVE_MIDI) wnm_fade_out(6000);
		else                             fm_fade_out(6000);
		return;
	}
#endif

	SDL_LockAudioDevice(audioDevice);

	lds_fade(1);

	SDL_UnlockAudioDevice(audioDevice);
}

void music_fade_out_init(MusicFadeOut *fade)
{
	fade->since = SDL_GetTicks();
	fade->volume = -1;
	fade->done = false;
}

void music_fade_out_tick(MusicFadeOut *fade)
{
	if (fade->done)
		return;

	const Uint32 elapsed = SDL_GetTicks() - fade->since;
	if (elapsed >= MUSIC_FADE_OUT_MS)
	{
		music_fade_out_finish(fade);
		return;
	}

	const int volume = tyrMusicVolume - tyrMusicVolume * (int)elapsed / MUSIC_FADE_OUT_MS;
	if (volume != fade->volume)  // the pollers run far finer than the ramp has steps
	{
		fade->volume = volume;
		set_volume((Uint8)volume, fxVolume);
	}
}

void music_fade_out_finish(MusicFadeOut *fade)
{
	if (fade->done)
		return;
	fade->done = true;

	// Stop, not fade_song: the master volume goes straight back up on the next line, and a song
	// still ramping down under its own steam would be heard swelling back in.
	stop_song();
	set_volume(tyrMusicVolume, fxVolume);
}

void set_volume(Uint8 musicVolume_, Uint8 sampleVolume_)  // FKA NortSong.setVol and Player.setVol
{
	if (audio_disabled)
		return;

	SDL_LockAudioDevice(audioDevice);

	musicVolume = musicVolume_;
	sampleVolume = sampleVolume_;

	SDL_UnlockAudioDevice(audioDevice);

#ifdef WITH_MIDI
	if (music_device == NATIVE_MIDI)       wnm_set_volume(musicVolume_);
	else if (music_device & IS_MIDI_DEVICE) fm_set_volume(musicVolume_);
#endif
}

// OPL observes music_disabled in the audio callback. MIDI backends need an explicit pause on
// their device or sequencer thread.
void set_music_disabled(bool disabled)
{
	music_disabled = disabled;

#ifdef WITH_MIDI
	if (!audio_disabled && (music_device & IS_MIDI_DEVICE) && ostTrack == NULL)
	{
		if (music_device == NATIVE_MIDI)
		{
			if (disabled) wnm_pause(); else wnm_resume();
		}
		else
		{
			if (disabled) fm_pause(); else fm_resume();
		}
	}
#endif
}

static void apply_custom_music(void)
{
	const unsigned int song = song_playing;
	if (audio_disabled || song == NO_SONG_PLAYING)
		return;

	CustomTrack *const track = open_ost_track(song);
	if (track != NULL)
	{
		start_ost_track(song, track);
	}
	else if (ostTrack != NULL)
	{
		close_ost_track();
		play_song(song);
	}
}

void set_custom_sounds(int mode)
{
	const int changed = custom_sounds ^ mode;
	custom_sounds = mode;

	if ((changed & CUSTOM_SOUNDS_SFX) != 0)
		reloadSndFile(xmas);
	if ((changed & CUSTOM_SOUNDS_OST) != 0)
		apply_custom_music();
}

// Caller holds the audio device lock.
static void startChannel(const Sint16 *samples, size_t sampleCount, Uint8 chan, Uint8 vol)
{
	channelSamples[chan] = samples;
	channelSampleStart[chan] = samples;
	channelSampleCount[chan] = sampleCount - sampleCount % AUDIO_OUTPUT_CHANNELS;
	channelVolume[chan] = vol;
}

void multiSamplePlay(const Sint16 *samples, size_t sampleCount, Uint8 chan, Uint8 vol)  // FKA Player.multiSamplePlay
{
	assert(chan < CHANNEL_COUNT);
	assert(vol < CHANNEL_VOLUME_LEVELS);

	if (audio_disabled || samples_disabled)
		return;

	SDL_LockAudioDevice(audioDevice);
	startChannel(samples, sampleCount, chan, vol);
	SDL_UnlockAudioDevice(audioDevice);
}

bool multiSamplePlayLimited(const Sint16 *samples, size_t sampleCount, Uint8 chan, Uint8 vol)
{
	assert(chan < CHANNEL_COUNT);
	assert(vol < CHANNEL_VOLUME_LEVELS);

	if (audio_disabled || samples_disabled)
		return false;

	SDL_LockAudioDevice(audioDevice);

	// The target channel does not count: starting there replaces whatever it plays.
	int copies = 0;
	for (size_t i = 0; i < CHANNEL_COUNT; ++i)
	{
		if (i != chan && channelSampleCount[i] > 0 && channelSampleStart[i] == samples)
			++copies;
	}

	const bool play = sfx_copy_limit <= 0 || copies < sfx_copy_limit;
	if (play)
		startChannel(samples, sampleCount, chan, vol);

	SDL_UnlockAudioDevice(audioDevice);

	return play;
}

static struct
{
	MusicDevice musicDevice;
	Uint8 musicVolume;
	Uint8 sampleVolume;
} qaSaved;

bool audio_qa_begin(int rate)
{
	if (!audio_disabled || audioDevice != 0)
		return false;

	qaSaved.musicDevice = music_device;
	qaSaved.musicVolume = musicVolume;
	qaSaved.sampleVolume = sampleVolume;

	audio_disabled = false;
	music_device = OPL;
	samplesUntilLdsUpdate = 0;
	samplesUntilLdsUpdateFrac = 0;
	set_output_rate(rate);
	load_music();
	return true;
}

void audio_qa_render(Sint16 *samples, int frames)
{
	audioCallback(NULL, (Uint8 *)samples, frames * AUDIO_OUTPUT_CHANNELS * (int)sizeof(*samples));
}

bool audio_qa_song_loops(unsigned int song_num)
{
	return song_loops(song_num);
}

#ifdef WITH_MIDI
bool audio_qa_midi_loops(unsigned int song_num)
{
	return midi_song_loops(song_num);
}

bool audio_qa_soundfont_searches(const char *dir)
{
	const char *dirs[SOUNDFONT_DIR_COUNT];
	const size_t dirCount = soundfont_dirs(dirs);
	for (size_t i = 0; i < dirCount; ++i)
	{
		if (strcmp(dirs[i], dir) == 0)
			return true;
	}
	return false;
}
#endif

void audio_qa_end(void)
{
	close_ost_track();
	stop_sample_channels();
	lds_free();
	music_deinit();

	song_playing = NO_SONG_PLAYING;
	music_stopped = true;
	playing = false;
	songlooped = false;
	music_device = qaSaved.musicDevice;
	musicVolume = qaSaved.musicVolume;
	sampleVolume = qaSaved.sampleVolume;
	audioSampleRate = 0;
	audio_disabled = true;
}

// Stop channels before freeing or replacing sample buffers they may still reference.
void stop_sample_channels(void)
{
	if (audio_disabled)
		return;

	SDL_LockAudioDevice(audioDevice);

	for (size_t i = 0; i < CHANNEL_COUNT; ++i)
		channelSampleCount[i] = 0;

	SDL_UnlockAudioDevice(audioDevice);
}
