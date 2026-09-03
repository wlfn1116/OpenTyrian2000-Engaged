#!/usr/bin/env python3
"""Run two real game peers through a deterministic hostile UDP proxy."""

from __future__ import annotations

import argparse
import hashlib
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
    (22, "guest-esc", 0, 90, True),
    (23, "custom-endless-converge", 0, 480, True),
    (24, "custom-resume-converge", 0, 480, True),
    (25, "custom-episode-converge", 0, 120, True),
    (26, "custom-lifecycle", 0, 480, True),
    (27, "custom-device-transfer", 0, 240, True),
    (28, "custom-disconnect-save", 0, 480, True),
)


def run_device_transfer(executable: Path, data_dir: Path, kind: str,
                        send_dir: str, recv_dir: str, timeout_s: int,
                        expect_fail: bool = False, push: bool = False) -> tuple[int, str]:
    """Run a real Transfer-menu exchange between two local processes."""
    base = [str(executable), "--no-sound", "--no-joystick", "--no-xmas",
            "--data", str(data_dir)]
    env = os.environ.copy()
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    send_env, recv_env = env.copy(), env.copy()
    send_env["XDG_CONFIG_HOME"] = send_dir
    recv_env["XDG_CONFIG_HOME"] = recv_dir

    # Start the peer that owns port 1332 first.
    send_args = base + ["--test-xfer-send", kind]
    recv_args = base + ["--test-xfer-recv", kind]
    if push:
        send_args += ["--test-xfer-push", "--test-xfer-host", "127.0.0.1"]
        recv_args += ["--test-xfer-push"]
        first, second = ("receiver", recv_args, recv_env, recv_dir), \
                        ("sender", send_args, send_env, send_dir)
    else:
        recv_args += ["--test-xfer-host", "127.0.0.1"]
        first, second = ("sender", send_args, send_env, send_dir), \
                        ("receiver", recv_args, recv_env, recv_dir)

    procs = {}
    procs[first[0]] = subprocess.Popen(first[1], env=first[2], cwd=first[3],
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # Passive receive binds port 1332 after discovery times out.
    time.sleep(4.0 if push else 1.5)
    procs[second[0]] = subprocess.Popen(second[1], env=second[2], cwd=second[3],
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    sender, receiver = procs["sender"], procs["receiver"]

    transcript = ""
    status = 0
    for who, proc in (("receiver", receiver), ("sender", sender)):
        try:
            out = proc.communicate(timeout=timeout_s)[0]
        except subprocess.TimeoutExpired:
            proc.kill()
            out = proc.communicate()[0]
            print(f"custom levels: the transfer {who} had to be killed")
            status = 1
        transcript += f"--- {who} ---\n{out}"
        if proc.returncode != 0:
            if not expect_fail:
                print(f"custom levels: the transfer {who} exited {proc.returncode}")
            status = 1
    return status, transcript

# Three valid containers with distinct contents and identities.
CLV_FIXTURES = ("clv_ep1.clv", "clv_ep2.clv", "clv_ep3.clv")


def peer_user_dir(user_dir: str) -> Path:
    if os.name == "nt":
        return Path(user_dir)
    return Path(user_dir) / "opentyrian2000"


def clv_dir(user_dir: str) -> Path:
    return peer_user_dir(user_dir) / "custom_levels"


def install_clv(user_dir: str, names, fixture_dir: Path) -> None:
    """Install exactly these fixtures for one peer."""
    target = clv_dir(user_dir)
    target.mkdir(parents=True, exist_ok=True)
    for stale in target.glob("*.clv"):
        stale.unlink()
    for name in names:
        shutil.copyfile(fixture_dir / name, target / name)


def clv_present(user_dir: str) -> set[str]:
    target = clv_dir(user_dir)
    return {p.name for p in target.glob("*.clv")} if target.is_dir() else set()


def clv_digest(user_dir: str, name: str) -> str | None:
    path = clv_dir(user_dir) / name
    if not path.is_file():
        return None
    return hashlib.sha256(path.read_bytes()).hexdigest()


def report_refusal(label: str, transcript: str, result: int) -> int:
    """Require a missing dependency set to stop the session."""
    refused = "Unable to synchronize custom levels." in transcript
    played = "NET GAMEPLAY PASS" in transcript
    if result == 0 and played:
        print(f"custom levels: {label} FAILED - the pair played without the containers")
        return 1
    if not refused:
        print(f"custom levels: {label} FAILED - stopped, but not for the missing containers")
        return 1
    print(f"custom levels: {label} refused the session, as it should")
    return 0


def report_convergence(label: str, host_dir: str, join_dir: str, expect: set[str],
                       fixture_dir: Path) -> int:
    """Require both peers to hold exact copies of the expected fixtures."""
    problems = []
    for who, where in (("host", host_dir), ("joiner", join_dir)):
        have = clv_present(where)
        if have != expect:
            problems.append(f"{who} holds {sorted(have)}, expected {sorted(expect)}")
        for name in sorted(have & expect):
            want = hashlib.sha256((fixture_dir / name).read_bytes()).hexdigest()
            if clv_digest(where, name) != want:
                problems.append(f"{who}'s copy of {name} does not match the original")
    if problems:
        print(f"custom levels: {label} FAILED")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print(f"custom levels: {label} converged on {sorted(expect)}")
    return 0


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
    if scenario == 22:
        common += ["--test-net-gameplay-ticks", "700"]
    if extra_common:
        common += extra_common
    host_cmd = [str(executable), *common, "--net", f"127.0.0.1:{proxy_a_addr[1]}",
                "--net-port", str(host_addr[1]), "--net-player-number", "1"]
    join_cmd = [str(executable), *common, "--net", f"127.0.0.1:{proxy_b_addr[1]}",
                "--net-port", str(join_addr[1]), "--net-player-number", "2"]
    if scenario == 4:
        join_cmd += ["--test-net-version-skew", "1"]
    if scenario == 22:
        join_cmd += ["--test-net-guest-esc"]

    base_env = os.environ.copy()
    base_env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    # Isolate each peer's work and config directories; callers may reuse them across stages.
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

    # Drain both pipes continuously so logging cannot block network progress.
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
    if scenario == 22:
        # Success inverts: the joiner walks out of the wait-for-details screen, and both peers
        # must end the session on their own, each saying why.
        cancelled = ("net gameplay: joiner escapes the details wait" in join_out
                     and "network test: session halt: Quitting..." in join_out
                     and "network test: session halt: Other player quit the game." in host_out
                     and host.returncode != 0 and join.returncode != 0)
        return (0 if cancelled else 1), transcript, injected
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
        # Lobby-selected Campaign runs must still record their starting episode.
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
                # Fresh stage-two processes must restore player two's compiled custom sidekick.
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
            # Restore the Endless sidecar and checkpoint outpost before either peer advances.
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
        elif scenario in (23, 24, 25):
            fixture_dir = executable.parent / "testing" / "fixtures"
            missing = [n for n in CLV_FIXTURES if not (fixture_dir / n).is_file()]
            if missing:
                print(f"network fault test: missing container fixtures {missing}")
                return 2

            host_dir = tempfile.mkdtemp(prefix="otnet_host_")
            join_dir = tempfile.mkdtemp(prefix="otnet_join_")
            a, b, c = CLV_FIXTURES

            if scenario == 23:
                # The host's pool is authoritative; the joiner's extra remains installed.
                install_clv(host_dir, (a, b), fixture_dir)
                install_clv(join_dir, (a, c), fixture_dir)
                common = ["--test-net-gameplay-ticks", "1000000", "--test-net-game-type", "2",
                          "--test-net-zones", "1", "--test-net-lobby-settings",
                          "--test-net-custom-endless", "2"]
                r1, t1, injected = run_scenario(
                    executable, data_dir, base_port, scenario, rounds,
                    extra_common=common, host_dir=host_dir, join_dir=join_dir,
                    deadline_s=deadline_s)
                result = r1
                transcript = f"[custom-only endless, split collections]\n{t1}"
                if clv_present(host_dir) != {a, b}:
                    print(f"custom levels: host collection changed to "
                          f"{sorted(clv_present(host_dir))}, expected {sorted({a, b})}")
                    result = 1
                joiner = clv_present(join_dir)
                if not {a, b, c} <= joiner:
                    print(f"custom levels: joiner holds {sorted(joiner)}, "
                          f"expected at least {sorted({a, b, c})}")
                    result = 1
                else:
                    print(f"custom levels: joiner took the host's collection, kept its own "
                          f"({sorted(joiner)})")
            elif scenario == 24:
                # Resume a saved dependency set split across the two peers.
                install_clv(host_dir, (a, b, c), fixture_dir)
                install_clv(join_dir, (a, b, c), fixture_dir)
                common = ["--test-net-gameplay-ticks", "1000000", "--test-net-game-type", "2",
                          "--test-net-zones", "1", "--test-net-lobby-settings",
                          "--test-net-custom-endless", "1"]
                r1, t1, injected = run_scenario(
                    executable, data_dir, base_port, scenario, rounds,
                    extra_common=common, host_dir=host_dir, join_dir=join_dir,
                    deadline_s=deadline_s)

                # Remove a different dependency from each peer.
                (clv_dir(host_dir) / b).unlink(missing_ok=True)
                (clv_dir(join_dir) / c).unlink(missing_ok=True)
                print(f"custom levels: before resume host has {sorted(clv_present(host_dir))}, "
                      f"joiner has {sorted(clv_present(join_dir))}")

                r2, t2, inj2 = run_scenario(
                    executable, data_dir, base_port + 4, scenario, rounds,
                    extra_common=common + ["--test-net-resume-slot", "22"],
                    host_dir=host_dir, join_dir=join_dir, deadline_s=deadline_s)
                for key, value in inj2.items():
                    injected[key] += value
                result = 1 if (r1 or r2) else 0
                transcript = f"[stage 1: mixed endless run]\n{t1}[stage 2: split resume]\n{t2}"
                result |= report_convergence("split resume", host_dir, join_dir,
                                             {a, b, c}, fixture_dir)
            else:
                # Campaign start downloads its selected container.
                install_clv(host_dir, (a, b), fixture_dir)
                install_clv(join_dir, (a,), fixture_dir)
                common = ["--test-net-gameplay-ticks", "700", "--test-net-game-type", "1",
                          "--test-net-lobby-settings", "--test-net-custom-episode", b]
                r1, t1, injected = run_scenario(
                    executable, data_dir, base_port, scenario, rounds,
                    extra_common=common, host_dir=host_dir, join_dir=join_dir,
                    deadline_s=deadline_s)
                result = r1
                transcript = f"[campaign on a custom episode]\n{t1}"
                result |= report_convergence("campaign episode", host_dir, join_dir,
                                             {a, b}, fixture_dir)

            for scratch in (host_dir, join_dir):
                shutil.rmtree(scratch, ignore_errors=True)
        elif scenario == 26:
            # Follow one collection through convergence, resumes, and refusal cases.
            fixture_dir = executable.parent / "testing" / "fixtures"
            missing = [n for n in CLV_FIXTURES if not (fixture_dir / n).is_file()]
            if missing:
                print(f"network fault test: missing container fixtures {missing}")
                return 2

            one_dir = tempfile.mkdtemp(prefix="otnet_one_")
            two_dir = tempfile.mkdtemp(prefix="otnet_two_")
            a, b, c = CLV_FIXTURES
            endless = ["--test-net-gameplay-ticks", "1000000", "--test-net-game-type", "2",
                       "--test-net-zones", "1", "--test-net-lobby-settings",
                       "--test-net-custom-endless", "2"]
            resume = endless + ["--test-net-resume-slot", "22"]
            result = 0
            transcript = ""
            injected = dict(loss=0, duplication=0, reordering=0, delay=0, pause=0)
            port = base_port

            def stage(title, extra, host_at, join_at):
                """Run another stage in the same two user directories."""
                nonlocal result, transcript, injected, port
                r, t, inj = run_scenario(
                    executable, data_dir, port, scenario, rounds, extra_common=extra,
                    host_dir=host_at, join_dir=join_at, deadline_s=deadline_s)
                port += 4
                for key, value in inj.items():
                    injected[key] += value
                transcript += f"[{title}]\n{t}"
                return r, t

            # Start with three host files and one joiner file.
            install_clv(one_dir, (a, b, c), fixture_dir)
            install_clv(two_dir, (a,), fixture_dir)
            r, _ = stage("1: uneven start, host authors three", endless, one_dir, two_dir)
            result |= r
            result |= report_convergence("uneven start", one_dir, two_dir,
                                         {a, b, c}, fixture_dir)

            # Swap roles after deleting different dependencies on each peer.
            (clv_dir(one_dir) / b).unlink(missing_ok=True)
            (clv_dir(one_dir) / c).unlink(missing_ok=True)
            (clv_dir(two_dir) / a).unlink(missing_ok=True)
            print(f"custom levels: split before the role swap - "
                  f"new joiner {sorted(clv_present(one_dir))}, "
                  f"new host {sorted(clv_present(two_dir))}")
            r, _ = stage("2: role swap over a split collection", resume, two_dir, one_dir)
            result |= r
            result |= report_convergence("role swap resume", one_dir, two_dir,
                                         {a, b, c}, fixture_dir)

            # Re-host the restored save without deleting anything.
            r, _ = stage("3: clean re-host of the same save", resume, two_dir, one_dir)
            result |= r
            result |= report_convergence("clean re-host", one_dir, two_dir,
                                         {a, b, c}, fixture_dir)

            # Remove one dependency from both peers; resume must fail.
            (clv_dir(one_dir) / b).unlink(missing_ok=True)
            (clv_dir(two_dir) / b).unlink(missing_ok=True)
            print(f"custom levels: one container lost everywhere - "
                  f"{sorted(clv_present(two_dir))} and {sorted(clv_present(one_dir))}")
            r, t = stage("4: a needed container lost on both", resume, two_dir, one_dir)
            result |= report_refusal("a save missing one container", t, r)

            # Remove every container from both peers; resume must fail.
            for where in (one_dir, two_dir):
                for stale in clv_dir(where).glob("*.clv"):
                    stale.unlink()
            r, t = stage("5: nothing left anywhere", resume, two_dir, one_dir)
            result |= report_refusal("a save with no containers left", t, r)

            for scratch in (one_dir, two_dir):
                shutil.rmtree(scratch, ignore_errors=True)
        elif scenario == 27:
            # Exercise each level-carrying Transfer item over its own UDP socket.
            fixture_dir = executable.parent / "testing" / "fixtures"
            missing = [n for n in CLV_FIXTURES if not (fixture_dir / n).is_file()]
            if missing:
                print(f"network fault test: missing container fixtures {missing}")
                return 2

            a, b, c = CLV_FIXTURES
            result = 0
            transcript = ""
            injected = dict(loss=0, duplication=0, reordering=0, delay=0, pause=0)

            # The receiver starts with one shared file and two missing files.
            for kind, label in (("levels", "Custom Levels"),
                                ("custom", "Custom Data"),
                                ("all", "Transfer All")):
                send_dir = tempfile.mkdtemp(prefix="otxfer_send_")
                recv_dir = tempfile.mkdtemp(prefix="otxfer_recv_")
                install_clv(send_dir, (a, b, c), fixture_dir)
                install_clv(recv_dir, (a,), fixture_dir)
                shared_before = clv_digest(recv_dir, a)

                r, t = run_device_transfer(executable, data_dir, kind,
                                           send_dir, recv_dir, deadline_s)
                transcript += f"[{label}]\n{t}"
                result |= r

                got = clv_present(recv_dir)
                if got != {a, b, c}:
                    print(f"custom levels: {label} left the receiver with {sorted(got)}, "
                          f"expected {sorted({a, b, c})}")
                    result = 1
                else:
                    mismatched = [n for n in sorted(got)
                                  if clv_digest(recv_dir, n)
                                  != hashlib.sha256((fixture_dir / n).read_bytes()).hexdigest()]
                    if mismatched:
                        print(f"custom levels: {label} corrupted {mismatched}")
                        result = 1
                    elif clv_digest(recv_dir, a) != shared_before:
                        print(f"custom levels: {label} rewrote the container both machines "
                              f"already shared")
                        result = 1
                    else:
                        print(f"custom levels: {label} carried the whole collection "
                              f"({sorted(got)})")

                # Repeat by direct push to cover the shared multipart sender.
                push_send = tempfile.mkdtemp(prefix="otxfer_psend_")
                push_recv = tempfile.mkdtemp(prefix="otxfer_precv_")
                install_clv(push_send, (a, b, c), fixture_dir)
                install_clv(push_recv, (a,), fixture_dir)
                r4, t4 = run_device_transfer(executable, data_dir, kind,
                                             push_send, push_recv, deadline_s, push=True)
                transcript += f"[{label} pushed to a waiting receiver]\n{t4}"
                result |= r4
                pushed = clv_present(push_recv)
                if pushed != {a, b, c}:
                    print(f"custom levels: {label} pushed only {sorted(pushed)}, "
                          f"expected {sorted({a, b, c})}")
                    result = 1
                else:
                    print(f"custom levels: {label} pushed the whole collection "
                          f"({sorted(pushed)})")
                for scratch in (push_send, push_recv):
                    shutil.rmtree(scratch, ignore_errors=True)

                # Empty Custom Data and Transfer All runs must not create the folder.
                if kind in ("custom", "all"):
                    bare_send = tempfile.mkdtemp(prefix="otxfer_bare_s_")
                    bare_recv = tempfile.mkdtemp(prefix="otxfer_bare_r_")
                    r3, t3 = run_device_transfer(executable, data_dir, kind,
                                                 bare_send, bare_recv, deadline_s)
                    transcript += f"[{label} with no containers anywhere]\n{t3}"
                    strays = [w for w in (bare_send, bare_recv) if clv_dir(w).exists()]
                    if r3 != 0:
                        print(f"custom levels: {label} failed between two machines that have "
                              f"no containers")
                        result = 1
                    elif strays:
                        print(f"custom levels: {label} created a custom_levels folder with no "
                              f"containers in play")
                        result = 1
                    else:
                        print(f"custom levels: {label} still works, and stays invisible, with "
                              f"no containers anywhere")
                    for scratch in (bare_send, bare_recv):
                        shutil.rmtree(scratch, ignore_errors=True)

                # Custom Levels cannot be offered from an empty collection.
                if kind == "levels":
                    empty_dir = tempfile.mkdtemp(prefix="otxfer_none_")
                    clv_dir(empty_dir).mkdir(parents=True, exist_ok=True)
                    r2, t2 = run_device_transfer(executable, data_dir, kind,
                                                 empty_dir, recv_dir, 90, expect_fail=True)
                    transcript += f"[{label} with nothing installed]\n{t2}"
                    if r2 == 0:
                        print("custom levels: a machine with no containers still offered a "
                              "transfer")
                        result = 1
                    else:
                        print("custom levels: a machine with no containers offers nothing, "
                              "as it should")
                    shutil.rmtree(empty_dir, ignore_errors=True)

                for scratch in (send_dir, recv_dir):
                    shutil.rmtree(scratch, ignore_errors=True)
        elif scenario == 28:
            # Save after the joiner leaves the outpost, then resume that save.
            fixture_dir = executable.parent / "testing" / "fixtures"
            missing = [n for n in CLV_FIXTURES if not (fixture_dir / n).is_file()]
            if missing:
                print(f"network fault test: missing container fixtures {missing}")
                return 2

            host_dir = tempfile.mkdtemp(prefix="otnet_host_")
            join_dir = tempfile.mkdtemp(prefix="otnet_join_")
            a, b, c = CLV_FIXTURES
            kept_slot = 15   # the two-player page; only it records both players
            endless = ["--test-net-gameplay-ticks", "1000000", "--test-net-game-type", "2",
                       "--test-net-zones", "1", "--test-net-lobby-settings",
                       "--test-net-custom-endless", "2"]

            install_clv(host_dir, (a, b, c), fixture_dir)
            install_clv(join_dir, (a,), fixture_dir)

            r1, t1, injected = run_scenario(
                executable, data_dir, base_port, scenario, rounds,
                extra_common=endless + ["--test-net-outpost-quit",
                                        "--test-net-disconnect-save", str(kept_slot)],
                host_dir=host_dir, join_dir=join_dir, deadline_s=deadline_s)
            transcript = f"[stage 1: the joiner leaves the outpost]\n{t1}"

            result = 0
            if "host takes the disconnect offer" not in t1:
                print("custom levels: the host was never offered the interrupted session")
                result = 1
            if "host kept the session in slot" not in t1:
                print("custom levels: the host never wrote the disconnect save")
                result = 1
            result |= report_convergence("disconnect save", host_dir, join_dir,
                                         {a, b, c}, fixture_dir)

            # Resume without deleting any dependencies.
            r2, t2, inj2 = run_scenario(
                executable, data_dir, base_port + 4, scenario, rounds,
                extra_common=endless + ["--test-net-resume-slot", str(kept_slot)],
                host_dir=host_dir, join_dir=join_dir, deadline_s=deadline_s)
            for key, value in inj2.items():
                injected[key] += value
            transcript += f"[stage 2: re-host from the disconnect save]\n{t2}"
            result |= r2
            result |= report_convergence("re-host after disconnect", host_dir, join_dir,
                                         {a, b, c}, fixture_dir)
            if r2 == 0:
                print(f"custom levels: the disconnect save in slot {kept_slot} re-hosted cleanly")

            for scratch in (host_dir, join_dir):
                shutil.rmtree(scratch, ignore_errors=True)
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
