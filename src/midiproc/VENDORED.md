# Vendored midiproc

This directory contains
[midiproc](https://github.com/andyvand/midiproc) at commit
`67213aafc05657a778000d85f1cbaac03eba5649`.

It converts Tyrian LDS music to Standard MIDI Files. The code is compiled into
Windows x86-64 builds when `WITH_MIDI` is enabled. See `LICENSE` for its license.

## Local changes

- `MIDIPROC_STATIC` removes DLL import/export attributes.
- `MIDPROC_Container_SerializeAsSMFLoop()` serializes the loop section with the
  required pre-loop channel state at timestamp zero.
- `GetTimeDivision()` exposes the container's timing division.
- C++ handle typedefs use `class` forward declarations.
- The RCP debug format string uses a valid conversion specifier.
- The LDS reader keeps each patch's detune byte, reduced to that patch's offset
  within its group of otherwise identical patches, then sounds the nearest key
  and leaves the remainder on the pitch wheel. Upstream skipped the byte, which
  flattened layered voices onto a single pitch.
- The LDS reader gives a patch with no key-off length the note length its Adlib
  envelope implies, ported from `opl.c`. Upstream left such a note sounding
  until the channel's next note, which can be most of a song away.
- The LDS reader releases a note still sounding at the end of a song at that
  point rather than however many ticks it had left. A looping song marks its end
  there, so a later release is never reached.
- The LDS reader sends a note's volume controller to the channel that note
  sounds on. Upstream addressed it to the channel the previous note used, so a
  track's first level change landed on channel 0.
- The LDS reader clears its pitch-wheel cache for the channel it centred, not
  for the channel about to sound.
- LDS length checks cover what the loop after them reads: ten bytes for the
  channel delays and the percussion register, and a full nine-channel record per
  position.

The MSVC project defines:

- `MIDIPROC_STATIC`;
- `NOMINMAX`;
- `NDEBUG`.

Do not define `WIN32_LEAN_AND_MEAN`; the XMI and RCP processors need multimedia
types omitted by lean mode. `NDEBUG` also avoids debug-iterator dependencies
because the project links the release CRT in all configurations.

These files are excluded from Win32 and non-MIDI builds.
