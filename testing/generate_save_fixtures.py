#!/usr/bin/env python3
"""Generate the independent Endless v3-v27 migration corpus."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


PERKS_OLD = 16
PERKS_NEW = 32
OFFERS_OLD = 3
OFFERS_NEW = 5
COURSES = 5
RECENT = 5
CASH_SLOTS = 12


def u8(value: int) -> bytes:
    return struct.pack("<B", value)


def i32(value: int) -> bytes:
    return struct.pack("<i", value)


def u32(value: int) -> bytes:
    return struct.pack("<I", value)


def u64(value: int) -> bytes:
    return struct.pack("<Q", value)


def fixed(text: str, width: int) -> bytes:
    raw = text.encode("ascii")
    assert len(raw) < width
    return raw + bytes(width - len(raw))


def record(version: int) -> bytes:
    out = bytearray()
    out += u8(1)  # used

    choice_count = 3 if version < 14 else 2
    common_i32 = [
        42, 7, 1234, 12, 3, 2, 5, 4, 9, 6,
        111, 222, 333, 444, 555, 666,
        1, 2, choice_count, 1, 1,
    ]
    out += b"".join(i32(value) for value in common_i32)
    out += u32(0x1234)
    out += bytes([1, 1, 0, 1, 1, 0])  # revive, rigged, won, pending, lastSec, forced

    perk_count = PERKS_NEW if version >= 11 else PERKS_OLD
    perks = bytearray(perk_count)
    if version < 14:
        perks[10] = 2  # Rapid Recharge before merge
        perks[14] = 3  # removed Rapid Charger
        perks[15] = 4  # shifts down to current slot 14
    else:
        perks[10] = 5
        perks[14] = 4
    out += perks
    out += fixed("fixture gamble", 48)
    out += fixed("fixture special", 31)

    offer_count = OFFERS_NEW if version >= 13 else OFFERS_OLD
    offers = [14, 15, 2] if version < 14 else [14, 2]
    offers += [0] * (offer_count - len(offers))
    out += b"".join(i32(value) for value in offers)

    out += b"".join(i32(value) for value in [1, 0, 0, 0, 0])
    course_mod = 0x100001234 if version >= 7 else 0x1234
    encoder = u64 if version >= 7 else u32
    out += b"".join(encoder(course_mod if index == 0 else 0) for index in range(COURSES))
    out += bytes([1, 0, 0, 0, 0])
    if version >= 8:
        out += bytes([1, 0, 0, 0, 0])

    out += bytes(range(90))
    out += bytes([3] * 9)
    out += fixed(f"fixture-v{version:02d}", 24)

    if version >= 4:
        out += u8(1)
        out += (u64(0x100005141) if version >= 7 else u32(0x5141))
        out += u8(1) + i32(1) + u8(1)
    if version >= 5:
        out += i32(77)
    if version >= 6:
        out += u8(2)
        out += b"".join(i32(value) for value in [1, 2, 0, 0, 0])
        out += bytes([1, 2, 0, 0, 0])
    if version >= 9:
        out += u8(1)
    if version >= 10:
        out += u8(4) + i32(41)
    if version >= 12:
        out += bytes([1, 2])
    if version >= 15:
        out += u8(1)  # Standard
    if version >= 16:
        out += u64(10000)
        if version >= 17:
            out += u64(2000)
            out += b"".join(u64((index + 1) * 10) for index in range(CASH_SLOTS))
    if version == 18:
        out += u64(900)
    elif version >= 19:
        out += b"".join(u64((index + 1) * 20) for index in range(CASH_SLOTS))
    if version >= 20:
        out += u8(1)
    if version >= 21:
        out += u8(1)   # coopHostCharts
        out += u8(2)   # courseChooser: Alternating
        coop_i32 = [
            13, 3, 25, 2, 1, 4, 8,
            777, 888, 999, 1010, 1111, 1212,
            5, 2,
        ]
        out += b"".join(i32(value) for value in coop_i32)
        out += u32(0x4321)                     # purchasedMods2
        out += bytes([1, 0, 0, 1])             # reviveHeld2, rigged2, downed[0], downed[1]
        rows = bytearray(PERKS_NEW * 2)
        rows[10] = 3                           # player 1 took three of perk 10...
        rows[14] = 4
        rows[PERKS_NEW + 10] = 2               # ...and player 2 two more
        out += bytes(rows)
        out += u64(0x0123456789abcdef) + u64(0xfedcba9876543210)
    if version >= 22:
        out += u8(1)   # baseLevelRule: Same, the one value v22 and v23 could write besides Varied
    if version >= 23:
        out += bytes([1, 1])  # chartRerolls: the Radar reroll is spent; chartStarCharts: it was owed
    if version >= 24:
        out += u32(37)  # shuffleNext: pieces this run has drawn from the level bag
        out += u32(33)  # shuffleHandStart: where the saved chart's hand came off
    # v26 widened no field: it only permits the Dragonwing ship id in the items block.
    if version >= 27:
        out += bytes([1, 1])          # partnerValid; partnerSeat: player two's half
        out += bytes([2] * 9)         # partnerAvailMax
        out += bytes(range(90, 180))  # partnerAvail rows
        out += u64(0x1122334455667788)

    return bytes(out)


WIDTH_VERSION = 25  # first version whose header states how wide its records are


def header(version: int, record_bytes: bytes) -> bytes:
    out = b"OTES" + bytes([version, 1])
    if version >= WIDTH_VERSION:
        out += struct.pack("<H", len(record_bytes))
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("testing/fixtures/endless"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    for version in range(3, 28):
        payload = record(version)
        path = args.output / f"v{version:02d}.sav"
        path.write_bytes(header(version, payload) + payload)
        print(f"{path}: {len(payload)} byte record")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
