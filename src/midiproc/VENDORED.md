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
- The LDS reader preserves relative detune within equivalent patch groups using
  the nearest MIDI key and pitch-bend remainder.
- A patch without a key-off length uses an envelope estimate ported from
  `opl.c`.
- Active notes are released at the song or loop boundary.
- Volume and pitch-wheel events use the channel that owns the note.
- LDS bounds checks cover the full delay block and nine-channel position record.

The MSVC project defines:

- `MIDIPROC_STATIC`;
- `NOMINMAX`;
- `NDEBUG`.

Do not define `WIN32_LEAN_AND_MEAN`; the XMI and RCP processors need multimedia
types omitted by lean mode. `NDEBUG` also avoids debug-iterator dependencies
because the project links the release CRT in all configurations.

These files are excluded from Win32 and non-MIDI builds.
