# Project style

These rules apply to code and documentation maintained by OpenTyrian2000
Engaged.

## Boundaries

- Keep style-only changes separate from behavior changes.
- During a style pass, do not change declarations, types, constants, control
  flow, evaluation order, layouts, or serialized values.
- Preserve license headers, upstream Doxygen, and established legacy
  interfaces.
- `stuff/` and `src/midiproc/` are third-party. Change them only through a
  documented local patch.
- Change generated files through their generator or source data.

## C and headers

- Use UTF-8 and LF unless `.editorconfig` says otherwise.
- Indent with tabs at four columns. Use spaces for continuation alignment.
- Use Allman braces. Add braces around a branch or loop with multiple
  statements.
- Indent `case` labels with their `switch`.
- Keep one statement per line.
- Target 100 columns. New code may reach 120 for URLs, generated data, or a
  declaration made less readable by wrapping.
- Put blank lines between phases of a function, not individual statements.
- Keep alignment local; wide alignment makes unrelated lines move together.
- Group includes as: the file's header, related project headers, other project
  headers, then system headers. Platform-required order wins.
- Do not sort includes automatically.

The root `.clang-format` carries these defaults. Run it only on lines already
being changed, then inspect the result:

```sh
git clang-format <base-commit>
```

## Names and types

- Keep established names in legacy code. New names follow their subsystem.
- Prefix public functions and global state by module. Keep file-local symbols
  `static`.
- Use `UPPER_SNAKE_CASE` for macros, constants, and enum values.
- Include units such as `_ms`, `_ticks`, `_px`, or `_bytes` when they are not
  clear from context.
- Use `bool` for new boolean state.
- Use fixed-width or SDL integer types for disk, network, and other binary
  formats.
- Prefer typed constants to unexplained literals.
- Add `const` when it clarifies ownership or prevents mutation.

## Structure and state

- Return early on invalid input and exceptional paths.
- Extract helpers that name a coherent rule. Function length alone is not a
  reason to split code.
- Give state one owner and expose operations through that module's header.
- Keep simulation and presentation state separate.
- Simulation uses deterministic RNG and math and joins rollback where required.
- Treat save and wire layouts as versioned, append-only interfaces.
- Keep platform conditionals narrow.
- Give structural refactors their own change and verification.

## Comments

Comment the rule the code cannot state: an invariant, owner, unit,
compatibility constraint, or surprising choice.

- Use one to three plain sentences.
- Do not narrate the next line or restate a clear identifier.
- Delete stale explanations instead of keeping a bug history beside the fix.
- Keep one authoritative explanation. Put longer context in `doc/notes.md` and
  leave a short pointer near the code.
- Wire layouts, persistent formats, and public API contracts may be longer when
  callers need the detail in place.
- A TODO must name the missing work and why it remains. Link an issue when one
  exists.

## Prose

Write as a maintainer speaking to a maintainer or player. Be specific.

- Prefer short paragraphs, lists, and tables to dense blocks.
- Cut canned introductions, repeated summaries, rhetorical questions, sales
  language, and claims without information.
- Keep history only when it explains compatibility or migration.
- Do not use em dashes or formulaic “not X, but Y” contrasts.
- Use headings and emphasis for navigation, not decoration.
- Do not call work obvious, simple, easy, or guaranteed unless it is a tested
  contract.

## Documentation map

| File | Purpose |
| --- | --- |
| `README.md` | Project overview and build entry point |
| `GUIDE.md` | Player-visible features, controls, and limits |
| `doc/notes.md` | Maintainer invariants and compatibility rules |
| Platform READMEs | Platform build and packaging instructions |
| `testing/README.md` | Test runners, fixtures, and scenarios |

Keep behavior, defaults, menu names, paths, controls, platforms, and formats in
sync with the code. Player documentation should avoid private symbols, internal
data flow, bug history, and exhaustive edge cases unless they help someone use
or troubleshoot the feature.

## Before committing a style pass

1. Inspect `git diff --ignore-space-at-eol`.
2. Confirm source edits are comments or whitespace only. For a large diff,
   compare tokens after stripping comments.
3. Build the affected PC target:

   ```powershell
   .\build-all.ps1 -Target PC -Configuration Release -NoCollect
   ```

4. Test any runtime path whose surrounding code changed.
5. Check for stale facts, duplicate explanations, long paragraphs, em dashes,
   and imported or generated files in the diff.
