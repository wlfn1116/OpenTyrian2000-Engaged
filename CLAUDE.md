# Project rules

## Comments: keep them short, put depth in `notes.md`

This is a release-ready codebase. In-code comments say **what** and **why-in-one-breath**;
the reasoning, derivations and history live in `notes.md` at the repo root, organised by
subsystem. Write the short comment at the site and the depth in `notes.md §Section`.

**Before writing an explanation, check whether `notes.md` already says it.** Comment walls
here have twice grown back by re-deriving prose that `notes.md` already carried in more
detail. Duplicating it is the failure mode, not a safe redundancy.

### Length

- Default **1–4 lines**. Six is a lot. Past that, you are writing `notes.md` in the wrong file.
- Longer blocks (8+) are legitimate only for a genuine **contract or table doc**: the fixed
  consequences of a shared entry point, a tuning-knob table, a header's ownership map. Even
  then, state the contract — don't argue for it.
- No comment line past ~110 columns.

### Never write

- **Tuning or changelog history.** No "used to", "previously", "earlier revisions", "the first
  attempt", "was 100 -> 300". Git has this. If a past mistake must not recur, state the rule as
  a present-tense constraint: *"Read the RAW tide here, never the adjusted figure"* — not
  *"reading the adjusted figure used to let one sector decide the next"*.
- **Rhetorical shouting.** No `THE COOLDOWN IS THE WHOLE BALANCE`, `THE DRAIN ALONE IS NOT
  ENOUGH`, `THE TUNING KNOBS ARE HERE`. Reserve all-caps for a single load-bearing word
  (`RAW`, `NOT`, `ORDER MATTERS`) and use it rarely.
- **Play-test narration.** No "which is exactly how it read in play", "felt dead", "looked
  inert", "brutal, not unwinnable", "earns its keep", "tuned by play-testing rather than theory".
- **Essayistic build-up.** No "Two things make this simple…", "…and this is the whole reason",
  "It is worth noting". Lead with the fact.
- **Restating the code.** If the line below says it, don't say it above.
- AI-register filler: *robust, seamless, comprehensive, leverage, utilize, essentially,
  basically, in order to, simply put*.

### Do write

- The non-obvious constraint, in present tense: what breaks if this is changed, what must stay
  in step with what, which of two similar things this is.
- Units, encodings and sentinels (`hundredths of a tick`, `shotDmg is an ENCODED byte`).
- A `notes.md §Section` pointer when there is more to know. **Verify the section exists** —
  stale pointers have shipped before.

### Leave alone

Upstream OpenTyrian / vendored comments are not ours to restyle: `config_file.h`, `font.c`
Doxygen, `varz.c`'s twiddle sheet, `animlib.c`, `destruct.c`'s file notes, the Jason-era
`game_menu.c` prose, `src/midiproc/`, `video_scale_hqNx.c`. GPL headers stay.

### Deduplicate

If the same explanation is needed in three places, write it once and cross-reference
(`same pattern as X; see the note there`). Near-verbatim repeats are slop by volume.

## Related conventions

- `notes.md` (repo root) is the canonical developer-notes/pitfalls doc.
- `GUIDE.md` (repo root) is the player-facing manual and must be updated in the same change as
  any system it documents.
- Rebuild through `./build-all.ps1` (`-Target PC` for a fast check), never a bare MSBuild.
