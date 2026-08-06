# OpenTyrian2000 Engaged player guide

The original Tyrian campaigns remain available alongside Endless mode, new
graphics settings, online play, the weapon editor, and optional restored
content. This guide explains those additions.

## Where to find things

| I want to | Menu |
| --- | --- |
| Start an Endless run | Main menu > 1 Player Endless |
| Play at my monitor's refresh rate | Setup > Graphics > Smooth Motion |
| Smooth out scrolling and movement | Setup > Graphics > Sub-pixel |
| Play with a friend | Main menu > Online |
| Design a weapon | Setup > Enhancements > Custom Weapon Creator |
| Change boss and enemy health bars | Setup > Enhancements |
| Turn restored content on or off | Setup > Enhancements > Game Tweaks |
| Use a SoundFont instead of OPL | Setup > Sound > Music Synth |
| Change mouse or touch sensitivity | Setup > Sensitivity |
| Reach the jukebox, Destruct, and SuperTyrian | Title screen > Extra |
| Turn on invincibility or a cheat without the key combo | Esc > Extra |

## Graphics settings

Three rows under **Setup > Graphics** are worth setting once.

**Smooth Motion** interpolates the game between simulation ticks so it presents
at your display's refresh rate instead of 35 Hz. In single-player it also moves
your ship at the display rate, which cuts input latency. Leave it on.

**Sub-pixel** renders the playfield internally at Auto, 1x-5x, or Native. It
shows most in background scrolling. Without it, distant layers creep in whole
pixels and visibly step. It only pays off with Smooth Motion on. **Native**
renders one internal sample per screen pixel, following your monitor rather than
stopping at 5x (11x on a 4K display, shown beside the setting). Pair it with the
**Native** scaler, which outputs at the exact window size. Auto stops at 5x
because the cost scales with your resolution.

**Filter** picks how sub-pixel output is resolved: Sharp, Smooth, or none.
The Vita build always resolves Sub-pixel to 1x while keeping Smooth Motion.

On the **FPS Cap** row, Left/Right steps by 5, or type a number directly.
Minimum 5, and 0 or Backspace clears it to Uncapped. Don't set it below 35 if you
plan to play online (see [Online play](#online-play)). On Switch and Vita,
pressing Select on the row opens the system keypad.

## Endless mode

Endless mode builds a run out of the shipped Tyrian levels. The levels
themselves are unedited. The difficulty comes from depth scaling and sector
modifiers.

Every run picks a mode on the start screen: **Relaxed**, **Standard** or
**Hardcore**. The modes range from a free retry of the zone to no saving at all.
See [Starting a run](#starting-a-run).

The loop is:

```text
outpost -> choose a course -> clear the zone -> outpost
```

There is no last zone. Death ends the run unless you own a revive, though a
Relaxed run gets one more choice first. Clearing Zone 100 rolls the credits,
then the run carries on.

### Starting a run

The start screen takes a seed and a run mode.

- A blank seed generates a random one.
- The same seed and choices reproduce levels, music, courses, shops, and perks.
- Combat randomness is not seeded.

| Mode | Saving | Dying |
| --- | --- | --- |
| Relaxed | Anytime | A choice over the wreck: retry the zone, return to the outpost, or end the run |
| Standard | Anytime | Ends the run, and the pause menu is locked from the fatal hit |
| Hardcore | Never | Ends the run, and the pause menu is locked from the fatal hit |

The mode is fixed for the whole run and travels with its save. A fatal hit is
final in both Standard and Hardcore. Neither mode lets you use the pause menu
while the wreck is still exploding. Standard still saves the last level and lets
you choose a different course before launch.

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

Both tiers are palette-shifted. Linked enemy parts share one tier and pay one
bounty. Bosses can be promoted too.

The natural share starts at 2%, reaches about 25% near zone 37, and caps at 80%.
Sector modifiers override it: Elite Pack forces 50%, Apex makes everything elite
or champion, Legion makes everything a champion, No Champions demotes champions
to elites, No Elites removes both, Giant Killer strips their health multiplier,
and Clean Signals strips their weapon bonuses.

### Sector modifiers

Course cards list threats in red and boons in green. Hostile modifiers raise
danger and payout together. Most boons lower one or both.

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
updates before launch, including the lower payout.

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
optional and has high variance.

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

Additional details:

- **Opening Salvo** charges after two seconds without main-gun fire, and the
  generator gauge turns green when it is ready. Firing opens a one-second window
  with 2.5x damage, no generator cost, and scaled specials. Weapons that burst
  into a second wave on impact, such as Mega Pulse and the bomb sidekicks, carry
  the boost into that wave even when it lands after the window closes. Every zone
  starts charged.
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

### Dying

A revive spends itself first: it refills the hull, clears enemy shots, briefly
stops enemy fire, and the zone carries on.

Without one, a Relaxed run puts a choice over the wreck. It stands in for
GAME OVER, so no extra keypress is needed to reach it, and fire, Enter or a click
during the explosion brings it up early. Esc still opens the pause menu instead:

| Choice | Result |
| --- | --- |
| Restart Zone | Fly the same zone again |
| Return to Outpost | Reopen the outpost and pick a new course |
| End Run | On to the summary |

Either retry rolls the run back to the launch snapshot, the way Quit Level does:
the loadout, cash, perks and shop stock you had when the zone started. Nothing
picked up in the failed attempt is kept.

Standard and Hardcore get no such choice: the wreck goes straight to GAME OVER and
the summary. Both also close off the pause menu the moment the ship dies, so its
Quit Level row is no escape from a fatal hit either.

### Saving

Relaxed and Standard runs checkpoint at the outpost, in `endless.sav`. Hardcore
runs never save. Quit Level restores the launch snapshot and drops you back into the
same committed sortie.

Your furthest zone is kept in `opentyrian.cfg`, one record per mode and
difficulty: Zone 40 on Relaxed Easy says nothing about how deep you can fly
Hardcore, so each record only moves under the mode and difficulty that set it.
The Run Over screen shows the one you were playing for, as in
`Furthest zone: 25`, with the mode and difficulty named on its **Mode** row.

The seed screen shows the deepest record for whichever mode is selected, across
all difficulties, since it is picked before you choose one.

All three records also have their own page in **High Scores** on the title
screen, reached by paging right past the episode and Timed Battle boards to the
last two: **2 Player Campaign** and **Endless**. The Endless page lists every run
mode at both crew sizes with the zone it stands at; the 2 Player Campaign page
lists the best combined cash each episode has been finished with online, and who
was flying. That board is separate from the two-player scores on the episode
pages, which belong to Arcade.

Selecting a mode opens its breakdown by difficulty: a row for each of Easy,
Normal, Hard, Impossible, Suicide and Lord of Game, above them an **Any
Difficulty** row. A run sets the record for the difficulty it was started on, and
the **Any Difficulty** row simply shows the deepest of them, which is what the
mode list and the seed screen show.

The breakdown is where records are erased. Move to one with Up/Down and select
it. The page then asks **Are You Sure?** and opens on **No, Keep It**, so
reaching the wipe means deliberately moving down to **Yes, Erase It** and
selecting that. Esc unwinds one step at a time: out of the question, then back to
the mode list, then off the screen. An erased record cannot be recovered.

Erasing **Any Difficulty** erases the deepest record under it, and the row then
shows the next deepest. It only reads `None` once every difficulty below it does.
Records a game already had before the breakdown existed stay on that row, since
there is no way to know which difficulty earned them, and erasing it clears those
the same way.

A record carries a trailing `C`, as in `Furthest zone: 58 C`, when the run that
set it flew a custom weapon, which the Endless high-score page spells out under
the list. It takes one zone with shots fired from the weapon, whether it is
flying as your front gun, rear gun or sidekick, and that zone counts however it
ends: cleared, died in, or left through the pause menu. Designing or previewing a
weapon costs you nothing, since neither happens inside a zone.

The mark belongs to the record, not to you, so it only appears on a record your
run actually set. Beating your own marked record without a custom weapon drops
the `C`.

With Debug Mode on, **Endless Effects** applies the scaling, modifiers, elites
and perks to a normal campaign without the Endless run structure around it.

## Arcade modes

The row of ship icons beside your name counts the lives you have, not the spare
ones: one icon on your last life, two on your second-to-last. It matches the
outpost's **Lives:** row. Past four it collapses to one icon and a number, so
eleven lives reads as a ship and "11".

### Arcade Life Boost

**Setup > Enhancements > Game Tweaks > Arcade Life Boost**, off by default.

Lives buy durability as well as retries, in 1 Player Arcade, 2 Player Arcade,
Online Arcade and on the Super Arcade secret ships. Each ship's shield
and armor ceilings scale off its own life count. At 1 life they are the vanilla
numbers, and at the 11-life maximum both gauges reach a full bar. Growth in
between is even, so six lives puts the Stalker at 22 armor and 19 shield.

| Mode | Ship | Armor at 1 life | Shield at 1 life | Either, at 11 lives |
| --- | --- | --- | --- | --- |
| 1 Player Arcade | Stalker | 15 | 10 | 28 |
| 2 Player (and Online Arcade) | Silver Ship | 10 | 10 | 28 |
| 2 Player (and Online Arcade) | Dragonwing | 10 | 10 | 28 |

Gaining a life raises both ceilings at once and carries damage across
proportionally, so a ship at half armor stays at half armor. Losing one shrinks
them, and the respawn refills both to the new ceiling instead of the vanilla
half. Armor pickups top up to the current ceiling rather than a flat 28, so a
light hull holds less at low life counts.

The Super Arcade ships each scale off their own hull, which runs from 8 to 30
across the nine. All nine carry the stock Gencore High Energy Shield, so their
shields climb 10 to 28 alike. The Nort-Ship Z's 30 hull is already past the bar
and stays there. SuperTyrian is excluded.

Off, every ship keeps its stock hull and shield. Online, the host's setting binds
the session.

The shield gauge's full-charge line is on both gauges of the two-player HUD now.

### Random Pickups

**Setup > Enhancements > Game Tweaks > Random Pickups**, off by default.

Weapon balls are hand-placed in the level scripts, so a level always drops the
same guns in the same order. On, each ball is re-rolled as it spawns, in 1 Player
Arcade, 2 Player Arcade, Online Arcade and on the Super Arcade ships.

A ball only becomes another of its own kind, so a rear-gun drop is still a rear
gun. Each episode rolls from its own arsenal.

| Ball | Rolls into | Ep 1-3 | Ep 4-5 |
| --- | --- | --- | --- |
| Front weapon | another front weapon | 12 | 12 |
| Rear weapon | another rear weapon | 8 | 8 |
| Sidekick | another sidekick | 9 | 9 |
| Special weapon | another special weapon | 6 | 8 |

Purple balls and the front and rear power-up balls are untouched. They grant a
power level, not a weapon. The main game, Endless and SuperTyrian are unaffected.

Super Arcade balls carry a color rather than a weapon, and the color picks a slot
in the current ship's five-gun arsenal. Normally they cycle 1-2-3-4-5. Rolled
instead, a ship can draw the same gun twice running, but never one it cannot fly.

Online, the host's setting binds the session.

## Online play

**Online** on the main menu opens the Multiplayer screen: Host Game, Find LAN
Games, Join by IP Address, and Your Nickname, the name the other player sees.
The game uses UDP port 1333.

The menu refuses to start a netgame while the FPS Cap is below 35: the simulation
runs at 35 Hz, and a lower render cap drags both players down to the capped
machine's rate. Set the cap to 35 or higher, or Uncapped.

**Host Game** covers the listen port, game type, episode or Endless Setup,
difficulty, Host Flies or Credit, Game Speed, Netcode, and Desync Recovery, one
row each with the current value on the right and a line explaining the highlighted
row underneath.
The host's choices bind the session for both machines. While the other player
waits for the host to start, they see the same list. The joiner's own settings
are left alone and restored afterwards.

| Lobby row | What it does |
| --- | --- |
| Game Type | **Arcade** keeps the linked Silver Ship and Dragonwing rules. **Campaign** runs a full episode with two independent, fully equipped ships, cash, and shops. **Endless** runs the Endless roguelite with the same two ships. |
| Episode | Starting episode for a new game. Only episodes installed on the host are offered. Endless always begins at episode 1 and travels on from there, so the row is replaced by Endless Setup. |
| Endless Setup | Endless only. Opens a page with the run seed, the run mode, and who charts each course. |
| Difficulty | Starting campaign or Endless difficulty. Arcade applies its usual two-player difficulty adjustment. |
| Host Flies | Which ship the host takes: the Silver Ship or the Dragonwing. Arcade only, and remembered between sessions; Campaign and Endless give both slots the same kind of ship, so the row is not shown and the host always flies as player one. |
| Credit | Campaign and Endless, in place of Host Flies. **Shared** (default) pays every kill and every score pickup to both players at its full value, so you each end the level with the same earnings and neither has to hang back. **Individual** pays a kill to whoever's shot destroyed the enemy and a pickup to whoever flew into it. In Endless, Individual splits what one player would have earned alone between two wallets, so it is the harder economy on purpose. |
| Game Speed | Session speed for both players. It does not appear in the in-game Esc menu online, so the lobby choice is final. |
| Netcode | **Rollback** (default) applies your input the instant you press it and quietly corrects the other ship when its input arrives. **Delay-Based** is the original lockstep, whose input lag grows with ping. |
| Desync Recovery | On by default. If the two machines drift apart, the game pauses for a moment, the host sends its whole game state over, and both continue from the host's version. Needs rollback netcode and two builds of the same version; it gives up after three repairs in one level. Greyed out unless Netcode is Rollback. |

**Join by IP Address** takes an address on its own or with a port, like
`123.45.67.89:1337`. Ctrl+V pastes over whatever is in the field. It comes back
pre-filled with the last address you used, restarts included, so rejoining the
same host is Join then Enter.

The outpost help bar shows a **Ping** figure at its right end, updated about
every one and a half seconds and reading `--` until the first reply. It is
dropped on rows whose description already reaches that far, rather than being
pushed up against the sentence. Under roughly 85 ms the game runs at full speed
on the default network delay of 3. Above that it starts to slow, and raising the
delay trades input lag for smoothness.

Options in the outpost is the ordinary options page with **Load Game** removed:
loading mid-session would leave the other machine playing something else. Save
Game, the volume and sensitivity sliders, and the joystick, keyboard, and mouse
setup screens all work as usual, and backing out of any of them returns here.

Online Arcade keeps the split two-player sidebar, tags each gauge block **P1**
or **P2**, and dims the other player's gauges. Online Campaign and Online Endless
give each machine the normal one-player sidebar for its local ship. Both player
names and cash totals remain visible along the bottom of the playfield.

In Campaign, each player chooses and powers up a complete ship independently:
front and rear weapons, sidekicks, generator, shield, hull, special, cash, and
weapon mode. The Arcade link, Dragonwing role, shared power rules, and lives do
not apply. Between levels, both players can use their own shop at the same time.
Purchases are sent to the peer as they are committed. Whoever picks a level first
waits at "Waiting for other player." while the other finishes outfitting; nobody
is pulled out of the outpost early. Where the route offers a choice of planets and
the two of you pick different ones, the host's choice is the one you both fly.

While you are waiting, **Esc** takes you back into the outpost to change
equipment or pick a different planet. The waiting screen says so. Once the other
player has picked their level too, the pair of you are committed and Esc no
longer answers; the level loads a moment later.

Custom weapons work in Campaign, one design each. Your design is sent to the
other machine on the way out of the outpost, so both of you fly and see the real
thing. Sending it happens once per changed design and can hold the "Waiting for
other player." screen for a moment on a large weapon.

At the start of each level both machines wait for each other. A slower loader
shows "Waiting for other player." before the two fade in together.

### Online Endless

The Endless lobby flies the roguelite with two ships. One run, one sector, one
zone counter; wallets, stock and gear belong to one player each.

| Endless Setup row | What it does |
| --- | --- |
| Seed | A named seed repeats a run exactly. Leave it blank and the run rolls its own. |
| Run Mode | **Relaxed**, **Standard** or **Hardcore**, exactly as in a solo run. Relaxed opens the death menu when both ships are down, Standard ends the run there, and Hardcore does that and saves nothing. |
| Charts Course | Who picks the next sector: **Host**, **Guest**, **Alternating** (turn about, and the turn is kept in the save), or **50-50** (a coin flip from the run seed, so both machines land on the same side of it). |

Both of you shop at the same outpost at the same time, each with your own stock,
your own prices and your own wallet. Rerolling changes only your own shelves, and
one player's rerolls and gambles never move what the other is dealt. Almost
everything you buy is yours alone: guns and gear, Reinforce hull tiers, a Revive
token, bombs, a Special Weapon, and the kill-fire drives.

Turbodrive, Overblast and Overdrive belong to the ship that paid for them. One of
you can be flying Turbodrive while the other stacks Overblast, each with their own
window and their own combo, and the buyer's ship glows in that drive's colour on
both screens. The kill-fire readout in the corner is your own drive's.

Two buys reach further than the ship that made them: **Sector Sabotage** strips a
danger off the sector you both fly, and **Extra Perk** adds to the run's shared
perk collection. Perks work that way throughout: you each pick from your own slate
and both ships fly under everything either of you took, up to each perk's normal
maximum. Sabotage charges from the two of you add up to the same three-strip cap.

A gamble is yours: the cash, gear and drives it hands out or takes away are the
gambler's. Its rarer outcomes change the next sector instead (a shop discount, a
bulked-up boss, a rush of rammers), and those land on the pair.

While the charting player is on the course list the other sees "Partner is
charting a course." Esc there goes back into the outpost, the same as in
Campaign, and both of you keep shopping until you are ready.

A ship that runs out of hull does not end the zone while its partner is still
flying. It spectates until the zone finishes, and comes back at the next outpost
with a full hull and no shield, keeping everything it owned. A revive token still
fires first, so it is spent before the ship goes down at all. Homing shots,
Seeker corrections and every other danger that picks a target go for the nearer
ship still flying and ignore a downed one. With both ships down the run mode
decides what happens, and in Relaxed the host makes the choice for the pair.

The zone-clear bonus and bank interest are each player's own. The run-over screen
shows what you personally earned and spent.

Two ships reach depths a solo run cannot, so co-op keeps its own records: the
Endless high-score page lists each run mode twice, **1P** and **2P**, and a co-op
run only ever writes the 2P side. Everything else about the page (the breakdown by
difficulty, the custom-weapon mark, erasing a record) works the same on both.

If either of you leaves through the in-game menu the session ends for both, but
the run does not: outside Hardcore it is still in its save from the last outpost,
and the other player goes back to the title rather than to a run-over summary.

### Saving and resuming an online game

Save from the shop with Options > **Save Game**, or Alt+S while no purchase
preview is open. The page's last slot is written automatically at the start of
every level as `LAST LEVEL`; save into a numbered slot to keep a separate run.
Both machines write their own copy after exchanging their latest shop state, so
either player can host the resume.

Arcade saves remain compatible with the regular local two-player page. Campaign
and Endless saves carry both players in full: each ship and its front and rear weapons with
their power levels, both sidekicks, generator, shield, special, rear-gun firing
mode, and cash total, plus the shared episode, difficulty, data cubes, and
next-level position. Nothing either of you bought is dropped. They can only be
loaded through Online Campaign or Online Endless, not through 1 Player or local
2 Player, and each lobby type only offers its own. An Endless save also carries
the run behind it: zone, seed, perks, both players' upgrades and the course
slate. Resuming redraws each player's shop shelves from the seed, so a reroll
bought before saving is not carried over.

If the session ends under you, because the other player quit, the connection
dropped, or an unrecoverable desync stopped the game, you get **Save Game** or
**Don't Save** before the title screen. Save Game opens the 2-player save page
with the run rolled back to the outpost before the current level, the same point
a game over returns to.

Hardcore Endless saves nothing at all, so an online Hardcore run cannot be
resumed.

To resume, host as usual. Once the other player connects, the host picks between
**New Game** and **Load Game**. Load Game opens the load menu fixed to the
2-player page, and the save you pick is sent to the joiner whole, resuming at
that save's next level with its own episode and difficulty. Saves whose episode
the other player's data files lack are dimmed. Loading is host-side and only at
session start. There is no Alt+L during play.

## Custom Weapon Creator

**Setup > Enhancements > Custom Weapon Creator.** Stores up to 32 weapons, each
with 11 power levels, an optional rear-gun firing mode, and a live test range. A
finished design can be equipped as a front gun, rear gun, or sidekick. The
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

In Online Campaign each player brings their own design. The two are kept apart, so
you can both fly a custom weapon in the same session without either of you seeing
the other's in your shop.

## Restored and tweakable content

**Setup > Enhancements > Game Tweaks** holds the differences between the Episode
1-3 and Episode 4-5 weapon data, plus the content the original game shipped but
never used:

- superspark trails for Mega Pulse, Wallop Beam, Protron B, and Ice
- Zica Laser level-11 patterns and beam behaviour
- Xega Ball and MicroSol Option 5 episode variants
- Flare and Super Bomb sprites
- Needle Laser and Bubble Gum-Gun sounds
- the cut Charge-Laser sidekick
- Ice Base Shots
- Unused Sprites
- sidekick autofire

**Ice Base Shots**, on by default, wakes the dormant dispenser bases on Camanis
(Episode 3) and the secret Camanis research base (Episode 4). The game data gives
these bases a full hatch open/close animation but never triggers it. Switched on,
they open on the same cadence as the small hatches beside them. At the moment the
hatch stands open, the eye fires a player-aimed shot and the orb below discharges
a fast four-segment bolt straight down. Endless ignores the toggle. Up to Zone 50
each zone has a 50/50 chance of waking them, fixed by the run seed, and from Zone
50 they are always awake.

**Unused Sprites**, on by default, gives distinct shop icons to items that share
another item's picture. The shop icon sheet carries eleven finished icons the
game never draws, while several weapons reuse the Pulse-Cannon or Multi-Cannon
icon and three had no icon at all. Switched on, twenty-one weapons and sidekicks
take a different icon, among them the NortShip guns, Atomic RailGun, Dragon
Frost, Wobbley, Satellite Marlo, and the Charge-Laser Cannon. A few reuse a
related item's icon so each family reads together: Protron Wave matches Protron
Z, Sonic Impulse matches Sonic Wave, and Tropical Cherry Companion matches
Banana Blast. Most of these weapons are sold only in Endless, where every port
can appear in one list. Switching the option off restores the original icons.
Nothing about the weapons themselves changes.

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
whose gear the rows below it change. In Arcade, player two flies the Dragonwing,
so swapping that player's hull changes the hit box but not the sprite or armour.
In Campaign, both players use the selected full ship. Online, the game stays
connected while the menu is open and every change is sent to the other player.
Endless Effects and the Rollback Self-Test are unavailable online.

The DIAGNOSTICS group holds the inspection tools. **Rollback Self-Test** replays
every tick and compares the result, checking that the snapshot online play rides
on covers everything the game changes. It runs the tick twice, so expect it to be
slower. The row counts verified ticks and failures, details go to
`rollback_selftest.log`, and the setting survives restarts.

## The Extra menus

The title screen and in-game pause menu each have an Extra menu.

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

Drop a SoundFont next to the game executable or in the `data` folder and
FluidSynth picks up the newest one automatically. Without one, FluidSynth is
grayed out in the picker and cannot be selected.

## Console builds

The Switch and Vita builds are unofficial homebrew ports. MIDI is not included.
Netplay works on both. Prefer Find LAN Games, and where a field does need typing,
the console's own keyboard opens.

- [Nintendo Switch](switch/README.md)
- [PlayStation Vita](vita/README.md)

## Files

On Windows these sit next to the executable, on Linux in
`~/.config/opentyrian2000`, and on Switch and Vita in the game's data folder.

| File | Contents |
| --- | --- |
| `opentyrian.cfg` | Settings, high scores, furthest Endless zone per mode |
| `tyrian.sav` | Campaign and 2-player saves |
| `endless.sav` | The current Relaxed or Standard Endless run |
| `log/opentyrian_log_<time>.log` | Crash report, Windows only, written only if the game falls over |
| `log/opentyrian_net_<time>.log` | Online session log |

Logs are created on first use under `log/` and named with the launch time, for
example `opentyrian_net_2026-08-04_143012.log`. They are not rotated or deleted.

For a desync report, attach the matching net log from **both** machines. Each
contains that machine's state for the disputed frame.

Network logging can be disabled under **Setup > Enhancements > Game Tweaks >
Network**. On Switch and Vita, **Clear Logs** removes all saved logs.
