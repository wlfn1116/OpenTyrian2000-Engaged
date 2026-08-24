# Tyrian data dumps

`tools/dump/dump_data.py` generates one decoded tree for each shipped release.
Every source file appears either as decoded output or as a byte-for-byte copy.
Edit the generator, not this page or the tree READMEs. `DIFFERENCES.md` is
maintained by hand.

| Release | Tree | Files | Episodes | Item sets | Palettes | Backdrops | Sprite frames | Tiles |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Tyrian 1.1 | [`dump_11/`](dump_11/) | 94 | 3 | 3 | 23 | 12 | 11455 | 1148 |
| Tyrian 2.1 | [`dump_21/`](dump_21/) | 106 | 4 | 2 | 23 | 13 | 14717 | 1339 |
| Tyrian 2000 | [`dump_2000/`](dump_2000/) | 114 | 5 | 3 | 24 | 14 | 17015 | 1531 |

[Compare the releases](DIFFERENCES.md) covers data, assets, text, and container
formats in detail.

## Reading a tree

Start with `index.csv`. Each row names a source file, its size and contents, the
engine code that reads it, and the output folders produced from it.

The tree README describes its folders. `manifest.json` records the command,
options, release, and warnings.

The same conventions hold in all three trees:

- Filenames are matched and written in lower case.
- Palette 0 colours sprites and tiles unless an asset identifies its own
  palette. Backdrops, PCX files, and cutscene faces carry or name theirs.
- `_sheet.png` is a readable contact sheet, not a byte-layout diagram.
- Compiled 12-pixel sheets use a 19-column grid at 3x size. Frames N, N+1, N+19,
  and N+20 therefore meet as one 2x2 item.
- Alternating dark cell colours expose transparent pixels.
- Field meanings for the weapon, item and enemy records are documented in
  [`doc/tyrian.hdt.txt`](../doc/tyrian.hdt.txt).

## Regenerating

The tool identifies a release from its item-table counts:

```sh
python tools/dump/dump_data.py --data DIR
```

Then verify that tree against its source data:

```sh
python tools/dump/verify_dump.py --data DIR --reproducible
```

Each check accounts for source bytes or compares a decoder with the engine
calculation it mirrors. The readers are deterministic, so a tree diff reflects
a data or decoder change.
