"""Minimal PNG writer for the data dumper. Standard library only.

Two pixel formats are enough for every asset in the game: 8-bit indexed with an
optional per-index alpha table, and straight RGBA for sprites whose transparency
is positional rather than a palette entry.
"""

import struct
import zlib


def _chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))


def _write(path, ihdr, extra, raw, level=6):
    out = [b"\x89PNG\r\n\x1a\n", _chunk(b"IHDR", ihdr)]
    out.extend(extra)
    out.append(_chunk(b"IDAT", zlib.compress(raw, level)))
    out.append(_chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(b"".join(out))


def write_indexed(path, width, height, pixels, palette, alpha=None):
    """Write an 8-bit indexed PNG.

    `pixels` is width * height palette indices, `palette` a list of (r, g, b),
    and `alpha` an optional list of per-index alpha values.
    """
    if len(pixels) != width * height:
        raise ValueError("pixel buffer is %d bytes, expected %d" % (len(pixels), width * height))

    plte = bytearray()
    for r, g, b in palette:
        plte += bytes((r & 0xff, g & 0xff, b & 0xff))

    extra = [_chunk(b"PLTE", bytes(plte))]
    if alpha is not None:
        extra.append(_chunk(b"tRNS", bytes(bytearray(alpha))))

    raw = bytearray()
    src = bytes(bytearray(pixels))
    for y in range(height):
        raw.append(0)  # filter type 0 (None)
        raw += src[y * width:(y + 1) * width]

    _write(path, struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0), extra, bytes(raw))


def write_rgba(path, width, height, pixels):
    """Write an 8-bit-per-channel RGBA PNG. `pixels` is width * height * 4 bytes."""
    if len(pixels) != width * height * 4:
        raise ValueError("pixel buffer is %d bytes, expected %d" % (len(pixels), width * height * 4))

    raw = bytearray()
    src = bytes(bytearray(pixels))
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += src[y * stride:(y + 1) * stride]

    _write(path, struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0), [], bytes(raw))


def indexed_to_rgba(indices, palette, transparent=None):
    """Expand palette indices (None or `transparent` meaning clear) into RGBA bytes."""
    out = bytearray()
    for i in indices:
        if i is None or i == transparent:
            out += b"\x00\x00\x00\x00"
        else:
            r, g, b = palette[i]
            out += bytes((r, g, b, 255))
    return bytes(out)
