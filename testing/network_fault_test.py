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
import threading
import time
from pathlib import Path


# Rows: id, name, warm-up rounds, deadline seconds, included by default.
# See testing/README.md for scenario behavior and acceptance criteria.
SCENARIOS = (
    (0, "base", 48, 90, True),
    (1, "campaign", 6, 90, True),
    (2, "endless", 6, 90, True),
    (3, "barriers", 6, 90, True),
    (4, "version-mismatch", 2, 90, True),
    (5, "gameplay", 0, 90, True),
    (6, "desync-recovery", 0, 90, True),
    (7, "save-resume", 0, 90, True),
    (8, "outage", 0, 90, True),
    (9, "peer-vanish", 0, 90, True),
    (10, "sidekick-combos", 0, 90, True),
    (11, "menu-race", 0, 90, True),
    (12, "endless-zones", 0, 480, True),
    (13, "campaign-shop", 0, 300, True),
    (14, "double-earnings", 0, 120, True),
    (15, "soak", 0, 480, False),  # Long-running opt-in case.
    (16, "arcade-separate", 0, 90, True),
    (17, "supertyrian", 0, 90, True),
    (18, "super-arcade", 0, 90, True),
    (19, "delay-linked-analog", 0, 90, True),
    (20, "timed-battle-finish", 0, 120, True),
    (21, "endless-resume", 0, 300, True),
)


def run_scenario(
    executable: Path, data_dir: Path, base_port: int, scenario: int, rounds: int,
    extra_common: list[str] | None = None,
    host_dir: str | None = None, join_dir: str | None = None,
    deadline_s: int = 90,
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
    if scenario == 11:
        common += ["--test-net-gameplay-ticks", "700", "--test-net-menu-frame", "300"]
    if scenario == 12:
        # The tick figure is a runaway backstop; the zone count is what ends the run.
        common += ["--test-net-gameplay-ticks", "1000000",
                   "--test-net-game-type", "2", "--test-net-zones", "10"]
    if scenario == 13:
        common += ["--test-net-gameplay-ticks", "1000000",
                   "--test-net-game-type", "1", "--test-net-zones", "3"]
    if scenario == 14:
        common += ["--test-net-gameplay-ticks", "700",
                   "--test-net-game-type", "1", "--test-net-lobby-settings"]
    if scenario == 15:
        common += ["--test-net-gameplay-ticks", "12000"]
    if scenario == 16:
        common += ["--test-net-gameplay-ticks", "700", "--test-net-arcade-separate"]
    if scenario == 17:
        common += ["--test-net-gameplay-ticks", "700",
                   "--test-net-game-type", "3", "--test-net-scrollock"]
    if scenario == 18:
        common += ["--test-net-gameplay-ticks", "700", "--test-net-game-type", "4"]
    if scenario == 19:
        common += ["--test-net-gameplay-ticks", "700"]
    if scenario == 20:
        common += ["--test-net-gameplay-ticks", "1000000", "--test-net-zones", "1"]
    if extra_common:
        common += extra_common
    host_cmd = [str(executable), *common, "--net", f"127.0.0.1:{proxy_a_addr[1]}",
                "--net-port", str(host_addr[1]), "--net-player-number", "1"]
    join_cmd = [str(executable), *common, "--net", f"127.0.0.1:{proxy_b_addr[1]}",
                "--net-port", str(join_addr[1]), "--net-player-number", "2"]
    if scenario == 4:
        join_cmd += ["--test-net-version-skew", "1"]

    base_env = os.environ.copy()
    base_env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    # Each peer gets its own scratch working and configuration directories. Unix builds use
    # XDG_CONFIG_HOME rather than the cwd; sharing the runner's home leaks settings between
    # scenarios and lets two peers race while writing the same configuration and save files.
    # A caller may pass directories in to carry saves across stages (the resume scenario).
    own_dirs = host_dir is None
    if own_dirs:
        host_dir = tempfile.mkdtemp(prefix="otnet_host_")
        join_dir = tempfile.mkdtemp(prefix="otnet_join_")
    host_env = base_env.copy()
    join_env = base_env.copy()
    host_env["XDG_CONFIG_HOME"] = host_dir
    join_env["XDG_CONFIG_HOME"] = join_dir
    host = subprocess.Popen(host_cmd, env=host_env, cwd=host_dir,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    join = subprocess.Popen(join_cmd, env=join_env, cwd=join_dir,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    # Drain both pipes for the whole run. Reading only at the end deadlocks a talkative peer:
    # the OS pipe buffer fills, its next print blocks, it stops servicing the socket, and the
    # partner reads that silence as a lost connection. The long Endless run tripped this at
    # whichever print happened to fill the buffer, which made it look like a netcode hang.
    def pump(stream, sink: list[str]) -> None:
        for line in stream:
            sink.append(line)

    host_lines: list[str] = []
    join_lines: list[str] = []
    pumps = (threading.Thread(target=pump, args=(host.stdout, host_lines), daemon=True),
             threading.Thread(target=pump, args=(join.stdout, join_lines), daemon=True))
    for thread in pumps:
        thread.start()

    started = time.monotonic()
    deadline = started + deadline_s
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
            raise TimeoutError(f"network peers did not finish within {deadline_s} seconds")
    except Exception as exc:
        print(f"network fault test: {exc}")
        for process in (host, join):
            if process.poll() is None:
                process.kill()
    finally:
        proxy_a.close()
        proxy_b.close()

    host.wait(timeout=5)
    join.wait(timeout=5)
    for thread in pumps:
        thread.join(timeout=5)
    host_out = "".join(host_lines)
    join_out = "".join(join_lines)
    if own_dirs:
        for scratch in (host_dir, join_dir):
            shutil.rmtree(scratch, ignore_errors=True)
    # Include exit codes so a crashed peer is distinguishable from a hang.
    transcript = (f"--- host (exit {host.returncode}) ---\n{host_out}"
                  f"--- joiner (exit {join.returncode}) ---\n{join_out}")
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
    if scenario in (0, 15):
        # Soak check: the exchange must not grow the working set. The peers print zero
        # start figures on platforms without the probe, which skips the comparison.
        for line in (host_out + join_out).splitlines():
            match = re.match(r"NETWORK TEST MEM player=\d+ start=(\d+) end=(\d+) kb", line)
            if match and int(match.group(1)) > 0:
                growth = int(match.group(2)) - int(match.group(1))
                if growth > 16384:
                    print(f"network fault test: working set grew {growth} kb over the session")
                    return 1, transcript, injected
    if scenario == 12:
        # Both machines derive both wallets; the printed sequences must be identical, and the
        # forced Apex/Legion/Elite Pack zones must actually have paid a bounty by the end.
        host_wallets = [l for l in host_out.splitlines() if l.startswith("NET ZONE WALLETS")]
        join_wallets = [l for l in join_out.splitlines() if l.startswith("NET ZONE WALLETS")]
        if len(host_wallets) < 11 or host_wallets != join_wallets:
            print("network fault test: the peers' zone wallet lines differ or are short "
                  f"({len(host_wallets)} vs {len(join_wallets)})")
            return 1, transcript, injected
        final = re.search(r"bounty=(\d+)", host_wallets[-1])
        if final is None or int(final.group(1)) == 0:
            print("network fault test: no elite bounty was ever paid across the forced zones")
            return 1, transcript, injected
    if scenario == 13:
        if ("driving the episode transition" not in host_out
                or "driving the episode transition" not in join_out):
            print("network fault test: the campaign run never drove the episode transition")
            return 1, transcript, injected
        for out, who in ((host_out, "host"), (join_out, "joiner")):
            if "NET SAVE ROUTE PASS" not in out:
                print(f"network fault test: the {who} routed online Save to Load")
                return 1, transcript, injected
        # The co-op Campaign board's inputs. A lobby row picks the episode, so neither peer runs
        # the episode-select menu that normally records where a run began; a machine that left the
        # field stale files the run under the wrong episode, or under none at all.
        cash = []
        for out, who in ((host_out, "host"), (join_out, "joiner")):
            mark = re.search(
                r"NET CAMPAIGN RECORD player=\d+ start=(\d+) episode=(\d+) cash=(\d+)", out)
            if mark is None:
                print(f"network fault test: the {who} never offered the campaign board an episode")
                return 1, transcript, injected
            if mark.group(1) != mark.group(2):
                print(f"network fault test: the {who} did not establish the starting episode "
                      f"(start={mark.group(1)}, episode={mark.group(2)})")
                return 1, transcript, injected
            cash.append(mark.group(3))
        if cash[0] != cash[1]:
            print("network fault test: the peers scored different combined cash "
                  f"({cash[0]} vs {cash[1]})")
            return 1, transcript, injected
    if scenario == 14:
        for out, who in ((host_out, "host"), (join_out, "joiner")):
            if "net session flags: shared=0 doubled=1" not in out:
                print(f"network fault test: the {who} did not arm Individual + Double Earnings")
                return 1, transcript, injected
    if scenario == 21 and "--test-net-resume-slot" in (extra_common or []):
        # This stage hosts from the player-two machine's directory, so the seats have to come out
        # crossed against the harness's own host/joiner roles.
        for out, who, seat in ((host_out, "host", 2), (join_out, "joiner", 1)):
            if f"NET GAMEPLAY PASS player={seat}" not in out:
                print(f"network fault test: the {who} did not keep player {seat} across the resume")
                return 1, transcript, injected
    if scenario == 16:
        # A run that quietly fell back to the linked pair would pass the desync check while
        # proving nothing, so require both peers to report the Separate session.
        for out, who in ((host_out, "host"), (join_out, "joiner")):
            if "separate=1 st=0 sa1=0 sa2=0" not in out:
                print(f"network fault test: the {who} did not fly Separate arcade ships")
                return 1, transcript, injected
    if scenario == 17:
        # 254 is SA_SUPERTYRIAN: both ships, on both machines, on the SuperTyrian ruleset.
        for out, who in ((host_out, "host"), (join_out, "joiner")):
            if "separate=1 st=1 sa1=254 sa2=254" not in out:
                print(f"network fault test: the {who} did not fly online SuperTyrian")
                return 1, transcript, injected
    if scenario == 18:
        # The picks crossed the wire only if BOTH machines ended up with ship 1 in slot one and
        # ship 2 in slot two; a peer that fell back to its own pick for both would read 1/1.
        for out, who in ((host_out, "host"), (join_out, "joiner")):
            if "separate=1 st=0 sa1=1 sa2=2" not in out:
                print(f"network fault test: the {who} did not equip both Super Arcade picks")
                return 1, transcript, injected
        # ...and the scripted colour-ball grant has to resolve per ship, so the two ships end the
        # flight holding the guns their own arsenals keep in that slot.
        for out, who in ((host_out, "host"), (join_out, "joiner")):
            balls = [l for l in out.splitlines() if l.startswith("NET SA BALL")]
            if not balls:
                print(f"network fault test: the {who} never resolved a Super Arcade colour ball")
                return 1, transcript, injected
        host_balls = [l for l in host_out.splitlines() if l.startswith("NET SA BALL")]
        join_balls = [l for l in join_out.splitlines() if l.startswith("NET SA BALL")]
        if host_balls != join_balls:
            print("network fault test: the peers disagree on what the colour balls handed out")
            return 1, transcript, injected
        if not any("p1=" in l and "p2=" in l and l.split("p1=")[1].split()[0]
                   != l.split("p2=")[1].split()[0] for l in host_balls):
            print("network fault test: one colour ball handed both ships the same gun; "
                  "the slot was not resolved against each ship's own arsenal")
            return 1, transcript, injected
    if scenario == 19:
        for out, who in ((host_out, "host"), (join_out, "joiner")):
            if "separate=0" not in out:
                print(f"network fault test: the {who} did not enter Linked Arcade")
                return 1, transcript, injected
            if "NET DELAY PASS" not in out:
                print(f"network fault test: the {who} did not complete Delay-Based gameplay")
                return 1, transcript, injected
    if scenario == 20:
        for out, who, dismissal in ((host_out, "host", "peer"),
                                    (join_out, "joiner", "local")):
            if "net gameplay: terminal rendezvous complete" not in out:
                print(f"network fault test: the {who} did not finish the Timed Battle barrier")
                return 1, transcript, injected
            if f"dismissal={dismissal}" not in out:
                print(f"network fault test: the {who} used the wrong result-screen dismissal path")
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

    selected = [s for s in SCENARIOS
                if (args.scenario == -1 and s[4]) or args.scenario == s[0]]
    if not selected:
        print(f"network fault test: no such scenario {args.scenario}")
        return 2

    status = 0
    # Every fault kind has to be injected somewhere across the run, not necessarily in each
    # scenario: the short ones do not carry enough packets to hit every modulus.
    totals = dict(loss=0, duplication=0, reordering=0, delay=0, pause=0)

    for index, (scenario, name, default_rounds, deadline_s, _default_run) in enumerate(selected):
        rounds = args.rounds if scenario == 0 else default_rounds
        # Fresh ports per scenario so a lingering packet cannot reach the next pair; the
        # sidekick matrix runs four pairs of its own inside its range.
        base_port = args.base_port + index * 20
        print(f"=== scenario {scenario} ({name}), {rounds} rounds, port {base_port} ===")
        if scenario == 10:
            result = 0
            transcript = ""
            injected = dict(loss=0, duplication=0, reordering=0, delay=0, pause=0)
            for profile in (1, 2, 3, 4):
                r, t, inj = run_scenario(
                    executable, data_dir, base_port + (profile - 1) * 4, scenario, rounds,
                    extra_common=["--test-net-gameplay-ticks", "700",
                                  "--test-net-loadout", str(profile)])
                transcript += f"[loadout {profile}]\n{t}"
                for key, value in inj.items():
                    injected[key] += value
                if r != 0:
                    result = 1
        elif scenario == 7:
            # Two stages over the same scratch directories: play-and-save, then resume it.
            host_dir = tempfile.mkdtemp(prefix="otnet_host_")
            join_dir = tempfile.mkdtemp(prefix="otnet_join_")
            r1, t1, injected = run_scenario(
                executable, data_dir, base_port, scenario, rounds,
                # Profile 4 leaves player two carrying a compiled custom sidekick. Stage two
                # starts fresh processes, so the pre-level resume exchange is the only way the
                # host can materialize that peer-owned design before the first saved tick.
                extra_common=["--test-net-gameplay-ticks", "200", "--test-net-save-exit",
                              "--test-net-loadout", "4"],
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
        elif scenario == 21:
            # The Endless half of scenario 7. Its run lives in a sidecar the save record does not
            # carry, so the resume has to hand the whole run over before either machine plays a
            # tick, and both have to come up in the outpost the checkpoint was written in.
            host_dir = tempfile.mkdtemp(prefix="otnet_host_")
            join_dir = tempfile.mkdtemp(prefix="otnet_join_")
            zone = ["--test-net-gameplay-ticks", "1000000",
                    "--test-net-game-type", "2", "--test-net-zones", "1"]
            # The outpost writes the LAST LEVEL checkpoint itself, so stage one only has to reach
            # one; it needs no --test-net-save-exit.
            r1, t1, injected = run_scenario(
                executable, data_dir, base_port, scenario, rounds,
                extra_common=zone, host_dir=host_dir, join_dir=join_dir, deadline_s=deadline_s)
            # Stage two hosts from the other machine's directory, so the peer that flew player two
            # is the one loading the save. Nobody may change seats across a resume.
            r2, t2, inj2 = run_scenario(
                executable, data_dir, base_port + 4, scenario, rounds,
                extra_common=zone + ["--test-net-resume-slot", "22"],
                host_dir=join_dir, join_dir=host_dir, deadline_s=deadline_s)
            for scratch in (host_dir, join_dir):
                shutil.rmtree(scratch, ignore_errors=True)
            for key, value in inj2.items():
                injected[key] += value
            result = 1 if (r1 or r2) else 0
            transcript = f"[stage 1: play and save]\n{t1}[stage 2: resume]\n{t2}"
        else:
            result, transcript, injected = run_scenario(
                executable, data_dir, base_port, scenario, rounds, deadline_s=deadline_s
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
