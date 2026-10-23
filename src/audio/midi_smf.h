/* Standard MIDI File parser for the MIDI backends. */
#ifndef MIDI_SMF_H
#define MIDI_SMF_H

#include "SDL.h"

#include <stdbool.h>
#include <stddef.h>

// Timed playable MIDI event; parsing consumes tempo, loop, and other metadata.
typedef struct
{
	Uint64 time_us;   // absolute time from song start, microseconds
	Uint8  status;    // 0x80..0xEF channel message, or 0xF0 == SysEx
	Uint8  d1, d2;    // data bytes (channel messages)
	Uint32 sx_off;    // SysEx: offset into sysex
	Uint32 sx_len;    // SysEx: byte count (0 for channel messages)
} MidiEvent;

typedef struct
{
	MidiEvent *events;
	size_t     event_count;
	Uint8     *sysex;         // concatenated SysEx payloads

	// Loop point from the SMF's "loopStart" marker (meta 0x06), as written by the
	// LDS->MIDI conversion for songs that jump back mid-song instead of restarting.
	bool       loop_marker;
	Uint64     loop_us;       // loop target, microseconds from song start
	size_t     loop_idx;      // first event at/after the loop target
} MidiSong;

// Parse `data` into song, replacing its contents. Returns false (and leaves nothing allocated)
// if the file isn't a usable SMF. Handles format 0/1 (tracks are merged by time).
bool midi_smf_parse(MidiSong *song, const Uint8 *data, size_t size);
void midi_smf_free(MidiSong *song);

#endif /* MIDI_SMF_H */
