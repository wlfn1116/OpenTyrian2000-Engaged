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
- the online mode split across every reachable combination of the two-player, arcade and co-op
  flags, the wallet each machine spends in all three online modes, arcade's immunity to the
  co-op credit settings, the two-wallet campaign economy, the co-op campaign record board, the
  strings the online menus print, and pause being refused in all three online modes;
- online Endless co-op as a matrix: credit mode against Double Pickups against Scavenger
  stacks against which machine is asking; all sixteen pairings of what the two ships are
  flying against Combo Feed and against who fired the killing shot; per-player perk stacking
  and its caps; the outpost and every E-Shop button from both machines, with their refusal
  gates and per-player price escalation and RNG streams; downed, revive-token and
  revive-at-outpost states against all three run modes; reactive-danger targeting; all four
  course-chooser modes; the co-op wire block; and whole-session scenarios that combine them;
- 768 deterministic course seeds across early, milestone, and deep-run depths, checking
  structural/gameplay RNG isolation, repeatability, launchable levels, unique display-safe names,
  danger/payout ordering, modifier compatibility, exact milestone rank distributions, and payout bounds;
- bounded shipped-demo replays with zero rollback divergence and fixed registered-state hashes;
- two real UDP peers behind a deterministic proxy that injects latency, loss, reordering,
  duplication, and a complete traffic pause, in three scenarios on their own ports:
  - *base*: the reliable channel round-trip, the Relaxed death prompt, and the original
    campaign-shop and Endless-outpost rendezvous sequences;
  - *campaign*: two complete and different loadouts converging in both directions, six rounds
    of interleaved purchases from both machines at once, a save checkpoint that moves nothing,
    and a rendezvous where one machine finishes long before the other;
  - *endless*: both ships holding different drives, paid charges, hull tiers, tokens, debts and
    perk slates, all crossing intact and combining the same way on both machines; Individual
    credit with Double Pickups; one ship down while the other flies on, and its revive at the
    outpost; the charted sector index surviving the rendezvous; the Relaxed both-down prompt;
    and the whole run record adopted by the joiner.

  Run one on its own with `--scenario N`. Each peer asserts what it should be seeing of the
  other, so a field that crosses in only one direction fails on the side that did not get it.

Regenerate save fixtures only when intentionally changing the migration corpus:

```sh
python3 testing/generate_save_fixtures.py
```

Replay hashes are compatibility fixtures. Update them only after reviewing an intentional
simulation change on every desktop CI platform.
