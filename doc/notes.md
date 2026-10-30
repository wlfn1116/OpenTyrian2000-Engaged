# Maintainer notes

Rules that are easy to break and hard to recover from the code live here. Keep
player instructions in [GUIDE.md](../GUIDE.md) and build recipes in the platform
READMEs.

Keep code comments local. Put subsystem contracts here.

## Contents

- [Build contracts](#build-contracts)
- [Rendering](#rendering)
- [Endless](#endless)
- [Menus and touch UI](#menus-and-touch-ui)
- [Networking](#networking)
- [Ships, weapons, and item data](#ships-weapons-and-item-data)
- [Audio, platforms, and logs](#audio-platforms-and-logs)
- [Level and data formats](#level-and-data-formats)
- [Tests](#tests)

## Build contracts

`build-all.ps1` builds PC, Switch, and Vita targets. Collected artifacts go under
`build/`; `-FailFast` stops after the first failed target.

- Windows and Linux ship x86-64 and ARM64 builds.
- FluidSynth and native MIDI are Windows x86-64 only.
- MSVC has no `-fsigned-char`, so `opentyr.c` checks the assumption at compile
  time.
- The Windows ARM64 job builds SDL2 and SDL2_net from source. SDL2 needs
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` and `-DSDL_LIBC=ON` there.
- Switch builds run under devkitPro bash. Vita builds use native CMake and Ninja.
- VitaSDK's linker starts the data segment on the next 64 KiB boundary, and
  `vita-elf-create` appends about 3 KB of import tables after the code.
  `vita/CMakeLists.txt` defines `__sce_headroom` as 64 KiB so they always fit.
  Without it, an unlucky code size fails with "Cannot allocate ... bytes for
  SCE data". The gap costs no memory because each segment loads separately.
- Console Release builds define `NDEBUG`.

For MSVC analysis:

```text
MSBuild visualc\opentyrian.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /p:EnableMicrosoftCodeAnalysis=true
```

Check range warnings instead of hiding them. `OT_ASSUME` belongs after a real
bounds check.

`make debug` compiles C99 with `-Wall -Wextra -Werror`, so GCC rejects what MSVC
lets through:

- Compare signed and unsigned values with an explicit cast on one side.
- `COMPILE_TIME_ASSERT` expands to an enum and works inside a function.
  `SDL_COMPILE_TIME_ASSERT` is a local typedef under C99; use it only at file
  scope.
- `JE_tyrianHalt` and `JE_tyrianExitWithoutSaving` are `OT_NORETURN`. Code after
  their calls is unreachable and draws MSVC C4702.

### Source layout

`src/` groups the game by topic: `audio`, `core`, `destruct`, `endless`,
`game`, `input`, `level`, `menus`, `net`, `platform`, `qa`, and `video`.
`opentyr.c` (`main`) stays at the root, and `src/midiproc` is vendored.

- Each build lists the source folders itself: `SRC_DIRS` in the root
  `Makefile`, `SRCDIRS` in `switch/Makefile`, and `OT_SRC_DIRS` in
  `android/app/src/main/jni/src/Android.mk` and the Vita, iOS, and macOS
  `CMakeLists.txt`. A new folder goes into all six. The Visual Studio project
  and its filters list every file.
- Every folder is on the include path, so an include names only the header.
- `endless_internal.h`, `net_internal.h`, and `qa_internal.h` are private to
  their folders. Each stops the build unless the including file first defines
  `ENDLESS_INTERNAL_ACCESS`, `NET_INTERNAL_ACCESS`, or `QA_INTERNAL_ACCESS`.
  The QA suite may also use the Endless header.
- `game_state.c` holds only state that several subsystems share: enemy pools,
  level flow, and scratch values. New state belongs with the module that owns
  its behaviour.
- Rollback item names keep their old `t2.` and `vz.` prefixes. The layout
  fingerprint hashes the names, so renaming one is a wire change.

## Rendering

The simulation runs at 35 Hz. `render_list.c` records one tick and replays it at
the display rate.

### Render list

- Give every moving object a stable `rl_current_id`.
- Exact replay must reproduce the recorded frame.
- Read velocity from the object being drawn. Pool slots are reused.
- Give conditional pieces separate IDs when their command count can change.
- Store rounded sub-pixel remainders in `rl_current_sub_x/y`.
- Keep ship motion separate from motion local to an attached shot.

`rl_finalize` pairs the current and previous tick. Presentation code uses that
pair for interpolation; simulation state does not.

`rl_get_ship_override_dx/dy` is the displayed ship offset. Anything attached to
the displayed ship must use it. `player[].x/y` remains the tick position online.

### Feedback surfaces

Ice, water, and lava keep filtered background in `render_gs` and the current
background in `smoothie_frame`. Entities never enter the persistent surface.

Apply full-screen colour and brightness to both sides of a residual comparison.
Keep text out of feedback surfaces and draw the performance overlay after the
final composite.

For low-resolution feedback with a supersampled foreground:

1. Replay the filtered background head at 1x.
2. Expand it into `pf_hi`.
3. Draw the background tail at the foreground factor.
4. Draw foreground commands over both.

At 1x with Smooth FX off, the Vita keeps the tick's endpoint in
`smoothie_frame` and composites display-rate foreground into
`smoothie_present_frame`.

### Display-rate ship movement

The real-time ship integrator (`vt_ship.c`) affects simulation.

- Disable it for demo recording and playback.
- Advance it on every presentation loop, including loops that also run a tick.
- Preserve joystick press edges used by menus and pause.
- Rollback sessions use the host's Smooth Motion choice.
- Rollback shows the local live position; Delay-Based shows the delayed tick
  position.
- Remote ships extrapolate from the last tick, then ease. Large jumps snap.
- Apply the Endless move scale once. VT scales its own step, so the fixed-tick
  input block skips input that VT already moved.
- The fixed-tick block scales the whole step before the input tuple. It predicts
  the simulation's momentum with the same `ship_axis_velocity` helper.

### Gauges and effects

Presentation-only drawing includes fades, picture wipes, gauges, boss bars, the
special meter, and the Zinglon pillar. Their state still advances once per tick.

- Interpolate gauge fill, not its base row.
- Linked Arcade merges special-meter edges from both movement passes.
- Re-arm the meter when a new special replaces a running recharge.
- Advance ready flashes only on live ticks.
- Silent rollback replay consumes gameplay RNG but emits no presentation sparks.

Supersparks share one ring. `JE_beginSPPass` and `JE_discardSPPass` undo a
presented pass that gets replaced. Seeded presentation effects must not draw from
the simulation RNG.

The ring has 101 classic slots and an upper window that Extra Sparks sweeps.
`JE_drawSP` steps the live range of the upper window (`sp_live_lo` to
`sp_live_hi`) and then the classic slots, both from the top down, which is the
order of a full scan. Every spawn must take its slot from `next_superpixel`,
which widens that range; a spark written any other way would never be drawn.
`qa_test_spark_ring_bounds` compares bounded passes with a full-ring scan
through cursor wraps and rewinds.

### CRT filter

Every present path goes through `present_frame`. It draws the frame and the CRT
layers; the caller then draws the touch controls unfiltered. The layers belong
to `main_window_renderer` and are released before it is destroyed. A CPU
renderer hides the CRT Filter submenu, whose settings are display preferences
outside the enhancement presets. Tuning lives in `crt_looks[]` (per Strength),
`crt_bends[]` (per Curvature) and `crt_bar_depths[]` (per Rolling Bar).

Every layer uses SDL's standard blend modes. The Vita's GXM renderer has no
custom ones. Alpha and colour mods stay on a texture across frames, and even a
NONE copy applies them, so a draw that sets one puts it back, unless that draw
is the texture's only user.

Lines:

- `crt_line_weights` gives every output row a weight, snapped to the rows the
  frame lands on (`crt_game_row`: the GPU's nearest mapping, or the Native
  scaler's floor mapping when the frame texture is 1:1). The gap sits at each
  game row's bottom edge.
- No lines under 400 output rows. Weights under 26 of 255 become 0, which keeps
  the redraw to the gap and shoulder rows.
- Those rows are drawn again from the frame with `SDL_BLENDMODE_MUL` and per-row
  vertex colours (`SDL_RenderGeometry`): c * (m + fill * (1 - m) * c), so bright
  channels fill their own gap.
- Without geometry (it fails, or SDL is older than 2.0.18) a 1 x H MOD strip
  draws plain lines.

Glow (Full):

- Halve the frame to at most 400 rows, copy it to 356x200, and cube it with two
  MOD draws so only bright pixels glow.
- Shrink to 178x100 and 89x50 with a [1 3 3 1] kernel: a half-weight center draw
  plus four diagonal ADD draws at alpha 32.
- Blend 89x50 back into 178x100 at alpha `glow_spread`, upsample to 356x200,
  and add the result over the picture at alpha `glow`.
- SDL2 reads a texture's scale mode when its command queue runs, so
  `crt_glow_from` flushes before it restores the frame's own mode.

Mask and vignette (Full):

- The mask is drawn with MUL. Each channel's multiplier is rgb + 1 - alpha, so
  a lit channel sits at most 1 above the unlit ones.
- Grille is one row of RGB columns stretched down the picture. Lit and unlit
  columns average 1.
- Slot is a tile of whole slots, as many as fit in `CRT_MASK_TILE_ROWS` rows
  (at least one), drawn in bands. A slot is `CRT_SLOT_ROWS` phosphor widths
  tall, and in the last `CRT_SLOT_GAP_ROWS` of them the lit channel drops to
  the unlit level. Odd column triads are offset by half a slot.
- `crt_slot_levels` floors the unlit level at (2r + g) / (3r) for r rows and g
  gap rows (0.75), the lowest that keeps the lit channel within 1 of it. It
  then solves the lit level so every channel still averages 1 over the tile.
- Off skips the mask. The vignette still draws.
- A phosphor is round(output pixels per game pixel / 5) wide, at least 1
  pixel.
- The vignette is crtemu's falloff, flattened.

Curvature:

- `crt_curve_begin` renders the frame, lines and glow into a target the size of
  `dst`. With the filter on and `dst` under 1000 rows, the target is scaled by
  the smallest whole factor that reaches 1000 rows, so 1-pixel lines survive
  the resample.
- `crt_curve_end` draws that target through a 64x36 mesh built in picture space,
  each vertex placed with the inverse bend. The mask and vignette are drawn
  flat afterwards.
- The bend maps screen point p to picture point p * (1 + b * q^2) per axis, with
  q the other axis, in -1..1 coordinates. Edge midpoints stay put and the whole
  picture stays visible.
- `mapWindowPointToScreen` and its float variant apply the forward bend
  (`crt_curve_source`) with the last present's bend (`crt_curve_shown`).
  Relative motion is not bent.
- Curvature also runs with the filter off, at 1x.

Corners and rolling bar:

- Both draw into the picture after the glow and before `crt_curve_end`, so they
  bend with Curvature. Like Curvature, they run with the filter off.
- Corners stretch one quadrant of a 128x128 black BLEND texture over each
  corner of the picture, at a radius of 4% of its height (at least 2 pixels).
  The alpha is the 4x4-sampled coverage outside a quarter circle.
- The rolling bar is a 1x64 black BLEND strip with a raised-cosine alpha,
  alpha-modded by `crt_bar_depths[]` and stretched to 20% of the picture's
  height. It enters above the top and leaves below the bottom in
  `CRT_BAR_CROSS_MS`, once every `CRT_BAR_PERIOD_MS`. It runs on `SDL_GetTicks`,
  so it keeps rolling in menus and pauses.

Every platform and output size gets the same layers. The one size floor is the
lines' 400 rows: a game row only one output row tall would be all gap.
`crt_curvature_available` hides the Curvature row when SDL is older than 2.0.18.
`video_test_crt_rows`, `video_test_crt_curve` and `video_test_crt_layers`, run
from `qa_test_present_paths`, cover the snapping, mask balance, phosphor widths,
bend inverse, supersample factor, pointer mapping, slot balance, gaps and
stagger, corner coverage, and the bar's profile and timing.

### Coordinates

The frame is 356x200. The playfield is 299x184 and the HUD is 57 pixels wide.

| Space | Rule |
| --- | --- |
| World | Draw through `game_screen` and `PLAYFIELD_LEFT` |
| Final playfield | Cropped to screen x=0 by the compositor |
| Menus | Centered 320-pixel canvas |
| HUD overlays | Composited-buffer coordinates |

SDL window events use points; renderers use output pixels on high-DPI backends.
Size render rectangles from `video_output_size`, and convert input with
`video_output_pixel_scale`. Pointer positions must go through
`mapWindowPointToScreen` or its float variant, which also follow the CRT
Curvature bend.

Validate every `Sprite2_array` index. A 2x2 sprite reads the base index plus 1,
19, and 20.

Destruct leaves open sky between its HUD boxes. Terrain, wall generation,
collision ceilings, and shot trails must respect that gap. Clamp wall writes to
`baseMap`; the next struct field is a pointer.

### Frame-row threads

`video_rows` in `video.c` splits a job's rows between the main thread and a
pool of helper threads. Seven sites use it:

| Site | Overlay | Work |
| --- | --- | --- |
| `VIDEO_ROWS_HI` | `HI` | `present_hi` palette conversion of hi-res frames |
| `VIDEO_ROWS_FIT` | `NATIVE` | The Native scaler `nn_fit_32/16` |
| `VIDEO_ROWS_REPLAY` | `REPLAY` | The normal-level playfield replay, at any scale |
| `VIDEO_ROWS_COMPOSE` | `COMPOSE` | Playfield composite and HUD expansion at hi-res |
| `VIDEO_ROWS_SMOOTHIE` | `SMOOTHIE` | Every smoothie pass in the present loop |
| `VIDEO_ROWS_TICK` | `TICK` | Replays run once per tick |
| `VIDEO_ROWS_FLAT` | `PLAIN` | `nn_32`, the None and 2x to 5x scalers |

`SMOOTHIE` covers the feedback replays, the background pass and its filters,
the expand, the background tail and the foreground. `TICK` covers the residual
reference replay, feedback trim and commit, and `rl_replay_bg_tick`, which
builds the Vita's cached plasma. `PLAIN` converts each source row once and
copies it into the other output rows of its scale, so a band covers whole
source rows. Unless the Native scaler is selected, menus and gameplay with
Sub-pixel Off or Smooth Motion Off present through it.

Output is the same on any number of threads. Every platform sends tick replays
through `TICK`. On the Switch, Smooth Motion with the default 4x scaler also
uses `HI`, `COMPOSE`, and `REPLAY` or `SMOOTHIE`. By default the Vita renders
at 1x: normal levels replay through `REPLAY` at 1x, smoothie levels draw their
foreground through `SMOOTHIE` at 1x, and every frame converts through `PLAIN`.
A Vita player who raises Sub-pixel above 1x, directly or through Auto and a
larger Scaler, gets the same hi-res sites as the Switch.

- `init_video` starts the pool unless Render Threads is 1, so creating the
  threads and running the self-test below happen at startup, outside any frame.
  A pool that is not running yet, as after Render Threads changes from 1,
  starts with the next job of at least 64K pixels (a 1x frame has 71,200). Jobs
  under 64K pixels or with one row always run on the main thread.
- `video_rows_max_threads()` sizes the pool to the CPU count, at most 16 threads
  (15 helpers). Switch and Vita give games three cores, so they count exactly
  three CPUs and run two helpers.
- A startup self-test runs a small job through the helpers, which must each
  finish within two seconds and match a single-threaded result. If the test
  fails, or not even one helper and its semaphores can be created, every job
  stays on one thread for the rest of the run. A pool that created only some
  helpers runs with those.
- When a helper misses the deadline, all helpers are detached and the
  semaphores and test buffers stay allocated, so a late helper never touches
  freed memory.
- On Auto, each site tunes its own thread count. It times 1, 2, 4, 8 and 16
  threads up to the pool size, plus the pool size itself, for 20 frames each,
  taking turns. Each sample is the site's own cost in one frame: the rows plus
  `SDL_UnlockTexture`, where OpenGL drivers copy the texture on the CPU.
  Whole-frame times would mix in logic ticks, vsync and frame-cap waits, and
  the other sites' tuning runs.
- The pick uses each count's median and 95th percentile (nearest rank, the 19th
  of 20 frames). A larger count wins only when its median is under 90% of the
  current pick's and its 95th percentile is no higher than the pick's. A count
  with two of its twenty frames above the pick's 95th percentile loses whatever
  its median, and one fast frame cannot win.
- `HI`, `NATIVE`, `COMPOSE` and `PLAIN` run once per frame and time each call. A
  new pixel count starts a new tuning run.
- The chosen count stays timed after the pick, in 120-frame windows. A window
  moves when more than half of its frames cost over twice the median the count
  had in its tuning run, or more than half cost under half of it. Two moved
  windows in a row start a new tuning run, which compares every count again,
  one thread included. This follows cost changes the pixel count does not
  show, such as sprite load, filters and smoothies on the round sites.
- `REPLAY`, `SMOOTHIE` and `TICK` can run several times per frame with jobs of
  different sizes, so they tune on rounds. Every call in a round uses the
  round's thread count, the round's time is the sum of its calls, and
  `video_rows_end_frame` closes the round after each present in
  `scale_and_flip` and `present_hi`. Tick replays get their own rounds because
  they use their own site. A round whose largest job differs from the previous
  round's restarts the tuning run. A round that never reaches a present, as in
  a fast-forwarded test replay, stays open on its first thread count.
- `video_rows` never runs more threads than the job has rows.
- Render Threads is `render_threads` in `video.c`, saved as
  `[video] render_threads`, with 0 for Auto. A number skips tuning and uses
  that many threads, capped at the pool size; 1 never starts the pool. The
  config loader lowers a value above `video_rows_max_threads()`.
- A job finishes before `video_rows` returns, and `deinit_video` joins the
  helpers. A band function may write only its own rows and must treat
  everything else as read-only.
- The scalers and `present_hi` return without drawing when `SDL_LockTexture`
  fails, so no band writes through an unset pointer and the texture keeps the
  previous frame.

Band replay lives in `rl_replay_common` (`render_list.c`). Every replay draws in
bands, in every phase and at every scale, unless `rl_band_reserve` cannot size
the item list. The usual loop still computes each command's position, then
collects it with its target surface, the playfield or the feedback scratch.
`rl_band_draw` sends the pending items through `video_rows`, and `rl_band_rows`
draws them into copies of the surfaces that start at the band's first row.

- Smoothie filters and darken are barriers: the pending items are drawn first,
  then the barrier runs on the whole surface. Darken stays on one thread
  because it reads the row above in the same pass.
- The playfield clear and the lazy scratch clear run inside the next batch of
  bands.
- At 1x, sprites, background rows and health bars use the same drawers as a
  sequential replay, on the band copy with the row shifted by the band's first
  row. Table sprites, background rows and the unclipped sprite2 filter clip by
  address range, so a sprite that wraps across a band edge lands in the band
  that owns the address. The sprite2 clip variants clip whole rows against the
  surface height.
- Above 1x, background rows go through `blit_background_row_scaled_batch`, so
  each band has its own scratch buffer.
- Stars, sparks and the grade use band versions at every scale
  (`draw_starfield_star_band`, `rl_draw_superpixel_band`,
  `filter_screen_apply_scaled_band`). Their visibility tests use absolute rows.
- A drawer may run in bands only if it writes only its own rows, reads only the
  pixels it writes, keeps no state between calls, and tests visibility in
  absolute rows.

The scaled smoothie filters split by sub-row set.
`*_filter_scaled_subrows(dst, src, scale, sub_first, sub_last)` processes the
rows whose index modulo `scale` lies in `[sub_first, sub_last)`. Each filter
row reads only its own row and the rows `scale` above and below it, so the sets
are independent. `rl_replay_common` dispatches `rl_filter_rows` with one row
per set, so a 4x filter uses at most four threads. 1x filters stay on one
thread; lava and water chain every row there.

`compose_hi_frame` (`game_loop.c`) runs the playfield composite and the HUD
expansion as one job over the 200 1x rows; the two write separate parts of
`vga_hi`. The spotlight anchor is computed once before the dispatch. The
smoothie expand into the hi-res buffer runs through
`expand_frame_to_hi_threaded` on `SMOOTHIE`; menus keep the one-thread
`expand_frame_to_hi`.

Each tuning run ends with one line on stderr per site:

```text
video: tick replay rows on 2 threads (median/p95 0.040/0.041 ms on 1, 0.019/0.023 ms on 2, 0.022/0.062 ms on 4, 0.028/0.031 ms on 8, 0.035/0.038 ms on 16)
```

The line names the count each site settled on, then the median and 95th
percentile for every count it tried. For `REPLAY`, `SMOOTHIE` and `TICK` the
times are round totals. `rows on one thread` means no larger count had a median
under 90% of one thread's without a higher 95th percentile. A fixed Render
Threads value prints nothing, since nothing is tuned.

`video: <site> rows moved from <ms> ms per frame; tuning again` means two
windows in a row moved, so the site starts a new tuning run.
`video: frame rows stay on one thread` means the pool did not start.

Linux and macOS show these lines in the terminal that started the game. The
Windows build has no console; start it from Command Prompt with `2> video.log`
to keep them. The Switch and Vita builds do not save stderr, so read the
decision from the debug perf overlay there.

To show the overlay, turn on Debug Mode in Setup > Diagnostics, then pick
Debug Menu > Perf Overlay from the in-game menu during a level. Two lines under
`FPS` break the frame time down, averaged over one-second windows:

| Value | Meaning |
| --- | --- |
| `TICK` | Game logic and tick drawing, between `JE_starShowVGA` calls |
| `DRAW` | Frame start to the present call: replay, compositing, HUD |
| `PRES` | The present call: conversion, upload, SDL present, vsync wait |
| `MAX` | Longest gap between two presents in the window |

`TICK` is per simulation tick; the others are per presented frame. Gaps of a
second or more, such as menus and pauses, are left out. `frame_times` in
`game_loop.c` collects the values, and `frame_times_get` returns the last window.

Under `ALPHA`, one `ROWS` line appears for each site used since the previous
overlay draw, read from `video_rows_stats`. The labels follow the site table
above. The two times are medians from the last tuning run; for the round sites
they are round totals.

| Line | Meaning |
| --- | --- |
| `ROWS HI 8T 0.16/0.03ms` | 8 threads; one- and 8-thread medians |
| `ROWS REPLAY 1T 0.36/0.40ms` | One thread; lowest larger-count median second |
| `ROWS HI TUNING` | A tuning run is in progress, the first one or a re-run |
| `ROWS HI 8T SET` | Render Threads fixed at 8, so nothing is timed |
| `ROWS NATIVE 1T SMALL` | One thread; under 64K pixels or one row |
| `ROWS HI OFF` | The pool did not start |
| `ROWS UNUSED` | No site ran since the previous overlay draw |

Each site keeps a call count, and its line appears when the count has moved
since the previous overlay draw. That relies on every overlay draw being
followed by exactly one present.

`qa_test_present_paths` checks the present and Native paths pixel by pixel
against a direct lookup and against one thread, compares every pool size from
two threads up with one thread, and fails if the pool does not start on a
multi-core machine. It also converts frames through `nn_32` at 1x to 5x, on one
thread and on the whole pool. `video_test_rows_stats` drives `video_rows`
through an Auto tuning run and both fixed settings, and checks the per-site
counts and modes. `video_test_rows_rounds` checks that the calls of a round
share a thread count, that a tuning run ends after its rounds, and that a round
with a different largest job restarts it.

`video_test_rows_pick` feeds the pick synthetic frame times. A steady 20% gain
must win. A count that spikes on two frames in twenty, a count that is faster on
only one frame, and a gain under 10% must lose. One slow frame must not outweigh
a steady gain. `video_test_rows_watch` checks that two slower or two faster
windows start a new tuning run, and that a single moved window or windows with
exactly half their frames slow do not. `video_scale_test_lock_failure` hands
every scaler a static texture, which `SDL_LockTexture` refuses. A scaler that
writes anyway goes through an unset pointer and crashes the suite.

`qa_test_background_bands` draws random background rows in thread bands and
compares them with one full-surface pass. `qa_test_filter_subrows` runs each
scaled filter one sub-row set at a time in a rotated order, and across threads,
against the scalar loops. `qa_test_flat_bands` draws every 1x replay drawer
over random band cuts, edges and wraps included, against one full-surface pass,
and `rl_test_superpixel_bands` does the same for sparks at 1x to 4x.

### Pixel loops

`swar.h` holds two layers for 8-bit pixel loops:

- `Swar8` packs eight lanes into a `uint64_t`, or four into a `uint32_t` on
  32-bit targets such as the Vita.
- `Vec8` is a 16-lane SSE2 or NEON vector. `VEC8_BYTES` is 16 when the compiler
  targets SSE2 (`__SSE2__` or x64 MSVC) or NEON (`__ARM_NEON`), and 0 otherwise.

Loops run 16-byte vectors first, then SWAR words, then single pixels. Vectors
cover the 1x tile rows, the expanded hi-res rows, the screen grade, iced blur
and blur, the scaled lava and water spans, and the spotlight's dark spans. The
1x lava and water loops stay SWAR because their waver changes every 8 pixels.
In-place lava and darken stay scalar because each pixel reads neighbours the
same pass already rewrote.

Every path must reproduce the frozen scalar copies in `qa_test_swar_pixel_loops`
byte for byte. Windows x64 tests the SSE2 path; the Linux aarch64 suite in CI
tests the NEON path.

## Endless

### Module ownership

| File | Owns |
| --- | --- |
| `endless.c` | Run lifecycle, milestones, summary |
| `endless_rng.c` | Seeds and structural RNG |
| `endless_level.c` | Level and music choice |
| `endless_combat.c` | Scaling, tiers, combat modifiers |
| `endless_perks.c` | Perk rules |
| `endless_shop.c` | Outpost, prices, E-Shop, gamble |
| `endless_mods.c` | Modifier registry and text |
| `endless_course.c` | Course generation and selection |
| `endless_save.c` | Saves and sortie snapshots |
| `endless.h` | Public interface |
| `endless_internal.h` | Private interface and shared tuning, for `src/endless` and the QA suite |

Keep tuning with its owner.

### RNG and level shuffle

Run structure uses SplitMix64 streams derived from the seed and depth. Combat
timing must not change later levels, shops, courses, or perks.

- Draw order is a compatibility interface.
- Give new work a unique phase salt.
- Append draw phases when possible.
- Store or reproduce music choices across retries.
- Generate courses in this order: gather, modify, filter, sort, uniquify, cache.

`endlessRunBaseRule` values are save, record, and wire order. Use
`endlessBaseRuleAtMenuIndex` to change menu order.

The Deck rules (Varied Deck, Same Deck) consume deterministic bags:

- Count each distinguishable level section once.
- Advance the cursor for every hand, including discarded Radar hands.
- Do not spend structural RNG while dealing from the bag.
- Keep the end of one bag out of the next bag's opening window when the pool is
  large enough.
- Publish the live cursor online. A disagreeing peer re-anchors to the charting
  player's cursor before dealing again.

Custom Endless has three pool modes:

| Mode | Pool |
| --- | --- |
| Off | Stock episodes only |
| Mixed | Stock episodes, then custom episodes |
| Custom Only | Custom episodes, with stock fallback if none are safe |

The setting is effective only when containers exist. It is saved as
`custom_endless` only when non-zero, and Clear `.clv` resets it. The pool holds
at most 640 entries before duplicate sections are removed.

Custom episodes use extended IDs beginning at 6. Courses, sortie snapshots, and
level-bag anchors keep these IDs instead of the container's base episode. The
IDs are positions in the active collection.

Custom Endless saves preserve that order in `custom_collection`. An offline
load restores the list only when every named container is installed. Older
saves without the field use the current file-name order, which can retarget a
stored extended ID.

### Combat

- Scale raw damage before `enemy_hp_divisor100` spends it.
- Decode piercing damage before scaling and encode it afterward.
- Keep piercing carry and repeat-hit state on the bullet.
- Use `enemy_has_boss_bar()` for boss classification.
- Round percentage effects that would vanish at small values.

`enemy_logical_death` owns kill count, bounty deduplication, Shockwave,
Martyrdom, and Chain Reaction. Destruction sites go through
`enemy_kill_group` or `enemy_part_destroy` so drops, transforms, links, events,
and credit stay together.

`enemy_death_payout` chooses the body, cash, datacube, or Super Arcade pickup.
`player_credit_cash` applies the credit rule. Do not choose payout at kill sites.

`healthbar_max` stores full damageable armour. Armour 255 means invulnerable and
must never become a denominator.

`endlessMode` controls run flow, saves, prices, and pickup replacement.
`endlessFxActive()` controls combat scaling, modifiers, perks, and tiers.
`reworkActive()` gates the Endless reworks of stock items: on wherever
`endlessFxActive()` is, and elsewhere per `reworkEnabled[]` (Setup >
Enhancements > Reworks). A rework must add no unregistered simulation state.

### Health and tiers

Normal difficulty uses these ceilings:

| Lever | Curve | Ceiling | Zone |
| --- | --- | ---: | ---: |
| Ordinary armour byte | `100 + depth * 4` percent | 600% | 101 |
| Ordinary overflow | Same curve through the damage divisor | 1200% | 221 |
| Elite and champion | Piecewise 2x, 4x, 6x | 6x | 99 |
| Boss | Piecewise 1x, 9x, 20x, 32x | 32x | 199 |

`endlessBossRamp100` and `endlessEliteRamp100` are authoritative. Their unit is
`ENDLESS_HP_MULT_SCALE`, which must equal `ENEMY_DAMAGE_ACCUM_SCALE`.

Tier rules:

- Settle a tier on the first processed frame.
- Cache it by nonzero link group.
- Keep pickups and permanent scenery normal.
- Pass it through hostile `enemydie` transforms.
- Treat transformed `enemyAvail == 2` bodies as wreckage.
- Keep the event scan derived and outside rollback state.

Tint belongs on projectiles and explosions that can outlive their source. Those
fields stay zero outside Endless.

Homing modifiers never target pickups, scenery, wreckage, or a downed co-op
ship. A homing enemy chooses its player once when created and keeps that target
until the ship goes down.

### Special pickups

`endlessSpecialPickup()` matches the two `JE_playerCollide` pickup branches.
Draw its glyph from `spriteSheet10` without changing the enemy record. Collision
follows the visible glyph, so changing the box is a wire change and requires a
`NET_VERSION` bump.

Seeded pickup sparks are presentation only and stay off silent rollback passes.
The Orange Shield orbit is a rework; without it the shipped special path runs.

The reworked Attractor (`endlessAttractorFire`, `endlessAttractorTick`) keeps
its target and speed in two bytes of enemy padding, so the registry layout did
not move. The tick runs in the simulation after chain reactions. It skips
`endlessSpecialPickup()` items, secret-level orbs (`evalue` 10001 to 20000),
and every `evalue` from 30000 up except the purple orb (30000) outside Super
Arcade. It lets go when the target ship dies.

### Modifiers and courses

`endlessModTable` owns modifier text, danger, payout, and class. Adding a
modifier also requires updates to masks, course pools, milestone rules, monitor
rows, help text, glyphs, and saved widths.

`endlessCanonicalMods` is deterministic and idempotent. Run it after generation,
purchase folding, launch, and restore.

Course-correction modifiers are exclusive. Canonicalization keeps the strongest:

| Modifier | Corrections | Maximum turn |
| --- | ---: | ---: |
| Seeker Rounds | 1 | 23 degrees |
| Twin Seekers | 2 | 23 degrees |
| Hunter Rounds | 1 | 55 degrees |
| True Aim | 1 | Direct |
| Kill Shot | 2 | Direct |

Concussion slows the ship a hit lands on. `JE_playerDamage` calls
`endlessConcussionHit()` whenever shield or armor drops, and a ready ship rolls
`mt_rand()` against a 20% chance. A landed roll slows that ship by 50% for 26
ticks, then holds a 140-tick guard in which no hit can concuss it again. Depth
and difficulty change none of these numbers.

- `endlessShipMoveScale` stacks the slowdown with Sluggish as `a + b - ab`, so
  either alone stays bit-exact and Afterburners resist both.
- The timer is per ship (`endless.concussion`), ticks with the other reactive
  timers, and clears at zone start. Only ready ships draw `mt_rand()`, so a
  sector without Concussion keeps its RNG stream.
- It is a scarce movement hazard in the milestone pool, weight 2 in the
  combinable pool, and a 1-in-20 draw appended to The End.

Milestones use the upcoming zone. Multiples of 100 exclude scroll-speed
modifiers; The End also excludes Dead Generator.

Milestone approach screens follow these rules:

- Show once per zone. A loaded save may show the warning again.
- Start `songBuy` on the warning screen and let the shop keep it playing.
- Use no simulation RNG. Online timing stays local; the shop HELLO resyncs the
  peers afterward.
- Use the send-off text and song on the first credits-zone approach.
- Use backdrop 5. Its dark artwork and palette bank 14 support the glow text
  and pulse bars used by every tier.

### Perks

Perks belong to a player. Use `perkMine` for the local shopper and `perkFx` for
the ship whose effect is being calculated.

- Guidance Package tags shots by bay and retargets after a kill. It re-encodes
  a beam pinned by the lone x sentinel (sx 101) as riding velocities on both
  axes and steers everything except patterns pinned to the ship on both axes.
- Twin Pods spends the second shot's power and ammunition. If the primary shot
  is refused, fire neither.
- Reinforced Prow scales damage dealt and received separately.
- Knife Fight measures hull clearance before HP scaling.
- Deflector fires only when shield falls and armour does not. Apply its refund
  after damage resolution so overflow remains unchanged.
- Executioner adds 17% per stack to hits on enemies below 25% health, or 20%
  for bosses. A kill whose finishing hit carried that bonus captures the
  nearest live enemy bullets before `enemy_kill_group` runs, so Shockwave
  cannot clear them first: within 64, 88 or 112 px, at most 6, 15 or 24
  bullets by stacks. Every captured bullet turns with its own damage, capped
  at 33, 66 or 100 per bullet. The earlier pool slot wins a tie. Stacks are
  the killer's own. Captures become the killer's shots through
  `player_shot_create_deflected` after the player-shot pass, so they first
  move next tick. Deflector and Executioner share `endlessReturnedShotDamage`,
  which keeps returned damage at 249 or below and off the 99 ice marker.
- Opening Salvo tags emitted shots and Chain Reaction pulses. Only front-gun
  fire spends the charge.
- Kinetic Converter uses actual shield or hull loss and applies only to
  affordable twiddle charges.
- Surveyor draws four kill drops, once per linked hull with the loot centered
  on the hull. The AST. CITY superbomb (enemy 800) and the ASTEROID1 orbiting
  orb (enemy 535) roll 1-in-(100/stacks) of the killing ship, the
  random-special datacube (enemy 513) 1-in-(300/stacks), and a score pickup
  1-in-(80/stacks). A landed score roll spends one more draw on
  `endlessSurveyorScoreFinds`: every score pickup worth 100 or more in any
  episode's enemy table that moves on its own, so the stationary Soh Jin
  artifact (enemy 475) is left out. Weights follow the gem ladder's
  35/25/18/12/7/3 split from 100 to 5,000 credits, interpolated between gems,
  so a cheaper pickup is always likelier. The purple star always drops as the
  2,000-credit star of episodes 1-3. The episode tables disagree at enemies
  507 and 509, so enemy 509 carries finds another episode defines and the
  spawn takes the find's sprite, value and popup; collection reads the popup
  from the enemy, not the table. A find whose carrier is not a moving cash
  pickup in the loaded table never drops. An unclaimed kill uses the deeper
  seat; either ship may collect.
- Afterburners act through `endlessShipMoveScale` and
  `endlessShipGravityDriftX`/`Y`: 10% speed per stack, and each stack resists
  16% of the Sluggish and Concussion slowdowns and of gravity drift, capped at
  80%. The sector values (`endlessMoveScale`, `endlessGravityDrift*`) stay
  perk-free.

Adding a perk grows `endless.perkOwned` and both rows of `endless.perkTakenBy`,
which moves every replay fixture hash. Prove the move is shape-only by
registering the old row width, then regenerate. The save, wire, and debug perk
blocks hold 32 perks.

Countermeasures triggers on every hull hit, has no cooldown, and ignores
shield-only hits.

Chain Reaction carries owner, salvo tag, and wave serial with each pulse. Queue
new pulses for the next tick and hit a linked hull once per wave. Damage and
destruction still go through the normal enemy paths.

### Economy

All wallet changes use `endlessCashCredit`, `endlessCashDebit`, or the shop trade
pair. The ledger must satisfy:

```text
earned - spent == wallet
```

Wallets and prices are `Sint64` bounded by `CASH_MAX`. `JE_cashLeft` may be
negative only while previewing an unaffordable row.

Item prices and weapon power levels share one scaling path in `item_prices.c`:
the expert multipliers, then the depth percentage. `JE_cashLeft` charges each
power level with `weapon_level_cost`, the figure the upgrade screen shows.

Sabotage charges are shared. Prices, perk rows, shop RNG, and paid-perk counters
are personal. Extra Perk pricing depends on held stacks, total bought picks, and
picks bought during the current visit. Wallet size and income do not affect it.

### Saves and retries

Current saves use the named-key format in `opentyrian.sav`:

| Section | Contents |
| --- | --- |
| `saves` | `SAVE_FILE_FORMAT` |
| `save`, `N` | Game slot |
| `endless`, `N` | Endless half of that slot |
| `highscore` | One score board |

Missing or invalid keys use defaults. Unknown keys are ignored. Add a new
Endless field to both section codecs and the online run codec.

Import runs only when `opentyrian.sav` is absent. It reads the 2398-byte slot
block that OpenTyrian, OpenTyrian2000, and DOS Tyrian write, then the Tyrian
2000 high-score boards when they follow. A `tyrian.sav` from a build of this
port before `opentyrian.sav` fails the checksums and is skipped. Binary
`endless.sav` and `tyrian.cfg` are never read.

Persistent tables are append-only. This includes run mode, difficulty, crew
size, Base Level rule, cash categories, and record marks.

Retry rules:

- Hardcore keeps only an in-memory sortie snapshot and writes no checkpoint.
- Restore the launch snapshot on retry.
- Return to Outpost uses Quit Level.
- Restart Zone keeps the same music and clears visit resume.
- Advance Alternating course ownership only after a cleared sector.
- Restore personal modifiers and purchased one-shots when relaunch skips course
  selection.

## Menus and touch UI

Menu labels, row counts, values, and help indices are parallel data. Change them
together.

- `enhancementSettings[]` owns both presets.
- Engaged values match fresh-install defaults.
- Sidekick Autofire is per-save and stays outside presets.
- Touch button settings are controls and stay outside presets.
- Apply table-backed settings immediately through `JE_applyItemDataSettings`.

`SMALL_FONT_SHAPES` has blank glyphs for `(`, `)`, `+`, `*`, `=`, `]`, `{`, and
`}`. Tilde changes brightness. Measure rendered width instead of character count.

Useful layout limits:

| Item | Width |
| --- | ---: |
| Row name beside value | 135 px |
| Value | 95 px |
| Help from x=45 | 275 px |

Seven rows fit above a classic help line. Debug headings are not selectable.
Boss and enemy bars use playfield coordinates.

`configure_customize_menu` rebuilds the outpost's Customize page each visit;
`customizeKind[]` maps its rows to actions, so rows can differ by mode. Its
pickers return to Customize, and Esc from Customize returns to the active
outpost. Shelves stocked during a visit re-sort through `sort_shop_stock`, which
keeps None last.

Press edges (`newkey`, `newmouse`) survive until `service_SDL_events(true)` or
an explicit clear. Keyboard repeat can re-arm `newkey`.

Screens that require a fresh press must call `wait_noinput(true, true, true)`,
then clear both edges. This applies to the credits, episode banner, high-score
screens, and Endless milestone warning.

### Touch requests

Touch layouts expire. Reassert a layout while its screen needs it, clear it on a
no-fade exit, and let transition fades carry it out otherwise.

- Request buttons before a blocking fade.
- Any-key waits accept Confirm and every visible navigation button.
- Startup logos consume the accepted press but keep their buttons through the
  fade.
- Idle screens re-present the last frame when the layout signature changes.
- Include every optional-button setting in `desired_signature()`.
- Opacity zero removes buttons from drawing and hit testing.

Queue touch keys until `push_joysticks_as_keyboard()`. Injecting them inside the
event pump loses keys on screens that pump twice. Drop stale queued keys after a
gap so a fade cannot feed the next screen.

Relative mouse mode is active only in a level. A finger there sets
`mouse_pressed[0]`; post-death waits must read that latch because
`JE_playerMovement` has already returned.

Copy 8-bit screen surfaces row by row. SDL palette conversion and ordinary blits
do not preserve the indexed pixels used by the software-keyboard backdrop.

## Networking

Both peers simulate both ships. Send every simulation-affecting choice before
either peer resumes. Rendering, audio, and local controls stay local.

Local choices must not reach the simulation:

- A skipped draw still advances simulation state. When the detail level skips
  background layer 2, `skip_background_2` still paces and scrolls it, because
  enemies ride that layer.
- Extra Parallax, Christmas Mode, and the reworks do change the simulation, so
  the settings block carries them; the reworks ride one bit each in the word at
  byte 8. Christmas travels as the host's effective state; the
  joiner swaps its shape tables and voices with `xmas_set` and restores its own
  on leave. Centered shot hitboxes read those sprites.
- Debug key codes that change the simulation are ignored online.
- With campaign mods on, `PACKET_CONNECT` carries the host's Endless debug
  block. Both peers set their Endless scaling pins aside for the session.

`network_is_host` identifies the settings authority.
`networkHostPlayerNum` identifies the ship flown by the host machine.

### Modules

| File | Owns |
| --- | --- |
| `network.c` | Reliable UDP, discovery, connection, settings, shop and run sync, barriers, diagnostics |
| `net_transfer.c` | Chunked content: custom weapons, extra ships, custom levels and episodes |
| `net_internal.h` | The `NCW_*` chunk header and the hooks those two files share |
| `net_start.c` | Session start: game-type setup, ship picks, resume |
| `net_lobby.c` | Host and join screens |
| `net_rollback.c` | Rollback driver |
| `rollback.c`, `rollback_state.c` | State registry and snapshot ring |
| `net_savexfer.c` | Save and custom-data transfer screens |
| `net_style.c` | Per-seat ship appearance |

The Endless run transfer in `network.c` uses the same chunk header.

### Wire compatibility

`NET_VERSION` in `network.c` is the compatibility gate. Bump it for any change to
a packet, deterministic rule, field meaning, offset, or registered simulation
layout. Readers validate lengths and clamp received enums before indexing.
Since version 106 `PACKET_CONNECT` also carries an FNV-1a fingerprint of
`OPENTYRIAN_VERSION`, so two releases never connect even when a release forgets
the bump.

Version 87 introduced a 72-byte custom-episode identity in `PACKET_CONNECT`.
Version 88 extended it to 76 bytes: a 64-byte file name, 32-bit byte length,
32-bit FNV-1a hash, Custom Endless mode, and three reserved bytes. Sessions with
neither feature zero the block.

Version 88 also added offer and failure meanings to `PACKET_CUSTOM_LEVEL`. Its
resume flag in `PACKET_DETAILS` now announces a required-set exchange instead
of the host's local collection.

Keep persistent and wire enums append-only. Version history belongs in Git; this
file records only current constraints.

### Reliable UDP

- A receive error does not prove that the peer left.
- `network_is_sync()` means the outbound reliable queue is empty.
- Retry from the queue head, oldest packet first.
- Apply backpressure when the outbound window is full.
- Trust acknowledgements only for packets still outstanding.
- Drop packets beyond the receive window without acknowledging them.
- Re-acknowledge packets behind the window.
- Leave packets queued for the state machine that owns them.

Chunked transfers keep no more than half of `NET_PACKET_QUEUE` outstanding.
Transport acknowledgement proves delivery; a completed transfer still needs an
application acknowledgement.

The peer timeout is 16 seconds. Long online screens must call `network_check()`
or `NETWORK_KEEP_ALIVE()`.

### Session and outpost

Each player owns their shop state. `PACKET_SHOP_SYNC` mirrors cash, loadout,
the stashed standard loadout, route, and mode after a committed purchase. Modes
without a shared shop send a Customize change with `PACKET_SEAT_LOADOUT`: the
sender's items and stash. A copy of this machine's own seat is ignored.

A shared outpost has two departure states:

1. DONE allows withdrawal.
2. LOCK closes withdrawal after both peers have seen DONE.

Modes without a shared outpost use `PACKET_DEPART_GATE` for the retractable gate
and `PACKET_WAITING` for final commit. Adopt the host's level only after the
local player leaves shopping.

Content publication can receive `PACKET_WAITING` before its transfer ack. Carry
that readiness into the level barrier, but leave a second marker queued for the
next boundary.

Co-op and Separate Arcade publish custom ships, and any custom weapon they
reference, at session start and again before each level. Customize > Custom
Ship publishes on demand and offers slots only while `extraShipsNetPeerHolds()`.

A custom weapon the peer never acknowledges is dropped on both machines until
the next publish; the sender withholds its own copy with
`customWeaponWithholdLocal`. An unacknowledged ship file falls back to the last
acknowledged copy (`extraShipsNetRevert`), or is withdrawn when there is none.

In online Endless, seed, depth, course, and difficulty are shared. Wallets,
equipment, shop stock, perks, revive, personal modifiers, and outpost RNG belong
to a player. Fold the selected course only after both purchase sets are known.

### Online appearance

Ship dye and view settings are local presentation choices, published per seat.
Dye affects the hull, and the sidekicks unless that seat turned Color Sidekicks
off; never shots. A kill-fire tint takes priority on both. Offline, only the
solo ship wears its dye; a local two-player game stays undyed.

`PACKET_PLAYER_LOOK` is 11 bytes: sender, dye, the three view fields, skin, and
the Color Sidekicks byte.

Opacity is not rollback state. Shield-hit effects follow hull opacity, while
health bars and other view choices remain local. Linked Arcade has no partner
health bars.

Ship skins travel in the same look packet. See [Ship skins](#ship-skins).

### Rollback

`PACKET_INPUT` carries a fixed header and up to sixteen redundant records. It is
unacknowledged, idempotent, and stamped with a level epoch.

The menu request travels with the input record. Open on the earliest verified
request frame, with host priority on a tie. Rewind to that frame and begin a new
epoch after the menu closes.

Every wait that stops frame production must still service menu requests and peer
records. Level-end confirmation waits for the peer through the same frame.

Deterministic simulation rules:

- Demo record and playback share seed and initial state.
- Store multiple `mt_rand()` results before combining them.
- Use `sim_sinf` and `sim_cosf` in simulation.
- Build with signed `char` semantics.
- Use precise floating point and disable contraction.
- Do not use mutable function-local statics in rollback simulation.
- Register pointer relocations needed by cross-process export.
- Change registered state only before `nrb_driver`. Code after it runs once per
  presented frame (see `boss_bar_tick`).
- Drawing never writes simulation state.

Presentation state stays outside snapshots. Silent replay repaints dirty HUD
state and suppresses live-only sound and flashes.

Main-game recovery compares registry size and
`rollback_layout_fingerprint()` before adopting host state. The joiner
acknowledges only after a complete generation has been adopted. Limit recovery
to three attempts per level.

Entries registered with `rollback_register_local` roll back with each snapshot
but are never adopted from the host; use it for presentation and local settings.
Restore fixups rebuild state that lives outside the registry:
`player_bind_lives` and `enemy_sprite_sheets_sync`. `player_lives_counter` is
the only lives binding, and wire relocation uses it too. Each seat's extra-ship
sprite bank has its own relocation code.

The desync canary and its detail report share `network_sim_player_row` and
`network_sim_enemy_row`. New canary fields go into those rows so both stay in
step.

Destruct recovery snapshots units, terrain, shots, explosions, RNG, and
`destructTempScreen`. Re-pin its three live pointers after restore and start a
new epoch each round.

### Online saves

Online saves use slots 12 through 22. Slot 22 is the read-only `LAST LEVEL`
backup. `save_record_pack` and `save_record_unpack` define the 196-byte wire
record. It was 97 bytes through `NET_VERSION` 86. Version 87 appended the
64-byte custom-episode file name. Version 94 replaced the packed 32-bit
dual-ship tag with five bytes: the session kind, two weapon powers, and two
weapon modes. Version 97 appended one ship-skin byte per seat. Version 102
appended each seat's stashed standard loadout (12 item bytes), its lent-part
mask, its two weapon powers, and the Color Sidekicks bytes.

A save writes only to the local machine. The peer acknowledgement supplies the
other outpost half. Preserve player numbers on resume and restore both co-op mode
flags after `JE_loadGameRecord`.

Disconnect recovery reloads the `LAST LEVEL` checkpoint before opening the save
picker. Preserve the live co-op flags across that load or the new slot drops the
departed player's state.

Custom resumes use these rules:

- Start with `custom_collection`, then add `custom_episode` if needed.
- For older Endless saves without a collection, use every host container.
- Pull missing files from either peer. Fail if neither has a copy.
- Install the resulting list as the session pool only for Endless.

Record co-op Campaign only when its starting episode is completed for the first
time. Later episodes, repeats, and deaths do not replace it.

### Data transfer

`net_savexfer.c` uses blocking UDP port 1332 outside live game sessions.

- Single saves and bulk transfers use separate packet families.
- A single save carries its page, online seat, Endless record, and custom
  dependencies; the receiver chooses the slot and name.
- All Saves replaces slots and Endless runs but keeps high scores and custom
  content.
- Custom Levels adds `.clv` files and replaces differing copies with the same
  name. Exact byte matches are left untouched.
- Custom Data replaces ships and weapons. It also adds custom levels when the
  sender has any.
- Transfer All replaces saves, scores, seats, ships, and weapons as one
  transaction, then adds any custom levels.
- Roll back replaced saves, ships, and weapons when their envelope fails. A
  later container-part failure does not undo an accepted envelope or earlier
  container additions.
- Large payloads pause every 16 chunks to poll acknowledgements and cancellation.

The single-save `OTSV` payload uses version 8. After its fixed header and
196-byte save record come the Endless bytes, a 16-bit dependency length, and
the colon-separated dependency string. Version 5 carried a 164-byte record,
version 4 a 162-byte record, and version 3 a 161-byte record; version 2 ended
after the Endless bytes.

Custom-level transfer negotiates transfer-kind version 7 and uses `OTCL`
version 2. Multi-byte envelope fields use network byte order:

| Offset | Field |
| ---: | --- |
| 0 | Four-byte `OTCL` magic |
| 4 | 16-bit version, currently 2 |
| 6 | 16-bit record count; the sender writes 1 |
| 8 | 16-bit zero-based part index |
| 10 | 16-bit total part count |
| 12 | Container record |

A record is a 64-byte padded name, 32-bit length, and the container bytes. A
collection has at most 64 parts. Each part carries one container of up to 16
MiB, so there is no longer a 12 MiB limit on the complete collection.

Custom Data version 2 has no container parts. Version 4 stores the number of
following `OTCL` parts in `CX_LEVELS_LEN`. Version 3 used the same field for the
length of embedded `OTCL` data and is rejected.

Transfer All version 2 contains Custom Data version 2. Transfer All version 3
contains Custom Data version 4, and the outer and nested versions must agree.
Custom Levels begins with part 0. Custom Data and Transfer All send every part
after their envelope.

Pulled and direct-push transfers use the same multipart sender. iOS has no
multicast entitlement, so direct push must work without discovery.

Port 1332 is used for discovery and passive receive. An uploader falls back to
an ephemeral port when 1332 is busy and can still send to a typed address. Its
reply advertises the port it actually bound. A passive receiver cannot start if
port 1332 is unavailable.

On platforms with `getifaddrs`, advertise only active broadcast-capable IPv4
interfaces that are neither loopback nor point-to-point.

### Switch local wireless

`switch_ldn.c` creates or joins an LDN network and the ordinary UDP session then
runs over it. The network advertises a discovery reply body
(`network_discover_reply_write`), so version filtering matches LAN discovery.
`network_shutdown` leaves the network first, which ties its lifetime to the
session on every exit path, including a failed `network_init`. LDN takes its
network id from the launching title's NACP, so both consoles must launch the
homebrew the same way. The network seats two nodes.

## Ships, weapons, and item data

### Extra ships

The stock `newsh$.shp` and writable `custom_ships.shp` contain a Sprite2 blob,
ten encrypted 15-byte ship records, and four plaintext checksums. The cipher
and cell codec must round-trip the stock file exactly.

`User.shp` is the DOS editor source: 304 presence bytes, one 12x14 cell for each
set byte, then the encrypted ship table. Match the name case-insensitively.

Search the writable state directory first, then the active data directory, the
executable's `data` directory, and the executable directory.

Graphic IDs are persistent:

- 1 through 7 are original built-in hulls.
- 8 through 15 are custom banks.
- Extended built-ins follow in raw graphic order.
- Raw values above 500 use the Tyrian 2000 sheet.
- Values 0 and 1 are the two-piece Dragonwing and Nort Ship sentinels.

Only repair known stock sprite defects when their exact pixel patterns match.
Replacement shape packs must remain untouched.

Online Campaign, Endless, and Separate Arcade exchange one ship file per seat.
Use `extraShipsFor(seat)` for simulation lookups.

Weapon byte 255 refers to the custom weapon owned by the same seat. Resolve it at
equip time because reserved ports differ by player.

`extraShipEquip` stashes the standard loadout, weapon power included, once per
seat; later custom ships keep that stash. The stash carries a lent mask over the
`ITEM_PART_*` parts:

- The first equip lends every part; later equips lend the parts they change,
  and a gun they change takes the stash's power.
- A committed shop purchase that changes a part clears its bit
  (`extraShipPartBought`).
- The shop prices an owned lent part at zero both ways, and on a lent gun the
  levels below the stash's power (`shop_first_paid_power`). Anything else lets
  a player sell free parts, revert, and keep the cash.
- `extraShipRestoreStandard` puts the stash back exactly, except sidekick
  series, sidekick level, and Super Arcade mode. `JE_getShipInfo` calls it when
  a seat's ship file is unavailable, and falls back to ship 1 without a stash.

Slots save the stash, mask, and powers per seat (`pN_standard*`,
`pN_standard_lent`). A save without stash powers treats the powers flown at the
time as borrowed (`save_record_restore_stash`).

Bank images are `custom-bank-<n>.png` in the user directory: 120x28, the five
poses left to right, index 0 transparent. The name uses hyphens because the tiny
font has no underscore glyph and the editor prints it. Files go through
`dir_fopen` like every other user file.

`src/video/image_png.c` reads and writes PNG without a library. The writer emits
8-bit indexed images in stored deflate blocks. The reader accepts every PNG
colour type, bit depth, and Adam7 interlacing. It checks chunk CRCs and the
zlib Adler-32, skips unknown ancillary chunks, rejects unknown critical ones,
and refuses headers over 16384 pixels a side or 4M pixels before allocating.

- In palette 0, 78 of the 255 inks share their colour with another ink, and
  banks 1 and 11, and 10 and 14, are identical. The export palette
  (`seImagePalette`) moves each shared ink one step to a colour no palette entry
  uses, so every index survives an editor that rebuilds the palette or saves
  full colour.
- Import, per pixel (`seImportPixel`): alpha below half or exact black is index
  0. An indexed pixel whose entry equals `palettes[0]` at that index keeps it.
  An export-palette colour maps to its index. Anything else takes the nearest
  `palettes[0]` colour (`palette_nearest`, `palette_distance`).
- When several inks share that nearest colour, `seResolveInk` takes the bank's
  current pixel, then the bank most used by the resolved neighbours, then by the
  whole image, then the lowest index.
- `tools/png_fixtures/gen_fixtures.py` writes `src/qa/qa_png_fixtures.h` and
  checks each fixture against Pillow. Pillow misreads `tRNS` keys on low-depth
  grey and 16-bit RGB images, so the generator compares only RGB for keyed ones.
- An import pushes one undo entry only after the file decodes.

### Ship skins

`ship_skin.c` owns one skin byte per seat in the Ship Editor graphic-ID space;
0 is Default. Skins are presentation only:

- Draw through `shipSkinApply`. Never write `shipGr` or `shipGrPtr`; they are
  rollback state, and their two-piece sentinels drive sidekick placement and
  Nort Ship sparks.
- Two-piece hulls neither wear skins nor serve as skins.
- Availability follows `extraShipsAllowedInGame`. A custom bank also needs the
  seat's ship file and some artwork, so a cleared bank draws the real hull.
- Store the received byte raw and validate it at use, because item data can load
  after a save or packet arrives. `JE_initPlayerData` resets both seats.
- The shop's ship list shows the owned ship's skin and "(Skin)"; Enter on that
  row goes to Done. Customize > Ship Skin is the outpost picker.

Slots write `p1_ship_skin` and `p2_ship_skin` only when nonzero. The packed save
record carries both seats' bytes; each `PACKET_PLAYER_LOOK` carries only the
sender's.

### Custom weapons

Custom weapons are always enabled. Saving the configuration removes the old
`custom_weapon_enabled` key.

Reserve one port, sidekick, and scratch range per player. Claim only placeholder
slots and materialize again after every item-data reload.

The wire decoder validates a complete design before committing it. Fixed-width
padding must be zero so identical designs produce identical bytes.

Older weapon-library rows may use a smaller bullet-array width. Derive the width
from the serialized row before reading its trailing scalars.

A synthesized custom sidekick fires consecutive mode-0 scratch weapons as its
charge stages. Clamp its 1-based sprite reads, including 2x2 offsets.

### Item tables

Apply episode differences after loading item data and keep the operation
idempotent. Settings that affect a gun usually apply to all eleven weapon
records in its port.

Projectile graphics above 1000 encode a superspark palette bank and base sprite.
`player_shot_fire_port` fires a gun level for both the game and the shop
preview. A temporary `poweruse == 0` removes the cost of its Zica Lv11 buff
beam, which fires only when the primary shot succeeds.

The Zica Lv11 pattern is baked at item apply: Auto restores the episode's own,
and Endless plays Auto as the Ep 4 spread. Endless re-applies item data at every
zone and outpost, so leaving a run restores the campaign pattern.

The Dragonwing is synthesized as ship 19 after each item load. Only Endless
shops list it. IDs 20 through 90 remain invalid normal ship rows.

Shop weapon tags follow these rules:

- `Dual-Mode` appears only in the rear list for a two-mode port.
- Endless mixed-bay lists may add `Front` or `Rear`.
- Port 16 is sidekick data and has no bay.
- Campaign lists do not show mixed-bay tags.

### Twiddles and specials

`shipCombos` is indexed by ship ID; SuperTyrian uses `shipCombosB`. All input
paths resolve through `SF_twiddleTarget`.

- One axis must exceed twice the other to count as a direction.
- Shallower diagonals are neutral and ride `RB_MOVE_DIAG` online.
- Apply the Topsy Turvy mirror inside `SF_twiddleTarget`.
- Wrong input cancels a combo. A held accepted direction and code 9 are exempt.
- Gate a fired twiddle on the ship's `twiddleWait`.
- Charge shield or armour only when the complete cost is available.
- Clear all special and twiddle clocks at level start.

Flare specials may own the full-screen grade only while the level grade is idle.
Track ownership in rollback state and clear only a grade the flare owns.

Soul of Zinglon and MineField share duration and ramp state. A refire refreshes
duration without resetting a live ramp. Overlapping beams do not stack; the
stronger hit wins and supplies perk context and kill credit.

## Audio, platforms, and logs

MIDI backends convert LDS through vendored midiproc. At loop start, replay
pre-loop program, controller, pitch, and SysEx state at time zero. Never replay
notes.

LDS detune uses sixteenths of a semitone. Normalize equivalent patch groups
around zero, sound the nearest MIDI key, and put the remainder on the pitch
wheel. Do not add `arp_tab[0]` on the MIDI path.

Release active notes at song and loop boundaries. Events scheduled beyond the
boundary never run on repeat.

The Setup > Sound filter and same-sound limit act at playback, after the
simulation has queued its sounds. Enemy fire picks its `soundQueue` slot with
`mt_rand()`, so a limit applied at queue time would shift the RNG stream. The
filter runs on each output channel's summed effects in the mixer's Q20.12 units,
leaves the output bit-identical when Off, and is redesigned whenever the device
rate changes.

The device is stereo. Sound buffers in `soundSamples` interleave two channels,
and `soundSampleCount` counts `Sint16` values; the loader duplicates the mono
originals. OPL music renders mono and is copied to both channels.

Setup > Sound > Custom Sounds is presentation only. No simulation or wire state
reads it, and peers may hear different files. `custom_audio.c` finds and decodes
the files; `audio.c` and `sfx.c` decide when they play.

- Lookup scans each folder with `dir_each_file`, the one folder iterator in the
  code. A two-digit number ranks first, then any number, then the song or sound
  name compared without case or punctuation; extension order breaks ties.
- The main thread reads a whole file before it hands the track to the callback,
  so the audio thread never touches the file system. Tracks swap under the
  device lock and close after it is released.
- A replacement song loops or plays once as its LDS original does.
  `lds_plays_once` runs the real player silently from the top, restores the
  loaded tempo, and rewinds. Callers set `music_stopped` and stop any MIDI song
  first. `fm_stop` and `wnm_stop` end a MIDI song without the finish hook that
  a paused one could still fire.
- Every facade call checks the custom track before its MIDI branch, so a MIDI
  song never resumes under a custom one.
- `fade_song` ramps 255 Loudness updates, the length of `lds_fade(1)`, then ends
  the track as the MIDI fade does, with `playing` false.
- The decoders in `stuff/` compile only through `custom_audio_codecs.c`, which
  marks them as system headers.

`audio_qa_begin` drives the real callback without a device for the unit suite.
It refuses to start while a device is open, so the suite must keep `--no-sound`.

Rumble is presentation. `JE_playerDamage` pulses only when `!rollback_resim`,
keys hull hits off `cmHullHit` so the infinite-armor cheat still rumbles, and
routes through the ship's input device the way `JE_playerMovement` reads it
(`seat_rumble_device`). Demo playback and the remote seat of an online game
never rumble.

Explosions call `explosion_rumble_add` as they spawn: every
`JE_setupExplosionLarge` as a heavy blast, plus the small enemy deaths and
sequence pops that call `JE_setupExplosion` directly as light ones. Each ship's
share falls off with its distance from the blast. `explosion_rumble_tick`, after
the sound queue, fades each ship's heavy and light levels and holds the result
on its pads with `joystick_rumble_level`; a resim or demo tick only drops its
blasts. Destruct fades its blasts the same way at its 70 Hz tick.

Each pad mixes two voices, a pulse (`joystick_rumble`) and a held level
(`joystick_rumble_level`), and runs each motor at the louder of the two. The
game ends every pulse itself in `joystick_service`, because SDL ends a timed one
only inside `SDL_JoystickUpdate`. SDL drops a request equal to its last one,
never resends a stop, and its HIDAPI rumble thread ignores a failed write, so a
single lost stop report can leave a Bluetooth DualSense running at its last
level. Every stop is therefore repeated twice over 750 ms, each preceded by a
(1, 1) nudge so SDL cannot drop it, and `joystick_rumble_stop_all` runs on
pause, the in-game menu, the shop and focus loss.

Controllers stay on the raw joystick API with joystick events ignored, so
nothing else runs SDL's device detection. `joystick_service` (from
`service_SDL_events`) rescans once a second. Slots never move or shrink: an
unplugged pad keeps its slot, index and bindings, its live input is cleared, and
a device with the same GUID takes it back. Switch and Vita keep their startup
list.

`reset_joystick_assignments` takes defaults from SDL's controller database when
it knows the pad; `SDL_INIT_GAMECONTROLLER` exists only for that. SDL2 binds
give no axis half, so stick and d-pad directions read up and left as negative.
The PS4/PS5 rumble hints are set before any pad opens because those drivers
report rumble capability only in extended mode, and the Rumble row is shown by
`joystick_can_rumble`. SDL claims rumble for every Vita pad, so there any paired
controller counts (`vita_pad_is_paired`) and the console's own controls never
do. Android needs the `VIBRATE` permission for gamepad
motors, and the mobile accelerometers are kept out of the joystick list.

Logs are created lazily under `log/`. Network reports keep unsymbolized RVAs to
avoid blocking the game loop; crash and hang reports may symbolize. Console log
cleanup removes only recognized OpenTyrian names.

A fresh profile has no user directory. Code that writes a file opens it under
`make_user_directory()`, which creates the directory. Readers and scans use
`get_user_directory()` and must not create it.

Switch must keep its SDL window resizable for dock changes and save controller
changes immediately. Its quit leaves through `_Exit`, because newlib's stdio
teardown in `exit` crashes once romfs is unmounted, so `JE_tyrianHalt` calls
`switch_platform_exit` itself. An NRO that skips `socketExit` leaves its socket
session in hbloader's process, and bsdsocket aborts with 2162-0002 when HOME
closes that process. Vita boots with the None scaler, resolves Sub-pixel Auto to
the scaler factor without the desktop's 2x minimum, keeps presenting during IME
use, and uses front touch only.

switch-sdl2 lists eight players whether or not they are connected and names
them all "Switch Controller". The game opens two, and slot 1 saves its bindings
as "Switch Controller 2" so it cannot overwrite slot 0's. Its rumble drives one
motor per controller, none in handheld mode and at a fixed strength, so
`switch_rumble` drives the motors through libnx and `joystick_service` stops
each pulse at its deadline.

switch-sdl2 reports nothing for a single Joy-Con's L/R or ZL/ZR, so
`switch_slot_joycon_extra` reads them through libnx as buttons 28 and 29. Both
sides share them, like the rest of switch-sdl2's single Joy-Con layout. Change
fire binds 29 by default, and older configs get it in an empty second slot. In
a local 2-player game a full controller takes that layout
(`joystick_2p_joycon_layout`): 8 and 9, a single Joy-Con's SL and SR, read its L
and R, and 29 reads its ZL and ZR. Joystick Setup records ZL or ZR there as 29.

switch-sdl2 also opens its own controller screen when a controller's style
changes, so after `switch_controller_setup` the game reopens its joysticks; the
driver then reads the new styles as its starting state.

## Level and data formats

Tyrian levels are event scripts. Reseeding `mt_rand` changes runtime effects but
does not reorder authored pickups.

- The event clock advances with vertical scroll.
- A map stop resumes when the screen clears, link 254 jumps, or `forceEvents`
  advances the clock.
- Cull only a stop holder with no reachable linked member after
  `MAP_STOP_STALL_LIMIT`.
- Test the HARVEST entrance after watchdog changes.
- Dormant dispenser bases are enemy IDs 80 through 83; piece 80 fires on frame 9.
- `level_read_event_script` corrects shipped scripts as they load: it backdates
  Episode 5 SAVARA's cannon groups and returns SURFACE's arch nubs (enemy 212),
  which Tyrian 2000 moved 194 px below their arches, to the Tyrian 2.1 spot.
- `qa_test_level_event_scripts` fails any spawn piece over 100 px from its
  group's median height, except three authored designs kept since Tyrian 2.1.

`tools/dump/dump_data.py` mirrors the loaders and writes the tracked `dumps/`
trees. Every decoder needs a verification check that accounts for its source
bytes or matches the engine arithmetic it copies.

Format traps:

- `user1.shp` and `user2.shp` start with a two-byte header.
- Shipped `shapes?.dat` files contain 520 bytes after tile 600.
- Episodes 1 through 3, 4, and 5 use different item and enemy tables.
- `tyrian.hdt` text groups are position-dependent.
- Tyrian 1.1 compiled sprite frames end at the next offset; later releases also
  use a `0x0f` terminator.
- Tyrian 1.1 orders the seven `tyrian.shp` banks differently.
- The enemy shapebank table only grows; no existing slot was reassigned.
- Re-encode decrypted text as CP437 to recover the original bytes.

Use each dump tree's `index.csv` to find the decoder, engine loader, references,
and output for a data file.

### Custom-episode containers

#### File format

Tyrian 2000 Atlas writes one little-endian `.clv` file per custom episode. The
160-byte `CLV1` header is fixed:

| Offset | Field |
| ---: | --- |
| 0 | Four-byte `CLV1` magic |
| 4 | 64-byte NUL-terminated title |
| 68 | 64-byte NUL-terminated author |
| 132 | One-byte base episode, 1 through 5 |
| 133 | Three reserved bytes |
| 136 | Level offset and length, two 32-bit values |
| 144 | Script offset and length, two 32-bit values |
| 152 | Datacube offset and length, two 32-bit values |
| 160 | Section data |

The level section uses the episode 4/5 layout: its item and enemy tables follow
the last level. The script matches `levelsN.dat`; the datacube section matches
`cubetxtN.dat` and may be empty. A blank title falls back to the file name.

Loader limits:

- File size: 160 bytes through 16 MiB.
- Level and script sections: non-empty and contained within the file.
- Opening level-offset count: odd and between 3 and 42.
- Invalid base episodes fall back to episode 1.

The base episode selects number-gated engine rules, not stock data. Activation
extracts `custom.lvl`, `custom.lev`, and `custom.cub` in `custom_levels`.

Switching between stock and custom data forces a reload even when the episode
number is unchanged. A rescan never changes the active episode.

#### Discovery and saves

At startup and before episode selection, loose `.clv` files move from the data
and user directories into `custom_levels`.

- A same-named file already there wins.
- Cross-volume moves use copy-and-delete and remove partial copies.
- The directory is created only for a write.

With no containers installed, custom-content menu rows stay hidden and a stored
`custom_endless` value has no effect. Read-only scans must not create the
directory.

Local saves store the active container in `custom_episode`. Custom Endless saves
also store their ordered container names in `custom_collection`, separated by
colons. Valid container names cannot contain a colon, and the field holds at
most 64 names in 4095 bytes.

Offline load rows lock when `custom_episode` or a collection member is missing.
Online rows stay available so the peers can reconcile the files. The advertised
identity is the file name, size, and FNV-1a hash.

#### Network sync

`PACKET_CUSTOM_LEVEL` uses the common chunk header (`NCW_*` in
`net_internal.h`). Count values have these meanings:

| Count | Meaning |
| ---: | --- |
| `0x0000` | Acknowledge a stream generation |
| `0xfffe` | Offer a named container |
| `0xffff` | Request a manifest or container |
| Other | Number of chunks in a data stream |

Payload kind 0 is a container; kind 1 is a manifest. An offer contains a
64-byte file name, 32-bit size, and 32-bit FNV-1a hash. Generation `0xffff`
acknowledges a settled required set; generation `0xfffe` reports failure.

A manifest starts with a 16-bit count, followed by up to 64 records. Each record
contains a 64-byte file name, 32-bit size, and 32-bit FNV-1a hash. New sessions
use the host's complete file-name-sorted collection. Resumes use the save's
ordered dependencies.

File reconciliation:

- Zero size and hash means the host needs the joiner's copy.
- For nonzero identities, the joiner reuses an exact match or downloads the
  host's copy.
- Local extras stay installed but do not enter the session pool.

Container requests may name a manifest entry; an empty name requests the
container advertised in `PACKET_CONNECT`. Received names must remain inside the
container directory. The receiving peer checks size and hash, then runs normal
`.clv` validation before writing the file.

After validation, an arriving container is compared byte for byte with an
installed file of the same name. An exact match keeps the existing file and
timestamp; different contents replace it.

Custom-container sync blocks on the start screen. The guest consumes
`PACKET_DETAILS` first so it cannot pin the reliable queue head.

During sync, `network_clv_pump` drains container, shop, debug, waiting, and
late-connect packets. `PACKET_QUIT` and `PACKET_GAME_QUIT` abort the transfer.

Stream chunk counts stay below the `0xfffe` offer sentinel; the 16 MiB container
ceiling fits within that range. A settled required set supersedes late queued
requests. Retry a named-container request only after no chunk has arrived for
three seconds.

## Tests

Runner details live in [testing/README.md](../testing/README.md). Persistent or
deterministic changes need coverage for:

- old save import and malformed input;
- rollback save, restore, and pointer relocation;
- two-ship replays more than one frame deep;
- a registry trace comparison for any setting meant to stay presentation-only;
- cross-process hashes;
- host setup and joiner adoption;
- both player indices and host roles;
- loss, reordering, duplication, outages, and sequence wrap.

Small mistakes with large effects:

- Use the correct sprite bank.
- `enemycycle` is one-based.
- Positional enums index shipped data. Do not remove unused-looking entries.
- Keep `config_file.c`, `opl.c`, and midiproc public contracts stable.
