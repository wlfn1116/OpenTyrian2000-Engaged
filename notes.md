# OpenTyrian2000 Engaged maintainer notes

This file records constraints that are easy to miss while changing the fork.
Player-facing behaviour belongs in [GUIDE.md](GUIDE.md). Source comments should
explain only the local reason for a non-obvious choice.

## Build and targets

`build-all.ps1` builds PC, Switch, and Vita targets and copies successful outputs
to `build`. A target failure does not stop the others unless `-FailFast` is used.

Keep these target differences in mind:

- The PC executable must run beside `data`; `build` is only an output folder.
- MIDI is enabled only for Windows x86-64.
- The Windows crash logger is stubbed on other platforms.
- Switch builds run through devkitPro bash with an MSYS-style `DEVKITPRO` path.
- Vita builds use native CMake and Ninja from PowerShell. MSYS paths break the
  native tools and some data filenames are unsafe as per-file shell arguments.
- Console Release builds define `NDEBUG`.

All maintained targets should build without warnings. The project intentionally
suppresses warnings caused by DOS-era idioms, but new warnings should be fixed at
the source.

MSVC uses `/source-charset:utf-8`. Third-party header warnings are suppressed
around the include, not across the project. GCC targets suppress
`-Wformat-truncation` because many strings intentionally target fixed-width UI
fields.

Run MSVC analysis separately:

```text
MSBuild visualc\opentyrian.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /p:EnableMicrosoftCodeAnalysis=true
```

Do not treat every analyzer range warning as proof of a bug. It often loses a
bound across an opaque helper. Use `OT_ASSUME` only after a real bounds check.

## Rendering

### Render list

The simulation remains fixed at 35 Hz. `render_list.c` records each tick and
replays it at the display rate.

Required invariants:

- Moving objects need a stable `rl_current_id`.
- Presentation timing uses the performance counter, not millisecond ticks.
- Exact replay with `use_override == false` must reproduce the recorded frame.
- Projectile velocity comes from the projectile, not from slot-to-slot position
  differences. Slots are reused.
- Ship-attached shots must separate ship motion from their own motion.

The render list snaps an ID when its command count changes. Conditional parts
should use their own ID if snapping the parent would be visible. The Nort ship's
banking trim uses `RL_ID_SHIP_TRIM_BASE` for this reason.

### Display-rate ship movement

The player ship has a real-time integrator in supported single-player games.
The 35 Hz simulation still reads the resulting position for shots and collision,
so the sprite and hitbox remain aligned.

The path is disabled for:

- demo recording;
- demo playback;
- network games.

Advance the integrator on every presentation-loop iteration, including an
iteration that also runs a simulation tick. Otherwise that frame's elapsed time
is lost. Joystick press edges consumed by the integrator must be passed back to
pause and menu handling.

### Feedback-filter levels

Ice, water, and lava effects use frame feedback. Smooth presentation keeps two
separate images:

- `render_gs`: persistent filtered background;
- `smoothie_frame`: the current background plus interpolated entities and
  overlays.

Entities must not enter the persistent buffer or they smear. Full-screen colour
and brightness effects must be applied to both sides of the residual comparison.

### Parallax and vertical scroll

Background layers record integer movement and a fractional phase. Bound enemies
and health bars must use the same layer phase.

Important details:

- `background3x1` binds layer 3 to layer 1; it is not another parallax ratio.
- Whole-pixel draw-phase correction and fractional layer phase are separate.
- Round the combined layer and local offset once. Rounding each part produces a
  one-pixel sawtooth when an enemy moves against the scroll.
- Layer 3 can be recorded after its advance; preserve its authored base step.
- Horizontal normalization uses the ship's actual travel range.

Mirrored Layers reflects out-of-range map columns within the same row. Reflection
parameters are stored in each render command so 1x and supersampled replay match.
The right margin includes an extra reflected strip because feedback filters read
past the visible playfield edge.

`endlessScrollExtraPx` serves two jobs:

1. publish the true average rate for smooth ordinary scrolling;
2. distribute Endless scroll boosts without changing the long-run distance.

Call it once per layer per tick. Layer-bound enemies use the same boost and carry.
Event spawns that occur after the scroll crosses several event coordinates need
catch-up to the layer's current phase.

### Other display-rate effects

- Palette fades use elapsed-time interpolation.
- Destruct has its own interpolated and supersampled presenter.
- HUD gauges and enhanced boss bars redraw during presentation.
- Soul of Zinglon records its pillar and follows the ship override.
- Picture wipes advance by elapsed time and keep their original duration.

### Supersampling

The playfield can render at 1x-5x. `0` means Auto. Auto follows the selected
scaler and never resolves below 2x when sub-pixel rendering is requested.

Filter enum values are persisted. Keep existing numeric values and append new
ones.

## Widescreen coordinates

The playfield and UI use different coordinate spaces:

- world rendering uses `game_screen` and `PLAYFIELD_LEFT`;
- the final compositor crops the playfield to x=0;
- menus use a centred 320-pixel virtual canvas;
- HUD overlays use composited-buffer coordinates.

`PLAYFIELD_LEFT` is the compositor crop offset. It is not derived from
`PLAYFIELD_X_SHIFT`, which is a background-tile phase.

Use a surface's pitch when stepping rows. Do not replace it with a logical width.

## Endless mode

### File ownership

| File | Responsibility |
| --- | --- |
| `endless.c` | Run state, lifecycle, milestones, run-over screen |
| `endless_rng.c` | Seed handling and structural RNG |
| `endless_level.c` | Level and music selection |
| `endless_combat.c` | Scaling, tiers, and combat modifiers |
| `endless_perks.c` | Perks |
| `endless_shop.c` | Outpost, prices, E-Shop, gamble |
| `endless_mods.c` | Modifier registry, names, ranks, help text |
| `endless_course.c` | Course generation and selection |
| `endless_save.c` | Sidecar save and sortie snapshots |
| `endless.h` | Public interface |
| `endless_internal.h` | Private interface and shared tuning constants |

Keep tuning constants near their owning system:

| Tuning | Location |
| --- | --- |
| Enemy scaling and rising tide | `endless_combat.c` |
| Perk strengths | `endless_internal.h` |
| Outpost prices | `endless_shop.c` |
| Signature-sector rarity | `endless_course.c` |
| Danger bands | `endless_mods.c` |
| Deep-run course tilt | `endless_course.c` |

### Structural RNG

Endless structure uses SplitMix64, separate from `mt_rand`. Streams are derived
from the run seed and depth. Combat timing must not alter later shops or courses.

Every phase salt must be unique, even when two consumers store separate RNG
states. Reusing a salt gives both consumers the same sequence.

Structural draw count and order are compatibility. Course generation currently
runs in this order:

1. gather levels;
2. shuffle themes;
3. deal hostile themes;
4. widen combinations;
5. deal a boon route;
6. add compatible gambits;
7. inject rare themes;
8. remove duplicates;
9. apply visit flavour;
10. ensure a readable choice;
11. apply milestone rules;
12. select gravity variants;
13. enforce tier rules;
14. sort;
15. make names unique;
16. cache base-level names.

Changing a draw changes existing seeds. Prefer appending a new draw phase. A
retry must reuse its stored or reproducible music choice.

### Scaling rules

Real depth drives progression and economy. Effective depth drives most combat
scaling and includes difficulty. Do not call effective depth a zone number.

General rules:

- Scale raw damage before the enemy-health accumulator.
- Decode piercing damage before scaling and re-encode it afterward.
- `enemy_has_boss_bar()` is the only boss test. `linknum == 0` means unlinked,
  while an unused boss-bar slot also stores zero.
- Contact scaling applies only to damage received by the player.
- A percentage increase on a small integer may need rounding; truncation can
  erase the entire effect.

Piercing projectiles need special handling because they can overlap a hull for
several ticks:

- the repeat-hit lock belongs to the bullet, not the enemy;
- ordinary enemies are never blocked by a boss lock;
- charge the lock once per tick, using the toughest crossed hull;
- bank the next lock and apply it on the following bullet pass;
- derive the lock from the same health multiplier as the target.

Every logical enemy death must call `enemy_logical_death`. It updates kill count,
bounty deduplication, Shockwave, Martyrdom, and Chain Reaction. The quiet mode
suppresses effects but still feeds deduplication state.

Elite and champion bounties are per logical enemy, not per linked sprite.

### Modifiers

`endlessModTable` is the registry for modifier text, danger weight, payout, and
classification. Avoid duplicating those facts elsewhere.

Compatibility filters must prevent modifiers that cancel the same lever. A new
modifier also needs review in:

- hostile or boon masks;
- course pools and rare injections;
- danger and payout;
- monitor rows and debug lists;
- save bit width;
- The End, if applicable.

Curated sector names must:

- be unique across all theme and generic-name pools;
- use glyphs available in `font_ascii`;
- fit the course card;
- stay within the explicit external array bounds in `endless_internal.h`.

The course-name uniqueness pass is RNG-free and runs after sorting.

Useful modifier-specific constraints:

- Martyrdom uses a fixed, symmetric sprite and fires once per logical enemy.
- Seeker state belongs to the projectile and permits one delayed correction.
- Rising-tide shots clone whole enemy volleys because `multi` entries may be
  tiles of one projectile or beam. The fan lean (`endlessFanPhaseNow`) holds one
  side for a second of zone time, then flips, so a burst reads as one sweep.
- Static must combine a power drain with a recharge lockout; generator recovery
  otherwise hides the drain.
- Retaliation refreshes one timer. It does not stack.
- Aegis Gate ignores trivial overflow and has a cooldown.
- Flak Screen reduces only rising-tide shots, never authored volleys.
- Low Profile changes damage tests, not pickup reach.
- Shockwave may clear `enemyShot[]` immediately because it does not edit enemies.
- Star Charts and Breakthrough are banked on clear and consumed later.

### Courses and milestones

Course order is based on danger rank, then the course's own payout. Do not use a
live payout that changes when the player buys a buff or Sabotage.

Whole-visit flavours roll unconditionally to preserve RNG order. Their effect is
then gated. Precedence is Jackpot, Ambush, Gauntlet.

Milestones use the real upcoming zone:

- odd multiples of 25: S/S+;
- other multiples of 50: S+/S++;
- multiples of 100: one END, two S+++, and two S++.

Milestone combination builders account for the selected level's intrinsic
danger, then verify the final displayed rank.

The End uses `ENDLESS_MOD_THEEND` as a marker. The marker supplies the name,
rank, danger label, and payout weight; mechanics come from the other bits. Keep
rank-name and rank-colour arrays the same length.

Milestone music uses different indexing for shop and level playback. `songBuy`
is passed directly to `play_song`; `levelSong` is one-based and is played as
`levelSong - 1`.

The remembered last song and depth prevent adjacent repeats and keep retries
stable. Milestone levels ignore both script fade and song-change events.

Perk scheduling is derived from cleared depth. A cadence pick and milestone pick
that coincide are paid on separate outposts. Offer widths are:

- normal, gamble, or Breakthrough: 3;
- bought perk: 4;
- milestone: 5.

### Economy and perks

The course card must show the exact payout that will be banked. Sabotage and
purchased next-sector modifiers update it immediately.

Shop inventories must be seeded from `player[0].items`, not stale menu caches.
Any capped E-Shop row should show a capped label and no price.

Perk effects should use existing player or weapon seams where possible. Keep UI
and gameplay derived from the same helper:

- sidekick magazine labels and loaded ammo use the same boosted capacity;
- the Opening Salvo gauge reads the same timer that applies its boost;
- perk offer count controls both generation and menu height.

Opening Salvo covers all emitted shots and manual special effects. The window is
time-based, begins charged each zone, and uses a separate shot tag. Attraction
velocity must remain within `Sint8`; repairs cap at the Endless hull maximum.

The perk decline bonus depends on depth, offer width, and owned stack count.
Scavenger applies to the payout.

### Save format

`tyrian.sav` is fixed and checksummed. Endless state lives in `endless.sav` and
stores both run state and the current outpost snapshot.

The checkpoint is written at outpost entry. Hardcore suppresses it. Quit Level
restores the launch snapshot.

`ENDLESS_SAVE_VERSION` is authoritative. Current history:

| Version | Change |
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
| 14 | Rapid Charger merged into Rapid Recharge |

Append fields and guard reads by version. Older records use their historical
field widths.

Perk IDs are persisted in both owned stacks and pending offers. Appending enum
members is safe; deleting or reordering one requires migration of both arrays.

The all-time best zone is stored in `opentyrian.cfg`, not the sidecar, so Hardcore
runs can update it. Record a zone when it starts and write the config immediately.

### Mode and effects

Use `endlessMode` for run structure:

- level selection;
- outposts and courses;
- save handling;
- Endless pickup substitutions;
- run records and prices.

Use `endlessFxActive()` for combat effects:

- scaling;
- active modifier bits;
- perks;
- elite and champion tiers.

Campaign debug mode can enable effects without run structure. Effect helpers must
remain identity operations at depth zero with no modifiers.

Purchases such as Reinforce and Revive remain gated by `endlessMode`. Per-level
effect state resets through `endlessResetZoneEffects`, which must stay RNG-free.

The scaling inspector calls the real accessors after temporarily substituting
zone, difficulty, and modifiers. Per-lever overrides bypass both depth and
modifiers.

## Menus and debug tools

Menu labels, choice counts, and help indices are parallel data. Update all three
when adding a row.

Debug menus separate row identity from display order. Switch logic uses a row ID,
never the current list position. Headings are non-selectable and navigation must
skip them.

Mid-level loadout edits must refresh cached ship data through the same path used
by the normal in-level ship change. Only a ship change restores armour; changing
another item must not heal the player.

The campaign level picker and Endless zone editor share row-navigation helpers.
Menu ID 15 is an intentional hole left by the removed level grid; do not reuse or
renumber it.

The Endless editor has two behaviours:

- jump mode stages values and commits only on launch;
- tune mode applies values when leaving the screen.

The editor is reachable from both centred shop UI and full-width in-game UI.
Choose its coordinate width from the active menu offset.

## UI and sprite safety

All `Sprite2_array` blits pass through `sprite2_index_valid`. A bad index otherwise
interprets arbitrary bytes as an offset.

Sidekick body sprites are one-based. Two-by-two mounts also use offsets `+1`,
`+19`, and `+20`, plus charge frames. Validate the full range, not only the base
index.

Boss and enemy bars draw in playfield coordinates. Enhanced boss bars also redraw
at the display rate; only the tick draw decrements their flash timer.

Enemy bars group by non-zero link number, ignore boss groups, and use the most
damaged visible part. Their render command must carry the same layer binding as
the enemy.

Help-bar values right-align to `ENDLESS_COURSE_PAYOUT_RIGHT`. Keep descriptions
short enough to leave room for prices or owned-stack counts.

Navigation-map planets iterate `mapPNum`, not the menu choice count.

## Weapons

Episode-specific weapon changes are applied after item data loads and must be
idempotent because an episode can be initialized repeatedly.

Projectile graphics above 1000 encode a superspark palette bank and base sprite.
Do not scale or compare the encoded value as ordinary damage or an ordinary
sprite index.

The custom weapon occupies spare runtime slots. Imported designs are clamped but
otherwise copied exactly. A design must always keep at least one segment and one
power state.

Custom weapon persistence and its library have fixed field widths. Validate
counts and indices before applying a record.

## Fonts

`font_ascii[]` mapping is not enough to prove that a glyph is visible. Some mapped
sprites are blank stubs.

| Bank | Reliable content |
| --- | --- |
| `TINY_FONT` | Most printable characters; widest coverage |
| `SMALL_FONT_SHAPES` | Letters, digits, and common punctuation |
| `FONT_SHAPES` | Uppercase headings; no digits |

In `SMALL_FONT_SHAPES`, `(`, `)`, `+`, `*`, `=`, `]`, `{`, and `}` are blank.
The tilde is a brightness toggle in text renderers and is not printed.

## Audio and MIDI

The MIDI backends convert LDS songs through the vendored midiproc library and run
their own sequencer threads.

At a loop boundary, replay pre-loop program, controller, pitch, and SysEx state
at timestamp zero. Do not replay notes. Without the state replay, later loops can
use end-of-song channel settings.

The Windows backend uses `CALLBACK_NULL` and its own thread to avoid callback
mutex deadlocks.

When a configured SoundFont path becomes stale, retry its filename under
`data_dir()` before falling back to automatic discovery.

See [src/midiproc/VENDORED.md](src/midiproc/VENDORED.md) for local library
changes.

## Crash logging

The Windows logger writes a stack trace and guarded game-state dump to
`opentyrian_log.log`.

The watchdog suspends the main thread only long enough to capture its context,
then resumes it before symbol loading and stack walking. Those operations may
need locks held by the suspended thread.

Item-name lookups in a crash path must tolerate unloaded tables and invalid IDs.
The Force Crash target is a volatile file-scope pointer so optimized builds still
perform the faulting write.

## Console ports

Platform paths live in `switch_platform.h` and `vita_platform.h`; shared code
accesses them through `console_platform.h`.

Switch:

- keep the SDL window resizable and out of desktop-fullscreen mode so dock changes
  can resize it;
- use `_Exit` after explicit persistence to avoid broken romfs/stdio teardown;
- save controller changes immediately because HOME exit may skip normal shutdown.

Vita:

- present at native size and force supersampling to 1x;
- keep presenting while the IME dialog is open;
- drive IME lifetime from `sceImeDialogGetStatus`, not
  `SDL_IsTextInputActive`;
- abort and terminate the dialog on every exit path so it releases the controls.

Both ports treat menu touch as absolute tap input and gameplay touch as relative
drag. The right stick is folded into ship movement. Networking and MIDI are
disabled.

## Level-script map stops

The event clock advances with vertical scroll. A map stop can resume only when
the screen clears, a link-254 jump fires, or `forceEvents` advances the clock.

HARVEST can leave an invisible, frozen linked anchor above the screen after the
visible boss dies. The map then cannot scroll and the script cannot reach its
release event.

The watchdog culls only enemies that are:

- above the reachable shot area;
- vertically unable to enter it;
- in a link group with no reachable live member;
- holding a non-`forceEvents` stop past `MAP_STOP_STALL_LIMIT`.

Horizontal motion is irrelevant. A live linked boss part makes the group
reachable and prevents culling. Detection uses a full enemy-pool scan rather than
the draw-loop visibility count.

Crash logs include parked-enemy and stall information for this path.

## Dormant dispenser bases

Enemy data 80-83 form the 2x2 "orb" base (event type 12, all four quadrants
share one linknum). Each piece carries a 17-frame palindromic `egraphic` cycle
with `animate=2` (play once per fire/launch), but ships with no turrets, no
launcher, and no arming or animate event in any level - the cycle is unreachable
in unmodified data. The base appears only in CAMANIS (episode 3, twice) and
ICESECRET (episode 4 file 20, "Secret Camanis research base", sixteen times);
the records are identical in all five episodes' item data. The single-tile
cousin, enemy 86, is the working model: same `animate=2` hatch with
`elaunchfreq=40 / elaunchtype=463` (the heavy orb, art bank 8).

The restore (`dispenserBasesActive`, set per level in `JE_main`) gives all
four pieces `launchfreq=40` in `JE_makeEnemy` so their launch countdowns stay
in sync (created the same tick) and the whole hatch opens as one; `launchtype`
stays 0 (an engine-supported animation-only path - the launch routine flips
`aniactive` before the `launchtype` check), so the launch trigger only starts
the animation.

The volley itself comes from `dispenser_fire`, called from the animation
advance when piece 80 reaches **frame 9** - the one frame where the top hatch
stands open on its lit eye and the orb below flares white. Firing on the art's
own glow frame, not on the launch tick, is what puts muzzle and animation in
step; the launch tick draws a closed hatch. The call sits inside the animation
advance so it lands exactly once per cycle even if the enemy leaves the draw
window, and it skips iced or dying bases.

- Eye: one weapon-59 shot (the same player-aimed round enemy 84's turret
  fires). Difficulty aim bonus, two-player targeting, and the endless
  seeker/speed/damage/champion scaling all mirror the turret path, and in
  endless the rising-tide extra shots fan from it like any other volley.
- Orb: a 1x4 lightning column - four stacked 12x14 enemy shots, 14px apart,
  straight down at speed 10, damage = weapon 59's, with `S_WEAPON_15` as its
  own firing sound. The segments are `spriteSheet8` (player shots) rows
  210/229/248/267 top-to-bottom, each with four consecutive frames, so the
  stock `sgr + animate` draw path with `animax=4` animates the bolt with no
  renderer change - and because that sheet is always resident, the bolt works
  in any level regardless of which enemy banks the level loaded. Same-tick
  spawns keep all four segments on the same frame, so the column always
  composes a whole authored bolt. Sprites and sound both come from weapons
  238-242, which fire this exact tile set as a `multi=4` composite; grepping
  the weapon table for a sprite id is the way to find its authentic sound.

Both emitters take the rising tide, but the bolt takes it as a composite: its
extra shots accumulate in turret slot 0's `eshotextracredit` and only cash out
as WHOLE extra bolts, exactly as the turret path treats a `multi` weapon -
a partial bolt is a broken sprite, not a weaker one. The base has no authored
turrets, so that credit slot is otherwise dead. Each tide bolt leans off the
vertical by the shared `endlessFanPhaseNow` fan; unlike the turret path, which
can only rotate a clone's velocity, this rotates the segment offsets too, so a
leaning bolt stays a straight line along its own travel instead of shearing.
A pool-space cap ahead of the loop keeps a bolt from ever spawning partially.

Assembly geometry (piece 80's `ex/ey` frame): art spans `ex-6..ex+42` by
`ey-35..ey+21`. Both emitters sit on the art's own centre line, assembly x 22
= `ex+16` (2px left of the geometric centre): eye at `ey-27`, and the orb's
white flare across `ey+4..ey+8` (the idle blue orb sits lower, `ey+8..ey+13`).
The bolt's top edge starts at `ey+4`, level with the top of the flare.
Sprites blit from their top-left, so a 12x14 shot centres on an emitter at
`(emitter - 6, emitter - 7)`; vanilla turret shots follow the same rule, which
is why enemy 84 spawns its shot at the enemy's raw `ex/ey`.

Campaign reads the Game Tweaks toggle (`restoreBaseDispensers`); Endless
ignores it and derives a 50/50 per zone from `endlessSplitMixSeed` salt
`depth*2 + 0x70000000` (salts `0x40/0x50/0x60000000` are taken by light cone,
elites, and gravity).

Other authored-but-idle hatch animations exist (GYGES sky hatches 158/159/161,
DELIANI base 239, and id 529 which is spawned nowhere); several lookalikes
(BUBBLES 465/467, FLEET/STATION 732/733, EYESPY 339/340) are armed at runtime
by event 31 and do animate. Only 80-83 are restored.

## General constraints

- Use the correct sprite bank. A wrong bank often produces plausible garbage.
- `enemycycle` is one-based.
- Positional enums index shipped data; do not remove apparently unused members.
- `config_file.c`, `opl.c`, and midiproc are maintained or vendored libraries.
  Do not prune their unused public API.
- Keep upstream Doxygen comments and third-party documentation in their original
  style unless the upstream code itself is being changed.
