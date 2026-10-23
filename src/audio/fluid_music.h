/* FluidSynth SMF playback with loop-marker support. */
#ifndef FLUID_MUSIC_H
#define FLUID_MUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Start FluidSynth; empty soundfonts are silent and nonpositive sample rates use 44100 Hz.
bool fm_init(const char *soundfont, int sample_rate);
void fm_quit(void);   // stop playback and tear down synth + driver

// Replace the current song with an in-memory SMF. Loops honor loopStart;
// on_finish may be NULL and runs on the sequencer thread.
bool fm_play(const uint8_t *smf, size_t size, bool loop, void (*on_finish)(void));

void fm_stop(void);            // stop and silence
void fm_pause(void);           // silence and freeze the playback clock
void fm_resume(void);          // continue from where pause froze it
void fm_fade_out(uint32_t ms); // ramp music down over ms, then finish like a one-shot
bool fm_playing(void);         // true while a song is actively playing
void fm_set_volume(uint8_t vol255);  // master music volume, 0..255

bool fm_soundfont_loaded(void);  // true if the SoundFont actually loaded into the synth

#endif /* FLUID_MUSIC_H */
