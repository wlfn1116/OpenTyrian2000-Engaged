# Maintainer notes

This file holds constraints that are easy to break and hard to recover from the
code alone. Player behavior belongs in the [player guide](../GUIDE.md). Local
comments should state only the nearby rule and point here for longer context.

## Build and targets

`build-all.ps1` builds PC, Switch, and Vita targets and collects successful
outputs under `build`. Use `-FailFast` when one failed target should stop the
rest.

Target constraints:

- The PC executable runs beside `data`; `build` is an output directory.
- MIDI is available only on Windows x86-64.
- The Windows crash logger is stubbed elsewhere.
- Switch builds use devkitPro bash and an MSYS-style `DEVKITPRO` path.
- Vita builds use native CMake and Ninja. MSYS paths are invalid there.
- Console Release builds define `NDEBUG`.

Keep maintained targets warning-free. Existing warning suppressions cover
specific DOS-era code and third-party headers.

MSVC analysis is a separate build:

```text
MSBuild visualc\opentyrian.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /p:EnableMicrosoftCodeAnalysis=true
```

Inspect analyzer range warnings. Use `OT_ASSUME` only after a real bounds check.

## Rendering

### Render list

The simulation runs at 35 Hz. `render_list.c` records a tick and replays it at
the display rate.

Required invariants:

- Every moving object has a stable `rl_current_id`.
- Presentation timing uses the performance counter.
- Exact replay with `use_override == false` reproduces the recorded frame.
- Projectile velocity comes from the projectile; shot slots are reused.
- Ship-attached shots separate ship motion from their own motion.
- Conditional pieces use a separate ID when their command count can change.

The Nort ship banking trim uses `RL_ID_SHIP_TRIM_BASE` because a changing trim
count must not snap the hull.

### Display-rate ship movement

Supported modes use a real-time ship integrator. The simulation reads its
position for shots and collision, keeping the sprite and hitbox together.

Disable this path for demos, which replay fixed-tick input. Advance it on every
presentation loop, including loops that also run a simulation tick. Preserve
joystick press edges for pause and menu handling.

Network games keep the integrator for the local ship and commit its current
position into the tick that goes on the wire. Rollback presents that live
position. Delay-Based presents the simulation position, so the sprite, shots,
and hitbox agree while the ship trails input by `network_delay` ticks. A remote
ship is never integrated locally: extrapolate it from its last tick position at
a clamped velocity and ease the result, snapping only across jumps too large to
be a misprediction.

The integrator is simulation code, so a rollback session adopts the host's
Smooth Motion choice through `nrb_session_vt()`. A machine's own setting still
selects how it samples live input.

### Feedback and overlays

Ice, water, and lava use frame feedback:

- `render_gs` holds the persistent filtered background.
- `smoothie_frame` holds the current background. Entities and overlays are drawn
  over it, or over its expansion in `pf_hi` when the plasma runs at a lower
  factor than the entities.

Entities never enter the persistent buffer. Full-screen color and brightness
effects apply to both sides of the residual comparison.

Palette fades, picture wipes, Destruct, gauges, boss bars, the special meter,
and Soul of Zinglon have display-rate draw paths. Their state advances once per
simulation tick.

Gauge interpolation changes only the filled height. Keep the base on an exact
scaled row so a steady gauge cannot jitter.

Linked arcade runs the shared special path once per player each tick. The meter
keeps the final clocks and merges ready/fired edges across those two passes.

Each burn and each recharge is measured against its own opening value. A special
equipped during a recharge starts a phase, so equipping calls
`hud_special_light_rearm`; the clocks alone cannot tell a replaced cooldown from
one still counting down.

Only live ticks step the ready pop, so the machine that does not own the firing
input can have the whole shot land in re-simulated ticks and never see its edge.
The meter leaving full since the previous live tick owes the pop instead. That
reading survives any split of the ticks and cannot count one shot twice.

`JE_drawPerfOverlay` runs at the end of every present path, onto the composited
frame at that pass's factor, so no later draw covers it. Keep it out of
`game_screen`: text drawn there becomes filter input on the next tick and smears
into the feedback. The hitbox boxes stay in `game_screen`, where world positions
belong.

### Superspark ring buffer

Sparks live in one array of `MAX_SUPERPIXELS` slots and die after 15 ticks. A
shower reserves nothing: a later spawn reaching the same slot overwrites a spark
still in flight, so the spawn rate around a source sets how long its sparks last.

`last_superpixel` advances in `next_superpixel` exactly as it did when every
source wrote through it: wrapping at `SUPERPIXELS_CLASSIC` for a `classic_cap`
call and at `MAX_SUPERPIXELS` otherwise. Capped sparks are written at that
cursor, so the superspark weapon trails keep their original slots. With Extra
Sparks off every call takes this path and nothing else applies.

The per-weapon cap setting is worth what the rest of the screen takes from it. A
capped trail looks classic-short because uncapped spawns keep landing in the
classic window and overwriting it, so the trail thins as combat gets busier.
Anything that stops uncapped spawns from reaching that window, such as giving the
capped sources a window of their own, leaves the setting doing almost nothing: a
single trail needs 20 ticks to walk 101 slots and its sparks live 15.

So under Extra Sparks an uncapped spark still costs the classic window the slot
the cursor landed on, retiring whatever was there, but the spark itself is
written at `last_uncapped_superpixel` in the rest of the array. The trail thins
at the original rate while a shower of one to three sparks, such as an elite aura
or a pickup glyph, keeps its full 15 ticks.

Only the per-weapon trails may pass `classic_cap`, and only from their own
setting. Everything else spawns uncapped, the Opening Salvo trail included: a
volley that also throws uncapped launch flashes retires the classic window faster
than its capped sparks can show, leaving no trail at all. A salvo drops the cap
on a weapon's native trail for the same reason.

`JE_drawSP` sweeps the whole array, so sparks already in flight animate out
cleanly when a setting changes. Both cursors are private to `varz.c`; clear the
field through `JE_resetSP`.

A seeded spawn, `JE_doSPSeeded`, has to spread its angles across the caller's
seed stride as well as within one shower, so each draw avalanches its own point
on a fixed walk. An LCG cannot serve: its output is affine in the seed, and these
sources build a seed from `rl_present_gen` and emit every few ticks, so their
showers sit a fixed stride apart and that stride becomes a fixed angular step.
The elite aura's stride of 685 lands half a degree short of a full turn, which
freezes each body's aura into a single ray. `qa_test_superspark_seeded_spread`
samples the strides the callers in `tyrian2.c` produce and requires every
quadrant to keep a share of the showers.

The ring is presentation state and is not registered for rollback, so nothing
restores it when the simulation rewinds. Two rules keep it in step with the
presented timeline instead.

A silent re-simulation pass must not write to it. `JE_doSP` still makes its
three generator draws per spark on such a pass, because that cost belongs to the
deterministic stream, and then skips the slot; `JE_drawSP` is skipped whole.
Otherwise a rollback would stack one shower per re-simulated frame onto a single
presented frame.

A pass that has already drawn and is then thrown away has to be undone, which
is what `JE_beginSPPass` and `JE_discardSPPass` are for. The discard rewinds the
two spawn cursors, so the pass that redraws the frame writes the same slots, and
subtracts the step `JE_drawSP` took, so that pass takes it once rather than
carrying every spark an extra tick of travel and decay. Travel is exactly
invertible; `z == 0` is left alone because a spark the pass retired is retired
on that frame either way, and a spark still at `SUPERPIXEL_SPAWN_Z` landed after
the draw and never took the step. Both abandon sites call it, separately from
`rl_abort_record`, which returns early when recording is off. The residue is
bounded by the difference in spawn count between the two passes, since they
simulate the same frame from slightly different state.

Both rules act on every spark on screen at once, and a peer changing direction
mispredicts most frames, so the unit suite pins them.
`qa_test_superspark_discarded_pass` hashes the ring after a clean three-frame
run and after the same run with the third pass discarded and replayed, and
requires the two to match. `qa_test_superspark_rng_cost` pins the draw count at
three a spark, which is what a misplaced silent-pass guard moves.

### Background layers

Background commands carry integer movement and fractional phase. Layer-bound
enemies and health bars use the same phase.

- `background3x1` binds layer 3 to layer 1.
- Publish a layer phase even when its rows are not drawn.
- Keep whole-pixel correction separate from fractional phase.
- Round combined layer and local offsets once.
- Preserve layer 3's authored base step when it records after advancing.
- Use the ship's actual travel range for horizontal normalization.
- `enemy_rides_layer2` is the common layer-binding test. Pickups are excluded.

Mirrored Layers reflects columns within the same map row. Render commands carry
the reflection state so 1x and supersampled replays match.

Call `endlessScrollExtraPx` once per layer per tick. Layer-bound enemies use the
same boost and fractional carry. Large scroll steps must catch event spawns up to
the current layer phase.

### Supersampling

Values 1 through 5 are fixed factors. `0` is Auto and
`RENDER_SUPERSAMPLE_NATIVE` follows the fitted output rectangle. Vita resolves
all modes to 1x.

The supersampled frame is copied directly with nearest-neighbor sampling. The
legacy `render_supersample_filter` key is ignored and removed on the next save.

`smoothie_full_res` chooses the plasma resolution on ice, water, and lava levels.
True filters at the sub-pixel factor. False filters at 1x and expands the result
into `pf_hi`, so the feedback pass, whose cost scales with the square of the
factor, stays at native size.

The split is per command, not per level. `rl_end_record` records how far into the
tick the last filter sits; with `split` the background-head pass stops there and
a background-tail pass draws the rest at its own scale, because a background
recorded after the last filter is not filter input. The ordinary foreground pass
then draws every entity over both background passes, preserving the same
composition order as full-resolution smoothies. Requirements:

- Run the tail after expanding the head and before the foreground.
- The tick advance keeps `split` false. The filter's feedback is the previous
  full frame, so the whole background belongs in the persistent plasma.
- Only the background phases take the render-list scratch surface, so a frame
  that mixes the two factors does not resize it twice.

Foreground replay also receives the plasma's scale and interpolation phase.
For an entity bound to a filtered background layer, replay quantizes only the
shared layer transform to that phase and keeps entity-local movement at the
foreground factor. Layers in the high-resolution tail retain normal phase.

Vita cannot reduce the plasma below its forced 1x factor. With Smooth FX off it
therefore computes the current tick endpoint once, keeps it in
`smoothie_frame`, and composites display-rate foregrounds into the separate
`smoothie_present_frame`. The endpoint becomes the next persistent feedback
without another replay. Layer-bound entities use that endpoint's shared phase.

The palette conversion in `present_hi` is unrolled eight pixels per iteration. It
converts the whole supersampled frame on every present, and a one-pixel loop
stalls on the dependent index load.

## Coordinates and sprite bounds

The frame is 356x200. The playfield is 299x184 and the HUD is 57 pixels wide.

- World rendering uses `game_screen` and `PLAYFIELD_LEFT`.
- The final compositor crops the playfield to screen x=0.
- Menus use a centered 320-pixel canvas.
- HUD overlays use composited-buffer coordinates.
- `PLAYFIELD_X_SHIFT` is a background phase, not a crop offset.
- Row walks use the surface pitch.

All `Sprite2_array` blits validate their index. A two-by-two sidekick claims the
base sprite plus `+1`, `+19`, and `+20`; charge frames need validation too.

Shop icons use the same nineteen-wide grid. Specials index `spriteSheet10`, and
ships use `shipgraphic`; their `itemgraphic` fields are unrelated.

### Destruct sky window

Destruct pins two 144-pixel HUD frames to the screen edges. The gap between them
is open sky in rows `0..HUD_ROWS-1`.

`destructTempScreen` is persistent terrain and explosion glow. In the sky window,
only effects that erase themselves may write to it. Dirt, wall generation, and
collision ceilings must respect the window bounds.

Wall footprints clamp to `baseMap`; the array is followed by a pointer in
`destruct_world_s`. Rectangle and pixel writers reject negative coordinates.

The smooth shot draw uses the same on-screen head test as the tick draw. A shot
that leaves the top must stop redrawing its frozen trail.

## Endless

### Ownership

| File | Responsibility |
| --- | --- |
| `endless.c` | Run lifecycle, milestones, summary |
| `endless_rng.c` | Seeds and structural RNG |
| `endless_level.c` | Level and music selection |
| `endless_combat.c` | Scaling, tiers, combat modifiers |
| `endless_perks.c` | Perks |
| `endless_shop.c` | Outpost, prices, E-Shop, gamble |
| `endless_mods.c` | Modifier registry and text |
| `endless_course.c` | Course generation and selection |
| `endless_save.c` | Sidecar save and sortie snapshots |
| `endless.h` | Public interface |
| `endless_internal.h` | Private interface and shared tuning |

Keep tuning in its owner. Combat scaling belongs in `endless_combat.c`, prices
in `endless_shop.c`, and course weights in `endless_course.c`.

### Structural RNG

Shops, courses, and other run structure use SplitMix64 streams derived from seed
and depth. Combat timing must not change later structure.

Draw order is a compatibility interface. Use unique phase salts and append new
draw phases where possible. Retries reuse stored or reproducible music choices.

Course generation gathers levels, assigns modifiers, applies visit rules, sorts
cards, makes names unique, and caches level names. Changing that order changes
existing seeds.

`endlessRunBaseRule` selects one of three gatherers. The rules consume different
draw counts, so the rule is stored with the run and has separate records. Its
values are the record, save, and wire order. The menu and the record pages read a
separate order through `endlessBaseRuleAtMenuIndex`, so reordering what a player
sees cannot move a stored record.

Radar rerolls are folded into the phase salt. Latch Star Charts and the charting
seat at visit start so a redeal uses the same visit rules on both peers.

### Level shuffle

The two Shuffle rules draw from a bag rather than sampling. `endlessShuffleNext`
is the run's cursor into it, and `endlessShuffleSafeLevel` resolves a position:
the bagful is `position / npool`, permuted by a stream of its own, and the piece
is `position % npool`. The pool counts a section once even where the level
scripts load it twice, so a bagful is every level a chart can tell apart.

Every deal advances the cursor by what it spent, which is what makes a Radar
reroll cost the discarded hand. Every deal advances it, including the redeal a
peer runs to catch up on a reroll count: both machines then deal the same number
of hands and land on the same piece. A shuffled deal consumes no structural RNG,
so it cannot shift the draws around it.

Online, the cursor is the only chart input that accumulates rather than being
recomputed from seed, depth, reroll count, and charting seat, so it is the only
one that can drift without healing at the next visit. Each seat publishes the
position its live hand came off on the player block, only the charting seat's is
acted on, and a machine that disagrees re-anchors and deals again
(`endlessShuffleSyncHand`, which logs the repair). The hand carries the depth it
was dealt for: a machine that has not charted the visit the packet belongs to
leaves its own next deal alone. Read the hand after the reroll count, so a
re-anchored redeal runs on the perks the same packet delivered.

A refill reshuffles independently, so its opening window is corrected against the
emptying bag's closing window: the last `ENDLESS_MAX_COURSES` pieces of a bag
cannot reappear in the first `2 * ENDLESS_MAX_COURSES - 1` of the next. That is
the span two consecutive charts can reach across the seam. Swapping the offending
piece further back keeps the bagful a permutation. A pool too small to seat both
windows gets no such promise, and the gatherer's own duplicate check covers it.

### Combat

- Scale raw damage before the enemy-health accumulator.
- Decode piercing damage before scaling and re-encode it afterward.
- `enemy_has_boss_bar()` is the common boss test.
- Contact scaling affects player damage only.
- Round small percentage effects when truncation would erase them.

Piercing repeat-hit state belongs to the bullet. Charge it once per tick from the
toughest crossed hull and apply the lock on the next bullet pass.

The piercing marker encodes 0 to 5 damage points, a quantity too coarse for a
percentage to land on in whole points. `endlessPierceHitDamage` therefore spends
the run percentage in `ENDLESS_PIERCE_DMG_SCALE` units and banks the remainder in
the bullet's `pierceDmgCarry`, which is what holds a lever's full value over the
bullet's life. `endlessPiercePotencyPercent` is the class's own depth ramp and
applies before the run percentage, for the same reason.

A piercing bullet carries its own damage to every hull it crosses in a tick. The
accumulator overwrites `damage` with what one hull's `damageAccum` paid out,
which is zero on anything with an HP multiplier, so the collision loop keeps the
undivided value in `bulletDamage` and re-encodes that. Re-encoding the payout
instead would limit a bullet to one scaled hull a tick, which on a multi-part
boss is one segment.

`pierceDmgCarry` occupies existing padding in `PlayerShotDataType`. The rollback
layout fingerprint and the replay fixtures depend on that; a field that grows the
struct moves both.

Every logical death calls `enemy_logical_death`. It owns kill count, bounty
deduplication, Shockwave, Martyrdom, and Chain Reaction.

Martyrdom uses the visible bounds of the dying linked body. Off-screen anchor
pieces do not move the burst away from the body on screen.

Endless specials require a fresh press unless Autofire Special is active. Keep
that gate on `endlessMode`; campaign debug effects do not change campaign firing.

`endlessMode` controls run structure, saving, prices, and pickup substitution.
`endlessFxActive()` controls combat scaling, modifiers, perks, and tiers.

#### Endless orbiting specials

A weapon frame whose `sx` and `sy` both exceed 100 pins the shot to the ship on
both axes, and a non-zero `circlesize` then walks it around a closed loop. The
Orange Shield (weapon 749) is the only shipped special built that way. Its loop
closes every 32 ticks and reaches 32px from its centre on each axis, but its
shipped `bx` of -24 leaves that centre 2px left of the ship and 26px above it,
so the ring orbits over the ship rather than around it.

`player_shot_create` centres that class on the ship in Endless: it drops the
frame's `bx`/`by` and spawns on the loop's own centre instead, which
`shot_circle_center_offset_px` measures by walking one period of the deviation
triangle wave. The starting phase stays where the shipped `shotDev`/`shotDir`
put it, so only the ring's placement moves. Campaign firing is unchanged.

Spawn position feeds collision, so this is a deterministic rule and owns the
`NET_VERSION` 40 bump.

#### Endless enemy tiers

Two curves decide which tier a roll lands on, both piecewise linear in effective
depth and both pivoting at `ENDLESS_SPECIAL_PIVOT_DEPTH`, which is zone 100 on
Normal. Up to the pivot the special-enemy share spreads 58 points and the
champion share of those specials spreads 20, reaching 60% and 30% there. Past it
the share climbs 0.16% a depth and the champion share 0.32%, so both meet their
ceilings of 80% and 70% at zone 200. The early divisor is the pivot constant
itself, so both anchors stay exact if the pivot moves. Champions are the rarer
of the two tiers until zone 153 on Normal, where their share passes half, and
their bounty carries a premium for it.

The two rates split a run in half. Specials arrive quickly while champions stay
scarce, so the first hundred zones fill with elites, and the second hundred turn
that crowd into champions while the crowd itself grows slowly. Neither curve
reads `endlessTideLevel`, which starts much earlier and is tuned for the shots
and damage it feeds, so sharing it would tie the champion mix to a different
tuning problem.

`endlessEliteTierNow` is the one place a body's tier is decided, and it answers
on the first frame `JE_drawEnemy` processes the slot. Settling that early is the
point: a tier changes an enemy's colour, its health and its fire rate, so an
answer that arrived later would land in front of the player. A score pickup is
normal, a damageable body rolls, and the answer is cached per link group for the
level so a multi-tile hull cannot end up wearing two tiers.

An enemy the level is holding invulnerable cannot be judged from its armor
alone. Levels spawn bosses and sealed hulls at 255 armor and open them with an
"Enemy Global Damage change" event (type 25 or 47), sometimes thousands of ticks
later, while permanent scenery sits at 255 for its whole life. So
`endlessResetElites` scans the loaded event list once per level and records
which link groups such an event can still open, treating only a value of 1 to
254 as an opening. A record with no link number opens every body, linked or not.
"Enemy Global Linknum Change" (type 39) renumbers a group, so a group opened
under its new number counts as openable under its old one, followed to a
fixpoint because renumbers chain.

An invulnerable body rolls when the scan says its group can be opened, and is
normal when it cannot. A part whose link group has already rolled adopts that
tier either way, which covers armor plating bolted to a damageable core. Across
the shipped levels the scan reaches about a tenth of the invulnerable spawns.
The rest are indestructible walls and hazards, and no bounty could ever be paid
for one of those.

The scan result is derived from level data and never changes during play, so
unlike `endlessEliteLink` it is not registered for rollback.

A kill that spawns the enemy named by `enemydie` hands its tier to that spawn
rather than letting it roll one, so a second stage cannot change colour, health
or fire rate the moment the first one dies. The gate is the avail value
`JE_makeEnemy` returned: 2 is loot, which stays untiered like any other
pickup, and 0 is a body. Across the shipped tables every one of those bodies
is hostile (the Harvest bomb that rises and fires, a homing wreck, a
launcher), and every `enemydie` target with no armor is a score pickup.
Inheriting also reaches bodies the roll could never have promoted, since the
spawn site marks anything carrying a value as a `scoreitem`: killing an
elite's second stage is now its own elite kill, with the tier's health and
its bounty.

Elite and champion bodies shed an aura in the tier's own filter bank, under the
presentation-only spark rules given in "Endless special pickups". It is
staggered by enemy slot, so a linked multi-tile body emits once per part and its
density tracks the area it covers. Iced and wrecked bodies do not emit.

`endlessEliteTint` is the one source of that bank, for the body, the aura, the
health bar, the bullets, the explosion and the tier name in the bounty line.
The name reaches it through `JE_drawTextWindowSplit`, which draws an opening
string in its own bank ahead of the rest of the line; the message bar is one
bank for the whole line otherwise. The explosion carries it through
`explosionFilter`: a kill site sets it, spawns, and clears it again, so
`JE_setupExplosion` can stamp every puff without a new argument at sixty call
sites. It never survives a tick, which is why it is not registered for rollback.
`rep_explosions` keeps its own copy because a big sequence re-arms itself from
the draw loop, where the dying enemy is long gone.

Bullets take the same bank, stamped into `EnemyShotType.filter` at each of the
four spawn sites in tyrian2.c (the turret volley, both halves of the dispenser
volley, and a Martyrdom death burst). A volley's extra tide shots are struct
copies of the shot they fan off, so they inherit it. Storing the bank beats
reading the shooter at draw time: a bullet outlives the enemy that fired it.
`network_sim_pools` names the shot fields it hashes, so the byte stays out of
the peer comparison.

A tinted bullet is drawn with `blit_sprite2_filter_bright`, the plain filter
blit plus `ENDLESS_SHOT_BRIGHT` on each pixel's shade. Shot art spans the whole
ramp and the champion bank's bottom third is nearly black, so its darker pixels
would be lost at their own shade. `blit_sprite2_filter` cannot carry the lift:
it ORs its argument over the sprite's shade, which would collapse the gradient
into four steps. Its render-list kinds replay through the clipping blitter,
because extrapolation can push a fast bullet past a screen edge, and the 1x
path does not clip on x.

The colour bytes added to `Explosion`, `rep_explosion_type` and `EnemyShotType`
land in existing padding, so neither `sizeof` nor the layout fingerprint moves
and the replay fixtures still hash what they did before. They are zero outside
an Endless run.

Explosions are normally drawn with `blit_sprite2_blend`, so a tinted one takes
`blit_sprite2_blend_filter`, which recolours and blends in one pass. Two passes
would read back its own tinted pixels and halve the shade twice. Its argument
packs the bank and a shade lift, because the blend alone leaves the sprite around
shade 4 and the elite banks are near-black there.

#### Endless special pickups

`endlessSpecialPickup()` names the data cubes and secret orbs that
`endlessGrantSpecial` answers. Its two branches mirror `JE_playerCollide`'s
pickup branches, so the art cannot appear where no special is handed out. It
reads enemy state and writes none.

The art swap lives entirely in `blit_enemy`: the sheet becomes `spriteSheet10`
and the frame becomes `ENDLESS_SPECIAL_PICKUP_ICON`, tinted by a bank that
advances with `rl_present_gen`. Nothing is written to `enemy[]`, so the rollback
registry hash is untouched and a peer running without the icon still agrees.
Rewriting `egr[]` or `sprite2s` at spawn instead would move that hash and
invalidate replay fixtures for no simulation reason.

The glyph carries a cardinal outline drawn with `blit_sprite2_solid`, in shade
`ENDLESS_SPECIAL_OUTLINE_SHADE` of whichever bank the icon is currently cycling
through. `blit_sprite2_filter` cannot draw it: it keeps the sprite's own shade in
the low nibble, so a rim drawn with it comes out shaded. The glyph spans both of
the icon's cells, so all four outline passes are drawn for both cells before
either glyph cell; going cell by cell lets the right cell's leftward outline
notch the left cell's art.

The pickup also throws a shower in the icon's current bank. Superpixels are
outside the rollback registry, so it uses `JE_doSPSeeded`, which runs its own
sequence instead of the simulation RNG and therefore costs the peers no shared
draw. Emission is skipped on silent resim passes and staggered by enemy slot.
`JE_drawSP` adds a spark's `color` to the plotted shade, so that argument carries
the palette bank alone; a non-zero low nibble spills into the next bank.

Both the bank cycle and the emission cadence count with `rl_present_gen`, which
is bumped only on a pass that goes on to be presented. `rl_enemy_gen` counts
simulation passes, the generation the velocity hints in `blit_enemy` want. It is
useless as a clock: rollback runs it forward by the re-simulation depth against
one presented frame, so a cadence keyed to it jumps the cycle and fires the
shower in bursts and gaps whenever a peer's inputs mispredict.

That shower passes behind the glyph rather than over it. Draw order cannot do it:
`JE_drawSP` runs after the whole playfield, and moving the sparks ahead of the
enemy banks would bury them under the background layers drawn between. Instead
the pickup spawns them `occluded` and publishes the glyph's solid shape (the
measured ink grown by one for the outline pass) through `JE_addSPOccluder` on
every tick it draws, and `JE_drawSP` skips an occluded spark landing inside one
of the frame's boxes. Sparks already in flight therefore follow the glyph's
current position, and they show in full as soon as the pickup is taken. An elite
secret orb wears the same glyph, so its aura shower is spawned occluded too.

Brightness comes from `z`, which halves into a mid shade at spawn and fades to
the bank floor. Both endless showers therefore pass `ENDLESS_SPARK_BRIGHT` as the
`bright` lift, which `rl_superpixel_value` adds to the shade and clamps at 15 so
the lift cannot spill into the next bank. Halo taps take half of it, keeping the
falloff the un-lifted spark has. `bright` is zero for every `JE_doSP` caller, so
weapon trails and explosion sparks plot exactly the values they always did.

Only the icon's top half is drawn, at `y_offset` 0 so the glyph lands on the
centre the full 2x2 would have had. The bottom half is a ship body. The pickup
box follows the glyph's measured extent (x 2..9 and y 2..12 from the enemy
reference point) grown by `ENDLESS_SPECIAL_GLYPH_GRAB` on each side, under the
same rule vanilla uses for pickups: the ship centre must lie inside the box.
That box is a simulation change and owns the `NET_VERSION` 37 bump, so retune it
with a version bump rather than in place.

`JE_playerCollide` ends its score-item branch by firing the enemy's authored
pickup graphic through `JE_setupExplosion`. Those are the floating value labels
(`explosion_data` entries 36 and up), and a data cube's is the word "DATA", so a
converted pickup suppresses it. `endlessSpecialPickup` is sampled before the
branch body because the body clears `enemyAvail`, which the predicate reads.

Specials are the only item class whose `itemgraphic` indexes `spriteSheet10`
rather than the shop sheet. Eleven of them share three icons between them, seven
wearing the same "?", so `unusedSpecialTops` in `episodes.c` hands each its own
upper half. `draw_special_icon` builds those icons instead of blitting the 2x2:
the bare ship body of `SPECIAL_ICON_SHIP_GR`, then an unused player-shot sprite
centred on the 24x14 above it. Centring goes by `sprite2_ink_bounds`, the
sprite's painted extent, since the art rarely fills its 12px cell. Skipping the
shipped icon whole is what keeps the effect pixels in the lower half of icons
129 and 273 off the block. No item field changes, so
`unusedShopSprites` still cannot change which specials `endlessGrantSpecial` can
draw and needs no host authority.

### Modifiers and courses

`endlessModTable` owns modifier text, danger, payout, and classification. Adding
a modifier requires checking:

- persisted bit width and save migration;
- compatibility masks and course pools;
- danger, payout, and The End behavior;
- monitor rows and help text;
- visible glyphs, card width, and unique generated names.

`endlessCanonicalMods` settles the special-enemy ladder, which is No Elites over
Legion over Apex over Elite Pack, plus the champion cap that turns Legion into
Apex. It matches what `endlessEliteChancePercent` and `endlessPickTier` read, so
a weaker bit never survives to add a monitor row, danger, or payout it does not
earn. It consumes no RNG and is idempotent; generation, launch, the purchase
fold, and save restore all run it. `endlessValidateModifierTables` rejects a
naming row that is not already settled.

Course order uses cached danger and payout. Purchased buffs and Sabotage update
the chosen card without changing the original ordering key.

Milestones use the upcoming zone:

- odd multiples of 25 offer S and S+;
- other multiples of 50 offer S+ and S++;
- multiples of 100 offer one END, two S+++, and two S++.

Shop music uses a direct `play_song` index. Level music is one-based and plays
as `levelSong - 1`.

### Economy and perks

The course card shows the amount that will be paid. Sabotage and purchased
sector effects update it before launch.

All run cash passes through `endlessCashCredit`, `endlessCashDebit`, or the
`endlessShopTradeBegin` / `endlessShopTradeCommit` pair. The ledger invariant is:

```text
earned - spent == wallet
```

`endlessCleanseCharges` is the pair's shared Sabotage count. Prices remain
personal. The launch path clamps simultaneous purchases to the shared cap.

Perk UI and gameplay use the same accessors. In co-op, use `perkMine` for the
local outpost owner and `perkFx` for the ship whose effect is being calculated.

Opening Salvo tags emitted shots. Chained projectiles inherit the tag so delayed
secondary damage keeps the original volley bonus.

### Saves and records

`tyrian.sav` is fixed and checksummed. `endless.sav` stores the run and current
outpost snapshot. Hardcore keeps an in-memory sortie snapshot but writes no run
checkpoint.

`ENDLESS_SAVE_VERSION` is the format authority:

| Version | Added data |
| ---: | --- |
| 3 | Seed |
| 4 | Locked sortie |
| 5 | Buff recharge |
| 6 | Recent-level ring |
| 7 | 64-bit modifiers |
| 8 | Exact course files |
| 9 | Credits-shown flag |
| 10 | Last song and depth |
| 11 | 32-slot perk block |
| 12 | Star Charts and Breakthrough debt |
| 13 | Five stored perk offers |
| 14 | Rapid Charger migration |
| 15 | Run mode |
| 16 | Total cash earned |
| 17 | Total spent and source breakdown |
| 18 | Gear-spending sink |
| 19 | Full spending breakdown |
| 20 | Custom-weapon record mark |
| 21 | Co-op player block and course turn |
| 22 | Base Level rule |
| 23 | Radar reroll count |
| 24 | Level bag cursor and hand; the v22 rule byte widened past Same/Varied |
| 25 | Record width in the header |

Append fields and guard reads by version. Perk IDs appear in stacks and pending
offers; append enum values or migrate both arrays.

From v25 the header states how many bytes a record is, taken from the writer, so
appending a field updates it with nothing else to keep in step. A record narrower
than the running build's is padded and a wider one is trimmed, and either logs the
mismatch. Before v25 the version alone fixed the width, so a field added without
the version changing with it read every slot but the first at the wrong offset:
the misplaced `used` byte fell on a zero, `endlessLoadSlot` reported no run, and
`JE_loadGame` had already restored the campaign half alone, leaving the slot to
replay one shipped level forever. Bump the version anyway; the width detects the
mistake, it does not license it.

Records are split by mode, difficulty, crew size, and Base Level rule. The
difficulty table order is persistent. Append entries without reordering it.

The High Scores page shows all four rules on one board, as the middle level of a
mode / rule / difficulty drill-down: each list is the breakdown of the row above
it, and the row above is the deepest figure in the list it opens. Only the last
list erases. Adding a split means adding a level, not a page.

The custom-weapon mark is earned by firing during a zone. Editor and shop
previews do not count. Every zone exit closes the mark, including death and Quit
Level.

### Retries

Relaxed death and run-over screens own their music fade and finish it before
returning. Inputs must be released before wreck dismissal is armed.

Retry restores the launch snapshot. Return to Outpost uses the Quit Level path;
Restart Zone clears the visit-resume flag and reloads the same music.

`itemAvail` belongs to the outpost. Level `]I` blocks must still be read to keep
the script parser aligned, then discarded under Endless.

Shop stock IDs resolve against the episode that created them. Preserve
`endlessSortieOutpostEp` across retry before drawing the restored shop.

## Menus and UI

Menu labels, choice counts, and help indices are parallel data. Update them
together.

Generic option rows map through `menuItemIntSetting` and
`menuItemBoolSetting`. Keep explicit cases for rows with side effects, such as
scalers, synths, sliders, and submenus.

`enhancementSettings[]` in `config.c` lists every setting the Enhancements menu
edits with the value each preset writes to it. The Preset row derives Vanilla and
Engaged from that table; an enhancement setting added to the menu but not to the
table keeps reading as an untouched preset after it is changed.
`qa_test_enhancement_presets` probes one setting per menu screen and fails when
one stops reaching the table. It runs last in the suite because it leaves the
settings where it put them.

Custom is the set the player last had by hand. `enhancementNoteCustom` captures
it whenever the live values match neither preset, and the menu loop calls it each
frame rather than every row that can write a setting. It persists as one
positional list guarded by `enhancementTableShape`, a fingerprint of the table's
fixed columns; reordering or retuning the table drops the stored list instead of
restoring values into the wrong settings.

The table deliberately omits `chargeSidekickAutofire`: it is stored per save
slot, so a preset would overwrite a value that loading a game sets.

Engaged has to equal the shipped defaults in `config.c`, or a fresh install reads
as Custom before the player has touched anything. Change the two together. All
five settings Engaged currently differs from Vanilla on travel in the host's
settings block already, so retuning a preset needs no `NET_VERSION` change.

`epDiffSounds[]` in `episodes.c` holds the five items whose two data sets differ
only in firing sound. `JE_applyEpDiffs` writes from it and
`JE_epDiffFiringSound` reads it for the menu's preview, so the two cannot drift.

A gun is a port, not a weapon record: each of its eleven power levels is its own
record with its own copy of the shared fields, and the shipped data sets them
alike. Patch such a field through `weaponPort[port].op[mode][level]`, as
`JE_setPortFiringSound` and `JE_applySuperSparks` do; writing the level-1 record
alone leaves every upgraded shot on the old value while still changing the item
data enough to look applied. `qa_test_firing_sound_levels` checks every level of
the three ported sounds. Sidekicks and specials with no charge stages are single
records and can be written directly.

Settings baked into the loaded item data (the Charge-Laser slot, Zica Lv11 shape,
superspark trails, the ep1-3/ep4-5 differences, shop icons) do not take effect by
being stored: something has to rewrite the tables. `JE_applyItemDataSettings` is
that step, and the menu calls it as a row changes so the change lands in the
running game rather than at the next episode load. A firing sound is read live as
`weapons[wpNum].sound` at fire time, so once the table is rewritten the next shot
uses it. Every other Enhancements setting is read at use time and needs no apply
step.

`JE_applyChargeLaserCannon` is reversible where the rest are merely idempotent:
adding the sidekick consumes a free `options[]` slot, so `JE_loadItemDat` captures
that slot and its original record first and the toggle writes or restores it. It
refuses a slot that something else holds, which can happen when the toggle was off
at load and a custom sidekick claimed it; that case waits for the next item-data
load rather than overwriting the sidekick.

`qa_test_item_data_settings` hashes the item tables around each of these settings
and fails if flipping one no longer changes them. Prefer adding a row there to
hand-checking a new setting.

Menus draw in `normal_font` throughout. `small_font` is not interchangeable with
it at the same brightness: `blit_sprite_hv` draws palette index
`hue<<4 | (glyph intensity + value)`, and the small font's glyphs store 7 where
the normal font stores 13, so the same `value` renders it six palette steps
darker. `JE_textShade` passes brightness 4 for the help line for that reason.

A row value drawn in `normal_font` is `SMALL_FONT_SHAPES`, which has no `+`
glyph; missing glyphs are skipped without advancing, so `"Ep 4+"` drew as
`"Ep 4"` until it was spelled `"Ep 4-5"`.

At the classic pitch, seven rows fit above the help line. Measured limits:

- 135 pixels for a row name beside its value;
- 95 pixels for the value itself;
- 275 pixels for help text from x=45;
- the active font width, not character count, decides whether text fits.

Chart-a-Course has gap rows. Use `endlessCourseRerollRow` for the reroll index
and `mapPNum` for planet iteration.

Debug menus separate row IDs from display order. Headings are not selectable;
filtered views own selection, scrolling, and hit testing.

Boss and enemy bars use playfield coordinates. Enemy bars group by non-zero link
number, exclude bosses, and follow the most damaged visible member.

Two-player gauge blocks repeat every 134 pixels. `JE_dBar3` draws
`2 * units + 1` rows upward; keep the clamped gauge and ceiling mark inside the
45-row wipe.

`font_ascii[]` mapping does not prove a glyph is visible:

| Bank | Safe use |
| --- | --- |
| `TINY_FONT` | Most printable characters |
| `SMALL_FONT_SHAPES` | Letters, digits, common punctuation |
| `FONT_SHAPES` | Uppercase headings without digits |

In `SMALL_FONT_SHAPES`, `(`, `)`, `+`, `*`, `=`, `]`, `{`, and `}` are blank.
The tilde changes brightness and is not printed.

## Networking

Both machines simulate both ships. Every simulation-affecting menu change must
reach the peer before either machine resumes. Rendering, audio, and local input
settings stay local.

`network_is_host` names the settings authority. `networkHostPlayerNum` names the
ship flown by that machine. Keep these concepts separate.

### Wire compatibility

Changing a field, offset, packet meaning, or deterministic rule requires a
`NET_VERSION` bump. The current value is 45.

Recent versions:

| Version | Compatibility break |
| ---: | --- |
| 20 | Debug zone jump in departure; retractable Super Arcade ship picks |
| 21 | Online Destruct handshake and lockstep payload |
| 22 | Online Destruct pause removed |
| 23 | Co-op ENGAGE exit and TIME WAR continuation |
| 24 | Timed Battle and retractable ready barrier |
| 25 | Expert settings tail in the session settings block |
| 26 | Endless Base Level lobby field |
| 27 | Endless chart-reroll byte |
| 28 | Destruct rollback input stream |
| 29 | Solar Shield Episode Versions bits |
| 30 | Centered shot-hitbox setting |
| 31 | Trailing large-sidekick shot origin |
| 32 | Destruct recovery stream |
| 33 | Endless special press latch and centered Martyrdom origin |
| 34 | Initial debug/autofire state and Delay-Based linked movement/analog aim |
| 35 | Dedicated level-start barrier packet |
| 36 | Host player number on the resume details packet |
| 37 | Glyph-sized pickup box on Endless special pickups |
| 38 | Ship picture Episode Versions bits |
| 39 | Settled Endless special-enemy tier bits |
| 40 | Ship-centred Endless orbiting specials |
| 41 | Endless tiers settled on an enemy's first frame |
| 42 | Endless Base Level rule byte carries four rules; level-bag hand in the player block |
| 43 | Endless run transfer carries the v25 save header |
| 44 | Endless piercing damage ramp, elite/champ rebalance, remainder carry, and carried per-hull damage |
| 45 | Kinetic Converter discount on twiddle shield and armor charges |

Packet reads verify the received length before touching optional fields. Fixed
wire and save structures use fixed-width types.

### Session modes

`coopCampaignMode` and `coopEndlessMode` are the two co-op rules flags.
`coop_mode_active()` covers either. `dual_ship_mode()` also includes Separate
Arcade, Timed Battle, SuperTyrian, and Super Arcade.

Use `split_arcade_mode()` for linked-pair behavior: docking, Dragonwing rules,
split HUD, and two-player script branches.

Per-ship runtime state is loaded around each movement pass and saved immediately
afterward. Generator state, cooldowns, sidekick state, lives, and specials must
remain in the owning `Player` and in rollback state.

`player[].lives` aliases a weapon-power byte. Always bind it through
`player_lives_port()` so linked and full-ship sessions choose the correct bay.

The last death spends no life, so the counter rests at one while the ship stays
dead. `player_is_out()` is the display rule: the lives row, the arcade lives
gauge, both hull gauges, and the superbomb row draw nothing for such a ship.
Outside the arcade rules the counter is a weapon's power and is not read. The
tick that ends an explosion raises `hud_bars_dirty` so the gauges are repainted
then.

Super Arcade resolves colored balls through `player_sa_ball_weapon()`. The ship's
`items.super_arcade_mode` selects its own arsenal.

### Session settings

The host arms local session flags through `network_arm_local_session`; the joiner
adopts the same set from the settings block. Unit tests compare both paths.

The first flags word is full. Expert settings and later Episode Versions bits
live in the settings tail. Initial debug/autofire settings occupy bytes 42
through 46, with byte 47 reserved. Preserve bytes 0 through 23 and clamp
received expert and enum values.

Credits use `player_award_pickup_cash`, `player_award_kill_cash`, and
`player_award_bounty_cash`. Level-time awards name the player index and execute
on both machines.

### Keep-alives

The peer timeout is 16 seconds. Any screen that can remain open online must call
`network_check()` or `NETWORK_KEEP_ALIVE()` from its wait loop.

Helpers that service the network include:

- `wait_input` and `wait_noinput`;
- `menuWaitForInput` and `lobbyWaitForInput`;
- `JE_outTextGlow` and `JE_outCharGlow`;
- `NETWORK_KEEP_ALIVE()`.

`service_SDL_events`, `wait_delay`, `JE_showVGA`, `shopWaitFrame`, and
`menuWaitWithSmoothCursor` do not service the connection.

### Reliable UDP channel

- A receive error does not prove the link is dead.
- `network_is_sync()` means the reliable outbound queue is empty.
- The retry timer belongs to the queue head.
- A retry sends all unacknowledged packets, oldest first.
- A full outbound window applies backpressure until a slot frees or the peer
  times out.
- Trust acknowledgements only for packets still outstanding.
- A packet beyond the receive window is dropped without acknowledgement.
- A packet behind the window is re-acknowledged.
- A late `PACKET_CONNECT` ahead of the head occupies its ordered slot and is
  discarded only when it reaches the head.

Do not consume a reliable packet in a helper when the next state machine owns
it. Several shop and level barriers depend on the packet remaining at the head.

The post-load level barrier exits after receiving the peer's dedicated marker,
which proves that peer has loaded. Normal gameplay service continues retrying
the local marker if its acknowledgement is still outstanding.

Chunked transfers keep at most half of `NET_PACKET_QUEUE` outstanding. Transport
acknowledgement confirms delivery; complete transfers also use an application
acknowledgement.

### Outpost protocol

Each owner is the only writer of their shop state. `PACKET_SHOP_SYNC` mirrors the
resulting cash, loadout, route, and mode after each committed purchase.

Departure has two states:

1. DONE allows withdrawal while waiting for the peer.
2. LOCK closes withdrawal once both peers have observed DONE.

DONE and LOCK are state fields, not one-shot events. `SHOP_SYNC_HELLO` and the
rate-limited shop keep-alive restate them after a view reset.

The host's level choice is adopted only after the local player finishes shopping.
This prevents a remote commit from closing an active purchase screen.

Custom weapon designs are published during the locked rendezvous. Save requests
use a separate request and acknowledgement checkpoint.

The acknowledgement comes from the peer's own outpost pump, so a peer still on
the level end screen cannot answer. The checkpoint therefore draws the outpost
wait notice and accepts Esc, and its `NET_SHOP_SAVE_WAIT` cap remains the last
resort. Every exit writes the save; only the mirrored peer loadout can be stale.

### Endless co-op ownership

Run-wide state is derived identically from seed, depth, and difficulty. Player
state is owned by that player's machine and mirrored in the fixed-width Endless
player block.

Per-player state includes:

- wallet, loadout, bombs, Reinforce tier, and revive;
- purchased sector effects and shop tax;
- prices, perk row, and outpost RNG;
- chart reroll count and level-bag hand, both acted on for the charting seat.

`itemAvail` and the local cash ledger stay local. Structural course RNG remains
separate from each player's outpost RNG.

Perks are stored in `endlessPerkTakenBy[2][PERK_COUNT]`. Every grant uses
`endlessPerkGrant`; effects read `endlessPerkEffective` for the current ship.

The course is folded at the departure rendezvous after both players' purchases
are known. The non-charting player waits before committing and may return to the
outpost with Esc.

`endlessPlayerDowned[]` is rollback state. Reactive dangers ignore downed ships;
the outpost revives them. Relaxed both-down choices are host-authoritative.

The Endless gameplay tick is split between run-wide work and per-ship work. In
co-op, call the per-ship half for both players.

### Rollback input

`PACKET_INPUT` has a fixed header and up to sixteen redundant input records. It
is unacknowledged and idempotent. A level epoch rejects frames from other levels.

Canaries wait until the local frame is available. Pool hashes cover player and
enemy shots, explosions, repeating explosions, and the sound queue.

Menu requests schedule a future frame and wait until it is final. When both
players press Esc together, the host takes the menu and the joiner waits.

Pause request bits remain reserved in the wire layout but are ignored online.

### Determinism

- Demo recording and playback use the same fixed RNG seed and initial state.
- Store multiple `mt_rand()` results in named locals before one expression.
- Simulation paths use `sim_sinf` and `sim_cosf`.
- Linux and console builds use signed `char` semantics.
- MSVC uses precise floating point; other targets disable contraction.
- Mutable function-local statics are forbidden in rollback simulation paths.
- Registered pointers need relocation entries for cross-process export.

Snapshot bytes can contain process-specific pointers. Use the relocation walk
before cross-process hashing.

### Desync recovery

Main-game recovery sends registered host state in `PACKET_RESYNC` chunks. Chunk
zero includes registry size, compressed size, and checksum; payloads use zero-run
RLE.

Peers compare registry size and `rollback_layout_fingerprint()` during connect.
Unknown pointer homes or layout mismatches disable recovery.

The joiner acknowledges only after adopting the complete generation. Recovery
is limited to three attempts per level.

Presentation state stays outside snapshots. After a silent replay:

- dirty flags repaint sidekick and gauge HUD state;
- text backgrounds redraw before messages;
- gauge flashes and sound cues advance only on live passes;
- `SFX_CUE_CHANNEL` remains presentation-only.

### Online saves

Online saves use slots 12 through 22 of the two-player page. Slot 22 is the
read-only `LAST LEVEL` backup.

`save_record_pack` and `save_record_unpack` define the 81-byte little-endian wire
record. `tyrian.sav` keeps its fixed layout and checksum.

Dual-ship records store missing weapon powers and modes in the existing
`highScore2` field:

- `0xc74f` marks Campaign and Endless co-op.
- `0xc7a5` marks Separate Arcade, Timed Battle, Super Arcade, and SuperTyrian.

Game-type compatibility is checked before a record appears in a load menu.
Rebind lives through `player_lives_port()` after loading.

A shop save completes the bidirectional state checkpoint first. Its wait is
bounded, drains preceding shop traffic, and leaves `PACKET_GAME_QUIT` for the
quit handler.

`JE_loadGameRecord` clears the session mode flags, and the record does not say
which online lobby is flying it. Both machines therefore reassert
`coopCampaignMode` and `coopEndlessMode` from `network_game_type` after loading,
and the disconnect prompt reads `endlessMode` before the load so it can restore
the sidecar. Resuming Endless also transfers the run itself; if that transfer
fails the session halts, because a machine that drops to Campaign alone leaves
the pair in two different modes.

A resume never changes anyone's player number. The record's first loadout is
always player one, so each machine records the seat it was flying and takes it
back when it hosts the resume. The seat describes the machine rather than the
record and `tyrian.sav` has a fixed layout, so `save_slot_online_player` keeps
it in `opentyrian.cfg`.

The lobby settled `networkHostPlayerNum` on the connect packet, before anyone
knew a save was coming, so the resume details packet carries the host's seat and
the joiner adopts the other. The joiner's settings screen has already drawn the
lobby's seat on its You Fly row by then; nothing simulation-facing has read it.

`is_dragonwing` is `p == 1`, so the Linked Arcade ship is the seat rather than a
separate choice: a resume overrides the lobby's `network_host_player` row and
the saved ship follows. The stored preference is left alone for the next new
game.

### Online Destruct

Destruct uses `NETWORK_GAME_DESTRUCT`. The connect packet carries battle mode,
terrain seed, host side, netcode, and recovery setting. Sessions run at Normal
speed.

Both netcodes use one action byte and one control byte per tick. QUIT and NEWMAP
are control bits; pause remains unused.

Delay-based play uses the main `PACKET_STATE` stream. Each peer applies both
players' inputs from `network_delay` ticks earlier.

Rollback lives in `destruct_rollback.c` with its own snapshot ring. The snapshot
includes units, walls, shots, explosions, world state, RNG, and every pixel of
`destructTempScreen`; terrain pixels affect collision.

Predictions repeat held directions and fire. Unit changes and weapon cycles are
edge-triggered and are never predicted.

Round end, Quit, and New Map wait for confirmed input. Each round starts a new
epoch so late packets cannot enter the next map.

Destruct recovery uses `PACKET_DESTRUCT_RESYNC` and `DE_StateSave`. Restore
re-pins the three live pointers in the blob before use. Recovery is limited to
three attempts per round.

Every map derives from the session seed and round number. Pin all config values
that change simulation to shipped defaults before generating the map.

### ENGAGE mini-games

Online Campaign keeps two-player mode active through `]e` and `]g`. Both ships
receive the issued loadout and own their lives on the front-gun slot.

Mini-game death or quit reloads two-player `LAST LEVEL` slot 22. A cleared TIME
WAR continues under SuperTyrian rules. All entry paths, including the debug
browser, use the same branch.

The online debug skip sets `endLevel` and `levelEnd = 40`. It does not set
`reallyEndLevel` or arm an expired `levelTimer`; both peers need the wind-down to
finish and confirm the same frame.

`JE_playCredits` consumes RNG on a local schedule. Decide any shared random
result before entering credits or send the result over the network.

## Weapons and item data

Episode-specific changes apply after item loading and remain idempotent.
Projectile graphics above 1000 encode a superspark palette bank and base sprite.

Custom weapons reserve one port, sidekick, and scratch range per player index.
Ownership is by player index on both machines. `customWeaponMaterializeAll()`
rebuilds every adopted design after item data reloads.

Wire designs are chunked and acknowledge complete generations. Invalid designs
are refused before materialization.

A shot with temporary `poweruse == 0` bypasses cost and generator availability.
Extra beams fire only when the primary shot succeeds.

`JE_applyUnusedShopSprites` captures its baseline after placeholder and
Charge-Laser setup, before custom weapon slots are claimed. The operation is
idempotent and restores the baseline when disabled.

The Flying Punch (weapon 794) fires five bolts. Four are the quarters of its
fist, `spriteSheet12` 155, 157, 159 and 161 at two frames each, and the one in
`sg[0]` continues that run into 163, where the fist art ends and People
Pretzels' eight-frame spin starts. Item loading blanks that bolt's sprite, so it
draws nothing while keeping its damage and the smoke trail, which `trail == 198`
reserves for `sg[0]`. All three item sets ship 663 there, so one guarded rewrite
covers every episode and stays idempotent.

The Gencore Solar Shield icon differs between the episode item sets, and two
ships differ in `bigshipgraphic` alone: the U-Ship (28 in Ep 1-3, 32 in Ep 4-5)
and the Nort Ship (33, then 32). Episode Versions > Shop Pictures owns
`shields[8].itemgraphic` and those two fields; the unused-sprite pass does not
rewrite shields. Only 28, 32, 33, 45, and 46 have a placement entry in
`draw_ship_illustration` and the Ship Specs screen, so no other value may be
written there.

## Audio, logs, and platforms

MIDI backends convert LDS through vendored midiproc and run their own sequencer
threads. At a loop boundary, replay pre-loop program, controller, pitch, and
SysEx state at time zero. Never replay notes.

The Windows MIDI backend uses `CALLBACK_NULL` and its own thread. If a configured
SoundFont path is stale, retry its filename under `data_dir()` before discovery.

See [midiproc vendor notes](../src/midiproc/VENDORED.md) for local patches.

The Windows crash logger resumes the main thread before symbol loading and stack
walking. State dumps tolerate unloaded item tables and invalid IDs.

Network and crash logs are created lazily under `log/`. Session diagnostics are
append-only; console cleanup removes only recognized OpenTyrian log names.

Net log entries keep the full report body but never load symbols: they are written
from the live game loop, where symbolisation stalls socket service past the
dead-link timeout. Their stack frames are RVAs, decoded against the build's
`.pdb`. Crash and hang reports still symbolise.

Switch constraints:

- Keep the SDL window resizable for dock changes.
- Persist state before `_Exit` to avoid broken romfs and stdio teardown.
- Save controller changes immediately because HOME may skip shutdown.

Vita constraints:

- Present at native size and force supersampling to 1x.
- Keep presenting while the IME is open.
- Terminate the IME once on every exit path.
- Clear latched key and mouse state after raw event draining.
- Disable rear touch; use front touch for menu taps and gameplay drag.

Both ports fold the right stick into movement and disable MIDI. Switch uses
`switch-sdl2_net`; Vita supplies the required UDP subset in `vita_net.c`.

## Level scripts

Tyrian levels are event scripts. Reseeding `mt_rand` changes spawn jitter,
background choices, effects, and sounds; it does not reorder authored pickups.

The event clock advances with vertical scroll. A map stop resumes when the screen
clears, link 254 jumps, or `forceEvents` advances the clock.

The parked-enemy watchdog culls a stop holder only when it cannot enter the
reachable shot area, has no reachable linked member, and exceeds
`MAP_STOP_STALL_LIMIT`. Keep HARVEST entrance kills in runtime testing.

Some map-backed structures use blank enemy frames. A fully blank frame falls
back to its 12x14 cell; a frame with drawn pixels uses those bounds. Resolve
blankness with `sprite2_is_blank` on the active sheet.

Dormant dispenser bases are enemy IDs 80 through 83. Piece 80 fires the eye shot
and four-part bolt on frame 9. Endless activation comes from structural RNG and
becomes permanent at `ENDLESS_DISPENSER_ALWAYS_ZONE`.

## Data dump

`tools/dump/dump_data.py` decodes every file in `data/` into `dump/`. That tree
is tracked, so regenerating it after a data or reader change produces a real
diff, and it carries the same content as `data/`, which is not tracked. Its
readers mirror the loaders in `src/` and name them, so a format question can be
checked against the code that runs. Update a reader whenever its loader changes.

`dump/index.csv` has one row per data file: category, contents, the loader that
reads it, the source files that mention it, and the folders the dump wrote. Use
it to answer "where does this come from" without re-deriving offsets.

`tools/dump/verify_dump.py` is the authority on whether the dump is correct.
Each check either accounts for every byte of a source file or compares a decoder
against the engine arithmetic it mirrors, so a passing run is evidence rather
than an absence of noticed problems. Adding a reader means adding its check; an
unchecked reader is unverified however plausible its output looks. Two defects
reached the tree before it existed, both silent: a trailing empty record dropped
from four script files, and icons that never converted because the handler only
covered 4bpp. Checks 5 and 3 exist because of them.

Two files decode against their format rather than their extension. `user1.shp`
and `user2.shp` are not `Sprite_array` files: after a two-byte header they hold
uncompressed 12x14 cells, and the game never reads them. Every shipped
`shapes?.dat` carries 520 bytes past its 600th tile slot, which the game never
reads either; `tiles.json` records the count.

Three item and enemy tables ship, and they disagree. Episodes 1 to 3 read the
set in `tyrian.hdt` at the offset its first int32 names; episodes 4 and 5 each
read their own from the last offset in their `.lvl` file. `JE_applyEpDiffs`
exists because of those differences, so compare the three `gamedata/` sets before
assuming a value is global.

The `tyrian.hdt` text groups are position-dependent: one wrong count desyncs the
rest of the file. The dumper's group table is the same one `JE_loadHelpText`
uses, and it checks that the text ends exactly at the item-data offset.

Every encrypted record file dumps one record per line, each line terminated.
Joining with newlines instead loses a trailing empty record, which `levels2.dat`,
`levels4.dat`, `cubetxt1.dat` and `cubetxt2.dat` all end with. Re-encode a line
as CP437 to recover its decrypted bytes; a reader drops exactly one empty entry
from the end of a split.

## Tests

The unit, replay, sanitizer, and two-peer suites are described in
[testing/README.md](../testing/README.md). Keep scenario mechanics there instead
of copying them into source comments.

Before changing persisted or deterministic state, add coverage for:

- old save migration and malformed input;
- rollback save and restore;
- cross-process state hashes;
- host arming versus joiner adoption;
- both player indices and both host roles;
- reliable-channel loss, reordering, duplication, and sequence wrap.

## General constraints

- Use the correct sprite bank.
- `enemycycle` is one-based.
- Positional enums index shipped data. Do not remove unused-looking entries.
- Preserve upstream Doxygen comments and third-party documentation.
- Keep `config_file.c`, `opl.c`, and midiproc public contracts stable.
