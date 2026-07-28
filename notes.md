# OpenTyrian2000 Engaged developer notes

Design notes and known pitfalls for the systems this fork adds on top of upstream
OpenTyrian2000. The source keeps short comments at the relevant sites; the full
reasoning lives here. Code comments reference these sections as `notes.md §Name`.

## Build & targets

- `build-all.ps1` is the build entry point. It builds PC (MSVC), Switch
  (devkitA64 `.nro` via `switch/build.sh`) and Vita (VitaSDK `.vpk` via
  `vita/build.ps1`), then collects successful
  deliverables into `build\`. Targets can be selected individually, `-Clean`
  performs a clean rebuild, and one target failing does not stop the others
  unless `-FailFast` is used.
- Collected filenames use a platform suffix (`Win64`, `Win32`, `Switch`, or
  `Vita`) without embedding the version. The source outputs retain the names
  expected by their toolchains.
- The PC `.exe` must live next to `data\` to run. `build\` is a collection of
  deliverables, not a runtime layout.
- Switch: `DEVKITPRO` must be in msys form (`/opt/devkitpro`); the build entry
  point neutralizes an inherited Windows-style value before invoking devkitPro's
  bash. Batch files need CRLF line endings.
- The Vita build runs from PowerShell (native cmake + ninja) rather than MSYS
  bash. The native tools need native `D:\...` paths (MSYS mangles them into
  `/d/...`), and the shell-hostile data filenames (`shapes).dat`, `newsh%.shp`)
  break per-file argument handling in `vita-pack-vpk` under a POSIX shell.
- `WITH_MIDI` is x64-only on Windows (the vendored midiproc and FluidSynth libs
  are x64), so Win32 builds compile without it.
- The crash logger (`crashlog.c`) is Windows-only; console ports get the stub paths.

### Warning flags

Every target builds warning-free (MSVC x64/Win32, Debug and Release, plus both
console toolchains); keep it that way, and prefer fixing the code to widening
these lists.

- MSVC compiles the engine at `EnableAllWarnings` (`/Wall`) with a
  `DisableSpecificWarnings` list in the `.vcxproj` for the checks that only fire
  on the DOS-era idioms this engine is built from (implicit conversions, unused
  padding, Spectre notes).
- MSVC also needs `/source-charset:utf-8`: the sources are UTF-8 without a BOM
  (`.editorconfig`), and without the switch `cl` assumes the system code page and
  reports C4819 on every comment containing an em dash or `§`. Source charset
  only -- no string literal holds a non-ASCII character, so the execution charset
  stays the system default and codegen does not change.
- Third-party headers are silenced at the include, not project-wide, so our own
  code keeps the check: see the `#pragma warning(push/disable/pop)` pairs around
  `<dbghelp.h>` in `crashlog.c` (C4255) and `<fluidsynth.h>` in `fluid_music.c`.
- `main()` keeps an unreachable `return 0;` after `JE_tyrianHalt()` for compilers
  that don't infer the exit. MSVC's C4702 is a code-generation warning, so the
  `#pragma warning` pair has to sit *outside* the function -- the state that
  counts is the one in effect at the closing brace.
- MSVC's `/analyze` is NOT part of the build. Run it separately if you want it, and
  expect noise: the two dominant categories are C6244/C6246 (a local shadowing one
  of the engine's one-letter globals -- `b`, `temp`, `x`, `y`, whose compiler
  equivalents 4456/4457/4459 are already suppressed on purpose) and C28301 (SAL
  annotation mismatches inside the Windows SDK's own headers). Range findings on
  indexed writes are usually the analyzer losing a bound across an opaque call, not
  a defect -- take one indexed reference right after the bounds check and it goes
  quiet (see `endlessScalingOverrideStock`).
- All three gcc-based builds pass `-Wno-format-truncation`. The engine's
  `snprintf` calls fill fixed-width on-screen fields where a clamped tail is the
  intended result, and gcc cannot see the real value ranges (it assumes any `int`
  can print 11 digits). `-Wstringop-truncation` is *not* disabled: use
  `SDL_strlcpy` rather than `strncpy` + a manual terminator.

## Smooth motion (render list)

The world simulates at the fixed 35Hz tick. Every draw is recorded into a render
list (`render_list.c`) and replayed at the display rate with positions
interpolated between the previous and current tick. Key invariants:

- Anything that moves in a menu or sim needs an `rl_current_id` tag, or it steps
  at the recording tick rate instead of interpolating. This bit the shop's weapon
  preview twice.
- Present timing uses the performance counter, not `SDL_GetTicks`. Ms-quantized
  elapsed time injects several percent of alpha error per frame at high refresh
  rates, which shows up as micro-judder.
- Shots and attachments: shot ids (`rl_id_extrapolates`) extrapolate forward at
  the render rate instead of interpolating a tick behind. They lead by velocity
  plus acceleration (the predicted next displacement), so a decelerating shot
  lands on its next-tick position instead of overshooting and snapping back.
  They are immune to slot recycling because `dx`/`dy` hold the shot's own
  recorded velocity, not a prev/cur diff. Ship-attached shots (laser, main pulse)
  follow the ship via `ship_attach`. An attached shot that also moves on its own
  (the orbiting asteroid-killer, weapon 104) records ship-move + own-move and
  subtracts the ship velocity to recover the interpolatable own-move part.
- The exact/residual replay (`rl_replay`, `use_override` off) must reproduce the
  recorded frame byte-exact; all sub-pixel smoothing is gated on `use_override`.

### Variable-timestep (VT) player ship — `tyrian2.c`

The player ship alone is simulated at the render rate with real dt while the
world stays at 35Hz. The integrator owns the ship: `JE_playerMovement`'s
movement/velocity integration is skipped (guarded by `vt`), player.x/y are
written each frame, and the sprite is driven through the render-list ship
override. The 35Hz sim still reads player.x/y for firing and collisions, so
sprite and hitbox move together and input latency drops below a tick.

- Single-player only. Determinism is broken by design, so VT is force-disabled
  for demo record/playback and network games.
- Control feel is a fresh accel/friction model; the original
  position-as-acceleration model can't be dt-scaled. `VT_DIRECT` exists because a
  pure momentum model feels laggy on a controller (mouse hides it by direct
  positioning).
- The integrator must step on every present-loop iteration, not only rendered
  frames. Skipping the iteration that triggers a sim tick discards that
  iteration's elapsed time, and the ship visibly stutters even at a solid 60fps.
- The VT ship's render-rate `poll_joystick` consumes press-edges, so menu/pause
  handling must get its edge data from `vt_ship_step`'s capture or controller
  menus break (this happened — see git history).

### Smoothie levels (ice/water/lava filters) — `tyrian2.c`, `render_list.c`

The per-pixel filters are frame-feedback effects, so smooth presentation uses two
passes and two buffers:

- `render_gs` — persistent background plasma. Advances one filter step per tick
  (alpha=1, backgrounds at tick positions), evolved per displayed frame at
  interpolated positions. Never holds entities; they would feed back and smear.
- `smoothie_frame` — per-frame display buffer: fresh copy of `render_gs` plus
  interpolated entities plus residual overlays.
- The BG pass applies the filter once, at full strength, with feedback on. Full
  strength matters: a partial blend leaves the background perpetually dim, and an
  accumulating buffer over-advances into runaway feedback. The filters are
  contractive toward the freshly-drawn background, so the plasma stays bounded
  without re-anchoring, and a sub-pixel-shifted background is safe.
- The residual delta diffs game_screen against the pre-overlay snapshot
  (VGAScreen2). Any full-screen grade — a colour flare (`levelFilter != -99`) or
  a brightness-only flash (`levelBrightness != -99`) — must be applied to the
  snapshot too, or it bakes into the residual and freezes the playfield at 35fps
  for its duration. `JE_filterScreenApply` self-skips `-99` components, so steady
  state is a no-op.

### Sub-pixel parallax & scroll tracking — `backgrnd.c`, `render_list.c`, `tyrian2.c`

- Horizontal: each layer's un-floored float offsets are captured at the draw site
  (`bg_layer_dx`/`bg_layer_frac`). Rows are replayed at recorded x plus
  `(frac - dx*inv)`; float endpoints make the pan continuous across tick
  boundaries while still tracking the ticks enemies anchor to.
- Parallax amplitude (all layers) — gated by the **Extra Parallax** toggle
  (Enhancements menu, `extraParallax` in config, below Debug Mode). OFF computes the
  stock `w_f = (1-u)*72` over the `[40,363]` normalization and makes `bg_clamp_map` a
  no-op, so the parallax + draw path matches stock — with ONE deliberate exception:
  when the ship is pinned fully left (`u<=0`), the bg2 overlay (EP1 TYRIAN clouds etc.)
  has its sub-pixel scroll fraction snapped to 0 (`mapX2Ofs_f = (float)mapX2Ofs`).
  Layer 2 is one strip at a single X offset, so it can only translate uniformly — a 1px
  shift trades the left gap for a right one. The real artifact is that at the far-left
  `mapX2Ofs` is 36 (int) / 36.667 (float), and the smoothed replay rounds that 0.667px
  fraction UP to a whole pixel → clouds drawn 1px right. Snapping the fraction makes the
  smoothed render land crisply on the integer pixel (1px left of the rounded-up spot),
  no fractional spill on either edge. Only the render-list interpolation path (Smooth
  Motion on) is affected; integer `mapX2Ofs` + glued layer-2 enemies untouched.
  `bg2CrispLeft` flag in the parallax block.
- Layer-2 right-edge coverage guard (both modes). The 14-tile (336px) bg2 strip is
  1px too narrow to reach the playfield's right edge (col `PLAYFIELD_RIGHT` = 322)
  once the parallax pushes `mapX2Ofs` to its far-right value `-2`: the strip draws at
  screen `x = mapX2Pos + PLAYFIELD_X_SHIFT = -14` and ends at col 321 → 1px cloud gap
  on the right. The near layer only reaches `-1` (`x=-13`, just covers 322), so clamp
  `mapX2Ofs` (and `mapX2Ofs_f`) to a floor of `-1` so the strip always reaches the
  right edge. ~1px pan freeze over the last few px of ship travel; both int and float
  clamped so the smoothed replay agrees. Applied after `mapXOfs` is derived (near layer
  keeps its own `-1` floor independently). The near map is 14
  tiles (336px) but the window is only 299px and the compositor crops the first 24px,
  so ~1 tile stayed permanently off the left. `mainint.c`'s parallax block keeps the
  original coupled 4:2:1 ratio (`mapX3Ofs = tempW`, `mapX2Ofs = (tempW-17)*2/3`,
  `mapXOfs = mapX2Ofs/2`) driven by a shared float `w_f`; ON reshapes `w_f` so a strafe
  sweeps *every* layer across its full width. Bound ground enemies ride
  `mapXOfs`/`mapX2Ofs`/`oldMapX3Ofs` via `tempMapXOfs` and stay glued (they slide much
  further now — intentional).
- ON pans the NEAR layer FLUSH at both extremes (0 px of map off either edge at the
  walls). `mapXOfs` sweeps `36` (far-left: plane-px 0 at the window's left edge =
  `PLAYFIELD_LEFT - PLAYFIELD_X_SHIFT`) down by the slack (`336 - 299 = 37`) to `-1`
  (far-right: the map's last px at the window's right edge). Crucially it's normalized
  over the ship's ACTUAL travel `[SHIP_LEFT_MARGIN, PLAYFIELD_WIDTH-SHIP_RIGHT_MARGIN]`
  = `[29,303]`, not the stock `[40,363]` u (which only reaches ~0.81 at the right wall
  → `mapXOfs` stuck at ~2, i.e. the ~4px of map left off the right edge). `w_f` is
  back-derived as `3*near + 17` so the mid/deep layers keep the coupled ratio and still
  over-pan (`mapX3Ofs → 125`, `mapX2Ofs → 72` at far-left). At `mapXOfs == 36`,
  `mapXPos == 12`, so the near layer's lone wrong-row boundary tile spans screen
  `[0,24)`, entirely inside the crop margin — no visible seam.
- Over-pan past the map's side edge is handled by the edge mirror (next section),
  which also subsumed the old `bg_clamp_map` out-of-bounds guard.
- The legacy z-order can put a layer and its bound enemies on opposite sides of
  `JE_mainGamePlayerFunctions`, which also recalculates horizontal parallax. The
  entity may therefore carry the previous anchor while its layer carries the new
  one (or vice versa), becoming 1px misaligned at integer crossings. Finalization
  normalizes tagged enemy sprites and HP bars to the absolute anchor their layer
  actually recorded; simulation and exact/residual replay retain their original
  integer coordinates.
- `background3x1` is not a parallax ratio — it welds layer 3 to layer 1
  (`mainint.c` sets `mapX3Ofs = mapXOfs`), making it a co-scrolling foreground
  overlay for pieces that belong to layer 1's plane but must draw over the ship.
  The two therefore have to pan as one, but layer 1 always records before the
  mid-tick parallax update while `background3over == 1` records layer 3 after it,
  so the *shared* anchor was sampled a tick apart. The stock whole-pixel blit
  truncated that sub-pixel difference away; the interpolated pan resolves it, and
  a foreground welded to the terrain shows it as a horizontal seam whenever the
  player strafes (EP4 SURFACE overhangs). `draw_background_3` now pans from the
  anchor layer 1 recorded (`bg_layer_xofs[1]`) whenever `background3x1` is set,
  keeping the integer at `mapX3Ofs` since that is what its rows blit at. Bound
  Top Enemy sprites need no extra handling — the normalizer above already pulls
  them onto their layer's recorded anchor. No-op unless `background3x1` is set,
  and a no-op even then when layer 3 draws before the update (`background3over`
  0/2 — BRAINIAC, DREAD-NOT), where both anchors are already the same value.
- Vertical: the same idea via `bg_layer_dy`/`yfrac`. An integer per-tick scroll
  can't express a fractional rate (3.2 → 3,3,3,4 velocity pulse that looks like
  speeding up and slowing down); the float rate makes the displayed velocity
  constant. The published rate lags one tick, matching how `bgScrollDeltaY` is a
  draw-time position diff. Applies to every level — see §Slow-scroll smoothing.
- Keep a vertically bound command's whole-pixel draw-phase correction separate
  from the layer's fractional phase. Replay adds the whole part exactly and rounds
  only the shared fractional offset with `floor(x + .5)`. Rounding the combined
  absolute coordinate half-away-from-zero is not translation-invariant: a
  negative background row and positive enemy choose opposite pixels at exactly
  `.5`, producing the scale-1-only 1px mismatch under fractional fast scroll.
- Every enemy bank is authored and recorded at a common pre-advance phase; HP bars
  are recorded after each enemy advance and receive the inverse whole-pixel correction.
  Layer 3 terrain is the sole draw-order exception: it is recorded post-advance, so
  replay preserves its authored base step, subtracts only the modifier-added pixels,
  and applies the bound entities' lagged fractional phase. This keeps the shipped
  placement while preventing the offset from growing with scroll speed (BRAINIAC).
- Finalization canonicalizes the bound part of an enemy's vertical displacement
  to the exact float layer rate. Replay applies that layer transform independently
  on every bound command, including new, clipped-count-changed, and snapped commands;
  only enemy-local motion comes from cross-tick matching. The large-jump guard tests
  only local motion rather than rejecting a legitimate high-speed layer delta.
- The layer offset and the entity-local offset are rounded as ONE value
  (`rl_layer_y_offset` takes `par_yown100`). Rounding them separately puts each on
  its own staircase; whenever the entity moves against the scroll (own opposing the
  layer rate, e.g. own100 = -200 for a 1px/tick upward swimmer on a 1px/tick layer)
  the thresholds interleave and the sum steps down-up-down inside a single tick — a
  1px sawtooth visible at scale 1 and diluted to 1/N px at supersample N (EP5
  CORAL's launched fish around the locked boss). A resting entity (own 0) still
  gets the background rows' expression bit-for-bit, so terrain gluing is unchanged,
  and a scroll-cancelling boss (own == -rate) reduces to the constant `frac` — held
  perfectly still at every alpha even under a fractional endless boost rate.
- The smooth-scroll accumulator is quantized in hundredths. Replay reconstructs those
  integer hundredths and evaluates the canonical layer offset in double precision;
  subtracting a large rate directly in `float` can turn an exact `-N.5` tick endpoint
  into `-N.500000004` and round it one pixel backward.

### Extra Parallax edge mirror — `backgrnd.c`, `render_list.c`

- The problem: with Extra Parallax on, the mid/deep layers over-pan far enough at
  far-left that their map's left edge slides *into* the window (deep layer's plane-px 0
  reaches screen x `mapX3Ofs − 36` = 89 at full pan, i.e. 65 px inside the 24-px window
  edge; mid layer 36 − 72 → 12 px inside… up to ~2–3 tile columns). The maps are stored
  row-major, so the columns read past the row start came from the *previous row's right
  side* — a hard content seam where the layer visibly "ended".
- The fix: columns outside `[0, row_width)` re-read the same row's columns in reflected
  order and render horizontally FLIPPED (`bg_mirror_tile`): tile column `c < 0` →
  column `−1−c` flipped, `c ≥ w` → `2w−1−c` flipped. Per-tile reflection + pixel flip
  compose to the exact plane-pixel mirror `p → −1−p` about the edge, so the layer
  continues as a seamless reflection of itself (mirrored-repeat, like GL's
  `MIRRORED_REPEAT`). Same-row sourcing means vertical scroll stays coherent for free.
- Gated by the **Mirrored Layers** toggle (`mirroredLayers`, default on, config key
  `mirrored_layers`), an Enhancements row directly under Extra Parallax. Independent of
  `extraParallax`: the stock span also uncovers ~12px of layer 3's left edge at
  far-left (plane-px 0 sits at screen `mapX3Ofs − 36` = 36 vs window-left 24), so the
  mirror covers that sliver too. Off = the original draw: adjacent-row wrap seam, with
  the pointer clamp applied only under Extra Parallax (`bg_mirror_setup` reduces to the
  old `bg_clamp_map` expression, `mirror_w` 0); stock+off is byte-for-byte original.
  Adding the 11th row auto-compresses the Enhancements pitch 15→13 px (the `y<=172`
  fit rule).
- Ship cast-shadow re-centre: the shadow x rides `- mapX2Ofs + shadow_light_dx`
  (`mainint.c` player draw), where the constant is mapX2Ofs at mid-travel so the swing
  is symmetric. Extra Parallax widens the sweep (72..−1 vs stock 36..−1), moving the
  mid-travel value from 18 to 34 (u=0.5 → w_f=69.5 → (69−17)*2/3), so
  `shadow_light_dx = extraParallax ? 34 : 18`.
- Plumbing: the row blitters (`blit_background_row`/`_blend`/`_scaled`) take
  `mirror_w` (map row width in tiles: 14, layer 3 = 15; 0 = off → stock reads,
  byte-for-byte) and `col0` (map-column index of `map[0]`). `rl_rec_bg_row` stores both
  in the `RenderCmd` (`bg_mirror_w`/`bg_col0`) so every render-rate replay — 1x and
  supersampled — mirrors identically. `col0` comes from the layer's `bpPos`:
  `mapXbpPos − 1` for the 14-wide layers (the `mapYPos` pointers carry a −1 bias),
  `mapX3bpPos` for the 15-wide layer 3; the smoothie x-sync and `background3x1` welds
  inherit the right value because they redirect `bpPos` itself.
- `bg_mirror_setup` replaced `bg_clamp_map`: reflected columns never dereference before
  `mainmap[0][0]`, so the old top-of-scroll clamp (which snapped the whole call's X
  phase to column 0) is only kept as a fallback for the level-end state that re-points
  `mapYPos` at row 0 without the −1 bias. Normal top-of-scroll far-left now keeps its
  column phase and mirrors instead of popping.
- The near layer passes mirror params too but never triggers them *in-window* (it pans
  flush by design); its lone boundary tile at `[0,24)` sits in the crop margin.
- **Right-edge margin strip** (`bg_edge_px`, Mirrored Layers only). A row is
  `BG_TILE_COUNT*24` = 336 px — exactly the near map — so at the far-right pan extreme
  (`mapXOfs` −1 → row x = −13) it ends *flush* with `PLAYFIELD_RIGHT` (322) and nothing
  covers the columns past it. That is fine for what's displayed, but the lava and water
  smoothie filters **sample to the right** of the pixel they write (`src_pixel + waver`,
  `waver ∈ [−1,7]` lava / `[−1,3]` water), so along the screen's right edge they read
  the black fill instead of terrain. `waver` is a triangle wave over the linear pixel
  index (period ≈ 23 scanlines), which stamps the miss out as the sawtooth **"black
  triangles"** reported on EP1 ASSASSIN / EP4 LAVA RUN. Both filters also read their own
  row above/below at `+waver`, so black *beyond* the direct read range still bleeds
  leftward across frames (steady-state deficit ≈ ½ per waver-step). And the smooth-motion
  replay independently shifts a row up to a tick's pan (≤12 px) further left, uncovering
  columns that are actually displayed.
  Fix: append up to one more tile column to the right, clipped to `surface->w`, so a row
  starting at `x ≤ 20` now runs all the way to buffer column 355 — no black fill to the
  right of the terrain at any pan position, nothing for the filters to pick up. This is
  the first live use of the `c ≥ w` branch; the appended column is a flipped copy of the
  row's last column, sits entirely at x ≥ 323 (past the crop, invisible) at tick
  positions, and only comes into view when a replay shift pulls it in — which is the
  point. Clipped rather than whole-tile so the row can never run past `surface->w` and
  wrap onto the next scanline; `blit_background_row_scaled` derives the same 1x px count
  (`/ scale`) so hi-res replays cover the same columns. Zero when `mirror_w == 0`, so
  Mirrored Layers off is unchanged (out-of-row columns there would wrap into the next
  map row).
- Enemies bound to the layers are sprites, not tiles: they slide over the mirrored
  region unreflected (same as before; intentional).

### Slow-scroll smoothing — `endless_combat.c`, `tyrian2.c`, `render_list.c`

Levels slow the vertical scroll with the delay gate (`map*YDelayMax`, event 3:
layer 1 = 1px every 3 ticks, layer 2 = 1px every 2 ticks). The average rate is
fractional but the per-tick scroll is integer (0,0,1), so the layer freezes on
the off-ticks and jumps a whole pixel on the fire tick — choppy even with
supersampling on. TYRIAN (ep1) shows it clearly across its layers.

The fix drives the render list's float vertical rate (`bg_layer_dy`/`yfrac`) off
the layer's true average rate on every level, so the display slides at a constant
sub-pixel velocity. `endlessScrollExtraPx` computes it: `target =
fireStep/delayMax` (px*100), `frac = Σ(target − px the base actually moved)`.
Without a scroll modifier it emits no extra scroll px, only the display rate/frac,
so the sim scroll (and demos, collision, events) stays byte-identical to stock.
`bg_smooth_y_active` is on throughout gameplay.

- `fireStep` is the per-fire step, not the live `backMove` (the delay gate has
  already forced that to 0 on off-ticks): `(delayMax>1 && backMove<2) ? 1 :
  backMove`. Feeding the forced value makes `Σ(target − base)` drift.
- Byte-exact no-op on full-speed layers: an integer rate gives `frac == 0`, so
  the float path equals the old integer path exactly — no regression, no new 1x jitter. At 1x a
  fractional layer still steps 1px per N ticks, but now at the correct sub-pixel
  phase and in lockstep with its scroll-tracked enemies (they read the same
  `bg_layer_yfrac`), instead of all freezing then jumping together.
- Only layers 1/2 gate (layer 3 always scrolls `backMove3`/tick); enemies
  scroll-track layers 1/3 only, so they inherit the glue automatically.

### Endless scroll boost — `endless_combat.c` (`endlessScrollExtraPx`)

Speeding the scroll by adding whole `baseMove` lumps pulses the velocity, and on
delay-gated layers (`map*YDelayMax`, backMove forced 0 on off-ticks) the boost
itself froze and lurched. `endlessScrollExtraPx` instead returns per-layer extra
pixels so base + extra tracks a constant target rate (avg base rate ×
(1 + boost/100), minus what the base actually scrolled this tick). A signed
per-channel carry keeps the long-run average exact and never emits negative px,
so lockstep with `curLoc`/events/enemy scroll-tracking is preserved (no drift, no
boss cut-off). Call it once per channel per tick; channels 0/1/2 = background
layers 1/2/3. `rateOut`/`fracOut` expose the smooth float rate and sub-pixel
remainder for the render-list vertical interpolation. With no modifier the same
call runs at boost 0, returning 0 extra px and only the base-rate rate/frac
(§Slow-scroll smoothing).

`fixedmovey` has mixed level-script semantics. Sky formations use it as local
motion and leave it unscaled; layer-bound enemies use it to modify (often exactly
cancel) their normal layer advance. An opposing `fixedmovey`/`eyc` pair first
cancels at stock speed (BRAINIAC uses -1/+1); only the remaining fixed component
is scroll-relative. That residual is scaled directly from the batch's actual
`tempBackMove + tempScrollExtraPx`, with a signed per-enemy carry for non-integral
ratios. A residual `fixedmovey == -baseStep` object thus cancels every boosted
pixel exactly, while an already-cancelled pair cannot acquire speed-dependent
drift. Delay-gated sections use the same modifier percentage with a carry because
stock `fixedmovey` runs even on gate-off ticks.
The removed `endlessExtraScrollSteps` path used an independent pulse phase, so
its average was right but individual ticks increasingly separated objects from
their layer at higher modifiers.

The sky bank (slots 0..24, `JE_drawEnemy(25)`) has no `tempBackMove` channel:
anything attached to background layer 2 authors the ride into the enemy's own
motion, in either of two styles. GYGES's plain glass structures spawn with
`eyc == backMove2` (event dat3); its first chain structure instead rides on
`fixedmovey == backMove2` (event dat6) with the `eycc` oscillator swinging
`eyc` symmetrically between `±eyrev` (average 0) for the sway. The boost
therefore outran both until the per-enemy attachment test in `JE_drawEnemy` —
ride = `fixedmovey` + (`eyc` unless `eycc` oscillates it) `== backMove2 > 0`,
no `yaccel` homing (it mutates `eyc` after the test and marks a free flyer) —
began adding `endlessScrollExtraPx2` to their scroll advance. The same test binds their
sprites and health bars to layer 2's canonical presentation transform
(`par_ylayer 2`, lagged fractional clock, bar pulled back by the post-advance
scroll part), so under a fractional boost rate they render pixel-locked to the
glass rather than pulsing against its smooth float rate. Free flyers, and the
whole bank at stock speed (`extraPx2 == 0`), keep byte-identical simulation.

Event-time spawn phase needs the same treatment. Level events are keyed to
layer 1's integer `curLoc`; a boosted tick can cross several event coordinates,
so the next tick may first process a spawn with `curLoc > eventtime`. Without a
catch-up, only the records on skipped coordinates start one or more pixels behind
their terrain (AST CITY exposes this densely). `tyrian2.c` retains the exact
layer-1 interval and layer-3 delta crossed by the preceding tick, then advances a
new bound enemy through the missed fraction of its layer plus its own `eyc` and
scaled `fixedmovey`. This also preserves fixed-motion cancellation in BRAINIAC.
Free-flying sky slots are excluded, as they are not vertically layer-bound. A
spawn that passes the sky attachment test rides layer 2 — a DIFFERENT layer
than the event clock, which changes the math. The glass and the event clock
quantize their boosted fractional rates through independent carries, so the
integer identity "glass == ratio × curLoc" that stock keeps exact wanders ±1px
with the relative carry phase. Pieces of one structure spawn on different ticks,
inherit different phases, and keep them for life — a permanent 1px seam inside
the structure (GYGES's chain machine showed it on Slipstream). Per-tick pulse
proration cannot fix this: even a late-0 spawn can land on a shifted-phase tick.
Each sky spawn is instead anchored to the ideal line — `ratio100 × late` at the
stock layer ratio (the boost cancels out of it) plus the current cross-layer
carry phase (`eventScrollSkyRatio100`/`eventScrollSkyPhase100`, captured in
hundredths from the smoothing carries at the end of each tick's scroll block).
Local motion beyond the ride is prorated like the other banks, and sky
`fixedmovey` itself never scales (local-motion semantics). Event jumps and
`forceEvents` timeline-only increments invalidate the catch-up.

### Other render-rate presents

- Palette fades (`palette.c smooth_fade_to`): recomputed per presented frame from
  real elapsed time. The classic incremental step is exactly linear, so a
  straight lerp reproduces it with the same duration and end palette.
- Destruct minigame (`destruct.c`): self-contained smooth and supersampled
  present. Static terrain is expanded once per tick into `destruct_bg_hi`; each
  frame copies it and draws interpolated units/shots/crosshairs plus HUD on top.
  Needs both Smooth Motion and Sub-pixel enabled.
- Power/shield HUD gauges smooth at render rate with an AA edge shade.
- Soul of Zinglon's light pillar records its request per tick (`zinglonPillar*`)
  and draws at display rate; interpolated frames shift its centre by the ship
  override so the pillar glides with the ship.
- Picture wipes (U/V/R): the classic path advances the wipe boundary one
  row/column per tick; the smooth path advances it by real elapsed time and
  presents every frame. Same total duration and end image, and a keypress skips
  to the end.

## Supersampling & video — `video.h`, `video_scale.c`

- The interpolated playfield can render into an NxN buffer so motion lands on
  1/N-pixel positions (slow scrolls glide instead of stepping). `0 = Auto`
  matches the video scaler's factor (Native's fractional ratio rounds up so the
  buffer covers every screen pixel); `1 = off`; 2..8 fixed.
- Fit modes when output is larger than the hi buffer: `Sharp` (nearest, classic),
  `Smooth` (sharp-bilinear: integer prescale + linear fit), `None` (point-sample,
  drops the supersampled detail entirely). Downscaling always blends linearly in
  Sharp/Smooth; nearest minification shimmers.
- Enum values persist in the config file: keep `Sharp=0/Smooth=1` and only append
  new modes.

## Widescreen

- The playfield sits at `game_screen[PLAYFIELD_LEFT ..+PLAYFIELD_WIDTH)`;
  `composite_playfield()` crops it to the final buffer's x=0. `PLAYFIELD_LEFT`
  must equal that crop offset, and is deliberately not derived from
  `PLAYFIELD_X_SHIFT` (the background tile phase, an unrelated quantity that only
  happened to be `-PLAYFIELD_LEFT/2`).
- Menus render on a 320px virtual screen centered in the wider buffer. Center
  text on `LEGACY_WIDTH/2`, not `vga_width/2`, or labels drift when blitted.
- HUD overlay draws use composited-buffer coordinates (HUD starts at x=299 at
  320-width equivalents); playfield draws use playfield coordinates. Mixing the
  two caused double-offset bugs (debug panel, xmas snow).
- A surface's pitch is not its logical width: code that steps rows by a width
  constant must use the right one. A "fix" here once squished the ship-specs
  zoom; it was correct as shipped.
- Destruct HUD backdrop: the top 12 rows of pic #11 are two identical box frames
  authored for 320px (x=2 and x=172). Pin each frame flush to its screen edge and
  black out the widened middle; the readout boxes anchor 1px inside the same
  constants, so frame and box stay locked.

## Endless mode — `endless*.c`, `endless.h`, `endless_internal.h`

Endless mode is split across nine files. `endless.h` is the public interface the
rest of the game calls; `endless_internal.h` is the private contract between the
endless files (shared run state, shared helpers) and maps out which file owns what:

| file | owns |
| --- | --- |
| `endless.c` | run state, lifecycle, zone milestones, the run-end screen |
| `endless_rng.c` | the run seed and the structural RNG, the seed-select screen |
| `endless_level.c` | which shipped level a zone plays, its music, its reroll |
| `endless_combat.c` | enemy scaling, elites, specials, player-side modifiers |
| `endless_perks.c` | perks |
| `endless_shop.c` | the outpost: stock, prices, the E-Shop buys, the gamble |
| `endless_mods.c` | the mutator table: sector names, danger tiers, help text |
| `endless_course.c` | Chart-a-Course: generating and committing the next sector |
| `endless_save.c` | the `endless.sav` sidecar and the Quit Level sortie snapshot |

### Where the tuning knobs live

Retuning endless mode means editing a named block, not hunting literals:

| what | where |
| --- | --- |
| enemy intensity (HP, boss HP, fire rate, shot speed, shot damage) | "Enemy intensity tuning" block, `endless_combat.c` |
| rising tide, contact damage, elite share | the `ENDLESS_TIDE_*` / `ENDLESS_CONTACT_*` blocks, `endless_combat.c` |
| perk strengths | `ENDLESS_PERK_*`, `endless_internal.h` |
| outpost prices and their per-visit escalation | "Outpost price tuning" block, `endless_shop.c` |
| how rare each signature sector is | `endlessRareInjections[]`, `endless_course.c` |
| what a danger score reads as (tier word + letter grade) | `endlessDangerBands[]`, `endless_mods.c` |
| deep-run danger tilt | `ENDLESS_DANGER_RAMP_*`, `endless_course.c` |

`endlessGenerateCourses` is a sequence of named phases, in this order: gather levels → shuffle
theme order → deal hostile themes → widen combos → boon course → gambit graft → rare injections →
dedupe → visit flavor (Jackpot / Gauntlet / Ambush) → ensure a legible choice → milestone slate →
gravity variants → enforce elite rules → sort by danger → unique names → cache base-level names
(for Radar, deliberately after the sort). **Order is behaviour**: every phase that draws does so
off the seeded structural stream, so moving one — or inserting a new one that draws — changes what
every existing seed generates. Append rather than insert, and keep this list complete: a phase
missing from it reads as a phase that is safe to reorder.

### Seeded structure RNG

Endless draws its structure (level order, course mutators, perk offers, shop
stock) from a dedicated SplitMix64 stream re-derived per zone from
`hash(seed, depth)`, separate from the shared gameplay RNG (`mt_rand`) so in-level
combat draws and player-timed gamble/reroll draws can never desync the cross-zone
structure. Elite/champion tier rolls get their own per-zone sub-stream. Same seed
and same choices reproduce the same run against the same build; adding or removing
a structural draw changes what a seed means. Moment-to-moment combat randomness is
deliberately unseeded, so the elite roll sequence is seed-fixed but which enemy
each roll lands on can shift.

**Every phase salt must be unique, even across separate state variables.** The
phases in use are `depth*2` (outpost stock), `depth*2+1` (level/music),
`+0x40000000` (light cone), `+0x50000000` (elite/champion sub-stream, its own
`endlessEliteRngState`) and `+0x60000000` (gravity heading). The same salt derives
the same SplitMix state, so two streams sharing a phase start from identical
sequences however separate their state variables are — gravity and the elite roll
both sat on `0x50000000` for a while and were correlated because of it.

Per-level music is deterministic per (seed, depth): the anti-repeat compares
against the previous zone's song recomputed from that zone's own stream, not a
mutable `last`, so a Quit-Level retry replays the same track.

### Difficulty ramp

- Enemy levers are driven by an effective depth: real depth × 1.25, tilted by the
  50..160 difficulty factor (`endlessDifficultyRampPercent`). Real
  `endlessRunDepth` still drives HUD/score/milestones/economy.
- Each lever has its own slope so caps mature one at a time (NORMAL real zones:
  shot damage ~55, elite HP ~64, shot speed ~67, fire rate ~80, boss HP ~96,
  ordinary HP ~100, elite share ~118).
- The elite **share** is two-stage, so ~37 is its *shoulder*, not its cap:
  `2 + effDepth/2` up to 25% (effective depth 46 ≈ real zone 37 on NORMAL), then a
  shallower `+0.54`/level to the 80% cap at effective depth 148 ≈ real zone 118. A
  true 100% is reserved for the Apex / Legion sectors. The 25% shoulder is also the
  unlock gate for the no-elite-tier boons (`endlessEliteBoonsUnlocked`).
- **Effective depth is not a zone number.** It is real depth × 1.25 on NORMAL, so a
  figure quoted in effective depth reads ~20% lower as a zone. Several comments have
  drifted by conflating the two — state which clock a number is on.
- Rising tide: intensity levers saturate by effective depth ~100–125 (armor byte
  cap, fire pinned at one tick). The tide adds the one axis with no engine
  ceiling — extra enemy shots per volley and a rising elite/champion share — from
  a single coefficient, starting at **effective depth** 35 (≈ real zone 28) so it
  never piles onto the intensity ramp (drives the elite share + shot-damage climb).
  `ENDLESS_TIDE_START` is on the effective-depth clock; the `ENDLESS_TIDE_SHOT_*`
  thresholds beside it are real zones. Shot damage resumes at +1% per 3 tide levels:
  ~+30% by zone 100, ~+70% by zone 200 on NORMAL.
- Extra volley shots (`endlessExtraEnemyShots`) run off the difficulty-scaled zone
  (`endlessDifficultyZone` = real zone on NORMAL), not the tide coefficient. Two
  segments meeting at the anchor (zone 100): an early ramp that adds the FIRST
  extra shot at zone 25 and rises evenly to `ANCHOR_ADD` (3) by zone 100, then a
  steady +1 shot every `STEP` (25) zones with NO hard cap — so 5 by zone 150 and
  climbing indefinitely (only the `MAX` 50 sanity backstop and the `ENEMY_SHOT_MAX`
  pool bound it). NORMAL: 1@z25, 3@z100, 5@z150, 7@z200, 9@z250… Harder/easier
  difficulties travel the same curve sooner/later.
- Contact-damage ramp (`endlessContactDamagePercent`): the collision/ram damage
  the PLAYER receives scales up with depth — no bonus until zone 35, linear to
  +150% by zone 100, same slope onward, capped +500% (~zone 252). Applied in the
  `mainint.c` ram-collision path to `playerHit` ONLY; `damage_to_enemy` keeps the
  unscaled value, so enemies aren't ground down any faster. Composes after the
  RAMPAGE ×1.5, then the final byte clamps to 255. Difficulty-scaled off the same
  `endlessDifficultyZone`. Base ram damage is small (`damageRate`, usually 2/tick),
  so the effective per-tick hit steps 2→3→4→5 as the multiplier crosses 150/200/
  250%.
- **"Is this a boss?" needs the `linknum != 0` guard — `enemy_has_boss_bar()`.**
  A boss is an enemy that *explicitly has a boss health bar*. An idle bar slot
  holds `link_num == 0` (both start there, and `draw_boss_bar` zeroes a slot once
  its group is dead), and `linknum == 0` means "unlinked" for an enemy — so the
  bare `enemy[b].linknum == boss_bar[i].link_num` test called **every ordinary
  unlinked enemy a boss** for as long as either slot was idle, which is nearly
  always. Consequences, all live before this was caught: ordinary enemies were
  handed the full boss HP multiplier (8× at zone 50, or `expertBossHpMult` in a
  campaign expert run), took the boss branch of the Executioner perk, and picked
  up the endless pierce lockout. The chain-reaction site
  ([tyrian2.c](src/tyrian2.c)) had the guard and was correct; the two damage sites
  did not. All three now call the one helper — **never open-code the comparison.**
- **Percentage levers must bite on RAW damage, never on the post-`hpMult` figure.**
  The hit site divides `damage` through `enemy[].damageAccum` (spend 1 armour per
  `hpMult` damage), so past the first few zones the surviving number is 1 or 2
  armour points, whatever the weapon actually deals. The **Executioner** perk was
  computed *after* that divide: `damage * stacks * 15 / 100` on a value of 1 is 0,
  so the one perk sold as "finish off wounded bosses" paid out **exactly zero on
  every boss hit** — 4× at zone 24, 16× at the cap, 24× on an elite boss. It now
  takes its cut off the raw damage and rides the accumulator, and the site measures
  the bonus in post-divide armour points (`damage - plain`, where `plain` re-divides
  the same pre-hit accumulator state without the perk) because the shot-carry paths
  at the bottom of the collision block undo `execBonus` in *that* space. Same trap
  as the pierce-marker scaling below: **truncation is not a rounding detail when the
  quantity being scaled is 1.** `endlessPerkExecutionerBonus` rounds to nearest for
  the same reason — at one stack, truncation meant no payout at all under 7 damage,
  and a piercing shot's raw damage is only 0..5.
- **Piercing weapons vs the boss HP multiplier.** `shotDmg` is an *encoded* byte:
  99 = ice (no damage), 250..255 = piercing with `damage = value - 250`, and
  100..249 is consumed earlier by `shots.c` (chain reaction, `shotDmg` forced to
  1). Two consequences that both landed on bosses:
  - The endless player-damage scale in `tyrian2.c` multiplied the *raw* byte, so
    Overcharge/Overdrive/Heavy Rounds turned a 3-damage piercing round into a
    ~129-damage one, a damage *cut* could drag the byte under 250 and strip the
    pierce flag outright, and an ordinary shot could scale onto either marker.
    It now decodes, scales only the real damage, and re-encodes (clamping a
    non-piercing result to 249 and steering it off 99). **That bug was carrying
    essentially all of pierce's endless damage output** — correcting it is a
    ~26–43× cut at typical multipliers, so anything that felt tuned against the
    old behaviour was tuned against a 40× buff.
  - Because a piercing shot's raw damage is only 0..5, integer scaling rounds the
    lever away entirely (+50% on 3 damage → 4, and on 1 damage → 1). The scale
    therefore rounds rather than truncates and guarantees an uplift moves the
    number by at least one, or Overcharge/Glass Cannon read as dead on exactly
    the weapons whose damage is smallest.
  - A piercing shot is never consumed on impact, so the *same* shot re-damages
    the *same* hull on every tick it overlaps (~5–10 free hits per pass, ±25×±29
    hitbox), times every linked segment it covers. That is DPS proportional to
    dwell time rather than to armour, so it ignores `endlessBossHpMult` entirely
    — a 16× boss died about as fast as a 1× one.
    `endlessPierceLock100(hasBossBar, hpMult, eliteState)` grants immunity to
    *repeat* piercing hits, stored in **`playerShotData[].pierceLock`** and aged
    once per bullet per tick at the top of that bullet's own collision pass.
  - **Ask the tier before consulting the lock, never the reverse.** The hit site
    computes `lock100` for the hull it is standing on *first*, and only then tests
    `pierceLock`. With the test first, a bullet locked out of a boss also passed
    harmlessly through every ordinary enemy overlapping it — a boss's protection
    spilling onto the trash beside it, contradicting "ordinary enemies, never".
    Measured at ~44% of pierce damage denied to trash sharing a 4-part boss's space
    at zone 50. `endlessPierceLock100` returns 0 for the ordinary tier *above* the
    `ENDLESS_OVERRIDE` line too: "no lockout on ordinary hulls" is structural, not a
    magnitude, so a pinned lever must not be able to introduce one. It is also the
    cheap path — the hit site now asks per bullet × per overlapping hull.
  - **The lock must be PER BULLET, never per enemy.** Piercing weapons fire a
    spread — Mega Cannon and Sonic Impulse are both attack `251` (1 damage) × 8
    bullets, and *all* of their damage is "many bullets, every tick". A per-hull
    lock let the first bullet of the tick claim it and silently discarded the other
    seven, collapsing a 16-bullet loadout to one hit per window: measured as
    "basically no damage to bosses" at zone 50. Per bullet, all eight still land;
    each is merely stopped from re-hitting the *same* hull every tick.
  - **Charged once per TICK, at the toughest hull crossed — not once per hit.**
    Spent per hit, the cost scaled with how many hulls happened to line up, so the
    tax was `1 − 1/(1 + H·L)`: a four-part boss paid 29% where a one-part boss paid
    the tuned 9%, and the real figure depended on boss geometry. Taking the max
    also stops a boss's tax being diluted by trash sharing its space.
  - **The charge is BANKED (`pierceLockPending`), converted at the top of the
    bullet's next pass.** Applying it inline would let the bullet lock itself
    partway through its own sweep and drop every hull behind the first — the same
    failure as the per-enemy version, just smaller. Because the countdown for the
    tick has already run by then, the whole part is stored as-is: no `+1` fudge.
  - **Ask the tier before consulting the lock, never the reverse.** An ordinary
    enemy returns 0 and so is neither charged nor blocked — including while the
    bullet is locked out of a boss it is overlapping. Testing the lock first let a
    boss's protection spill onto every scrap of trash sharing its space.
  - It takes `endlessEnemyHpMult`'s exact argument list on purpose: the lockout is
    read off **the same multiplier the target is carrying**, so the tiering falls
    out of that rather than being bolted on. Boss → full, off the boss ramp;
    champion and elite → the 2..4× elite ramp; **ordinary enemy → zero**. 0 at stock
    HP too, so a run that multiplied nothing is untouched. GIANT KILLER flattens the
    elite ramp to 1× and so removes both special tiers' lockout with it.
  - Measured attack bytes, straight out of `tyrian.hdt` (`doc/tools`-style dump):
    **Mega Cannon** (port 3) and **Sonic Impulse** (port 41) are `251` at *every*
    one of their 11 levels — 1 damage per hit, piercing, 8 bullets at max. Zica
    Laser (port 5) is 1..12 and **not** piercing; Mega Pulse (port 19) is 149..154,
    i.e. the *chain-reaction* range, so `shots.c` forces its `shotDmg` to 1. Any
    reasoning about pierce balance has to start from "1 damage, 8 bullets, every
    tick" — not from the encoded byte, which looks like 251 damage.
  - Carried in **hundredths of a tick**, not whole ticks. Both multipliers are
    integers (the damage accumulator can only divide by one), so keying off them
    made the lockout jump a whole tick every 16 effective depth. It reads
    `endlessBossHpMult100()` / `endlessEliteHpMult100()` instead — same slopes,
    deltas and caps, unrounded. Keep each smooth body in step with its integer twin.
  - **One tuning knob per tier, and each IS the play-tested figure** at a fixed
    reference zone (`_REF_ZONE` 50): `_BOSS` 10, `_CHAMP` 5, `_ELITE` 2 hundredths,
    with `_MAX` (1 whole tick) as a backstop the ramps never reach. Everything else
    is derived — `span × atRef / refSpan`, where `refSpan` is recomputed from the
    ramps themselves — so changing a tier moves that tier and nothing else, and
    retuning an HP ramp can no longer silently drag the lockout with it. The
    reference point is evaluated at NORMAL on purpose, so the calibration doesn't
    mean a different thing in every run. Earlier revisions were a shared slope plus
    per-tier percentages (`_NUM`/`_DEN` 1/2 → 1/4 → 1/12 → 3/50 → 1/30, with
    `_ELITE_PCT` / `_CHAMP_PCT`) that had to be hand-fitted every time a ramp moved.
  - Figures are deliberately small: pierce DPS is ~`1/(lock+1)`, so a zone-50 boss
    taxes ~9%, a plain boss at the 16× cap ~16% (≈0.19 tick), and the special tiers
    barely register (≈0.02 elite / ≈0.05 champion even at their 4× cap). Only an
    elite/champion boss riding the `ENDLESS_HP_MULT_MAX` 24× cap reaches ~0.30 (~23%). It is
    a safeguard, not a wall — after the marker fix above there is very little headroom
    to spend here, because the weapons it touches deal **1 damage a hit** and anything
    heavier reads in play as "my gun does nothing".
  - **Elite/champion HP ramp history.** It was `2 + depth/20` capped at **5×**, written
    while the `has_boss_bar` bug above was quietly handing ordinary enemies the boss
    multiplier too — so a 5× elite read as only ~2× tougher than the trash beside it.
    With ordinary enemies correctly back at 1×, the same 5× read as a wall, especially
    against the 1-damage piercing weapons where a divisor of N means N hits per armour
    point. Now `2 + effDepth/40` capped at **4×** (`ENDLESS_ELITE_HP_*`).

    | zone | boss | champ | elite | boss tax | champ tax | elite tax |
    |---|---|---|---|---|---|---|
    | 1 | 0.00 | 0.01 | 0.00 | 0% | 1% | 0% |
    | 20 | 0.03 | 0.03 | 0.01 | 3% | 3% | 1% |
    | **50** | **0.10** | **0.05** | **0.02** | 9% | 5% | 2% |
    | 100+ | 0.19 | 0.05 | 0.02 | 16% | 5% | 2% |

    The champion figure is nonzero at zone 1 because its HP ramp *starts* at 2× (the
    elite's rounds to zero there), and both flatten from zone ~65 because that ramp
    caps at 4× — so the tiers separate further the deeper the run goes. `_MAX` (1
    whole tick) is only a backstop; the boss ramp tops out at 0.19 and never reaches it.
  - **Tuning shape.** The knobs are `_REF_ZONE` plus one figure per tier
    (`_BOSS`/`_CHAMP`/`_ELITE`), and each figure *is* the play-tested value in
    hundredths of a tick at that zone — change one and only that tier moves. The
    ramp shape is derived: `lock = span × atRef / refSpan`, where `span` is how far
    above stock the target's multiplier sits and `refSpan` is the same span at the
    reference zone, recomputed from `endlessBossRamp100`/`endlessEliteRamp100`. That
    is what makes it self-refitting — earlier revisions used a slope plus per-tier
    percentages that had to be hand-fitted every time an HP ramp moved, and retuning
    elite HP silently dragged the lockout with it. `endlessEffectiveDepthOf(zone-1,
    100)` pins the reference to NORMAL so difficulty and mutators can't shift the
    calibration. `endlessBossRamp100` is deliberately *not* folded into
    `endlessBossHpMult100`: the live path must clamp **last**, after the mutator
    deltas, or FRAGILE would halve an already-clamped figure.
- **Elite/champion HP was over-tuned by a masked bug.** `endlessEliteHpMult` was
  `2 + effDepth/20` capped at **5**, and it is a damage divisor stacking on top of
  `endlessArmorPercent` (344% at zone 50). It was tuned while the `has_boss_bar` bug
  above was handing *ordinary* enemies the boss multiplier as well, so a 5× elite
  read as only ~2× tougher than the trash beside it. Fixing `has_boss_bar` put trash
  back at 1× and the same 5× became a wall — worst against the 1-damage piercing
  weapons, where a divisor of N literally means N hits per armour point. Now
  `ENDLESS_ELITE_HP_BASE/_PER_X/_MAX` = 2 / 40 / **4**: 3× at zone 50 (was 5×), 4×
  from zone ~65. Effective HP of a 20-armour elite at zone 50: 204, down from 340,
  against an ordinary neighbour's 68. `endlessEliteHpMult100()` must move with it.
- **The elite/champion BOUNTY is a per-ENEMY payout, not a per-tile one.**
  `endlessAwardEliteKill` used to take only `eliteState` and pay every time it was
  called, while both tyrian2.c death sites call it inside the linkgroup cascade —
  once per destroyed tile. A multi-tile elite hull therefore paid its bounty twice,
  three times, once per tile, silently inflating run income (worst on the fattest
  targets, which are exactly the ones already paying a champion rate). It now takes
  `(linknum, eliteState)` and latches the linknum in `endlessBountyLastLink`, the
  same "consecutive same-linknum removals are one enemy" rule `endlessCountKill`,
  Martyrdom, Shockwave and the Chain Reaction perk all use; reset per level in
  `endlessResetElites` so a zone's first kill always pays.
  - The call is UNCONDITIONAL — the `eliteState >= 2` test lives inside the helper.
    That is the point, not tidying: the latch has to see ordinary kills too, or two
    same-linknum elites separated only by fodder would read as one enemy and the
    second would go unpaid. `endlessCountKill` is called for every kill for exactly
    this reason.
- **Every kill goes through `enemy_logical_death` (tyrian2.c). There is one kill
  path, and adding a second is a bug.** Retiring a killed `enemy[]` slot has six
  required consequences — `enemyKilled`, `endlessCountKill`, `endlessAwardEliteKill`,
  `endlessShockwaveRadius`+sweep, `endlessMartyrdomBurstShots`+burst, and
  `chain_queue_kill` — and three of them carry the per-linkgroup dedup latch above.
  A latch is only correct if it sees **every** kill, so a kill site that forgets one
  entry fails silently and in both directions: pay a multipart elite once per tile,
  or strand a linknum so the next enemy reusing it reads as another tile of the
  previous one and gets nothing. That list used to be copy-pasted at each death site
  and defended with comments; it is now written once, so a new kill path inherits
  the whole contract by calling the helper.
  - `ENEMY_DEATH_QUIET` (vs `ENEMY_DEATH_FULL`) is the only knob, and it suppresses
    **effects only** — the latch-feeding calls run for every caller. The Chain
    Reaction drain is its sole user: its pulse must not queue further pulses
    (`chain_reaction_process` is non-recursive by construction) and a chain pop was
    never meant to throw a death burst or sweep bullets. But it still has to feed the
    latches, because a chain pop always removes lone (linknum 0) fodder and that zero
    is exactly what breaks a stale run.
  - Centralising this fixed `endlessShockwaveRadius`, which tested `eliteState < 2`
    *before* updating `endlessShockwaveLastLink` and so only ever latched on elite
    calls. The last elite's linknum stayed latched indefinitely — ordinary kills
    never cleared it — so the next enemy to reuse that linknum had its sweep silently
    skipped. It now latches first, exactly like `endlessAwardEliteKill`; the three
    latches finally have identical semantics.
  - **Despawns are not kills** and must keep assigning `enemyAvail[i] = 1` directly:
    scrolling off the playfield, the map-stop watchdog cull, and the level-event
    clear/replace paths (events 41, 59/68) feed no tally and no latch.
  - The **ram kill** in `JE_playerCollide` (mainint.c) also stays off this path, and
    that is a balance decision rather than an oversight — a ram has never fed
    `enemyKilled`, and giving it the full contract would hand ramming the run tally,
    the combo/Turbodrive window, Overdrive stacks, Siphon armour, elite bounties and
    the death effects, i.e. make suiciding into elites a farming strategy. Swap its
    two `enemyAvail` writes for `enemy_logical_death` calls to reverse it.
- **Never repaint the HUD from the debug menu unless a HUD is on screen.**
  `debug_apply_loadout_change` repaints the shield/armour gauges and
  `JE_drawOptions` because both are event-driven and would otherwise keep showing
  the old ship's numbers. But the debug menu opens from the shop and title screens
  too, where that stamps gameplay gauges and sidekick icons into the bottom-right of
  the shop art — and they *stay*, because the shop only redraws the regions it owns.
  Gated on `debugMenuOverHud` (set from `JE_debugMenu`'s `!center`, saved/restored so
  a nested open can't clear it). Skipping it off-HUD costs nothing: level start in
  `JE_main` runs the same calls before the playfield fades in. Note `JE_drawOptions`
  both re-seeds sidekick state *and* draws, so it has to be inside the gate.
  - A tick is indivisible, so the fraction is spent through
    `enemy[].pierceLockCarry`: whole part locks outright, remainder accumulates
    and buys one extra tick when due, making the *average* lock the fractional
    figure. `pierceLock` stores `ticks + 1` because the countdown pass runs at the
    **top** of the tick — without the extra, a lock of N blocks only N−1 ticks.
  - Pinnable as `ESO_PIERCELOCK` ("Boss Pierce Lock") on the SCALING page, which
    formats it as `N.NN` (curve line gets tenths — `helpBuf` is only 96 bytes).
    It is the one lever that is a function of another (it reads
    `endlessBossHpMult()` to carry the elite-boss bump across as a ratio).
- Gravity (Gravity Well course): base plus per-zone ramp, tilted by the same
  difficulty factor, with an absolute cap that stays clear of the ship's top
  speed (`VT_VMAX`) so full throttle can always climb. The VT integrator scales
  it by dt; the classic tick path applies the rounded per-tick amount.

### Course generation & danger labels

- **Danger distribution (2026-07-25 rebalance).** Two things used to concentrate
  almost every sector onto the same handful of bits, and both are fixed:
  1. `endlessCombinableMods` was a FLAT 11-bit pool with 2.8-5 bits drawn per
     course, so each of those bits sat on ~25-45% of courses while whole
     mechanics were injection-only at ~3%. It is now a WEIGHTED table
     (`EndlessModWeight`, drawn by `endlessWeightedModDraw`): core four
     Fortified/Frenzy/Swift/Devastating at 3, mid tier at 4, under-seen
     Static at 6 and Shieldless/Retaliation at 5, the scarce bits at 3, and Martyrdom / Seeker /
     Retaliation PROMOTED in (each acts on a system nothing else in the pool
     touches, so they stack cleanly). A bit's share of a course ≈ bits-drawn ×
     weight ÷ total weight — that is the "how often do I meet this" knob.
  2. `endlessHostileThemes` is a NAME dictionary whose rows were authored
     mostly out of the same four bits (as of the 2026-07-25 name expansion: 256
     rows, Swift in 75, Slipstream in 19 — the counts move with every extension,
     so treat them as the symptom, not a knob), so a
     uniform row draw inherited that skew. `endlessPickSignatureTheme` now picks
     WHICH danger the sector is about from `endlessThemeSignatures` (weighted),
     then a random row carrying it. No table row changed; every name stays
     reachable. Used by the initial deal, the duplicate re-roll AND the Gauntlet
     fallback (`endlessUnusedHostileTheme`) — the Gauntlet matters, it is ~38% of
     deep visits and was re-importing the skew on its own.
- Martyrdom and Seeker are absent from `endlessThemeSignatures` on purpose: they
  reach the chart through the weighted pool and are named from their curated rows
  there (20 rows each since the name expansion — the original reason, "no curated
  row of their own", no longer holds), so a signature would double-count them.
  Their name tables stay wired into `endlessFindTheme`.
- Per-slate diversity: both weighted draws cut a bit already charted on another
  route of the same slate to ⅓ weight (`endlessOtherCourseMods`), so one danger
  rarely covers three of five offered routes.
- **SCARCE BITS (`endlessScarceMods`, 2026-07-28).** Topsy and Gravity are held
  below the weight their danger earns: both tax how the whole sector must be
  FLOWN — crooked, or against a pull — rather than how hard it hits, so they tire
  a run faster than a stat bump does. Both are 3 in the widen pool (Topsy was 6,
  Gravity 4) and low in the signatures (Topsy 8 → 5, Gravity 4 → 3); per hostile
  route, Topsy went ~20% → ~13% and Gravity ~16% → ~13%. Nothing about how either
  bit MIXES changed — neither can softlock a sector.
  - The signature side moves far less (Topsy 17.5% → 14.2%) because 31 of the 256
    `endlessHostileThemes` rows carry Topsy and 47 carry Gravity, so both ride in
    under OTHER signatures. There the dictionary, not the weight, is the floor:
    thinning those rows is the only way further down, and it costs names.
  - The omni share is untouched — still the ½ coin in
    `endlessRollGravityVariants`, so a Rogue Well follows Gravity down to ~7% of
    hostile routes.
- Deep runs escalate by WHICH dangers, not HOW MANY: the widen tops out near 3.4
  bits with a hard ceiling of 4 (it used to reach a guaranteed 5, which read as
  "everything at once"), and the combo share caps at 80%.
- `endlessRareInjections` rows carry a `brutal` flag; `endlessDangerRareDivEx`
  CAPS the deep ramp at 2x for those instead of the full ~6x. Without it the
  punishing signatures stopped being rare at depth (Apex ~12% of zone-250 slates,
  dead generator ~9%, Overload ~23%). Tar Pit is the only flavour row left on the
  full ramp. Martyrdom / Seeker / Retaliation injections were REMOVED (they are
  in the weighted pool now).
- Milestone slates take a SEPARATE path and are not affected by any of the above:
  `endlessMakeRankComboForLevel` draws `endlessMilestonePool` (flat shuffle +
  randomised greedy to a score band), and The End is fully self-contained in
  `endlessMakeTheEndMods`. Neither reads the weighted pool, the signatures or the
  injections, so the brutal-ramp cap does NOT apply — a milestone can still deal
  Legion/Apex/Overload/Warp freely, which is the point of a set-piece. The four
  reactive dangers were added to `endlessMilestonePool` (2026-07-25) so a slate
  isn't the one place left that reads as a core-bit wall; the rank guarantee is
  unaffected because the builder verifies against `endlessDangerRankLevelEx`.
  The flat pool is why the scarcity above needed its own handling here: every
  `endlessScarceMods` bit sits out 1 attempt in 3 (one roll per bit per attempt, so
  the stream stays predictable), leaving each on roughly two thirds as many
  milestone routes as before. `endlessMakeTheEndMods` keeps its own coins — the
  finale is allowed to be flipped and dragged.
- The BOON economy is deliberately untouched by all of the above: boon courses
  thin with depth (`3 + ramp*2/100`) and the gambit graft floors at 5%. Boons
  slowly fading but never vanishing is the intended shape — do not "fix" it.
- Kill-fire mods: three boons (Turbodrive/Overdrive/Overblast) and three evil
  mirrors (Backfire/Burnout/Misfire) share the combo/stack machinery, so a sector
  carries at most one; sub-masks (`FIREBOOST/FIREJAM/DMGUP/DMGDOWN/STACKED`) say
  which effect each grants. The evil three are also forced gamble outcomes
  (`EGO_CURSE_*`), independent of course injection.
- **The purchase wins.** Generation and the shop each hold the one-kill-fire line
  internally, but merging them at launch is where it used to break: a plain OR of
  `endlessCourseMod[i] | endlessPurchasedMods` left a bought Turbodrive AND a
  charted Backfire both set, so `endlessKillBuffFireDecrements` and
  `endlessKillFireJamTicks` both hit the same shot, the boon's free-power break
  was withheld (`shots.c` gates on `endlessKillFireIsEvil`, which the evil bit
  makes true) and the HUD/ship tinted evil over a paid-for buff.
  `endlessFoldPurchasedMods` is now the single merge point — whatever the player
  brought clears the sector's kill-fire bit, in BOTH directions (a bought boon
  clears a charted curse, a gambled curse clears a charted boon) — and it also
  owns the NOELITE-supersedes-NOCHAMP rule. The price is the balance: a kill-fire
  buy costs 66-95% of the purse and locks all three behind the recharge for 2+
  sectors. The launch fold-in and the debug zone jump route through it directly;
  the Chart-a-Course card reaches it via `endlessCourseLaunchMods`, which replays
  BOTH launch passes (the fold, then the queued Sabotage strips) so the card
  prices and lists exactly what the sortie commits. Course NAME and RANK stay as
  charted (seed-determined; the slate is sorted by rank), so a "Backfire" card
  can legitimately show no gun-jam row. The Long Con's deferred APEX is
  deliberately NOT replayed — that one is paid for precisely so it arrives
  unannounced.
- Sabotage is shown, not hidden: a bit a queued charge will strip stays listed on
  the monitor, flagged `EndlessCourseModRow.cleansed`, and draws WHITE (bank 13,
  brightness +4) instead of its danger red — the card reads "this threat was here
  and your charge takes it off" rather than quietly shrinking. Palette 18 has no
  clean grey RAMP (it's a luminance-sorted planet palette, so most banks mix
  hues), but bank 13 at +4 lands the three TINY_FONT shades on 236/244/252
  neutral greys. Don't push the offset past +5: the shade-10 highlight would
  overflow bank 13, and `blit_sprite_hv_unsafe` ORs the shade in rather than
  clamping. Lower offsets pick up bank 13's yellow-green entries (shades 0/3/5).
  The payout on the card drops with the strip, so buying a cleanse visibly costs
  danger money.
- `endlessStripWorstMod` (the Sabotage ladder) must list EVERY hostile bit
  `endlessModTable` prices, or a charge spent on a sector carrying only that bit
  silently does nothing — the evil kill-fire three, Overheat, and all four of the
  reactive dangers (bits 40-43) were missing. Deliberately absent: THEEND (a
  label that pays, not a danger) and the gamble-only bits with no table row
  (Marked/Nitro/Dud). The pass runs on the MERGED set, so it can strip a gambled
  curse as well as a charted one — that has always been true of gambled
  Frenzy/Rampage. It runs AFTER the fold, so a charge is never wasted on a
  kill-fire bit the purchase already overrode.
- The generator gauge recolours while a kill-fire boon window is up (main-gun
  fire costs no power then). The value is a palette bank base; the whole 14-shade
  ramp must stay inside one bank (`draw_power_gauge` derives the AA dark end from
  the bank floor).
  Opening Salvo owns a second recolour on the same gauge; see §Opening Salvo.
- `endlessDangerTier` (word) and `endlessDangerRank` (letter F..S+++) band the
  same net danger score; keep their thresholds in lockstep so the pair never
  disagree. Tier thresholds: ≤9 Low, ≤13 Moderate, ≤19 Tough, ≤26 High, ≤33
  Severe, ≤39 Deadly, ≤49 Extreme, ≤59 NIGHTMARE, >59 APOCALYPSE.
- One grade sits OFF that scale: **END** (level 10, the finale marker, tier word
  FINALITY), keyed BEFORE the score in `endlessDangerRankLevelEx`.
  `endlessRankName[]` and game_menu.c's `endlessRankHue[]` are both indexed by the
  level, so both are length 11 — keep them equal.
- Rank-letter tint (`endlessRankHue`; the chart palette is palette.dat block 17,
  applied via newPal 18): easy grades F/E/D are GREEN from **bank 0** (that
  palette's clean green ramp, shades 3-7 — the same green the boon mod rows use);
  C..S+++ and END ride **bank 15**, the pale-yellow → orange → deep-red fire ramp.
  bank 8 was used for the greens originally but renders brown/gray at the shades
  F/E/D need in this palette (that's why the easy ranks looked muddy). brightness
  kept in [-2,+5] so a glyph's shades never leave the bank.
- A Cursed sector has NO combat danger (score 0), so it reads **Boon** (rank F,
  green) like any score-0 course — there is no separate "Trap" tier/rank/help
  label. Its catch (big cash now, empty shop next) is carried entirely by its own
  red "cash now, empty shop" modifier row; an earlier "!" rank + "Trap" help line
  were removed as redundant with that row. It still isn't a pure win, so the chart
  sort key (`endlessDangerSortKey`) gives it a slot of its own — just right of the
  calm/boon routes, just left of the mildest combat danger — so it never sits at
  the far-left "safest pick" end.
- **Slate order** (`endlessSortCoursesByDanger`, a stable RNG-free insertion sort
  run once at generation): primary key is the DISPLAYED rank, not the raw score,
  so the left-to-right ramp can never contradict the letter grade on the cards.
  Key rungs: 0 = Calm (`mods == 0`, pinned top — boons may pay LESS than it, since
  survival boons carry negative rewards), 1 = the other pure boons, 2 = Cursed,
  3.. = rank + 2 for every combat route (E..END). Ties on the key break on the
  clear PAYOUT, cheapest first, so a slate of same-grade sectors also reads as a
  rising price. The payout used is `endlessDangerSortPayout` — the course's own
  bits plus the level's `payoutMille` — deliberately NOT `endlessCoursePayout`,
  which re-prices as the player buys buffs or queues Sabotage charges mid-visit
  and would leave the fixed order disagreeing with the numbers on screen.
- Faster scroll always reads hostile (2026-07). Mechanics unchanged: Overclock
  (+70%) / Overload (+220%) still speed fire + shots + scroll together, and
  Slipstream / Warp are the scroll-only versions at the same strengths — but
  Slipstream/Warp moved to `ENDLESS_HOSTILE_MASK` (red rows, weights 6/20, paid
  like threats, cleansable), so no faster-scroll mod ever shows as a green boon.
  The PRESENTATION is decoupled: Overclock/Overload's monitor word is now
  "faster/extreme enemy attacks" and `endlessCourseModRows` appends a separate
  display-only red "(much) faster scrolling" row (suppressed if a real scroll
  bit is present), replacing the ambiguous "faster scroll + fire" phrasing;
  curated help lines attribute fire to the foe ("foe fire/shot/scroll+").
  Slipstream's old boon combos (Blitz / Smash and Grab / Time Warp / Power
  Play / Payday) moved to `endlessMixedThemes` (reachable via boon grafts —
  `mixCommon` explicitly includes SLIPSTREAM), its hostile pairs (Fast Lane /
  Runaway / Bypass) to the hostile table, and "Warp Speed" lives in a
  naming-only `endlessWarpThemes` kept out of the shuffle pool. Slipstream
  stays OUT of the combinable widen pool (Overclock already carries the same
  +70% scroll — pairing them would be a redundant bit). Same bits, no save bump.
- Rare whole-visit flavors (Jackpot ~1/25: all boons; Ambush ~1/20: one forced
  dangerous sector; Gauntlet ~1/7: all hostile). All three dice are rolled up
  front unconditionally so the seed stream stays aligned; precedence Jackpot >
  Ambush > Gauntlet; none fire at depth 0. All three are suppressed on a
  milestone zone (below) — the dice still roll, only the effect is gated.
- MILESTONE ZONES (`endlessMilestoneKind`, keyed off the REAL zone
  `endlessRunDepth + 1`, not the difficulty-scaled one): every 25th zone charts a
  full FIVE-course slate of nothing but S-tier sectors, in three classes. The MINOR
  one (kind 3 — 25/75/125/…, the odd multiples of 25, sitting halfway between the
  others) runs S/S+ on the same 2-and-3 split as the plain class, and pins its
  music to "Tunneling Trolls"; it is the mildest
  class despite carrying the highest kind number (the kinds are tags, not an
  ordinal). Zones 50/150/250/… run
  S+/S++, split 2-of-one and 3-of-the-other with the seed deciding which rung gets
  the pair. Every 100th zone (100/200/300/…) has a FIXED shape instead: **1 END +
  2 S+++ + 2 S++** — the END course is "The End" (below), and the four generated
  slots split evenly. The 2-or-3 roll still runs on a grand milestone and is then
  overridden, so the seed stream stays aligned with a plain one.
  `endlessMakeRankComboForLevel(rank, baseDanger)` builds each sector: shuffle
  `endlessMilestonePool[]`, greedily take bits that don't overshoot the rank's
  score band, stop the moment the score is inside it, then VERIFY with
  `endlessDangerRankLevelEx` before handing it back (so a retuned band can't
  silently mislabel a slate) and reshuffle if it missed. The score band is first
  shifted DOWN by the target LEVEL's own `baseDanger`, and the verify folds that
  same `baseDanger` in — so the rank a slate GUARANTEES (S / S+ / S++ …) is the
  rank it still DISPLAYS after the level is attached and the chart sorts, rather
  than drifting a grade on a gentle (−2) or harsh (+5) stage. Bit weights are read off
  `endlessModTable` via `endlessModReward`, never duplicated. `group` in the pool
  marks mutually redundant bits — at most one scroll mod, one homing tier, one
  elite tier, one shield handicap. Out of the pool on purpose: ELITEPACK (the
  deep-run redundancy swap would move the score), and the super-rare signatures
  (Deadgen / evil kill-fire / Redline) — a milestone is a wall of ordinary
  dangers, not a scheduled visit from the rarest sector in the game. Bands (at
  baseDanger 0, before the per-level shift above) are S+ 40-49, S++ 50-59,
  S+++ 60-95 (the rank itself is open-ended above 60; the build stops at 95 so a
  slate stays flyable). The override runs after every
  ordinary generation step and before the OMNI roll / danger sort / unique-name
  pass, so a milestone chart is finished off like any other; it keeps the levels
  gathered up top and re-deals only the mutator sets. Reward follows danger
  automatically (same `endlessModTable` the payout reads), so these pay big.
  The milestone constants and `endlessMilestoneKind` (the zone about to be
  charted) / `endlessMilestoneClearedAt` (a depth that WAS one) live in
  `endless.c` with the other run-progress state, since the outpost needs them long
  before the course generator does.
- A GRAND milestone always deals **"The End"**, and it is NOT a fixed bitset — only
  its CORE is constant, the rest is re-rolled per milestone off the seeded stream,
  so a run's zone-100 finisher differs from its zone-200 one and from every other
  run's (80 variants, all reachable). `ENDLESS_THEEND_CORE` is the enemy at its
  worst and nothing else: Fortified, Frenzy, Swift, Devastating, Enrage. What
  varies (`endlessMakeTheEndMods`): the special-enemy tier (always one — Apex, or
  Legion at 1-in-3), the scroll pace (none / Slipstream / Overclock / Overload /
  Warp), and a coin apiece for Gravity, Topsy and Sluggish. Gravity+Sluggish can
  both land — that's the Tar Pit pairing, brutal but always flyable, since
  `endlessGravityDrift` scales the pull down in lock-step with the ship.
  Deliberately excluded: the homing tiers, which turn a gun fight into a chase, and
  the two handicaps that simply take a system away (Shieldless, Deadgen). Elite
  Pack is out too — deep runs retire it as redundant
  (`endlessFixRedundantElitePack`), which would rewrite the finale's bitset from
  under it.
- Everything that makes The End *the* End hangs off one marker bit,
  `ENDLESS_MOD_THEEND` (bit 39), not off matching an exact combination — which is
  precisely what lets the dangers vary. The marker carries no mechanic; no gameplay
  lever reads it. It supplies: the name ("The End", special-cased at the top of
  `endlessComboNameSalted`), the **END** rank (`endlessDangerRankLevel` returns 10,
  off the letter scale — `endlessRankName[]` and game_menu.c's `endlessRankHue[]`
  are both indexed by that level and MUST stay the same length), the **FINALITY**
  danger word (a rung above APOCALYPSE in `endlessDangerTier`), and the bounty: its
  `endlessModTable` reward is 150, so the finale pays ~25-31x base (≈570-710k at
  zone 100). Because the danger score sums that same table, the marker also puts
  the sector at 238-301 — far above the 95 ceiling `endlessMakeRankComboForLevel`
  tops S+++ courses out at — so it is always strictly the worst course on the slate and
  the sort always puts it last. It is its OWN rank, not one of the two generated
  rungs, which is why the grand slate reads 1 END + 2 S+++ + 2 S++. Pinned into
  slot 0 so every later draw sees it in `used`.
- Moving The End onto the marker left the Cataclysm sub-pool (the `endlessRareThemes`
  rows carrying neither Apex nor Legion, dealt by the ~1/45 injection) without that
  bitset, so three rows were added at its top: **Ruination** (56, S++ — the same
  six-danger enemy wall plus a well that the old fixed The End used, so the
  combination stays in the game), **Death Knell** (68) and **Black Sun** (73).
  Those last two are S+++, and the only S+++ sectors an ORDINARY zone can be dealt
  without an Apex/Legion tier — a rare shot at a genuine nightmare outside the
  milestones. The sub-pool now spans 46-73 across 20 rows.
- The marker's `word` in `endlessModTable` is **NULL**, meaning "label, not
  mechanic": `endlessCourseModRows` skips NULL-word bits, so the monitor's threat
  column lists only the sector's real dangers — 6-10 rows (11 when
  Overload/Overclock adds its display-only scroll
  row), against a 16-row monitor. That is the point of the design:
  the finale is read as a long wall of threats plus an off-scale rank, not as a
  curated one-liner. (Any future label-only bit gets this behaviour for free.)
- Each milestone CLASS is PINNED to its own track, so the two set-pieces stay
  distinct: `ENDLESS_MILESTONE_SONG_GRAND` 35 = "One Mustn't Fall" on every 100th
  zone, `ENDLESS_MILESTONE_SONG_PLAIN` 37 = "A Field for Mag" on the other 50th
  zones. Both are 1-based into `musicTitle[]`, matching `levelSong`, which the
  level start plays as `levelSong - 1`. `endlessPickLevelMusic` still runs its
  normal rolls first and overrides the result, so the seeded song stream stays
  aligned with an ordinary zone.
- The OUTPOST that charts a milestone announces it: `endlessBetweenLevels` sets
  `songBuy` to `ENDLESS_MILESTONE_SHOP_SONG` ("Parlance") instead of
  `DEFAULT_SONG_BUY`, so the warning plays while the player still has the course
  list in front of them. It needs no save field — `songBuy` is re-derived from the
  (saved) run depth at every outpost entry, and every endless route into the shop
  goes through `endlessBetweenLevels`, including the in-shop load that JE_main
  detours via `endlessResumePending`. MIND THE INDEXING: `songBuy` is played as-is
  (`play_song(songBuy)`, and the `]i` script command stores `temp - 1` into it),
  while `levelSong` is 1-based — so the same track has two constants,
  `ENDLESS_MILESTONE_SHOP_SONG` 26 and `..._LVL` 27. Keep them in step. The
  levelSong form exists because the zone BEFORE a milestone must also avoid that
  track (see below), or its level music would run straight into the shop playing
  the same thing and the warning would read as "nothing changed". The ordinary
  buy/sell theme never needed this: song 3 isn't in `endlessLevelSongs` at all.
- NO track ever plays twice running anywhere in the shop → level → shop chain,
  exactly — not statistically. A zone rejects the song its predecessor really
  played, the one its successor is PINNED to, and the warning track its following
  outpost will play if that outpost charts a milestone (so zones 49 and 51 both
  dodge "A Field for Mag", 99 and 101 both dodge "One Mustn't Fall", and 49/99 also
  dodge "Parlance"). Rejection is a bounded 6-retry loop, so a pathological seed
  can't spin. Two mechanisms make the neighbour tests exact:
  - The SUCCESSOR test is RNG-free: `endlessMilestoneKindOfZone(zone)` takes the
    zone as a parameter, so a neighbouring milestone's pinned song is simply
    looked up.
  - The PREDECESSOR test remembers rather than re-derives:
    `endlessLastSong` / `endlessLastSongDepth` record what the last-played zone
    actually used, and ride the save (v10). The old approach replayed the previous
    zone's stream and took its FIRST draw, which is only an approximation — that
    zone may itself have re-rolled — and left a real ~1-in-1150 chance of an
    audible back-to-back repeat between two ordinary zones. Deriving it exactly
    instead is impossible without recursing back through every prior zone, hence
    the stored value. The first-draw derivation survives as the fallback for when
    there's nothing remembered: a pre-v10 save, or a debug zone jump that skipped
    the intervening zones.
  `endlessLastSongDepth` also pins a RETRY: re-entering the same zone (Quit Level,
  a locked relaunch, a reload) returns the stored song immediately, so the track
  can't reshuffle — even if the player re-charts to a different course, since the
  music belongs to the zone, not the level. Verified against the real SplitMix RNG
  over 20k seeds × 210 zones (4.18M transitions): 0 wrong milestone tracks, 0 wrong
  shop tracks, 0 adjacent repeats of any kind (level→level, level→shop, shop→level),
  0 retries or reloads that changed a track.
- The pin also has to survive the level's own script: `endlessMilestoneZone()`
  (the one public predicate, in `endless.h`) makes tyrian2.c ignore event 35 (play
  new song) AND event 34 (start music fade) on a milestone — suppressing only 35
  would strand the pinned track at the fade floor for the rest of the zone, since
  34/35 are normally used as a pair. Event 35 still restores the volume. Songs 35
  and 37 remain in the ordinary random pool (`endlessLevelSongs`), so they can
  also come up on a normal zone (just never adjacent to the milestone that owns
  them); pulling one from that array would make it milestone-exclusive, at the
  cost of reshuffling every seed's song order.
- Forced perk picks are decided by ONE predicate, `endlessPerkDueAtDepth(depth)`
  (depth = the zone just cleared), for three reasons: the every-4th-zone cadence
  (`ENDLESS_PERK_EVERY`, depths 1, 5, 9, …); a cleared MILESTONE zone of ANY class
  (25, 50, 75, 100, …), the payoff for surviving the S-tier slate; or the zone right
  after a depth where those two COLLIDED. A collision — a depth where `depth % 25 == 0
  && depth % ENDLESS_PERK_EVERY == 1` — would otherwise hand out one perk where the player earned two, so the
  second is DEFERRED by a zone instead of being swallowed; the cadence is unaffected
  and carries on from its own schedule. Derived purely from the depth, deliberately:
  no "owed perk" flag to persist, so it needs no save field and comes out the same
  across a save/reload or a mid-zone bail. `endlessPerkDepthDone` still caps it at
  one pick per depth, so re-entering the same outpost can't farm a second.
- How WIDE that pick is comes off the same depth, via `endlessPerkOffersAtDepth`:
  `ENDLESS_PERK_OFFERS` (3) normally, `ENDLESS_PERK_OFFERS_MILESTONE` (5) after a
  cleared milestone of any class — the forced slate pays in CHOICE as well as cash.
  A collision's deferred half lands on the zone AFTER the milestone, an ordinary
  depth, so it deals three. The other three callers of
  `endlessGeneratePerkChoices` pass their width explicitly rather than letting the
  generator read the depth, which would have quietly upgraded every one of them at
  a milestone outpost: the gamble win and Breakthrough deal 3, and **Buy Extra Perk
  deals `ENDLESS_PERK_OFFERS_BOUGHT` (4)** as of 2026-07-28. The E-Shop pick is the
  most expensive thing in the mode ($70,000 + $2,500/zone, doubling per buy, ×11 at
  the surcharge cap) and was landing the same 3-wide slate the free cadence hands
  out, so the money bought a perk but no say in which — the same complaint the
  milestone slate was widened to answer. Four rather than five keeps the milestone
  the widest deal in the game. The offer count also drives the menu's row count
  (`configure_endless_perk_menu`), so a wider pick needs no layout work: 16px rows
  from y=38 put "Take the Cash" at y=134 even at five, clear of the help line. No
  save bump either — the on-disk offer list has held five slots since v13.
- The cadence was 3 (depths 1, 4, 7, …) until 2026-07-25; it is now 4.
  - **Breakthrough.** `endlessBreakthroughAllowed` (`endless_course.c`) bars the
    boon from any zone whose clear already owes a scheduled perk, so it is blocked
    on ~1 zone in 4 from the cadence (it was ~1 in 3 at a cadence of 3) plus the
    milestone depths. That partly offsets the slower cadence on its own, which is
    why no Breakthrough constant was retuned alongside it.
- **The minor (25th) milestone pays a perk too**, as of 2026-07-27 — it was the one
  forced slate with no payoff, which read as a pure tax. `endlessPerkMilestoneAt`
  now accepts every milestone class rather than kinds 1 and 2, which also makes the
  deferral branch REACHABLE again: collisions are depths 25, 125, 225, … (`25k % 4`
  is 1 on every other minor milestone), so a run reads …21, 25, **26**, 29… . It
  had been dead code at the 50-only schedule (`50k % 4` is only ever 2 or 0).
- Perk-completion pacing: 25 perks / **77 total stacks** (sum of `maxStack` in
  `endlessPerkTable`). Every forced pick is worth exactly +1 stack regardless of
  which row you take, because `endlessGeneratePerkChoices` only pools perks with
  `owned < maxStack` — so an offer is never wasted and the completion depth is
  order-independent. Picks banked by depth = `floor((d-1)/ENDLESS_PERK_EVERY) + 1
  + floor(d/25)` (a collision depth banks one of its two that zone and the other
  the next). At a cadence of 4 the 77th pick lands at **depth 265**; waypoints:
  z50 = 15 picks, z100 = 29, z150 = 44, z200 = 58, z250 = 73. Past the last pick the scheduled gate still opens with an empty
  pool — `configure_endless_perk_menu` then renders the "Take the Cash" row alone,
  so it degrades into a recurring cash payout rather than breaking. A zero-wide
  slate is clamped back up to a standard three in `endlessPerkDeclineBonus`, and by
  then every stack is owned, so that tail pays at the capped rate.
- Zone-100 credits: clearing zone 100 rolls `JE_playCredits` once, at the outpost
  that follows (top of `endlessBetweenLevels`, before the course roll and the
  auto-save), then the run carries on into zone 101. `endlessCreditsShown` gates
  it and rides the save (v9), so reloading the zone-101 outpost — or bailing out
  of zone 101 with Quit Level — never replays them. The test is `>=` so a debug
  zone jump over the mark still gets its one showing.
- The fork's credit — three consecutive rows, "OpenTyrian 2000" / "Engaged" /
  "wlfn" — is SPLICED into the roll in code, leaving `tyrian.cdt` (and its
  Switch/Vita romfs copies) untouched at exactly 126 records. `JE_playCredits`
  reads the file, then finds the insert point by SCANNING — last non-blank line
  (the "The   End" card, row 119), up to the top of the blank run above it (row
  111), then +4 — rather than hardcoding a row, and memmoves the tail down.
  Result: 4 spacers, the card, 4 spacers, "The End", which is the same gap the
  roll puts between credited people. Everything below keeps its position relative
  to the END of the roll, so the `lines_max - 8` song fade and the final held
  frame (which parks "The End" at y=81) land exactly where they always did.
- Deep-run danger escalation (`endlessDangerRamp`, a SCALE — not a percent). It's a
  TWO-STAGE ramp off `endlessDifficultyZone`: a gentle first stage 0→100 across
  zones 40→100 (`MID_SCALE`, byte-for-byte the old single-stage ramp — zone≤100
  unchanged), then a STEEPER second stage 100→500 across zones 100→250
  (`FULL_SCALE`), capped. So it tilts the Chart-a-Course rolls ~2× harder by zone
  100 and ~6× by zone 250, then holds. Levers (baseline → zone 100 → zone 250 cap):
  widen share 50→75→85% (capped, so a few legible curated themes survive); bits avg
  ~2.8→~4.05→5; boon-course roll 1/3→1/6→1/18; gambit boon-graft 35→15→5% (floored);
  every rare/super-rare injection routes its "1 in N" through `endlessDangerRareDiv`
  (~N/2 at mid, ~N/6 at cap — e.g. Apex 1/40→1/20→1/6, Deadgen 1/55→1/27→1/9);
  Jackpot 1/25→1/50→~1/150. The two danger-only whole-visit flavors use a percentage
  form HARD-CAPPED below certainty (`ENDLESS_DANGER_GAUNTLET_CAP_PCT` 45,
  `..._AMBUSH_CAP_PCT` 15), and they SATURATE at those caps by ~zone 160-190 — so the
  jump from ~5× to ~6× lands entirely on the uncapped levers (rarer boons/jackpots,
  more frequent rare injections), NOT on the "no safe route" odds. Gauntlet 14→25→45%,
  Ambush 5→9→15%; a calm route still survives ~46% of visits even at the deepest cap —
  danger is never a sure thing. None of these change the endlessRand DRAW COUNT (only
  thresholds/moduli), so the seed stream stays aligned; course 0 stays clean unless
  Gauntlet/Ambush fires. Past zone 250 the course tilt is frozen at ~6×; the always-on
  enemy levers (stats, extra shots, contact damage) keep climbing.
- Sector variety is all combinations of the existing `endlessModTable` bits, so
  it needs no new bits/save bump: the danger score, monitor rows, tier/rank and
  payout are all mod-agnostic. Sources, in generation order: distinct named
  hostile themes; a ~50% "widen" that swaps in a random 1-5 bit combo of the
  `combinable[]` hostiles (weights lean toward 2-4 bits); a ~1/3 boon course
  (60% a named boon theme, 40% an emergent 2-3 bit boon combo from
  `endlessMakeBoonCombo`). Pure-boon and Jackpot generation swap the
  Turbodrive/Overblast rarity slots, making Overblast the common roll while the
  theme table remains the canonical name dictionary. MIXED "gambit" sectors —
  after the boon roll,
  ~35% of each ORDINARY hostile course gains one compatible boon
  (`endlessPickMixBoon`), welding reward onto danger. Kill-fire boons get a
  separate 4% roll within those gambits instead of ordinary candidate weight,
  keeping Turbodrive/Overblast rare in hostile+boon combinations.
  Compatibility avoids same-lever cancels (no frail+fortified, no
  dilation+swift/overclock) and keeps the one-kill-fire rule (only one boon
  added). Un-named combos read with the right tone via three generic-name pools
  (ominous / fortunate / gambit), chosen in `endlessComboNameSalted` off
  `ENDLESS_HOSTILE_MASK` vs `ENDLESS_BOON_MASK`.
- Names are unique per chart: distinct bitsets can hash to the same generic
  word, so a final RNG-free pass in `endlessGenerateCourses` (after the danger
  sort — it must consume no `endlessRand`, keeping the seed stream aligned)
  bumps `endlessCourseNameSalt[]` until every offered label differs; the salt
  steps a generated name to the next word in its pool and is a no-op on curated
  names. Curated names must therefore be unique across ALL theme tables — the
  evil twins of "Crossfire"/"Death March" were renamed "Friendly
  Fire"/"Forced March" for this — and must not reuse a generic-pool word.
- `endlessMixedThemes[]` supplies cosmetic names for the common gambit combos
  (pairs→quads + a few double-boon rares); `FRAGILE|DEVASTATING` is intentionally
  absent (it's the hostile table's "Glass Cannon"). The single-danger guarantee
  (≥4 courses) skips any course carrying a boon bit, so a gambit is never
  flattened into a plain single.
- Adding sector names — the four constraints, all verifiable before a build:
  (1) **Unique across every table AND the three generic pools** (the salt pass
  only ever steps a *generated* name, so two curated labels that read alike can
  land on one chart). (2) **`font_ascii`-displayable only**: letters, space,
  apostrophe, hyphen — every other punctuation mark maps to −1 and draws
  nothing. (3) **Fits the course list**: `JE_dString` draws it in
  `SMALL_FONT_SHAPES` at x=166 on a 356px screen, so the practical ceiling is
  ~134px ("Thread the Needle", the widest label shipped); `menuInt` rows hold 23
  chars + NUL on top of that. (4) **`endless_internal.h` declares the pools with
  explicit bounds** (`endlessHostileThemes[256]`, `endlessBoonThemes[122]`, …)
  because `endless_course.c` sizes stack arrays off their `COUNTOF` — growing a
  table without updating its extern is a `C2078 too many initializers`.
  Two deliberate holes in the hostile pairs: Overclock+Slipstream (Overclock
  already carries the same scroll, so the row would be a redundant bit) and
  Gravity+Sluggish (that pairing is Tar Pit and must stay rare-injected — a row
  in `endlessHostileThemes` would let the ordinary signature draw deal it).
- Four dangers reuse the enemy-death / enemy-projectile / player-damage systems
  rather than the enemy layout, so each is a small engine hook plus an
  `endless_combat.c` decision (bits 40-43, all in `ENDLESS_HOSTILE_MASK`):
  - **Martyrdom** (bit 40, weight 18, RARE — own pool `endlessMartyrdomThemes`,
    injected ~1/22): a destroyed enemy fires a radial burst — 4 cardinal
    (normal) / 6 (elite) / 8 (champion), evenly spaced. Spawned at BOTH
    enemy-death sites in tyrian2.c (`endlessSpawnMartyrBurst`, which owns the
    `enemyShot[]` pool), gated by `endlessMartyrdomBurstShots(linknum,
    eliteState)`. That gate DEDUPS per linked enemy exactly like
    `endlessCountKill` (a file-static "last link", reset each level in
    `endlessResetElites`), so a multi-tile enemy bursts once, not per tile;
    linknum 0 (lone) always fires. Suppressed when fewer than `shots + 48` of
    the 500 enemy-shot slots are free (the "pool nearly full" rule). The bullet
    SPRITE is Martyrdom's OWN fixed graphic — `endlessMartyrShotSprite` returns
    the constant `ENDLESS_MARTYR_SHOT_SGR` (100: a fat radially-symmetric orb in
    `spriteSheet8`, which is loaded once from `tyrian.shp` and so is valid in
    every level and episode). Symmetry is a requirement, not taste: the burst
    fires 4/6/8 directions at once, so a directional bolt would look wrong on
    most of them. Bullets are slow (3 px/tick, fixed — the spec's "slow"), base
    damage 4 × `endlessShotDamagePercent`.
    - PER-DIRECTION SPEED — tried and reverted: `sxm`/`sym` are WHOLE pixels per
      tick (`JE_integer`) and the render list extrapolates the smooth path from
      those same integers (`rl_current_vel_x` is an `int`), so a fractional carry
      would desync it. A true 45° diagonal therefore has exactly TWO expressible
      speeds — (2,2) = 2.83 px/tick and (3,3) = 4.24 — against the cardinals'
      3.00, with nothing in between; the only way to land between them is to skew
      the diagonals off 45° (e.g. (3,2) = 3.61, a pinwheel). Both were built and
      both were rejected: the burst keeps the plain even ring at one speed for
      every direction. Don't re-litigate this without new precision in the shot
      struct AND the render list.
    - HISTORY (two GOTCHAs, both now moot): the first cut derived the sprite from
      the level's own fire — `endlessNoteEnemyShotSprite` captured the last enemy
      bullet fired and the burst reused it. That capture originally filtered to
      sgr <500 and skipped the burst on a 0 sprite, making Martyrdom INVISIBLE on
      ep4/5 levels whose bullets are all ≥500 spark sprites (patched with a
      fallback + widened capture). The design itself was the real bug: because the
      capture was last-writer-wins across the whole level, the burst CHANGED
      APPEARANCE mid-level as different shooters came on screen. A hazard the
      player has to read on sight can't look like a different thing every minute,
      so the capture was deleted outright (state, setter, `extern`, and the
      tyrian2.c shot-spawn call) in favour of the fixed sprite above.
  - **Seeker Rounds** (bit 41, weight 14, RARE — own pool `endlessSeekerThemes`,
    injected ~1/24; +4 Seeker+Swift synergy): each enemy shot makes ONE bounded
    course correction toward the player ~0.5s after firing, then never again —
    distinct from Homing/Kamikaze (which bend enemy MOVEMENT). State rides on the
    shot itself: a new `EnemyShotType.seekerArm` byte (carved out of `fill`), set
    to 1 at the single spawn site only when `endlessSeekerActive()`, 0 for every
    other shot. The shot-move loop counts it up and, at
    `ENDLESS_SEEKER_DELAY_TICKS` (17 ≈ 0.5s at 35 Hz), calls
    `endlessSeekerCorrect` then disarms it (0). The turn is capped at ~23°
    (precomputed cos/sin, no `acos`): if the player is already within that cone
    it snaps straight, else it rotates by exactly the cap toward the player's
    side (sign from the 2-D cross). Speed is preserved; the render list records
    the post-turn velocity that tick, so the smooth path just kinks once.
  - **Static Discharge** (bit 42, weight 11, COMMON — in `endlessCombinableMods`
    AND the shuffle themes): taking shield/hull damage bleeds generator power,
    capped at the current reserve. Hook is in `JE_playerDamage` (main player only
    — `power` is its generator). TWO gotchas, both hit in testing: (1) must use the
    real shield+armor DROP (`oldShield - shield` + `oldArmor - armor`, losses only
    so a revive's armor refill can't go negative) — NOT the function's return
    `playerDamage`, which is 0 whenever the shield fully absorbs a hit (the common
    early-game case). (2) THE DRAIN ALONE DOES NOTHING — the real reason this read
    as broken through two attempts. The raw `power` pool is 0..900 (gauge shows
    `power/10`) and the generator repays it EVERY TICK (`power += powerAdd`,
    ~5–23/tick), so even draining the full 900 is refunded in about a second and no
    magnitude is ever felt. So a hit ALSO locks generator regen out for a
    damage-scaled window (`ENDLESS_STATIC_LOCKOUT_PER_DMG` 6 ticks/point, capped
    `..._MAX` 70 ≈ 2s, longest window wins), applied at the one existing regen seam
    `endlessGeneratorPowerAdd` — the same hook DEADGEN uses — ticked down in
    `endlessGameplayTick` and cleared per level. THAT is what makes the loss stick
    (confirmed in play-testing — the drain and the lockout together are what the
    effect actually is; the drain on its own is unobservable at any magnitude):
    the drain (`× ENDLESS_STATIC_POWER_PER_DMG` 30, floored at `..._POWER_MIN` 150
    so a graze still reads, lockout floored at `..._LOCKOUT_MIN` 25) persists to
    drop `power` under a shot's cost and stall the front gun (`if (power <
    power_use) return` in shots.c) and to stall shield regen (which also spends
    power). Rear guns / sidekicks / specials are power-free, so it's brutal, not
    unwinnable — the same balance DEADGEN relies on. INCOMPATIBLE with Dead
    Generator: `endlessStaticDischargeDrain` returns 0 when DEADGEN is also set
    (belt-and-suspenders — DEADGEN is injected-only and overwrites the whole
    course mod, so the two never co-occur via generation anyway).
  - **Retaliation** (bit 43, weight 15, UNCOMMON; +5 Retaliation+Enrage synergy):
    every kill (re)opens a ~1s window (`endlessRetaliationTimer`,
    `ENDLESS_RETALIATION_TICKS` 35) during which ALL enemy fire is ~25% quicker.
    Refreshed (not stacked) in `endlessCountKill`, drained in
    `endlessGameplayTick`, reset per level. It folds into `endlessFireDelayPercent`
    as a final MULTIPLY (×80%), not another additive reduce, so it still bites
    once the depth/mod cooldown reduce has hit its floor — and reads as a
    kill-TEMPO tax, unlike Enrage's TIME-based climb. RARITY: it's in the shuffle
    themes (not the common combinable pool) PLUS a dedicated ~1/12 injection
    (`RARE_PICK` on its own `endlessHostileThemes` rows) — the shuffle alone put
    it at ~the Rare rate, so the injection lifts it to a genuine Uncommon,
    between Static (common) and Martyrdom/Seeker (rare).
- All four are also eligible in **The End**: `endlessMakeTheEndMods` rolls an
  independent coin for each (1280 finale combinations now), so a zone-100+
  finisher can carry any mix of them. Static is safe there because the core
  omits DEADGEN.
- **Ten later BOONS** (bits 44–53, all in `ENDLESS_BOON_MASK`). The earlier boon
  roster already covered enemy HP, cash, kill-fed guns, weapon damage, enemy shot
  speed, shop prices and the elite TIER; these deliberately pick systems none of
  those touch, so none is a reskin of an existing lever. Most carry a NEGATIVE
  `endlessModTable` reward — an easier sector pays less, the mirror of a hostile
  bit's positive reward — and the survival ones also grant an
  `endlessBoonMitigation` credit so a gambit's tier reads its true net danger.
  - **Aegis Gate** (bit 44, −5, credit 5): while the shield holds, a hit can't
    spill into armor — the gate dumps the remaining shield and eats the rest.
    Hooked in `JE_playerDamage` right after `shield = 0` and BEFORE `cmHullHit`,
    so a blocked hit reaches neither the hull, the armor gauge, the Countermeasure
    burst nor the death path. THE COOLDOWN IS THE BALANCE
    (`ENDLESS_AEGIS_COOLDOWN` 70 ≈ 2s, per level, ticked in
    `endlessGameplayTick`): without it a single regenerated shield point would
    block forever, and Shield Matrix would make that trivial.
    `endlessAegisGateConsume` ARMS the cooldown when it answers true, so exactly
    one caller may ask per hit.
    TWO things make it FELT, and the first cut had neither — it read as completely
    inert in play even though the logic was right:
    (1) `ENDLESS_AEGIS_MIN_SPILL` (2). A shield only ever overflows on the hit
    that finishes it, so the spill is whatever the shield couldn't cover — often a
    single point. Gating those spent the whole 2-second window to save 1 hull and
    left the gate on cooldown for the champion railgun a moment later: it fired
    constantly and was worth nothing. Skipping trivial spills keeps the gate
    LOADED for the hits that matter.
    (2) A distinct cue. It originally reused the shield's own flare and
    `S_SHIELD_HIT` — what *every* ordinary hit already plays — so a block was
    indistinguishable from being hit normally. It now draws the full nine-point
    shield-absorb ring and plays `S_CLINK`, which nothing else in the damage path
    uses.
    KNOWN DESIGN PROPERTY (per spec, not a bug): the gate needs `shield > 0`, and
    a block empties the shield, so under sustained fire — where the shield is
    pinned at 0 — it does little. It is at its best beside Shield Matrix /
    Auxiliary Reactor, which is the synergy the boon was specified for.
  - **Flak Screen** (bit 45, −5, credit 5): halves what `endlessExtraEnemyShots`
    adds, rounding the kept half UP — so only the Endless-specific projectile
    multiplication thins and every authored firing pattern still plays as
    designed. Gated on `endlessTideBoonsUnlocked` (the tide adding ≥1 shot at
    all), else it would be an empty green row before zone ~25.
  - **Auxiliary Reactor** (bit 46, 0, credit 3): shield regen costs no generator
    power. Same interval, so it is mechanically distinct from Shield Matrix
    (shorter interval) and Efficient Coils (cheaper FIRING). Note the regen gate
    `power > shieldT` must be relaxed too, not just the `power -= shieldT` — an
    empty reserve must not stall a recharge that costs nothing.
  - **Low Profile** (bit 47, −8, credit 7 — the biggest cushion of the set): the
    DAMAGE hitbox shrinks to 75%. Applied at the collision TESTS via
    `endlessHitboxScale`, not to `player[].shot_hit_area_*`, for two reasons:
    shrinking the source fields would also shrink the ITEM-collect reach and the
    Countermeasure sweep, and the helper is the identity outside the boon so no
    other game mode changes. Both damaging tests route through it — enemy
    projectiles (tyrian2.c) and enemy contact (mainint.c). In `JE_playerCollide`
    the shrunk box is added ONLY to the damage branch; the outer 12×14 test also
    collects pickups and powerups, and a boon must never make items harder to grab.
  - **Giant Killer** (bit 48, −6, credit 5): `endlessEliteHpMult` returns 1 and
    `endlessEnemyHpMult` drops the elite-boss ×2 bump. Elites/champions still
    spawn, keep their tint, their aggression and their FULL bounty — which is what
    separates it from NOELITE, which deletes the tier and its income outright.
    Gated on the same `endlessEliteBoonsUnlocked` 25%-share unlock.
  - **Clean Signals** (bit 53, −5, credit 4): the exact complement — champion
    `endlessChampionFireDelayPercent` / `...ShotDamagePercent` both return 100, so
    the tier keeps HP, tint and bounty but loses its offensive bonuses. Same gate.
    Those two are CHAMPION-only in the engine (`eliteState == 3`), so on its own
    the boon did nothing to plain elites while its monitor row promised the whole
    special tier. The one offensive bonus an elite does carry is the RAM premium
    (elites +25% / champions +50%, mainint.c), so that moved behind
    `endlessEliteContactPercent` and Clean Signals flattens it to 100 as well.
    That keeps it distinct from Soft Landing, which scales ALL contact damage:
    this removes only the premium special enemies add, so the two stack without
    overlapping.
  - **Shockwave** (bit 49, −4, credit 3): an elite/champion death vaporises enemy
    bullets within 40px (elite) / 60px (champion); a boss bar emptying clears the
    whole field (`endlessShockwaveClear` radius −1). Deduped per linknum exactly
    like Martyrdom. Unlike the Chain Reaction perk it may fire STRAIGHT from the
    death sites inside the player-shot loop, because it only touches
    `enemyShot[]`/`enemyShotAvail[]` and never `enemy[]` — so it cannot disturb
    that loop's per-linkgroup kill bookkeeping.
  - **Soft Landing** (bit 52, −4, credit 3): folds into
    `endlessContactDamagePercent` as a final ×30%, so it bites into the depth ramp
    AND the elite/champion ram bonuses (a deep champion ram is the case it exists
    for). Projectiles untouched, which keeps it distinct from a general damage cut.
  - **Star Charts** (bit 50, 0, NO mitigation credit) and **Breakthrough**
    (bit 51, −10, no credit): the two whose reward lands at the NEXT outpost, so
    they buy no in-sector safety and must not soften a gambit's tier. Both are
    banked by `endlessOnSectorCleared` (called from tyrian2.c right where
    `endlessRunDepth` is bumped, since `endlessActiveMods` is overwritten by the
    time the outpost charts again) into run state that rides the save (**v12**).
    Star Charts sets `wantCourses = ENDLESS_MAX_COURSES`; it is HELD, not spent,
    on a milestone visit (whose full slate is the point of the zone) and is handed
    BACK if an Ambush later collapses the visit to one sector — the boon promises
    a real choice, so a visit with none to give doesn't consume it.
    `endlessBreakthroughOwed` is a COUNT, not a flag, so two can queue; the
    outpost spends one only when no scheduled perk already claimed the visit
    (gated on `!endlessPerkPending`, so the scheduled perk always wins), which is
    the deferral the collision rule asks for.
  - Breakthrough is the rarest boon in the game: `ENDLESS_BREAKTHROUGH_PCT` 7% of
    the ~1-in-3 visits that deal a boon course ≈ 1 visit in 45, from its OWN pool
    (`endlessBreakthroughThemes`). It has no `endlessBoonThemes` row at all —
    that is precisely what keeps the ordinary boon deal, the emergent boon combo
    and the Jackpot from ever handing out a free perk pick. It is also barred
    below zone 5 and on any zone whose clear already owes a scheduled perk
    (`endlessBreakthroughAllowed`); the roll is made unconditionally and the gate
    applied after, so the seed stream stays aligned.
  - `endlessLockedBoons()` is the single source of "which boons would be EMPTY at
    this depth" — read by the boon deal, the Jackpot and the final scrub in
    `endlessEnforceEliteRules`, so all three forbid exactly the same set.
- Safe-special filter: a pickup special needs a non-empty name, a
  dispatcher-handled effect type (stype 1..18), and an in-range `itemgraphic`.
  The HUD redraws the equipped icon every frame, and an out-of-range icon reads
  past the sprite offset table and crashes instantly (several arcade-internal
  specials carry a bad icon).
- The outpost gamble is a neutral-EV, high-variance ladder: a weighted 0..99
  table plus shared ~1/5000 ultra-rare draws (mega jackpot / Kamikaze rush / free
  perk). Every outcome reuses an existing lever and is applied through
  `endlessApplyGambleOutcome`, so the random path and the debug screen's forced
  outcomes can't drift apart.

### Economy & perk plumbing

- Shop prices inflate +19% per cleared depth (cap 100x); the first 5 zones ramp
  at half slope (+9.5%), then the full slope resumes at zone 6, leaving a ~0.4x
  discount across the rest of the run. `endlessRunDepth` is constant within a
  visit, so every `JE_getCost` call scales consistently.
- Rapid Recharge speeds the special-weapon cooldown and sidekick ammo-refill
  counters (not the main guns). Its decrement accumulator is stateful: sample it
  once per tick in the main-player block, then reuse the sampled value.
  It has a THIRD effect on a different lever: `endlessPerkChargeTicks` shortens
  the charge-sidekick charge interval (20 ticks, −`ENDLESS_PERK_CHARGE_STEP` per
  stack, floor `ENDLESS_PERK_CHARGE_MIN`). That was its own perk, "Rapid Charger",
  until 2026-07-28 — a perk that did nothing at all unless you happened to fly a
  charge sidekick, which made it a dead offer most of the time. Folding it in
  makes one perk cover every sidekick: magazines refill quicker, charges build
  quicker. Both halves cap at 4 stacks, so the merge is 1:1 for the charge half.
- **Ordnance Reserves** (`PERK_ORDNANCE`, max 4) is one perk with two halves, both
  hanging off levers that already existed:
  - *Sidekick magazines.* `endlessPerkSidekickAmmo(base)` grows a shipped
    `option.ammo` by `ENDLESS_PERK_AMMO_PCT` (30%) per stack — rounded UP to at
    least +1, so a 5-round MegaMissile actually gains a shot — capped at
    `ENDLESS_PERK_AMMO_CAP` (250). Applied where `JE_drawOptions` seeds
    `sidekick[].ammo_max`. The refill cadence deliberately keeps using the
    **shipped** size: vanilla computes it as `(105 - ammo) * 4` into a `uint`, so a
    boosted magazine past 105 rounds would wrap it huge and stall the reload
    forever — and a bigger magazine shouldn't come with slower reloads anyway.
  - *Special duration.* `endlessPerkSpecialDuration(base, cap)` stretches the
    timed specials by `ENDLESS_PERK_SPECDUR_PCT` (30%) per stack at every
    `flareDuration` / `astralDuration` / `invulnerable_ticks` assignment in
    `JE_specialComplete`. `cap` exists for the byte-wide fields (`astralDuration`).
    `zinglonDuration` is the one duration deliberately left alone: its beam
    brightness is drawn as `25 - abs(zinglonDuration - 25)`, a ramp that only
    works on the stock 50 ticks.
  - The shop name has to advertise the boosted number, so `JE_labelAmmoSidekicks`
    (episodes.c) now REBUILDS every `"<name>   Ammo N"` label instead of only
    adding the two that shipped without one. `JE_loadItemDat` snapshots the bare
    names + shipped magazines once (after the Charge-Laser and custom weapon have
    claimed their slots), and the relabel renders from that snapshot, so it is
    idempotent however often it runs. It self-guards on the current bonus %, which
    is why the shop frame loop and `JE_drawOptions` can just call it every time.
  - `AMMO_GAUGE_STEP` (player.h) replaces the open-coded `MAX(1, ammo_max / 10)` at
    the three HUD gauge draws. Ceiling division keeps a full magazine at ≤10
    segments inside the 29px strip; the shipped sizes (5/10/20/80) are unchanged by
    it, but a 26-round boosted magazine would otherwise have drawn 13 segments and
    run off the end of the bar.
- **Failsafe** (`PERK_FAILSAFE`, max 2) grants `ENDLESS_PERK_FAILSAFE_TICKS` (9 ≈
  0.25s) of invulnerability per stack when a hit reaches the HULL, hung off
  `cmHullHit` in `JE_playerDamage` — the same flag the Countermeasure burst uses,
  which is deliberately set before the armor deduction so `cheatInfiniteArmor`
  can't silently disarm it. Three things make it need no cooldown of its own:
  - It EXTENDS `invulnerable_ticks` instead of assigning, so it can never cut short
    a longer window already running — notably the 100 ticks a spent revive grants a
    few lines above, which would otherwise be truncated to 18.
  - It cannot chain. Arming it requires hull damage, and `invulnerable_ticks` gates
    both damaging tests (enemy shots in tyrian2.c, contact in mainint.c), so no hit
    lands while it runs.
  - Dead ships are skipped (`is_alive`): a respawn sets its own 100 ticks, and
    arming a corpse would be meaningless anyway.
  The readout is free — the post-hit transparency flash in `JE_playerMovement`
  already draws exactly while `invulnerable_ticks > 0`, so the perk is visible
  without a tell of its own.
- **Financier** (`PERK_FINANCIER`, max 4) is the economy perk, and it works both
  ends of the ledger. It was "Compound Interest" (the interest half alone) until
  2026-07-28; the ENUM ENTRY was renamed, not moved, so its on-disk slot and every
  slot after it are untouched and no save bump was needed.
  - *Interest.* Level-clear bank interest is `ENDLESS_INTEREST_BASE_PCT` (10%) of
    unspent cash, +5 points per stack (max 4 → 30%), capped at `3000 + depth*80`
    scaled by the SAME rate factor — a rate rise with a fixed cap would just hit
    the ceiling a level sooner and pay nothing extra. The multiply is split into
    hundreds + remainder so a big bank can't overflow 32-bit `long`.
  - *Discount.* `endlessPerkShopCostBp` returns what the outpost charges in BASIS
    POINTS (10000 = unchanged, 6700 at 4 stacks). Basis points because
    `ENDLESS_PERK_DISCOUNT_BP` is 825 = 8.25%/stack, which has no whole-percent
    form and would lose a third of its value to truncation as an integer percent.
    Applied in `JE_getCost` as a multiplier on the depth-scaled `pct`, right after
    Merchant's Favor's `* 65 / 100`, so the depth ramp, the Loan Shark tax and both
    discounts COMPOUND rather than overriding one another. Floor needs no guard: the
    worst case (depth 0, Favor, 4 stacks) still lands at `pct` 43, not 0.
    It rides the same lever as the tax, so its coverage is exactly the tax's — the
    buy/sell shop, not the E-Shop buys, which price themselves via `endlessRebuy`.
- **"Take the Cash"** — the decline row every perk pick carries — is a buyout, not a
  consolation prize, as of 2026-07-28. It was a flat `1000 + depth*200`: roughly a
  fifth of a zone's income at *every* depth, which is to say never once the better
  play. `endlessPerkDeclineBonus` now prices off `endlessClearBase()` (which is why
  the shop file's base function is no longer static, and why the buyout can't drift
  behind a retuned economy) at `ENDLESS_PERK_DECLINE_MULT` — 25 tenths, ×2.5 — and
  then by the two things the flat line ignored:
  - *The slate on the table.* `offers / ENDLESS_PERK_OFFERS`, clamped UP to a
    standard three, so a milestone's five-perk deal costs ×1.67 to walk away from
    while a pool thinned to one offer — or to none, past the 77th pick — never pays
    less than an ordinary one.
  - *The collection already flown.* `ENDLESS_PERK_DECLINE_OWNED_PCT` (6%) per owned
    stack, capped at +150% (reached at 25 stacks). Deliberately the extra-perk
    surcharge read backwards: a perk is dearer the more you hold, on both sides of
    the counter. It cannot feed itself, because declining is the one move that never
    raises your own stack count — so a serial decliner's buyout stays flat, while a
    player who banked perks first cashes out at the higher rate.
  Scavenger applies last, as to every other endless cash source, and its description
  now says "buyouts" alongside clears and bounties. Measured against a zone's income
  (clear ≈ 2.5× base, plus capped interest) that lands at ~0.5 zones at depth 1
  — early perks *should* win — ~1.5 by zone 50, and ~3.5 at a deep milestone. The
  multiplies are stepped with a divide between each: the depth-capped base (60000)
  against all four factors combined would overflow a 32-bit `long`.
  `endlessPerkTotalOwned` moved from `endless_shop.c` to `endless_perks.c` for this
  (both prices need it) and is declared in `endless_internal.h`.
  One ordering consequence, left as-is because it's a fair reward rather than a
  leak: `endlessShopEntryCash` is frozen in `endlessBetweenLevels`, and the perk
  screen opens *after* that (the front gate in `JE_itemScreen`), so buyout cash
  doesn't inflate the E-Shop's cash-fraction prices until the next visit.
- **In-level special-weapon drops are converted to gun powerups.** Vanilla events
  33/45 (`tyrian2.c`) rewrite the front-powerup dropper (enemy 533) into one of the
  six special-weapon droppers (829..834, each `value = 32100 + specialId`) on the
  roll `lives == 11 || (mt_rand() % 15) < lives` — and `player[0].lives` aliases
  `items.weapon[FRONT_WEAPON].power` outside arcade, so the roll is really "chance
  proportional to front-gun power, guaranteed once it's maxed at 11", i.e. it fires
  exactly when a front powerup would be wasted. Endless already grants a guaranteed
  random special for every *collectable* datacube and secret orb it converts, so a
  third source just re-rolls what the player was handed; `endlessPowerupDropEnemy`
  (`endless_combat.c`) instead returns the front (533) or rear (534) powerup at even
  odds. Enemy ids verified by parsing `tyrian.hdt` with the `JE_loadItemDat` record
  layout: 533 `value=-1`, 534 `value=-2`, 399 `value=5000` (the top gem of the
  390..399 ladder, same shapebank 21 as the powerups, so it reads as part of the same
  pickup set). Note the event handlers *overwrite* `eventRec[].eventdat` in place —
  that's vanilla's own behaviour, and harmless because every level load re-reads the
  event table.
- **The maxed-gun redirect lives in `JE_makeEnemy`, not at the drop site.** Deciding
  it where the event fires is wrong twice over: the event runs long before the enemy
  dies (a gun can fill in between), and it only ever inspects `eventdat == 533`, so
  the rear-powerup drops (534) a level scripts directly sail past untouched — which is
  why a maxed player still saw powerups. `endlessResolvePowerupDrop` instead hangs off
  `JE_makeEnemy`, the single choke point every enemy spawn goes through (vanilla
  already remaps 534 → 533 there for super-arcade): a 533/534 whose port is unmounted
  or at power 11 falls through to the other gun, and with both full becomes enemy 399.
  Same test as `power_up_weapon`'s `can_power_up`, so a pickup can never land as the
  +1000 cash consolation. Everything else passes through by id, and because the swap
  stays inside the `value < 30000` class it doesn't disturb the `enemy_offset` choice
  the caller made from the pre-swap id.
- **Datacubes come in two shapes, and only one of them still grants a special.**
  `value == 1` is a sentinel in the enemy data meaning "data cube", not cash — the
  difficulty scaler in `JE_makeEnemy` deliberately scales only `value > 1 && < 10000`,
  so a cube can never be multiplied into a payout. Two delivery mechanisms use it:
  - *Collectable* — an `armor == 0` pickup (enemy 513) a linkgroup drops on death.
    You fly into it; `mainint.c`'s scoreitem branch handles it. Endless still calls
    `endlessGrantSpecial()` here, and likewise for the secret-level orbs
    (`value > 10000`, enemies 823/843), because both are things the player chose to
    collect.
  - *Carried by a shot enemy* — an armored piece whose own `value` is 1, resolved in
    the linkgroup-death loop in `tyrian2.c`. Nothing appears on screen; vanilla just
    does `cubeMax++`. Endless used to grant a special here too, which read as a whole
    special weapon materialising out of nowhere at a boss kill. It now calls
    `endlessDropCubeGem(slot)`, which spawns the 5000-point gem (enemy 399) at the
    dead piece — visible, consistent with the powerup-drop fallback, and the cash
    feeds the E-Shop where a special can be bought deliberately.
  - Where this actually bites: **TYRIAN**, both cuts (`lvlFileNum` 9 and 15 of
    `tyrian1.lvl` — `levels1.dat` lists two `]L` entries with the same name). Enemy
    type 53 sits in the closing structure spawned at `t=5356` (linkgroup 142, driven
    by the script until the level-end event at `t=8100`) and is the *only* piece of
    the 46..65 block with `value == 1`. Verified by parsing `tyrian.hdt` /
    `tyrian1.lvl` directly. Note the event records are **11 bytes**, not 12 — u16
    time, u8 type, s16 dat, s16 dat2, s8 dat3, s8 dat5, s8 dat6, u8 dat4, exactly as
    `JE_loadMap` reads them; assuming 12 desyncs the whole table.
  - The `endlessMode` gate at the call site is intentional rather than
    `endlessFxActive()`: a campaign run with the endless mods layered on still has a
    working cube archive, so `cubeMax++` remains correct there.
- **A spent Revive token buys a grace window, not just a hull.** Restoring armor
  mid-fight hands the ship straight back into the volley that killed it, so
  `endlessConsumeRevive` (`endless_shop.c`) also arms `endlessReviveGraceArm()` —
  `ENDLESS_REVIVE_GRACE_TICKS` = 105, ~3s at the 35Hz sim, per level like the Aegis
  cooldown, drained in `endlessGameplayTick`, cleared by `endlessResetZoneEffects`.
  While it runs, `endlessReviveGraceActive()` suppresses enemy bullets at two
  places in `tyrian2.c`: the `default:` turret case (**inside** the case, so the
  reloaded `eshotwait` still counts down and the non-shooting turret behaviours —
  the magnets, the Savara boss's launch puffs — carry on; only the bullets are
  withheld) and `endlessSpawnMartyrBurst`, so a MARTYRDOM sector can't refill the
  field with death bursts from the kills the player is making. Arming it inside
  `endlessConsumeRevive` rather than at the varz.c call site keeps it attached to
  the token itself. Launched *enemies* (`launchtype`) are deliberately untouched —
  they're spawns, and stalling them for 3s would desync boss scripts.
  The screen wipe itself stays in the `JE_playerDamage` revive branch, and now pops
  a `JE_setupExplosion` per cleared bullet: without it the wipe was invisible,
  indistinguishable from the shots simply having missed. Player shots are left
  alone on purpose — clearing them would cancel a superbomb already in flight, a
  bought consumable, for no defensive gain.

### Opening Salvo — `endless_perks.c`, `shots.c`, `varz.c`, `tyrian2.c`

Two timers, and the perk is the handoff between them. `endlessSalvoIdle` counts
ticks since the main gun fired; at `ENDLESS_PERK_SALVO_IDLE` (70 ticks = 2s) the
salvo is CHARGED. Firing then opens `endlessSalvoWindow` = `ENDLESS_PERK_SALVO_WINDOW`
(35 ticks ≈ 1s), during which everything the ship emits is x2.5 and power-free.
The charge was 50 ticks (~1.4s) until 2026-07-28; a 2:1 wait-to-burst ratio makes
holding fire a real cost rather than a rhythm you can sit in.

- The window drains on **real time**, one tick per tick, whether or not the
  trigger is held — the gauge counts it down in front of the player, so it has to
  be the clock they can see. No new charge banks while one runs, so at most one
  salvo is ever live.
- **The window is what let specials in.** `JE_doSpecialShot` runs BEFORE the
  main-weapon loop that arms the salvo, so the older same-tick flag could never
  reach a special. Anything spawning shots (stype 1, flares 5-11/16) now gets the
  tag free via `player_shot_create`; the rest are scaled by hand with
  `endlessOpeningSalvoScale` — repulsor push, attractor pull, invuln duration,
  repair amount. It carries a "must actually move" floor because the repulsor
  hands it a literal 1. The Zinglon pillar is a pseudo-shot at `MAX_PWEAPON-1`
  with no `playerShotData` entry, so it is scaled at its `damage = 10` site in
  tyrian2.c instead.
- Two clamps came with that, both endless-only so vanilla is untouched. The
  attractor's `exc/eyc` are **Sint8** and accumulate per activation (a x2.5 step
  reaches the wrap sooner, and a wrapped speed flings the pickup away). The repair
  specials leaned on `JE_drawArmor`'s blanket 28 clamp — which endless
  deliberately SKIPS for the reinforced hull — so they cap at `initial_armor`,
  the rule an armour pickup already follows.
- **The `stype` set is closed.** Dumped from `tyrian.hdt` (ep1-3) and
  `tyrian5.lvl` (ep4/5) by replaying `JE_loadItemDat`'s read order: both use
  exactly `{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,16,17,18}`. **stype 15 is in no
  shipped data and has no `case` in `JE_specialComplete`** — were it ever to
  appear the special would silently do nothing. 17/18 (spawn sidekick) are the
  only live types with no magnitude to scale.
- Every sector opens CHARGED: `endlessResetZonePerkTimers` parks
  `endlessSalvoIdle` at the threshold (not 0) and `endlessResetRun` matches. The
  reset still exists so neither perk timer inherits the previous sector's tail;
  it just resolves to "charged" now. Parking *at* the threshold is fine — the
  tick keeps incrementing and caps at 1000000.

**Readouts.** Both tells are deliberate: the perk is worthless if you can't see it.

- Gauge: `endlessOpeningSalvoGaugePercent()` (0..100) is the share of the bar that
  takes `ENDLESS_SALVO_GAUGE_BASE` (bank 12 + 1 = **193**) instead of the normal
  base — 100 while a charge is banked, then the window's remaining fraction, so
  the green recedes as the salvo burns and the bar is back to fire colour exactly
  when the boost ends. The split is measured against the bar's own height, not
  `BAR_MAX`, or the drain would be invisible on a part-full generator. It sits
  over the kill-fire tint: the rarer, player-timed state is the one worth reading.
- `draw_power_gauge` therefore mixes two banks in one bar. Both draw paths break a
  span at the salvo boundary (the vertical one adds it to the same-shade band
  test), and the sub-pixel AA row above the fill only reads green when the bar is
  *fully* green — it sits one row above `salvoRows`, so a draining bar caps in the
  normal colour. `salvo_render_prev/cur` interpolate the fraction at present rate,
  exactly like `power_render_prev/cur`, so the edge slides instead of stepping at 35Hz.
- The in-game palette is **palette 5** (`pcxpal[3-1]`), structurally identical to
  palette 0: bank 0 grey, 2 dark green, 7 red→yellow fire (the stock gauge, base
  113), 12 pure green (0,12,0 → 11,63,26). `base + 13` (the tallest band) must stay
  inside the bank; 206 does.
- Sparks: `salvo_shot_sparks` colours the trail from the shot's own dominant
  palette bank. `sprite2_dominant_bank` (sheets) and `sprite_dominant_bank`
  (tables, for the blended sprite > 60000 shots — Astral Zone, Protron Field,
  Plasma Storm) share `dominant_bank_of`, which picks among banks **1..15** only:
  bank 0 is the grey ramp and a weapon's white-hot core would outvote the hue that
  identifies it. Blended shots centre on `out_special_radiusw/h`, not a bullet's +6/+6.
- `salvoBoost` doubles as the trail's phase: 1 = first drawn tick (fat 14-spark
  launch burst), stepped to 2 for the 4-spark flight trail. Both stay truthy,
  which is all the tyrian2.c collision bonus tests. Flight trail passes
  `classic_cap = true` like every weapon trail; the launch burst passes false, so
  a wide volley can't flush the classic 101-spark ring in one tick.
- The four ep4/5 trail-tagged weapons (sprite > 1000: Mega Pulse, Wallop Beam,
  Protron B, Ice) already emit a dense plume that a second trail just muddies, so
  the salvo turns the NATIVE `JE_doSP` up instead — 5→16 sparks, spread 3→7 — and
  suppresses the added flight trail (`ownTrail`); only the launch flash layers on.
  That plume passes `classic_cap = false`: at 16 sparks/tick/shot the 101-ring
  refills inside two ticks and visibly cuts the tail short. Extra Sparks OFF caps
  it at 101 regardless, so the big buffer is only spent when opted into.
- Specials with no bullet to trail (repulsor/attractor/invuln/repair) fire
  `salvo_special_burst` off the ship; the Zinglon pillar scatters sparks up the
  beam, width following its own `temp` ramp. Both use `ENDLESS_SALVO_SPARK_COLOR`
  (bank 12) — the same green as the gauge, so green reads as "salvo" throughout.
- Front gun, rear gun and both sidekicks all get the boost: the rear because the
  bay loop runs `SHOT_FRONT` then `SHOT_REAR` in one tick, the sidekicks because
  their fire loop sits after it.

### Save / resume

- The per-slot `JE_saveGame` (tyrian.sav) is a fixed, checksummed layout with no
  room to extend, so the run lives in a sidecar (`endless.sav`) keyed by the same
  slot: run-persistent state (depth, hull, kills, perks, tokens, gamble debts,
  superbombs) plus the outpost snapshot (this visit's courses, shop stock, prices,
  perk offer, gamble line, pending buys). Restoring the snapshot instead of
  regenerating stops save/reload from rerolling the shop for free and keeps buffs
  already paid for.
- Auto-checkpoint into the "LAST LEVEL" continue slot happens at outpost entry
  (the one coherent resume point). Hardcore suppresses all saving, so dying or
  quitting ends the run with no reload.
- Sidecar version history: v3 seed, v4 locked sortie, v5 buff recharge, v6
  recent-level ring, v7 64-bit mods, v8 exact course files, v9 zone-100
  credits-shown flag, v10 last zone's song + its depth, v11 widened the fixed
  perk block (16 → 32 slots) so the 17th perk (Radar) persists, v12 the Star Charts
  / Breakthrough debts owed by a cleared sector, v13 widened the stored perk OFFER
  list (3 → 5) for the milestone pick, v14 dropped the Rapid Charger perk and so
  RENUMBERED the perk slots. `ENDLESS_SAVE_VERSION` is the
  authority — keep this list in step with it. Each new field is
  appended and read behind a `version >= N` guard, so older sidecars still load (a
  missing field reads as the memset-zero default — note v10's apply step has to map
  a zeroed `lastSong` back to depth −1, or a pre-v10 record would read as a real
  entry for depth 0; v11 reads the legacy 16-slot perk width for pre-v11 files, the
  zeroed extra slots reading as newer perks unowned; v13 likewise reads 3 offer
  slots from an older file and clamps that record's offer COUNT to 3, since a
  widened field in the MIDDLE of the record would otherwise desync everything after
  it). A `COMPILE_TIME_ASSERT` keeps
  `PERK_COUNT <= ENDLESS_SAVE_PERKS`, so the next overflow fails the build instead
  of silently dropping a perk.
- v14 is the odd one out: the record LAYOUT is unchanged (the perk block is a fixed
  32 bytes either way) but the MEANING of its slots moved, because deleting a perk
  from the middle of the enum renumbered every id below it. TWO stored arrays are
  keyed by those ids and both need migrating in `endlessReadRec`:
  - `perkOwned` — a perk's on-disk slot IS its `PERK_*` id. The dropped Rapid
    Charger stacks are added to Rapid Recharge (which inherited the effect; the
    apply step clamps to `maxStack`), then the tail memmoves down one. Without it a
    resumed run reads every perk below slot 14 as its neighbour.
  - `perkChoice` — this visit's pending OFFERS are `PERK_*` values too. Ids above
    the hole shift down, the offer that WAS Rapid Charger is dropped and the list
    compacted. Skipping this is worse than cosmetic: a stored id of 24 (the old
    last perk) would index one past the shortened `endlessPerkTable`.
  This is the only sanctioned way to renumber, and the cost of it is exactly this
  paragraph — appending stays the rule.
- Quit Level in endless reverts the level to its launch state and reopens the
  outpost. Hardcore relocks it (retry same level or quit run; no farm-then-bail);
  non-hardcore unlocks it (re-outfit freely, still no mid-zone farming).
- Reroll runs neither the entry merchant-sort nor the equipped auto-adds, and a
  purchase doesn't sync `last_items`, so shop-stock code must seed from
  `player[0].items`, not stale caches.

### All-time record — furthest zone reached

The furthest zone ever reached is the only endless state that outlives a run, so
it deliberately does NOT ride `endless.sav`: that sidecar is keyed by save slot,
and a hardcore run — the one most likely to set a record — never writes one at
all. It lives in `opentyrian.cfg`'s own `[endless]` section (`best_zone`), with
the read/write pair in `endless_save.c` next to the `[endless_debug]` block, so
`config.c` still knows nothing about endless internals.

- `endlessNoteZoneReached(zone)` is called from the endless level-start path in
  `tyrian2.c`, right after `endlessCaptureSortie`. **Launching a zone is
  reaching it** — the same reading the Run Over screen uses ("You fell in Zone
  `runDepth + 1`"), so the record and that line always agree. It writes the
  config through immediately rather than at exit: a record set at zone 60 must
  survive an alt-F4.
- `endlessRecordRunStart()` snapshots the record as a run finds it, and the Run
  Over screen shows the gap as `(+n)`. It is called from `newEndlessGame` and
  `endlessLoadSlot` — deliberately NOT from `endlessResetRun`, which a Quit
  Level bail re-runs via `endlessApplyCurrent`; baselining there would silently
  zero out a gain the run had already earned.
- The Run Over screen also shows the run's seed, so a good run can be replayed.
  It now builds its lines into an array first, because only the run knows its own
  height (the hull line is conditional): the pitch tightens until the block fits,
  and the whole screen — title, stats, milestone line — is then centred
  vertically on that measured height instead of starting at a fixed y.
- **`SMALL_FONT_SHAPES` has blank glyphs for `( ) + * =`.** They are 1x2/2x2 stubs
  in `data/tyrian.shp`, so they draw *nothing* — a `(+4)` renders as `4`. The
  record's gain therefore reads "up 4", in words. This is a property of that bank
  only: `TINY_FONT` draws them all fine, and `FONT_SHAPES` is worse still (no
  digits at all). See §Font glyph coverage.
- The backdrop is `tshp2.pcx`, the painted ship from the campaign ending (the
  "NOT ZINGLON!" screen), which the level scripts reach via `]P0`. Three things
  make dropping it behind live text easy, and they are worth knowing before
  reusing any picture this way:
  - It is a **PCX with its own 8-bit palette** (`JE_loadPCX` copies the bytes
    straight into `colors[]`, no 6-bit rescale — unlike `palette.dat`), and it
    replaces `colors[]` wholesale. So the fade-in must come *after* the load.
  - The picture only uses indices **0..223**. Banks 14–15 in the file are
    placeholder green, which would render the glow text unreadable, so bank 15
    is overwritten with `palettes[0]`'s dark→white ramp — the one every other
    glow screen draws from. The outline pass (`hue 0, value −12`) wraps to
    indices 0..3, which in this file are near-black browns, so the halo works
    out on its own.
  - **Dimming is a palette scale, not a pixel pass**: because those 224 entries
    belong to nothing but the picture, scaling them to 32% darkens the backdrop
    with no per-pixel work and no risk to the text.
  `JE_loadPCX` writes 320 columns per row at x=0, so the picture is `memmove`d
  into the middle of the 356px surface and its edge columns smeared into the two
  18px side strips — its edges are soft haze, so the repeat reads as more sky
  rather than as a seam.

### The mode / effect split — `endlessFxActive()`

`endlessMode` used to gate two unrelated things, so Debug Mode could not run the
fun half of endless inside a normal game. They are now separate:

- **`endlessMode`** — the run STRUCTURE: which level plays next, the outpost,
  Chart-a-Course, the `endless.sav` sidecar, the Run Over screen, disabled
  mid-level savepoints, the datacube/secret-orb rewrites, the powerup-drop
  redirect, the depth-inflated shop prices, the boss-kill tally.
- **`endlessFxActive()`** (`endless.h`, inline) — the EFFECTS: the depth-scaled
  levers, every `ENDLESS_MOD_*` bit, perks, elites, the rising tide. Reads
  `endlessMode || endlessCampaignMods`.

**Which gate a new site wants is decided by what it DOES, not where it lives.**
Reads a lever, a mod bit or a perk → `endlessFxActive()`. Touches run flow, the
shop or the save → `endlessMode`. The effect layer is a no-op at depth 0 with no
mods (every lever returns its identity value), so switching it on alone changes
nothing — that property is the regression floor and is worth preserving.

Three places needed care, and are the pattern for anything similar:

- **Purchases must stay `endlessMode`.** The Reinforce-Hull bonus (`varz.c`
  ship-info) and the revive token are bought at an outpost; a campaign has no
  shop, so a finished run's purchases would otherwise follow the player into a
  normal game. `endlessCampaignModsArm()` clears that state when the layer is
  switched on; perks and mod bits are deliberately left alone, being what the
  debug screen exists to set.
- **The layer may only RAISE a cap, never lower one.** The armour-pickup cap
  (`mainint.c`) is `initial_armor` in endless but keeps the classic 28 as a
  FLOOR under campaign mods — otherwise merely enabling the layer would nerf
  pickups for any ship with a hull under 28.
- **Per-level effect state has to be reset.** `endlessResetZoneEffects()`
  (`endless_level.c`) holds the elite tier decisions, the three zone timers and
  the kill-fire combo; `endlessRegenerateLevel` and `endlessCampaignLevelStart`
  both call it. It is deliberately RNG-free so `endlessRegenerateLevel` keeps
  drawing its seeded rolls in their established phase order — moving a draw
  changes every existing seed's run. The campaign path re-rolls the elite stream
  and gravity heading from `mt_rand` instead, because it has no zone counter to
  key a per-level phase off.

`difficultyAdjust` is the one campaign behaviour the layer SUPPRESSES rather than
adds to (`mainint.c` JE_endLevelAni): a score-driven difficulty bump between
levels would move the whole ramp under the scaling readout.

### Zone scaling: readout and per-lever overrides

Every depth-driven lever is a pure function of (`endlessRunDepth`,
`difficultyLevel`, `endlessActiveMods`). `endlessScalingSnapshot(zone,
difficulty, mods, *out)` (`endless_combat.c`, at the file tail) exploits that: it
saves those three globals, swaps in the requested triple, calls the REAL
accessors and restores. So the readout can never drift from the formulas, and a
snapshot taken mid-level leaves the run untouched. It also forces the effect
layer on for its duration, so the page describes the RAMP rather than whether it
currently applies — otherwise the `endlessFxActive()`-gated levers would read as
flat zeroes exactly when someone is looking the curve up. The one figure it
cannot predict is ENRAGE's and RETALIATION's contribution to `fireDelayPct`: both
key off live per-level timers, so that reads as whatever it is right now.

`endlessScalingOverride[ESO_*]` pins a lever: its accessor returns the pinned
value outright via the one-line `ENDLESS_OVERRIDE(id)` macro at the top of the
function, bypassing depth AND mutators. That total bypass is the point — it
isolates which axis a difficulty wall came from. The macro and the array sit
early in `endless_combat.c`; the name/bounds tables and the snapshot builder sit
at the tail, because their editing bounds cite tunables declared further down
beside the levers they belong to.

### The debug screens — `game_menu.c` `endlessDebugScreen(bool jumpMode)`

One screen, two shapes. `jumpMode` is the endless ZONE JUMP the debug level
picker opens (Base Level + START ZONE, staged and committed only on launch, so
Esc is a real cancel). `!jumpMode` is the TUNE form the debug menu opens
(`endlessDebugTuneScreen`): same slate/perk/scaling editors, but it applies in
place on the way out — Esc applies too, by design, because it is a live control
panel rather than a dialog proposing a change. Outside endless it also carries
the master `endlessCampaignMods` toggle.

Both reach the `EDS_SCALING` page: one row per lever, value at the previewed
zone, `PIN` marking an override, and the selected row's help line showing the
CURVE across zones 1/25/50/100/200 — a lever's whole point is where it turns on
and where it caps, which one figure at one zone shows neither of.

Gotcha: this screen is now reachable from the IN-GAME debug menu, where there is
no pillarbox, as well as from the shop, where there is. It centres on
`video_get_menu_x_offset() != 0 ? LEGACY_WIDTH : vga_width` — assuming either one
unconditionally strands or double-offsets the panel.

The whole setup (toggle, virtual zone, mod mask, perk stacks, pinned levers) is
persisted to `opentyrian.cfg` `[endless_debug]`. `endlessDebugConfigSave/Load`
live in `endless_save.c`, not `config.c`, which has no business knowing what a
perk is. **The save declines to write during an endless run** — those globals
belong to the RUN then (and ride `endless.sav`), so writing would overwrite the
campaign slate; leaving the keys untouched means quitting from inside a run
preserves whatever was last saved. Written immediately on apply rather than at a
clean exit, since a crash is a plausible way for a debug session to end.

## Debug menu mid-level edits — `mainint.c` `JE_debugMenu`

The loadout rows write straight into `player[0].items`, but the engine caches a
great deal off those and recomputes it only at LEVEL START: `shipGr` **and which
sheet it lives on** (`shipGrPtr`), the hull, `initial_armor`, the hit box,
`powerAdd`, `shield_max`, the sidekick pods' ammo/style. Changing a ship mid-level
moved none of it — a sprite index read against the wrong sheet is where the
garbled hull came from.

`debug_apply_loadout_change(shipChanged)` runs after any edit key on a loadout
row. It is deliberately the same sequence as the engine's OWN in-level ship
change (the Tab+digit extra-ship path in `JE_playerMovement`) so the two cannot
drift: reset `shotMultiPos`, clamp `weapon_mode` to `JE_portConfigs()`,
`JE_getShipInfo()`, then repaint — both gauges are **event-driven**, painted when
they change rather than per frame, so they must be repainted explicitly.

Two rules it encodes:
- Only a real hull SWAP re-armors you (compare `items.ship` before/after — Left at
  id 0 changes nothing). Nudging the rear weapon must not quietly heal you.
- `JE_getShipInfo` rewrites BOTH players' armor, but only player 1's loadout is
  editable here, so player 2 always keeps what it had.

The **generator** row is covered by the same call: `powerAdd` (the per-tick
recharge) is set *only* in `JE_getShipInfo`, so before this nothing about a
generator swap took effect. Max power is a flat 900 regardless of generator, so
`powerAdd` is the whole of it.

Related: the cyclers now clamp at the top (`SHIP_NUM`, `PORT_NUM`, `SHIELD_NUM`,
`POWER_NUM`, `OPTION_NUM`) the way Left already clamped at 0. Each indexes an
array sized `[X_NUM + 1]`, so stepping past was an out-of-bounds read the moment
anything looked the item up. `JE_getShipInfo` had the same latent bug for the
"extra" ships (id > 90, described by `extraShips[]` not `ships[]`) — guarded now.

### Sprite2 blits are now index-bounded — `sprite.c`

A `Sprite2_array` is one raw blob: a `Uint16` offset table, then packed sprites.
Every `blit_sprite2*` read `offsets[index - 1]` and walked bytes from there with
**no bounds check at all**, so an index the sheet doesn't have followed a junk
offset into arbitrary memory and painted whatever it found. That is what a bad
item id looks like on screen — "distorted graphics", not a missing sprite.

`sprite2_index_valid()` now gates all ten of them (and
`sprite2_has_pixel_in_window`). It deliberately assumes **nothing** about the
sprite count: it only requires that the offset-table read and the offset it
yields both land inside the blob. That contains a wild index without any risk of
wrongly rejecting a legitimate one.

### The Nort ship's banking trim — `RL_ID_SHIP_TRIM_BASE`

The Nort ship (id 12, the `shipgraphic == 1` sentinel — see the two-piece hull
notes) is the only 1-player hull that draws a sprite **conditionally**: its
banking trim (39/40/58/59) appears only while banked. It shared the hull's
`RL_ID_SHIP_BASE + player` id, and `rl_finalize` snaps a whole id on any tick
whose per-id blit count differs from the previous one — so the hull lost its
interpolation every time banking started or stopped, i.e. continuously while
moving. Normal ships draw a constant blit count and never hit this; the
Dragonwing draws two constant halves, so it doesn't either.

The trim now has its own id, kept inside the ship range so it still rides the
ship's render-rate override and stays welded to the hull. That made the player
index derivation in `rl_replay` a **wrap** rather than a clamp — a clamp would
have handed player 1's trim to player 2.

## Boss & enemy health bars — `tyrian2.c`, `varz.c`

- Enhanced boss bars: a recessed track plus glossy gradient fill kept inside one
  palette bank (bank 7 = 112..127 normally; elite/champion tints in endless), so
  they read on any level palette. Layout (Top/Bottom/Left/Right; one- or two-bar
  Split/Together/Stacked) comes from the Enhancements menu; geometry must clear
  the corner HUD indicators.
- Bars draw into `game_screen` (playfield space), not composited-buffer space —
  see §Widescreen. `BOSS_BAR_THICK`/`GAP` are shared between the layout code and
  the HUD-shift helper so they can't drift.
- Enhanced boss-bar hit flash fades smoothly at render rate (like the shield/armor
  gauge flash). The bars are captured in the playfield residual, so they'd otherwise
  step at the 35 Hz tick; `draw_boss_bar_present` redraws them every displayed frame
  in `JE_starShowVGA`'s present loop at an interpolated flash (`boss_flash_render`,
  same `color + 1 - alpha` trick), overdrawing the residual bars. `draw_boss_bar_gauge`
  and `draw_boss_bars_enhanced` gained a `(dst, scale)` so the per-frame redraw lands
  on the residual's exact pixels: `bbfill` scales a 1x rect to a `scale`×`scale` block
  identically to the residual re-apply (`x0*scale .. (x1+1)*scale-1`), and the frame
  is now two filled rects instead of an outline+track (byte-identical at scale 1).
  The once-per-tick call passes `decrement = true`; the per-frame call must not, or
  the flash would race down at the display rate. Classic bars keep the per-tick flash.
- Per-enemy bars: one bar per linknum group, spanning the group's on-screen
  bounding box and showing its most-damaged part; only live and damageable slots
  count (armorleft 1..254, `healthbar_seen` latched); boss-linked groups are
  skipped. Bars are recorded (`RC_HP_BAR`) so they interpolate with their enemy.
- Endless reinforced hulls above the 28-unit armour bar draw as stacked rollover
  layers, each 28-unit chunk in its own palette-relative gradient (tuned by eye).
- Shield/armor gauges flash white when depleted by damage (`Gauge Gradients` menu
  toggles `gaugeFlashShield`/`gaugeFlashArmor`, on by default, each independent).
  `JE_playerDamage` starts a per-player countdown (`shieldGaugeFlash`/
  `armorGaugeFlash`) only when the value drops — never on shield regen or armour
  pickups. `JE_updateGaugeFlash` (in `JE_main`) decays it one step per tick.
- The fade is a white pop then a smooth in-family return: the `flash` arg to
  `JE_dBar3` paints a flat white (index 15) at the peak, then for the lower steps
  brightens the bar's own gradient toward its bank top (`flash * 3`, clamped) and
  fades that to normal. This avoids a grey detour and a hue snap at the end — the
  shield bank tops out light-blue and armour cream (not white, unlike the boss
  bar's near-white bank 7), so the peak must reach into white but the tail should
  stay in-family. `bright == 0` keeps the normal draw byte-exact.
- Smoothness: the event-driven bars would otherwise step at the 35 Hz tick. Like
  the power gauge, `gauge_flash_present(alpha)` repaints them every displayed frame
  in `JE_starShowVGA`'s present loop at an interpolated intensity. Interp needs no
  prev array — the counter decrements by exactly 1/tick, so `prev == cur + 1` and
  intensity `= cur + 1 - alpha` (rounds to `cur` at `alpha == 1`, the non-interp /
  per-tick path). `gaugeFlashAlpha` defaults to 1 so all other callers get `cur`.
- Endless stacked armour flashes only the newest/active tier, not the whole column:
  `endlessDrawArmorBar` passes the flash to the last-drawn rollover layer (`total-1`),
  which sits at the bar's bottom (rollovers fill bottom-up) and is the chunk being
  depleted; the fuller tiers below it in the stack keep their normal colour.

## Menus & shop — `game_menu.c`

- Weapon-sim preview presents smoothly by replaying the recorded frame at
  interpolated positions and copying only the preview box (8..143 × 8..182).
  `weaponSimOverlayFn(alpha)` lets the custom-weapon creator draw above the
  finished preview; it receives the interpolation alpha so overlays can glide.
- Menu label data (`menuInt`) is loaded once at startup. Inserting a row (the
  Switch/Vita "Touch" volume row) means shifting labels down once and re-applying
  bumped `menuChoices` counts on every entry (they reset from `menuChoicesDefault`
  at the top of `JE_itemScreen`). Watch for out-of-bounds when a menu's choice
  count grows.
- Buy-menu ship illustration: each weapon id has a mount point in exactly one of
  `front_weapon_*`/`rear_weapon_*` (the other holds -1). The endless shop can put
  a front weapon in the rear slot (or vice versa), so prefer the slot's table but
  fall back to the weapon's real mount table; indexing at [-1] smears the sprite
  across the screen. NortShip specials (ids ~32–40) have no authored position
  (both tables 0) and are pinned to the front mount.
- Chart-a-Course monitor overlay: threats top-left in red, boons bottom-right in
  green — opposite corners so long lines never collide. Palette-18 facts: bank 15
  = dark-red→orange ramp, bank 0 shades 0..7 = green ramp; TINY_FONT bodies at
  shade 7 (edges 3, highlights 10), so brightness offsets slide along the ramp —
  don't go below -2 or the shade-2 edge pixels underflow.
- Shop preview sidekicks mirror gameplay mounts (side/front/orbit; trailing kept
  side-by-side deliberately). Front-mounted (tr==2) pods launch from both slots
  via `button[1+i]`.
- Endless swaps the shop front-menu items 2/3 (Data Cubes → E-Shop, Ship Specs →
  Perks). Stock labels are captured once; the E-Shop labels
  (`menuInt[MENU_ESHOP+1]`) are re-applied after every buy so prices stay live.
  The Perks entry reuses MENU_PERKS as a read-only scrolling list rendered from
  `perkListId[]`.
- Any capped E-Shop buy must say so in BOTH the row label and the help line, and
  print NO price once capped — "Hull Maxed", "Bombs Full", "Revive Ready",
  "Sabotage Maxed" (`ENDLESS_CLEANSE_MAX_CHARGES`, 3 queued strips per visit). A
  price beside a row that can't be bought reads as a failed purchase rather than
  a full stock; the buy itself already answers with `S_SPRING`.
- E-Shop rows are tinted by category so related buys share a hue
  (`endless_eshop_row_bank` in `JE_drawMenuChoices`, keyed by row x ==
  `curSel[MENU_ESHOP]`): buffs Turbodrive/Overblast/Overdrive = green (bank 12),
  Reinforce/Extra-Perk = cyan (8), Special Weapon = red (4), Bomb/Revive = purple
  (5), Gamble = fiery red→yellow (7), Reroll/Sabotage/Done = default gold (15).
  Only `curMenu == MENU_ESHOP` is recoloured; the perk list stays gold. Measured
  palette-1 (shop `colors`) bank hues, TINY_FONT body ≈ shade 9: 0 grey, 1 tan/gold
  (cash), 2 olive-green, 3 steel-blue, 4 red/salmon, 5 muted-purple, 6 blue-grey,
  7 red→orange→yellow (vivid, also HP/boss-bar bank), 8 light-cyan, 9 pure-blue
  (dark low shades — avoid on the dark list bg), 10/11/14 tan/brown, 12 vivid
  green, 13 muted-teal, 15 red→gold (the default menu-text ramp).
- Chart-a-Course text plumbing: the danger RANK label centres on the monitor
  window's centre (x=77, under the map), not the asymmetric slot midpoint, so
  every rank width lines up. `endlessModText` draws its own 8-direction outline
  plus `JE_outTextAndDarken` fill; `FULL_SHADE` can't be used because negative
  brightness is `JE_outText`'s shadow sentinel (deep-red tiers would render
  black). Help-line amounts: E-Shop cost = bank 1/brightness 6 (palette-1 cash
  colour); Chart-a-Course runs under palette 18, so its payout = bank
  14/brightness 6.
- **Every help-bar figure right-aligns to `ENDLESS_COURSE_PAYOUT_RIGHT` (305)** —
  description left, figure right — via `draw_help_bar_right`, which falls back to
  just-after-the-text when a long description would otherwise reach it. Both the
  perk pick's "Take the Cash" buyout and the offered perk's `Owned n/max` joined
  the Chart-a-Course payout and the E-Shop price there on 2026-07-28; that retired
  the last caller of the after-the-text path, so the per-menu test went with it and
  only the overlap guard remains. Two strings feed it, and the split is by MEANING,
  not by menu: `costStr` is money and takes a highlight bank, `ownedStr` is a stack
  count and keeps the help text's own 14/1. They can never coexist (only the perk
  pick sets `ownedStr`, and only on rows where the decline's buyout doesn't apply),
  which is why the draw is an `else if` rather than two independent blocks.
  The overlap guard is also why the decline's help line is kept short: the buyout
  reaches seven figures on a deep capped run. On the perk rows the margin is
  thinner still — the longest description in `endlessPerkTable` (41 chars) leaves
  about 12px before `Owned n/max` would be bumped out of alignment, so a longer one
  needs the count checked, not just the row width.
- The read-only Perks list (`endlessPerkListMode`) draws each perk's stack count on
  the ROW, right-aligned at x=302 just clear of the scroll-bar track — so its help
  line carries the description only. It used to prefix `Owned n/max.` there as well,
  which said the same thing twice on one screen. The perk PICK is the opposite case:
  its rows are bare names, so the help line is the only place the count appears.
- Nav-map planet draws must loop `mapPNum`, not `menuChoices - 1`: in endless
  `menuChoices = courseCount + 2`, which reads past `mapPlanet[5]` and feeds
  garbage to `JE_drawPlanet` (crash).
- Blank shop icons: `JE_drawItem` draws nothing when an item's `itemgraphic` is 0,
  leaving an empty box. `JE_loadItemDat` (episodes.c) fills every icon-less shop
  item — front/rear weapon, generator, sidekick, shield — with placeholder icon
  167 after load, skipping "None" entries (blank on purpose) and empty slots. So an
  unauthored icon now shows 167, not a blank; ships are exempt (they draw
  `shipgraphic`, never `itemgraphic`).

### In-game debug menu — `JE_debugMenu` in `mainint.c`

- Row IDENTITY and row ORDER are separate tables at file scope, just above the
  function: `dbgLabel[]` / `dbgHelp[]` are indexed by the `DBG_*` id, `dbgRows[]`
  is the display order and interleaves non-selectable group headings (`id < 0`)
  between them. Every switch keys off `selId` — the row id — never off a list
  position, so regrouping the menu or slipping a heading in shifts nothing.
- `COMPILE_TIME_ASSERT(dbg_rows_cover_every_row, …)` is the guard that matters: a
  row added to the enum but forgotten in `dbgRows` would be silently unreachable.
  Bump `DBG_HEADING_COUNT` when adding a heading.
- 34 rows of hull ids, cheats and diagnostics in one flat list read as noise, so
  they sit under SURVIVAL / LOADOUT / FIRING / DIFFICULTY / LEVEL / DIAGNOSTICS
  (survival first — god mode and friends are what you open the menu for mid-run),
  and the footer gained a second line: what the selected row DOES. `dbgRowStep`
  skips headings when moving, `dbgRowSnap` pushes any jump (page, Home/End, a
  click, a hover) off one; the scroll keeps a heading on screen with the first row
  under it. Same row-kind vocabulary as the endless zone jump below — the two
  screens are meant to read as one system.
- Console: the shoulder buttons page the list via raw-button edge reads (menus
  only receive confirm/cancel/directions from a pad), and the key legend names
  A/B instead of Enter/Esc. The two typed fields (Add Cash, Hang Watchdog) already
  pop `console_swkbd`.

### Campaign debug level picker — `JE_debugLevelSelect` in `game_menu.c`

- One list of EVERY level, grouped under `EPISODE n` headings, instead of the old
  per-episode list you paged between with Left/Right. The browsed episode was a
  mode you had to track, and it hid two thirds of the game behind it; Left/Right
  now JUMPS an episode inside the one list, so the fast move survives without the
  mode. Opens standing on the level being played, and that row stays tinted so a
  jump can't land back where it started by accident.
- Shares the endless jump screen's row model (`PickerRow` + `pickerRowStep` /
  `pickerRowSnap`) and its level list. Those arrays are `allLevel*`, NOT
  `endlessBase*` — `endlessBaseName` is also a global in endless_level.c (the
  crash log's base-level history), and a file-static of the same name in
  game_menu.c would silently shadow it.
- Footer names the selected level's episode / section / file, which is what you
  need the moment a jump misbehaves. Console: shoulders page, legend says A/B.
- This picker used to be the only caller of `load_debug_levels`, and that parser
  fed the OLD two-column `MENU_DEBUG_PLAY_LEVEL` grid, which nothing could open
  any more. Both were removed (2026-07-26), along with the grid's branches inside
  the shared draw functions and its `debugPlayMenu` / `debugMenuInt` /
  `debugLevel*` statics. **Menu id 15 is left as a hole, not reused**: `MENU_MAX`,
  `menuEsc[]`, `menuChoicesDefault[]` and the `menuInt` label file are all indexed
  by menu id, so renumbering 16/17 down would silently shift every one of them.
- Proof the grid was dead, for the next time something looks removable here:
  `curMenu` is a file-static starting at 0, and every assignment to it is either a
  literal `MENU_*` (never 15), `menuEsc[curMenu] - 1` (that table maxes at 11), an
  `isNetworkGame` ternary between the two Options menus, or `oldMenu` — which is
  only ever a previous `curMenu`. `debugPlayMenu` was likewise only ever assigned
  `false`, and `menuChoicesDefault[15]` was 0, so the grid had no rows either.

### Endless debug zone jump — `endlessDebugScreen` in `game_menu.c`

- Reached from the debug level select (`JE_debugLevelSelect` forks to it whenever
  `endlessMode` is on; the campaign browser below it is untouched). It sets up a
  zone the way generation would have, then arms the same `select_level` jump.
- Shape: a HUB that never scrolls — Zone, Base Level, one row per GROUP of
  toggles (Sector Modifiers / Personal Buffs / Perks), Gamble Outcomes, two
  reset actions, Start — with drill-in list screens behind the groups. It used to
  be three Tab-paged lists totalling 120-odd rows, which meant scrolling past 48
  modifiers to reach Start and cycling a 250-entry level list one press at a time.
- Rows are rebuilt from row KINDS every frame, per screen, so adding a modifier or
  a perk shifts nothing: there are no fixed `ROW_*` offsets anywhere. `EDR_HEADER`
  rows are drawn but never selectable — `endlessDebugStep` skips them when moving
  and `endlessDebugSnap` pushes any jump (page, Home/End, click, letter) off one.
  Selection and scroll are per screen (`sel[]`/`top[]`), so backing out of a list
  returns to the row it was opened from. `sel[]` is written back under the screen
  the key was pressed on, NOT under `screen` — a drill-in changes `screen`
  mid-handler, and filing the old row index under the new screen drags a bogus
  selection in with it.
- Console parity is the point of the redesign, not a retrofit: d-pad + confirm +
  cancel reaches every row and every value. The old screen needed Tab (faked from
  the Y/Square button) to page and a number row to type a zone. Now the shoulder
  buttons page a list (raw-button edge reads synthesizing PageUp/PageDown, since
  menus only get confirm/cancel/directions from a pad), Left/Right steps the zone
  by 1 and PageUp/PageDown by 10, and Confirm on the Zone row opens
  `console_swkbd` — the one place a number must be typed. Desktop keeps typed
  digits, Backspace, the wheel, hover and click, plus a first-letter jump on the
  list screens (the fast way through 250-odd level names).
- Modifier rows carry a `grp` tag purely to file them under DANGERS / BOONS /
  GAMBLE DEALS headings, and a `hint` that is normally NULL: the help line comes
  from `endlessModWord(bit)`, which reads the real registry phrase out of
  `endlessModTable`, so wording can't drift from the Chart-a-Course monitor. Only
  bits with no registry row (Gravity-omni, Marked, Nitro, Dud) spell one out
  locally. Perk rows get `endlessPerkDesc` for free.
- The launch itself is unchanged: `endlessFoldPurchasedMods(dbgMods,
  endlessPendingMods())` (so a debug jump can't produce a kill-fire state
  generation would never hand out), perk stacks via `endlessPerkSetOwned`, then
  `endlessPickNextLevel` or an explicit episode + section + file. A gamble outcome
  fired without launching still routes the pending perk pick to `MENU_PERKS`.

## Weapons — `episodes.c`, `shots.c`, `custom_weapon.c`

- Supersparks: only the ep4/5 item data tags certain projectiles with the ">1000"
  shot-graphic encoding (spark shower from colour bank `sg/1000` behind sprite
  `sg%1000`). The full data diff finds exactly: Mega Pulse (wpns 400–410,
  35↔7035), Beno Wallop Beam (736, 30↔7030 + second bolt 7029↔29), Beno Protron
  -B- (737, 28↔9028), Ice Beam/Blast (621/706, 634↔9634). Retagging is idempotent
  (each bolt is set from its target regardless of current state) because
  `JE_initEpisode` re-applies on the same-episode early-return path.
- Episode differences (`JE_applyEpDiffs`): weapons whose ep1-3 vs ep4/5 data
  differ beyond sparks are rewritten from shipped constants, idempotent for the
  same reason. Only the `[0..max-1]` pattern slots are touched; higher slots carry
  leftover editor garbage the fire cursor never reaches.
- Charge-Laser Cannon: the cut-from-Tyrian-2000 5-stage DOS charge sidekick,
  re-added from the original TYRIAN{1,2,3}.LVL data (wport 4 / wpnum 452 / pwr 5 /
  cost 30000) into scratch weapon slots 900–905; body sprites 87/106/125/144 in
  `spriteSheet9`. Re-offered in its original shops when `chargeLaserCannon` is
  enabled.
- Zica Laser Lv11 tweaks: the port-5 level-11 pattern's native horizontal layout
  is captured at load; the configured base/length options rebuild the side beams
  from full Lv10 beam copies in two scratch slots (the unused 818..1000 weapon-id
  gap). Rebuilt from shipped values, so re-applying is idempotent.
- Fire-cursor wrap must test `>=`, not `==`: the cursor is a persistent per-bay
  global, so swapping weapons mid-cycle can leave it above the new weapon's `max`.
  An exact test then silently kills the gun for hundreds of volleys while the
  cursor crawls through empty slots.
- Charge autofire "Yes (fastest)": `player_shot_create` has just set `shotRepeat`
  from the fired stage; override it with the quickest charge stage's shotrepeat
  (scan all stages) so max-power blasts fire at stage-0 cadence.
- Charge-sidekick autofire (`chargeSidekickAutofire`, per-slot, default On) has two
  edit points on one byte: the debug menu's "Autofire Charge Sidekicks" row cycles
  all four modes (Off / On / Charged / Fast=fastest), while Setup → Enhancements →
  Weapon Tweaks → "Sidekick Autofire" cycles only the three player-visible ones and
  skips `CHARGE_AUTOFIRE_FAST`. Fast still *renders* as "Fast" in the Weapon Tweaks
  row if the debug menu set it, but the visible cycle can never land back on it.
- Custom weapon creator: designs compile into `weapons[910+]` and a "Test" port
  60. The sidekick variant fires mode-0 levels; with charge (pwr > 0) the engine
  fires `wpnum + charge`, and consecutive level slots make the per-level curve the
  charge ramp. Sidekick mount style is the engine option `tr` (0 side / 1
  trailing-large / 2 front / 3 trailing / 4 orbit); styles 1–2 draw from 2x2
  `spriteSheet10`, the rest from `spriteSheet9`.
- **Christmas shot sprites — shipped data bug in `tyrianc.shp`.** The shape file
  holds 13 banks; bank 7 is `spriteSheet8` (player shots, `sg <= 500`) and bank 11
  is `spriteSheet12` (player shots 2, `sg > 500`, used by `shots.c` and by enemy
  shots in `tyrian2.c`). In `tyrianc.shp` bank 11 is a *byte-exact copy of bank 7*
  (both 32404 bytes, md5 `ab95a26cfc0b…`) — the tool that built the festive file
  wrote bank 7 twice and bank 11's real data was never emitted. Result: every
  `sg > 500` projectile draws unrelated bank-7 art in Christmas mode.
  No Christmas variant of bank 11 exists to substitute: in Tyrian 2.1 (12 banks,
  same bank layout minus the T2000 ship sheet) bank 11 is byte-identical between
  `tyrian.shp` and `tyrianc.shp` — that bank never had festive art. Only banks 7,
  8, 9 legitimately differ between the two files, in both 2.1 and 2000.
  `JE_loadMainShapeTables` therefore detects the duplication (bank 11 == bank 7,
  only when loading a non-`tyrian.shp` file) and reloads bank 11 from
  `tyrian.shp`. Both files index 304 sprites in that bank, so the swap is safe.

## Font glyph coverage — `fonthand.c`, `data/tyrian.shp`

`font_ascii[]` maps a character to a sprite id, and −1 means "draws nothing".
That table is NOT the whole story: a mapped id can still resolve to a **blank
1x2 / 2x2 stub** in the shape bank, which also draws nothing — silently, and with
no gap, so `(+4)` comes out as `4`. The three banks differ, and a string is only
safe in the bank it is actually drawn with:

- **`TINY_FONT`** (table 2, 127 sprites, ≤8px tall) — everything printable except
  `& < > @ ^ _ ` ~` and a stubbed `'`. The most permissive bank.
- **`SMALL_FONT_SHAPES`** (table 1, 85 sprites, ≤13px tall) — letters, digits and
  most punctuation, but `( ) + * = ] { }` are blank stubs and `& < > @ ^ _ ` ~`
  are unmapped. `$ : , . / ! ? - '` are all fine.
- **`FONT_SHAPES`** (table 0, 60 sprites, ≤20px tall) — **uppercase letters and
  little else**: no digits at all, no `+ - / ( )`. Headings only.

Verify by parsing `data/tyrian.shp` rather than by eye: u16 table count, then u32
offsets; each table is a u16 sprite count followed by, per sprite, a bool
"populated" plus u16 width/height/size and the data. Width ≤ 2 *and* height ≤ 2
is the blank-stub signature. Advance is `width + 1` per glyph, 6 for a space, 0
for the `~` highlight toggle.

## Audio / MIDI — `loudness.c`, `fluid_music.c`, `win_native_midi.c`

With `WITH_MIDI`, the same `music.mus` songs are converted to Standard MIDI
(vendored midiproc LDS reader) and played through FluidSynth (needs a `.sf2`) or
the OS synth (Native MIDI). OPL emulation stays the default. Both MIDI backends
run their own playback (their own sequencer thread), which allows a mid-song loop
at the "loopStart" marker with channel state carried over the seam, exactly like
the OPL player. SDL Mixer X could only repeat whole files and was removed.
`play_song` is idempotent, which fixes the level-end-jingle repeat.

- Both backends re-derive channel state at the loop seam by replaying every
  pre-loop-point program/CC/pitch/sysex event (notes skipped). The LDS→MIDI
  conversion only emits changes, so without the replay a channel holds its
  end-of-song state on pass 2+ (dropped or mis-voiced instruments).
- Native MIDI opens the stream with `CALLBACK_NULL` and runs its own thread to
  avoid the `CALLBACK_FUNCTION` deadlock SDL Mixer X had (`Mix_HaltMusic` stalling
  on the winmm callback mutex).
- SoundFont autodetect adopts the newest `.sf`/`.sf2`/`.sf3` in `data_dir()` when
  none is configured; `resolve_soundfont` re-anchors a stale configured path there
  (survives moved installs) and clears it if unresolvable so autodetect runs
  again.

## Crash logging — `crashlog.c`, `crashlog_state.c` (Windows only)

- Writes `opentyrian_log.log` with a stack trace and a full game-state dump. The
  debug menu has a Force Crash row that exercises the real path.
- The force-crash pointer must be a `volatile` file-scope global: a provably null
  local store gets folded into `__ud2` or elided by MSVC /O2 and never faults
  (why the old version produced no log).
- Duplicate suppression: the top-level backup filter would re-report the same
  fault the vectored handler already logged, but with a useless 1-frame
  thread-start context, so it skips the recorded (code, addr) pair. This is not a
  latch; only the most recent fault is held.
- Hang watchdog: a background thread watches a heartbeat pumped from
  `service_SDL_events`. If it stalls past the threshold (default 5s), it captures
  the main thread's context under the shortest possible suspension, resumes it
  before the stack walk and symbol load (those take loader/CRT-heap locks, and
  walking while suspended could self-deadlock), then logs. A hung thread makes no
  progress after resume, so the stack stays coherent.
- Item-name lookups in the state dump are guarded (tables empty before
  `JE_loadItemDat`, ids can be garbage) and trimmed into a rotating static buffer
  so one `fprintf` can hold several names without clobbering.

## Console ports — `switch/`, `vita/`, `*_platform.c`

- Both ports share `console_platform.h`; Vita mirrors the Switch port's seams.
- The four console path literals live only in `switch_platform.h` / `vita_platform.h`
  (`SWITCH_USER_DIR`, `SWITCH_ROMFS_DIR`, `VITA_USER_DIR`, `VITA_DATA_DIR`).
  `file.c`'s `data_dir()` search list and `config.c`'s `get_user_directory()` pull
  them in via `console_platform.h` instead of repeating the strings — they used to
  be hardcoded in all three places, which left the header macros unreferenced.
- Switch exit crash: libc `exit()` runs the romfs atexit teardown and then
  fcloses all stdio streams; closing a stream whose device is gone (or libnx's
  stdout/stderr) null-derefs in newlib's `_close_r`. Everything that must persist
  has already been flushed by then, so `JE_tyrianHalt` calls `_Exit()` on Switch
  and skips atexit/stdio cleanup entirely.
- Dock/undock (and desktop resolution changes) can leave the frame unpainted
  until input. Fixed by a repaint poll in the event pump (`service_SDL_events`),
  not in the resize handler.
- The console SDL drivers own the window size (window = panel). switch-sdl2 tracks
  dock/undock only while the window stays resizable and never enters SDL
  fullscreen; forcing `FULLSCREEN_DESKTOP` pinned the Switch buffer to 1080p and
  broke handheld layout. The Vita additionally forces supersample=1 (the GPU can't
  sustain the NxN present) and skips the side-gradient cache (its per-frame
  256-colour search dominates fade frames).
- Switch/Vita shoulder buttons cycle the shop preview's rear-gun mode via a
  raw-button edge pattern (console button ids differ from SDL mappings).
- Controller quirks: the right stick is folded into `analog_direction[]` so it
  drives the ship like the left; a button bound to both a confirm and a cancel
  action pushes only cancel (B-is-back); switch-sdl2 reports 8 idle controller
  slots sharing one name, so use only slot 0 (saving the rest clobbers slot 0's
  bindings); the d-pad arrives as buttons (0 hats), so defaults and config loads
  back-fill d-pad button bindings.
- Touch: base multiplier 4.0 cancels `VT_MOUSE_SENS` (0.25) for 1:1 finger→ship
  travel at the sensitivity slider's midpoint. Menus treat a touch as
  tap-to-click at the touched point (absolute mode), gameplay as relative drag.
- Joystick config is flushed to disk on every change (`save_joystick_config_now`):
  the Switch HOME-menu exit path never runs `JE_tyrianHalt`'s config write.
- Vita presents at native size and lets the GPU upscale to 960x544 (a software
  upscale is too slow); the scaler is forced to None and supersample to 1 each
  boot. The Vita IME dialog only draws while the app keeps presenting, so the
  modal keyboard loop (`vita_swkbd`) must present every frame.
- Vita on-screen keyboard: a system common dialog that grabs the control pad until
  `sceImeDialogTerm()` runs; touch (`sceTouchPeek`) is polled separately, so a
  dialog left standing means buttons dead, touch still working (the Switch
  sidesteps all this with the blocking native `swkbdShow`). Two traps drove the
  cancel-freeze: (1) `SDL_IsTextInputActive()` stays true from
  `SDL_StartTextInput()` until `SDL_StopTextInput()`, so gating the modal loop on
  it never lets a cancel end the loop; (2) SDL only terminates the dialog when its
  own event pump happens to observe `FINISHED`. The fix (`vita_swkbd`): drive the
  loop off the native `sceImeDialogGetStatus()` (not `SDL_IsTextInputActive`),
  treat the `SDL_SCANCODE_RETURN` SDL emits only on Enter as the confirm signal
  (so a cancel returns false), and force `sceImeDialogAbort`/`sceImeDialogTerm`
  after `SDL_StopTextInput()` so the pad is always released.
- Console release builds compile with `-DNDEBUG` exactly like desktop Release:
  several asserts are latent tripwires the engine relies on being elided
  (`blit_sprite` bounds, `JE_loadCompShapesB`); live, they abort into a silent
  close.

## Level scripting — `tyrian2.c`

### Map-stop softlock watchdog (`enemyParkedAbove` / `mapStopStallTicks`)

- The event clock is the scroll: `curLoc += backMove`. A scripted map stop
  (events 4/83, usually paired with an event 2 that zeroes `backMove`) freezes the
  script. The only exits are (a) the screen-clear release (`enemyOnScreen == 0`
  restores `backMove` and the clock resumes), (b) a `superEnemy254Jump` fired by a
  linknum-254 enemy death, or (c) `forceEvents` (event 53), which ticks the clock
  even at `backMove == 0`.
- Boss fights lean on the linkgroup death cascade: killing any member of a linknum
  group kills the whole group, the screen clears, release (a) fires, and a later
  timed event (11) ends the level.
- The softlock (reported on ep4 HARVEST): the script stages the fight in the same
  event tick that stops the map. HARVEST t=6331 spawns an invisible ground anchor
  (enemy 66 — no turrets, no launcher, no accel) at y=-108 with the boss linknum
  10, sets the group's armor to 254, and arms the boss bar. Kill all 12 visible
  boss pieces during their descent (t=6280..6331) and the anchor misses the
  cascade: it's inside the y∈[-112,190] keep-alive band, so it counts toward
  `enemyOnScreen`, but shots can't reach it, the stopped scroll can't carry it in,
  and the frozen clock can't move it. The armed bar then shows a full-health
  "boss" that is already dead.
- Why the anchor exists at all: the `stopBackgroundNum == 1` release census only
  covers the two batches drawn with `tempBackMove = backMove` — `JE_drawEnemy(50)`
  (slots 25–49) and `JE_drawEnemy(100)` (slots 75–99). HARVEST's 12 boss pieces are
  event-7 "Top Enemy" spawns, i.e. slots 50–74, drawn by `JE_drawEnemy(75)` on
  layer 3 — invisible to that census. The event-10 anchor lands in slots 75–99, so
  it alone holds the stop, and the cascade that kills the pieces kills it too.
- The watchdog (`enemy_stuck_orphaned` = `enemy_stuck_above_screen` +
  `enemy_link_group_reachable`, counted by `count_stuck_above_screen`): each
  tick a dedicated full-pool scan sets `enemyParkedAbove` = live enemies that are
  `ey <= -58` and vertically frozen (`eyc<=0`, `eycc<=0`, `fixedmovey<=0`). -58 =
  ascending shots live to y=-40 and the widest hitbox reaches ~17px below `ey`, so
  ≤ -58 is unhittable; vertical stasis plus a frozen clock means the enemy can be
  neither hit, moved by its own kinematics, nor scripted in. When a non-`forceEvents`
  stop has been held ≥ `MAP_STOP_STALL_LIMIT` (210 ticks ≈ 6 s) with ≥1 such
  enemy, they are culled like anything drifting off the playfield (`enemyAvail=1`,
  no explosion or score); the normal screen-clear release then resumes the level,
  just as the death cascade would have. Culling it also auto-clears the boss bar
  (no live linknum member, so `draw_boss_bar` zeroes `link_num`).
- Horizontal state must be ignored. HARVEST's anchor is spawned with an
  Enemy-Global-Accel event (type 20, `dat=3`) that sets `excc=3`, a sideways sway.
  It never lifts the anchor into reach (the `exrev` wobble keeps `exc` near 0), but
  an earlier predicate that demanded `excc==exc==0` rejected it outright
  (`parkedAbove=0`, watchdog never armed). Only the vertical axis decides "stuck
  above the reach line".
- Detection is a standalone scan, not the draw-loop on-screen census. The
  on-screen census only sees enemies inside the x-window and skips damage-flashing
  ones, so a horizontally-swaying anchor could blink out of the count and keep
  resetting the timer. The dedicated scan sees it every tick.
- Independent of the other on-screen enemies. A real fight leaves small enemies
  (HARVEST types 142/559, `move=(0,0)`) frozen in place beside the anchor; they
  ride the now-stopped scroll, so `enemyOnScreen` stays > 1 (e.g. 4). Requiring
  `parkedAbove == onScreen` (a dead first attempt) never fired. Those small
  enemies are killable and are a normal clear-the-arena step; they must not gate
  recovery. The watchdog keys on `parkedAbove != 0` alone and culls only the
  stuck-above set (they sit at visible `ey > -58`, so the scan never touches them).
- **The geometry test alone false-fires on every live HARVEST fight.** The anchor
  is spawned at `ey=-108` and frozen (event 19 `dat2=0`, event 20 `dat2=0`) whether
  or not the boss is alive, so a normal fight trips the same predicate and the
  watchdog culled the anchor ~6 s in. That released the stop mid-fight: the clock
  resumed, `t=6332` restored `backMove3=2`, the still-alive boss scrolled off the
  bottom, and the level ran on to its `t=7100` end event — the boss fight simply
  evaporated. Hence the second gate, `enemy_link_group_reachable`: killing ANY
  linkgroup member cascades the whole group, so a single live partner that is *not*
  itself stuck-above means the fight is winnable and nothing is orphaned. The 12
  boss pieces rest at `ey ≈ +4`/`+32` (they spawn at -56/-28, ride `backMove3=2`
  for 30 ticks, then event 19 sets `eyc=-2` and exactly cancels the ride), so
  during a real fight the anchor is always rescued. `linknum == 0` is "unlinked" —
  those never cascade, so each is its own group and is never rescued.
- Why it can't false-fire otherwise: a descending boss (`eyc>0`), a bouncer
  (`eyc` flips), or a homing chaser (drives `eyc>0` toward the player) all fail the
  vertical test and resolve on their own; a boss killed normally cascades the
  anchor away before the timer. In practice it fires only in the orphaned-anchor
  softlock. `forceEvents` levels self-drive the clock and are exempt.
- The crash-log dumps `parkedAbove=` / `stallTicks=` and a per-enemy table
  (`ex,ey exc,eyc excc,eycc link armor type`), marking each stuck-above enemy
  `(orphaned)` — what the watchdog culls — or `(group reachable)` — a live fight's
  parked anchor, left alone — so a stuck scroll shows exactly what is holding it.

## Static analysis (MSVC `/analyze`)

Run it with:

```
MSBuild visualc\opentyrian.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /p:EnableMicrosoftCodeAnalysis=true
```

A 2026-07-27 sweep took it from **66 warnings to 17**. Four real defects came out of it:

- `game_menu.c` filled `char tempStr[67]` with `memcpy(..., sizeof(tempStr))` from
  `mainMenuHelp[][66]` — a one-byte over-read on all ten sites, past the end of
  the array on the last row. Now `SDL_strlcpy`.
- `ships[]` holds `SHIP_NUM + 1` entries but only ids **> 90** are "extra" ships,
  so 19..90 read past the end in `JE_getCost` (mainint.c) and twice in
  `JE_getShipInfo` (varz.c). Bound against the array, not the extra-ship id.
- The MIDI SMF parsers (`fluid_music.c`, `win_native_midi.c`) grew their event
  buffers with `p = realloc(p, ...)`, which leaks the block and then writes
  through NULL if it fails. Both now use a local `grow_buf` + `goto oom`; growth
  (doubling from 256) is unchanged.
- ~12 allocations were used unchecked. New `malloc_die` (file.c) joins the
  existing `*_die` family — reports through crashlog and exits. It rounds a
  zero-byte request up to one byte, so "NULL means out of memory" stays true for
  every caller (`malloc(0)` may legitimately return NULL, and joysticks with zero
  buttons hit exactly that).

Three annotation macros in `opentyr.h` do the rest, all no-ops in codegen:
`OT_NORETURN` (config_file has its own `CONFIG_NORETURN` to stay standalone),
`OT_RET_NOTNULL`, and `OT_ASSUME` — the last states an invariant a helper
guarantees but the analyser can't see through (`clampi` in custom_weapon.c, the
global scratch `temp` in mainint.c). **Only use OT_ASSUME on a bound checked by
hand**: an untrue one silences a real bug instead of a false one.

**`__analysis_assume` can crash CL.** Adding `temp < COUNTOF(...)` as a second
condition on the weapon-bay loop in `JE_playerMovement` made `cl.exe` die with
0xC0000005 during analysis (the ordinary build was fine). `OT_ASSUME` on the line
inside the loop clears the same warning without crashing. If the analysis run
fails with MSB6006, suspect the most recent bound you added, not the code.

The **17 that remain are deliberate**:

- *Upstream / vendored (8)* — `opl.c` ×4, `MIDIContainer.h` ×2, `animlib.c`,
  `mtrand.c`. Left alone on the same rule as the comment sweep.
- *C6262 large stack frames (5)* — `config.c`, `custom_weapon.c` ×2, `mainint.c`,
  `tyrian2.c` (86 KB). Moving them to the heap is a real behaviour change for no
  benefit; they have always worked.
- *Network (2)* — `network.c`, `tyrian2.c` packet derefs. Guarding them means
  touching two-player sync, which can't be tested here.
- *`destruct.c` ×2* — a C6001 on `destruct_player.unit` (static storage, so it is
  NULL before the `malloc_die`, and `free(NULL)` is fine either way) and a C6385
  on the HUD row copy that survives both an entry guard and an `OT_ASSUME`.

## General pitfalls

- Sprite banks: gameplay sprite draws must use the right one of the four sprite
  banks; a wrong-bank draw renders garbage that looks "almost right".
- `enemycycle` indexes `egr[]` 1-based; an out-of-range or zero index underflows
  `blit_sprite2` into a wild read. `blit_enemy` skips anything that doesn't name a
  real in-sheet sprite.
- Sidekick body blits are 1-based (`blit_sprite2` reads `offsetTable[index-1]`); a
  2x2 mount also reads index+1/+19/+20 and the engine adds the charge stage
  (0..pwr), so clamp base+frames to `1..count-pwr` (minus 20 for 2x2 mounts).
- Menu data model: `menuInt` (labels) / `menuChoices` (counts) / `menuHelp` must
  stay consistent when adding rows to any mode menu.
- Fonts: TINY_FONT glyph shades — body 7, edge 3, sparse highlights 10; the
  minimum safe brightness offset is -2. `'~'` is a brightness-toggle in
  `JE_outText*`/`JE_dString`, never printed, so keep it out of help-line format
  strings.
- The in-game debug menu (opened from the shop front menu, or the Esc-pause "Debug
  Menu" row while Debug Mode is on — there is no key shortcut) is the extension
  point for cheat/diagnostic rows; new rows follow the table pattern described
  under *In-game debug menu* above.
- The Doxygen-style `/** */` documentation in upstream files (font.c,
  config_file.c, …) is upstream convention; leave it be.
- Dead-code sweeps: a few symbols are unreferenced *by design* and should not be
  pruned. `config_file.c` (Carl Reinke's upstream config parser) and `opl.c` (the
  DOSBox OPL2/OPL3 emulator) are vendored libraries whose published API is kept
  whole — `config_deinit`, `config_find_sections`,
  `config_get_or_set_{string,uint}_option`, the `foreach_option_value` macro,
  `adlib_reg_read` and `adlib_write_index` all have no in-tree caller. Likewise
  `JE_readTextSync` (tyrian2.c) is an intentionally empty stub: its body is
  `#if 0`'d out alongside the other `TODO: NETWORK` placeholders, and it is still
  called from the level-script `'S'` opcode. Enum members that look unused
  (`sndmast.h` sound ids, `destruct.c` mode/shot/team ids, …) are positional —
  they index shipped data tables, so removing one shifts everything after it.
