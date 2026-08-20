# Maintainer notes

This is the home for rules that are hard to recover from the code. Player-facing
behavior belongs in the [player guide](../GUIDE.md). Keep source comments local
and short; point here when a rule needs more room.

## Build and targets

`build-all.ps1` builds PC, Switch, and Vita targets. Successful outputs are
collected under `build`. `-FailFast` stops after the first failed target.

- PC executables run beside `data`. `build` is an output directory.
- MIDI is available on Windows x86-64.
- Switch builds use devkitPro bash and an MSYS-style `DEVKITPRO` path.
- Vita builds use native CMake and Ninja. MSYS paths do not work there.
- Console Release builds define `NDEBUG`.
- Warning suppressions should name a DOS-era or third-party issue.

Run MSVC analysis separately:

```text
MSBuild visualc\opentyrian.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /p:EnableMicrosoftCodeAnalysis=true
```

Check analyzer range warnings. Use `OT_ASSUME` only after a real bounds check.

## Rendering

### Render list

The simulation runs at 35 Hz. `render_list.c` records a tick and replays it at
the display rate.

- Every moving object needs a stable `rl_current_id`.
- Use the performance counter for presentation timing.
- Exact replay (`use_override == false`) must reproduce the recorded frame.
- Read projectile velocity from the projectile. Shot slots are reused.
- Separate ship motion from motion belonging to a ship-attached shot.
- Give conditional pieces separate IDs when their command count can change.
- Store rounded sub-pixel remainders in `rl_current_sub_x/y`.

`rl_finalize` pairs each remainder with the previous tick. Display replay uses
the pair to recover smooth motion without changing exact replay. The Nort ship
trim uses `RL_ID_SHIP_TRIM_BASE` because its command count changes while banking.

### Display-rate ship movement

The real-time integrator is simulation code.

- Disable it during demo recording and playback.
- Advance it on every presentation loop, including loops that run a tick.
- Preserve joystick press edges used by pause and menu handling.
- Rollback sessions adopt the host's Smooth Motion setting through
  `nrb_session_vt()`.

Online placement differs by netcode:

- Rollback presents the local ship at its live integrated position.
- Delay-Based presents the delayed simulation position.
- Remote ships are extrapolated from their last tick, clamped, then eased.
- Large jumps snap instead of easing.

Anything attached to the presented ship uses
`rl_get_ship_override_dx/dy`. Online, `player[].x/y` remains the tick position;
using it directly can leave attached effects one tick behind the sprite.

### Feedback and overlays

Ice, water, and lava use two surfaces:

- `render_gs` holds persistent filtered background.
- `smoothie_frame` holds the current background.

Entities never enter the persistent surface. Apply full-screen color and
brightness to both sides of the residual comparison.

Presentation-only drawing includes fades, picture wipes, Destruct, gauges, boss
bars, the special meter, and the Zinglon pillar. Their state still advances once
per simulation tick.

- Interpolate gauge fill, not its base row.
- Linked Arcade merges special-meter edges from both player passes.
- Re-arm the meter when a new special replaces a running recharge.
- Advance ready flashes only on live ticks.
- Draw `JE_drawPerfOverlay` after the final composite.
- Keep hitbox overlays in `game_screen`; keep text out of feedback surfaces.

### Supersparks

Sparks share one `MAX_SUPERPIXELS` ring. A later spawn may replace a live spark.
`z` is both shade and remaining life.

- `classic_cap` is reserved for weapon trails controlled by their own setting.
- Other effects, including Opening Salvo, spawn uncapped.
- Uncapped sparks still retire the classic cursor slot, then use the uncapped
  area. This preserves the way combat traffic thins classic trails.
- Clear both private cursors through `JE_resetSP`.

Seeded effects use `JE_doSPSeeded` and related helpers. Their mixer must spread
regular caller strides across the full angle range. An affine LCG produces
visible fixed rays for those strides.

The spark ring is presentation state and is absent from rollback snapshots:

- Silent re-simulation consumes the normal RNG cost and writes no sparks.
- `JE_beginSPPass` and `JE_discardSPPass` undo a presented pass that is replaced.
- Spawn brief-life sparks before `JE_drawSP`; after the draw their `z` is
  indistinguishable from a stepped spark.

Coverage lives in `qa_test_superspark_seeded_spread`,
`qa_test_superspark_discarded_pass`, `qa_test_superspark_rng_cost`, and
`qa_test_superspark_shapes`.

### Backgrounds and supersampling

Background commands carry integer movement and fractional phase.

- `background3x1` binds layer 3 to layer 1.
- Publish phase even when a layer draws no rows.
- Keep whole-pixel correction separate from fractional phase.
- Round combined layer and local offsets once.
- Preserve layer 3's authored base step after advancing.
- Use the ship's actual travel range for horizontal normalization.
- `enemy_rides_layer2` is the shared binding test. Pickups do not bind.
- Call `endlessScrollExtraPx` once per layer per tick.

Mirrored Layers is part of the render command so 1x and supersampled replay
agree. Large scroll steps must process event spawns through the current phase.

Supersampling values 1 through 5 are fixed. `0` is Auto and
`RENDER_SUPERSAMPLE_NATIVE` follows the fitted output. Vita always resolves to
1x. The final copy uses nearest-neighbor sampling.

For low-resolution smoothie effects:

1. Replay the filtered background head at 1x.
2. Expand it into `pf_hi`.
3. Draw the background tail at the foreground factor.
4. Draw foreground commands over both.

The tick advance keeps `split` false because persistent feedback needs the full
background. Cache render-list scratch surfaces by factor to avoid resizing them
twice in one frame.

Vita keeps the current 1x endpoint in `smoothie_frame` and composites
display-rate foreground into `smoothie_present_frame`. The endpoint becomes the
next feedback frame.

The weapon preview is also a `present_hi` consumer. Run the 1x replay for its
gauges and overlays, restore anything drawn over the box, and draw the cursor
last into the high-resolution frame.

### Coordinates and bounds

The frame is 356x200. The playfield is 299x184 and the HUD is 57 pixels wide.

- World drawing uses `game_screen` and `PLAYFIELD_LEFT`.
- The final compositor crops the playfield to screen x=0.
- Menus use a centered 320-pixel canvas.
- HUD overlays use composited-buffer coordinates.
- `PLAYFIELD_X_SHIFT` is background phase, not crop offset.
- Row walks use the surface pitch.

Validate every `Sprite2_array` index. A 2x2 sprite uses the base index plus 1,
19, and 20. Specials use `spriteSheet10`; ship `itemgraphic` values do not.

Destruct leaves an open sky window between its two HUD boxes. Persistent terrain,
wall generation, collision ceilings, and shot trails must respect that window.
Clamp wall writes to `baseMap`; the next field in the struct is a pointer.

## Endless

### Module ownership

| File | Owns |
| --- | --- |
| `endless.c` | Run lifecycle, milestones, summary |
| `endless_rng.c` | Seeds and structural RNG |
| `endless_level.c` | Level and music selection |
| `endless_combat.c` | Scaling, tiers, combat modifiers |
| `endless_perks.c` | Perk rules |
| `endless_shop.c` | Outpost, prices, E-Shop, gamble |
| `endless_mods.c` | Modifier registry and text |
| `endless_course.c` | Course generation and selection |
| `endless_save.c` | Save records and sortie snapshots |
| `endless.h` | Public interface |
| `endless_internal.h` | Private interface and shared tuning |

Keep tuning with its owner.

### Structural RNG and level shuffle

Run structure uses SplitMix64 streams derived from seed and depth. Combat timing
must not change later shops or courses.

- Draw order is a compatibility interface.
- Use a unique phase salt for new work.
- Append draw phases when possible.
- Store or reproduce music choices across retries.
- Course generation order is gather, modify, filter, sort, uniquify, cache.

`endlessRunBaseRule` values are save, record, and wire order. Menu order goes
through `endlessBaseRuleAtMenuIndex`; never reorder the persisted enum to change
the screen.

The Shuffle rules use `endlessShuffleNext` as a cursor into deterministic bags.

- Count each distinguishable level section once.
- Advance the cursor for every hand, including discarded Radar hands.
- Shuffled deals consume no structural RNG.
- Keep the end of one bag out of the opening window of the next when the pool is
  large enough.

Online peers publish the live hand position. `endlessShuffleSyncHand` re-anchors
a disagreeing peer at the charting seat's cursor, then redeals. Read the reroll
count and player block before doing that redeal.

### Combat pipeline

- Scale raw damage before `enemy_hp_divisor100` spends it.
- Decode piercing damage before scaling and encode it afterward.
- Keep piercing repeat-hit state and fractional carry on the bullet.
- Use `enemy_has_boss_bar()` for boss classification.
- Contact scaling changes damage to the player only where stated.
- Round percentage effects that would otherwise disappear at low values.

`enemy_logical_death` owns kill count, bounty deduplication, Shockwave,
Martyrdom, and Chain Reaction. Destruction sites use `enemy_kill_group` and
`enemy_part_destroy` so drops, transformations, linked parts, events, and credit
stay together.

`enemy_death_payout` decides the resulting body, cash, datacube, or Super Arcade
pickup. `player_credit_cash` applies Shared or Individual credit. Keep payout
choice out of kill sites.

`healthbar_max` records the full damageable armor for health bars and Executioner.
Direct writes to `armorleft` call `enemy_note_full_armor`; damage does not. Armor
255 is the invulnerable sentinel and is never a denominator.

`endlessMode` controls run structure, saves, prices, and pickup replacement.
`endlessFxActive()` controls scaling, modifiers, perks, and tiers.

### Perk interactions

Perks belong to a player. Use `perkMine` for the local outpost owner and `perkFx`
for the ship whose effect is being calculated.

- Guidance Package marks shots by bay. It targets shootable screen positions,
  retargets after a kill, preserves ship-relative velocity ranges, and ignores
  circle shots.
- Twin Pods creates a second shot from the same bay. It pays power, advances the
  pattern, and spends ammo. Fire neither twin when the primary is refused.
- Reinforced Prow scales the enemy and player sides of contact separately.
  Invulnerable rams use `ENDLESS_RAM_INVULN_CADENCE`.
- Knife Fight measures hull-to-hull clearance. Apply its raw bonus before the HP
  divisor. Presentation blood uses seeded sparks and a per-frame budget.
- Deflector fires only when shield falls and armor does not. The returned shot
  keeps the incoming art and tint, reverses motion, and takes the firing ship's
  damage context.
- Opening Salvo tags emitted shots. Chained shots and Chain Reaction waves carry
  the tag. Rams and shotless specials read the live window.
- Kinetic Converter applies to actual shield or hull loss and to affordable
  twiddle charges.

Countermeasures is stateless. Each hull hit calls `endlessCountermeasureBurst`;
shield-only hits do not. The sweep extends 80 or 120 pixels past the hitbox on
each axis. Cleared shots keep their sparks; the edge flare and sound require at
least one cleared shot.

Opening Salvo is armed before `JE_doSpecialShot` and the front-gun loop. Only the
front gun consumes the charge.

Chain Reaction queues one target per linked hull and one hit per wave:

- `chainPulseOwner`, `chainPulseSalvo`, and `chainPulseWave` travel with each
  queued pulse and are rollback state.
- A cascade inherits its owner's effects, salvo tag, and wave serial.
- New pulses queued during a drain run on the next tick.
- `CHAIN_QUEUE_MAX` bounds one hop.
- Reset the queue and wave serial at level start.
- Apply damage through `enemy_spend_damage`, then destroy through the normal
  group path.

Presentation rings and bolts use seeded spark helpers and do not run during
silent re-simulation. `qa_test_chain_wave_latch`, `qa_test_chain_cascade`, and
the owner/credit matrix cover the queue.

### Health and tiers

All figures below use Normal difficulty zones.

| Lever | Curve | Ceiling | Ceiling zone |
| --- | --- | ---: | ---: |
| Ordinary armor | `100 + effective_depth * 4` percent | 600% in the armor byte | 101 |
| Ordinary overflow | Same curve through the damage divisor | 1200% total | 221 |
| Elite and champion | Piecewise 2x, 4x, 6x | 6x | 99 |
| Boss | Piecewise 1x, 9x, 20x, 32x | 32x | 199 |

`endlessBossRamp100` and `endlessEliteRamp100` are the authorities for tier HP.
Their hundredths values use `ENDLESS_HP_MULT_SCALE`, which must equal
`ENEMY_DAMAGE_ACCUM_SCALE`. The whole-number accessors are for mechanics defined
in whole multipliers, such as pierce delay.

The byte-sized `armorleft` stops at 254. `endlessArmorOverflow100` carries later
ordinary HP through the damage divisor. `endlessArmorPercentTotal` is the public
total and the unit used by debug overrides.

Champions use elite HP. Champion status changes offense, bounty, pierce lock,
ram damage, and Shockwave radius. An elite boss multiplies boss HP by two, capped
by `ENDLESS_HP_MULT_MAX`.

Tier selection rules:

- Settle a tier on the first processed enemy frame.
- Cache the tier by nonzero link group.
- Score pickups remain normal.
- Scan level events for groups that begin at armor 255 and later become
  damageable. Permanent scenery must not roll a tier.
- Follow type 39 link renames to a fixpoint.
- Pass a tier through hostile `enemydie` transformations.
- Treat `enemyAvail == 2 && edamaged` after an `edlevel == -1` transformation
  as wreckage. Wrecks stay out of combat and draw without tier tint.
- Keep the event scan derived and outside rollback state.

`endlessEliteTint` is the palette-bank authority for bodies, health bars, shots,
explosions, auras, and bounty labels. Store tint on projectiles and explosions
that can outlive the source. Keep those bytes zero outside Endless.

### Special pickups and orbiting specials

`endlessSpecialPickup()` must match the two pickup branches in
`JE_playerCollide`. Its art is presentation-only:

- Draw from `spriteSheet10` without changing `enemy[]`.
- Use `rl_present_gen` for color cycle and emission cadence.
- Use seeded sparks and skip silent re-simulation.
- Publish the glyph as a spark occluder.
- Keep palette bank and brightness in separate arguments.

The collision box follows the glyph, not the original 2x2 item. This is a wire
rule. Change it only with a `NET_VERSION` bump.

Some specials share or lack icons. `unusedSpecialIcons` assigns the spare whole
icon. `unusedSpecialTops` builds the remaining icons from a common ship body and
an unused shot sprite. Center overlays by measured ink bounds.

The Orange Shield is the shipped shot whose `sx`, `sy`, and `circlesize` pin it
to a ship and orbit it. Endless removes the table's `bx/by` offset and centers
the orbit on the loop measured by `shot_circle_center_offset_px`. Campaign keeps
the shipped path.

### Modifiers and courses

`endlessModTable` owns modifier text, danger, payout, and classification. Adding
a modifier requires updates to:

- persisted mask width and migration;
- compatibility masks and course pools;
- danger, payout, and milestone rules;
- monitor rows and help text;
- glyphs, card width, and generated-name uniqueness.

`endlessCanonicalMods` resolves the special-enemy and course-correction ladders.
It is idempotent and uses no RNG. Run it after generation, purchase folding,
launch, and restore.

Course-correction modifiers are mutually exclusive. Canonicalization keeps the
strongest one:

| Modifier | Corrections | Maximum turn | Availability |
| --- | ---: | ---: | --- |
| Seeker Rounds | 1 | 23 degrees | Ordinary pools |
| Twin Seekers | 2 | 23 degrees | 40-zone rare window |
| Hunter Rounds | 1 | 55 degrees | 110-zone window from zone 45 |
| True Aim | 1 | Direct | 140-zone window from zone 120 |
| Kill Shot | 2 | Direct | 200-zone window from zone 180 |

`seekerArm` counts down to the next correction; `seekerLeft` records how many
remain. Both fields must start at zero for ordinary shots. The two bytes come
from `EnemyShotType`'s reserved tail, keeping the structure size unchanged.

The finale unlocks one more correction tier per 100 difficulty zones. Draw its
tier after the established finale rolls so older seed-stream positions remain
stable.

Rare signatures are scheduled by seeded windows. A Radar reroll may move the
signature within its window. Guarded signatures suppress Jackpot and Ambush;
milestone slates may replace them.

Milestones use the upcoming zone:

- Odd multiples of 25 offer S and S+.
- Other multiples of 50 offer S+ and S++.
- Multiples of 100 offer one END, two S+++, and two S++.
- Multiples of 100 exclude every scroll-pace modifier.

Course order uses cached danger and payout. Purchases and Sabotage update the
chosen card without changing its original sort key.

### Economy and perks

All cash changes use `endlessCashCredit`, `endlessCashDebit`, or the shop trade
pair. Preserve this ledger equation:

```text
earned - spent == wallet
```

Wallets and prices are `Sint64`, bounded by `CASH_MAX`. Use the helpers in
`player.h`; `JE_cashLeft` may be negative only while previewing an unaffordable
row.

`endlessCleanseCharges` is the shared Sabotage count. Prices, perk rows, shop RNG,
and Extra Perk counters are personal.

`endlessExtraPerkPrice` starts with the depth price, then applies:

- `ENDLESS_PERK_OWNED_PCT` for every held stack, including free picks;
- `ENDLESS_PERK_PAID_GROWTH_PCT` compounded over `endlessExtraPerksBought`;
- `ENDLESS_PERK_VISIT_REPEAT_PCT` compounded over `endlessExtraPerksVisit`.

All three counts are personal. The purchase counts are saved and mirrored;
`endlessResetShopPrices` clears only the visit count. Wallet size and income do
not affect the price. `ENDLESS_PERK_COMPOUND_MAX` bounds both exponents if a
save contains a corrupt count.

### Saves, records, and retries

All current saves live in `opentyrian.sav`, using the named-key format from
`config_file.c`:

- `section 'saves'` stores `SAVE_FILE_FORMAT`.
- `section 'save' 'N'` stores the game slot.
- `section 'endless' 'N'` stores its Endless half.
- `section 'highscore'` stores each board.

Missing or invalid keys use defaults. Unknown keys are ignored. Add an Endless
field to both `endlessRecToSection` and `endlessRecFromSection`. Online resume
uses the same text codec through `endlessRunSerialize` and `endlessRunAdopt`.

Hardcore keeps an in-memory sortie snapshot and writes no checkpoint.

Legacy import runs only when `opentyrian.sav` is absent:

- `legacy_save_parse` reads `tyrian.sav`.
- `endlessLegacyReadRec` reads binary `endless.sav` versions 3 through 27.
- The 28-byte `tyrian.cfg` is imported into `opentyrian.cfg`.

Read a future sidecar through its declared record width and the known v27
prefix. A missing width is the case that cannot be recovered. If a `ZONE n` slot
lacks an Endless section, `endlessSaveRepairFromLegacy` may restore it from the
sidecar.

Records are split by mode, difficulty, crew size, and Base Level rule. Append to
persistent tables; never reorder them. A custom-weapon record mark is earned only
by firing during a zone.

Retry rules:

- Relaxed death screens own and finish their music fade.
- Release input before arming dismissal.
- Restore the launch snapshot on retry.
- Return to Outpost uses Quit Level.
- Restart Zone reloads the same music and clears visit resume.
- Advance the Alternating chart seat only after a cleared sector.
- Restore `endlessPlayerMods` and purchased one-shots when relaunch bypasses
  course selection.
- Read `]I` shop blocks to keep the parser aligned, then discard them in
  Endless.
- Preserve `endlessSortieOutpostEp` while restoring stock IDs.

## Menus and UI

Menu labels, row counts, values, and help indices are parallel data. Update them
together.

- Generic option rows map through `menuItemIntSetting` and
  `menuItemBoolSetting`.
- Keep explicit cases for rows with side effects.
- `enhancementSettings[]` is the authority for both presets.
- Engaged values must match fresh-install defaults.
- `chargeSidekickAutofire` is per-save and stays outside presets.
- Apply table-backed settings through `JE_applyItemDataSettings` immediately.

Custom preset state is a positional list guarded by `enhancementTableShape`.
Reordering or retuning the table invalidates the stored list. Capture Custom
when the live values match neither built-in preset.

Gun settings usually apply to all eleven weapon records in a port. Use helpers
such as `JE_setPortFiringSound` and `JE_applySuperSparks`; changing power level 1
alone leaves upgraded shots unchanged.

`JE_applyChargeLaserCannon` owns a captured free option slot. Restore the
captured record when disabled and refuse to overwrite a slot claimed by a
custom sidekick.

Font constraints:

| Bank | Safe use |
| --- | --- |
| `TINY_FONT` | Most printable characters |
| `SMALL_FONT_SHAPES` | Letters, digits, common punctuation |
| `FONT_SHAPES` | Uppercase headings without digits |

`SMALL_FONT_SHAPES` has blank glyphs for `(`, `)`, `+`, `*`, `=`, `]`, `{`, and
`}`. Tilde changes brightness and is not printed. Measure rendered width instead
of character count.

Other UI limits:

- Seven rows fit above a classic help line.
- Row name beside value: 135 px.
- Value: 95 px.
- Help text from x=45: 275 px.
- Chart-a-Course uses `endlessCourseRerollRow` and `mapPNum` because it has gaps.
- Debug menu headings are not selectable.
- Boss and enemy bars use playfield coordinates.
- Two-player gauge blocks repeat every 134 px.

## Networking

Both machines simulate both ships. Send every simulation-affecting choice before
either peer resumes. Rendering, audio, and local input settings stay local.

`network_is_host` names settings authority. `networkHostPlayerNum` names the ship
flown by that machine.

### Wire compatibility

Any deterministic rule, packet meaning, field, or offset change requires a
`NET_VERSION` bump. The current version is 73. Packet readers check length before
optional fields and use fixed-width types.

Recent compatibility points:

| Version | Change |
| ---: | --- |
| 37 | Glyph-sized Endless special pickup collision |
| 40 | Ship-centered Endless orbiting specials |
| 42 | Four Base Level rules and shuffle-hand position |
| 43 | Endless run transfer uses the v25-width save header |
| 44 | Piercing carry and fractional tier damage |
| 46 | Twiddle ownership and scheduled rare sectors |
| 48 | Health bars remember starting armor |
| 49 | Synthesized Dragonwing ship row |
| 50 | Save acknowledgement returns the peer outpost half |
| 51 | Withdrawable departure gate |
| 52 | Twiddle direction cone and neutral diagonal bit |
| 53 | Strict twiddle cancellation |
| 54 | Twiddle intent mirrors with Topsy Turvy |
| 55 | Short Opening Salvo spark cue |
| 56 | Opening Salvo covers the special fired on the same tick |
| 58 | Equal online vertical travel ranges |
| 59 | Guidance Package steering |
| 60 | Guided Aim screen-position rule |
| 61 | Twin Pods second volley |
| 62 | Endless ram, Prow, Knife Fight, and Deflector rules |
| 63 | 64-bit wallets and text run transfer |
| 64 | Whole-state Endless debug block |
| 65 | Opening Salvo and Knife Fight on rams |
| 66 | Zinglon ramp and refire gate |
| 67 | Rollback menu request removed from the old handshake |
| 68 | Rollback menu opens on the verified press frame |
| 69 | Extra Perk counters in the outpost player block |
| 70 | Fractional and overflow Endless HP scaling |
| 71 | Course-correction tiers and per-shot pass state |
| 72 | Countermeasure Suite bursts on every hull hit |
| 73 | Zinglon pillar damage scale and beam ownership |

Earlier versions are available in Git history. Keep this table focused on rules
that still constrain current code.

### Modes and session settings

`coopCampaignMode` and `coopEndlessMode` are co-op flags.
`coop_mode_active()` covers both. `dual_ship_mode()` also covers Separate Arcade,
Timed Battle, SuperTyrian, and Super Arcade. Use `split_arcade_mode()` for the
linked Silver Ship and Dragonwing pair.

Load and save per-ship runtime state around each movement pass. Generator state,
cooldowns, sidekicks, lives, and specials remain in the owning `Player` and in
rollback state.

`player[].lives` aliases a weapon-power byte. Access it through
`player_lives_port()`. `player_is_out()` controls HUD visibility after the final
life is spent.

The host arms session flags through `network_arm_local_session`; the joiner
adopts them from the settings block. Preserve bytes 0 through 23, keep byte 47
reserved, and clamp received enums and expert values.

### Discovery, keep-alives, and reliable UDP

LAN discovery broadcasts every 400 ms to the well-known port, the last host
port, the global broadcast, and interface /24 broadcasts. A host on another game
port keeps `discover_socket` on the well-known port until a player joins.

The peer timeout is 16 seconds. Long-lived online screens must call
`network_check()` or `NETWORK_KEEP_ALIVE()`.

Reliable channel rules:

- A receive error does not prove the peer is gone.
- `network_is_sync()` means the outbound reliable queue is empty.
- Retry from the queue head and resend unacknowledged packets oldest first.
- Apply backpressure when the outbound window is full.
- Trust acknowledgements only for packets still outstanding.
- Drop packets beyond the receive window without acknowledging them.
- Re-acknowledge packets behind the window.
- Leave packets queued for the state machine that owns them.

Chunked transfers keep at most half of `NET_PACKET_QUEUE` outstanding. Transport
acknowledgement covers delivery; complete transfers also use an application ack.

Level-start and result barriers use dedicated markers. Once both result
dismissals have arrived, `NET_DEPART_GRACE` allows the slower peer to finish even
if its last acknowledgement was lost.

### Outpost and departure

Each player owns their shop state. `PACKET_SHOP_SYNC` mirrors cash, loadout,
route, and mode after committed purchases.

Shared-outpost departure has two persistent states:

1. DONE allows withdrawal.
2. LOCK closes withdrawal after both peers have seen DONE.

Modes without a shared outpost use `PACKET_DEPART_GATE` for a retractable gate
and `PACKET_WAITING` for the final commit. Adopt the host's level only after the
local player leaves shopping.

Custom weapon and Endless run transfers retire stale handshake duplicates but
leave quit packets for the quit handler. `network_shop_begin` owns the co-op quit
transition back to the outpost.

A save checkpoint may wait for a peer still on the level-end screen. Draw a wait
notice, allow Esc, and keep `NET_SHOP_SAVE_WAIT` as the final bound.

### Endless co-op ownership

Run seed, depth, course, and difficulty are shared. Each player's machine owns
and mirrors that player's:

- wallet, loadout, bombs, hull upgrades, and revive;
- purchases, shop tax, prices, perk rows, and outpost RNG;
- chart rerolls and shuffle hand;
- perk stacks and personal modifier masks.

`itemAvail` and the local cash ledger remain local. The partner stash keeps the
remote outpost half between checkpoint acknowledgement and record capture.

Store perks in `endlessPerkTakenBy[2][PERK_COUNT]` and grant them through
`endlessPerkGrant`. Fold the chosen course only after both players' purchases are
known. Call the per-ship half of the Endless tick for both players.

The online debug panel sends one whole-state block through
`endlessPackDebugBlock`. It includes depth, modifiers, both perk tables, and both
personal modifier masks. Host wins simultaneous edits.

### Rollback and determinism

`PACKET_INPUT` carries a fixed header and up to sixteen redundant records. It is
unacknowledged and idempotent. Reject records from another level epoch.

The rollback menu request rides the input record. Open on the earliest verified
request frame, with host priority on a tie. Rewind to that frame, reset the core,
and begin a new epoch after the menu closes. `PACKET_GAME_MENU` carries the input
image needed to release a peer that missed the original datagram.

Every wait that stops frame production must continue servicing menu requests and
peer records. Level-end confirmation waits for the peer through the same frame
and uses `NRB_PEER_IDLE_TIME_OUT` from the last observed advance.

Determinism rules:

- Demo record and playback share seed and initial state.
- Store multiple `mt_rand()` calls in named locals before combining them.
- Use `sim_sinf` and `sim_cosf` in simulation.
- Build Linux and consoles with signed `char` semantics.
- Use precise floating point on MSVC and disable contraction elsewhere.
- Do not use mutable function-local statics in rollback simulation.
- Register pointer relocations for cross-process export.

Main-game recovery transfers registered host state in `PACKET_RESYNC` chunks.
Peers compare registry size and `rollback_layout_fingerprint()` first. The joiner
acknowledges only after adopting a complete generation. Limit recovery to three
attempts per level.

Presentation state stays outside snapshots. Silent replay must repaint dirty HUD
state, restore text backgrounds, suppress live-only flashes and sounds, and leave
`SFX_CUE_CHANNEL` presentation-only.

### Online saves and records

Online saves use slots 12 through 22. Slot 22 is the read-only `LAST LEVEL`
backup. `save_record_pack` and `save_record_unpack` define the 89-byte
little-endian wire record.

Dual-ship tags:

- `0xc74f`: Campaign and Endless co-op.
- `0xc7a5`: Separate Arcade, Timed Battle, Super Arcade, and SuperTyrian.

A save writes only to the local machine. Its acknowledgement returns the peer's
outpost half for inclusion in that record. Preserve each machine's player number
through resume; the lobby's `networkHostPlayerNum` must adopt the saved seat.

Reassert `coopCampaignMode` and `coopEndlessMode` after `JE_loadGameRecord`.
Endless resume must also adopt the run record or halt the session.

`coopCampaignScoreNote` owns co-op Campaign record eligibility. Record only a
completed starting episode, before repeat, outside demos. Do not record a death
or later episode reached by the same run. Store the Credit rule beside the row.

### Destruct and ENGAGE modes

Online Destruct uses `NETWORK_GAME_DESTRUCT` and Normal speed. Both netcodes send
one action byte and one control byte per tick. QUIT and NEWMAP are confirmed
control bits; pause is unused.

Rollback snapshots include units, walls, shots, explosions, world state, RNG,
and every pixel of `destructTempScreen`. Predictions repeat held movement and
fire; unit and weapon changes are edge-triggered. Start a new epoch each round.

Recovery uses `PACKET_DESTRUCT_RESYNC` and `DE_StateSave`. Restore the three live
pointers in the blob before use. Derive each map from session seed and round
number after pinning simulation settings.

Online Campaign keeps two-player state active through `]e` and `]g`. Mini-game
death or quit reloads slot 22. A cleared TIME WAR continues under SuperTyrian
rules. Decide shared random results before locally timed credits begin.

## Weapons and item data

Apply episode differences after item loading and keep the operation idempotent.
Projectile graphics above 1000 encode a superspark palette bank and base sprite.

Custom weapons reserve a port, sidekick, and scratch range per player. Ownership
is by player index on both peers. Call `customWeaponMaterializeAll()` after item
data reload. Validate a complete wire design before materializing it.

A temporary `poweruse == 0` bypasses cost and generator availability. Fire extra
beams only after the primary succeeds.

`JE_applyUnusedShopSprites` captures its baseline after placeholder and
Charge-Laser setup and before custom slots. Restore that baseline when disabled.

The Flying Punch's fifth bolt has no sprite. It still deals damage and carries
its reserved smoke trail.

Episode Versions owns the Gencore Solar Shield icon and the U-Ship/Nort Ship
`bigshipgraphic` values. Only 28, 32, 33, 45, and 46 have illustration layouts.

The Dragonwing is synthesized as ship 19 after each item load:

- `shipgraphic == 0` selects its two-piece hull.
- Only Endless shops list it.
- IDs 20 through 90 remain invalid for normal ship rows.
- In `dual_ship_mode()`, a bought Dragonwing is a full ship, including seat two.

Wide shop hulls straddle their anchor. Shift only the item-list call site;
changing `JE_drawItem` would move centered preview art.

Weapon row tags:

- `Dual-Mode` belongs only to rear-list ports with `opnum == 2`.
- Endless mixed-bay lists may add `Front` or `Rear` from `shopRearGunPorts`.
- Port 16 is sidekick data and has no bay.
- Campaign lists do not show mixed-bay tags.

The uncertain front-port classification for 6, 32 through 35, 44, 46, and 47
comes from shipped shop and pickup evidence. Revisit it only with better source
data. `qa_test_weapon_bay_tags` pins the current classification.

### Twiddles and specials

`shipCombos` is indexed by ship ID. `shipCombosB` replaces it in SuperTyrian.
Ships outside the table have no twiddles. Linked seat two uses row 0; full-ship
modes use each ship's row.

All input paths pass through `SF_twiddleTarget`:

- An axis must exceed twice the other to count as a direction.
- Shallower diagonals are neutral.
- `RB_MOVE_DIAG` preserves that neutral result online.
- Apply the Topsy Turvy horizontal mirror inside `SF_twiddleTarget`.
- Self-test replay passes an already resolved target.

Recognition is strict. Wrong input cancels a combo. Holding the last accepted
direction and code 9 (all released) are exempt. `SFExecuted` lasts one tick.

Gate a fired twiddle on per-ship `twiddleWait`, not the equipped special's
cooldown. Charge shield or armor only when the full cost is available.
`JE_resetSpecialState` clears all live special and twiddle clocks at level start.

Flare-family specials and level event 44 share the full-screen grade. A flare
may claim it only while the level grade is inactive. Track ownership in
`flareOwnsFilter`, which is rollback state, and clear only a grade the flare
owns.

Soul of Zinglon and MineField share `zinglonDuration` and `zinglonRamp`.
`zinglon_pillar_width` opens for 25 ticks, holds, then closes for 25. A refire
refreshes duration without resetting a live ramp. Keep the ramp per ship and in
rollback state.

The beam occupies `MAX_PWEAPON - 1` without a `playerShotData` entry.
`zinglon_pillar_hit` supplies its collision width, scaled damage, and owner.
Overlapping beams do not stack; the stronger hit wins and its owner gets the
perk context and kill credit.

## Audio, logs, and consoles

MIDI backends convert LDS through vendored midiproc. At loop start, replay
pre-loop program, controller, pitch, and SysEx state at time zero. Never replay
notes.

An LDS patch carries a detune in sixteenths of a semitone, and it serves two ends.
Across a group of otherwise identical patches it is the relative offset that makes
a layered voice shimmer. On a patch standing alone it is Adlib voicing, and
`midi_transpose` already covers MIDI pitch there, so sounding it leaves that part
flat or sharp by itself. The converter reduces every patch to its offset within
its group, sounds the nearest key, and leaves the remainder on the pitch wheel, in
the term that already carries glide and vibrato for that channel. Across the
shipped songs that leaves 6 to 19 cents of detune on 7.3% of melodic notes and
moves no note off its written key. `qa_test_lds_midi_detune` pins both jobs.

The same split governs `arp_tab[0]`, which the Adlib player folds in as a fixed
transpose whenever a patch has no arpeggio sequence. It is Adlib-side voicing, and
`midi_transpose` is the MIDI-side transpose the author set beside it. Adding
`arp_tab[0]` on the MIDI path double-transposes: of the 58 melodic patches that
carry one, 19 have `midi_transpose` set to the same value, and one pair sums to 72
semitones. Leave it to `midi_transpose`.

A patch whose `keyoff` is zero names no note length. The Adlib voice stops on its
own: a percussive operator decays to the sustain level and then keeps going to
silence with the key still down. MIDI has no such envelope, and the converter's
only other release is the channel's next note, so those notes rang on. The
converter now derives a length from the envelope, using `opl.c`'s own constants
and its 1e-8 cutoff, and takes whichever comes first. The estimate uses the
slowest key-scale slot, so it only ever bounds a note from above: of 66462
candidate note-ons it shortens 44, the worst falling from 126 seconds to 2.2.
Voices that sustain, and rates of zero, are left to hold as before.

A note still sounding when the walk stops is released at that tick. The loop end
marker sits there, and a looping player repeats the section above it, so a release
scheduled past that point is never reached and the note stays held through every
repeat. It also stretched the reported duration, which is what decides whether a
song loops at all.

The envelope length is an upper bound, not the exact figure. `opl.c` scales the
decay by a key-scale offset taken from the note's block and the patch's KSR bit,
and this uses the slowest slot instead. A real note can fall silent up to 1.75x
sooner with KSR clear, which is 393 of the 421 candidate patches, and up to 14x
sooner with it set. Reproducing that needs the note, so it belongs in PlaySound
with the frequency table beside it.

Three LDS features still do not reach the MIDI path, all behind `ENABLE_VIB`,
`ENABLE_TREM`, and `ENABLE_ARP` in `MIDIProcessorLDS.cpp`. Measured over the
shipped songs: vibrato covers 64% of note-ons, tremolo 33%, and running arpeggio
sequences 2.5%. That code has never been compiled and does not build as written:
it calls `add_event` rather than `AddEvent`, names `PitchWheel` rather than
`PitchBendChange`, and strands the `vibwait` countdown inside a branch that only
exists when `ENABLE_ARP` is set, so a delayed vibrato would never start.

Windows native MIDI uses `CALLBACK_NULL` and its own sequencer thread. When a
configured SoundFont path is stale, retry its filename under `data_dir()` before
discovery. See the [midiproc vendor notes](../src/midiproc/VENDORED.md).

The Windows crash logger resumes the main thread before symbol work. State dumps
must tolerate unloaded item tables and invalid IDs.

Logs are created lazily under `log/`:

- Network reports keep full bodies and unsymbolized RVAs to avoid blocking the
  game loop.
- Crash and hang reports may symbolize.
- Console cleanup removes only recognized OpenTyrian log names.

Switch constraints:

- Keep the SDL window resizable for dock changes.
- Persist state before `_Exit`.
- Save controller changes immediately; HOME may bypass shutdown.

Vita constraints:

- Present at native size and force 1x supersampling.
- Keep presenting while the IME is open.
- Terminate the IME once on every exit path.
- Clear latched input after draining raw events.
- Use front touch only.

Both consoles fold the right stick into movement and disable MIDI. Switch uses
`switch-sdl2_net`; Vita provides the needed UDP subset in `vita_net.c`.

## Level scripts and data

Tyrian levels are event scripts. Reseeding `mt_rand` changes runtime jitter,
background choices, effects, and sounds. It does not reorder authored pickups.

- The event clock advances with vertical scroll.
- A map stop resumes when the screen clears, link 254 jumps, or `forceEvents`
  advances the clock.
- The parked-enemy watchdog culls only an unreachable stop holder with no
  reachable linked member after `MAP_STOP_STALL_LIMIT`.
- Test the HARVEST entrance after watchdog changes.
- Use `sprite2_is_blank` on the active sheet for blank map structures.
- Dormant dispenser bases are enemy IDs 80 through 83. Piece 80 fires on frame 9.

## Data dump

`tools/dump/dump_data.py` mirrors loaders in `src/` and writes the tracked
`dump/` tree. Update a reader when its loader changes.

The dumper reads Tyrian 1.1, Tyrian 2.1 and Tyrian 2000. It identifies a data
directory from the item counts stored in front of its tables and binds the
tables that differ, so `data/`, `data_21/` and `data_11/` dump to
`dumps/dump_2000/`, `dumps/dump_21/` and `dumps/dump_11/` with the same layout.
`dumps/DIFFERENCES.md` records what changed between the three.

`tools/dump/verify_dump.py` is the correctness check. Every reader needs a check
that accounts for all source bytes or compares the engine arithmetic it mirrors.
Point it at the tree under test with `--data` and `--dump`.

Format traps:

- `user1.shp` and `user2.shp` contain a two-byte header and raw 12x14 cells.
- Shipped `shapes?.dat` files contain 520 bytes after tile 600.
- Episodes 1 through 3, 4, and 5 use three different item/enemy tables. Tyrian
  2.1 has the same split with no episode 5. Tyrian 1.1 stores a set at the end of
  every level file and none in `tyrian.hdt`.
- `tyrian.hdt` text groups are position-dependent. They end at the item-data
  offset in Tyrian 2000 and at the last byte in Tyrian 1.1, which stores no
  offset at all.
- A compiled 12px frame ends where the next offset in its table starts. Tyrian
  2.1 and 2000 also terminate one with 0x0f. Tyrian 1.1 does not, and pads its
  streams with zero bytes that skip nothing.
- Tyrian 1.1 orders the seven `tyrian.shp` sprite tables differently and ships
  eleven banks; Tyrian 2.1 ships twelve in the 2000 order.
- The enemy shapebank table only grows: 30 entries in 1.1, 34 in 2.1, 36 in 2000,
  with no slot reassigned. Each release stores it verbatim in `file0001.exe`.
- Encrypted record dumps preserve one terminator per record, including trailing
  empty records.
- Re-encode decrypted text as CP437 to recover its bytes.
- Data files are matched and dumped in lower case. Tyrian 1.1 names them in
  upper case, and the same release dumps to the same tree either way.

Use `dump/index.csv` to find a data file's decoder, engine loader, references,
and outputs.

## Tests

Runner details belong in [testing/README.md](../testing/README.md). Before
changing persistent or deterministic state, cover:

- old save import and malformed input;
- rollback save, restore, and pointer relocation;
- cross-process state hashes;
- host arming and joiner adoption;
- both player indices and host roles;
- loss, reordering, duplication, outages, and sequence wrap.

## Small rules with large consequences

- Use the correct sprite bank.
- `enemycycle` is one-based.
- Positional enums index shipped data. Do not remove unused-looking entries.
- Keep `config_file.c`, `opl.c`, and midiproc public contracts stable.
- Preserve upstream Doxygen comments and third-party documentation.
