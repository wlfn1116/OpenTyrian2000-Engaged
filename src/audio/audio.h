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
#ifndef AUDIO_H
#define AUDIO_H

#include "opentyr.h"
#include "opl.h"

#include "SDL.h"

extern int audioSampleRate;

extern unsigned int song_playing;

extern bool audio_disabled, music_disabled, samples_disabled;

// MIDI choices require WITH_MIDI; other builds force OPL during initialization.
extern char soundfont[4096];  // path to a General-MIDI SoundFont (.sf2), used by FLUIDSYNTH

// True when FLUIDSYNTH is the active MIDI backend and a readable SoundFont is
// configured, i.e. the SoundFont will actually be heard. For status/UI display.
extern bool midi_soundfont_loaded;
const char *soundfont_basename(void);  // basename of `soundfont` for display, "" if unset

// True when FLUIDSYNTH has something to load: either the configured `soundfont` is readable, or
// a .sf/.sf2/.sf3 sits next to the .exe, in the data folder or in a mus or sfx folder.
bool soundfont_available(void);

typedef enum {
	OPL,          // built-in OPL3 (AdLib) FM emulation; the classic Tyrian sound
	FLUIDSYNTH,   // SoundFont MIDI via FluidSynth
	NATIVE_MIDI,  // OS MIDI synth (e.g. the Windows MIDI mapper)
	MUSIC_DEVICE_MAX
} MusicDevice;

extern MusicDevice music_device;
extern const char *const music_device_names[MUSIC_DEVICE_MAX];

typedef enum
{
	CUSTOM_SOUNDS_OFF = 0,
	CUSTOM_SOUNDS_OST = 1,
	CUSTOM_SOUNDS_SFX = 2,
	CUSTOM_SOUNDS_BOTH = CUSTOM_SOUNDS_OST | CUSTOM_SOUNDS_SFX,
	CUSTOM_SOUNDS_COUNT
} CustomSounds;

extern int custom_sounds;
extern const char *const custom_sounds_names[CUSTOM_SOUNDS_COUNT];
void set_custom_sounds(int mode);

bool init_audio(void);
bool restart_audio(void);  // tear down + re-init audio (after changing music_device/soundfont)
void deinit_audio(void);
/* Release the loaded song archive and converted MIDI data. */
void music_deinit(void);

void load_music(void);
void play_song(unsigned int song_num);
void play_random_song(void);
void restart_song(void);
void stop_song(void);
void fade_song(void);
// Drop the "already selected" state so the next play_song() reloads even for the same song.
void clear_song_selection(void);

void set_volume(Uint8 musicVolume, Uint8 sampleVolume);
void set_music_disabled(bool disabled);  // toggle music on/off (pauses/resumes MIDI too)

#define AUDIO_OUTPUT_CHANNELS 2

// A short master-volume ramp to silence, for a screen that comes up over a playing track.
#define MUSIC_FADE_OUT_MS 500

typedef struct
{
	Uint32 since;  // SDL_GetTicks when the ramp started
	int volume;    // last master volume pushed, -1 for none yet
	bool done;
} MusicFadeOut;

void music_fade_out_init(MusicFadeOut *fade);
void music_fade_out_tick(MusicFadeOut *fade);
void music_fade_out_finish(MusicFadeOut *fade);

// Channels 0-7 belong to the sim's soundQueue slots; this one is reserved for
// presentation-side cues so playing them can never cut a queued game sound.
#define SFX_CUE_CHANNEL 8

// Setup > Sound low-pass on the mixed sound effects. Music bypasses it.
typedef enum
{
	SFX_FILTER_OFF = 0,
	SFX_FILTER_LIGHT,
	SFX_FILTER_STRONG,
	SFX_FILTER_COUNT
} SfxFilter;

extern int sfx_filter;  // SfxFilter

// Most channels one sound effect may play on at once; 0 means no limit.
#define SFX_COPY_LIMIT_MAX 3
extern int sfx_copy_limit;

void multiSamplePlay(const Sint16 *samples, size_t sampleCount, Uint8 chan, Uint8 vol);
// multiSamplePlay unless sfx_copy_limit other channels already play these samples.
// Returns whether the sound started.
bool multiSamplePlayLimited(const Sint16 *samples, size_t sampleCount, Uint8 chan, Uint8 vol);
void stop_sample_channels(void);

// Settled gain of a filter preset for a constant (nyquist false) or alternating input. QA only.
float sfx_filter_gain(int filter, int sample_rate, bool nyquist);

bool audio_qa_begin(int rate);
void audio_qa_render(Sint16 *samples, int frames);
bool audio_qa_song_loops(unsigned int song_num);
bool audio_qa_midi_loops(unsigned int song_num);
bool audio_qa_soundfont_searches(const char *dir);
void audio_qa_end(void);

#endif /* AUDIO_H */
