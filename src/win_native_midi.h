/* Callback-free Win32 SMF playback. Non-Windows builds use no-op stubs. */
#ifndef WIN_NATIVE_MIDI_H
#define WIN_NATIVE_MIDI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool wnm_init(void);   // open the OS MIDI-out device; false if unavailable
void wnm_quit(void);   // stop playback and close the device

// Start playing an in-memory SMF. `loop` repeats it until stopped; `on_finish`
// (may be NULL) is called from the player thread when a non-looping song ends.
// Replaces any song already playing. Returns false if the SMF can't be played.
bool wnm_play(const uint8_t *smf, size_t size, bool loop, void (*on_finish)(void));

void wnm_stop(void);           // stop and silence (safe; never blocks on winmm)
void wnm_pause(void);          // silence and freeze the playback clock
void wnm_resume(void);         // continue from where pause froze it
void wnm_fade_out(uint32_t ms);// ramp music down over ms, then finish like a one-shot
bool wnm_playing(void);        // true while a song is actively playing
void wnm_set_volume(uint8_t vol255);  // master music volume, 0..255

#endif /* WIN_NATIVE_MIDI_H */
