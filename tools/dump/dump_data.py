#!/usr/bin/env python3
"""Dump every file in the Tyrian data directory into a browsable tree.

Sprites and backdrops become PNGs, tables become JSON and CSV, text becomes
plain text, sound becomes WAV, and level maps become both CSV and rendered
images. Anything with no decoder is copied verbatim so the dump covers the
whole directory. `dump/index.csv` lists every source file, what it holds, what
it produced, and which part of the game reads it. Nothing in data/ is modified.

    python tools/dump/dump_data.py                 # dump everything into dump/
    python tools/dump/dump_data.py --list          # show the section names
    python tools/dump/dump_data.py --only audio text

Run with --help for the full option list.
"""

import argparse
import csv
import fnmatch
import json
import os
import re
import shutil
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import pngwrite as png                                  # noqa: E402
import tyrian_formats as tf                             # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, "..", ".."))

# Palette 0 is the one gameplay and the shop run on (src/tyrian2.c), so it is
# the sane default for anything whose palette is not stored with it.
DEFAULT_PALETTE = 0

SECTIONS = ["palettes", "images", "sprites", "tiles", "anim",
            "gamedata", "text", "levels", "audio", "demos", "raw"]

# What each data file holds and which part of the game reads it. Patterns are
# matched in order, so specific names come before wildcards. The "used by"
# column names the loader; `src_references` adds anything else that mentions the
# file by name.
CATALOG = [
    ("palette.dat", "graphics", "24 VGA palettes of 256 six-bit colours",
     "src/palette.c JE_loadPals; every 8-bit draw resolves through one of these"),
    ("tyrian.pic", "graphics", "14 run-length 320x200 interface backdrops",
     "src/picload.c JE_loadPic"),
    ("tshp2.pcx", "graphics", "PCX backdrop for the Super Tyrian ship pick and the Endless run-over screen",
     "src/pcxload.c JE_loadPCX; src/tyrian2.c; src/endless.c ENDLESS_RUNEND_PIC"),
    ("shipedit.pcx", "graphics", "Backdrop for the DOS ship editor",
     "not read at runtime"),
    ("tyrian.shp", "sprites", "13 banks: fonts, planets, faces, menu art, player shots and ships",
     "src/sprite.c JE_loadMainShapeTables"),
    ("tyrianc.shp", "sprites", "Christmas variant of tyrian.shp",
     "src/sprite.c JE_loadMainShapeTables, when the Christmas theme is on"),
    ("newsh*.shp", "sprites", "Compiled 12px sheets: enemy shapebanks, shop, explosions, destruct",
     "src/sprite.c JE_loadCompShapes; enemy records name the bank, src/lvlmast.c maps it to the file"),
    ("estsc.shp", "sprites", "Ending and credits sprites",
     "src/mainint.c JE_playCredits"),
    ("estpa.shp", "sprites", "Ending sprites in the same format, unused by the game",
     "not read at runtime; browsable in the debug sprite viewer"),
    ("user1.shp", "sprites", "Player ship written by the DOS ship editor",
     "not read at runtime; browsable in the debug sprite viewer"),
    ("user2.shp", "sprites", "Player ship written by the DOS ship editor",
     "not read at runtime; browsable in the debug sprite viewer"),
    ("shapes*.dat", "tiles", "Level tileset: 600 slots of 24x28 tiles",
     "src/tyrian2.c JE_loadMap, chosen by the level header"),
    ("tyrian?.lvl", "levels", "Level headers, event scripts and tile maps; episodes 4 and 5 also hold their item tables",
     "src/lvllib.c JE_analyzeLevel; src/tyrian2.c JE_loadMap; src/episodes.c JE_loadItemDat"),
    ("levels?.dat", "levels", "Episode script: shop stock, planets, datacubes, text and level order",
     "src/tyrian2.c JE_loadMap"),
    ("cubetxt?.dat", "text", "Datacube text for one episode",
     "src/game_menu.c load_cube"),
    ("tyrian.hdt", "text", "Interface text plus the episode 1-3 item and enemy tables",
     "src/helptext.c JE_loadHelpText; src/episodes.c JE_loadItemDat"),
    ("tyrian.cdt", "text", "Credits roll, one colour-tagged line per record",
     "src/mainint.c JE_playCredits"),
    ("exitmsg.bin", "text", "80x25 DOS text screen shown on exit",
     "not read at runtime"),
    ("readme.txt", "text", "Readme shipped with the game data",
     "not read at runtime"),
    ("tyrian.snd", "audio", "31 sound effects, signed 8-bit mono at 11025 Hz",
     "src/nortsong.c loadSndFile"),
    ("voices.snd", "audio", "9 voice samples",
     "src/nortsong.c loadSndFile"),
    ("voicesc.snd", "audio", "Christmas variant of voices.snd",
     "src/nortsong.c loadSndFile, when the Christmas theme is on"),
    ("music.mus", "audio", "41 LOUDNESS (LDS) AdLib songs",
     "src/loudness.c load_music; src/lds_play.c lds_load"),
    ("loudness.awe", "audio", "AWE32 instrument bank from the DOS build",
     "not read at runtime"),
    ("gm.sf2", "audio", "General MIDI SoundFont for the FluidSynth music device",
     "src/fluid_music.c, through the soundfont path in opentyrian.cfg"),
    ("tyrend.anm", "animation", "Episode 3 ending animation, DeluxePaint LPF",
     "src/animlib.c JE_playAnim, from the ']A' episode script command"),
    ("demo.?", "demos", "Recorded attract-mode input for one level",
     "src/mainint.c load_next_demo and replay_demo_keys"),
    ("setup.box", "misc", "24-byte marker written by the DOS setup program",
     "not read at runtime"),
    ("*.ico", "graphics", "Windows icon for one of the DOS-era launchers",
     "not read at runtime"),
    ("*.pif", "misc", "Windows program information file for a DOS tool",
     "not read at runtime"),
    ("opentyrian.cfg", "config", "OpenTyrian settings written by this port",
     "src/config.c; player state, not shipped data"),
    ("tyrian.cfg", "config", "Original Tyrian settings block",
     "src/config.c"),
    ("tyrian.sav", "save", "Save slots, high scores and unlock flags",
     "src/config.c JE_loadConfiguration; player state, not shipped data"),
    ("*.ovl", "executable", "DOS extender overlay",
     "not read at runtime"),
    ("*.exe", "executable", "DOS or Windows executable shipped with the data",
     "not read at runtime"),
    ("*.dll", "library", "Runtime library for the shipped Tyrian 2000 build",
     "not read at runtime by this port"),
]


# Output helpers


def ensure_dir(path):
    os.makedirs(path, exist_ok=True)
    return path


def write_json(path, obj):
    ensure_dir(os.path.dirname(path))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=1, ensure_ascii=False)
        f.write("\n")


def write_text(path, text):
    ensure_dir(os.path.dirname(path))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
        if not text.endswith("\n"):
            f.write("\n")


def write_records(path, lines):
    """One decrypted record per line, each terminated.

    Joining with newlines instead would lose a trailing empty record, which four
    of the shipped script files end with. A reader drops exactly one empty entry
    from the end of a split.
    """
    ensure_dir(os.path.dirname(path))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        for line in lines:
            f.write(line)
            f.write("\n")


def flatten(value):
    if isinstance(value, (list, tuple)):
        return "|".join(str(flatten(v)) for v in value)
    if isinstance(value, bool):
        return "1" if value else "0"
    return value


def csv_writer(f):
    # LF, not the csv module's CRLF: the repository normalizes text to LF, so
    # CRLF here would rewrite every row of every file on each regeneration.
    return csv.writer(f, lineterminator="\n")


def write_csv(path, rows, columns=None):
    if not rows:
        return
    columns = columns or list(rows[0].keys())
    ensure_dir(os.path.dirname(path))
    with open(path, "w", encoding="utf-8", newline="") as f:
        w = csv_writer(f)
        w.writerow(columns)
        for row in rows:
            w.writerow([flatten(row.get(c, "")) for c in columns])


def grid_shape(count):
    """Columns and rows for a roughly square contact sheet."""
    if count <= 0:
        return 0, 0
    cols = 1
    while cols * cols < count:
        cols += 1
    return cols, (count + cols - 1) // cols


def portable_path(path):
    """Repo-relative and slash-separated when possible, so the tracked output of
    two machines matches."""
    full = os.path.abspath(path)
    try:
        inside = os.path.relpath(full, REPO)
    except ValueError:
        return full.replace("\\", "/")
    if inside.startswith(".."):
        return full.replace("\\", "/")
    return inside.replace("\\", "/")


def slug(text):
    out = "".join(c.lower() if c.isalnum() else "_" for c in text).strip("_")
    while "__" in out:
        out = out.replace("__", "_")
    return out or "unnamed"


def catalog_entry(name):
    lowered = name.lower()
    for pattern, category, description, used_by in CATALOG:
        if fnmatch.fnmatch(lowered, pattern.lower()):
            return category, description, used_by
    return "misc", "", ""


# Dump orchestration


class Dumper(object):

    def __init__(self, args):
        self.data_dir = args.data
        self.out = args.out
        self.palette_index = args.palette
        self.map_images = args.map_images
        self.sprite_files = args.sprite_files
        self.anim_stride = max(1, args.anim_stride)
        self.verbose = not args.quiet
        self.manifest = {"source": portable_path(self.data_dir), "sections": {}}
        self.warnings = []
        self.palettes = []
        self.raw_palettes = []
        self.level_names = {}
        self.outputs = {}      # data file -> list of paths written from it
        self.source_text = {}  # src file -> contents, for the reference scan

    # -- small utilities ---------------------------------------------------

    def path(self, *parts):
        return os.path.join(self.data_dir, *parts)

    def exists(self, name):
        return os.path.exists(self.path(name))

    def dest(self, *parts):
        return os.path.join(self.out, *parts)

    def log(self, message):
        if self.verbose:
            print(message, flush=True)

    def warn(self, message):
        self.warnings.append(message)
        print("warning: " + message, file=sys.stderr, flush=True)

    def record(self, section, key, value):
        self.manifest["sections"].setdefault(section, {})[key] = value

    def cover(self, source_name, out_path):
        """Note that `source_name` produced `out_path` (absolute or dump-relative)."""
        rel = os.path.relpath(out_path, self.out) if os.path.isabs(out_path) else out_path
        self.outputs.setdefault(source_name, [])
        rel = rel.replace("\\", "/")
        if rel not in self.outputs[source_name]:
            self.outputs[source_name].append(rel)

    def palette(self, index=None):
        if not self.palettes:
            return [(i, i, i) for i in range(256)]
        return self.palettes[(self.palette_index if index is None else index) % len(self.palettes)]

    def data_files(self):
        return sorted(n for n in os.listdir(self.data_dir)
                      if os.path.isfile(self.path(n)))

    # -- palette.dat -------------------------------------------------------

    def dump_palettes(self):
        if not self.exists("palette.dat"):
            self.warn("palette.dat is missing; images fall back to a grey ramp")
            return
        self.palettes, self.raw_palettes = tf.load_palettes(self.path("palette.dat"))

        out = ensure_dir(self.dest("palettes"))
        self.cover("palette.dat", out)
        record = []
        for i, pal in enumerate(self.palettes):
            record.append({
                "index": i,
                "rgb8": [list(c) for c in pal],
                "rgb6": [list(c) for c in self.raw_palettes[i]],
                "hex": ["#%02x%02x%02x" % c for c in pal],
            })

            # 16x16 swatch grid at 16 pixels per colour.
            cell = 16
            pixels = bytearray(256 * cell * cell)
            for entry in range(256):
                cx, cy = (entry % 16) * cell, (entry // 16) * cell
                for y in range(cell):
                    at = (cy + y) * (16 * cell) + cx
                    for x in range(cell):
                        pixels[at + x] = entry
            png.write_indexed(os.path.join(out, "palette_%02d.png" % i),
                              16 * cell, 16 * cell, bytes(pixels), pal)

            lines = ["GIMP Palette", "Name: Tyrian %02d" % i, "Columns: 16", "#"]
            lines += ["%3d %3d %3d\tindex %d" % (c[0], c[1], c[2], n) for n, c in enumerate(pal)]
            write_text(os.path.join(out, "palette_%02d.gpl" % i), "\n".join(lines))

        write_json(os.path.join(out, "palettes.json"),
                   {"count": len(self.palettes), "palettes": record,
                    "picScreenPalette": tf.PCX_PALETTE, "facePalette": tf.FACE_PALETTE})
        self.record("palettes", "count", len(self.palettes))
        self.log("palettes: %d" % len(self.palettes))

    # -- full-screen images ------------------------------------------------

    def dump_images(self):
        if self.exists("tyrian.pic"):
            out = ensure_dir(self.dest("images", "pic"))
            screens = tf.load_pic(self.path("tyrian.pic"))
            index = []
            for i, pixels in enumerate(screens):
                pal_index = tf.PCX_PALETTE[i] if i < len(tf.PCX_PALETTE) else 0
                png.write_indexed(os.path.join(out, "pic_%02d.png" % (i + 1)),
                                  320, 200, pixels, self.palette(pal_index))
                index.append({"screen": i + 1, "palette": pal_index})
            write_json(os.path.join(out, "pic.json"),
                       {"note": "tyrian.pic holds 14 320x200 backdrops; each names a palette.dat index",
                        "screens": index})
            self.cover("tyrian.pic", out)
            self.record("images", "tyrian.pic", len(screens))
            self.log("images: tyrian.pic (%d screens)" % len(screens))

        for name in ("tshp2.pcx", "shipedit.pcx"):
            if not self.exists(name):
                continue
            out = ensure_dir(self.dest("images", "pcx"))
            pixels, palette = tf.load_pcx(self.path(name))
            target = os.path.join(out, name.replace(".pcx", ".png"))
            png.write_indexed(target, 320, 200, pixels, palette or self.palette())
            self.cover(name, target)
            self.log("images: %s" % name)

        icons = [n for n in self.data_files() if n.lower().endswith(".ico")]
        if icons:
            out = ensure_dir(self.dest("images", "icons"))
            for name in icons:
                shutil.copyfile(self.path(name), os.path.join(out, name))
                self.cover(name, os.path.join(out, name))
                if self.dump_ico(self.path(name), out, name):
                    self.cover(name, os.path.join(out, name.replace(".ico", ".png")))
            self.record("images", "icons", len(icons))
            self.log("images: %d icon files" % len(icons))

    def dump_ico(self, src, out_dir, name):
        """Convert a palette-indexed Windows icon to RGBA PNG beside its copy.

        Handles the 1, 4 and 8 bit-per-pixel DIB forms the shipped icons use. Both
        bitmaps are stored bottom-up with 4-byte row padding, and the 1bpp AND mask
        that follows the image marks transparent pixels.
        """
        try:
            data = tf.read_file(src)
            count = struct.unpack_from("<H", data, 4)[0]
            for n in range(count):
                entry = 6 + n * 16
                w, h = data[entry] or 256, data[entry + 1] or 256
                size, offset = struct.unpack_from("<II", data, entry + 8)
                if size == 0:
                    continue

                bpp = struct.unpack_from("<IiiHH", data, offset)[4]
                if bpp not in (1, 4, 8):
                    self.warn("%s entry %d is %d bpp; only indexed icons convert" % (name, n, bpp))
                    continue

                used = struct.unpack_from("<I", data, offset + 32)[0]
                colours = used or (1 << bpp)
                palette, at = [], offset + 40
                for _ in range(colours):
                    palette.append((data[at + 2], data[at + 1], data[at]))
                    at += 4

                xor_stride = ((w * bpp + 31) // 32) * 4
                and_stride = ((w + 31) // 32) * 4
                and_at = at + xor_stride * h
                per_byte = 8 // bpp
                mask = (1 << bpp) - 1

                out = bytearray()
                for y in range(h):
                    xor_row = at + (h - 1 - y) * xor_stride
                    and_row = and_at + (h - 1 - y) * and_stride
                    for x in range(w):
                        shift = (per_byte - 1 - (x % per_byte)) * bpp
                        index = (data[xor_row + x // per_byte] >> shift) & mask
                        clear = (data[and_row + x // 8] >> (7 - x % 8)) & 1
                        r, g, b = palette[index] if index < len(palette) else (0, 0, 0)
                        out += bytes((r, g, b, 0 if clear else 255))

                png.write_rgba(os.path.join(out_dir, name.replace(".ico", ".png")), w, h, bytes(out))
                return True
            self.warn("%s produced no convertible image" % name)
        except Exception as exc:                        # noqa: BLE001 - report and continue
            self.warn("could not convert %s: %s" % (name, exc))
        return False

    # -- sprite sheets -----------------------------------------------------

    def write_sprite_array(self, sprites, out_dir, title):
        """Individual PNGs plus a contact sheet for one Sprite_array bank."""
        ensure_dir(out_dir)
        palette = self.palette()
        entries, decoded = [], []
        for s in sprites:
            entry = {"index": s["index"], "width": s["width"],
                     "height": s["height"], "bytes": s["size"]}
            if s["width"] == 0 or s["height"] == 0:
                entry["empty"] = True
                entries.append(entry)
                decoded.append(None)
                continue
            indices = tf.decode_sprite(s["width"], s["height"], s["data"])
            decoded.append((s["width"], s["height"], indices))
            if self.sprite_files:
                png.write_rgba(os.path.join(out_dir, "sprite_%03d.png" % s["index"]),
                               s["width"], s["height"],
                               png.indexed_to_rgba(indices, palette))
            entries.append(entry)

        self.write_contact_sheet(os.path.join(out_dir, "_sheet.png"), decoded, palette)
        write_json(os.path.join(out_dir, "sprites.json"),
                   {"title": title, "format": "Sprite_array", "count": len(sprites),
                    "palette": self.palette_index, "sprites": entries})
        return len(sprites)

    def write_sprite2_sheet(self, blob, out_dir, title):
        """Individual PNGs plus a contact sheet for one compiled 12px sheet."""
        ensure_dir(out_dir)
        palette = self.palette()
        frames = tf.load_sprite2_sheet(blob)
        entries, decoded = [], []
        for frame in frames:
            entry = {"index": frame["index"], "offset": frame["offset"],
                     "width": tf.SPRITE2_WIDTH, "height": frame["height"]}
            if not frame["pixels"]:
                entry["empty"] = True
                entries.append(entry)
                decoded.append(None)
                continue
            decoded.append((tf.SPRITE2_WIDTH, frame["height"], frame["pixels"]))
            if self.sprite_files:
                png.write_rgba(os.path.join(out_dir, "sprite_%04d.png" % frame["index"]),
                               tf.SPRITE2_WIDTH, frame["height"],
                               png.indexed_to_rgba(frame["pixels"], palette))
            entries.append(entry)

        self.write_contact_sheet(os.path.join(out_dir, "_sheet.png"), decoded, palette)
        write_json(os.path.join(out_dir, "sprites.json"),
                   {"title": title, "format": "compiled 12px sheet (Sprite2)",
                    "count": len(frames), "palette": self.palette_index,
                    "note": "frames are 1-based; 2x2 items use N, N+1, N+19, N+20",
                    "sprites": entries})
        return len(frames)

    def write_contact_sheet(self, path, decoded, palette, pad=2):
        """One image holding every frame of a bank, laid out on a grid."""
        items = [d for d in decoded if d is not None]
        if not items:
            return
        cell_w = max(d[0] for d in items) + pad
        cell_h = max(d[1] for d in items) + pad
        cols, rows = grid_shape(len(decoded))
        width, height = cols * cell_w, rows * cell_h
        if width * height > 64 * 1024 * 1024:
            self.warn("contact sheet for %s would be %dx%d; skipped" % (path, width, height))
            return

        canvas = bytearray(width * height * 4)
        for n, item in enumerate(decoded):
            if item is None:
                continue
            w, h, indices = item
            ox, oy = (n % cols) * cell_w, (n // cols) * cell_h
            for y in range(h):
                base = ((oy + y) * width + ox) * 4
                for x in range(w):
                    value = indices[y * w + x]
                    if value is None:
                        continue
                    r, g, b = palette[value]
                    at = base + x * 4
                    canvas[at:at + 4] = bytes((r, g, b, 255))
        png.write_rgba(path, width, height, bytes(canvas))

    def dump_sprites(self):
        total = 0

        for name in ("tyrian.shp", "tyrianc.shp"):
            if not self.exists(name):
                continue
            root = ensure_dir(self.dest("sprites", name.replace(".", "_")))
            banks = tf.load_main_shp(self.path(name))
            summary = []
            for (folder, kind, title), blob in banks:
                out_dir = os.path.join(root, folder)
                if kind == "array":
                    count = self.write_sprite_array(tf.load_sprite_array(blob), out_dir, title)
                else:
                    count = self.write_sprite2_sheet(blob, out_dir, title)
                summary.append({"bank": folder, "kind": kind, "title": title,
                                "count": count, "bytes": len(blob)})
                total += count
            write_json(os.path.join(root, "banks.json"), {"file": name, "banks": summary})
            self.cover(name, root)
            self.log("sprites: %s (%d banks)" % (name, len(banks)))

        # Stand-alone Sprite_array files: the ending and credits art.
        stand_alone = [("estsc.shp", "Ending and credits sprites"),
                       ("estpa.shp", "Unused ending sprites")]
        for name, title in stand_alone:
            if not self.exists(name):
                continue
            out_dir = self.dest("sprites", "standalone", name.replace(".", "_"))
            count = self.write_sprite_array(tf.load_sprite_array(tf.read_file(self.path(name))),
                                            out_dir, title)
            self.cover(name, out_dir)
            total += count
            self.log("sprites: %s (%d)" % (name, count))

        for name in ("user1.shp", "user2.shp"):
            if self.exists(name):
                total += self.dump_user_ship(name)

        # Compiled enemy and interface sheets.
        newsh = [n for n in self.data_files()
                 if n.lower().startswith("newsh") and n.lower().endswith(".shp")]
        bank_use = {}
        for n, char in enumerate(tf.SHAPE_FILE, start=1):
            bank_use.setdefault("newsh%s.shp" % char.lower(), []).append(n)
        for name in newsh:
            out_dir = self.dest("sprites", "newsh", os.path.splitext(name)[0])
            title = "Compiled sheet %s" % name
            banks = bank_use.get(name.lower())
            if banks:
                title += " (enemy shapebank %s)" % ", ".join(str(b) for b in banks)
            total += self.write_sprite2_sheet(tf.read_file(self.path(name)), out_dir, title)
            self.cover(name, out_dir)
        if newsh:
            write_json(self.dest("sprites", "newsh", "index.json"),
                       {"note": "enemy records name a 1-based shapebank; the table maps it to a file",
                        "shapeBankToFile": {str(n): "newsh%s.shp" % c.lower()
                                            for n, c in enumerate(tf.SHAPE_FILE, start=1)},
                        "files": newsh})
            self.log("sprites: %d newsh sheets" % len(newsh))

        self.record("sprites", "frames", total)

    def dump_user_ship(self, name):
        """user1.shp and user2.shp hold uncompressed 12x14 cells, not a Sprite_array."""
        cells, header, tail = tf.load_raw_cell_sheet(self.path(name))
        out_dir = ensure_dir(self.dest("sprites", "standalone", name.replace(".", "_")))
        palette = self.palette()
        alpha = [0] + [255] * 255
        decoded = []
        for i, cell in enumerate(cells):
            decoded.append((tf.RAW_CELL_W, tf.RAW_CELL_H, list(cell)))
            if self.sprite_files:
                png.write_indexed(os.path.join(out_dir, "cell_%03d.png" % i),
                                  tf.RAW_CELL_W, tf.RAW_CELL_H, cell, palette, alpha)
        self.write_contact_sheet(os.path.join(out_dir, "_sheet.png"), decoded, palette)
        write_json(os.path.join(out_dir, "sprites.json"),
                   {"title": "Ship written by the DOS ship editor", "file": name,
                    "format": "two-byte header, then uncompressed %dx%d cells"
                              % (tf.RAW_CELL_W, tf.RAW_CELL_H),
                    "count": len(cells), "palette": self.palette_index,
                    "header": header.hex(),
                    "trailingBytes": tail.hex(),
                    "note": "the game never reads these files; palette index 0 is transparent"})
        self.cover(name, out_dir)
        self.log("sprites: %s (%d cells)" % (name, len(cells)))
        return len(cells)

    # -- tilesets ----------------------------------------------------------

    def dump_tiles(self):
        files = [n for n in self.data_files()
                 if n.lower().startswith("shapes") and n.lower().endswith(".dat")]
        palette = self.palette()
        alpha = [0] + [255] * 255            # tile index 0 is transparent in every layer
        total = 0
        for name in files:
            tiles = tf.load_tileset(self.path(name))
            out_dir = ensure_dir(self.dest("tiles", os.path.splitext(name)[0]))
            present = 0
            for i, tile in enumerate(tiles):
                if tile is None:
                    continue
                present += 1
                if self.sprite_files:
                    png.write_indexed(os.path.join(out_dir, "tile_%03d.png" % (i + 1)),
                                      tf.TILE_W, tf.TILE_H, tile, palette, alpha)

            cols, rows = grid_shape(len(tiles))
            width, height = cols * tf.TILE_W, rows * tf.TILE_H
            canvas = bytearray(width * height)
            for i, tile in enumerate(tiles):
                if tile is None:
                    continue
                ox, oy = (i % cols) * tf.TILE_W, (i // cols) * tf.TILE_H
                for y in range(tf.TILE_H):
                    at = (oy + y) * width + ox
                    canvas[at:at + tf.TILE_W] = tile[y * tf.TILE_W:(y + 1) * tf.TILE_W]
            png.write_indexed(os.path.join(out_dir, "_sheet.png"),
                              width, height, bytes(canvas), palette, alpha)

            # Each of the 600 slots costs one flag byte plus, when it is not blank,
            # a full tile. Every shipped tileset carries 520 unread bytes past that.
            consumed = len(tiles) + present * tf.TILE_W * tf.TILE_H
            write_json(os.path.join(out_dir, "tiles.json"),
                       {"file": name, "slots": len(tiles), "present": present,
                        "tileWidth": tf.TILE_W, "tileHeight": tf.TILE_H,
                        "bytesRead": consumed,
                        "bytesAfterLastTile": os.path.getsize(self.path(name)) - consumed,
                        "note": "tiles are 1-based in level tile lookups; index 0 draws nothing",
                        "blank": [i + 1 for i, t in enumerate(tiles) if t is None]})
            self.cover(name, out_dir)
            total += present
            self.log("tiles: %s (%d of %d slots)" % (name, present, len(tiles)))
        self.record("tiles", "tiles", total)

    # -- tyrend.anm --------------------------------------------------------

    def dump_anim(self):
        if not self.exists("tyrend.anm"):
            return
        anm = tf.load_anm(self.path("tyrend.anm"))
        out = ensure_dir(self.dest("anim", "tyrend"))
        screen = bytearray(320 * 200)
        written, frame_number = 0, 0
        for page_index in range(anm["pageCount"]):
            try:
                base, records = tf.anm_page_records(anm, page_index)
            except (struct.error, IndexError) as exc:
                self.warn("tyrend.anm page %d is unreadable: %s" % (page_index, exc))
                break
            for record in records:
                tf.anm_apply_frame(screen, record)
                if frame_number % self.anim_stride == 0:
                    png.write_indexed(os.path.join(out, "frame_%04d.png" % frame_number),
                                      320, 200, bytes(screen), anm["palette"])
                    written += 1
                frame_number += 1

        write_json(os.path.join(out, "tyrend.json"),
                   {"file": "tyrend.anm", "format": "DeluxePaint Animation (LPF)",
                    "pages": anm["pageCount"], "records": anm["recordCount"],
                    "framesDecoded": frame_number, "framesWritten": written,
                    "stride": self.anim_stride,
                    "note": "frames are deltas; the last record loops back to frame 0",
                    "palette": [list(c) for c in anm["palette"]]})
        self.cover("tyrend.anm", out)
        self.record("anim", "frames", frame_number)
        self.log("anim: tyrend.anm (%d frames, %d written)" % (frame_number, written))

    # -- item and enemy tables ---------------------------------------------

    def dump_gamedata(self):
        sets = []
        if self.exists("tyrian.hdt"):
            data = tf.read_file(self.path("tyrian.hdt"))
            offset = struct.unpack_from("<i", data, 0)[0]
            sets.append(("episodes_1-3", "tyrian.hdt", data, offset))

        for episode in (4, 5):
            name = "tyrian%d.lvl" % episode
            if not self.exists(name):
                continue
            data, count, offsets = tf.load_level_index(self.path(name))
            # Episodes 4 and 5 keep their own item tables at the final offset.
            sets.append(("episode_%d" % episode, name, data, offsets[count - 1]))

        written = []
        for label, source, data, offset in sets:
            try:
                tables = tf.load_item_data(data, offset)
            except (EOFError, struct.error) as exc:
                self.warn("item data in %s is unreadable: %s" % (source, exc))
                continue
            out = ensure_dir(self.dest("gamedata", label))
            self.write_item_tables(out, label, source, offset, tables,
                                   len(data) - tables["endOffset"])
            self.cover(source, out)
            written.append(label)
            self.log("gamedata: %s (from %s)" % (label, source))

        write_json(self.dest("gamedata", "index.json"),
                   {"note": "episodes 1-3 share the tables in tyrian.hdt; episodes 4 and 5 each "
                            "carry their own at the end of their .lvl file, and the three sets "
                            "disagree on several items",
                    "sets": written})
        self.record("gamedata", "sets", written)

    def write_item_tables(self, out, label, source, offset, tables, trailing):
        weapons = [dict(record, id=key) for key, record in sorted(tables["weapons"].items())]
        enemies = [dict(record, id=key) for key, record in sorted(tables["enemies"].items())]

        for enemy in enemies:
            bank = enemy["shapeBank"]
            enemy["shapeBankFile"] = ("newsh%s.shp" % tf.SHAPE_FILE[bank - 1].lower()
                                      if 1 <= bank <= len(tf.SHAPE_FILE) else "")

        simple = [
            ("weapons", weapons),
            ("weapon_ports", [dict(r, id=i) for i, r in enumerate(tables["weaponPorts"])]),
            ("specials", [dict(r, id=i) for i, r in enumerate(tables["specials"])]),
            ("generators", [dict(r, id=i) for i, r in enumerate(tables["generators"])]),
            ("ships", [dict(r, id=i) for i, r in enumerate(tables["ships"])]),
            ("sidekicks", [dict(r, id=i) for i, r in enumerate(tables["sidekicks"])]),
            ("shields", [dict(r, id=i) for i, r in enumerate(tables["shields"])]),
            ("enemies", enemies),
        ]

        for name, rows in simple:
            # Names are space-padded to 30 in the data and the game keeps the padding
            # (the shop aligns its "Ammo N" suffix against it). Keep it, and add a
            # trimmed copy so the CSV reads cleanly.
            for row in rows:
                if "name" in row:
                    row["displayName"] = row["name"].rstrip()
            columns = (["id"] + [c for c in rows[0].keys() if c != "id"]) if rows else None
            write_json(os.path.join(out, name + ".json"), rows)
            write_csv(os.path.join(out, name + ".csv"), rows, columns)

        write_json(os.path.join(out, "index.json"), {
            "set": label,
            "source": source,
            "offset": offset,
            "storedCounts": tables["header"],
            "endOffset": tables["endOffset"],
            "bytesAfterLastTable": trailing,
            "bytesAfterLastTableNote": "77 bytes is one more enemy record, a duplicate of the "
                                       "last one with no graphics; the game stops before it",
            "tableSizes": {
                "weapons": "0..%d and %d..%d" % (tf.WEAP_END1, tf.WEAP_START2, tf.WEAP_NUM),
                "weaponPorts": tf.PORT_NUM + 1,
                "specials": tf.SPECIAL_NUM + 1,
                "generators": tf.POWER_NUM + 1,
                "ships": tf.SHIP_NUM + 1,
                "sidekicks": tf.OPTION_NUM + 1,
                "shields": tf.SHIELD_NUM + 1,
                "enemies": "0..%d and %d..%d" % (tf.ENEMY_END1, tf.ENEMY_START2, tf.ENEMY_NUM),
            },
            "note": "field meanings are documented in doc/tyrian.hdt.txt",
        })

    # -- text --------------------------------------------------------------

    def dump_text(self):
        if self.exists("tyrian.hdt"):
            out = ensure_dir(self.dest("text"))
            text = tf.load_hdt_text(self.path("tyrian.hdt"))
            write_json(os.path.join(out, "hdt_text.json"), text)

            lines = ["tyrian.hdt interface text",
                     "item data offset: %d, text ends at %d"
                     % (text["itemDataOffset"], text["textEndOffset"]), ""]
            for group in text["groups"]:
                lines.append("[%s]  %s" % (group["name"], group.get("label", "")))
                for i, entry in enumerate(group["entries"]):
                    lines.append("%3d: %s" % (i, entry))
                lines.append("")
            write_text(os.path.join(out, "hdt_text.txt"), "\n".join(lines))
            self.cover("tyrian.hdt", os.path.join(out, "hdt_text.json"))
            if text["textEndOffset"] > text["itemDataOffset"]:
                self.warn("tyrian.hdt text overran the item-data offset; group counts may be stale")
            self.log("text: tyrian.hdt (%d groups)" % len(text["groups"]))

        if self.exists("tyrian.cdt"):
            lines = tf.read_script_lines(tf.read_file(self.path("tyrian.cdt")))
            write_records(self.dest("text", "credits.txt"), lines)
            write_json(self.dest("text", "credits.json"),
                       {"note": "each record starts with a colour letter (colour = letter - 'A'); "
                                "'.' is a blank spacer row",
                        "lines": lines})
            self.cover("tyrian.cdt", self.dest("text", "credits.txt"))
            self.log("text: tyrian.cdt (%d lines)" % len(lines))

        for episode in range(1, 6):
            name = "cubetxt%d.dat" % episode
            if not self.exists(name):
                continue
            lines = tf.read_script_lines(tf.read_file(self.path(name)))
            write_records(self.dest("text", "datacubes", "cubetxt%d.txt" % episode), lines)
            cubes = self.split_cubes(lines)
            write_json(self.dest("text", "datacubes", "cubetxt%d.json" % episode),
                       {"file": name, "cubes": cubes})
            self.cover(name, self.dest("text", "datacubes"))
            self.log("text: %s (%d datacubes)" % (name, len(cubes)))

        if self.exists("exitmsg.bin"):
            rows = tf.load_text_screen(self.path("exitmsg.bin"))
            write_records(self.dest("text", "exitmsg.txt"), [r["text"].rstrip() for r in rows])
            write_json(self.dest("text", "exitmsg.json"),
                       {"note": "80x25 DOS text screen: one character byte and one attribute byte "
                                "per cell", "rows": rows})
            self.cover("exitmsg.bin", self.dest("text", "exitmsg.txt"))
            self.log("text: exitmsg.bin")

        if self.exists("readme.txt"):
            ensure_dir(self.dest("text"))
            shutil.copyfile(self.path("readme.txt"), self.dest("text", "data_readme.txt"))
            self.cover("readme.txt", self.dest("text", "data_readme.txt"))

        self.dump_episode_scripts()

    @staticmethod
    def split_cubes(lines):
        """Group a cubetxt file's records into its datacubes (a '*' opens each)."""
        cubes, current = [], None
        for line in lines:
            if line.startswith("*"):
                current = {"marker": line, "faceSprite": None, "title": "", "header": "", "body": []}
                cubes.append(current)
                try:
                    current["faceSprite"] = int(line[4:].split()[0]) - 1
                except (ValueError, IndexError):
                    pass
                continue
            if current is None:
                continue
            if not current["title"]:
                current["title"] = line
            elif not current["header"]:
                current["header"] = line
            else:
                current["body"].append(line)
        return cubes

    # -- episode scripts ---------------------------------------------------

    def dump_episode_scripts(self):
        # Output lands under text/, but the levels section needs the level names
        # these records carry, so both call it and the second call is a no-op.
        if self.level_names:
            return
        for episode in range(1, 6):
            name = "levels%d.dat" % episode
            if not self.exists(name):
                continue
            lines = tf.read_script_lines(tf.read_file(self.path(name)))
            out = ensure_dir(self.dest("text", "episode_scripts"))
            write_records(os.path.join(out, "levels%d.txt" % episode), lines)

            sections, current, section_index = [], None, 0
            for line in lines:
                if line.startswith("*"):
                    section_index += 1
                    current = {"section": section_index, "marker": line, "lines": []}
                    sections.append(current)
                    continue
                if current is None:
                    current = {"section": 0, "marker": "", "lines": []}
                    sections.append(current)
                entry = {"raw": line}
                if line.startswith("]"):
                    entry["command"] = line[1]
                    if line[1] == "L":
                        record = self.parse_level_record(line)
                        entry.update(record)
                        self.level_names.setdefault(episode, {}).setdefault(
                            record["levelFileNumber"], record["levelName"])
                current["lines"].append(entry)

            write_json(os.path.join(out, "levels%d.json" % episode),
                       {"file": name, "sections": sections,
                        "note": "']' opens a script command; see doc/notes.md, Level scripts"})
            self.cover(name, out)
            self.log("text: %s (%d sections)" % (name, len(sections)))

    @staticmethod
    def parse_level_record(line):
        """One ']L' record: the level a section plays (src/tyrian2.c, case 'L')."""
        def number(at, width):
            try:
                return int(line[at:at + width].strip() or 0)
            except ValueError:
                return 0
        return {
            "nextLevel": number(9, 4),
            "levelName": line[13:22].strip(),
            "song": number(22, 3),
            "levelFileNumber": number(25, 2),
            "normalBonusLevel": len(line) > 27 and line[27] == "$",
            "bonusLevel": len(line) > 28 and line[28] == "$",
        }

    # -- levels ------------------------------------------------------------

    def dump_levels(self):
        self.dump_episode_scripts()

        tilesets = {}
        for episode in range(1, 6):
            name = "tyrian%d.lvl" % episode
            if not self.exists(name):
                continue
            data, count, offsets = tf.load_level_index(self.path(name))
            level_count = count // 2
            names = self.level_names.get(episode, {})
            index = []
            for file_number in range(1, level_count + 1):
                try:
                    level = tf.load_level(data, offsets[(file_number - 1) * 2])
                except (EOFError, struct.error) as exc:
                    self.warn("%s level %d is unreadable: %s" % (name, file_number, exc))
                    continue
                label = names.get(file_number, "")
                folder = "%02d_%s" % (file_number, slug(label) if label else "unnamed")
                out = ensure_dir(self.dest("levels", "episode_%d" % episode, folder))
                self.write_level(out, level, episode, file_number, label, tilesets)
                index.append({"levelFileNumber": file_number, "name": label,
                              "folder": folder, "shapeFile": level["shapeFile"],
                              "events": len(level["events"]),
                              "randomEnemies": len(level["randomEnemies"])})
            write_json(self.dest("levels", "episode_%d" % episode, "index.json"),
                       {"file": name, "levels": index,
                        "note": "level names come from the ']L' records in levels%d.dat" % episode})
            self.cover(name, self.dest("levels", "episode_%d" % episode))
            self.log("levels: episode %d (%d levels)" % (episode, len(index)))
        self.record("levels", "episodes", sorted(self.level_names.keys()))

    def write_level(self, out, level, episode, file_number, label, tilesets):
        header = {k: v for k, v in level.items() if k not in ("events", "maps", "tileLookup")}
        header.update({"episode": episode, "levelFileNumber": file_number, "name": label,
                       "eventCount": len(level["events"])})
        write_json(os.path.join(out, "level.json"), header)

        write_csv(os.path.join(out, "events.csv"), level["events"],
                  ["time", "type", "data", "data2", "data3", "data4", "data5", "data6"])
        write_json(os.path.join(out, "events.json"), level["events"])
        write_json(os.path.join(out, "tile_lookup.json"),
                   {"note": "map byte to 1-based tile in %s; 0 draws nothing. Layer 2 forces slot "
                            "71 empty and layer 3 slots 70 and 71 (src/tyrian2.c, JE_loadMap)"
                            % level["shapeFile"],
                    "layers": level["tileLookup"]})

        for n, rows in enumerate(level["maps"]):
            with open(os.path.join(out, "map%d.csv" % (n + 1)), "w",
                      encoding="utf-8", newline="") as f:
                writer = csv_writer(f)
                for row in rows:
                    writer.writerow(row)

        if self.map_images:
            self.render_level_maps(out, level, tilesets)

    def render_level_maps(self, out, level, tilesets):
        tile_file = level["shapeFile"]
        if tile_file not in tilesets:
            if not self.exists(tile_file):
                self.warn("tileset %s is missing; skipping map images" % tile_file)
                tilesets[tile_file] = None
            else:
                tilesets[tile_file] = tf.load_tileset(self.path(tile_file))
        tiles = tilesets[tile_file]
        if tiles is None:
            return

        palette = self.palette()
        alpha = [0] + [255] * 255
        blank_row = b"\x00" * tf.TILE_W

        for layer, rows in enumerate(level["maps"]):
            lookup = level["tileLookup"][layer]
            # The loader drops the last lookup slots on the two scrolling layers.
            dropped = {1: (71,), 2: (70, 71)}.get(layer, ())

            strips = {}
            for slot in range(256):
                shape = lookup[slot] if slot < len(lookup) else 0
                if slot in dropped or shape == 0 or shape > len(tiles) or tiles[shape - 1] is None:
                    strips[slot] = [blank_row] * tf.TILE_H
                    continue
                tile = tiles[shape - 1]
                strips[slot] = [tile[y * tf.TILE_W:(y + 1) * tf.TILE_W] for y in range(tf.TILE_H)]

            width = len(rows[0]) * tf.TILE_W
            height = len(rows) * tf.TILE_H
            chunks, painted = [], False
            for row in rows:
                for y in range(tf.TILE_H):
                    for value in row:
                        strip = strips[value][y]
                        chunks.append(strip)
                        if not painted and strip != blank_row:
                            painted = True
            if not painted:
                continue
            png.write_indexed(os.path.join(out, "map%d.png" % (layer + 1)),
                              width, height, b"".join(chunks), palette, alpha)

    # -- audio -------------------------------------------------------------

    def dump_audio(self):
        titles = tf.name_list_from_source(os.path.join(REPO, "src", "sndmast.c"), "soundTitle")
        banks = [("tyrian.snd", "sfx", tf.SFX_COUNT, 0),
                 ("voices.snd", "voices", tf.VOICE_COUNT, 100),
                 ("voicesc.snd", "voices_christmas", tf.VOICE_COUNT, 100)]
        for name, folder, count, trim in banks:
            if not self.exists(name):
                continue
            try:
                samples = tf.load_sound_bank(self.path(name), count, trim)
            except ValueError as exc:
                self.warn(str(exc))
                continue
            out = ensure_dir(self.dest("audio", folder))
            index = []
            for i, blob in enumerate(samples):
                title = ""
                if folder == "sfx" and i < len(titles):
                    title = titles[i]
                elif folder.startswith("voices") and tf.SFX_COUNT + i < len(titles):
                    title = titles[tf.SFX_COUNT + i]
                stem = "%02d_%s" % (i + 1, slug(title)) if title else "%02d" % (i + 1)
                with open(os.path.join(out, stem + ".wav"), "wb") as f:
                    f.write(tf.wav_from_signed8(blob))
                index.append({"index": i + 1, "name": title, "bytes": len(blob),
                              "seconds": round(len(blob) / float(tf.SAMPLE_RATE), 4)})
            write_json(os.path.join(out, "index.json"),
                       {"file": name, "format": "signed 8-bit mono, %d Hz" % tf.SAMPLE_RATE,
                        "trimmedTailBytes": trim, "samples": index})
            self.cover(name, out)
            self.log("audio: %s (%d samples)" % (name, len(samples)))

        if self.exists("music.mus"):
            names = tf.name_list_from_source(os.path.join(REPO, "src", "musmast.c"), "musicTitle")
            songs = tf.load_music_bank(self.path("music.mus"))
            out = ensure_dir(self.dest("audio", "music"))
            index = []
            for i, blob in enumerate(songs):
                title = names[i] if i < len(names) else ""
                stem = "%02d_%s" % (i + 1, slug(title)) if title else "%02d" % (i + 1)
                with open(os.path.join(out, stem + ".lds"), "wb") as f:
                    f.write(blob)
                entry = {"index": i + 1, "name": title, "bytes": len(blob), "file": stem + ".lds"}
                try:
                    song = tf.parse_lds(blob)
                    entry.update({"instruments": len(song["instruments"]),
                                  "orders": len(song["orders"]), "speed": song["speed"],
                                  "tempo": song["tempo"], "patternLength": song["patternLength"]})
                    write_json(os.path.join(out, stem + ".json"), song)
                except (EOFError, struct.error) as exc:
                    self.warn("song %d is unreadable: %s" % (i + 1, exc))
                index.append(entry)
            write_json(os.path.join(out, "index.json"),
                       {"file": "music.mus", "format": "LOUDNESS (LDS) AdLib songs",
                        "note": ".lds blobs play in AdPlug; the .json beside each one holds the "
                                "parsed header, instruments and order list",
                        "songs": index})
            self.cover("music.mus", out)
            self.log("audio: music.mus (%d songs)" % len(songs))

    # -- demos -------------------------------------------------------------

    def dump_demos(self):
        out = ensure_dir(self.dest("demos"))
        found = []
        for n in range(1, 6):
            name = "demo.%d" % n
            if not self.exists(name):
                continue
            demo = tf.load_demo(self.path(name))
            write_json(os.path.join(out, "demo_%d.json" % n), demo)
            write_csv(os.path.join(out, "demo_%d_input.csv" % n), demo["input"],
                      ["keys", "hold", "pressed"])
            self.cover(name, os.path.join(out, "demo_%d.json" % n))
            found.append({"file": name, "episode": demo["episode"],
                          "level": demo["levelName"], "frames": len(demo["input"])})
        if found:
            write_json(os.path.join(out, "index.json"),
                       {"note": "input is a key bitmask plus a 16-bit hold count; bit order is "
                                "up, down, left, right, fire, change fire, left sidekick, "
                                "right sidekick",
                        "demos": found})
            self.log("demos: %d" % len(found))
        self.record("demos", "count", len(found))

    # -- whatever is left --------------------------------------------------

    def dump_raw(self):
        """Copy every data file with no decoded output, so the dump is complete."""
        copied = 0
        for name in self.data_files():
            if self.outputs.get(name):
                continue
            category = catalog_entry(name)[0]
            out_dir = ensure_dir(self.dest("raw", category))
            shutil.copyfile(self.path(name), os.path.join(out_dir, name))
            self.cover(name, os.path.join(out_dir, name))
            copied += 1

            if name.lower().endswith((".exe", ".ovl")):
                strings = self.extract_strings(self.path(name))
                if strings:
                    target = os.path.join(out_dir, name + ".strings.txt")
                    write_text(target, "\n".join(strings))
                    self.cover(name, target)

        if copied:
            write_json(self.dest("raw", "index.json"),
                       {"note": "files with no decoder, copied byte for byte. Executables also get "
                                "a .strings.txt of their printable runs.",
                        "count": copied})
        self.record("raw", "copied", copied)
        self.log("raw: %d files copied verbatim" % copied)

    @staticmethod
    def extract_strings(path, minimum=6, limit=20000):
        data = tf.read_file(path)
        out, run = [], bytearray()
        for byte in data:
            if 0x20 <= byte < 0x7f:
                run.append(byte)
                continue
            if len(run) >= minimum:
                out.append(run.decode("ascii"))
                if len(out) >= limit:
                    return out
            run = bytearray()
        if len(run) >= minimum:
            out.append(run.decode("ascii"))
        return out

    # -- master index ------------------------------------------------------

    def src_references(self, name):
        """Source files that mention this data file by name."""
        if not self.source_text:
            src_dir = os.path.join(REPO, "src")
            if not os.path.isdir(src_dir):
                return []
            for entry in sorted(os.listdir(src_dir)):
                if entry.endswith((".c", ".h")):
                    with open(os.path.join(src_dir, entry), "r",
                              encoding="utf-8", errors="replace") as f:
                        self.source_text[entry] = f.read()

        # Files the game opens through a format string never appear by name, so
        # each family also matches the format string that builds it.
        patterns = [re.escape(name)]
        for family, fmt in ((r'tyrian[1-9]\.lvl$', r'tyrian%[a-z]*[du]\.lvl'),
                            (r'levels[1-9]\.dat$', r'levels%[a-z]*[du]\.dat'),
                            (r'cubetxt[1-9]\.dat$', r'cubetxt%[a-z]*[du]\.dat'),
                            (r'shapes.\.dat$', r'shapes%c\.dat'),
                            (r'newsh.\.shp$', r'newsh%c\.shp'),
                            (r'demo\.[1-9]$', r'demo\.%d')):
            if re.match(family, name.lower()):
                patterns.append(fmt)

        hits = []
        for entry, text in self.source_text.items():
            if any(re.search(p, text) for p in patterns):
                hits.append(entry)
        return hits

    def write_index(self, complete=True):
        rows = []
        for name in self.data_files():
            category, description, used_by = catalog_entry(name)
            outputs = self.outputs.get(name, [])
            rows.append({
                "file": name,
                "bytes": os.path.getsize(self.path(name)),
                "category": category,
                "contents": description,
                "readBy": used_by,
                "sourceReferences": self.src_references(name),
                "dumpedTo": outputs,
                "dumped": bool(outputs),
            })

        write_csv(self.dest("index.csv"), rows,
                  ["file", "bytes", "category", "contents", "readBy",
                   "sourceReferences", "dumpedTo", "dumped"])
        write_json(self.dest("index.json"),
                   {"source": portable_path(self.data_dir),
                    "fileCount": len(rows),
                    "note": "one row per file in the data directory: what it holds, which source "
                            "files read it, and where this dump put it",
                    "files": rows})
        self.record("index", "files", len(rows))
        missing = [r["file"] for r in rows if not r["dumped"]]
        if missing and complete:
            self.warn("no output written for: %s" % ", ".join(missing))

    # -- driver ------------------------------------------------------------

    def run(self, sections):
        started = time.time()
        ensure_dir(self.out)

        # Palettes come first either way: everything else colours with them.
        self.dump_palettes()
        if "palettes" not in sections:
            self.manifest["sections"].pop("palettes", None)

        order = [("images", self.dump_images), ("sprites", self.dump_sprites),
                 ("tiles", self.dump_tiles), ("anim", self.dump_anim),
                 ("gamedata", self.dump_gamedata), ("text", self.dump_text),
                 ("levels", self.dump_levels), ("audio", self.dump_audio),
                 ("demos", self.dump_demos)]
        for name, fn in order:
            if name in sections:
                fn()

        if "raw" in sections:
            self.dump_raw()
        self.write_index(complete=sections >= set(SECTIONS))

        # The dump is tracked, so nothing that changes between runs of the same
        # input belongs in it: no timestamp, no absolute path, no elapsed time.
        self.manifest["palette"] = self.palette_index
        self.manifest["mapImages"] = self.map_images
        self.manifest["spriteFiles"] = self.sprite_files
        self.manifest["animStride"] = self.anim_stride
        self.manifest["warnings"] = self.warnings
        write_json(self.dest("manifest.json"), self.manifest)
        write_text(self.dest("README.md"), README_TEMPLATE % {
            "source": portable_path(self.data_dir),
            "palette": self.palette_index,
        })
        self.log("done in %.1fs -> %s" % (time.time() - started, self.out))


README_TEMPLATE = """# Tyrian data dump

Generated by `tools/dump/dump_data.py` from `%(source)s`. Every file in the data
directory is represented here. Sprites and tiles are coloured with palette
%(palette)d unless the asset stores its own palette. Regenerate with:

    python tools/dump/dump_data.py

The same data and the same readers produce the same tree, so a diff here is a
real change. Do not edit these files by hand.

## Where to start

`index.csv` has one row per data file: its size, what it holds, which source
files read it, and the folders this dump wrote from it. `manifest.json` records
the options this run used and any warnings.

## Layout

| Folder | Contents |
| --- | --- |
| `palettes/` | The 24 palettes in `palette.dat` as JSON, PNG swatches and GIMP `.gpl` files. |
| `images/pic/` | The 14 full-screen backdrops in `tyrian.pic`, each with the palette it ships with. |
| `images/pcx/` | `tshp2.pcx` and `shipedit.pcx`, which carry their own palettes. |
| `images/icons/` | The Windows icons, copied and converted to PNG. |
| `sprites/tyrian_shp/` | The 13 banks in `tyrian.shp`: fonts, faces, planets, menu art, shots and ships. |
| `sprites/tyrianc_shp/` | The Christmas variant of the same file. |
| `sprites/newsh/` | The compiled 12px enemy and interface sheets, plus the shapebank map. |
| `sprites/standalone/` | `estsc.shp`, `estpa.shp` and the two DOS ship-editor files. |
| `tiles/` | The five `shapes?.dat` tilesets, one PNG per tile plus a contact sheet. |
| `anim/tyrend/` | Every decoded frame of the episode 3 ending animation. |
| `gamedata/` | Weapons, ports, specials, generators, ships, sidekicks, shields and enemies. |
| `text/` | Interface text, credits, datacubes, episode scripts and the DOS exit screen. |
| `levels/` | Per level: header, event script, the three tile maps as CSV, and rendered maps. |
| `audio/` | Sound effects and voices as WAV, songs as `.lds` blobs with parsed JSON. |
| `demos/` | The five recorded attract-mode demos, header and input stream. |
| `raw/` | Every file no other section decoded, copied byte for byte, with strings pulled from executables. |

## Notes

Item and enemy tables exist in three versions. Episodes 1 to 3 share the set in
`tyrian.hdt`; episodes 4 and 5 each carry their own at the end of their `.lvl`
file. `gamedata/` keeps them separate because they disagree on several items.

Enemy records name a 1-based shapebank rather than a file.
`sprites/newsh/index.json` maps each shapebank to the sheet that holds its frames.

Compiled sheet frames are 1-based and 12 pixels wide. A 2x2 item uses frames
N, N+1, N+19 and N+20.

Level tile maps store a byte per cell. `tile_lookup.json` turns that byte into a
1-based tile in the level's tileset, where 0 draws nothing. Palette index 0 is
transparent in every background layer.

Field meanings for the weapon, item and enemy records are documented in
`doc/tyrian.hdt.txt`.

`tyrian.sav` and `opentyrian.cfg` are player state rather than shipped data, so
they are copied into `raw/` without being parsed.
"""


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default=os.path.join(REPO, "data"),
                        help="data directory to read (default: <repo>/data)")
    parser.add_argument("--out", default=os.path.join(REPO, "dump"),
                        help="output directory (default: <repo>/dump)")
    parser.add_argument("--only", nargs="+", metavar="SECTION",
                        help="dump only these sections")
    parser.add_argument("--skip", nargs="+", metavar="SECTION", default=[],
                        help="skip these sections")
    parser.add_argument("--palette", type=int, default=DEFAULT_PALETTE,
                        help="palette.dat index for assets with no palette of their own")
    parser.add_argument("--no-map-images", dest="map_images", action="store_false",
                        help="write level maps as CSV only, skipping the rendered PNGs")
    parser.add_argument("--no-sprite-files", dest="sprite_files", action="store_false",
                        help="write only contact sheets, not one PNG per sprite")
    parser.add_argument("--anim-stride", type=int, default=1,
                        help="write every Nth animation frame (default: every frame)")
    parser.add_argument("--list", action="store_true", help="list the section names and exit")
    parser.add_argument("--quiet", action="store_true", help="only report warnings")
    args = parser.parse_args(argv)

    if args.list:
        print("\n".join(SECTIONS))
        return 0

    unknown = [s for s in (args.only or []) + args.skip if s not in SECTIONS]
    if unknown:
        parser.error("unknown section(s): %s" % ", ".join(unknown))

    if not os.path.isdir(args.data):
        parser.error("data directory not found: %s" % args.data)

    sections = set(args.only or SECTIONS) - set(args.skip)
    Dumper(args).run(sections)
    return 0


if __name__ == "__main__":
    sys.exit(main())
