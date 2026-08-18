"""Readers for the container formats shipped in the Tyrian data directory.

Every reader mirrors the loader the game uses at runtime. The docstring on each
one names that loader so the two can be compared when a format question comes
up. All multi-byte fields are little-endian on disk unless a reader says
otherwise.
"""

import os
import struct

# Table sizes from src/lvlmast.h. Weapon and enemy tables have two banks separated by a gap.

WEAP_END1 = 818
WEAP_START2 = 1000
WEAP_NUM = 1818
PORT_NUM = 60
POWER_NUM = 6
OPTION_NUM = 37
SHIP_NUM = 18
SHIELD_NUM = 11
SPECIAL_NUM = 54
ENEMY_END1 = 850
ENEMY_START2 = 1001
ENEMY_NUM = 1850

# Enemy shapebank (1-based) to the newsh<c>.shp that holds its frames (src/lvlmast.c).
SHAPE_FILE = ['2', '4', '7', '8', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
              'O', 'P', 'Q', 'R', 'S', 'T', 'U', '5', '#', 'V', '0', '@', '3', '^', '5', '9', "'", '%']

# Palette index each tyrian.pic screen is drawn with (src/pcxmast.c).
PCX_PALETTE = [0, 7, 5, 8, 10, 5, 18, 19, 19, 20, 21, 22, 5, 23]
PCX_NUM = 14

# Palette index each shop/menu face uses (src/pcxmast.c, facepal).
FACE_PALETTE = [1, 2, 3, 4, 6, 9, 11, 12, 16, 13, 14, 15]

TILE_W, TILE_H = 24, 28
TILES_PER_SET = 600

SFX_COUNT = 31
VOICE_COUNT = 9
SAMPLE_RATE = 11025  # signed 8-bit mono (src/nortsong.c builds the converter from this)

# Layout of the map layers inside a level record (src/tyrian2.c, JE_loadMap).
MAP_LAYERS = ((14, 300), (14, 600), (15, 600))


# Byte-level helpers


class Reader(object):
    """Cursor over an in-memory blob with the primitive reads the formats use."""

    def __init__(self, data, pos=0):
        self.data = data
        self.pos = pos

    def eof(self):
        return self.pos >= len(self.data)

    def remaining(self):
        return len(self.data) - self.pos

    def bytes(self, n):
        if self.pos + n > len(self.data):
            raise EOFError("read past end of data at %d (+%d)" % (self.pos, n))
        out = self.data[self.pos:self.pos + n]
        self.pos += n
        return out

    def seek(self, pos):
        self.pos = pos

    def skip(self, n):
        self.pos += n

    def unpack(self, fmt):
        fmt = "<" + fmt
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.bytes(size))

    def u8(self):
        return self.bytes(1)[0]

    def s8(self):
        return self.unpack("b")[0]

    def u16(self):
        return self.unpack("H")[0]

    def s16(self):
        return self.unpack("h")[0]

    def u32(self):
        return self.unpack("I")[0]

    def s32(self):
        return self.unpack("i")[0]

    def u16_be(self):
        return struct.unpack(">H", self.bytes(2))[0]

    def array(self, fmt, count):
        return list(self.unpack("%d%s" % (count, fmt)))


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


# Encrypted Pascal strings (src/helptext.c, decrypt_string)

CRYPT_KEY = (204, 129, 63, 255, 71, 19, 25, 62, 1, 99)


def decrypt_string(buf):
    """Undo the per-byte XOR chain the text records are stored with."""
    out = bytearray(buf)
    for i in range(len(out) - 1, -1, -1):
        out[i] ^= CRYPT_KEY[i % len(CRYPT_KEY)]
        if i > 0:
            out[i] ^= buf[i - 1]
    return bytes(out)


def read_encrypted_string(r):
    """One length-prefixed encrypted record, decoded as CP437."""
    length = r.u8()
    raw = r.bytes(length)
    return decrypt_string(raw).decode("cp437")


def read_script_lines(data):
    """Every encrypted record in a levels?.dat / cubetxt?.dat / tyrian.cdt blob."""
    r = Reader(data)
    lines = []
    while not r.eof():
        try:
            lines.append(read_encrypted_string(r))
        except EOFError:
            break
    return lines


# palette.dat (src/palette.c, JE_loadPals)


def load_palettes(path):
    """24 palettes of 256 colours. Returns (expanded_8bit, raw_6bit)."""
    data = read_file(path)
    count = len(data) // (256 * 3)
    expanded, raw = [], []
    pos = 0
    for _ in range(count):
        pal8, pal6 = [], []
        for _ in range(256):
            r, g, b = data[pos], data[pos + 1], data[pos + 2]
            pos += 3
            pal6.append((r, g, b))
            # VGA 6-bit components widened to 8 bits, keeping both endpoints.
            pal8.append((((r << 2) | (r >> 4)) & 0xff,
                         ((g << 2) | (g >> 4)) & 0xff,
                         ((b << 2) | (b >> 4)) & 0xff))
        expanded.append(pal8)
        raw.append(pal6)
    return expanded, raw


# Sprite_array .shp files (src/sprite.c, load_sprites)
#
# u16 sprite count, then per sprite a "populated" byte and, when set, u16
# width/height/size followed by `size` bytes of run-length data:
#   255 = transparent run whose length is the next byte
#   254 = advance to the next pixel row
#   253 = one transparent pixel
#   else = one opaque pixel of that palette index


def decode_sprite(width, height, data):
    """Run-length sprite to a width * height list of palette indices (None = clear)."""
    out = [None] * (width * height)
    limit = len(out)
    pos = 0        # linear cursor, row-major over `width`
    x_offset = 0
    i = 0
    while i < len(data):
        b = data[i]
        if b == 255:
            i += 1
            if i >= len(data):
                break
            pos += data[i]
            x_offset += data[i]
        elif b == 254:
            pos += width - x_offset
            x_offset = width
        elif b == 253:
            pos += 1
            x_offset += 1
        else:
            if 0 <= pos < limit:
                out[pos] = b
            pos += 1
            x_offset += 1
        if x_offset >= width:
            pos += width - x_offset
            x_offset = 0
        i += 1
    return out


def load_sprite_array(data, offset=0):
    """One Sprite_array bank. Returns a list of dicts; empty slots keep width 0."""
    r = Reader(data, offset)
    count = r.u16()
    sprites = []
    for index in range(count):
        populated = r.u8()
        if not populated:
            sprites.append({"index": index, "width": 0, "height": 0, "size": 0, "data": b""})
            continue
        width, height, size = r.u16(), r.u16(), r.u16()
        sprites.append({"index": index, "width": width, "height": height,
                        "size": size, "data": r.bytes(size)})
    return sprites


# Compiled 12px sheets, "Sprite2" (src/sprite.c, blit_sprite2)
#
# A table of u16 byte offsets, then the frames. Frame N (1-based) starts at
# offsets[N - 1] and runs to a 0x0f terminator. Each control byte packs a
# transparent count in the low nibble and an opaque run length in the high
# nibble; a zero run length means "next pixel row".

SPRITE2_WIDTH = 12


def sprite2_offsets(data):
    if len(data) < 2:
        return []
    first = struct.unpack_from("<H", data, 0)[0]
    count = first // 2
    if count == 0 or count * 2 > len(data):
        return []
    return list(struct.unpack_from("<%dH" % count, data, 0))


def decode_sprite2(data, offset):
    """One compiled frame. Returns (height, list of 12 * height indices or None)."""
    pixels = {}
    row, col = 0, 0
    max_row = 0
    i = offset
    while i < len(data) and data[i] != 0x0f:
        b = data[i]
        col += b & 0x0f
        run = (b >> 4) & 0x0f
        if run == 0:
            row += 1
            col -= SPRITE2_WIDTH
            i += 1
            continue
        for _ in range(run):
            i += 1
            if i >= len(data):
                break
            if 0 <= col < SPRITE2_WIDTH and row >= 0:
                pixels[(col, row)] = data[i]
                if row > max_row:
                    max_row = row
            col += 1
        i += 1

    height = max(max_row + 1, 1)
    out = [None] * (SPRITE2_WIDTH * height)
    for (x, y), value in pixels.items():
        if y < height:
            out[y * SPRITE2_WIDTH + x] = value
    return height, out


def load_sprite2_sheet(data):
    """Every frame in a compiled sheet, in file order."""
    offsets = sprite2_offsets(data)
    frames = []
    for n, off in enumerate(offsets, start=1):
        if off >= len(data):
            frames.append({"index": n, "offset": off, "height": 0, "pixels": []})
            continue
        height, pixels = decode_sprite2(data, off)
        frames.append({"index": n, "offset": off, "height": height, "pixels": pixels})
    return frames


# user1.shp / user2.shp
#
# Neither file is a Sprite_array despite the extension: after a two-byte header
# they hold uncompressed 12x14 cells, one byte per pixel, the same cell size the
# compiled sheets use. The game never reads them; the DOS ship editor wrote them.

RAW_CELL_W, RAW_CELL_H = 12, 14


def load_raw_cell_sheet(path, offset=2):
    """Returns (cells, header, trailing bytes that do not fill a cell)."""
    data = read_file(path)
    body = data[offset:]
    size = RAW_CELL_W * RAW_CELL_H
    count = len(body) // size
    cells = [body[i * size:(i + 1) * size] for i in range(count)]
    return cells, data[:offset], body[count * size:]


# tyrian.shp / tyrianc.shp (src/sprite.c, JE_loadMainShapeTables)
#
# u16 bank count (13), s32 bank offsets, then the banks. Banks 0-6 are
# Sprite_array tables; banks 7-12 are compiled 12px sheets.

MAIN_SHP_BANKS = [
    ("00_font", "array", "Large font"),
    ("01_small_font", "array", "Small font"),
    ("02_tiny_font", "array", "Tiny font"),
    ("03_planets", "array", "Planet and map sprites"),
    ("04_faces", "array", "Cutscene and datacube faces"),
    ("05_options_help", "array", "Menu, option and help sprites"),
    ("06_weapons", "array", "Weapon and shop item sprites"),
    ("07_player_shots", "sheet", "Player shot sprites"),
    ("08_player_ships", "sheet", "Player ship and sidekick sprites"),
    ("09_powerups", "sheet", "Power-up sprites"),
    ("10_pickups", "sheet", "Coins, datacubes and other pickups"),
    ("11_player_shots_2", "sheet", "More player shot sprites"),
    ("12_t2000_ships", "sheet", "Tyrian 2000 ship sprites"),
]


def load_main_shp(path):
    """Split a main shape file into its banks. Returns a list of (spec, blob)."""
    data = read_file(path)
    r = Reader(data)
    count = r.u16()
    offsets = r.array("i", count)
    offsets.append(len(data))

    banks = []
    for i in range(count):
        spec = MAIN_SHP_BANKS[i] if i < len(MAIN_SHP_BANKS) else ("%02d_unknown" % i, "sheet", "")
        banks.append((spec, data[offsets[i]:offsets[i + 1]]))
    return banks


# shapes?.dat tilesets (src/tyrian2.c, JE_loadMap)
#
# 600 records: a "blank" byte and, when it is zero, a 24x28 tile. Palette index
# 0 is transparent in every background layer (src/backgrnd.c).


def load_tileset(path):
    data = read_file(path)
    r = Reader(data)
    tiles = []
    for index in range(TILES_PER_SET):
        if r.eof():
            break
        blank = r.u8()
        if blank:
            tiles.append(None)
            continue
        try:
            tiles.append(r.bytes(TILE_W * TILE_H))
        except EOFError:
            break
    return tiles


# tyrian.pic and stand-alone PCX files use the same PCX run-length encoding.
# See src/picload.c and src/pcxload.c.


def decode_pcx_rle(data, width=320, height=200):
    out = bytearray(width * height)
    i, n = 0, 0
    limit = width * height
    while n < limit and i < len(data):
        p = data[i]
        i += 1
        if (p & 0xc0) == 0xc0:
            run = p & 0x3f
            if i >= len(data):
                break
            value = data[i]
            i += 1
            run = min(run, limit - n)
            for k in range(run):
                out[n + k] = value
            n += run
        else:
            out[n] = p
            n += 1
    return bytes(out)


def load_pic(path):
    """The 14 full-screen backdrops. Returns a list of 320x200 index buffers."""
    data = read_file(path)
    r = Reader(data)
    r.u16()  # stored count; the game trusts PCX_NUM instead
    offsets = r.array("i", PCX_NUM)
    offsets.append(len(data))
    return [decode_pcx_rle(data[offsets[i]:offsets[i + 1]]) for i in range(PCX_NUM)]


def load_pcx(path):
    """A stand-alone 320x200 PCX. Returns (pixels, palette) with 8-bit colours."""
    data = read_file(path)
    palette = None
    if len(data) >= 769 and data[-769] == 12:
        tail = data[-768:]
        palette = [(tail[i * 3], tail[i * 3 + 1], tail[i * 3 + 2]) for i in range(256)]
    body = data[128:-769] if palette is not None else data[128:]
    return decode_pcx_rle(body), palette


# tyrian.snd / voices.snd / voicesc.snd (src/nortsong.c, loadSndFile)
#
# u16 sample count, u32 offsets, then signed 8-bit mono at 11025 Hz. Voice
# banks carry 100 bytes of junk at the end of each sample.


def load_sound_bank(path, expected_count=None, trim_tail=0):
    data = read_file(path)
    r = Reader(data)
    count = r.u16()
    if expected_count is not None and count != expected_count:
        raise ValueError("%s holds %d samples, expected %d" % (path, count, expected_count))
    offsets = r.array("I", count)
    offsets.append(len(data))

    samples = []
    for i in range(count):
        blob = data[offsets[i]:offsets[i + 1]]
        if trim_tail and len(blob) > trim_tail:
            blob = blob[:-trim_tail]
        samples.append(blob)
    return samples


def wav_from_signed8(samples, rate=SAMPLE_RATE):
    """Wrap signed 8-bit mono PCM in a WAV container (WAV stores 8-bit unsigned)."""
    body = bytes(bytearray((b + 128) & 0xff for b in samples))
    header = b"RIFF" + struct.pack("<I", 36 + len(body)) + b"WAVEfmt "
    header += struct.pack("<IHHIIHH", 16, 1, 1, rate, rate, 1, 8)
    header += b"data" + struct.pack("<I", len(body))
    return header + body


# music.mus and its LDS songs (src/loudness.c and src/lds_play.c)


def load_music_bank(path):
    data = read_file(path)
    r = Reader(data)
    count = r.u16()
    offsets = r.array("I", count)
    offsets.append(len(data))
    return [data[offsets[i]:offsets[i + 1]] for i in range(count)]


def parse_lds(blob):
    """Header, instrument bank, order list and packed pattern words of one song."""
    r = Reader(blob)
    song = {
        "mode": r.u8(),
        "speed": r.u16(),
        "tempo": r.u8(),
        "patternLength": r.u8(),
        "channelDelay": [r.u8() for _ in range(9)],
        "regBD": r.u8(),
    }

    patch_count = r.u16()
    patches = []
    for _ in range(patch_count):
        p = {}
        for name in ("modMisc", "modVol", "modAD", "modSR", "modWave",
                     "carMisc", "carVol", "carAD", "carSR", "carWave",
                     "feedback", "keyOff", "portamento", "glide", "fineTune",
                     "vibrato", "vibDelay", "modTrem", "carTrem", "tremWait", "arpeggio"):
            p[name] = r.u8()
        p["arpTable"] = [r.u8() for _ in range(12)]
        p["start"] = r.u16()
        p["size"] = r.u16()
        p["fms"] = r.u8()
        p["transpose"] = r.u16()
        p["midiInstrument"] = r.u8()
        p["midiVelocity"] = r.u8()
        p["midiKey"] = r.u8()
        p["midiTranspose"] = r.u8()
        p["midiUnused1"] = r.u8()
        p["midiUnused2"] = r.u8()
        patches.append(p)
    song["instruments"] = patches

    position_count = r.u16()
    positions = []
    for _ in range(position_count):
        row = []
        for _ in range(9):
            pattern = r.u16() // 2   # byte offset into the pattern space, words
            row.append({"pattern": pattern, "transpose": r.u8()})
        positions.append(row)
    song["orders"] = positions

    r.skip(2)  # digital sound count, unused by the player
    words = r.remaining() // 2
    song["patternWords"] = list(struct.unpack_from("<%dH" % words, blob, r.pos)) if words else []
    return song


# tyrend.anm, a DeluxePaint Animation "LPF" file (src/animlib.c)

ANM_PALETTE_OFFSET = 0x100
ANM_PAGEHEADER_OFFSET = 0x500
ANM_OFFSET = 0x0B00
ANM_PAGE_SIZE = 0x10000


def load_anm(path):
    """Header, palette and page table of an LPF animation."""
    data = read_file(path)
    if data[:4] != b"LPF ":
        raise ValueError("%s is not an LPF animation" % path)

    nlps = struct.unpack_from("<H", data, 6)[0]
    n_records = struct.unpack_from("<I", data, 8)[0]

    palette = []
    for i in range(256):
        b, g, r, _ = data[ANM_PALETTE_OFFSET + i * 4:ANM_PALETTE_OFFSET + i * 4 + 4]
        palette.append((r, g, b))

    pages = []
    for i in range(nlps):
        base = ANM_PAGEHEADER_OFFSET + i * 6
        base_record, records, nbytes = struct.unpack_from("<HHH", data, base)
        pages.append({"baseRecord": base_record, "records": records, "bytes": nbytes})

    return {"data": data, "pages": pages, "palette": palette,
            "pageCount": nlps, "recordCount": n_records}


def anm_page_records(anm, page_index):
    """Split one 64 KiB page into its per-frame compressed records."""
    data = anm["data"]
    pos = ANM_OFFSET + page_index * ANM_PAGE_SIZE
    base_record, records, nbytes = struct.unpack_from("<HHH", data, pos)
    pos += 6 + 2  # the page repeats its header, then two padding bytes
    sizes = list(struct.unpack_from("<%dH" % records, data, pos))
    pos += records * 2
    body = data[pos:pos + nbytes]

    out, at = [], 0
    for size in sizes:
        out.append(body[at:at + size])
        at += size
    return base_record, out


def anm_apply_frame(screen, record):
    """Apply one RunSkipDump record to the 320x200 frame buffer, in place.

    Frames are deltas: a skip leaves the previous frame's pixels alone. Mirrors
    JE_playRunSkipDump, including its 4-byte record prefix.
    """
    data = record[4:]
    i, cursor = 0, 0
    limit = len(screen)
    while i < len(data):
        opcode = data[i]
        i += 1
        if opcode == 0x80:  # long operation
            if i + 2 > len(data):
                break
            opcode = struct.unpack_from("<H", data, i)[0]
            i += 2
            if opcode == 0:
                break
            if not (opcode & 0x8000):
                cursor += opcode
            elif opcode & 0x4000:
                count = opcode & 0x3fff
                value = data[i]
                i += 1
                end = min(cursor + count, limit)
                for k in range(cursor, end):
                    screen[k] = value
                cursor += count
            else:
                count = opcode & 0x3fff
                end = min(cursor + count, limit)
                screen[cursor:end] = data[i:i + (end - cursor)]
                i += count
                cursor += count
        elif opcode & 0x80:  # short skip
            cursor += opcode & 0x7f
        elif opcode == 0:    # short run
            count = data[i]
            value = data[i + 1]
            i += 2
            end = min(cursor + count, limit)
            for k in range(cursor, end):
                screen[k] = value
            cursor += count
        else:                # short copy
            count = opcode
            end = min(cursor + count, limit)
            screen[cursor:end] = data[i:i + (end - cursor)]
            i += count
            cursor += count
    return screen


# Item and enemy tables (src/episodes.c, JE_loadItemDat)
#
# Episodes 1-3 keep them in tyrian.hdt at the offset its first int32 names.
# Episodes 4 and 5 keep their own copy at the end of the episode's .lvl file.


def _name(r):
    length = r.u8()
    raw = r.bytes(30)
    return raw[:min(length, 30)].decode("cp437")


def _read_weapon(r):
    w = {
        "drain": r.u16(),
        "shotRepeat": r.u8(),
        "multi": r.u8(),
        "weaponAnimation": r.u16(),
        "max": r.u8(),
        "tx": r.u8(),
        "ty": r.u8(),
        "aim": r.u8(),
        "attack": r.array("B", 8),
        "del": r.array("B", 8),
        "sx": r.array("b", 8),
        "sy": r.array("b", 8),
        "bx": r.array("b", 8),
        "by": r.array("b", 8),
        "sg": r.array("H", 8),
        "acceleration": r.s8(),
        "accelerationX": r.s8(),
        "circleSize": r.u8(),
        "sound": r.u8(),
        "trail": r.u8(),
        "shipBlastFilter": r.u8(),
    }
    return w


def _read_enemy(r):
    return {
        "animation": r.u8(),
        "turret": r.array("B", 3),
        "fireFrequency": r.array("B", 3),
        "xMove": r.s8(),
        "yMove": r.s8(),
        "xAccel": r.s8(),
        "yAccel": r.s8(),
        "xCAccel": r.s8(),
        "yCAccel": r.s8(),
        "startX": r.s16(),
        "startY": r.s16(),
        "startXC": r.s8(),
        "startYC": r.s8(),
        "armor": r.u8(),
        "size": r.u8(),
        "graphic": r.array("H", 20),
        "explosionType": r.u8(),
        "animate": r.u8(),
        "shapeBank": r.u8(),
        "xRev": r.s8(),
        "yRev": r.s8(),
        "deathGraphic": r.u16(),
        "deathLevel": r.s8(),
        "deathAnimation": r.s8(),
        "launchFrequency": r.u8(),
        "launchType": r.u16(),
        "value": r.s16(),
        "enemyDie": r.u16(),
    }


def load_item_data(data, offset):
    """Every item and enemy table stored at `offset`."""
    r = Reader(data, offset)
    header = r.array("H", 7)

    weapons = {}
    for lo, hi in ((0, WEAP_END1), (WEAP_START2, WEAP_NUM)):
        for i in range(lo, hi + 1):
            weapons[i] = _read_weapon(r)

    ports = []
    for _ in range(PORT_NUM + 1):
        ports.append({
            "name": _name(r),
            "opnum": r.u8(),
            "op": [r.array("H", 11), r.array("H", 11)],
            "cost": r.u16(),
            "itemGraphic": r.u16(),
            "powerUse": r.u16(),
        })

    specials = []
    for _ in range(SPECIAL_NUM + 1):
        specials.append({
            "name": _name(r),
            "itemGraphic": r.u16(),
            "power": r.u8(),
            "type": r.u8(),
            "weapon": r.u16(),
        })

    generators = []
    for _ in range(POWER_NUM + 1):
        generators.append({
            "name": _name(r),
            "itemGraphic": r.u16(),
            "power": r.u8(),
            "speed": r.s8(),
            "cost": r.u16(),
        })

    ships = []
    for _ in range(SHIP_NUM + 1):
        ships.append({
            "name": _name(r),
            "shipGraphic": r.u16(),
            "itemGraphic": r.u16(),
            "animation": r.u8(),
            "speed": r.s8(),
            "damageFactor": r.u8(),
            "cost": r.u16(),
            "bigShipGraphic": r.u8(),
        })

    sidekicks = []
    for _ in range(OPTION_NUM + 1):
        sidekicks.append({
            "name": _name(r),
            "power": r.u8(),
            "itemGraphic": r.u16(),
            "cost": r.u16(),
            "tr": r.u8(),
            "option": r.u8(),
            "optionSpeed": r.s8(),
            "animation": r.u8(),
            "graphic": r.array("H", 20),
            "weaponPort": r.u8(),
            "weaponNumber": r.u16(),
            "ammo": r.u8(),
            "stop": bool(r.u8()),
            "iconGraphic": r.u8(),
        })

    shields = []
    for _ in range(SHIELD_NUM + 1):
        shields.append({
            "name": _name(r),
            "totalPower": r.u8(),
            "maxPower": r.u8(),
            "itemGraphic": r.u16(),
            "cost": r.u16(),
        })

    enemies = {}
    for lo, hi in ((0, ENEMY_END1), (ENEMY_START2, ENEMY_NUM)):
        for i in range(lo, hi + 1):
            enemies[i] = _read_enemy(r)

    return {
        "header": header,
        "weapons": weapons,
        "weaponPorts": ports,
        "specials": specials,
        "generators": generators,
        "ships": ships,
        "sidekicks": sidekicks,
        "shields": shields,
        "enemies": enemies,
        "endOffset": r.pos,
    }


# tyrian.hdt text blocks (src/helptext.c, JE_loadHelpText)
#
# Groups of encrypted records, each wrapped in a label record the game skips.
# The counts below are the ones the game reads; a mismatch desynchronises the
# whole file, so the dumper checks the final offset against the item-data one.

HDT_GROUPS = [
    ("helpText", 39),
    ("planetNames", 21),
    ("miscText", 72),
    ("miscTextB", 8),
    ("keyNames", 11),
    ("menuText", 7),
    ("eventText", 9),
    ("helpTopics", 6),
    ("mainMenuHelp", 37),
    ("menu1_main", 7),
    ("menu2_items", 9),
    ("menu3_options", 9),
    ("inGameMenu", 6),
    ("detailLevel", 6),
    ("gameSpeed", 5),
    ("episodeNames", 6),
    ("difficultyNames", 7),
    ("gameplayNames", 6),
    ("menu10_twoPlayerMain", 6),
    ("inputDevices", 3),
    ("networkText", 5),
    ("menu11_twoPlayerNetwork", 4),
    ("highScoreDifficultyNames", 11),
    ("menu12_networkOptions", 7),
    ("menu13_joystick", 7),
    ("joystickButtonNames", 5),
    ("superShips", 13),
    ("specialNames", 11),
    ("destructHelp", 25),
    ("destructWeapons", 17),
    ("destructModes", 5),
    ("shipInfo", 40),
    ("menu14_superTyrian", 5),
    ("timedBattlePlanets", 4),
    ("setupMenu0", 10),
    ("setupMenu1", 5),
    ("setupMenu2", 4),
    ("setupMenu3", 4),
    ("setupMenu4", 5),
    ("setupMenu5", 7),
    ("setupMenu6", 7),
    ("setupMenu7", 21),
    ("setupMenu8", 3),
    ("setupMenu9", 3),
    ("menu15_mouseSettings", 6),
    ("licensingInfo", 3),
    ("defaultHighScoreNames", 39),
    ("defaultTeamNames", 10),
    ("orderingInfo", 6),
    ("superTyrianText", 6),
]


def load_hdt_text(path):
    """Every text group in tyrian.hdt, plus the item-data offset it starts with."""
    data = read_file(path)
    r = Reader(data)
    item_offset = r.s32()

    groups = []
    for index, (name, count) in enumerate(HDT_GROUPS):
        try:
            label = read_encrypted_string(r)
            entries = [read_encrypted_string(r) for _ in range(count)]
        except EOFError:
            groups.append({"name": name, "label": "", "entries": [], "truncated": True})
            break
        # Every group but the last is followed by a closing label record.
        end = ""
        if index < len(HDT_GROUPS) - 1:
            try:
                end = read_encrypted_string(r)
            except EOFError:
                pass
        groups.append({"name": name, "label": label, "endLabel": end, "entries": entries})

    return {"itemDataOffset": item_offset, "textEndOffset": r.pos, "groups": groups}


# tyrian?.lvl level files (src/lvllib.c, JE_analyzeLevel; src/tyrian2.c, JE_loadMap)
#
# u16 offset count, s32 offsets. Each playable level owns two offsets and the
# last offset marks the end. Episodes 4 and 5 keep item data at the final one.

EVENT_FIELDS = ("time", "type", "data", "data2", "data3", "data5", "data6", "data4")


def load_level_index(path):
    data = read_file(path)
    r = Reader(data)
    count = r.u16()
    offsets = r.array("i", count)
    offsets.append(len(data))
    return data, count, offsets


def load_level(data, offset):
    """One level record: header, enemy pool, event script, tile lookup and maps."""
    r = Reader(data, offset)
    map_char = chr(r.u8())
    shape_char = chr(r.u8())
    level = {
        "mapFileChar": map_char,
        "shapeFileChar": shape_char,
        "shapeFile": "shapes%s.dat" % shape_char.lower(),
        "mapX": r.u16(),
        "mapX2": r.u16(),
        "mapX3": r.u16(),
    }

    enemy_count = r.u16()
    level["randomEnemies"] = r.array("H", enemy_count)

    event_count = r.u16()
    events = []
    for _ in range(event_count):
        events.append({
            "time": r.u16(),
            "type": r.u8(),
            "data": r.s16(),
            "data2": r.s16(),
            "data3": r.s8(),
            "data5": r.s8(),
            "data6": r.s8(),
            "data4": r.u8(),
        })
    level["events"] = events

    # The tile lookup is the one big-endian field in the format: the game reads
    # it little-endian and then swaps every entry (src/tyrian2.c, JE_loadMap).
    level["tileLookup"] = [[r.u16_be() for _ in range(128)] for _ in range(3)]

    maps = []
    for width, height in MAP_LAYERS:
        maps.append([list(r.bytes(width)) for _ in range(height)])
    level["maps"] = maps
    level["endOffset"] = r.pos
    return level


# demo.1 .. demo.5 (src/mainint.c, load_next_demo / replay_demo_keys)

DEMO_KEY_BITS = ("up", "down", "left", "right", "fire", "changeFire",
                 "leftSidekick", "rightSidekick")


def load_demo(path):
    data = read_file(path)
    r = Reader(data)
    demo = {
        "episode": r.u8(),
        "levelName": r.bytes(10).decode("cp437").rstrip("\x00 "),
        "levelFileNumber": r.u8(),
        "frontWeapon": r.u8(),
        "rearWeapon": r.u8(),
        "superArcadeMode": r.u8(),
        "leftSidekick": r.u8(),
        "rightSidekick": r.u8(),
        "generator": r.u8(),
        "sidekickLevel": r.u8(),
        "sidekickSeries": r.u8(),
        "initialEpisode": r.u8(),
        "shield": r.u8(),
        "special": r.u8(),
        "ship": r.u8(),
        "frontWeaponPower": r.u8(),
        "rearWeaponPower": r.u8(),
        "unused": list(r.bytes(3)),
        "levelSong": r.u8(),
    }

    # Input stream: a key bitmask followed by a big-endian 16-bit hold count.
    frames = []
    first = r.bytes(2)
    frames.append({"keys": 0, "hold": (first[0] << 8) | first[1]})
    while r.remaining() >= 3:
        keys = r.u8()
        hold = r.bytes(2)
        frames.append({"keys": keys, "hold": (hold[0] << 8) | hold[1],
                       "pressed": [n for i, n in enumerate(DEMO_KEY_BITS) if keys & (1 << i)]})
    demo["input"] = frames
    return demo


# exitmsg.bin: an 80x25 DOS text-mode screen (character, attribute pairs).


def load_text_screen(path, width=80, height=25):
    data = read_file(path)
    rows = []
    for y in range(height):
        chars, attrs = [], []
        for x in range(width):
            at = (y * width + x) * 2
            if at + 1 >= len(data):
                break
            chars.append(bytes(data[at:at + 1]).decode("cp437"))
            attrs.append(data[at + 1])
        rows.append({"text": "".join(chars), "attributes": attrs})
    return rows


# Small conveniences shared by the writers.


def name_list_from_source(src_path, array_name):
    """Pull a C string-array initialiser out of src/ so name tables stay in sync."""
    if not os.path.exists(src_path):
        return []
    with open(src_path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    at = text.find(array_name)
    if at < 0:
        return []
    start = text.find("{", at)
    end = text.find("};", start)
    if start < 0 or end < 0:
        return []

    names, i, body = [], 0, text[start + 1:end]
    while i < len(body):
        c = body[i]
        if c == '"':
            j = i + 1
            out = []
            while j < len(body) and body[j] != '"':
                if body[j] == "\\":
                    j += 1
                out.append(body[j])
                j += 1
            names.append("".join(out))
            i = j + 1
        elif c == "/" and i + 1 < len(body) and body[i + 1] == "*":
            i = body.find("*/", i) + 2
        elif c == "/" and i + 1 < len(body) and body[i + 1] == "/":
            i = body.find("\n", i) + 1
        else:
            i += 1
    return names
