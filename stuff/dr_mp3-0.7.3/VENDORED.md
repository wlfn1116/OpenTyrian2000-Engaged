# Vendored dr_mp3

`dr_mp3.h` is dr_mp3 0.7.3 from [mackron/dr_libs](https://github.com/mackron/dr_libs)
at commit `5690d4671d7ad07ae6021756d7222eb159745f06`, unchanged. Its SHA-256 is
`d80e6800ddde29d50988ce894affe601858802d1bb8f047f98b84b33ae66c11a`. The license,
public domain or MIT No Attribution, is at the end of the file.

It decodes MP3 files for Setup > Sound > Custom Sounds.
`src/audio/custom_audio_codecs.c` defines `DR_MP3_IMPLEMENTATION` and
`DR_MP3_NO_STDIO` and compiles it once; other files see only its declarations.
