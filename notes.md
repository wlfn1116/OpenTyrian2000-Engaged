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

`RENDER_SUPERSAMPLE_NATIVE` (6) instead follows the presented output rectangle -
one internal sample per screen pixel, `RENDER_SUPERSAMPLE_MAX` does not bind it,
and `RENDER_SUPERSAMPLE_LIMIT` is the only ceiling. `display_supersample_factor`
measures against `native_output_size`, the same fitted rect the Native scaler
renders at, so scaler and sub-pixel Native agree. It resolves to 11x on a 4K
screen, so it stays opt-in: Auto must not become a resolution-dependent cost.

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

The debug menu's loadout rows act on the player chosen by its Edit Player row,
which appears only in two-player games and opens on your own ship. Rows the
current mode cannot honour are dropped from the list rather than shown inert, so
selection, scrolling, and hit-testing index the filtered view, never the row table.

Player two flies the Dragonwing in two-player games: its hull id still drives the
hit box, but the sprite and armour come from that fixed role, so a hull swap there
looks like it did nothing.

The campaign level picker and Endless zone editor share row-navigation helpers.
Menu ID 15 is an intentional hole left by the removed level grid; do not reuse or
renumber it.

The Endless editor has two behaviours:

- jump mode stages values and commits only on launch;
- tune mode applies values when leaving the screen.

The editor is reachable from both centred shop UI and full-width in-game UI.
Choose its coordinate width from the active menu offset.

## Networking

Both machines simulate both ships, so anything a menu writes into simulation state
has to reach the other machine or the two sims diverge from the next tick.

A modal panel stops the packet pump the peer's liveness test reads. Any panel that
can stay open during a session needs `NETWORK_KEEP_ALIVE()` in its loop, or the
peer declares the connection dead after `NET_TIME_OUT`.

Debug Mode edits travel as one block, `PACKET_DEBUG_SYNC`: both loadouts, cash,
armour, shield, the cheat flags, difficulty, and the expert tunables. What makes
applying it safe:

- it is sent when the debug menu closes, from inside the rendezvous the menu was
  opened from (the in-game options menu, or the shop), so the peer is never
  simulating when it arrives;
- it is reliable and ordered, so it lands ahead of the `PACKET_WAITING` that
  releases the peer;
- armour and shield ride along instead of being re-derived, because only the
  editing machine knows whether a hull actually swapped;
- a generation counter orders two blocks, and equal generations resolve in the
  host's favour, so simultaneous edits cannot end up swapped.

Rows whose state is not on the wire are dropped from the debug menu in a network
game (currently the Endless effect layer). Skip Level has to go through the request
bit both sims consume on the same frame, not through `reallyEndLevel`.

Adding a field to the block moves the wire offsets; bump `NET_VERSION`.

### Host and player slot

Two separate things, since a host can choose to fly player two (the Dragonwing):

- `network_is_host` — who listens and decides for both machines;
- `networkHostPlayerNum` — which slot that machine flies, so "the host decides"
  never means "player 1 decides". Command-line netplay has no host and leaves it 1,
  which is what it always did.

The joiner has to send its connect packet before the host's arrives, so the slot it
declares there is provisional; the host's own declared number settles it, and the
joiner takes the other. That is also why only command-line games still treat two
equal numbers as a conflict — in a lobby game the two are *expected* to collide, and
the check would reject exactly the case the assignment resolves.

Slot-based rules stay slot-based, and must not be converted: player two is the
Dragonwing (`is_dragonwing`, the docking rules), player one gets the Silver Ship and
the 6px tighter bottom bound.

The wire format does not change, so no `NET_VERSION` bump: an old peer reads the
host's slot from the same field it always did. Only an old *joiner* against a host
that picked player two is broken, and it fails loudly on its own conflict check.

### Reliability layer

Three rules the UDP plumbing rests on, each of which was once broken:

- **A receive error is not a dead link.** Windows fails the next `recv` on a UDP
  socket with `WSAECONNRESET` once an ICMP port-unreachable comes back — routine
  throughout the connect handshake, and immediate when the peer's process dies. It
  is one-shot per ICMP and leaves the datagram queue alone, so `network_check()`
  reports it as a plain "nothing this time" and every wait loop must sleep on `<= 0`
  rather than `== 0`, or it spins a core for the whole stall. `network_init` also
  turns the behaviour off at the socket, which means reading the handle out of
  SDL_net's opaque `UDPsocket`; it is proved to be ours (a datagram socket, on the
  port SDL_net reports binding) before anything is set on it, and skipped otherwise.
- **Queue room is checked before the send, not after.** The other order put the
  datagram on the wire and only then returned without advancing `last_out_sync`, so
  every later packet reused that sequence number and the peer discarded one of each
  pair as a duplicate. A full outbound queue means `NET_PACKET_QUEUE` rendezvous
  packets outstanding with no acknowledgement at all — a dead link, reported as one.
- **Read no field the packet's length does not cover.** `packet_copy` fills only the
  first `len` bytes of a reused `NET_PACKET_SIZE` buffer, so anything past the length
  is the *previous* packet's payload. A short `PACKET_DETAILS` set the episode and
  difficulty from those stale bytes and desynced before the first tick; a short
  `PACKET_CONNECT` took the version, delay and whole settings block the same way.

### Rollback input stream

`PACKET_INPUT` is a 48-byte header plus up to 16 redundant 14-byte input records,
unacknowledged and idempotent. Four invariants hold it together:

- **Drain the socket, don't sample it.** The peer sends one datagram per frame and
  every caller polls at most once per frame, so one datagram per poll runs at exactly
  break-even: any burst leaves a backlog that never clears and reads as permanent
  latency. `network_check()` therefore drains up to `NET_DRAIN_MAX` itself, and
  callers must not wrap it in a loop of their own.
- **The header carries a level epoch.** Frame numbers alone cannot separate a
  stalled peer's leftovers from the previous level, because a short level leaves
  numbers small enough to land inside the new level's acceptance window. Only a
  strictly older epoch is refused — refusing a newer one would turn a one-sided
  level-start skew into a mutual stall.
- **Pause and menu request bits are outside the misprediction test.** They are
  processed from the received truth rather than from what a frame consumed, so an
  unpredicted pulse changes no simulated byte and must not cost a rollback.
- **Received canaries queue.** A canary arrives for a frame our own side has not
  finalised yet; a single slot is overwritten by the next packet before it can ever
  be compared, and the desync check silently never runs.

The in-game menu is a **frame** rendezvous, not just a wall-clock one. It writes
simulation state from outside the tuple stream, so opening it as soon as its frame
is confirmed is not enough: each machine is then at its own prediction depth past
that frame, the frames in between get the change on one machine only, and the
inputs still match so no rollback corrects it. A request on frame f schedules the
menu for `f + NRB_REQ_LEAD` — past the deepest either machine can be when it
notices — and both stall there until the frame is final. Pause is exempt: it writes
nothing the sim reads, and scheduling it would delay the keypress by a third of a
second.

`shipGr`/`shipGrPtr` are in the registry despite looking like render state. They
cache a derivation of `player[].items.ship` that only an explicit `JE_getShipInfo`
refreshes — and that call re-armors, so it cannot serve as a restore fixup.

The Endless effect layer (zone timer, turbodrive decay, gravity carries, damage over
time) is outside the rollback registry by design, so nothing that re-runs a tick may
be armed while it is active — `rollback_selftest_active()` is the gate.

### Desync recovery

On a canary mismatch the host streams its whole registered state (`PACKET_RESYNC`)
and the joiner adopts it; both then run `nrb_reset_core` — the level-start reset —
so the repair is a fresh epoch at frame 1, and the guards that already police level
boundaries discard every in-flight datagram of the abandoned timeline. Rollback
sessions only; the host's toggle binds the session through settings-flag bit 6.

What the design rests on:

- **A snapshot is not wire-safe.** The registry holds raw pointers, each with a
  known set of homes: `enemy[].sprite2s` (six sheet globals), `enemy[].enemydatofs`
  (`&enemyDat[i]`), `shipGrPtr`/`shipGr2ptr`, and the `mapY*Pos`/`BKwrap*` family
  (offsets into `megaData*.mainmap`, where `mapYPos` legally sits one element
  *before* the array — the level-init `- 1`). `rollback_wire_export` rewrites them
  as tags or offsets and then **decodes its own output back and compares against
  live state**; a pointer with an unknown home refuses the whole resync rather than
  ship garbage. A new registered pointer therefore fails loudly, not silently —
  extend `rb_relocs` when adding one.
- **Same-build peers only.** The payload is the registry's native layout, so the
  preamble carries `rollback_state_size()` and the joiner refuses a mismatch. This
  is the honest cross-platform answer: PC–Vita pairs differ in pointer width and
  fall back to today's behaviour.
- **Dead pool slots are canonicalized, on the host, in live state.** A spawn may
  read what a recycled slot left behind, so after adoption both machines must hold
  identical *dead* bytes too, or the first reuse diverges again.
  `rollback_wire_canonicalize` zeroes dead `playerShotData`/`enemyShot`/
  `explosions`/`rep_explosions` slots — which also makes the 400KB-plus snapshot
  collapse to a few tens of KB under the zero-run RLE. `enemy[]` is exempt: a new
  enemy whose sheet is not loaded deliberately inherits its slot's `sprite2s`
  (APPROACH), so dead enemy slots ship as data.
- **The acknowledged channel acks on receipt, not on consumption.** A full window
  against a peer that is not consuming gets acknowledged into a full inbound queue
  and dropped — the one loss the reliability layer cannot see. The sender therefore
  keeps at most half of `NET_PACKET_QUEUE` in flight, the joiner treats a skipped
  chunk index as unrecoverable (ordered channel: the hole can never fill), and a NAK
  answers with a fresh stream. Every wait loop the driver owns dispatches inbound
  chunks (`nrb_stall_pump`), because a desync at the level end arrives while the
  peer sits in exactly such a loop.
- **A NAK after a fully-acked stream means the host already reset.** Its acks came
  home before the joiner's validation failed, so on the final failed attempt no
  shared frame exists any more and the joiner halts the session cleanly instead of
  wedging both machines into the long stall timeout.
- **Recovery is capped (3 per level) and every use is a crashlog entry.** The
  canary exists to surface determinism bugs; recovery converts their cost from "the
  rest of the level is garbage" into a hitch, and must not also convert them into
  silence. A host `PACKET_WAITING`/`PACKET_DETAILS` at the queue head during a
  stream means the joiner already left the level — the packet belongs to the
  level-end machinery and aborts the attempt unconsumed. The receive side follows
  the same rule: a rendezvous release consumed by a dying stream would strand the
  peer at that rendezvous forever.

`PACKET_WAITING` is a strictly paired rendezvous: every use (in-game-menu
release, pause release, shop exit, level start) is passed by both machines in
the same order, one send and one consume each, so the ordered deduplicated
queue can never cross the pairings. Any loop that drains `packet_in` while one
of these rendezvous could be pending must either be between the same pair on
both machines or leave `PACKET_WAITING` unconsumed.

The level-start rendezvous (tyrian2.c, just before `rollback_level_start`)
exists because everything after the shop-exit barrier — map scan, sprite
loads, the HUD-picture fade — is unsynchronized wall-clock work, while the
level fade-in (`levelBrightness`, one step per tick) is sim state. Without it
the faster loader ticked to frame 3, froze nearly black at the driver's
`remote_newest == 0` barrier for the whole load-time difference, and looked
like a stretched fade-in; the frame-3 barrier now only ever holds for about a
round trip and stays as the safety net.

The docked Dragonwing's "did player 2 press a direction" test must come from
the tuple's `RB_MOVE_*` intent bits in rollback netplay, never from comparing
tuple x/y against the local tick-start snapshot. The tuple is recorded before
the dock pin rewrites x/y, and the pin embeds the sender's own — possibly
predicted — copy of player 1, so the position compare reads phantom movement
whenever the carrier moves: with fire held it merely mis-aimed the turret,
without it the link unlinked/relinked every tick (the "stuttery fused pair"
and the "Dragonwing refuses to fuse" reports). The bits carry the dominant
axis only, matching the classic `|dx|>|dy|` turret-target choice, and are
predicted flat like the held buttons. Offline and lockstep keep the classic
position test (their pins agree by construction). `linkAngle` is quantized to
256 steps at capture and only compared by `nrb_wire_differs` when fire is
involved: the sim consumes it solely in the fire-held rotate branch, so an
aim-only stick wiggle must not buy a rollback.

The message bar (`textErase`) is presentation state deliberately outside the
rollback registry, and its per-tick countdown must only run on live passes:
sprite blits are no-ops in silent re-simulation passes, so a 1→0 crossing
landing there swallowed the erase and left stale glyphs that the next
`JE_drawTextWindow` — whose pre-erase used to trust `textErase > 0` — drew
straight over (the garbled-helptext screenshots). The pre-erase is now
unconditional as well, so a poisoned bar self-heals on the next message.

The same silent-pass asymmetry — sprite blits are skipped, plain fills
(`fill_rectangle_xy`, `draw_segmented_gauge`) are not — wipes any HUD element
drawn once at an event rather than per tick. The sidekick HUD boxes were the
case in point: a rollback re-crossing the pickup tick re-ran `JE_drawOptions`,
whose box fill executed while the icon blit was swallowed, leaving black boxes
with live ammo dots on both machines. The painting now lives in
`JE_drawOptionsHUD` (varz.c); `JE_drawOptions` sets `hud_sidekicks_dirty` when
it runs under `rollback_resim_silent`, and the level loop repaints on the first
non-silent pass, before anything is presented. The flag is presentation-only
and stays out of the rollback registry. Any future event-drawn HUD blit needs
the same dirty-flag repaint.

The shield/armor gauges are the fill-side mirror of that bug: they are painted
entirely with fills (`JE_dBar3`), which silent passes do NOT suppress, so every
re-simulation pass that crossed a damage, regen or gauge-flash tick repainted
the persistent HUD surface with rolled-back values — under netplay's frequent
shallow rollbacks the bars (and the white depletion-flash rect) visibly
flickered. `JE_wipeShieldArmorBars` / `JE_drawShield` / `JE_drawArmor` now go
quiet under `rollback_resim_silent` and raise `hud_bars_dirty`; the level loop
repaints via `JE_repaintShieldArmorBars` on the first non-silent pass, and
`rb_restore_from` raises the flag on every snapshot restore so a discarded
timeline that had already painted (e.g. a mispredicted hit) is also settled.
`JE_drawArmor`'s 28-point clamp mutates sim state and therefore still runs on
silent passes, before the gate.

The fuse/unfuse sound cue has the equivalent problem in audio form, plus one of
its own: `soundQueue` slot 4 doubles as the sidekick-fire slot (`soundChannel`
in shots.c), so queueing the cue in the sim lost it whenever a sidekick fired
that tick, and a link discovered only by a rollback correction drained silently.
The cue is therefore played presentation-side: `link_cue_state` (tyrian2.c,
unregistered, reset at level start) tracks the last PRESENTED `twoPlayerLinked`
value, and the level loop plays S_CLINK/S_SPRING on `SFX_CUE_CHANNEL` when it
flips on a non-silent pass. Comparing presented state also prevents double-plays
when a correction replays the same transition. `SFX_CUE_CHANNEL` (loudness.h) is
a ninth mixer channel reserved for presentation cues — channels 0-7 belong to
the queue slots, and `JE_playSampleNum` hardcodes channel 0 (the front gun's),
so neither can be borrowed without cutting a game sound.

### Crash-log diagnostics

Netplay health events — desyncs, stalls, resyncs, livelocks, timeouts, the
offline rollback selftest — are written by `crashlog_note_net` to their own
`opentyrian_net.log` (same report format and rotation as the crash log), so
they cannot bury a real crash report; the crash log keeps only process
failures and recovered would-be-crashes.

Every online session is bracketed in the net log: `network_connect` writes a
`NETWORK SESSION START` line (role, netcode, recovery, delay) the moment the
sync handshake completes, and `network_shutdown` a `SESSION END` line with the
session's desync/stall totals before it wipes `net_diag`. These go through
`crashlog_netlog_line` — header line plus detail only, no context/stack body —
so the brackets stay cheap. The point is falsifiability: a session with no
entries between its brackets was healthy, a missing file means logging never
ran, and neither state is confusable with the other. (Prompted by a real
desync that left an empty-looking log; between startup rotation and a build
predating the net-log split there was no way to tell which had happened.)

On non-Windows (Switch/Vita), `crashlog_note_net` and `crashlog_netlog_line`
are real now, not stubs: reduced entries (timestamped header + detail, no
stack — there is no walker there) appended to `opentyrian_net.log` in
`get_user_directory()`. Append-only deliberately: consoles have no startup
rotation, and truncate-on-first-write would destroy the previous session's
reports on relaunch, exactly when someone finally pulls the SD card to look.
The other crashlog entry points stay no-ops on consoles.

The lockstep detector's once-per-level latch (`tyrian2.c`) compares
`mainLevel`, not `curLoc` — it originally latched on `curLoc`, which advances
with the scroll, so a desynced level re-reported every few ticks (the same
multi-KB-per-tick ballooning the rollback canary's `canary_reported` flag
exists to prevent).

Every report's game-state dump — in either log — ends with a Network section
when `isNetworkGame` is set (`network_write_diagnostics`, called from
`crashlog_state.c`; `nrb_write_diagnostics` appends the rollback block when the
session runs rollback). Each desync/stall/resync entry therefore automatically
carries the session's whole health picture: role and negotiated session
settings, ping, per-channel sync counters and queue occupancy, and the
`net_diag`/`nrb_diag` counters (datagram totals, socket errors, retries, xor
rebuilds, state resends, refused input packets by reason, stall reports,
desynced-level memo).

Counter lifetime is deliberate: `net_diag` resets in `network_shutdown` and
`nrb_diag` in `nrb_set_session_mode`, so both span the *session*, unlike the
per-level `stat_*`/canary counters that `nrb_reset_core` clears — the writer
labels which is which. The desync memo (`network_diag_note_desync`) is fed
once per desynced level by both detection modes (the lockstep once-per-level
report and the rollback canary's first report), so a crash long after the
fact still names when trouble started. Everything the writers touch is a
static in their own TU read without locks — no SDL_net calls — so they are
safe from the fault handler and the watchdog thread.

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

A shot fired with `poweruse` temporarily zeroed is not just free, it is also
exempt from the generator check inside `player_shot_create` - so it keeps firing
on an empty generator. Any such extra beam must additionally be gated on its
primary shot having succeeded (`b < MAX_PWEAPON`), or the weapon half-fires when
starved. The Zica Lv11 Long/Buff beams do this at both fire sites: gameplay in
`JE_mainGamePlayerFunctions` and the Creator's test range in `game_menu.c`.

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
`opentyrian_log.log`. Netplay health events (`crashlog_note_net`) go to a
separate `opentyrian_net.log` in the same format, rotated the same way, so a
lossy session cannot bury a real crash report; only genuine process failures
and recovered would-be-crashes use the crash log.

The watchdog suspends the main thread only long enough to capture its context,
then resumes it before symbol loading and stack walking. Those operations may
need locks held by the suspended thread.

Item-name lookups in a crash path must tolerate unloaded tables and invalid IDs.
The Force Crash target is a volatile file-scope pointer so optimized builds still
perform the faulting write.

During a network game the dump also includes the netcode section — see
"Crash-log diagnostics" under Networking.

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
- keep presenting while the IME dialog is open (SDL_RenderPresent's
  `sceCommonDialogUpdate` is what composites it);
- own the IME dialog natively and never call `SDL_StartTextInput`: SDL's Vita
  backend also tracks the dialog and terminates it from inside `SDL_PollEvent`
  the moment it finishes, so any caller-side status poll or second
  `sceImeDialogTerm` races that teardown -- shared ownership froze the game on
  every lobby-field close;
- terminate the dialog exactly once, on every exit path, so it releases the
  controls;
- a modal that raw-drains SDL events must force the `keydown`/`mousedown` level
  flags back down before returning: the drain eats release edges (above all the
  FINGERUP of the tap that opened it), and `wait_noinput` then spins forever on
  the latched level.

Both ports treat menu touch as absolute tap input and gameplay touch as relative
drag. The right stick is folded into ship movement. MIDI is disabled on both.

### Console netplay

`WITH_NETWORK` is on for both ports. The engine needed no changes for it: every
call site was already guarded, and the SDL_net surface is UDP plus the byte-order
helpers. What the platforms did need:

- **Switch.** `switch-sdl2_net` supplies the library, but libnx leaves the BSD
  socket layer unmounted until `socketInitializeDefault()`, which
  `switch_platform_init` now calls. Without it every `socket()` fails and nothing
  says why.
- **Vita.** VitaSDK has no SDL2_net package *and* no BSD socket wrappers in libc,
  so `vita_net.c` reimplements the subset over SceNet and `network.h` includes it
  instead of `<SDL_net.h>`. SceNet wants a memory pool that outlives the sockets,
  so the stack comes up once and stays up; `SDLNet_Quit` only drops a refcount.
- **Local address.** `SDLNet_GetLocalAddresses` needs a `SIOCGIFCONF` ioctl neither
  console services, so it returns nothing and the lobby had no address to show a
  host. `network_local_addresses()` wraps it and falls back to `console_get_local_ip`
  (nifm on Switch, SceNetCtl on Vita). Discovery degrades on its own: the global
  broadcast still goes out with an empty interface list.
- **Text entry.** Nothing on either console produces `SDL_TEXTINPUT`, so the lobby's
  port/address/name fields would never see a character. They route to
  `console_swkbd` instead, and still run the result through the field's filter --
  the Vita number pad is asked for but can fall back to the full keyboard, and
  neither console restricts the character set.

## Invisible level structures

Player shots may only damage an enemy that `enemy_has_visible_pixel` places inside
the playfield, so a shot leaving the top of the screen cannot kill enemies that
have not scrolled in yet.

Levels also place enemies whose current frame has no art at all, standing in for a
structure the MAP draws: BRAINIAC's walls (enemy 519), FLEET's hulls (698/699),
IXMUCANE's linked boss pieces (107), NOSE DRIP (794), DELIANI (144). A pure pixel
test can never find those on screen, which left them unhittable while they still
rammed the player.

When every gated cell of a frame is blank, the gate falls back to the frame's
nominal 12x14 cell footprint. A frame with any drawn cell is still decided by its
pixels alone. Armour 255 then does the rest: `JE_doSP` sparks, the shot stops,
piercing shots carry on - all stock code, reached again.

Verify blankness against the sheet, not the enemy record: `sprite2_is_blank`
answers for a resolved bank plus index, and a level's banks come from event 5.

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

Both emitters take the rising tide at the same rate: one extra shot buys one
whole extra bolt. Do NOT divide the tide by the four tiles the way the turret
path does for a `multi` weapon - a base rarely lives long enough to bank four
extra shots, so the accounting that is correct for an authored composite
starves this one and the column never thickens. Bolts are still all-or-nothing
(a partial bolt is a broken sprite, not a weaker one), so a pool-space cap
ahead of the loop drops whole bolts rather than truncating one. Each tide bolt
leans off the vertical by the shared `endlessFanPhaseNow` fan; unlike the
turret path, which can only rotate a clone's velocity, this rotates the segment
offsets too, so a leaning bolt stays a straight line along its own travel
instead of shearing.

Assembly geometry (piece 80's `ex/ey` frame): art spans `ex-6..ex+42` by
`ey-35..ey+21`. Both emitters sit on the art's own centre line, assembly x 22
= `ex+16` (2px left of the geometric centre): eye at `ey-27`, and the orb's
white flare across `ey+4..ey+8` (the idle blue orb sits lower, `ey+8..ey+13`).
The bolt's top edge starts at `ey+4`, level with the top of the flare.
Sprites blit from their top-left, so a 12x14 shot centres on an emitter at
`(emitter - 6, emitter - 7)`; vanilla turret shots follow the same rule, which
is why enemy 84 spawns its shot at the enemy's raw `ex/ey`.

Campaign reads the Game Tweaks toggle (`restoreBaseDispensers`); Endless
ignores it and asks `endlessDispenserBaseRoll`: a coin per zone below
`ENDLESS_DISPENSER_ALWAYS_ZONE` (50), always on from that zone up, where they
stop being a surprise and become furniture. The coin is `endlessSplitMixSeed`
salt `depth*2 + 0x70000000` (salts `0x40/0x50/0x60000000` are taken by light
cone, elites, and gravity). It was checked over 800k seed/zone samples: 0.5005
on, per-seed mean 0.5002, all four consecutive-pair combinations at 0.25, and
run lengths matching the geometric distribution - so zones are independent,
with no alternation or stickiness despite consecutive depths differing by a
fixed step into the mixer.

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
