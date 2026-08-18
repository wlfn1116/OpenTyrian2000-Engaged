# Project style

This guide applies to code and documentation maintained by OpenTyrian2000
Engaged.

## Scope

- Keep style-only work separate from behavior changes.
- Do not change declarations, types, constants, control flow, evaluation order,
  layouts, or serialized values during a style pass.
- Preserve license headers, upstream Doxygen comments, and established legacy
  interfaces.
- `stuff/` and `src/midiproc/` are third-party code. Change them only as part of
  a documented local patch.
- Leave generated files alone unless their generator or source data changed.

## C and headers

- Use UTF-8 and LF unless `.editorconfig` says otherwise.
- Indent with tabs, displayed at four columns. Use spaces for continuation
  alignment.
- Use Allman braces. A branch or loop with more than one statement needs
  braces.
- Indent `case` labels with their `switch`.
- Keep one statement per line and target 100 columns. New code should not exceed
  120 columns except for URLs, generated data, or declarations made worse by
  wrapping.
- Use blank lines between phases of a function, not between individual
  statements.
- Keep alignment local. Wide alignment makes unrelated lines move together.
- Group includes as the file's own header, related project headers, other
  project headers, and system headers. Required platform order wins.
- Do not sort includes automatically.

The root `.clang-format` describes these defaults. Run it only on lines already
being changed, then inspect the result:

```sh
git clang-format <base-commit>
```

## Names and types

- Keep established names in legacy code. New names follow their subsystem.
- Prefix public functions and global state by module. Keep file-local symbols
  `static`.
- Use `UPPER_SNAKE_CASE` for macros, constants, and enum values.
- Include units in names when they are not clear from context, such as `_ms`,
  `_ticks`, `_px`, or `_bytes`.
- Use `bool` for new boolean state. Use fixed-width or SDL integer types for
  disk, network, and other binary formats.
- Prefer typed constants to unexplained literals.
- Add `const` when it clarifies ownership or prevents mutation.

## Structure and state

- Return early on invalid input and exceptional paths.
- Extract helpers that name a coherent rule. Function length alone is not a
  reason to split code.
- Give state one owner and expose operations through that module's header.
- Keep simulation and presentation state separate.
- Simulation code uses deterministic RNG and math and must participate in
  rollback where required.
- Treat save and wire layouts as versioned, append-only interfaces.
- Keep platform conditionals as narrow as practical.
- Give structural refactors their own change and their own verification.

## Comments

Write a comment when the code cannot state the rule by itself. Good subjects
include invariants, ownership, units, compatibility constraints, and surprising
choices.

- Use one to three plain sentences.
- Do not narrate the next line or translate a clear identifier into English.
- Delete stale explanations instead of recording their history beside the fix.
- Keep one authoritative explanation. Put longer maintainer context in
  `doc/notes.md` and leave a short pointer near the code.
- Longer comments are acceptable for wire layouts, persistent formats, and
  public API contracts when the detail is needed at the call site.
- A TODO must name the missing work and why it remains. Include an issue when
  one exists.

## Prose

Write like a maintainer talking to another maintainer or player. Be specific.

- Prefer short paragraphs, lists, and tables over dense blocks.
- Cut canned introductions, repeated summaries, rhetorical questions, sales
  language, and claims that add no information.
- Avoid bug-story narration. Keep history only when it explains a compatibility
  or migration rule.
- Do not use em dashes or formulaic contrasts such as “not X, but Y.”
- Use headings and emphasis for navigation, not decoration.
- Do not call work obvious, simple, easy, or guaranteed unless that word states
  a tested contract.

## Documentation map

- `README.md` is the project overview and build entry point.
- `GUIDE.md` explains player-visible features, controls, and limitations.
- `doc/notes.md` records maintainer invariants and compatibility constraints.
- Platform build and packaging details belong in the platform README.
- Test runners and scenarios belong in `testing/README.md`.

Keep behavior, defaults, menu names, paths, controls, supported platforms, and
formats in sync with the code. Verify claims against the implementation. Player
docs should omit private symbols, internal data flow, bug history, and exhaustive
edge cases unless a player needs them to use or troubleshoot a feature.

## Before committing a style pass

1. Inspect `git diff --ignore-space-at-eol`.
2. Confirm source edits are comment- or whitespace-only. Compare tokens with
   comments stripped when the diff is large.
3. Build the affected PC target:

   ```powershell
   .\build-all.ps1 -Target PC -Configuration Release -NoCollect
   ```

4. Test any runtime path whose surrounding code changed.
5. Check for stale facts, duplicate explanations, long paragraphs, em dashes,
   and imported or generated files in the diff.
