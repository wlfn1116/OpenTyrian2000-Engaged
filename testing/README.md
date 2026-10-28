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
- the campaign and Endless save codecs, and upstream `tyrian.sav` import
- custom-episode activation, Endless pools, save dependencies, and transfer envelopes
- custom weapon serialization and capacity limits
- modifier, perk, course, and economy invariants
- Arcade lives, hull, rear-gun, and HUD geometry
- online mode flags, ownership, and lobby settings
- desync guards: canary coverage, draw paths that must leave the simulation
  alone, skipped background layers, online debug keys, and recovery relocation
  for every two-ship layout
- both-player Endless perks, shops, drives, deaths, and course choices
- deterministic generation across 768 course seeds
- every shipped level's event script through the loader, its data corrections,
  and how far each spawn group's pieces spread
- hostile values, truncation, string termination, and guard bytes
- PNG reading and writing, including truncated and corrupted files, and sprite
  editor image round trips for every palette colour
- SSE2, NEON, and SWAR pixel loops against frozen scalar copies
- threaded frame rows, Render Threads tuning, band replay, and scalers whose
  texture lock fails

The suite lives in `src/qa/`. `qa_run_unit_suite` in `qa.c` calls the checks in
a fixed order, which some of them rely on. The topic files beside it, such as
`qa_course.c`, `qa_saves.c`, and `qa_render.c`, hold the checks, and
`qa_internal.h` declares them. `qa_net.c` is the in-game side of the network
fault suite.

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

Four co-op flights write synthetic demos at run time. Player one follows the
demo's key stream; player two flies a seeded random walk through the rollback
input tuples. Each flight runs 1,500 ticks, and every tick is replayed from a
snapshot up to ten frames back. A fifth flight runs at detail levels 4 and 3,
hashes every registry item on each verified frame, and fails when any
simulation item differs.

| Option | Effect |
| --- | --- |
| `--test-replay-file FILE` | Play FILE in place of the shipped demo |
| `--test-replay-coop` | Add player two as a co-op Campaign ship scripted through input tuples |
| `--test-replay-depth N` | Replay from up to N frames back, cycling 1 to N |
| `--test-replay-trace FILE` | Write each registry item's hash for every verified frame |
| `--test-replay-seed N` | Seed player two's scripted input |
| `--test-replay-detail N` | Run at detail level N (1 to 6) |

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
| 12 | endless-zones | Ten zones cover the modifier registry and wallet parity once both purchases land |
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
| 23 | custom-endless-converge | The host pool reaches the joiner without removing local extras |
| 24 | custom-resume-converge | Split save dependencies are restored in both directions |
| 25 | custom-episode-converge | Campaign start transfers the selected container |
| 26 | custom-lifecycle | A collection survives role swaps and rejects incomplete resumes |
| 27 | custom-device-transfer | Level-carrying Transfer items stream complete collections by pull and direct push |
| 28 | custom-disconnect-save | A disconnect save keeps both players and re-hosts cleanly |
| 29 | campaign-desync-recovery | Deliberate corruption is detected and repaired in campaign co-op |

Each peer runs in its own temporary directory. The runner drains output while
the processes run so a full pipe cannot stall network service.

`fixtures/clv_ep1.clv` through `clv_ep3.clv` are distinct valid containers used
by scenarios 23 through 28. Keep their names and bytes stable; the tests compare
their identities and complete contents.

## Save fixtures

`fixtures/upstream_tyrian.sav` is the `tyrian.sav` that ships with the freeware
Tyrian 2000 data, in the layout OpenTyrian, OpenTyrian2000, and DOS Tyrian
write. The suite imports it and checks known slot and high-score values, so
keep its bytes stable. Its name keeps it clear of the repository's `tyrian.sav`
ignore rule.
