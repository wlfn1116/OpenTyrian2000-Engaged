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

The MSVC project defines:

- `MIDIPROC_STATIC`;
- `NOMINMAX`;
- `NDEBUG`.

Do not define `WIN32_LEAN_AND_MEAN`; the XMI and RCP processors need multimedia
types omitted by lean mode. `NDEBUG` also avoids debug-iterator dependencies
because the project links the release CRT in all configurations.

These files are excluded from Win32 and non-MIDI builds.
