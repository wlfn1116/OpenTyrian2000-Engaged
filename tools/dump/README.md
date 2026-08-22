# Data dumper

`dump_data.py` decodes every file in a data directory into a tree under
`dumps/`. It reads only; the data directory is never modified. The trees are
tracked, so they carry the same content as the data directories, which are not.

```sh
python tools/dump/dump_data.py
```

A full run takes about ten seconds and writes roughly 90 MB.

## Data versions

The dumper reads all three shipped releases. It identifies one from the item
counts stored in front of its tables, so no option is needed to switch, and each
release has a data directory and a tree of its own:

| Release | Data | Tree |
| --- | --- | --- |
| Tyrian 1.1 | `data_11/` | `dumps/dump_11/` |
| Tyrian 2.1 | `data_21/` | `dumps/dump_21/` |
| Tyrian 2000 | `data/` | `dumps/dump_2000/` |

```sh
python tools/dump/dump_data.py --data data_21
```

Table sizes and container layouts differ by release. Tyrian 2.1 moved the
episode 1 to 3 item tables from the level files into `tyrian.hdt`.
`tyrian_formats.py` binds the matching tables with `use_version`; see
`dumps/DIFFERENCES.md` for a readable comparison. Source names are normalized to
lower case, so Tyrian 1.1 produces the same tree with upper- or lower-case files.

## Options

| Option | Effect |
| --- | --- |
| `--data DIR` | Data directory to read (default `<repo>/data`). |
| `--out DIR` | Output directory (default `dumps/<release tree>`). |
| `--only SECTION...` | Dump only these sections. |
| `--skip SECTION...` | Skip these sections. |
| `--palette N` | `palette.dat` index for assets that store no palette (default 0, the gameplay palette). |
| `--version V` | Decode as `tyrian2000`, `tyrian2.1` or `tyrian1.1` instead of the sniffed release. |
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
reader change, once per tree; `--data` and `--dump` pick the pair to check.
Every new decoder needs a corresponding check.

## Output

`index.csv` is the entry point of a tree: one row per data file with its
category, what it holds, the loader that reads it, the source files that mention
it by name, and the folders this dump wrote from it. The tree's `README.md`
describes its folder layout and `manifest.json` records the run. `dumps/README.md`
indexes the trees and is rewritten whenever one of them is.

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

Undecoded files still appear under `dumps/<release tree>/raw/`.
