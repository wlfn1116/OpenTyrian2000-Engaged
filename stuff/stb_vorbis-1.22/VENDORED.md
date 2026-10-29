# Vendored stb_vorbis

`stb_vorbis.c` is stb_vorbis 1.22 from [nothings/stb](https://github.com/nothings/stb)
at commit `1ee679ca2ef753a528db5ba6801e1067b40481b8`, unchanged. Its SHA-256 is
`4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`. The license,
public domain or MIT, is at the end of the file.

It decodes Ogg Vorbis files for Setup > Sound > Custom Sounds.
`src/audio/custom_audio_codecs.c` compiles it once with
`STB_VORBIS_NO_PUSHDATA_API` and `STB_VORBIS_NO_STDIO`; other files see only its
declarations.
