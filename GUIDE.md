# OpenTyrian2000 Engaged player guide

Engaged keeps the original Tyrian campaigns and adds Endless mode, online play,
editors, restored content, and smoother rendering.

## Find a feature

| Task | Menu |
| --- | --- |
| Start Endless | Main menu > 1 Player Endless |
| Play online | Main menu > Online Multiplayer |
| Transfer saves or custom data | Title screen > Extra > Transfer |
| Edit ships | Title screen > Extra > Ship Editor |
| Design a weapon | Setup > Enhancements > Weapons > Weapon Creator |
| Change graphics | Setup > Graphics |
| Choose restored content | Setup > Enhancements |
| Configure network logs | Setup > Diagnostics |
| Open Destruct or SuperTyrian | Title screen > Extra |
| Use solo cheats | Esc > Extra |

## Graphics

Graphics settings are under **Setup > Graphics**.

- **Smooth Motion** presents interpolated frames at the display rate. Supported
  modes also move your ship at that rate.
- **Sub-pixel** renders the playfield at Auto, Off, 2x through 5x, or Native.
  The mouse cursor also moves at this finer precision on every screen.
- **Native** follows the fitted output size and costs the most GPU time.
- **Sub-pixel FX** applies the same treatment to ice, water, and lava. Console
  builds default it off.
- **FPS Cap** accepts a preset or a typed number. Zero means Uncapped. Online
  play requires at least 35.

The simulation remains fixed at 35 Hz. Demos always use fixed-step movement.

Vita resolves Sub-pixel to 1x. Its **Smooth FX** switch controls whether
feedback backgrounds update at display rate or once per simulation tick.

## Endless mode

Endless builds an open run from the shipped levels:

```text
outpost -> chart a course -> clear a zone -> outpost
```

Zone 100 rolls the credits, then the run continues.

### Start a run

The setup screen asks for a seed, run mode, and Base Level rule.

- Leave the seed blank for a random run.
- The same seed and choices reproduce levels, music, courses, shops, and perks.
- Combat randomness is separate from the seed.

| Base Level rule | Course levels |
| --- | --- |
| Varied | Each route draws its own level |
| Varied Shuffle | Each route comes from a shuffled bag of all levels |
| Same | Every route uses one random level |
| Same Shuffle | Every route uses one level from the shuffled bag |

Shuffle rules exhaust the level pool before refilling it. A Radar reroll spends
the discarded hand and deals the next one.

| Run mode | Saving | On death |
| --- | --- | --- |
| Relaxed | At outposts | Retry, return to the outpost, or end the run |
| Standard | At outposts | End the run |
| Hardcore | Disabled | End the run |

Relaxed retries restore the launch loadout, cash, perks, and shop state.

Starting cash depends on difficulty:

| Difficulty | Cash |
| --- | ---: |
| Easy | $34,000 |
| Normal | $25,000 |
| Hard | $18,000 |
| Impossible | $14,000 |
| Suicide | $9,000 |
| Lord of Game | $9,000 |

Every run starts with an Atomic RailGun at power 1.

### Depth and enemy tiers

Enemies gain health and offense as the run deepens. Elite and champion enemies
are palette-shifted; linked parts share one tier and one bounty.

| Tier | Difference |
| --- | --- |
| Elite | More health and contact damage |
| Champion | Elite health, faster fire, stronger shots, larger bounty |

Piercing shots can hit the same target again while overlapping it. Bosses,
elites, and champions have a short repeat-hit lock.

The Mega Cannon, Sonic Impulse, and Needle Laser always pierce. Dragon Frost
and Dragon Flame gain piercing shots at power 9.

### Courses and modifiers

Course cards are ordered by danger and payout. Red modifiers are threats; green
modifiers are boons. The card text describes each effect.

Common modifier families include:

- enemy health, fire rate, shot speed, damage, and homing;
- scroll speed, gravity, flipped controls, and slower ship movement;
- elite and champion population;
- shield, generator, superbomb, and hitbox rules;
- kill-triggered bonuses and penalties;
- next-outpost prices, course choices, and perk rewards.

Only one special-enemy share and one course-correction threat survive on a card;
the strongest wins. The danger grade runs from F through S+++.

Radar reveals level names and grants one reroll per outpost. Surveyor adds
routes. Sector Sabotage removes threats from the chosen route.

Milestone charts appear every 25 zones. Multiples of 100 include **The End**.

### Outpost

The normal shop restocks each visit. **Data Cubes** opens the E-Shop and **Ship
Specs** opens the perk list.

The weapon categories are **Primary Gun** and **Secondary Gun**; both draw from
one pool. Each row is tagged **Front** or **Rear** for the gun's original bay
(**Dual-Mode** replaces **Rear** on two-mode guns in the Secondary list). A gun
works in either bay, though its firing pattern can change.

E-Shop stock may include:

| Item | Use |
| --- | --- |
| Shop Reroll | Replace normal shop stock |
| Sector Sabotage | Remove a threat from the selected course |
| Reinforce | Raise maximum armour |
| Extra Perk | Buy a four-choice perk pick |
| Special Weapon | Equip a random safe special |
| Turbodrive, Overblast, Overdrive | Add a one-sector kill-fire boost |
| Revive | Survive one lethal hit |
| Bomb | Add a superbomb |
| Gamble | Apply a random good or bad result |

Repeated purchases can grow in price. Perk prices rise with owned stacks and
picks bought during the visit.

### Perks

Free perk picks appear after zone 1 and every fourth zone after it. Milestones,
Breakthrough, the E-Shop, and gambles can add picks.

| Perk | Effect |
| --- | --- |
| Heavy Rounds | More shot damage |
| Rapid Cyclers | Faster firing cycle |
| Ablative Plating | More maximum armour |
| Scavenger | More clear, bounty, and buyout cash |
| Nanorepair | Regenerates armour |
| Siphon | Chance to repair on a kill |
| Bounty Hunter | Larger special-enemy and score-pickup rewards |
| Bulwark | Reduces each hit, with a one-point minimum |
| Adrenaline | Faster and stronger fire at low armour |
| Glass Cannon | More damage, less maximum armour |
| Rapid Recharge | Faster sidekick refill and special recharge |
| Autofire Special | Fires a ready special while fire is held |
| Efficient Coils | Lower main-gun power use |
| Shield Matrix | Faster shield recharge |
| High-Velocity Shots | Faster player shots |
| Radar | Reveals levels and grants a chart reroll |
| Surveyor | Adds a course choice; kills can drop superbombs, orbs, and specials |
| Executioner | More damage to badly wounded enemies |
| Opening Salvo | Charges a strong, free opening volley |
| Kinetic Converter | Damage refunds power, recharge, and sidekick charge |
| Countermeasures | Hull hits clear nearby enemy shots |
| Chain Reaction | Kills damage nearby enemies and can cascade |
| Financier | More interest and lower shop prices |
| Ordnance Reserves | More sidekick ammo and special duration |
| Failsafe | Brief invulnerability after hull damage |
| Guidance Package | Adds homing to guns, then sidekicks and specials |
| Twin Pods | Doubles sidekick volleys at double cost |
| Reinforced Prow | Stronger, safer ramming |
| Knife Fight | More damage at close range |
| Deflector | Shielded shots bounce back; later stacks reduce shield loss |

Every perk screen also offers **Take the Cash**.

### Saving and records

Relaxed and Standard checkpoint at the outpost. Hardcore never writes a run
save. **Quit Level** restores the launch snapshot.

An Endless slot appears as **Endless** on Saved Games and `End` in the outpost
load and save lists.

Records are split by run mode, difficulty, Base Level rule, and crew size. A
trailing `C` marks a run that used a custom weapon or edited ship.

## Shop notes

A two-mode gun is tagged **Dual-Mode** in the rear weapon list. The front bay
always uses its first mode. The rear-list preview names the key that cycles the
equipped mode.

## Arcade options

Open **Setup > Enhancements > Gameplay > Arcade Modes**.

- **Life Boost** raises arcade shield and armour with the life count.
- **Random Pickups** rerolls weapon balls within their episode arsenal.
- **Rear Gun Scale** adds lives minus one to collected rear-gun power.

The Engaged preset enables all three. The host controls them online.

## Online play

Open **Online Multiplayer** to host, search the LAN, join an address, or set a
nickname. The default port is UDP 1333.

LAN discovery works on the same subnet. Direct addresses are remembered.
Players need the same game version.

### Lobby

| Setting | Meaning |
| --- | --- |
| Listen Port | Host UDP port |
| Game Type | Arcade, Campaign, Endless, SuperTyrian, Super Arcade, Destruct |
| Mode | Linked, Separate, or Timed Battle for Arcade |
| Episode or Level | Starting campaign episode or Timed Battle level |
| Difficulty | Session difficulty |
| Host Flies | Host side in Linked Arcade or Destruct |
| Credit | Shared or Individual co-op income |
| Double Earnings | Doubles Individual combat income |
| Game Speed | Shared speed; Destruct always uses Normal |
| Netcode | Rollback or Delay-Based |
| Desync Recovery | Lets rollback adopt host state after a mismatch |

The host's session settings temporarily replace the joiner's local choices.

Rollback applies local input immediately and predicts the peer. Delay-Based
buffers both players' inputs and adds input lag.

Online games do not pause. Pressing P or changing window focus leaves play
running.

### Game types

| Type | Rules |
| --- | --- |
| Linked Arcade | Silver Ship and Dragonwing with the split HUD and docking |
| Separate Arcade | Two complete ships with separate equipment and lives |
| Campaign | Two complete ships and separate shops |
| SuperTyrian | Two Stalker 21.126 ships under SuperTyrian rules |
| Super Arcade | Each player chooses one of nine ships |
| Timed Battle | Separate Stalkers race for cash on one level |
| Destruct | Opposing sides of the artillery game |

In Campaign, the host's planet wins when players pick different routes. The
hidden ENGAGE games work online; a cleared TIME WAR continues as SuperTyrian.

### Leaving an outpost

**Start Level** waits for the other player. Esc withdraws while the other player
shops or changes options. Once both players commit, the level loads.

If both players press Esc on the same rollback frame, the host gets the in-game
menu.

### Online Endless

The run, course, depth, and sector are shared. Each player owns their wallet,
equipment, stock, perks, revive, and purchased drive.

Endless Setup adds:

- **Charts Course**: Host, Guest, Alternating, or seeded 50-50;
- **Combo Feed**: personal or shared kill-fire streaks.

Both players may shop at once. Rerolls and gambles affect the buyer. Sabotage
combines because both ships fly the same route.

A destroyed ship spectates until the next outpost. If both ships go down, the
run mode decides the result; the host chooses in Relaxed.

### Appearance

Choose **Customize** at an online outpost.

- **Ship Color** dyes your hull and sidekicks.
- **Partner Opacity** fades the other player's ship or shots on your screen.
- **Partner HP Bars** shows the other player's shield and armour.

Linked Arcade omits partner bars because its shared HUD already shows both
players. Online saves keep both players' appearance choices.

### Saving and resuming

Save at an outpost with **Options > Save Game** or Alt+S. The save is written
only on your machine, so save on both if either player may host the resume.

If the peer is still on the level-end screen, the save waits for them. Esc writes
without their confirmation. The automatic `LAST LEVEL` slot captures the
outpost before a level starts.

Resume by hosting the matching game type and choosing **Load Game** after the
peer connects. Loading is available only at session start. Player numbers and
Linked Arcade sides remain as saved, even if the other machine hosts.

### Desync reports

Enable network logging under **Setup > Diagnostics**. Attach the matching log
from both machines; each contains its local state for the disputed frame.

## Weapon Creator

Open **Setup > Enhancements > Weapons > Weapon Creator**. The library holds 32
weapons with 11 power levels. A design can be equipped as a front gun, rear gun,
or sidekick.

| Control | Action |
| --- | --- |
| Up / Down | Move between rows |
| Left / Right | Change a value |
| Shift + Left / Right | Change by a larger step |
| Tab / Shift+Tab | Next / previous section |
| Page Up / Page Down | Previous / next power level |
| `[` / `]` | Previous / next bullet segment |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+S | Save |
| Esc or right-click | Save and return |

Levels can be copied, pasted, spread across the power curve, or generated as a
curve. The preview uses the real shot simulation.

Online Campaign and Endless exchange one design per player. Separate Arcade
also sends designs referenced by custom ships.

## Enhancements

**Setup > Enhancements** groups settings by purpose:

| Menu | Contents |
| --- | --- |
| Visuals | Parallax, mirrored layers, sparks, tint, shop sprites |
| Heads-Up Display | Enemy bars, boss bars, gauges, touch buttons |
| Weapons | Weapon Creator, Charge-Laser, autofire, spark trails |
| Gameplay | Hitboxes, Guided Aim, Ice Base Shots, Arcade options |
| Episode Versions | Data choices that differ between episode sets |

### Presets

| Preset | Effect |
| --- | --- |
| Vanilla | Closest available match to Tyrian 2000 |
| Engaged | Default set for this fork |
| Custom | Most recent hand-edited set |

Changing an enhancement stores the result as Custom. Sidekick Autofire and
touch button settings stay outside presets.

Episode Versions rows accept Auto, Ep 1-3, or Ep 4+. Auto follows the current
episode. Online sessions use the host's choices.

Useful individual settings:

- **Ice Base Shots** restores the dormant dispenser-base attack.
- **Unused Sprites** gives distinct shipped icons to items that share or lack
  one.
- **Special Tint** controls the full-screen wash from flare specials.
- **Vulnerable Cue** greys invulnerable boss bars and flashes enemies when they
  become damageable.
- **Shot Hitboxes** chooses classic top-left or centered sprite anchors.
- **Guided Aim** makes homing weapons steer toward the drawn enemy position.

The special-weapon HUD light is a charge meter. It drains during use, fills
during recharge, and flashes when ready.

## Diagnostics

**Setup > Diagnostics** holds Debug Mode and network logging. Console builds
also provide **Clear Logs**.

Debug Mode adds a menu to shops and the in-game menu, plus a level browser. It
can change equipment, cash, cheats, difficulty, and Endless effects. Rollback
Self-Test writes failures to `rollback_selftest.log`.

## Extra menu and Ship Editor

**Title screen > Extra** contains Transfer, Ship Editor, Jukebox, Destruct,
SuperTyrian, Super Arcade ships, command-line cheats, and Christmas Mode.

**Esc > Extra** exposes solo cheats and the custom-ship picker. Custom ships are
available in solo Campaign, Endless, and Arcade, plus online Campaign, Endless,
and Separate Arcade.

### Edit ships

The Ship Editor edits ten slots saved in `custom_ships.shp`; slot 10 uses the 0
key. Put `User.shp` beside the game executable or in `data`, then choose
**Import**. Its filename is case-insensitive. **Done** writes `custom_ships.shp`
without changing the shipped `data/newsh$.shp`.

To change ship during play:

- hold Tab and press a number in a solo game; or
- use **Esc > Extra > Custom Ship** with keyboard, controller, or touch.

**Standard** restores the loadout held before the switch. Damage is preserved as
a percentage when maximum armour or shield changes.

The sprite editor stores five banking poses per custom bank. Color 0 is
transparent. Mouse controls use right-click to erase and middle-click to pick a
colour; on a controller, Tab moves focus to or from the canvas.

Online Campaign, Endless, and Separate Arcade exchange each player's custom
ships and any custom weapons they need.

## Music

| Backend | Requirement |
| --- | --- |
| OPL3 | None; default |
| FluidSynth | Windows x86-64 and a SoundFont |
| Native MIDI | Windows x86-64 |

Choose under **Setup > Sound > Music Synth**. Put a `.sf2`, `.sf3`, or `.sf`
SoundFont beside the executable or in `data`.

## Mobile and console builds

- [Nintendo Switch](switch/README.md)
- [PlayStation Vita](vita/README.md)
- [Android](android/README.md)
- [iOS](ios/README.md)

These builds include online play and use the system keyboard for text fields.
MIDI is disabled.

### Touch controls

Drag anywhere to steer; holding a finger also fires the main weapon. **Setup >
Sensitivity** controls tracking distance.

Buttons change with the screen. **Setup > Button Opacity** changes visibility;
zero also removes their touch targets. Ordinary menus accept direct taps.

Optional navigation and sidekick buttons are under **Setup > Enhancements >
Heads-Up Display**. A finger held on a button does not steer or fire the main
weapon.

Lift your finger before dismissing the wreck or GAME OVER screen; both require a
fresh tap.

Switch and Vita use physical controls and draw no touch overlay.

## Transfer data

Open **Extra > Transfer** on the title screen. Both devices need compatible
builds and the same network. Transfers use UDP port 1332.

| Choice | Data copied |
| --- | --- |
| Save | One save; receiver chooses slot and name |
| All Saves | Every save slot and Endless run |
| Custom Ships | Ship Editor output |
| Custom Weapons | Weapon library and enabled state |
| Custom Data | Ships and weapons |
| Transfer All | Saves, scores, ships, and weapons |

Choose the same category on both devices. Select **Upload** on the sender and
**Download** on the receiver. Downloads replace the selected data.

If discovery fails, type the sender's address or choose **Wait for a sender** on
the receiver and push to the address shown. iOS requires this direct path.

Transfer is unavailable during an online session.

## Files and logs

Windows keeps files beside the executable. Linux uses
`~/.config/opentyrian2000`; the macOS app uses
`~/Library/Application Support/OpenTyrian/OpenTyrian2000`. Mobile apps keep
files in private storage that is deleted on uninstall.

| File | Contents |
| --- | --- |
| `opentyrian.cfg` | Settings and records |
| `opentyrian.sav` | Saves, Endless runs, and high scores |
| `custom_weapons.cfg` | Weapon Creator library |
| `custom_ships.shp` | Compiled custom ships |
| `User.shp` | Optional DOS ShipEdit source |
| `log/opentyrian_log_<time>.log` | Windows crash report |
| `log/opentyrian_net_<time>.log` | Online session log |

`opentyrian.cfg` and `opentyrian.sav` are plain text. Edit them only while the
game is closed.

Older builds used `tyrian.sav`, `endless.sav`, and `tyrian.cfg`. The first launch
without `opentyrian.sav` imports them and leaves the old files in place.
