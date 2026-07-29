/* FluidSynth SMF playback with loop-marker support. */
#ifndef FLUID_MUSIC_H
#define FLUID_MUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Create the synth + audio driver and load `soundfont` (may be "" -> silent synth).
// `sample_rate` <= 0 defaults to 44100. Returns false if the synth or audio driver
// can't be created (caller then falls back to OPL).
bool fm_init(const char *soundfont, int sample_rate);
void fm_quit(void);   // stop playback and tear down synth + driver

// Start playing an in-memory SMF. `loop` repeats it -- at the loopStart marker if the
// song has one, else from the top -- until stopped; `on_finish` (may be NULL) is
// called from the sequencer thread when a non-looping song ends. Replaces any song
// already playing. Returns false if the SMF can't be played.
bool fm_play(const uint8_t *smf, size_t size, bool loop, void (*on_finish)(void));

void fm_stop(void);            // stop and silence
void fm_pause(void);           // silence and freeze the playback clock
void fm_resume(void);          // continue from where pause froze it
void fm_fade_out(uint32_t ms); // ramp music down over ms, then finish like a one-shot
bool fm_playing(void);         // true while a song is actively playing
void fm_set_volume(uint8_t vol255);  // master music volume, 0..255

bool fm_soundfont_loaded(void);  // true if the SoundFont actually loaded into the synth

#endif /* FLUID_MUSIC_H */
