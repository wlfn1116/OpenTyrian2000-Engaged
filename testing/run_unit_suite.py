#!/usr/bin/env python3
"""Run the in-process correctness suite and propagate its real exit status."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument(
        "--fixtures", type=Path, default=Path("testing/fixtures/endless")
    )
    args = parser.parse_args()

    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    command = [
        str(args.exe.resolve()),
        "--no-sound",
        "--no-joystick",
        "--no-xmas",
        "--data",
        str(args.data.resolve()),
        "--test-suite",
        "--test-fixtures",
        str(args.fixtures.resolve()),
    ]
    result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=120)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
