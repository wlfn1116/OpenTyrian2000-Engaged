# Test runners

The runners exercise production code through hidden command-line entries. They
run headlessly and require the freeware Tyrian 2000 data.

From the repository root:

```sh
make test TEST_DATA=/path/to/data
make sanitize-test TEST_DATA=/path/to/data
```

## In-process suite

`run_unit_suite.py` runs the broad correctness suite:

```sh
python3 testing/run_unit_suite.py \
  --exe ./opentyrian2000 \
  --data ./data
```

It covers:

- rollback snapshots, restore fixups, and malformed wire state
- every supported Endless save version and migration
- custom-episode activation, Endless pools, save identity, and transfer envelopes
- custom weapon serialization and capacity limits
- modifier, perk, course, and economy invariants
- Arcade lives, hull, rear-gun, and HUD geometry
- online mode flags, ownership, and lobby settings
- both-player Endless perks, shops, drives, deaths, and course choices
- deterministic generation across 768 course seeds
- hostile values, truncation, string termination, and guard bytes

## Replay suite

`run_replay_fixtures.py` replays shipped demos from rollback snapshots and
checks their registered-state hashes:

```sh
python3 testing/run_replay_fixtures.py \
  --exe ./opentyrian2000 \
  --data ./data
```

Destruct has no demo corpus. Its test runs a scripted headless battle and
compares each tick with a replay from that tick's snapshot.

Replay hashes are compatibility fixtures. Change them only after reviewing an
intentional simulation change on every desktop CI platform.

## Network fault suite

`network_fault_test.py` starts two game processes behind a deterministic UDP
proxy. The proxy can add latency, loss, reordering, duplication, and a complete
traffic pause.

```sh
python3 testing/network_fault_test.py \
  --exe ./opentyrian2000 \
  --data ./data
```

Use `--scenario N` to run one case. Scenario 15 is excluded from the default
run.

| ID | Scenario | Contract |
| ---: | --- | --- |
| 0 | base | Reliable round trip, death prompt, and outpost rendezvous |
| 1 | campaign | Two loadouts, concurrent purchases, save checkpoint, and departure |
| 2 | endless | Player-owned run state, income, downed ship, and course state |
| 3 | barriers | Forty reliable phase barriers and sequence wrap |
| 4 | version-mismatch | Both peers reject incompatible wire versions |
| 5 | gameplay | Linked Arcade rollback, prediction, special-ready flashes, and no desync |
| 6 | desync-recovery | Deliberate corruption is detected and repaired |
| 7 | save-resume | Two-stage save, custom sidekick, and host-driven resume in fresh processes |
| 8 | outage | Recovery after an eight-second traffic blackout |
| 9 | peer-vanish | Host exits cleanly after the joiner disappears |
| 10 | sidekick-combos | Four mount combinations, including charge and custom weapons |
| 11 | menu-race | Both players press Esc on the same rollback frame |
| 12 | endless-zones | Ten zones cover the modifier registry and wallet parity |
| 13 | campaign-shop | Shop protocol, online Save routing, custom weapons, and episode transition |
| 14 | double-earnings | Host publishes settings and the joiner adopts them |
| 15 | soak | Long accelerated flight with working-set checks |
| 16 | arcade-separate | Two complete Arcade ships keep independent state |
| 17 | supertyrian | Online SuperTyrian on the Scrollock variant |
| 18 | super-arcade | Independent ship picks and per-ship coloured-ball weapons |
| 19 | delay-linked-analog | Delay-Based linked pair carries movement, analog turret aim, and special-ready flashes |
| 20 | timed-battle-finish | Guest dismissal retires both result screens before teardown |
| 21 | endless-resume | The joiner receives a run, both peers resume in the outpost, and player numbers remain stable |
| 22 | guest-esc | The joiner cancels the pre-game wait screen and both peers end the session cleanly |

Each peer runs in its own temporary directory. The runner drains output while
the processes run so a full pipe cannot stall network service.

## Save fixtures

`fixtures/endless/v03.sav` through `v27.sav` cover every binary `endless.sav`
version. They are generated independently of the C reader and must import and
round-trip through the current text format. No new versions are added to this
corpus.

Regenerate only to repair a fixture:

```sh
python3 testing/generate_save_fixtures.py
```

`fixtures/legacy/` holds a real `tyrian.sav` and `endless.sav` pair from before
`opentyrian.sav`. The suite checks first-launch import and repair of a zone slot
whose Endless section is missing. Any replacement pair needs equivalent known
contents.

Review generated records before committing them.
