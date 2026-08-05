#!/usr/bin/env python3
"""Run bounded shipped demos and compare canonical rollback-state hashes."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


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
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
