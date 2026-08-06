#!/usr/bin/env python3
"""Run two real game peers through a deterministic hostile UDP proxy."""

from __future__ import annotations

import argparse
import heapq
import os
import select
import socket
import subprocess
import time
from pathlib import Path


# Each scenario is a separate pair of peers on its own ports, so one mode's protocol cannot
# leave state behind for the next. Rounds are only the warm-up that establishes the session;
# the campaign and Endless scenarios spend their time in their own protocols instead.
SCENARIOS = (
    (0, "base", 48),
    (1, "campaign", 6),
    (2, "endless", 6),
)


def run_scenario(
    executable: Path, data_dir: Path, base_port: int, scenario: int, rounds: int
) -> tuple[int, str, dict[str, int]]:
    host_addr = ("127.0.0.1", base_port)
    proxy_a_addr = ("127.0.0.1", base_port + 1)
    join_addr = ("127.0.0.1", base_port + 2)
    proxy_b_addr = ("127.0.0.1", base_port + 3)

    proxy_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    proxy_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    proxy_a.bind(proxy_a_addr)
    proxy_b.bind(proxy_b_addr)
    proxy_a.setblocking(False)
    proxy_b.setblocking(False)

    common = [
        "--no-sound", "--no-joystick", "--no-xmas", "--data", str(data_dir),
        "--test-net-rounds", str(rounds),
        "--test-net-scenario", str(scenario),
    ]
    host_cmd = [str(executable), *common, "--net", f"127.0.0.1:{proxy_a_addr[1]}",
                "--net-port", str(host_addr[1]), "--net-player-number", "1"]
    join_cmd = [str(executable), *common, "--net", f"127.0.0.1:{proxy_b_addr[1]}",
                "--net-port", str(join_addr[1]), "--net-player-number", "2"]

    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    host = subprocess.Popen(host_cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    join = subprocess.Popen(join_cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    started = time.monotonic()
    deadline = started + 90
    pause_to = 0.0
    pause_triggered = False
    queue: list[tuple[float, int, socket.socket, tuple[str, int], bytes]] = []
    serial = 0
    seen = [0, 0]
    injected = dict(loss=0, duplication=0, reordering=0, delay=0, pause=0)

    def schedule(direction: int, payload: bytes) -> None:
        nonlocal pause_to, pause_triggered, serial
        seen[direction] += 1
        number = seen[direction]
        if number % 13 == 0:
            injected["loss"] += 1
            return

        now = time.monotonic()
        # A fixed packet count keeps loader-speed differences from moving the
        # pause outside the exchange.
        if not pause_triggered and sum(seen) >= 40:
            pause_triggered = True
            pause_to = now + 0.350
        due = now + (0.008 + (number % 5) * 0.006)
        injected["delay"] += 1
        if number % 5 == 0:
            due += 0.070
            injected["reordering"] += 1
        if now < pause_to:
            due = max(due, pause_to)
            injected["pause"] += 1

        send_sock = proxy_b if direction == 0 else proxy_a
        destination = join_addr if direction == 0 else host_addr
        serial += 1
        heapq.heappush(queue, (due, serial, send_sock, destination, payload))
        if number % 7 == 0:
            serial += 1
            heapq.heappush(queue, (due + 0.004, serial, send_sock, destination, payload))
            injected["duplication"] += 1

    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            while queue and queue[0][0] <= now:
                _, _, sender, destination, payload = heapq.heappop(queue)
                sender.sendto(payload, destination)

            if host.poll() is not None and join.poll() is not None and not queue:
                break

            readable, _, _ = select.select([proxy_a, proxy_b], [], [], 0.005)
            for source in readable:
                while True:
                    try:
                        payload, _ = source.recvfrom(65535)
                    except BlockingIOError:
                        break
                    except ConnectionResetError:
                        # Windows reports an earlier ICMP port-unreachable on the next recv.
                        # A peer may still be opening its socket; reliable connect retries it.
                        break
                    schedule(0 if source is proxy_a else 1, payload)
        else:
            raise TimeoutError("network peers did not finish within 90 seconds")
    except Exception as exc:
        print(f"network fault test: {exc}")
        for process in (host, join):
            if process.poll() is None:
                process.kill()
    finally:
        proxy_a.close()
        proxy_b.close()

    host_out, _ = host.communicate(timeout=5)
    join_out, _ = join.communicate(timeout=5)
    transcript = f"--- host ---\n{host_out}--- joiner ---\n{join_out}"
    failed = host.returncode != 0 or join.returncode != 0
    return (1 if failed else 0), transcript, injected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=48)
    parser.add_argument("--base-port", type=int, default=45100)
    parser.add_argument(
        "--scenario", type=int, default=-1,
        help="run one scenario by id instead of all of them",
    )
    args = parser.parse_args()

    executable = args.exe.resolve()
    data_dir = args.data.resolve()

    selected = [s for s in SCENARIOS if args.scenario in (-1, s[0])]
    if not selected:
        print(f"network fault test: no such scenario {args.scenario}")
        return 2

    status = 0
    # Every fault kind has to be injected somewhere across the run, not necessarily in each
    # scenario: the short ones do not carry enough packets to hit every modulus.
    totals = dict(loss=0, duplication=0, reordering=0, delay=0, pause=0)

    for index, (scenario, name, default_rounds) in enumerate(selected):
        rounds = args.rounds if scenario == 0 else default_rounds
        # Fresh ports per scenario so a lingering packet cannot reach the next pair.
        base_port = args.base_port + index * 10
        print(f"=== scenario {scenario} ({name}), {rounds} rounds, port {base_port} ===")
        result, transcript, injected = run_scenario(
            executable, data_dir, base_port, scenario, rounds
        )
        print(transcript, end="")
        print("faults:", " ".join(f"{k}={v}" for k, v in injected.items()))
        for key, value in injected.items():
            totals[key] += value
        if result != 0:
            print(f"network fault test: scenario {scenario} ({name}) failed")
            status = 1

    missing = [name for name, count in totals.items() if count == 0]
    if missing:
        print("network fault test: uninjected conditions:", ", ".join(missing))
        status = 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
