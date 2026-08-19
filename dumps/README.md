# Tyrian data dumps

One decoded tree per shipped release. Each tree holds every file of its data
directory, decoded where a reader exists and copied byte for byte where none
does. `tools/dump/dump_data.py` writes this page and everything under the trees,
so edit the tool rather than the output. DIFFERENCES.md is written by hand.

| Release | Tree | Files | Episodes | Item sets | Palettes | Backdrops | Sprite frames | Tiles |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Tyrian 1.1 | [`dump_11/`](dump_11/) | 94 | 3 | 3 | 23 | 12 | 11455 | 1148 |
| Tyrian 2.1 | [`dump_21/`](dump_21/) | 106 | 4 | 2 | 23 | 13 | 14717 | 1339 |
| Tyrian 2000 | [`dump_2000/`](dump_2000/) | 114 | 5 | 3 | 24 | 14 | 17015 | 1531 |

[DIFFERENCES.md](DIFFERENCES.md) walks through what changed between the three
releases, from the ship list to the container formats.

## Reading a tree

Start at `index.csv`. It has one row per data file: its size, what it holds,
which files in `src/` read it, and the folders this dump wrote from it. Each
tree's own `README.md` describes its layout and `manifest.json` records the
options that run used.

The same conventions hold in all three trees:

- Data file names are matched and written in lower case, whatever case the
  release ships them in.
- Sprites and tiles are coloured with palette 0, the one gameplay and the shop
  run on. Anything that names or carries another palette is drawn with that one
  instead: each `tyrian.pic` backdrop names a `palette.dat` index, each
  stand-alone PCX embeds a palette, and each cutscene face has a palette of its
  own that it is unreadable without.
- `_sheet.png` in a sprite or tile folder is a contact sheet, laid out to be
  read rather than to match the file. Compiled 12-pixel sheets use the 19-column
  grid the format itself implies, so a 2x2 item drawn from frames N, N+1, N+19
  and N+20 appears as one picture, and they are magnified three times. Cells
  alternate between two dark tones so transparent pixels stay visible.
- Field meanings for the weapon, item and enemy records are documented in
  [`doc/tyrian.hdt.txt`](../doc/tyrian.hdt.txt).

## Regenerating

Each release is dumped from its own data directory and lands in its own tree.
The tool identifies the release from the item counts in front of its tables, so
one command covers all three:

    python tools/dump/dump_data.py --data DIR

`tools/dump/verify_dump.py` checks a tree against the data it came from. Every
check either accounts for each byte of a source file or compares a decoder with
the engine calculation it mirrors:

    python tools/dump/verify_dump.py --data DIR --reproducible

Run it once per tree after regenerating. Because the readers are deterministic,
a diff in a tree is a real change in the data or in a decoder.
