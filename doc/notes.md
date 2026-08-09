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

Destruct is its own layout: two 144px HUD frames pinned flush to the screen
edges, with rows `0..HUD_ROWS-1` between them left as open sky so shots crossing
it stay visible. That window is transient-only. `destructTempScreen` is the
persistent world -- terrain plus explosion glow -- and the sole thing that
clears itself there is the 241..255 fade in `DE_blendTempPixel`; anything else
written above the HUD line is never repainted, never falls, and (below the
`y <= 14` collision gate) cannot even be shot away, so it hangs in the window
for the rest of the round. Hence `DE_RunTickExplosions` lets only `EXPL_NORMAL`
flares up there and keeps `EXPL_DIRT` at the classic `y > 15` ceiling. Every
other painter already stops short of the window: `DE_generateRings` gates on
`y > 12`, the base terrain clamps to `y >= 40`, and `DE_widenHUDBackdrop` blacks
the gap before the round's `VGAScreen -> temp` copy.

The window also made two latent `JE_superPixel` bugs reachable, both from its
walking pointer: the star starts at `(x-2, y-2)`, so `rowLen * (tempPosY - 2)`
underflowed in unsigned arithmetic at `y == 1` into a ~4GB pointer jump, and a
row skipped by the bounds check still took the loop's `s += rowLen - 5` without
the inner loop's five steps, shifting every row after it 5px left. It now
addresses each pixel from its own clipped coordinates. Behaviour is unchanged
for every case vanilla could reach (only trailing rows clip there, where the
misalignment had nothing left to corrupt).

Wall placement had a memory-safety bug older than the port: `baseMap[vga_width]`
is immediately followed in `destruct_world_s` by the `VGAScreen` pointer, and a
wall footprint at the maximum `wallX` (`vga_width - 11`, 12 wide) reads one
column past the array -- the pointer's low half, ~3 billion, which
`JE_placementPosition`'s flatten then wrote back across a dozen real columns.
The next `DE_drawBaseTerrain` handed that to `JE_rectangle`, whose guard only
checked the UPPER bounds; as a negative int it slipped through into a wild
memset (the 2026-08-08 Backspace-reroll crash). Vanilla had the same overflow
at 320 wide. Fixed at both ends: the placement footprint clamps to the array,
and `JE_rectangle` rejects negative coordinates like `JE_pix` always has. It
was also an online desync source -- each machine flattened its own pointer
bits into the terrain, so the poisoned maps differed per machine.

The window is also SOLID now, not just visible. The shot collision pass used to
skip everything at `y <= 14` (vanilla's ceiling, from when the HUD strip covered
those rows full-width), which made anything standing in the window a ghost. In
the gap columns the skip now reaches to the top of the screen; over the boxes
the classic ceiling stands. The things that can stand there: wall towers
(`DE_generateWalls` stacks up to five 14px blocks above terrain that clamps at
y=40, so a peak tower reached y=-30 -- wrapping its unsigned `wallY` -- and the
tower height is now capped by the headroom above `HUD_ROWS` instead), and ring
dirt at rows 13-14 (`DE_generateRings` gates at `y > 12`). Units never enter the
window: fliers clamp at `unitY >= 24` (hitbox rows 18+) and satellites spawn at
`y >= 30`. Bouncing shots follow the same rule -- the y=14 bounce ceiling was
thin air in the middle of the open window, so inside the gap columns it drops
to y=1.

A third sky-window leak lived in the smooth present, not the sim. The tick draw
shows a shot -- head and trails both -- only while the head's `y` is on-screen
(`DE_RunTickShots`), so a shot that exits the top freezes its trail slots
un-decayed at the exit point; `DE_DrawShotsScaled` had no such gate and kept
repainting those frozen pixels every presented frame for the whole hang time of
the lob. The shot list is per-tick state, so what looked like stuck debris was
really a live redraw at the display rate. The scaled draw now applies the tick
draw's own head gate before touching head or trails; the classic (non-smooth)
path never had the leak.

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

Records are per difficulty, indexed by a slot in `endlessDifficultyLevel`, the
six levels `difficultySelect` can return. That array's order is the on-disk
order, so append to it rather than reordering it. One key per mode holds the
zones as a comma-separated list and another holds their custom marks as a string
of 0 and 1; a short or absent value leaves the remaining slots empty, which is
how the list grows. A run writes exactly one record, chosen by `endlessRunRecord`
from `initialDifficulty`, which `newEndlessGame` fixes before the run and
`JE_loadGame` restores.

`endlessRecordRunStart` baselines the same record, so the run-over gain measures
that record alone rather than a mode-wide figure a different difficulty may hold.
The records themselves live in `endless_internal.h`; readers elsewhere go through
`endlessBestZoneForDifficulty` and `endlessRecordDiffCustomMark`.
`endlessBestZoneAny` derives a mode's figure as the deepest of its records, for
the mode list and the seed screen, which is picked before a difficulty exists. It
cannot disagree with the breakdown behind it, and `endlessClearDeepestRecord`
erases every record standing at that depth so one confirmation always moves the
figure. The old per-mode `best_zone` keys survive as `endlessBestZoneUntagged`
(endless_internal.h), which counts towards the derived figure but is written only
by a run on a difficulty outside the six. That keeps records from a config
written before the breakdown existed without inventing a difficulty for them.

Each record also stores whether a custom weapon was in use, under the same key
plus `_custom`. One rule covers it: a record shows the mark when the run that set
it flew a custom weapon in a zone. `player_shot_create` reports every shot
leaving `customWeaponPort` (the custom sidekick fires through that port too), and
`endlessNoteCustomWeaponShot` arms a per-zone flag, but only while a zone is
running. That gate, not the timing of anything else, is what keeps the outpost
weapon editor and the shop's weapon preview out of the record: both fire through
the same path and neither is a zone.

`endlessCustomWeaponZoneEnd` promotes the flag to `endlessRunUsedCustom` and
closes the zone. It is idempotent and every way out of a zone calls it: a clear
through `endlessOnSectorCleared`, the outpost through `endlessBetweenLevels`, and
a run that ended mid-zone through `endlessOnRunEnd`. Do not narrow it back to
clears only, or dying in the zone you flew the weapon in loses the mark.

`endlessSeedSelect` shows the selected mode's record. `JE_highScoreScreen` gained
a ninth page after the five episodes and three Timed Battles, which lists the
three modes, opens a mode's breakdown by difficulty, and erases a row through
`endlessClearDeepestRecord` or `endlessClearRecordDifficulty`. Endless has no score
table, so that page draws itself: `JE_drawEndlessRecordPage` and
`JE_endlessRecordPageInput` share the `endlessPage*` geometry and one
`EndlessPageState`, and the input half returns whether it consumed the tick,
which is what keeps paging and exit in the screen's own shared handling. Erasing
is menu steps rather than a keypress so it works on a controller. A breakdown or
a pending answer swallows every input and hides the paging arrows and hint,
neither of which does anything while one is up; Esc unwinds one level per press.
The answer always opens on No.

`endlessPageColumns` derives the page's two columns from the widest note line and
centers that block, so the layout stays centered if the notes are reworded. Zones
are right-aligned on a column that leaves the custom mark its own strip, which is
why a record ends flush with the notes whether or not it carries the mark.

Every screen shows a record against a named mode, so the mark accessors supply
the trailing " C" alone. The page's two note lines explain that mark and
warn what selecting a row leads to. The first uses `=`, which exists in TINY_FONT
but not in the two larger shape tables, so keep that character out of
`normal_font` and `large_font` strings. Do not route those notes through
`JE_helpBox`: it wraps at its `boxwidth` in characters and draws each line
downward, so a long string at the bottom of a screen loses its tail.

The mark is written in two places, because it is earned after the record it
belongs to was stamped. `endlessNoteZoneReached` stamps zone and mark together as
a zone is entered, before it is flown, so `endlessMarkRecordCustom` writes it
after the fact. That one tests ownership against `endlessBestZoneAtRunStart`, not
against `endlessRunDepth`: a stamped record is always one ahead of the depth
while its zone is being flown, so a depth test rejects exactly the record the run
just set. Nothing clears a mark in place; it goes away when an unassisted run
sets a deeper record.

### Death, retries, and effects

Relaxed mode opens `JE_endlessDeathMenu` over the frozen playfield. Standard and
Hardcore use GAME OVER and lock the pause menu after the fatal hit.

Both that menu and the run-over summary ramp the track away with `MusicFadeOut`
(loudness.h): init when the screen is up, tick it from that screen's wait loop,
finish before leaving so a screen dismissed mid-ramp cannot strand the master
volume down. It exists because `fade_song` takes six seconds on the MIDI backends
and a backend-dependent time elsewhere, which is too vague to use as a cue. The
paths into the summary deliberately leave their track playing for it, so do not
put a `fade_song` back in front of `endlessOnRunEnd`.

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
path. Only a ship change restores armor. In Arcade, player two remains the
Dragonwing role, so its sprite and armor do not follow an edited hull ID. Online
Campaign gives both players the selected full ship.

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

With the arcade Life Boost tweak enabled, shield and armor ceilings scale from the
hull's one-life values to 28 units at 11 lives. The integer accessors are
`arcade_armor_max`, `arcade_shield_max`, and `arcade_rescale_to_lives`.

`player[].lives` aliases a weapon-power field inside `PlayerItems`, and which bay
it aliases is `player_lives_port()` (see the Separate arcade section). Life changes
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
Arcade rules remain keyed to player number because player two is the Dragonwing.
Online Campaign gives both slots the complete one-player ship model.

The joiner's initial slot is provisional in an in-game lobby. The host's choice
settles both slots. Command-line netplay retains its historical equal-slot
conflict behavior.

### Online campaign mode

`coopCampaignMode` is the online-only rules flag. `arcade_rules_active()` and
`split_arcade_mode()` keep the established one-player, local two-player, and
linked Online Arcade branches unchanged. The host publishes game type, episode, and
difficulty in `PACKET_CONNECT`; the joiner validates the same values against
`PACKET_DETAILS` before the session begins. Changing those fields requires a
`NET_VERSION` bump. The joiner is not prompted to accept them: `PACKET_DETAILS`
only arrives once the host has already left its start menu, so a prompt there
would stall a session that has begun. The connect-time host name and game type
are shown on the joiner's waiting screen instead.

Both peers simulate both complete ships. State that was historically held in
single-player globals, including generator charge, shot repeat counters,
sidekick attachment, special cooldowns, and Zinglon state, lives in each
`Player` during Campaign and is part of the rollback registry. The active ship's
state is loaded around its movement step and saved immediately afterwards.

### Separate arcade ships

`arcade_separate_mode()` is Online Arcade's second shape: two personal
single-player arcades sharing one level. `dual_ship_mode()` is the union of it and
the two co-op modes, and it is the condition every per-ship path keys on, from the
`coop_ship_runtime_load` / `save` swap and the specials in varz.c down to the
superbomb row and the score labels. `split_arcade_mode()` keys the opposite half:
the docking, the Dragonwing, the split HUD picture, and the level-script events
that skip a block for two players. `arcadeSeparateShips` is the stored lobby
preference; `arcadeSeparateMode` is the session flag it arms, packed as settings
bit 11 and stashed by `network_settings_stash` so leaving a session cannot leave
the next local two-player arcade in the Separate shape. All three Arcade tweaks
are host-authoritative: `arcadeLifeBoost` on bit 7, `arcadeRandomBalls` on bit 8
and `arcadeRearGunScale` on bit 12, each stashed and restored with the rest.

Separate arcade starts both ships on the Stalker (ship 8), the same ship
`newGame()` hands a solo arcade run, from one copy of player one's arsenal.

Two things differ from co-op rather than following it. Difficulty: the linked pair
adds a step because two players concentrate fire on one hull, and Separate arcade
does not, so the host's bump and the joiner's matching subtraction both key on
`dual_ship_mode()`. Life counters: `player[].lives` aliases a weapon-power byte,
the Dragonwing's rear bay for the linked pair and each ship's own front gun in
Separate arcade. Every binding site reads `player_lives_port()`, including
`rb_fixup_player_lives` in the rollback registry, or a restore would hand ship two
a different counter than the one the level started with.

`rollback_state_hash` keeps its legacy pre-co-op projection only outside a
dual-ship session. The block that projection skips (`Player.generator_power`
through `Player.x`) is live state in Separate arcade exactly as it is in co-op, so
the gate is `dual_ship_mode()`; single-player replay fixtures are unaffected.

### Online Timed Battle

Arcade's third shape, and the lobby's Mode row is where all three live.
`network_timed_battle()` is the pair `network_game_type == NETWORK_GAME_ARCADE
&& network_host_timed_battle`; picking it arms `arcadeSeparateShips` with it, so
nothing downstream asks about the ships twice. `network_host_battle_level` is
which of the three battles, and it becomes `timeBattleSelection` for the session.

Both ride the settings block rather than fields of their own -- the shape on bit
13, the level minus one in bits 14-15, the last three the `Uint16` had. The
level is not the episode: a battle is reached through the episode holding it
(`network_timed_battle_episode`, 1 for the first and 5 for the other two), which
`networkStartScreen` initialises, so it travels to the joiner as the episode it
is in `PACKET_DETAILS` and the episode script's `]T` jump list picks the section
out of it. The lobby's Episode row rebadges itself Level and cycles the three
battle names, the way Difficulty rebadges itself Variant for SuperTyrian.

Nothing in the level scripts needed changing. `timedBattleMode` already gates the
clock (event 84), the enemies that hatch out of other enemies (event 85), the
silent countdown and the `]T` jump, and it was already in the rollback registry;
`timeBattleSelection` is not, and does not need to be -- it is settled before the
session and constant through it, like `episodeNum`. What was single-player was
the scoring around them. `JE_endLevelAni` pays the time bonus to both purses (one
clock, one level) and gives each ship its own lives bonus, and its layout splits:
solo keeps vanilla's rows with the ship parade moved onto a row of its own,
clear of the total it used to be drawn through, and a race prints two named bonus
lines under the shared time bonus instead, having no room for two parades.

A Timed Battle is never offered Load Game -- nothing carries into a scored run
against a clock -- and it ends on `JE_timedBattleResult` rather than
`JE_highScoreCheck`: name entry blocks on a keyboard the other machine cannot
see, and the board is indexed by battle level with one score per row. Both purses
are settled simulation state and both names came out of the handshake, so the two
machines compose the same scoreboard without a packet passing. It is reached from
both exits, the clock's `]q` and the both-dead path, and that second one arrives
straight from the level with the pillarbox off, so the screen sets and restores
`set_menu_centered` itself.

`networkTimedBattleReady` is the other both-ready barrier, on the same
`network_ready_publish` / `network_ready_peer` pair as the Destruct title and for
the same reason twice over: the joiner has seen nothing but the details list, and
a race is scored from the first tick, so a machine that started while the other
was still reading would bank a free lead.

### Online SuperTyrian and Super Arcade

`network_game_type_is_super()` names the two one-player rulesets flown online.
Both give each player a complete ship, so `networkStartScreen` arms
`arcadeSeparateMode` for them directly rather than from the settings block: that
bit carries the host's Arcade preference, and these two game types settle the
question themselves. The lobby hides the Mode and Host Flies rows for them.

SuperTyrian has no difficulty ladder. Its two variants ride the same
`network_host_difficulty` field every other type reads as difficulty, because that
is what they are: `newSuperTyrianGame` reads Scroll Lock and picks between
`DIFFICULTY_LORD_OF_GAME` (Standard) and `DIFFICULTY_SUICIDE` (Scrollock). The
lobby row renames itself to Variant and cycles those two, and `PACKET_DETAILS`
already carries the value. Both ships take the Stalker 21.126 and the Atomic
RailGun, and the twiddle detector keys off the `superTyrian` flag with per-player
`SFCurrentCode` / `SFExecuted` rows, so each player works their own combos.

`networkDifficultyBump()` is the one place the host's addition and the joiner's
subtraction are decided, for every game type. Super Arcade adds the step
`newSuperArcadeGame` adds solo; SuperTyrian and the two Separate shapes add none;
the linked arcade pair adds one. The unit suite pins both halves against each
other per type.

Super Arcade lets each player pick their own hull, and they may match.
`networkSuperArcadeShipSelect` is its own loop rather than menus.c's
`episodeSelect` because it has to service the link while it is open and keep
drawing after this player has chosen, so waiting for the partner is visible.
It reaches the screen straight out of the handshake's `fade_black`, so it fades
the palette in on its first composed frame like every other menu; without that the
whole screen draws under a black palette. Its layout is declared in tyrian2.h
(`SA_PICK_*`, `sa_pick_name_x` / `_y`) and the unit suite measures the real ship
names from the data file against it.

The pick is announced rather than dictated: `PACKET_SA_SHIP` carries sender and
ship, `network_sa_ship_publish` sends this machine's and `network_sa_ship_peer`
retires the peer's. It is reliable, so a lost announcement is retransmitted, and
it is listed in `network_recv_one`'s reliable switch, without which it would be
queued nowhere, never acknowledged, and the pair would deadlock. Both ends clamp
to 1..SA: the value indexes `SAShip[]` and a `SAWeapon` row on both machines.
Both machines then equip both ships from the settled pair.

Which Super Arcade ruleset a ship flies is `items.super_arcade_mode`, read through
`player_sa_ship()`, because online the two ships differ. Every `SAWeapon`,
`SASpecialWeapon` and `SASpecialWeaponB` read is per ship. A colour ball carries a
slot, and `player_sa_ball_weapon()` looks that slot up in the collector's own
arsenal, so one red ball hands each player the gun their own hull keeps there.
The byte is cleared by `JE_initPlayerData` with the rest of the loadout: left over
from a previous Super Arcade run it would hand a plain arcade ship that ship's ball
table and paired special.

### The top-of-playfield special block

The special-weapon icon and the ready light beside it are drawn from two files
(`JE_inGameDisplays` and `JE_doSpecialShot`) straight into playfield space, with
no clipping and no precedence of their own, so their geometry is declared once in
mainint.h and read by everything that has to agree with it. `hud_special_block_shown`
names the one ship that gets a block, the ship this machine draws the HUD for, and
that ship's name and lives drop to `HUD_LIVES_Y_SPECIAL` to clear it while the
other ship's row stays put. Player one's block sits inside the left playfield edge
and player two's mirrors it against the right, so a Separate-arcade joiner cannot
paint its special over player one's row. `hud_top_left_right_edge` and
`hud_top_right_left_edge` report the whole block, light included, because a centred
TOP boss bar shrinks to those edges and would otherwise draw straight through it.
The unit suite checks the rectangles do not intersect from both machines.

The shop uses `JE_shopPlayerIndex()` for local presentation and purchasing.
`PACKET_SHOP_SYNC` publishes an ordered transaction input carrying the owner's
resulting cash, loadout, weapon mode, and route after each commit. Each owner is
the sole writer of that state. Save requests use a two-way request/acknowledgement
checkpoint, and the final shop rendezvous waits for both players. Modal shop
loops continue servicing acknowledgements and keep-alives.

Each player leaves the outpost by choosing a level, and the two picks can differ.
The host's is authoritative, but it is held rather than applied on arrival:
`network_shop_adopt_host_level()` writes `mainLevel` and `jumpSection` only after
the local player has also finished, so the host committing cannot end the other
player's outpost visit mid-purchase.

Credit sharing is `coopSharedCredit`, packed as settings bit 9 and adopted into
`coop_set_session_shared_credit()`, so both machines award identical cash.
`player_award_pickup_cash` and `player_award_kill_cash` are the only two credit
paths. The shared branch pays both players, and routes this machine's own share
through `endlessCashCredit` rather than adding to the wallet directly. Campaign
has no ledger to reach, but Endless does: paying past it made every shared credit
undeclared drift, so the audit warned on each one and the run summary filed the
whole run's income under "other" instead of the source that earned it.

### Leaving the outpost

`shopLeaveOutpost()` runs inside the shop loop rather than after it, so a
withdrawn commit only has to clear `jumpSection` for the loop to reopen the
outpost. `ShopOutpostRoute` is captured before anything can pick a level and is
what the withdrawal restores, along with a staged debug-browser pick.

`shopCampaignRendezvous()` settles the commit in two steps because it must be
impossible for one machine to leave while the other is going back. Step one
publishes DONE and accepts Esc until the peer's DONE arrives; step two publishes
LOCK and waits for the peer's. A machine may withdraw only before it has seen the
peer's DONE, so the retract window closes for both at the same event, and the
ordered channel makes a received LOCK proof that no withdrawal can still follow.
DONE and LOCK are state bits carried by every `PACKET_SHOP_SYNC`, not events, and
the receiver assigns rather than latches them.

Step two is also the one place both machines are guaranteed to be draining the
inbound queue, which is why `network_custom_weapon_publish()` is called there.

Those bits are state, so a machine has to be told them again whenever its view is
reset. `network_shop_begin()` clears what it knew about the peer, which is right
for a fresh visit and wrong for a partner who committed while this machine was
still on its way to the outpost: that commit was announced exactly once, into the
reset, and the two then waited on each other with nothing left to say. Two things
close it. `SHOP_SYNC_HELLO` rides the packet `network_shop_begin()` sends and asks
the peer to restate where it stands (its reply carries no HELLO, so it is one
exchange, never a volley). `network_shop_keepalive()` re-announces every 400ms
from any loop that waits on the peer, which also covers a restatement that was
dropped. It is gated on `network_is_sync()`: the reliable queue is 16 deep and
overflowing it ends the session, so a partner parked in a screen that does not
drain the queue is never beaten at.

A restatement covers a view that was reset, and not a peer that has already left.
Once a machine reaches the departure handshake it stops announcing DONE and LOCK
altogether, so a partner still waiting on either would wait for good. Both waits
therefore treat `network_shop_departure_pending()` as the rendezvous being over:
the ordered channel puts that packet behind the announcements, so anything not yet
seen was missed rather than still coming. The packet is left at the head, because
the handshake immediately below is the one meant to read it and `network_update`
there throws away the very thing that handshake then waits on. Endless is where
this bites: the non-charting player waits for a sector before committing, so the
two machines reach the rendezvous a long way apart and the second one can find the
first already gone.

Every outpost purchase publishes, including the E-Shop and the perk pick, which
previously only reached the peer at the rendezvous.

### Session flags arm on both sides

Every flag the settings block carries must be armed by
`network_arm_local_session()` from the host's own config and adopted by the
joiner from the block, the same set in both places. A flag the block carries
but the arm misses runs the two simulations on different rules from the first
place it pays out. Double Earnings was exactly that: the joiner adopted it, the
host never armed it, and every pickup desynced the wallets by its own value,
one desync-recovery stall after another for the whole session. The unit suite
pins host arming against joiner adoption with stale session values in place.

The same audit caught Expert Mode. Its flag rides the save record and its six
tunables ride each machine's own `opentyrian.cfg`, and nothing published either
at connect time -- only `PACKET_DEBUG_SYNC` did, and only when somebody opened
that menu and changed something, since it sends on a diff against its own
baseline. Two players who had each set a Boss HP multiplier once therefore
started a campaign fighting bosses with different health. They travel in the
settings block now. The flags word at byte 4 was full by then (Timed Battle took
the last three bits), so the block grew a tail at byte 24: a second flags word
with fifteen bits still spare, then `NETWORK_EXPERT_SLOTS` Uint16 slots written
straight off the `expertSettings[]` table, so a seventh tunable needs no wire
change. Bytes 0..23 kept their offsets. `clamp_expert_settings` runs on adopt
as well as at config load, so a hostile packet cannot name a 65535x multiplier.

Two things that look like the same class of bug and are not, both checked: the
Extra Sparks toggle and the per-weapon spark cap only move where `JE_doSP`'s
ring buffer wraps -- its loop bound is `num`, so the `mt_rand` draw count is
identical either way -- and Unused Shop Sprites only rewrites `.itemgraphic`.

Payouts must go to deterministic wallets. `endlessCashCredit` pays the local
keyboard's wallet and is for outpost-time, self-only flows; anything paid
during or at the end of a level has to name the player index and run on both
machines (`endlessAwardEliteKill`, `endlessApplyLevelPayout`, the kill and
pickup paths). The zone payout pays every participating ship its own interest
and clear bonus on both machines for that reason.

The flip/spotlight code (`JE_deriveStarShowSpecial`) runs identically online;
network games used to clear it wholesale, which disabled Topsy Turvy, scripted
inverted-control levels and the light cone for every online session.
`smoothies[]` and `starShowVGASpecialCode` are rollback-registered since the
inverted-control flag reads back into input handling; the replay fixture
hashes were regenerated for that registry addition after verifying the sim
bit-identical with the registration removed.

### Keep-alive audit

The peer declares this machine dead after `NET_TIME_OUT` (16s) without traffic,
and only `network_check()` sends the keep-alives, so every screen a player can
sit on during an online session must reach it at least every few seconds. Of the
frame-service helpers, only `wait_input`, `wait_noinput`, `menuWaitForInput`
(menus.c), `lobbyWaitForInput`, `JE_outTextGlow`/`JE_outCharGlow` and the
`NETWORK_KEEP_ALIVE()` macro service the network. `service_SDL_events`,
`service_wait_delay`, `wait_delay`, `JE_showVGA`, `shopWaitFrame` and
`menuWaitWithSmoothCursor` do not; a loop built only on those starves the link.
`network_shop_keepalive()` is not a substitute either: it only sends, rate
limited, and never drains.

Audited screens, all serviced as of this audit: the lobby menus and their text
entries (pre-session), both connect waits, the outpost and every submenu of it,
ship specs, save/load and both confirm dialogs, the options pages and both
input-capture screens, the custom weapon editor, the debug menus, the in-game
menu and its waits, the help overlay, the Endless death prompt and run-over
tally, both chunked transfers, the level rendezvous, level text, level-end and
episode-end screens, the credits, and the halt paths. Five of those starved the
link until this audit and were fixed by adding `NETWORK_KEEP_ALIVE()` (paired
with a `network_shop_pump` drain where the outpost protocol can be active):
the custom weapon editor, the quit confirm dialog, the keyboard and joystick
capture screens, and the Endless run-over tally.

When adding a new screen reachable online, service the connection in its wait
loop and extend this list.

### Custom weapons online

Both machines fly both ships, so each player's design has to exist on both.
`CUSTOM_WEAPON_OWNERS` reserved sets are claimed by `customWeaponClaimSlots()`:
one weapon port, one sidekick option and one scratch weapon range per player
index. Ownership is by player index, never by local versus remote, or the two
machines would disagree about which port a `PlayerItems` id names.
`customWeaponPort` and `customSidekickSlot` stay as the editor's own, resolved
through `customWeaponLocalOwner()`. The same rule binds sim-side tests: the
Endless record flag is registered state, so the shot path asks
`customWeaponPortIsCustom()` — every owner's port — never `customWeaponPort`,
which names a different port on each machine.

Reloading item data wipes those slots, and a network game reloads at every level
start, so `customWeaponInit()` ends in `customWeaponMaterializeAll()`, which
rebuilds the peer's adopted design as well as the local one.

`customWeaponSerializeDesign` writes only each level's populated bullet slots,
which keeps an ordinary weapon near two kilobytes against a
`CUSTOM_WEAPON_WIRE_MAX` worst case of about 45. `PACKET_CUSTOM_WEAPON` carries
it as numbered chunks. Transport delivery is not enough: the inbound queue is
`NET_PACKET_QUEUE` deep and only advances as the application consumes it, so the
receiver answers a completed generation with a chunk count of zero and the sender
resends the generation until that answer arrives. A per-generation chunk bitmap
makes a resend idempotent. A design that does not decode is refused rather than
compiled, and both refusal and non-delivery are recorded in the net log.

`MENU_LIMITED_OPTIONS` is the options page with Load Game removed, built from
`menuInt[3]` by `configure_options_sens_menu()` rather than from the data file's
shorter DOS network menu. Its rows therefore sit one higher than the offline
page's; `options_row()` and `options_full_row()` are the only place that offset is
written down, and every row is named by the `OPT_*` enum. Sub-screens reached from
either page (`MENU_LOAD_SAVE`, joystick, keyboard, mouse) have `menuEsc` pointing
at `MENU_OPTIONS`, so the Esc handler redirects to the online page when
`isNetworkGame`.

Campaign HUD rendering is local: each machine draws the one-player sidebar for
its controlled ship. Names and cash totals for both players are drawn inside the
playfield. Online Arcade retains the split gauges and link presentation.

Both ships run the whole per-player simulation on both machines, so anything that
paints the shared HUD from inside it has to ask whose strip it is rather than
paint for whichever player it is simulating. `hud_sidekick_player_index()` (varz.c)
is that rule; the sidekick ammo gauge goes through it, as does
`JE_drawOptionsHUD()`. Getting it wrong puts the other player's magazine on your
HUD, under sidekick icons you may not own.

### Endless online

`coopEndlessMode` is the Endless half of online co-op; `coop_mode_active()` is
either kind and is what the shared plumbing (ship runtime, HUD, pickups, shop
netcode, save tag) keys off. `coopCampaignMode` now means the Campaign lobby
alone.

Split of responsibility inside a run:

- Run-wide and derived identically on both machines from the seed, depth and
  difficulty: the course slate, the sector's modifiers, zone depth and kills,
  milestones, deferred Star Charts and Breakthrough picks. Nothing about these
  travels.
- Per player, owned by that player's machine and mirrored to the peer on every
  `PACKET_SHOP_SYNC`: wallet, gear, superbombs, Reinforce tier, revive token,
  pending sector purchases, Sabotage charges, Loan Shark tax, The Long Con,
  shop prices and the perk row. `endlessPackPlayerBlock` /
  `endlessUnpackPlayerBlock` are that block; `ENDLESS_PLAYER_BLOCK_SIZE` is its
  fixed width and the receiver checks the packet is long enough before reading.
- Local only, never sent: `itemAvail` (each machine shows its own player's
  shelves) and the cash ledger, which follows `endlessEconomyIndex()` and so
  tallies this machine's own ship. `player_credit_cash` routes through the ledger
  on that same index: gating it on player 1 outright had the joiner booking its
  partner's earnings into its own wallet and paying its own straight past the
  ledger. Solo the two indices are the same, which is why it read as correct.

Perks are personal: a stack works on the ship that took it and no other. They are
stored as `endlessPerkTakenBy[2][PERK_COUNT]`, one row per player, and every write
goes through `endlessPerkGrant` so neither machine clobbers the other's row.
`endlessPerkEffective(p, id)` is what effects read, either through the fx-ship
context (`perkFx`, the ship the current effect belongs to) or through the local
economy index (`perkMine`, for shop pricing, offers and menu text).
`endlessPerkOwned` survives as the capped sum for diagnostics and the legacy save
field; no gameplay path reads it.

Anything read at the outpost belongs to the buyer, so it takes `perkMine`:
`endlessPerkTotalOwned` (the extra-perk surcharge and the buyout's own surcharge),
`endlessPerkShopCostBp` (Financier's discount, charged to the same player
`endlessShopTaxPercent` taxes), the slate's offer pool and its Owned counts.
Anything a level pays or a shot does takes `perkFx` and runs inside a context that
names the ship: `endlessAwardEliteKill` sets it to the killer,
`endlessApplyLevelPayout` to each payee in turn, `endlessPerkDeclineBonus` to the
player taking the buyout, `endlessApplyHullBonus` and `JE_resetPlayerOptions` to
the ship being outfitted. `endlessAdrenalineActive` is personal on both halves: the
fx ship's own stacks, armed by the fx ship's own hull.

Two perks act on shared screens instead of on a ship: `endlessPerkSurveyorRoutes`
reads `endlessChartingPlayerIndex()`, the seat both machines derive identically, so
one slate cannot be widened differently on the two sides; `endlessPerkRadarActive`
reads the local player, because the help text it adds is drawn locally.
`endlessPerkGetOwned` / `endlessPerkSetOwned` (the debug screen and the
campaign-mods config) both name the local row, so the pair round-trips.

Outpost draws (stock, rerolls, gambles, perk slates) run on
`endlessRandFor(player)`, forked from the run seed by `endlessReseedPlayers` at
each outpost. The structural stream `endlessRand()` keeps generating the course
slate alone, so a reroll can never shift a later zone's layout for either player.

The sector is committed at the rendezvous, not at the pick: `endlessCoopCourse`
holds the charting player's index, rides the shop packet in the field Campaign
uses for `mainLevel`, and both machines call `endlessSelectCourse` on it once
both are done shopping. Folding earlier would use a stale copy of the other
player's purchases. `endlessLocalPlayerCharts` answers the Host / Guest /
Alternating / 50-50 setting; the coin flip derives from `endlessSplitMixSeed` of
the depth rather than drawing, so it cannot depend on how much either player
shopped.

The player who is not charting waits for that index *before* committing, in
`shopEndlessAwaitCourse(true)`: Esc stays a way back into the outpost for as long
as the wait lasts, and a session that somehow agreed neither of them was charting
is something both players can walk out of instead of a screen with no live key.
A locked outpost and a loaded game skip the commit entirely, since both arrive
with the route already armed. The un-escapable form of the wait remains as the
backstop for reaching the rendezvous with no index at all.

That backstop, and any other outpost wait that outlives the peer's departure, has
to leave the reliable queue alone when `network_shop_departure_pending()` is
true. `network_update()` on a `PACKET_WAITING` at the head throws away the packet
the level-start handshake three lines later is the one waiting for, and that
handshake has no timeout: the machine that ate it sat on "Waiting for other
player." forever while the other loaded the level. The two-peer fault test drives
the whole departure for exactly this reason; removing the guard fails it.

`endlessPlayerDowned[]` latches a ship that lost its hull while its partner flew
on. It gates the reactive dangers and the per-tick effects, and
`endlessReviveDownedAtOutpost` clears it. It is registered rollback state: an
unregistered latch would resurrect or re-kill a ship across a correction. Both
ships down is an ordinary death, and in Relaxed the host publishes its death-menu
choice through `network_endless_death_sync` so both machines take the same branch.

A blank seed in the lobby means "roll one", the same as leaving the solo seed
screen empty. `network_endless_session_begin()` settles it host-side before the
connect packet carries it, into `network_endless_session_seed` rather than the
lobby field, so the row stays "(random)" and the next session rolls again instead
of silently repeating this one. Sending the blank through was hashing the empty
string, which dealt every online run the same zones.

Resuming an online run streams the host's sidecar record over
`PACKET_ENDLESS_RUN`, chunked the same way a custom weapon design is, and the
joiner adopts it with `endlessRunAdopt`. The same packet carries the death-menu
choice under a sentinel chunk count of 0xffff.

A packet type only reaches `packet_in[]` if `network_check()` names it in the
reliable-delivery switch. `PACKET_ENDLESS_RUN` was missing from that list, so
every packet on the channel was dropped there unread and unacknowledged and the
whole thing was write-only: the resume transfer, the "I have left the level"
notice, and the death-prompt choice, which is why picking Return to Outpost left
the other player waiting on a screen for an answer that could never arrive. Add
new types to that switch and to nothing else; the sender side looks perfectly
healthy without it, ack backlog included.

The prompt is read for as long as the player takes, so its frame has to drain the
queue and not merely acknowledge: the other machine's wait announces itself, and
a receive queue nothing advances fills and then silently drops what follows.
`JE_endlessDeathMenu` pumps for that reason. The announcement itself is sent once,
with a slow re-send behind `network_is_sync()`; repeating it several times a
second stacked unacknowledged copies until the waiter's own outbound queue
overflowed and ended the session.

Endless save v21 appends the second player's half, the course-chooser setting,
the alternating-turn flag, both perk rows and both RNG streams. A v20 or earlier
record loads into slot 0 with the second slot zeroed and the perk rows rebuilt
from the effective stacks, so a solo run resumes unchanged.

`enemy_logical_death` carries the killer (0/1, or `ENDLESS_KILLER_NONE` for a
death nothing can claim, such as a despawn), taken from the shot's
`playerNumber` at both kill sites. `endlessCountKill` uses it for the Combo Feed
setting and `endlessAwardEliteKill` for the bounty, which then follows the same
Shared / Individual credit rule as any other kill cash. An unclaimable kill feeds
both streaks, so neither ship is punished for it, and its bounty pays player 1:
"the local player" would have paid a different wallet on each machine.

Double Earnings rides settings-flags bit 10 and `coop_earnings_are_doubled` gates
itself on Individual credit, so the flag can be stored On without doing anything
under Shared. It covers combat income whole: `player_award_pickup_cash`,
`player_award_kill_cash` and `player_award_bounty_cash` all double, because a
split take is the same split whatever earned it. Zone clear bonuses and bank
interest stay at face value; they are not collected in the field and doubling them
would compound with the Financier and Scavenger rates already applied there.
Combo Feed rides a byte in the connect packet's Endless block (widened to 3 +
seed, NET_VERSION 17).

Personal sector effects have their own mask. `endlessActiveMods` is what the
sector charted, and `endlessPlayerMods[p]` is that plus whatever player p bought
for themselves; `ENDLESS_PERSONAL_MOD_MASK` is the split, and
`endlessApplyPurchasedMods` performs it once when the course is committed. The
kill-fire windows (`endlessTurbodriveTimer`, `endlessComboKills`,
`endlessOverdriveStacks`) are per player too, so two ships can fly different
drives at once. A kill feeds both windows; only a ship whose own mask carries a
drive gets anything from it.

The Endless per-tick work is split in two. `endlessGameplayTick` advances the
run's own clocks and stays on player 1; `endlessPerShipTick` (mainint.c) is
everything a ship does for itself, and co-op runs it for BOTH ships. Gating that
half on player 1, as it originally was, left the second ship with no drive, no
Rapid Cyclers and no gravity while every rule function still answered correctly
in isolation. It is a named function rather than an inline block so the test
suite can call it and cover the gate itself.

Opening Salvo is per ship for the same reason: the charge belongs to the gun
that sat idle and is spent by the gun that fires, so `endlessSalvoIdle` and
`endlessSalvoWindow` are `[2]` and `endlessOpeningSalvoTick` walks the ships.
Its green on the generator gauge is the same state read for the ship THIS
machine flies: both HUD branches go through `local_salvo_gauge_percent`
(tyrian2.c), which names the local ship rather than trusting the fx context the
last simulated ship left behind. The co-op branch used to pass a hard zero, so
the perk worked online while its gauge never lit. Every other HUD read that
picks a row follows the rule — the endless armour bar's rollover layers and the
damage flash under them both come from `gameplay_local_player_index()`.
The reactive timers follow the same rule: the Aegis Gate cooldown, the Static
Discharge recharge lockout and the Countermeasure Suite cooldown are all `[2]`,
indexed by the fx ship, so one hull's block, hit or burst never spends the
partner's. The dual-ship shield-regen loop in tyrian2.c names each ship as it
computes it, which is what points the Shield Matrix interval and the lockout
read at that ship's own row.

The in-game menu's Quit means "back to the outpost" in Endless, for both
players: `endlessCoopPeerQuitLevel` sets `endlessQuitToOutpost` on the peer,
which is the same thing the local press does. Campaign and Arcade keep treating
it as the end of the session, and only they set `haltGame`, which the level
warning screen reads as "stop the game" and an Endless relaunch would run into.

Three paths read the peer's `PACKET_GAME_QUIT`, and all three have to agree that
a quit is not a clear, or the machine that stayed banks the zone and deepens
while the one that quit reopens the same outpost. From there the pair is a zone
apart for the rest of the run, charting from slates that no longer match. The
rollback stall's copy of the rule is `nrb_peer_left_level()`, named rather than
inlined so the suite can drive it.

Which ship an effect is being computed for is `endlessFxPlayer()`, set by
`endlessSetFxPlayer` at the few places that work through the players in turn:
`JE_playerMovement` (cadence, tint and the ship blit), `JE_playerDamage`,
`player_shot_create`, the shot-damage site in tyrian2.c (the shooter, from
`playerShotData[].playerNumber`), and the two HUD readouts (the local ship). It
is 0 outside co-op, so single-player behaviour is untouched.

Zone records are kept per crew size: `endlessBestZoneDiff[table][mode][slot]`
with table 0 solo and 1 co-op, `endlessRecordTable()` choosing the one a run
writes. The co-op half lives under the same config keys with a `_2p` tail, so a
config written before the split reads as an empty co-op set. Online co-op
Campaign has its own board in `coopCampaignScores` (config section
`coop_scores`), one best run per episode, written by `coopCampaignScoreNote`
without a name-entry dialog: the lobby already knows both names, and a modal at
that point would leave the other machine on an unpumped screen.

`JE_itemScreen` performs the level-start handshake (both machines publish
`PACKET_WAITING`, then `network_state_reset` and a resync) on the way out of the
outpost. A path that reaches a level WITHOUT passing through it owes the same
handshake, or one machine starts simulating while the other is still loading and
the peer's stall gate is the first thing to notice: Restart Zone off the Endless
death menu and the ENGAGE debug-browser return both call
`network_level_rendezvous` for that reason.

Two rules keep a session from wedging when one machine leaves a level first.
`nrb_stall_pump` treats a `PACKET_WAITING`, `PACKET_DETAILS`, `PACKET_GAME_QUIT`,
`PACKET_SHOP_SYNC` or `PACKET_ENDLESS_RUN` at the head of the reliable queue as
"the peer is between levels" and ends the local level out of band; without it a
peer that is alive (keep-alives) but gone holds the stall open for the absolute
wedge cap. `JE_endlessDeathMenu` sends keep-alives while it is read, and the
joiner waiting on the host's choice announces itself with the run packet's
`0xfffe` sentinel so the host is released even if it is still finishing the
level. A peer that quits through the in-game menu sets `endlessCoopPeerQuit`,
which the level-end path checks before the death branch: `playerEndLevel` on its
own would read as this player's death and print the run-over summary.

### Reliable channel

The reliable UDP layer follows these rules:

- A receive error does not prove the link is dead. Wait loops sleep on
  `network_check() <= 0`.
- Packet fields are read only when the received length covers them.
- `network_is_sync()` is `network_ack_backlog() == 0`: every reliable packet
  sent has been acknowledged and removed from the queue. The acknowledgement
  high-water mark is not usable for this. It reads ahead of an unacknowledged
  head whenever the ack for it was lost, and senders gated on it keep queueing.
- The retry timer belongs to the queue head and starts when a packet becomes
  it. Restarting it on later sends postpones the head's retransmission for as
  long as new sends keep coming, so a 250ms keepalive cadence would starve
  retransmission entirely.
- A retry resends everything still unacknowledged, oldest first. Resending only
  the head drains a stale backlog at one packet per interval. The interval
  follows the measured round trip (twice the keep-alive ping plus margin,
  clamped to [120, `NET_RETRY`] ticks).
- A full outbound window in `network_send` is backpressure, not a dead link.
  The sender services the socket until a slot frees, preserving the prepared
  packet across the wait because acknowledgement and ping replies overwrite
  `packet_out_temp`. Only a peer that frees nothing within `NET_TIME_OUT` is
  treated as lost.
- An acknowledgement is trusted only for a packet still outstanding. Anything
  else drifts `queue_out_sync` off `last_out_sync`, and the send path reads
  that as an overflow.
- A reliable packet past the end of the receive window is dropped WITHOUT an
  acknowledgement. Acknowledging it tells the sender it was delivered, it is
  never sent again, and the window only advances as packets are consumed, so it
  can never come back: permanent one-way deafness. The slot arithmetic is
  signed, so a packet from behind the window (consumed already; the ack was
  lost) still re-acknowledges. `network_window_overflow()` counts the drops.
- The same rule extends to a late `PACKET_CONNECT` on a connected session (the
  handshake ends with a deliberate trailing connect, and `connect_reset`
  retries add more). One that arrives AHEAD of the window head -- reordered
  past a younger packet -- must be placed into its sequence slot like any
  reliable packet, discarded only once it surfaces at the head. It used to be
  acknowledged and dropped on the spot, which left a hole at the head of the
  window: an acknowledged packet is never resent, `network_update` cannot
  advance past an empty head, and every later reliable packet parked behind
  the gap for good. Both peers idle with keep-alives flowing (each side's own
  outbound is fully acknowledged), so nothing times out. The wire suite's
  Super Arcade scenario was the only place with an untimed dependency on the
  window right after the handshake -- the ship-pick exchange -- which is why
  the hole surfaced as a rare scenario-18 wedge; the arcade/SuperTyrian
  scenarios rode the same hole out through the level rendezvous's 30-second
  escape. Heads holding a stale connect are consumed by `network_update`
  waits, `network_sa_ship_peer`, and `qa_net_drain`.

The sender keeps at most half of `NET_PACKET_QUEUE` outstanding during a resync.
This prevents transport acknowledgements from filling the receiver's inbound
queue before the application consumes the chunks.

### Rollback input stream

`PACKET_INPUT` has a 48-byte header and up to sixteen redundant 14-byte input
records. It is unacknowledged and idempotent.

- `network_check()` drains up to `NET_DRAIN_MAX`; callers do not add another
  drain loop.
- The level epoch separates frames from different levels.
- Menu request bits are processed from received truth outside the simulation
  misprediction test.
- Received canaries queue until the local frame can be compared.
- The pool hash covers player shots, enemy shots, explosions, repeating
  explosions, and the sound queue.

An in-game menu request schedules frame `f + NRB_REQ_LEAD`, and both peers stall
there until it is final.

Pause is offline-only. `JE_pauseGame` returns immediately under `isNetworkGame`,
the level loop swallows the key and the focus-loss edge without raising a
request, and neither `RB_REQ_PAUSE` nor state-packet request bit 0 is set or
honoured. Both stay reserved so the wire layout is unchanged. A halt on one
machine alone strands the other, and losing window focus is not consent to it.

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

Function-local `static` state in a simulation path is a determinism bug twice
over: a rollback cannot restore it, so every re-simulated frame advances it
again, and the two machines re-simulate different frames. Four of them each
desynced online Endless in its own way before the registry picked them up: the
gravity fractional carry (an extra pixel of pull per resim), the Rapid Cyclers
and Rapid Recharge fire accumulators (a gun quickened a tick apart), and
`endlessCountKill`'s multi-part dedup guard, which made a rolled-back kill
unrepeatable so the Turbodrive combo feed starved on one machine. All four are
file-scope and registered now (`endless.gravityCarry*`, `endless.perkFireAccum`,
`endless.perkCdAccum`, `endless.killDedup`), and the run cash ledger
(`endless.cash*`) rides with them so a re-simulated payment books exactly once.
The sweep that found them: `grep -n "static" src/endless*.c` filtered to
mutable locals; keep it clean.

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

`PACKET_WAITING` is a paired rendezvous. Menu release, shop exit, and level start
consume it in the same order on both machines. Loops that inspect packets during
a rendezvous must leave unmatched waiting packets queued.

The level-start barrier completes map and sprite loading before the simulation
fade begins. `JE_advanceLevelFade` advances the fade inside the tick so rollback
re-simulation sees the same state.

### Presentation after rollback

Presentation state stays outside snapshots and is repaired on the first live
pass:

- `textErase` decrements only on live passes, and a new message always redraws
  the message-bar background.
- Sidekick HUD changes set `hud_sidekicks_dirty` during a silent replay. The rule
  lives inside `JE_drawOptionsHUD()` and the ammo-gauge painter rather than at the
  call sites, so a new caller cannot forget it.
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
Arcade records remain compatible with local two-player. Campaign records are
tagged and accepted only while loading an Online Campaign session.

`save_record_pack` and `save_record_unpack` define the 81-byte little-endian
network record. A resumed `PACKET_DETAILS` appends that record after its normal
ten-byte prefix. The joiner keeps its own live input-device assignments.

The existing `highScore2` field carries a Campaign marker plus the weapon powers
and modes not represented by the legacy two-player record. The two `PlayerItems`
blocks and cash fields then preserve both full loadouts without changing the
fixed `tyrian.sav` layout or checksum. Shop saves first complete a bidirectional
state checkpoint so both machines serialize the same transaction boundary.

The legacy record reaches only player one's front bay and player two's rear one,
which is the whole loadout for the linked pair, where player two's rear bay is
also its life counter. Every session flying two complete ships needs the other
half, so `JE_saveGame` writes it for `dual_ship_mode()` under one of two markers:
`0xc74f` for the co-op modes, `0xc7a5` for the three dual-ship arcade shapes
(Separate, Super Arcade, SuperTyrian). They are separate because
`save_record_is_coop` is what admits a record to the Campaign and Endless lobbies,
and an arcade record must not answer to it. `JE_loadGameRecord` unpacks on either
marker, keyed on what the record says it is rather than on the current session,
and rebinds `player[].lives` through `player_lives_port()`: it is the one path
that installs a loadout without going through `newGame()`, and a resumed Separate
arcade left on the linked binding would spend ship two's rear-gun power on every
death.

The three arcade lobbies share one slot page, so `save_type_compatible` reads the
record to tell them apart: the dual-ship marker must match the session's shape,
and the two `super_arcade_mode` bytes (`save_record_sa_ship`) must match the game
type. Loading across them would resume with a loadout the session's own rules
never issue.

The host selects New Game or Load Game after connection. Network load menus stay
on the two-player page, filter unavailable episodes and mismatched game types,
and keep the peer alive. Alt+L is disabled online; Alt+S opens the shared
two-player save page except while a purchase preview is active.

An involuntary disconnect after gameplay offers a save based on the pre-level
LAST LEVEL backup. The prompt clears silent rollback state before drawing and
keeps acknowledgement traffic moving while open.

`network_shop_sync_for_save` waits on the peer's acknowledgement, and that wait
is a real synchronization point: both machines serialize the same transaction
boundary through it. It consumes shop traffic and a debug block queued ahead of
the acknowledgement, then keeps the queue moving on anything else, because the
acknowledgement is behind whatever is at the head and stopping there strands the
save. Two exceptions to that:

- `PACKET_GAME_QUIT` is left alone. It was acknowledged on arrival, so the peer
  counts it delivered and never repeats it; consuming it here means the quit
  handler never sees the peer leave.
- The wait is bounded by `NET_SHOP_SAVE_WAIT` and by peer liveness. It had
  neither, and a peer that vanished between the request and the reply held the
  game in the loop with no way out.

Breaking on *any* foreign head instead looks tidier and is wrong: the base wire
scenario's save checkpoint stops converging, because the packet at the head is
usually transient rendezvous traffic and the acknowledgement is behind it.

### Reaching a queued debug block

`network_debug_sync_pump` inspects only the head. The reliable queue is ordered
and indexed by sequence number, so nothing can be lifted out of the middle of it:
a block sitting behind outpost traffic is reachable only to a caller that also
runs `network_shop_pump`. Every wait pairs the two for that reason.

### Wire-scenario barriers

`qa_net.c` barriers are numbered announcements on `PACKET_WAITING`, completing on
"the peer has reached at least here". Scenarios may use as many as they need.
The stall that used to appear past two barriers was the reliable channel, not
the barrier design: the old retry rules let a keepalive cadence postpone the
head's retransmission indefinitely and drained a backlog one packet per
interval, and the outbound window treated fullness as a dead link. The rules
that replaced them are in the reliable-channel section above.

Wire scenario 3 (`qa_net_barrier_phases`, forty barriers and nothing else) is
the reproducer: against the old rules it failed about one run in six, one peer
stalled with a fifteen-deep unacknowledged backlog or halted by queue overflow
mid-ladder. In a live game that overflow was a healthy session dying with
"Network connection was lost" after a few seconds of one-way loss at an outpost
rendezvous. A test peer that hits a session halt exits non-zero with the halt
message instead of sitting on the "press a button" screen.

Phase announcements are re-broadcast state, not events: every qa_net wait
re-announces the highest phase this machine has reached, rate limited to one in
flight. A product wait that legitimately consumes transient rendezvous traffic
(the save checkpoint does) can eat an announcement unrecorded, and the announcer
has moved on and never says it again.

Every test-peer scenario starts its reliable sequence space at 0xFFD0, so the
Uint16 wraparound is crossed in the normal course of every run rather than
waiting for a soak to reach it.

### Gameplay wire scenarios

Scenarios 4 to 9 in `testing/network_fault_test.py` go beyond the outpost
protocols. 4 gives the joiner a skewed wire version (`--test-net-version-skew`
offsets both what the connect packet advertises and what the peer's packet is
compared against); success is both peers rejecting the handshake with the
mismatch message and exiting on their own.

Scenarios 5 and 6 fly the first Arcade level on two real peers through the
fault proxy, using the command-line start with its screens answered by test
hooks (`qa_net_gameplay_ticks`): the host auto-picks New Game, the outpost and
briefing are skipped, movement is a scripted wiggle so held-input prediction
keeps being wrong and the rollback path does real work, and the run reports a
verdict and exits at the frame limit. 5 requires zero canary mismatches and at
least one real rollback. 6 also bends one joiner frame's armor
(`qa_net_corrupt_frame`) after the snapshot, armor because the input tuples
carry positions and would repair those; its verdict requires the bend detected,
a recovery run, and the timeline being flown at the limit clean again, on
session-scoped counters because a recovery and a level restart both wipe the
per-level ones. Command-line peers have no lobby roles and the recovery
dispatch acts on the host role alone, so the gameplay test assigns player 1
the role the lobby would have.

Scenario 7 is save/resume in two stages over the same scratch directories: the
pair flies and banks the LAST LEVEL slot on exit (`--test-net-save-exit`), then
relaunches with the host auto-loading it (`--test-net-resume-slot`) so the
joiner adopts the `PACKET_DETAILS` resume form; the resumed level must fly
desync-free, which it cannot if either machine resumed to different state. This
scenario is what found the `net_last_host` clobber: the remembered lobby host
was applied after command-line parsing and overrode `--net`'s target.

Scenario 8 blacks the proxy out completely for 8 seconds mid-level, inside the
dead-link timeout; the session must ride it out and still finish clean.
Scenario 9 kills the joiner mid-level; the host must reach its own clean
"Network connection was lost" exit rather than hang. The base scenario also
prints a working-set figure after the handshake and at the finish
(`NETWORK TEST MEM`), and the harness fails a session whose memory grew.

Scenario 10 flies four sidekick mount combinations with scripted fire
(`--test-net-loadout`, applied identically on both machines; the fire buttons
are forced where the input devices would have been sampled): front pod + side
pod against a trailing pair, double front against satellite + chaser, a
satellite pair against chaser + front, and ammo-limited + charge-up kicks
against a custom design + satellite. The custom design is the identical startup
default on both machines, adopted into owner 1's slots the way the outpost
exchange would deliver it. A mount whose simulation reads unregistered or
local-only state desyncs here. The gameplay scenarios all fire constantly since
the same hook serves them.

Scenarios 11 to 15 extend the same harness. 11 raises the in-game-menu request
on both machines on the same frame (`--test-net-menu-frame`) and requires the
host-wins arbitration to leave a clean reliable queue. 12 and 13 are multi-level
runs (`--test-net-game-type`, `--test-net-zones`): the outpost auto-visit in
game_menu.c (`qa_shop_auto_visit`) stands in for the player, running the real
shop protocol (purchases, perk picks, custom weapon designs, the course
rendezvous and the departure handshake), and a frame-keyed scripted level end
(`QA_NET_ZONE_END_FRAME`, replayed identically by re-simulation passes) turns
each bounded flight into a cleared level. 12 flies ten Online Endless zones,
each charted with a forced modifier slate (`qa_net_zone_mods`) that covers
every registry bit across the run; both peers print their view of both wallets
at every outpost and the harness requires the sequences identical, plus a
nonzero elite bounty by the end. Multi-zone runs set `cheatInfiniteArmor` on
both peers so the scripted wiggle cannot die into the death menus, and the
verdict holds a final `PACKET_WAITING` barrier before exiting, because a peer
still confirming its last level end needs the other machine's packets to
escape its stall. 13 flies the first two campaign levels with the shop between
them, each ship flying its own custom weapon design, then jumps the pair into
episode 1's `]Q` section (26) to drive the real episode transition and flies
the first level of episode 2. 14 runs the peers under the production lobby
roles (`--test-net-lobby-settings`): the host arms Individual credit plus
Double Earnings from its own config, the joiner starts from the opposite values
and must adopt the settings block, and frame-keyed in-simulation pickup grants
then have to pay the same doubled wallets on both machines. 15 is the
accelerated soak, a 12000-tick flight watched by the same working-set check as
the base scenario (the baseline is re-marked at each level start so one-time
sprite loads stay out of the figure); it runs only when selected explicitly. 16
flies a level with Separate arcade ships (`--test-net-arcade-separate` arms it on
both peers, since command-line netplay adopts nothing) and requires both to report
`separate=1`, so a run that quietly fell back to the linked pair fails instead of
passing while proving nothing.

17 and 18 fly the two online one-player rulesets. 17 is SuperTyrian on the
Scrollock variant (`--test-net-scrollock` pins the variant field on both peers,
as the lobby would); both must report `st=1 sa1=254 sa2=254`. 18 is Super Arcade:
the wire peers pick different ships through the real announcement protocol (slot
1 takes ship 1, slot 2 ship 2), both must report `sa1=1 sa2=2` (a peer that fell
back to its own pick for both reads 1/1), and a frame-keyed script then walks the
five colour slots, granting each to both ships through `player_sa_ball_weapon`.
The harness requires the two peers' `NET SA BALL` lines identical and at least
one slot to hand the two ships different guns, which is the per-ship lookup
doing its job.

The session-flags line every gameplay peer prints carries the mode fields the
scenarios assert: `shared`/`doubled`/`separate`, `st` (superTyrian), and
`sa1`/`sa2`, the two ships' own `super_arcade_mode` bytes.

Command-line netplay now assigns player 1 the host role at startup (opentyr.c),
for real sessions and test peers alike: the desync recovery dispatch and the
menu arbitration act on the host role alone, and a session without one silently
ran with recovery disabled. Settings stay configured-by-hand on both sides;
only the role is filled in.

The endless scenario exchanges all three Relaxed death-prompt choices, host to
joiner, one exchange per choice the way three separate deaths would arrive. The
campaign scenario ends with the host leaving for a level the joiner did not
pick, adopted through `network_shop_adopt_host_level`.

The harness gives each peer its own scratch working directory: the game writes
`tyrian.cfg` and saves into the cwd, and a shared directory leaks state between
runs and races the two peers against each other. A caller can pass directories
in to carry saves across stages, which is how the resume scenario works.

### Esc during an online session

Every Esc path reachable online was audited (2026-08-07). All are safe: they
either back out locally, withdraw through the shop protocol, or send a quit
notice the peer acts on. Two were fixed to get there: cancelling the lobby's
connect wait now sends a best-effort `PACKET_QUIT` to whoever it may already
have been talking to (a joiner mid-connect has no timeout and would otherwise
wait on its own Esc alone), and an Endless charting player who withdraws from
the rendezvous clears `endlessCoopCourse` before the withdrawal publishes, or
the packet still carried the sector and the two machines could chart different
courses. The Relaxed death prompt deliberately ignores Esc: one of its three
rows must be chosen, so the joiner's wait always gets an answer.

Simultaneous presses are arbitrated: when both players' menu requests coalesce
into one scheduled opening, only the host takes the local-menu branch and the
joiner waits on the host's menu (`req_host_menu` in net_rollback.c). Both
machines derive "did the host press" from the same verified input records, so
the two sides always take complementary branches. Without it both machines took
the local branch and each sent a `PACKET_WAITING` nobody consumed. The
menu-race wire scenario presses Esc on both machines on the same frame and
fails on any stale `PACKET_WAITING` left behind.

The same scenario found that the menu release itself leaked: the waiting
machine broke out of its wait on the release `PACKET_WAITING` without consuming
it (`network_check` where `network_update` was needed), so every later
rendezvous on that machine was released one packet early. The peer's
`PACKET_GAME_QUIT` in that wait stays queued on purpose; the level-end paths
are the ones that read a quit.

### Online Destruct

Destruct is `NETWORK_GAME_DESTRUCT`, settled entirely in the lobby: the battle
mode and a rolled `Uint32` terrain seed travel in the connect packet's Destruct
block, the host's side rides the existing host-slot field (1 = left), and
`networkStartScreen` diverts to `loadDestruct` without a `PACKET_DETAILS`
handshake. Sessions are always delay-based: the minigame is outside the
rollback registry, so `network_settings_pack` and `network_arm_local_session`
both force the rollback and recovery bits off for this type. They are also
always Normal speed: the tick loop paces one lockstep exchange per delay unit
(`DE_SmoothPresent`), so `gameSpeed` sets how fast the two machines trade state
packets, not how the battle plays. `network_settings_apply_session_speed` pins
it before the connect packet is packed, so the joiner adopts the forced value
along with everything else, and the lobby hides the row rather than clearing
`network_host_game_speed`, which remains the host's choice for other types.

The tick loop reuses the main game's lockstep state stream (`PACKET_STATE`,
XOR parity, resends) with its own payload: one action byte and one control byte
per tick, plus `mt_rand_count` and an FNV hash of units/shots as a desync
canary (`DE_NetExchange` in destruct.c). Applied inputs are both sides' bytes
from `network_delay` ticks ago -- our own replayed from
`packet_state_out[network_delay]`, the peer's from the arrived packet -- so
both machines run the identical pair at the identical tick. Control bits: QUIT
(Esc, ends the session on both at the same tick) and NEWMAP (Backspace's round
reroll). Pause is offline-only here as in every online mode; control bit 0x02
stays deliberately unused. The desync canary reports once per session to the
net log and play continues.

Every map is generated from `network_destruct_session_seed + golden_ratio *
round` on both machines; nothing about the world crosses the wire. That is why
every config-file knob that shapes the simulation is pinned to shipped
defaults for the session in `JE_destructGame` -- shot/explosion/wall pools,
crater aliasing (it rewrites collision pixels), jumper trajectories, the tracer
laser, the AI, and `max_installations`, which is a MAX accumulator over the
config-set custom army sizes and never shrinks across visits. The wall-count
draw was moved from libc `rand()` to `mt_rand` and the generator/tick trig to
`sim_sinf`/`sim_cosf` for PC-console determinism. `JE_destructGame` frees the
previous visit's buffers on entry, because a network teardown longjmps out of
the tick loop past the frees at the bottom.

The intro card is a both-ready barrier (`DE_netIntroBarrier`; Timed Battle's card
is the other one). It announces this
player's confirmation as a `PACKET_WAITING` and holds until the peer's arrives,
so no machine reaches `DE_NetExchange` while the other is still reading the
card. It replaced a five-second auto-advance, which existed only because the
peer's first `network_state_update` gives up after sixteen -- with the barrier
neither state stream starts early, so that window never opens and the wait needs
no timeout at all. The wait is unbounded by design: both peers sit on this
screen pumping keep-alives, and a timeout would start one session without the
other -- Escape is the way out instead, sending `PACKET_QUIT` before halting so
an abandoned partner is told rather than left to the dead-link timeout.
`network_ready_peer` drains a trailing `PACKET_CONNECT` off the
head first (the same hole `network_sa_ship_peer` guards), and the barrier will
not release until this machine's own announcement is acknowledged, or the
retransmit would head the queue on the first tick. A 500ms arming window
swallows the press that opened the screen, since a held pad button auto-repeats
into fresh `newkey` edges, and a headless wire peer (`qa_net_gameplay_ticks`)
confirms itself on arrival.

### ENGAGE mini-games in Online Campaign

The `]e` script command (\*\* ALE \*\*, TIME WAR, SQUADRON) rewrites player
one's loadout, zeroes their cash, and forces `superTyrian`, `onePlayerAction`
and -- solo only -- `twoPlayerMode = false`; `]g` on the galaga pair
additionally clones that loadout over player two. The mode flips are why the
mini-games needed care online:

- Co-op keeps `twoPlayerMode` through the whole mini-game so both ships fly it
  (the movement loop's two-ship branch is keyed on that flag). Every galaga
  drop site is gated on `!coop_mode_active()`: the 30000-ball's wing spawn
  (which would link the pair and clobber the joiner's live ship -- in co-op it
  only pays), the respawn and out-of-lives drops in `JE_playerDamage`, the
  per-tick "spawned Dragonwing died" check, and the level-end reset. Solo keeps
  the vanilla one-ship-plus-spawned-wing rules exactly.
- Co-op mini-games issue the ENGAGE loadout to BOTH ships (`]e` loops over the
  pair; `]g` hands the galaga Vulcan to both), and `player_lives_port()` now
  answers FRONT for every `dual_ship_mode()` session, so each ship counts three
  lives on its own front gun instead of player two aliasing onto a rear bay.
- The classic arcade infinite-generator rule (`power = 900` under
  `arcade_rules_active()`) must reach the per-ship generator copies in every
  session that swaps them through the movement pass -- the push is keyed on
  `dual_ship_mode()`, not `arcade_separate_mode()`. Keying it on Separate alone
  left co-op mini-game ships draining a generator nothing refilled (the co-op
  regen branch is the `else` of the arcade branch), which read as "nobody can
  shoot" a second into ** ALE **.
- The arcade lives HUD in co-op mini-games draws both ships' rows and the local
  ship's gauge (Separate-arcade shape); the galaga score/fps corner reservations
  keep player two's corner in co-op.
- `shopLeaveOutpost` restores the outpost route before adopting a peer's
  debug-browser pick: the adopting machine may have committed its own planet
  already, and the capture inside `debugLevelPickApply` would then record that
  destination as the jump's return "home" -- quitting the picked level bounced
  that machine into the mini-game its dead commit pointed at (the
  quit-SQUADRON-land-in-\*\* ALE \*\* report) while the peer went to its
  outpost.

- Every wipe/quit reload and pre-level backup goes through `backup_save_slot()`
  (tyrian2.c): slot 22 whenever `twoPlayerMode || isNetworkGame`. Keying the
  reload on `twoPlayerMode` alone (the old code) read each machine's *local
  solo* slot 11 after a mini-game death -- `]e` had cleared the flag -- so the
  two sims restored two unrelated saves and desynced on the first frame of the
  reloaded level, with the session flipped to solo flags. The slot-22 record is
  written by `]b` at the pre-mini-game outpost in lockstep, so both machines
  restore identical coop state and land back at that outpost. The same path
  covers Esc-menu quits (`PACKET_GAME_QUIT` mirrors `playerEndLevel`) and the
  nrb peer-left endings.
- A cleared TIME WAR keeps its vanilla meaning: `]Q` -> `JE_nextEpisode`, the
  campaign carries on under SuperTyrian rules with the loadouts as flown --
  and it does so for EVERY entry path, debug browser included (the browser
  branch exempts `cleared && !galagaMode`). Online, an entry-flag mismatch on
  that branch split the pair between "return to the outpost" and "carry on",
  stranding one machine at `network_level_rendezvous` (30s timeout on a faded
  screen) while the other ran the ending alone; one shared path removes the
  split by construction. `superTyrian` and `onePlayerAction` deliberately
  survive, and a save made during the carry-on resumes back into it: `]e` never
  writes `items.super_arcade_mode`, but `JE_saveGame` re-derives that byte from
  the live flag (`superTyrian` -> `SA_SUPERTYRIAN`), and `JE_loadGameRecord`
  reads it back the same way. Both machines write and restore the same record,
  so the resume is symmetric. Ship two's byte stays `SA_NONE` (only player
  one's is derived), which is harmless here: `superTyrian` is one global that
  covers both ships, and a coop record is admitted to its lobby by
  `save_record_is_coop`, not by the `save_record_sa_ship` bytes the arcade
  lobbies gate on. A resumed coop session lands with `onePlayerAction` false,
  since the two-ship branch clears it.
  Contrast the mini-game exit path, which is where the flags do go away: the
  `doNotSaveBackup` branch after the backup reload clears `superTyrian`,
  `onePlayerAction` and `items.super_arcade_mode` outright, so dying or quitting
  a mini-game returns a plain campaign.
- The between-level cutscene chain (`]P` pics, `]W` briefings, `]Q`) is safe
  online as-is: text screens auto-advance on their `]W` delay digit, and
  `JE_displayText`/`JE_outCharGlow` pump `NETWORK_KEEP_ALIVE()`, so a slow
  reader cannot starve the link; the pair re-meets at the next outpost's shop
  handshake, which tolerates one side arriving late. `JE_readTextSync` (`]S`)
  is a vanilla `#if 0` stub -- do not lean on it for synchronization.
- The online debug skip (`RB_REQ_SKIPLEVEL`) sets `endLevel` + `levelEnd = 40`
  and nothing else. Both halves matter and they pull in opposite directions:
  - It must be the **wind-down, not `reallyEndLevel`**. That flag stops this
    machine's sim on the spot, and `nrb_driver`'s end confirmation waits for
    `verified_upto == nrb_cur` -- the peer's input for the frame this machine
    now never sends. Whoever consumed the request first walked off into the
    cutscene while the other sat on "waiting to confirm level end" until the
    8-second escape or the next between-levels packet freed it. Forty ticks of
    continued simulation are what let the request propagate, roll back if it
    landed late, and take effect on the same frame on both machines, which is
    also what makes them reach `reallyEndLevel` together
    (`levelEnd == 0`, mainint.c).
  - It must **not** set `levelTimer` with a zeroed countdown, though the F2
    cheat sets that pair alongside these two. The natural level end only clears
    `levelTimer` when time remains, so a zeroed countdown survives into the
    script and arms every `]t` "you failed" branch.
  The `!endLevel` guard keeps a re-consumed request (rollback replays the frame)
  from re-arming the countdown and stretching the wind-down.
- `adjust_difficulty` scores a co-op pair as two solo players (mean of the two
  `JE_totalScore`s against the solo thresholds). Vanilla's two-player branch
  counts raw cash -- correct for the shopless linked arcade where cash is the
  score, but a co-op pair spends its cash on equipment and read as broke (the
  drift is `MAX`-only, so the miscount could never raise, only fail to).
- The galaga rounds are authored endless; `engageGalagaEnd` routes a
  hypothetical clear of one into the same outpost reload in coop rather than
  letting galaga flags continue into the campaign. Solo keeps every vanilla
  behavior, including the slot-11 reload.
- `]w` (the Time War door) requires both ships in a `dual_ship_mode()` session
  to fly the Stalker 21.126. One machine evaluating it from a lone Stalker
  would be a scripted fork taken from synced state, so it stays deterministic
  either way; requiring both is a design choice, not a sync fix.
- Entry needs no special handling: the mini-game planets ride the normal
  outpost departure, where `network_shop_adopt_host_level()` already settles a
  host/joiner disagreement in the host's favor.
- The `]h`/`]H` difficulty gates read `initialDifficulty`, which both machines
  hold identically (host sends lobby+bump, joiner subtracts the bump;
  resume adopts the host's record), so the hard-only levels -- ep1's harder
  SAVARA and TYRIAN maps, ep3's TYRIAN X -- appear consistently online. The old
  slot-11 reload was the one thing that skewed `initialDifficulty` between
  machines (the 9-vs-15 TYRIAN map split in desync reports).
- NET_VERSION 23: a v22 peer still reloads its local solo save at a mini-game
  exit, so the versions must not pair.

The light-cone spotlight (`starShowVGASpecialCode == 2`) is anchored to the
local machine's own ship in `composite_playfield`/`composite_playfield_hi`
(`thisPlayerNum >= 2` picks player two on the joiner). The composite runs at
present time and feeds nothing back into the sim, so the two machines drawing
different cones cannot desync.

### Withdrawing from the outpost rendezvous

`shopLeaveOutpost` puts its notice up once and leaves it there, so every wait
below it inherits the "Press Esc to go back" line and each one has to be able
to honour it. Both steps of `shopCampaignRendezvous` now do:

- Step one (waiting for the peer's commit) withdraws outright: clear the
  charted sector, re-announce uncommitted, return false.
- Step two (our lock published, waiting for theirs) cannot simply withdraw --
  the peer may be reading that lock and leaving on it right now. It drops the
  lock and the commit, then waits up to 1.5 s to see which happened. No
  departure packet in that window means the peer read the withdrawal and
  reopened its own first wait, so the withdrawal stands; a departure means the
  peer is already at the handshake waiting on our packet and nothing can recall
  it, so we re-commit and leave with them. Both outcomes are states the peer
  already handles, which is why this cannot strand either machine.

Past that, in the departure handshake and the state-sync barrier, both machines
are committed and the level is loading; the hint is briefly stale there by
design. A peer that withdraws clears its commit, which drops the other machine
out of step two and back into step one, so the pair never sits in mismatched
steps.

### The episode loop (`JE_nextEpisode`)

Between levels the RNG is only in lockstep up to the first screen that draws
from it on its own schedule. `JE_playCredits` is exactly that: its ship
animation draws `mt_rand` per pass and a keypress ends the roll early, so
after it the two machines hold different streams. The SuperCarrot grant is
therefore decided *before* the credits run, while both are still on the
level's stream (reseeded at level start, draw counts canary-verified) -- drawn
after, the pair disagreed about the joke. Divergence past that point is
harmless only because every level start reseeds; anything added between here
and the next level that must match on both machines has to be settled ahead of
the credits too, or sent.

The grant itself equips every ship that flies its own arsenal
(`dual_ship_mode()`). Vanilla's single `player[1]` line hands out a rear gun
because in the linked pair that bay *is* ship two; a co-op ship needs the whole
hull. `wait_input` and `JE_displayText` both pump the link (`network_check`
sends the keep-alives), so the banner, codes screen and credits cannot time the
peer out.

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

The custom weapon uses spare runtime slots, one reserved set per player index; see
Custom weapons online for how the sets are claimed and kept live. Imported designs
are clamped and otherwise copied exactly. Every design retains at least one segment
and one power state. Persistence records have fixed field widths.

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
