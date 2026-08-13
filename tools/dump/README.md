# Data dumper

`dump_data.py` decodes every file in `data/` into `dump/`. It reads only; the
data directory is never modified. `dump/` is tracked, so it carries the same
content as `data/`, which is not.

```sh
python tools/dump/dump_data.py
```

A full run takes about ten seconds and writes roughly 90 MB.

## Options

| Option | Effect |
| --- | --- |
| `--data DIR` | Data directory to read (default `<repo>/data`). |
| `--out DIR` | Output directory (default `<repo>/dump`). |
| `--only SECTION...` | Dump only these sections. |
| `--skip SECTION...` | Skip these sections. |
| `--palette N` | `palette.dat` index for assets that store no palette (default 0, the gameplay palette). |
| `--no-map-images` | Write level maps as CSV only. |
| `--no-sprite-files` | Write contact sheets but not one PNG per sprite. |
| `--anim-stride N` | Write every Nth animation frame. |
| `--list` | Print the section names. |
| `--quiet` | Report warnings only. |

Sections are `palettes`, `images`, `sprites`, `tiles`, `anim`, `gamedata`,
`text`, `levels`, `audio`, `demos` and `raw`. `raw` copies whatever no other
enabled section decoded, so a partial run puts more files there than a full one.

## Verify

`verify_dump.py` checks the dump against the data it came from. Every check
either accounts for each byte of a source file or compares a decoder against the
arithmetic the engine uses, so "the dump is correct" is a command rather than a
judgement:

```sh
python tools/dump/verify_dump.py --reproducible
```

It prints TAP and exits nonzero on any failure. Run it after regenerating, and
after changing a reader. Adding a decoder means adding its check here; a reader
with no check is not verified, whatever the dump looks like.

## Output

`dump/index.csv` is the entry point: one row per data file with its category,
what it holds, the loader that reads it, the source files that mention it by
name, and the folders this dump wrote from it. `dump/README.md` describes the
folder layout and `dump/manifest.json` records the run.

## Files

- `dump_data.py` decides what to write and where.
- `tyrian_formats.py` decodes the containers. Each reader names the loader in
  `src/` it mirrors; keep the two in step.
- `pngwrite.py` writes indexed and RGBA PNGs with no third-party dependency.

## Adding a format

Put the decoder in `tyrian_formats.py` with a comment naming its loader, add a
writer method to `Dumper`, call `self.cover(source_file, out_path)` so the master
index links the two, and add a `CATALOG` row describing the file. A file with no
decoder still reaches `dump/raw/`, so nothing goes missing while a reader is
being written.
