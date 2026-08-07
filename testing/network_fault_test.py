#!/usr/bin/env python3
"""Run two real game peers through a deterministic hostile UDP proxy."""

from __future__ import annotations

import argparse
import heapq
import os
import re
import select
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path


# Each scenario is a separate pair of peers on its own ports, so one mode's protocol cannot
# leave state behind for the next. Rounds are only the warm-up that establishes the session;
# the campaign and Endless scenarios spend their time in their own protocols instead.
SCENARIOS = (
    (0, "base", 48),
    (1, "campaign", 6),
    (2, "endless", 6),
    (3, "barriers", 6),
    # The joiner reports a skewed wire version; success is both peers rejecting the handshake
    # with the mismatch message and exiting on their own, not a hang or a half-open session.
    (4, "version-mismatch", 2),
    # Real gameplay: both peers fly the first Arcade level under rollback with scripted
    # movement. 5 must stay desync-free while rollbacks happen; 6 bends one joiner frame and
    # must detect the desync and repair it through a recovery epoch.
    (5, "gameplay", 0),
    (6, "desync-recovery", 0),
    # Two-stage save/resume: the pair flies and saves the LAST LEVEL slot on exit, then the
    # same pair (same scratch directories) resumes it, host loading and the joiner adopting
    # the resume form; the resumed level must fly desync-free.
    (7, "save-resume", 0),
    # The proxy goes completely silent for 8 seconds mid-level, then returns. Shorter than the
    # dead-link timeout, so the session has to ride it out and still finish clean.
    (8, "outage", 0),
    # The joiner is killed mid-level. The host must reach its clean connection-lost path and
    # exit with the message on its own, not hang until this harness's deadline.
    (9, "peer-vanish", 0),
)


def run_scenario(
    executable: Path, data_dir: Path, base_port: int, scenario: int, rounds: int,
    extra_common: list[str] | None = None,
    host_dir: str | None = None, join_dir: str | None = None,
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
    if scenario in (5, 6, 8, 9):
        common += ["--test-net-gameplay-ticks", "700"]
    if scenario == 6:
        # Both peers know a desync is expected; only the joiner actually bends the frame.
        common += ["--test-net-corrupt-frame", "300"]
    if extra_common:
        common += extra_common
    host_cmd = [str(executable), *common, "--net", f"127.0.0.1:{proxy_a_addr[1]}",
                "--net-port", str(host_addr[1]), "--net-player-number", "1"]
    join_cmd = [str(executable), *common, "--net", f"127.0.0.1:{proxy_b_addr[1]}",
                "--net-port", str(join_addr[1]), "--net-player-number", "2"]
    if scenario == 4:
        join_cmd += ["--test-net-version-skew", "1"]

    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    # Each peer gets its own scratch working directory: the game writes tyrian.cfg and saves
    # into the cwd, and a shared one leaks state between runs and races between the peers.
    # A caller may pass directories in to carry saves across stages (the resume scenario).
    own_dirs = host_dir is None
    if own_dirs:
        host_dir = tempfile.mkdtemp(prefix="otnet_host_")
        join_dir = tempfile.mkdtemp(prefix="otnet_join_")
    host = subprocess.Popen(host_cmd, env=env, cwd=host_dir,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    join = subprocess.Popen(join_cmd, env=env, cwd=join_dir,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    started = time.monotonic()
    deadline = started + 90
    pause_to = 0.0
    pause_triggered = False
    outage_to = 0.0
    outage_done = False
    kill_at = started + 14 if scenario == 9 else None
    queue: list[tuple[float, int, socket.socket, tuple[str, int], bytes]] = []
    serial = 0
    seen = [0, 0]
    injected = dict(loss=0, duplication=0, reordering=0, delay=0, pause=0)

    def schedule(direction: int, payload: bytes) -> None:
        nonlocal pause_to, pause_triggered, outage_to, outage_done, serial
        seen[direction] += 1
        number = seen[direction]

        now = time.monotonic()
        # The outage scenario: the wire goes completely dead in both directions for 8 seconds
        # mid-level, shorter than the dead-link timeout, then comes back.
        if scenario == 8 and not outage_done and sum(seen) >= 400:
            outage_done = True
            outage_to = now + 8.0
        if now < outage_to:
            injected["loss"] += 1
            return

        if number % 13 == 0:
            injected["loss"] += 1
            return

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

            if kill_at is not None and now >= kill_at and join.poll() is None:
                join.kill()
                kill_at = None

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
    if own_dirs:
        for scratch in (host_dir, join_dir):
            shutil.rmtree(scratch, ignore_errors=True)
    transcript = f"--- host ---\n{host_out}--- joiner ---\n{join_out}"
    if scenario == 4:
        # Success inverts: both peers must reject the handshake on their own and say why.
        rejected = ("Network version mismatch" in host_out
                    and "Network version mismatch" in join_out
                    and host.returncode != 0 and join.returncode != 0)
        return (0 if rejected else 1), transcript, injected
    if scenario == 9:
        # The joiner was killed; success is the host reaching its own clean connection-lost
        # exit with the message, well before this harness's deadline would have killed it.
        detected = ("Network connection was lost" in host_out and host.returncode != 0)
        return (0 if detected else 1), transcript, injected
    if scenario == 0:
        # Soak check: the base exchange must not grow the working set. The peers print zero
        # start figures on platforms without the probe, which skips the comparison.
        for line in (host_out + join_out).splitlines():
            match = re.match(r"NETWORK TEST MEM player=\d+ start=(\d+) end=(\d+) kb", line)
            if match and int(match.group(1)) > 0:
                growth = int(match.group(2)) - int(match.group(1))
                if growth > 16384:
                    print(f"network fault test: working set grew {growth} kb over the session")
                    return 1, transcript, injected

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
        if scenario == 7:
            # Two stages over the same scratch directories: play-and-save, then resume it.
            host_dir = tempfile.mkdtemp(prefix="otnet_host_")
            join_dir = tempfile.mkdtemp(prefix="otnet_join_")
            r1, t1, injected = run_scenario(
                executable, data_dir, base_port, scenario, rounds,
                extra_common=["--test-net-gameplay-ticks", "200", "--test-net-save-exit"],
                host_dir=host_dir, join_dir=join_dir)
            r2, t2, inj2 = run_scenario(
                executable, data_dir, base_port + 4, scenario, rounds,
                extra_common=["--test-net-gameplay-ticks", "300",
                              "--test-net-resume-slot", "22"],
                host_dir=host_dir, join_dir=join_dir)
            for scratch in (host_dir, join_dir):
                shutil.rmtree(scratch, ignore_errors=True)
            for key, value in inj2.items():
                injected[key] += value
            result = 1 if (r1 or r2) else 0
            transcript = f"[stage 1: play and save]\n{t1}[stage 2: resume]\n{t2}"
        else:
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

    # Only a full run carries enough packets to hit every fault modulus; a single short
    # scenario (version-mismatch exchanges two packets) legitimately misses some.
    missing = [name for name, count in totals.items() if count == 0]
    if missing and args.scenario == -1:
        print("network fault test: uninjected conditions:", ", ".join(missing))
        status = 1
    return status


if __name__ == "__main__":
    raise SystemExit(main())
