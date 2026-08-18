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

`verify_dump.py` checks the dump against its source data. Each check accounts for
every byte or compares a decoder with the engine calculation it mirrors:

```sh
python tools/dump/verify_dump.py --reproducible
```

It prints TAP and exits nonzero on failure. Run it after regeneration or a
reader change. Every new decoder needs a corresponding check.

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

To add a format:

1. Add the decoder to `tyrian_formats.py` and name the engine loader it mirrors.
2. Add a writer method to `Dumper`.
3. Call `self.cover(source_file, out_path)` for the master index.
4. Add a `CATALOG` row.
5. Add verification coverage.

Undecoded files still appear under `dump/raw/`.
