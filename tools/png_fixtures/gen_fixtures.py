#!/usr/bin/env python3
import io
import os
import struct
import sys
import zlib

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "src", "qa", "qa_png_fixtures.h"))

CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}
ADAM7 = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
         (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
PALETTE_SIZE = 11
PALETTE_ALPHA = [0, 64, 255, 128]
PALETTE = b"".join(bytes((i * 40 & 255, i * 90 & 255, i * 150 & 255)) for i in range(PALETTE_SIZE))


def sample(x, y, c, depth):
    return (x * 7919 + y * 104729 + c * 31337 + 12345) & ((1 << depth) - 1)


def pack_row(samples, depth):
    if depth == 16:
        return b"".join(struct.pack(">H", s) for s in samples)
    if depth == 8:
        return bytes(samples)
    out, acc, bits = bytearray(), 0, 0
    for s in samples:
        acc, bits = (acc << depth) | s, bits + depth
        if bits == 8:
            out.append(acc)
            acc, bits = 0, 0
    if bits:
        out.append(acc << (8 - bits))
    return bytes(out)


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def filter_row(kind, row, prior, bpp):
    out = bytearray(len(row))
    for i in range(len(row)):
        left = row[i - bpp] if i >= bpp else 0
        up = prior[i] if prior is not None else 0
        corner = prior[i - bpp] if prior is not None and i >= bpp else 0
        out[i] = (row[i] - [0, left, up, (left + up) // 2, paeth(left, up, corner)][kind]) & 255
    return bytes(out)


def raw_stream(color, depth, w, h, interlaced, value, filters):
    channels = CHANNELS[color]
    bpp = max(1, channels * depth // 8)
    out = bytearray()
    for x0, y0, dx, dy in (ADAM7 if interlaced else [(0, 0, 1, 1)]):
        pw = (w - x0 + dx - 1) // dx if w > x0 else 0
        ph = (h - y0 + dy - 1) // dy if h > y0 else 0
        if pw == 0 or ph == 0:
            continue
        prior = None
        for py in range(ph):
            row = pack_row([value(x0 + px * dx, y0 + py * dy, c)
                            for px in range(pw) for c in range(channels)], depth)
            kind = filters(py)
            out.append(kind)
            out += filter_row(kind, row, prior, bpp)
            prior = row
    return bytes(out)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def deflate(level, strategy=zlib.Z_DEFAULT_STRATEGY):
    def run(data):
        z = zlib.compressobj(level, zlib.DEFLATED, 15, 9, strategy)
        return z.compress(data) + z.flush()
    return run


def make(color, depth, w, h, interlaced, value, filters, compress, plte=None, trns=None, parts=1):
    header = struct.pack(">IIBBBBB", w, h, depth, color, 0, 0, 1 if interlaced else 0)
    z = compress(raw_stream(color, depth, w, h, interlaced, value, filters))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
    if plte is not None:
        png += chunk(b"PLTE", plte)
    if trns is not None:
        png += chunk(b"tRNS", trns)
    cuts = [len(z) * i // parts for i in range(parts + 1)]
    for i in range(parts):
        png += chunk(b"IDAT", z[cuts[i]:cuts[i + 1]])
    return png + chunk(b"IEND", b""), (z[2] >> 1) & 3


def scale(s, depth):
    return s >> 8 if depth == 16 else s * 255 // ((1 << depth) - 1)


def expected_rgba(color, depth, keyed, x, y):
    s = [sample(x, y, c, depth) for c in range(CHANNELS[color])]
    if color == 3:
        index = s[0] % PALETTE_SIZE
        alpha = PALETTE_ALPHA[index] if index < len(PALETTE_ALPHA) else 255
        return (index * 40 & 255, index * 90 & 255, index * 150 & 255, alpha)
    gray = color in (0, 4)
    rgb = [scale(s[0 if gray else c], depth) for c in range(3)]
    if color in (4, 6):
        return tuple(rgb + [scale(s[-1], depth)])
    key = [sample(1, 1, c, depth) for c in range(len(s))]
    return tuple(rgb + [0 if keyed and s == key else 255])


def fnv1a32(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def c_array(name, data):
    lines = [f"static const Uint8 {name}[] = {{"]
    for i in range(0, len(data), 16):
        lines.append("\t" + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    return lines + ["};", ""]


def pixel(depth):
    return lambda x, y, c: sample(x, y, c, depth)


KEY2 = struct.pack(">H", sample(1, 1, 0, 2))
KEY16 = b"".join(struct.pack(">H", sample(1, 1, c, 16)) for c in range(3))

FIXTURES = [
    ("rgba8_dynamic", 6, 8, 32, 8, False, pixel(8), lambda r: r % 2,
     deflate(9), None, None, 1, 2),
    ("rgba8_fixed", 6, 8, 12, 8, False, pixel(8), lambda r: r % 5,
     deflate(9, zlib.Z_FIXED), None, None, 1, 1),
    ("gray2_key", 0, 2, 9, 3, False, pixel(2), lambda r: (1, 3, 4)[r],
     deflate(9), None, KEY2, 1, None),
    ("pal4_adam7", 3, 4, 10, 9, True, lambda x, y, c: sample(x, y, 0, 4) % PALETTE_SIZE,
     lambda r: r % 5, deflate(6), PALETTE, bytes(PALETTE_ALPHA), 3, None),
    ("ga16_stored", 4, 16, 3, 2, False, pixel(16), lambda r: (2, 4)[r],
     deflate(0), None, None, 1, 0),
    ("rgb16_paeth", 2, 16, 4, 3, False, pixel(16), lambda r: 4,
     deflate(9), None, KEY16, 1, None),
    ("gray1", 0, 1, 17, 2, False, pixel(1), lambda r: (0, 2)[r],
     deflate(9), None, None, 1, None),
    ("rgba8_adam7_2x1", 6, 8, 2, 1, True, pixel(8), lambda r: r % 5,
     deflate(9), None, None, 1, None),
]

lines = ["/*", " * Generated PNG decoder fixtures. Do not edit.",
         " * Regenerate with tools/png_fixtures.", " */", "#pragma once", ""]
table = []
for name, color, depth, w, h, interlaced, value, filters, compress, plte, trns, parts, block \
        in FIXTURES:
    png, first_block = make(color, depth, w, h, interlaced, value, filters, compress, plte, trns,
                            parts)
    if block is not None and first_block != block:
        sys.exit(f"{name}: first deflate block has type {first_block}, wanted {block}")
    keyed = trns is not None and color in (0, 2)
    pillow = Image.open(io.BytesIO(png)).convert("RGBA")
    rgba = bytearray()
    for y in range(h):
        for x in range(w):
            want = expected_rgba(color, depth, keyed, x, y)
            got = pillow.getpixel((x, y))
            if got[:3] != want[:3] or (not keyed and got[3] != want[3]):
                sys.exit(f"{name}: Pillow decodes {got}, expected {want} at {x},{y}")
            rgba += bytes(want)
    lines += c_array(f"qa_png_{name}", png)
    table.append(f"\t{{ \"{name}\", qa_png_{name}, sizeof(qa_png_{name}), {w}, {h}, "
                 f"{'true' if color == 3 else 'false'}, 0x{fnv1a32(rgba):08x}u }},")

lines += ["static const QaPngFixture qa_png_fixtures[] = {"] + table + ["};", ""]
bad, _ = make(3, 8, 4, 2, False, lambda x, y, c: x + y, lambda r: 0, deflate(9), PALETTE[:12])
lines += c_array("qa_png_pal8_out_of_range", bad)[:-1]
open(OUT, "w", newline="\n").write("\n".join(lines) + "\n")
print(f"wrote {OUT}")
