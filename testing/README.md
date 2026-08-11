# Tests

The test runners exercise production code through hidden command-line entry
points. They run headlessly and need the freeware Tyrian 2000 data.

From the repository root:

```sh
make test TEST_DATA=/path/to/data
make sanitize-test TEST_DATA=/path/to/data
```

## In-process suite

`testing/run_unit_suite.py` runs the broad correctness suite:

```sh
python3 testing/run_unit_suite.py \
  --exe ./opentyrian2000 \
  --data ./data
```

Coverage includes:

- rollback snapshots, restore fixups, and malformed wire state;
- every supported Endless save version and migration;
- custom weapon serialization and capacity limits;
- modifier, perk, course, and economy invariants;
- arcade life, hull, rear-gun, and HUD geometry;
- online mode flags, player ownership, and lobby settings;
- both-player Endless perks, shops, drives, deaths, and course choices;
- deterministic generation across 768 course seeds;
- hostile values, truncation, string termination, and guard bytes.

## Replay suite

`testing/run_replay_fixtures.py` replays shipped demos from rollback snapshots and
checks their registered-state hashes:

```sh
python3 testing/run_replay_fixtures.py \
  --exe ./opentyrian2000 \
  --data ./data
```

Destruct has no demo corpus. The runner uses a scripted headless battle and
compares each tick with a replay from that tick's snapshot.

Replay hashes are compatibility fixtures. Update them only after reviewing an
intentional simulation change on every desktop CI platform.

## Network fault suite

`testing/network_fault_test.py` starts two game processes behind a deterministic
UDP proxy. The proxy injects latency, loss, reordering, duplication, and a full
traffic pause.

```sh
python3 testing/network_fault_test.py \
  --exe ./opentyrian2000 \
  --data ./data
```

Run one case with `--scenario N`. Scenario 15 is excluded from the default run.

| ID | Name | What it checks |
| ---: | --- | --- |
| 0 | base | Reliable round trip, death prompt, and outpost rendezvous |
| 1 | campaign | Two loadouts, concurrent purchases, save checkpoint, departure |
| 2 | endless | Player-owned run state, income, downed ship, and course state |
| 3 | barriers | Forty reliable phase barriers and sequence wrap |
| 4 | version-mismatch | Both peers reject incompatible wire versions |
| 5 | gameplay | Real Arcade rollback with prediction and no desync |
| 6 | desync-recovery | Deliberate corruption detected and repaired |
| 7 | save-resume | Two-stage save and host-driven resume |
| 8 | outage | Eight-second traffic blackout and recovery |
| 9 | peer-vanish | Host exits cleanly after the joiner disappears |
| 10 | sidekick-combos | Four mount combinations, including charge and custom weapons |
| 11 | menu-race | Both players press Esc on the same rollback frame |
| 12 | endless-zones | Ten zones covering the modifier registry and wallet parity |
| 13 | campaign-shop | Shop protocol, custom weapons, and episode transition |
| 14 | double-earnings | Host arms settings and joiner adopts them |
| 15 | soak | Long accelerated flight with working-set checks |
| 16 | arcade-separate | Two complete Arcade ships keep independent state |
| 17 | supertyrian | Online SuperTyrian on the Scrollock variant |
| 18 | super-arcade | Independent ship picks and per-ship colored-ball weapons |

Each peer runs in its own temporary directory. Output pipes are drained while
the processes run so a full pipe cannot stall network service.

## Save fixtures

Regenerate fixtures only when intentionally changing the migration corpus:

```sh
python3 testing/generate_save_fixtures.py
```

Review the generated records before committing them.
