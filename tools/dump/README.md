# Tyrian data dumper

`dump_data.py` decodes a Tyrian data directory into a tracked tree under
`dumps/`. It never modifies the source data.

```sh
python tools/dump/dump_data.py
```

A complete Tyrian 2000 run takes about ten seconds and writes roughly 90 MB.

## Supported releases

The item-table counts identify the release, so the normal command needs no
version flag.

| Release | Source directory | Output tree |
| --- | --- | --- |
| Tyrian 1.1 | `data_11/` | `dumps/dump_11/` |
| Tyrian 2.1 | `data_21/` | `dumps/dump_21/` |
| Tyrian 2000 | `data/` | `dumps/dump_2000/` |

Example:

```sh
python tools/dump/dump_data.py --data data_21
```

Tyrian 2.1 moved the episode 1 to 3 item tables from the level files into
`tyrian.hdt`. `tyrian_formats.py` selects the matching layout with
`use_version`. See [version differences](../../dumps/DIFFERENCES.md) for the
full comparison.

Source filenames are normalized to lower case, including the upper-case names
from Tyrian 1.1.

## Output

Start with `index.csv`. It lists every source file, its category and contents,
the engine loader that reads it, source references, and the output folders it
produced.

Each tree also contains:

- `README.md`, describing that tree's folders
- `manifest.json`, recording the release, command, options, and warnings
- `raw/`, containing files no enabled decoder claimed

A partial run leaves more files under `raw/` than a complete run.

## Sections and options

Sections are `palettes`, `images`, `sprites`, `tiles`, `anim`, `gamedata`,
`text`, `levels`, `audio`, `demos`, and `raw`.

| Option | Effect |
| --- | --- |
| `--data DIR` | Read `DIR` instead of `<repo>/data`. |
| `--out DIR` | Write to `DIR` instead of the release's tracked tree. |
| `--only SECTION...` | Dump only the named sections. |
| `--skip SECTION...` | Omit the named sections. |
| `--palette N` | Use palette N for assets without their own palette. Default: 0. |
| `--version V` | Force `tyrian2000`, `tyrian2.1`, or `tyrian1.1`. |
| `--no-map-images` | Write map CSV files without rendered map PNGs. |
| `--no-sprite-files` | Write contact sheets without individual sprite PNGs. |
| `--anim-stride N` | Write every Nth animation frame. |
| `--list` | Print section names and exit. |
| `--quiet` | Print warnings only. |

## Verify a tree

`verify_dump.py` accounts for every source byte or compares a decoder with the
engine calculation it mirrors.

```sh
python tools/dump/verify_dump.py --reproducible
```

The verifier prints TAP and returns a nonzero status on failure. Use `--data`
and `--dump` to select another source/tree pair. Run it once per tree after a
regeneration or reader change.

## Code map

- `dump_data.py` owns orchestration, output, and the catalog.
- `tyrian_formats.py` decodes containers and names the matching engine loader.
- `pngwrite.py` writes indexed and RGBA PNGs without third-party packages.

## Add a decoder

1. Add the reader to `tyrian_formats.py` and name the engine loader it mirrors.
2. Add a writer method to `Dumper`.
3. Call `self.cover(source_file, out_path)` for the master index.
4. Add a `CATALOG` row.
5. Add verification coverage.

Until then, the file remains available under the tree's `raw/` directory.
