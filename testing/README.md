# Automated correctness tests

The suite exercises production code through the game's hidden QA command-line entry points.
It is intended to run headlessly with the freeware Tyrian 2000 data beside the build.

`make test TEST_DATA=/path/to/data` runs:

- rollback snapshot and wire-restore checks;
- save migration fixtures for every supported Endless format (v3 through v20), including
  load -> current save -> reload stability and malformed/truncated/oversized input probes;
- resync serialization property tests and malformed rollback-packet fuzz cases;
- fixed-width network settings and resumed-save round trips, guard-byte checks, hostile-value
  clamps, string termination, and restoration of local preferences;
- custom-weapon editor serialization, malformed input, import-registry validation, port/sidekick
  materialization bounds, and exact bullet, charge, and library capacities;
- modifier, theme-name, and perk-registry integrity (unique persisted bits and names, visible
  font glyphs, menu-width limits, valid combinations, clamped stacks, and bounded distinct offers);
- cash-ledger conservation across ordinary spending, temporary upgrade balances, refunds,
  trade-ins, duplicate commits, and over-wallet debits;
- arcade life/hull/rear-gun scaling, damage-ratio preservation, alias avoidance, and HUD ammo
  gauge segment bounds;
- legacy and per-difficulty record derivation, effect gates that prevent Endless modifiers from
  leaking into normal play, and non-overlapping render/object-pool identity ranges;
- 768 deterministic course seeds across early, milestone, and deep-run depths, checking
  structural/gameplay RNG isolation, repeatability, launchable levels, unique display-safe names,
  danger/payout ordering, modifier compatibility, exact milestone rank distributions, and payout bounds;
- bounded shipped-demo replays with zero rollback divergence and fixed registered-state hashes;
- two real UDP peers behind a deterministic proxy that injects latency, loss, reordering,
  duplication, and a complete traffic pause.

Regenerate save fixtures only when intentionally changing the migration corpus:

```sh
python3 testing/generate_save_fixtures.py
```

Replay hashes are compatibility fixtures. Update them only after reviewing an intentional
simulation change on every desktop CI platform.
