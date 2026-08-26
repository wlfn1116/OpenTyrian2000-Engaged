# Maintainer notes

Rules that are easy to break and hard to recover from the code live here. Keep
player instructions in [GUIDE.md](../GUIDE.md) and build recipes in the platform
READMEs.

If a code comment grows past three sentences, shorten it and put the missing
context in the relevant section below.

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
- Console Release builds define `NDEBUG`.

For MSVC analysis:

```text
MSBuild visualc\opentyrian.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:RunCodeAnalysis=true /p:EnableMicrosoftCodeAnalysis=true
```

Check range warnings instead of hiding them. `OT_ASSUME` belongs after a real
bounds check.

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

Vita keeps the 1x endpoint in `smoothie_frame` and composites display-rate
foreground into `smoothie_present_frame`.

### Display-rate ship movement

The real-time ship integrator affects simulation.

- Disable it for demo recording and playback.
- Advance it on every presentation loop, including loops that also run a tick.
- Preserve joystick press edges used by menus and pause.
- Rollback sessions use the host's Smooth Motion choice.
- Rollback shows the local live position; Delay-Based shows the delayed tick
  position.
- Remote ships extrapolate from the last tick, then ease. Large jumps snap.

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
`video_output_pixel_scale`.

Validate every `Sprite2_array` index. A 2x2 sprite reads the base index plus 1,
19, and 20.

Destruct leaves open sky between its HUD boxes. Terrain, wall generation,
collision ceilings, and shot trails must respect that gap. Clamp wall writes to
`baseMap`; the next struct field is a pointer.

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
| `endless_internal.h` | Private interface and shared tuning |

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

Shuffle rules consume deterministic bags:

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
The Orange Shield orbit is an Endless effect; campaign play keeps the shipped
special path.

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

Milestones use the upcoming zone. Multiples of 100 exclude scroll-speed
modifiers; The End also excludes Dead Generator.

### Perks

Perks belong to a player. Use `perkMine` for the local shopper and `perkFx` for
the ship whose effect is being calculated.

- Guidance Package tags shots by bay and retargets after a kill.
- Twin Pods spends the second shot's power and ammunition. If the primary shot
  is refused, fire neither.
- Reinforced Prow scales damage dealt and received separately.
- Knife Fight measures hull clearance before HP scaling.
- Deflector fires only when shield falls and armour does not. Apply its refund
  after damage resolution so overflow remains unchanged.
- Opening Salvo tags emitted shots and Chain Reaction pulses. Only front-gun
  fire spends the charge.
- Kinetic Converter uses actual shield or hull loss and applies only to
  affordable twiddle charges.

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

Legacy import runs only when `opentyrian.sav` is absent. It reads `tyrian.sav`,
binary `endless.sav` versions 3 through 27, and the 28-byte `tyrian.cfg`.

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

`network_is_host` identifies the settings authority.
`networkHostPlayerNum` identifies the ship flown by the host machine.

### Wire compatibility

`NET_VERSION` in `network.c` is the compatibility gate. Bump it for any change to
a packet, deterministic rule, field meaning, offset, or registered simulation
layout. Readers validate lengths and clamp received enums before indexing.

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
route, and mode after a committed purchase.

A shared outpost has two departure states:

1. DONE allows withdrawal.
2. LOCK closes withdrawal after both peers have seen DONE.

Modes without a shared outpost use `PACKET_DEPART_GATE` for the retractable gate
and `PACKET_WAITING` for final commit. Adopt the host's level only after the
local player leaves shopping.

Content publication can receive `PACKET_WAITING` before its transfer ack. Carry
that readiness into the level barrier, but leave a second marker queued for the
next boundary.

In online Endless, seed, depth, course, and difficulty are shared. Wallets,
equipment, shop stock, perks, revive, personal modifiers, and outpost RNG belong
to a player. Fold the selected course only after both purchase sets are known.

### Online appearance

Ship dye and view settings are local presentation choices, published per seat.
Dye affects the hull and sidekicks, never shots. A kill-fire tint takes priority.

Opacity is not rollback state. Shield-hit effects follow hull opacity, while
health bars and other view choices remain local. Linked Arcade has no partner
health bars.

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

Presentation state stays outside snapshots. Silent replay repaints dirty HUD
state and suppresses live-only sound and flashes.

Main-game recovery compares registry size and
`rollback_layout_fingerprint()` before adopting host state. The joiner
acknowledges only after a complete generation has been adopted. Limit recovery
to three attempts per level.

Destruct recovery snapshots units, terrain, shots, explosions, RNG, and
`destructTempScreen`. Re-pin its three live pointers after restore and start a
new epoch each round.

### Online saves

Online saves use slots 12 through 22. Slot 22 is the read-only `LAST LEVEL`
backup. `save_record_pack` and `save_record_unpack` define the 161-byte wire
record. It was 97 bytes through `NET_VERSION` 86; version 87 appended the
64-byte custom-episode file name.

A save writes only to the local machine. The peer acknowledgement supplies the
other outpost half. Preserve player numbers on resume and restore both co-op mode
flags after `JE_loadGameRecord`.

For a resumed custom run, the host builds an ordered required set from
`custom_collection`, then adds `custom_episode` if needed. An older Endless save
without a collection falls back to every container installed on the host. The
sync can pull a missing required file from either peer; it fails if neither has
one. Only Endless resumes install the resulting list as the session pool.

Record co-op Campaign only when its starting episode is completed for the first
time. Later episodes, repeats, and deaths do not replace it.

### Data transfer

`net_savexfer.c` uses blocking UDP port 1332 outside live game sessions.

- Single saves and bulk transfers use separate packet families.
- A single save carries its page, online seat, Endless record, and custom
  dependencies; the receiver chooses the slot and name.
- All Saves replaces slots and Endless runs but keeps high scores and custom
  content.
- Custom Levels adds `.clv` files and replaces matching names.
- Custom Data replaces ships and weapons. It also adds custom levels when the
  sender has any.
- Transfer All replaces saves, scores, seats, ships, and weapons as one
  transaction, then adds any custom levels.
- Roll back replaced saves, ships, and weapons if adoption fails. Custom levels
  already added remain installed.
- Large payloads pause every 16 chunks to poll acknowledgements and cancellation.

The single-save `OTSV` payload uses version 3. After its fixed header and
161-byte save record come the Endless bytes, a 16-bit dependency length, and
the colon-separated dependency string. Version 2 ended after the Endless
bytes.

Custom-level transfer uses transfer version 7 and an `OTCL` version 1 payload.
Each record is a 64-byte padded name, a big-endian 32-bit length, and the file
bytes. A bundle holds at most 64 files and 12 MiB of container data. Custom Data
uses its version 2 envelope when no levels exist and version 3 when an `OTCL`
part is present. Transfer All version 2 embeds that Custom Data envelope.

iOS has no multicast entitlement, so direct push must work without discovery.
On platforms with `getifaddrs`, advertise only active broadcast-capable IPv4
interfaces that are neither loopback nor point-to-point.

## Ships, weapons, and item data

### Extra ships

The stock `newsh$.shp` and writable `custom_ships.shp` contain a Sprite2 blob,
ten encrypted 15-byte ship records, and four plaintext checksums. The cipher
and cell codec must round-trip the stock file exactly.

`User.shp` is the DOS editor source. It has 304 presence bytes, followed by one
12x14 cell for each set byte and then the encrypted ship table. Match its name
case-insensitively. Search the writable state directory, active data directory,
the executable's `data` directory, and the executable directory, in that order.

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
equip time because reserved ports differ by player. The reference works even
when the local Weapon Creator setting is off.

### Custom weapons

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
A temporary `poweruse == 0` removes generator cost; extra beams still fire only
when the primary shot succeeds.

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

Logs are created lazily under `log/`. Network reports keep unsymbolized RVAs to
avoid blocking the game loop; crash and hang reports may symbolize. Console log
cleanup removes only recognized OpenTyrian names.

Switch must keep its SDL window resizable for dock changes and save controller
changes immediately. Vita presents at native size, forces 1x supersampling,
keeps presenting during IME use, and uses front touch only.

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

The loader accepts files from 160 bytes through 16 MiB. Level and script
sections must be non-empty and every non-empty section must stay within the
file. The level section's opening 16-bit offset count must be odd and between 3
and 42. An invalid base episode is treated as episode 1.

The base episode selects number-gated engine rules; it does not select stock
data. On activation, the sections are extracted as `custom.lvl`, `custom.lev`,
and `custom.cub` in `custom_levels`. Switching between stock and custom data
forces a reload even when their episode number is the same. A rescan never
changes the active episode.

At startup and before episode selection, loose `.clv` files in the data and user
directories move into `custom_levels`. A same-named file already there wins.
Cross-volume moves fall back to copy-and-delete, and partial copies are removed.
The directory is created only for a write.

Local saves store the active container in `custom_episode`. Custom Endless saves
also store their ordered container names in `custom_collection`, separated by
colons. Valid container names cannot contain a colon, and the field holds at
most 64 names in 4095 bytes.

Offline load rows lock when `custom_episode` or a collection member is missing.
Online rows stay available so the peers can reconcile the files. The advertised
identity is the file name, size, and FNV-1a hash.

`PACKET_CUSTOM_LEVEL` uses the common chunk header. Count values have these
meanings:

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

A zero size and hash means the host lacks that required file. The joiner must
offer a local copy, which the host then requests. For nonzero identities, the
joiner reuses an exact match or downloads the host's copy. Local extras remain
installed but do not enter the session pool.

Container requests may name a manifest entry; an empty name requests the
container advertised in `PACKET_CONNECT`. Received names must remain inside the
container directory. The receiving peer checks size and hash, then runs normal
`.clv` validation before writing the file.

Custom-container sync blocks on the start screen. The guest must consume
`PACKET_DETAILS` before entering that loop so it cannot pin the reliable queue
head. While syncing, `network_clv_pump` drains container, shop, debug, waiting,
and late-connect packets; `PACKET_QUIT` and `PACKET_GAME_QUIT` abort the transfer.

## Tests

Runner details live in [testing/README.md](../testing/README.md). Persistent or
deterministic changes need coverage for:

- old save import and malformed input;
- rollback save, restore, and pointer relocation;
- cross-process hashes;
- host setup and joiner adoption;
- both player indices and host roles;
- loss, reordering, duplication, outages, and sequence wrap.

Small mistakes with large effects:

- Use the correct sprite bank.
- `enemycycle` is one-based.
- Positional enums index shipped data. Do not remove unused-looking entries.
- Keep `config_file.c`, `opl.c`, and midiproc public contracts stable.
