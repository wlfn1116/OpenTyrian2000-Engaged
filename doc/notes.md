# OpenTyrian2000 Engaged maintainer notes

This file records constraints that are easy to miss while changing the fork.
Player-facing behavior belongs in [GUIDE.md](GUIDE.md). Source comments should
explain only the local reason for an unclear choice.

## Build and targets

`build-all.ps1` builds PC, Switch, and Vita targets and copies successful
outputs to `build`. A target failure does not stop the remaining targets unless
`-FailFast` is used.

Target constraints:

- The PC executable runs beside `data`; `build` is only an output folder.
- MIDI is enabled only for Windows x86-64.
- The Windows crash logger is stubbed on other platforms.
- Switch builds run through devkitPro bash with an MSYS-style `DEVKITPRO` path.
- Vita builds use native CMake and Ninja. MSYS paths break the native tools, and
  some data filenames are unsafe as individual shell arguments.
- Console Release builds define `NDEBUG`.

Maintained targets should build without warnings. Existing suppressions cover
specific DOS-era idioms. New warnings should be fixed at their source.

MSVC uses `/source-charset:utf-8`. Third-party header warnings are suppressed
around the include. GCC targets suppress `-Wformat-truncation` for fixed-width
UI fields.

Run MSVC analysis separately:

```text
MSBuild visualc\opentyrian.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /p:EnableMicrosoftCodeAnalysis=true
```

Analyzer range warnings require inspection. Use `OT_ASSUME` only after a real
bounds check.

## Rendering

### Render list

The simulation remains fixed at 35 Hz. `render_list.c` records each tick and
replays it at the display rate.

Required invariants:

- Moving objects need a stable `rl_current_id`.
- Presentation timing uses the performance counter.
- Exact replay with `use_override == false` reproduces the recorded frame.
- Projectile velocity comes from the projectile because slots are reused.
- Ship-attached shots separate ship motion from their own motion.

The render list snaps an ID when its command count changes. Conditional parts
need their own ID when snapping the parent would be visible. The Nort ship's
banking trim uses `RL_ID_SHIP_TRIM_BASE` for this reason.

### Display-rate ship movement

Supported single-player games use a real-time ship integrator. The 35 Hz
simulation reads the resulting position for shots and collision, keeping the
sprite and hitbox aligned.

The path is disabled during demo recording, demo playback, and network games.
Advance it on every presentation-loop iteration, including an iteration that
also runs a simulation tick. Joystick press edges consumed by the integrator
must remain available to pause and menu handling.

### Feedback filters

Ice, water, and lava effects use frame feedback. Smooth presentation keeps two
separate images:

- `render_gs`: persistent filtered background;
- `smoothie_frame`: current background, interpolated entities, and overlays.

Entities must stay out of the persistent buffer or they smear. Full-screen
color and brightness effects apply to both sides of the residual comparison.

### Parallax and scroll

Background layers record integer movement and a fractional phase. Bound enemies
and health bars use the same layer phase.

- `background3x1` binds layer 3 to layer 1.
- Whole-pixel draw correction and fractional phase remain separate.
- Round the combined layer and local offset once.
- Layer 3 may be recorded after its advance; preserve its authored base step.
- Horizontal normalization uses the ship's actual travel range.
- `enemy_rides_layer2` is the shared layer-binding test. Pickups are excluded.

Mirrored Layers reflects out-of-range columns within the same map row. Each
render command stores its reflection parameters so 1x and supersampled replay
match. Feedback filters need the extra reflected strip beyond the visible edge.

`endlessScrollExtraPx` publishes the average rate and distributes Endless scroll
boosts without changing long-run distance. Call it once per layer per tick.
Layer-bound enemies use the same boost and carry. Event spawns crossed during a
large scroll step need catch-up to the current layer phase.

Palette fades, picture wipes, Destruct, HUD gauges, enhanced boss bars, and Soul
of Zinglon all have display-rate presentation paths. Their simulation state
still advances once per tick.

### Supersampling

The playfield supports fixed 1x through 5x rendering. `0` means Auto, which
follows the selected scaler and resolves to at least 2x while Smooth Motion is
active.

`RENDER_SUPERSAMPLE_NATIVE` follows the fitted output rectangle and is bounded
only by `RENDER_SUPERSAMPLE_LIMIT`. Auto retains
`RENDER_SUPERSAMPLE_MAX`. The Vita target always resolves to 1x.

Filter enum values are persisted. Append new values without renumbering existing
ones.

## Widescreen coordinates

The frame is 356x200. The visible playfield is 299x184 and the HUD is 57 pixels
wide.

- World rendering uses `game_screen` and `PLAYFIELD_LEFT`.
- The final compositor crops the playfield to screen x=0.
- Menus use a centered 320-pixel virtual canvas.
- HUD overlays use composited-buffer coordinates.

`PLAYFIELD_LEFT` is the compositor crop offset. `PLAYFIELD_X_SHIFT` is a separate
background-tile phase. Use a surface's pitch when stepping rows.

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

Keep tuning values in the owning module: combat scaling in
`endless_combat.c`, perk strengths in `endless_internal.h`, prices in
`endless_shop.c`, and course generation weights in `endless_course.c`.

### Structural RNG

Endless structure uses SplitMix64 streams derived from the run seed and depth.
Combat timing must not alter later shops or courses. Every phase salt is unique.

Structural draw order is a compatibility interface. Course generation gathers
levels, assigns themes and boons, applies visit and milestone rules, sorts the
cards, makes names unique, and caches base-level names. Adding or reordering a
draw changes existing seeds. Prefer a new draw phase at the end of the relevant
sequence. Retries reuse their stored or reproducible music choice.

### Scaling and combat

Real depth drives progression and economy. Effective depth includes difficulty
and drives most combat scaling.

- Scale raw damage before the enemy-health accumulator.
- Decode piercing damage before scaling and re-encode it afterward.
- `enemy_has_boss_bar()` is the shared boss test.
- Contact scaling applies only to player damage.
- Round percentage changes when truncation would erase a small effect.

Piercing repeat-hit state belongs to the bullet. Charge it once per tick using
the toughest crossed hull, bank the next lock, and apply it on the following
bullet pass. Ordinary enemies are unaffected by a boss-specific lock.

Every logical enemy death calls `enemy_logical_death`. It updates kill count,
bounty deduplication, Shockwave, Martyrdom, and Chain Reaction. Quiet mode still
updates deduplication state. Elite and champion bounties pay once per logical
enemy.

### Modifiers and courses

`endlessModTable` owns modifier text, danger weight, payout, and classification.
New modifiers require review of masks, course pools, danger and payout, monitor
rows, save bit width, and The End behavior.

Compatibility filters prevent modifiers that cancel the same gameplay lever.
Curated names must be unique, use visible `font_ascii` glyphs, fit the course
card, and remain within the external table bounds in `endless_internal.h`.
Name collision resolution is deterministic and runs after sorting.

Modifier-specific constraints:

- Martyrdom fires one symmetric burst per logical enemy.
- Seeker state belongs to the projectile and permits one delayed correction.
- Rising-tide shots clone complete authored volleys.
- Static combines a power drain with a recharge lockout.
- Retaliation refreshes one timer.
- Flak Screen affects rising-tide shots only.
- Low Profile changes damage tests without changing pickup reach.
- Star Charts and Breakthrough are banked on clear and consumed later.

Course order uses danger rank and the course's cached payout. Purchased buffs
and Sabotage must not change the ordering key. Visit flavor rolls are consumed in
the fixed order Jackpot, Ambush, Gauntlet.

Milestones use the upcoming real zone:

- odd multiples of 25: S/S+;
- other multiples of 50: S+/S++;
- multiples of 100: one END, two S+++, and two S++.

The End uses `ENDLESS_MOD_THEEND` for its name, rank, label, and payout. Other
modifier bits provide its mechanics. Keep rank-name and rank-color tables the
same length.

Shop and level music use different indexing. `songBuy` passes directly to
`play_song`; `levelSong` is one-based and plays as `levelSong - 1`. Stored song
and depth values prevent adjacent repeats and keep retries stable. Milestone
levels ignore script fade and song-change events.

### Economy and perks

The course card shows the exact payout that will be banked. Sabotage and
purchased next-sector modifiers update it immediately. Inventories seed from
`player[0].items`, and capped E-Shop rows show no price.

Perk UI and gameplay derive from the same accessors. Sidekick capacity, Opening
Salvo timing, and offer count each have one authoritative value. Opening Salvo
covers emitted shots and manual special effects. A chain-reaction carrier deals
no damage itself, so `player_shot_create_chained` copies its salvo tag onto the
child shots in place of the window test, and the boost follows the volley that
fired the carrier for the whole cascade. Attraction velocity remains within
`Sint8`, and repairs cap at the Endless hull maximum.

All run cash uses `endlessCashCredit`, `endlessCashDebit`, or the
`endlessShopTradeBegin` and `endlessShopTradeCommit` bracket. Playfield pickups
use `player_award_pickup_cash`. The ledger invariant is
`earned - spent == wallet`.

### Save format

`tyrian.sav` is fixed and checksummed. `endless.sav` stores run state and the
current outpost snapshot. The checkpoint is written at outpost entry. Hardcore
suppresses disk checkpoints, while its in-memory sortie snapshot still carries
the run mode across a retry or bail.

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
| 14 | Rapid Charger migration |
| 15 | Run mode |
| 16 | Total cash earned |
| 17 | Total spent and source breakdown |
| 18 | Gear-spending sink |
| 19 | Full spending breakdown |
| 20 | Custom-weapon record mark |

Append fields and guard reads by version. Older records retain their historical
field widths. Perk IDs are persisted in owned stacks and pending offers; append
enum members, and migrate both arrays if an ID is removed or reordered.

The best zone is stored in `opentyrian.cfg`, indexed by `EndlessRunMode`.
`best_zone` remains the Relaxed compatibility key, `best_zone_normal` remains the
Standard compatibility key, and Hardcore uses `best_zone_hardcore`. Set the run
mode before calling `endlessRecordRunStart`.

Each record also stores whether a custom weapon was in use, under the same key
plus `_custom`. `player_shot_create` reports every shot leaving
`customWeaponPort` (the custom sidekick fires through that port too), which arms
a per-zone flag; `endlessOnSectorCleared` promotes it to `endlessRunUsedCustom`,
so only a cleared zone counts. `endlessResetZoneEffects` clears the zone flag at
every zone start, which is what keeps outpost editor and shop previews out of the
record.

`endlessSeedSelect` shows the selected mode's record. `JE_highScoreScreen` gained
a ninth page after the five episodes and three Timed Battles, which lists all
three records and erases one through `endlessClearRecord`. Endless has no score
table, so that page draws itself: `JE_drawEndlessRecordPage` and
`JE_endlessRecordPageInput` share the `endlessPage*` geometry, and the input half
returns whether it consumed the tick, which is what keeps paging and exit in the
screen's own shared handling. Erasing is menu steps rather than a keypress so it
works on a controller. A pending answer swallows every input and always opens on
No, so it also hides the paging arrows and the paging hint, neither of which does
anything while it is up.

`endlessPageColumns` derives the page's two columns from the widest note line and
centers that block, so the layout stays centered if the notes are reworded. Zones
are right-aligned on a column that leaves the custom mark its own strip, which is
why a record ends flush with the notes whether or not it carries the mark.

Every screen shows a record against a named mode, so `endlessRecordCustomMark`
supplies the trailing " C" alone. The page's two note lines explain that mark and
warn what selecting a row leads to. The first uses `=`, which exists in TINY_FONT
but not in the two larger shape tables, so keep that character out of
`normal_font` and `large_font` strings. Do not route those notes through
`JE_helpBox`: it wraps at its `boxwidth` in characters and draws each line
downward, so a long string at the bottom of a screen loses its tail.

The mark is written in two places, because it is earned after the record it
belongs to was stamped. `endlessNoteZoneReached` stamps depth and mark together
when the run goes deeper, but it runs at zone start, before that zone is flown,
and a run that only matches the record never reaches the assignment at all. So
`endlessOnSectorCleared` also calls `endlessMarkRecordCustom`, which marks the
record whenever the run is standing at or past its depth. Nothing clears a mark
in place: it goes away when an unassisted run sets a deeper record.

### Death, retries, and effects

Relaxed mode opens `JE_endlessDeathMenu` over the frozen playfield. Standard and
Hardcore use GAME OVER and lock the pause menu after the fatal hit.

Live-level panels copy `palettes[0][240..255]` into the active text ramp, use the
correct brightness for each font, center within the 299x184 playfield, and
restore `colors` before the caller fades out.

Wreck dismissal is armed after all inputs are released. `newkey` includes key
repeat and synthesized joystick keydowns. Esc remains reserved for the pause
menu in Relaxed mode.

Restart Zone calls `clear_song_selection()` so the unchanged track reloads after
`fade_song()`. Both retry choices restore the launch snapshot. Return to Outpost
uses the Quit Level path. Restart Zone clears `endlessResumeVisit` and arms the
locked relaunch.

The outpost owns `itemAvail`. A level script's `']I'` replaces it with that
level's campaign shop list, and `JE_loadMap` runs before `endlessCaptureSortie`,
so `']I'` reads its nine rows and discards them under `endlessMode`. Skipping the
reads would leave the script parser out of step with the file.

Stock is stored as item ids, which resolve against the item tables of the episode
that generated them. `JE_initEpisode` loads ep1-3 from `tyrian.hdt` and ep4/5 from
the level file, so `endlessSortieOutpostEp` records the outpost's episode and
`endlessRestoreSortie` restores it before the shop redraws. Both retry paths carry
that field and `endlessSortieOutpostMods` across the reset inside
`endlessApplyCurrent`. It needs no save field, because an Endless checkpoint is
only written at an outpost and `tyrian.sav` already holds the episode.

The two item table sets differ in the Gencore Solar Shield icon, two ship
illustrations, The Stalker 21.126 price (65535 against 30000), and the weapon data
covered by the Episode Differences menu. Other campaign writes in the same parser
need no guard: `']e'`, `']g'` and `']2'` sections are excluded from the Endless
pool, and `endlessRegenerateLevel` clears the rest.

Use `endlessMode` for run structure, saves, prices, and pickup substitution. Use
`endlessFxActive()` for combat scaling, modifiers, perks, and enemy tiers.
Campaign debug mode can enable effects without run structure. Effect helpers are
identity operations at depth zero with no modifiers. Per-zone effect reset stays
RNG-free.

## Menus and debug tools

Menu labels, choice counts, and help indices are parallel data. Update all three
when adding a row.

Panels over a live level center within the playfield. The pause menu shifts both
boxes as complete units, and its help line uses the same axis. Keep over-wide
text from starting at a negative x because `blit_sprite_hv` wraps rows instead
of clipping there.

Debug menus separate row identity from display order. Switch logic uses row IDs.
Headings are non-selectable, and navigation skips them. Filtered views own
selection, scrolling, and hit testing.

Mid-level loadout edits refresh cached ship data through the normal in-level
path. Only a ship change restores armor. Player two remains the Dragonwing role,
so its sprite and armor do not follow an edited hull ID.

Menu ID 15 is an intentional hole left by the removed level grid. The Endless
editor stages jump values until launch and applies tuning values when leaving
the screen. Its coordinate width follows the active menu offset.

## Level scripts and randomness

Tyrian levels are authored event scripts. Reseeding `mt_rand` changes background
enemy selection, spawn jitter, effects, and sound choices. It does not reshuffle
the authored pickup sequence.

Features that promise varied routes or rewards must make that choice at event
processing time. Endless uses independent SplitMix64 structural streams in
`endless_rng.c`.

## Arcade life scaling

With Arcade Life Boost enabled, shield and armor ceilings scale from the hull's
one-life values to 28 units at 11 lives. The integer accessors are
`arcade_armor_max`, `arcade_shield_max`, and `arcade_rescale_to_lives`.

`player[].lives` aliases a weapon-power field inside `PlayerItems`. Life changes
therefore travel through existing save, network, and rollback state. A life
pickup flows through `power_up_weapon`; the Galaga and Dragonwing paths are the
other direct writers.

`Player.hull_armor` stores the hull value. `Player.initial_armor` stores the
scaled ceiling used by respawn, pickups, and Endless clamps. Gauge values rescale
proportionally, and `hud_bars_dirty` schedules the presentation repaint.

SuperTyrian and Super Arcade ships keep their authored scaling. A zero shield
base remains zero.

## Networking

Both machines simulate both ships. Any menu write that affects simulation must
reach the peer before either machine resumes. Modal loops call
`NETWORK_KEEP_ALIVE()` while connected.

Adding or resizing a wire field changes offsets and requires a `NET_VERSION`
bump. Rendering, audio, and local input settings remain local.

### Host and player slot

`network_is_host` selects the machine that listens and decides session settings.
`networkHostPlayerNum` selects the ship slot that machine flies. Slot-specific
rules remain keyed to player number because player two is the Dragonwing.

The joiner's initial slot is provisional in an in-game lobby. The host's choice
settles both slots. Command-line netplay retains its historical equal-slot
conflict behavior.

### Reliable channel

The reliable UDP layer follows three rules:

- A receive error does not prove the link is dead. Wait loops sleep on
  `network_check() <= 0`.
- Outbound queue room is checked before sending and assigning a sequence number.
- Packet fields are read only when the received length covers them.

The sender keeps at most half of `NET_PACKET_QUEUE` outstanding during a resync.
This prevents transport acknowledgements from filling the receiver's inbound
queue before the application consumes the chunks.

### Rollback input stream

`PACKET_INPUT` has a 48-byte header and up to sixteen redundant 14-byte input
records. It is unacknowledged and idempotent.

- `network_check()` drains up to `NET_DRAIN_MAX`; callers do not add another
  drain loop.
- The level epoch separates frames from different levels.
- Menu and pause request bits are processed from received truth outside the
  simulation misprediction test.
- Received canaries queue until the local frame can be compared.
- The pool hash covers player shots, enemy shots, explosions, repeating
  explosions, and the sound queue.

An in-game menu request schedules frame `f + NRB_REQ_LEAD`, and both peers stall
there until it is final. Pause changes presentation state only and remains
immediate.

`shipGr` and `shipGrPtr` are registered derivations of the selected ship.
`JE_getShipInfo` also restores armor, so it cannot be used as a restore fixup.
The display-rate ship prediction latch is registered and reset at level start.

Endless combat effects remain outside the rollback registry. The rollback
self-test is gated while those effects are active and is enabled through
`rollback_selftest_set()` so the registry and snapshot ring exist before play.

### Determinism

Demo recording and playback use the same fixed RNG seed. Compare cold-start demo
traces and enter the demo from the same initial state.

Raw snapshot bytes contain process-specific pointers. Per-item comparisons use
the relocation walk before hashing. Summary hashes remain the authoritative
cross-process comparison for fields repaired by restore fixups.

Two `mt_rand()` calls in one C expression have unspecified evaluation order.
Draw into named locals before using both values. Matching draw counts alone do
not prove that values were assigned to the same operands.

Simulation floats require consistent compiler behavior. Switch, Vita, and Linux
disable floating-point contraction, while MSVC uses precise floating-point mode.
Simulation paths use `sim_sinf` and `sim_cosf`; presentation-only paths may use
the platform math library. Linux and console builds use signed `char` semantics.

`RB_TRACE_ITEMS_TO` is zero by default. Enable a narrow frame window because a
full per-item trace is large and can disturb console timing.

### Desync recovery

Rollback recovery sends the host's registered state in `PACKET_RESYNC` chunks.
Chunk fields after the four-byte reliable header are generation, chunk index,
chunk count, and payload length. Chunk zero carries registry size, compressed
size, and checksum. The payload uses zero-run RLE.

Wire export relocates registered pointers to tags or offsets. Supported targets
include sprite-sheet globals, `enemyDat`, ship graphic pointers, and background
map arrays. Unknown pointer homes reject the export. Extend `rb_relocs` when
registering another pointer.

Peers compare `rollback_layout_fingerprint()` and `rollback_state_size()` during
connection. Registered state uses fixed-width types across platforms. A layout
mismatch disables recovery for the session.

The host canonicalizes unused shot and explosion slots before export. Dead enemy
slots remain intact because some spawn paths inherit their sprite-sheet pointer.

Transport acknowledgement means that bytes arrived. The joiner sends
`NRB_RS_ACK` after adopting a complete generation, and the host resets only after
that application-level acknowledgement. Fatal layout refusal uses a reasoned
NAK and retires recovery on both peers. Recovery is capped at three attempts per
level.

`PACKET_WAITING` is a paired rendezvous. Menu release, pause release, shop exit,
and level start consume it in the same order on both machines. Loops that inspect
packets during a rendezvous must leave unmatched waiting packets queued.

The level-start barrier completes map and sprite loading before the simulation
fade begins. `JE_advanceLevelFade` advances the fade inside the tick so rollback
re-simulation sees the same state.

### Presentation after rollback

Presentation state stays outside snapshots and is repaired on the first live
pass:

- `textErase` decrements only on live passes, and a new message always redraws
  the message-bar background.
- Sidekick HUD changes set `hud_sidekicks_dirty` during a silent replay.
- Shield and armor changes set `hud_bars_dirty`; restore paths request a repaint.
- Gauge flash counters and link sound cues are presentation state.
- `SFX_CUE_CHANNEL` is reserved for presentation cues.

Future event-driven HUD drawing needs the same dirty-flag repaint pattern.

### Diagnostics

Network events use the net log. Process failures use the crash log. Both are
created lazily under `log/` with the launch timestamp and are append-only.

Each online session records start and end lines. Desync reports include the
disputed frame, recent input tuples, pool details, player shots, and enemy state.
Resync aborts record generation, progress, elapsed time, and cause.

Diagnostic counters span the session. Per-level canary counters reset with the
rollback core. Writers read module statics without SDL_net calls so crash and
watchdog contexts can use them.

Switch and Vita write reduced network entries in their user directory. Console
Clear Logs removes only recognized OpenTyrian log names.

### Online saves

Online games use the existing two-player page in `tyrian.sav`, slots 12 through
22. Slot 22 remains the automatic LAST LEVEL backup and is read-only in the menu.
Online and local two-player sessions intentionally share these slots.

`save_record_pack` and `save_record_unpack` define the 77-byte little-endian
network record. A resumed `PACKET_DETAILS` appends that record after its normal
eight-byte prefix. The joiner keeps its own live input-device assignments.

The host selects New Game or Load Game after connection. Network load menus stay
on the two-player page, filter unavailable episodes, and keep the peer alive.
Alt+L is disabled online; Alt+S opens the shared two-player save page.

An involuntary disconnect after gameplay offers a save based on the pre-level
LAST LEVEL backup. The prompt clears silent rollback state before drawing and
keeps acknowledgement traffic moving while open.

## UI and sprite safety

All `Sprite2_array` blits pass through `sprite2_index_valid`. Sidekick body
sprites are one-based. Two-by-two mounts also require offsets `+1`, `+19`, and
`+20`, plus charge frames. Validate the complete range.

Boss and enemy bars draw in playfield coordinates. Enhanced boss bars also
redraw at the display rate; only the tick draw decrements their flash timer.
Enemy bars group by non-zero link number, ignore boss groups, and use the most
damaged visible part. Their render command carries the enemy's layer binding.

Two-player gauge blocks repeat at a 134-pixel stride. `JE_dBar3` paints
`2 * units + 1` rows upward, while the two-player wipe clears 45 rows. Keep the
clamped gauge and shield ceiling mark within that cleared region.

Help-bar values right-align to `ENDLESS_COURSE_PAYOUT_RIGHT`. Descriptions leave
room for prices and stack counts. Navigation-map planets iterate `mapPNum`.

Shop icons are two-by-two blocks on the nineteen-wide `newsh1.shp` grid, so a
base index also claims `+1`, `+19`, and `+20`. Only `weaponPort`, `powerSys`,
`options`, and `shields` index that sheet through `itemgraphic`. Specials index
`spriteSheet10`, and ships use `shipgraphic` against `spriteSheet9` or the
Tyrian 2000 sheet, so `ships[].itemgraphic` is unused. A zero `itemgraphic` on a
special is meaningful: the equip guards reject it, which keeps script-only
specials out of play.

`JE_applyUnusedShopSprites` rewrites icons for the Unused Sprites option. Its
capture pass runs after the placeholder fallback and after
`JE_addChargeLaserCannon`, so the stored baseline matches what the shops would
otherwise draw, and before `customWeaponInit`, which claims unrelated slots.
Apply is idempotent and writes the baseline back when the option is off, so
`JE_initEpisode` and the menu handlers can call it without an item reload. The
Charge-Laser slot moves between episodes, so its write is guarded against
`chargeLaserSlot`.

## Weapons

Episode-specific changes apply after item data loads and remain idempotent.
Projectile graphics above 1000 encode a superspark palette bank and base sprite.

The custom weapon uses spare runtime slots. Imported designs are clamped and
otherwise copied exactly. Every design retains at least one segment and one power
state. Persistence records have fixed field widths.

A shot with `poweruse` temporarily zero bypasses both power cost and generator
availability. Extra beams also require the primary shot to succeed. The Zica
level-11 paths enforce this in gameplay and the Creator test range.

## Fonts

`font_ascii[]` mapping does not prove that a glyph has visible pixels.

| Bank | Reliable content |
| --- | --- |
| `TINY_FONT` | Most printable characters |
| `SMALL_FONT_SHAPES` | Letters, digits, and common punctuation |
| `FONT_SHAPES` | Uppercase headings without digits |

In `SMALL_FONT_SHAPES`, `(`, `)`, `+`, `*`, `=`, `]`, `{`, and `}` are blank.
The tilde changes brightness and is not printed.

## Audio and MIDI

MIDI backends convert LDS songs through the vendored midiproc library and run
their own sequencer threads. At a loop boundary, replay pre-loop program,
controller, pitch, and SysEx state at timestamp zero. Do not replay notes.

The Windows backend uses `CALLBACK_NULL` with its own thread. When a configured
SoundFont path becomes stale, retry its filename under `data_dir()` before
automatic discovery.

See [src/midiproc/VENDORED.md](src/midiproc/VENDORED.md) for local library
changes.

## Crash logging

The Windows crash log contains a stack trace and guarded game-state dump. The
watchdog resumes the main thread before symbol loading and stack walking.
Item-name lookups tolerate unloaded tables and invalid IDs. The Force Crash
target remains a volatile file-scope pointer in optimized builds.

During a network game, state dumps include the network and rollback diagnostic
sections.

## Console ports

Platform paths live in `switch_platform.h` and `vita_platform.h`. Shared code
accesses them through `console_platform.h`.

Switch constraints:

- Keep the SDL window resizable and outside desktop-fullscreen mode for dock
  changes.
- Use `_Exit` after explicit persistence to avoid broken romfs and stdio
  teardown.
- Save controller changes immediately because HOME exit may skip shutdown.

Vita constraints:

- Present at native size and force supersampling to 1x.
- Keep presenting while the IME dialog is open.
- Own the IME natively and terminate it exactly once on every exit path.
- Clear latched `keydown` and `mousedown` state after raw event draining.
- Disable rear touch and treat front touch as menu taps or gameplay drag.

Both ports fold the right stick into ship movement and disable MIDI.

### Console netplay

`WITH_NETWORK` is enabled on both ports. Switch uses `switch-sdl2_net` and calls
`socketInitializeDefault()`. Vita implements the required UDP subset in
`vita_net.c` over SceNet.

`network_local_addresses()` falls back to the native console address APIs when
`SDLNet_GetLocalAddresses` is unavailable. Text fields use `console_swkbd` and
then apply the field's normal character filter.

## Invisible level structures

Player shots use `enemy_has_visible_pixel` to avoid damaging art-backed enemies
before they enter the playfield. Some level structures have blank enemy frames
because the map supplies their art. A fully blank frame falls back to its nominal
12x14 cell footprint; a frame with any drawn cell uses its pixels.

Resolve blankness against the active sprite sheet with `sprite2_is_blank`.
Armor 255 keeps the stock spark and projectile behavior.

## Level-script map stops

The event clock advances with vertical scroll. A map stop resumes when the screen
clears, a link-254 jump fires, or `forceEvents` advances the clock.

The parked-enemy watchdog culls a stop-holding enemy only when it is above the
reachable shot area, cannot move into it, has no reachable live linked member,
and exceeds `MAP_STOP_STALL_LIMIT` without `forceEvents`. Horizontal motion does
not affect this test. Crash logs include parked-enemy and stall state.

Runtime validation remains required for HARVEST boss kills during the entrance.
The generic parked-enemy watchdog alone does not close that acceptance path.

## Dormant dispenser bases

Enemy IDs 80 through 83 form a linked 2x2 base with an authored hatch cycle.
`dispenserBasesActive` arms the animation. `dispenser_fire` emits the eye shot and
the four-segment bolt when piece 80 reaches frame 9.

The bolt uses four `spriteSheet8` segments with shared animation timing. Extra
rising-tide bolts are created as complete four-segment units, and their position
offsets rotate with velocity so the column remains straight.

Campaign behavior follows `restoreBaseDispensers`. Endless uses a structural
per-zone roll below `ENDLESS_DISPENSER_ALWAYS_ZONE` and enables the bases at and
above that depth.

## General constraints

- Use the correct sprite bank.
- `enemycycle` is one-based.
- Positional enums index shipped data; do not remove unused-looking members.
- `config_file.c`, `opl.c`, and midiproc expose maintained or vendored APIs.
- Preserve upstream Doxygen comments and third-party documentation.
