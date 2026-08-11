# OpenTyrian2000 Engaged player guide

This guide covers the features added by Engaged. The original Tyrian campaigns
and controls work as before.

## Menu map

| Task | Menu |
| --- | --- |
| Start Endless | Main menu > 1 Player Endless |
| Play online | Main menu > Online Multiplayer |
| Enable smooth rendering | Setup > Graphics |
| Design a weapon | Setup > Enhancements > Weapons > Weapon Creator |
| Change health bars or gauges | Setup > Enhancements > Heads-Up Display |
| Configure restored content | Setup > Enhancements > Weapons or Gameplay |
| Choose a music backend | Setup > Sound > Music Synth |
| Open Destruct or SuperTyrian | Title screen > Extra |
| Use in-game cheats | Esc > Extra |

## Graphics

The useful settings are under **Setup > Graphics**.

- **Smooth Motion** presents interpolated frames at the display rate. In
  supported single-player modes it also moves the ship at that rate.
- **Sub-pixel** renders the playfield at Auto, Off, 2x through 5x, or Native.
  Supersampled output uses unfiltered nearest-neighbor sampling.
- **Native** follows the fitted output size. It costs more GPU time than the
  fixed modes, especially on high-resolution displays.
- **Sub-pixel FX** extends sub-pixel rendering to the ice, water, and lava
  effects. Turning it off draws the effect, and only the background layers that
  feed it, at native size, which costs much less on those levels at 3x and
  above. Ships, enemies, shots, and the layers drawn over the effect stay
  sub-pixel either way. Console builds default this off.
- The Vita always resolves Sub-pixel to 1x.
- On Vita this setting is named **Smooth FX**. Turning it off updates the
  smoothie background once per 35 Hz simulation tick while foreground movement
  remains display-rate, avoiding repeated feedback filters on the same tick.
- **FPS Cap** accepts Left/Right steps or a typed number. Use 35 or higher for
  online play. A value of 0 means Uncapped.

The simulation still runs at 35 Hz. Demo recording, demo playback, and network
games use fixed-step ship movement even when Smooth Motion is enabled.

## Endless mode

Endless builds an open-ended run from the shipped Tyrian levels. Difficulty
comes from depth scaling, special enemy tiers, and sector modifiers.

The loop is:

```text
outpost -> choose a course -> clear the zone -> outpost
```

Zone 100 rolls the credits and then continues.

### Starting a run

The start screen asks for a seed, run mode, and Base Level rule.

- A blank seed creates a random run.
- The same seed and choices reproduce levels, music, courses, shops, and perks.
- Combat randomness is separate from the run seed.
- **Varied** gives each route its own level. **Same** uses one level for the
  whole course card and varies only the modifiers.

| Mode | Saving | Death |
| --- | --- | --- |
| Relaxed | At outposts | Retry the zone, return to the outpost, or end the run |
| Standard | At outposts | End the run |
| Hardcore | Disabled | End the run |

Standard and Hardcore lock the pause menu after a fatal hit. Relaxed retries
restore the loadout, cash, perks, and shop state from launch.

Starting cash and scaling depend on difficulty:

| Difficulty | Cash | Scaling rate |
| --- | ---: | ---: |
| Easy | $34,000 | 75% |
| Normal | $25,000 | 100% |
| Hard | $18,000 | 120% |
| Impossible | $14,000 | 134% |
| Suicide | $9,000 | 160% |
| Lord of Game | $9,000 | 160% |

Lord of Game also keeps its higher base-game enemy health. Every run starts with
an Atomic RailGun at power 1.

### Depth and enemy tiers

On Normal, most ordinary scaling approaches its cap between zones 55 and 100.
The share of elites and champions keeps rising deeper into the run. A rising
tide begins near zone 28 and adds shots, damage, and special enemies after the
ordinary ramps flatten.

| Tier | Health | Offense | Bounty |
| --- | --- | --- | ---: |
| Normal | Depth-scaled | Normal | None |
| Elite | 2x to 4x | +25% contact damage | $150 + $40/zone, max $2,500 |
| Champion | 2x to 4x | Faster fire, +50% shot and contact damage | $350 + $90/zone, max $6,000 |

Elites and champions are palette-shifted. Linked parts share one tier and one
bounty. Bosses can be promoted.

### Sector modifiers

Course cards show threats in red and boons in green. The payout column below is
a multiple of the base clear reward.

#### Threats

| Modifier | Effect | Payout |
| --- | --- | ---: |
| Fortified | More enemy health | +1.0 |
| Frenzy | Faster enemy fire | +1.0 |
| Swift | Faster enemy shots | +0.8 |
| Devastating | More enemy shot damage | +1.0 |
| Enrage | Enemy fire accelerates over time | +1.0 |
| Gravity Well | Pulls the ship in one direction | +0.8 |
| Elite Pack | 50% special enemies | +2.0 |
| Apex Swarm | All enemies are elite or champion | +4.0 |
| Legion | All enemies are champions | +5.0 |
| Overclock | Faster fire, shots, and scrolling | +1.6 |
| Overload | Stronger Overclock | +3.0 |
| Slipstream | Faster scrolling | +0.6 |
| Warp Speed | Much faster scrolling | +2.0 |
| Light Homing | Weak enemy homing | +0.6 |
| Kamikaze | Moderate enemy homing | +1.2 |
| Rampage | Strong homing and contact damage | +5.0 |
| Topsy Turvy | Flips the playfield and controls | +1.0 |
| Molasses | Slows the player ship | +1.5 |
| No Shield Regen | Disables shield recharge | +1.2 |
| Dead Generator | Disables shield recharge and starves the main gun | +3.0 |
| Martyrdom | Killed enemies fire a radial burst | +1.8 |
| Seeker Rounds | Enemy shots make one delayed correction | +1.4 |
| Static Discharge | Damage drains and stalls the generator | +1.1 |
| Retaliation | Kills briefly speed up enemy fire | +1.5 |
| Backfire | Kills briefly jam your guns | +1.2 |
| Burnout | Backfire with stacking penalties | +1.8 |
| Misfire | Kills stack a player damage penalty | +1.4 |
| Overheat | Kills speed up your guns and damage the hull | +1.4 |
| Marked | Strengthens the next boss | Varies |
| Nitro | More player damage; any hit is fatal | Varies |
| Dud | Disables superbombs | Varies |

#### Boons

| Modifier | Effect | Payout |
| --- | --- | ---: |
| Fragile | Less enemy health | -0.5 |
| Bounty | Larger clear reward | +3.0 |
| Cursed Bounty | Large reward; next shop is empty | +4.0 |
| Turbodrive | Kills briefly speed up your guns | 0 |
| Overcharged | More player damage | 0 |
| Overdrive | Turbodrive plus Overblast | 0 |
| Overblast | Kills stack player damage | 0 |
| Time Dilation | Slower enemy shots | 0 |
| Merchant Favor | Lower prices at the next outpost | 0 |
| No Champions | Demotes champions to elites | 0 |
| No Elites | Removes elites and champions | 0 |
| Aegis Gate | Prevents ready-shield overflow damage | -0.5 |
| Flak Screen | Halves rising-tide shots | -0.5 |
| Auxiliary Reactor | Shield recharge costs no generator power | 0 |
| Low Profile | Shrinks the damage hitbox | -0.8 |
| Giant Killer | Removes tier health multipliers | -0.6 |
| Shockwave | Special-enemy kills clear nearby shots | -0.4 |
| Star Charts | Guarantees a full next course list | 0 |
| Breakthrough | Grants a perk after the sector | -1.0 |
| Soft Landing | Reduces contact damage to 30% | -0.4 |
| Clean Signals | Removes special-enemy weapon bonuses | -0.5 |

#### Danger grades

| Grade | Label | Score |
| --- | --- | ---: |
| F | Calm / Boon | 0 |
| E | Low | 1-9 |
| D | Moderate | 10-13 |
| C | Tough | 14-19 |
| B | High | 20-26 |
| A | Severe | 27-33 |
| S | Deadly | 34-39 |
| S+ | Extreme | 40-49 |
| S++ | Nightmare | 50-59 |
| S+++ | Apocalypse | 60+ |
| END | Finality | The End |

### Chart-a-Course

The course screen offers two to five routes, ordered by danger and then payout.
Each card shows a generated name, modifiers, grade, and exact clear reward.

Radar reveals level names and allows one chart reroll per outpost. Surveyor adds
routes. Sector Sabotage removes threats from the selected route before launch.

The Base Level rule is fixed when the run starts. Varied and Same runs keep
separate records.

### Outpost and E-Shop

The ordinary shop restocks at each visit. Data Cubes opens the E-Shop and Ship
Specs opens the perk list.

| Item | Base cost | Effect |
| --- | ---: | --- |
| Shop Reroll | $6,000 + $1,000/zone | Replace your shop stock |
| Sector Sabotage | $25,000 + $2,500/zone | Remove one threat, up to three per outpost |
| Reinforce | $15,000 + $2,000/zone | Add 6 maximum armour |
| Extra Perk | $70,000 + $2,500/zone | Open a four-choice perk pick |
| Special Weapon | Share of entry cash | Equip a random safe special |
| Turbodrive / Overblast / Overdrive | Share of entry cash | Add the chosen drive next sector |
| Revive | $150,000 + $10,000/zone | Survive one lethal hit |
| Bomb | $2,500 + $400/zone | Add one superbomb |
| Gamble | $25,000 + $2,000/zone | Apply a random good or bad result |

Repeated purchases can cost more. Kill-fire drive prices use the cash held on
entry to the outpost.

### Perks

Free picks appear after zone 1 and every fourth zone after it. Milestones,
Breakthrough, the E-Shop, and the gamble can add more picks.

| Perk | Effect per stack | Max |
| --- | --- | ---: |
| Heavy Rounds | +12% shot damage | 5 |
| Rapid Cyclers | +20% firing cycle | 4 |
| Ablative Plating | +8 maximum armour | 6 |
| Scavenger | +15% clear, bounty, and buyout cash | 4 |
| Nanorepair | Regenerates armour | 3 |
| Siphon | +12% kill repair chance | 3 |
| Bounty Hunter | Doubles elite and champion bounties | 1 |
| Bulwark | -1 damage per hit, minimum 1 | 5 |
| Adrenaline | Faster and stronger fire below one-third armour | 3 |
| Glass Cannon | +40% damage, -8 maximum armour | 1 |
| Rapid Recharge | +25% sidekick refill and special recharge | 4 |
| Autofire Special | Fires a ready special while fire is held | 1 |
| Efficient Coils | -15% main-weapon power use | 5 |
| Shield Matrix | Faster shield recharge | 4 |
| High-Velocity Shots | +25% player shot speed | 3 |
| Radar | Shows level names and grants a chart reroll | 1 |
| Surveyor | +1 course choice | 2 |
| Executioner | More damage to badly wounded enemies | 3 |
| Opening Salvo | Charges a stronger opening volley | 1 |
| Kinetic Converter | Refunds absorbed shield cost | 3 |
| Countermeasures | Clears nearby shots after damage | 2 |
| Chain Reaction | Kills damage nearby enemies | 3 |
| Financier | More interest and lower shop prices | 4 |
| Ordnance Reserves | More sidekick ammo and special duration | 4 |
| Failsafe | Brief invulnerability after hull damage | 2 |

Opening Salvo charges after two seconds without main-gun fire. Its one-second
window gives 2.5x damage and removes generator cost. The generator gauge turns
green when it is ready.

Every perk screen also offers **Take the Cash**. Its value rises with depth,
offer count, and owned perk stacks.

### Economy and milestones

```text
base clear reward = $900 + $220 per cleared zone, capped at $60,000
minimum payout    = one quarter of base
```

Cards include modifier rewards and a small level adjustment. Interest starts at
10% of unspent cash, capped at `$3,000 + $80/zone`.

| Zone | Course grades | Reward |
| --- | --- | --- |
| 25, 75, 125, ... | S / S+ | Five-choice perk |
| 50, 150, 250, ... | S+ / S++ | Five-choice perk |
| 100, 200, 300, ... | S++ / S+++, plus The End | Five-choice perk |

Data cubes and secret-level orbs become random safe specials. A random-special
event becomes a weapon power-up.

### Death and saving

A revive restores the hull, clears enemy shots, and briefly stops enemy fire.
Without one, Relaxed offers Restart Zone, Return to Outpost, or End Run.

Relaxed and Standard checkpoint at the outpost in `endless.sav`. Hardcore never
writes a run save. Quit Level restores the launch snapshot.

Records are split by run mode, difficulty, Base Level rule, and crew size. A
trailing `C` marks a record set while either ship used a custom weapon in a zone.
Record pages are under **High Scores** and can be erased there.

## Arcade tweaks

Open **Setup > Enhancements > Gameplay > Arcade Modes**. All three settings are
off by default; the host controls them online.

### Life Boost

Life Boost scales each arcade ship's shield and armour from its stock values at
one life to 28 at eleven lives. Damage is preserved proportionally when the cap
changes.

It applies to 1 Player Arcade, local 2 Player Arcade, Online Arcade, Timed
Battle, and Super Arcade. SuperTyrian is excluded.

### Random Pickups

Random Pickups rerolls each scripted weapon ball within its own category and
episode arsenal. Power-up balls remain unchanged.

It applies to Arcade and Super Arcade modes. Campaign, Endless, and SuperTyrian
keep their normal pickups.

### Rear Gun Scale

Rear Gun Scale adds lives minus one to the collected rear-gun power. It applies
to 1 Player Arcade, Separate Online Arcade, and Timed Battle.

The linked two-player pair and SuperTyrian are excluded.

## Online play

Open **Online Multiplayer** for Host Game, Find LAN Games, Join by IP Address,
and Your Nickname. The default port is UDP 1333.

LAN discovery works on the same subnet. Direct join addresses are remembered.
The game refuses online play when the FPS cap is below 35.

### Lobby settings

- **Listen Port** sets the host's UDP port.
- **Game Type** selects Arcade, Campaign, Endless, SuperTyrian, Super Arcade,
  or Destruct.
- **Mode** selects Linked, Separate, or Timed Battle for Arcade.
- **Episode** selects the starting campaign episode. Timed Battle replaces it
  with **Level**; Endless replaces it with **Endless Setup**.
- **Difficulty** sets the session difficulty. SuperTyrian replaces it with the
  Standard or Scrollock **Variant**.
- **Host Flies** chooses the host's side in Linked Arcade. Destruct calls this
  row **Host Fights On**.
- **Credit** selects Shared or Individual income in Campaign and Endless.
- **Double Earnings** doubles combat income under Individual credit.
- **Game Speed** applies to both players. Destruct always uses Normal.
- **Netcode** selects Rollback or Delay-Based.
- **Desync Recovery** lets rollback sessions adopt host state after a mismatch.

The host's settings apply to both machines for the session. The joiner's local
settings are restored afterward.

Rollback applies local input immediately and predicts the remote player.
Delay-Based waits for the configured network delay. The outpost shows ping;
raising delay can help a high-latency connection at the cost of input lag.

Online games do not pause. Pressing P or changing window focus leaves the game
running. Use Esc for the in-game menu.

### Game types

**Linked Arcade** uses the Silver Ship and Dragonwing with the split HUD and
docking rules.

**Separate Arcade** gives both players a complete Stalker with their own lives,
weapons, sidekicks, special, score, and HUD.

**Campaign** gives both players complete ships and separate shops. The host's
planet wins when the two players choose different routes.

**SuperTyrian** runs two Stalker 21.126 ships under the SuperTyrian rules.
Standard and Scrollock variants are available.

**Super Arcade** lets each player choose one of the nine ships. The picks may
match, and each ship resolves colored weapon balls against its own arsenal.

The hidden ENGAGE mini-games work in Online Campaign. Both players receive the
mini-game loadout. A cleared TIME WAR continues under SuperTyrian rules; dying or
quitting returns the pair to the previous outpost.

### Online Endless

Online Endless shares the run, course, depth, and sector. Each player owns their
wallet, equipment, shop stock, perk stacks, revive, and purchased drive.

Endless Setup adds:

- **Seed**, **Run Mode**, and **Base Level**, matching solo Endless;
- **Charts Course**, choosing Host, Guest, Alternating, or a seeded 50-50 pick;
- **Combo Feed**, choosing personal or shared kill-fire streaks.

Both players can shop at once. Rerolls and gambles affect the buyer. Sector
Sabotage combines across the pair because both ships fly the same sector.

The charting player chooses the course. The other player may keep shopping or
wait, and can return to the outpost with Esc until departure is committed.

A destroyed ship spectates while its partner continues. It returns at the next
outpost with full hull and no shield. If both ships go down, the run mode decides
the result; in Relaxed, the host chooses for the pair.

### Online Destruct

Destruct puts the players on opposite sides of the artillery game. The lobby
chooses the battle type, side, netcode, and recovery setting.

Both players confirm the title card before the first round. During play:

| Key | Result |
| --- | --- |
| Backspace | Start a new round and keep the scores |
| Esc | End the session for both players |
| P | Disabled online |
| F1, F10, F11 | Disabled online |

Rollback corrections include terrain damage. Desync recovery replaces units,
shots, terrain, and scores with the host's copy. Input held during recovery must
be pressed again.

Destruct sessions are not saved. The battle type and host side are remembered
for the next lobby.

### Online Timed Battle

Timed Battle is an Arcade race with Separate ships. Both players fly a Stalker
and compete for cash until the selected battle level ends.

Both players confirm a title card before the clock starts. The result screen
shows both totals. Timed Battle cannot load a save and does not write to the
single-player Timed Battle boards.

### Saving and resuming

Save from the shop with **Options > Save Game** or Alt+S. Both machines write a
copy, so either player can host the resume.

Linked Arcade saves remain compatible with local two-player play. Separate
Arcade, SuperTyrian, Super Arcade, Campaign, and Endless saves must be loaded
through the same online game type that wrote them.

The automatic `LAST LEVEL` slot is written at level start. After a disconnect,
the game can offer a save based on the outpost before the interrupted level.
Hardcore Endless never offers a save.

To resume, host the matching game type and choose **Load Game** after the joiner
connects. Loading is available only at session start.

### Desync reports

Attach the matching network log from both machines. Each log contains its local
state for the disputed frame.

Network logging is under **Setup > Enhancements > Diagnostics**. Console builds
also provide **Clear Logs**.

## Custom Weapon Creator

Open **Setup > Enhancements > Weapons > Weapon Creator**. The library holds 32
weapons with 11 power levels each. A design can be used as a front gun, rear gun,
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

Levels can be copied, pasted, duplicated across the power curve, or generated as
a curve. The preview uses the real shot simulation.

Online Campaign and Endless support one design per player. Each design is sent
to the other machine before launch.

## Enhancements

**Setup > Enhancements** is split by purpose:

| Menu | Contents |
| --- | --- |
| Visuals | Parallax, mirrored layers, sparks, special tint, shop sprites |
| Heads-Up Display | Enemy bars, boss bars, gauges |
| Weapons | Weapon Creator, Charge-Laser, autofire, trails, episode versions |
| Gameplay | Shot hitboxes, Ice Base Shots, Arcade tweaks |
| Diagnostics | Debug Mode, network logging, console log cleanup |

Episode Versions rows accept Auto, Ep 1-3, or Ep 4+. Auto follows the data for
the current episode. Online sessions use the host's choices.

Useful restored-content settings:

- **Ice Base Shots** enables the dormant dispenser-base attack. Endless rolls it
  from the run seed and always enables it from zone 50.
- **Unused Sprites** assigns distinct shipped icons to weapons that otherwise
  share or lack one.
- **Special Tint** controls the full-screen wash from flare-family specials. It
  is visual and may differ between online players.
- **Shot Hitboxes** selects Classic top-left anchors or Centered sprite anchors.
  The host controls it online.

The special-weapon HUD light is a charge meter. It drains during use, fills
during recharge, and flashes when ready.

### Debug Mode

Debug Mode adds a menu to the shop and in-game menu, plus a level browser. It can
change loadout, cash, cheats, difficulty, and expert multipliers.

Online changes are sent to the peer. Endless Effects and Rollback Self-Test are
disabled online.

Rollback Self-Test restores and replays each tick, then compares the result.
Failures are written to `rollback_selftest.log`.

## Extra menus

**Title screen > Extra** contains the jukebox, Destruct, SuperTyrian, Super
Arcade ships, command-line cheats, and Christmas Mode.

**Esc > Extra** exposes the old cheat key combinations as menu items. These rows
work only in a normal solo game.

## Music

| Backend | Requirement |
| --- | --- |
| OPL3 | None; default |
| FluidSynth | A `.sf2`, `.sf3`, or `.sf` SoundFont |
| Native MIDI | Windows x86-64 |

Choose a backend under **Setup > Sound > Music Synth**. Put a SoundFont beside
the executable or in `data`; FluidSynth is unavailable in the menu until one is
found.

## Console builds

Switch and Vita builds include online play and use the system keyboard for text
fields. MIDI is disabled.

- [Nintendo Switch](switch/README.md)
- [PlayStation Vita](vita/README.md)

## Files and logs

On Windows, files sit beside the executable. Linux uses
`~/.config/opentyrian2000`. Console locations are listed in their build guides.

| File | Contents |
| --- | --- |
| `opentyrian.cfg` | Settings and records |
| `tyrian.sav` | Campaign and two-player saves |
| `endless.sav` | Current Relaxed or Standard Endless run |
| `log/opentyrian_log_<time>.log` | Windows crash report |
| `log/opentyrian_net_<time>.log` | Online session log |

Logs are created on first use and are not rotated automatically.
