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

Supported single-player modes use a real-time ship integrator. The simulation
reads its position for shots and collision, keeping the sprite and hitbox
together.

Disable this path for demos and network games. Advance it on every presentation
loop, including loops that also run a simulation tick. Preserve joystick press
edges for pause and menu handling.

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

`JE_drawPerfOverlay` runs at the end of every present path, onto the composited
frame at that pass's factor, so no later draw covers it. Keep it out of
`game_screen`: text drawn there becomes filter input on the next tick and smears
into the feedback. The hitbox boxes stay in `game_screen`, where world positions
belong.

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

`endlessRunBaseLevelSame` selects one of two gatherers. Same and Varied consume
different draw counts, so the rule is stored with the run and has separate
records.

Radar rerolls are folded into the phase salt. Latch Star Charts and the charting
seat at visit start so a redeal uses the same visit rules on both peers.

### Combat

- Scale raw damage before the enemy-health accumulator.
- Decode piercing damage before scaling and re-encode it afterward.
- `enemy_has_boss_bar()` is the common boss test.
- Contact scaling affects player damage only.
- Round small percentage effects when truncation would erase them.

Piercing repeat-hit state belongs to the bullet. Charge it once per tick from the
toughest crossed hull and apply the lock on the next bullet pass.

Every logical death calls `enemy_logical_death`. It owns kill count, bounty
deduplication, Shockwave, Martyrdom, and Chain Reaction.

Martyrdom uses the visible bounds of the dying linked body. Off-screen anchor
pieces do not move the burst away from the body on screen.

Endless specials require a fresh press unless Autofire Special is active. Keep
that gate on `endlessMode`; campaign debug effects do not change campaign firing.

`endlessMode` controls run structure, saving, prices, and pickup substitution.
`endlessFxActive()` controls combat scaling, modifiers, perks, and tiers.

### Modifiers and courses

`endlessModTable` owns modifier text, danger, payout, and classification. Adding
a modifier requires checking:

- persisted bit width and save migration;
- compatibility masks and course pools;
- danger, payout, and The End behavior;
- monitor rows and help text;
- visible glyphs, card width, and unique generated names.

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

Append fields and guard reads by version. Perk IDs appear in stacks and pending
offers; append enum values or migrate both arrays.

Records are split by mode, difficulty, crew size, and Base Level rule. The
difficulty table order is persistent. Append entries without reordering it.

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

At the classic pitch, seven rows fit above the help line. Measured limits:

- 135 pixels for a row name beside its value;
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
`NET_VERSION` bump. The current value is 35.

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

### Endless co-op ownership

Run-wide state is derived identically from seed, depth, and difficulty. Player
state is owned by that player's machine and mirrored in the fixed-width Endless
player block.

Per-player state includes:

- wallet, loadout, bombs, Reinforce tier, and revive;
- purchased sector effects and shop tax;
- prices, perk row, and outpost RNG;
- chart reroll count for the charting seat.

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

The Gencore Solar Shield icon differs between episode item sets. Episode Versions
owns `shields[8].itemgraphic`; the unused-sprite pass does not rewrite shields.

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
