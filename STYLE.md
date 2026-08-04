# Project style

These rules apply to code and documentation owned by OpenTyrian2000 Engaged.
Their purpose is readable code and reviewable diffs, not uniform formatting of
the project's history.

## Scope

- New code follows this guide. Existing code should be brought into line only
  when it is already being changed or in a separate style-only commit.
- Preserve the local style of imported upstream code. Do not rewrite license or
  copyright headers.
- `stuff/` and `src/midiproc/` are third-party code. Do not reformat or rewrite
  them except when maintaining an explicit local patch.
- Keep style-only and behavioral changes in separate commits. A style pass must
  not change declarations, types, constants, control flow, evaluation order,
  data layouts, or serialized values.
- Do not rename a legacy interface merely to make it match newer code.

## Formatting

- Use UTF-8 and LF line endings except where `.editorconfig` specifies a platform
  format.
- Indent C and headers with tabs displayed at four columns. Use spaces only for
  continuation alignment within an indented line.
- Use Allman braces. Braces are required for a multi-statement branch or loop.
- Indent `case` labels to the same depth as their `switch`.
- Put one statement on each line. Do not combine unrelated declarations.
- Target 100 columns and do not exceed 120 in new code. Long URLs, generated
  data, tables, and declarations that become less readable when wrapped are
  exceptions.
- Use blank lines to separate logical phases, not individual statements.
- Keep alignment local. Do not create wide columns whose maintenance causes
  unrelated lines to change.
- Normally order includes as: the file's own header, closely coupled internal
  headers, other project headers, then C or platform headers. Keep project and
  system groups separate. Required include order takes precedence and should
  have a short explanation.
- Do not sort includes automatically; several platform headers are order-sensitive.

The root `.clang-format` describes these defaults. It is an aid, not authority:

```sh
git clang-format <base-commit>
```

Run it only on changed lines and review the result. Never run it across the
whole tree.

## Naming and types

- Preserve established names in legacy code. New names follow the owning
  subsystem's convention.
- Public functions and global state use a module-specific prefix. File-private
  functions and state are `static`.
- Constants, macros, and enum values use `UPPER_SNAKE_CASE`.
- Use descriptive names. Single-letter names are limited to small loops,
  coordinates, and established mathematical notation.
- Include units in names when they are not obvious, such as `_ms`, `_ticks`,
  `_px`, or `_bytes`.
- Use `bool` for new boolean state. Use fixed-width or SDL integer types for
  disk, network, and other binary formats.
- Prefer typed constants to magic numbers. A literal is fine when its meaning is
  obvious at the call site and it is not a shared rule.
- Add `const` where it documents ownership or prevents accidental mutation.

## Structure and state

- Prefer early returns for invalid input and exceptional paths.
- Extract a helper when it names one coherent rule or operation. Do not split a
  function solely to reduce its line count.
- Keep state owned by one module. Expose operations through its header instead
  of scattering direct writes across the project.
- Keep simulation and presentation state separate. Simulation-affecting code
  must use the project's deterministic RNG and math paths and participate in
  rollback state where applicable.
- Treat save and network layouts as append-only, versioned interfaces. Reordering
  or resizing fields requires migration or a protocol version change.
- Limit preprocessor conditionals to the smallest practical scope. Shared code
  should remain shared when only a platform boundary differs.
- Do not disguise a structural refactor as cleanup. Refactors need their own
  behavioral verification.

## Comments

- Comment invariants, ownership, units, compatibility constraints, and reasons
  the obvious implementation is wrong.
- Do not narrate the next line or restate a clear name.
- Do not write long comment walls. Keep comments to one to three direct
  sentences. If an explanation needs more space, put it in the relevant section
  of `notes.md` and leave only a short summary or section pointer beside the code.
- Wire layouts, persistent formats, and public API contracts may use longer
  comments when every detail is necessary at the call site.
- Use sentence case and neutral wording. Avoid rhetorical questions, bug-story
  prose, ALL-CAPS emphasis, decorative banners, jokes, and claims such as
  "obvious", "simple", or "guaranteed" unless they are part of a contract.
- Do not duplicate documentation. Keep one authoritative explanation and link
  or refer to it from other locations.
- Remove stale comments. Do not preserve an obsolete explanation as history.
- A TODO must name a concrete missing action and why it remains. Include an issue
  reference when one exists.

## Documentation

- `README.md` is the concise project overview and build entry point.
- `GUIDE.md` documents current player-visible behavior.
- `notes.md` records maintainer invariants, compatibility constraints, and
  non-obvious implementation decisions.
- Platform build and packaging details belong in the corresponding platform
  README.
- Describe current behavior in present tense. Keep implementation history only
  when it explains a compatibility or migration requirement.
- Use exact menu names, paths, units, modes, and platform names.
- Do not copy the same explanation into player and maintainer documentation.

## Verification

Before finishing a style-only change:

1. Inspect `git diff --ignore-space-at-eol` and confirm the scope.
2. Check that source edits are whitespace- or comment-only. For comment cleanup,
   compare the files with comments stripped or use an equivalent token check.
3. Build the affected target. Use the root build script for PC changes:

   ```powershell
   .\build-all.ps1 -Target PC -Configuration Release -NoCollect
   ```

4. Test the actual game path when a refactor can affect runtime behavior. A
   successful build does not prove gameplay, save migration, netplay, or hardware
   behavior.
5. Leave unrelated worktree changes, generated files, and vendor code untouched.
