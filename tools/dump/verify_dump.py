#!/usr/bin/env python3
"""Verify a dump against its source data directory.

Checks account for source bytes or compare decoder output with engine arithmetic.
Run after regenerating ``dumps/<release tree>/``:

    python tools/dump/verify_dump.py

Exit status is 0 on success and 1 on failure. ``--reproducible`` also regenerates
the tree in a temporary directory and compares hashes.
"""

import argparse
import csv
import glob
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import dump_data                                        # noqa: E402
import tyrian_formats as tf                             # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, "..", ".."))

CHECKS = []

_NAMES = {}


def data_path(data_dir, name):
    """The file on disk behind a lower-case data name (Tyrian 1.1 ships upper case)."""
    if data_dir not in _NAMES:
        _NAMES[data_dir] = tf.data_index(data_dir)
    return os.path.join(data_dir, _NAMES[data_dir].get(name, name))


def data_files(data_dir, prefix="", suffix=""):
    """Lower-case names of the data files matching a prefix and a suffix."""
    if data_dir not in _NAMES:
        _NAMES[data_dir] = tf.data_index(data_dir)
    return sorted(n for n in _NAMES[data_dir]
                  if n.startswith(prefix) and n.endswith(suffix))


def check(name):
    def wrap(fn):
        CHECKS.append((name, fn))
        return fn
    return wrap


class Failure(Exception):
    pass


def require(condition, message):
    if not condition:
        raise Failure(message)


# Helpers shared by several checks


def decrypted_records(path, start=0, end=None):
    """Every encrypted record in a byte range, decrypted, straight from the source."""
    raw = tf.read_file(path)
    r = tf.Reader(raw, start)
    stop = len(raw) if end is None else end
    out = []
    while r.pos < stop:
        length = r.u8()
        out.append(tf.decrypt_string(r.bytes(length)))
    return out


def dumped_records(path):
    """The same records as this dump wrote them: one per line, CP437."""
    lines = open(path, encoding="utf-8").read().split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]          # the terminator on the final record
    return [line.encode("cp437") for line in lines]


def png_indexed_pixels(path):
    data = open(path, "rb").read()
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length = struct.unpack_from(">I", data, pos)[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if tag == b"IHDR":
            width, height = struct.unpack_from(">II", body, 0)
        elif tag == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    out = bytearray()
    for y in range(height):
        out += raw[y * (width + 1) + 1:(y + 1) * (width + 1)]
    return width, height, bytes(out)


# README.md names the directory the run wrote to, so a scratch regeneration
# cannot match the tracked copy. Everything it decoded still has to.
TREE_HASH_SKIP = ("README.md",)


def tree_hash(root):
    digest = hashlib.sha256()
    for base, dirs, files in os.walk(root):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            if rel in TREE_HASH_SKIP:
                continue
            digest.update(rel.encode())
            digest.update(open(path, "rb").read())
    return digest.hexdigest()


# Coverage and hygiene


@check("every data file produced output")
def _coverage(data_dir, dump_dir):
    rows = list(csv.DictReader(open(os.path.join(dump_dir, "index.csv"), encoding="utf-8")))
    on_disk = data_files(data_dir)
    require(sorted(r["file"] for r in rows) == on_disk,
            "index.csv lists %d files, data holds %d" % (len(rows), len(on_disk)))
    missing = [r["file"] for r in rows if r["dumped"] != "1"]
    require(not missing, "no output for: %s" % ", ".join(missing))
    return "%d files" % len(rows)


@check("the run recorded no warnings")
def _warnings(data_dir, dump_dir):
    manifest = json.load(open(os.path.join(dump_dir, "manifest.json"), encoding="utf-8"))
    require(not manifest["warnings"], "; ".join(manifest["warnings"]))
    return "clean"


@check("no sprite bank or tileset silently lost its contact sheet")
def _sheets(data_dir, dump_dir):
    banks = 0
    for base, _dirs, files in os.walk(os.path.join(dump_dir, "sprites")):
        if "sprites.json" in files:
            banks += 1
            require("_sheet.png" in files, "%s has no contact sheet" % base)
    for base, _dirs, files in os.walk(os.path.join(dump_dir, "tiles")):
        if "tiles.json" in files:
            require("_sheet.png" in files, "%s has no contact sheet" % base)
    icons = glob.glob(os.path.join(dump_dir, "images", "icons", "*.ico"))
    for icon in icons:
        require(os.path.exists(icon[:-4] + ".png"), "%s did not convert" % icon)
    return "%d banks, %d icons" % (banks, len(icons))


@check("generated CSVs use LF")
def _line_endings(data_dir, dump_dir):
    checked = 0
    for name in ("index.csv",
                 os.path.join("gamedata", "episodes_1-3", "enemies.csv"),
                 os.path.join("levels", "episode_1", "01_asteroid1", "map1.csv")):
        path = os.path.join(dump_dir, name)
        if not os.path.exists(path):
            continue
        require(b"\r\n" not in open(path, "rb").read(), "%s has CRLF" % name)
        checked += 1
    return "%d files" % checked


# Text: every decrypted record must survive the round trip


@check("decrypted records round-trip byte for byte")
def _records(data_dir, dump_dir):
    total = 0
    pairs = [(data_path(data_dir, "levels%d.dat" % e),
              os.path.join(dump_dir, "text", "episode_scripts", "levels%d.txt" % e))
             for e in range(1, 6)]
    pairs += [(data_path(data_dir, "cubetxt%d.dat" % e),
               os.path.join(dump_dir, "text", "datacubes", "cubetxt%d.txt" % e))
              for e in range(1, 6)]
    pairs.append((data_path(data_dir, "tyrian.cdt"),
                  os.path.join(dump_dir, "text", "credits.txt")))

    for src, out in pairs:
        if not os.path.exists(src):
            continue
        want = decrypted_records(src)
        got = dumped_records(out)
        require(want == got, "%s: %d records in the file, %d in the dump"
                % (os.path.basename(src), len(want), len(got)))
        total += len(want)

    # tyrian.hdt keeps its text in labelled groups, so compare through the JSON.
    hdt = data_path(data_dir, "tyrian.hdt")
    if os.path.exists(hdt):
        # Tyrian 2000 prefixes the text with the offset of the item tables behind
        # it. Tyrian 1.1 has none, so its text runs from byte 0 to the last byte.
        start, end = 0, None
        if tf.HDT_ITEM_OFFSET:
            start = 4
            end = struct.unpack_from("<i", tf.read_file(hdt), 0)[0]
        want = decrypted_records(hdt, start, end)
        text = json.load(open(os.path.join(dump_dir, "text", "hdt_text.json"), encoding="utf-8"))
        got = []
        for i, group in enumerate(text["groups"]):
            got.append(group["label"])
            got.extend(group["entries"])
            if i < len(text["groups"]) - 1:
                got.append(group["endLabel"])
        require(want == [g.encode("cp437") for g in got],
                "tyrian.hdt: %d records in the file, %d in the dump" % (len(want), len(got)))
        total += len(want)

    return "%d records" % total


@check("tyrian.hdt text ends where the file says it should")
def _hdt_alignment(data_dir, dump_dir):
    text = json.load(open(os.path.join(dump_dir, "text", "hdt_text.json"), encoding="utf-8"))
    # Tyrian 2000 stores the item tables behind the text and names their offset;
    # Tyrian 1.1 keeps its tables elsewhere, so the text runs to the last byte.
    end = text["itemDataOffset"]
    if end is None:
        end = os.path.getsize(data_path(data_dir, "tyrian.hdt"))
    require(text["textEndOffset"] == end,
            "text ends at %d, expected %d" % (text["textEndOffset"], end))
    return "offset %d" % end


# Tables and levels: account for every byte


@check("item tables account for every byte of their file")
def _item_tables(data_dir, dump_dir):
    checked = 0
    for label in sorted(os.listdir(os.path.join(dump_dir, "gamedata"))):
        index_path = os.path.join(dump_dir, "gamedata", label, "index.json")
        if not os.path.exists(index_path):
            continue
        index = json.load(open(index_path, encoding="utf-8"))
        size = os.path.getsize(data_path(data_dir, index["source"]))
        require(index["endOffset"] + index["bytesAfterLastTable"] == size,
                "%s: %d + %d != %d" % (label, index["endOffset"],
                                       index["bytesAfterLastTable"], size))
        # Tyrian 2000 leaves one spare enemy record past the tables; Tyrian 1.1
        # ends its level file on the last one.
        require(index["bytesAfterLastTable"] in (0, 77),
                "%s: expected nothing or one trailing enemy record, found %d bytes"
                % (label, index["bytesAfterLastTable"]))
        checked += 1
    return "%d table sets" % checked


@check("the stored item counts are the ones this release uses")
def _stored_counts(data_dir, dump_dir):
    seen = 0
    for label in sorted(os.listdir(os.path.join(dump_dir, "gamedata"))):
        index_path = os.path.join(dump_dir, "gamedata", label, "index.json")
        if not os.path.exists(index_path):
            continue
        index = json.load(open(index_path, encoding="utf-8"))
        require(tuple(index["storedCounts"]) == tuple(tf.STORED_COUNTS),
                "%s stores %s, expected %s"
                % (label, tuple(index["storedCounts"]), tuple(tf.STORED_COUNTS)))
        seen += 1
    return "%s in %d sets" % (",".join(str(n) for n in tf.STORED_COUNTS), seen)


@check("compiled frames fit the cell the contact sheets lay them on")
def _sprite2_cells(data_dir, dump_dir):
    tallest, n = 0, 0
    for label, kind, blob in _each_bank(data_dir):
        if kind != "sheet":
            continue
        for frame in tf.load_sprite2_sheet(blob):
            require(frame["height"] <= dump_data.SPRITE2_CELL[1],
                    "%s frame %d is %d rows tall" % (label, frame["index"], frame["height"]))
            tallest = max(tallest, frame["height"])
            n += 1
    return "%d frames, tallest %d of %d rows" % (n, tallest, dump_data.SPRITE2_CELL[1])


@check("level records leave no gap in the .lvl file")
def _level_gaps(data_dir, dump_dir):
    levels = 0
    for episode in range(1, 6):
        path = data_path(data_dir, "tyrian%d.lvl" % episode)
        if not os.path.exists(path):
            continue
        data, count, offsets = tf.load_level_index(path)
        for n in range(1, count // 2 + 1):
            level = tf.load_level(data, offsets[(n - 1) * 2])
            at = (n - 1) * 2 + 2
            if at < len(offsets) - 1:
                require(level["endOffset"] == offsets[at],
                        "episode %d level %d ends at %d, next starts at %d"
                        % (episode, n, level["endOffset"], offsets[at]))
            levels += 1
    return "%d levels" % levels


@check("demo input streams are consumed to the last byte")
def _demos(data_dir, dump_dir):
    total = 0
    for n in range(1, 6):
        path = data_path(data_dir, "demo.%d" % n)
        if not os.path.exists(path):
            continue
        demo = tf.load_demo(path)
        used = 31 + 2 + (len(demo["input"]) - 1) * 3
        require(used == os.path.getsize(path),
                "demo.%d: consumed %d of %d bytes" % (n, used, os.path.getsize(path)))
        total += len(demo["input"])
    return "%d input frames" % total


# Pixels: compare each decoder against the engine's own arithmetic


def _blit_sprite_reference(width, height, data):
    """src/sprite.c blit_sprite(), on a surface whose pitch equals the sprite width."""
    buf = [None] * (width * height)
    limit = width * height
    pos = x_offset = i = 0
    while i < len(data):
        b = data[i]
        if b == 255:
            i += 1
            pos += data[i]
            x_offset += data[i]
        elif b == 254:
            pos += width - x_offset
            x_offset = width
        elif b == 253:
            pos += 1
            x_offset += 1
        else:
            if pos >= limit:
                return buf                 # the engine returns here
            if pos >= 0:
                buf[pos] = b
            pos += 1
            x_offset += 1
        if x_offset >= width:
            pos += width - x_offset
            x_offset = 0
        i += 1
    return buf


def _sprite2_reference(blob, offset, end):
    """Reference the 12-pixel cursor, stopping at 0x0f or Tyrian 1.1's next offset."""
    painted = {}
    cursor, i = 0, offset
    while i < end and blob[i] != 0x0f:
        cursor += blob[i] & 0x0f
        run = (blob[i] >> 4) & 0x0f
        for _ in range(run):
            i += 1
            if i >= end:
                break
            painted[cursor] = blob[i]
            cursor += 1
        i += 1
    return painted, i


def _each_bank(data_dir):
    present = set(data_files(data_dir))
    for name in ("tyrian.shp", "tyrianc.shp"):
        if name in present:
            for (folder, kind, _title), blob in tf.load_main_shp(data_path(data_dir, name)):
                yield "%s/%s" % (name, folder), kind, blob
    for name in ("estsc.shp", "estpa.shp"):
        if name in present:
            yield name, "array", tf.read_file(data_path(data_dir, name))
    for name in data_files(data_dir, "newsh", ".shp"):
        yield name, "sheet", tf.read_file(data_path(data_dir, name))


@check("Sprite_array decode matches blit_sprite()")
def _sprite_array(data_dir, dump_dir):
    n = 0
    for label, kind, blob in _each_bank(data_dir):
        if kind != "array":
            continue
        for s in tf.load_sprite_array(blob):
            if s["width"] == 0:
                continue
            want = _blit_sprite_reference(s["width"], s["height"], s["data"])
            require(tf.decode_sprite(s["width"], s["height"], s["data"]) == want,
                    "%s sprite %d differs from the engine" % (label, s["index"]))
            n += 1
    return "%d frames" % n


@check("compiled sheet decode matches the engine cursor")
def _sprite2(data_dir, dump_dir):
    n = 0
    for label, kind, blob in _each_bank(data_dir):
        if kind != "sheet":
            continue
        offsets = tf.sprite2_offsets(blob)
        for index, offset in enumerate(offsets, start=1):
            if offset >= len(blob):
                continue
            end = offsets[index] if index < len(offsets) and offsets[index] > offset else len(blob)
            want, stopped = _sprite2_reference(blob, offset, end)
            require(stopped <= end,
                    "%s frame %d reads %d bytes past the next frame" % (label, index, stopped - end))
            height, got = tf.decode_sprite2(blob, offset, end)
            painted = sum(1 for v in got if v is not None)
            require(painted == len(want),
                    "%s frame %d paints %d pixels, engine paints %d"
                    % (label, index, painted, len(want)))
            for cursor, value in want.items():
                x, y = cursor % tf.SPRITE2_WIDTH, cursor // tf.SPRITE2_WIDTH
                require(y < height and got[y * tf.SPRITE2_WIDTH + x] == value,
                        "%s frame %d differs at (%d, %d)" % (label, index, x, y))
            n += 1
    return "%d frames" % n


@check("palette expansion matches JE_loadPals")
def _palette(data_dir, dump_dir):
    path = data_path(data_dir, "palette.dat")
    raw = tf.read_file(path)
    expanded, six = tf.load_palettes(path)
    for p in range(len(expanded)):
        for i in range(256):
            at = (p * 256 + i) * 3
            r, g, b = raw[at], raw[at + 1], raw[at + 2]
            want = (((r << 2) | (r >> 4)) & 0xff,
                    ((g << 2) | (g >> 4)) & 0xff,
                    ((b << 2) | (b >> 4)) & 0xff)
            require(tuple(expanded[p][i]) == want and (r, g, b) == tuple(six[p][i]),
                    "palette %d entry %d" % (p, i))
    return "%d entries" % (len(expanded) * 256)


@check("tile PNGs hold the source bytes")
def _tiles(data_dir, dump_dir):
    n = 0
    for name in data_files(data_dir, "shapes", ".dat"):
        path = data_path(data_dir, name)
        stem = os.path.splitext(os.path.basename(path))[0]
        for i, tile in enumerate(tf.load_tileset(path)):
            if tile is None:
                continue
            out = os.path.join(dump_dir, "tiles", stem, "tile_%03d.png" % (i + 1))
            if not os.path.exists(out):
                continue                 # written only when individual files are enabled
            width, height, pixels = png_indexed_pixels(out)
            require((width, height) == (tf.TILE_W, tf.TILE_H) and pixels == tile,
                    "%s tile %d differs from the source" % (stem, i + 1))
            n += 1
    return "%d tiles" % n


@check("tyrian.pic screens decode to exactly 320x200")
def _pic(data_dir, dump_dir):
    path = data_path(data_dir, "tyrian.pic")
    data = tf.read_file(path)
    r = tf.Reader(data)
    r.u16()
    offsets = r.array("i", tf.PCX_NUM)
    offsets.append(len(data))
    for i in range(tf.PCX_NUM):
        blob = data[offsets[i]:offsets[i + 1]]
        n = j = 0
        while n < 320 * 200 and j < len(blob):
            p = blob[j]
            j += 1
            if (p & 0xc0) == 0xc0:
                n += p & 0x3f
                j += 1
            else:
                n += 1
        require(n == 320 * 200, "screen %d decoded %d pixels" % (i + 1, n))
        # Each slice keeps the PCX palette marker; the palette itself lives in palette.dat.
        require(len(blob) - j == 1 and blob[-1] == 12,
                "screen %d has %d unexplained trailing bytes" % (i + 1, len(blob) - j))
    return "%d screens" % tf.PCX_NUM


@check("stand-alone PCX bodies are fully consumed")
def _pcx(data_dir, dump_dir):
    n = 0
    for name in data_files(data_dir, suffix=".pcx"):
        body = tf.read_file(data_path(data_dir, name))[128:-769]
        px = j = 0
        while px < 320 * 200 and j < len(body):
            p = body[j]
            j += 1
            if (p & 0xc0) == 0xc0:
                px += p & 0x3f
                j += 1
            else:
                px += 1
        require(px == 320 * 200 and j == len(body),
                "%s decoded %d pixels using %d of %d bytes" % (name, px, j, len(body)))
        n += 1
    return "%d images" % n


@check("sound and music offsets cover their file")
def _audio(data_dir, dump_dir):
    n = 0
    banks = [("tyrian.snd", tf.SFX_COUNT), ("voices.snd", tf.VOICE_COUNT),
             ("voicesc.snd", tf.VOICE_COUNT), ("music.mus", None)]
    present = set(data_files(data_dir))
    for name, expected in banks:
        if name not in present:
            continue
        data = tf.read_file(data_path(data_dir, name))
        r = tf.Reader(data)
        count = r.u16()
        if expected is not None:
            require(count == expected, "%s holds %d entries, expected %d" % (name, count, expected))
        offsets = r.array("I", count)
        require(offsets == sorted(offsets), "%s offsets are not ascending" % name)
        require(offsets[0] >= r.pos, "%s first entry overlaps its header" % name)
        require(offsets[-1] <= len(data), "%s last offset is past the end" % name)
        n += count
    return "%d entries" % n


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", default=os.path.join(REPO, "data"))
    parser.add_argument("--dump", help="dump to check (default: the tree for this release)")
    parser.add_argument("--reproducible", action="store_true",
                        help="also regenerate into a scratch tree and compare hashes")
    args = parser.parse_args(argv)

    if not os.path.isdir(args.data):
        parser.error("data directory not found: %s" % args.data)
    if args.dump is None:
        args.dump = os.path.join(REPO, dump_data.DUMP_ROOT,
                                 dump_data.TREE_NAME[tf.sniff_version(args.data)])
    if not os.path.isdir(args.dump):
        parser.error("dump directory not found: %s" % args.dump)

    # The checks read the same tables the dump was written with.
    manifest = os.path.join(args.dump, "manifest.json")
    version = None
    if os.path.exists(manifest):
        version = json.load(open(manifest, encoding="utf-8")).get("dataVersion")
    tf.use_version(version or tf.sniff_version(args.data))

    failures = 0
    print("1..%d" % (len(CHECKS) + (1 if args.reproducible else 0)))
    for n, (name, fn) in enumerate(CHECKS, start=1):
        try:
            detail = fn(args.data, args.dump)
            print("ok %d - %s (%s)" % (n, name, detail))
        except Failure as exc:
            failures += 1
            print("not ok %d - %s" % (n, name))
            print("  # %s" % exc)
        except Exception as exc:                        # noqa: BLE001 - a crash is a failure
            failures += 1
            print("not ok %d - %s" % (n, name))
            print("  # %s: %s" % (type(exc).__name__, exc))

    if args.reproducible:
        n = len(CHECKS) + 1
        with tempfile.TemporaryDirectory() as scratch:
            out = os.path.join(scratch, "dump")
            subprocess.run([sys.executable, os.path.join(HERE, "dump_data.py"),
                            "--data", args.data, "--out", out, "--quiet"], check=True)
            if tree_hash(out) == tree_hash(args.dump):
                print("ok %d - regenerating reproduces the tree" % n)
            else:
                failures += 1
                print("not ok %d - regenerating reproduces the tree" % n)
                print("  # the dump on disk does not match a fresh run")

    print("# %d checks, %d failures" % (len(CHECKS) + (1 if args.reproducible else 0), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
