#!/usr/bin/env python3
"""Replay the rollback simulations from their own snapshots.

The shipped demos cover the main game and are checked against canonical state hashes; the
Destruct minigame has no demo corpus, so it is run headlessly instead and only has to replay
each frame identically.
"""

from __future__ import annotations

import argparse
import os
import random
import re
import struct
import subprocess
import tempfile
from pathlib import Path

DESTRUCT_TICKS = "400"

LOADOUTS = (
    (7, 29, 21, 20, 5, 6, 0, 1, 1, 1),
    (5, 29, 21, 21, 5, 8, 2, 6, 10, 11),
    (19, 0, 21, 23, 5, 6, 0, 4, 3, 1),
    (12, 27, 2, 3, 4, 7, 5, 9, 11, 6),
)
COOP_FLIGHTS = ((1, 16, 11601, 1500), (2, 5, 20501, 1500), (4, 4, 40401, 1500), (5, 2, 50201, 1500))
COOP_MIN_TICKS = 300
DETAIL_FLIGHT = (1, 17, 10701, 900)
SIM_ITEMS = ("mtrand", "player", "enemy", "enemyAvail", "enemyShot", "playerShotData",
             "explosions", "backPos2", "backMove2", "mapY2", "mapY2Pos", "map2YDelay")


def write_flight_demo(path: Path, episode: int, level: int, seed: int, ticks: int) -> None:
    rng = random.Random(seed)
    front, rear, left, right, generator, shield, special, ship, front_power, rear_power = \
        LOADOUTS[level % len(LOADOUTS)]
    data = bytearray([episode]) + b"FLIGHT   \x00"
    data += bytes([level, front, rear, 0, left, right, generator, 2, 1, episode, shield, special,
                   ship, front_power, rear_power, 0, 0, 0, 1 + level % 30])
    data += struct.pack(">H", 1)
    directions = (0, 1, 2, 4, 8, 5, 9, 6, 10)
    covered = 0
    while covered < ticks + 200:
        keys = rng.choice(directions)
        keys |= 16 if rng.random() < 0.85 else 0
        keys |= 32 if rng.random() < 0.03 else 0
        keys |= 64 if rng.random() < 0.3 else 0
        keys |= 128 if rng.random() < 0.3 else 0
        hold = rng.randint(3, 40)
        data += bytes([keys]) + struct.pack(">H", hold)
        covered += hold
    path.write_bytes(bytes(data))


def run_flight(executable: Path, data_dir: Path, env: dict, work: Path, flight: tuple,
               extra: list[str]) -> tuple[re.Match | None, subprocess.CompletedProcess]:
    episode, level, seed, ticks = flight
    demo = work / f"flight_{episode}_{level}.demo"
    write_flight_demo(demo, episode, level, seed, ticks)
    command = [
        str(executable), "--no-sound", "--no-joystick", "--no-xmas", "--data", str(data_dir),
        "--test-replay", "1", "--test-replay-file", str(demo), "--test-replay-ticks", str(ticks),
        "--test-replay-coop", "--test-replay-seed", str(seed), *extra,
    ]
    result = subprocess.run(command, env=env, cwd=work, text=True, capture_output=True, timeout=600)
    return re.search(r"REPLAY demo=\d+ ticks=(\d+) failures=(\d+)", result.stdout), result


def read_trace(path: Path) -> tuple[list[str], dict[int, list[str]]]:
    lines = path.read_text(encoding="ascii").splitlines()
    count = int(lines[0].split()[1])
    names = [line.rsplit(" ", 1)[0] for line in lines[1:1 + count]]
    frames = {int(line.split()[1]): line.split()[2:] for line in lines[1 + count:]}
    return names, frames


def first_sim_difference(left: Path, right: Path) -> str | None:
    names, left_frames = read_trace(left)
    _, right_frames = read_trace(right)
    columns = [names.index(name) for name in SIM_ITEMS]
    for frame in sorted(set(left_frames) & set(right_frames)):
        for column in columns:
            if left_frames[frame][column] != right_frames[frame][column]:
                return f"{names[column]} at frame {frame}"
    if len(left_frames) != len(right_frames):
        return f"frame counts {len(left_frames)} and {len(right_frames)}"
    return None


def run_coop_flights(executable: Path, data_dir: Path, env: dict) -> bool:
    ok = True
    with tempfile.TemporaryDirectory(prefix="otflight_") as scratch:
        work = Path(scratch)
        for flight in COOP_FLIGHTS:
            match, result = run_flight(executable, data_dir, env, work, flight,
                                       ["--test-replay-depth", "10"])
            ticks = int(match.group(1)) if match else 0
            failures = int(match.group(2)) if match else -1
            passed = failures == 0 and ticks >= COOP_MIN_TICKS
            print(f"COOP FLIGHT episode={flight[0]} level={flight[1]} ticks={ticks} "
                  f"failures={failures} {'ok' if passed else 'FAILED'}")
            if not passed:
                print(result.stdout + result.stderr, end="")
                ok = False

        details = ("4", "3", "1")
        traces = []
        for detail in details:
            trace = work / f"detail_{detail}.trace"
            match, result = run_flight(executable, data_dir, env, work, DETAIL_FLIGHT,
                                       ["--test-replay-detail", detail,
                                        "--test-replay-trace", str(trace)])
            if match is None or int(match.group(2)) != 0 or not trace.exists():
                print(result.stdout + result.stderr, end="")
                ok = False
            traces.append(trace)
        if ok:
            for detail, trace in zip(details[1:], traces[1:]):
                difference = first_sim_difference(traces[0], trace)
                print(f"DETAIL LEVEL FLIGHT episode={DETAIL_FLIGHT[0]} level={DETAIL_FLIGHT[1]} "
                      f"detail={detail} {'ok' if difference is None else 'FAILED: ' + difference}")
                ok = ok and difference is None
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=Path("testing/replay_fixtures.tsv"))
    args = parser.parse_args()

    executable = args.exe.resolve()
    data_dir = args.data.resolve()

    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    failed = False
    for raw in args.fixtures.read_text(encoding="ascii").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        demo, ticks, expected = line.split()
        command = [
            str(executable), "--no-sound", "--no-joystick", "--no-xmas",
            "--data", str(data_dir), "--test-replay", demo,
            "--test-replay-ticks", ticks, "--test-replay-hash", expected,
        ]
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=60)
        print(result.stdout, end="")
        if result.returncode != 0:
            print(result.stderr, end="")
            failed = True

    command = [
        str(executable), "--no-sound", "--no-joystick", "--no-xmas",
        "--data", str(data_dir), "--test-destruct-ticks", DESTRUCT_TICKS,
    ]
    result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=120)
    print(result.stdout, end="")
    if result.returncode != 0:
        print(result.stderr, end="")
        failed = True

    if not run_coop_flights(executable, data_dir, env):
        failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
