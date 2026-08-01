# OpenTyrian2000 Engaged player guide

This guide covers the features added by this fork. The original Tyrian campaigns
and arcade modes work as before.

## Quick start

| Task | Menu |
| --- | --- |
| Start an Endless run | Main menu > 1 Player Endless |
| Enable high-refresh rendering | Setup > Graphics > Smooth Motion |
| Smooth slow scrolling | Setup > Graphics > Sub-pixel |
| Edit boss or enemy bars | Setup > Enhancements |
| Create a weapon | Setup > Enhancements > Custom Weapon Creator |
| Select MIDI playback | Setup > Sound > Music Synth |
| Toggle the low-armor siren | Setup > Sound > Armor Alarm |
| Toggle the two-player fuse/unfuse sounds | Setup > Sound > Link Sounds |
| Change mouse or touch sensitivity | Setup > Sensitivity |
| Restore episode-specific weapons | Setup > Enhancements > Game Tweaks |
| Wake the dormant dispenser bases | Setup > Enhancements > Game Tweaks |

## Endless mode

Endless mode builds a run from the shipped Tyrian levels. Levels are not edited;
enemy scaling and sector modifiers supply the extra difficulty.

The loop is:

```text
outpost -> choose a course -> clear the zone -> outpost
```

A run has no last zone, however there is a credits roll after clearing Zone 100. Death ends it unless you own a revive.

### Starting a run

The start screen accepts a seed and a Hardcore setting.

- A blank seed generates a random one.
- The same seed and choices reproduce levels, music, courses, shops, and perks.
- Combat randomness is not fixed by the seed.
- Hardcore disables saving. Quitting or dying ends the run.

Difficulty changes starting cash and the rate of depth scaling:

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

Different enemy stats reach their caps at different times. On Normal, the rough
schedule is:

| Stat | Near its cap |
| --- | ---: |
| Enemy shot damage | Zone 55 |
| Elite and champion health | Zone 64 |
| Enemy shot speed | Zone 67 |
| Enemy fire rate | Zone 80 |
| Boss health | Zone 96 |
| Ordinary enemy health | Zone 100 |
| Elite and champion share | Zone 118 |

The rising tide starts near zone 28. It keeps adding shots per volley and raises
shot damage and the special-enemy share after the ordinary ramps flatten.

Player contact damage starts rising near zone 35 and eventually caps at +500%.
Course generation also becomes less generous in deep runs.

### Elites and champions

| Tier | Health | Weapons | Contact | Bounty |
| --- | --- | --- | --- | ---: |
| Normal | Depth-scaled | Normal | Normal | None |
| Elite | 2x to 4x | Normal | +25% | $150 + $40/zone, capped at $2,500 |
| Champion | 2x to 4x | ~1.7x fire rate, +50% damage | +50% | $350 + $90/zone, capped at $6,000 |

Elites and champions are palette-shifted. Linked enemy parts share one tier and
pay one bounty. Bosses can also receive a special tier.

The natural special-enemy share starts at 2%, reaches about 25% near zone 37,
and caps at 80%. Sector modifiers can override it:

- Elite Pack sets the share to 50%.
- Apex makes every enemy elite or champion.
- Legion makes every enemy a champion.
- No Champions downgrades champions to elites.
- No Elites removes both tiers.
- Giant Killer removes their health multiplier.
- Clean Signals removes their offensive bonuses.

### Sector modifiers

Course cards list threats in red and boons in green. Hostile modifiers increase
both danger and payout. Most boons reduce one or both.

`Pays` is a multiple of the base clear reward. For example, `+1.0` adds one base
reward.

#### Threats

| Modifier | Effect | Pays |
| --- | --- | ---: |
| Fortified | More enemy health | +1.0 |
| Frenzy | Faster enemy fire | +1.0 |
| Swift | Faster enemy shots | +0.8 |
| Devastating | More enemy shot damage | +1.0 |
| Enrage | Enemy fire accelerates over time | +1.0 |
| Gravity Well | Pulls the ship down or along a fixed random heading | +0.8 |
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
| Backfire | Kills briefly jam the player's guns | +1.2 |
| Burnout | Backfire plus stacking fire and damage penalties | +1.8 |
| Misfire | Kills stack a player damage penalty | +1.4 |
| Overheat | Kills speed up the guns but damage the hull over time | +1.4 |
| Marked | Strengthens the next boss | - |
| Nitro | Increases player damage, but any hit is fatal | - |
| Dud | Disables superbombs | - |

Enrage is time-based. Retaliation is refreshed by kills.

#### Boons

| Modifier | Effect | Pays |
| --- | --- | ---: |
| Fragile | Less enemy health | -0.5 |
| Bounty | Larger clear reward | +3.0 |
| Cursed | Large reward; next shop is empty | +4.0 |
| Turbodrive | Kills briefly speed up the player's guns | 0 |
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

The score includes sector modifiers and a small level-specific adjustment.

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

The course screen offers two to five routes, ordered from safest to most
dangerous. Equal grades are ordered by payout. Each card shows:

- a generated sector name;
- danger label and grade;
- active threats and boons;
- the exact clear payout.

Radar reveals the underlying level. Surveyor adds routes. Recently played levels
are avoided.

Queued Sabotage charges remove threats from the chosen route. The card updates
before launch, including the lower payout.

### Outpost and E-Shop

The normal shop receives random stock on each visit. Data Cubes becomes the
E-Shop entry; Ship Specs becomes the perk list.

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

Only one kill-fire buff can be bought per recharge period. Its price and strength
use the cash held when the outpost opened, not the balance after shopping.

The gamble has about 40 outcomes. These range from cash, equipment, perks, and
revives to lost cash, stolen equipment, shop tax, or a next-sector curse. It is
high variance and is not required for progression.

### Perks

Perks last for the run and stack up to their listed limit.

Free picks occur after zone 1 and every fourth zone after it. Milestones,
Breakthrough, the E-Shop, and the gamble can add more. Normal picks show three
choices, bought picks show four, and milestone picks show five.

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

Opening Salvo charges after two seconds without main-gun fire. The generator
gauge turns green when ready. Firing starts a one-second window with 2.5x damage
and no generator cost. The window also scales specials. Every zone starts
charged.

Ordnance Reserves increases magazine size without increasing total refill time.
Rapid Recharge shortens the refill itself and also handles charge-type sidekicks.

Every perk screen includes Take the Cash. The buyout grows with depth, offer
count, and total owned perk stacks. Scavenger increases it.

### Economy

```text
base clear reward = $900 + $220 per cleared zone, capped at $60,000
modifier reward   = base multiplied by the sum of modifier rewards
minimum payout    = one quarter of base
```

A small level-specific term separates otherwise equal routes. The course card
shows the final payout.

Interest starts at 10% of unspent cash, capped at `$3,000 + $80/zone`.
Financier raises the rate and cap and discounts the normal equipment shop. It
does not discount the E-Shop.

Elite and champion bounties are paid on kill. Shop prices rise with depth.

### Pickups

| Original pickup | Endless result |
| --- | --- |
| Data cube or secret-level orb | Random safe special weapon |
| Random-special event drop | Front or rear weapon power-up |
| Power-up for a maxed port | Other port, then a 5,000-point gem if both are full |

### Milestones

| Zone | Course list | Reward |
| --- | --- | --- |
| 25, 75, 125, ... | S / S+ | Five-choice perk |
| 50, 150, 250, ... | S+ / S++ | Five-choice perk |
| 100, 200, 300, ... | S++ / S+++, plus The End | Five-choice perk |

The first clear of zone 100 also plays the credits. The run continues.

### Saving and quitting

- Non-Hardcore runs checkpoint at the outpost in `endless.sav`.
- Hardcore runs never save.
- Quit Level restores the launch snapshot and returns to the same committed
  sortie.
- A revive restores full hull, clears enemy shots, and briefly stops enemy fire.
- The furthest reached zone is stored in `opentyrian.cfg`.

With Debug Mode enabled, Endless Effects can apply scaling, modifiers, elites,
and perks to a normal campaign without enabling the Endless run structure.

## Graphics

Settings are under Setup > Graphics.

| Setting | Effect |
| --- | --- |
| Display | Windowed or fullscreen output |
| Scaler | Pixel-art scaling algorithm |
| Scaling Mode | Fit inside the window |
| Smooth Motion | Interpolates the 35 Hz simulation at the display rate |
| Sub-pixel | Renders the playfield internally at Auto, 1x-5x, or Native |
| Filter | Sharp, Smooth, or unfiltered sub-pixel output |
| VSync / FPS Cap / Show FPS | Presentation controls |

Smooth Motion also enables display-rate ship movement in supported single-player
games. Sub-pixel rendering has the largest effect when Smooth Motion is enabled.

Sub-pixel **Native** renders one internal sample per screen pixel, so it follows your
monitor instead of stopping at 5x (11x on a 4K display, shown beside the setting). It
pairs with the **Native** scaler, which already outputs at the exact window size. The
cost scales with your resolution, which is why Auto still stops at 5x.

## Enhancements

Settings are under Setup > Enhancements.

| Setting | Effect |
| --- | --- |
| Debug Mode | Adds debug menus and level selection |
| Extra Parallax | Uses the full horizontal range of background layers |
| Mirrored Layers | Reflects background content beyond a layer edge |
| Extra Sparks | Raises the spark-particle limit |
| Enemy Bars | Shows health for damaged enemies |
| Boss Health Bars | Selects boss-bar style and placement |
| Gauge Gradients | Selects gauge direction and damage flashes |
| Game Tweaks | Opens weapon tweaks and restored cut behaviors |
| Custom Weapons | Enables the custom weapon in shops |
| Custom Weapon Creator | Opens the weapon editor |

### Debug Mode

Debug Mode adds a Debug Menu row to the shop and to the Esc pause menu, plus a
level browser. The menu changes the game as you use it: loadout, cash, cheats,
difficulty, and the expert multipliers.

In a two-player game, local or online, an **Edit Player** row at the top of the
LOADOUT group chooses whose gear the rows below it change. It opens on your own
ship. Player two flies the Dragonwing, so swapping that player's hull changes the
hit box but not the sprite or armour.

Online, the game stays connected while the menu is open, and every change you make
is sent to the other player, so both machines keep playing the same game. Endless
Effects are not available online.

## Online play

Host Game opens a screen with the listen port, **Netcode**, **Desync Recovery**,
**Host Flies**, and Start Hosting. Host Flies picks which ship you take when
hosting: player one, or player two, which is the Dragonwing. Whoever joins gets
the other one. Left and right change it, and it is remembered between sessions.
The host chooses the episode and difficulty either way.

Netcode picks how the two machines stay in step. **Rollback**, the default,
applies your input the moment you press it and quietly corrects the other ship
when its inputs arrive, so the game feels the same as playing offline.
**Delay-Based** is the original lockstep: both ships wait for each other's
inputs, which adds input lag that grows with the connection's ping. The host's
choice binds the session, and it is remembered between sessions.

Desync Recovery, on by default, repairs a game whose two machines have drifted
apart instead of letting the rest of the level play out differently on each.
When a desync is detected, the game pauses for a moment ("Resyncing players."),
the host sends its complete game state to the other player, and both continue
from the host's version of events. The host's setting decides for the session,
like every other rule that affects the simulation. It works in rollback netplay
between two copies of the same build on the same platform — Delay-Based netcode
has no way to detect a desync, so choosing it switches the row off and grays it
out — and gives up after
three repairs in one level; every repair is still recorded in the network log
(`opentyrian_net.log`, next to `opentyrian_log.log` beside the game), so a
recurring desync stays visible to bug reports.

The outpost help bar carries a **Ping** figure at its right end, showing the round
trip to the other player in milliseconds. It updates about every one and a half
seconds and reads `--` until the first reply arrives. Under roughly 85ms the game
runs at full speed on the default network delay of 3; above that it starts to slow
down, and raising the delay trades input lag for smoothness.

Join by IP Address takes a host address alone or with a port, like
`12.345.67.89:1337`. Ctrl+V pastes a copied address into the field, replacing
whatever was already typed there.

At the start of each level the two machines wait for each other before play
begins, so if one side loads levels slower the other briefly shows "Waiting for
other player." and then both fade in together.

## Weapons

The Custom Weapon Creator stores up to 32 weapons. Each weapon has 11 power
levels, an optional rear-gun firing mode, and a live test range. Designs can be
equipped as a front gun, rear gun, or sidekick.

The Levels section can copy and paste one level, copy it to every level, or
build a full level curve automatically. Undo and Redo stay available at the
bottom of every section.

| Creator control | Action |
| --- | --- |
| Up / Down | Move through fields and actions |
| Left / Right | Change a value |
| Shift + Left / Right | Change numeric values in larger steps |
| Click left / right half | Decrease / increase a value |
| Tab / Shift + Tab | Next / previous section |
| Page Up / Page Down | Previous / next power level |
| `[` / `]` | Previous / next bullet segment |
| Type digits | Enter an exact value |
| Ctrl+Z / Ctrl+Y | Undo / redo |
| Ctrl+S | Save now |
| Esc | Cancel a number being entered; otherwise save and return |
| Right-click | Save and return |

Game Tweaks covers:

- superspark trails for Mega Pulse, Wallop Beam, Protron B, and Ice;
- Zica Laser level-11 patterns and beam behaviour;
- Xega Ball and MicroSol Option 5 episode variants;
- Flare and Super Bomb sprites;
- Needle Laser and Bubble Gum-Gun sounds;
- the removed Charge-Laser sidekick;
- Ice Base Shots;
- sidekick autofire.

Ice Base Shots, on by default, wakes the dormant dispenser bases of Camanis
(Episode 3) and the secret Camanis research base (Episode 4). The game data
gives these bases a full hatch open/close animation, but ships them with no way
to ever trigger it. When enabled, the bases open on the same cadence as the
small hatches beside them. At the moment the hatch stands open, the eye fires a
player-aimed shot and the orb below it discharges a fast four-segment lightning
bolt straight down. Endless mode ignores the toggle: up to Zone 50 each zone has
a 50/50 chance of waking them, fixed by the run seed, and from Zone 50 onward
they are always awake.

## Music

| Backend | Requirement |
| --- | --- |
| OPL | None; default |
| FluidSynth | SoundFont file |
| System synth | Windows x86-64 |

MIDI songs loop at their internal loop point. The title-screen Extra menu also
contains a jukebox.

## Console builds

The Switch and Vita builds are unofficial homebrew ports. MIDI is not included.

Netplay works on both. Use Find LAN Games rather than typing an address; where a
field does need typing, the console's own keyboard opens.

- [Nintendo Switch](switch/README.md)
- [PlayStation Vita](vita/README.md)
