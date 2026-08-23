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
#ifndef LOUDNESS_H
#define LOUDNESS_H

#include "opentyr.h"
#include "opl.h"

#include "SDL.h"

extern int audioSampleRate;

extern unsigned int song_playing;

extern bool audio_disabled, music_disabled, samples_disabled;

// Selected music synthesizer. The MIDI devices (FLUIDSYNTH/NATIVE_MIDI) only
// produce sound in builds compiled WITH_MIDI; otherwise music_device is forced
// to OPL at init. These are always declared so config/menu code compiles either
// way (see loudness.c).
extern char soundfont[4096];  // path to a General-MIDI SoundFont (.sf2), used by FLUIDSYNTH

// True when FLUIDSYNTH is the active MIDI backend and a readable SoundFont is
// configured, i.e. the SoundFont will actually be heard. For status/UI display.
extern bool midi_soundfont_loaded;
const char *soundfont_basename(void);  // basename of `soundfont` for display, "" if unset

// True when FLUIDSYNTH has something to load: either the configured `soundfont` is
// readable, or a .sf/.sf2/.sf3 sits next to the .exe or in the data folder. False
// grays the option out in the Sound menu (and keeps init_audio() on OPL). The scan
// result is cached until the next init_audio().
bool soundfont_available(void);

typedef enum {
	OPL,          // built-in OPL3 (AdLib) FM emulation; the classic Tyrian sound
	FLUIDSYNTH,   // SoundFont MIDI via FluidSynth
	NATIVE_MIDI,  // OS MIDI synth (e.g. the Windows MIDI mapper)
	MUSIC_DEVICE_MAX
} MusicDevice;

extern MusicDevice music_device;
extern const char *const music_device_names[MUSIC_DEVICE_MAX];

bool init_audio(void);
bool restart_audio(void);  // tear down + re-init audio (after changing music_device/soundfont)
void deinit_audio(void);
/* Release the loaded song archive and converted MIDI data. */
void music_deinit(void);

void load_music(void);
void play_song(unsigned int song_num);
void restart_song(void);
void stop_song(void);
void fade_song(void);
// Drop the "already selected" state so the next play_song() reloads even for the same song.
void clear_song_selection(void);

void set_volume(Uint8 musicVolume, Uint8 sampleVolume);
void set_music_disabled(bool disabled);  // toggle music on/off (pauses/resumes MIDI too)

// A short master-volume ramp to silence, for a screen that comes up over a playing track. Unlike
// fade_song it takes the same time on every backend, which is what makes it usable as a cue.
// Init when the screen appears, tick it from that screen's wait loop, and finish before leaving so
// a screen dismissed mid-ramp cannot strand the master volume down.
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

void multiSamplePlay(const Sint16 *samples, size_t sampleCount, Uint8 chan, Uint8 vol);
void stop_sample_channels(void);

#endif /* LOUDNESS_H */
