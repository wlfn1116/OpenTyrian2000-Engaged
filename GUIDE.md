# OpenTyrian2000 Engaged — player guide

The original Tyrian campaigns play exactly as they always did. This guide is
about what the fork adds on top: Endless mode, the new graphics settings, online
play, the weapon editor, and the cut content that has been switched back on.

## Where to find things

| I want to | Menu |
| --- | --- |
| Start an Endless run | Main menu > 1 Player Endless |
| Play at my monitor's refresh rate | Setup > Graphics > Smooth Motion |
| Smooth out scrolling | Setup > Graphics > Sub-pixel |
| Play with a friend | Main menu > 2 Player Online Arcade |
| Design a weapon | Setup > Enhancements > Custom Weapon Creator |
| Change boss and enemy health bars | Setup > Enhancements |
| Turn restored content on or off | Setup > Enhancements > Game Tweaks |
| Use a SoundFont instead of OPL | Setup > Sound > Music Synth |
| Change mouse or touch sensitivity | Setup > Sensitivity |
| Reach the jukebox, Destruct, and SuperTyrian | Title screen > Extra |
| Turn on invincibility or a cheat without the key combo | Esc > Extra |

## Graphics settings

Three rows under **Setup > Graphics** are worth setting once, before anything
else.

**Smooth Motion** interpolates the game between simulation ticks so it presents
at your display's refresh rate instead of 35 Hz. In single-player it also moves
your ship at the display rate, which cuts input latency. Leave it on.

**Sub-pixel** renders the playfield internally at Auto, 1x–5x, or Native.
Background scrolling is where it shows the most: without it, distant layers creep in
whole pixels and visibly step. It only pays off with Smooth Motion on. **Native**
renders one internal sample per screen pixel, so it follows your monitor rather
than stopping at 5x (11x on a 4K display, shown beside the setting) — pair it
with the **Native** scaler, which outputs at the exact window size. Auto stops at
5x because the cost scales with your resolution.

**Filter** picks how sub-pixel output is resolved: Sharp, Smooth, or none.

On the **FPS Cap** row, Left/Right steps by 5, or type a number directly.
Minimum 5; 0 or Backspace clears it to Uncapped. Don't set it below 35 if you
plan to play online — see [Online play](#online-play). On Switch and Vita,
pressing Select on the row opens the system keypad.

## Endless mode

Endless mode builds a run out of the shipped Tyrian levels. The levels
themselves are unedited; the difficulty comes from depth scaling and sector
modifiers.

The loop is:

```text
outpost -> choose a course -> clear the zone -> outpost
```

There is no last zone. Death ends the run unless you own a revive. Clearing
Zone 100 rolls the credits, then the run carries on.

### Starting a run

The start screen takes a seed and a Hardcore setting.

- A blank seed generates a random one.
- The same seed and choices reproduce levels, music, courses, shops, and perks.
- Combat randomness is not seeded.
- Hardcore disables saving. Quitting or dying ends the run.

Difficulty sets your starting cash and how fast depth scaling ramps:

| Difficulty | Starting cash | Ramp |
| --- | ---: | ---: |
| Easy | $34,000 | 75% |
| Normal | $25,000 | 100% |
| Hard | $18,000 | 120% |
| Impossible | $14,000 | 134% |
| Suicide | $9,000 | 160% |
| Lord of Game | $9,000 | 160% |

Lord of Game also keeps its higher base-game enemy health. Every run starts with
an Atomic RailGun at power 1.

### Depth scaling

Enemy stats reach their caps at different depths. On Normal, roughly:

| Stat | Near its cap |
| --- | ---: |
| Enemy shot damage | Zone 55 |
| Elite and champion health | Zone 64 |
| Enemy shot speed | Zone 67 |
| Enemy fire rate | Zone 80 |
| Boss health | Zone 96 |
| Ordinary enemy health | Zone 100 |
| Elite and champion share | Zone 118 |

A rising tide starts near zone 28 and keeps adding shots per volley, shot damage
and special-enemy share after the ordinary ramps flatten. Contact damage starts
climbing near zone 35 and caps at +500%. Course generation also turns stingier
in deep runs.

### Elites and champions

| Tier | Health | Weapons | Contact | Bounty |
| --- | --- | --- | --- | ---: |
| Normal | Depth-scaled | Normal | Normal | None |
| Elite | 2x to 4x | Normal | +25% | $150 + $40/zone, capped at $2,500 |
| Champion | 2x to 4x | ~1.7x fire rate, +50% damage | +50% | $350 + $90/zone, capped at $6,000 |

Both tiers are palette-shifted, so you can spot them. Linked enemy parts share
one tier and pay one bounty. Bosses can be promoted too.

The natural share starts at 2%, reaches about 25% near zone 37, and caps at 80%.
Sector modifiers override it: Elite Pack forces 50%, Apex makes everything elite
or champion, Legion makes everything a champion, No Champions demotes champions
to elites, No Elites removes both, Giant Killer strips their health multiplier,
and Clean Signals strips their weapon bonuses.

### Sector modifiers

Course cards list threats in red and boons in green. Hostile modifiers raise
danger and payout together; most boons lower one or both.

`Pays` is a multiple of the base clear reward, so `+1.0` adds one base reward.

#### Threats

| Modifier | Effect | Pays |
| --- | --- | ---: |
| Fortified | More enemy health | +1.0 |
| Frenzy | Faster enemy fire | +1.0 |
| Swift | Faster enemy shots | +0.8 |
| Devastating | More enemy shot damage | +1.0 |
| Enrage | Enemy fire accelerates over time | +1.0 |
| Gravity Well | Pulls the ship down, or along a fixed random heading | +0.8 |
| Elite Pack | 50% special enemies | +2.0 |
| Apex | 100% special enemies | +4.0 |
| Legion | 100% champions | +5.0 |
| Overclock | Faster enemy fire, shots, and scrolling | +1.6 |
| Overload | Stronger Overclock | +3.0 |
| Slipstream | Faster scrolling | +0.6 |
| Warp | Much faster scrolling | +2.0 |
| Light homing | Weak enemy homing | +0.6 |
| Kamikaze | Moderate enemy homing | +1.2 |
| Rampage | Strong homing and more contact damage | +5.0 |
| Topsy Turvy | Flips the playfield and controls | +1.0 |
| Molasses | Slows the player ship | +1.5 |
| Shieldless | Disables shield recharge | +1.2 |
| Dead Generator | Disables shield recharge and starves the main gun | +3.0 |
| Martyrdom | Killed enemies fire a final radial burst | +1.8 |
| Seeker | Enemy shots make one delayed correction | +1.4 |
| Static | Damage drains and briefly disables generator recharge | +1.1 |
| Retaliation | Kills briefly speed up enemy fire | +1.5 |
| Backfire | Kills briefly jam your guns | +1.2 |
| Burnout | Backfire plus stacking fire and damage penalties | +1.8 |
| Misfire | Kills stack a player damage penalty | +1.4 |
| Overheat | Kills speed up your guns but damage the hull over time | +1.4 |
| Marked | Strengthens the next boss | - |
| Nitro | More player damage, but any hit is fatal | - |
| Dud | Disables superbombs | - |

Enrage is on a timer. Retaliation is refreshed by kills.

#### Boons

| Modifier | Effect | Pays |
| --- | --- | ---: |
| Fragile | Less enemy health | -0.5 |
| Bounty | Larger clear reward | +3.0 |
| Cursed | Large reward; next shop is empty | +4.0 |
| Turbodrive | Kills briefly speed up your guns | 0 |
| Overcharge | More player damage | 0 |
| Overdrive | Turbodrive and Overblast | 0 |
| Overblast | Kills stack player damage | 0 |
| Dilation | Slower enemy shots | 0 |
| Merchant Favor | Lower prices at the next outpost | 0 |
| No Champions | Downgrades champions to elites | 0 |
| No Elites | Removes elites and champions | 0 |
| Aegis Gate | Prevents shield overflow damage while ready | -0.5 |
| Flak Screen | Halves extra rising-tide shots | -0.5 |
| Auxiliary Reactor | Shield recharge costs no generator power | 0 |
| Low Profile | Shrinks the damage hitbox by about 25% | -0.8 |
| Giant Killer | Removes elite and champion health multipliers | -0.6 |
| Shockwave | Special-enemy kills clear nearby enemy shots | -0.4 |
| Star Charts | Guarantees a full course list at the next ordinary outpost | 0 |
| Breakthrough | Grants a perk pick after clearing the sector | -1.0 |
| Soft Landing | Reduces contact damage taken to 30% | -0.4 |
| Clean Signals | Removes special-enemy weapon bonuses | -0.5 |

#### Danger ranks

The score covers the sector's modifiers plus a small level-specific adjustment.

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

The course screen offers two to five routes, safest first, with equal grades
ordered by payout. Each card shows a generated sector name, the danger label and
grade, the active threats and boons, and the exact clear payout.

Radar reveals which level is underneath. Surveyor adds routes. Recently played
levels are avoided.

Queued Sabotage charges strip threats from the route you pick, and the card
updates before launch — including the lower payout.

### Outpost and E-Shop

The normal shop restocks randomly on every visit. Data Cubes becomes the E-Shop
entry, and Ship Specs becomes the perk list.

| E-Shop item | Base cost | Result |
| --- | ---: | --- |
| Shop Reroll | $6,000 + $1,000/zone | Replaces shop stock; later rerolls cost more |
| Sector Sabotage | $25,000 + $2,500/zone | Removes one threat from the selected course; up to three |
| Reinforce | $15,000 + $2,000/zone | Adds 6 permanent maximum armour; later tiers cost more |
| Extra Perk | $70,000 + $2,500/zone | Opens a four-choice perk pick; cost doubles and scales with owned perks |
| Special Weapon | Share of entry cash | Equips a random safe special |
| Turbodrive | Share of entry cash | Adds Turbodrive to the next sector |
| Overblast | Share of entry cash | Adds Overblast to the next sector |
| Overdrive | Share of entry cash | Adds both buffs to the next sector |
| Revive | $150,000 + $10,000/zone | Survives one lethal hit; spent revives double the price |
| Bomb | $2,500 + $400/zone | Adds one superbomb; restocks cost more |
| Gamble | $25,000 + $2,000/zone | Applies a random good or bad outcome |

You can buy only one kill-fire buff per recharge period, and its price and
strength use the cash you walked in with, not what is left after shopping.

The gamble has about 40 outcomes, from cash, equipment, perks and revives down to
lost cash, stolen equipment, a shop tax, or a curse on the next sector. It is
high variance and no run needs it.

### Perks

Perks last the whole run and stack up to their listed limit.

Free picks come after zone 1 and every fourth zone after that. Milestones,
Breakthrough, the E-Shop and the gamble can add more. Normal picks offer three
choices, bought picks four, milestone picks five.

| Perk | Per stack | Max |
| --- | --- | ---: |
| Heavy Rounds | +12% shot damage | 5 |
| Rapid Cyclers | +20% firing cycle | 4 |
| Ablative Plating | +8 maximum armour | 6 |
| Scavenger | +15% clear, bounty, and perk-buyout cash | 4 |
| Nanorepair | Regenerates armour; faster with more stacks | 3 |
| Siphon | +12% chance to restore armour on a kill | 3 |
| Bounty Hunter | Doubles elite and champion bounties | 1 |
| Bulwark | -1 damage per hit, minimum 1 | 5 |
| Adrenaline | +45% firing speed and +25% damage below one-third armour | 3 |
| Glass Cannon | +40% damage, -8 maximum armour | 1 |
| Rapid Recharge | +25% sidekick refill and special recharge | 4 |
| Autofire Special | Fires the special while the fire button is held | 1 |
| Efficient Coils | -15% main-weapon power use | 5 |
| Shield Matrix | Reduces the shield-recharge interval by 3 ticks | 4 |
| High-Velocity Shots | +25% player shot speed | 3 |
| Radar | Reveals course level names | 1 |
| Surveyor | +1 course choice | 2 |
| Executioner | +15% damage to badly wounded enemies | 3 |
| Opening Salvo | Charges after 2 seconds of not firing | 1 |
| Kinetic Converter | Refunds 20% of absorbed shield-hit cost | 3 |
| Countermeasures | Clears shots 80px beyond the hull after damage, or 120px at 2 stacks | 2 |
| Chain Reaction | Kills damage nearby enemies | 3 |
| Financier | +5% interest and -8.25% shop prices | 4 |
| Ordnance Reserves | +30% sidekick ammo and special duration | 4 |
| Failsafe | About 0.25 seconds of invulnerability after hull damage | 2 |

A few that need more than one line:

- **Opening Salvo** charges after two seconds without main-gun fire, and the
  generator gauge turns green when it is ready. Firing opens a one-second window
  with 2.5x damage, no generator cost, and scaled specials. Every zone starts
  charged.
- **Ordnance Reserves** grows the magazine without lengthening the total refill.
  **Rapid Recharge** shortens the refill itself and also covers charge-type
  sidekicks.

Every perk screen also offers **Take the Cash**. The buyout grows with depth,
with the number of offers, and with how many perk stacks you already own.
Scavenger raises it.

### Economy

```text
base clear reward = $900 + $220 per cleared zone, capped at $60,000
modifier reward   = base multiplied by the sum of modifier rewards
minimum payout    = one quarter of base
```

A small level-specific term breaks ties between otherwise equal routes. The
course card always shows the final figure.

Interest pays 10% of unspent cash, capped at `$3,000 + $80/zone`. Financier
raises both the rate and the cap and discounts the normal shop, but not the
E-Shop. Elite and champion bounties pay on the kill. Shop prices rise with depth.

### Pickups and milestones

| Original pickup | Endless result |
| --- | --- |
| Data cube or secret-level orb | Random safe special weapon |
| Random-special event drop | Front or rear weapon power-up |
| Power-up for a maxed port | Other port, then a 5,000-point gem if both are full |

| Zone | Course list | Reward |
| --- | --- | --- |
| 25, 75, 125, ... | S / S+ | Five-choice perk |
| 50, 150, 250, ... | S+ / S++ | Five-choice perk |
| 100, 200, 300, ... | S++ / S+++, plus The End | Five-choice perk |

### Saving

Non-Hardcore runs checkpoint at the outpost, in `endless.sav`. Hardcore runs
never save. Quit Level restores the launch snapshot and drops you back into the
same committed sortie. A revive refills the hull, clears enemy shots, and briefly
stops enemy fire. Your furthest zone is kept in `opentyrian.cfg`.

With Debug Mode on, **Endless Effects** applies the scaling, modifiers, elites
and perks to a normal campaign without the Endless run structure around it.

## Arcade modes

The row of ship icons beside your name counts the lives you have, not the spare
ones: one icon on your last life, two on your second-to-last. It matches the
outpost's **Lives:** row. Past four it collapses to one icon and a number, so
eleven lives reads as a ship and "11".

### Arcade Life Boost

**Setup > Enhancements > Game Tweaks > Arcade Life Boost**, on by default.

Lives buy durability as well as retries, in 1 Player Arcade, 2 Player Arcade,
Timed Battle and 2 Player Online Arcade. Each ship's shield and armor ceilings
scale off its own life count: at 1 life they are the vanilla numbers, and at the
11-life maximum both gauges reach a full bar. Growth in between is even, so six
lives puts the Stalker at 22 armor and 19 shield.

| Mode | Ship | Armor at 1 life | Shield at 1 life | Either, at 11 lives |
| --- | --- | --- | --- | --- |
| 1 Player Arcade | Stalker | 15 | 10 | 28 |
| 2 Player (and Online) | Silver Ship | 10 | 10 | 28 |
| 2 Player (and Online) | Dragonwing | 10 | 10 | 28 |

Gaining a life mid-level raises both ceilings immediately, and damage already
taken carries across proportionally — a ship at half armor stays at half armor,
so the extra hull is a bigger bar, not a free repair. Losing a life shrinks the
ceilings before the respawn refill. Armor pickups top up to the current ceiling
rather than a flat 28, so a light hull holds less at low life counts than it used
to.

Switch it off and every arcade ship keeps the hull and shield it always had.
Online, the host's setting binds the session, like every other rule that reaches
the simulation.

The shield gauge carries a thin line marking a full charge. It is on both gauges
of the two-player HUD now, and it vanishes while the gauge is at its ceiling — a
quick way to see the shield is topped up.

## Online play

**2 Player Online Arcade** on the main menu opens the Multiplayer screen: Host
Game, Find LAN Games, Join by IP Address, and Your Nickname, which is the name
the other player sees. The game uses UDP port 1333. On the same network, Find LAN
Games saves anyone typing an address.

The menu refuses to start a netgame while the FPS Cap is below 35, and says so
instead of showing its options. The simulation runs at 35 Hz, so a lower render
cap would drag both players down to the capped machine's rate. Set the cap to 35
or higher, or Uncapped.

**Host Game** covers the listen port, Netcode, Desync Recovery, Host Flies, and
Game Speed. The host's choices bind the session for both machines; the joiner's
own settings are left alone and restored afterwards.

| Lobby row | What it does |
| --- | --- |
| Netcode | **Rollback** (default) applies your input the instant you press it and quietly corrects the other ship when its input arrives. **Delay-Based** is the original lockstep, whose input lag grows with ping. |
| Desync Recovery | On by default. If the two machines drift apart, the game pauses for a moment, the host sends its whole game state over, and both continue from the host's version. Needs rollback netcode and two builds of the same version; it gives up after three repairs in one level. |
| Host Flies | Which ship you take: player one, or player two (the Dragonwing). Remembered between sessions. |
| Game Speed | Session speed for both players. It does not appear in the in-game Esc menu online, so the lobby choice is final. |

**Join by IP Address** takes an address on its own or with a port, like
`12.345.67.89:1337`. Ctrl+V pastes over whatever is in the field. It comes back
pre-filled with the last address you used, restarts included, so rejoining the
same host is Join then Enter.

The outpost help bar shows a **Ping** figure at its right end, updated about
every one and a half seconds and reading `--` until the first reply. Under
roughly 85ms the game runs at full speed on the default network delay of 3; above
that it starts to slow, and raising the delay trades input lag for smoothness.

In any two-player game the sidebar tags each gauge block **P1** or **P2**.
Online, the other player's gauges are dimmed as well.

At the start of each level both machines wait for each other, so a slower loader
briefly shows "Waiting for other player." before the two fade in together.

### Saving and resuming an online game

Save from the shop with Options > **Save Game**, or Alt+S anywhere in the shop.
Online games share the regular 2-player save page, so a session saved online can
be continued on the couch and vice versa. The page's last slot is written
automatically at the start of every level as `LAST LEVEL` — handy if nobody saved
by hand, but it means an online session overwrites that slot's local 2-player
backup. Save into a numbered slot to keep a run. Both machines write their own
copy of the same session, so either player can host the resume.

If the session ends under you — the other player quits, the connection drops, or
an unrecoverable desync stops the game — you are offered **Save Game** or
**Don't Save** before the title screen. Save Game opens the 2-player save page
with the run rolled back to the outpost before the current level, the same point
a game over returns to.

To resume, host as usual. Once the other player connects, the host picks between
**New Game** and **Load Game**. Load Game opens the load menu fixed to the
2-player page, and the save you pick is sent to the joiner whole, resuming at
that save's next level with its own episode and difficulty. Saves whose episode
the other player's data files lack are dimmed. Loading is host-side and only at
session start; there is no Alt+L during play, because a mid-session load on one
machine would fork the game.

## Custom Weapon Creator

**Setup > Enhancements > Custom Weapon Creator.** Stores up to 32 weapons, each
with 11 power levels, an optional rear-gun firing mode, and a live test range. A
finished design can be equipped as a front gun, rear gun, or sidekick — the
**Custom Weapons** row puts it in the shops.

The Levels section can copy and paste a single level, copy one level to all of
them, or generate a full level curve. Undo and Redo sit at the bottom of every
section.

| Control | Action |
| --- | --- |
| Up / Down | Move through fields and actions |
| Left / Right | Change a value |
| Shift + Left / Right | Larger steps |
| Click left / right half | Decrease / increase |
| Tab / Shift + Tab | Next / previous section |
| Page Up / Page Down | Previous / next power level |
| `[` / `]` | Previous / next bullet segment |
| Type digits | Enter an exact value |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+S | Save now |
| Esc | Cancel a number being entered, otherwise save and return |
| Right-click | Save and return |

## Restored and tweakable content

**Setup > Enhancements > Game Tweaks** holds the differences between the Episode
1-3 and Episode 4-5 weapon data, plus the content the original game shipped but
never used:

- superspark trails for Mega Pulse, Wallop Beam, Protron B, and Ice;
- Zica Laser level-11 patterns and beam behaviour;
- Xega Ball and MicroSol Option 5 episode variants;
- Flare and Super Bomb sprites;
- Needle Laser and Bubble Gum-Gun sounds;
- the cut Charge-Laser sidekick;
- Ice Base Shots;
- sidekick autofire.

**Ice Base Shots**, on by default, wakes the dormant dispenser bases on Camanis
(Episode 3) and the secret Camanis research base (Episode 4). The game data gives
these bases a full hatch open/close animation but never triggers it. Switched on,
they open on the same cadence as the small hatches beside them; at the moment the
hatch stands open, the eye fires a player-aimed shot and the orb below discharges
a fast four-segment bolt straight down. Endless ignores the toggle: up to Zone 50
each zone has a 50/50 chance of waking them, fixed by the run seed, and from Zone
50 they are always awake.

## Other enhancements

| Setting | Effect |
| --- | --- |
| Debug Mode | Adds the debug menu and level selection |
| Extra Parallax | Uses the full horizontal range of the background layers |
| Mirrored Layers | Continues background content past a layer's edge as a mirror |
| Extra Sparks | Raises the spark-particle limit |
| Enemy Bars | Health bars on enemies you have damaged |
| Boss Health Bars | Boss-bar style and placement |
| Gauge Gradients | Gauge direction and damage flashes |

Under **Setup > Sound**, *Armor Alarm* is the low-armor siren and *Link Sounds*
is the cue for two ships fusing or unfusing. **Setup > Sensitivity** covers the
mouse on desktop and touch on consoles.

### Debug Mode

Debug Mode adds a Debug Menu row to the shop and the Esc pause menu, plus a level
browser. The menu changes the game live: loadout, cash, cheats, difficulty, and
the expert multipliers.

In a two-player game an **Edit Player** row at the top of the LOADOUT group picks
whose gear the rows below it change. Player two flies the Dragonwing, so swapping
that player's hull changes the hit box but not the sprite or armour. Online, the
game stays connected while the menu is open and every change is sent to the other
player. Endless Effects and the Rollback Self-Test are unavailable online.

The DIAGNOSTICS group holds the inspection tools. **Rollback Self-Test** replays
every tick and compares the result, checking that the snapshot online play rides
on covers everything the game changes. It runs the tick twice, so expect it to be
slower; the row counts verified ticks and failures, details go to
`rollback_selftest.log`, and the setting survives restarts.

## The Extra menus

Two menus named Extra, in two places.

**Title screen > Extra** collects the things that used to need a code typed at
the title: the jukebox, the Destruct mini-game, SuperTyrian, the secret Super
Arcade ships, the command-line cheat options, and Christmas Mode.

**Esc > Extra**, during a game, is the DOS cheat combos made clickable. It has an
Invincibility toggle, a Cheat Codes page (Nort Ship, Self-Destruct, Skip Level)
and a Debug Codes page (debug overlay, hyper-speed, level filter, random music,
screenshot pause). Each row shows its original key combo in the footer, so you
can still press them by hand. The cheat rows only work in a normal solo game.

## Music

| Backend | Requirement |
| --- | --- |
| OPL | None; default |
| FluidSynth | A SoundFont file (`.sf2`, `.sf3`, `.sf`) |
| System synth | Windows x86-64 |

Pick one on **Setup > Sound > Music Synth**. MIDI songs loop at their internal
loop point.

## Console builds

The Switch and Vita builds are unofficial homebrew ports. MIDI is not included.
Netplay works on both; prefer Find LAN Games, and where a field does need typing,
the console's own keyboard opens.

- [Nintendo Switch](switch/README.md)
- [PlayStation Vita](vita/README.md)

## Files

On Windows these sit next to the executable, on Linux in
`~/.config/opentyrian2000`, and on Switch and Vita in the game's data folder.

| File | Contents |
| --- | --- |
| `opentyrian.cfg` | Settings, high scores, furthest Endless zone |
| `tyrian.sav` | Campaign and 2-player saves |
| `endless.sav` | The current non-Hardcore Endless run |
| `opentyrian_log.log` | Crash report, Windows only, written only if the game falls over |
| `opentyrian_net.log` | Online session log |

Both logs rotate per launch, so the live file always belongs to the run you have
open now; earlier ones are numbered `.1.log` upwards beside it.

If you hit a desync, attach `opentyrian_net.log` from **both** machines to the
report. Each side logs the disputed frame as it computed it, and comparing the
two points straight at what diverged. A session with nothing between its start
and end lines was healthy.

The net log can be turned off, and cleared on consoles, under **Setup >
Enhancements > Game Tweaks > Network**.
