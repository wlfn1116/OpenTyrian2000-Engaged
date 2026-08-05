# Automated correctness tests

The suite exercises production code through the game's hidden QA command-line entry points.
It is intended to run headlessly with the freeware Tyrian 2000 data beside the build.

`make test TEST_DATA=/path/to/data` runs:

- rollback snapshot and wire-restore checks;
- save migration fixtures for every supported Endless format (v3 through v20), including
  load -> current save -> reload stability and malformed/truncated/oversized input probes;
- resync serialization property tests and malformed rollback-packet fuzz cases;
- custom-weapon editor serialization, malformed input, and array-capacity properties;
- 768 deterministic course seeds across early, milestone, and deep-run depths, checking
  repeatability, launchable levels, modifier compatibility, and payout bounds;
- bounded shipped-demo replays with zero rollback divergence and fixed registered-state hashes;
- two real UDP peers behind a deterministic proxy that injects latency, loss, reordering,
  duplication, and a complete traffic pause.

Regenerate save fixtures only when intentionally changing the migration corpus:

```sh
python3 testing/generate_save_fixtures.py
```

Replay hashes are compatibility fixtures. Update them only after reviewing an intentional
simulation change on every desktop CI platform.
